/*
 * Pac the Man X for the Mac: the AppController of the original, its menus and the game window.
 * The game logic runs at the original's 60 steps a second, whatever the display's rate.
 *
 * The editor asks for one of its levels to be tried with the distributed notification
 * PacxTryLevel (File, Level, Players), or with the same arguments when it starts the game.
 */
#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "pack.h"
#include "engine.h"
#include "game.h"
#include "actors.h"
#include "input.h"
#include "audio.h"
#include "settings.h"
#include "levels.h"
#include "ui.h"
#include "macplat.h"

#define STEP (1.0 / FPS)
#define EDITOR_ID @"com.quatric.pacx.editor"
#define TRY_NOTIFICATION @"com.quatric.pacx.TryLevel"

const char *platform_language(void) {
    for (NSString *l in [NSLocale preferredLanguages]) {
        if ([l hasPrefix:@"de"]) return "de";
        if ([l hasPrefix:@"fr"]) return "fr";
        if ([l hasPrefix:@"en"]) return "en";
    }
    return "en";
}

/* ================================================================== the frame */

/* the title buttons with the keyboard: -1 until it is used */
static int splashSel = -1;
static int testFrame;
static const char *testDir;
static LevelSet trySet;

static Sprite *splash_button(int i) {
    for (int k = 0; k < layer_count(L(0)); k++) {
        Sprite *s = layer_at(L(0), k);
        if (s && s->kind == K_BUTTON && s->frameIndex == i) return s;
    }
    return NULL;
}

static void splash_input(void) {
    u32 rep = in_menu_repeat(), pr = in_menu_pressed();
    if (rep & (MB_LEFT | MB_RIGHT | MB_UP | MB_DOWN)) {
        if (splashSel < 0) splashSel = 0;
        else if (rep & (MB_LEFT | MB_UP)) splashSel = (splashSel + 5) % 6;
        else splashSel = (splashSel + 1) % 6;
        game.automaticTimerStart = 45;
    }
    float x, y;
    if (in_pointer(&x, &y)) {
        static float lx, ly;
        if (x != lx || y != ly) splashSel = -1;
        lx = x;
        ly = y;
    }
    if ((pr & MB_OK) && splashSel >= 0) {
        Sprite *b = splash_button(splashSel);
        if (b) button_press(b);
    }
}

/* mouseDown: and keyDown: of the game view */
static void game_input(void) {
    if (game.mode == MODE_SPLASH) splash_input();
    float x, y;
    if (in_click() && in_pointer(&x, &y)) {
        eng_post(EV_MOUSE, x, y);
        return;
    }
    if (!in_any_pressed()) return;
    if (game.mode == MODE_SPLASH) return;
    game.keyEvent = in_key_pressed(KEY_CANCEL) ? KEY_CANCEL : in_key_pressed(KEY_PAUSE) ? KEY_PAUSE : -1;
    eng_post(EV_KEY, 0, 0);
}

static void handle_events(void) {
    Event e;
    while (eng_poll(&e)) {
        if (e.type == EV_PREFS) ui_open_prefs();
        else game_handle_event(&e);
    }
}

static void draw_splash_selection(void) {
    if (game.mode != MODE_SPLASH || splashSel < 0) return;
    Sprite *b = splash_button(splashSel);
    if (b) gfx_frame(b->x - 2, b->y - 2, 52, 52, 2, 1, 1, 0.4f, 0.9f);
}

static void back_to_editor(void) {
    NSRunningApplication *ed = [NSRunningApplication runningApplicationsWithBundleIdentifier:EDITOR_ID].firstObject;
    if (@available(macOS 14.0, *)) [ed activateWithOptions:0];
    else [ed activateWithOptions:NSApplicationActivateIgnoringOtherApps];
}

static void test_shot(int n) {
    char p[600];
    snprintf(p, sizeof p, "%s/%02d.png", testDir, n);
    gfx_mac_grab_png(p);
}

