#---------------------------------------------------------------------------------
# Pac the Man X for the Nintendo Switch (devkitA64, libnx, SDL2)
#
#   python3 -I tools/convert_assets.py --console "<.../Pac the Man X.app/Contents/Resources>"
#   make -C ports/switch
#
# builds pacx-switch.nro with the pack and the music in its romfs.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif

TOPDIR		:=	$(CURDIR)
ROOT		:=	$(TOPDIR)/../..
BUILD		:=	$(ROOT)/build-switch
TARGET		:=	pacx-switch
ROMFS		:=	$(ROOT)/ports/data
APP_TITLE	:=	Pac the Man X
APP_AUTHOR	:=	quatric
APP_VERSION	:=	1.5.1a-port1
ICON		:=	$(ROOT)/ports/data/icon.jpg

PORTLIBS	:=	$(DEVKITPRO)/portlibs/switch
LIBNX		:=	$(DEVKITPRO)/libnx
export PATH := $(DEVKITPRO)/devkitA64/bin:$(DEVKITPRO)/tools/bin:$(PATH)

CC			:=	$(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-gcc

SRC_GAME	:=	actors engine game levels plist pack settings editor edit_rules ui main
SRC_PORT	:=	$(ROOT)/ports/sdl/port_sdl.c $(ROOT)/ports/common/audio_soft.c

ARCH		:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS		:=	-g -O2 -Wall -ffp-contract=off -ffunction-sections $(ARCH) -D__SWITCH__ \
				-DPACX_PORT -DPACX_SDL -I$(ROOT)/source -I$(ROOT)/ports/common \
				-I$(PORTLIBS)/include -I$(PORTLIBS)/include/SDL2 -isystem $(LIBNX)/include
LDFLAGS		:=	-specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map \
				-L$(PORTLIBS)/lib -L$(LIBNX)/lib
LIBS		:=	-lSDL2main -lSDL2 -lEGL -lglapi -ldrm_nouveau -lnx -lstdc++ -lm

OBJS		:=	$(SRC_GAME:%=$(BUILD)/%.o) $(BUILD)/port_sdl.o $(BUILD)/audio_soft.o $(BUILD)/util.o

.PHONY: all clean
all: $(BUILD)/$(TARGET).nro
	@cp $< $(ROOT)/$(TARGET).nro
	@echo built $(TARGET).nro

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

$(BUILD)/$(TARGET).nacp:
	@nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(BUILD)/$(TARGET).nro: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).nacp
	@elf2nro $< $@ --icon=$(ICON) --nacp=$(BUILD)/$(TARGET).nacp --romfsdir=$(ROMFS)

clean:
	@rm -rf $(BUILD) $(ROOT)/$(TARGET).nro

-include $(OBJS:.o=.d)
