//
//  Host.h
//  The Omega Project
//  https://github.com/h5n1xp/Omega
//
//  Created by Matt Parsons on 02/02/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//  <h5n1xp@gmail.com>
//
//
//  This Source Code Form is subject to the terms of the
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
//  2026 revision: this header is now platform neutral. The emulator core only
//  sees the functions below; each platform supplies ONE host backend:
//
//      HostSDL.c   - desktop (SDL2): window, keyboard, mouse, file loading
//      HostNull.c  - headless, no external libraries (tests, and the template
//                    for a Raspberry Pi bare metal backend)
//
//  Everything else (CPU, chipset, DMA, blitter, CIA, floppy, Render.c,
//  Input.c, main.c) uses no SDL and no POSIX calls.

#ifndef Host_h
#define Host_h

#include <stdint.h>

// Emulated display: the core renders ARGB8888 pixels into this buffer.
// Only the first 640 x FB_VISIBLE_HEIGHT lines are shown at the moment.
#define FB_WIDTH          640
#define FB_HEIGHT         400
#define FB_VISIBLE_HEIGHT 256   // PAL: lines 44-299

typedef struct{
    int FBCounter;      // write position in the framebuffer, reset every VBL
    int vblCount;       // incremented once per emulated frame
    uint32_t* pixels;   // FB_WIDTH * FB_HEIGHT ARGB pixels, owned by Render.c
}Host_t;

extern Host_t host;

//---------------- Rendering helpers (Render.c, portable) ----------------
void renderInit(void);
void hiresPlanar2Chunky(uint32_t* pixBuff,uint32_t* palette,uint16_t plane1, uint16_t plane2,uint16_t plane3,uint16_t plane4);
void loresPlanar2Chunky(uint32_t* pixBuff,uint32_t* palette,uint16_t plane1, uint16_t plane2,uint16_t plane3,uint16_t plane4,uint16_t plane5,uint16_t plane6);
void loresHAM2Chunky(uint32_t* pixBuff,uint32_t* palette,uint16_t plane1, uint16_t plane2,uint16_t plane3,uint16_t plane4,uint16_t plane5,uint16_t plane6);
void sprite2chunky(uint32_t* pixBuff,uint32_t* palette, int x,uint16_t plane1, uint16_t plane2,int delta);

//---------------- Input (Input.c, portable) ----------------
// Keys use Amiga raw key codes (0x00-0x67). Events are queued and handed to
// the keyboard CIA one per frame, so fast typing is not lost.
void inputKeyDown(uint8_t amigaKey);
void inputKeyUp(uint8_t amigaKey);
void inputMouseMove(int dx, int dy);            // in Amiga mouse counts
void inputMouseButtons(int left, int right);
// Joysticks. port 0 = Amiga port 1 (mouse port), port 1 = Amiga port 2 (joystick port)
#define JOY_UP      1
#define JOY_DOWN    2
#define JOY_LEFT    4
#define JOY_RIGHT   8
#define JOY_FIRE    16
#define JOY_FIRE2   32
void inputJoystick(int port, unsigned bits);
void inputPort1Joystick(int on);                // 1: joystick in port 1 instead of the mouse
void inputFrame(void);
void inputReset(void);                          // forget queued keys (machine reset)                          // called once per emulated frame

//---------------- Backend (HostSDL.c / HostNull.c / bare metal) ----------------
int  hostInit(void);                    // 0 on failure
int  hostPollEvents(void);              // translate host input; returns 0 to quit
void hostDisplay(void);                 // called by the core at every VBL
void hostShutdown(void);
long hostLoadFile(const char* path, uint8_t* buffer, long maxSize);   // bytes read, -1 on error
void toggleLEDs(void);

#endif /* Host_h */
