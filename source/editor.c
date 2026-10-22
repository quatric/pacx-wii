/*
 * The Wii level editor: the maze is edited where the game draws it, with the pointer. The
 * editing rules of the Mac editor are in edit_rules.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "editor.h"
#include "ui.h"
#include "uimenu.h"
#include "gfx.h"
#include "engine.h"
#include "game.h"
#include "input.h"
#include "audio.h"
#include "settings.h"
#include "levels.h"
#include "edit_rules.h"
#include "pack.h"

/* the maze view of the editor (572x440) sits where the game draws the maze */
#define VX 34
#define VY 36
#define CELL 44

enum { T_PATH, T_ERASE, T_PELLET, T_PLAYER1, T_PLAYER2, T_CITADEL, T_COUNT };

static int active;
static LevelSet set;
static int cur;
static char file[64];           /* "" for the standard levels, which are saved under a new name */
static int dirty;
static int tool;

/* the mouse drag */
static int dragging, startCol, startRow, endCol, endRow, dirSet, vertical, pokeHole;
static float startX, startY, dragX, dragY;

static char message[64];
static int messageTimer;

#define UNDO_MAX 48
static struct { int lv; Level l; } undo[UNDO_MAX];
static int undoCount;

static Level *lv(void) { return &set.lv[cur]; }

static void say(const char *m) {
    snprintf(message, sizeof message, "%s", m);
    messageTimer = 2 * FPS;
}

/* prepareTo: */
static void prepare(void) {
    if (undoCount == UNDO_MAX) {
        memmove(&undo[0], &undo[1], sizeof undo[0] * (UNDO_MAX - 1));
        undoCount--;
    }
    undo[undoCount].lv = cur;
    undo[undoCount].l = *lv();
    undoCount++;
    dirty = 1;
}

static void do_undo(void) {
    if (!undoCount) return;
    undoCount--;
    if (undo[undoCount].lv < set.n) {
        cur = undo[undoCount].lv;
        set.lv[cur] = undo[undoCount].l;
    }
    dirty = 1;
}

/* ================================================================== the rules (edit_rules.c) */

static int modifier(void) { return in_shift(); }

static int can_place(int c, int r) { return ed_can_place(lv(), c, r, modifier()); }

static void update_paths(int erasing) {
    EdDrag d = {startCol, startRow, endCol, endRow, vertical, pokeHole};
    prepare();
    ed_update_paths(lv(), &d, erasing, modifier());
}

static void toggle_pellet(int c, int r) {
    Level before = *lv();
    if (!ed_toggle_pellet(lv(), c, r, modifier())) return;
    Level after = *lv();
    *lv() = before;
    prepare();
    *lv() = after;
}

static void set_player(int nr, int c, int r) {
    if (!can_place(c, r)) return;
    prepare();
    ed_set_player(lv(), nr, c, r, modifier());
}

static void move_citadel(int cc, int rr) {
    prepare();
    ed_move_citadel(lv(), cc, rr);
}

/* ================================================================== levels */

static void new_level(void) {
    Level copy = *lv();
    Level *l = levels_insert(&set, cur + 1);
    *l = copy;
    ed_blank_level(l);
    cur++;
    undoCount = 0;
    dirty = 1;
}

static void duplicate_level(void) {
    Level copy = *lv();
    *levels_insert(&set, cur + 1) = copy;
    cur++;
    undoCount = 0;
    dirty = 1;
}

static void delete_level(void) {
    if (set.n <= 1) return;
    levels_remove(&set, cur);
    if (cur >= set.n) cur = set.n - 1;
    undoCount = 0;
    dirty = 1;
}

static void move_to_first(void) {
    if (cur == 0) return;
    Level copy = *lv();
    levels_remove(&set, cur);
    *levels_insert(&set, 0) = copy;
    cur = 0;
    undoCount = 0;
    dirty = 1;
}

/* ================================================================== files */

