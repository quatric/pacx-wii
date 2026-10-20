# Pac the Man X for the Wii

A port of **Pac the Man X 1.5.1a**, the freeware Mac maze game by McSebi Software, to the
Nintendo Wii (Homebrew Channel), together with its level editor.

The game logic is rebuilt from the original program, so it plays the same as the original:
the same mazes, speeds, timers, scores, bonus items, demo mode, high score tables and
screens, with the original graphics, sounds and music.

## What you need

This repository holds no game data. The pictures, sounds, music and levels are converted
from your own copy of the game, which you can still download as freeware.

- devkitPro with devkitPPC and libogc, plus the portlibs `ppc-libvorbisidec`, `ppc-libpng`
  and `ppc-zlib`
- Python 3 with Pillow
- Pac the Man X 1.5.1a for the Mac (`Pac the Man X.app`)

## Building

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

## Controls

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

## Level editor

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

Pac the Man X is freeware by McSebi Software. This port is not affiliated with McSebi
Software or with Bandai Namco.
