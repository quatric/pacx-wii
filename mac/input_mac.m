/*
 * The keys of the original (set in the preferences, the arrow keys and A S D W by default),
 * Return to join, Space to pause and Esc to leave, plus the mouse in the game view. Game
 * controllers steer as well: the first one player 1, the second one player 2. In a
 * one-player game both sets of keys steer, like the original.
 */
#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#import <Carbon/Carbon.h>
#include <math.h>
#include <string.h>
#include "input.h"
#include "macplat.h"

NSString *const kKeyDefaults[8] = {@"Left1", @"Right1", @"Up1", @"Down1", @"Left2", @"Right2", @"Up2", @"Down2"};
const unsigned short kKeyDefaultCodes[8] = {123, 124, 126, 125, 0, 2, 13, 1};

enum { KC_RETURN = 36, KC_ENTER = 76, KC_SPACE = 49, KC_ESC = 53 };

static unsigned short keyCodes[8];
static u8 down[128], latch[128];        /* held now, and went down since the last frame */
static u8 held[KEY_COUNT], prev[KEY_COUNT];
static u32 menuHeld, menuPrev, menuPressed, menuRepeat;
static int repeatTimer, anyPressed;
static u32 prevPadBits[2];
static u8 join[2];
static int shift;

static float mouseX, mouseY;
static int mouseInside;
static u8 buttonDown[2], buttonLatch[2], buttonPrev[2], buttonNow[2];

void in_mac_load_keys(void) {
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    for (int i = 0; i < 8; i++) {
        id v = [ud objectForKey:kKeyDefaults[i]];
        keyCodes[i] = v ? (unsigned short)[v integerValue] : kKeyDefaultCodes[i];
    }
}

void in_init(void) { in_mac_load_keys(); }

void in_mac_key(unsigned short k, int d) {
    if (k >= 128) return;
    down[k] = d;
    if (d) latch[k] = 1;
}

void in_mac_flags(NSEventModifierFlags f) { shift = (f & NSEventModifierFlagShift) != 0; }

void in_mac_mouse(float x, float y, int inside) {
    mouseX = x;
    mouseY = y;
    mouseInside = inside;
}

void in_mac_button(int b, int d) {
    if (b < 0 || b > 1) return;
    buttonDown[b] = d;
    if (d) buttonLatch[b] = 1;
}

void in_mac_release_all(void) {
    memset(down, 0, sizeof down);
    buttonDown[0] = buttonDown[1] = 0;
    shift = 0;
}

static int key(unsigned short k) { return k < 128 && (down[k] || latch[k]); }

/* ------------------------------------------------------------------ game controllers */

enum { X_JOIN = 16, X_PAUSE = 32, X_CANCEL = 64 };

static u32 stick_dir(float x, float y) {
    if (sqrtf(x * x + y * y) < 0.45f) return 0;
    /* the stronger axis wins, so a diagonal does not press two directions */
    if (fabsf(x) > fabsf(y)) return x < 0 ? 1 : 2;
    return y > 0 ? 4 : 8;
}

static u32 pad_bits(GCController *c) {
    GCExtendedGamepad *g = c.extendedGamepad;
    if (!g) return 0;
    u32 bits = 0;
    if (g.dpad.left.pressed) bits |= 1;
    if (g.dpad.right.pressed) bits |= 2;
    if (g.dpad.up.pressed) bits |= 4;
    if (g.dpad.down.pressed) bits |= 8;
    if (!bits) bits |= stick_dir(g.leftThumbstick.xAxis.value, g.leftThumbstick.yAxis.value);
    if (g.buttonA.pressed || g.buttonB.pressed || g.buttonX.pressed || g.buttonY.pressed) bits |= X_JOIN;
    if (g.buttonMenu.pressed) bits |= X_PAUSE;
    if (g.buttonOptions.pressed) bits |= X_CANCEL;
    return bits;
}

static u32 pad_menu(GCController *c) {
    GCExtendedGamepad *g = c.extendedGamepad;
    if (!g) return 0;
    u32 m = 0;
    if (g.dpad.left.pressed) m |= MB_LEFT;
    if (g.dpad.right.pressed) m |= MB_RIGHT;
    if (g.dpad.up.pressed) m |= MB_UP;
    if (g.dpad.down.pressed) m |= MB_DOWN;
    float x = g.leftThumbstick.xAxis.value, y = g.leftThumbstick.yAxis.value;
    if (sqrtf(x * x + y * y) > 0.6f) {
        if (fabsf(x) > fabsf(y)) m |= x < 0 ? MB_LEFT : MB_RIGHT;
        else m |= y > 0 ? MB_UP : MB_DOWN;
    }
    if (g.buttonA.pressed) m |= MB_OK;
    if (g.buttonB.pressed) m |= MB_BACK;
    if (g.buttonMenu.pressed) m |= MB_PLUS;
    if (g.buttonOptions.pressed) m |= MB_MINUS;
    return m;
}

/* ------------------------------------------------------------------ the frame */

