/*
 * Player 1 plays with the controllers on channel 1 (Wii Remote held sideways, or with a
 * Nunchuk or Classic Controller, GameCube port 1) and the arrow keys; player 2 with channel 2
 * and W A S D, as on the Mac. In a one-player game both sets steer, like the original.
 */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <math.h>
#include <string.h>
#include <input_wii.h>
#include "input.h"
#include "gfx.h"

static u8 held[KEY_COUNT], prev[KEY_COUNT];
static u32 menuHeld, menuPrev, menuPressed, menuRepeat;
static int repeatTimer;
static int anyPressed;
static u32 prevPadBits[2];
static u8 join[2];

#define HID_RIGHT 0x4F
#define HID_LEFT 0x50
#define HID_DOWN 0x51
#define HID_UP 0x52
#define HID_A 0x04
#define HID_S 0x16
#define HID_D 0x07
#define HID_W 0x1A
#define HID_RETURN 0x28
#define HID_ESC 0x29
#define HID_SPACE 0x2C
#define HID_KP_ENTER 0x58

void in_init(void) {
    INPUT_Init();
    INPUT_SetScreenSize(640, 480);
    INPUT_SetCursorMode(INPUT_CURSOR_IR | INPUT_CURSOR_MOUSE);
}

/* direction bits (1 left, 2 right, 4 up, 8 down) and extra bits from one controller channel */
enum { X_JOIN = 16, X_PAUSE = 32, X_CANCEL = 64 };

static u32 stick_dir(float x, float y, float mag) {
    if (mag < 0.45f) return 0;
    /* the stronger axis wins, so a diagonal does not press two directions */
    if (fabsf(x) > fabsf(y)) return x < 0 ? 1 : 2;
    return y > 0 ? 4 : 8;
}

static u32 channel_bits(int chan) {
    u32 bits = 0;
    u32 type;
    if (WPAD_Probe(chan, &type) == WPAD_ERR_NONE) {
        WPADData *wd = WPAD_Data(chan);
        u32 h = WPAD_ButtonsHeld(chan);
        if (wd && wd->exp.type == WPAD_EXP_CLASSIC) {
            if (h & WPAD_CLASSIC_BUTTON_LEFT) bits |= 1;
            if (h & WPAD_CLASSIC_BUTTON_RIGHT) bits |= 2;
            if (h & WPAD_CLASSIC_BUTTON_UP) bits |= 4;
            if (h & WPAD_CLASSIC_BUTTON_DOWN) bits |= 8;
            if (!bits) {
                joystick_t *j = &wd->exp.classic.ljs;
                float a = j->ang * (float)M_PI / 180.0f;
                bits |= stick_dir(sinf(a) * j->mag, cosf(a) * j->mag, j->mag);
            }
            if (h & (WPAD_CLASSIC_BUTTON_A | WPAD_CLASSIC_BUTTON_B)) bits |= X_JOIN;
            if (h & WPAD_CLASSIC_BUTTON_PLUS) bits |= X_PAUSE;
            if (h & (WPAD_CLASSIC_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_HOME)) bits |= X_CANCEL;
        } else if (wd && wd->exp.type == WPAD_EXP_NUNCHUK) {
            joystick_t *j = &wd->exp.nunchuk.js;
            float a = j->ang * (float)M_PI / 180.0f;
            bits |= stick_dir(sinf(a) * j->mag, cosf(a) * j->mag, j->mag);
            if (h & WPAD_BUTTON_LEFT) bits |= 1;
            if (h & WPAD_BUTTON_RIGHT) bits |= 2;
            if (h & WPAD_BUTTON_UP) bits |= 4;
            if (h & WPAD_BUTTON_DOWN) bits |= 8;
            if (h & (WPAD_BUTTON_A | WPAD_NUNCHUK_BUTTON_Z | WPAD_NUNCHUK_BUTTON_C)) bits |= X_JOIN;
        } else {
            /* held sideways, the D-pad on the left */
            if (h & WPAD_BUTTON_UP) bits |= 1;
            if (h & WPAD_BUTTON_DOWN) bits |= 2;
            if (h & WPAD_BUTTON_RIGHT) bits |= 4;
            if (h & WPAD_BUTTON_LEFT) bits |= 8;
            if (h & (WPAD_BUTTON_1 | WPAD_BUTTON_2 | WPAD_BUTTON_A)) bits |= X_JOIN;
        }
        if (h & WPAD_BUTTON_PLUS) bits |= X_PAUSE;
        if (h & (WPAD_BUTTON_MINUS | WPAD_BUTTON_HOME)) bits |= X_CANCEL;
    }
    u16 p = PAD_ButtonsHeld(chan);
    if (p & PAD_BUTTON_LEFT) bits |= 1;
    if (p & PAD_BUTTON_RIGHT) bits |= 2;
    if (p & PAD_BUTTON_UP) bits |= 4;
    if (p & PAD_BUTTON_DOWN) bits |= 8;
    if (!(bits & 15)) {
        float x = PAD_StickX(chan) / 70.0f, y = PAD_StickY(chan) / 70.0f;
        bits |= stick_dir(x, y, sqrtf(x * x + y * y));
    }
    if (p & (PAD_BUTTON_A | PAD_BUTTON_B)) bits |= X_JOIN;
    if (p & PAD_BUTTON_START) bits |= X_PAUSE;
    if (p & PAD_TRIGGER_Z) bits |= X_CANCEL;
    return bits;
}

