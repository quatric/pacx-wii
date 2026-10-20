#!/usr/bin/env python3
"""
Builds the Wii data from your own copy of Pac the Man X 1.5.x.

usage: python3 -I tools/convert_assets.py "<path>/Pac the Man X.app/Contents/Resources"

writes
  data/pack.bin            textures (GX tiled), sounds, level sets and strings, linked into the .dol
  apps/pacx/music/*.ogg    the soundtrack, streamed from the SD card
  apps/pacx/icon.png       Homebrew Channel icon
  apps/pacx/levels/        the extra level set, editable with the built-in editor

Nothing from the game is stored in this repository; run this once before `make`.
The player and enemy sprites are not taken from the game: they are drawn by
tools/original_sprites.py.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from PIL import Image  # noqa: E402
import original_sprites as orig  # noqa: E402

ROOT = os.path.dirname(HERE)

FMT_RGBA8, FMT_RGB565 = 0, 1
T_TEX, T_SND, T_DATA = 1, 2, 3


# ------------------------------------------------------------------------------ images

def load_raw(path):
    with open(path, "rb") as f:
        b = f.read()
    w, h, bpp = struct.unpack(">III", b[:12])
    px = b[12:]
    if bpp == 32:
        return Image.frombytes("RGBA", (w, h), px[: w * h * 4])
    if bpp == 24:
        return Image.frombytes("RGB", (w, h), px[: w * h * 3]).convert("RGBA")
    raise ValueError(f"{path}: unsupported bpp {bpp}")


def padded(img):
    w, h = img.size
    pw, ph = (w + 3) & ~3, (h + 3) & ~3
    if (pw, ph) != (w, h):
        c = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
        c.paste(img, (0, 0))
        img = c
    return img, pw, ph


def tile_rgba8(img):
    img, pw, ph = padded(img.convert("RGBA"))
    src = img.tobytes()
    out = bytearray()
    stride = pw * 4
    for ty in range(0, ph, 4):
        for tx in range(0, pw, 4):
            ar = bytearray()
            gb = bytearray()
            for y in range(4):
                o = (ty + y) * stride + tx * 4
                row = src[o:o + 16]
                for x in range(4):
                    r, g, b, a = row[x * 4:x * 4 + 4]
                    ar += bytes((a, r))
                    gb += bytes((g, b))
            out += ar + gb
    return bytes(out), pw, ph


def tile_rgb565(img):
    img, pw, ph = padded(img.convert("RGBA"))
    src = img.convert("RGB").tobytes()
    out = bytearray()
    stride = pw * 3
    for ty in range(0, ph, 4):
        for tx in range(0, pw, 4):
            for y in range(4):
                o = (ty + y) * stride + tx * 3
                for x in range(4):
                    r, g, b = src[o + x * 3:o + x * 3 + 3]
                    out += struct.pack(">H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return bytes(out), pw, ph


# ------------------------------------------------------------------------------ pack

class Pack:
    def __init__(self):
        self.entries = []

    def tex(self, name, img, fmt=FMT_RGBA8):
        w, h = img.size
        data, pw, ph = tile_rgba8(img) if fmt == FMT_RGBA8 else tile_rgb565(img)
        self.entries.append((name, T_TEX, fmt, w, h, pw, ph, 0, data))

    def snd(self, name, pcm_be, rate):
        self.entries.append((name, T_SND, 0, 0, 0, 0, 0, rate, pcm_be))

    def data(self, name, b):
        self.entries.append((name, T_DATA, 0, 0, 0, 0, 0, 0, b))

    def write(self, path):
        head = struct.pack(">4sIII", b"PXPK", len(self.entries), 0, 0)
        table_size = len(self.entries) * 64
        off = (len(head) + table_size + 31) & ~31
        table = b""
        blobs = b""
        for name, typ, fmt, w, h, pw, ph, rate, data in self.entries:
            nb = name.encode("utf-8")
            assert len(nb) < 40, name
            pos = off + len(blobs)
            table += struct.pack(">40sBBHHHHHHII2x", nb, typ, fmt, w, h, pw, ph, rate, 0, pos, len(data))
            blobs += data
            blobs += b"\0" * (((len(blobs) + 31) & ~31) - len(blobs))
        body = head + table
        body += b"\0" * (off - len(body))
        with open(path, "wb") as f:
            f.write(body + blobs)


# ------------------------------------------------------------------------------ patches

def patch_title(title):
    """The title picture has three dim icons under the first three buttons that show the
    original characters; replace them with dim versions of the new button art."""
    img = title.copy()
    px = img.load()
    buttons = [(103, 374), (174, 384), (254, 389)]
    for k, (bx, by) in enumerate(buttons):
        # the third icon (both players) is wider than its socket
        cx, cy, r = bx + 24, by + 67, 18 if k < 2 else 23
        # rebuild the background inside the circle from the rows just above and below it
        for x in range(cx - r, cx + r + 1):
            top = px[x, cy - r - 1]
            bot = px[x, min(cy + r + 1, img.height - 1)]
            for y in range(cy - r, cy + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    t = (y - (cy - r - 1)) / float(2 * r + 2)
                    px[x, y] = tuple(int(top[i] * (1 - t) + bot[i] * t) for i in range(3)) + (255,)
        icon = orig.button_icon(k).resize((34, 34), Image.LANCZOS)
        a = icon.split()[3].point(lambda v: int(v * 0.28))
        icon.putalpha(a)
        img.alpha_composite(icon, (cx - 17, cy - 17))
    return img


def patch_ending(ending):
    """The ending picture shows the original cast in one row; draw the new cast there."""
    img = ending.copy()
    px = img.load()
    x0, y0, x1, y1 = 176, 182, 466, 242
    samples = [px[x, y0 - 2] for x in range(x0, x1, 7)] + [px[x, y1 + 2] for x in range(x0, x1, 7)]
    samples.sort(key=lambda c: sum(c[:3]))
    bg = samples[len(samples) // 2]
    for y in range(y0, y1):
        for x in range(x0, x1):
            px[x, y] = bg
    row = orig.ending_row()
    img.alpha_composite(row, (320 - row.width // 2, 188))
    return img


def patch_buttons(buttons):
    img = buttons.copy()
    for k in range(3):
        img.paste(Image.new("RGBA", (48, 48), (0, 0, 0, 0)), (k * 48, 0))
        img.alpha_composite(orig.button_icon(k), (k * 48, 0))
    return img


# ------------------------------------------------------------------------------ text

def read_strings(path):
    raw = open(path, "rb").read()
    for enc in ("utf-16", "utf-8"):
        try:
            text = raw.decode(enc)
            break
        except UnicodeError:
            continue
    out = {}
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)"\s*;', text):
        k = m.group(1)
        v = m.group(2).replace('\\"', '"').replace("\\n", " ")
        out[k] = v
    return out


# the help lines that name the original characters get neutral wording
HELP_NAMES = {
    "en": ("This is you - Player 1", "This is your friend - Player 2"),
    "de": ("Das bist du - Spieler 1", "Dein Freund - Spieler 2"),
    "fr": ("C'est toi - Joueur 1", "Ton ami - Joueur 2"),
}


# ------------------------------------------------------------------------------ main

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    res = os.path.abspath(sys.argv[1])
    spr = os.path.join(res, "Sprites")
    if not os.path.isdir(spr):
        sys.exit(f"no Sprites folder in {res}")
    editor = os.path.join(res, "Pac the Man X Editor.app", "Contents", "Resources")

    pack = Pack()

    opaque = {"title", "ending", "highscore_title"}
    from_game = ["arrow", "pellet", "barrier", "game_over", "heart", "ready", "pause", "spot",
                 "help", "the_end", "highscore_title", "extra", "super_pellet", "die_part2", "pop",
                 "tile", "tile2", "points", "font", "font_small",
                 "citadel1", "citadel2", "citadel3", "citadel4"]
    for name in from_game:
        img = load_raw(os.path.join(spr, name + ".raw"))
        pack.tex(name, img, FMT_RGB565 if name in opaque else FMT_RGBA8)
        print("sprite", name, img.size)

    pack.tex("title", patch_title(load_raw(os.path.join(spr, "title.raw"))), FMT_RGB565)
    pack.tex("ending", patch_ending(load_raw(os.path.join(spr, "ending.raw"))), FMT_RGB565)
    pack.tex("buttons", patch_buttons(load_raw(os.path.join(spr, "buttons.raw"))))

    pack.tex("player1", orig.player_sheet(1))
    pack.tex("player1_flash", orig.player_sheet(1, True))
    pack.tex("player2", orig.player_sheet(2))
    pack.tex("player2_flash", orig.player_sheet(2, True))
    pack.tex("die_part1", orig.death_sheet())
    pack.tex("ghost", orig.enemy_sheet())
    pack.tex("ghost_run", orig.scared_sheet())
    print("original character sheets drawn")

    bgdir = os.path.join(res, "Backgrounds")
    for f in sorted(os.listdir(bgdir)):
        if f.lower().endswith(".png"):
            img = Image.open(os.path.join(bgdir, f)).convert("RGBA")
            pack.tex("bg/" + os.path.splitext(f)[0], img, FMT_RGB565)
            print("background", f, img.size)

    # editor tool icons (the citadel tool and the start-position tools use the new art)
    for f in ("erasure", "pathway", "pellet"):
        p = os.path.join(editor, f + ".png")
        if os.path.exists(p):
            pack.tex("ed_" + f, Image.open(p).convert("RGBA"))
    pack.tex("ed_citadel", orig.finish(orig.draw_gloom(orig.ENEMY_COLORS[1], (0, 1), 0), 32, 32))

    sdir = os.path.join(res, "Sounds")
    for f in sorted(os.listdir(sdir)):
        if not f.lower().endswith(".wav"):
            continue
        with wave.open(os.path.join(sdir, f)) as w:
            ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
            frames = w.readframes(n)
        if sw != 2:
            sys.exit(f"{f}: only 16-bit sounds are supported")
        samples = struct.unpack("<%dh" % (len(frames) // 2), frames)
        if ch == 2:
            samples = [(samples[i] + samples[i + 1]) // 2 for i in range(0, len(samples), 2)]
        pcm = struct.pack(">%dh" % len(samples), *samples)
        pcm += b"\0" * (((len(pcm) + 31) & ~31) - len(pcm))
        pack.snd("snd/" + os.path.splitext(f)[0], pcm, rate)
        print("sound", f, rate)

    std = os.path.join(editor, "Levels.plist")
    pack.data("levels/Standard levels", open(std, "rb").read())
    xlev = os.path.join(res, "Levels", "The X Levels.plist")
    if os.path.exists(xlev):
        pack.data("levels/The X Levels", open(xlev, "rb").read())

    for lang in ("en", "de", "fr"):
        p = os.path.join(res, lang + ".lproj", "Localizable.strings")
        if not os.path.exists(p):
            continue
        s = read_strings(p)
        s["Help1"], s["Help2"] = HELP_NAMES[lang]
        txt = "".join(f"{k}={v}\n" for k, v in sorted(s.items()))
        pack.data("strings/" + lang, txt.encode("utf-8"))

    os.makedirs(os.path.join(ROOT, "data"), exist_ok=True)
    out = os.path.join(ROOT, "data", "pack.bin")
    pack.write(out)
    print("wrote", out, os.path.getsize(out), "bytes")

    app = os.path.join(ROOT, "apps", "pacx")
    os.makedirs(os.path.join(app, "music"), exist_ok=True)
    os.makedirs(os.path.join(app, "levels"), exist_ok=True)
    orig.app_icon().save(os.path.join(app, "icon.png"))
    if os.path.exists(xlev):
        shutil.copyfile(xlev, os.path.join(app, "levels", "The X Levels.plist"))

    mdir = os.path.join(res, "Music")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("ffmpeg not found: no music written")
        return
    for f in sorted(os.listdir(mdir)):
        base, ext = os.path.splitext(f)
        if ext.lower() not in (".mp3", ".mov", ".m4a"):
            continue
        dst = os.path.join(app, "music", base + ".ogg")
        if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(os.path.join(mdir, f)):
            continue
        subprocess.run([ffmpeg, "-loglevel", "error", "-y", "-i", os.path.join(mdir, f), "-vn",
                        "-ac", "2", "-ar", "32000", "-c:a", "libvorbis", "-q:a", "3", dst], check=True)
        print("music", base)


if __name__ == "__main__":
    main()