static void load_file(const char *name) {
    LevelSet s;
    memset(&s, 0, sizeof s);
    if (name[0]) {
        char p[300];
        snprintf(p, sizeof p, "%s/levels/%s", appDir, name);
        if (!levels_load_file(&s, p)) {
            levels_free(&s);
            ui_alert("This level file cannot be read.");
            return;
        }
    } else {
        levels_copy(&s, &game.standard);
    }
    levels_free(&set);
    set = s;
    snprintf(file, sizeof file, "%s", name);
    cur = 0;
    undoCount = 0;
    dirty = 0;
}

static void save_to(const char *name) {
    char p[300];
    snprintf(p, sizeof p, "%s/levels/%s", appDir, name);
    if (!storageOk || !levels_save_file(&set, p)) {
        ui_alert("The level file cannot be saved. Is the SD card locked?");
        return;
    }
    snprintf(file, sizeof file, "%s", name);
    dirty = 0;
    say("SAVED");
}

static char pendingName[64];

static void save_overwrite(void) { save_to(pendingName); }

static void save_as_done(const char *text) {
    if (!text) return;
    char n[64];
    int k = 0;
    for (const char *s = text; *s && k < 50; s++) {
        unsigned char ch = *s;
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
            ch == '>' || ch == '|' || (ch < 32))
            continue;
        if (k == 0 && (ch == '.' || ch == ' ')) continue;
        n[k++] = ch;
    }
    while (k > 0 && n[k - 1] == ' ') k--;
    n[k] = 0;
    if (!k) return;
    int l = strlen(n);
    if (l < 6 || strcasecmp(n + l - 6, ".plist")) strcat(n, ".plist");
    snprintf(pendingName, sizeof pendingName, "%s", n);
    char p[300];
    snprintf(p, sizeof p, "%s/levels/%s", appDir, n);
    FILE *f = fopen(p, "rb");
    if (f) {
        fclose(f);
        if (strcmp(n, file)) {
            ui_confirm("Save As", "A level file with this name already exists. Replace it?", save_overwrite);
            return;
        }
    }
    save_to(n);
}

static void save_as(void) {
    char d[64];
    if (file[0]) levelfile_display(file, d, sizeof d);
    else snprintf(d, sizeof d, "My Levels");
    ui_text_entry("Save the level file as:", d, 40, save_as_done);
}

static void save(void) {
    if (!file[0]) save_as();    /* the standard levels stay as they are */
    else save_to(file);
}

/* ---- the file list */

static char fileNames[32][64];
static char fileLabels[32][40];
static int nFiles;
static char openPending[64];

static const char *file_label(int i) { return fileLabels[i]; }

static void open_now(void) { load_file(openPending); }

static void open_activate(int i) {
    ui_menu_close();
    snprintf(openPending, sizeof openPending, "%s", fileNames[i]);
    if (dirty) ui_confirm("Open", "Your changes are not saved. Open another file anyway?", open_now);
    else open_now();
}

static void open_file(void) {
    nFiles = levelfiles_list(fileNames, 32);
    UiMenu m = {0};
    m.title = "OPEN LEVEL FILE";
    m.n = nFiles;
    for (int i = 0; i < nFiles; i++) {
        levelfile_display(fileNames[i], fileLabels[i], sizeof fileLabels[i]);
        if (!strcmp(fileNames[i], file)) m.sel = i;
    }
    m.label = file_label;
    m.activate = open_activate;
    ui_menu_open(&m);
}

static void new_file_now(void) {
    levels_free(&set);
    set.lv = calloc(1, sizeof(Level));
    set.cap = 1;
    set.n = 1;
    /* starts from the first standard level, so it can be tried at once */
    if (game.standard.n) set.lv[0] = game.standard.lv[0];
    else ed_blank_level(&set.lv[0]);
    file[0] = 0;
    cur = 0;
    undoCount = 0;
    dirty = 1;
    save_as();
}

static void new_file(void) {
    if (dirty) ui_confirm("New", "Your changes are not saved. Start a new file anyway?", new_file_now);
    else new_file_now();
}

/* ================================================================== open, close, try */

