//
//  Blitter.c
//  The Omega Project
//  https://github.com/h5n1xp/Omega
//
//  Created by Matt Parsons on 20/02/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//  <h5n1xp@gmail.com>
//
//
//  This Source Code Form is subject to the terms of the
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
//

/*

 cycle 0:   set up blitter;
            if(mode = copy mode){
                cycle = 0;
            }
            if(mode = fill mode){
                cycle = 5;
            }
            if(mode = line mode){
                cycle = -1
            }
 
 cycle 1:  if(A needs load){
                load A;
                cycle = 2;
            }else if{B needs to load){
                load B;
                cycle = 3;
            } else if{C needs to load){
                Load C;
                cycle = 4;
            }
 
 cycle 2:  if{B needs to load){
                load B;
                cycle = 3;
            } else if{C needs to load){
                Load C;
                cycle = 4;
            }
 
 cycle 3:  if{C needs to load){
                Load C;
                cycle = 4;
            }
 
 cycle 4:   mask A;
            Shift A;
            Shift B;
            apply logic functions
            set clear zero flag
 
            if(Store D needed){
                Store D;
            }
 
            update v and h counters
            cycle = 1 ; //repeat the operation
 
            if(v == end condition){
                reset v = starting value.
                increment h
            }
 
            if(h == end conditon{
                clear blitter busy flag
                Generate interrupt;
                cycle = 0;  // rest the blitter state
            }
 
 cycle 5:   fill mode
 
 
 */


#include "Blitter.h"


void blitterCycle(void){
    
}

void (*blitterOperation[]) (void) = {blitterCycle,blitterCycle};

int OblitterExecute(){
    
    static int cycle;
    
    //check if blitter DMA is on; if not then return 0
    //Check if blitter needs to run (blitter busy flag is set); if not then return 0;
    
    blitterOperation[cycle]();

    return 1; //if we got here we probably used the memory cycle...
    
    
}

//********************* BLITER SECTION *****************************

// Area fill (HRM ch.6): bits are processed from bit 0 (rightmost pixel) to bit 15.
// The fill carry toggles on every 1 bit of the logic output.
//  inclusive (IFE): out = in | carry  -> both edges are kept
//  exclusive (EFE): out = in ^ carry  -> the left edge is removed
// The carry continues across the words of a line and restarts from FCI on every line.
static uint16_t blitterFill(uint16_t data, int* carry, int exclusive){
    uint16_t out = 0;
    int fc = *carry;
    for(int i=0;i<16;++i){
        int bit = (data >> i) & 1;
        int o = exclusive ? (bit ^ fc) : (bit | fc);
        out |= (uint16_t)(o << i);
        fc ^= bit;
    }
    *carry = fc;
    return out;
}

uint16_t logicFunction(int minterm,uint16_t wordA, uint16_t wordB, uint16_t wordC){
    
    //Not section
    uint16_t notA = ~wordA;
    uint16_t notB = ~wordB;
    uint16_t notC = ~wordC;
    uint16_t channelD = 0;
    
    //Logic Section
    
    if(minterm & 0x80){
        channelD =            (wordA & wordB & wordC);}
    if(minterm & 0x40){
        channelD = channelD | (wordA & wordB & notC);}
    if(minterm & 0x20){
        channelD = channelD | (wordA & notB  & wordC);}
    if(minterm & 0x10){
        channelD = channelD | (wordA & notB  & notC);}
    if(minterm & 0x08){
        channelD = channelD | (notA  & wordB & wordC);}
    if(minterm & 0x04){
        channelD = channelD | (notA  & wordB & notC);}
    if(minterm & 0x02){
        channelD = channelD | (notA  & notB  & wordC);}
    if(minterm & 0x01){
        channelD = channelD | (notA  & notB  & notC);}
    
    return channelD;
}

