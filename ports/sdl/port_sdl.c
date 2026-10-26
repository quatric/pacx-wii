/*
 * The screen, sound, controllers and start-up of the Switch and Wii U ports, on SDL2. The
 * game draws into a 640x480 view; this file puts it in the middle of the screen (pillarboxed
 * on a 16:9 picture unless stretched) and turns SDL's controllers, keyboard, mouse and touch
 * screen into the game's keys, menu buttons and pointer.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>
#if defined(__SWITCH__)
#include <switch.h>
#elif defined(__WIIU__)
#include <whb/sdcard.h>
#endif
#include "gfx.h"
#include "pack.h"
#include "input.h"
#include "settings.h"
#include "port.h"

/* ================================================================== system */

static SDL_Window *window;
static SDL_Renderer *renderer;
static int quitRequested;
static char dataDir[256], saveDir[256];

int port_quit_requested(void) { return quitRequested; }
const char *port_data_dir(void) { return dataDir; }

#ifdef PACX_DESKTOP
const char *port_language(void) {
    const char *r = "en";
    SDL_Locale *l = SDL_GetPreferredLocales();
    for (SDL_Locale *p = l; p && p->language; p++) {
        if (!strcmp(p->language, "de")) r = "de";
        else if (!strcmp(p->language, "fr")) r = "fr";
        else if (strcmp(p->language, "en")) continue;
        else r = "en";
        break;
    }
    SDL_free(l);
    return r;
}
#else
const char *port_language(void) { return "en"; }
#endif

static void make_dirs(char *path) {
    for (char *s = strchr(path, ':') ? strchr(path, ':') + 2 : path + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(path, 0777); *s = '/'; }
    mkdir(path, 0777);
}

int port_storage_dir(char *out, int size) {
    snprintf(out, size, "%s", saveDir);
    make_dirs(out);
    if (strcmp(dataDir, saveDir) != 0) port_copy_levels(dataDir, saveDir);
    return 1;
}

void port_fatal(const char *message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pac the Man X", message, window);
    port_shutdown();
    exit(1);
}

void port_shutdown(void) {
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
#if defined(__SWITCH__)
    romfsExit();
#endif
}

#ifdef PACX_DESKTOP
static int has_pack(const char *dir) {
    char p[600];
    snprintf(p, sizeof p, "%s/pack.bin", dir);
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static void slashes(char *s) {
    for (; *s; s++)
        if (*s == '\\') *s = '/';
}

/* the data (pack.bin, music/, levels/) is found in PACX_DATA, in data/ next to the program, in
   ../share/pacx or in ./data; the settings, scores and level files go in the user's folder of
   SDL (~/.local/share/quatric/Pac the Man X, %APPDATA%\quatric\Pac the Man X, PACX_HOME) */
static void desktop_dirs(void) {
    char *base = SDL_GetBasePath();
    char c1[300] = "", c2[300] = "";
    if (base) {
        snprintf(c1, sizeof c1, "%sdata", base);
        snprintf(c2, sizeof c2, "%s../share/pacx", base);
    }
    const char *env = getenv("PACX_DATA");
    const char *cands[] = {env, c1, c2, "data", "ports/data", "."};
    snprintf(dataDir, sizeof dataDir, "%s", ".");
    for (int i = 0; i < 6; i++)
        if (cands[i] && *cands[i] && has_pack(cands[i])) {
            snprintf(dataDir, sizeof dataDir, "%s", cands[i]);
            break;
        }
    SDL_free(base);
    const char *home = getenv("PACX_HOME");
    char *pref = home && *home ? SDL_strdup(home) : SDL_GetPrefPath("quatric", "Pac the Man X");
    snprintf(saveDir, sizeof saveDir, "%s", pref ? pref : dataDir);
    SDL_free(pref);
    slashes(dataDir);
    slashes(saveDir);
    for (size_t n = strlen(saveDir); n > 1 && saveDir[n - 1] == '/'; ) saveDir[--n] = 0;
}
#endif

void port_init(void) {
#if defined(__SWITCH__)
    romfsInit();
    snprintf(dataDir, sizeof dataDir, "romfs:");
    snprintf(saveDir, sizeof saveDir, "sdmc:/switch/pacx");
#elif defined(__WIIU__)
    const char *sd = WHBMountSdCard() ? WHBGetSdCardMountPath() : NULL;
    snprintf(saveDir, sizeof saveDir, "%s/wiiu/apps/pacx", sd ? sd : "fs:/vol/external01");
    snprintf(dataDir, sizeof dataDir, "%s", saveDir);
#else
    desktop_dirs();
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) exit(1);
}

