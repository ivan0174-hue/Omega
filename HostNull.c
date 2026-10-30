//
//  HostNull.c
//  The Omega Project
//
//  Headless host backend: no window, no input, no external libraries.
//  Used for automated tests (see HostHarness.h) and as the starting point
//  for a Raspberry Pi bare metal backend, where:
//    - hostInit()       -> mailbox framebuffer setup, timers, USB/HID
//    - hostDisplay()    -> copy/scale host.pixels into the GPU framebuffer
//    - hostPollEvents() -> read USB keyboard/mouse, call inputKey*/inputMouse*
//    - hostLoadFile()   -> read from the SD card (e.g. with FatFs)
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <stdio.h>
#include <stdlib.h>
#include "Host.h"
#include "HostHarness.h"
#include "Memory.h"
#include "Paula.h"

// OMEGA_WAV=file.wav: record the audio output (48 kHz stereo 16 bit)
static FILE* wavFile = NULL;
static long wavFrames = 0;

static void put32(uint8_t* p, uint32_t v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

static void wavFinish(void){
    if(!wavFile) return;
    uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
                     0,0,0,0,0,0,0,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    put32(h+4, 36 + wavFrames*4); put32(h+24, PAULA_DEFAULT_RATE); put32(h+28, PAULA_DEFAULT_RATE*4);
    put32(h+40, wavFrames*4);
    fseek(wavFile, 0, SEEK_SET); fwrite(h, 1, 44, wavFile); fclose(wavFile); wavFile = NULL;
}

static void wavDrain(void){
    int16_t buf[2048*2];
    int n;
    while(wavFile && (n = paulaRead(buf, 2048)) > 0){
        fwrite(buf, 4, n, wavFile);
        wavFrames += n;
    }
}

static int quitRequested = 0;

int hostInit(void){
    // OMEGA_CHIP / OMEGA_SLOW: memory in KB (default 512 + 512, an Amiga 500 with trapdoor expansion)
    if(getenv("OMEGA_CHIP") || getenv("OMEGA_SLOW")){
        memoryConfigure((getenv("OMEGA_CHIP") ? atoi(getenv("OMEGA_CHIP")) : 512) * 1024,
                        (getenv("OMEGA_SLOW") ? atoi(getenv("OMEGA_SLOW")) : 512) * 1024);
    }
    if(getenv("OMEGA_WAV")){
        wavFile = fopen(getenv("OMEGA_WAV"), "wb");
        if(wavFile){ uint8_t z[44] = {0}; fwrite(z, 1, 44, wavFile); atexit(wavFinish); }
    }
    paulaSetEnabled(wavFile != NULL);
    if(getenv("OMEGA_ATTACH_ALL")){     // test: 4 drives like the Raspberry Pi build
        for(int i=1;i<4;i++) floppyAttach(i);
    }
    return 1;
}

int hostPollEvents(void){
    return !quitRequested;
}

void hostDisplay(void){
    wavDrain();
    if(harnessFrame()){
        quitRequested = 1;
        exit(0);    // frame captured; stop immediately so later frames don't run
    }
}

void hostShutdown(void){
}

void toggleLEDs(void){
}

long hostLoadFile(const char* path, uint8_t* buffer, long maxSize){
    FILE* f = fopen(path, "rb");
    if(f == NULL){
        return -1;
    }
    long n = (long)fread(buffer, 1, (size_t)maxSize, f);
    fclose(f);
    return n;
}