void in_update(void) {
    INPUT_Update();
    memcpy(prev, held, sizeof held);
    memset(held, 0, sizeof held);

    u32 b[2] = {channel_bits(0), channel_bits(1)};
    for (int p = 0; p < 2; p++) {
        int base = p ? KEY_LEFT2 : KEY_LEFT1;
        for (int d = 0; d < 4; d++)
            if (b[p] & (1u << d)) held[base + d] = 1;
        if (b[p] & X_JOIN) held[KEY_JOIN] = 1;
        join[p] = (b[p] & X_JOIN) != 0;
        if (b[p] & X_PAUSE) held[KEY_PAUSE] = 1;
        if (b[p] & X_CANCEL) held[KEY_CANCEL] = 1;
    }
    if (INPUT_KeyHeld(HID_LEFT)) held[KEY_LEFT1] = 1;
    if (INPUT_KeyHeld(HID_RIGHT)) held[KEY_RIGHT1] = 1;
    if (INPUT_KeyHeld(HID_UP)) held[KEY_UP1] = 1;
    if (INPUT_KeyHeld(HID_DOWN)) held[KEY_DOWN1] = 1;
    if (INPUT_KeyHeld(HID_A)) held[KEY_LEFT2] = 1;
    if (INPUT_KeyHeld(HID_D)) held[KEY_RIGHT2] = 1;
    if (INPUT_KeyHeld(HID_W)) held[KEY_UP2] = 1;
    if (INPUT_KeyHeld(HID_S)) held[KEY_DOWN2] = 1;
    if (INPUT_KeyHeld(HID_RETURN) || INPUT_KeyHeld(HID_KP_ENTER)) held[KEY_JOIN] = 1;
    if (INPUT_KeyHeld(HID_SPACE)) held[KEY_PAUSE] = 1;
    if (INPUT_KeyHeld(HID_ESC)) held[KEY_CANCEL] = 1;

    anyPressed = 0;
    for (int k = 0; k < KEY_COUNT; k++)
        if (held[k] && !prev[k]) anyPressed = 1;
    for (int p = 0; p < 2; p++) {
        if (b[p] & ~prevPadBits[p]) anyPressed = 1;
        prevPadBits[p] = b[p];
    }

    /* menus: every device folded together by libinput-wii, D-pad as printed */
    u32 bt = INPUT_ButtonsHeld();
    u32 m = 0;
    if (bt & WPAD_BUTTON_UP) m |= MB_UP;
    if (bt & WPAD_BUTTON_DOWN) m |= MB_DOWN;
    if (bt & WPAD_BUTTON_LEFT) m |= MB_LEFT;
    if (bt & WPAD_BUTTON_RIGHT) m |= MB_RIGHT;
    if (bt & (WPAD_BUTTON_A | WPAD_BUTTON_2 | INPUT_BUTTON_C)) m |= MB_OK;
    if (bt & (WPAD_BUTTON_B | WPAD_BUTTON_1 | INPUT_BUTTON_Z)) m |= MB_BACK;
    if (bt & WPAD_BUTTON_PLUS) m |= MB_PLUS;
    if (bt & WPAD_BUTTON_MINUS) m |= MB_MINUS;
    if (bt & WPAD_BUTTON_HOME) m |= MB_HOME;
    if (bt & WPAD_BUTTON_1) m |= MB_1;
    if (bt & WPAD_BUTTON_2) m |= MB_2;
    float sx, sy;
    if (INPUT_Stick(INPUT_STICK_LEFT, &sx, &sy, NULL) > 0.6f) {
        if (fabsf(sx) > fabsf(sy)) m |= sx < 0 ? MB_LEFT : MB_RIGHT;
        else m |= sy > 0 ? MB_UP : MB_DOWN;
    }
    if (INPUT_MouseButtonsHeld() & INPUT_MOUSE_LEFT) m |= MB_CLICK;
    float px, py;
    if ((bt & WPAD_BUTTON_A) && INPUT_Pointer(&px, &py)) m |= MB_CLICK;
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
    if (INPUT_KeyHeld(HID_RETURN) || INPUT_KeyHeld(HID_KP_ENTER)) return 1;
    return player >= 1 && player <= 2 && join[player - 1];
}
int in_key_pressed(int key) { return held[key] && !prev[key]; }
int in_any_pressed(void) { return anyPressed || (menuPressed & ~MB_CLICK); }

