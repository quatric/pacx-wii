/*
 * PMSprite, PMPlayer, PMGhost, PMExtra, PMChar, PMDigit, PMHeart, PMPoints and PMButton.
 * Each function follows the method of the same name in the original.
 */
#include <math.h>
#include <string.h>
#include "engine.h"
#include "game.h"
#include "actors.h"
#include "audio.h"
#include <stdlib.h>

/* ================================================================== PMSprite */

static int tile_x(const Sprite *s) { return (spr_px(s) - 40) / 44; }
static int tile_y(const Sprite *s) { return (spr_py(s) - 42) / 44; }
static int on_branch_h(const Sprite *s) { int v = spr_px(s) - 40; return v == (v / 44) * 44; }
static int on_branch_v(const Sprite *s) { int v = spr_py(s) - 42; return v == (v / 44) * 44; }

static void check_bounds(Sprite *s) {
    if (spr_px(s) <= 1) s->x = 606.0f;
    else if (spr_px(s) >= 607) s->x = 2.0f;
    else if (spr_py(s) <= 3) s->y = 476.0f;
    else if (spr_py(s) >= 477) s->y = 4.0f;
}

static int is_on_citadel_entry(const Sprite *s) {
    return spr_px(s) == 44 * game.citadelEntryX + 40 && spr_py(s) == 44 * game.citadelEntryY + 42;
}

/* PMSprite.directionAllowed: passes the pixel position, which the game clamps to 0..12, 0..9 */
static int sprite_direction_allowed(const Sprite *s, int dir) {
    return game_direction_allowed(dir, spr_px(s), spr_py(s));
}

static void check_directions(Sprite *s, int stop) {
    PMSpriteData *p = &s->pm;
    int tx = (spr_px(s) - 40) / 44, ty = (spr_py(s) - 42) / 44;
    if (!stop) {
        p->wantLeft = !spr_moving_right(s);
        p->wantRight = !spr_moving_left(s);
        p->wantUp = !spr_moving_down(s);
        p->wantDown = !spr_moving_up(s);
    }
    p->directions = 0;
    p->canDown = p->canUp = p->canRight = p->canLeft = 0;
    if (game_direction_allowed(1, tx, ty)) { p->canLeft = 1; p->directions++; }
    else if (stop) spr_set_hspeed(s, 0);
    if (game_direction_allowed(2, tx, ty)) { p->canRight = 1; p->directions++; }
    else if (stop) spr_set_hspeed(s, 0);
    if (game_direction_allowed(4, tx, ty)) { p->canUp = 1; p->directions++; }
    else if (stop) spr_set_vspeed(s, 0);
    if (game_direction_allowed(8, tx, ty) && !game_is_citadel_entry(tx, ty)) {
        p->canDown = 1;
        p->directions++;
        if (stop) return;
    } else if (stop) {
        spr_set_vspeed(s, 0);
        return;
    }
    p->wantLeft = p->canLeft && p->wantLeft;
    p->wantRight = p->canRight && p->wantRight;
    p->wantUp = p->canUp && p->wantUp;
    p->wantDown = p->canDown && p->wantDown;
}

void actor_start(Sprite *s) {
    /* PMSprite.start, called by createSprite:xPos:yPos: */
    if (s->kind == K_PLAYER || s->kind == K_GHOST || s->kind == K_EXTRA) {
        s->pm.startX = spr_px(s);
        s->pm.startY = spr_py(s);
    }
    if (s->kind == K_PLAYER) s->u.pl.lookDirection = 1;
    if (s->kind == K_EXTRA) s->u.ex.extraNr = -1;
}

/* ================================================================== PMPlayer */

static void adjust_spots(Sprite *s) {
    float x = s->x, y = s->y;
    float sx = x + -134.0f;
    Sprite *sp = spr_get(s->u.pl.spot);
    if (sp) spr_move_to(sp, sx, y + -134.0f);
    Sprite *r;
    if ((r = spr_get(s->u.pl.rects[0]))) spr_move_to(r, -640.0f + sx + 2.0f, 0);
    if ((r = spr_get(s->u.pl.rects[1]))) spr_move_to(r, x + 134.0f + 30.0f, 0);
    float ry = -480.0f + y + -134.0f + 2.0f;
    sx = sx + -2.0f;
    if ((r = spr_get(s->u.pl.rects[2]))) spr_move_to(r, sx, ry);
    if ((r = spr_get(s->u.pl.rects[3]))) spr_move_to(r, sx, 134.0f + y + 30.0f);
}

void player_set_frameset(Sprite *s) {
    int look = s->u.pl.lookDirection;
    int d = look == 2 ? 1 : look == 8 ? 2 : look == 4 ? 3 : 0;
    if (s->u.pl.invulnerable) d |= 4;
    if (s->u.pl.playerNr == 2) d |= 8;
    int fi = s->frameIndex <= 4 ? s->frameIndex : 0;
    spr_set_frameset_index(s, fs_player[d], fi);
}

