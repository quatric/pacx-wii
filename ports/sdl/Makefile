#---------------------------------------------------------------------------------
# The SDL2 layer of the Switch and Wii U ports, built for the desktop to try it out
#
#   python3 -I tools/convert_assets.py --console "<.../Pac the Man X.app/Contents/Resources>"
#   make -C ports/sdl
#   PACX_DATA=ports/data build-sdl/pacx-sdl
#---------------------------------------------------------------------------------
ROOT	:=	$(CURDIR)/../..
BUILD	:=	$(ROOT)/build-sdl
SRC_GAME	:=	actors engine game levels plist pack settings editor edit_rules ui main
CFLAGS	:=	-O2 -g -Wall -ffp-contract=off -DPACX_PORT -DPACX_SDL $(EXTRA) -I$(ROOT)/source -I$(ROOT)/ports/common $(shell sdl2-config --cflags) -I$(shell sdl2-config --prefix)/include
LIBS	:=	$(shell sdl2-config --libs) -lm
OBJS	:=	$(SRC_GAME:%=$(BUILD)/%.o) $(BUILD)/port_sdl.o $(BUILD)/audio_soft.o $(BUILD)/util.o

all: $(BUILD)/pacx-sdl

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

$(BUILD)/pacx-sdl: $(OBJS)
	@echo linking ... $(notdir $@)
	@$(CC) $^ $(LIBS) -o $@

clean:
	@rm -rf $(BUILD)

-include $(OBJS:.o=.d)
