#include "Paula.h"
//
//  DMA.c
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

#include "DMA.h"
#include "Memory.h"
#include "Chipset.h"
#include "CIA.h"
#include "Host.h"
#include "Blitter.h"

#include "CPU.h"


#include "debug.h"

#include "Floppy.h"







































































void (*DMALores[])() = {
    evenCycle,
    dramCycle,
    evenCycle,
    dramCycle,
    evenCycle,
    dramCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    audio0Cycle,
    evenCycle,
    audio1Cycle,
    evenCycle,
    audio2Cycle,
    evenCycle,
    audio3Cycle,
    evenCycle,
    spriteCycle,    //00
    evenCycle,
    spriteCycle,    //00
    evenCycle,
    spriteCycle,    //01
    evenCycle,
    spriteCycle,    //01
    evenCycle,
    spriteCycle,    //02
    evenCycle,
    spriteCycle,    //02
    evenCycle,
    spriteCycle,    //03
    evenCycle,
    spriteCycle,    //03
    evenCycle,
    spriteCycle,    //04
    evenCycle,
    spriteCycle,    //04
    evenCycle,
    spriteCycle,    //05
    evenCycle,
    spriteCycle,    //05
    evenCycle,
    spriteCycle,    //06
    evenCycle,
    spriteCycle,    //06
    evenCycle,
    spriteCycle,    //07
    evenCycle,
    spriteCycle,    //07
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,   //Normal start
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,

};


void (*DMAHires[]) (void) = {
    evenCycle,
    dramCycle,
    evenCycle,
    dramCycle,
    evenCycle,
    dramCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    audio0Cycle,
    evenCycle,
    audio1Cycle,
    evenCycle,
    audio2Cycle,
    evenCycle,
    audio3Cycle,
    evenCycle,
    spriteCycle,//00
    evenCycle,
    spriteCycle,//00
    evenCycle,
    spriteCycle,//01
    evenCycle,
    spriteCycle,//01
    evenCycle,
    spriteCycle,//02
    evenCycle,
    spriteCycle,//02
    evenCycle,
    spriteCycle,//03
    evenCycle,
    spriteCycle,//03
    evenCycle,
    spriteCycle,//04
    evenCycle,
    spriteCycle,//04
    evenCycle,
    spriteCycle,//05
    evenCycle,
    spriteCycle,//05
    evenCycle,
    spriteCycle,//06
    evenCycle,
    spriteCycle,//06
    evenCycle,
    spriteCycle,//07
    evenCycle,
    spriteCycle,//07
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,    //Normal Plane Start
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
};


void waitFreeSlot(){
    
    
    
    while(cpuWait){
        // only used with THREADED_CPU
    }
    
}

int lastFetchCycle;
int firstFetchCycle;

// CPU/DMA handshake for the (experimental) threaded CPU mode. Plain volatile
// int instead of SDL_atomic_t so the core does not depend on SDL.
volatile int cpuWait = 0;

// set when bitplane data was fetched on the current line
static int bitplaneLineFetched = 0;

static void scrollReset(void);

// Each emulated line is drawn on its own framebuffer row (line 44 = row 0),
// starting at a position given by DDFSTRT (standard: lores 0x38, hires 0x3C).
// Previously all lines were drawn as one continuous stream of pixels.
#define FIRST_DISPLAY_LINE 44
static int renderLine = -1;     // emulated line currently being drawn
static int rowStartPx = 0;      // framebuffer index of the start of its row

// Rows without bitplane data show the background colour (COLOR00), as the
// border does on a real Amiga, instead of keeping old pixels.
static void fillRowBackground(int line){
    int row = line - FIRST_DISPLAY_LINE;
    if(host.pixels == NULL || row < 0 || row >= FB_HEIGHT){
        return;
    }
    uint32_t* p = &host.pixels[row * FB_WIDTH];
    uint32_t c = internal.palette[0];
    for(int x=0;x<FB_WIDTH;++x){
        p[x] = c;
    }
}

