#ifndef UIMENU_H
#define UIMENU_H

/* a list of rows; rows with a value change with left / right, the others run with A */
typedef struct {
    const char *title;
    int n, sel;
    int top;                            /* first row shown, for lists longer than the screen */
    const char *(*label)(int i);
    const char *(*value)(int i);        /* NULL, or NULL for a row: the row is an action */
    void (*change)(int i, int dir);
    void (*activate)(int i);
    void (*back)(void);
} UiMenu;

#define MENU_ROWS 11

void ui_menu_open(const UiMenu *m);
void ui_menu_close(void);

#endif
