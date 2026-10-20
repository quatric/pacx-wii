#ifndef ENGINE_H
#define ENGINE_H
/*
 * The sprite engine of the original (MGAnimation, MGLayer, MGSprite, MGFrameSet), rebuilt
 * with the same per-frame order and the same arithmetic so the game runs identically.
 */
#include "gfx.h"

#define FPS 60
#define SPEED_FACTOR 0.5f      /* MGAnimation.speedFactor = 30 / fps */

typedef struct Frame { const Tex *tex; short sx, sy, w, h; } Frame;

typedef struct FrameSet {
    char name[32];
    int count;
    Frame *frames;
} FrameSet;

FrameSet *fs_find(const char *name);
FrameSet *fs_whole(const char *name);                         /* addFrameSet: the whole picture */
FrameSet *fs_split(const char *name, int pw, int ph);         /* addFrameSetSplitted:partWidth:partHeight: */
FrameSet *fs_dup(const char *name, FrameSet *src, int from, int to);  /* addFrameSetDuplicate (to inclusive) */
FrameSet *fs_from_tex(const char *name, const Tex *t);
void fs_cycle(FrameSet *fs);                                  /* adds frames n-2 .. 1 */
void fs_remove(FrameSet *fs);

/* sprite kinds (the PM* classes) */
enum {
    K_SPRITE = 0, K_PLAYER, K_GHOST, K_EXTRA, K_CHAR, K_DIGIT, K_HEART, K_POINTS, K_BUTTON
};

typedef struct Sprite Sprite;

typedef struct { Sprite *s; u32 gen; } SpriteRef;

typedef struct {
    /* PMSprite */
    u8 canLeft, canRight, canUp, canDown;
    u8 wantLeft, wantRight, wantUp, wantDown;
    int directions, startX, startY, changedDirection;
} PMSpriteData;

struct Sprite {
    u32 gen;
    u8 used;
    u8 kind;
    signed char layer;              /* index into the engine layers, -1 when not in one */
    u8 inLayer;

    float x, y;
    float hSpeed, vSpeed, speedFactor;
    float angle, rotationSpeed;
    float scaleX, scaleY;
    float color[4];
    u8 visible, flipped;
    u8 mustBeDeleted, deleteAfterAnimation;
    u8 frameSetPatrolling, fading, flashedOn;

    FrameSet *frameSet;
    const Frame *frame;
    int frameIndex, rangeStart, rangeLen, advanceDirection;
    float frameSpeed, lastFrame;
    float width, height;            /* for empty frame sets */
    float insetL, insetT, insetR, insetB;

    int delay, duration, flashFrequency, flashCounter;
    float fadeSpeed;

    PMSpriteData pm;

    union {
        struct {   /* PMPlayer */
            int playerNr, autoPlayer, dying, invulnerable, atePellet, soundCount;
            int doubleSpeed, lookDirection;
            SpriteRef doubleScore;
            SpriteRef spot, rects[4];
            int hasSpot;
        } pl;
        struct {   /* PMGhost */
            int ghostNr, state, reachedTarget;
            float runaway, blink;
        } gh;
        struct {   /* PMExtra */
            int extraNr, timer;
            SpriteRef arrow;
        } ex;
        struct {   /* PMChar */
            int targetX, targetY;
        } ch;
        struct {   /* PMDigit */
            int digitNr, playerNr;
            const int *value;
        } dg;
        struct {   /* PMHeart */
            int number;
            const int *value;
        } ht;
        struct {   /* PMPoints */
            int targetY, reachedTarget, moveTimer;
        } pt;
        struct {   /* PMButton */
            int hiliteTimer;
        } bt;
    } u;
};

typedef struct { Sprite **v; int n, cap; } SpriteList;

typedef struct Layer {
    SpriteList sprites, added;
    int colliding[8], ncolliding;
    u8 visible, paused;
} Layer;

/* layer 0 is MGAnimation.backLayer, layers 1..16 are the game's L0..L15 */
#define LAYER_BACK 0
#define L(n) ((n) + 1)
#define NUM_LAYERS 17