void blitter_execute(Chipset_t* chipset){
    
    //Blitter DMA Enabled and blitter busy flag set (which means bltsize was written to and blitting should start).
    if( (chipset->dmaconr & 16960) == 16960 ){
        
        /*
         //make it so the blitter doesn't blit immediatly
         static int delay;
         
         delay -=1;
         
         if(delay>0){
         return;
         }
         delay = 10;
         */
        
        
        if(chipset->bltcon1 & 1){
            //Line Mode - follows the hardware step by step (as documented in the
            //HRM and implemented by UAE):
            //  BLTCON0: ASH = start pixel in the word, minterm (CA normal, 4A XOR)
            //  BLTCON1: BSH = texture start, SIGN, SUD/SUL/AUL = octant, SING = one dot
            //  BLTAPTL = 4dy-2dx, BLTAMOD = 4(dy-dx), BLTBMOD = 4dy, BLTCMOD = width
            //  BLTSIZV = length in pixels
            //One-dot mode (only the first pixel of every row is written) is what
            //filled vector polygons rely on; it was missing before, so the area
            //fill leaked across the screen (e.g. State of the Art).
            uint16_t con0 = chipset->bltcon0;
            uint16_t con1 = chipset->bltcon1;
            int minterm  = con0 & 255;
            int ashift   = con0 >> 12;
            int sud = con1 & 0x10, sul = con1 & 0x08, aul = con1 & 0x04;
            int sing = con1 & 0x02;
            int sign = (con1 & 0x40) != 0;
            int16_t error = (int16_t)chipset->bltapt;
            int16_t amod = chipset->bltamod;
            int16_t bmod = chipset->bltbmod;
            int cmod = chipset->bltcmod;
            int length = chipset->bltsizv ? chipset->bltsizv : 1024;

            int bshift = con1 >> 12;
            uint16_t texture = chipset->bltbdat;
            texture = (uint16_t)((texture >> bshift) | (texture << ((16 - bshift) & 15)));
            uint16_t adat = chipset->bltadat & chipset->bltafwm;

            uint32_t cpt = chipset->bltcpt;
            uint32_t dpt = chipset->bltdpt;
            int oneDotDone = 0;
            int zero = 1;

            for(int i=0;i<length;++i){
                //pixel
                uint16_t a = (uint16_t)(adat >> ashift);
                uint16_t b = (texture & 1) ? 0xFFFF : 0;
                uint16_t c = CHIPW(cpt >> 1);
                c = (uint16_t)((c << 8) | (c >> 8));
                if(sing && oneDotDone){
                    a = 0;
                }
                uint16_t d = logicFunction(minterm, a, b, c);
                if(d) zero = 0;
                if(!(sing && oneDotDone)){
                    CHIPW(dpt >> 1) = (uint16_t)((d << 8) | (d >> 8));
                }
                oneDotDone = 1;

                //step
                int stepYMinor = 0, stepYMajor = 0;
                if(!sign){                          //minor axis step
                    if(sud){
                        stepYMinor = sul ? -1 : 1;
                    }else{
                        int dx = sul ? -1 : 1;
                        ashift += dx;
                        if(ashift == 16){ ashift = 0;  cpt += 2; }
                        if(ashift == -1){ ashift = 15; cpt -= 2; }
                    }
                }
                if(sud){                            //major axis step
                    int dx = aul ? -1 : 1;
                    ashift += dx;
                    if(ashift == 16){ ashift = 0;  cpt += 2; }
                    if(ashift == -1){ ashift = 15; cpt -= 2; }
                }else{
                    stepYMajor = aul ? -1 : 1;
                }
                if(stepYMinor || stepYMajor){
                    cpt += (stepYMinor + stepYMajor) * cmod;
                    oneDotDone = 0;                 //new row: the next pixel may be drawn
                }
                //only the first pixel goes to BLTDPT, the hardware then writes
                //every pixel back to the address it read through channel C
                dpt = cpt;

                error = (int16_t)(error + (sign ? bmod : amod));
                sign = error < 0;
                texture = (uint16_t)((texture << 1) | (texture >> 15));
            }

            chipset->bltcpt = cpt;
            chipset->bltdpt = dpt;
            chipset->bltapt = (chipset->bltapt & 0xFFFF0000) | (uint16_t)error;
            chipset->bltcon0 = (uint16_t)((con0 & 0x0FFF) | ((ashift & 15) << 12));
            chipset->bltcon1 = (uint16_t)((con1 & ~0x40) | (sign ? 0x40 : 0));

            if(zero){
                chipset->dmaconr |= 0x2000;
            }else{
                chipset->dmaconr &= 0xDFFF;
            }
            chipset->bltsizh = 0; // all done;
            chipset->bltsizv = 0; // all done;
            chipset->dmaconr = chipset->dmaconr & 49151; //clear blitter busy bit
            putChipReg16[INTREQ](0x8040); //generate an interrupt!
            
        }else{
            
            static int count = 0;
            
            //printf("Blitter Copy Mode: %d\n",count);
            
            count++;
            
            //Area Copy Blitter
            //printf("blit %d: A-%06x (%d) B-%06x (%d) C-%06x (%d) D-%06x (%d) W-%d H-%d\n",count,chipset->bltapt,chipset->bltamod,chipset->bltbpt,chipset->bltbmod,chipset->bltcpt,chipset->bltcmod,chipset->bltdpt,chipset->bltdmod,chipset->bltsizh,chipset->bltsizv);
            
            int useMask = chipset->bltcon0 >> 8;
            int shiftA = chipset->bltcon0 >> 12;
            int shiftB = chipset->bltcon1 >> 12;
            int minterm = chipset->bltcon0 & 255;
            int xIncrement = 1;
            int fillmode = (chipset->bltcon1 & 0x18) >> 3; // 1= Inclusive Fill, 2 = Exclusive Fill
            
            
            //if descend mode is on.
            if((chipset->bltcon1 & 2)){
                xIncrement = -1;
            }else{
                fillmode = 0;   //area fill only works in descending mode on real hardware
            }
            int fillExclusive = (fillmode & 2) ? 1 : 0;
            int fillCarryIn = (chipset->bltcon1 >> 2) & 1;  //FCI
            
            
            //all pointers are word addressed and masked for 2meg only
            uint32_t apt=(chipset->bltapt >> 1);
            uint32_t bpt=(chipset->bltbpt >> 1);
            uint32_t cpt=(chipset->bltcpt >> 1);
            uint32_t dpt=(chipset->bltdpt >> 1);
            
            //Shifted seems to work properly
            int amod=chipset->bltamod >> 1;
            int bmod=chipset->bltbmod >> 1;
            int cmod=chipset->bltcmod >> 1;
            int dmod=chipset->bltdmod >> 1;
            
            //these are signed so need to be divided not shifted... but doesn't work properly
            //int amod=chipset->bltamod / 2;
            //int bmod=chipset->bltbmod / 2;
            //int cmod=chipset->bltcmod / 2;
            //int dmod=chipset->bltdmod / 2;
            
            uint16_t adat=chipset->bltadat;
            uint16_t bdat=chipset->bltbdat;
            uint16_t cdat=chipset->bltcdat;
            // int ddat=chipset->bltddat;
            
            uint16_t afwm = chipset->bltafwm;
            uint16_t alwm = chipset->bltalwm;
            
            int sizeh = chipset->bltsizh;
            int sizev = chipset->bltsizv;
            int lastHWord=sizeh - 1;
            
            //All operations start with a Zero Flag
            chipset->dmaconr = chipset->dmaconr | 0x2000;   //set zero flag
            
            for(int y=0;y<sizev;++y){
                
                uint16_t previousA = 0;
                uint16_t previousB = 0;
                int fillCarry = fillCarryIn;
                
                
                for(int x=0;x<sizeh;++x){
                    uint16_t channelD = 0;
                    
                    //Channel A
                    if(useMask & 8){
                        adat = CHIPW(apt);
                        adat = adat << 8 | adat >> 8;
                        apt +=xIncrement;
                    }
                    uint16_t channelA = adat;
                    
                    //Channel B
                    if(useMask & 4){
                        bdat = CHIPW(bpt);
                        bdat = bdat << 8 | bdat >> 8;
                        bpt +=xIncrement;
                    }
                    uint16_t channelB = bdat;
                    
                    //Channel C
                    if(useMask & 2){
                        cdat = CHIPW(cpt);
                        cdat = cdat << 8 | cdat >> 8;
                        cpt +=xIncrement;
                    }
                    
                    
                    //Masking section
                    
                    if(x==0){
                        channelA = channelA & afwm;
                    }
                    
                    if(x==lastHWord){
                        channelA = channelA & alwm;
                    }
                    
                    uint16_t A = channelA; //Need to record the masked A for next cycle (Thanks to aros-sg on eab)
                    
                    //shifting section
                    if(xIncrement==-1){
                        
                        channelA = (previousA >> (16-shiftA)) | (channelA << shiftA);
                        channelB = (previousB >> (16-shiftB)) | (channelB << shiftB);
                        
                    }else{
                        
                        channelA = ((uint32_t)previousA << (16-shiftA)) | (channelA >> shiftA);
                        channelB = ((uint32_t)previousB << (16-shiftB)) | (channelB >> shiftB);
                        
                    }
                    //previousA = adat;
                    previousA = A;
                    previousB = bdat;
                    
                    
                    channelD = logicFunction(minterm, channelA, channelB, cdat);
                    
                    if(fillmode){
                        channelD = blitterFill(channelD, &fillCarry, fillExclusive);
                    }
                    
                    //Zero Flag
                    if(channelD!=0){
                        chipset->dmaconr = chipset->dmaconr & 0xDFFF; // clear zero flag
                    }
                    
                    //Channel D
                    if(useMask & 1){
                        
                        channelD = channelD << 8 | channelD >>8;
                        CHIPW(dpt) = channelD;
                        dpt +=xIncrement;
                    }
                    
                }
                
                if(xIncrement == -1){
                    apt -= amod;
                    bpt -= bmod;
                    cpt -= cmod;
                    dpt -= dmod;
                }else{
                    apt += amod;
                    bpt += bmod;
                    cpt += cmod;
                    dpt += dmod;
                }
                
            }
            
            
            chipset->bltsizh = 0; // all done;
            chipset->bltsizv = 0; // all done;
            
            //save blitter state - something might depend upon the blitter being in a known state
            chipset->bltapt = apt << 1;
            chipset->bltbpt = bpt << 1;
            chipset->bltcpt = cpt << 1;
            chipset->bltdpt = dpt << 1;
            chipset->bltadat = adat;
            chipset->bltbdat = bdat;
            chipset->bltcdat = cdat;
            //chipset->bltddat = ddat;
            
            chipset->dmaconr = chipset->dmaconr & 0xBFFF; //clear blitter busy bit
            putChipReg16[INTREQ](0x8040); // generate an interrupt!
            return;
        }
    }
    
}