static void beginRenderLine(void){
    if(renderLine == internal.vPos){
        return;
    }
    renderLine = internal.vPos;
    scrollReset();
    fillRowBackground(internal.vPos);   // areas outside the fetched data are background
    int row = internal.vPos - FIRST_DISPLAY_LINE;
    rowStartPx = row * FB_WIDTH;
    int standard = (chipset.bplcon0 & 0x8000) ? 0x3C : 0x38;
    int offsetPx = (firstFetchCycle - standard) * 4;   // one colour clock = 4 hires pixels
    host.FBCounter = (rowStartPx + offsetPx) / 2;      // renderers draw at 2*FBCounter
}

// The renderers write 'width' pixels at index 2*FBCounter: stay inside the row
static inline int framebufferRoom(int width){
    int pos = 2*host.FBCounter;
    int row = internal.vPos - FIRST_DISPLAY_LINE;
    return host.pixels != NULL && row >= 0 && row < FB_HEIGHT
        && pos >= rowStartPx && pos + width <= rowStartPx + FB_WIDTH;
}

// order of the bitplane fetches inside one fetch unit (as in the tables above)
static void (*loresSlots[8])(void) = {evenCycle, plane4, plane6, plane2, evenCycle, plane3, plane5, loresPlane1};
static void (*hiresSlots[4])(void) = {plane4, plane2, plane3, hiresPlane1};

// BPLCON1 horizontal scroll: odd planes are delayed by PF1P (bits 0-3), even
// planes by PF2P (bits 4-7), in lores pixels (2 hires pixels in hires).
// The plane words are kept in memory byte order; history is per line.
static uint64_t scrollHistory[6];
static inline uint16_t swap16(uint16_t v){ return (uint16_t)((v << 8) | (v >> 8)); }

static void scrollReset(void){
    for(int i=0;i<6;++i) scrollHistory[i] = 0;
}

static void scrollPlanes(uint16_t out[6], int hires){
    uint16_t in[6] = {chipset.bpl1dat, chipset.bpl2dat, chipset.bpl3dat,
                      chipset.bpl4dat, chipset.bpl5dat, chipset.bpl6dat};
    if((chipset.bplcon1 & 0xFF) == 0){          // no scrolling: the common case
        for(int i=0;i<6;++i){
            scrollHistory[i] = (scrollHistory[i] << 16) | swap16(in[i]);
            out[i] = in[i];
        }
        return;
    }
    int pf1 = chipset.bplcon1 & 15;
    int pf2 = (chipset.bplcon1 >> 4) & 15;
    for(int i=0;i<6;++i){
        scrollHistory[i] = (scrollHistory[i] << 16) | swap16(in[i]);
        int shift = (i & 1) ? pf2 : pf1;          // planes 1,3,5 = PF1; 2,4,6 = PF2
        if(hires) shift *= 2;
        out[i] = shift ? swap16((uint16_t)(scrollHistory[i] >> shift)) : in[i];
    }
}

// Amiga 500 PAL: 227 colour clocks per line (0-226), 313 lines per frame
// (0-312) = 49.92 frames per second. The original timing was NTSC
// (228 x 263), 20% faster for anything that counts frames.
#define LAST_CCK_OF_LINE   0xE2
#define LAST_LINE_OF_FRAME 312

