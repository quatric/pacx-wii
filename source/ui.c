#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef PACX_PORT
#include <wiiuse/wpad.h>
#include <input_wii.h>
#include <wiiosk.h>
#include <wiiosk_input_wii.h>
#endif
#include "ui.h"
#include "uimenu.h"
#include "gfx.h"
#include "engine.h"
#include "game.h"
#include "actors.h"
#include "input.h"
#include "audio.h"
#include "settings.h"
#include "levels.h"
#include "editor.h"

/* ================================================================== text */

static FrameSet *font_set(int color, int small) {
    static FrameSet *cache[2][4];
    if (color < 0 || color > 3) color = 3;
    if (!cache[small][color]) {
        char n[16];
        snprintf(n, sizeof n, small ? "font_small%d" : "font%d", color);
        cache[small][color] = fs_find(n);
    }
    return cache[small][color];
}

/* the next character of a UTF-8 string as a Latin-1 code */
static int next_char(const unsigned char **p) {
    int c = *(*p)++;
    if (c >= 0xC0 && (**p & 0xC0) == 0x80) {
        c = ((c & 0x1F) << 6) | (*(*p)++ & 0x3F);
        while ((**p & 0xC0) == 0x80) (*p)++;
    }
    return c;
}

int ui_text_width(const char *s, int small) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) { next_char(&p); n++; }
    return n * (small ? 8 : 16);
}

void ui_text(float x, float y, const char *s, int color, int small, float alpha) {
    FrameSet *fs = font_set(color, small);
    if (!fs) return;
    int cw = small ? 8 : 16, ady = small ? 7 : 12;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int c = next_char(&p);
        if (c != ' ') {
            int accent, g = char_glyph(c, &accent);
            if (g >= 0 && g < fs->count) {
                const Frame *f = &fs->frames[g];
                gfx_quad(f->tex, f->sx, f->sy, f->w, f->h, x, y, f->w, f->h, 0, 0, 1, 1, 1, alpha);
                if (accent) {
                    const Frame *a = &fs->frames[accent == 1 ? 48 : 49];
                    if (accent == 1) {
                        gfx_quad(a->tex, a->sx, a->sy, a->w, a->h, x - cw / 4, y - ady, a->w, a->h, 0, 0, 1, 1, 1, alpha);
                        gfx_quad(a->tex, a->sx, a->sy, a->w, a->h, x + cw / 4, y - ady, a->w, a->h, 0, 0, 1, 1, 1, alpha);
                    } else {
                        gfx_quad(a->tex, a->sx, a->sy, a->w, a->h, x, y - ady, a->w, a->h, 0, accent == 3, 1, 1, 1, alpha);
                    }
                }
            }
        }
        x += cw;
    }
}

void ui_text_center(float y, const char *s, int color, int small, float alpha) {
    ui_text((VIEW_W - ui_text_width(s, small)) / 2, y, s, color, small, alpha);
}

void ui_panel(float x, float y, float w, float h) {
    gfx_rect(x, y, w, h, 0, 0, 0.08f, 0.88f);
    gfx_frame(x, y, w, h, 2, 0.35f, 0.45f, 1.0f, 1.0f);
}

/* ================================================================== the screen stack */

enum { S_MENU = 1, S_ALERT, S_CONFIRM, S_OSK };

typedef struct {
    int type;
    UiMenu menu;
    char title[96], text[256];
    void (*yes)(void);
    int sel;
#ifdef PACX_PORT
    char buf[32];
    int maxc, kx, ky;
#else
    osk_keyboard *osk;
#endif
    void (*done)(const char *text);
} Screen;

#define MAX_SCREENS 8
static Screen stack[MAX_SCREENS];
static int depth;
static int quitRequested;

int ui_quit_requested(void) { return quitRequested; }
int ui_active(void) { return depth > 0; }

void ui_close_all(void) {
    while (depth > 0) {
        Screen *s = &stack[depth - 1];
#ifndef PACX_PORT
        if (s->type == S_OSK && s->osk) {
            OSK_Destroy(s->osk);
            INPUT_SetScreenSize(VIEW_W, VIEW_H);
        }
#else
        (void)s;
#endif
        depth--;
    }
}

static void menu_scroll(UiMenu *m);

static Screen *push(int type) {
    if (depth >= MAX_SCREENS) return NULL;
    Screen *s = &stack[depth++];
    memset(s, 0, sizeof *s);
    s->type = type;
    return s;
}

