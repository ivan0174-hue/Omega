//
//  Memory.c
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
//  2026 revision:
//  - every address is masked to 24 bits (68000 bus) before decoding
//  - custom chip register accesses are mirrored into 0xDFF000-0xDFF1FF and
//    bounds checked, so no access can index past the end of a handler table
//    (previously writes to autoconfig space 0xE80000 or long reads above
//    0xDFF020 called random function pointers and crashed)
//  - ROM boundary uses >= 0xF80000 (the first ROM byte was mapped to chip RAM)
//  - 32-bit write to the IDE/Gayle area no longer falls through into RAM
//  - writes to 0xE00000-0xF7FFFF (autoconfig / unmapped) are ignored
//  - big endian access is done byte-wise: no unaligned or type-punned loads,
//    which is undefined behaviour in C and faults on some ARM configurations

#include <stdint.h>
#include "Memory.h"
#include "Chipset.h"
#include "CIA.h"
#include "debug.h"
#include "DMA.h"
#include "Gayle.h"

#if defined(_MSC_VER)
__declspec(align(16)) unsigned char low16Meg[16777216];
#else
unsigned char low16Meg[16777216] __attribute__((aligned(16)));
#endif

#define ADDR_MASK   0xFFFFFF
#define ROM_BASE    0xF80000
#define CUSTOM_BASE 0xDFF000

// Number of entries in the read handler tables defined in Chipset.c
#define CHIPREG8_READS  32
#define CHIPREG16_READS 17   // indices 0..16 have handlers, the rest are write only
#define CHIPREG32_READS 16
#define DENISEID_INDEX  62

void loadROM(){
}