void player_start(Sprite *s, int nr, int autoPlayer) {
    s->u.pl.autoPlayer = autoPlayer;
    s->u.pl.playerNr = nr;
    spr_set_frame_speed(s, 50);
    player_set_frameset(s);
    if (game.difficulty == 4) {
        Sprite *sp = spr_create(L(9), K_SPRITE);
        spr_set_frameset_name(sp, "spot");
        s->u.pl.spot = spr_ref(sp);
        s->u.pl.hasSpot = 1;
        for (int i = 0; i < 4; i++) {
            Sprite *r = spr_create(L(9), K_SPRITE);
            spr_set_empty(r, 640, 480, 0, 0, 0, 1);
            s->u.pl.rects[i] = spr_ref(r);
        }
        adjust_spots(s);
    }
}

void player_move_to_start(Sprite *s) {
    spr_move_to(s, (float)s->pm.startX, (float)s->pm.startY);
    s->visible = 1;
    spr_set_frame_speed(s, 0);
    player_set_frameset(s);
    spr_set_frame_index(s, 3);
    spr_stop(s);
    s->color[3] = 0.7f;
    spr_set_delay(s, 1.0f);
    s->u.pl.dying = FPS;
}

static void player_move_automatic(Sprite *s) {
    PMSpriteData *p = &s->pm;
    int look = s->u.pl.lookDirection;
    if (p->changedDirection >= 5 && on_branch_h(s) && on_branch_v(s)) {
        check_directions(s, 0);
        p->wantUp = p->wantDown = p->wantRight = p->wantLeft = 0;
        if (p->directions == 2) {
            p->wantLeft = p->canLeft;
            p->wantRight = p->canRight;
            p->wantDown = p->canDown;
            p->wantUp = p->canUp;
        } else if (p->directions >= 3) {
            for (int i = 0; i <= 3; i++) {
                switch (rnd(0, 3)) {
                case 0: p->wantLeft = p->canLeft && look != 2; break;
                case 1: p->wantRight = p->canRight && look != 1; break;
                case 2: p->wantDown = p->canDown && look != 4; break;
                case 3: p->wantUp = p->canUp && look != 8; break;
                }
                if (p->wantLeft || p->wantRight || p->wantDown || p->wantUp) break;
            }
        }
        p->wantLeft = p->wantLeft && look != 2;
        p->wantRight = p->wantRight && look != 1;
        p->wantUp = p->wantUp && look != 8;
        p->wantDown = p->wantDown && look != 4;
        if (!p->wantLeft && !p->wantRight && !p->wantUp && !p->wantDown) {
            check_directions(s, 0);
            if (!p->wantLeft && !p->wantRight && !p->wantUp && !p->wantDown) {
                p->wantLeft = p->canLeft;
                p->wantRight = p->canRight;
                p->wantUp = p->canUp;
                p->wantDown = p->canDown;
            }
        }
        p->changedDirection = 0;
    } else {
        p->wantRight = look == 2;
        p->wantLeft = look == 1;
        p->wantUp = look == 4;
        p->wantDown = look == 8;
    }
}

