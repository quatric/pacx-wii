#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "engine.h"
#include "game.h"
#include "actors.h"

Layer layers[NUM_LAYERS];
Animation anim;

/* ------------------------------------------------------------------ random */

static u32 rstate = 0x2545F491;

int rnd(int a, int b) {
    /* xorshift; the original used random() */
    rstate ^= rstate << 13;
    rstate ^= rstate >> 17;
    rstate ^= rstate << 5;
    u32 r = rstate & 0x7fffffff;
    return a + (int)(r % (u32)(b - a + 1));
}

void rnd_seed(u32 s) { rstate = s ? s : 1; }

/* ------------------------------------------------------------------ frame sets */

#define MAX_FS 160
static FrameSet framesets[MAX_FS];
static int nfs;

FrameSet *fs_find(const char *name) {
    for (int i = 0; i < nfs; i++)
        if (framesets[i].count >= 0 && strcmp(framesets[i].name, name) == 0) return &framesets[i];
    return NULL;
}

static FrameSet *fs_new(const char *name, int count) {
    FrameSet *fs = NULL;
    for (int i = 0; i < nfs; i++)
        if (framesets[i].count < 0) { fs = &framesets[i]; break; }
    if (!fs) {
        if (nfs >= MAX_FS) return NULL;
        fs = &framesets[nfs++];
    }
    memset(fs, 0, sizeof *fs);
    strncpy(fs->name, name, sizeof fs->name - 1);
    fs->count = count;
    fs->frames = calloc(count > 0 ? count : 1, sizeof(Frame));
    return fs;
}

void fs_remove(FrameSet *fs) {
    if (!fs) return;
    free(fs->frames);
    fs->frames = NULL;
    fs->count = -1;
    fs->name[0] = 0;
}

FrameSet *fs_from_tex(const char *name, const Tex *t) {
    FrameSet *fs = fs_new(name, 1);
    if (!fs) return NULL;
    fs->frames[0] = (Frame){t, 0, 0, t->w, t->h};
    return fs;
}

FrameSet *fs_whole(const char *name) {
    const Tex *t = gfx_tex(name);
    if (!t) return NULL;
    return fs_from_tex(name, t);
}

FrameSet *fs_split(const char *name, int pw, int ph) {
    const Tex *t = gfx_tex(name);
    if (!t) return NULL;
    int cols = t->w / pw, rows = t->h / ph;
    FrameSet *fs = fs_new(name, cols * rows);
    if (!fs) return NULL;
    int k = 0;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            fs->frames[k++] = (Frame){t, c * pw, r * ph, pw, ph};
    return fs;
}

FrameSet *fs_dup(const char *name, FrameSet *src, int from, int to) {
    if (!src) return NULL;
    if (to >= src->count) to = src->count - 1;
    int n = to - from + 1;
    if (n < 0) n = 0;
    FrameSet *fs = fs_new(name, n);
    if (!fs) return NULL;
    for (int i = 0; i < n; i++) fs->frames[i] = src->frames[from + i];
    return fs;
}

void fs_cycle(FrameSet *fs) {
    int n = fs->count;
    if (n < 3) return;
    Frame *f = realloc(fs->frames, sizeof(Frame) * (n + n - 2));
    if (!f) return;
    fs->frames = f;
    int k = n;
    for (int i = n - 2; i >= 1; i--) f[k++] = f[i];
    fs->count = k;
}

/* ------------------------------------------------------------------ sprites */

#define MAX_SPRITES 3072
static Sprite pool[MAX_SPRITES];
static int poolHint;
static Sprite *graveyard[MAX_SPRITES];
static int ngrave;

static void list_add(SpriteList *l, Sprite *s) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->v = realloc(l->v, sizeof(Sprite *) * l->cap);
    }
    l->v[l->n++] = s;
}

static void bury(Sprite *s) {
    /* freed at the end of the step, so code still holding the pointer this frame is safe */
    if (s->used == 1) {
        s->used = 2;
        graveyard[ngrave++] = s;
    }
}

static void free_graveyard(void) {
    for (int i = 0; i < ngrave; i++) {
        graveyard[i]->used = 0;
        graveyard[i]->gen++;
    }
    ngrave = 0;
}

