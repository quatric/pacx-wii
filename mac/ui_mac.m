/*
 * The windows and panels of the original AppController: the preferences window, the name
 * panel after a high score, the try level panel and the alerts. The panels are sheets on the
 * game window; the game waits while one is open, as it did behind the modal panels.
 */
#import <Cocoa/Cocoa.h>
#include <string.h>
#include "ui.h"
#include "gfx.h"
#include "engine.h"
#include "game.h"
#include "input.h"
#include "audio.h"
#include "settings.h"
#include "levels.h"
#include "macplat.h"

static NSWindow *gameWindow;
static int sheets;              /* sheets open or about to open */
static int quitRequested;

void ui_mac_set_window(NSWindow *w) { gameWindow = w; }
int ui_quit_requested(void) { return quitRequested; }
int ui_active(void) { return sheets > 0; }
void ui_draw(void) {}
void ui_init(void) {}

static NSString *T(const char *key) { return @(tr(key)); }

void ui_close_all(void) {
    for (NSWindow *s in [gameWindow.sheets copy]) [gameWindow endSheet:s returnCode:NSModalResponseCancel];
}

/* a sheet on the game window, or a modal alert when the window is not on screen */
static void run_alert(NSAlert *a, void (^done)(NSModalResponse r)) {
    sheets++;
    if (gameWindow.visible) {
        [a beginSheetModalForWindow:gameWindow completionHandler:^(NSModalResponse r) {
            sheets--;
            if (done) done(r);
        }];
    } else {
        NSModalResponse r = [a runModal];
        sheets--;
        if (done) done(r);
    }
}

void ui_alert(const char *text) {
    NSAlert *a = [NSAlert new];
    a.messageText = T("Error");
    a.informativeText = @(text);
    run_alert(a, nil);
}

void ui_confirm(const char *title, const char *text, void (*yes)(void)) {
    NSAlert *a = [NSAlert new];
    a.messageText = @(title);
    a.informativeText = @(text);
    [a addButtonWithTitle:T("No")];
    [a addButtonWithTitle:T("Yes")];
    run_alert(a, ^(NSModalResponse r) {
        if (r == NSAlertSecondButtonReturn && yes) yes();
    });
}

void ui_text_entry(const char *title, const char *initial, int maxChars, void (*done)(const char *text)) {
    (void)maxChars;
    NSAlert *a = [NSAlert new];
    a.messageText = @(title ? title : "");
    NSTextField *f = [NSTextField textFieldWithString:@(initial ? initial : "")];
    f.frame = NSMakeRect(0, 0, 228, 22);
    a.accessoryView = f;
    [a addButtonWithTitle:@"OK"];
    [a addButtonWithTitle:@"Cancel"];
    a.window.initialFirstResponder = f;
    run_alert(a, ^(NSModalResponse r) {
        if (done) done(r == NSAlertFirstButtonReturn ? f.stringValue.UTF8String : NULL);
    });
}

/* ================================================================== the name panel */

static int namePlayers;     /* nameInput0/1/2: the players still to ask */

static void name_next(void);

static NSImage *player_icon(int p) {
    NSString *path = [[NSBundle mainBundle] pathForResource:p == 1 ? @"player1icon" : @"player2icon" ofType:@"png"];
    NSImage *i = path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
    return i ? i : [NSApp applicationIconImage];
}

static void ask_name(int p, NSString *initial) {
    NSAlert *a = [NSAlert new];
    a.icon = player_icon(p);
    a.messageText = T("Highscores");
    a.informativeText = [NSString stringWithFormat:T("Congrats"), p];
    NSView *v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 46)];
    NSTextField *label = [NSTextField labelWithString:@"Your name:"];
    label.frame = NSMakeRect(0, 26, 260, 17);
    NSTextField *f = [NSTextField textFieldWithString:initial];
    f.frame = NSMakeRect(0, 0, 228, 22);
    f.placeholderString = @"Name";
    [v addSubview:label];
    [v addSubview:f];
    a.accessoryView = v;
    [a addButtonWithTitle:@"OK"];
    [a addButtonWithTitle:@"Cancel"];
    a.window.initialFirstResponder = f;
    run_alert(a, ^(NSModalResponse r) {
        if (r != NSAlertFirstButtonReturn) {
            name_next();
            return;
        }
        NSString *name = [f.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        if (!name.length) {
            /* scoreInputOk: "Please enter a valid name." and the panel again */
            NSAlert *e = [NSAlert new];
            e.messageText = T("Error");
            e.informativeText = T("EnterName");
            run_alert(e, ^(NSModalResponse r2) {
                (void)r2;
                ask_name(p, @"");
            });
            return;
        }
        /* at most 23 bytes, not cutting a UTF-8 character in two */
        const char *s = name.UTF8String;
        size_t n = strlen(s);
        if (n > sizeof cfg.lastEntry[0] - 1) {
            n = sizeof cfg.lastEntry[0] - 1;
            while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
        }
        memcpy(cfg.lastEntry[p - 1], s, n);
        cfg.lastEntry[p - 1][n] = 0;
        settings_save();
        eng_post(p == 1 ? EV_NEW_SCORE1 : EV_NEW_SCORE2, 0, 0);
        name_next();
    });
}

