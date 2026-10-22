/*
 * The maze view of the editor (PacBackgroundView): the level drawn as the game draws it, with
 * the game's own pictures, and edited with the tools of the Mac editor. Hold Shift or Option
 * to bypass the editor's checks, as the modifier key did.
 */
#import "editor.h"
#include <string.h>
#include "pack.h"
#include "edit_rules.h"

#define VIEW_W 640
#define VIEW_H 480

/* ================================================================== pictures */

@implementation PackArt

static NSMutableDictionary<NSString *, id> *cache;

+ (BOOL)openPack {
    /* the pack of the game around the editor, or one next to the editor */
    NSBundle *b = [NSBundle mainBundle];
    NSString *own = [b pathForResource:@"pack" ofType:@"bin"];
    NSString *game = [[b.bundlePath stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"pack.bin"];
    NSString *installed = nil;
    NSURL *app = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:@"com.quatric.pacx"];
    if (app) installed = [app.path stringByAppendingPathComponent:@"Contents/Resources/pack.bin"];
    for (NSString *p in @[own ?: @"", game, installed ?: @""]) {
        if (p.length && pack_open(p.fileSystemRepresentation)) {
            pack_init();
            cache = [NSMutableDictionary dictionary];
            return pack_count() > 0;
        }
    }
    return NO;
}

static CGImageRef make_image(const u8 *rgba, int w, int h, int stride) {
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef c = CGBitmapContextCreate(NULL, w, h, 8, w * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    u8 *dst = CGBitmapContextGetData(c);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const u8 *s = rgba + y * stride + x * 4;
            u8 *d = dst + (y * w + x) * 4;
            int a = s[3];
            d[0] = s[0] * a / 255;
            d[1] = s[1] * a / 255;
            d[2] = s[2] * a / 255;
            d[3] = a;
        }
    CGImageRef img = CGBitmapContextCreateImage(c);
    CGContextRelease(c);
    CGColorSpaceRelease(cs);
    return img;
}

static const PackEntry *tex_entry(NSString *name) {
    const PackEntry *e = pack_find(name.UTF8String);
    return e && e->type == PK_TEX ? e : NULL;
}

+ (CGImageRef)cached:(NSString *)key make:(CGImageRef (^)(void))make {
    id v = cache[key];
    if (v == [NSNull null]) return NULL;
    if (v) return (__bridge CGImageRef)v;
    CGImageRef img = make();
    cache[key] = img ? (__bridge_transfer id)img : [NSNull null];
    return img;
}

+ (CGImageRef)image:(NSString *)name {
    return [self cached:name make:^CGImageRef {
        const PackEntry *e = tex_entry(name);
        return e ? make_image(pack_data(e), e->w, e->h, e->pw * 4) : NULL;
    }];
}

+ (CGImageRef)frame:(int)i of:(NSString *)name w:(int)w h:(int)h {
    NSString *key = [NSString stringWithFormat:@"%@#%d", name, i];
    return [self cached:key make:^CGImageRef {
        const PackEntry *e = tex_entry(name);
        if (!e || w <= 0 || h <= 0) return NULL;
        int cols = e->w / w, rows = e->h / h;
        if (i < 0 || i >= cols * rows) return NULL;
        const u8 *px = (const u8 *)pack_data(e) + ((i / cols) * h * e->pw + (i % cols) * w) * 4;
        return make_image(px, w, h, e->pw * 4);
    }];
}

+ (CGImageRef)tinted:(NSString *)name red:(int)r green:(int)g blue:(int)b {
    /* the whole picture in the colour of the maze, as spr_set_color tints it in the game */
    NSString *key = [NSString stringWithFormat:@"%@/%d,%d,%d", name, r, g, b];
    return [self cached:key make:^CGImageRef {
        const PackEntry *e = tex_entry(name);
        if (!e) return NULL;
        int n = e->pw * e->h * 4;
        u8 *px = malloc(n);
        const u8 *src = pack_data(e);
        for (int i = 0; i < n; i += 4) {
            px[i] = src[i] * r / 255;
            px[i + 1] = src[i + 1] * g / 255;
            px[i + 2] = src[i + 2] * b / 255;
            px[i + 3] = src[i + 3];
        }
        CGImageRef img = make_image(px, e->w, e->h, e->pw * 4);
        free(px);
        return img;
    }];
}

