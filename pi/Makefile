#
# Omega - Raspberry Pi bare metal build (Circle)
#
# 1. Get Circle:   git clone https://github.com/rsta2/circle.git
# 2. Configure it: see README-PI.md (Config.mk with AARCH=64, RASPPI=4,
#                  DEPTH=32 and a larger KERNEL_MAX_SIZE), then build
#                  lib, lib/usb, lib/input, lib/fs, lib/fs/fat, lib/sound, addon/SDCard
# 3. Here:         make CIRCLEHOME=/path/to/circle
#
# Result: kernel8-rpi4.img (copy to the SD card together with the firmware)
#

CIRCLEHOME ?= ../../circle

# the emulator core lives one directory up
VPATH = ..

CORE	= CPU.o Memory.o Chipset.o DMA.o Blitter.o CIA.o Floppy.o Gayle.o \
	  Render.o Input.o Snapshot.o Paula.o debug.o \
	  m68kcpu.o m68kops.o m68kopac.o m68kopdm.o m68kopnz.o m68kdasm.o

OBJS	= pimain.o kernel.o HostCircle.o libc.o omega_main.o $(CORE)

LIBS	= $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	  $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/sound/libsound.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

# Circle builds with -nostdinc: add our tiny libc headers, plus the compiler's
# own freestanding headers (stdint.h, stddef.h, stdarg.h)
ifeq ($(strip $(CLANG)),1)
COMPILER_INCLUDE := $(shell $(CC) -print-resource-dir)/include
else
COMPILER_INCLUDE := $(shell $(CC) -print-file-name=include)
endif
INCLUDE += -I .. -I libc -isystem $(COMPILER_INCLUDE)

# the 30 year old Musashi/Omega C code produces many harmless warnings
CFLAGS += -Wno-unused -Wno-sign-compare -Wno-parentheses -Wno-comment \
	  -Wno-implicit-function-declaration -Wno-pointer-sign -Wno-missing-braces \
	  -Wno-unused-but-set-variable -Wno-int-conversion -Wno-incompatible-pointer-types \
	  -Wno-shift-negative-value -Wno-dangling-else -Wno-tautological-constant-out-of-range-compare \
	  -Wno-format -Wno-constant-conversion

# uncomment to send the emulator's debug output to the serial port (slow)
#DEFINE += -DOMEGA_SERIAL_LOG

-include $(DEPS)
