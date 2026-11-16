# Omega per Raspberry Pi 4 (fork)

Fork di [h5n1xp/Omega](https://github.com/h5n1xp/Omega), un emulatore Amiga
500, con una versione **bare metal per Raspberry Pi 4 / 400**: niente
Linux, il Pi si accende direttamente come un Amiga.

**Per provarlo:** copia il contenuto della cartella [`pi/sd`](pi/sd) su una
scheda SD formattata FAT32, aggiungi la tua ROM Kickstart chiamata
`KICK.ROM` e i tuoi file `.adf`, poi accendi il Pi. Istruzioni complete in
[`pi/sd/LEGGIMI.txt`](pi/sd/LEGGIMI.txt). Kickstart e dischi sono protetti
da copyright e non sono inclusi.

Novità rispetto all'originale (dettagli in [CHANGES-2026.md](CHANGES-2026.md)):

- velocità di un Amiga 500 PAL (7,09 MHz, 50 Hz)
- grafica dei giochi corretta: finestra di fetch dei bitplane, modulo,
  scroll, tempi del copper, area fill e line mode del blitter
- audio (Paula) su HDMI o jack, con il filtro dell'A500
- joystick: gamepad USB e joystick da tastiera con tasti configurabili
- floppy vero: legge i dischi originali con un
  [DrawBridge](https://github.com/RobSmithDev/ArduinoFloppyDiskReader) (Arduino) collegato alla USB
- menu dischi (F1-F4), impostazioni (F5: memoria chip/slow, audio,
  joystick), istantanee (F12), reset completo
- correzioni di crash e di memoria; la versione desktop SDL e quella
  "headless" per i test restano disponibili

Compilare la versione per Pi: [`pi/README-PI.md`](pi/README-PI.md).

---

# Omega


### Getting Started (quickly).

Omega only needs a basic install of libSDL 2 for the desktop build. To build on Linux/macOS:

    make            # SDL2 desktop build -> ./omega
    make headless   # no external libraries at all -> ./omega-headless

(Building by hand: compile every .c file except HostNull.c and link with -lSDL2; for the headless build use HostNull.c instead of HostSDL.c.)

Run it with a kickstart ROM and up to 4 ADF images:

    ./omega ./kick13.rom ./workbench.adf

Any KS1.x or KS2.x rom will work (256k or 512k), Currently Kickstart 3.x is unable to boot to GUI.

Once the emulator is running, hit F1 to "insert" the adf image into DF0: the emulator will now boot the disk. 
Currently the emulator is able to boot to Workbench 1.3 (up to 4 ADF can be specified in the command line), to trigger a disk insertion event, press the associated F key (F1 for DF0: | F2 for DF1: | F3 for DF2: | F4 for DF3:). A small graphic of the inserted disk will show in the bottom left corner of the screen (the grpahic shows the spin of each disk, and drive head position). Press F5 to toggle the on screen graphics. The key to the right of the left-shift key (or closing the window) quits. Amiga F6-F10 work normally, F11/Insert is HELP, PageUp/PageDown act as the Amiga keys on PC keyboards without Windows keys.

A Raspberry Pi 4 bare metal build (no OS) is in the `pi/` folder, see `pi/README-PI.md`.

See CHANGES-2026.md for the 2026 revision (bug fixes, blitter fill, host layer split for bare metal).

### Background.

This project started around December 2017, when I was doing some bare metal coding on the RaspberryPi. 
After getting a basic kernel booting I realised I had no software to run, the killer of all hobbyOS projects. So my mind wandered what would it be like if the RaspberryPI had a 68000 CPU instead of an ARM? 
There are plenty of popular "old school" 68k operating systems out there (TOS, Classic Mac, AmigaOS) with lots of great software, why can't I run these on the raspberryPi without a host OS?

After the birth of my daughter, I found I had some free time so I set about trying to make a RaspberryPi kernel which was nothing but a 68000 emulator running on the bare metal, with full access to the RaspberryPi address space. 
I decided to use the Musashi 68k emulator as the basis as I had used it in another project so had the source code working in "raw C" (no support libraries or host OS functions) and knew the emulator well. As the project developed, I realised that since I needed a set of functions to do the byte swapping to and from the memory there was no reason why I couldn't trap calls to certan addresses an emulate extra hardware functionality required to get an old 68k OS booting.

This is how the Omega project was born, although I initially called it Zorro (after one of the Commodore Amiga prototypes). Since the only real hardware coding I have done on a 68k machine was with the Amiga, that is the machine I knew best to emulate.
In retrospect choosing a simpler machine (like the Atari ST or classic Mac) would probably have been a better idea, as Amiga emulators are difficult to get working and nothing will ever come close to the functionlity of (Win)UAE. It seems unlikely there will be any more Amiga emulators, this could well be the last.. So I decided that a half pun, half tongue in cheek name of Omega was probably better than Zorro (which has other meanings in the Amiga world).

The current code base is a mixture of the original "Zorro" project with one coding style which was hacked together to "just work"(tm) and the project rewrite with a different coding style, making the code very messy. I hope to clean this up over time.


### Architecture.

The emulator is actually very simple. It is built around a core set of functions which manage memory access to the lower 16 megabytes of the system address space (For the time being the upper address space isn't accesible, since that adds a whole level of complexity). Within this 16meg space, there are four descrete areas, the 'Chipram", the "CIA" chips, the custom "chipset" registers, and the ROM. Almost all memory access to this space is big endian, where my plan is that the upper address space will be native endian. Right now the chipset registers are stored as a seprate structure (but in future I will probably allocate the memory for them in the actual 16meg space). The memory part is from the original project and is a mess, but kept since it works (subsequent rewrites haven't worked).

The next part is the "chipset" register trap, a set of functions which trap read/writes to any address between 0xDFF000 and 0xDFF1FF and implements any special hardware function located at that address. For example a write to address 0xDFF09C needs to write a bit pattern to address 0xDFF01E and potentially raise an IRQ on the 68K. This is imlmented as a set of function tables with specific functions for different sized accesses (8bit, 16bit, 32bit). Since the data written to these registers is used by the extensively by the emulator internally, the are kept in native endian byte ordering for efficency. 
The "CIA" chips are similar to the chipset part, but also require a periodic function since they implement timers and are able to raise interrupts.

The next part is the DMA sequencer. This should have been the core of the project, but it wasn't originally part of my design. This is fundamental to how the Amiga works, again implemeneted as a single function table containg 227 funcitions, a counter steps through each function, advanced every system cycle wrapping back to 0 when it reaches the end. There are "odd" and "even" cycles. "Odd" cycles have specific hardware functions mostly related to display generation. The "Even" cycles currently implement "the copper", a simple 3 instruction processor which manages the display, these cycles will also need to implement "the blitter", a simple gfx processor. Currently the blitter is a single function part of the "chipset" functions which once all the required registers have been written to, then performs the blitter operation, in a single "system cycle" (in other Amiga emulators this is known as "immediate mode").
The 68k runs largely independantly of the other parts, currently stepping through CPU cycles at the same rate as the DMA, interacting via the chipset and interrupts. 


### Design Goals.

Currently I'm using SDL 2 as my host layer, this allows quick and easy development, but I eventually want to run this on my RaspberryPI as a baremetal emulator (also perhaps for integration into NG AmigaOS projects like AROS) so there are conditions as to what can be added to the code: 
1. The Emulator should use no external libraries, or support functions. 
2. All I/O must go through a "Host layer", therefore this will be the only part to need a rewrite when porting.
3. C only... I originally missed the functionality of C++ so much, that I used an object oriented design pattern, but this became unnecessarily cumbersome to implement in C, so the rewrite changed this to a more straight forward data structure/function approach. Older parts of the emulator still have vestiges of the older design which will eventually be removed. Unfortuanlley at the moment there is a mix of coding styles. I will do my best to clean this up.
4. I'm not wedded to the Musashi 68K emulator, and would be happy to replace it with something else if a suitable replacement is available.
5. The primary target for emulation is AmigaOS legal applications, therefore the hardardware emulation does not need to be as precise as the amazing (Win)UAE, and ultimately I don't *plan* to support anything more advanced than ECS... 
6. There are two "threads" of execution here, the DMA and the CPU. When running baremetal, I would be thinking about running the DMA in a timed interrupt.

### Issues.

1. The Emulator can bootstrap Amiga Kickstart ROMs 1.x and 2.x, both can open an intuiton screen with a working mouse and keyboard! But it is unable to boot ROM 3.0 to GUI. KS2.x and KS3.x are unable to show the "insert disk" screen https://youtu.be/j0ZXrPQ41BU 
2. The blitter works (Copy mode and Line mode), and works well enough to generate graphics, I also want to implement this as a DMA operation. Missing are the "single pixel per scanline line mode, and the inclusive and exclusive fill modes.
3. There is no real sprite emulation, sprite 0 is sort of emulated (it isn't generated by the DMA, but during the VBL). This is suffient to implement the mouse pointer and interact with the operating system. It only supports 16 by 16 sprites using whatever colours the palette is set to at the end of the screen refresh. 
4. The display is based upon whatever the DMA has fetched for the frame, if no bitplane data is fetched, nothing is displayed, my earlier design would just draw colour00 pixels, which is more "correct" from a visual behaviour point of view, but I felt it wasn't really adding anything...
5. The horisonal values in diwstrt and diwstop are ignored, the pixels are drawn as fetched... So currently no overscan, underscan, or horisonal screen scrolling (which depends upon the display window) is supported. 

### Licence.

I have decided to release the code under MPL 2.0 licence.