+ (CGImageRef)tile:(int)i set:(NSString *)tileset red:(int)r green:(int)g blue:(int)b {
    NSString *key = [NSString stringWithFormat:@"%@#%d/%d,%d,%d", tileset, i, r, g, b];
    return [self cached:key make:^CGImageRef {
        CGImageRef whole = [self tinted:tileset red:r green:g blue:b];
        if (!whole) return NULL;
        size_t cols = CGImageGetWidth(whole) / 11, rows = CGImageGetHeight(whole) / 11;
        if (i < 0 || (size_t)i >= cols * rows) return NULL;
        return CGImageCreateWithImageInRect(whole, CGRectMake((i % cols) * 11, (i / cols) * 11, 11, 11));
    }];
}

+ (NSImage *)icon:(NSString *)name {
    CGImageRef img = [self image:name];
    if (!img) return nil;
    return [[NSImage alloc] initWithCGImage:img size:NSMakeSize(CGImageGetWidth(img), CGImageGetHeight(img))];
}

+ (NSArray<NSString *> *)backgrounds {
    NSMutableArray *a = [NSMutableArray array];
    for (int i = 0; i < pack_count(); i++) {
        const PackEntry *e = pack_entry(i);
        if (e->type == PK_TEX && !strncmp(e->name, "bg/", 3)) [a addObject:@(e->name + 3)];
    }
    [a sortUsingSelector:@selector(localizedStandardCompare:)];
    return a;
}

@end

/* ================================================================== the view */

@implementation MazeView {
    BOOL dragging, dirSet;
    EdDrag drag;
    NSPoint start, dragAt, hover;
    BOOL hoverOn;
    NSTrackingArea *tracking;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }

/* the 640x480 picture in the view, keeping its proportions */
- (NSRect)pictureRect {
    NSRect b = self.bounds;
    CGFloat s = fmin(b.size.width / VIEW_W, b.size.height / VIEW_H);
    CGFloat w = VIEW_W * s, h = VIEW_H * s;
    return NSMakeRect(floor((b.size.width - w) / 2), floor((b.size.height - h) / 2), w, h);
}

- (NSPoint)viewPoint:(NSEvent *)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    NSRect r = [self pictureRect];
    return NSMakePoint((p.x - r.origin.x) / r.size.width * VIEW_W, (p.y - r.origin.y) / r.size.height * VIEW_H);
}

static int modifier(NSEvent *e) {
    return (e.modifierFlags & (NSEventModifierFlagShift | NSEventModifierFlagOption)) != 0;
}

- (void)updateTrackingAreas {
    if (tracking) [self removeTrackingArea:tracking];
    tracking = [[NSTrackingArea alloc] initWithRect:self.bounds
                                            options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                                                    NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                              owner:self
                                           userInfo:nil];
    [self addTrackingArea:tracking];
    [super updateTrackingAreas];
}

- (void)mouseMoved:(NSEvent *)e {
    hover = [self viewPoint:e];
    hoverOn = YES;
    self.needsDisplay = YES;
}

- (void)mouseExited:(NSEvent *)e {
    hoverOn = NO;
    self.needsDisplay = YES;
}

- (void)mouseDown:(NSEvent *)e {
    [self.window makeFirstResponder:self];
    NSPoint p = [self viewPoint:e];
    Level *l = [self.delegate currentLevel];
    if (!l) return;
    int c, r;
    if (!ed_cell_at(p.x, p.y, &c, &r)) return;
    int mod = modifier(e);
    switch ([self.delegate tool]) {
    case T_PATH:
    case T_ERASE:
    case T_CITADEL:
        dragging = YES;
        drag = (EdDrag){c, r, c, r, 0, 0};
        start = dragAt = p;
        dirSet = NO;
        break;
    case T_PELLET:
        if (ed_is_super_pellet(l, c, r) || ed_can_place(l, c, r, mod)) {
            [self.delegate levelWillChange:ed_is_super_pellet(l, c, r) ? @"Remove Super Pellet" : @"Add Super Pellet"];
            ed_toggle_pellet(l, c, r, mod);
            [self.delegate levelDidChange];
        } else {
            NSBeep();
        }
        break;
    case T_PLAYER1:
    case T_PLAYER2:
        if (ed_can_place(l, c, r, mod)) {
            int nr = [self.delegate tool] == T_PLAYER1 ? 1 : 2;
            [self.delegate levelWillChange:nr == 1 ? @"Move Player 1" : @"Move Player 2"];
            ed_set_player(l, nr, c, r, mod);
            [self.delegate levelDidChange];
        } else {
            NSBeep();
        }
        break;
    }
    self.needsDisplay = YES;
}

