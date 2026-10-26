/*
 * The screens, sound, buttons and touch screen of the 3DS port (citro2d and ndsp). The game's
 * 640x480 view is drawn at half size: on the bottom screen (320x240, so touches map straight
 * onto the view) and in the middle of the top screen.
 */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "gfx.h"
#include "pack.h"
#include "input.h"
#include "settings.h"
#include "port.h"

/* ================================================================== system */

static int quitRequested, audioOk;
static char dataDir[256], saveDir[256];

int port_quit_requested(void) { return quitRequested || !aptMainLoop(); }
const char *port_data_dir(void) { return dataDir; }

const char *port_language(void) {
    u8 lang = 1;
    if (R_SUCCEEDED(cfguInit())) {
        CFGU_GetSystemLanguage(&lang);
        cfguExit();
    }
    return lang == CFG_LANGUAGE_DE ? "de" : lang == CFG_LANGUAGE_FR ? "fr" : "en";
}

int port_storage_dir(char *out, int size) {
    snprintf(out, size, "%s", saveDir);
    mkdir("sdmc:/3ds", 0777);
    mkdir(out, 0777);
    port_copy_levels(dataDir, saveDir);
    return 1;
}

void port_fatal(const char *message) {
    consoleInit(GFX_TOP, NULL);
    printf("Pac the Man X\n\n%s\n\nPress START to exit.\n", message);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
    romfsExit();
    exit(1);
}

void port_init(void) {
    romfsInit();
    snprintf(dataDir, sizeof dataDir, "romfs:");
    snprintf(saveDir, sizeof saveDir, "sdmc:/3ds/pacx");
    gfxInitDefault();
    gfxSet3D(false);
}

void port_shutdown(void) {
    if (audioOk) ndspExit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
}

/* ================================================================== screen */

#define MAX_TEX 160
#define MAX_CMDS 12000
static Tex textures[MAX_TEX];
static char texnames[MAX_TEX][40];
static int ntex;
static Tex white;
static C3D_RenderTarget *top, *bottom;

typedef struct {
    const Tex *t;               /* NULL: a plain rectangle */
    float sx, sy, sw, sh, x, y, w, h, angle;
    int flip;
    u32 color;
} Cmd;
static Cmd cmds[MAX_CMDS];
static int ncmd;

int gfx_is_widescreen(void) { return 0; }
int gfx_is_pal50(void) { return 0; }
void gfx_restore_state(void) {}
void gfx_set_aspect(int mode) { (void)mode; }
float gfx_screen_to_view_x(float x) { return x; }

static int pow2(int v) {
    int r = 8;
    while (r < v) r <<= 1;
    return r;
}

/* the PICA200 keeps textures in 8x8 tiles, Morton order inside */
static inline u32 tile_index(int x, int y, int w) {
    u32 m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return ((y >> 3) * (w >> 3) + (x >> 3)) * 64 + m;
}

static C3D_Tex *make_texture(const u8 *rgba, int w, int h, int opaque) {
    int tw = pow2(w), th = pow2(h);
    C3D_Tex *tex = malloc(sizeof *tex);
    if (!tex) return NULL;
    if (!C3D_TexInit(tex, tw, th, opaque ? GPU_RGB565 : GPU_RGBA8)) { free(tex); return NULL; }
    memset(tex->data, 0, tw * th * (opaque ? 2 : 4));
    for (int y = 0; y < h; y++) {
        int ty = y;
        for (int x = 0; x < w; x++) {
            const u8 *p = rgba + (y * w + x) * 4;
            u32 idx = tile_index(x, ty, tw);
            if (opaque) {
                ((u16 *)tex->data)[idx] = (u16)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
            } else {
                u8 *d = (u8 *)tex->data + idx * 4;
                d[0] = p[3]; d[1] = p[2]; d[2] = p[1]; d[3] = p[0];
            }
        }
    }
    C3D_TexFlush(tex);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return tex;
}

void gfx_init(void) {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(8192);
    C2D_Prepare();
    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    static const u8 px[8 * 8 * 4] = {[0 ... 255] = 255};
    white.obj = make_texture(px, 8, 8, 0);
    white.w = white.h = 8;
    white.pw = white.ph = 8;
}