void dma_execute(){
    
    chipset.vposr   = internal.vPos >> 8;   // Agnus ID 0 = PAL (0x1000 was NTSC)
    chipset.vhposr  = internal.vPos << 8;
    chipset.vhposr |= internal.hPos;
    

    cpuWait = 1;
    // Bitplane fetch window from DDFSTRT/DDFSTOP: fetch units of 8 colour clocks
    // start at DDFSTRT; the unit in which DDFSTOP falls is the last one, and
    // hires fetches one extra unit (standard: lores $38-$D0 = 20 words,
    // hires $3C-$D0 = 40 words). Previously the window was always DDFSTRT+168
    // (21 lores words per line), which only looked right while the modulo was 0.
    // Inside the window the bitplane slots follow the fetch unit (they used to
    // exist only from $38 on, so an early DDFSTRT - used for hardware scrolling -
    // lost a word on every line). Outside it the normal slot table is used.
    int inWindow = (chipset.dmaconr & 0x300) == 0x300;
    if(chipset.bplcon0 & 0x8000){
        firstFetchCycle = chipset.ddfstrt & 0xFC;
        int units = ((int)chipset.ddfstop - firstFetchCycle) >> 3;
        lastFetchCycle  = firstFetchCycle + (units < 0 ? 0 : units) * 8 + 15;
        inWindow = inWindow && internal.hPos >= firstFetchCycle && internal.hPos <= lastFetchCycle;
        if(inWindow){
            hiresSlots[(internal.hPos - firstFetchCycle) & 3]();
        }else{
            DMAHires[internal.hPos]();
        }
    }else{
        firstFetchCycle = chipset.ddfstrt & 0xF8;
        int units = ((int)chipset.ddfstop - firstFetchCycle) >> 3;
        lastFetchCycle  = firstFetchCycle + (units < 0 ? 0 : units) * 8 + 7;
        inWindow = inWindow && internal.hPos >= firstFetchCycle && internal.hPos <= lastFetchCycle;
        if(inWindow){
            loresSlots[(internal.hPos - firstFetchCycle) & 7]();
        }else{
            DMALores[internal.hPos]();
        }
    }
    
    
    eclock_execute(&chipset);   // CIA timers
    PAULA_TICK();               // audio

    internal.hPos++;
    

    
    
    //end of line reached! 227 colour clocks have executed
    if(internal.hPos > LAST_CCK_OF_LINE){
        cpuWait = 0;
        

        //************DeBugging**************
        int hpos = internal.hPos;
        int vpos = internal.vPos;
        uint16_t ddfstrt = chipset.ddfstrt;
        uint16_t ddfstop = chipset.ddfstop;
        uint16_t diwstrt = chipset.diwstrt;
        uint16_t diwstop = chipset.diwstop;
        uint16_t actualDdfSyop = lastFetchCycle;
        
        int diff = ddfstop - ddfstrt;
        
        if(internal.vPos>43){
            int vBeam = (internal.vPos-44);
            
            if(vBeam==0){
                //printf("NLine");
                
            }

            
            //host.FBCounter =vBeam*640;
            //printf("");
            
        }
        //***********************************
        
        
        
        
        
        if(renderLine != internal.vPos){
            fillRowBackground(internal.vPos);   //nothing was drawn on this line
        }
        internal.hPos = 0;
        internal.vPos +=1;
        CIATODEvent(&CIAB);
        
        
        //Update bitplane modulo - only after a line on which bitplanes were
        //fetched (on real hardware the modulo is added at the last fetch).
        //Adding it on every line moved the picture whenever the modulo was not 0.
        if(bitplaneLineFetched){
            chipset.bpl1pt += chipset.bpl1mod;
            chipset.bpl3pt += chipset.bpl1mod;
            chipset.bpl5pt += chipset.bpl1mod;
            chipset.bpl7pt += chipset.bpl1mod;
            chipset.bpl2pt += chipset.bpl2mod;
            chipset.bpl4pt += chipset.bpl2mod;
            chipset.bpl6pt += chipset.bpl2mod;
            chipset.bpl8pt += chipset.bpl2mod;
            bitplaneLineFetched = 0;
        }
        
        //VBL Time
        if(internal.vPos > LAST_LINE_OF_FRAME){
            internal.vPos = 0;
            //chipset.vposr   = (internal.LOF | 0x1000); //0x1000 is for NTSC / 0x0000 is for PAL
            
            //Reset Copper.
            putChipReg16[COPJMP1](0);
            CIATODEvent(&CIAA);
            
            
            //need to generate a vbl int
            putChipReg16[INTREQ](0x8020);
            
            host.FBCounter = 0; // restart the frambuffer pointer
            renderLine = -1;
            
            
            //Draw sprite 0
            if( (chipset.dmaconr & 0x220)== 0x220){
                //Software renderer - draw sprite to the frame buffer
                
                
                if(chipset.spr0pt !=0){
                    uint16_t* pos = &CHIPW(chipset.spr0pt);
                    
                    int Nx=(pos[0] & 0xFF00) >> 7;
                    Nx |=( (pos[1] >> 8) & 1);
                    Nx = (Nx * 2)-254;
                    int Ny= ((pos[0] & 255))-43;
                    
                    // clip vertically: rows outside the framebuffer were written
                    // before/after the buffer (heap corruption) in the old code
                    for(int i=2, line=0; i<32; i +=2, ++line){
                        int row = Ny + line;
                        if(row < 0 || row >= FB_HEIGHT){
                            continue;
                        }
                        sprite2chunky(&host.pixels[row*FB_WIDTH], &internal.palette[16], Nx, pos[i], pos[i+1],16);
                    }
                    
                }
            }
            
            
            
            host.vblCount++;
            inputFrame();  //deliver one queued keyboard event per frame
            hostDisplay(); //Call the host to update the display.
        }
        
    }
}


