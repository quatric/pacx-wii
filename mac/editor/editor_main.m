/*
 * Pac the Man X Editor (PacEditorDelegate of the Mac original, by Stone Design): edits the
 * level files of the levels folder the game reads, ~/Library/Application Support/Pac the
 * Man X/levels, or any other level file. "Launch Pac the Man X" plays the level being edited.
 */
#import "editor.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <string.h>
#include "levels.h"
#include "edit_rules.h"
#include "settings.h"
#include "pack.h"

#define GAME_ID @"com.quatric.pacx"
#define TRY_NOTIFICATION @"com.quatric.pacx.TryLevel"
#define LEVEL_PBOARD @"com.quatric.pacx.levels"

const char *platform_language(void) { return "en"; }

static NSString *const toolIcons[T_COUNT] = {@"ed_pathway", @"ed_erasure", @"ed_pellet", @"ed_player1", @"ed_player2",
                                             @"ed_citadel"};
static NSString *const toolNames[T_COUNT] = {@"Path", @"Erase", @"Super Pellet", @"Player 1", @"Player 2", @"Citadel"};
/* the help tags of the original */
static NSString *const toolHelp[T_COUNT] = {
    @"Select 'path' and click and drag to create new pathways for the pac-people. Drag off the edge for a path that wraps around.",
    @"Erase pathways (or think of it as fill in paths).",
    @"Add and move super pellets - up to 10. Click on a pellet to remove it, and click anywhere to place one.",
    @"Set the initial position of player one.",
    @"Set the initial position of player 2 - select this button, and click where you want it.",
    @"Move around the Citadel - the home of the ghosts!",
};
static NSString *const tilesetNames[2][2] = {{@"tile", @"Embossed tiles"}, {@"tile2", @"Classic tiles"}};
static NSString *const citadelNames[4][2] = {{@"citadel3", @"Classic Citadel"}, {@"citadel4", @"Aqua Citadel"},
                                             {@"citadel2", @"Glassy Citadel"}, {@"citadel", @"Embossed Citadel"}};
static const int waitTimes[] = {1, 3, 5, 10, 15};

static NSString *levelsDir(void) { return [@(appDir) stringByAppendingPathComponent:@"levels"]; }

@interface EditorController : NSObject <NSApplicationDelegate, NSWindowDelegate, MazeViewDelegate, NSMenuItemValidation>
@end

@implementation EditorController {
    NSWindow *window;
    MazeView *maze;
    NSPopUpButton *filesPopup, *levelPopup, *tilesetPopup, *citadelPopup, *backgroundPopup, *waitPopup, *tryPopup;
    NSColorWell *well;
    NSSegmentedControl *tools;
    NSTextField *hint;
    NSUndoManager *undo;
    BOOL colorUndoOpen;

    LevelSet set, standard;
    int cur, tool;
    NSString *fileName;     /* a file of the levels folder, @"" for the standard levels, nil when untitled */
    NSString *otherPath;    /* a level file somewhere else */
    BOOL dirty;
}

/* ================================================================== the level set */

- (Level *)currentLevel { return set.n ? &set.lv[cur] : NULL; }
- (int)tool { return tool; }

- (NSString *)path {
    if (otherPath) return otherPath;
    if (fileName.length) return [levelsDir() stringByAppendingPathComponent:fileName];
    return nil;
}

- (NSString *)displayName {
    if (otherPath) return otherPath.lastPathComponent.stringByDeletingPathExtension;
    if (!fileName) return @"Untitled";
    if (!fileName.length) return @"Standard Levels";
    return fileName.stringByDeletingPathExtension;
}

- (void)setDirty:(BOOL)d {
    dirty = d;
    window.documentEdited = d;
}

- (NSData *)snapshot {
    NSMutableData *d = [NSMutableData dataWithBytes:&cur length:sizeof cur];
    [d appendBytes:set.lv length:sizeof(Level) * set.n];
    return d;
}

- (void)restore:(NSData *)d {
    int c, n = (int)((d.length - sizeof c) / sizeof(Level));
    memcpy(&c, d.bytes, sizeof c);
    if (n > set.cap) {
        set.cap = n;
        set.lv = realloc(set.lv, sizeof(Level) * n);
    }
    memcpy(set.lv, (const char *)d.bytes + sizeof c, sizeof(Level) * n);
    set.n = n;
    cur = c < n ? c : n - 1;
}

- (void)registerUndo:(NSString *)name {
    NSData *s = [self snapshot];
    [undo registerUndoWithTarget:self handler:^(EditorController *t) {
        [t registerUndo:name];
        [t restore:s];
        [t setDirty:YES];
        [t refresh];
    }];
    [undo setActionName:name];
}

- (void)levelWillChange:(NSString *)name { [self registerUndo:name]; }

- (void)levelDidChange {
    [self setDirty:YES];
    [self refresh];
}

/* a change made with the controls of the window */
- (void)change:(NSString *)name with:(void (^)(Level *l))block {
    if (!set.n) return;
    [self registerUndo:name];
    block(&set.lv[cur]);
    [self levelDidChange];
}

