//
//  HostHarness.h
//  The Omega Project
//
//  Headless regression harness shared by the desktop backends (not used on
//  bare metal). Controlled by environment variables:
//
//    OMEGA_INSERT_AT=<frame>   insert the disk in DF0: at that frame
//    OMEGA_KEYS_AT=<frame>     type OMEGA_KEYS (Amiga raw codes, hex, comma
//                              separated) starting at that frame
//    OMEGA_SHOT_AT=<frame>     save the display to OMEGA_SHOT_FILE (PPM) and exit
//    OMEGA_SHOT_FILE=<path>
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef HostHarness_h
#define HostHarness_h

#include <stdio.h>
#include <stdlib.h>
#include "Host.h"
#include "Floppy.h"

static int harnessEnvInt(const char* name){
    const char* v = getenv(name);
    return v ? atoi(v) : -1;
}

// OMEGA_TESTSCREEN_AT=<frame> OMEGA_TESTSCREEN_MOD=<bytes>: take over the display
// like a game does: own copper list, lores 320x200, 5 bitplanes (32 colours),
// interleaved bitplanes. MOD=160 is the normal interleaved layout, MOD=120
// with LINES=2 shows each line twice using a negative modulo trick on odd lines
// is not used here; the pattern is colour = ((x>>3) ^ (y>>2)) & 31.
#include "Memory.h"
#include "Chipset.h"
#include "Snapshot.h"
#include "CPU.h"

static int harnessFileWriter(const void* data, size_t size, void* context){
    return fwrite(data, 1, size, (FILE*)context) == size;
}
// OMEGA_TESTSCREEN_SCROLL=<0..15>: hardware scrolling like games do: picture
// 336 pixels wide (21 words), DDFSTRT $30 (one extra fetch), BPLCON1 delay.
// The visible x=0 then shows picture column 16 - delay.
static void harnessScrollScreen(int delay){
    const int words = 21, bpr = words*2, stride = bpr*5, mod = stride - bpr;
    uint32_t base = 0x20000, cop = 0x10000;
    for(uint32_t i=0;i<0x40000;++i) low16Meg[base+i]=0;
    for(int y=0;y<200;++y){
        for(int x=0;x<words*16;++x){
            int c = ((y*3) + (x>>2)) & 31;
            for(int p=0;p<5;++p){
                if(c & (1<<p)){
                    low16Meg[base + y*stride + p*bpr + (x>>3)] |= (uint8_t)(0x80 >> (x&7));
                }
            }
        }
    }
    uint32_t a = cop;
    #define CMOVE(reg,val) do{ low16Meg[a]=(uint8_t)((reg)>>8); low16Meg[a+1]=(uint8_t)(reg); \
        low16Meg[a+2]=(uint8_t)((val)>>8); low16Meg[a+3]=(uint8_t)(val); a+=4; }while(0)
    for(int p=0;p<5;++p){
        uint32_t pt = base + p*bpr;
        CMOVE(0xE0 + p*4, pt >> 16);
        CMOVE(0xE2 + p*4, pt & 0xFFFF);
    }
    CMOVE(0x100, 0x5200);
    CMOVE(0x102, (delay << 4) | delay);
    CMOVE(0x104, 0);
    CMOVE(0x108, mod);
    CMOVE(0x10A, mod);
    CMOVE(0x08E, 0x2C81);
    CMOVE(0x090, 0xF4C1);
    CMOVE(0x092, 0x0030);
    CMOVE(0x094, 0x00D0);
    for(int i=0;i<32;++i){
        int r = (i*5)&15, g = (i*3+7)&15, b = (15-i/2)&15;
        CMOVE(0x180 + i*2, (r<<8)|(g<<4)|b);
    }
    CMOVE(0xFFFF, 0xFFFE);
    #undef CMOVE
    putChipReg16[INTENA](0x7FFF);
    putChipReg16[DMACON](0x7FFF);
    putChipReg16[COP1LCH](cop >> 16);
    putChipReg16[COP1LCH+1](cop & 0xFFFF);
    putChipReg16[COPJMP1](0);
    putChipReg16[DMACON](0x8380);
}

