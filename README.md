# Pac the Man X for the Wii, Wii U, Switch, 3DS, Mac, Linux and Windows

A port of **Pac the Man X 1.5.1a**, the freeware Mac maze game by McSebi Software, to the
Nintendo Wii (Homebrew Channel), together with its level editor, and from there to the Wii U,
the Switch, the 3DS and back to today's Macs (Apple silicon and Intel, macOS 11 or newer) with
a native level editor.

The game logic is rebuilt from the original program, so it plays the same as the original:
the same mazes, speeds, timers, scores, bonus items, demo mode, high score tables and
screens, with the original graphics, sounds and music. The Wii and the Mac share that code;
only the drawing, sound, input and windows are their own (`source/` for the Wii, `mac/` for
the Mac).

## What you need for the Wii

This repository holds no game data. The pictures, sounds, music and levels are converted
from your own copy of the game, which you can still download as freeware.

- devkitPro with devkitPPC and libogc, plus the portlibs `ppc-libvorbisidec`, `ppc-libpng`
  and `ppc-zlib`
- Python 3 with Pillow
- Pac the Man X 1.5.1a for the Mac (`Pac the Man X.app`)

## Building for the Wii

```bash
git clone --recursive https://github.com/quatric/pacx-wii.git
cd pacx-wii
python3 -I tools/convert_assets.py "/path/to/Pac the Man X.app/Contents/Resources"
make dist
```

`convert_assets.py` writes `data/pack.bin` (built into the program) and fills `apps/pacx`
with the music, the icon and the extra level file. `make dist` builds the program, compressed
with dolxz, and puts it in `apps/pacx/boot.dol`. Copy the `apps/pacx` folder to `apps/` on
your SD card. `make NO_DOLXZ=1` skips the compression.

## Controls on the Wii

| | Player 1 | Player 2 |
|---|---|---|
| Wii Remote held sideways | port 1 D-pad | port 2 D-pad |
| Nunchuk / Classic Controller | stick or D-pad | stick or D-pad |
| GameCube controller | port 1 | port 2 |
| USB keyboard | arrow keys | W A S D |

- **Join a game**: the other player presses A, 1 or 2 (Return on a keyboard).
- **Pause**: + or START (Space).
- **Back to the title**: − or HOME (Esc).
- **Title screen**: point and press A, or use the D-pad and A. HOME opens the preferences.

You enter high score names with the Wii keyboard.

### Preferences

Difficulty, music and sound volume, and the level file to play. The screen setting chooses
between automatic 16:9 pillarboxing, 4:3, and a stretched picture. You can also try a single
level, open the level editor, clear the high scores, or quit.

## Level editor on the Wii

The editor works like the Mac *Pac the Man X Editor* and edits the maze right where the
game draws it. Point with the Wii Remote (or a USB mouse) and use the tools in the top bar:

- **Path**: drag along a row or a column to lay a path. Dragging off the edge of the maze
  makes a path that wraps around to the other side.
- **Eraser**: click or drag to remove paths.
- **Super pellet**: click to place or remove one (up to 10).
- **Player 1 / Player 2**: click to set where each player starts.
- **Citadel**: click to move the enemies' home.

Hold B (or Z, or Shift) to bypass the editor's checks, as the modifier key did on the Mac.

- **D-pad**: left and right step through the levels; up and down change the tool.
- **−**: undo.
- **1**: try the level.
- **2**: save.
- **+ / MENU**: open the editor menu, for these tasks:
  - add, duplicate, delete or reorder levels
  - set the colour, tiles, citadel style, background and wait time
  - open, create or save level files

Level files are stored in `apps/pacx/levels` and use the same `.plist` format as the Mac
editor, so files can move between the two. The standard levels are read-only; editing them
saves a new file.

## The Mac version

### Building

You need the Xcode command line tools and Python 3 with Pillow.

```bash
python3 -I tools/convert_assets.py --mac "/path/to/Pac the Man X.app/Contents/Resources"
make -C mac
```

The converter writes `mac/data` (the pack, the music, the extra levels and the icons of the
game and of the editor from your copy) and `make -C mac` builds `build-mac/Pac the Man X.app`,
with `Pac the Man X Editor.app` inside its Resources folder, as in the original.
`make -C mac dist` also zips it. The application is signed ad hoc, so the first time you
open a copy downloaded from elsewhere, open it with a right-click and *Open*.

### Playing

The game runs in a window you can resize or make full screen (⌘F); the picture keeps its
proportions unless you choose to stretch it in the preferences. The menus are those of the
original: new games (⌘1, ⌘2, ⌘N), pause and resume (⌘P, ⌘R), try level (⌘T), the level
editor (⌘L), the high scores (⌘S) and the preferences (⌘,), where you choose the
difficulty, the volumes, the level file and the keys of both players.

| | Player 1 | Player 2 |
|---|---|---|
| Keyboard (set in the preferences) | arrow keys | A S D W |
| Game controller | first controller | second controller |

Return joins a game, Space pauses and Esc goes back to the title. On a game controller,
any face button joins, Menu pauses and Options goes back. The settings, high scores and
level files are kept in `~/Library/Application Support/Pac the Man X`; *Open custom levels
folder in Finder* in the Game menu shows the level files.