/* ================================================================== the window */

static NSTextField *label(NSString *s) {
    NSTextField *t = [NSTextField labelWithString:s];
    t.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize weight:NSFontWeightMedium];
    t.textColor = NSColor.secondaryLabelColor;
    return t;
}

- (NSPopUpButton *)popup:(SEL)action {
    NSPopUpButton *p = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    p.target = self;
    p.action = action;
    return p;
}

- (NSButton *)button:(NSString *)title action:(SEL)action tip:(NSString *)tip {
    NSButton *b = [NSButton buttonWithTitle:title target:self action:action];
    b.toolTip = tip;
    return b;
}

- (void)buildWindow {
    NSRect r = NSMakeRect(0, 0, 1000, 640);
    window = [[NSWindow alloc] initWithContentRect:r
                                         styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    window.title = @"Edit Pac the Man X Levels";
    window.delegate = self;
    window.contentMinSize = NSMakeSize(760, 480);
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    NSView *content = window.contentView;
    const CGFloat side = 240, W = r.size.width, H = r.size.height;

    NSVisualEffectView *sidebar = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, side, H)];
    sidebar.material = NSVisualEffectMaterialSidebar;
    sidebar.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    sidebar.autoresizingMask = NSViewHeightSizable | NSViewMaxXMargin;
    [content addSubview:sidebar];

    filesPopup = [self popup:@selector(changeFiles:)];
    filesPopup.toolTip = @"The level file being edited. The game finds the files of the levels folder.";
    levelPopup = [self popup:@selector(changeLevel:)];
    levelPopup.toolTip = @"Select one of the levels to view and edit";
    NSButton *add = [self button:@"+" action:@selector(newLevel:) tip:@"New level"];
    NSButton *del = [self button:@"−" action:@selector(removeLevel:) tip:@"Remove the current level"];
    NSButton *dup = [self button:@"Duplicate" action:@selector(duplicateLevel:) tip:@"Duplicate current level"];
    NSStackView *levelButtons = [NSStackView stackViewWithViews:@[add, del, dup]];
    levelButtons.distribution = NSStackViewDistributionFillProportionally;
    NSButton *first = [self button:@"Move Level to #1" action:@selector(moveLevelToFirst:)
                               tip:@"Move current level to the first level - so when you test the game, you'll start this level"];

    well = [[NSColorWell alloc] initWithFrame:NSMakeRect(0, 0, 64, 26)];
    well.target = self;
    well.action = @selector(changeColor:);
    well.toolTip = @"Set the color of the mazes";
    tilesetPopup = [self popup:@selector(changeTileset:)];
    for (int i = 0; i < 2; i++) [tilesetPopup addItemWithTitle:tilesetNames[i][1]];
    citadelPopup = [self popup:@selector(changeCitadel:)];
    for (int i = 0; i < 4; i++) [citadelPopup addItemWithTitle:citadelNames[i][1]];
    backgroundPopup = [self popup:@selector(changeBackground:)];
    backgroundPopup.toolTip = @"Select a background for this level";
    for (NSString *b in [PackArt backgrounds]) {
        [backgroundPopup addItemWithTitle:b];
        CGImageRef img = [PackArt image:[@"bg/" stringByAppendingString:b]];
        if (img) {
            NSImage *full = [[NSImage alloc] initWithCGImage:img size:NSZeroSize];
            NSImage *thumb = [NSImage imageWithSize:NSMakeSize(32, 24) flipped:NO drawingHandler:^BOOL(NSRect rr) {
                [full drawInRect:rr];
                return YES;
            }];
            backgroundPopup.lastItem.image = thumb;
        }
    }
    waitPopup = [self popup:@selector(changeWaitTime:)];
    waitPopup.toolTip = @"Wait time: The time period (in seconds) after which a ghost starts hunting and how long "
                        @"ghosts are blue after eating a super pellet.";

    tryPopup = [self popup:@selector(changeTryPlayers:)];
    [tryPopup addItemsWithTitles:@[@"Pac-Man", @"Ms. Pac-Man", @"Two players"]];
    NSInteger tp = [[NSUserDefaults standardUserDefaults] integerForKey:@"TryPlayers"];
    [tryPopup selectItemAtIndex:tp == 2 ? 1 : tp == 0 && [[NSUserDefaults standardUserDefaults] objectForKey:@"TryPlayers"] ? 2 : 0];
    NSButton *launch = [self button:@"Launch Pac the Man X" action:@selector(launchGame:)
                                tip:@"Play the current level in Pac the Man X"];
    launch.bezelColor = NSColor.controlAccentColor;
    launch.keyEquivalent = @"\r";

    NSArray *views = @[label(@"LEVELS FILE"), filesPopup, label(@"LEVEL"), levelPopup, levelButtons, first,
                       label(@"MAZE COLOR"), well, label(@"TILES"), tilesetPopup, label(@"CITADEL"), citadelPopup,
                       label(@"BACKGROUND"), backgroundPopup, label(@"WAIT TIME"), waitPopup,
                       label(@"TRY WITH"), tryPopup, launch];
    NSStackView *stack = [NSStackView stackViewWithViews:views];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 6;
    for (NSView *v in views) {
        if ([v isKindOfClass:[NSTextField class]]) [stack setCustomSpacing:3 afterView:v];
        else if (v != launch) [stack setCustomSpacing:14 afterView:v];
        if (v != well) [v.widthAnchor constraintEqualToConstant:side - 32].active = YES;
    }
    [stack setCustomSpacing:6 afterView:levelButtons];
    [well.widthAnchor constraintEqualToConstant:64].active = YES;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [sidebar addSubview:stack];
    [stack.topAnchor constraintEqualToAnchor:sidebar.topAnchor constant:16].active = YES;
    [stack.leadingAnchor constraintEqualToAnchor:sidebar.leadingAnchor constant:16].active = YES;

    /* the tools, the maze and the help text */
    tools = [NSSegmentedControl segmentedControlWithImages:@[] trackingMode:NSSegmentSwitchTrackingSelectOne
                                                    target:self action:@selector(changeTool:)];
    tools.segmentCount = T_COUNT;
    for (int t = 0; t < T_COUNT; t++) {
        NSImage *icon = [PackArt icon:toolIcons[t]];
        if (icon) {
            CGFloat s = 22 / fmax(icon.size.width, icon.size.height);
            icon.size = NSMakeSize(icon.size.width * s, icon.size.height * s);
            [tools setImage:icon forSegment:t];
        } else {
            [tools setLabel:toolNames[t] forSegment:t];
        }
        [tools setToolTip:[NSString stringWithFormat:@"%@ (⌘%d)", toolNames[t], t + 1] forSegment:t];
        [tools setWidth:44 forSegment:t];
    }
    tools.selectedSegment = 0;
    tools.segmentStyle = NSSegmentStyleTexturedRounded;
    [tools sizeToFit];
    tools.frame = NSMakeRect(side + 16, H - 44, tools.frame.size.width, 32);
    tools.autoresizingMask = NSViewMinYMargin | NSViewMaxXMargin;
    [content addSubview:tools];

    maze = [[MazeView alloc] initWithFrame:NSMakeRect(side, 34, W - side, H - 34 - 52)];
    maze.delegate = self;
    maze.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [content addSubview:maze];

    hint = [NSTextField labelWithString:@""];
    hint.frame = NSMakeRect(side + 16, 8, W - side - 32, 18);
    hint.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    hint.textColor = NSColor.secondaryLabelColor;
    hint.lineBreakMode = NSLineBreakByTruncatingTail;
    [content addSubview:hint];

    [window setFrameUsingName:@"EditorWindow"];
    window.frameAutosaveName = @"EditorWindow";
}

