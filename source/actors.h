#ifndef ACTORS_H
#define ACTORS_H
/* the PM* sprite classes (actors.c) */
#include "engine.h"

void actor_start(Sprite *s);                    /* PMSprite.start, from createSprite:xPos:yPos: */

void player_start(Sprite *s, int nr, int autoPlayer);
void player_set_frameset(Sprite *s);
void player_move_to_start(Sprite *s);
int  player_double_score(const Sprite *s);

void ghost_start(Sprite *s, int nr, int x, int y);
void ghost_set_frameset(Sprite *s);
void ghost_start_hunting(Sprite *s);
void ghost_start_running_away(Sprite *s);
void ghost_start_going_home(Sprite *s);
void ghost_send_back_to_start(Sprite *s);

void extra_start(Sprite *s, int nr, Sprite *arrow);

int  char_glyph(int c, int *accent);            /* startWithCharacter: the frame and the accent */
void char_move_to_start(Sprite *s);

void digit_start(Sprite *s, int digitNr, int playerNr, const int *value);
void heart_start(Sprite *s, int number, const int *value);
void points_start(Sprite *s, int digit, int color, int position);
void button_press(Sprite *s);

#endif