static void name_next(void) {
    int p;
    if (namePlayers & 1) p = 1;
    else if (namePlayers & 2) p = 2;
    else return;
    namePlayers &= ~(1 << (p - 1));
    dispatch_async(dispatch_get_main_queue(), ^{
        sheets--;
        ask_name(p, @(cfg.lastEntry[p - 1]));
    });
    sheets++;
}

void ui_update(void) {
    if (sheets) return;
    /* panels the game asks for */
    if (game.alertPending) {
        game.alertPending = 0;
        ui_alert(tr("NoTwoPlayer"));
    } else if (game.nameInputPending) {
        namePlayers = game.nameInputPending;
        game.nameInputPending = 0;
        name_next();
    }
}

/* ================================================================== level files */

static int count_levels_of(const char *file) {
    if (!file[0]) return 25;
    LevelSet s = {0};
    char p[400];
    snprintf(p, sizeof p, "%s/levels/%s", appDir, file);
    int n = levels_load_file(&s, p) ? s.n : 0;
    levels_free(&s);
    return n > 0 ? n : 1;
}

static void fill_level_popup(NSPopUpButton *pop) {
    char names[64][64];
    int n = levelfiles_list(names, 64);
    [pop removeAllItems];
    for (int i = 0; i < n; i++) {
        char d[64];
        levelfile_display(names[i], d, sizeof d);
        [pop addItemWithTitle:@(d)];
        pop.lastItem.representedObject = @(names[i]);
        if (!strcmp(names[i], cfg.levelFile)) [pop selectItem:pop.lastItem];
    }
}

/* ================================================================== the try level panel */

void ui_mac_show_try_panel(void) {
    if (sheets) return;
    if (game.mode == MODE_GAME && anim.running) eng_post(EV_PAUSE, 0, 0);
    int n = count_levels_of(cfg.levelFile);
    NSAlert *a = [NSAlert new];
    a.messageText = @"Try level";
    a.informativeText = [NSString stringWithFormat:T("TryLevel"), n];
    NSView *v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 290, 46)];
    NSTextField *ll = [NSTextField labelWithString:@"Level:"];
    ll.frame = NSMakeRect(0, 26, 80, 17);
    NSTextField *lf = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%d", cfg.tryLevel]];
    lf.frame = NSMakeRect(0, 0, 64, 22);
    NSTextField *pl = [NSTextField labelWithString:@"Player:"];
    pl.frame = NSMakeRect(93, 26, 80, 17);
    NSPopUpButton *pp = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(90, -2, 186, 26) pullsDown:NO];
    [pp addItemsWithTitles:@[@"Pac-Man", @"Ms. Pac-Man", @"Two players"]];
    [pp selectItemAtIndex:cfg.tryPlayer == 1 ? 0 : cfg.tryPlayer == 2 ? 1 : 2];
    for (NSView *s in @[ll, lf, pl, pp]) [v addSubview:s];
    a.accessoryView = v;
    [a addButtonWithTitle:@"OK"];
    [a addButtonWithTitle:@"Cancel"];
    a.window.initialFirstResponder = lf;
    run_alert(a, ^(NSModalResponse r) {
        if (r != NSAlertFirstButtonReturn) return;
        int lv = lf.intValue;
        int players = pp.indexOfSelectedItem == 0 ? 1 : pp.indexOfSelectedItem == 1 ? 2 : 0;
        if (lv < 1 || lv > n) {
            ui_alert([[NSString stringWithFormat:T("TryLevel"), n] UTF8String]);
            return;
        }
        if (players == 0 && cfg.difficulty == 4) {
            ui_alert(tr("NoTwoPlayer"));
            return;
        }
        cfg.tryLevel = lv;
        cfg.tryPlayer = players;
        settings_save();
        game_update_settings();
        eng_post(EV_TRY_LEVEL, 0, 0);
    });
}

static void clear_scores(void) { eng_post(EV_CLEAR_SCORES, 0, 0); }

void ui_mac_clear_scores(void) {
    if (sheets) return;
    ui_confirm(tr("ClearScoresTitle"), tr("ClearScoresText"), clear_scores);
}

/* ================================================================== the preferences window */

