#include <malloc.h>
#include <string.h>
#include <math.h>
#include <ogc/conf.h>
#include "gfx.h"
#include "pack.h"

#define FIFO_SIZE (256 * 1024)
#define MAX_TEX 160

static void *xfb[2];
static int fbi;
static GXRModeObj *rmode;
static void *fifo;
static int wide, aspect = ASPECT_AUTO, pal50;
static float vx0, vx1;          /* the view's left and right edge in EFB pixels */

static Tex textures[MAX_TEX];
static char texnames[MAX_TEX][40];
static int ntex;
static Tex white;
static u8 white_px[64] ATTRIBUTE_ALIGN(32);
static const GXTexObj *bound;

int gfx_is_widescreen(void) { return wide; }
int gfx_is_pal50(void) { return pal50; }

static void setup_projection(void) {
    /* the 4:3 view sits in the middle of a 16:9 picture: 640 view pixels cover 3/4 of the EFB */
    float left = 0, right = VIEW_W;
    vx0 = 0;
    vx1 = rmode->fbWidth;
    if (wide && aspect != ASPECT_STRETCH) {
        float extra = VIEW_W / 3.0f / 2.0f * 1.0f;   /* 106.67 view pixels on each side */
        left = -extra;
        right = VIEW_W + extra;
        vx0 = rmode->fbWidth * (extra / (VIEW_W + 2 * extra));
        vx1 = rmode->fbWidth - vx0;
    }
    Mtx44 proj;
    guOrtho(proj, 0, VIEW_H, left, right, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    int l = (int)floorf(vx0 + 0.5f), r = (int)floorf(vx1 + 0.5f);
    GX_SetScissor(l, 0, r - l, rmode->efbHeight);
}

void gfx_set_aspect(int mode) {
    aspect = mode;
    if (mode == ASPECT_4_3) wide = 0;
    else wide = (CONF_GetAspectRatio() == CONF_ASPECT_16_9);
    setup_projection();
}

float gfx_screen_to_view_x(float sx) {
    float efb = sx * rmode->fbWidth / 640.0f;
    return (efb - vx0) / (vx1 - vx0) * VIEW_W;
}

void gfx_restore_state(void) {
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);

    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetNumTevStages(1);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);

    Mtx mv;
    guMtxIdentity(mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    bound = NULL;
    if (white.w) setup_projection();
}

GXRModeObj *gfx_rmode(void) { return rmode; }