/* everything shown from the level set */
- (void)refresh {
    Level *l = [self currentLevel];
    window.title = [NSString stringWithFormat:@"%@ — Level %d of %d", [self displayName], cur + 1, set.n];
    if (self.path) window.representedURL = [NSURL fileURLWithPath:self.path];
    else window.representedURL = nil;

    [levelPopup removeAllItems];
    for (int i = 0; i < set.n; i++) [levelPopup addItemWithTitle:[NSString stringWithFormat:@"Level %d", i + 1]];
    [levelPopup selectItemAtIndex:cur];

    if (l) {
        well.color = [NSColor colorWithSRGBRed:l->red / 255.0 green:l->green / 255.0 blue:l->blue / 255.0 alpha:1];
        [tilesetPopup selectItemAtIndex:strcmp(l->tileset, "tile2") ? 0 : 1];
        int ci = 3;
        for (int i = 0; i < 4; i++)
            if (citadel_number(l->citadel) == citadel_number(citadelNames[i][0].UTF8String)) ci = i;
        [citadelPopup selectItemAtIndex:ci];
        NSInteger bi = [backgroundPopup indexOfItemWithTitle:@(l->background)];
        if (bi < 0) {
            [backgroundPopup addItemWithTitle:@(l->background)];
            bi = backgroundPopup.numberOfItems - 1;
        }
        [backgroundPopup selectItemAtIndex:bi];
        [waitPopup removeAllItems];
        BOOL found = NO;
        for (unsigned i = 0; i < sizeof waitTimes / sizeof waitTimes[0]; i++) {
            if (!found && l->wait < waitTimes[i]) {
                [waitPopup addItemWithTitle:[NSString stringWithFormat:@"%d seconds", l->wait]];
                waitPopup.lastItem.tag = l->wait;
                found = YES;
            }
            [waitPopup addItemWithTitle:[NSString stringWithFormat:@"%d second%s", waitTimes[i], waitTimes[i] == 1 ? "" : "s"]];
            waitPopup.lastItem.tag = waitTimes[i];
            if (waitTimes[i] == l->wait) found = YES;
        }
        if (!found) {
            [waitPopup addItemWithTitle:[NSString stringWithFormat:@"%d seconds", l->wait]];
            waitPopup.lastItem.tag = l->wait;
        }
        [waitPopup selectItemWithTag:l->wait];
    }
    [self refreshFiles];
    tools.selectedSegment = tool;
    hint.stringValue = toolHelp[tool];
    maze.needsDisplay = YES;
}

