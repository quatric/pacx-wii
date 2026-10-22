#ifndef MACPLAT_H
#define MACPLAT_H
/* what the Mac platform files (gfx, input, audio, ui) offer the application around them */
#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>

/* gfx_mac.m */
void gfx_mac_init(MTKView *view);
NSRect gfx_mac_view_rect(NSSize size);       /* where the 640x480 picture goes in a view of that size */
int gfx_mac_grab_png(const char *path);      /* writes the last frame, for the test run */

/* input_mac.m: the events of the game view */
void in_mac_key(unsigned short keyCode, int down);
void in_mac_flags(NSEventModifierFlags flags);
void in_mac_mouse(float x, float y, int inside);
void in_mac_button(int button, int down);    /* 0 left, 1 right */
void in_mac_release_all(void);               /* the window lost the keyboard */
void in_mac_load_keys(void);                 /* the game keys from the preferences */
NSString *in_mac_key_name(unsigned short keyCode);
extern NSString *const kKeyDefaults[8];      /* Left1 Right1 Up1 Down1 Left2 Right2 Up2 Down2 */
extern const unsigned short kKeyDefaultCodes[8];

/* ui_mac.m: the windows and panels of the original */
void ui_mac_set_window(NSWindow *w);
void ui_mac_show_try_panel(void);
void ui_mac_clear_scores(void);
void ui_mac_refresh_level_files(void);

/* main_mac.m */
void mac_open_editor(void);
void mac_open_levels_folder(void);
void mac_update_window_aspect(void);

#endif