void editor_open(void) {
    memset(&set, 0, sizeof set);
    load_file(cfg.levelFile);
    if (!set.n) load_file("");
    cur = cfg.tryLevel - 1;
    if (cur < 0 || cur >= set.n) cur = 0;
    tool = T_PATH;
    dragging = 0;
    active = 1;
    mus_stop();
    snd_stop_all();
}

int editor_active(void) { return active; }

void editor_resume(void) {
    active = 1;
    dragging = 0;
    mus_stop();
    snd_stop_all();
}

static void close_now(void) {
    active = 0;
    levels_free(&set);
    game.mode = MODE_NONE;
    game_show_splash();
}

static void close_editor(void) {
    if (dirty) ui_confirm("Level Editor", "Your changes are not saved. Close the editor anyway?", close_now);
    else close_now();
}

static void try_level(void) {
    if (cfg.tryPlayer == 0 && cfg.difficulty == 4) {
        ui_alert(tr("NoTwoPlayer"));
        return;
    }
    active = 0;
    game_try_editor_level(&set, cur, cfg.tryPlayer);
}

/* ================================================================== the menu */

enum {
    M_LEVEL, M_NEW_LEVEL, M_DUPLICATE, M_DELETE, M_FIRST, M_UNDO,
    M_RED, M_GREEN, M_BLUE, M_WAIT, M_TILES, M_CITADEL, M_BACKGROUND,
    M_TRY_WITH, M_TRY, M_OPEN, M_NEW_FILE, M_SAVE, M_SAVE_AS, M_CLOSE, M_COUNT
};

static char bgNames[64][40];
static int nBg;

static int natcmp(const void *a, const void *b) {
    const char *x = a, *y = b;
    while (*x && *y) {
        if (isdigit((unsigned char)*x) && isdigit((unsigned char)*y)) {
            long n = strtol(x, (char **)&x, 10), m = strtol(y, (char **)&y, 10);
            if (n != m) return n < m ? -1 : 1;
        } else {
            if (*x != *y) return (unsigned char)*x - (unsigned char)*y;
            x++;
            y++;
        }
    }
    return (unsigned char)*x - (unsigned char)*y;
}

static void list_backgrounds(void) {
    if (nBg) return;
    for (int i = 0; i < pack_count() && nBg < 64; i++) {
        const PackEntry *e = pack_entry(i);
        if (e->type == PK_TEX && !strncmp(e->name, "bg/", 3))
            snprintf(bgNames[nBg++], sizeof bgNames[0], "%s", e->name + 3);
    }
    qsort(bgNames, nBg, sizeof bgNames[0], natcmp);
}

static const char *menu_label(int i) {
    static const char *l[M_COUNT] = {
        "LEVEL", "NEW LEVEL", "DUPLICATE LEVEL", "DELETE LEVEL", "MAKE FIRST LEVEL", "UNDO",
        "RED", "GREEN", "BLUE", "WAIT TIME", "TILES", "CITADEL", "BACKGROUND",
        "TRY WITH", "TRY LEVEL", "OPEN FILE", "NEW FILE", "SAVE", "SAVE AS", "CLOSE EDITOR"};
    return l[i];
}

static const char *menu_value(int i) {
    static char v[48];
    Level *l = lv();
    switch (i) {
    case M_LEVEL: snprintf(v, sizeof v, "%d / %d", cur + 1, set.n); return v;
    case M_RED: snprintf(v, sizeof v, "%d", l->red); return v;
    case M_GREEN: snprintf(v, sizeof v, "%d", l->green); return v;
    case M_BLUE: snprintf(v, sizeof v, "%d", l->blue); return v;
    case M_WAIT: snprintf(v, sizeof v, "%d", l->wait); return v;
    case M_TILES: return strcmp(l->tileset, "tile2") ? "EMBOSSED" : "CLASSIC";
    case M_CITADEL: {
        static const char *n[4] = {"EMBOSSED", "GLASSY", "CLASSIC", "AQUA"};
        return n[citadel_number(l->citadel) - 1];
    }
    case M_BACKGROUND: snprintf(v, sizeof v, "%s", l->background); return v;
    case M_TRY_WITH: return cfg.tryPlayer == 1 ? "PLAYER 1" : cfg.tryPlayer == 2 ? "PLAYER 2" : "BOTH";
    }
    return NULL;
}