/* the scripted run of PACX_TEST_SHOTS=<folder>: pictures of each screen, then it quits */
static void test_script(int frame) {
    switch (frame) {
    case 150: test_shot(1); break;                                  /* title */
    case 160: eng_post(EV_NEW_GAME1, 0, 0); break;
    case 260: test_shot(2); break;                                  /* ready */
    case 900: test_shot(3); break;                                  /* playing */
    case 910: eng_post(EV_PAUSE, 0, 0); break;
    case 930: test_shot(4); break;                                  /* paused */
    case 940: game.keyEvent = KEY_CANCEL; eng_post(EV_KEY, 0, 0); break;
    case 1000: eng_post(EV_HIGHSCORES, 0, 0); break;
    case 1100: test_shot(5); break;                                 /* high scores */
    case 1110: game_show_splash(); eng_post(EV_HELP, 0, 0); break;
    case 1250: test_shot(6); break;                                 /* help */
    case 1260: game_show_splash(); eng_post(EV_NEW_GAME, 0, 0); break;
    case 1500: test_shot(7); break;                                 /* two players */
    case 1510: [NSApp terminate:nil]; break;
    }
}

static void tick(void) {
    in_update();
    audio_update();
    ui_update();
    if (!ui_active()) {
        game_input();
        handle_events();
    }
    if (game_take_editor_return()) back_to_editor();
    if (!ui_active() && anim.running) eng_step();
}

/* the quads of a frame, drawn by gfx_end */
static void record(void) {
    gfx_begin();
    gfx_rect(0, 0, VIEW_W, VIEW_H, 0, 0, 0, 1);
    eng_draw();
    draw_splash_selection();
    ui_draw();
}

/* ================================================================== the game view */

@interface GameView : MTKView <MTKViewDelegate>
@end

@implementation GameView {
    double last, acc;
    NSTrackingArea *tracking;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }

- (void)keyDown:(NSEvent *)e {
    if (!e.isARepeat) in_mac_key(e.keyCode, 1);
    if (game.mode == MODE_GAME) [NSCursor setHiddenUntilMouseMoves:YES];
}
- (void)keyUp:(NSEvent *)e { in_mac_key(e.keyCode, 0); }
- (void)flagsChanged:(NSEvent *)e { in_mac_flags(e.modifierFlags); }

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

- (void)mouseAt:(NSEvent *)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    NSRect r = gfx_mac_view_rect(self.bounds.size);
    float x = (p.x - r.origin.x) / r.size.width * VIEW_W;
    float y = (r.origin.y + r.size.height - p.y) / r.size.height * VIEW_H;
    in_mac_mouse(x, y, x >= 0 && x < VIEW_W && y >= 0 && y < VIEW_H);
}

- (void)mouseMoved:(NSEvent *)e { [self mouseAt:e]; }
- (void)mouseDragged:(NSEvent *)e { [self mouseAt:e]; }
- (void)mouseExited:(NSEvent *)e { in_mac_mouse(-1, -1, 0); }
- (void)mouseDown:(NSEvent *)e {
    [self mouseAt:e];
    in_mac_button(0, 1);
}
- (void)mouseUp:(NSEvent *)e { in_mac_button(0, 0); }
- (void)rightMouseDown:(NSEvent *)e {
    [self mouseAt:e];
    in_mac_button(1, 1);
}
- (void)rightMouseUp:(NSEvent *)e { in_mac_button(1, 0); }

- (void)mtkView:(MTKView *)v drawableSizeWillChange:(CGSize)size {}

- (void)drawInMTKView:(MTKView *)v {
    double now = CACurrentMediaTime();
    if (last == 0) last = now - STEP;
    acc += now - last;
    last = now;
    if (acc > 0.25) acc = 0.25;
    /* one step per frame at 60 Hz; the remainder evens out other rates */
    for (int n = 0; acc > STEP * 0.5 && n < 4; n++) {
        tick();
        acc -= STEP;
        if (testDir) {
            record();
            test_script(++testFrame);
        }
    }
    record();
    gfx_end();
}

@end

/* ================================================================== the application */

@interface AppController : NSObject <NSApplicationDelegate, NSWindowDelegate, NSMenuItemValidation>
@property (strong) NSWindow *window;
@property (strong) GameView *view;
- (void)updateAspect:(NSWindow *)w;
@end

static void activate(void) {
    if (@available(macOS 14.0, *)) [NSApp activate];
    else [NSApp activateIgnoringOtherApps:YES];
}

void mac_update_window_aspect(void) {
    AppController *c = (AppController *)NSApp.delegate;
    [c updateAspect:c.window];
}

