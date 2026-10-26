#ifndef GFX_H
#define GFX_H
#include "platform.h"
#ifndef PACX_NOGX
#include <gccore.h>
#endif

/* The game draws into a 640x480 view, y down, like the original glOrtho(0, 640, 480, 0). */
#define VIEW_W 640
#define VIEW_H 480

typedef struct Tex {
#ifdef PACX_NOGX
    void *obj;      /* the Metal texture (retained), or the port's texture */
#else
    GXTexObj obj;
#endif
    int w, h;       /* picture size */
    int pw, ph;     /* texture size (padded to 4) */
    int fmt;
    void *mem;      /* malloc'ed pixels for textures made at run time, else NULL */
} Tex;

enum { ASPECT_AUTO = 0, ASPECT_4_3, ASPECT_STRETCH };

void gfx_init(void);
void gfx_set_aspect(int mode);       /* ASPECT_*: 16:9 TVs show the 4:3 game pillarboxed unless stretched */
int  gfx_is_widescreen(void);        /* the console is set to 16:9 */
int  gfx_is_pal50(void);

Tex *gfx_tex(const char *name);      /* texture from the pack, NULL if missing */
Tex *gfx_tex_rgba(const u8 *rgba, int w, int h);   /* a texture from linear RGBA pixels */
void gfx_tex_free(Tex *t);

void gfx_restore_state(void);      /* our GX state again, after another library drew */
#ifndef PACX_NOGX
GXRModeObj *gfx_rmode(void);
#endif

void gfx_begin(void);
void gfx_end(void);

/* draws part (sx, sy, sw, sh in texture pixels) of t at x, y with size w, h. angle is in
   degrees around the centre, flip mirrors horizontally. */
void gfx_quad(const Tex *t, float sx, float sy, float sw, float sh,
              float x, float y, float w, float h, float angle, int flip,
              float r, float g, float b, float a);
void gfx_rect(float x, float y, float w, float h, float r, float g, float b, float a);
void gfx_frame(float x, float y, float w, float h, float t, float r, float g, float b, float a);

/* the IR pointer / mouse arrow */
void gfx_cursor(float x, float y);

/* converts a point on the screen (0..640 across the whole TV picture) to view coordinates */
float gfx_screen_to_view_x(float sx);

/* RGB copy of the last finished frame (640x480, for the test build) */
int gfx_grab(u8 *rgb, int w, int h);

#endif