- (void)mouseDragged:(NSEvent *)e {
    if (!dragging) return;
    NSPoint p = [self viewPoint:e];
    dragAt = hover = p;
    if ([self.delegate tool] != T_CITADEL) {
        int c, r;
        if (!ed_cell_at(p.x, p.y, &c, &r) && [self.delegate tool] == T_PATH) drag.pokeHole = 1;
        float dx = p.x - start.x, dy = p.y - start.y;
        if (!dirSet && (fabs(dx) > 22 || fabs(dy) > 22)) {
            dirSet = YES;
            drag.vertical = fabs(dy) > fabs(dx);
        }
        drag.endCol = c;
        drag.endRow = r;
        if (dirSet) {
            if (drag.vertical) drag.endCol = drag.startCol;
            else drag.endRow = drag.startRow;
        }
    }
    self.needsDisplay = YES;
}

- (void)mouseUp:(NSEvent *)e {
    if (!dragging) return;
    dragging = NO;
    Level *l = [self.delegate currentLevel];
    int tool = [self.delegate tool];
    if (tool == T_CITADEL) {
        int cc, rr;
        ed_citadel_cell(dragAt.x, dragAt.y, &cc, &rr);
        [self.delegate levelWillChange:@"Move Citadel"];
        ed_move_citadel(l, cc, rr);
        [self.delegate levelDidChange];
    } else if (dirSet || tool == T_ERASE) {
        [self.delegate levelWillChange:tool == T_ERASE ? @"Erase Path" : @"Draw Path"];
        ed_update_paths(l, &drag, tool == T_ERASE, modifier(e));
        [self.delegate levelDidChange];
    }
    self.needsDisplay = YES;
}

/* ------------------------------------------------------------------ drawing */

static void draw(CGContextRef c, CGImageRef img, CGFloat x, CGFloat y) {
    if (!img) return;
    CGFloat w = CGImageGetWidth(img), h = CGImageGetHeight(img);
    CGContextSaveGState(c);
    CGContextTranslateCTM(c, x, y + h);
    CGContextScaleCTM(c, 1, -1);
    CGContextDrawImage(c, CGRectMake(0, 0, w, h), img);
    CGContextRestoreGState(c);
}

typedef struct { CGContextRef c; int x, y; NSString *tileset; const Level *l; } Emit;

static void emit(void *u, int frame, int xo, int yo) {
    Emit *e = u;
    CGImageRef t = [PackArt tile:frame set:e->tileset red:e->l->red green:e->l->green blue:e->l->blue];
    draw(e->c, t, 44 * e->x + 11 * xo + 34, 44 * e->y + 11 * yo + 36);
}

static void box(CGContextRef c, CGRect r, CGFloat red, CGFloat g, CGFloat b, CGFloat a, CGFloat fill) {
    if (fill > 0) {
        CGContextSetRGBFillColor(c, red, g, b, fill);
        CGContextFillRect(c, r);
    }
    CGContextSetRGBStrokeColor(c, red, g, b, a);
    CGContextSetLineWidth(c, 2);
    CGContextStrokeRect(c, CGRectInset(r, 1, 1));
}