static void player_move(Sprite *s) {
    PMSpriteData *p = &s->pm;
    if (s->u.pl.invulnerable > 0 && --s->u.pl.invulnerable == 0) player_set_frameset(s);
    if (s->u.pl.soundCount > 0) s->u.pl.soundCount--;
    if (s->u.pl.dying > 0) {
        if (--s->u.pl.dying != 0) return;
        if (game.lives[0] == 0 && game.lives[1] == 0) { game_over(); return; }
        if (game.lives[s->u.pl.playerNr - 1] == 0) { spr_delete(s); return; }
        s->color[3] = 1.0f;
        spr_set_frame_speed(s, 50);
        return;
    }
    if (s->u.pl.hasSpot) adjust_spots(s);
    if (!game_moving_allowed()) return;
    if (!s->visible) s->visible = 1;
    int keyPlayer = game_player_nr() == 0 ? s->u.pl.playerNr : 0;
    int iterations = s->u.pl.doubleSpeed ? 14 : 10;
    for (int it = 0; it < iterations; it++) {
        if (s->u.pl.atePellet > 0) {
            s->u.pl.atePellet--;
            s->speedFactor = 0.77f;
        }
        int bh = on_branch_h(s), bv = on_branch_v(s);
        p->canLeft = p->canRight = !bh;
        p->canUp = p->canDown = !bv;
        if (bh && bv) {
            check_directions(s, 1);
        } else if (!s->u.pl.autoPlayer) {
            if (bh) s->x = (float)spr_px(s);
            else if (bv) s->y = (float)spr_py(s);
        }
        if (s->u.pl.autoPlayer) {
            player_move_automatic(s);
            if (p->wantLeft && p->wantRight) p->wantLeft = 0;
        } else {
            if (game_key_down(keyPlayer, 1)) {
                p->wantLeft = 1; p->wantDown = 0; p->wantUp = 0; p->wantRight = 0;
                if (p->wantLeft && p->wantRight) p->wantLeft = 0;
            } else if (game_key_down(keyPlayer, 2)) {
                p->wantRight = 1; p->wantDown = 0; p->wantUp = 0;
                p->wantLeft = 0;
            } else if (game_key_down(keyPlayer, 4)) {
                p->wantUp = 1; p->wantLeft = 0; p->wantRight = 0; p->wantDown = 0;
                if (p->wantLeft && p->wantRight) p->wantLeft = 0;
            } else if (game_key_down(keyPlayer, 8)) {
                p->wantDown = 1; p->wantRight = 0; p->wantLeft = 0; p->wantUp = 0;
                if (p->wantLeft && p->wantRight) p->wantLeft = 0;
            } else {
                if (p->wantLeft && p->wantRight) p->wantLeft = 0;
            }
        }
        if (p->wantUp && p->wantDown) p->wantDown = 0;

        float h, v;
        int set = 1;
        if (p->canLeft && p->wantLeft) { h = -1.0f; v = 0; }
        else if (p->canRight && p->wantRight) { h = 1.0f; v = 0; }
        else if (p->canUp && p->wantUp) { h = 0; v = -1.0f; }
        else {
            h = 0;
            v = (p->canDown && p->wantDown) ? 1.0f : 0.0f;
            if (v < 0.0001f) set = 0;
        }
        if (set) {
            int look = 0;
            if (!spr_moving_left(s) && h < -0.0001f) look = 1;
            else if (!spr_moving_right(s) && h > 0.0001f) look = 2;
            else if (!spr_moving_up(s) && v < -0.0001f) look = 4;
            else if (!spr_moving_down(s) && v > 0.0001f) look = 8;
            if (look) {
                s->u.pl.lookDirection = look;
                player_set_frameset(s);
            }
            spr_set_speed(s, h, v);
        }
        spr_move(s);
        check_bounds(s);
    }
    s->speedFactor = 1.0f;
    p->changedDirection++;
}

static void bounds_inset(const Sprite *s, float d, float r[4]) {
    /* CGRectInset([sprite bounds], d, d) */
    r[0] = s->x + d;
    r[1] = s->y + d;
    r[2] = spr_width(s) - 2 * d;
    r[3] = spr_height(s) - 2 * d;
}

static int intersects(const float a[4], const float b[4]) {
    if (a[2] <= 0 || a[3] <= 0 || b[2] <= 0 || b[3] <= 0) return 0;
    return a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3];
}

static void player_collide_pellet(Sprite *s, Sprite *t) {
    float a[4], b[4];
    bounds_inset(s, 10, a);
    bounds_inset(t, 0, b);
    if (!intersects(a, b)) return;
    spr_delete(t);
    s->u.pl.atePellet = 80;
    game_add_score(5, s);
    if (game.mode != 3) return;
    int alt = s->u.pl.soundCount;
    if (alt > 0) s->u.pl.soundCount = 0;
    else s->u.pl.soundCount = FPS / 2;
    if (s->u.pl.playerNr == 1) snd_play(alt > 0 ? SND_EAT_PELLET_F : SND_EAT_PELLET_B);
    else snd_play(alt > 0 ? SND_EAT_PELLET_F2 : SND_EAT_PELLET_B2);
}

static void player_collide_super_pellet(Sprite *s, Sprite *t) {
    float a[4], b[4];
    bounds_inset(s, 15, a);
    bounds_inset(t, 0, b);
    if (!intersects(a, b)) return;
    spr_delete(t);
    s->u.pl.atePellet = 80;
    game_add_score(10, s);
    if (game.mode == 3) snd_play(SND_EAT_SUPERPELLET);
    game_super_pellet_eaten();
}

static void player_collide_ghost(Sprite *s, Sprite *t) {
    if (abs(spr_px(s) - spr_px(t)) > 10) return;
    if (abs(spr_py(s) - spr_py(t)) > 10) return;
    if (t->u.gh.state == 3) {
        ghost_start_going_home(t);
        t->visible = 0;
        s->visible = 0;
        if (game.mode == 3) snd_play(SND_EAT_GHOST);
        game_ghost_eaten(s);
        return;
    }
    if (s->u.pl.invulnerable != 0 || t->u.gh.state != 2) return;
    s->u.pl.dying = FPS * 3;
    spr_set_frameset_index(s, fs_die_part1, 0);
    spr_set_frame_speed(s, 18);
    Sprite *ds = spr_get(s->u.pl.doubleScore);
    if (ds) spr_delete(ds);
    s->u.pl.doubleScore = spr_ref(NULL);
    s->u.pl.doubleSpeed = 0;
    s->u.pl.lookDirection = 1;
    s->pm.wantDown = s->pm.wantUp = s->pm.wantRight = s->pm.wantLeft = 0;
    if (game.mode == 3) snd_play(SND_PLAYER_DIE);
    game_player_starts_dying(s->u.pl.playerNr);
}