extern Layer layers[NUM_LAYERS];

/* animation-wide state */
typedef struct {
    int running;            /* isAnimationRunning */
    int frameCount;
    float fadeColor[4];
    float fadeSpeed;
    int fading;
} Animation;
extern Animation anim;

void eng_init(void);
void eng_layer_collides(int layer, int with);

Sprite *spr_create(int layer, int kind);   /* goes into the layer's pending list */
Sprite *spr_create_at(int layer, int kind, int x, int y);
void spr_delete(Sprite *s);                 /* deleteFromAnimation */
SpriteRef spr_ref(Sprite *s);
Sprite *spr_get(SpriteRef r);               /* NULL once the sprite is gone */

void layer_remove_all(int layer);           /* removeAllSprites */
int  layer_count(int layer);                /* countSprites (takes pending sprites in first) */
Sprite *layer_at(int layer, int i);
Sprite *layer_sprite_at(int layer, int x, int y, int tolerance);
void eng_remove_all(void);                  /* MGAnimation.removeAllSprites */

/* MGSprite */
void spr_set_frameset(Sprite *s, FrameSet *fs);
void spr_set_frameset_name(Sprite *s, const char *name);
void spr_set_frameset_index(Sprite *s, FrameSet *fs, int index);
void spr_set_frame_index(Sprite *s, int i);
void spr_set_frame_range(Sprite *s, int start, int len);
void spr_set_empty(Sprite *s, float w, float h, float r, float g, float b, float a);
void spr_set_frame_speed(Sprite *s, float f);
void spr_set_rotation_speed(Sprite *s, float v);
void spr_set_delay(Sprite *s, float secs);
void spr_set_duration(Sprite *s, float secs);
void spr_set_flash(Sprite *s, float secs);
void spr_fadein(Sprite *s, float secs);
void spr_fadeout(Sprite *s, float secs);
void spr_set_color(Sprite *s, float r, float g, float b, float a);
void spr_set_hspeed(Sprite *s, float v);
void spr_set_vspeed(Sprite *s, float v);
void spr_set_speed(Sprite *s, float h, float v);
void spr_inc_vspeed(Sprite *s, float v);
void spr_stop(Sprite *s);
void spr_move_by(Sprite *s, float h, float v);   /* movePositionX:y: */
void spr_move(Sprite *s);                        /* movePosition */
void spr_move_to(Sprite *s, float x, float y);
void spr_center_on_view(Sprite *s);
void spr_turn(Sprite *s, float a);
float spr_width(const Sprite *s);
float spr_height(const Sprite *s);
static inline int spr_px(const Sprite *s) { return (int)s->x; }
static inline int spr_py(const Sprite *s) { return (int)s->y; }
static inline int spr_moving_left(const Sprite *s) { return s->hSpeed < -0.01f; }
static inline int spr_moving_right(const Sprite *s) { return s->hSpeed > 0.01f; }
static inline int spr_moving_up(const Sprite *s) { return s->vSpeed < -0.01f; }
static inline int spr_moving_down(const Sprite *s) { return s->vSpeed > 0.01f; }
void spr_bounds(const Sprite *s, float r[4]);    /* screenBounds: x, y, w, h */

/* one animation step (MGAnimation.timerFired without the drawing) */
void eng_step(void);
void eng_draw(void);
void eng_start_fadein(float secs);
void eng_start_fadeout(float secs);

/* events (MGAnimation.createEvent and the input events) */
typedef struct { int type; float x, y; } Event;
void eng_post(int type, float x, float y);
int  eng_poll(Event *e);

/* random(a, b) of the original: a + random() % (b - a + 1) */
int rnd(int a, int b);
void rnd_seed(u32 s);

/* actors.c: the PM* behaviour, called by the engine */
void actor_move(Sprite *s);
void actor_collide(Sprite *s, Sprite *t);
void actor_animation_finished(Sprite *s);
void actor_timer(Sprite *s);
void actor_deleted(Sprite *s);      /* deleteFromAnimation overrides */

#endif
