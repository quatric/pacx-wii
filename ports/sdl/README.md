# Pac the Man X on Linux and Windows

The SDL 2 layer of the Switch and Wii U ports also runs on the desktop: the game logic is the
code in `source/`, and `ports/sdl/port_sdl.c` has the window, drawing, controllers, keyboard
and mouse. The sound is the ports' software mixer. The preferences, the name entry after a high
score and the level editor are the Wii's screens, drawn in the game's own window.

## Building

This repository holds no game data. Convert it from your own copy of Pac the Man X 1.5.1a (the
freeware Mac game), which writes `ports/data` with the pack, the `.ogg` music (needs ffmpeg)
and the extra levels:

```bash
python3 -I tools/convert_assets.py --console "/path/to/Pac the Man X.app/Contents/Resources"
```

You need a C compiler, `make` and SDL 2.0.18 or newer:

| System | Packages |
|---|---|
| Debian, Ubuntu | `build-essential libsdl2-dev` |
| Fedora | `gcc make SDL2-devel` |
| Windows (MSYS2, MINGW64 shell) | `make mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2` |

```bash
make -C ports/sdl         # build-sdl/pacx-sdl (pacx-sdl.exe on Windows)
make -C ports/sdl dist    # ports/sdl/dist/pacx: the program with data/ next to it
```

On Windows, copy the DLLs that `ldd build-sdl/pacx-sdl.exe` lists from `/mingw64/bin` into the
folder of the program (the CI job does this). To cross-compile for Windows from Linux or the
Mac, use `make -C ports/sdl CC=x86_64-w64-mingw32-gcc SDL_CONFIG=<MinGW SDL2>/bin/sdl2-config`.

The program finds its data in `PACX_DATA`, in `data/` next to itself, in `../share/pacx`, or in
`ports/data` of the working directory.

## Playing

The settings, high scores and level files are kept in the user's folder of SDL
(`~/.local/share/quatric/Pac the Man X`, `%APPDATA%\quatric\Pac the Man X`); `PACX_HOME`
names another. The window can be resized; F11 or Alt+Return switches full screen.

| | Player 1 | Player 2 |
|---|---|---|
| Keyboard | arrow keys | W A S D |
| Game controller | first controller | second controller |

- **Join a game**: Return, or A or B on a controller.
- **Pause**: Space, or Start.
- **Back to the title**: Esc, or Back.
- **Preferences**: Tab or F1 on the title screen (X on a controller).
- **Menus**: arrow keys and Return, Esc to go back, or the mouse.
- **Names**: type, then Return.

### Level editor

Open it from the preferences. Use the mouse as on the Wii; the Wii's buttons are `-` (undo),
`1` (try the level), `2` (save), `=` or Tab (the editor menu) and Shift (bypass the checks).

`PACX_SHOTS=<folder>` saves every 60th frame there as a BMP file, for testing.
