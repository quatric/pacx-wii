/*
 * The drawing of gfx.h with Metal. The quads of a frame are collected between gfx_begin and
 * gfx_end, then drawn into the view in batches of the same texture: the 640x480 picture is
 * scaled to the window, keeping its proportions unless the stretched screen mode is chosen.
 */
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <math.h>
#include <string.h>
#include "gfx.h"
#include "pack.h"
#include "macplat.h"

#define MAX_TEX 160

typedef struct { float x, y, u, v, r, g, b, a; } Vtx;
typedef struct { const Tex *t; int first, count; } Batch;

static id<MTLDevice> dev;
static id<MTLCommandQueue> queue;
static id<MTLRenderPipelineState> pipeline;
static id<MTLSamplerState> sampler;
static MTKView *view;
static int aspect = ASPECT_AUTO;

static Tex textures[MAX_TEX];
static char texnames[MAX_TEX][40];
static int ntex;
static Tex white;

static Vtx *verts;
static int nverts, capverts;
static Batch *batches;
static int nbatches, capbatches;

static const char *shaderSource =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct V { float2 pos; float2 uv; float4 col; };\n"
    "struct O { float4 pos [[position]]; float2 uv; float4 col; };\n"
    "vertex O vs(const device V *v [[buffer(0)]], uint id [[vertex_id]]) {\n"
    "    O o;\n"
    "    o.pos = float4(v[id].pos.x / 320.0 - 1.0, 1.0 - v[id].pos.y / 240.0, 0.0, 1.0);\n"
    "    o.uv = v[id].uv;\n"
    "    o.col = v[id].col;\n"
    "    return o;\n"
    "}\n"
    "fragment float4 fs(O i [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {\n"
    "    return t.sample(s, i.uv) * i.col;\n"
    "}\n";

int gfx_is_widescreen(void) { return 0; }
int gfx_is_pal50(void) { return 0; }
void gfx_set_aspect(int mode) { aspect = mode; }
float gfx_screen_to_view_x(float sx) { return sx; }
void gfx_restore_state(void) {}
void gfx_init(void) {}

static id<MTLTexture> make_texture(const u8 *rgba, int w, int h) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                 width:w height:h mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> t = [dev newTextureWithDescriptor:d];
    [t replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:rgba bytesPerRow:w * 4];
    return t;
}

void gfx_mac_init(MTKView *v) {
    view = v;
    dev = v.device;
    queue = [dev newCommandQueue];
    NSError *err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@(shaderSource) options:nil error:&err];
    if (!lib) NSLog(@"shaders: %@", err);
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vs"];
    pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
    MTLRenderPipelineColorAttachmentDescriptor *ca = pd.colorAttachments[0];
    ca.pixelFormat = v.colorPixelFormat;
    ca.blendingEnabled = YES;
    ca.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
    ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeline = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!pipeline) NSLog(@"pipeline: %@", err);
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler = [dev newSamplerStateWithDescriptor:sd];

    u8 px[16 * 4];
    memset(px, 0xff, sizeof px);
    white.obj = (__bridge_retained void *)make_texture(px, 4, 4);
    white.w = white.h = white.pw = white.ph = 4;
}

Tex *gfx_tex(const char *name) {
    for (int i = 0; i < ntex; i++)
        if (strcmp(texnames[i], name) == 0) return &textures[i];
    const PackEntry *e = pack_find(name);
    if (!e || e->type != PK_TEX || ntex >= MAX_TEX || !dev) return NULL;
    Tex *t = &textures[ntex];
    strncpy(texnames[ntex], name, 39);
    ntex++;
    t->w = e->w; t->h = e->h; t->pw = e->pw; t->ph = e->ph; t->fmt = e->fmt; t->mem = NULL;
    t->obj = (__bridge_retained void *)make_texture(pack_data(e), e->pw, e->ph);
    return t;
}

Tex *gfx_tex_rgba(const u8 *rgba, int w, int h) {
    Tex *t = calloc(1, sizeof(Tex));
    t->w = t->pw = w;
    t->h = t->ph = h;
    t->fmt = PK_RGBA8;
    t->mem = t;
    t->obj = (__bridge_retained void *)make_texture(rgba, w, h);
    return t;
}

void gfx_tex_free(Tex *t) {
    if (!t || !t->mem) return;
    for (int i = 0; i < nbatches; i++)
        if (batches[i].t == t) batches[i].t = &white;
    CFRelease(t->obj);
    free(t);
}

/* ------------------------------------------------------------------ the quads of a frame */

void gfx_begin(void) {
    nverts = 0;
    nbatches = 0;
}

