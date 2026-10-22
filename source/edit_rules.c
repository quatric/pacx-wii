/*
 * updatePaths:erasing:, moveCitadel, togglePellet and setPlayer of the Mac editor, including
 * its quirks; only reads outside the grid, which raised exceptions there, are guarded.
 */
#include <string.h>
#include "edit_rules.h"

static int bits_of(char letter) {
    int t = letter - 'A';
    return (t >= 0 && t <= 15) ? t : 0;
}

static char letter_for(int top, int bottom, int left, int right) {
    return 'A' + (left ? 1 : 0) + (right ? 2 : 0) + (top ? 4 : 0) + (bottom ? 8 : 0);
}

static char tile(const Level *l, int c, int r) {
    if (c < 0 || c >= LV_W || r < 0 || r >= LV_H) return 'A';
    return l->tiles[r][c];
}

static int top_open(const Level *l, int c, int r) { return (bits_of(tile(l, c, r)) & 4) != 0; }
static int bottom_open(const Level *l, int c, int r) { return (bits_of(tile(l, c, r)) & 8) != 0; }
static int left_open(const Level *l, int c, int r) { return (bits_of(tile(l, c, r)) & 1) != 0; }
static int right_open(const Level *l, int c, int r) { return (bits_of(tile(l, c, r)) & 2) != 0; }

static void put(Level *l, int c, int r, char t) {
    if (c < 0 || c >= LV_W || r < 0 || r >= LV_H) return;
    l->tiles[r][c] = t;
}

/* setTileAtColumn:row:toLetter: the citadel cannot be painted over */
static void set_tile(Level *l, int c, int r, char t) {
    char o = tile(l, c, r);
    if (o == 'Q' || o == 'R') return;
    put(l, c, r, t);
}

/* validateNextColumn:row: a path may not lead into the citadel */
static int validate(const Level *l, int c, int r, int mod) {
    if (mod) return 1;
    if (c < 0 || c >= LV_W || r < 0 || r >= LV_H) return 1;
    return tile(l, c, r) != 'Q';
}

int ed_can_place(const Level *l, int c, int r, int mod) {
    if (mod) return 1;
    char t = tile(l, c, r);
    return !(t == 'A' || t == 'Q' || t == 'R' || t == 'S');
}

/* ================================================================== updatePaths:erasing: */

#define TOP(c, r) top_open(l, c, r)
#define BOTTOM(c, r) bottom_open(l, c, r)
#define LEFT(c, r) left_open(l, c, r)
#define RIGHT(c, r) right_open(l, c, r)

