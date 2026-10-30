# Omega - Amiga emulator
#
#   make            SDL2 desktop build   -> ./omega
#   make headless   no external libraries -> ./omega-headless (tests / bare metal template)
#   make test       boots Kickstart 1.3 + Workbench headless and checks a screenshot
#                   (needs KICK=path/to/kick13.rom and ADF=path/to/workbench.adf)

CC      ?= cc
CFLAGS  ?= -O2 -std=gnu99 -w
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null || echo -lSDL2)

CORE = main.c CPU.c Memory.c Chipset.c DMA.c Blitter.c CIA.c Floppy.c Gayle.c \
       Render.c Input.c Snapshot.c Paula.c debug.c \
       m68kcpu.c m68kops.c m68kopac.c m68kopdm.c m68kopnz.c m68kdasm.c

all: omega

omega: $(CORE) HostSDL.c *.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(CORE) HostSDL.c -o $@ $(SDL_LIBS) -lm

headless: omega-headless

omega-headless: $(CORE) HostNull.c *.h
	$(CC) $(CFLAGS) $(CORE) HostNull.c -o $@ -lm

test: omega-headless
	OMEGA_INSERT_AT=150 OMEGA_SHOT_AT=1500 OMEGA_SHOT_FILE=test.ppm ./omega-headless $(KICK) $(ADF)
	@test -s test.ppm && echo "screenshot written to test.ppm"

clean:
	rm -f omega omega-headless test.ppm

.PHONY: all headless test clean