void displayLineReset(){
    //Lines are positioned individually by beginRenderLine(); after a BPLCON0
    //change in the middle of a line, restart positioning on the next render.
    renderLine = -1;
}

void setDisplayMode(int mode){
    
    int temp = host.FBCounter / 640;
    temp = (temp) * 640;
    host.FBCounter= temp;
    
}



void evenCycle(void){

    // The copper only owns even colour clocks (a MOVE takes 4 clocks). Bitplane
    // slots that are not used fall back to evenCycle() on odd clocks too, which
    // made the copper run twice as fast as real hardware: games that change
    // registers at an exact point of a line (e.g. Monkey Island) broke.
    if(internal.hPos & 1){
        oddCycle();
        return;
    }
    
    if(copperExecute()==1){
        return;
    }
    
    //if the copper doesn't want the even cycle, give it to the odd cycle devices.
    oddCycle();

}

void oddCycle(void){
    
    if(blitterExecute()==1){
        return;
    }
    
    //A Free slot for CPU... but the CPU isn't currently bound to the DMA timing
    cpuWait = 0;
}

void dramCycle(void){
    
}

int turboFloppy = 8;    //8 for turbo

void diskCycle(void){
    
        
    if( (chipset.dmaconr & 0x210) && chipset.dsklen & 0x8000 ){
        
        if(chipset.dsklen & 0x3FFF){
            
            
            //OS2+ Disk loading
            
            for(int i=0;i<turboFloppy;++i){
                
                if(chipset.adkconr & 0x400){
                    //printf("Needs a disk sync");
                    //sync = 0;
                }
            
                uint8_t b1 = floppyDataRead();
                uint8_t b2 = floppyDataRead();
            
                uint16_t syncword = b1 << 8 | b2;
            
                if(syncword==chipset.dsksync){
                
                    putChipReg16[INTREQ](0x9000);   //DSKSYNC INT
                    chipset.dskbytr |= 0x1000;  // set word sync bit
                
                    if(floppySync==0){
                    
                        //printf("dsklen: %04x, dskpt: %06x | Track %d | Side %d | Index %d\n",chipset.dsklen & 0x3FFF,chipset.dskpt,df[driveSelected].cylinder,df[driveSelected].side,df[driveSelected].index);
                        floppySync=1;
                        return;
                    }
                }
                
                if(floppySync == 1){
                    
                    //chipset.dskbytr &= 0xEFFF;      //clear word sync
                    low16Meg[chipset.dskpt & CHIPB_MASK] = b1;
                    chipset.dskpt +=1;
                
                    low16Meg[chipset.dskpt & CHIPB_MASK] = b2;
                    chipset.dskpt +=1;
                
                    chipset.dsklen -= 1;
                
                    if( (chipset.dsklen & 0x3FFF) == 0){
                        putChipReg16[INTREQ](0x8002);   //Disk block loaded INT
                        floppySync = 0;
                        return;
                    }
                
                }
            }
            
            
            return;
        }
       
    }
    
    oddCycle();
}


