#ifndef EDITOR_H
#define EDITOR_H
/*
 * Pac the Man X Editor (PacEditorDelegate): the maze is edited where it is shown in the game,
 * with the pointer. The tools, the path rules and the citadel rules are those of the Mac editor.
 */

void editor_open(void);         /* from the preferences: opens the level file in use */
void editor_resume(void);       /* back from trying a level */
int  editor_active(void);
void editor_update(void);
void editor_draw(void);

#endif
