/*
 * Pac the Man X for the Wii: the start-up of AppController and the run loop. The game logic
 * runs at the original's 60 steps a second; on 50 Hz PAL six steps are made every five frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>
#include "gfx.h"
#include "pack.h"
#include "engine.h"
#include "game.h"
#include "actors.h"
#include "input.h"
#include "audio.h"
#include "settings.h"
#include "ui.h"
#include "editor.h"

#ifdef PACX_TEST
#define TRACE(...) printf(__VA_ARGS__)
#else
#define TRACE(...) do { } while (0)
#endif

static volatile int resetPressed, powerPressed;

static void on_reset(u32 irq, void *ctx) { resetPressed = 1; }
static void on_power(void) { powerPressed = 1; }
static void on_wpad_power(s32 chan) { powerPressed = 1; }

/* the title buttons with the D-pad: -1 until it is used */
static int splashSel = -1;

static Sprite *splash_button(int i) {
    for (int k = 0; k < layer_count(L(0)); k++) {
        Sprite *s = layer_at(L(0), k);
        if (s && s->kind == K_BUTTON && s->frameIndex == i) return s;
    }
    return NULL;
}

static void splash_input(void) {
    u32 rep = in_menu_repeat(), pr = in_menu_pressed();
    if (pr & MB_HOME) {
        ui_open_prefs();
        return;
    }
    if (rep & (MB_LEFT | MB_RIGHT | MB_UP | MB_DOWN)) {
        if (splashSel < 0) splashSel = 0;
        else if (rep & (MB_LEFT | MB_UP)) splashSel = (splashSel + 5) % 6;
        else splashSel = (splashSel + 1) % 6;
        game.automaticTimerStart = 45;
    }
    float x, y;
    if (in_pointer(&x, &y)) {
        static float lx, ly;
        if (x != lx || y != ly) splashSel = -1;
        lx = x;
        ly = y;
    }
    if ((pr & MB_OK) && !(pr & MB_CLICK) && splashSel >= 0) {
        Sprite *b = splash_button(splashSel);
        if (b) button_press(b);
    }
}

/* mouseDown: and keyDown: of the game view */
static void game_input(void) {
    if (game.mode == MODE_SPLASH) splash_input();
    float x, y;
    if (in_click() && in_pointer(&x, &y)) {
        eng_post(EV_MOUSE, x, y);
        return;
    }
    if (game.mode == MODE_GAME && game_is_paused() && (in_menu_pressed() & MB_HOME)) {
        /* HOME while paused: back to the title, as Esc */
        game.keyEvent = KEY_CANCEL;
        eng_post(EV_KEY, 0, 0);
        return;
    }
    if (!in_any_pressed()) return;
    if (game.mode == MODE_SPLASH) return;
    game.keyEvent = in_key_pressed(KEY_CANCEL) ? KEY_CANCEL : in_key_pressed(KEY_PAUSE) ? KEY_PAUSE : -1;
    eng_post(EV_KEY, 0, 0);
}

static void handle_events(void) {
    Event e;
    while (eng_poll(&e)) {
        if (e.type == EV_PREFS) ui_open_prefs();
        else game_handle_event(&e);
    }
}

static void draw_splash_selection(void) {
    if (game.mode != MODE_SPLASH || splashSel < 0) return;
    Sprite *b = splash_button(splashSel);
    if (b) gfx_frame(b->x - 2, b->y - 2, 52, 52, 2, 1, 1, 0.4f, 0.9f);
}