static Vtx *add_quad(const Tex *t) {
    if (nverts + 6 > capverts) {
        capverts = capverts ? capverts * 2 : 4096;
        verts = realloc(verts, sizeof(Vtx) * capverts);
    }
    if (!nbatches || batches[nbatches - 1].t != t) {
        if (nbatches == capbatches) {
            capbatches = capbatches ? capbatches * 2 : 256;
            batches = realloc(batches, sizeof(Batch) * capbatches);
        }
        batches[nbatches++] = (Batch){t, nverts, 0};
    }
    batches[nbatches - 1].count += 6;
    Vtx *v = &verts[nverts];
    nverts += 6;
    return v;
}

void gfx_quad(const Tex *t, float sx, float sy, float sw, float sh,
              float x, float y, float w, float h, float angle, int flip,
              float r, float g, float b, float a) {
    if (!t || a <= 0.001f) return;
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
    float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    Vtx *v = add_quad(t);
    for (int i = 0; i < 6; i++) {
        int k = order[i];
        v[i] = (Vtx){px[k], py[k], us[k], vs[k], r, g, b, a};
    }
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

/* the Mac shows its own mouse pointer */
void gfx_cursor(float x, float y) { (void)x; (void)y; }

/* ------------------------------------------------------------------ drawing */

NSRect gfx_mac_view_rect(NSSize size) {
    if (aspect == ASPECT_STRETCH) return NSMakeRect(0, 0, size.width, size.height);
    double s = fmin(size.width / VIEW_W, size.height / VIEW_H);
    double w = VIEW_W * s, h = VIEW_H * s;
    return NSMakeRect(floor((size.width - w) / 2), floor((size.height - h) / 2), round(w), round(h));
}

static void encode(id<MTLRenderCommandEncoder> enc, NSRect r) {
    if (!nverts) return;
    [enc setViewport:(MTLViewport){r.origin.x, r.origin.y, r.size.width, r.size.height, 0, 1}];
    [enc setScissorRect:(MTLScissorRect){(NSUInteger)r.origin.x, (NSUInteger)r.origin.y,
                                         (NSUInteger)r.size.width, (NSUInteger)r.size.height}];
    [enc setRenderPipelineState:pipeline];
    [enc setFragmentSamplerState:sampler atIndex:0];
    id<MTLBuffer> vb = [dev newBufferWithBytes:verts length:sizeof(Vtx) * nverts options:MTLResourceStorageModeShared];
    [enc setVertexBuffer:vb offset:0 atIndex:0];
    for (int i = 0; i < nbatches; i++) {
        [enc setFragmentTexture:(__bridge id<MTLTexture>)batches[i].t->obj atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:batches[i].first vertexCount:batches[i].count];
    }
}

void gfx_end(void) {
    MTLRenderPassDescriptor *rp = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!rp || !drawable) return;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    CGSize ds = view.drawableSize;
    encode(enc, gfx_mac_view_rect(NSMakeSize(ds.width, ds.height)));
    [enc endEncoding];
    [cb presentDrawable:drawable];
    [cb commit];
}

/* the frame drawn again into a 640x480 texture and read back */
static id<MTLTexture> grab_texture(void) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:view.colorPixelFormat
                                                                                 width:VIEW_W height:VIEW_H mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget;
    d.storageMode = MTLStorageModeManaged;
    id<MTLTexture> t = [dev newTextureWithDescriptor:d];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = t;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    encode(enc, NSMakeRect(0, 0, VIEW_W, VIEW_H));
    [enc endEncoding];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl synchronizeResource:t];
    [bl endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    return t;
}

int gfx_grab(u8 *rgb, int w, int h) {
    if (w != VIEW_W || h != VIEW_H) return 0;
    id<MTLTexture> t = grab_texture();
    u8 *bgra = malloc(VIEW_W * VIEW_H * 4);
    [t getBytes:bgra bytesPerRow:VIEW_W * 4 fromRegion:MTLRegionMake2D(0, 0, VIEW_W, VIEW_H) mipmapLevel:0];
    for (int i = 0; i < VIEW_W * VIEW_H; i++) {
        rgb[i * 3] = bgra[i * 4 + 2];
        rgb[i * 3 + 1] = bgra[i * 4 + 1];
        rgb[i * 3 + 2] = bgra[i * 4];
    }
    free(bgra);
    return 1;
}

int gfx_mac_grab_png(const char *path) {
    static u8 rgb[VIEW_W * VIEW_H * 3];
    if (!gfx_grab(rgb, VIEW_W, VIEW_H)) return 0;
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, rgb, sizeof rgb, NULL);
    CGImageRef img = CGImageCreate(VIEW_W, VIEW_H, 8, 24, VIEW_W * 3, cs, (CGBitmapInfo)kCGImageAlphaNone, dp, NULL, false,
                                   kCGRenderingIntentDefault);
    NSURL *url = [NSURL fileURLWithPath:@(path)];
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url,
                                                                (__bridge CFStringRef)UTTypePNG.identifier, 1, NULL);
    int ok = 0;
    if (dst) {
        CGImageDestinationAddImage(dst, img, NULL);
        ok = CGImageDestinationFinalize(dst);
        CFRelease(dst);
    }
    CGImageRelease(img);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
    return ok;
}