static void menu_change(int i, int d) {
    Level *l = lv();
    switch (i) {
    case M_LEVEL:
        cur = (cur + d + set.n) % set.n;
        break;
    case M_RED:
    case M_GREEN:
    case M_BLUE: {
        int *c = i == M_RED ? &l->red : i == M_GREEN ? &l->green : &l->blue;
        prepare();
        *c += d * 5;
        if (*c < 0) *c = 0;
        if (*c > 255) *c = 255;
        break;
    }
    case M_WAIT: {
        static const int w[4] = {3, 5, 10, 15};
        int k = 0;
        while (k < 3 && w[k] < l->wait) k++;
        if (w[k] != l->wait) k = d > 0 ? k : k - 1;
        else k += d;
        k = (k + 4) % 4;
        prepare();
        l->wait = w[k];
        break;
    }
    case M_TILES:
        prepare();
        snprintf(l->tileset, sizeof l->tileset, "%s", strcmp(l->tileset, "tile2") ? "tile2" : "tile");
        break;
    case M_CITADEL: {
        int n = (citadel_number(l->citadel) - 1 + d + 4) % 4;
        prepare();
        if (n == 0) snprintf(l->citadel, sizeof l->citadel, "citadel");
        else snprintf(l->citadel, sizeof l->citadel, "citadel%d", n + 1);
        break;
    }
    case M_BACKGROUND: {
        list_backgrounds();
        if (!nBg) break;
        int k = 0;
        for (int j = 0; j < nBg; j++)
            if (!strcmp(bgNames[j], l->background)) k = j;
        k = (k + d + nBg) % nBg;
        prepare();
        snprintf(l->background, sizeof l->background, "%s", bgNames[k]);
        break;
    }
    case M_TRY_WITH:
        cfg.tryPlayer = (cfg.tryPlayer + d + 3) % 3;
        break;
    }
}

static void delete_now(void) { delete_level(); }

static void menu_activate(int i) {
    switch (i) {
    case M_NEW_LEVEL: new_level(); break;
    case M_DUPLICATE: duplicate_level(); break;
    case M_DELETE:
        if (set.n > 1) ui_confirm("Delete Level", "Do you really want to delete this level?", delete_now);
        break;
    case M_FIRST: move_to_first(); break;
    case M_UNDO: do_undo(); break;
    case M_TRY:
        ui_menu_close();
        try_level();
        break;
    case M_OPEN: open_file(); break;
    case M_NEW_FILE:
        ui_menu_close();
        new_file();
        break;
    case M_SAVE:
        ui_menu_close();
        save();
        break;
    case M_SAVE_AS:
        ui_menu_close();
        save_as();
        break;
    case M_CLOSE:
        ui_menu_close();
        close_editor();
        break;
    default: menu_change(i, 1); break;
    }
}

static void open_menu(void) {
    UiMenu m = {0};
    m.title = "LEVEL EDITOR";
    m.n = M_COUNT;
    m.sel = M_LEVEL;
    m.label = menu_label;
    m.value = menu_value;
    m.change = menu_change;
    m.activate = menu_activate;
    m.back = ui_menu_close;
    ui_menu_open(&m);
}

/* ================================================================== input */

#define TB_X 6
#define TB_STEP 38
#define MENU_BX 560

static int cell_of(float px, float py, int *c, int *r) { return ed_cell_at(px, py, c, r); }

static void citadel_cell(float px, float py, int *cc, int *rr) { ed_citadel_cell(px, py, cc, rr); }