void audio0Cycle(void){
    // Audio DMA slot: Paula.c fetches the data itself (paulaTick); the slot
    // is only taken away from the blitter while the channel is running.
    if((chipset.dmaconr & 0x201) == 0x201){
        return;
    }
    oddCycle();
}


void audio1Cycle(void){
    // Audio DMA slot: Paula.c fetches the data itself (paulaTick); the slot
    // is only taken away from the blitter while the channel is running.
    if((chipset.dmaconr & 0x202) == 0x202){
        return;
    }
    oddCycle();
}

void audio2Cycle(void){
    // Audio DMA slot: Paula.c fetches the data itself (paulaTick); the slot
    // is only taken away from the blitter while the channel is running.
    if((chipset.dmaconr & 0x204) == 0x204){
        return;
    }
    oddCycle();
}

void audio3Cycle(void){
    // Audio DMA slot: Paula.c fetches the data itself (paulaTick); the slot
    // is only taken away from the blitter while the channel is running.
    if((chipset.dmaconr & 0x208) == 0x208){
        return;
    }
    oddCycle();
}


void spriteCycle(void){

    oddCycle();
}


static inline int bitplaneActive(void){
    
    //check if DMA is on, if not let the Copper and Blitter run.
    if((chipset.dmaconr & 0x300) != 0x300){
        return 0;
    }
    
    //too early horisonal position let the Copper and Blitter run
    if(internal.hPos<(chipset.ddfstrt)){
        return 0;
    }
 
    //too late horisonal position let the Copper and Blitter run... why + 16?
    
    if(internal.hPos>lastFetchCycle){// 0xd7+16 (chipset.ddfstop+16)){     //not sure why the ddfstop sometimes have wrong values.
    //if(internal.hPos>(chipset.ddfstop+4)){
        return 0;
    }
    
    //too early vertical position let the Copper and Blitter run
    if(internal.vPos<(chipset.diwstrt>>8)){
        return 0;
    }
    
    //vertical stop: DIWSTOP V7-V0, with V8 = NOT V7 (OCS)
    int vstop = ((chipset.diwstop >> 8) & 0xFF) | ((~chipset.diwstop >> 7) & 0x100);
    if(internal.vPos >= vstop){
        return 0;
    }
    
    return 1;
}



void plane6(void){
    
    if(bitplaneActive()==0){
        evenCycle(); // let the copper run
        return;
    }
    
    chipset.bpl6dat = 0;
    if( (internal.bitplaneMask & 0x20)  == 0x20){
        uint16_t* p = &CHIPW(chipset.bpl6pt);
        chipset.bpl6pt +=1;
        bitplaneLineFetched = 1;
        chipset.bpl6dat = *p;
        return;
    }
    
    evenCycle();
}


void plane5(void){
    
    if(bitplaneActive()==0){
        evenCycle(); // let the copper run
        return;
    }
    
    chipset.bpl5dat = 0;
    if( (internal.bitplaneMask & 0x10)  == 0x10){
        uint16_t* p = &CHIPW(chipset.bpl5pt);
        chipset.bpl5pt +=1;
        bitplaneLineFetched = 1;
        chipset.bpl5dat = *p;
        return;
    }

    evenCycle();
    
}
void loresPlane1(void){
    
    
    if(bitplaneActive()==0){
        //drawBlank();
        return;
    }
    
    if(host.pixels == NULL){
        return;
    }
    
    if( (internal.bitplaneMask & 0x1)  == 0x1){

    
        uint16_t* p = &CHIPW(chipset.bpl1pt);
        chipset.bpl1pt +=1;
        bitplaneLineFetched = 1;
        chipset.bpl1dat = *p;
    }
    
    //don't start actually rendering a deiplay before 44 lines
    if(internal.vPos<44){
        evenCycle();
        return;
    }
    
    beginRenderLine();
    uint16_t d[6];
    scrollPlanes(d, 0);
    if(!framebufferRoom(32)){
        host.FBCounter +=16;
        return;
    }
    uint32_t* pixbuff = (uint32_t*)host.pixels;
    if(chipset.bplcon0 & 0x800){
        loresHAM2Chunky(&pixbuff[host.FBCounter], internal.palette, d[0], d[1], d[2], d[3], d[4], d[5]);
    }else{
        loresPlanar2Chunky(&pixbuff[host.FBCounter], internal.palette, d[0], d[1], d[2], d[3], d[4], d[5]);
    }
    host.FBCounter +=16;
    
}