static void harnessTestScreen(void){
    if(getenv("OMEGA_TESTSCREEN_SCROLL")){
        harnessScrollScreen(atoi(getenv("OMEGA_TESTSCREEN_SCROLL")));
        return;
    }
    const char* m = getenv("OMEGA_TESTSCREEN_MOD");
    int mod = m ? atoi(m) : 160;           // bytes added after each line
    int stride = 40 + mod;                 // bytes from one line of a plane to the next
    int planeOffset = (mod == 0) ? 8000 : 40;  // separate planes, or interleaved
    uint32_t base = 0x20000, cop = 0x10000;
    for(uint32_t i=0;i<0x40000;++i) low16Meg[base+i]=0;
    for(int y=0;y<200;++y){
        for(int x=0;x<320;++x){
            int c = getenv("OMEGA_TESTSCREEN_P2") ? ((y*7 + (x>>4)) & 31) : (((x>>3) ^ (y>>2)) & 31);
            for(int p=0;p<5;++p){
                if(c & (1<<p)){
                    low16Meg[base + y*stride + p*planeOffset + (x>>3)] |= (uint8_t)(0x80 >> (x&7));
                }
            }
        }
    }
    uint32_t a = cop;
    #define CMOVE(reg,val) do{ low16Meg[a]=(uint8_t)((reg)>>8); low16Meg[a+1]=(uint8_t)(reg); \
        low16Meg[a+2]=(uint8_t)((val)>>8); low16Meg[a+3]=(uint8_t)(val); a+=4; }while(0)
    for(int p=0;p<5;++p){
        uint32_t pt = base + p*planeOffset;
        CMOVE(0xE0 + p*4, pt >> 16);
        CMOVE(0xE2 + p*4, pt & 0xFFFF);
    }
    CMOVE(0x100, 0x5200);
    CMOVE(0x102, 0);
    CMOVE(0x104, 0);
    CMOVE(0x108, mod & 0xFFFF);
    CMOVE(0x10A, mod & 0xFFFF);
    CMOVE(0x08E, 0x2C81);
    CMOVE(0x090, 0xF4C1);
    CMOVE(0x092, 0x0038);
    CMOVE(0x094, 0x00D0);
    for(int i=0;i<32;++i){
        int r = (i*5)&15, g = (i*3+7)&15, b = (15-i/2)&15;
        CMOVE(0x180 + i*2, (r<<8)|(g<<4)|b);
    }
    CMOVE(0xFFFF, 0xFFFE);
    #undef CMOVE
    putChipReg16[INTENA](0x7FFF);
    putChipReg16[DMACON](0x7FFF);
    putChipReg16[COP1LCH](cop >> 16);
    putChipReg16[COP1LCH+1](cop & 0xFFFF);
    putChipReg16[COPJMP1](0);
    putChipReg16[DMACON](0x8380);
}