void mac_open_editor(void) {
    NSString *p = [[NSBundle mainBundle] pathForResource:@"Pac the Man X Editor" ofType:@"app"];
    if (!p) {
        NSBeep();
        return;
    }
    NSWorkspaceOpenConfiguration *c = [NSWorkspaceOpenConfiguration configuration];
    [[NSWorkspace sharedWorkspace] openApplicationAtURL:[NSURL fileURLWithPath:p] configuration:c completionHandler:nil];
}

void mac_open_levels_folder(void) {
    char p[300];
    snprintf(p, sizeof p, "%s/levels", appDir);
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:@(p) isDirectory:YES]];
}

static NSMenuItem *item(NSMenu *m, NSString *title, SEL action, NSString *key, id target) {
    NSMenuItem *i = [m addItemWithTitle:title action:action keyEquivalent:key ? key : @""];
    i.target = target;
    return i;
}

@implementation AppController

- (void)buildMenus {
    NSMenu *bar = [NSMenu new];
    NSString *name = @"Pac the Man X";

    NSMenu *app = [[NSMenu alloc] initWithTitle:name];
    item(app, [@"About " stringByAppendingString:name], @selector(about:), nil, self);
    [app addItem:[NSMenuItem separatorItem]];
    item(app, @"Preferences…", @selector(showPrefs:), @",", self);
    [app addItem:[NSMenuItem separatorItem]];
    NSMenu *services = [NSMenu new];
    [app addItemWithTitle:@"Services" action:nil keyEquivalent:@""].submenu = services;
    NSApp.servicesMenu = services;
    [app addItem:[NSMenuItem separatorItem]];
    item(app, [@"Hide " stringByAppendingString:name], @selector(hide:), @"h", nil);
    item(app, @"Hide Others", @selector(hideOtherApplications:), @"h", nil).keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagOption;
    item(app, @"Show All", @selector(unhideAllApplications:), nil, nil);
    [app addItem:[NSMenuItem separatorItem]];
    item(app, [@"Quit " stringByAppendingString:name], @selector(terminate:), @"q", nil);
    [bar addItemWithTitle:@"" action:nil keyEquivalent:@""].submenu = app;

    NSMenu *g = [[NSMenu alloc] initWithTitle:@"Game"];
    item(g, @"Single player game (Pac-Man)", @selector(newGame1:), @"1", self);
    item(g, @"Single player game (Ms. Pac-Man)", @selector(newGame2:), @"2", self);
    item(g, @"Two player game (cooperative)", @selector(newGameTwoPlayers:), @"n", self);
    [g addItem:[NSMenuItem separatorItem]];
    item(g, @"Pause game", @selector(pauseGame:), @"p", self);
    item(g, @"Resume game", @selector(resumeGame:), @"r", self);
    [g addItem:[NSMenuItem separatorItem]];
    item(g, @"Try level…", @selector(showTry:), @"t", self);
    item(g, @"Launch level editor…", @selector(launchEditor:), @"l", self);
    item(g, @"Open custom levels folder in Finder", @selector(openLevelsFolder:), nil, self);
    [g addItem:[NSMenuItem separatorItem]];
    item(g, @"View highscore list", @selector(viewScores:), @"s", self);
    item(g, @"Clear highscore list…", @selector(clearScores:), nil, self);
    [bar addItemWithTitle:@"Game" action:nil keyEquivalent:@""].submenu = g;

    NSMenu *ed = [[NSMenu alloc] initWithTitle:@"Edit"];
    item(ed, @"Undo", @selector(undo:), @"z", nil);
    item(ed, @"Redo", @selector(redo:), @"Z", nil);
    [ed addItem:[NSMenuItem separatorItem]];
    item(ed, @"Cut", @selector(cut:), @"x", nil);
    item(ed, @"Copy", @selector(copy:), @"c", nil);
    item(ed, @"Paste", @selector(paste:), @"v", nil);
    item(ed, @"Delete", @selector(delete:), nil, nil);
    item(ed, @"Select All", @selector(selectAll:), @"a", nil);
    [bar addItemWithTitle:@"Edit" action:nil keyEquivalent:@""].submenu = ed;

    NSMenu *w = [[NSMenu alloc] initWithTitle:@"Window"];
    item(w, @"Minimize", @selector(performMiniaturize:), @"m", nil);
    item(w, @"Zoom", @selector(performZoom:), nil, nil);
    item(w, @"Toggle full screen", @selector(toggleFullScreen:), @"f", nil);
    [w addItem:[NSMenuItem separatorItem]];
    item(w, @"Bring All to Front", @selector(arrangeInFront:), nil, nil);
    [bar addItemWithTitle:@"Window" action:nil keyEquivalent:@""].submenu = w;
    NSApp.windowsMenu = w;

    NSMenu *h = [[NSMenu alloc] initWithTitle:@"Help"];
    item(h, @"Pac the Man X Help", @selector(showHelp:), @"?", self);
    [bar addItemWithTitle:@"Help" action:nil keyEquivalent:@""].submenu = h;
    NSApp.helpMenu = h;

    NSApp.mainMenu = bar;
}