/* ================================================================== screen */

#define MAX_TEX 160
static Tex textures[MAX_TEX];
static char texnames[MAX_TEX][40];
static int ntex;
static SDL_Texture *whiteTex;
static Tex white;
static int aspect = ASPECT_AUTO;
static float ox, oy, sx = 1, sy = 1;      /* view -> screen */

int gfx_is_widescreen(void) { return 1; }
int gfx_is_pal50(void) { return 0; }
void gfx_restore_state(void) {}

static void update_transform(void) {
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    if (aspect == ASPECT_STRETCH) {
        sx = w / (float)VIEW_W;
        sy = h / (float)VIEW_H;
        ox = oy = 0;
    } else {
        float k = fminf(w / (float)VIEW_W, h / (float)VIEW_H);
        sx = sy = k;
        ox = (w - VIEW_W * k) / 2;
        oy = (h - VIEW_H * k) / 2;
    }
}

void gfx_set_aspect(int mode) {
    aspect = mode;
    if (renderer) update_transform();
}

float gfx_screen_to_view_x(float x) { return (x - ox) / sx; }

static SDL_Texture *make_texture(const u8 *rgba, int w, int h) {
    SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return NULL;
    SDL_UpdateTexture(t, NULL, rgba, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    return t;
}

void gfx_init(void) {
#ifdef PACX_DESKTOP
    window = SDL_CreateWindow("Pac the Man X", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
#else
    window = SDL_CreateWindow("Pac the Man X", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
#endif
#if defined(__SWITCH__) || defined(__WIIU__)
                              SDL_WINDOW_FULLSCREEN
#else
                              SDL_WINDOW_RESIZABLE
#endif
                              );
    if (!window) exit(1);
#ifdef PACX_DESKTOP
    /* the game runs at 60 steps a second: vsync only on a display of about 60 Hz, else the
       frames are paced here (gfx_end) */
    SDL_DisplayMode dm;
    int vsync = SDL_GetWindowDisplayMode(window, &dm) == 0 && dm.refresh_rate >= 57 && dm.refresh_rate <= 63;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
#else
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#endif
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) exit(1);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_ShowCursor(SDL_DISABLE);
    static const u8 px[4 * 4 * 4] = {[0 ... 63] = 255};
    whiteTex = make_texture(px, 4, 4);
    white.obj = whiteTex;
    white.w = white.h = white.pw = white.ph = 4;
    update_transform();
}

Tex *gfx_tex(const char *name) {
    for (int i = 0; i < ntex; i++)
        if (strcmp(texnames[i], name) == 0) return &textures[i];
    const PackEntry *e = pack_find(name);
    if (!e || e->type != PK_TEX || ntex >= MAX_TEX) return NULL;
    Tex *t = &textures[ntex];
    strncpy(texnames[ntex], name, 39);
    ntex++;
    t->w = e->w; t->h = e->h; t->pw = e->pw; t->ph = e->ph; t->fmt = e->fmt; t->mem = NULL;
    t->obj = make_texture(pack_data(e), e->pw, e->ph);
    return t;
}

Tex *gfx_tex_rgba(const u8 *rgba, int w, int h) {
    Tex *t = calloc(1, sizeof(Tex));
    t->w = t->pw = w;
    t->h = t->ph = h;
    t->fmt = PK_RGBA8;
    t->obj = make_texture(rgba, w, h);
    return t;
}

void gfx_tex_free(Tex *t) {
    if (!t || t->mem) return;
    if (t->obj) SDL_DestroyTexture((SDL_Texture *)t->obj);
    free(t);
}

void gfx_begin(void) {
    update_transform();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

#ifdef PACX_DESKTOP
/* never faster than 60 frames a second, whatever the display does */
static void pace(void) {
    static Uint64 next;
    Uint64 freq = SDL_GetPerformanceFrequency(), now = SDL_GetPerformanceCounter(), step = freq / 60;
    if (!next || now > next + step * 4) next = now;
    while (now < next) {
        Uint64 ms = (next - now) * 1000 / freq;
        if (ms > 1) SDL_Delay((Uint32)(ms - 1));
        now = SDL_GetPerformanceCounter();
    }
    next += step;
}
#endif

void gfx_end(void) {
#ifdef PACX_DESKTOP
    char *shots = getenv("PACX_SHOTS");      /* every 60th frame as a BMP into that folder, for testing */
    static int frameNo;
    if (shots && ++frameNo % 60 == 0) {
        int w, h;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (sf) {
            char path[600];
            snprintf(path, sizeof path, "%s/frame%04d.bmp", shots, frameNo);
            if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch) == 0)
                SDL_SaveBMP(sf, path);
            SDL_FreeSurface(sf);
        }
    }
    pace();
#endif
    SDL_RenderPresent(renderer);
}