void plane4(){
    
    if(bitplaneActive()==0){
        evenCycle(); // let the copper run
        return;
    }
    
    chipset.bpl4dat = 0;
    if( (internal.bitplaneMask & 0x8)  == 0x8){
        uint16_t* p = &CHIPW(chipset.bpl4pt);
        chipset.bpl4pt +=1;
        bitplaneLineFetched = 1;
        chipset.bpl4dat = *p;
        return;
    }
    
    evenCycle();
    
}

void plane2(){
    
    if(bitplaneActive()==0){
        evenCycle(); // let the copper run
        return;
    }
    
    chipset.bpl2dat = 0;
    if( (internal.bitplaneMask & 0x2)  == 0x2){
        uint16_t* p = &CHIPW(chipset.bpl2pt);
        chipset.bpl2pt +=1;
        bitplaneLineFetched = 1;
        chipset.bpl2dat = *p;
        return;
    }
    
    oddCycle();
    
}

void plane3(){
    
    if(bitplaneActive()==0){
        evenCycle(); // let the copper run
        return;
    }
    
    chipset.bpl3dat = 0;
    if( (internal.bitplaneMask & 0x4)  == 0x4){
        uint16_t* p = &CHIPW(chipset.bpl3pt);
        chipset.bpl3pt +=1;
        bitplaneLineFetched = 1;
        chipset.bpl3dat = *p;
        return;
    }
    
    evenCycle();
    
}

void hiresPlane1(){
     
    if(bitplaneActive()==0){
        
        evenCycle(); // let the copper run
        return;
    }
    
    if(host.pixels == NULL){
        return;
    }
    
    chipset.bpl1dat = 0;
    if( (internal.bitplaneMask & 0x1)  == 0x1){
        uint16_t* p = &CHIPW(chipset.bpl1pt);
        chipset.bpl1pt +=1;
        bitplaneLineFetched = 1;
        chipset.bpl1dat = *p;
    
    }
        
    //don't start actually rendering a display before 44 lines
    if(internal.vPos<43){
        evenCycle();
        return;
    }
    
    beginRenderLine();
    uint16_t d[6];
    scrollPlanes(d, 1);
    if(!framebufferRoom(16)){
        host.FBCounter +=8;
        return;
    }
    uint32_t* pixbuff = (uint32_t*)host.pixels;
    hiresPlanar2Chunky(&pixbuff[host.FBCounter], internal.palette, d[0], d[1], d[2], d[3]);
    host.FBCounter +=8;
    return;
    

    
    evenCycle();
    
}







void drawBlank(){
    
    uint32_t* pixbuff = (uint32_t*)host.pixels;
    
    if(!framebufferRoom(16) || internal.vPos<44){
        return;
    }
    
    for(int i=0;i<16;++i){
        pixbuff[host.FBCounter+i]=internal.palette[0];
    }
    host.FBCounter +=16;
    
}