static const int scoreTable[10] = {200, 400, 500, 800, 1000, 2000, 3000, 4000, 5000, 10000};

static void player_collide_extra(Sprite *s, Sprite *t) {
    if (abs(spr_px(s) - spr_px(t)) > 10) return;
    if (abs(spr_py(s) - spr_py(t)) > 10) return;
    int nr = t->u.ex.extraNr, idx = 0;
    switch (nr) {
    case 0: s->u.pl.doubleSpeed = 1; idx = 2; break;
    case 1: idx = 4; break;
    case 2:
        s->u.pl.invulnerable = FPS * 8;
        player_set_frameset(s);
        idx = 5;
        break;
    case 3: idx = 6; break;
    case 4: idx = 8; break;
    }
    spr_delete(t);
    int pts = scoreTable[idx];
    game_add_score(pts, s);
    game_create_points(pts, s);
    if (game.mode == 3) snd_play(SND_EXTRA_EATEN);
    if (nr == 1 && !spr_get(s->u.pl.doubleScore)) {
        Sprite *m = spr_create(L(11), K_SPRITE);
        s->u.pl.doubleScore = spr_ref(m);
        spr_set_frameset_name(m, s->u.pl.playerNr == 1 ? "font0" : "font1");
        spr_set_frame_index(m, 54);
        spr_move_to(m, s->u.pl.playerNr == 2 ? 554.0f : 70.0f, 7.0f);
    }
}

static void player_collide(Sprite *s, Sprite *t) {
    if (s->u.pl.dying > 0) return;
    if (!game_moving_allowed()) return;
    switch (t->layer) {
    case L(2): player_collide_pellet(s, t); break;
    case L(3): player_collide_super_pellet(s, t); break;
    case L(5): player_collide_ghost(s, t); break;
    case L(6): player_collide_extra(s, t); break;
    }
}

int player_double_score(const Sprite *s) { return spr_get(s->u.pl.doubleScore) != NULL; }

/* ================================================================== PMGhost */

void ghost_set_frameset(Sprite *s) {
    switch (s->u.gh.state) {
    case 1:
    case 2: {
        int d = s->u.gh.ghostNr * 4;
        if (spr_moving_left(s)) d |= 1;
        else if (spr_moving_up(s)) d |= 2;
        else if (spr_moving_down(s)) d |= 3;
        int fi = s->frameIndex <= 2 ? s->frameIndex : 0;
        spr_set_frameset_index(s, fs_ghosts[d], fi);
        break;
    }
    case 3:
    case 5:
        if (s->frameSet != fs_ghost_run) {
            spr_set_frameset(s, fs_ghost_run);
            spr_set_frame_range(s, 0, 3);
        }
        break;
    case 4:
        spr_set_frameset(s, fs_ghost);
        if (spr_moving_right(s)) spr_set_frame_range(s, 48, 1);
        else if (spr_moving_left(s)) spr_set_frame_range(s, 49, 1);
        else if (spr_moving_up(s)) spr_set_frame_range(s, 50, 1);
        else spr_set_frame_range(s, 51, 1);
        break;
    }
}

void ghost_start(Sprite *s, int nr, int x, int y) {
    spr_set_frame_speed(s, 5);
    s->u.gh.ghostNr = nr;
    s->pm.startX = 44 * (x - (nr == 1) + (nr == 3)) + 40;
    s->pm.startY = 44 * (y + (nr != 0)) + 42;
    spr_move_to(s, (float)s->pm.startX, (float)s->pm.startY);
    if (nr != 0) {
        s->u.gh.state = 1;
        spr_set_vspeed(s, 1.0f);
    } else {
        if (sprite_direction_allowed(s, 1)) spr_set_hspeed(s, -1.0f);
        else spr_set_vspeed(s, -1.0f);
        s->u.gh.state = 2;
        s->u.gh.reachedTarget = 1;
        s->pm.startY += 44;
    }
    ghost_set_frameset(s);
}

static void ghost_check_running_state(Sprite *s) {
    float old = s->u.gh.runaway;
    s->u.gh.runaway = old + -1.0f;
    if (!(s->u.gh.blink > old) && s->u.gh.blink > s->u.gh.runaway) spr_set_frame_range(s, 0, 6);
    if (0.0f >= s->u.gh.runaway) {
        s->u.gh.state = 2;
        s->u.gh.reachedTarget = 0;
        ghost_set_frameset(s);
    }
}

static void ghost_wait(Sprite *s) {
    if (s->pm.changedDirection >= 6 && on_branch_v(s)) {
        s->vSpeed = -s->vSpeed;
        s->pm.changedDirection = 0;
        if (s->u.gh.state == 1) ghost_set_frameset(s);
    }
    spr_move(s);
}