- (void)refreshFiles {
    [filesPopup removeAllItems];
    char names[128][64];
    int n = levelfiles_list(names, 128);
    for (int i = 0; i < n; i++) {
        char d[64];
        levelfile_display(names[i], d, sizeof d);
        [filesPopup addItemWithTitle:@(d)];
        filesPopup.lastItem.representedObject = @(names[i]);
        if (!otherPath && fileName && [fileName isEqualToString:@(names[i])]) [filesPopup selectItem:filesPopup.lastItem];
    }
    if (otherPath || !fileName) {
        [filesPopup.menu addItem:[NSMenuItem separatorItem]];
        [filesPopup addItemWithTitle:[self displayName]];
        [filesPopup selectItem:filesPopup.lastItem];
    }
    [filesPopup.menu addItem:[NSMenuItem separatorItem]];
    [filesPopup addItemWithTitle:@"Open Other Levels…"];
    filesPopup.lastItem.action = @selector(openAnyLevels:);
    filesPopup.lastItem.target = self;
}

/* ================================================================== files */

/* the changes are saved, or may be lost: returns NO to cancel */
- (BOOL)confirmDiscard {
    if (!dirty) return YES;
    NSAlert *a = [NSAlert new];
    a.messageText = [NSString stringWithFormat:@"Do you want to save the changes made to “%@”?", [self displayName]];
    a.informativeText = @"Your changes will be lost if you don't save them.";
    [a addButtonWithTitle:@"Save"];
    [a addButtonWithTitle:@"Cancel"];
    [a addButtonWithTitle:@"Don't Save"];
    NSModalResponse r = [a runModal];
    if (r == NSAlertFirstButtonReturn) return [self save];
    return r == NSAlertThirdButtonReturn;
}

- (void)useSet:(LevelSet)s name:(NSString *)name other:(NSString *)other {
    levels_free(&set);
    set = s;
    fileName = name;
    otherPath = other;
    cur = 0;
    [undo removeAllActions];
    [self setDirty:NO];
    [self refresh];
}

- (BOOL)openPath:(NSString *)path {
    LevelSet s = {0};
    if (!levels_load_file(&s, path.fileSystemRepresentation) || s.n < 1) {
        levels_free(&s);
        NSAlert *a = [NSAlert new];
        a.messageText = @"This level file cannot be read.";
        a.informativeText = path.lastPathComponent;
        [a runModal];
        return NO;
    }
    NSString *dir = path.stringByDeletingLastPathComponent.stringByStandardizingPath;
    if ([dir isEqualToString:levelsDir().stringByStandardizingPath])
        [self useSet:s name:path.lastPathComponent other:nil];
    else
        [self useSet:s name:nil other:path];
    return YES;
}

- (void)openStandard {
    LevelSet s = {0};
    levels_copy(&s, &standard);
    [self useSet:s name:@"" other:nil];
}

- (BOOL)writeTo:(NSString *)path {
    if (!levels_save_file(&set, path.fileSystemRepresentation)) {
        NSAlert *a = [NSAlert new];
        a.messageText = @"The level file cannot be saved.";
        a.informativeText = path;
        [a runModal];
        return NO;
    }
    return YES;
}

- (BOOL)save {
    NSString *p = self.path;
    if (!p) return [self saveAs];   /* the standard levels stay as they are */
    if (![self writeTo:p]) return NO;
    [self setDirty:NO];
    [self refresh];
    return YES;
}

- (BOOL)saveAs {
    NSSavePanel *sp = [NSSavePanel savePanel];
    sp.allowedContentTypes = @[UTTypePropertyList];
    sp.directoryURL = [NSURL fileURLWithPath:otherPath ? otherPath.stringByDeletingLastPathComponent : levelsDir()];
    sp.nameFieldStringValue = fileName.length || otherPath ? [self displayName] : @"My Levels";
    sp.message = @"Level files in the levels folder can be chosen in the preferences of Pac the Man X.";
    if ([sp runModal] != NSModalResponseOK) return NO;
    NSString *p = sp.URL.path;
    if (![self writeTo:p]) return NO;
    NSString *dir = p.stringByDeletingLastPathComponent.stringByStandardizingPath;
    if ([dir isEqualToString:levelsDir().stringByStandardizingPath]) {
        fileName = p.lastPathComponent;
        otherPath = nil;
    } else {
        fileName = nil;
        otherPath = p;
    }
    [self setDirty:NO];
    [self refresh];
    return YES;
}

- (void)openPanelIn:(NSString *)dir {
    if (![self confirmDiscard]) return;
    NSOpenPanel *op = [NSOpenPanel openPanel];
    op.allowedContentTypes = @[UTTypePropertyList];
    if (dir) op.directoryURL = [NSURL fileURLWithPath:dir];
    if ([op runModal] == NSModalResponseOK) [self openPath:op.URL.path];
    else [self refresh];
}