void ed_update_paths(Level *l, const EdDrag *d, int erasing, int mod) {
    int minC = d->startCol < d->endCol ? d->startCol : d->endCol;
    int maxC = d->startCol < d->endCol ? d->endCol : d->startCol;
    int minR = d->startRow < d->endRow ? d->startRow : d->endRow;
    int maxR = d->startRow < d->endRow ? d->endRow : d->startRow;
    int pokeHole = d->pokeHole;
    if (d->vertical && erasing) {
        int c = minC;
        for (int r = minR; r <= maxR; r++) {
            int T = bits_of(tile(l, c, r));
            if (!mod) {
                /* the path that wrapped around the screen goes as well */
                if (r == 9) {
                    if (T & 8) set_tile(l, c, 0, letter_for(0, BOTTOM(c, 0), LEFT(c, 0), RIGHT(c, 0)));
                } else if (r == 0) {
                    if (T & 4) set_tile(l, c, 9, letter_for(TOP(c, 9), 0, LEFT(c, 9), RIGHT(c, 9)));
                }
            }
            set_tile(l, c, r, 'A');
            if ((T & 1) && c > 0)
                set_tile(l, c - 1, r, letter_for(TOP(c - 1, r), BOTTOM(c - 1, r), LEFT(c - 1, r), 0));
            if ((T & 2) && c <= 11)
                set_tile(l, c + 1, r, letter_for(TOP(c + 1, r), BOTTOM(c + 1, r), 0, RIGHT(c + 1, r)));
            /* the original tests the first row of the range here, not r */
            if ((T & 4) && minR > 0)
                set_tile(l, c, r - 1, letter_for(TOP(c, r - 1), 0, LEFT(c, r - 1), RIGHT(c, r - 1)));
            if ((T & 8) && minR <= 8)
                set_tile(l, c, r + 1, letter_for(0, BOTTOM(c, r + 1), LEFT(c, r + 1), RIGHT(c, r + 1)));
        }
    } else if (d->vertical) {
        int c = minC;
        for (int r = minR; r <= maxR; r++) {
            int top = r > minR ? 1 : TOP(c, r);
            int bottom = r < maxR ? 1 : BOTTOM(c, r);
            int vt = top ? validate(l, c, r - 1, mod) : 0;
            if (pokeHole && r == 0) vt = 1;
            int vb = bottom ? validate(l, c, r + 1, mod) : 0;
            if (pokeHole && r == 9) vb = 1;
            set_tile(l, c, r, letter_for(vt, vb, LEFT(c, r), RIGHT(c, r)));
            if (pokeHole && r == 0) set_tile(l, c, 9, letter_for(TOP(c, 9), 1, LEFT(c, 9), RIGHT(c, 9)));
            if (pokeHole && r == 9) set_tile(l, c, 0, letter_for(1, BOTTOM(c, 0), LEFT(c, 0), RIGHT(c, 0)));
        }
    } else if (erasing) {
        int r = minR;
        for (int c = minC; c <= maxC; c++) {
            int T = bits_of(tile(l, c, r));
            set_tile(l, c, r, 'A');
            if (!mod) {
                if (c == 12) {
                    if (T & 2) set_tile(l, 0, r, letter_for(TOP(0, r), BOTTOM(0, r), 0, RIGHT(0, r)));
                } else if (c == 0) {
                    if (T & 1) set_tile(l, 12, r, letter_for(TOP(12, r), BOTTOM(12, r), LEFT(12, r), 0));
                }
            }
            /* the original tests the first column of the range here, not c */
            if ((T & 1) && minC > 0)
                set_tile(l, c - 1, r, letter_for(TOP(c - 1, r), BOTTOM(c - 1, r), LEFT(c - 1, r), 0));
            if ((T & 2) && minC <= 11)
                set_tile(l, c + 1, r, letter_for(TOP(c + 1, r), BOTTOM(c + 1, r), 0, RIGHT(c + 1, r)));
            if ((T & 4) && r > 0)
                set_tile(l, c, r - 1, letter_for(TOP(c, r - 1), 0, LEFT(c, r - 1), RIGHT(c, r - 1)));
            if ((T & 8) && r <= 8)
                set_tile(l, c, r + 1, letter_for(0, BOTTOM(c, r + 1), LEFT(c, r + 1), RIGHT(c, r + 1)));
        }
    } else {
        int r = minR;
        for (int c = minC; c <= maxC; c++) {
            int left = c > minC ? 1 : LEFT(c, r);
            int right = c < maxC ? 1 : RIGHT(c, r);
            int vl = left ? validate(l, c - 1, r, mod) : 0;
            if (pokeHole && c == 0) vl = 1;
            int vr = right ? validate(l, c + 1, r, mod) : 0;
            if (pokeHole && c == 12) vr = 1;
            set_tile(l, c, r, letter_for(TOP(c, r), BOTTOM(c, r), vl, vr));
            /* the original passes the top side of (12, r) as its left side */
            if (pokeHole && c == 0) set_tile(l, 12, r, letter_for(TOP(12, r), BOTTOM(12, r), TOP(12, r), 1));
            if (pokeHole && c == 12) set_tile(l, 0, r, letter_for(TOP(0, r), BOTTOM(0, r), 1, RIGHT(0, r)));
        }
    }
}

/* ================================================================== items */

int ed_is_super_pellet(const Level *l, int c, int r) {
    for (int i = 0; i < l->nsp; i++)
        if (l->sp[i][0] == c && l->sp[i][1] == r) return 1;
    return 0;
}

int ed_toggle_pellet(Level *l, int c, int r, int mod) {
    for (int i = 0; i < l->nsp; i++)
        if (l->sp[i][0] == c && l->sp[i][1] == r) {
            memmove(&l->sp[i], &l->sp[i + 1], sizeof l->sp[0] * (l->nsp - i - 1));
            l->nsp--;
            return 1;
        }
    if (!ed_can_place(l, c, r, mod)) return 0;
    l->sp[l->nsp][0] = c;
    l->sp[l->nsp][1] = r;
    l->nsp++;
    if (l->nsp > 10) {
        memmove(&l->sp[0], &l->sp[1], sizeof l->sp[0] * (l->nsp - 1));
        l->nsp--;
    }
    return 1;
}