// ---- big endian helpers (compilers turn these into a single load + byteswap)
static inline uint32_t be16(const unsigned char* p){
    return ((uint32_t)p[0] << 8) | p[1];
}
static inline uint32_t be32(const unsigned char* p){
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void wbe16(unsigned char* p, uint32_t v){
    p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v;
}
static inline void wbe32(unsigned char* p, uint32_t v){
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

// Custom chip registers are only decoded on A1-A8: 0xDFF000-0xDFFFFF mirrors 0xDFF000-0xDFF1FF
static inline unsigned int customOffset(unsigned int address){
    return (address - CUSTOM_BASE) & 0x1FF;
}

// CIA register is selected by A8-A11
static inline int ciaReg(unsigned int address){
    return (address >> 8) & 0xF;
}

// ---- configurable memory (Amiga 500 default: 512K chip + 512K slow RAM)
uint32_t chipRamSize = 512 * 1024;
uint32_t chipRamMask = 512 * 1024 - 1;
uint32_t slowRamSize = 512 * 1024;
static uint32_t slowRamEnd = SLOW_BASE + 512 * 1024;

static uint32_t clampSize(uint32_t size, uint32_t min, uint32_t max){
    return size < min ? min : size > max ? max : size;
}

void memoryConfigure(uint32_t chipBytes, uint32_t slowBytes){
    // chip RAM: 256K, 512K, 1M or 2M (a power of two: smaller sizes are mirrored
    // through the 2MB chip area, as the Kickstart memory test expects)
    chipBytes = clampSize(chipBytes, 256*1024, CHIP_MAX);
    uint32_t c = 256*1024;
    while(c * 2 <= chipBytes) c *= 2;
    chipRamSize = c;
    chipRamMask = c - 1;

    // slow ("trapdoor") RAM at $C00000, in 256K steps, up to $DBFFFF
    slowBytes = clampSize(slowBytes, 0, SLOW_MAX) & ~(256*1024 - 1);
    slowRamSize = slowBytes;
    slowRamEnd = SLOW_BASE + slowBytes;
}

static inline unsigned int chipAddr(unsigned int address){
    return address & chipRamMask;
}

static inline int isSlowRam(unsigned int address){
    return address >= SLOW_BASE && address < slowRamEnd;
}


//******************************* READ *************************************

//******************************* READ *************************************
//
// Map ($000000-$FFFFFF):
//   chip RAM (mirrored up to $1FFFFF) | $BFD000/$BFE001 CIAs | slow RAM from $C00000
//   | $DA0000-$DEFFFF Gayle/clock | $DFF000 custom chips | $F80000 ROM

unsigned int chipReadByte(unsigned int address){
    address &= ADDR_MASK;

    if(address >= ROM_BASE){
        return low16Meg[address];
    }
    if(address > 0xDFFFFF){     // autoconfig / unmapped
        return 0;
    }
#ifdef THREADED_CPU
    waitFreeSlot();
#endif
    if(address >= CUSTOM_BASE){
        unsigned int off = customOffset(address);
        debugChipAddress = off;
        return off < CHIPREG8_READS ? getChipReg8[off]() : 0;
    }
    if(isSlowRam(address)){
        return low16Meg[address];
    }
    if(address >= GAYLE_BASE){
        return readGayleB(address);
    }
    if(address >= SLOW_BASE){   // no RAM fitted there
        return 0;
    }
    if(address >= 0xBFE001){
        return CIARead(&CIAA, ciaReg(address - 0xBFE001));
    }
    if(address >= 0xBFD000){
        return CIARead(&CIAB, ciaReg(address - 0xBFD000));
    }
    if(address > 0x1FFFFF){     // no fast RAM
        return 0;
    }
    return low16Meg[chipAddr(address)];
}

unsigned int chipReadWord(unsigned int address){
    address &= ADDR_MASK & ~1u;

    if(address >= ROM_BASE){
        return be16(&low16Meg[address]);
    }
    if(address > 0xDFFFFF){
        return 0;
    }
#ifdef THREADED_CPU
    waitFreeSlot();
#endif
    if(address >= CUSTOM_BASE){
        unsigned int index = customOffset(address) >> 1;
        debugChipAddress = index;
        if(index < CHIPREG16_READS){
            return getChipReg16[index]();
        }
        // Reading a write-only register returns bus noise; DeniseID is the only readable one up there
        return chipset.deniseid;
    }
    if(isSlowRam(address)){
        return be16(&low16Meg[address]);
    }
    if(address >= GAYLE_BASE){
        return readGayle(address);
    }
    if(address >= SLOW_BASE){
        return 0;
    }
    if(address >= 0xBFE001){
        return CIARead(&CIAA, ciaReg(address - 0xBFE001));
    }
    if(address >= 0xBFD000){
        return CIARead(&CIAB, ciaReg(address - 0xBFD000));
    }
    if(address > 0x1FFFFF){
        return 0;
    }
    return be16(&low16Meg[chipAddr(address)]);
}

unsigned int chipReadLong(unsigned int address){
    address &= ADDR_MASK & ~1u;

    if(address >= ROM_BASE){
        if(address > ADDR_MASK - 3){   // long read straddling the top of memory
            return (be16(&low16Meg[address]) << 16) | be16(&low16Meg[0]);
        }
        return be32(&low16Meg[address]);
    }
    if(address > 0xDFFFFF){
        return 0;
    }
#ifdef THREADED_CPU
    waitFreeSlot();
#endif
    if(address >= CUSTOM_BASE){
        unsigned int index = customOffset(address) >> 1;
        debugChipAddress = index;
        if(index < CHIPREG32_READS){
            return getChipReg32[index]();
        }
        return (chipReadWord(address) << 16) | chipReadWord(address + 2);
    }
    if(address >= SLOW_BASE){
        // the two halves may fall in different areas (end of slow RAM)
        if(isSlowRam(address) && isSlowRam(address + 2)){
            return be32(&low16Meg[address]);
        }
        return (chipReadWord(address) << 16) | chipReadWord(address + 2);
    }
    if(address >= 0xBFE001){
        return CIARead(&CIAA, ciaReg(address - 0xBFE001));
    }
    if(address >= 0xBFD000){
        return CIARead(&CIAB, ciaReg(address - 0xBFD000));
    }
    if(address > 0x1FFFFF){
        return 0;
    }
    return (be16(&low16Meg[chipAddr(address)]) << 16) | be16(&low16Meg[chipAddr(address + 2)]);
}


//******************************* WRITE ************************************

void chipWriteByte(unsigned int address,unsigned int value){
    address &= ADDR_MASK;

    if(address > 0xDFFFFF){     // ROM, autoconfig, unmapped
        return;
    }
#ifdef THREADED_CPU
    waitFreeSlot();
#endif
    if(address >= CUSTOM_BASE){
        debugChipAddress = customOffset(address) >> 1;
        return;                 // byte writes to custom chips are not emulated
    }
    if(isSlowRam(address)){
        low16Meg[address] = (unsigned char)value;
        return;
    }
    if(address >= GAYLE_BASE){
        writeGayleB(address, value);
        return;
    }
    if(address >= SLOW_BASE){
        return;
    }
    if(address >= 0xBFE001){
        CIAWrite(&CIAA, ciaReg(address - 0xBFE001), value);
        return;
    }
    if(address >= 0xBFD000){
        CIAWrite(&CIAB, ciaReg(address - 0xBFD000), value);
        return;
    }
    if(address >= 0xBFA001){    // CIA A partial decode used by Kickstart 3
        CIAWrite(&CIAA, ciaReg(address - 0xBFA001), value);
        return;
    }
    if(address > 0x1FFFFF){
        return;
    }
    low16Meg[chipAddr(address)] = (unsigned char)value;
}

void chipWriteWord(unsigned int address,unsigned int value){
    address &= ADDR_MASK & ~1u;
    value &= 0xFFFF;

    if(address > 0xDFFFFF){
        return;
    }
#ifdef THREADED_CPU
    waitFreeSlot();
#endif
    if(address >= CUSTOM_BASE){
        unsigned int index = customOffset(address) >> 1;   // < 256, table has 256 entries
        debugChipAddress = index;
        debugChipValue = value;
        putChipReg16[index]((uint16_t)value);
        return;
    }
    if(isSlowRam(address)){
        wbe16(&low16Meg[address], value);
        return;
    }
    if(address >= GAYLE_BASE){
        writeGayle(address, value);
        return;
    }
    if(address >= SLOW_BASE){
        return;
    }
    if(address >= 0xBFD000){    // CIA: word writes are ignored
        return;
    }
    if(address > 0x1FFFFF){
        return;
    }
    wbe16(&low16Meg[chipAddr(address)], value);
}

void chipWriteLong(unsigned int address,unsigned int value){
    address &= ADDR_MASK & ~1u;

    if(address > 0xDFFFFF){
        return;
    }
#ifdef THREADED_CPU
    waitFreeSlot();
#endif
    if(address >= CUSTOM_BASE){
        unsigned int index = customOffset(address) >> 1;
        debugChipAddress = index;
        putChipReg32[index](value);
        return;
    }
    if(address >= SLOW_BASE){
        if(isSlowRam(address) && isSlowRam(address + 2)){
            wbe32(&low16Meg[address], value);
            return;
        }
        chipWriteWord(address, value >> 16);
        chipWriteWord(address + 2, value & 0xFFFF);
        return;
    }
    if(address >= 0xBFD000){
        return;
    }
    if(address > 0x1FFFFF){     // no fast RAM
        return;
    }
    wbe16(&low16Meg[chipAddr(address)], value >> 16);
    wbe16(&low16Meg[chipAddr(address + 2)], value);
}