static inline u8 c8(float v) {
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    return (u8)(v * 255.0f + 0.5f);
}

void gfx_quad(const Tex *t, float qx, float qy, float qw, float qh,
              float x, float y, float w, float h, float angle, int flip,
              float r, float g, float b, float a) {
    if (!t || !t->obj || a <= 0.001f) return;
    float u0 = qx / t->pw, v0 = qy / t->ph, u1 = (qx + qw) / t->pw, v1 = (qy + qh) / t->ph;
    if (flip) { float tmp = u0; u0 = u1; u1 = tmp; }
    float px[4] = {x, x + w, x + w, x}, py[4] = {y, y, y + h, y + h};
    if (angle > 0.001f || angle < -0.001f) {
        /* glRotatef(angle) in a y-down view: positive angles turn clockwise on screen */
        float cx = x + w * 0.5f, cy = y + h * 0.5f;
        float rad = angle * (float)M_PI / 180.0f, c = cosf(rad), s = sinf(rad);
        for (int i = 0; i < 4; i++) {
            float dx = px[i] - cx, dy = py[i] - cy;
            px[i] = cx + dx * c - dy * s;
            py[i] = cy + dx * s + dy * c;
        }
    }
    SDL_Color col = {c8(r), c8(g), c8(b), c8(a)};
    float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    SDL_Vertex v[4];
    for (int i = 0; i < 4; i++) {
        v[i].position.x = ox + px[i] * sx;
        v[i].position.y = oy + py[i] * sy;
        v[i].color = col;
        v[i].tex_coord.x = us[i];
        v[i].tex_coord.y = vs[i];
    }
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, (SDL_Texture *)t->obj, v, 4, idx, 6);
}

void gfx_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    gfx_quad(&white, 1, 1, 2, 2, x, y, w, h, 0, 0, r, g, b, a);
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

static SDL_AudioDeviceID audioDev;

static void audio_cb(void *user, Uint8 *stream, int len) {
    (void)user;
    audio_mix((s16 *)stream, len / 4);
}

void port_audio_lock(void) { if (audioDev) SDL_LockAudioDevice(audioDev); }
void port_audio_unlock(void) { if (audioDev) SDL_UnlockAudioDevice(audioDev); }

void port_audio_open(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    audioDev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audioDev) SDL_PauseAudioDevice(audioDev, 0);
}

/* ================================================================== input */

#define MAX_PADS 4
static SDL_GameController *pads[MAX_PADS];
static u8 held[KEY_COUNT], prev[KEY_COUNT];
static u32 menuHeld, menuPrev, menuPressed, menuRepeat;
static int repeatTimer, anyPressed;
static u32 prevPadBits[2];
static u8 join[2];
static int ptrVisible, ptrDown, ptrPressed, ptrRightPressed, ptrRightDown;
static float ptrX, ptrY;
static char textQ[32];
static int textN;
static int shiftHeld;

enum { X_JOIN = 16, X_PAUSE = 32, X_CANCEL = 64 };

void in_init(void) {
    SDL_StartTextInput();
}

static void text_push(char c) {
    if (textN < (int)sizeof textQ) textQ[textN++] = c;
}

static void open_pad(int index) {
    if (!SDL_IsGameController(index)) return;
    for (int i = 0; i < MAX_PADS; i++)
        if (!pads[i]) { pads[i] = SDL_GameControllerOpen(index); return; }
}

static void close_pad(SDL_JoystickID id) {
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) {
            SDL_GameControllerClose(pads[i]);
            pads[i] = NULL;
        }
}

static u32 stick_dir(float x, float y) {
    float mag = sqrtf(x * x + y * y);
    if (mag < 0.45f) return 0;
    if (fabsf(x) > fabsf(y)) return x < 0 ? 1 : 2;
    return y < 0 ? 4 : 8;
}

static int btn(SDL_GameController *c, SDL_GameControllerButton b) { return SDL_GameControllerGetButton(c, b); }
static float axis(SDL_GameController *c, SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(c, a) / 32767.0f; }

