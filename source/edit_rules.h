#ifndef EDIT_RULES_H
#define EDIT_RULES_H
/*
 * The editing rules of the Mac Pac the Man X Editor (PacEditorDelegate), shared by the Wii
 * editor and the Mac one. Each level is a 13x10 grid of letters, 'A' + the open sides (1 left,
 * 2 right, 4 up, 8 down); Q and R make up the citadel, R being its entry. mod is the modifier
 * key that bypasses the checks. The callers keep the undo history.
 */
#include "levels.h"

/* the maze in the game's 640x480 view */
#define ED_VX 34
#define ED_VY 36
#define ED_CELL 44

/* a drag of the path or eraser tool, in cells */
typedef struct {
    int startCol, startRow, endCol, endRow;
    int vertical;       /* the drag goes along a column */
    int pokeHole;       /* the path was dragged off the maze: it wraps around */
} EdDrag;

void ed_update_paths(Level *l, const EdDrag *d, int erasing, int mod);   /* updatePaths:erasing: */
int  ed_can_place(const Level *l, int c, int r, int mod);                /* canPlaceItemAtColumn:row: */
int  ed_is_super_pellet(const Level *l, int c, int r);
int  ed_toggle_pellet(Level *l, int c, int r, int mod);                  /* 0 when nothing changed */
int  ed_set_player(Level *l, int nr, int c, int r, int mod);             /* 0 when nothing changed */
int  ed_find_citadel(const Level *l, int *cc, int *rr);                  /* its top left cell */
void ed_move_citadel(Level *l, int cc, int rr);
void ed_citadel_cell(float px, float py, int *cc, int *rr);              /* view point -> top left cell */
int  ed_cell_at(float px, float py, int *c, int *r);                     /* clamped; 0 when outside */
void ed_blank_level(Level *l);

#endif