SpriteRef spr_ref(Sprite *s) { SpriteRef r = {s, s ? s->gen : 0}; return r; }

Sprite *spr_get(SpriteRef r) {
    if (!r.s || r.s->gen != r.gen || r.s->used != 1) return NULL;
    return r.s;
}

static Sprite *spr_alloc(void) {
    for (int k = 0; k < MAX_SPRITES; k++) {
        int i = (poolHint + k) % MAX_SPRITES;
        if (!pool[i].used) {
            poolHint = i + 1;
            u32 gen = pool[i].gen;
            memset(&pool[i], 0, sizeof(Sprite));
            pool[i].gen = gen + 1;
            pool[i].used = 1;
            return &pool[i];
        }
    }
    return NULL;
}

Sprite *spr_create(int layer, int kind) {
    Sprite *s = spr_alloc();
    if (!s) {
        static Sprite dummy;
        memset(&dummy, 0, sizeof dummy);
        dummy.used = 3;
        dummy.layer = -1;
        return &dummy;
    }
    s->kind = kind;
    s->layer = layer;
    s->visible = 1;
    s->speedFactor = 1;
    s->scaleX = s->scaleY = 1;
    s->color[0] = s->color[1] = s->color[2] = s->color[3] = 1;
    s->advanceDirection = 1;
    list_add(&layers[layer].added, s);
    return s;
}

Sprite *spr_create_at(int layer, int kind, int x, int y) {
    Sprite *s = spr_create(layer, kind);
    /* createSprite:xPos:yPos: moves the sprite only for a non-zero position, then starts it */
    if (x | y) {
        s->x = (float)x;
        s->y = (float)y;
    }
    actor_start(s);
    return s;
}

void spr_delete(Sprite *s) {
    s->mustBeDeleted = 1;
    actor_deleted(s);
}

/* ------------------------------------------------------------------ layers */

void eng_layer_collides(int layer, int with) {
    Layer *l = &layers[layer];
    l->colliding[l->ncolliding++] = with;
}

static void check_added(int li) {
    Layer *l = &layers[li];
    if (!l->added.n) return;
    for (int i = 0; i < l->added.n; i++) {
        Sprite *s = l->added.v[i];
        s->inLayer = 1;
        list_add(&l->sprites, s);
    }
    l->added.n = 0;
}

static void check_removed(int li) {
    Layer *l = &layers[li];
    int k = 0;
    for (int i = 0; i < l->sprites.n; i++) {
        Sprite *s = l->sprites.v[i];
        if (s->mustBeDeleted) { s->inLayer = 0; bury(s); }
        else l->sprites.v[k++] = s;
    }
    l->sprites.n = k;
}

void layer_remove_all(int li) {
    Layer *l = &layers[li];
    for (int i = 0; i < l->sprites.n; i++) {
        Sprite *s = l->sprites.v[i];
        if (!s->mustBeDeleted) spr_delete(s);
        s->inLayer = 0;
        bury(s);
    }
    for (int i = 0; i < l->added.n; i++) {
        Sprite *s = l->added.v[i];
        if (!s->mustBeDeleted) spr_delete(s);
        bury(s);
    }
    l->sprites.n = 0;
    l->added.n = 0;
}

void eng_remove_all(void) {
    for (int i = 0; i < NUM_LAYERS; i++) layer_remove_all(i);
}

int layer_count(int li) {
    check_added(li);
    return layers[li].sprites.n;
}

Sprite *layer_at(int li, int i) {
    check_added(li);
    if (i < 0 || i >= layers[li].sprites.n) return NULL;
    return layers[li].sprites.v[i];
}

static int contains(const float r[4], float x, float y) {
    return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
}

Sprite *layer_sprite_at(int li, int x, int y, int tol) {
    Layer *l = &layers[li];
    check_added(li);
    /* spriteAtPositionX:y: first (exact bounds), then the bounds grown by the tolerance */
    for (int pass = 0; pass < 2; pass++)
        for (int i = l->sprites.n - 1; i >= 0; i--) {
            Sprite *s = l->sprites.v[i];
            float r[4] = {s->x + s->insetL, s->y + s->insetT,
                          spr_width(s) - s->insetL - s->insetR, spr_height(s) - s->insetT - s->insetB};
            if (pass) { r[0] -= tol; r[1] -= tol; r[2] += 2 * tol; r[3] += 2 * tol; }
            if (contains(r, x, y)) return s;
        }
    return NULL;
}