static void mouse_down(float px, float py) {
    if (py < VY) {
        int t = (int)((px - TB_X) / TB_STEP);
        if (px >= TB_X && t >= 0 && t < T_COUNT) tool = t;
        else if (px >= MENU_BX) open_menu();
        return;
    }
    int c, r;
    if (!cell_of(px, py, &c, &r)) return;
    switch (tool) {
    case T_PATH:
    case T_ERASE:
    case T_CITADEL:
        dragging = 1;
        startCol = endCol = c;
        startRow = endRow = r;
        startX = dragX = px;
        startY = dragY = py;
        dirSet = vertical = pokeHole = 0;
        break;
    case T_PELLET: toggle_pellet(c, r); break;
    case T_PLAYER1: set_player(1, c, r); break;
    case T_PLAYER2: set_player(2, c, r); break;
    }
}

static void mouse_dragged(float px, float py) {
    dragX = px;
    dragY = py;
    if (tool == T_CITADEL) return;
    int c, r;
    if (!cell_of(px, py, &c, &r) && tool == T_PATH) pokeHole = 1;
    float dx = px - startX, dy = py - startY;
    if (!dirSet && (dx > 22 || dx < -22 || dy > 22 || dy < -22)) {
        dirSet = 1;
        vertical = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx);
    }
    endCol = c;
    endRow = r;
    if (dirSet) {
        if (vertical) endCol = startCol;
        else endRow = startRow;
    }
}

static void mouse_up(void) {
    dragging = 0;
    if (tool == T_CITADEL) {
        int cc, rr;
        citadel_cell(dragX, dragY, &cc, &rr);
        move_citadel(cc, rr);
        return;
    }
    if (dirSet || tool == T_ERASE) update_paths(tool == T_ERASE);
}

void editor_update(void) {
    if (messageTimer > 0) messageTimer--;
    float px = -1, py = -1;
    int on = in_pointer(&px, &py);
    if (dragging) {
        if (on) mouse_dragged(px, py);
        if (!in_click_held()) mouse_up();
        return;
    }
    if (in_click() && on) {
        mouse_down(px, py);
        return;
    }
    u32 pr = in_menu_pressed(), rep = in_menu_repeat();
    if (pr & (MB_PLUS | MB_HOME)) open_menu();
    else if (pr & MB_MINUS) do_undo();
    else if (pr & MB_1) try_level();
    else if (pr & MB_2) save();
    else if (rep & MB_UP) tool = (tool + T_COUNT - 1) % T_COUNT;
    else if (rep & MB_DOWN) tool = (tool + 1) % T_COUNT;
    else if (rep & MB_LEFT) cur = (cur + set.n - 1) % set.n;
    else if (rep & MB_RIGHT) cur = (cur + 1) % set.n;
}

#ifdef PACX_TEST
static void test_path(int c0, int r0, int c1, int r1, int vert, int poke, int erase) {
    startCol = c0;
    startRow = r0;
    endCol = c1;
    endRow = r1;
    vertical = vert;
    pokeHole = poke;
    update_paths(erase);
}

/* a scripted editing session for the Dolphin test */
void editor_test(void) {
    new_level();
    test_path(1, 1, 11, 1, 0, 0, 0);
    test_path(1, 1, 1, 8, 1, 0, 0);
    test_path(11, 1, 11, 8, 1, 0, 0);
    test_path(1, 8, 11, 8, 0, 0, 0);
    test_path(0, 4, 1, 4, 0, 1, 0);         /* through the left edge: opens (12, 4) as well */
    test_path(6, 0, 6, 1, 1, 1, 0);         /* through the top edge */
    move_citadel(5, 4);
    set_player(1, 6, 8);
    set_player(2, 4, 8);
    toggle_pellet(1, 1);
    toggle_pellet(11, 8);
    test_path(3, 1, 3, 1, 0, 0, 1);         /* erases one cell */
}
#endif

/* ================================================================== drawing */

static void draw_frame(const Frame *f, float x, float y, float w, float h, float r, float g, float b, float a) {
    gfx_quad(f->tex, f->sx, f->sy, f->w, f->h, x, y, w, h, 0, 0, r, g, b, a);
}

static void draw_set(const char *name, int idx, float x, float y, float r, float g, float b, float a) {
    FrameSet *fs = fs_find(name);
    if (!fs) fs = fs_whole(name);
    if (!fs || idx >= fs->count) return;
    const Frame *f = &fs->frames[idx];
    draw_frame(f, x, y, f->w, f->h, r, g, b, a);
}

