//
//  main.c
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
//  2026 revision: platform neutral. Files are loaded through hostLoadFile()
//  and events through hostPollEvents(), so this file needs no SDL and no
//  POSIX calls. Host events are now polled once per emulated frame instead of
//  every 200 colour clocks (~300 times per frame).

#include <stdio.h>
#include <stdint.h>

#include "CPU.h"
#include "Host.h"
#include "Memory.h"
#include "DMA.h"
#include "Chipset.h"
#include "CIA.h"
#include "Floppy.h"
#include "m68k.h"

int screenWidth = 640;
int screenHeight = 200;

int disass = 0; //set to any value greater than 0 to turn on the disassembler

#define ROM_SIZE_MAX (512*1024)
#define ADF_SIZE_MAX (82*22*512)    // the largest image the MFM buffer can hold

static uint8_t adfBuffer[ADF_SIZE_MAX + 1];

static int loadKickstart(const char* path){
    long size = hostLoadFile(path, &low16Meg[0xF80000], ROM_SIZE_MAX);
    if(size <= 0){
        printf("Can't read Kickstart ROM %s\n", path);
        return 0;
    }

    if(size == 0x40000){
        // 256K ROMs are mirrored at 0xF80000 and 0xFC0000
        for(long i=0;i<size;++i){
            low16Meg[0xFC0000 + i] = low16Meg[0xF80000 + i];
        }
    }else if(size != 0x80000){
        printf("Unexpected ROM size %ld (need 256K or 512K)\n", size);
        return 0;
    }

    if(low16Meg[0xF80000] != 0x11){
        printf("Not an Amiga ROM\n");
        return 0;
    }
    printf("ROM loaded\n");
    return 1;
}

static void loadDisk(int drive, const char* path){
    long size = hostLoadFile(path, adfBuffer, sizeof(adfBuffer));
    if(size <= 0){
        printf("No disk in DF%d: (%s)\n", drive, path);
        return;
    }
    if(size > ADF_SIZE_MAX){
        printf("DF%d: %s is larger than a DD floppy, truncated\n", drive, path);
        size = ADF_SIZE_MAX;
    }
    ADF2MFM(adfBuffer, (int)size, floppyInit(drive));
    printf("DF%d: %s loaded (press F%d to insert disk)\n", drive, path, drive+1);
}

int main(int argc, char * argv[]) {

    if(argc < 2){
        printf("Usage: omega <kickstart.rom> [df0.adf] [df1.adf] [df2.adf] [df3.adf]\n");
        return 1;
    }

    renderInit();

    if(!loadKickstart(argv[1])){
        return 1;
    }

    floppyAttach(0);    // the internal drive is always there, even without a disk
    for(int drive=0; drive<4 && argc > drive+2; ++drive){
        loadDisk(drive, argv[drive+2]);
    }

    if(!hostInit()){
        return 1;
    }
    cpu_init();
    ChipsetInit();

    printf("\nEntering main loop\n");

    int running = 1;
    while(running){

        // run until the chipset reaches the next vertical blank
        int frame = host.vblCount;
        while(host.vblCount == frame){
            for(int cycles=0; cycles<200; ++cycles){
                dma_execute();
                cpu_execute();
            }
        }

        running = hostPollEvents();
    }

    hostShutdown();
    return 0;
}