/* a key of the preferences: click it, then press the key */
@interface KeyBox : NSButton
@property (nonatomic) unsigned short code;
@property (nonatomic) BOOL listening;
@property (nonatomic, copy) BOOL (^inUse)(KeyBox *box, unsigned short code);
@end

@implementation KeyBox
- (instancetype)initWithFrame:(NSRect)r {
    if ((self = [super initWithFrame:r])) {
        self.bezelStyle = NSBezelStyleRounded;
        self.target = self;
        self.action = @selector(listen:);
    }
    return self;
}
- (void)setCode:(unsigned short)c {
    _code = c;
    self.title = in_mac_key_name(c);
}
- (BOOL)acceptsFirstResponder { return YES; }
- (void)listen:(id)sender {
    self.listening = YES;
    self.title = @"…";
    [self.window makeFirstResponder:self];
}
- (BOOL)resignFirstResponder {
    if (self.listening) {
        self.listening = NO;
        self.code = self.code;
    }
    return [super resignFirstResponder];
}
- (BOOL)performKeyEquivalent:(NSEvent *)e {
    if (self.listening && self.window.firstResponder == self && !(e.modifierFlags & NSEventModifierFlagCommand)) {
        [self keyDown:e];
        return YES;
    }
    return [super performKeyEquivalent:e];
}
- (void)keyDown:(NSEvent *)e {
    if (!self.listening) {
        [super keyDown:e];
        return;
    }
    unsigned short c = e.keyCode;
    self.listening = NO;
    /* Return, Enter, Space and Esc join, pause and leave the game */
    if (c == 36 || c == 76 || c == 49 || c == 53 || (self.inUse && self.inUse(self, c))) {
        NSBeep();
        NSAlert *a = [NSAlert new];
        a.messageText = T("Key");
        a.informativeText = T("Key already in use");
        [a beginSheetModalForWindow:self.window completionHandler:nil];
        self.code = self.code;
        return;
    }
    self.code = c;
}
@end

@interface Prefs : NSObject <NSWindowDelegate>
@property (strong) NSWindow *window;
@property (strong) NSPopUpButton *levelFile, *difficulty, *scale;
@property (strong) NSSlider *sound, *music;
@property (strong) NSMutableArray<KeyBox *> *keys;
@property float oldMusic, oldSound;
@end

static Prefs *prefs;

static NSTextField *label(NSView *v, NSString *s, NSRect r, NSTextAlignment al) {
    NSTextField *t = [NSTextField labelWithString:s];
    t.frame = r;
    t.alignment = al;
    [v addSubview:t];
    return t;
}

@implementation Prefs

- (instancetype)init {
    if (!(self = [super init])) return nil;
    NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 568, 294)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                backing:NSBackingStoreBuffered
                                                  defer:YES];
    w.title = @"Preferences";
    w.releasedWhenClosed = NO;
    w.delegate = self;
    NSView *v = w.contentView;
    self.window = w;

    label(v, @"Level file:", NSMakeRect(19, 259, 202, 17), NSTextAlignmentLeft);
    self.levelFile = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(17, 233, 204, 26) pullsDown:NO];
    [v addSubview:self.levelFile];

    label(v, @"Difficulty:", NSMakeRect(21, 209, 202, 17), NSTextAlignmentLeft);
    self.difficulty = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(18, 183, 204, 26) pullsDown:NO];
    [self.difficulty addItemsWithTitles:@[T("LevelEasy"), T("LevelNormal"), T("LevelHard"), T("LevelMaster")]];
    [v addSubview:self.difficulty];

    label(v, @"Sound volume:", NSMakeRect(21, 150, 160, 17), NSTextAlignmentLeft);
    self.sound = [NSSlider sliderWithValue:0 minValue:0 maxValue:1 target:self action:@selector(soundChanged:)];
    self.sound.frame = NSMakeRect(22, 116, 196, 26);
    [v addSubview:self.sound];

    label(v, @"Music volume:", NSMakeRect(21, 93, 160, 17), NSTextAlignmentLeft);
    self.music = [NSSlider sliderWithValue:0 minValue:0 maxValue:1 target:self action:@selector(musicChanged:)];
    self.music.frame = NSMakeRect(22, 59, 196, 26);
    [v addSubview:self.music];

    NSBox *sep = [[NSBox alloc] initWithFrame:NSMakeRect(239, 60, 5, 214)];
    sep.boxType = NSBoxSeparator;
    [v addSubview:sep];

    NSImageView *i1 = [NSImageView imageViewWithImage:player_icon(1)];
    i1.frame = NSMakeRect(370, 242, 34, 34);
    NSImageView *i2 = [NSImageView imageViewWithImage:player_icon(2)];
    i2.frame = NSMakeRect(486, 242, 34, 34);
    [v addSubview:i1];
    [v addSubview:i2];
    NSString *rows[4] = {@"Move left:", @"Move right:", @"Move up:", @"Move down:"};
    float ys[4] = {210, 179, 147, 116};
    self.keys = [NSMutableArray array];
    __weak Prefs *weak = self;
    for (int p = 0; p < 2; p++)
        for (int d = 0; d < 4; d++) {
            KeyBox *k = [[KeyBox alloc] initWithFrame:NSMakeRect(p ? 458 : 342, ys[d] - 2, 90, 30)];
            k.inUse = ^BOOL(KeyBox *box, unsigned short c) {
                for (KeyBox *o in weak.keys)
                    if (o != box && o.code == c) return YES;
                return NO;
            };
            [self.keys addObject:k];
            [v addSubview:k];
        }
    for (int d = 0; d < 4; d++) label(v, rows[d], NSMakeRect(250, ys[d] + 3, 86, 17), NSTextAlignmentRight);

    label(v, @"Scale mode:", NSMakeRect(263, 81, 160, 17), NSTextAlignmentLeft);
    self.scale = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(260, 55, 290, 26) pullsDown:NO];
    [self.scale addItemsWithTitles:@[@"Keep the proportions", @"Stretch to the window"]];
    [v addSubview:self.scale];

    NSButton *ok = [NSButton buttonWithTitle:@"OK" target:self action:@selector(ok:)];
    ok.frame = NSMakeRect(468, 12, 86, 32);
    ok.keyEquivalent = @"\r";
    NSButton *cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancel:)];
    cancel.frame = NSMakeRect(382, 12, 86, 32);
    cancel.keyEquivalent = @"\033";
    [v addSubview:ok];
    [v addSubview:cancel];
    return self;
}