- (void)newFile:(id)s {
    if (![self confirmDiscard]) return;
    /* starts from the first standard level, so it can be tried at once */
    LevelSet n = {0};
    Level *l = levels_insert(&n, 0);
    if (standard.n) *l = standard.lv[0];
    else ed_blank_level(l);
    [self useSet:n name:nil other:nil];
    [self setDirty:YES];
}

- (void)openLevels:(id)s { [self openPanelIn:levelsDir()]; }
- (void)openAnyLevels:(id)s { [self openPanelIn:nil]; }
- (void)saveLevels:(id)s { [self save]; }
- (void)saveLevelsAs:(id)s { [self saveAs]; }
- (void)showLevelsFolder:(id)s { [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:levelsDir() isDirectory:YES]]; }

- (void)changeFiles:(id)s {
    NSString *name = filesPopup.selectedItem.representedObject;
    if (!name) return;
    if (!otherPath && [name isEqualToString:fileName ?: @"\x01"]) return;
    if (![self confirmDiscard]) {
        [self refresh];
        return;
    }
    if (!name.length) [self openStandard];
    else [self openPath:[levelsDir() stringByAppendingPathComponent:name]];
}

- (void)removeFile:(id)s {
    NSString *p = self.path;
    if (!p) return;
    NSAlert *a = [NSAlert new];
    a.messageText = [NSString stringWithFormat:@"Move the level file “%@” to the Trash?", [self displayName]];
    [a addButtonWithTitle:@"Move to Trash"];
    [a addButtonWithTitle:@"Cancel"];
    if ([a runModal] != NSAlertFirstButtonReturn) return;
    NSError *err = nil;
    if (![[NSFileManager defaultManager] trashItemAtURL:[NSURL fileURLWithPath:p] resultingItemURL:nil error:&err]) {
        [[NSAlert alertWithError:err] runModal];
        return;
    }
    [self setDirty:NO];
    [self openStandard];
}

/* ================================================================== levels */

- (void)changeLevel:(id)s {
    cur = (int)levelPopup.indexOfSelectedItem;
    [self refresh];
}

- (void)previousLevel:(id)s {
    if (cur > 0) cur--;
    [self refresh];
}

- (void)nextLevel:(id)s {
    if (cur < set.n - 1) cur++;
    [self refresh];
}

- (void)newLevel:(id)s {
    [self registerUndo:@"New Level"];
    Level copy = set.lv[cur];
    Level *l = levels_insert(&set, cur + 1);
    *l = copy;
    ed_blank_level(l);
    cur++;
    [self levelDidChange];
}

- (void)duplicateLevel:(id)s {
    [self registerUndo:@"Duplicate Level"];
    Level copy = set.lv[cur];
    *levels_insert(&set, cur + 1) = copy;
    cur++;
    [self levelDidChange];
}

- (void)removeLevel:(id)s {
    if (set.n <= 1) return;
    [self registerUndo:@"Remove Level"];
    levels_remove(&set, cur);
    if (cur >= set.n) cur = set.n - 1;
    [self levelDidChange];
}

- (void)moveLevel:(int)to {
    if (to < 0 || to >= set.n || to == cur) return;
    [self registerUndo:@"Move Level"];
    Level copy = set.lv[cur];
    levels_remove(&set, cur);
    *levels_insert(&set, to) = copy;
    cur = to;
    [self levelDidChange];
}

- (void)moveLevelToFirst:(id)s { [self moveLevel:0]; }

- (void)moveLevelToAny:(id)s {
    NSAlert *a = [NSAlert new];
    a.messageText = [NSString stringWithFormat:@"Move level %d to:", cur + 1];
    a.informativeText = [NSString stringWithFormat:@"A position between 1 and %d.", set.n];
    NSTextField *f = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%d", cur + 1]];
    f.frame = NSMakeRect(0, 0, 80, 22);
    a.accessoryView = f;
    [a addButtonWithTitle:@"Move"];
    [a addButtonWithTitle:@"Cancel"];
    a.window.initialFirstResponder = f;
    [a beginSheetModalForWindow:window completionHandler:^(NSModalResponse r) {
        if (r == NSAlertFirstButtonReturn) [self moveLevel:f.intValue - 1];
    }];
}

/* copy and paste whole levels, as level file text */
- (void)copy:(id)s {
    LevelSet one = {&set.lv[cur], 1, 1};
    int len;
    char *x = levels_write(&one, &len);
    NSString *text = [[NSString alloc] initWithBytes:x length:len encoding:NSUTF8StringEncoding];
    free(x);
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:text forType:LEVEL_PBOARD];
    [pb setString:text forType:NSPasteboardTypeString];
}

- (void)cut:(id)s {
    if (set.n <= 1) return;
    [self copy:s];
    [self registerUndo:@"Cut Level"];
    levels_remove(&set, cur);
    if (cur >= set.n) cur = set.n - 1;
    [self levelDidChange];
}