static void pop(void) {
    if (depth > 0) depth--;
}

void ui_menu_open(const UiMenu *m) {
    Screen *s = push(S_MENU);
    if (!s) return;
    s->menu = *m;
    menu_scroll(&s->menu);
}

void ui_menu_close(void) {
    if (depth > 0 && stack[depth - 1].type == S_MENU) pop();
}

void ui_alert(const char *text) {
    Screen *s = push(S_ALERT);
    if (s) snprintf(s->text, sizeof s->text, "%s", text);
}

void ui_confirm(const char *title, const char *text, void (*yes)(void)) {
    Screen *s = push(S_CONFIRM);
    if (!s) return;
    snprintf(s->title, sizeof s->title, "%s", title);
    snprintf(s->text, sizeof s->text, "%s", text);
    s->yes = yes;
    s->sel = 0;     /* "No" is the default button, as on the Mac */
}

void ui_text_entry(const char *title, const char *initial, int maxChars, void (*done)(const char *text)) {
    Screen *s = push(S_OSK);
    if (!s) return;
    snprintf(s->title, sizeof s->title, "%s", title ? title : "");
    snprintf(s->text, sizeof s->text, "%s", title ? title : "");
    s->done = done;
#ifdef PACX_PORT
    snprintf(s->buf, sizeof s->buf, "%s", initial ? initial : "");
    s->maxc = maxChars > 31 ? 31 : maxChars;
    if ((int)strlen(s->buf) > s->maxc) s->buf[s->maxc] = 0;
    return;
#else
    osk_config c;
    OSK_DefaultConfig(&c);
    OSK_UseSystemLanguage(&c);
    c.dictionary = NULL;
    c.title = s->title;
    c.initial = initial;
    c.max_chars = maxChars;
    GXRModeObj *rm = gfx_rmode();
    c.screen_width = rm->fbWidth;
    c.screen_height = rm->efbHeight;
    c.flags |= OSK_FLAG_REQUIRE_TEXT;
    if (gfx_is_widescreen()) c.flags |= OSK_FLAG_WIDESCREEN;
    s->osk = OSK_Create(&c);
    if (!s->osk) { pop(); if (done) done(NULL); return; }
    INPUT_SetScreenSize(rm->fbWidth, rm->efbHeight);
#endif
}

/* ================================================================== text wrapping */

static int wrap(const char *text, char lines[][80], int maxLines, int width) {
    int n = 0;
    const char *p = text;
    while (*p && n < maxLines) {
        while (*p == ' ') p++;
        int len = 0, lastSpace = -1, chars = 0;
        const char *q = p;
        while (q[len] && chars < width) {
            if (q[len] == ' ') lastSpace = len;
            if ((q[len] & 0xC0) != 0x80) chars++;
            len++;
        }
        while ((q[len] & 0xC0) == 0x80) len++;
        if (q[len] && lastSpace > 0) len = lastSpace;
        if (len > 79) len = 79;
        memcpy(lines[n], p, len);
        lines[n][len] = 0;
        n++;
        p += len;
    }
    return n;
}

/* ================================================================== menu */

#define MENU_W 560
#define ROW_H 34
#define VAL_X0 232          /* the value column of a row, between the arrows */
#define VAL_X1 (MENU_W - 24)

static int menu_visible(const UiMenu *m) { return m->n < MENU_ROWS ? m->n : MENU_ROWS; }

static void menu_scroll(UiMenu *m) {
    int v = menu_visible(m);
    if (m->sel < m->top) m->top = m->sel;
    if (m->sel >= m->top + v) m->top = m->sel - v + 1;
    if (m->top > m->n - v) m->top = m->n - v;
    if (m->top < 0) m->top = 0;
}

static void menu_geometry(const UiMenu *m, float *x, float *y, float *h) {
    *h = 70 + menu_visible(m) * ROW_H + 16;
    *x = (VIEW_W - MENU_W) / 2;
    *y = (VIEW_H - *h) / 2;
}

static int menu_row_at(const UiMenu *m, float px, float py) {
    float x, y, h;
    menu_geometry(m, &x, &y, &h);
    if (px < x || px > x + MENU_W) return -1;
    int r = (int)((py - (y + 62)) / ROW_H);
    if (py < y + 62 || r < 0 || r >= menu_visible(m)) return -1;
    return r + m->top;
}