/* ------------------------------------------------------------------ MGSprite */

static void set_frame(Sprite *s) {
    if (s->frameSet && s->frameIndex >= 0 && s->frameIndex < s->frameSet->count)
        s->frame = &s->frameSet->frames[s->frameIndex];
    else
        s->frame = NULL;
}

void spr_set_frameset(Sprite *s, FrameSet *fs) {
    if (s->frameSet != fs) s->lastFrame = 0;
    s->frameSet = fs;
    int n = fs ? fs->count : 0;
    if (s->frameIndex >= n || s->frameIndex < 0) s->frameIndex = 0;
    set_frame(s);
    s->rangeStart = 0;
    s->rangeLen = n;
}

void spr_set_frameset_name(Sprite *s, const char *name) { spr_set_frameset(s, fs_find(name)); }

void spr_set_frameset_index(Sprite *s, FrameSet *fs, int index) {
    spr_set_frameset(s, fs);
    spr_set_frame_index(s, index);
}

void spr_set_frame_index(Sprite *s, int i) {
    s->frameIndex = i;
    set_frame(s);
}

void spr_set_frame_range(Sprite *s, int start, int len) {
    int n = s->frameSet ? s->frameSet->count : 0;
    /* intersect (start, len) with (0, count) */
    int a = start < 0 ? 0 : start, b = start + len > n ? n : start + len;
    if (b < a) b = a;
    s->rangeStart = a;
    s->rangeLen = b - a;
    spr_set_frame_index(s, start);
}

void spr_set_empty(Sprite *s, float w, float h, float r, float g, float b, float a) {
    s->frameSet = NULL;
    s->frame = NULL;
    s->width = w;
    s->height = h;
    spr_set_color(s, r, g, b, a);
}

float spr_width(const Sprite *s) { return s->frame ? s->frame->w * s->scaleX : s->width; }
float spr_height(const Sprite *s) { return s->frame ? s->frame->h * s->scaleY : s->height; }

void spr_set_frame_speed(Sprite *s, float f) { s->frameSpeed = (f > 0.0001f || f < -0.0001f) ? FPS / f : 0; }
void spr_set_rotation_speed(Sprite *s, float v) { s->rotationSpeed = v * SPEED_FACTOR; }

void spr_set_delay(Sprite *s, float secs) {
    s->delay = (int)(FPS * secs);
    s->visible = 0;
}

void spr_set_duration(Sprite *s, float secs) { s->duration = (int)(FPS * secs); }

void spr_set_flash(Sprite *s, float secs) {
    s->flashFrequency = s->flashCounter = (int)(FPS * secs);
    s->flashedOn = 0;
}

void spr_fadein(Sprite *s, float secs) {
    s->fadeSpeed = (secs > 0.0001f) ? 1.0f / (secs * 30.0f) : 30.0f;
    s->color[3] = 0;
    s->fading = 1;
}

void spr_fadeout(Sprite *s, float secs) {
    s->fadeSpeed = (secs > 0.0001f) ? -1.0f / (secs * 30.0f) : -30.0f;
    s->color[3] = 1;
    s->fading = 1;
}

void spr_set_color(Sprite *s, float r, float g, float b, float a) {
    s->color[0] = r; s->color[1] = g; s->color[2] = b; s->color[3] = a;
}

void spr_set_hspeed(Sprite *s, float v) { s->hSpeed = v * SPEED_FACTOR; }
void spr_set_vspeed(Sprite *s, float v) { s->vSpeed = v * SPEED_FACTOR; }
void spr_set_speed(Sprite *s, float h, float v) { s->hSpeed = h * SPEED_FACTOR; s->vSpeed = v * SPEED_FACTOR; }
void spr_inc_vspeed(Sprite *s, float v) { s->vSpeed = s->vSpeed + v * SPEED_FACTOR; }
void spr_stop(Sprite *s) { s->hSpeed = 0; s->vSpeed = 0; }

void spr_move_by(Sprite *s, float h, float v) {
    float f = SPEED_FACTOR * s->speedFactor;
    s->x = h * f + s->x;
    s->y = f * v + s->y;
}