- (void)paste:(id)s {
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    NSString *text = [pb stringForType:LEVEL_PBOARD] ?: [pb stringForType:NSPasteboardTypeString];
    LevelSet in = {0};
    NSData *d = [text dataUsingEncoding:NSUTF8StringEncoding];
    if (!d || !levels_parse(&in, d.bytes, (int)d.length) || in.n < 1) {
        levels_free(&in);
        NSBeep();
        return;
    }
    [self registerUndo:in.n > 1 ? @"Paste Levels" : @"Paste Level"];
    for (int i = 0; i < in.n; i++) *levels_insert(&set, cur + 1 + i) = in.lv[i];
    cur++;
    levels_free(&in);
    [self levelDidChange];
}

/* ================================================================== the controls */

- (void)changeTool:(id)s {
    tool = (int)tools.selectedSegment;
    [self refresh];
}

- (void)selectTool:(NSMenuItem *)m {
    tool = (int)m.tag;
    [self refresh];
}

- (void)changeColor:(id)s {
    NSColor *c = [well.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!c || !set.n) return;
    /* the dragging of the colour well makes one undo step */
    if (!colorUndoOpen) {
        [self registerUndo:@"Change Maze Color"];
        colorUndoOpen = YES;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2), dispatch_get_main_queue(), ^{
            self->colorUndoOpen = NO;
        });
    }
    Level *l = &set.lv[cur];
    l->red = (int)lround(c.redComponent * 255);
    l->green = (int)lround(c.greenComponent * 255);
    l->blue = (int)lround(c.blueComponent * 255);
    [self setDirty:YES];
    maze.needsDisplay = YES;
}

- (void)changeTileset:(id)s {
    NSString *t = tilesetNames[tilesetPopup.indexOfSelectedItem][0];
    [self change:@"Change Tiles" with:^(Level *l) { snprintf(l->tileset, sizeof l->tileset, "%s", t.UTF8String); }];
}

- (void)changeCitadel:(id)s {
    NSString *t = citadelNames[citadelPopup.indexOfSelectedItem][0];
    [self change:@"Change Citadel" with:^(Level *l) { snprintf(l->citadel, sizeof l->citadel, "%s", t.UTF8String); }];
}

- (void)changeBackground:(id)s {
    NSString *t = backgroundPopup.titleOfSelectedItem;
    [self change:@"Change Background" with:^(Level *l) {
        snprintf(l->background, sizeof l->background, "%s", t.UTF8String);
    }];
}

- (void)changeWaitTime:(id)s {
    int w = (int)waitPopup.selectedTag;
    [self change:@"Change Wait Time" with:^(Level *l) { l->wait = w; }];
}

- (int)tryPlayers {
    NSInteger i = tryPopup.indexOfSelectedItem;
    return i == 0 ? 1 : i == 1 ? 2 : 0;
}

- (void)changeTryPlayers:(id)s {
    [[NSUserDefaults standardUserDefaults] setInteger:[self tryPlayers] forKey:@"TryPlayers"];
}

/* ================================================================== trying the level */

- (NSURL *)gameURL {
    /* the game this editor came with, else the one Launch Services knows */
    NSString *around = [[[NSBundle mainBundle].bundlePath stringByDeletingLastPathComponent]
        stringByDeletingLastPathComponent].stringByDeletingLastPathComponent;
    if ([[NSBundle bundleWithPath:around].bundleIdentifier isEqualToString:GAME_ID]) return [NSURL fileURLWithPath:around];
    return [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:GAME_ID];
}

- (void)launchGame:(id)s {
    NSString *tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:@"Pac the Man X Try.plist"];
    if (![self writeTo:tmp]) return;
    int players = [self tryPlayers];
    NSRunningApplication *game = [NSRunningApplication runningApplicationsWithBundleIdentifier:GAME_ID].firstObject;
    if (game) {
        [[NSDistributedNotificationCenter defaultCenter] postNotificationName:TRY_NOTIFICATION
                                                                       object:nil
                                                                     userInfo:@{@"File": tmp, @"Level": @(cur),
                                                                                @"Players": @(players)}
                                                           deliverImmediately:YES];
        if (@available(macOS 14.0, *)) [game activateWithOptions:0];
        else [game activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        return;
    }
    NSURL *url = [self gameURL];
    if (!url) {
        NSAlert *a = [NSAlert new];
        a.messageText = @"Pac the Man X cannot be found.";
        [a runModal];
        return;
    }
    NSWorkspaceOpenConfiguration *c = [NSWorkspaceOpenConfiguration configuration];
    c.arguments = @[@"-TryFile", tmp, @"-TryLevel", [NSString stringWithFormat:@"%d", cur],
                    @"-TryPlayers", [NSString stringWithFormat:@"%d", players]];
    [[NSWorkspace sharedWorkspace] openApplicationAtURL:url configuration:c completionHandler:nil];
}

/* ================================================================== menus */

