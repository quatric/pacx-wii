#---------------------------------------------------------------------------------
# Pac the Man X for the Mac, with its level editor
#
#   python3 -I tools/convert_assets.py --mac "<.../Pac the Man X.app/Contents/Resources>"
#   make -C mac
#
# builds "build-mac/Pac the Man X.app" (arm64 and x86_64, macOS 11 or newer), with
# "Pac the Man X Editor.app" inside its Resources, as in the original. `make -C mac dist`
# also makes a .zip of it. ARCHS="-arch arm64" builds for Apple silicon only.
#---------------------------------------------------------------------------------
CC		:=	clang
ARCHS	?=	-arch arm64 -arch x86_64
BUILD	:=	../build-mac
DATA	:=	data
SRC		:=	../source

# -ffp-contract=off keeps a*b+c as two roundings, like the original x86 build,
# so sprite positions come out the same.
CFLAGS	:=	-O2 -g -Wall -ffp-contract=off -DPACX_MAC $(ARCHS) -mmacosx-version-min=11.0 -I$(SRC) -I.
OBJCFLAGS	:=	$(CFLAGS) -fobjc-arc

GAME_C	:=	actors engine game levels plist pack settings
GAME_M	:=	main_mac gfx_mac input_mac audio_mac ui_mac
EDIT_C	:=	levels plist pack settings edit_rules
EDIT_M	:=	editor/editor_main editor/maze_view

GAME_FW	:=	-framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore \
			-framework GameController -framework AVFoundation -framework AudioToolbox \
			-framework Carbon -framework ImageIO -framework UniformTypeIdentifiers
EDIT_FW	:=	-framework Cocoa -framework UniformTypeIdentifiers

GAME_OBJ	:=	$(GAME_C:%=$(BUILD)/obj/game/%.o) $(GAME_M:%=$(BUILD)/obj/game/%.o)
EDIT_OBJ	:=	$(EDIT_C:%=$(BUILD)/obj/editor/%.o) $(EDIT_M:%=$(BUILD)/obj/editor/%.o)

GAME_APP	=	$(BUILD)/Pac the Man X.app
EDIT_APP	=	$(GAME_APP)/Contents/Resources/Pac the Man X Editor.app

.PHONY: all app clean dist

all: app

$(BUILD)/obj/game/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/obj/game/%.o: %.m
	@mkdir -p $(dir $@)
	@echo $(notdir $<)
	@$(CC) $(OBJCFLAGS) -MMD -c $< -o $@

$(BUILD)/obj/editor/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo editor: $(notdir $<)
	@$(CC) $(CFLAGS) -DPACX_EDITOR -MMD -c $< -o $@

$(BUILD)/obj/editor/%.o: %.m
	@mkdir -p $(dir $@)
	@echo editor: $(notdir $<)
	@$(CC) $(OBJCFLAGS) -DPACX_EDITOR -MMD -c $< -o $@

$(BUILD)/pacx: $(GAME_OBJ)
	@echo linking ... $(notdir $@)
	@$(CC) $(ARCHS) -mmacosx-version-min=11.0 $(GAME_FW) $^ -o $@

$(BUILD)/pacx-editor: $(EDIT_OBJ)
	@echo linking ... $(notdir $@)
	@$(CC) $(ARCHS) -mmacosx-version-min=11.0 $(EDIT_FW) $^ -o $@

app: $(BUILD)/pacx $(BUILD)/pacx-editor
	@[ -f $(DATA)/pack.bin ] || { echo "mac/data/pack.bin is missing: run tools/convert_assets.py --mac first (see README.md)"; exit 1; }
	@echo bundling ... Pac the Man X.app
	@rm -rf "$(GAME_APP)"
	@mkdir -p "$(GAME_APP)/Contents/MacOS" "$(GAME_APP)/Contents/Resources/Levels"
	@cp Info.plist "$(GAME_APP)/Contents/Info.plist"
	@printf 'APPL????' > "$(GAME_APP)/Contents/PkgInfo"
	@cp $(BUILD)/pacx "$(GAME_APP)/Contents/MacOS/Pac the Man X"
	@cp $(DATA)/pack.bin $(DATA)/pacman.icns "$(GAME_APP)/Contents/Resources/"
	@cp $(DATA)/player1icon.png $(DATA)/player2icon.png "$(GAME_APP)/Contents/Resources/"
	@cp -R $(DATA)/music "$(GAME_APP)/Contents/Resources/Music"
	@cp $(DATA)/levels/*.plist "$(GAME_APP)/Contents/Resources/Levels/" 2>/dev/null || true
	@mkdir -p "$(EDIT_APP)/Contents/MacOS" "$(EDIT_APP)/Contents/Resources"
	@cp EditorInfo.plist "$(EDIT_APP)/Contents/Info.plist"
	@printf 'APPL????' > "$(EDIT_APP)/Contents/PkgInfo"
	@cp $(BUILD)/pacx-editor "$(EDIT_APP)/Contents/MacOS/Pac the Man X Editor"
	@cp $(DATA)/ptm.icns "$(EDIT_APP)/Contents/Resources/"
	@codesign --force --sign - "$(EDIT_APP)" 2>/dev/null
	@codesign --force --sign - "$(GAME_APP)" 2>/dev/null
	@echo built "$(GAME_APP)"

dist: app
	@cd $(BUILD) && rm -f "Pac the Man X.zip" && ditto -c -k --keepParent "Pac the Man X.app" "Pac the Man X.zip"
	@echo built $(BUILD)/Pac the Man X.zip

clean:
	@rm -rf $(BUILD)

-include $(GAME_OBJ:.o=.d) $(EDIT_OBJ:.o=.d)
