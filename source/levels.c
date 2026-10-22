#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "levels.h"
#include "plist.h"
#include "pack.h"
#include "settings.h"

static void xy(const PlNode *d, int *x, int *y) {
    /* valueForKey on a missing dictionary gives nil, and [nil intValue] is 0 */
    *x = (int)pl_int(pl_get(d, "x"), 0);
    *y = (int)pl_int(pl_get(d, "y"), 0);
}

static void read_level(Level *l, const PlNode *d) {
    memset(l, 0, sizeof *l);
    for (int r = 0; r < LV_H; r++) {
        memset(l->tiles[r], 'A', LV_W);
        l->tiles[r][LV_W] = 0;
    }
    const PlNode *t = pl_get(d, "tiles");
    int rows = pl_count(t);
    for (int r = 0; r < rows && r < LV_H; r++) {
        const char *s = pl_str(pl_at(t, r), "");
        if (strlen(s) > LV_W) break;
        for (int c = 0; c < LV_W && s[c]; c++) l->tiles[r][c] = s[c];
        l->nrows = r + 1;
    }
    xy(pl_get(d, "position1"), &l->p1x, &l->p1y);
    xy(pl_get(d, "position2"), &l->p2x, &l->p2y);
    const PlNode *sp = pl_get(d, "super_pellets");
    for (int i = 0; i < pl_count(sp) && l->nsp < LV_MAX_SP; i++) {
        xy(pl_at(sp, i), &l->sp[l->nsp][0], &l->sp[l->nsp][1]);
        l->nsp++;
    }
    const PlNode *c = pl_get(d, "color");
    l->red = (int)pl_int(pl_get(c, "red"), 0);
    l->green = (int)pl_int(pl_get(c, "green"), 0);
    l->blue = (int)pl_int(pl_get(c, "blue"), 0);
    l->wait = (int)pl_int(pl_get(d, "wait_time"), 0);
    snprintf(l->background, sizeof l->background, "%s", pl_str(pl_get(d, "background"), ""));
    snprintf(l->tileset, sizeof l->tileset, "%s", pl_str(pl_get(d, "tileset"), "tile"));
    snprintf(l->citadel, sizeof l->citadel, "%s", pl_str(pl_get(d, "citadel"), "citadel"));
    l->item = (int)pl_int(pl_get(d, "item"), 0);
}

int levels_parse(LevelSet *set, const char *xml, int len) {
    memset(set, 0, sizeof *set);
    PlNode *root = pl_parse(xml, len);
    if (!root || root->type != PL_ARRAY) { pl_free(root); return 0; }
    int n = pl_count(root);
    set->lv = calloc(n > 0 ? n : 1, sizeof(Level));
    set->cap = n > 0 ? n : 1;
    for (PlNode *c = root->child; c; c = c->next) {
        if (c->type != PL_DICT) continue;
        read_level(&set->lv[set->n++], c);
    }
    pl_free(root);
    return set->n > 0;
}

int levels_load_pack(LevelSet *set, const char *name) {
    const PackEntry *e = pack_find(name);
    if (!e) return 0;
    return levels_parse(set, pack_data(e), e->size);
}

int levels_load_file(LevelSet *set, const char *path) {
    int len;
    char *b = file_read(path, &len);
    if (!b) return 0;
    int ok = levels_parse(set, b, len);
    free(b);
    return ok;
}

static PlNode *pos(int x, int y) {
    PlNode *d = pl_new(PL_DICT);
    pl_set(d, "x", pl_new_int(x));
    pl_set(d, "y", pl_new_int(y));
    return d;
}