- (void)buildWindow {
    NSRect r = NSMakeRect(0, 0, VIEW_W, VIEW_H);
    NSWindow *w = [[NSWindow alloc] initWithContentRect:r
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    w.title = @"Pac the Man X";
    w.delegate = self;
    w.contentMinSize = NSMakeSize(VIEW_W / 2, VIEW_H / 2);
    w.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    w.backgroundColor = NSColor.blackColor;
    [self updateAspect:w];

    GameView *v = [[GameView alloc] initWithFrame:r device:MTLCreateSystemDefaultDevice()];
    v.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    v.preferredFramesPerSecond = FPS;
    v.delegate = v;
    v.layer.opaque = YES;
    ((CAMetalLayer *)v.layer).colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    w.contentView = v;
    [w makeFirstResponder:v];
    if (![w setFrameUsingName:@"GameWindow"]) [w center];
    w.frameAutosaveName = @"GameWindow";
    self.window = w;
    self.view = v;
}

- (void)updateAspect:(NSWindow *)w {
    /* the window keeps the 4:3 shape of the picture unless it is stretched */
    if (cfg.aspect == ASPECT_STRETCH) {
        w.contentResizeIncrements = NSMakeSize(1, 1);
    } else {
        w.contentAspectRatio = NSMakeSize(VIEW_W, VIEW_H);
        if (!(w.styleMask & NSWindowStyleMaskFullScreen)) {
            NSRect f = [w contentRectForFrameRect:w.frame];
            f.size.height = round(f.size.width * VIEW_H / VIEW_W);
            [w setFrame:[w frameRectForContentRect:f] display:YES animate:NO];
        }
    }
}

- (void)installLevels {
    /* the extra level set, once, into the levels folder where the game and the editor find it */
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    if ([ud boolForKey:@"InstalledXLevels"]) return;
    NSString *src = [[NSBundle mainBundle] pathForResource:@"The X Levels" ofType:@"plist" inDirectory:@"Levels"];
    if (!src) return;
    NSString *dst = [[@(appDir) stringByAppendingPathComponent:@"levels"] stringByAppendingPathComponent:@"The X Levels.plist"];
    NSFileManager *fm = [NSFileManager defaultManager];
    if ([fm fileExistsAtPath:dst] || [fm copyItemAtPath:src toPath:dst error:nil]) [ud setBool:YES forKey:@"InstalledXLevels"];
}

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    NSString *packPath = [[NSBundle mainBundle] pathForResource:@"pack" ofType:@"bin"];
    if (!packPath || !pack_open(packPath.fileSystemRepresentation)) {
        NSAlert *a = [NSAlert new];
        a.messageText = @"The game data is missing.";
        a.informativeText = @"Build the application with the data converted from your copy of Pac the Man X (see README.md).";
        [a runModal];
        [NSApp terminate:nil];
        return;
    }
    pack_init();
    storage_init(NULL);
    settings_load();
    strings_init();
    hs_load();
    [self installLevels];

    [self buildMenus];
    [self buildWindow];
    ui_mac_set_window(self.window);
    gfx_mac_init(self.view);
    gfx_set_aspect(cfg.aspect);
    in_init();
    NSString *music = [[NSBundle mainBundle].resourcePath stringByAppendingPathComponent:@"Music"];
    audio_init(music.fileSystemRepresentation);
    eng_init();
    game_init();
    ui_init();
    game_update_settings();

    rnd_seed((u32)(CACurrentMediaTime() * 1000));
    game_show_splash();

    testDir = getenv("PACX_TEST_SHOTS");
    [[NSDistributedNotificationCenter defaultCenter] addObserver:self
                                                        selector:@selector(tryLevelNotification:)
                                                            name:TRY_NOTIFICATION
                                                          object:nil];
    NSUserDefaults *args = [NSUserDefaults standardUserDefaults];
    NSString *file = [args stringForKey:@"TryFile"];
    if (file) [self tryFile:file level:(int)[args integerForKey:@"TryLevel"] players:(int)[args integerForKey:@"TryPlayers"]];

    [self.window makeKeyAndOrderFront:nil];
    activate();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }

- (void)applicationWillTerminate:(NSNotification *)n {
    settings_save();
    mus_stop();
    snd_stop_all();
}

- (void)applicationWillResignActive:(NSNotification *)n {
    in_mac_release_all();
    if (game.mode == MODE_GAME && anim.running && !testDir) eng_post(EV_DEACTIVATE, 0, 0);
}

- (void)windowDidResignKey:(NSNotification *)n { in_mac_release_all(); }

/* ------------------------------------------------------------------ trying a level of the editor */

- (void)tryFile:(NSString *)file level:(int)level players:(int)players {
    LevelSet s = {0};
    if (!levels_load_file(&s, file.fileSystemRepresentation) || s.n < 1) {
        levels_free(&s);
        ui_alert("This level file cannot be read.");
        return;
    }
    ui_close_all();
    levels_free(&trySet);
    trySet = s;
    if (level < 0 || level >= trySet.n) level = 0;
    if (players < 0 || players > 2) players = 1;
    game_try_editor_level(&trySet, level, players);
    [self.window makeKeyAndOrderFront:nil];
    activate();
}

- (void)tryLevelNotification:(NSNotification *)n {
    NSDictionary *u = n.userInfo;
    NSString *file = u[@"File"];
    if (![file isKindOfClass:[NSString class]]) return;
    [self tryFile:file level:[u[@"Level"] intValue] players:[u[@"Players"] intValue]];
}

/* ------------------------------------------------------------------ menu actions */

- (void)post:(int)ev {
    if (ui_active()) return;
    eng_post(ev, 0, 0);
}

- (void)about:(id)s {
    NSMutableString *c = [NSMutableString string];
    for (int i = 2; i <= 8; i++) {
        char k[16];
        snprintf(k, sizeof k, "Credits%d", i);
        if (i != 7) [c appendFormat:@"%s\n", tr(k)];
    }
    [c appendString:@"\nMac port of the Wii version by quatric."];
    NSDictionary *attrs = @{NSFontAttributeName: [NSFont systemFontOfSize:11],
                            NSForegroundColorAttributeName: NSColor.labelColor};
    NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new];
    ps.alignment = NSTextAlignmentCenter;
    NSMutableAttributedString *a = [[NSMutableAttributedString alloc] initWithString:c attributes:attrs];
    [a addAttribute:NSParagraphStyleAttributeName value:ps range:NSMakeRange(0, a.length)];
    [NSApp orderFrontStandardAboutPanelWithOptions:@{NSAboutPanelOptionCredits: a}];
}

- (void)showPrefs:(id)s { ui_open_prefs(); }
- (void)newGame1:(id)s { [self post:EV_NEW_GAME1]; }
- (void)newGame2:(id)s { [self post:EV_NEW_GAME2]; }
- (void)newGameTwoPlayers:(id)s { [self post:EV_NEW_GAME]; }
- (void)pauseGame:(id)s { [self post:EV_PAUSE]; }
- (void)resumeGame:(id)s { [self post:EV_RESUME]; }
- (void)showTry:(id)s { ui_mac_show_try_panel(); }
- (void)launchEditor:(id)s { mac_open_editor(); }
- (void)openLevelsFolder:(id)s { mac_open_levels_folder(); }
- (void)viewScores:(id)s { [self post:EV_HIGHSCORES]; }
- (void)clearScores:(id)s { ui_mac_clear_scores(); }
- (void)showHelp:(id)s { [self post:EV_HELP]; }

- (BOOL)validateMenuItem:(NSMenuItem *)m {
    SEL a = m.action;
    if (a == @selector(pauseGame:)) return game.mode == MODE_GAME && anim.running && !game.gameOver && !ui_active();
    if (a == @selector(resumeGame:)) return game.mode == MODE_GAME && !anim.running && !ui_active();
    if (a == @selector(about:) || a == @selector(launchEditor:) || a == @selector(openLevelsFolder:)) return YES;
    return !ui_active();
}

@end

int main(int argc, const char **argv) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        AppController *c = [AppController new];
        app.delegate = c;
        [app run];
    }
    return 0;
}