static void ghost_move_in_level(Sprite *s) {
    PMSpriteData *p = &s->pm;
    if (!(on_branch_h(s) && on_branch_v(s) && p->changedDirection > 4)) {
        spr_move(s);
        check_bounds(s);
        return;
    }
    int diff = game.difficulty;
    int px = spr_px(s), py = spr_py(s);
    Sprite *pl = game_player((s->u.gh.ghostNr & 1) + 1);
    if (!pl) return;
    check_directions(s, 0);
    int ex = game.citadelEntryX, ey = game.citadelEntryY;
    int state = s->u.gh.state;
    if (!s->u.gh.reachedTarget && is_on_citadel_entry(s)) {
        s->u.gh.reachedTarget = 1;
        if (state == 4) {
            p->wantUp = p->wantRight = p->wantLeft = 0;
            p->wantDown = 1;
            p->canDown = 1;
            p->directions = 0;
        }
    }
    int tx = 44 * ex + 40, ty = 44 * ey + 42;
    if (s->u.gh.reachedTarget) {
        if (state == 4) {
            tx = p->startX;
            ty = p->startY;
            if (px == tx && py == ty) {
                spr_set_speed(s, 0, 1.0f);
                p->changedDirection = 0;
                spr_move(s);
                s->u.gh.state = 1;
                ghost_set_frameset(s);
                return;
            }
        } else if (state == 2 || state == 3) {
            tx = spr_px(pl);
            ty = spr_py(pl);
        }
    }

    if (p->directions > 2 || s->u.gh.reachedTarget) {
        int nl, nr, nu, nd;
        int toTarget;
        if (state == 2) toTarget = 1;
        else if (state == 4) toTarget = -1;
        else if (state == 3 && !s->u.gh.reachedTarget) toTarget = 1;
        else toTarget = 0;

        if (toTarget == -1) {
            int h = game_home_direction(tile_x(s), tile_y(s));
            if (h != 0 && !s->u.gh.reachedTarget) {
                nu = p->wantUp && h == 4;
                nd = p->wantDown && h == 8;
                nl = p->wantLeft && h == 1;
                nr = p->wantRight && h == 2;
            } else {
                int r = rnd(0, 20);
                nl = p->wantLeft && (tx < px || r == 0);
                nr = p->wantRight && (tx > px || r == 1);
                nu = p->wantUp && (ty < py || r == 2);
                nd = p->wantDown && (ty > py || r == 3);
            }
        } else if (toTarget == 1) {
            nu = p->wantUp && ty < py;
            nd = p->wantDown && ty > py;
            nl = p->wantLeft && tx < px;
            nr = p->wantRight && tx > px;
        } else {
            nu = p->wantUp && ty > py;
            nd = p->wantDown && ty < py;
            nl = p->wantLeft && tx > px;
            nr = p->wantRight && tx < px;
        }
        if (!(nd || nu || nr || nl)) {
            if (p->wantUp && p->wantDown) {
                int r = rnd(0, 1);
                nd = r & 0xff ? 1 : 0;
                nu = (r & 0xff) == 0;
                nr = 0;
                nl = 0;
            } else if (p->wantLeft && p->wantRight) {
                int r = rnd(0, 1);
                nl = r & 0xff ? 1 : 0;
                nr = (r & 0xff) == 0;
                nu = 0;
                nd = 0;
            }
        }
        if (s->u.gh.state == 2 && s->u.gh.reachedTarget && diff <= 2) {
            int m = diff == 1 ? 2 : 26;
            if (rnd(0, m) == 1) {
                int r = rnd(0, 3), cnt = 0;
                nr = 0;
                nl = 0;
                if (p->wantLeft) { nl = r == 0; cnt = 1; }
                if (p->wantRight) { nr = cnt == r; cnt++; }
                nu = 0;
                if (p->wantUp) { nu = cnt == r; cnt++; }
                nd = p->wantDown && cnt == r;
            }
        }
        p->wantLeft = nl;
        p->wantRight = nr;
        p->wantUp = nu;
        p->wantDown = nd;
        if (diff >= 3 && (p->wantLeft || p->wantRight) && (p->wantUp || p->wantDown)) {
            int dy = abs(ty - py), dx = abs(tx - px);
            if (dx < dy) { p->wantLeft = 0; p->wantRight = 0; }
            else { p->wantUp = 0; p->wantDown = 0; }
        }
    } else if (p->directions == 2 && s->u.gh.state == 2 && rnd(0, 100) == 0) {
        p->wantDown = p->wantUp = p->wantRight = p->wantLeft = 0;
        if (spr_moving_left(s)) p->wantRight = 1;
        else if (spr_moving_right(s)) p->wantLeft = 1;
        else if (spr_moving_up(s)) p->wantDown = 1;
        else if (spr_moving_down(s)) p->wantUp = 1;
    }

    /* 251a8: turn into the wanted direction */
    if (!p->wantLeft && !p->wantRight && !p->wantUp && !p->wantDown) {
        check_directions(s, 0);
        if (!p->wantLeft && !p->wantRight && !p->wantUp && !p->wantDown) {
            p->wantLeft = p->canLeft;
            p->wantRight = p->canRight;
            p->wantUp = p->canUp;
            p->wantDown = p->canDown;
        }
    }
    float speed = diff > 1 ? 0.9f : 0.8f;
    int st = s->u.gh.state;
    int done = 0;
    if (p->wantLeft && p->canLeft) {
        if (st == 2 && tile_x(s) == 0) speed = speed * 0.8f;
        spr_set_speed(s, -speed, 0);
        done = 1;
    } else if (p->wantRight && p->canRight) {
        if (st == 2 && tile_x(s) == 12) speed = speed * 0.8f;
        spr_set_speed(s, speed, 0);
        done = 1;
    } else if (p->wantUp && p->canUp) {
        if (st == 2 && tile_y(s) == 0) speed = speed * 0.8f;
        spr_set_speed(s, 0, -speed);
        done = 1;
    } else if (p->wantDown && p->canDown) {
        if (st == 2 && tile_y(s) == 9) speed = speed * 0.8f;
        spr_set_speed(s, 0, speed);
        done = 1;
    }
    if (done) ghost_set_frameset(s);
    s->x = (float)spr_px(s);
    s->y = (float)spr_py(s);
    p->changedDirection = 0;
}