Tex *gfx_tex(const char *name) {
    for (int i = 0; i < ntex; i++)
        if (strcmp(texnames[i], name) == 0) return &textures[i];
    const PackEntry *e = pack_find(name);
    if (!e || e->type != PK_TEX || ntex >= MAX_TEX) return NULL;
    Tex *t = &textures[ntex];
    strncpy(texnames[ntex], name, 39);
    ntex++;
    int opaque = !strncmp(name, "bg/", 3) || !strcmp(name, "title") || !strcmp(name, "ending") ||
                 !strcmp(name, "highscore_title");
    t->w = e->w; t->h = e->h; t->pw = pow2(e->w); t->ph = pow2(e->h); t->fmt = e->fmt; t->mem = NULL;
    t->obj = make_texture(pack_data(e), e->w, e->h, opaque);
    return t;
}

Tex *gfx_tex_rgba(const u8 *rgba, int w, int h) {
    Tex *t = calloc(1, sizeof(Tex));
    t->w = w; t->h = h; t->pw = pow2(w); t->ph = pow2(h); t->fmt = PK_RGBA8;
    t->obj = make_texture(rgba, w, h, 0);
    return t;
}

void gfx_tex_free(Tex *t) {
    if (!t || t->mem) return;
    if (t->obj) { C3D_TexDelete((C3D_Tex *)t->obj); free(t->obj); }
    free(t);
}

void gfx_begin(void) { ncmd = 0; }

static inline u8 c8(float v) {
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    return (u8)(v * 255.0f + 0.5f);
}

static void replay(float ox, float oy) {
    const float s = 0.5f;
    C2D_SetTintMode(C2D_TintMult);
    for (int i = 0; i < ncmd; i++) {
        const Cmd *c = &cmds[i];
        if (!c->t) {
            C2D_DrawRectSolid(ox + c->x * s, oy + c->y * s, 0, c->w * s, c->h * s, c->color);
            continue;
        }
        const Tex *t = c->t;
        Tex3DS_SubTexture sub;
        sub.width = (u16)c->sw;
        sub.height = (u16)c->sh;
        float l = c->sx / t->pw, r = (c->sx + c->sw) / t->pw;
        sub.left = c->flip ? r : l;
        sub.right = c->flip ? l : r;
        sub.top = 1.0f - c->sy / t->ph;
        sub.bottom = 1.0f - (c->sy + c->sh) / t->ph;
        C2D_Image img = {(C3D_Tex *)t->obj, &sub};
        C2D_DrawParams p = {
            {ox + c->x * s, oy + c->y * s, c->w * s, c->h * s},
            {0, 0},
            0.0f, 0.0f,
        };
        if (c->angle > 0.001f || c->angle < -0.001f) {
            /* turns about the middle of the picture: pos is where that middle goes */
            p.pos.x += c->w * s * 0.5f;
            p.pos.y += c->h * s * 0.5f;
            p.center.x = c->w * s * 0.5f;
            p.center.y = c->h * s * 0.5f;
            p.angle = -c->angle * (float)M_PI / 180.0f;
        }
        if (c->color == 0xFFFFFFFF) {
            C2D_DrawImage(img, &p, NULL);
        } else {
            C2D_ImageTint tint;
            C2D_PlainImageTint(&tint, c->color, 1.0f);
            C2D_DrawImage(img, &p, &tint);
        }
    }
}

void gfx_end(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
    C2D_TargetClear(bottom, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(top);
    replay(40, 0);
    C2D_SceneBegin(bottom);
    replay(0, 0);
    C3D_FrameEnd(0);
}

void gfx_quad(const Tex *t, float qx, float qy, float qw, float qh,
              float x, float y, float w, float h, float angle, int flip,
              float r, float g, float b, float a) {
    if (!t || !t->obj || a <= 0.001f || ncmd >= MAX_CMDS) return;
    Cmd *c = &cmds[ncmd++];
    c->t = t == &white ? NULL : t;
    c->sx = qx; c->sy = qy; c->sw = qw; c->sh = qh;
    c->x = x; c->y = y; c->w = w; c->h = h;
    c->angle = angle; c->flip = flip;
    c->color = C2D_Color32(c8(r), c8(g), c8(b), c8(a));
}

void gfx_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    gfx_quad(&white, 0, 0, 8, 8, x, y, w, h, 0, 0, r, g, b, a);
}

void gfx_frame(float x, float y, float w, float h, float t, float r, float g, float b, float a) {
    gfx_rect(x, y, w, t, r, g, b, a);
    gfx_rect(x, y + h - t, w, t, r, g, b, a);
    gfx_rect(x, y + t, t, h - 2 * t, r, g, b, a);
    gfx_rect(x + w - t, y + t, t, h - 2 * t, r, g, b, a);
}