char *levels_write(const LevelSet *set, int *len) {
    PlNode *root = pl_new(PL_ARRAY);
    for (int i = 0; i < set->n; i++) {
        const Level *l = &set->lv[i];
        PlNode *d = pl_new(PL_DICT);
        pl_set(d, "background", pl_new_str(l->background));
        pl_set(d, "citadel", pl_new_str(l->citadel));
        PlNode *c = pl_new(PL_DICT);
        pl_set(c, "blue", pl_new_int(l->blue));
        pl_set(c, "green", pl_new_int(l->green));
        pl_set(c, "red", pl_new_int(l->red));
        pl_set(d, "color", c);
        pl_set(d, "item", pl_new_int(i));
        pl_set(d, "position1", pos(l->p1x, l->p1y));
        pl_set(d, "position2", pos(l->p2x, l->p2y));
        PlNode *sp = pl_new(PL_ARRAY);
        for (int k = 0; k < l->nsp; k++) pl_append(sp, pos(l->sp[k][0], l->sp[k][1]));
        pl_set(d, "super_pellets", sp);
        PlNode *t = pl_new(PL_ARRAY);
        for (int r = 0; r < LV_H; r++) pl_append(t, pl_new_str(l->tiles[r]));
        pl_set(d, "tiles", t);
        pl_set(d, "tileset", pl_new_str(l->tileset));
        pl_set(d, "wait_time", pl_new_int(l->wait));
        pl_append(root, d);
    }
    char *x = pl_write(root, len);
    pl_free(root);
    return x;
}

int levels_save_file(const LevelSet *set, const char *path) {
    int len;
    char *x = levels_write(set, &len);
    int ok = file_write(path, x, len);
    free(x);
    return ok;
}

void levels_free(LevelSet *set) {
    free(set->lv);
    memset(set, 0, sizeof *set);
}

void levels_copy(LevelSet *dst, const LevelSet *src) {
    levels_free(dst);
    dst->cap = src->n > 0 ? src->n : 1;
    dst->lv = malloc(sizeof(Level) * dst->cap);
    memcpy(dst->lv, src->lv, sizeof(Level) * src->n);
    dst->n = src->n;
}

Level *levels_insert(LevelSet *set, int at) {
    if (set->n == set->cap) {
        set->cap = set->cap ? set->cap * 2 : 8;
        set->lv = realloc(set->lv, sizeof(Level) * set->cap);
    }
    if (at < 0) at = 0;
    if (at > set->n) at = set->n;
    memmove(&set->lv[at + 1], &set->lv[at], sizeof(Level) * (set->n - at));
    set->n++;
    memset(&set->lv[at], 0, sizeof(Level));
    return &set->lv[at];
}

void levels_remove(LevelSet *set, int at) {
    if (at < 0 || at >= set->n) return;
    memmove(&set->lv[at], &set->lv[at + 1], sizeof(Level) * (set->n - at - 1));
    set->n--;
}

int citadel_number(const char *name) {
    if (!strcmp(name, "citadel2")) return 2;
    if (!strcmp(name, "citadel3")) return 3;
    if (!strcmp(name, "citadel4")) return 4;
    return 1;
}

/* the 11x11 tile parts of one maze cell, in the order buildLevel creates them */
void maze_cell_parts(int t, void (*emit)(void *u, int frame, int xo, int yo), void *u) {
    if (t == 0) {
        for (int xo = 0; xo < 4; xo++)
            for (int yo = 0; yo < 4; yo++) emit(u, 14, xo, yo);
        return;
    }
    int L = t & 1, R = t & 2, U = t & 4, D = t & 8;
    if (L) {
        emit(u, U ? 13 : 1, 0, 0);
        emit(u, D ? 10 : 7, 0, 3);
    } else {
        emit(u, 3, 0, 1);
        emit(u, 3, 0, 2);
        if (!U) emit(u, 0, 0, 0);
        if (!D) emit(u, 6, 0, 3);
    }
    if (R) {
        emit(u, U ? 12 : 1, 3, 0);
        emit(u, D ? 9 : 7, 3, 3);
    } else {
        emit(u, 5, 3, 1);
        emit(u, 5, 3, 2);
        if (!U) emit(u, 2, 3, 0);
        if (!D) emit(u, 8, 3, 3);
    }
    if (!U) {
        emit(u, 1, 1, 0);
        emit(u, 1, 2, 0);
    }
    if (!D) {
        emit(u, 7, 1, 3);
        emit(u, 7, 2, 3);
    }
    if (U) {
        if (!L) emit(u, 3, 0, 0);
        if (!R) emit(u, 5, 3, 0);
    }
    if (D) {
        if (!L) emit(u, 3, 0, 3);
        if (!R) emit(u, 5, 3, 3);
    }
}