static NSMenuItem *item(NSMenu *m, NSString *title, SEL action, NSString *key, NSEventModifierFlags mods, id target) {
    NSMenuItem *i = [m addItemWithTitle:title action:action keyEquivalent:key ? key : @""];
    if (key) i.keyEquivalentModifierMask = mods ? mods : NSEventModifierFlagCommand;
    i.target = target;
    return i;
}

- (void)buildMenus {
    const NSEventModifierFlags cmd = NSEventModifierFlagCommand, shift = cmd | NSEventModifierFlagShift;
    NSString *name = @"Pac the Man X Editor";
    NSMenu *bar = [NSMenu new];

    NSMenu *app = [[NSMenu alloc] initWithTitle:name];
    item(app, [@"About " stringByAppendingString:name], @selector(orderFrontStandardAboutPanel:), nil, 0, nil);
    [app addItem:[NSMenuItem separatorItem]];
    NSMenu *services = [NSMenu new];
    [app addItemWithTitle:@"Services" action:nil keyEquivalent:@""].submenu = services;
    NSApp.servicesMenu = services;
    [app addItem:[NSMenuItem separatorItem]];
    item(app, [@"Hide " stringByAppendingString:name], @selector(hide:), @"h", cmd, nil);
    item(app, @"Hide Others", @selector(hideOtherApplications:), @"h", cmd | NSEventModifierFlagOption, nil);
    item(app, @"Show All", @selector(unhideAllApplications:), nil, 0, nil);
    [app addItem:[NSMenuItem separatorItem]];
    item(app, [@"Quit " stringByAppendingString:name], @selector(terminate:), @"q", cmd, nil);
    [bar addItemWithTitle:@"" action:nil keyEquivalent:@""].submenu = app;

    NSMenu *file = [[NSMenu alloc] initWithTitle:@"File"];
    item(file, @"New Levels File", @selector(newFile:), @"n", shift, self);
    item(file, @"Open Levels…", @selector(openLevels:), @"o", cmd, self);
    item(file, @"Open Other Levels…", @selector(openAnyLevels:), @"o", shift, self);
    [file addItem:[NSMenuItem separatorItem]];
    item(file, @"Close", @selector(performClose:), @"w", cmd, nil);
    item(file, @"Save Levels", @selector(saveLevels:), @"s", cmd, self);
    item(file, @"Save Levels As…", @selector(saveLevelsAs:), @"s", shift, self);
    [file addItem:[NSMenuItem separatorItem]];
    item(file, @"Remove Levels File…", @selector(removeFile:), @"r", shift, self);
    item(file, @"Show Levels Folder in Finder", @selector(showLevelsFolder:), nil, 0, self);
    [file addItem:[NSMenuItem separatorItem]];
    item(file, @"Launch Pac the Man X", @selector(launchGame:), @"l", cmd, self);
    [bar addItemWithTitle:@"File" action:nil keyEquivalent:@""].submenu = file;

    NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    item(edit, @"Undo", @selector(undo:), @"z", cmd, nil);
    item(edit, @"Redo", @selector(redo:), @"z", shift, nil);
    [edit addItem:[NSMenuItem separatorItem]];
    item(edit, @"Cut Level", @selector(cut:), @"x", cmd, nil);
    item(edit, @"Copy Level", @selector(copy:), @"c", cmd, nil);
    item(edit, @"Paste Level", @selector(paste:), @"v", cmd, nil);
    [bar addItemWithTitle:@"Edit" action:nil keyEquivalent:@""].submenu = edit;

    NSMenu *lv = [[NSMenu alloc] initWithTitle:@"Levels"];
    item(lv, @"New Level", @selector(newLevel:), @"n", cmd, self);
    item(lv, @"Duplicate Level", @selector(duplicateLevel:), @"d", cmd, self);
    item(lv, @"Remove Level", @selector(removeLevel:), [NSString stringWithFormat:@"%C", (unichar)NSBackspaceCharacter], cmd, self);
    [lv addItem:[NSMenuItem separatorItem]];
    item(lv, @"Move Level to First", @selector(moveLevelToFirst:), nil, 0, self);
    item(lv, @"Move Level to…", @selector(moveLevelToAny:), @"m", shift, self);
    [lv addItem:[NSMenuItem separatorItem]];
    item(lv, @"Previous Level", @selector(previousLevel:), @"[", cmd, self);
    item(lv, @"Next Level", @selector(nextLevel:), @"]", cmd, self);
    [lv addItem:[NSMenuItem separatorItem]];
    for (int t = 0; t < T_COUNT; t++) {
        NSMenuItem *i = item(lv, toolNames[t], @selector(selectTool:), [NSString stringWithFormat:@"%d", t + 1], cmd, self);
        i.tag = t;
    }
    [bar addItemWithTitle:@"Levels" action:nil keyEquivalent:@""].submenu = lv;

    NSMenu *w = [[NSMenu alloc] initWithTitle:@"Window"];
    item(w, @"Minimize", @selector(performMiniaturize:), @"m", cmd, nil);
    item(w, @"Zoom", @selector(performZoom:), nil, 0, nil);
    item(w, @"Colors", @selector(orderFrontColorPanel:), @"c", shift, nil);
    [w addItem:[NSMenuItem separatorItem]];
    item(w, @"Bring All to Front", @selector(arrangeInFront:), nil, 0, nil);
    [bar addItemWithTitle:@"Window" action:nil keyEquivalent:@""].submenu = w;
    NSApp.windowsMenu = w;

    NSMenu *h = [[NSMenu alloc] initWithTitle:@"Help"];
    item(h, @"Pac the Man X Editor Help", @selector(showHelp:), @"?", cmd, self);
    [bar addItemWithTitle:@"Help" action:nil keyEquivalent:@""].submenu = h;
    NSApp.helpMenu = h;
    NSApp.mainMenu = bar;
}