void spr_move(Sprite *s) { spr_move_by(s, s->hSpeed, s->vSpeed); }

void spr_move_to(Sprite *s, float x, float y) { s->x = x; s->y = y; }

void spr_center_on_view(Sprite *s) {
    s->x = VIEW_W * 0.5f - spr_width(s) * 0.5f;
    s->y = VIEW_H * 0.5f - spr_height(s) * 0.5f;
}

void spr_turn(Sprite *s, float a) {
    float v = a + s->angle;
    s->angle = v;
    if (v < 0) v += 360.0f;
    else if (v > 360.0f) v += -360.0f;
    s->angle = v;
}

void spr_bounds(const Sprite *s, float r[4]) {
    r[0] = s->x + s->insetL;
    r[1] = s->y + s->insetT;
    r[2] = spr_width(s) - s->insetL - s->insetR;
    r[3] = spr_height(s) - s->insetT - s->insetB;
}

static void advance_frame(Sprite *s) {
    int start = s->rangeStart, len = s->rangeLen;
    if (s->frameSet) {
        if (s->advanceDirection == 1 && s->frameIndex < start + len - 1) {
            s->frameIndex++;
            set_frame(s);
            return;
        }
        if (s->advanceDirection == -1 && s->frameIndex > start) {
            s->frameIndex--;
            set_frame(s);
            if (s->frameSetPatrolling && s->frameIndex == start) {
                actor_animation_finished(s);
                s->advanceDirection = -s->advanceDirection;
            }
            return;
        }
    }
    if (s->deleteAfterAnimation) {
        s->visible = 0;
        s->mustBeDeleted = 1;
        actor_animation_finished(s);
        return;
    }
    if (!s->frameSet) return;
    int d = s->advanceDirection;
    if (s->frameSetPatrolling) {
        s->advanceDirection = -d;
        s->frameIndex -= d;
        set_frame(s);
        return;
    }
    s->frameIndex = (d == 1) ? start : start + len;
    set_frame(s);
    actor_animation_finished(s);
}

static void inc_alpha(Sprite *s, float v) {
    float a = SPEED_FACTOR * v + s->color[3];
    if (a > 1) a = 1;
    if (a < 0) a = 0;
    s->color[3] = a;
}

static void sprite_step(Sprite *s, int timer) {
    if (s->delay > 0) {
        if (--s->delay == 0) s->visible = 1;
        return;
    }
    if (s->frameSpeed > 0.0001f) {
        s->lastFrame += 1.0f;
        if (s->lastFrame >= s->frameSpeed) {
            advance_frame(s);
            s->lastFrame -= s->frameSpeed;
        }
    }
    if (!(s->rotationSpeed < 0.0001f && s->rotationSpeed > -0.0001f)) spr_turn(s, s->rotationSpeed);
    if (s->flashFrequency > 0) {
        if (--s->flashCounter == 0) {
            s->visible = s->flashedOn;
            s->flashedOn = !s->flashedOn;
            s->flashCounter = s->flashFrequency;
        }
    }
    if (s->fading && !(s->fadeSpeed < 0.0001f && s->fadeSpeed > -0.0001f)) {
        inc_alpha(s, s->fadeSpeed);
        if (s->color[3] >= 1.0f) {
            s->fadeSpeed = 0; s->color[3] = 1; s->fading = 0;
        } else if (s->color[3] <= 0) {
            s->fadeSpeed = 0; s->color[3] = 0; s->fading = 0;
        }
    }
    if (s->duration > 0) {
        if (--s->duration == 0) {
            spr_delete(s);
            return;
        }
    }
    actor_move(s);
    if (timer) actor_timer(s);
}

/* ------------------------------------------------------------------ animation */

#define MAX_EVENTS 64
static Event events[MAX_EVENTS];
static int evHead, evTail;

void eng_post(int type, float x, float y) {
    int n = (evTail + 1) % MAX_EVENTS;
    if (n == evHead) return;
    events[evTail] = (Event){type, x, y};
    evTail = n;
}

int eng_poll(Event *e) {
    if (evHead == evTail) return 0;
    *e = events[evHead];
    evHead = (evHead + 1) % MAX_EVENTS;
    return 1;
}

