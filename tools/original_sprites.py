"""
Original character art for the Wii port.

The two players ("Bibs": round, antenna-wearing critters) and the four
enemies ("Glooms": wobbling jelly cubes) are drawn here from scratch with
PIL, on the same sprite-sheet layouts and frame counts the game engine
expects, so the gameplay timing is unchanged:

  player sheet   512x64, 32x32 frames: 0-7 left, 8-15 right, 16-23 down, 24-31 up
  player death   512x32, 16 frames
  enemy sheet    192x288, 32x32 frames: enemy n, dir d (0 right, 1 left, 2 up, 3 down)
                 at frames (n*4+d)*3 .. +2; frames 48-51 are the eyes only
  scared sheet   192x32: frames 0-2 scared, 3-5 scared flashing
  button icons   48x48 (one player, two player, both)

Everything is drawn 8x larger and scaled down for clean edges.
"""
import math
from PIL import Image, ImageDraw

SS = 8  # supersampling factor

PLAYER_COLORS = {
    1: ((96, 214, 72), (40, 120, 30), (250, 230, 90)),    # body, outline, antenna ball
    2: ((88, 172, 255), (30, 80, 170), (255, 120, 170)),
}
ENEMY_COLORS = [
    ((232, 64, 60), (130, 20, 20)),
    ((160, 96, 235), (80, 40, 140)),
    ((38, 190, 170), (10, 100, 90)),
    ((248, 168, 40), (150, 90, 10)),
]
SCARED = ((52, 70, 230), (20, 30, 120))
SCARED_FLASH = ((240, 240, 255), (150, 150, 200))

# direction vectors for the eyes: left, right, down, up (player frame-group order)
PLAYER_DIRS = [(-1, 0), (1, 0), (0, 1), (0, -1)]
# enemy frame-group order: right, left, up, down
ENEMY_DIRS = [(1, 0), (-1, 0), (0, -1), (0, 1)]