- (void)showHelp:(id)s {
    NSAlert *a = [NSAlert new];
    a.messageText = @"Pac the Man X Editor";
    a.informativeText =
        @"Choose an editing tool, then click and drag in the Pac the Man area.\n\n"
        @"Path: drag along a row or a column to lay a path. Drag off the edge of the maze for a path that wraps "
        @"around to the other side.\n"
        @"Erase: click or drag to remove paths.\n"
        @"Super Pellet: click to place or remove one (up to 10).\n"
        @"Player 1 and Player 2: click where each player starts.\n"
        @"Citadel: click to move the ghosts' home.\n\n"
        @"Hold Shift or Option to bypass the editor's checks.\n\n"
        @"Level files are kept in the levels folder of Pac the Man X, where the game finds them: choose one in "
        @"the game's preferences. Launch Pac the Man X (⌘L) plays the current level.";
    [a beginSheetModalForWindow:window completionHandler:nil];
}

- (void)undo:(id)s { [undo undo]; }
- (void)redo:(id)s { [undo redo]; }

- (BOOL)validateMenuItem:(NSMenuItem *)m {
    SEL a = m.action;
    if (a == @selector(undo:)) {
        m.title = undo.undoMenuItemTitle;
        return undo.canUndo;
    }
    if (a == @selector(redo:)) {
        m.title = undo.redoMenuItemTitle;
        return undo.canRedo;
    }
    if (a == @selector(removeLevel:) || a == @selector(cut:)) return set.n > 1;
    if (a == @selector(moveLevelToFirst:)) return cur > 0;
    if (a == @selector(moveLevelToAny:)) return set.n > 1;
    if (a == @selector(previousLevel:)) return cur > 0;
    if (a == @selector(nextLevel:)) return cur < set.n - 1;
    if (a == @selector(removeFile:)) return self.path != nil;
    if (a == @selector(paste:)) {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        return [pb availableTypeFromArray:@[LEVEL_PBOARD, NSPasteboardTypeString]] != nil;
    }
    if (a == @selector(selectTool:)) m.state = m.tag == tool ? NSControlStateValueOn : NSControlStateValueOff;
    return YES;
}

/* ================================================================== the application */

- (NSUndoManager *)windowWillReturnUndoManager:(NSWindow *)w { return undo; }

- (BOOL)windowShouldClose:(NSWindow *)w { return [self confirmDiscard]; }

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)a {
    return window.visible && ![self confirmDiscard] ? NSTerminateCancel : NSTerminateNow;
}

- (BOOL)application:(NSApplication *)a openFile:(NSString *)path {
    if (!window) {
        /* before the launch is finished: opened instead of the game's level file */
        otherPath = path;
        return YES;
    }
    if (![self confirmDiscard]) return NO;
    return [self openPath:path];
}

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    if (![PackArt openPack]) {
        NSAlert *a = [NSAlert new];
        a.messageText = @"The game data is missing.";
        a.informativeText = @"The editor uses the pictures of Pac the Man X: keep it inside the game, or install the game.";
        [a runModal];
        [NSApp terminate:nil];
        return;
    }
    storage_init(NULL);
    settings_load();
    const PackEntry *std = pack_find("levels/Standard levels");
    if (std) levels_parse(&standard, pack_data(std), (int)std->size);
    undo = [NSUndoManager new];

    [self buildMenus];
    [self buildWindow];

    /* the level file the game uses, at the level tried last */
    NSString *requested = otherPath;
    otherPath = nil;
    BOOL opened = NO;
    if (requested) opened = [self openPath:requested];
    else if (cfg.levelFile[0]) opened = [self openPath:[levelsDir() stringByAppendingPathComponent:@(cfg.levelFile)]];
    if (!opened) [self openStandard];
    if (!requested && cfg.tryLevel >= 1 && cfg.tryLevel <= set.n) cur = cfg.tryLevel - 1;
    [self refresh];
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:maze];
    if (@available(macOS 14.0, *)) [NSApp activate];
    else [NSApp activateIgnoringOtherApps:YES];
}

@end

int main(int argc, const char **argv) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        EditorController *c = [EditorController new];
        app.delegate = c;
        [app run];
    }
    return 0;
}