/* 1 left, 2 right, 4 up, 8 down, X_JOIN, X_PAUSE, X_CANCEL of one controller */
static u32 pad_bits(SDL_GameController *c) {
    u32 bits = 0;
    if (!c) return 0;
    if (btn(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) bits |= 1;
    if (btn(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) bits |= 2;
    if (btn(c, SDL_CONTROLLER_BUTTON_DPAD_UP)) bits |= 4;
    if (btn(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) bits |= 8;
    if (!(bits & 15))
        bits |= stick_dir(axis(c, SDL_CONTROLLER_AXIS_LEFTX), axis(c, SDL_CONTROLLER_AXIS_LEFTY));
    if (btn(c, SDL_CONTROLLER_BUTTON_A) || btn(c, SDL_CONTROLLER_BUTTON_B)) bits |= X_JOIN;
    if (btn(c, SDL_CONTROLLER_BUTTON_START)) bits |= X_PAUSE;
    if (btn(c, SDL_CONTROLLER_BUTTON_BACK)) bits |= X_CANCEL;
    return bits;
}

static u32 pad_menu(SDL_GameController *c) {
    u32 m = 0;
    if (!c) return 0;
    if (btn(c, SDL_CONTROLLER_BUTTON_DPAD_UP)) m |= MB_UP;
    if (btn(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) m |= MB_DOWN;
    if (btn(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) m |= MB_LEFT;
    if (btn(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) m |= MB_RIGHT;
    float sx_ = axis(c, SDL_CONTROLLER_AXIS_LEFTX), sy_ = axis(c, SDL_CONTROLLER_AXIS_LEFTY);
    if (sqrtf(sx_ * sx_ + sy_ * sy_) > 0.6f) {
        if (fabsf(sx_) > fabsf(sy_)) m |= sx_ < 0 ? MB_LEFT : MB_RIGHT;
        else m |= sy_ < 0 ? MB_UP : MB_DOWN;
    }
    if (btn(c, SDL_CONTROLLER_BUTTON_A)) m |= MB_OK;
    if (btn(c, SDL_CONTROLLER_BUTTON_B)) m |= MB_BACK;
    if (btn(c, SDL_CONTROLLER_BUTTON_START)) m |= MB_PLUS;
    if (btn(c, SDL_CONTROLLER_BUTTON_BACK)) m |= MB_MINUS;
    if (btn(c, SDL_CONTROLLER_BUTTON_X)) m |= MB_HOME;
    if (btn(c, SDL_CONTROLLER_BUTTON_Y)) m |= MB_1;
    if (btn(c, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || btn(c, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) m |= MB_2;
    return m;
}

static void pointer_from(float wx, float wy) {
    ptrX = gfx_screen_to_view_x(wx);
    ptrY = (wy - oy) / sy;
    ptrVisible = 1;
}

static void pump_events(void) {
    SDL_Event e;
    ptrPressed = ptrRightPressed = 0;
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: quitRequested = 1; break;
        case SDL_CONTROLLERDEVICEADDED: open_pad(e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: close_pad(e.cdevice.which); break;
        case SDL_MOUSEMOTION:
            pointer_from((float)e.motion.x, (float)e.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int down = e.type == SDL_MOUSEBUTTONDOWN;
            pointer_from((float)e.button.x, (float)e.button.y);
            if (e.button.button == SDL_BUTTON_LEFT) { if (down && !ptrDown) ptrPressed = 1; ptrDown = down; }
            else if (e.button.button == SDL_BUTTON_RIGHT) { if (down && !ptrRightDown) ptrRightPressed = 1; ptrRightDown = down; }
            break;
        }
        case SDL_TEXTINPUT:
            for (const char *p = e.text.text; *p; p++)
                if (*p >= 32 && *p < 127) text_push(*p);
            break;
        case SDL_KEYDOWN:
#ifdef PACX_DESKTOP
            if (!e.key.repeat && (e.key.keysym.sym == SDLK_F11 ||
                                  (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))))
                SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP)
                                                    ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
            if (e.key.keysym.sym == SDLK_BACKSPACE) text_push('\b');
            else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) text_push('\n');
            else if (e.key.keysym.sym == SDLK_ESCAPE) text_push(27);
            break;
        }
    }
}

void in_update(void) {
    pump_events();
    memcpy(prev, held, sizeof held);
    memset(held, 0, sizeof held);

    u32 b[2] = {pad_bits(pads[0]), pad_bits(pads[1])};
    for (int p = 0; p < 2; p++) {
        int base = p ? KEY_LEFT2 : KEY_LEFT1;
        for (int d = 0; d < 4; d++)
            if (b[p] & (1u << d)) held[base + d] = 1;
        if (b[p] & X_JOIN) held[KEY_JOIN] = 1;
        join[p] = (b[p] & X_JOIN) != 0;
        if (b[p] & X_PAUSE) held[KEY_PAUSE] = 1;
        if (b[p] & X_CANCEL) held[KEY_CANCEL] = 1;
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    if (k[SDL_SCANCODE_LEFT]) held[KEY_LEFT1] = 1;
    if (k[SDL_SCANCODE_RIGHT]) held[KEY_RIGHT1] = 1;
    if (k[SDL_SCANCODE_UP]) held[KEY_UP1] = 1;
    if (k[SDL_SCANCODE_DOWN]) held[KEY_DOWN1] = 1;
    if (k[SDL_SCANCODE_A]) held[KEY_LEFT2] = 1;
    if (k[SDL_SCANCODE_D]) held[KEY_RIGHT2] = 1;
    if (k[SDL_SCANCODE_W]) held[KEY_UP2] = 1;
    if (k[SDL_SCANCODE_S]) held[KEY_DOWN2] = 1;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]) held[KEY_JOIN] = 1;
    if (k[SDL_SCANCODE_SPACE]) held[KEY_PAUSE] = 1;
    if (k[SDL_SCANCODE_ESCAPE]) held[KEY_CANCEL] = 1;
    shiftHeld = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i] && (btn(pads[i], SDL_CONTROLLER_BUTTON_B) || axis(pads[i], SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.5f ||
                        axis(pads[i], SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f))
            shiftHeld = 1;

    anyPressed = 0;
    for (int i = 0; i < KEY_COUNT; i++)
        if (held[i] && !prev[i]) anyPressed = 1;
    for (int p = 0; p < 2; p++) {
        if (b[p] & ~prevPadBits[p]) anyPressed = 1;
        prevPadBits[p] = b[p];
    }

    u32 m = 0;
    for (int i = 0; i < MAX_PADS; i++) m |= pad_menu(pads[i]);
#ifdef PACX_DESKTOP
    /* the keyboard in the menus: arrows, Return and Esc; Tab or F1 stand for HOME (the preferences,
       the editor's menu) and = - 1 2 for the Wii's + - 1 2 in the editor */
    if (k[SDL_SCANCODE_UP]) m |= MB_UP;
    if (k[SDL_SCANCODE_DOWN]) m |= MB_DOWN;
    if (k[SDL_SCANCODE_LEFT]) m |= MB_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) m |= MB_RIGHT;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]) m |= MB_OK;
    if (k[SDL_SCANCODE_ESCAPE]) m |= MB_BACK;
    if (k[SDL_SCANCODE_TAB] || k[SDL_SCANCODE_F1]) m |= MB_HOME;
    if (k[SDL_SCANCODE_EQUALS] || k[SDL_SCANCODE_KP_PLUS]) m |= MB_PLUS;
    if (k[SDL_SCANCODE_MINUS] || k[SDL_SCANCODE_KP_MINUS]) m |= MB_MINUS;
    if (k[SDL_SCANCODE_1]) m |= MB_1;
    if (k[SDL_SCANCODE_2]) m |= MB_2;
#endif
    if (m) ptrVisible = 0;      /* the controller took over from the touch screen / mouse */
    if (ptrDown) m |= MB_CLICK;
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

int in_join(int player) {
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]) return 1;
    return player >= 1 && player <= 2 && join[player - 1];
}
int in_key_pressed(int key) { return held[key] && !prev[key]; }
int in_any_pressed(void) { return anyPressed || (menuPressed & ~MB_CLICK); }

u32 in_menu_held(void) { return menuHeld; }
u32 in_menu_pressed(void) { return menuPressed; }
u32 in_menu_repeat(void) { return menuRepeat; }

int in_pointer(float *x, float *y) {
    if (!ptrVisible) return 0;
    *x = ptrX;
    *y = ptrY;
    return 1;
}

int in_click(void) { return ptrPressed; }
int in_click_held(void) { return ptrDown; }
int in_right_click(void) { return ptrRightPressed; }
int in_shift(void) { return shiftHeld; }

int in_text(char *ch) {
    if (!textN) return 0;
    *ch = textQ[0];
    memmove(textQ, textQ + 1, --textN);
    return 1;
}