static void menu_update(Screen *s) {
    UiMenu *m = &s->menu;
    u32 rep = in_menu_repeat(), pr = in_menu_pressed();
    float px = -1, py = -1;
    if (in_pointer(&px, &py)) {
        int r = menu_row_at(m, px, py);
        static float lx, ly;
        if (r >= 0 && (px != lx || py != ly)) m->sel = r;
        lx = px;
        ly = py;
    }
    if (rep & MB_UP) m->sel = (m->sel + m->n - 1) % m->n;
    if (rep & MB_DOWN) m->sel = (m->sel + 1) % m->n;
    if (rep & (MB_UP | MB_DOWN)) menu_scroll(m);
    int i = m->sel;
    if (m->change && (rep & (MB_LEFT | MB_RIGHT))) m->change(i, (rep & MB_LEFT) ? -1 : 1);
    if (in_click()) {
        int r = menu_row_at(m, px, py);
        if (r >= 0) {
            m->sel = r;
            float x, y, h;
            menu_geometry(m, &x, &y, &h);
            const char *v = m->value ? m->value(r) : NULL;
            if (v && m->change) {
                /* the arrows around the value */
                m->change(r, px < x + (VAL_X0 + VAL_X1) / 2 ? -1 : 1);
            } else if (m->activate) {
                m->activate(r);
            }
        } else if (m->back) {
            m->back();
        }
        return;
    }
    if ((pr & MB_OK) && !(pr & MB_CLICK)) {
        if (m->activate) m->activate(i);
        else if (m->change) m->change(i, 1);
        return;
    }
    if (pr & (MB_BACK | MB_HOME | MB_MINUS)) {
        if (m->back) m->back();
        else ui_menu_close();
    }
}

static void menu_draw(const UiMenu *m) {
    float x, y, h;
    menu_geometry(m, &x, &y, &h);
    ui_panel(x, y, MENU_W, h);
    ui_text_center(y + 18, m->title, 0, 0, 1);
    if (m->top > 0) ui_text(x + MENU_W - 30, y + 18, "+", 2, 0, 1);
    if (m->top + menu_visible(m) < m->n) ui_text(x + MENU_W - 30, y + h - 30, "+", 2, 0, 1);
    for (int i = m->top; i < m->top + menu_visible(m); i++) {
        float ry = y + 62 + (i - m->top) * ROW_H;
        if (i == m->sel) gfx_rect(x + 8, ry - 3, MENU_W - 16, ROW_H - 2, 0.25f, 0.3f, 0.9f, 0.45f);
        const char *l = m->label(i);
        const char *v = m->value ? m->value(i) : NULL;
        if (v) {
            ui_text(x + 24, ry, l, i == m->sel ? 0 : 3, 0, 1);
            if (m->change) {
                ui_text(x + VAL_X0 - 16, ry, "(", 2, 0, 1);
                ui_text(x + VAL_X1, ry, ")", 2, 0, 1);
            }
            ui_text(x + (VAL_X0 + VAL_X1) / 2 - ui_text_width(v, 0) / 2, ry, v, 2, 0, 1);
        } else {
            ui_text(x + (MENU_W - ui_text_width(l, 0)) / 2, ry, l, i == m->sel ? 0 : 3, 0, 1);
        }
    }
}

/* ================================================================== alert and confirm */

static void dialog_geometry(const Screen *s, int *nlines, char lines[][80], float *x, float *y, float *w, float *h) {
    *nlines = wrap(s->text, lines, 6, 32);
    *w = 560;
    *h = 60 + *nlines * 30 + 60 + (s->title[0] ? 36 : 0);
    *x = (VIEW_W - *w) / 2;
    *y = (VIEW_H - *h) / 2;
}

static int dialog_button_at(const Screen *s, float px, float py) {
    char lines[6][80];
    int n;
    float x, y, w, h;
    dialog_geometry(s, &n, lines, &x, &y, &w, &h);
    float by = y + h - 52;
    if (py < by - 6 || py > by + 34) return -1;
    if (s->type == S_ALERT) return (px > VIEW_W / 2 - 80 && px < VIEW_W / 2 + 80) ? 0 : -1;
    if (px > VIEW_W / 2 - 200 && px < VIEW_W / 2 - 20) return 0;
    if (px > VIEW_W / 2 + 20 && px < VIEW_W / 2 + 200) return 1;
    return -1;
}

