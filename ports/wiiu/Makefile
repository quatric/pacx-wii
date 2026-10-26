#---------------------------------------------------------------------------------
# Pac the Man X for the Nintendo Wii U (devkitPPC, wut, SDL2)
#
#   python3 -I tools/convert_assets.py --console "<.../Pac the Man X.app/Contents/Resources>"
#   make -C ports/wiiu
#
# builds pacx-wiiu.wuhb (and the .rpx). Copy ports/data's pack.bin and music folder to
# sd:/wiiu/apps/pacx/ on the SD card; the settings, scores and levels are kept there too.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif

ROOT		:=	$(CURDIR)/../..
BUILD		:=	$(ROOT)/build-wiiu
TARGET		:=	pacx-wiiu
WUT			:=	$(DEVKITPRO)/wut
PORTLIBS	:=	$(DEVKITPRO)/portlibs/wiiu
PPCBIN		:=	$(DEVKITPRO)/devkitPPC/bin
export PATH := $(PPCBIN):$(DEVKITPRO)/tools/bin:$(PATH)

CC			:=	$(PPCBIN)/powerpc-eabi-gcc

SRC_GAME	:=	actors engine game levels plist pack settings editor edit_rules ui main

# -ffp-contract=off keeps a*b+c as two roundings, like the original x86 build
MACHDEP		:=	-DESPRESSO -mcpu=750 -meabi -mhard-float
CFLAGS		:=	-g -O2 -Wall -ffp-contract=off -ffunction-sections $(MACHDEP) -D__WIIU__ -D__WUT__ \
				-DPACX_PORT -DPACX_SDL -I$(ROOT)/source -I$(ROOT)/ports/common \
				-I$(PORTLIBS)/include -I$(PORTLIBS)/include/SDL2 -I$(WUT)/include -I$(DEVKITPRO)/portlibs/ppc/include
LDFLAGS		:=	-g $(MACHDEP) -specs=$(WUT)/share/wut.specs -Wl,-Map,$(BUILD)/$(TARGET).map \
				-L$(PORTLIBS)/lib -L$(WUT)/lib -L$(DEVKITPRO)/portlibs/ppc/lib
LIBS		:=	-lSDL2main -lSDL2 -lwut -lm

OBJS		:=	$(SRC_GAME:%=$(BUILD)/%.o) $(BUILD)/port_sdl.o $(BUILD)/audio_soft.o $(BUILD)/util.o

.PHONY: all clean
all: $(BUILD)/$(TARGET).wuhb
	@cp $< $(ROOT)/$(TARGET).wuhb
	@cp $(BUILD)/$(TARGET).rpx $(ROOT)/$(TARGET).rpx
	@echo built $(TARGET).wuhb

$(BUILD)/%.o: $(ROOT)/source/%.c
	@mkdir -p $(BUILD)
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/port_sdl.o: $(ROOT)/ports/sdl/port_sdl.c
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

$(BUILD)/$(TARGET).rpx: $(BUILD)/$(TARGET).elf
	@elf2rpl $< $@
	@echo built ... $(notdir $@)

$(BUILD)/$(TARGET).wuhb: $(BUILD)/$(TARGET).rpx
	@wuhbtool $< $@ --name="Pac the Man X" --short-name="Pac the Man X" --author="quatric" --icon=$(ROOT)/ports/data/icon-wiiu.png
	@echo built ... $(notdir $@)

clean:
	@rm -rf $(BUILD) $(ROOT)/$(TARGET).wuhb $(ROOT)/$(TARGET).rpx

-include $(OBJS:.o=.d)