int copperExecute(){
    
    if((chipset.dmaconr & 0x280) != 0x280){
        return 0;
    }
        
    switch(internal.copperCycle){
        case 0:
            internal.IR1 = CHIPW(internal.copperPC);
            internal.IR1 = (internal.IR1 <<8) | (internal.IR1 >>8);
            internal.copperPC += 1;
            internal.copperCycle = 1;
            
            if( (internal.IR1 & 0x1) == 0x1){
                internal.copperCycle = 2;
            }
            return 1;
            break;
            
        case 1:
            internal.IR2 = CHIPW(internal.copperPC);
            internal.copperPC += 1;
            
            internal.IR1 = (internal.IR1 >> 1) & 255;   // divide by 2 and mask off bad bits
            
            if(internal.IR1<0x20){
                internal.copperCycle = 4;return 1;};         //pause copper until next vbl
            if(internal.IR1<0x40 && chipset.copcon==0){
                internal.copperCycle = 4;return 1;};         //pause copper until next vbl, unless we allow copper access to blitter
            

            
            //Move
            internal.IR2 = (internal.IR2 <<8) | (internal.IR2 >> 8);
            //printf("%04x Cop Move: %04x -> (%s)\n",internal.copperPC - 4,internal.IR2,regNames[internal.IR1]);
            
            debugChipAddress = internal.IR1; //Debug what the Copper is writing to...
            debugChipValue   = internal.IR2;
            
            putChipReg16[internal.IR1](internal.IR2);
            internal.copperCycle = 0;
            return 1;
            break;
            
        case 2:
            internal.IR2 = CHIPW(internal.copperPC);
            internal.copperPC += 1;
            
            internal.IR2 = (internal.IR2 <<8) | (internal.IR2 >>8);
            
            internal.comparisonMask = (internal.IR2 | 0x0000); //ignore the instruction bits

            internal.IR1 &= internal.comparisonMask; //mask the wait position

            internal.copperCycle = 3;
            
            //Skip
            if( (internal.IR2 & 1) == 1){
                
                if( chipset.vhposr >= internal.IR1){
                    internal.copperPC +=2;
                }
                internal.copperCycle = 0;
                
                //printf("%04x Cop Skip!\n",internal.copperPC - 4);
                return 1;
                
            }
            
            return 1;
            break;
            
        case 3:
            
            /* old debugging stuff
            if(1){}
            uint16_t vhposr = chipset.vhposr;
            uint16_t wait = internal.IR1;
            uint16_t comparison = internal.IR2;
            */
            
            //Wait
            if( (chipset.vhposr & internal.IR2)  >= internal.IR1){
                internal.copperCycle = 0;
            }
            
            break;
        case 4:
            //Copper operation frozen until next VBL/copper reset
            break;
    }
    
    return 0;
}



//********************Blitter



int blitterExecute(){
    
    static int state = 0;
    
    
    switch(state){
            
        case 0:
            //Blitter DMA Enabled and blitter busy flag set (which means bltsize was written to and blitting should start).
            if( (chipset.dmaconr & 0x4240) == 0x4240 ){
                
                if(chipset.bltcon1 & 1){
                    //Line Mode
                    //printf("Line Mode\n");
                    state = 1;
                }else{
                    //Copy Mode
                    //printf("Copy Mode\n");
                    /*
                    internal.useMask = chipset.bltcon0 >> 8;
                    internal.shiftA = chipset.bltcon0 >> 12;
                    internal.shiftB = chipset.bltcon1 >> 12;
                    internal.minterm = chipset.bltcon0 & 255;
                    internal.xIncrement = 1;
                    internal.fillmode = (chipset.bltcon1 & 0x18) >> 3; // 1= Inclusive Fill, 2 = Exclusive Fill
                    
                    //all pointers are word addressed and masked for 2meg only
                    internal.apt=(chipset.bltapt >> 1);
                    internal.bpt=(chipset.bltbpt >> 1);
                    internal.cpt=(chipset.bltcpt >> 1);
                    internal.dpt=(chipset.bltdpt >> 1);
                    
                    
                    internal.amod=chipset.bltamod / 2; //these are signed so need to be divided not shifted
                    internal.bmod=chipset.bltbmod / 2;
                    internal.cmod=chipset.bltcmod / 2;
                    internal.dmod=chipset.bltdmod / 2;
                    
                    //internal.lastHWord=chipset.bltsizh - 1;
                    internal.sizeh = chipset.bltsizh;
                    
                    Internal_t* debug = &internal;
                    */
                    state = 2;
                }
            }
            break;
            
        case 1:
            //Line Mode
            
            //No DMA cycle mode yet
            
            //Immediate mode Blitter
            blitter_execute(&chipset);
            state = 0;
            
            break;
            
        case 2:
            //Copy Mode
            //if descend mode is on.
            if((chipset.bltcon1 & 2)){
                internal.xIncrement = -1;
                
                if(internal.fillmode>0){
                    printf("NO FILL MODE YET!!");
                }
                
            }
            
            //DMA cycle mode
            //state = blitterCopyCycle();   //doesn't work at all.
            
            
            //Immediate mode Blitter
            blitter_execute(&chipset);
            state = 0;
            
            break;
    }
    
    
    
    return 0;
}