// returns 1 when the emulator should exit
static int harnessFrame(void){
    static int frames = 0;
    frames++;

    if(frames == harnessEnvInt("OMEGA_INSERT_AT")){
        floppyInsert(0);
    }

    // OMEGA_EJECT_AT=<frame>: eject DF0:
    // OMEGA_SNAPSHOT=<file> [OMEGA_SNAPSHOT_AT=<frame>, default 2]: restore a snapshot
    // (e.g. one saved with F12 on the Raspberry Pi)
    if(getenv("OMEGA_SNAPSHOT") && frames == (harnessEnvInt("OMEGA_SNAPSHOT_AT") > 0 ? harnessEnvInt("OMEGA_SNAPSHOT_AT") : 2)){
        FILE* f = fopen(getenv("OMEGA_SNAPSHOT"),"rb");
        int ok = 0;
        if(f){
            static uint8_t snap[8*1024*1024];
            size_t n = fread(snap, 1, sizeof(snap), f);
            fclose(f);
            ok = snapshotLoad(snap, n);
        }
        fprintf(stderr, "snapshot %s: %s\n", getenv("OMEGA_SNAPSHOT"), ok ? "loaded" : "FAILED");
    }

    // OMEGA_SAVE_SNAPSHOT_AT=<frame> OMEGA_SAVE_SNAPSHOT_FILE=<file>
    if(getenv("OMEGA_SAVE_SNAPSHOT_FILE") && frames == harnessEnvInt("OMEGA_SAVE_SNAPSHOT_AT")){
        FILE* f = fopen(getenv("OMEGA_SAVE_SNAPSHOT_FILE"),"wb");
        if(f){ snapshotSave(harnessFileWriter, f); fclose(f); }
    }

    if(frames == harnessEnvInt("OMEGA_TESTSCREEN_AT")){
        harnessTestScreen();
    }

    // OMEGA_MEMCHANGE_AT=<frame> OMEGA_CHIP2/OMEGA_SLOW2 (KB): change memory, then reset
    // (what the Raspberry Pi settings menu does)
    if(frames == harnessEnvInt("OMEGA_MEMCHANGE_AT")){
        memoryConfigure(harnessEnvInt("OMEGA_CHIP2") * 1024, harnessEnvInt("OMEGA_SLOW2") * 1024);
        cpu_pulse_reset();
    }

    // OMEGA_RESET_AT=<frame>: Ctrl+Amiga+Amiga
    if(frames == harnessEnvInt("OMEGA_RESET_AT")){
        inputKeyDown(0x63); inputKeyDown(0x66); inputKeyDown(0x67);
        inputKeyUp(0x63); inputKeyUp(0x66); inputKeyUp(0x67);
    }

    if(frames == harnessEnvInt("OMEGA_EJECT_AT")){
        floppyEjectDisk(0);
    }

    // OMEGA_LOAD_AT=<frame> OMEGA_LOAD_FILE=<adf>: load an image into DF0: while running
    // and insert it (retrying until the drive accepts it), like the Pi disk menu does
    static int pendingInsert = 0;
    if(frames == harnessEnvInt("OMEGA_LOAD_AT") && getenv("OMEGA_LOAD_FILE")){
        static uint8_t image[82*22*512];
        FILE* f = fopen(getenv("OMEGA_LOAD_FILE"),"rb");
        if(f){
            int n = (int)fread(image,1,sizeof(image),f);
            fclose(f);
            if(floppyLoadImage(0,image,n)) pendingInsert = 1;
        }
    }
    if(frames == harnessEnvInt("OMEGA_LOAD2_AT") && getenv("OMEGA_LOAD2_FILE")){
        static uint8_t image2[82*22*512];
        FILE* f = fopen(getenv("OMEGA_LOAD2_FILE"),"rb");
        if(f){
            int n = (int)fread(image2,1,sizeof(image2),f);
            fclose(f);
            if(floppyLoadImage(0,image2,n)) pendingInsert = 1;
        }
    }
    // OMEGA_LIVE_AT=<frame>: insert the real disk (DrawBridge, see HostNull.c) in DF0:
    if(frames == harnessEnvInt("OMEGA_LIVE_AT") && getenv("OMEGA_DRAWBRIDGE_SIM")){
        floppyEjectDisk(0);
        if(floppyLoadLive(0)) pendingInsert = 1;
        fprintf(stderr, "frame %d: real disk in DF0:\n", frames);
    }
    if(pendingInsert && floppyInsertDisk(0)){
        pendingInsert = 0;
    }

    if(frames == harnessEnvInt("OMEGA_KEYS_AT")){
        const char* keys = getenv("OMEGA_KEYS");
        while(keys && *keys){
            char* end;
            long code = strtol(keys, &end, 16);
            if(end == keys) break;
            inputKeyDown((uint8_t)code);
            inputKeyUp((uint8_t)code);
            keys = (*end == ',') ? end + 1 : end;
        }
    }

    // OMEGA_CYCLES_FROM=<frame>: print the average CPU cycles per frame at the screenshot
    static unsigned long long cyclesAtStart = 0;
    if(frames == harnessEnvInt("OMEGA_CYCLES_FROM")){
        extern unsigned long long cpuCyclesTotal;
        cyclesAtStart = cpuCyclesTotal;
    }

    const char* file = getenv("OMEGA_SHOT_FILE");
    if(file && frames == harnessEnvInt("OMEGA_SHOT_AT")){
        if(harnessEnvInt("OMEGA_CYCLES_FROM") > 0){
            extern unsigned long long cpuCyclesTotal;
            int n = frames - harnessEnvInt("OMEGA_CYCLES_FROM");
            fprintf(stderr, "CPU cycles per frame: %llu (A500 PAL: %d)\n",
                    (cpuCyclesTotal - cyclesAtStart) / (n > 0 ? n : 1), 313*227*2);
        }
        FILE* f = fopen(file,"wb");
        if(f){
            fprintf(f,"P6\n%d %d\n255\n", FB_WIDTH, FB_VISIBLE_HEIGHT);
            for(int i=0;i<FB_WIDTH*FB_VISIBLE_HEIGHT;++i){
                uint32_t p = host.pixels[i];
                fputc((p>>16)&255,f); fputc((p>>8)&255,f); fputc(p&255,f);
            }
            fclose(f);
        }
        return 1;
    }
    return 0;
}

#endif
