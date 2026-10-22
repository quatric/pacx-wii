#ifndef EDITOR_MAC_H
#define EDITOR_MAC_H
/* Pac the Man X Editor for the Mac: the maze view and the pictures it draws with */
#import <Cocoa/Cocoa.h>
#include "levels.h"

enum { T_PATH, T_ERASE, T_PELLET, T_PLAYER1, T_PLAYER2, T_CITADEL, T_COUNT };

/* the game's pictures, from the pack.bin of the game */
@interface PackArt : NSObject
+ (BOOL)openPack;
+ (CGImageRef)image:(NSString *)name;                               /* the whole picture */
+ (CGImageRef)frame:(int)i of:(NSString *)name w:(int)w h:(int)h;   /* fs_split: row by row */
+ (CGImageRef)tinted:(NSString *)name red:(int)r green:(int)g blue:(int)b;
+ (CGImageRef)tile:(int)i set:(NSString *)tileset red:(int)r green:(int)g blue:(int)b;
+ (NSImage *)icon:(NSString *)name;
+ (NSArray<NSString *> *)backgrounds;                               /* "back1" ... in natural order */
@end

@protocol MazeViewDelegate <NSObject>
- (Level *)currentLevel;
- (int)tool;
- (void)levelWillChange:(NSString *)actionName;    /* for undo */
- (void)levelDidChange;
@end

/* the 640x480 picture of the game, scaled to the view; edited with the mouse */
@interface MazeView : NSView
@property (weak) id<MazeViewDelegate> delegate;
@end

#endif
