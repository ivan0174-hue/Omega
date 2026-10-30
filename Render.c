//
//  Render.c
//  The Omega Project
//
//  Portable pixel conversion (planar -> chunky) and the emulated framebuffer.
//  Split out of the old SDL Host.c so that a bare metal backend only needs to
//  copy host.pixels to the screen.
//
//  The planar->chunky routines are table driven: one 256 entry table spreads
//  the 8 bits of a bitplane byte into 8 byte lanes of a 64 bit word, so 8
//  pixels are built with one lookup per plane instead of 8 shifts per plane.
//  Output is bit-for-bit identical to the original loops.
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "Host.h"
#include "Chipset.h"

Host_t host;

static uint32_t framebuffer[FB_WIDTH * FB_HEIGHT];
static uint64_t spread[256];    // lane k (0 = leftmost pixel) = bit (7-k) of the byte

void renderInit(void){
    for(int b=0;b<256;++b){
        uint64_t v = 0;
        for(int k=0;k<8;++k){
            v |= (uint64_t)((b >> (7-k)) & 1) << (8*k);
        }
        spread[b] = v;
    }
    host.pixels = framebuffer;
    host.FBCounter = 0;
    host.vblCount = 0;
}

static inline uint64_t lanes4(uint16_t p1,uint16_t p2,uint16_t p3,uint16_t p4,int shift){
    return  spread[(p1 >> shift) & 255]
         | (spread[(p2 >> shift) & 255] << 1)
         | (spread[(p3 >> shift) & 255] << 2)
         | (spread[(p4 >> shift) & 255] << 3);
}

// The plane words are in memory byte order: the low byte holds the 8 leftmost pixels.
void hiresPlanar2Chunky(uint32_t* pixBuff,uint32_t* palette,uint16_t plane1, uint16_t plane2,uint16_t plane3,uint16_t plane4){
    uint32_t* out = &pixBuff[host.FBCounter];
    uint64_t lo = lanes4(plane1,plane2,plane3,plane4,0);
    uint64_t hi = lanes4(plane1,plane2,plane3,plane4,8);
    for(int k=0;k<8;++k){
        out[k]   = palette[(lo >> (8*k)) & 15];
        out[k+8] = palette[(hi >> (8*k)) & 15];
    }
}

void loresPlanar2Chunky(uint32_t* pixBuff,uint32_t* palette,uint16_t plane1, uint16_t plane2,uint16_t plane3,uint16_t plane4,uint16_t plane5,uint16_t plane6){
    uint32_t* out = &pixBuff[host.FBCounter];
    uint64_t lo = lanes4(plane1,plane2,plane3,plane4,0)
                | (spread[plane5 & 255] << 4) | (spread[plane6 & 255] << 5);
    uint64_t hi = lanes4(plane1,plane2,plane3,plane4,8)
                | (spread[plane5 >> 8] << 4) | (spread[plane6 >> 8] << 5);
    for(int k=0;k<8;++k){
        uint32_t c1 = palette[(lo >> (8*k)) & 63];
        uint32_t c2 = palette[(hi >> (8*k)) & 63];
        out[2*k]    = c1;  out[2*k+1]  = c1;     // lores pixels are doubled
        out[2*k+16] = c2;  out[2*k+17] = c2;
    }
}