void gfx_cursor(float x, float y) {
    /* a plain arrow, outlined, tip at (x, y) */
    static const char *shape[] = {
        "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
        "X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ",
        "X.X X..X    ", "XX  X..X    ", "X    X..X   ", "     X..X   ", "      XX    ",
    };
    for (int j = 0; j < 17; j++)
        for (int i = 0; i < 12; i++) {
            char c = shape[j][i];
            if (c == 'X') gfx_rect(x + i, y + j, 1, 1, 0, 0, 0, 1);
            else if (c == '.') gfx_rect(x + i, y + j, 1, 1, 1, 1, 1, 1);
        }
}

int gfx_grab(u8 *rgb, int w, int h) { (void)rgb; (void)w; (void)h; return 0; }

/* ================================================================== sound output */

#define ABUF_FRAMES 1024
#define NABUF 4
static ndspWaveBuf waveBufs[NABUF];
static s16 *abuf[NABUF];
static LightLock audioLock;
static volatile int audioRun;
static Thread audioThread;

void port_audio_lock(void) { LightLock_Lock(&audioLock); }
void port_audio_unlock(void) { LightLock_Unlock(&audioLock); }

static void audio_thread(void *arg) {
    (void)arg;
    while (audioRun) {
        for (int i = 0; i < NABUF; i++) {
            ndspWaveBuf *wb = &waveBufs[i];
            if (wb->status != NDSP_WBUF_DONE && wb->status != NDSP_WBUF_FREE) continue;
            LightLock_Lock(&audioLock);
            audio_mix(abuf[i], ABUF_FRAMES);
            LightLock_Unlock(&audioLock);
            wb->data_pcm16 = abuf[i];
            wb->nsamples = ABUF_FRAMES;
            DSP_FlushDataCache(abuf[i], ABUF_FRAMES * 4);
            ndspChnWaveBufAdd(0, wb);
        }
        svcSleepThread(4 * 1000 * 1000LL);
    }
}

void port_audio_open(void) {
    LightLock_Init(&audioLock);
    if (R_FAILED(ndspInit())) return;       /* no DSP firmware dumped: play without sound */
    audioOk = 1;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetRate(0, AUDIO_RATE);
    float mix[12] = {1.0f, 1.0f};
    ndspChnSetMix(0, mix);
    for (int i = 0; i < NABUF; i++) {
        abuf[i] = linearAlloc(ABUF_FRAMES * 4);
        memset(abuf[i], 0, ABUF_FRAMES * 4);
        memset(&waveBufs[i], 0, sizeof waveBufs[i]);
        waveBufs[i].status = NDSP_WBUF_DONE;
    }
    audioRun = 1;
    audioThread = threadCreate(audio_thread, NULL, 32 * 1024, 0x18, -2, false);
}

/* ================================================================== input */

static u8 held[KEY_COUNT], prev[KEY_COUNT];
static u32 menuHeld, menuPrev, menuPressed, menuRepeat;
static int repeatTimer, anyPressed;
static u32 prevPadBits[2];
static u8 join[2];
static int touching, touchPressed;
static float ptrX, ptrY;
static int shiftHeld;

enum { X_JOIN = 16, X_PAUSE = 32, X_CANCEL = 64 };

void in_init(void) {}

static u32 stick_dir(float x, float y) {
    if (sqrtf(x * x + y * y) < 0.45f) return 0;
    if (fabsf(x) > fabsf(y)) return x < 0 ? 1 : 2;
    return y > 0 ? 4 : 8;
}