static void ghost_move(Sprite *s) {
    if (!game_moving_allowed()) return;
    s->color[3] = 1.0f;
    s->visible = 1;
    switch (s->u.gh.state) {
    case 1:
        for (int i = 0; i < 10; i++) ghost_wait(s);
        break;
    case 2:
        for (int i = 0; i < 10; i++) ghost_move_in_level(s);
        break;
    case 3:
        for (int i = 0; i < 5; i++) ghost_move_in_level(s);
        ghost_check_running_state(s);
        break;
    case 4:
        for (int i = 0; i <= 31; i++) {
            ghost_move_in_level(s);
            if (s->u.gh.state != 4) break;
        }
        break;
    case 5:
        for (int i = 0; i < 5; i++) ghost_wait(s);
        ghost_check_running_state(s);
        break;
    }
    s->pm.changedDirection++;
}

void ghost_start_hunting(Sprite *s) {
    s->u.gh.state = 2;
    s->u.gh.reachedTarget = 0;
}

void ghost_start_running_away(Sprite *s) {
    int st = s->u.gh.state;
    if (st == 4) return;
    if (st == 5) {
        s->u.gh.state = 3;
        spr_set_frame_range(s, 0, 3);
        return;
    }
    s->u.gh.runaway = (float)(game_next_ghost_time() * FPS);
    if (game_player_nr() != 0) s->u.gh.runaway = s->u.gh.runaway * 1.5f;
    s->u.gh.blink = s->u.gh.runaway * 0.3f;
    if (st == 2 && s->pm.changedDirection >= 4) {
        s->hSpeed = -s->hSpeed;
        s->vSpeed = -s->vSpeed;
    }
    st = s->u.gh.state;
    s->u.gh.reachedTarget = st == 2;
    s->u.gh.state = st == 1 ? 5 : 3;
    ghost_set_frameset(s);
    spr_set_frame_range(s, 0, 3);
}

void ghost_start_going_home(Sprite *s) {
    s->u.gh.state = 4;
    s->u.gh.reachedTarget = 0;
    ghost_set_frameset(s);
}

void ghost_send_back_to_start(Sprite *s) {
    if (s->u.gh.ghostNr != 0) {
        spr_move_to(s, (float)s->pm.startX, (float)s->pm.startY);
        spr_set_speed(s, 0, 1.0f);
        s->u.gh.state = 1;
        s->u.gh.reachedTarget = 0;
    } else {
        spr_move_to(s, (float)s->pm.startX, (float)(s->pm.startY - 44));
        if (sprite_direction_allowed(s, 1)) spr_set_speed(s, -1.0f, 0);
        else spr_set_speed(s, 0, -1.0f);
        s->u.gh.state = 2;
        s->u.gh.reachedTarget = 1;
    }
    s->pm.changedDirection = 0;
    ghost_set_frameset(s);
}

/* ================================================================== PMExtra */

void extra_start(Sprite *s, int nr, Sprite *arrow) {
    spr_set_frameset(s, fs_extra);
    spr_set_frame_index(s, nr * 3 + 1);
    spr_set_rotation_speed(s, 3.0f);
    s->u.ex.timer = FPS * 10;
    s->u.ex.extraNr = nr;
    s->u.ex.arrow = spr_ref(arrow);
}

