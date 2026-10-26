#---------------------------------------------------------------------------------
# Pac the Man X for the Nintendo 3DS (devkitARM, libctru, citro2d)
#
#   python3 -I tools/convert_assets.py --console "<.../Pac the Man X.app/Contents/Resources>"
#   make -C ports/ctr
#
# builds pacx-3ds.3dsx with the pack and the music in its romfs. Sound needs the DSP
# firmware dumped from the console (DSP1), as for every homebrew.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif

ROOT		:=	$(CURDIR)/../..
BUILD		:=	$(ROOT)/build-3ds
TARGET		:=	pacx-3ds
CTRU		:=	$(DEVKITPRO)/libctru
ARMBIN		:=	$(DEVKITPRO)/devkitARM/bin
export PATH := $(ARMBIN):$(DEVKITPRO)/tools/bin:$(PATH)

CC			:=	$(ARMBIN)/arm-none-eabi-gcc

SRC_GAME	:=	actors engine game levels plist pack settings editor edit_rules ui main

ARCH		:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS		:=	-g -O2 -Wall -ffp-contract=off -ffunction-sections $(ARCH) -D__3DS__ -DPACX_PORT -DPACX_3DS $(EXTRA) \
				-I$(ROOT)/source -I$(ROOT)/ports/common -I$(CTRU)/include -I$(DEVKITPRO)/portlibs/3ds/include
LDFLAGS		:=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map -L$(CTRU)/lib -L$(DEVKITPRO)/portlibs/3ds/lib
LIBS		:=	-lcitro2d -lcitro3d -lctru -lm

OBJS		:=	$(SRC_GAME:%=$(BUILD)/%.o) $(BUILD)/port_ctr.o $(BUILD)/audio_soft.o $(BUILD)/util.o

.PHONY: all clean
all: $(BUILD)/$(TARGET).3dsx
	@cp $< $(ROOT)/$(TARGET).3dsx
	@echo built $(TARGET).3dsx

$(BUILD)/%.o: $(ROOT)/source/%.c
	@mkdir -p $(BUILD)
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/port_ctr.o: $(ROOT)/ports/ctr/port_ctr.c
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/util.o: $(ROOT)/ports/common/util.c
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/audio_soft.o: $(ROOT)/ports/common/audio_soft.c
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -Wno-unused-function -MMD -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	@echo linking ... $(notdir $@)
	@$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD)/$(TARGET).smdh: $(ROOT)/ports/data/icon-3ds.png
	@smdhtool --create "Pac the Man X" "Maze game with a level editor" "quatric" $< $@

$(BUILD)/$(TARGET).3dsx: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).smdh
	@3dsxtool $< $@ --smdh=$(BUILD)/$(TARGET).smdh --romfs=$(ROOT)/ports/data

clean:
	@rm -rf $(BUILD) $(ROOT)/$(TARGET).3dsx

-include $(OBJS:.o=.d)
