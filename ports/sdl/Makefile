#---------------------------------------------------------------------------------
# The SDL2 layer of the Switch and Wii U ports, which also runs on the desktop: Linux, Windows
# (MSYS2 MINGW64, or cross-compiled) and the Mac
#
#   python3 -I tools/convert_assets.py --console "<.../Pac the Man X.app/Contents/Resources>"
#   make -C ports/sdl                  # build-sdl/pacx-sdl (pacx-sdl.exe on Windows)
#   make -C ports/sdl dist             # ports/sdl/dist/pacx: the program and its data/
#   build-sdl/pacx-sdl                 # finds ports/data, or data/ next to the program
#
# Needs SDL 2.0.18 or newer. Cross-compiling for Windows: CC=x86_64-w64-mingw32-gcc
# SDL_CONFIG=<MinGW SDL2 prefix>/bin/sdl2-config (see ports/sdl/README.md).
#---------------------------------------------------------------------------------
SDL_CONFIG	?=	sdl2-config
ROOT	:=	$(CURDIR)/../..
BUILD	:=	$(ROOT)/build-sdl
SRC_GAME	:=	actors engine game levels plist pack settings editor edit_rules ui main
CFLAGS	:=	-O2 -g -Wall -ffp-contract=off -DPACX_PORT -DPACX_SDL $(EXTRA) -I$(ROOT)/source -I$(ROOT)/ports/common $(shell $(SDL_CONFIG) --cflags) -I$(shell $(SDL_CONFIG) --prefix)/include
LIBS	:=	$(shell $(SDL_CONFIG) --libs) -lm
ifneq (,$(findstring mingw,$(CC))$(findstring Windows,$(OS)))
EXE	:=	.exe
endif
PROGRAM	:=	$(BUILD)/pacx-sdl$(EXE)
OBJS	:=	$(SRC_GAME:%=$(BUILD)/%.o) $(BUILD)/port_sdl.o $(BUILD)/audio_soft.o $(BUILD)/util.o

all: $(PROGRAM)

.PHONY: all dist clean

$(BUILD)/%.o: $(ROOT)/source/%.c
	@mkdir -p $(BUILD)
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/port_sdl.o: port_sdl.c
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/util.o: $(ROOT)/ports/common/util.c
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/audio_soft.o: $(ROOT)/ports/common/audio_soft.c
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -Wno-unused-function -MMD -c $< -o $@

$(PROGRAM): $(OBJS)
	@echo linking ... $(notdir $@)
	@$(CC) $^ $(LIBS) -o $@

# the program with its data next to it, where it looks for them
dist: $(PROGRAM)
	@[ -f $(ROOT)/ports/data/pack.bin ] || { echo "ports/data/pack.bin is missing: run tools/convert_assets.py --console first"; exit 1; }
	@rm -rf dist/pacx
	@mkdir -p dist/pacx
	@cp $(PROGRAM) dist/pacx/
	@cp -R $(ROOT)/ports/data dist/pacx/data
	@cp README.md dist/pacx/README.md
	@echo built ports/sdl/dist/pacx

clean:
	@rm -rf $(BUILD) dist

-include $(OBJS:.o=.d)