void in_update(void) {
    hidScanInput();
    u32 h = hidKeysHeld();
    circlePosition cp, cs;
    hidCircleRead(&cp);
    irrstScanInput();
    irrstCstickRead(&cs);

    memcpy(prev, held, sizeof held);
    memset(held, 0, sizeof held);

    /* player 1: the D-pad and the Circle Pad; player 2: the face buttons as a diamond (X up,
       B down, Y left, A right); L or R joins, START pauses, SELECT goes back */
    u32 b[2] = {0, 0};
    if (h & KEY_DLEFT) b[0] |= 1;
    if (h & KEY_DRIGHT) b[0] |= 2;
    if (h & KEY_DUP) b[0] |= 4;
    if (h & KEY_DDOWN) b[0] |= 8;
    if (!(b[0] & 15)) b[0] |= stick_dir(cp.dx / 156.0f, cp.dy / 156.0f);
    if (h & KEY_Y) b[1] |= 1;
    if (h & KEY_A) b[1] |= 2;
    if (h & KEY_X) b[1] |= 4;
    if (h & KEY_B) b[1] |= 8;
    if (!(b[1] & 15)) b[1] |= stick_dir(cs.dx / 156.0f, cs.dy / 156.0f);
    if (h & (KEY_L | KEY_R)) { b[0] |= X_JOIN; b[1] |= X_JOIN; }
    if (h & KEY_START) b[0] |= X_PAUSE;
    if (h & KEY_SELECT) b[0] |= X_CANCEL;

    for (int p = 0; p < 2; p++) {
        int base = p ? KEY_LEFT2 : KEY_LEFT1;
        for (int d = 0; d < 4; d++)
            if (b[p] & (1u << d)) held[base + d] = 1;
        if (b[p] & X_JOIN) held[KEY_JOIN] = 1;
        join[p] = (b[p] & X_JOIN) != 0;
        if (b[p] & X_PAUSE) held[KEY_PAUSE] = 1;
        if (b[p] & X_CANCEL) held[KEY_CANCEL] = 1;
    }
    shiftHeld = (h & (KEY_B | KEY_ZL | KEY_ZR)) != 0;

    anyPressed = 0;
    for (int i = 0; i < KEY_COUNT; i++)
        if (held[i] && !prev[i]) anyPressed = 1;
    for (int p = 0; p < 2; p++) {
        if (b[p] & ~prevPadBits[p]) anyPressed = 1;
        prevPadBits[p] = b[p];
    }

    u32 m = 0;
    if (h & KEY_DUP) m |= MB_UP;
    if (h & KEY_DDOWN) m |= MB_DOWN;
    if (h & KEY_DLEFT) m |= MB_LEFT;
    if (h & KEY_DRIGHT) m |= MB_RIGHT;
    {
        float sx = cp.dx / 156.0f, sy = cp.dy / 156.0f;
        if (sqrtf(sx * sx + sy * sy) > 0.6f) {
            if (fabsf(sx) > fabsf(sy)) m |= sx < 0 ? MB_LEFT : MB_RIGHT;
            else m |= sy > 0 ? MB_UP : MB_DOWN;
        }
    }
    if (h & KEY_A) m |= MB_OK;
    if (h & KEY_B) m |= MB_BACK;
    if (h & KEY_START) m |= MB_PLUS;
    if (h & KEY_SELECT) m |= MB_MINUS;
    if (h & KEY_X) m |= MB_HOME;
    if (h & KEY_Y) m |= MB_1;
    if (h & (KEY_L | KEY_R)) m |= MB_2;

    touchPressed = 0;
    if (h & KEY_TOUCH) {
        touchPosition tp;
        hidTouchRead(&tp);
        if (!touching) touchPressed = 1;
        touching = 1;
        ptrX = tp.px * 2.0f;
        ptrY = tp.py * 2.0f;
        m |= MB_CLICK;
    } else {
        touching = 0;
    }

    menuPrev = menuHeld;
    menuHeld = m;
    menuPressed = m & ~menuPrev;
    menuRepeat = menuPressed;
    u32 dirs = m & (MB_UP | MB_DOWN | MB_LEFT | MB_RIGHT);
    if (dirs && (dirs & menuPrev) == dirs) {
        if (++repeatTimer > 24 && (repeatTimer - 24) % 5 == 0) menuRepeat |= dirs;
    } else {
        repeatTimer = 0;
    }
}

int in_key(int key) { return held[key]; }
int in_join(int player) { return player >= 1 && player <= 2 && join[player - 1]; }
int in_key_pressed(int key) { return held[key] && !prev[key]; }
int in_any_pressed(void) { return anyPressed || (menuPressed & ~MB_CLICK); }

u32 in_menu_held(void) { return menuHeld; }
u32 in_menu_pressed(void) { return menuPressed; }
u32 in_menu_repeat(void) { return menuRepeat; }

int in_pointer(float *x, float *y) {
    if (!touching) return 0;
    *x = ptrX;
    *y = ptrY;
    return 1;
}

int in_click(void) { return touchPressed; }
int in_click_held(void) { return touching; }
int in_right_click(void) { return 0; }
int in_shift(void) { return shiftHeld; }
int in_text(char *ch) { (void)ch; return 0; }