static void dialog_update(Screen *s) {
    u32 pr = in_menu_pressed();
    float px = -1, py = -1;
    int hover = in_pointer(&px, &py) ? dialog_button_at(s, px, py) : -1;
    if (s->type == S_CONFIRM) {
        if (hover >= 0) s->sel = hover;
        if (pr & MB_LEFT) s->sel = 0;
        if (pr & MB_RIGHT) s->sel = 1;
    }
    int act = -1;
    if (in_click()) act = hover;
    else if (pr & MB_OK) act = s->type == S_ALERT ? 0 : s->sel;
    else if (pr & (MB_BACK | MB_HOME)) act = s->type == S_ALERT ? 0 : 0;
    if (act < 0) return;
    void (*yes)(void) = s->yes;
    pop();
    if (s->type == S_CONFIRM && act == 1 && yes) yes();
}

static void dialog_draw(const Screen *s) {
    char lines[6][80];
    int n;
    float x, y, w, h;
    dialog_geometry(s, &n, lines, &x, &y, &w, &h);
    gfx_rect(0, 0, VIEW_W, VIEW_H, 0, 0, 0, 0.4f);
    ui_panel(x, y, w, h);
    float ty = y + 24;
    if (s->title[0]) {
        ui_text_center(ty, s->title, 0, 0, 1);
        ty += 36;
    }
    for (int i = 0; i < n; i++) ui_text_center(ty + i * 30, lines[i], 3, 0, 1);
    float by = y + h - 52;
    if (s->type == S_ALERT) {
        gfx_rect(VIEW_W / 2 - 80, by - 4, 160, 34, 0.25f, 0.3f, 0.9f, 0.45f);
        ui_text_center(by, "OK", 0, 0, 1);
    } else {
        const char *no = tr("No"), *yes = tr("Yes");
        gfx_rect(VIEW_W / 2 - 200 + (s->sel ? 220 : 0), by - 4, 180, 34, 0.25f, 0.3f, 0.9f, 0.45f);
        ui_text(VIEW_W / 2 - 110 - ui_text_width(no, 0) / 2, by, no, s->sel == 0 ? 0 : 3, 0, 1);
        ui_text(VIEW_W / 2 + 110 - ui_text_width(yes, 0) / 2, by, yes, s->sel == 1 ? 0 : 3, 0, 1);
    }
}

#ifdef PACX_PORT
/* a keyboard of our own, steered with the D-pad or the pointer: 4 rows of 10 keys, then
   space, delete, cancel and OK (the game's font has capitals only) */
static const char *kbRows[4] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789-_.!"};
enum { KB_X = 50, KB_Y = 140, KB_W = 540, KB_KEYS_Y = 240, KB_KEY_W = 50, KB_KEY_H = 36 };

static int kb_cols(int row) { return row < 4 ? 10 : 4; }

static void kb_key_rect(int kx, int ky, float *x, float *y, float *w) {
    if (ky < 4) { *x = KB_X + 20 + kx * KB_KEY_W; *w = KB_KEY_W; }
    else { *x = KB_X + 20 + kx * 125; *w = 125; }
    *y = ky < 4 ? KB_KEYS_Y + ky * KB_KEY_H : KB_KEYS_Y + 4 * KB_KEY_H + 12;
}

static int kb_hit(float px, float py, int *kx, int *ky) {
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < kb_cols(r); c++) {
            float x, y, w;
            kb_key_rect(c, r, &x, &y, &w);
            if (px >= x && px < x + w && py >= y && py < y + KB_KEY_H) { *kx = c; *ky = r; return 1; }
        }
    return 0;
}

static void kb_finish(Screen *s, int ok) {
    char text[32];
    strncpy(text, s->buf, sizeof text - 1);
    text[sizeof text - 1] = 0;
    void (*done)(const char *) = s->done;
    pop();
    if (done) done(ok ? text : NULL);
}

static void kb_press(Screen *s, int kx, int ky) {
    int len = (int)strlen(s->buf);
    if (ky < 4) {
        if (len < s->maxc) { s->buf[len] = kbRows[ky][kx]; s->buf[len + 1] = 0; }
        return;
    }
    switch (kx) {
    case 0: if (len < s->maxc) { s->buf[len] = ' '; s->buf[len + 1] = 0; } break;
    case 1: if (len) s->buf[len - 1] = 0; break;
    case 2: kb_finish(s, 0); break;
    case 3: if (len) kb_finish(s, 1); break;
    }
}