void eng_init(void) {
    memset(layers, 0, sizeof layers);
    for (int i = 0; i < NUM_LAYERS; i++) layers[i].visible = 1;
    memset(&anim, 0, sizeof anim);
    anim.running = 1;
    anim.frameCount = FPS;
}

void eng_start_fadein(float secs) {
    anim.fadeColor[0] = anim.fadeColor[1] = anim.fadeColor[2] = 0;
    anim.fadeColor[3] = 1;
    anim.fadeSpeed = -1.0f / (secs * FPS);
    anim.fading = 1;
}

void eng_start_fadeout(float secs) {
    anim.fadeColor[0] = anim.fadeColor[1] = anim.fadeColor[2] = 0;
    anim.fadeColor[3] = 0;
    anim.fadeSpeed = 1.0f / (secs * FPS);
    anim.fading = 1;
}

static void collide_layers(int a, int b) {
    Layer *la = &layers[a], *lb = &layers[b];
    if (lb->paused) return;
    int na = la->sprites.n, nb = lb->sprites.n;
    if (!na || !nb) return;
    for (int i = 0; i < na && i < la->sprites.n; i++) {
        Sprite *s = la->sprites.v[i];
        if (s->mustBeDeleted) continue;
        float rs[4];
        spr_bounds(s, rs);
        for (int j = (a == b) ? i + 1 : 0; j < nb && j < lb->sprites.n; j++) {
            Sprite *t = lb->sprites.v[j];
            if (t->mustBeDeleted) continue;
            float rt[4];
            spr_bounds(t, rt);
            if (rs[2] <= 0 || rs[3] <= 0 || rt[2] <= 0 || rt[3] <= 0) continue;
            if (rs[0] < rt[0] + rt[2] && rt[0] < rs[0] + rs[2] &&
                rs[1] < rt[1] + rt[3] && rt[1] < rs[1] + rs[3])
                actor_collide(s, t);
        }
    }
}

void eng_step(void) {
    if (anim.fading) {
        anim.fadeColor[3] += anim.fadeSpeed;
        if (anim.fadeColor[3] <= 0) { anim.fadeColor[3] = 0; anim.fading = 0; }
        else if (anim.fadeColor[3] >= 1) { anim.fadeColor[3] = 1; anim.fading = 0; }
    }
    if (anim.frameCount) {
        anim.frameCount--;
    } else {
        anim.frameCount = FPS;
        game_timer_changed();
    }
    int timer = anim.frameCount == FPS;
    for (int li = 0; li < NUM_LAYERS; li++) {
        Layer *l = &layers[li];
        if (l->paused) continue;
        for (int i = 0; i < l->sprites.n; i++) {
            Sprite *s = l->sprites.v[i];
            if (s->used != 1) continue;
            sprite_step(s, timer);
        }
    }
    for (int li = 0; li < NUM_LAYERS; li++) {
        Layer *l = &layers[li];
        if (l->paused) continue;
        for (int c = 0; c < l->ncolliding; c++) collide_layers(li, l->colliding[c]);
    }
    for (int li = 0; li < NUM_LAYERS; li++) check_removed(li);
    for (int li = 0; li < NUM_LAYERS; li++) check_added(li);
    game_frame_finished();
    free_graveyard();
}

static void draw_sprite(const Sprite *s) {
    if (!s->visible) return;
    if (s->frame) {
        const Frame *f = s->frame;
        gfx_quad(f->tex, f->sx, f->sy, f->w, f->h, s->x, s->y, f->w * s->scaleX, f->h * s->scaleY,
                 s->angle, s->flipped, s->color[0], s->color[1], s->color[2], s->color[3]);
    } else if (!s->frameSet && s->width > 0) {
        gfx_rect(s->x, s->y, s->width, s->height, s->color[0], s->color[1], s->color[2], s->color[3]);
    }
}

void eng_draw(void) {
    for (int li = 0; li < NUM_LAYERS; li++) {
        check_added(li);
        Layer *l = &layers[li];
        if (!l->visible) continue;
        for (int i = 0; i < l->sprites.n; i++) draw_sprite(l->sprites.v[i]);
    }
    if (anim.fadeColor[3] > 0)
        gfx_rect(0, 0, VIEW_W, VIEW_H, anim.fadeColor[0], anim.fadeColor[1], anim.fadeColor[2], anim.fadeColor[3]);
}