def canvas(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def finish(img, w, h):
    return img.resize((w, h), Image.LANCZOS)


def S(v):
    return v * SS


def ellipse(d, cx, cy, rx, ry, fill, outline=None, width=0):
    d.ellipse((S(cx - rx), S(cy - ry), S(cx + rx), S(cy + ry)), fill=fill,
              outline=outline, width=int(S(width)) if outline else 0)


def eyes(d, cx, cy, dx, dy, spread, r, pupil, white=(255, 255, 255), dark=(20, 20, 30)):
    for side in (-1, 1):
        ex = cx + side * spread + dx * 1.2
        ey = cy + dy * 1.0
        ellipse(d, ex, ey, r, r * 1.15, white, dark, 0.6)
        ellipse(d, ex + dx * (r - pupil) * 0.8, ey + dy * (r - pupil) * 0.8, pupil, pupil, dark)


# ---------------------------------------------------------------------------------- players

def draw_bib(nr, direction, frame, flash=False, size=32, scale=1.0, cx=16.0, cy=17.0, img=None):
    """One player frame. frame 0..7 drives the bounce, feet and antenna."""
    body, line, ball = PLAYER_COLORS[nr]
    if flash:
        body = tuple(min(255, int(c * 0.35 + 255 * 0.65)) for c in body)
    own = img is None
    if own:
        img = canvas(size, size)
    d = ImageDraw.Draw(img)
    dx, dy = direction
    t = frame / 7.0
    bounce = math.sin(t * math.pi) * 1.4 * scale
    r = 10.5 * scale
    by = cy - bounce
    # feet
    step = math.sin(t * math.pi * 2)
    for side in (-1, 1):
        fx = cx + side * 5.0 * scale + (dx * step * 1.5 * side * scale)
        fy = cy + r - 0.5 * scale + (step * side * 0.8 * scale if dx == 0 else 0)
        ellipse(d, fx, fy, 3.6 * scale, 2.2 * scale, line)
    # antenna
    sway = math.sin(t * math.pi * 2) * 2.5 * scale - dx * 1.5 * scale
    top = (cx + sway, by - r - 5.5 * scale)
    d.line((S(cx), S(by - r + 1 * scale), S(top[0]), S(top[1])), fill=line, width=int(S(1.3 * scale)))
    ellipse(d, top[0], top[1], 2.3 * scale, 2.3 * scale, ball, line, 0.6 * scale)
    # body with a highlight
    ellipse(d, cx, by, r, r * 0.95, body, line, 1.1 * scale)
    ellipse(d, cx - 3.5 * scale, by - 4.5 * scale, 3.2 * scale, 2.0 * scale,
            tuple(min(255, c + 70) for c in body))
    if nr == 2:
        # cheek freckles set player 2 apart
        for side in (-1, 1):
            ellipse(d, cx + side * 6.2 * scale + dx * 1.5 * scale, by + 3.5 * scale, 1.6 * scale, 1.0 * scale,
                    (255, 150, 190))
    # eyes look where the critter walks
    eyes(d, cx + dx * 2.0 * scale, by - 2.0 * scale + dy * 2.0 * scale, dx, dy,
         3.6 * scale, 2.7 * scale, 1.3 * scale)
    # a mouth that opens and closes while walking (not shown when walking away, up)
    if dy >= 0:
        open_ = 0.6 + 2.2 * abs(math.sin(t * math.pi))
        mx = cx + dx * 3.5 * scale
        my = by + 4.5 * scale + dy * 1.5 * scale
        d.ellipse((S(mx - 2.6 * scale), S(my - open_ * 0.5 * scale), S(mx + 2.6 * scale), S(my + open_ * 0.5 * scale)),
                  fill=(90, 20, 30), outline=line, width=int(S(0.5 * scale)))
    if flash:
        # sparkles around an invincible player
        for k in range(4):
            a = t * math.pi + k * math.pi / 2
            sx = cx + math.cos(a) * 13 * scale
            sy = by + math.sin(a) * 13 * scale
            d.line((S(sx - 2 * scale), S(sy), S(sx + 2 * scale), S(sy)), fill=(255, 255, 160), width=int(S(0.8 * scale)))
            d.line((S(sx), S(sy - 2 * scale), S(sx), S(sy + 2 * scale)), fill=(255, 255, 160), width=int(S(0.8 * scale)))
    return img


def player_sheet(nr, flash=False):
    sheet = Image.new("RGBA", (512, 64), (0, 0, 0, 0))
    for g, direction in enumerate(PLAYER_DIRS):
        for f in range(8):
            idx = g * 8 + f
            fr = finish(draw_bib(nr, direction, f, flash), 32, 32)
            sheet.paste(fr, ((idx % 16) * 32, (idx // 16) * 32))
    return sheet


def death_sheet():
    """The player squashes, spins, and pops into a puff (16 frames)."""
    sheet = Image.new("RGBA", (512, 32), (0, 0, 0, 0))
    body, line, ball = PLAYER_COLORS[1]
    for f in range(16):
        img = canvas(32, 32)
        d = ImageDraw.Draw(img)
        if f < 11:
            k = f / 10.0
            rx = 10.5 * (1.0 + 0.35 * k)
            ry = 10.0 * (1.0 - 0.75 * k)
            cy = 17 + 9 * k
            ellipse(d, 16, cy, rx, max(1.5, ry), body, line, 1.1)
            # dizzy X eyes
            for side in (-1, 1):
                ex, ey = 16 + side * 4, cy - max(1.0, ry * 0.3)
                s = 1.8
                d.line((S(ex - s), S(ey - s), S(ex + s), S(ey + s)), fill=(20, 20, 30), width=S(1))
                d.line((S(ex - s), S(ey + s), S(ex + s), S(ey - s)), fill=(20, 20, 30), width=S(1))
        else:
            k = (f - 11) / 4.0
            rr = 4 + 10 * k
            a = int(255 * (1 - k))
            for j in range(6):
                ang = j * math.pi / 3 + k
                px = 16 + math.cos(ang) * rr
                py = 24 + math.sin(ang) * rr * 0.6
                ellipse(d, px, py, 2.4 * (1 - k * 0.6), 2.4 * (1 - k * 0.6), body + (a,))
        sheet.paste(finish(img, 32, 32), (f * 32, 0))
    return sheet


# ---------------------------------------------------------------------------------- enemies

def draw_gloom(color, direction, frame, eyes_only=False, scared=False, img=None, cx=16.0, cy=16.0, scale=1.0):
    fill, line = color
    own = img is None
    if own:
        img = canvas(32, 32)
    d = ImageDraw.Draw(img)
    dx, dy = direction
    wob = [0.0, 1.0, -1.0][frame % 3]
    if not eyes_only:
        # jelly cube body with rounded corners, a wobbling top and three drips below
        x0, x1 = cx - 11.5 * scale - wob * 0.4, cx + 11.5 * scale + wob * 0.4
        y0, y1 = cy - 10.5 * scale + wob * 0.6, cy + 8.5 * scale
        d.rounded_rectangle((S(x0), S(y0), S(x1), S(y1)), radius=S(5.5 * scale), fill=fill,
                            outline=line, width=int(S(1.1 * scale)))
        for k, off in enumerate((-7, 0, 7)):
            ln = (3.0 + [wob, -wob, wob * 0.5][k] * 1.2) * scale
            px = cx + off * scale
            d.rounded_rectangle((S(px - 2.2 * scale), S(y1 - 2 * scale), S(px + 2.2 * scale), S(y1 + ln)),
                                radius=S(2.2 * scale), fill=fill, outline=line, width=int(S(0.8 * scale)))
            d.rectangle((S(px - 1.4 * scale), S(y1 - 2.6 * scale), S(px + 1.4 * scale), S(y1)), fill=fill)
        # shine
        d.rounded_rectangle((S(x0 + 3 * scale), S(y0 + 2.5 * scale), S(x0 + 8 * scale), S(y0 + 4.5 * scale)),
                            radius=S(1 * scale), fill=tuple(min(255, c + 80) for c in fill))
    if scared:
        # wavy frown and small spiral eyes
        dark = line
        for side in (-1, 1):
            ex, ey = cx + side * 4.5 * scale, cy - 3 * scale
            ellipse(d, ex, ey, 2.3 * scale, 2.3 * scale, None, dark, 0.8 * scale)
            ellipse(d, ex, ey, 0.8 * scale, 0.8 * scale, dark)
        pts = []
        for i in range(9):
            px = cx - 7 * scale + i * 1.75 * scale
            py = cy + 4 * scale + (1.2 * scale if i % 2 else -0.4 * scale)
            pts.append((S(px), S(py)))
        d.line(pts, fill=dark, width=int(S(1.0 * scale)))
        return img
    # grumpy brows over eyes that follow the direction of travel
    ex_c = cx + dx * 2.0 * scale
    ey_c = cy - 3.0 * scale + dy * 2.0 * scale
    eyes(d, ex_c, ey_c, dx, dy, 4.4 * scale, 3.1 * scale, 1.6 * scale)
    for side in (-1, 1):
        bx = ex_c + side * 4.4 * scale
        by = ey_c - 4.2 * scale
        d.line((S(bx - 3 * scale * side), S(by - 1.6 * scale), S(bx + 2.5 * scale * side), S(by + 0.9 * scale)),
               fill=(25, 15, 20), width=int(S(1.4 * scale)))
    if not eyes_only:
        mx = cx + dx * 2.5 * scale
        my = cy + 4.0 * scale + max(0, dy) * 1.5 * scale
        d.line((S(mx - 3 * scale), S(my + 0.8 * scale), S(mx), S(my - 0.6 * scale), S(mx + 3 * scale), S(my + 0.8 * scale)),
               fill=(40, 10, 15), width=int(S(1.0 * scale)))
    return img


def enemy_sheet():
    sheet = Image.new("RGBA", (192, 288), (0, 0, 0, 0))
    for n, color in enumerate(ENEMY_COLORS):
        for g, direction in enumerate(ENEMY_DIRS):
            for f in range(3):
                idx = (n * 4 + g) * 3 + f
                fr = finish(draw_gloom(color, direction, f), 32, 32)
                sheet.paste(fr, ((idx % 6) * 32, (idx // 6) * 32))
    # eyes travelling home: right, left, up, down
    for g, direction in enumerate(ENEMY_DIRS):
        idx = 48 + g
        fr = finish(draw_gloom(ENEMY_COLORS[0], direction, 0, eyes_only=True), 32, 32)
        sheet.paste(fr, ((idx % 6) * 32, (idx // 6) * 32))
    return sheet


def scared_sheet():
    sheet = Image.new("RGBA", (192, 32), (0, 0, 0, 0))
    for f in range(6):
        col = SCARED if f < 3 else SCARED_FLASH
        fr = finish(draw_gloom(col, (0, 0), f % 3, scared=True), 32, 32)
        sheet.paste(fr, (f * 32, 0))
    return sheet


# ---------------------------------------------------------------------------------- buttons / scenes

def button_icon(kind):
    """48x48 title-screen button art: 0 one player, 1 player two, 2 both players."""
    img = canvas(48, 48)
    if kind == 0:
        draw_bib(1, (1, 0), 3, img=img, cx=24, cy=27, scale=1.5)
    elif kind == 1:
        draw_bib(2, (-1, 0), 3, img=img, cx=24, cy=27, scale=1.5)
    else:
        draw_bib(2, (-1, 0), 2, img=img, cx=30, cy=29, scale=1.15)
        draw_bib(1, (1, 0), 4, img=img, cx=17, cy=26, scale=1.15)
    return finish(img, 48, 48)


def ending_row():
    """The cast for the ending picture: four enemies around the two players."""
    w, h = 300, 46
    img = canvas(w, h)
    xs = [18, 66, 114, 186, 234, 282]
    draw_gloom(ENEMY_COLORS[3], (1, 0), 0, img=img, cx=xs[0], cy=24, scale=1.2)
    draw_gloom(ENEMY_COLORS[2], (1, 0), 1, img=img, cx=xs[1], cy=24, scale=1.2)
    draw_bib(1, (1, 0), 3, img=img, cx=xs[2] + 16, cy=26, scale=1.25)
    draw_bib(2, (-1, 0), 3, img=img, cx=xs[3] - 16, cy=26, scale=1.25)
    draw_gloom(ENEMY_COLORS[1], (-1, 0), 2, img=img, cx=xs[4], cy=24, scale=1.2)
    draw_gloom(ENEMY_COLORS[0], (-1, 0), 0, img=img, cx=xs[5], cy=24, scale=1.2)
    # a heart between the two players
    d = ImageDraw.Draw(img)
    hx, hy = (xs[2] + xs[3]) / 2, 8
    ellipse(d, hx - 2.6, hy, 3.2, 3.2, (255, 90, 130))
    ellipse(d, hx + 2.6, hy, 3.2, 3.2, (255, 90, 130))
    d.polygon([(S(hx - 5.6), S(hy + 0.8)), (S(hx + 5.6), S(hy + 0.8)), (S(hx), S(hy + 7.5))], fill=(255, 90, 130))
    return finish(img, w, h)


def app_icon():
    """128x48 Homebrew Channel icon."""
    img = canvas(128, 48)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((0, 0, S(128) - 1, S(48) - 1), radius=S(8), fill=(10, 14, 40), outline=(70, 90, 200), width=S(2))
    for i in range(7):
        ellipse(d, 52 + i * 9, 24, 1.6, 1.6, (255, 230, 160))
    draw_bib(1, (1, 0), 3, img=img, cx=28, cy=25, scale=1.25)
    draw_gloom(ENEMY_COLORS[0], (-1, 0), 1, img=img, cx=108, cy=24, scale=1.1)
    return finish(img, 128, 48)


if __name__ == "__main__":
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    player_sheet(1).save(f"{out}/player1.png")
    player_sheet(1, True).save(f"{out}/player1_flash.png")
    player_sheet(2).save(f"{out}/player2.png")
    player_sheet(2, True).save(f"{out}/player2_flash.png")
    death_sheet().save(f"{out}/die_part1.png")
    enemy_sheet().save(f"{out}/ghost.png")
    scared_sheet().save(f"{out}/ghost_run.png")
    for k in range(3):
        button_icon(k).save(f"{out}/button{k}.png")
    ending_row().save(f"{out}/ending_row.png")
    app_icon().save(f"{out}/icon.png")