typedef struct { int x, y; FrameSet *fs; float col[3]; } Emit;

static void emit(void *u, int frame, int xo, int yo) {
    Emit *e = u;
    if (!e->fs || frame >= e->fs->count) return;
    const Frame *f = &e->fs->frames[frame];
    draw_frame(f, 44 * e->x + 11 * xo + 34, 44 * e->y + 11 * yo + 36, f->w, f->h, e->col[0], e->col[1], e->col[2], 1);
}

static int is_sp(const Level *l, int c, int r) {
    for (int i = 0; i < l->nsp; i++)
        if (l->sp[i][0] == c && l->sp[i][1] == r) return 1;
    return 0;
}

static void draw_level(void) {
    const Level *l = lv();
    float col[3] = {l->red / 255.0f, l->green / 255.0f, l->blue / 255.0f};

    char bg[48];
    snprintf(bg, sizeof bg, "bg/%s", l->background);
    FrameSet *bfs = fs_find(bg);
    if (!bfs) bfs = fs_whole(bg);
    if (bfs && bfs->count) {
        const Frame *f = &bfs->frames[0];
        for (int y = 0; y < VIEW_H; y += f->h)
            for (int x = 0; x < VIEW_W; x += f->w) draw_frame(f, x, y, f->w, f->h, 1, 1, 1, 1);
    } else {
        gfx_rect(0, 0, VIEW_W, VIEW_H, 0, 0, 0, 1);
    }

    int ec = -1, er = -1;
    for (int r = 0; r < LV_H; r++)
        for (int c = 0; c < LV_W; c++)
            if (l->tiles[r][c] == 'R') {
                ec = c;
                er = r - 1;
                break;
            }

    Emit e;
    e.fs = fs_find(strcmp(l->tileset, "tile2") ? "tile" : "tile2");
    memcpy(e.col, col, sizeof col);
    FrameSet *pellet = fs_find("pellet"), *sp = fs_find("super_pellet");
    for (int r = 0; r < LV_H; r++)
        for (int c = 0; c < LV_W; c++) {
            int t = l->tiles[r][c] - 'A';
            if (t < 0 || t > 17) t = 0;
            if (t > 15) continue;
            e.x = c;
            e.y = r;
            maze_cell_parts(t, emit, &e);
            if (t == 0) continue;
            int start = (c == l->p1x && r == l->p1y) || (c == l->p2x && r == l->p2y);
            if (is_sp(l, c, r) && !start) {
                if (sp && sp->count) draw_frame(&sp->frames[0], 44 * c + 41, 44 * r + 43, sp->frames[0].w, sp->frames[0].h, 1, 1, 1, 1);
            } else if (!start && pellet) {
                draw_frame(&pellet->frames[0], 44 * c + 51, 44 * r + 53, pellet->frames[0].w, pellet->frames[0].h, 1, 1, 1, 1);
            }
            if ((t & 2) && c < 12 && pellet)
                draw_frame(&pellet->frames[0], 44 * c + 73, 44 * r + 53, pellet->frames[0].w, pellet->frames[0].h, 1, 1, 1, 1);
            if ((t & 8) && r <= 8 && !(c == ec && r == er) && pellet)
                draw_frame(&pellet->frames[0], 44 * c + 51, 44 * r + 75, pellet->frames[0].w, pellet->frames[0].h, 1, 1, 1, 1);
        }
    if (ec >= 0) {
        char cn[16];
        snprintf(cn, sizeof cn, "citadel%d", citadel_number(l->citadel));
        draw_set(cn, 0, 44 * ec - 10, 44 * er + 80, col[0], col[1], col[2], 1);
    }
    draw_set("player1", 11, 44 * l->p1x + 40, 44 * l->p1y + 42, 1, 1, 1, 1);
    draw_set("player2", 4, 44 * l->p2x + 40, 44 * l->p2y + 42, 1, 1, 1, 1);

    /* the parts of the maze that wrap around are covered, as in the game */
    gfx_rect(0, 0, VIEW_W, VY, 0, 0, 0, 1);
    gfx_rect(0, 476, VIEW_W, 4, 0, 0, 0, 1);
    gfx_rect(0, 0, VX, VIEW_H, 0, 0, 0, 1);
    gfx_rect(606, 0, 34, VIEW_H, 0, 0, 0, 1);
}

