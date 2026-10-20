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


# ------------------------------------------------------------------------------ icon

LOGO_BOX = (88, 45, 552, 200)  # "PAC THE MAN" in the title picture


def app_icon(res):
    """128x48 Homebrew Channel icon: the game's own icon beside the title logo."""
    title = load_raw(os.path.join(res, "Sprites", "title.raw")).convert("RGB")
    icon = Image.new("RGBA", (128, 48), (0, 0, 0, 255))
    logo = title.crop(LOGO_BOX)
    logo.thumbnail((128, 48), Image.LANCZOS)
    icon.paste(logo, ((128 - logo.width) // 2, (48 - logo.height) // 2))
    return icon


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
                 "citadel1", "citadel2", "citadel3", "citadel4",
                 "title", "ending", "buttons",
                 "player1", "player1_flash", "player2", "player2_flash", "die_part1",
                 "ghost", "ghost_run"]
    for name in from_game:
        img = load_raw(os.path.join(spr, name + ".raw"))
        pack.tex(name, img, FMT_RGB565 if name in opaque else FMT_RGBA8)
        print("sprite", name, img.size)

    bgdir = os.path.join(res, "Backgrounds")
    for f in sorted(os.listdir(bgdir)):
        if f.lower().endswith(".png"):
            img = Image.open(os.path.join(bgdir, f)).convert("RGBA")
            pack.tex("bg/" + os.path.splitext(f)[0], img, FMT_RGB565)
            print("background", f, img.size)

    # editor tool icons
    for f, name in (("erasure", "ed_erasure"), ("pathway", "ed_pathway"), ("pellet", "ed_pellet"),
                    ("player1", "ed_player1"), ("player2", "ed_player2"), ("citadel_small", "ed_citadel")):
        p = os.path.join(editor, f + ".png")
        if os.path.exists(p):
            pack.tex(name, Image.open(p).convert("RGBA"))

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
        txt = "".join(f"{k}={v}\n" for k, v in sorted(s.items()))
        pack.data("strings/" + lang, txt.encode("utf-8"))

    os.makedirs(os.path.join(ROOT, "data"), exist_ok=True)
    out = os.path.join(ROOT, "data", "pack.bin")
    pack.write(out)
    print("wrote", out, os.path.getsize(out), "bytes")

    app = os.path.join(ROOT, "apps", "pacx")
    os.makedirs(os.path.join(app, "music"), exist_ok=True)
    os.makedirs(os.path.join(app, "levels"), exist_ok=True)
    app_icon(res).save(os.path.join(app, "icon.png"))
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
