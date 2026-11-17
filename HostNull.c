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
#include <string.h>
#include "Host.h"
#include "HostHarness.h"
#include "Memory.h"
#include "Paula.h"
#include "Floppy.h"
#include "DrawBridge.h"

// ---------------------------------------------------------------------------
// OMEGA_DRAWBRIDGE_SIM=file.adf: a simulated DrawBridge Arduino with that disk
// in its drive, speaking the real serial protocol. Each track read starts at a
// random bit position (the index hole is anywhere relative to the data) and
// returns ~1.1 revolutions packed like the firmware does.
// OMEGA_DRAWBRIDGE_JITTER=N: drop one bit in N on average (misreads of a worn disk).

#include <time.h>

static uint8_t* simMfm = NULL;          // ADF2MFM layout: 12798 bytes per track, 12668 used
static int simCyl = 0, simSide = 0, simMotor = 0;
static uint8_t simOut[40000];
static int simOutHead = 0, simOutCount = 0;
static int simAwaitIndexByte = 0;
static char simCmd[4]; static int simCmdLen = 0;
static uint32_t simRand = 12345;
static int simReads = 0;

static uint32_t simNext(void){ simRand = simRand * 1103515245u + 12345u; return simRand >> 8; }
static void simPut(uint8_t b){ if(simOutCount < (int)sizeof simOut) simOut[simOutCount++] = b; }

static int simTrackBit(const uint8_t* t, int i){ return (t[i >> 3] >> (7 - (i & 7))) & 1; }

static void simReadTrack(void){
    const uint8_t* t = &simMfm[simCyl * 12798 * 2 + simSide * 12798];
    int rev = 12668 * 8;
    int pos = (int)(simNext() % (uint32_t)rev);         // where the index hole is
    int target = (0x1900*2 + 0x440) * 8;                // bits the firmware reads
    uint32_t jitter = getenv("OMEGA_DRAWBRIDGE_JITTER") ? (uint32_t)atoi(getenv("OMEGA_DRAWBRIDGE_JITTER")) : 0;
    int bits = 0, zeros = 0, codes = 0;
    uint8_t cur = 0;
    simReads++;
    while(bits < target){
        int b = simTrackBit(t, pos);
        pos = (pos + 1) % rev;
        if(jitter && (simNext() % jitter) == 0){ bits++; continue; }   // drop a bit now and then
        bits++;
        if(!b){ zeros++; continue; }
        while(zeros > 3){                                // "0000"
            cur = (uint8_t)(cur << 2); codes++; zeros -= 4;
            if(codes == 4){ simPut(cur ? cur : 0x55); cur = 0; codes = 0; }
        }
        int code = zeros ? zeros : 1;
        cur = (uint8_t)((cur << 2) | code); codes++; zeros = 0;
        if(codes == 4){ simPut(cur); cur = 0; codes = 0; }
    }
    if(codes){ while(codes < 4){ cur = (uint8_t)((cur << 2) | 1); codes++; } simPut(cur); }
    simPut(0);
}

static int simWrite(const void* data, int count){
    const uint8_t* d = (const uint8_t*)data;
    for(int i=0;i<count;i++){
        uint8_t c = d[i];
        if(simAwaitIndexByte){ simAwaitIndexByte = 0; simReadTrack(); continue; }
        if(simCmdLen){                      // '#' parameters
            simCmd[simCmdLen++] = (char)c;
            if(simCmdLen == 3){
                int cyl = (simCmd[1]-'0')*10 + (simCmd[2]-'0');
                if(cyl >= 0 && cyl < 82){ simCyl = cyl; simPut('1'); } else simPut('0');
                simCmdLen = 0;
            }
            continue;
        }
        switch(c){
            case 'x': break;
            case 'R': simMotor = 0; break;
            case '?': simPut('1'); simPut('V'); simPut('1'); simPut('.'); simPut('9'); break;
            case '+': simMotor = 1; simPut('1'); break;
            case '-': simMotor = 0; simPut('1'); break;
            case '#': simCmd[0] = '#'; simCmdLen = 1; break;
            case '[': simSide = 1; simPut('1'); break;
            case ']': simSide = 0; simPut('1'); break;
            case '^': simPut('1'); simPut('1'); break;
            case '<': if(simMotor){ simPut('1'); simAwaitIndexByte = 1; } else simPut('0'); break;
            default:  simPut('!'); break;
        }
    }
    return count;
}

static int simRead(void* data, int count){
    int n = simOutCount - simOutHead;
    if(n > count) n = count;
    if(n > 62) n = 62;                          // like FTDI packets
    // OMEGA_DRAWBRIDGE_LOSS=N: lose one packet in N during track reads (a USB link too slow)
    static int loss = -1;
    if(loss < 0) loss = getenv("OMEGA_DRAWBRIDGE_LOSS") ? atoi(getenv("OMEGA_DRAWBRIDGE_LOSS")) : 0;
    if(loss > 0 && n == 62 && simOutCount - simOutHead > 200 && (simNext() % (uint32_t)loss) == 0){
        simOutHead += n;                        // dropped
        n = 0;
        return 0;
    }
    memcpy(data, simOut + simOutHead, (size_t)n);
    simOutHead += n;
    if(simOutHead == simOutCount) simOutHead = simOutCount = 0;
    return n;
}

static uint32_t simMicros(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000ull + ts.tv_nsec / 1000);
}

static const DbPort simPort = { simWrite, simRead, simMicros };

static void simInit(const char* adfPath){
    static uint8_t adf[1802240];
    FILE* f = fopen(adfPath, "rb");
    if(!f){ fprintf(stderr, "DrawBridge sim: cannot open %s\n", adfPath); return; }
    long n = (long)fread(adf, 1, sizeof adf, f);
    fclose(f);
    simMfm = calloc(12798 * 82 * 2, 1);
    ADF2MFM(adf, (int)n, simMfm);
    // ADF2MFM leaves a few invalid clock bits ("11" where a sector follows a
    // data bit 1, and in the track gap); a disk written by an Amiga has none.
    // Recompute the clock bits, keeping the sync words.
    for(int t=0;t<164;t++){
        uint8_t* m = simMfm + t * 12798;
        static uint8_t keep[12668];
        memset(keep, 0, sizeof keep);
        for(int i=0;i+1<12668;i+=2) if(m[i]==0x44 && m[i+1]==0x89){ keep[i]=keep[i+1]=1; }
        for(int b=0;b<12668*8;b+=2){
            if(keep[b>>3]) continue;
            int prev = b > 0 ? (m[(b-1)>>3] >> (7-((b-1)&7))) & 1 : 0;
            int next = (m[(b+1)>>3] >> (7-((b+1)&7))) & 1;
            int clk = !(prev | next);
            if(clk) m[b>>3] |= (uint8_t)(0x80 >> (b&7)); else m[b>>3] &= (uint8_t)~(0x80 >> (b&7));
        }
    }
    if(dbOpen(&simPort)){
        fprintf(stderr, "DrawBridge sim: found %s\n", dbVersion());
        floppySetTrackReader(dbReadTrack);
    }else{
        fprintf(stderr, "DrawBridge sim: not found\n");
    }
}

static void simReport(void){
    if(simMfm) fprintf(stderr, "DrawBridge sim: %d tracks read by the drive, %d read errors, %d incomplete\n", dbTracksRead, dbReadErrors, dbDataLost);
}

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
    if(getenv("OMEGA_DRAWBRIDGE_SIM")){
        simInit(getenv("OMEGA_DRAWBRIDGE_SIM"));
        atexit(simReport);
    }
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
    if(simMfm) dbIdle();
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