static void osk_update(Screen *s) {
    u32 rep = in_menu_repeat(), pr = in_menu_pressed();
    float px, py;
    int hx, hy;
    if (in_pointer(&px, &py) && kb_hit(px, py, &hx, &hy)) {
        static float lx, ly;
        if (px != lx || py != ly) { s->kx = hx; s->ky = hy; }
        lx = px;
        ly = py;
        if (in_click()) { s->kx = hx; s->ky = hy; kb_press(s, hx, hy); return; }
    }
    if (rep & MB_LEFT) s->kx = (s->kx + kb_cols(s->ky) - 1) % kb_cols(s->ky);
    if (rep & MB_RIGHT) s->kx = (s->kx + 1) % kb_cols(s->ky);
    if (rep & MB_UP) {
        if (s->ky == 4) s->kx = s->kx * 10 / 4;
        s->ky = (s->ky + 4) % 5;
        if (s->ky == 4) s->kx = s->kx * 4 / 10;
    }
    if (rep & MB_DOWN) {
        if (s->ky == 4) s->kx = s->kx * 10 / 4;
        s->ky = (s->ky + 1) % 5;
        if (s->ky == 4) s->kx = s->kx * 4 / 10;
    }
    if (s->kx >= kb_cols(s->ky)) s->kx = kb_cols(s->ky) - 1;
    if (pr & MB_OK) kb_press(s, s->kx, s->ky);
    else if (pr & MB_BACK) { int len = (int)strlen(s->buf); if (len) s->buf[len - 1] = 0; }
    else if (pr & MB_PLUS) { if (s->buf[0]) kb_finish(s, 1); }
    else if (pr & (MB_MINUS | MB_HOME)) kb_finish(s, 0);
    char ch;
    while (in_text(&ch)) {
        int len = (int)strlen(s->buf);
        if (ch == '\n') { if (len) { kb_finish(s, 1); return; } }
        else if (ch == 27) { kb_finish(s, 0); return; }
        else if (ch == '\b') { if (len) s->buf[len - 1] = 0; }
        else if (len < s->maxc) { s->buf[len] = ch; s->buf[len + 1] = 0; }
    }
}

static void osk_draw(Screen *s) {
    ui_panel(KB_X, KB_Y - 70, KB_W, 375);
    char lines[3][80];
    int n = wrap(s->text, lines, 3, 62);
    for (int i = 0; i < n; i++) ui_text_center(KB_Y - 62 + i * 16, lines[i], 0, 1, 1);
    char shown[40];
    snprintf(shown, sizeof shown, "%s_", s->buf);
    gfx_rect(KB_X + 20, KB_Y - 8, KB_W - 40, 28, 0, 0, 0, 0.6f);
    ui_text(KB_X + 28, KB_Y - 2, shown, 2, 0, 1);
    static const char *special[4] = {"Space", "Delete", "Cancel", "OK"};
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < kb_cols(r); c++) {
            float x, y, w;
            kb_key_rect(c, r, &x, &y, &w);
            int sel = (r == s->ky && c == s->kx);
            gfx_rect(x + 2, y + 2, w - 4, KB_KEY_H - 4, sel ? 0.35f : 0.1f, sel ? 0.45f : 0.12f, sel ? 1.0f : 0.3f, 0.9f);
            char k[2] = {0, 0};
            const char *t;
            if (r < 4) { k[0] = kbRows[r][c]; t = k; }
            else t = special[c];
            ui_text(x + (w - ui_text_width(t, 0)) / 2, y + 10, t, sel ? 0 : 3, 0, 1);
        }
}
#else
/* ================================================================== the Wii keyboard */

static void osk_update(Screen *s) {
    osk_input in;
    OSK_ReadInputWii(&in);
    osk_status st = OSK_Update(s->osk, &in);
    if (st == OSK_STATUS_EDITING) return;
    char text[128];
    OSK_CopyText(s->osk, text, sizeof text);
    OSK_Destroy(s->osk);
    s->osk = NULL;
    INPUT_SetScreenSize(VIEW_W, VIEW_H);
    void (*done)(const char *) = s->done;
    pop();
    if (done) done(st == OSK_STATUS_DONE ? text : NULL);
}

