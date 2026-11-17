# Omega – 2026 revision

Goal: fix crash bugs, make the core portable for a Raspberry Pi bare metal
port, and add a regression test. Workbench 1.3 boot output is pixel-identical
to the previous version (checked at frames 100, 400 and 1500).

## Crash and memory-safety fixes

| Where | Problem | Fix |
|---|---|---|
| Memory.c | Writes to 0xE00000–0xF7FFFF (Zorro autoconfig, used by KS 2.x/3.x and AROS) were sent to the custom-chip handler tables with an index in the hundreds of thousands → call through a random function pointer (segfault at startup with 512K ROMs that probe autoconfig) | Address decoding rewritten: 24-bit mask, unmapped areas ignored |
| Memory.c | Custom registers not mirrored/bounds-checked: byte reads index a 32-entry table, long reads a 16-entry table with offsets up to 255 | Mirror to 0xDFF000–0xDFF1FF, bounds check every table |
| Memory.c | 32-bit write to the Gayle/IDE area fell through and also wrote RAM (missing `return`) | Fixed |
| Memory.c | First ROM byte (0xF80000) was treated as chip RAM for byte read/write | `>=` boundary |
| Memory.c | Unaligned type-punned loads (`*(uint16_t*)&mem[addr]`): undefined behaviour, can fault on ARM | Byte-wise big-endian helpers (compile to load + byteswap) |
| Chipset/DMA/Blitter | Bitplane, sprite, copper, blitter and disk DMA pointers were never masked → garbage pointer values read/wrote outside the 16 MB array | `CHIPW()` mask to 2 MB chip RAM at every DMA access (wraps like hardware) |
| DMA.c | Sprite 0 with vertical start < 43 wrote before the framebuffer (heap corruption); no left clipping | Clipped on all sides |
| DMA.c | No limit on framebuffer write position | Guard before every render call |
| Render (HAM) | Read one pixel before the start of the framebuffer | Guarded |
| Host (keyboard) | Key table indexed by truncated SDL keycodes: out-of-bounds reads, collisions (F2 sent `;`, F4 sent `=`), unmapped keys sent the `` ` `` key | Proper SDL scancode → Amiga raw key table |
| Floppy.c | `floppyInit(n)` cleared the state of `df[driveSelected]` instead of `df[n]` | Fixed |
| Floppy.c | ADF larger than 82 cylinders overflowed the MFM buffer; `read()` result unchecked | Size clamped, loader moved to host |
| CPU.c | `printf("%0x6")` format typo | `%06x` |

## Functional improvements

- **Blitter area fill** (inclusive, exclusive, FCI carry-in) – listed as missing
  in the README; implemented per the Hardware Reference Manual, unit-tested.
- **Keyboard queue**: events are delivered to CIA-A one per frame; before,
  two events in the same poll overwrote each other (lost keys with Shift etc.).
- Left mouse button no longer also presses joystick fire (mask was `& 63`).
- Mouse buttons tested with bit masks (left+right together works).
- Amiga F6–F10, HELP (F11/Insert), Amiga keys on PageUp/PageDown.

## Bitplane display fixes (games)

Found with a test screen that sets up the display like a game (own copper list,
lores 320x200, 32 colours, interleaved bitplanes); now pixel-identical to the
expected image, Kickstart/Workbench output unchanged:

- Bitplane modulo was unsigned (negative modulos became huge) and was added on
  every line, even lines without bitplane DMA: any modulo other than 0 moved
  the picture. Now signed and added only after a fetched line.
- Fetch window was always DDFSTRT+168 (21 lores words per line) and pixels were
  drawn as one continuous stream, which only looked right with modulo 0. Now
  the window follows DDFSTRT/DDFSTOP (8-cycle units, one extra unit in hires)
  and every line is drawn on its own row, placed according to DDFSTRT.
- DIWSTOP vertical stop is honoured; rows without bitplane data show COLOR00.
- Hardware scrolling: bitplane fetch slots now exist wherever the fetch window
  is (an early DDFSTRT such as $30 lost one word per line), and BPLCON1
  playfield delays are applied. Checked with a 336-pixel-wide test picture at
  scroll positions 0, 5 and 15: pixel-identical.

## Copper timing

The copper could execute on every free colour clock, including odd ones, so it
ran twice as fast as real hardware. Games that change display registers at an
exact point of a line broke: Monkey Island's copper list sets the bitplane
pointers at the end of line $2B, which the emulator did before the last
bitplane fetches of that line, shifting every plane differently (wrong colours,
picture wrapped horizontally). Found with a snapshot taken on the Raspberry Pi
(F12) and reproduced on the PC. The copper now only uses even clocks.

## Amiga 500 PAL timing

- 68000 at 7.09379 MHz: exactly 2 CPU cycles per colour clock (cycle budget
  carried between colour clocks). The original ran 16 cycles per colour clock,
  about 8 times an A500. Measured: 142,101 CPU cycles per frame
  (A500 PAL: 313 x 227 x 2 = 142,102).
- PAL video timing: 313 lines of 227 colour clocks (49.92 Hz), PAL Agnus ID
  in VPOSR (was NTSC, 263 x 228, 60 Hz).
- Hosts pace frames at 20.032 ms (the SDL build no longer follows the
  monitor's vsync); 256 visible lines.
- Not cycle-exact: the CPU is not slowed down by DMA contention (bitplanes,
  blitter), the blitter still completes instantly, and the floppy keeps its
  8x turbo transfer.

## Machine reset

Ctrl+Amiga+Amiga (and the 68000 RESET instruction) only cleared RAM and moved
the PC to the ROM. The CPU state, custom chip registers (interrupts and DMA
still enabled), CIAs and floppy drives kept their old state, so after changing
disk and resetting the machine hung (white screen) or did not read the floppy.
The reset now restarts CPU, custom chips, CIAs, keyboard and drive electronics;
disks stay in the drives. Tested: disk change then reset, reset then disk
change (both boot the new disk exactly like a cold start), reset with the same
disk, two resets in a row.

## Memory configuration

Chip RAM 256K/512K/1M/2M (mirrored through $000000-$1FFFFF; DMA limited to the
fitted size, like the Agnus) and slow RAM 0-1.75MB at $C00000 in 256K steps
(`memoryConfigure()`). Default 512K + 512K (A500 with A501). Raspberry Pi: F5
settings menu, saved in OMEGA.CFG; desktop: OMEGA_CHIP / OMEGA_SLOW (KB).
Kickstart 1.3 reports the expected free memory in all tested configurations
(256K: 100,648 ... 2M + 1.75M: 3,770,632 bytes free). Snapshots now store the
memory configuration (format version 2; version 1 is still read).

## Blitter line mode (filled vectors)

The line mode was an approximation: no one-dot mode (SING), fixed texture, D
pointer stepping with C. Filled vector demos and games draw polygon edges with
one dot per raster line and then area-fill them; with extra dots on a line the
fill toggled an odd number of times and leaked to the left edge of the screen
(horizontal streaks, seen in State of the Art from a Raspberry Pi snapshot).
Rewritten to follow the hardware step by step: error term from BLTAPTL with
BLTAMOD/BLTBMOD, octant from SUD/SUL/AUL, ASH stepping across words, one-dot
mode, BLTBDAT texture rotated from BSH, first dot to BLTDPT and the rest back
to the C address, final pointers/ASH/SIGN and zero flag written back. Checked:
no fill row leaks in the demo's frames, Kickstart 1.3 hand picture and
Workbench unchanged, ASan/UBSan clean.

## Audio (Paula)

New `Paula.c`: the 4 channels with DMA (AUDxLC/LEN restart and an interrupt at
the start of every block, which music players use to chain samples) and
CPU-driven playback (AUDxDAT, interrupt when the word has been played), volume
0-64, ADKCON attach (volume/period modulation), hard stereo (0+3 left, 1+2
right) with optional separation, Amiga 500 output filters (fixed ~4.9 kHz low
pass, the 12 dB "LED" filter at ~3.1 kHz when the power LED is bright) and a DC
blocker. Event driven: work is only done when a channel moves to its next
sample or an output sample is due (≈5 % of the emulation time). Output at
48 kHz; hosts trim the rate by at most ±0.5 % to follow their sound queue.
Fixed on the way: AUD1VOL wrote channel 0's volume; the old audio slots
counted per scan line and checked a DMACON copy that was never set.
Checked: synthetic test tones come out at the exact pitch on the expected
side (436.4 Hz on the left, 277.1 Hz on the right), Monkey Island music
plays from a snapshot, graphics regressions unchanged, ASan/UBSan clean.
Outputs: SDL audio (desktop), HDMI or headphone jack (Pi, F5 menu),
`OMEGA_WAV=file.wav` (headless recording).

## Joysticks

- Game port emulation in `Input.c`: `inputJoystick(port, bits)` encodes the
  switches in JOYxDAT as the hardware does (right = X1, left = Y1, down =
  X1^X0, up = Y1^Y0), fire on CIA-A PRA bits 6/7, second button on POTGOR
  bits 10/14; `inputPort1Joystick()` puts a joystick in port 1 instead of
  the mouse. POTGOR now idles at $5500 (it read 0: second buttons and the
  right mouse button looked pressed until the first frame).
- CIA PRA writes only change the lines programmed as outputs (DDRA). Before,
  any write cleared bits 6-7: a program switching the power LED / audio
  filter left the left mouse button and joystick fire "pressed".
- Raspberry Pi: USB gamepads (port 2, second one optionally in port 1),
  keyboard joystick with redefinable keys, gamepad Start = chosen Amiga key,
  Select = disk menu, gamepad navigation in the menus; all in a page of the
  F5 menu, saved in OMEGA.CFG.
- Desktop: SDL game controllers (port 2; second one in port 1 with
  `OMEGA_PORT1=pad`), `OMEGA_KBJOY=1` for arrows + right Ctrl/Alt.

## Real floppy drive (DrawBridge)

- `DrawBridge.c`: client for Rob Smith's Arduino Floppy Disk Reader
  firmware 1.x (version handshake, motor, seek, side, track read, disk
  check; packed 2-bit flux codes decoded to MFM). The serial port comes from
  the host; on the Pi it is a Circle USB serial device (FTDI, 2 Mbaud).
- `Floppy.c`: a drive can play the disk in the real drive. A track is read
  when the Amiga first reads it and kept in a cache: one revolution is cut
  out of the ~1.1 turns read (a sector header found again one turn later),
  the seam is moved into the track gap, and sync words are aligned to word
  boundaries like Paula does (the `$AAAA $AAAA` preamble is kept next to the
  sync: Kickstart 1.3 checks it). AmigaDOS tracks with bad sectors are read
  again (up to 20 times, stopping after 6 reads without progress); every
  sector read correctly once is kept, and when all 11 are there the track is
  rebuilt clean. Reads start at once (no wait for the index hole); reads that
  arrive incomplete (bytes lost on the USB link) are counted; after an error
  the client reconnects instead of giving up. Read only (the disk is write
  protected for the Amiga).
- Disk menu: real drive diagnostics (reads tracks 0, 1, 80, 159 three times;
  time, bytes, bits, revolution, good sectors; DBTEST.TXT and DBRAW.BIN).
- Pi: F5 option "Floppy vero (DrawBridge)" (off / DF0: / DF1:), disk menu
  entry to insert the real disk, boots from it at power-on if a disk is in.
- Tested with a simulated Arduino speaking the real protocol
  (`OMEGA_DRAWBRIDGE_SIM`): Workbench 1.3 boots pixel-identical to the ADF,
  also with misread bits (one in 9,000 bits) or lost USB packets (one in
  100), after a reset and after a disk change; ADF boot and snapshots
  unchanged.

## Performance

- Table-driven planar→chunky conversion (one lookup per plane per 8 pixels).
- Host events polled once per frame instead of every 200 colour clocks.
- Result: 1500 frames of Workbench 1.3 in ~1.7 s instead of ~3.4 s on the
  same x86 machine (≈2× faster).

## Portability (bare metal preparation)

The emulator core no longer uses SDL or POSIX:

- `Host.h` is platform neutral. One backend is linked:
  - `HostSDL.c` – desktop (all SDL code is here)
  - `HostNull.c` – headless, no libraries; template for a Pi backend
- `Render.c` (framebuffer + pixel conversion) and `Input.c` (keyboard/mouse →
  CIA/Paula) are portable and shared.
- `main.c` loads files with `hostLoadFile()`; `ADF2MFM()` converts from memory.
- `SDL_atomic_t cpuWait` replaced with a plain `volatile int`.

A bare metal port now needs only a new `Host*.c` implementing 6 functions
(`hostInit`, `hostPollEvents`, `hostDisplay`, `hostShutdown`, `hostLoadFile`,
`toggleLEDs`) plus startup code, a framebuffer from the VideoCore mailbox,
a timer and USB HID input (e.g. via the Circle library).

## Raspberry Pi bare metal (new: `pi/`)

A Circle based backend (`pi/HostCircle.cpp`) runs Omega directly on a
Raspberry Pi 4 / 400 without an OS: 640x480 32-bit framebuffer, 59.94 Hz frame
pacing with the system timer, USB keyboard and mouse, Kickstart and ADF images
read from the SD card (`KICK.ROM`, `DF0.ADF`...), ACT LED = floppy motor.
The emulator core is shared unchanged with the desktop build. See
`pi/README-PI.md`. Built with clang 18 against Circle 51.1; not yet tested on
real hardware.

## Testing

`make headless` then:

    OMEGA_INSERT_AT=150 OMEGA_SHOT_AT=1500 OMEGA_SHOT_FILE=wb.ppm \
        ./omega-headless kick13.rom workbench13.adf

saves frame 1500 as a PPM. Also `OMEGA_KEYS_AT` / `OMEGA_KEYS` (hex raw codes)
to type keys. The core was also run under AddressSanitizer/UBSan with no
memory errors.

## Still open (unchanged)

- Kickstart 3.x does not reach the GUI (now runs without crashing, black screen)
- Only sprite 0, no horizontal DIW handling
- 68000 odd-address word accesses are aligned instead of raising an address error