void loresHAM2Chunky(uint32_t* pixBuff,uint32_t* palette,uint16_t plane1, uint16_t plane2,uint16_t plane3,uint16_t plane4,uint16_t plane5,uint16_t plane6){
    int counter = host.FBCounter;
    
    //Fetch and display 2 bytes of bitplane data (16 pixels)
    uint32_t colourIndex1 = 0;
    uint32_t colourIndex2 = 0;
    
    for(int j=7;j>-1;--j){
        
        //build 8 chunky pixels from a byte of bitplane data - Don't need to byte swap as the lump below does that
        colourIndex1 =                  (plane1>>j) & 1;
        colourIndex1 = colourIndex1 | (((plane2>>j) & 1) << 1);
        colourIndex1 = colourIndex1 | (((plane3>>j) & 1) << 2);
        colourIndex1 = colourIndex1 | (((plane4>>j) & 1) << 3);
        colourIndex1 = colourIndex1 | (((plane5>>j) & 1) << 4);
        colourIndex1 = colourIndex1 | (((plane6>>j) & 1) << 5);
        
        //check if we have a ham pixel
        if(colourIndex1 & 0xF0){
            
            int control = colourIndex1 >> 4;
            uint32_t colour = colourIndex1 & 0xF;
            colour = (colour << 4) | colour;
            uint32_t previous = (&pixBuff[counter] > host.pixels) ? pixBuff[counter-1] : palette[0];  //never read before the framebuffer
            
            switch(control){
                case 1:
                    previous = (previous & 0xFFFFFF00) | colour;
                    break;
                case 2:
                    colour <<= 16;
                    previous = (previous & 0xFF00FFFF) | colour;
                    break;
                case 3:
                    colour <<= 8;
                    previous = (previous & 0xFFFF00FF) | colour;
                    break;
                    
                    
            }
            
            
            pixBuff[counter] = previous;
            counter +=1;
            pixBuff[counter] = previous;
            counter +=1;
            
            
        }else{
            uint32_t colour = internal.palette[colourIndex1];
            pixBuff[counter] = colour;
            counter +=1;
        
            //Double up pixels since lores only has 320 pixels
            pixBuff[counter] = colour;
            counter +=1;
        }
        
        
    }

    counter = host.FBCounter;
    for(int j=7;j>-1;--j){
        
        int k=j+8;
        //build 8 chunky pixels from a byte of bitplane data
        colourIndex2 =                  (plane1>>k) & 1;
        colourIndex2 = colourIndex2 | (((plane2>>k) & 1) << 1);
        colourIndex2 = colourIndex2 | (((plane3>>k) & 1) << 2);
        colourIndex2 = colourIndex2 | (((plane4>>k) & 1) << 3);
        colourIndex2 = colourIndex2 | (((plane5>>k) & 1) << 4);
        colourIndex2 = colourIndex2 | (((plane6>>k) & 1) << 5);
        
        //check if we have a ham pixel
        if(colourIndex2 & 0xF0){
            
            int control = colourIndex2 >> 4;
            uint32_t colour = colourIndex2 & 0xF;
            colour = (colour << 4) | colour;
            uint32_t previous = pixBuff[counter+15];
            
            switch(control){
                case 1:
                    previous = (previous & 0xFFFFFF00) | colour;
                    break;
                case 2:
                    colour <<= 16;
                    previous = (previous & 0xFF00FFFF) | colour;
                    break;
                case 3:
                    colour <<= 8;
                    previous = (previous & 0xFFFF00FF) | colour;
                    break;
                    
                    
            }
            
            
            pixBuff[counter+16] = previous;
            counter +=1;
            pixBuff[counter+16] = previous;
            counter +=1;
            
            
        }else{
            uint32_t colour = internal.palette[colourIndex2];
            pixBuff[counter+16] = colour;
            counter +=1;
            
            //Double up pixels since lores only has 320 pixels
            pixBuff[counter+16] = colour;
            counter +=1;
        }
        
        
    }


}

// Draws one 16 pixel sprite line; clipped on both sides of the 640 pixel line.
void sprite2chunky(uint32_t* pixBuff,uint32_t* palette, int x,uint16_t plane1, uint16_t plane2,int delta){
    int counter = x;
    for(int j=7;j>-1;--j){
        int k=j+8;
        uint32_t colourIndex1 = ((plane1>>j) & 1) | (((plane2>>j) & 1) << 1);
        uint32_t colourIndex2 = ((plane1>>k) & 1) | (((plane2>>k) & 1) << 1);
        int reps = (delta==16) ? 2 : 1;
        for(int r=0;r<reps;++r){
            if(colourIndex1>0 && counter>=0 && counter<FB_WIDTH){
                pixBuff[counter] = palette[colourIndex1];
            }
            int c2 = counter + delta;
            if(colourIndex2>0 && c2>=0 && c2<FB_WIDTH){
                pixBuff[c2] = palette[colourIndex2];
            }
            counter +=1;
        }
    }
}