static void osk_draw(Screen *s) {
    if (!s->osk) return;
    OSK_Draw(s->osk);
    gfx_restore_state();
    /* the question of the Mac panel, above the keyboard */
    char lines[3][80];
    int n = wrap(s->text, lines, 3, 76);
    if (n) {
        gfx_rect(0, 0, VIEW_W, 12 + n * 16, 0, 0, 0.08f, 0.8f);
        for (int i = 0; i < n; i++) ui_text_center(6 + i * 16, lines[i], 0, 1, 1);
    }
}

#endif

/* ================================================================== high score names */

static int namePlayers;     /* nameInput0/1/2: the players still to ask */
static int nameAsking;

static void name_next(void);

static void name_done(const char *text) {
    int p = nameAsking;
    if (text) {
        /* stringByTrimmingCharactersInSet: whitespaceAndNewlineCharacterSet */
        while (*text == ' ') text++;
        char t[24];
        snprintf(t, sizeof t, "%s", text);
        int l = strlen(t);
        while (l > 0 && t[l - 1] == ' ') t[--l] = 0;
        snprintf(cfg.lastEntry[p - 1], sizeof cfg.lastEntry[p - 1], "%s", t);
        settings_save();
    }
    eng_post(p == 1 ? EV_NEW_SCORE1 : EV_NEW_SCORE2, 0, 0);
    name_next();
}

static void name_next(void) {
    if (namePlayers & 1) nameAsking = 1;
    else if (namePlayers & 2) nameAsking = 2;
    else return;
    namePlayers &= ~(1 << (nameAsking - 1));
    char title[96];
    snprintf(title, sizeof title, tr("Congrats"), nameAsking);
    ui_text_entry(title, cfg.lastEntry[nameAsking - 1], 19, name_done);
}

/* ================================================================== preferences */

enum {
    P_DIFFICULTY, P_MUSIC, P_SOUND, P_LEVELS, P_SCREEN, P_TRY_LEVEL, P_TRY_PLAYER, P_TRY,
    P_EDITOR, P_CLEAR, P_DONE, P_QUIT, P_COUNT
};

static char levelFiles[32][64];
static int nLevelFiles;

static const char *prefs_label(int i) {
    static const char *l[P_COUNT] = {"DIFFICULTY", "MUSIC", "SOUND", "LEVELS", "SCREEN", "TRY LEVEL",
                                     "TRY WITH", "START TRY LEVEL", "LEVEL EDITOR", "CLEAR HIGHSCORES",
                                     "DONE", "QUIT"};
    return l[i];
}

static const char *bar(float v) {
    static char b[16];
    int n = (int)(v * 10 + 0.5f);
    for (int i = 0; i < 10; i++) b[i] = i < n ? '|' : '.';
    b[10] = 0;
    return b;
}

static const char *prefs_value(int i) {
    static char v[64];
    static const char *diff[4] = {"LevelEasy", "LevelNormal", "LevelHard", "LevelMaster"};
    switch (i) {
    case P_DIFFICULTY: return tr(diff[(cfg.difficulty >= 1 && cfg.difficulty <= 4 ? cfg.difficulty : 2) - 1]);
    case P_MUSIC: return bar(cfg.musicVol);
    case P_SOUND: return bar(cfg.soundVol);
    case P_LEVELS:
        levelfile_display(cfg.levelFile, v, 19);
        return v;
    case P_SCREEN:
        return cfg.aspect == ASPECT_4_3 ? "4:3" : cfg.aspect == ASPECT_STRETCH ? "STRETCH" : "AUTO";
    case P_TRY_LEVEL:
        snprintf(v, sizeof v, "%d", cfg.tryLevel);
        return v;
    case P_TRY_PLAYER:
        return cfg.tryPlayer == 1 ? "PLAYER 1" : cfg.tryPlayer == 2 ? "PLAYER 2" : "BOTH";
    }
    return NULL;
}

static int count_levels_of(const char *file) {
    if (!file[0]) return 25;
    LevelSet s;
    char p[300];
    snprintf(p, sizeof p, "%s/levels/%s", appDir, file);
    int n = levels_load_file(&s, p) ? s.n : 0;
    levels_free(&s);
    return n > 0 ? n : 1;
}

