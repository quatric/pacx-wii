#ifndef UI_H
#define UI_H
/*
 * The Wii screens that stand in for the Mac's windows and panels: preferences, the name entry
 * after a high score, alerts and confirmations, and text entry with the Wii keyboard.
 */

/* the game's fonts: 0 orange, 1 pink, 2 blue, 3 grey */
void ui_text(float x, float y, const char *s, int color, int small, float alpha);
int  ui_text_width(const char *s, int small);
void ui_text_center(float y, const char *s, int color, int small, float alpha);
void ui_panel(float x, float y, float w, float h);

void ui_init(void);
int  ui_active(void);           /* a panel is open: the game waits, like behind a modal window */
void ui_update(void);           /* input for the open panel; also opens the panels the game asks for */
void ui_draw(void);

void ui_open_prefs(void);
void ui_alert(const char *text);
void ui_confirm(const char *title, const char *text, void (*yes)(void));

/* text entry with the Wii keyboard; done gets NULL when cancelled */
void ui_text_entry(const char *title, const char *initial, int maxChars, void (*done)(const char *text));

int  ui_quit_requested(void);
void ui_close_all(void);         /* closes every panel without running its actions */

#endif