#ifdef PACX_TEST
/* the scripted run for Dolphin: screenshots of each screen into sd:/shots */
static void shot(int n) {
    static u8 rgb[640 * 480 * 3];
    TRACE("pacx: shot %d\n", n);
    gfx_grab(rgb, 640, 480);
    char root[32], p[300];
    snprintf(root, sizeof root, "%s", appDir);
    char *c = strstr(root, ":/");
    if (c) c[2] = 0;
    snprintf(p, sizeof p, "%sshots", root);
    mkdir(p, 0777);
    snprintf(p, sizeof p, "%sshots/%02d.bmp", root, n);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    u8 h[54] = {'B', 'M'};
    int size = 54 + 640 * 480 * 3;
    #define LE32(o, v) do { h[o] = (v) & 255; h[o + 1] = ((v) >> 8) & 255; h[o + 2] = ((v) >> 16) & 255; h[o + 3] = ((v) >> 24) & 255; } while (0)
    LE32(2, size);
    LE32(10, 54);
    LE32(14, 40);
    LE32(18, 640);
    LE32(22, 480);
    h[26] = 1;
    h[28] = 24;
    fwrite(h, 1, 54, f);
    static u8 row[640 * 3];
    for (int y = 479; y >= 0; y--) {
        for (int x = 0; x < 640; x++) {
            row[x * 3] = rgb[(y * 640 + x) * 3 + 2];
            row[x * 3 + 1] = rgb[(y * 640 + x) * 3 + 1];
            row[x * 3 + 2] = rgb[(y * 640 + x) * 3];
        }
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
}

static void name_test_done(const char *t) { (void)t; }
void editor_test(void);

/* returns 1 when the run is over */
static int test_script(int frame) {
    switch (frame) {
    case 150: shot(1); break;                                   /* title */
    case 160: eng_post(EV_NEW_GAME1, 0, 0); break;
    case 260: shot(2); break;                                   /* ready */
    case 900: shot(3); break;                                   /* playing */
    case 910: eng_post(EV_PAUSE, 0, 0); break;
    case 930: shot(4); break;                                   /* paused */
    case 940: game.keyEvent = KEY_CANCEL; eng_post(EV_KEY, 0, 0); break;
    case 1000: eng_post(EV_HIGHSCORES, 0, 0); break;
    case 1100: shot(5); break;                                  /* high scores */
    case 1110: game_show_splash(); eng_post(EV_HELP, 0, 0); break;
    case 1250: shot(6); break;                                  /* help */
    case 1260: game_show_splash(); break;
    case 1300: ui_open_prefs(); break;
    case 1320: shot(7); break;                                  /* preferences */
    case 1330: ui_close_all(); break;
    case 1340: editor_open(); break;
    case 1380: shot(8); break;                                  /* editor */
    case 1385: editor_test(); break;
    case 1395: shot(10); break;                                 /* edited level */
    case 1400: ui_text_entry("Congratulations Player 1! You reached the Top 10.", "BIBS", 19, name_test_done); break;
    case 1460: shot(9); break;                                  /* keyboard */
    case 1470: eng_post(EV_NEW_GAME2, 0, 0); break;
    case 1520: return 1;
    }
    return 0;
}
#endif

int main(int argc, char **argv) {
    gfx_init();
    TRACE("pacx: %s done\n", "gfx_init");
    pack_init();
    TRACE("pacx: %s done\n", "pack_init");
    in_init();
    TRACE("pacx: %s done\n", "in_init");
    storage_init(argc > 0 ? argv[0] : NULL);
    TRACE("pacx: %s done\n", "storage_init");
    settings_load();
    TRACE("pacx: %s done\n", "settings_load");
    strings_init();
    TRACE("pacx: %s done\n", "strings_init");
    hs_load();
    TRACE("pacx: %s done\n", "hs_load");
    gfx_set_aspect(cfg.aspect);
    TRACE("pacx: %s done\n", "gfx_set_aspect");
    char music[300];
    snprintf(music, sizeof music, "%s/music", appDir);
    audio_init(music);
    TRACE("pacx: %s done\n", "audio_init");
    eng_init();
    TRACE("pacx: %s done\n", "eng_init");
    game_init();
    TRACE("pacx: %s done\n", "game_init");
    ui_init();
    TRACE("pacx: %s done\n", "ui_init");

    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);
    WPAD_SetPowerButtonCallback(on_wpad_power);

    rnd_seed((u32)gettime());
    game_show_splash();

    int pal = gfx_is_pal50(), palPhase = 0;
    int frame = 0;
    while (!resetPressed && !powerPressed && !ui_quit_requested()) {
        in_update();
        audio_update();

        if (ui_active()) {
            ui_update();
        } else if (editor_active()) {
            editor_update();
        } else {
            ui_update();            /* opens the panels the game asked for */
            if (!ui_active()) game_input();
        }
        if (!editor_active() && !ui_active()) handle_events();
        if (game_take_editor_return()) editor_resume();

        /* the animation timer: 60 steps a second */
        int steps = 1;
        if (pal) {
            palPhase = (palPhase + 1) % 5;
            steps = palPhase == 0 ? 2 : 1;
        }
        if (!editor_active() && !ui_active())
            for (int i = 0; i < steps && anim.running; i++) eng_step();

        gfx_begin();
        if (editor_active()) {
            editor_draw();
        } else {
            gfx_rect(0, 0, VIEW_W, VIEW_H, 0, 0, 0, 1);
            eng_draw();
            draw_splash_selection();
        }
        ui_draw();
        float x, y;
        if (!editor_active() && in_pointer(&x, &y) && (game.mode != MODE_GAME || ui_active() || game_is_paused()))
            gfx_cursor(x, y);
        gfx_end();

#ifdef PACX_TEST
        if (test_script(++frame)) {
            settings_save();
            SYS_ResetSystem(SYS_POWEROFF, 0, 0);
        }
#else
        (void)frame;
#endif
    }

    settings_save();
    mus_stop();
    snd_stop_all();
    if (powerPressed) SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    return 0;
}