u32 in_menu_held(void) { return menuHeld; }
u32 in_menu_pressed(void) { return menuPressed; }
u32 in_menu_repeat(void) { return menuRepeat; }

int in_pointer(float *x, float *y) {
    if (!INPUT_CursorActive()) return 0;
    int ix, iy;
    INPUT_CursorPosition(&ix, &iy);
    *x = gfx_screen_to_view_x((float)ix);
    *y = (float)iy;
    return 1;
}

int in_click(void) {
    if (INPUT_MouseButtonsPressed() & INPUT_MOUSE_LEFT) return 1;
    float x, y;
    return (INPUT_PadsDown() & WPAD_BUTTON_A) && INPUT_Pointer(&x, &y);
}

int in_click_held(void) {
    if (INPUT_MouseButtonsHeld() & INPUT_MOUSE_LEFT) return 1;
    float x, y;
    return (INPUT_PadsHeld() & WPAD_BUTTON_A) && INPUT_Pointer(&x, &y);
}

int in_right_click(void) {
    if (INPUT_MouseButtonsPressed() & INPUT_MOUSE_RIGHT) return 1;
    float x, y;
    return (INPUT_PadsDown() & WPAD_BUTTON_B) && INPUT_Pointer(&x, &y);
}

int in_shift(void) {
    if (INPUT_KeyModifiers() & INPUT_MOD_SHIFT) return 1;
    return (INPUT_PadsHeld() & (WPAD_BUTTON_B | INPUT_BUTTON_Z)) != 0;
}

int in_text(char *ch) {
    input_key_event ev;
    while (INPUT_KeyPop(&ev)) {
        if (ev.code == 0x2A) { *ch = '\b'; return 1; }
        if (ev.code == HID_RETURN || ev.code == HID_KP_ENTER) { *ch = '\n'; return 1; }
        if (ev.code == HID_ESC) { *ch = 27; return 1; }
        if (ev.ch >= 32 && ev.ch < 127) { *ch = ev.ch; return 1; }
    }
    return 0;
}