static void prefs_change(int i, int d) {
    switch (i) {
    case P_DIFFICULTY:
        cfg.difficulty += d;
        if (cfg.difficulty < 1) cfg.difficulty = 4;
        if (cfg.difficulty > 4) cfg.difficulty = 1;
        break;
    case P_MUSIC:
    case P_SOUND: {
        float *v = i == P_MUSIC ? &cfg.musicVol : &cfg.soundVol;
        int n = (int)(*v * 10 + 0.5f) + d;
        if (n < 0) n = 0;
        if (n > 10) n = 10;
        *v = n / 10.0f;
        mus_set_volume(cfg.musicVol);
        snd_set_volume(cfg.soundVol);
        if (i == P_SOUND) snd_play(SND_EAT_PELLET_B);
        break;
    }
    case P_LEVELS: {
        nLevelFiles = levelfiles_list(levelFiles, 32);
        int cur = 0;
        for (int k = 0; k < nLevelFiles; k++)
            if (!strcmp(levelFiles[k], cfg.levelFile)) cur = k;
        cur = (cur + d + nLevelFiles) % nLevelFiles;
        snprintf(cfg.levelFile, sizeof cfg.levelFile, "%s", levelFiles[cur]);
        break;
    }
    case P_SCREEN:
        cfg.aspect = (cfg.aspect + d + 3) % 3;
        gfx_set_aspect(cfg.aspect);
        break;
    case P_TRY_LEVEL: {
        int n = count_levels_of(cfg.levelFile);
        cfg.tryLevel += d;
        if (cfg.tryLevel < 1) cfg.tryLevel = n;
        if (cfg.tryLevel > n) cfg.tryLevel = 1;
        break;
    }
    case P_TRY_PLAYER:
        cfg.tryPlayer = (cfg.tryPlayer + d + 3) % 3;
        break;
    }
}

static void prefs_close(void) {
    ui_menu_close();
    settings_save();
    game_update_settings();
}

static void clear_scores(void) { eng_post(EV_CLEAR_SCORES, 0, 0); }

static void prefs_activate(int i) {
    switch (i) {
    case P_TRY: {
        int n = count_levels_of(cfg.levelFile);
        if (cfg.tryLevel < 1 || cfg.tryLevel > n) cfg.tryLevel = 1;
        if (cfg.tryPlayer == 0 && cfg.difficulty == 4) {
            ui_alert(tr("NoTwoPlayer"));
            return;
        }
        prefs_close();
        eng_post(EV_TRY_LEVEL, 0, 0);
        break;
    }
    case P_EDITOR:
        prefs_close();
        editor_open();
        break;
    case P_CLEAR:
        ui_confirm(tr("ClearScoresTitle"), tr("ClearScoresText"), clear_scores);
        break;
    case P_DONE:
        prefs_close();
        break;
    case P_QUIT:
        prefs_close();
        quitRequested = 1;
        break;
    default:
        prefs_change(i, 1);
        break;
    }
}

void ui_open_prefs(void) {
    UiMenu m = {0};
    m.title = "PREFERENCES";
    m.n = P_COUNT;
    m.sel = P_DONE;
    m.label = prefs_label;
    m.value = prefs_value;
    m.change = prefs_change;
    m.activate = prefs_activate;
    m.back = prefs_close;
    ui_menu_open(&m);
}

/* ================================================================== frame */

void ui_init(void) {
#ifndef PACX_PORT
    OSK_InitSound(false);
#endif
}

void ui_update(void) {
    if (!depth) {
        /* panels the game asks for */
        if (game.alertPending) {
            game.alertPending = 0;
            ui_alert(tr("NoTwoPlayer"));
        } else if (game.nameInputPending) {
            namePlayers = game.nameInputPending;
            game.nameInputPending = 0;
            name_next();
        }
        return;
    }
    Screen *s = &stack[depth - 1];
    switch (s->type) {
    case S_MENU: menu_update(s); break;
    case S_ALERT:
    case S_CONFIRM: dialog_update(s); break;
    case S_OSK: osk_update(s); break;
    }
}

void ui_draw(void) {
    for (int i = 0; i < depth; i++) {
        Screen *s = &stack[i];
        switch (s->type) {
        case S_MENU:
            if (i == 0) gfx_rect(0, 0, VIEW_W, VIEW_H, 0, 0, 0, 0.35f);
            menu_draw(&s->menu);
            break;
        case S_ALERT:
        case S_CONFIRM: dialog_draw(s); break;
        case S_OSK: osk_draw(s); break;
        }
    }
}
