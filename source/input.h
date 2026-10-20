#ifndef INPUT_H
#define INPUT_H
#include <gctypes.h>

/* the original's keys: Left1..Down1 (arrows), Left2..Down2 (A S D W), Return (join),
   Space (pause) and Esc (cancel) */
enum {
    KEY_LEFT1, KEY_RIGHT1, KEY_UP1, KEY_DOWN1,
    KEY_LEFT2, KEY_RIGHT2, KEY_UP2, KEY_DOWN2,
    KEY_JOIN, KEY_PAUSE, KEY_CANCEL,
    KEY_COUNT
};

/* menu buttons, for the Wii screens (from any controller) */
enum {
    MB_UP = 1, MB_DOWN = 2, MB_LEFT = 4, MB_RIGHT = 8, MB_OK = 16, MB_BACK = 32,
    MB_PLUS = 64, MB_MINUS = 128, MB_HOME = 256, MB_1 = 512, MB_2 = 1024, MB_CLICK = 2048,
};

void in_init(void);
void in_update(void);

int in_key(int key);                /* held now */
int in_join(int player);            /* the join button of that player's controller, or Return */
int in_key_pressed(int key);        /* went down this frame */
int in_any_pressed(void);           /* any game key or controller button went down */

u32 in_menu_held(void);
u32 in_menu_pressed(void);
u32 in_menu_repeat(void);           /* pressed, plus auto-repeat while held */

int in_pointer(float *x, float *y); /* pointer in view coordinates; 0 when off screen */
int in_click(void);                 /* A on the pointer, or the left mouse button, went down */
int in_click_held(void);
int in_right_click(void);           /* B on the pointer, or the right mouse button */

int in_text(char *ch);              /* next typed character from a USB keyboard; '\b' for backspace, '\n' for return */
int in_shift(void);                 /* a modifier for the editor (B, Z, shift) */

#endif
