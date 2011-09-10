#
# Handheld Reverse Engineering Tool
#
# This program is free software distributed under the terms of the
# license that can be found in the COPYING file.
#
# This Makefile requires GNU Make.
#

# Program version
VERSION=pre-0.5.3-$(shell date +"%Y%m%d_%H%M%S")

# Output directory
OUT=out/

# Default compiler flags (note -march=armv4 is needed for 16 bit insns)
CXXFLAGS = -Wall -O -g -MD -march=armv6 -Iinclude -fno-exceptions -fno-rtti
CXXFLAGS_ARMV5 = -Wall -O -g -MD -march=armv6 -Iinclude -fno-exceptions -fno-rtti
LDFLAGS = -Wl,--major-subsystem-version=5,--minor-subsystem-version=2 -static
# LDFLAGS to debug invalid imports in exe
#LDFLAGS = -Wl,-M -Wl,--cref

#LIBS = -L/media/ECB082C3B08293AC/cegcc-55/opt/cegcc/arm-cegcc/lib/w32api/  -lwinsock
LIBS = -L/home/micdav/cegcc55/opt/cegcc/arm-cegcc/lib/w32api/

all: $(OUT) $(OUT)haret.exe $(OUT)haretconsole.tar.gz

# Run with "make V=1" to see the actual compile commands
ifdef V
Q=
else
Q=@
endif

.PHONY : all FORCE

vpath %.cpp src src/wince src/mach
vpath %.S src src/wince src/mach
vpath %.rc src/wince

################ cegcc settings

BASE ?= /home/micdav/cegcc55/opt/cegcc
export BASE

RC = $(BASE)/bin/arm-cegcc-windres
RCFLAGS = -r -l 0x409 -Iinclude
#RCFLAGS = 

CXX = $(BASE)/bin/arm-cegcc-g++
STRIP = $(BASE)/bin/arm-cegcc-strip

DLLTOOL = $(BASE)/bin/arm-cegcc-dlltool
DLLTOOLFLAGS =

define compile_armv4
@echo "  Compiling (armv6) $1"
$(Q)$(CXX) $(CXXFLAGS) -c $1 -o $2
endef

define compile_armv5
@echo "  Compiling (armv6) $1"
$(Q)$(CXX) $(CXXFLAGS_ARMV5) -c $1 -o $2
endef

define compile
$(if $(findstring -armv5.,$1),
	$(call compile_armv5,$1,$2),
	$(call compile_armv4,$1,$2)
)
endef

$(OUT)%.o: %.cpp ; $(call compile,$<,$@)
$(OUT)%.o: %.S ; $(call compile,$<,$@)

$(OUT)%.o: %.rc
	@echo "  Building resource file from $<"
	$(Q)$(RC) $(RCFLAGS) -i $< -o $@

$(OUT)%.lib: src/wince/%.def
	@echo "  Building library $@"
	$(Q)$(DLLTOOL) $(DLLTOOLFLAGS) -d $< -l $@

$(OUT)%-debug:
	$(Q)echo 'const char *VERSION = "$(VERSION)";' > $(OUT)version.cpp
	$(call compile,$(OUT)version.cpp,$(OUT)version.o)
	@echo "  Checking for relocations"
	$(Q)tools/checkrelocs $(filter %.o,$^)
	@echo "  Linking $@ (Version \"$(VERSION)\")"
	$(Q)$(CXX) $(LDFLAGS) $(OUT)version.o $^ $(LIBS) -o $@

$(OUT)%.exe: $(OUT)%-debug
	@echo "  Stripping $^ to make $@"
	$(Q)$(STRIP) $^ -o $@

################ Haret exe rules

# List of machines supported - note order is important - it determines
# which machines are checked first.
MACHOBJS := machines.o \
  mach-autogen.o \
  arch-pxa27x.o arch-pxa.o arch-sa.o arch-omap.o arch-s3.o arch-msm.o \
  arch-imx.o arch-centrality.o arch-arm.o arch-msm-asm.o

$(OUT)mach-autogen.o: src/mach/machlist.txt
	@echo "  Building machine list"
	$(Q)tools/buildmachs.py < $^ > $(OUT)mach-autogen.cpp
	$(call compile,$(OUT)mach-autogen.cpp,$@)

COREOBJS := $(MACHOBJS) haret-res.o libcfunc.o \
  script.o memory.o video.o asmstuff.o lateload.o output.o cpu.o \
  linboot.o fbwrite.o font_mini_4x6.o winvectors.o exceptions.o \
  asmstuff-armv5.o

HARETOBJS := $(COREOBJS) haret.o gpio.o uart.o wincmds.o \
  watch.o irqchain.o irq.o pxatrace.o mmumerge.o l1trace.o arminsns.o \
  network.o terminal.o com_port.o tlhcmds.o memcmds.o pxacmds.o aticmds.o \
  imxcmds.o s3c-gpio.o msmcmds.o

$(OUT)haret-debug: $(addprefix $(OUT),$(HARETOBJS)) src/haret.lds

####### Stripped down linux bootloading program.
LINLOADOBJS := $(COREOBJS) stubboot.o kernelfiles.o

KERNEL := zImage
INITRD := /dev/null
SCRIPT := docs/linload.txt

$(OUT)kernelfiles.o: src/wince/kernelfiles.S FORCE
	@echo "  Building $@"
	$(Q)$(CXX) -c -DLIN_KERNEL=\"$(KERNEL)\" -DLIN_INITRD=\"$(INITRD)\" -DLIN_SCRIPT=\"$(SCRIPT)\" -o $@ $<

$(OUT)oldlinload-debug: $(addprefix $(OUT), $(LINLOADOBJS)) src/haret.lds

oldlinload: $(OUT)oldlinload.exe

linload: $(OUT) $(OUT)haret.exe
	@echo "  Building boot bundle"
	$(Q)tools/make-bootbundle.py -o $(OUT)linload.exe $(OUT)haret.exe $(KERNEL) $(INITRD) $(SCRIPT)

####### Haretconsole tar files

HC_FILES := README console *.py arm-linux-objdump

$(OUT)haretconsole.tar.gz: $(wildcard $(addprefix haretconsole/, $(HC_FILES)))
	@echo "  Creating tar $@"
	$(Q)tar cfz $@ $^

####### Generic rules
clean:
	rm -rf $(OUT)

$(OUT):
	mkdir $@

-include $(OUT)*.d