- (void)drawLevel:(const Level *)l in:(CGContextRef)c {
    /* the background picture, tiled over the screen */
    CGImageRef bg = [PackArt image:[@"bg/" stringByAppendingString:@(l->background)]];
    if (bg) {
        size_t w = CGImageGetWidth(bg), h = CGImageGetHeight(bg);
        for (size_t y = 0; y < VIEW_H; y += h)
            for (size_t x = 0; x < VIEW_W; x += w) draw(c, bg, x, y);
    }

    int ec = -1, er = -1;
    for (int r = 0; r < LV_H; r++)
        for (int col = 0; col < LV_W; col++)
            if (l->tiles[r][col] == 'R') {
                ec = col;
                er = r - 1;
                break;
            }

    Emit e = {c, 0, 0, strcmp(l->tileset, "tile2") ? @"tile" : @"tile2", l};
    CGImageRef pellet = [PackArt image:@"pellet"];
    CGImageRef sp = [PackArt frame:0 of:@"super_pellet" w:30 h:30];
    for (int r = 0; r < LV_H; r++)
        for (int col = 0; col < LV_W; col++) {
            int t = l->tiles[r][col] - 'A';
            if (t < 0 || t > 17) t = 0;
            if (t > 15) continue;
            e.x = col;
            e.y = r;
            maze_cell_parts(t, emit, &e);
            if (t == 0) continue;
            int startCell = (col == l->p1x && r == l->p1y) || (col == l->p2x && r == l->p2y);
            if (ed_is_super_pellet(l, col, r) && !startCell) draw(c, sp, 44 * col + 41, 44 * r + 43);
            else if (!startCell) draw(c, pellet, 44 * col + 51, 44 * r + 53);
            if ((t & 2) && col < 12) draw(c, pellet, 44 * col + 73, 44 * r + 53);
            if ((t & 8) && r <= 8 && !(col == ec && r == er)) draw(c, pellet, 44 * col + 51, 44 * r + 75);
        }
    if (ec >= 0) {
        NSString *cn = [NSString stringWithFormat:@"citadel%d", citadel_number(l->citadel)];
        draw(c, [PackArt tinted:cn red:l->red green:l->green blue:l->blue], 44 * ec - 10, 44 * er + 80);
        draw(c, [PackArt image:@"barrier"], 44 * ec + 36, 44 * er + 76);
    }
    draw(c, [PackArt frame:11 of:@"player1" w:32 h:32], 44 * l->p1x + 40, 44 * l->p1y + 42);
    draw(c, [PackArt frame:4 of:@"player2" w:32 h:32], 44 * l->p2x + 40, 44 * l->p2y + 42);

    /* the parts of the maze that wrap around are covered, as in the game */
    CGContextSetRGBFillColor(c, 0, 0, 0, 1);
    CGContextFillRect(c, CGRectMake(0, 0, VIEW_W, ED_VY));
    CGContextFillRect(c, CGRectMake(0, 476, VIEW_W, 4));
    CGContextFillRect(c, CGRectMake(0, 0, ED_VX, VIEW_H));
    CGContextFillRect(c, CGRectMake(606, 0, 34, VIEW_H));
}

- (void)drawRect:(NSRect)dirty {
    CGContextRef c = [NSGraphicsContext currentContext].CGContext;
    [[NSColor colorWithWhite:0.12 alpha:1] setFill];
    NSRectFill(self.bounds);
    Level *l = [self.delegate currentLevel];
    if (!l) return;
    NSRect pr = [self pictureRect];
    CGContextSaveGState(c);
    CGContextTranslateCTM(c, pr.origin.x, pr.origin.y);
    CGContextScaleCTM(c, pr.size.width / VIEW_W, pr.size.height / VIEW_H);
    CGContextClipToRect(c, CGRectMake(0, 0, VIEW_W, VIEW_H));
    CGContextSetInterpolationQuality(c, kCGInterpolationHigh);
    [self drawLevel:l in:c];

    int tool = [self.delegate tool];
    if (dragging && tool != T_CITADEL) {
        int minC = MIN(drag.startCol, drag.endCol), maxC = MAX(drag.startCol, drag.endCol);
        int minR = MIN(drag.startRow, drag.endRow), maxR = MAX(drag.startRow, drag.endRow);
        CGFloat r = tool == T_ERASE ? 1 : 0.2, g = tool == T_ERASE ? 0.2 : 1;
        for (int y = minR; y <= maxR; y++)
            for (int x = minC; x <= maxC; x++)
                box(c, CGRectMake(ED_VX + x * ED_CELL, ED_VY + y * ED_CELL, ED_CELL, ED_CELL), r, g, 0.2, 0.9, 0.25);
    } else if (tool == T_CITADEL && (dragging || hoverOn)) {
        int cc, rr;
        NSPoint p = dragging ? dragAt : hover;
        ed_citadel_cell(p.x, p.y, &cc, &rr);
        box(c, CGRectMake(ED_VX + cc * ED_CELL, ED_VY + rr * ED_CELL, 3 * ED_CELL, 2 * ED_CELL), 1, 1, 0.3, 0.9, 0);
    } else if (hoverOn) {
        int col, r;
        if (ed_cell_at(hover.x, hover.y, &col, &r)) {
            int mod = (NSEvent.modifierFlags & (NSEventModifierFlagShift | NSEventModifierFlagOption)) != 0;
            BOOL ok = tool == T_PATH || tool == T_ERASE || ed_can_place(l, col, r, mod) ||
                      (tool == T_PELLET && ed_is_super_pellet(l, col, r));
            box(c, CGRectMake(ED_VX + col * ED_CELL, ED_VY + r * ED_CELL, ED_CELL, ED_CELL), 1, ok ? 1 : 0.2,
                ok ? 1 : 0.2, 0.9, 0);
        }
    }
    CGContextRestoreGState(c);
}

- (void)flagsChanged:(NSEvent *)e {
    self.needsDisplay = YES;
    [super flagsChanged:e];
}

@end