void in_update(void) {
    memcpy(prev, held, sizeof held);
    memset(held, 0, sizeof held);

    NSArray<GCController *> *pads = [GCController controllers];
    u32 b[2] = {0, 0}, padMenu = 0;
    for (NSUInteger i = 0; i < pads.count; i++) {
        if (i < 2) b[i] = pad_bits(pads[i]);
        padMenu |= pad_menu(pads[i]);
    }
    for (int p = 0; p < 2; p++) {
        int base = p ? KEY_LEFT2 : KEY_LEFT1;
        for (int d = 0; d < 4; d++)
            if (b[p] & (1u << d)) held[base + d] = 1;
        if (b[p] & X_JOIN) held[KEY_JOIN] = 1;
        join[p] = (b[p] & X_JOIN) != 0;
        if (b[p] & X_PAUSE) held[KEY_PAUSE] = 1;
        if (b[p] & X_CANCEL) held[KEY_CANCEL] = 1;
    }
    for (int i = 0; i < 8; i++)
        if (key(keyCodes[i])) held[KEY_LEFT1 + i] = 1;
    if (key(KC_RETURN) || key(KC_ENTER)) held[KEY_JOIN] = 1;
    if (key(KC_SPACE)) held[KEY_PAUSE] = 1;
    if (key(KC_ESC)) held[KEY_CANCEL] = 1;

    anyPressed = 0;
    for (int k = 0; k < 128; k++)
        if (latch[k]) anyPressed = 1;
    for (int p = 0; p < 2; p++) {
        if (b[p] & ~prevPadBits[p]) anyPressed = 1;
        prevPadBits[p] = b[p];
    }

    u32 m = padMenu;
    if (key(123)) m |= MB_LEFT;
    if (key(124)) m |= MB_RIGHT;
    if (key(126)) m |= MB_UP;
    if (key(125)) m |= MB_DOWN;
    if (key(KC_RETURN) || key(KC_ENTER)) m |= MB_OK;
    if (key(KC_ESC)) m |= MB_BACK;
    for (int i = 0; i < 2; i++) {
        buttonPrev[i] = buttonNow[i];
        buttonNow[i] = buttonDown[i] || buttonLatch[i];
        buttonLatch[i] = 0;
    }
    if (buttonNow[0]) m |= MB_CLICK;
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
    memset(latch, 0, sizeof latch);
}

int in_key(int k) { return held[k]; }

int in_join(int player) {
    if (key(KC_RETURN) || key(KC_ENTER)) return 1;
    return player >= 1 && player <= 2 && join[player - 1];
}

int in_key_pressed(int k) { return held[k] && !prev[k]; }
int in_any_pressed(void) { return anyPressed || (menuPressed & ~MB_CLICK); }

u32 in_menu_held(void) { return menuHeld; }
u32 in_menu_pressed(void) { return menuPressed; }
u32 in_menu_repeat(void) { return menuRepeat; }

int in_pointer(float *x, float *y) {
    if (!mouseInside) return 0;
    *x = mouseX;
    *y = mouseY;
    return 1;
}

int in_click(void) { return buttonNow[0] && !buttonPrev[0]; }
int in_click_held(void) { return buttonNow[0]; }
int in_right_click(void) { return buttonNow[1] && !buttonPrev[1]; }
int in_shift(void) { return shift; }
int in_text(char *ch) { (void)ch; return 0; }

/* ------------------------------------------------------------------ key names */

NSString *in_mac_key_name(unsigned short k) {
    switch (k) {
    case 123: return @"←";
    case 124: return @"→";
    case 125: return @"↓";
    case 126: return @"↑";
    case 36: return @"Return";
    case 76: return @"Enter";
    case 49: return @"Space";
    case 48: return @"Tab";
    case 51: return @"Delete";
    case 117: return @"⌦";
    case 115: return @"Home";
    case 119: return @"End";
    case 116: return @"Page Up";
    case 121: return @"Page Down";
    case 53: return @"Esc";
    }
    static const char *keypad[] = {
        [65] = ".", [67] = "*", [69] = "+", [75] = "/", [78] = "-", [81] = "=",
        [82] = "0", [83] = "1", [84] = "2", [85] = "3", [86] = "4", [87] = "5", [88] = "6", [89] = "7",
        [91] = "8", [92] = "9",
    };
    if (k < sizeof keypad / sizeof keypad[0] && keypad[k]) return [NSString stringWithFormat:@"Keypad %s", keypad[k]];
    /* the character the key types with the current keyboard layout */
    TISInputSourceRef src = TISCopyCurrentKeyboardLayoutInputSource();
    CFDataRef data = src ? TISGetInputSourceProperty(src, kTISPropertyUnicodeKeyLayoutData) : NULL;
    NSString *name = nil;
    if (data) {
        const UCKeyboardLayout *lay = (const UCKeyboardLayout *)CFDataGetBytePtr(data);
        UInt32 dead = 0;
        UniChar chars[4];
        UniCharCount n = 0;
        if (UCKeyTranslate(lay, k, kUCKeyActionDisplay, 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit, &dead, 4,
                           &n, chars) == noErr && n > 0)
            name = [[NSString stringWithCharacters:chars length:n] uppercaseString];
    }
    if (src) CFRelease(src);
    return name.length ? name : [NSString stringWithFormat:@"Key %d", k];
}