static void extra_move_in_level(Sprite *s) {
    PMSpriteData *p = &s->pm;
    p->changedDirection++;
    if (!(on_branch_h(s) && on_branch_v(s) && p->changedDirection > 4)) {
        spr_move(s);
        check_bounds(s);
        return;
    }
    check_directions(s, 0);
    if (p->directions >= 3) {
        int nl = 0, nr = 0, nu = 0, nd = 0;
        if (p->wantLeft) nl = rnd(0, 1) != 0;
        if (p->wantRight) nr = rnd(0, 1) != 0;
        if (p->wantUp) nu = rnd(0, 1) != 0;
        if (p->wantDown) nd = rnd(0, 1) != 0;
        if (!nl && !nr && !nu && !nd) {
            if (p->wantUp && p->wantDown) {
                int r = rnd(0, 1);
                nd = r;
                nu = r == 0;
                nr = 0;
                nl = 0;
            } else if (p->wantLeft && p->wantRight) {
                int r = rnd(0, 1);
                nl = r;
                nr = r == 0;
                nu = 0;
                nd = 0;
            }
        }
        p->wantLeft = nl;
        p->wantRight = nr;
        p->wantUp = nu;
        p->wantDown = nd;
    }
    if (!p->wantLeft && !p->wantRight && !p->wantUp && !p->wantDown) {
        check_directions(s, 0);
        if (!p->wantLeft && !p->wantRight && !p->wantUp && !p->wantDown) {
            p->wantLeft = p->canLeft;
            p->wantRight = p->canRight;
            p->wantUp = p->canUp;
            p->wantDown = p->canDown;
        }
    }
    float h = 0, v = 0;
    if (p->wantLeft && p->canLeft) h = -1.0f;
    else if (p->wantRight && p->canRight) h = 1.0f;
    else if (p->wantUp && p->canUp) v = -1.0f;
    else if (p->wantDown) v = p->canDown ? 1.0f : 0.0f;
    spr_set_speed(s, h, v);
    p->changedDirection = 0;
    spr_move(s);
    check_bounds(s);
}

static void extra_move(Sprite *s) {
    if (s->u.ex.timer > 0) {
        if (--s->u.ex.timer <= 0) {
            spr_delete(s);
            game_sprite_pops(s, 0);
            return;
        }
    }
    float a = s->angle, rs = s->rotationSpeed;
    if (rs > 0.0001f && a >= 25.0f && 335.0f > a) spr_set_rotation_speed(s, -3.0f);
    else if (rs < -0.0001f && 335.0f >= a && a > 25.0f) spr_set_rotation_speed(s, 3.0f);
    for (int i = 0; i < 2; i++) extra_move_in_level(s);
    Sprite *ar = spr_get(s->u.ex.arrow);
    if (ar) spr_move_to(ar, s->x, s->y + -16.0f);
}

/* ================================================================== PMChar */

/* the glyph for a character; *accent gets 1 umlaut, 2 acute, 3 grave */
int char_glyph(int c, int *accent) {
    *accent = 0;
    switch (c) {
    case 0xE4: case 0xC4: c = 'A'; *accent = 1; break;
    case 0xF6: case 0xD6: c = 'O'; *accent = 1; break;
    case 0xFC: case 0xDC: c = 'U'; *accent = 1; break;
    case 0xE1: case 0xC1: c = 'A'; *accent = 2; break;
    case 0xE9: case 0xC9: c = 'E'; *accent = 2; break;
    case 0xED: case 0xCD: c = 'I'; *accent = 2; break;
    case 0xFA: case 0xDA: c = 'U'; *accent = 2; break;
    case 0xE0: case 0xC0: c = 'A'; *accent = 3; break;
    case 0xE8: case 0xC8: c = 'E'; *accent = 3; break;
    case 0xF9: case 0xD9: c = 'U'; *accent = 3; break;
    }
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c >= '0' && c <= '9') return c - '0';
    switch (c) {
    case '!': return 46;
    case '"': return 53;
    case '#': return 45;
    case '$': case '%': case '&': return 47;
    case '\'': return 52;
    case '(': return 42;
    case ')': return 43;
    case '*': return 44;
    case '+': return 38;
    case ',': return 49;
    case '-': return 37;
    case '.': return 48;
    case '/': return 40;
    case ':': return 50;
    case ';': return 51;
    case '=': return 39;
    case '_': return 36;
    case '|': return 41;
    }
    return 47;
}

void char_move_to_start(Sprite *s) {
    s->u.ch.targetX = spr_px(s);
    s->u.ch.targetY = spr_py(s);
    int x = s->u.ch.targetX, y = s->u.ch.targetY;
    if (x <= 319) { s->x = (float)(x + -300); spr_set_hspeed(s, 0.5f); }
    else { s->x = (float)(x + 300); spr_set_hspeed(s, -0.5f); }
    if (y <= 239) { s->y = (float)(y + -300); spr_set_vspeed(s, 0.5f); }
    else { s->y = (float)(y + 300); spr_set_vspeed(s, -0.5f); }
}

