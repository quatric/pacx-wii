#---------------------------------------------------------------------------------
# Pac the Man X for the Wii
#
#   python3 -I tools/convert_assets.py "<.../Pac the Man X.app/Contents/Resources>"
#   make
#
# The converter writes data/pack.bin (linked into the program) and the files in
# apps/pacx/ (music, icon, extra levels). PACX_TEST=1 builds the scripted test
# version used with Dolphin (see tools/dolphin_test.sh).
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

ifeq ($(PACX_TEST),1)
TARGET		:=	pacx-test
BUILD		:=	build-test
else
TARGET		:=	pacx
BUILD		:=	build
endif
SOURCES		:=	source libinput-wii/source
DATA		:=	data
INCLUDES	:=	source

#---------------------------------------------------------------------------------
# -ffp-contract=off keeps a*b+c as two roundings, like the original x86 build,
# so sprite positions come out the same.
CFLAGS	= -g -O2 -Wall -ffp-contract=off $(MACHDEP) $(INCLUDE)
ifeq ($(PACX_TEST),1)
CFLAGS	+=	-DPACX_TEST
endif
CXXFLAGS	=	$(CFLAGS)
LDFLAGS	=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map

LIBS	:=	-lwiiosk -lfat -lwiikeyboard -lwiiuse -lbte -lasnd -logc -lvorbisidec -logg -lpng -lz -lm
LIBDIRS	:=	$(DEVKITPRO)/portlibs/ppc $(CURDIR)/libinput-wii $(CURDIR)/libwiiosk

ifeq ($(NO_DOLXZ),1)
BOOT_DOL	=	$(TARGET).dol
else
BOOT_DOL	=	$(TARGET)-xz.dol
endif

ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))

export LD	:=	$(CC)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CFILES:.c=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES), -iquote $(CURDIR)/$(dir)) \
					-I$(CURDIR)/libinput-wii/include \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBOGC_INC)
export LIBPATHS	:= -L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean dist

$(BUILD):
	@[ -f $(CURDIR)/data/pack.bin ] || { echo "data/pack.bin is missing: run tools/convert_assets.py first (see README.md)"; exit 1; }
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C libwiiosk
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@$(MAKE) --no-print-directory -C libwiiosk clean
	@rm -fr build build-test pacx.elf pacx.dol pacx-xz.dol pacx-test.elf pacx-test.dol pacx-test-xz.dol

# the Homebrew Channel folder: apps/pacx with boot.dol, meta.xml, icon.png, music, levels
dist: $(BUILD)
	@cp $(BOOT_DOL) apps/pacx/boot.dol
	@echo apps/pacx is ready to copy to the SD card

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

ifneq ($(NO_DOLXZ),1)
$(OUTPUT)-xz.dol: $(OUTPUT).dol
	@echo compressing ... $(notdir $@)
	@sh $(dir $(OUTPUT))scripts/dolxz.sh $< $@
endif

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES) : $(HFILES)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

endif