int ed_set_player(Level *l, int nr, int c, int r, int mod) {
    if (!ed_can_place(l, c, r, mod)) return 0;
    if (nr == 1) { l->p1x = c; l->p1y = r; }
    else { l->p2x = c; l->p2y = r; }
    return 1;
}

/* the cell of the citadel entry R, as the game finds it (the last row with an R) */
int ed_find_citadel(const Level *l, int *cc, int *rr) {
    int found = 0;
    for (int r = 0; r < LV_H; r++)
        for (int c = 0; c < LV_W; c++)
            if (tile(l, c, r) == 'R') {
                *cc = c - 1;
                *rr = r;
                found = 1;
                break;
            }
    return found;
}

void ed_move_citadel(Level *l, int cc, int rr) {
    if (cc < 0) cc = 0;
    if (cc > 10) cc = 10;
    if (rr < 1) rr = 1;
    if (rr > 8) rr = 8;
    int oc, orr;
    if (ed_find_citadel(l, &oc, &orr)) {
        for (int i = 0; i < 3; i++) {
            put(l, oc + i, orr, 'A');
            put(l, oc + i, orr + 1, 'A');
        }
        put(l, oc + 1, orr - 1, letter_for(TOP(oc + 1, orr - 1), 0, LEFT(oc + 1, orr - 1), RIGHT(oc + 1, orr - 1)));
    }
    for (int i = 0; i < 3; i++) {
        put(l, cc + i, rr, i == 1 ? 'R' : 'Q');
        put(l, cc + i, rr + 1, 'Q');
    }
    put(l, cc + 1, rr - 1, TOP(cc + 1, rr - 1) ? 'P' : 'L');
    if (cc > 0)
        for (int k = 0; k < 2; k++)
            put(l, cc - 1, rr + k, letter_for(TOP(cc - 1, rr + k), BOTTOM(cc - 1, rr + k), LEFT(cc - 1, rr + k), 0));
    if (cc <= 9)
        for (int k = 0; k < 2; k++)
            put(l, cc + 3, rr + k, letter_for(TOP(cc + 3, rr + k), BOTTOM(cc + 3, rr + k), 0, RIGHT(cc + 3, rr + k)));
    put(l, cc, rr - 1, letter_for(TOP(cc, rr - 1), 0, LEFT(cc, rr - 1), 1));
    put(l, cc + 2, rr - 1, letter_for(TOP(cc + 2, rr - 1), 0, 1, RIGHT(cc + 2, rr - 1)));
    if (rr <= 7)
        for (int i = 0; i < 3; i++)
            put(l, cc + i, rr + 2, letter_for(0, BOTTOM(cc + i, rr + 2), LEFT(cc + i, rr + 2), RIGHT(cc + i, rr + 2)));
}

/* ================================================================== geometry */

int ed_cell_at(float px, float py, int *c, int *r) {
    int cc = (int)((px - ED_VX) / ED_CELL), rr = (int)((py - ED_VY) / ED_CELL);
    int inside = px >= ED_VX && px < ED_VX + LV_W * ED_CELL && py >= ED_VY && py < ED_VY + LV_H * ED_CELL;
    if (cc < 0) cc = 0;
    if (cc > LV_W - 1) cc = LV_W - 1;
    if (rr < 0) rr = 0;
    if (rr > LV_H - 1) rr = LV_H - 1;
    *c = cc;
    *r = rr;
    return inside;
}

/* mouseUp: with the citadel tool: the citadel is centred on the point */
void ed_citadel_cell(float px, float py, int *cc, int *rr) {
    float x = px - ED_VX - ED_CELL, y = (py - ED_VY) - ED_CELL / 2;
    if (x < 0) x = 0;
    if (x > 439) x = 439;
    if (y < 44) y = 44;
    if (y > 395) y = 395;
    *cc = (int)(x / ED_CELL);
    *rr = (int)(y / ED_CELL);
    if (*cc > 10) *cc = 10;
    if (*rr < 1) *rr = 1;
    if (*rr > 8) *rr = 8;
}

void ed_blank_level(Level *l) {
    for (int r = 0; r < LV_H; r++) {
        memset(l->tiles[r], 'A', LV_W);
        l->tiles[r][LV_W] = 0;
    }
    l->nrows = LV_H;
    l->nsp = 0;
}
