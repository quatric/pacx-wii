#ifndef GAME_H
#define GAME_H
/* PMGame: the game controller of the original */
#include "engine.h"
#include "levels.h"

/* gameMode */
enum {
    MODE_NONE = 0, MODE_SPLASH = 1, MODE_HIGHSCORES = 2, MODE_GAME = 3, MODE_DEMO = 4,
    MODE_HELP = 5, MODE_CREDITS = 6
};

/* the events of eventOccured: */
enum {
    EV_MOUSE = 1, EV_KEY = 9, EV_DEACTIVATE = 19,
    EV_NEW_GAME = 1000, EV_NEW_GAME1 = 1001, EV_NEW_GAME2 = 1002, EV_PAUSE = 1003, EV_RESUME = 1004,
    EV_PREFS = 1005, EV_HELP = 1006, EV_HIGHSCORES = 1007, EV_CLEAR_SCORES = 1008,
    EV_NEW_SCORE1 = 1009, EV_NEW_SCORE2 = 1010, EV_TRY_LEVEL = 1011
};

typedef struct {
    int mode;
    int difficulty;
    int levelNr;
    int score[2], lives[2];
    int citadelEntryX, citadelEntryY, citadelNr;
    int moveDelay, timeForNextGhost, ghostsEaten, countExtrasAppeared;
    int gameOver, levelCompleted, tryLevel, playerCanJoin, gameFinished;
    int splashTimer, automaticTimerStart, automaticTimerRun;
    float tileColor[4];
    int tiles[4][LV_W][LV_H];       /* MGLevel layers: 0 tiles, 1 super pellets, 2 starts, 3 home */
    int extraX[LV_W * LV_H], extraY[LV_W * LV_H], nextras;
    LevelSet custom;                /* customLevelSet, n == 0 when the standard levels are used */
    LevelSet standard;
    int lastEntry[2];               /* lastHighscoreEntry: index in the table, -1 if none */
    char lastTable[80];
    int keyEvent;                   /* the key of the last EV_KEY event */
    int nameInputPending;           /* nameInput0/1/2 of the AppController: 3, 1 or 2 */
    int alertPending;               /* NSRunAlertPanel NoTwoPlayer */
} Game;

extern Game game;

/* frame sets the actors use */
extern FrameSet *fs_player[16], *fs_ghosts[16], *fs_points[5];
extern FrameSet *fs_die_part1, *fs_ghost_run, *fs_ghost, *fs_extra;

void game_init(void);                   /* applicationDidFinishLaunching */
void game_handle_event(const Event *e); /* eventOccured: */
void game_show_splash(void);

/* the engine's callbacks */
void game_timer_changed(void);          /* animationTimerDidChange */
void game_frame_finished(void);         /* animationFrameFinished */

/* for the actors */
int  game_direction_allowed(int dir, int x, int y);
int  game_is_citadel_entry(int x, int y);
int  game_home_direction(int x, int y);
int  game_moving_allowed(void);
int  game_player_nr(void);
Sprite *game_player(int nr);
int  game_key_down(int player, int dir);
int  game_next_ghost_time(void);
void game_add_score(int pts, Sprite *player);
void game_create_points(int pts, Sprite *player);
void game_super_pellet_eaten(void);
void game_ghost_eaten(Sprite *player);
void game_player_starts_dying(int nr);
void game_player_dying_completed(Sprite *player);
void game_sprite_pops(Sprite *s, int isPlayer);
void game_over(void);

/* for the Wii screens */
void game_update_settings(void);        /* updateSettings */
void game_set_paused(int paused);
int  game_is_paused(void);
int  game_count_levels(void);
void game_table_name(char *out, int max);
void game_enter_new_score(int player);

/* the editor: plays one of its levels, then game_take_editor_return() says to go back to it */
void game_try_editor_level(const LevelSet *set, int level, int players);
int  game_take_editor_return(void);

#endif