static void char_move(Sprite *s) {
    for (int i = 0; i < 50; i++) {
        spr_move(s);
        if (spr_px(s) == s->u.ch.targetX) spr_set_hspeed(s, 0);
        if (spr_py(s) == s->u.ch.targetY) spr_set_vspeed(s, 0);
    }
}

/* ================================================================== PMDigit, PMHeart, PMPoints */

void digit_start(Sprite *s, int digitNr, int playerNr, const int *value) {
    s->u.dg.digitNr = digitNr;
    s->u.dg.playerNr = playerNr;
    s->u.dg.value = value;
    if (playerNr == 2) spr_set_frameset_name(s, "font1");
    else if (playerNr == 1) spr_set_frameset_name(s, "font0");
    else if (playerNr == 0) spr_set_frameset_name(s, "font2");
    s->y = 7.0f;
    actor_move(s);
}

static void digit_move(Sprite *s) {
    int gp = game_player_nr(), pn = s->u.dg.playerNr;
    if (gp > 0 && pn > 0 && gp != pn) { s->visible = 0; return; }
    int v = *s->u.dg.value + (pn == 0);
    s->visible = 1;
    int d = s->u.dg.digitNr, x = 0;
    if (pn == 1) {
        x = 106 - 16 * d;
        static const int th[7] = {99, 999, 9999, 99999, 999999, 9999999, 99999999};
        for (int i = 0; i < 7; i++) if (v > th[i]) x += 16;
    } else if (pn == 2) {
        x = 534 - 16 * d;
    } else if (pn == 0) {
        x = (v < 10 ? 312 : 320) - 16 * d;
    }
    int div = 1;
    if (d >= 1 && d <= 8) {
        for (int i = 0; i < d; i++) div *= 10;
        if (v <= div - 1) s->visible = 0;
    }
    s->x = (float)x;
    spr_set_frame_index(s, (v / div) % 10);
}

void heart_start(Sprite *s, int number, const int *value) {
    s->u.ht.number = number;
    s->u.ht.value = value;
    spr_set_frameset_name(s, "heart");
    s->visible = 0;
}

void points_start(Sprite *s, int digit, int color, int position) {
    spr_set_frameset_index(s, fs_points[color], digit);
    spr_set_duration(s, 1.5f);
    s->u.pt.moveTimer = position + position;
    s->u.pt.targetY = spr_py(s) + 6;
    s->y = (float)(spr_py(s) + 10);
}

static void points_move(Sprite *s) {
    if (s->u.pt.reachedTarget) return;
    if (s->u.pt.moveTimer >= 0) {
        if (s->u.pt.moveTimer == 0) spr_set_vspeed(s, -14.0f);
        s->u.pt.moveTimer--;
        return;
    }
    spr_move(s);
    if (spr_moving_down(s) && spr_py(s) > s->u.pt.targetY) {
        spr_stop(s);
        s->y = (float)s->u.pt.targetY;
        s->u.pt.reachedTarget = 1;
        return;
    }
    spr_inc_vspeed(s, 1.0f);
}

/* ================================================================== PMButton */

void button_press(Sprite *s) {
    if (s->u.bt.hiliteTimer != 0) return;
    s->u.bt.hiliteTimer = (int)(FPS * 0.2f);
    spr_set_color(s, 0.4f, 0.4f, 1.0f, 1.0f);
}

static void button_unpress(Sprite *s) {
    static const int ev[6] = {1001, 1002, 1000, 1005, 1007, 1006};
    s->u.bt.hiliteTimer = 0;
    spr_set_color(s, 1, 1, 1, 1);
    if (s->frameIndex >= 0 && s->frameIndex <= 5) eng_post(ev[s->frameIndex], 0, 0);
}

static void button_move(Sprite *s) {
    if (s->u.bt.hiliteTimer > 0 && --s->u.bt.hiliteTimer == 0) button_unpress(s);
}

/* ================================================================== dispatch */

void actor_move(Sprite *s) {
    switch (s->kind) {
    case K_PLAYER: player_move(s); break;
    case K_GHOST: ghost_move(s); break;
    case K_EXTRA: extra_move(s); break;
    case K_CHAR: char_move(s); break;
    case K_DIGIT: digit_move(s); break;
    case K_HEART: s->visible = s->u.ht.number < *s->u.ht.value; break;
    case K_POINTS: points_move(s); break;
    case K_BUTTON: button_move(s); break;
    default: spr_move(s); break;
    }
}

void actor_collide(Sprite *s, Sprite *t) {
    if (s->kind == K_PLAYER) player_collide(s, t);
}

void actor_animation_finished(Sprite *s) {
    if (s->kind == K_PLAYER && s->u.pl.dying > 0 && s->visible) {
        s->visible = 0;
        game_player_dying_completed(s);
    }
}

void actor_timer(Sprite *s) { (void)s; }

void actor_deleted(Sprite *s) {
    if (s->kind == K_EXTRA) {
        Sprite *a = spr_get(s->u.ex.arrow);
        if (a) a->mustBeDeleted = 1;
    }
}
