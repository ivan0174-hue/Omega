//
//  Memory.h
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

#ifndef Memory_h
#define Memory_h

#include <stdint.h>

// Memory configuration (see memoryConfigure). Changes take effect immediately;
// the host resets the machine afterwards.
#define CHIP_MAX   (2 * 1024 * 1024)           // chip RAM: 256K - 2MB at $000000
#define SLOW_BASE  0xC00000                    // slow RAM ($C00000 - $DBFFFF)
#define SLOW_MAX   (0xDC0000 - SLOW_BASE)      // 1.75MB, as far as Kickstart looks
#define GAYLE_BASE 0xDA0000                    // IDE/clock area when no slow RAM is there

extern uint32_t chipRamSize;
extern uint32_t chipRamMask;                   // chipRamSize - 1
extern uint32_t slowRamSize;
void memoryConfigure(uint32_t chipBytes, uint32_t slowBytes);

#define CHIPTOP (chipRamMask)

extern unsigned char low16Meg[16777216]; // only the DMA should use this

void loadROM();

unsigned int chipReadByte(unsigned int address);
unsigned int chipReadWord(unsigned int address);
unsigned int chipReadLong(unsigned int address);

void chipWriteByte(unsigned int address,unsigned int value);
void chipWriteWord(unsigned int address,unsigned int value);
void chipWriteLong(unsigned int address,unsigned int value);

#endif /* Memory_h */