static void draw_icon(int t, float x, float y) {
    const char *names[T_COUNT] = {"ed_pathway", "ed_erasure", "ed_pellet", "ed_player1", "ed_player2", "ed_citadel"};
    FrameSet *fs = fs_find(names[t]);
    if (!fs) fs = fs_whole(names[t]);
    int idx = 0;
    if (!fs || idx >= fs->count) return;
    const Frame *f = &fs->frames[idx];
    float s = 30.0f / (f->w > f->h ? f->w : f->h);
    if (s > 1) s = 1;
    float w = f->w * s, h = f->h * s;
    draw_frame(f, x + (34 - w) / 2, y + (32 - h) / 2, w, h, 1, 1, 1, 1);
}

static void draw_toolbar(void) {
    for (int t = 0; t < T_COUNT; t++) {
        float x = TB_X + t * TB_STEP;
        if (t == tool) gfx_rect(x - 1, 1, 36, 34, 0.25f, 0.3f, 0.9f, 0.6f);
        draw_icon(t, x, 2);
    }
    char s[96], d[40];
    if (messageTimer > 0) {
        snprintf(s, sizeof s, "%s", message);
    } else {
        levelfile_display(file, d, sizeof d);
        snprintf(s, sizeof s, "%s%s  %d/%d", d, dirty ? "*" : "", cur + 1, set.n);
    }
    int maxc = (MENU_BX - 252) / 8;
    if ((int)strlen(s) > maxc) s[maxc] = 0;
    ui_text(252, 12, s, 3, 1, 1);
    gfx_rect(MENU_BX, 4, 74, 28, 0.25f, 0.3f, 0.9f, 0.45f);
    ui_text(MENU_BX + 5, 5, "MENU", 0, 0, 1);
}

static void draw_cell_box(int c, int r, float cr, float cg, float cb) {
    gfx_frame(VX + c * CELL, VY + r * CELL, CELL, CELL, 2, cr, cg, cb, 0.9f);
}

void editor_draw(void) {
    draw_level();
    float px, py;
    int on = in_pointer(&px, &py);
    if (dragging && tool != T_CITADEL) {
        int minC = startCol < endCol ? startCol : endCol, maxC = startCol < endCol ? endCol : startCol;
        int minR = startRow < endRow ? startRow : endRow, maxR = startRow < endRow ? endRow : startRow;
        float r = tool == T_ERASE ? 1 : 0.2f, g = tool == T_ERASE ? 0.2f : 1;
        for (int y = minR; y <= maxR; y++)
            for (int x = minC; x <= maxC; x++) {
                gfx_rect(VX + x * CELL, VY + y * CELL, CELL, CELL, r, g, 0.2f, 0.25f);
                draw_cell_box(x, y, r, g, 0.2f);
            }
    } else if (on && (tool == T_CITADEL)) {
        int cc, rr;
        citadel_cell(on && dragging ? dragX : px, on && dragging ? dragY : py, &cc, &rr);
        gfx_frame(VX + cc * CELL, VY + rr * CELL, 3 * CELL, 2 * CELL, 2, 1, 1, 0.3f, 0.9f);
    } else if (on && py >= VY) {
        int c, r;
        if (cell_of(px, py, &c, &r)) {
            int ok = tool == T_PATH || tool == T_ERASE || can_place(c, r) ||
                     (tool == T_PELLET && ed_is_super_pellet(lv(), c, r));
            draw_cell_box(c, r, ok ? 1 : 1, ok ? 1 : 0.2f, ok ? 1 : 0.2f);
        }
    }
    draw_toolbar();
    if (on) gfx_cursor(px, py);
}