- (void)load {
    fill_level_popup(self.levelFile);
    [self.difficulty selectItemAtIndex:(cfg.difficulty >= 1 && cfg.difficulty <= 4 ? cfg.difficulty : 2) - 1];
    self.sound.floatValue = self.oldSound = cfg.soundVol;
    self.music.floatValue = self.oldMusic = cfg.musicVol;
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    for (int i = 0; i < 8; i++) {
        id v = [ud objectForKey:kKeyDefaults[i]];
        self.keys[i].code = v ? (unsigned short)[v integerValue] : kKeyDefaultCodes[i];
    }
    [self.scale selectItemAtIndex:cfg.aspect == ASPECT_STRETCH ? 1 : 0];
}

- (void)soundChanged:(id)s {
    snd_set_volume(self.sound.floatValue);
    if (!snd_is_playing(SND_EAT_PELLET_B)) snd_play(SND_EAT_PELLET_B);
}

- (void)musicChanged:(id)s { mus_set_volume(self.music.floatValue); }

- (void)ok:(id)s {
    NSString *f = self.levelFile.selectedItem.representedObject;
    snprintf(cfg.levelFile, sizeof cfg.levelFile, "%s", f ? f.UTF8String : "");
    cfg.difficulty = (int)self.difficulty.indexOfSelectedItem + 1;
    cfg.soundVol = self.sound.floatValue;
    cfg.musicVol = self.music.floatValue;
    cfg.aspect = self.scale.indexOfSelectedItem == 1 ? ASPECT_STRETCH : ASPECT_AUTO;
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    for (int i = 0; i < 8; i++) [ud setInteger:self.keys[i].code forKey:kKeyDefaults[i]];
    if (cfg.tryLevel > count_levels_of(cfg.levelFile)) cfg.tryLevel = 1;
    in_mac_load_keys();
    gfx_set_aspect(cfg.aspect);
    mac_update_window_aspect();
    settings_save();
    game_update_settings();
    [self.window orderOut:nil];
    [gameWindow makeKeyAndOrderFront:nil];
}

- (void)cancel:(id)s {
    mus_set_volume(self.oldMusic);
    snd_set_volume(self.oldSound);
    [self.window orderOut:nil];
    [gameWindow makeKeyAndOrderFront:nil];
}

- (BOOL)windowShouldClose:(NSWindow *)w {
    [self cancel:nil];
    return NO;
}

@end

void ui_open_prefs(void) {
    if (!prefs) prefs = [Prefs new];
    if (!prefs.window.visible) {
        if (game.mode == MODE_GAME && anim.running) eng_post(EV_PAUSE, 0, 0);
        [prefs load];
        [prefs.window center];
    }
    [prefs.window makeKeyAndOrderFront:nil];
}

void ui_mac_refresh_level_files(void) {
    if (prefs && prefs.window.visible) fill_level_popup(prefs.levelFile);
}