int blitterCopyCycle(){
    
    
    //Internal_t* debug = &internal;
    
        static uint16_t previousA = 0;
        static uint16_t previousB = 0;
        
            uint16_t channelD = 0;
            
            //Channel A
            if(internal.useMask & 8){
                chipset.bltadat = CHIPW(internal.apt);
                chipset.bltadat = chipset.bltadat << 8 | chipset.bltadat >> 8;
                internal.apt +=internal.xIncrement;
            }
            uint16_t channelA = chipset.bltadat;
            
            //Channel B
            if(internal.useMask & 4){
                chipset.bltbdat = CHIPW(internal.bpt);
                chipset.bltbdat = chipset.bltbdat << 8 | chipset.bltbdat >> 8;
                internal.bpt +=internal.xIncrement;
            }
            uint16_t channelB = chipset.bltbdat;
            
            //Channel C
            if(internal.useMask & 2){
                chipset.bltcdat = CHIPW(internal.cpt);
                chipset.bltcdat = chipset.bltcdat << 8 | chipset.bltcdat >> 8;
                internal.cpt +=internal.xIncrement;
            }
            
            
            //Masking section
            
            if(chipset.bltsizh==internal.sizeh){
                channelA = channelA & chipset.bltafwm;
            }
            
            if(chipset.bltsizh==1){
                channelA = channelA & chipset.bltalwm;
            }
            
            //shifting section
            if(internal.xIncrement==-1){
                
                channelA = (previousA >> (16-internal.shiftA)) | (channelA << internal.shiftA);
                channelB = (previousB >> (16-internal.shiftB)) | (channelB << internal.shiftB);
                
            }else{
                
                channelA = (previousA << (16-internal.shiftA)) | (channelA >> internal.shiftA);
                channelB = (previousB << (16-internal.shiftB)) | (channelB >> internal.shiftB);
                
            }
            previousA = chipset.bltadat;
            previousB = chipset.bltbdat;
            
            
            channelD = logicFunction(internal.minterm, channelA, channelB, chipset.bltcdat);
            
            //Zero Flag
            if(channelD==0){
                chipset.dmaconr = chipset.dmaconr | 0x2000;   //set zero flag
            }else{
                chipset.dmaconr = chipset.dmaconr & 0xDFFF; // clear zero flag
            }
            
            //Channel D
            if(internal.useMask & 1){
                
                channelD = channelD << 8 | channelD >>8;
                CHIPW(internal.dpt) = channelD;
                internal.dpt +=internal.xIncrement;
            }
            
        
        
        if(internal.xIncrement == -1){
            internal.apt -= internal.amod;
            internal.bpt -= internal.bmod;
            internal.cpt -= internal.cmod;
            internal.dpt -= internal.dmod;
        }else{
            internal.apt += internal.amod;
            internal.bpt += internal.bmod;
            internal.cpt += internal.cmod;
            internal.dpt += internal.dmod;
        }
        
        chipset.bltsizh -= 1;
        
        if(chipset.bltsizh == 0x0){
            chipset.bltsizh = internal.sizeh;
            chipset.bltsizv -=1;
            previousA = 0;
            previousB = 0;

        }
        
        if(chipset.bltsizv==0x0){
            //chipset.bltsizh = 0;
            
            //save blitter state - something might depend upon the blitter being in a known state
            //chipset.bltapt = internal.apt << 1;
            chipset.bltbpt = internal.bpt << 1;
            chipset.bltcpt = internal.cpt << 1;
            chipset.bltdpt = internal.dpt << 1;
            
            chipset.dmaconr = chipset.dmaconr & 49151; //clear blitter busy bit
            putChipReg16[INTREQ](0x8040);              // generate an interrupt!
            return 0; // no more copy to do
        }
    
    return 2; // more copy to do!
}