void gfx_init(void) {
    VIDEO_Init();
    CONF_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    pal50 = (rmode == &TVPal576IntDfScale || rmode == &TVPal576ProgScale);
    if (pal50) {
        /* keep a 640x480 EFB and let the VI scale it, so the game logic sees the same picture */
        rmode = &TVPal528IntDf;
    }
    wide = (CONF_GetAspectRatio() == CONF_ASPECT_16_9);
    if (wide) {
        rmode->viWidth = 678;
        rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - 678) / 2;
    }

    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    fbi = 1;

    fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);
    GXColor bg = {0, 0, 0, 0xff};
    GX_SetCopyClear(bg, 0x00ffffff);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    f32 ys = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfbh = GX_SetDispCopyYScale(ys);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfbh);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering, (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_CopyDisp(xfb[fbi], GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    gfx_restore_state();

    memset(white_px, 0xff, sizeof white_px);
    DCFlushRange(white_px, sizeof white_px);
    GX_InitTexObj(&white.obj, white_px, 4, 4, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    white.w = white.h = white.pw = white.ph = 4;

    setup_projection();
}

Tex *gfx_tex(const char *name) {
    for (int i = 0; i < ntex; i++)
        if (strcmp(texnames[i], name) == 0) return &textures[i];
    const PackEntry *e = pack_find(name);
    if (!e || e->type != PK_TEX || ntex >= MAX_TEX) return NULL;
    Tex *t = &textures[ntex];
    strncpy(texnames[ntex], name, 39);
    ntex++;
    t->w = e->w; t->h = e->h; t->pw = e->pw; t->ph = e->ph; t->fmt = e->fmt; t->mem = NULL;
    void *px = (void *)pack_data(e);
    DCFlushRange(px, e->size);
    GX_InitTexObj(&t->obj, px, e->pw, e->ph, e->fmt == PK_RGBA8 ? GX_TF_RGBA8 : GX_TF_RGB565,
                  GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&t->obj, GX_LINEAR, GX_LINEAR);
    return t;
}

Tex *gfx_tex_rgba(const u8 *rgba, int w, int h) {
    Tex *t = calloc(1, sizeof(Tex));
    int pw = (w + 3) & ~3, ph = (h + 3) & ~3;
    u8 *dst = memalign(32, pw * ph * 4);
    memset(dst, 0, pw * ph * 4);
    u8 *o = dst;
    for (int ty = 0; ty < ph; ty += 4)
        for (int tx = 0; tx < pw; tx += 4) {
            u8 ar[32], gb[32];
            int k = 0;
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++, k++) {
                    int sx = tx + x, sy = ty + y;
                    const u8 *p = (sx < w && sy < h) ? rgba + (sy * w + sx) * 4 : NULL;
                    ar[k * 2] = p ? p[3] : 0; ar[k * 2 + 1] = p ? p[0] : 0;
                    gb[k * 2] = p ? p[1] : 0; gb[k * 2 + 1] = p ? p[2] : 0;
                }
            memcpy(o, ar, 32); memcpy(o + 32, gb, 32);
            o += 64;
        }
    DCFlushRange(dst, pw * ph * 4);
    t->w = w; t->h = h; t->pw = pw; t->ph = ph; t->fmt = PK_RGBA8; t->mem = dst;
    GX_InitTexObj(&t->obj, dst, pw, ph, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&t->obj, GX_LINEAR, GX_LINEAR);
    return t;
}

void gfx_tex_free(Tex *t) {
    if (!t || !t->mem) return;
    GX_DrawDone();
    if (bound == &t->obj) bound = NULL;
    free(t->mem);
    free(t);
}

void gfx_begin(void) {
    GX_InvalidateTexAll();
    bound = NULL;
    setup_projection();
}

void gfx_end(void) {
    GX_DrawDone();
    GX_CopyDisp(xfb[fbi], GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(xfb[fbi]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fbi ^= 1;
}

static void bind(const GXTexObj *o) {
    if (bound == o) return;
    GX_LoadTexObj((GXTexObj *)o, GX_TEXMAP0);
    bound = o;
}

static inline u8 c8(float v) {
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    return (u8)(v * 255.0f + 0.5f);
}

void gfx_quad(const Tex *t, float sx, float sy, float sw, float sh,
              float x, float y, float w, float h, float angle, int flip,
              float r, float g, float b, float a) {
    if (!t || a <= 0.001f) return;
    bind(&t->obj);
    float u0 = sx / t->pw, v0 = sy / t->ph, u1 = (sx + sw) / t->pw, v1 = (sy + sh) / t->ph;
    if (flip) { float tmp = u0; u0 = u1; u1 = tmp; }
    float px[4] = {x, x + w, x + w, x}, py[4] = {y, y, y + h, y + h};
    if (angle > 0.001f || angle < -0.001f) {
        /* glRotatef(angle) in a y-down view: positive angles turn clockwise on screen */
        float cx = x + w * 0.5f, cy = y + h * 0.5f;
        float rad = angle * (float)M_PI / 180.0f, c = cosf(rad), s = sinf(rad);
        for (int i = 0; i < 4; i++) {
            float dx = px[i] - cx, dy = py[i] - cy;
            px[i] = cx + dx * c - dy * s;
            py[i] = cy + dx * s + dy * c;
        }
    }
    u8 R = c8(r), G = c8(g), B = c8(b), A = c8(a);
    float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (int i = 0; i < 4; i++) {
        GX_Position2f32(px[i], py[i]);
        GX_Color4u8(R, G, B, A);
        GX_TexCoord2f32(us[i], vs[i]);
    }
    GX_End();
}

void gfx_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    gfx_quad(&white, 1, 1, 2, 2, x, y, w, h, 0, 0, r, g, b, a);
}

void gfx_frame(float x, float y, float w, float h, float t, float r, float g, float b, float a) {
    gfx_rect(x, y, w, t, r, g, b, a);
    gfx_rect(x, y + h - t, w, t, r, g, b, a);
    gfx_rect(x, y + t, t, h - 2 * t, r, g, b, a);
    gfx_rect(x + w - t, y + t, t, h - 2 * t, r, g, b, a);
}

void gfx_cursor(float x, float y) {
    /* a plain arrow, outlined, tip at (x, y) */
    static const char *shape[] = {
        "X           ",
        "XX          ",
        "X.X         ",
        "X..X        ",
        "X...X       ",
        "X....X      ",
        "X.....X     ",
        "X......X    ",
        "X.......X   ",
        "X........X  ",
        "X.....XXXXX ",
        "X..X..X     ",
        "X.X X..X    ",
        "XX  X..X    ",
        "X    X..X   ",
        "     X..X   ",
        "      XX    ",
    };
    for (int j = 0; j < 17; j++)
        for (int i = 0; i < 12; i++) {
            char c = shape[j][i];
            if (c == 'X') gfx_rect(x + i, y + j, 1, 1, 0, 0, 0, 1);
            else if (c == '.') gfx_rect(x + i, y + j, 1, 1, 1, 1, 1, 1);
        }
}

int gfx_grab(u8 *rgb, int w, int h) {
    /* reads the finished picture back from the external frame buffer (YUV422) */
    void *src = xfb[fbi ^ 1];
    int fw = rmode->fbWidth, fh = rmode->xfbHeight;
    u32 *p = (u32 *)src;
    for (int y = 0; y < h; y++) {
        int sy = y * fh / h;
        for (int x = 0; x < w; x += 2) {
            u32 v = p[(sy * fw + x * fw / w) / 2];
            int y0 = (v >> 24) & 0xff, u = (v >> 16) & 0xff, y1 = (v >> 8) & 0xff, vv = v & 0xff;
            for (int k = 0; k < 2; k++) {
                int Y = k ? y1 : y0;
                float R = Y + 1.371f * (vv - 128), G = Y - 0.336f * (u - 128) - 0.698f * (vv - 128),
                      B = Y + 1.732f * (u - 128);
                u8 *o = rgb + (y * w + x + k) * 3;
                o[0] = R < 0 ? 0 : R > 255 ? 255 : (u8)R;
                o[1] = G < 0 ? 0 : G > 255 ? 255 : (u8)G;
                o[2] = B < 0 ? 0 : B > 255 ? 255 : (u8)B;
            }
        }
    }
    return 1;
}