### Level editor

*Pac the Man X Editor* is a native Mac application, like the original by Stone Design, and
draws the levels with the game's own pictures. Open it from the Game menu (⌘L).

- The tools above the maze (⌘1 to ⌘6): **Path** (drag along a row or a column; drag off the
  edge for a path that wraps around), **Erase**, **Super Pellet** (up to 10), **Player 1**,
  **Player 2** and **Citadel**. Hold Shift or Option to bypass the editor's checks.
- The sidebar chooses the level file and the level, adds, removes, duplicates and reorders
  levels, and sets the maze colour, the tiles, the citadel, the background and the wait time.
- **Launch Pac the Man X** (⌘L) plays the current level in the game, with Pac-Man, Ms.
  Pac-Man or both; Esc in the game brings you back to the editor.
- Undo and redo, copy and paste of whole levels, and the File menu to create, open, save
  and remove level files. The standard levels are read-only; saving them asks for a new file.

The level files are the same `.plist` files as those of the Mac and Wii editors.

### Testing

`PACX_TEST_SHOTS=<folder> "build-mac/Pac the Man X.app/Contents/MacOS/Pac the Man X"`
walks through the screens like the Wii test build, saves PNG pictures of them in the folder
and quits.

## Wii U, Switch and 3DS

These share the game code with the Wii and the Mac (`source/`); `ports/` holds what is their
own: `ports/sdl` (screen, controllers and sound on SDL2, for the Switch and Wii U),
`ports/ctr` (citro2d, ndsp and the touch screen of the 3DS) and `ports/common` (a software
mixer that plays the sound effects and the .ogg music, with
[stb_vorbis](https://github.com/nothings/stb) by Sean Barrett, public domain). They play the
same game, with the same menus, preferences and level editor, and the same level files.

### Building

Install devkitPro with the toolchains and libraries of the system you build for (`wut` and the
`wiiu-sdl2` package for the Wii U, `libnx` and `switch-sdl2` for the Switch, `libctru` and
`citro2d` for the 3DS), Python 3 with Pillow and ffmpeg, then:

```bash
python3 -I tools/convert_assets.py --console "/path/to/Pac the Man X.app/Contents/Resources"
make -C ports/wiiu       # pacx-wiiu.wuhb (and pacx-wiiu.rpx)
make -C ports/switch     # pacx-switch.nro
make -C ports/ctr        # pacx-3ds.3dsx
```

`scripts/build-all.sh "<Resources folder>"` converts the data and builds every system,
the Wii and the Mac included.

### Installing

- **Switch**: copy `pacx-switch.nro` to `sd:/switch/`. The pack and the music are inside it;
  the settings, high scores and level files are kept in `sd:/switch/pacx`.
- **3DS**: copy `pacx-3ds.3dsx` to `sd:/3ds/`. The pack and the music are inside it; the
  settings, high scores and level files are kept in `sd:/3ds/pacx`. The music needs the DSP
  firmware dumped from the console, as with any homebrew; without it the game is silent.
- **Wii U**: copy `pacx-wiiu.wuhb` to `sd:/wiiu/apps/` (Aroma), and `ports/data/pack.bin` and
  `ports/data/music` to `sd:/wiiu/apps/pacx/`, where the settings, scores and levels go too.

### Controls

| | Switch and Wii U | 3DS |
|---|---|---|
| Player 1 | first controller: D-pad or left stick | D-pad or Circle Pad |
| Player 2 | second controller | the face buttons (X up, B down, Y left, A right), or the C-Stick |
| Join a game | A or B on that controller | L or R |
| Pause | + | START |
| Back to the title | − | SELECT |
| Menus | A select, B back, X preferences on the title | A, B, X |
| Pointer (menus and level editor) | touch screen or a mouse | touch screen |
| Level editor | Y try, L or R save, hold B or ZL/ZR to bypass the checks, + menu, − undo | Y try, L or R save, hold B or ZL/ZR (New 3DS) to bypass the checks, START menu, SELECT undo |

On the 3DS the maze is drawn at half size on both screens: the bottom screen is the touch
area, with the same picture. Names after a high score are typed on a keyboard on the screen.

## Linux and Windows

`ports/sdl` builds the game for Linux and Windows on SDL 2, with the keyboard, mouse and game
controllers. See [ports/sdl/README.md](ports/sdl/README.md).

## Testing in Dolphin

`make PACX_TEST=1 NO_DOLXZ=1` builds a version that walks through every screen by itself,
saves screenshots to `sd:/shots` and then switches off. `tools/dolphin_test.sh` runs it in
Dolphin and collects the pictures.

## Credits

- Pac the Man X: Sebastian Wegner, McSebi Software (programming and music); Maxime
  Chevalier (graphics); Jens Nilsson (sound effects); Andrew Stone (level editor).
- [libwiiosk](https://github.com/quatric/libwiiosk) and
  [libinput-wii](https://github.com/quatric/libinput-wii) for the keyboard and input.
- dolxz by FIX94.
- [stb_vorbis](https://github.com/nothings/stb) by Sean Barrett, for the music of the Wii U,
  Switch and 3DS versions.

Pac the Man X is freeware by McSebi Software. This port is not affiliated with McSebi
Software or with Bandai Namco.
