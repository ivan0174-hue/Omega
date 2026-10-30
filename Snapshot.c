#include "Paula.h"
//
//  Snapshot.c
//  The Omega Project
//
//  See Snapshot.h. File layout (all little endian, host structures as is;
//  the Raspberry Pi 64 bit build and x86-64 desktops use the same layout):
//    "OMEGASN1"  u32 version
//    u32 size + Chipset_t      u32 size + Internal_t     u32 size + 2 x CIA_t
//    u32 count + CPU registers
//    floppy: driveSelected, then 4 x {pra, prb, idMode, index, cylinder, side}
//    u32 size + chip RAM       u32 size + slow RAM ($C00000)
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <string.h>
#include "Snapshot.h"
#include "Chipset.h"
#include "CIA.h"
#include "CPU.h"
#include "Floppy.h"
#include "Memory.h"
#include "m68k.h"

#define SNAP_VERSION  2             // 2: memory configuration + 1.75MB slow RAM area
#define SLOWRAM_BASE  SLOW_BASE
#define SLOWRAM_SIZE  SLOW_MAX

static const int cpuRegs[] = {
    M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
    M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6,
    M68K_REG_USP, M68K_REG_ISP, M68K_REG_SR, M68K_REG_PC
};
#define CPU_REG_COUNT (int)(sizeof(cpuRegs)/sizeof(cpuRegs[0]))

static int put32(SnapshotWriter w, void* c, uint32_t v){
    return w(&v, 4, c);
}

static int putBlock(SnapshotWriter w, void* c, const void* data, uint32_t size){
    return put32(w, c, size) && w(data, size, c);
}

int snapshotSave(SnapshotWriter w, void* c){
    if(!w(("OMEGASN1"), 8, c) || !put32(w, c, SNAP_VERSION)) return 0;
    if(!putBlock(w, c, &chipset, sizeof(chipset))) return 0;
    if(!putBlock(w, c, &internal, sizeof(internal))) return 0;
    if(!put32(w, c, 2 * sizeof(CIA_t)) || !w(&CIAA, sizeof(CIA_t), c) || !w(&CIAB, sizeof(CIA_t), c)) return 0;

    if(!put32(w, c, CPU_REG_COUNT)) return 0;
    for(int i=0;i<CPU_REG_COUNT;++i){
        if(!put32(w, c, m68k_get_reg(NULL, cpuRegs[i]))) return 0;
    }

    if(!put32(w, c, (uint32_t)driveSelected)) return 0;
    for(int i=0;i<4;++i){
        int32_t f[6] = {df[i].pra, df[i].prb, df[i].idMode, df[i].index, df[i].cylinder, df[i].side};
        if(!w(f, sizeof(f), c)) return 0;
    }

    if(!put32(w, c, chipRamSize) || !put32(w, c, slowRamSize)) return 0;
    if(!putBlock(w, c, &low16Meg[0], CHIP_MAX)) return 0;
    if(!putBlock(w, c, &low16Meg[SLOWRAM_BASE], SLOWRAM_SIZE)) return 0;
    return 1;
}

// ---- loading

typedef struct{
    const uint8_t* p;
    size_t left;
}Reader_t;

static int get(Reader_t* r, void* out, size_t size){
    if(r->left < size) return 0;
    memcpy(out, r->p, size);
    r->p += size;
    r->left -= size;
    return 1;
}

static int get32(Reader_t* r, uint32_t* v){
    return get(r, v, 4);
}

// a block whose size must match the structure exactly
static int getExact(Reader_t* r, void* out, size_t size){
    uint32_t n;
    return get32(r, &n) && n == size && get(r, out, size);
}

int snapshotLoad(const uint8_t* data, size_t size){
    Reader_t r = {data, size};
    char magic[8];
    uint32_t version, n;
    if(!get(&r, magic, 8) || memcmp(magic, "OMEGASN1", 8) != 0) return 0;
    if(!get32(&r, &version) || (version != 1 && version != SNAP_VERSION)) return 0;

    uint16_t* chipramW = internal.chipramW;     // the only pointer: keep ours
    if(!getExact(&r, &chipset, sizeof(chipset))) return 0;
    if(!getExact(&r, &internal, sizeof(internal))) return 0;
    internal.chipramW = chipramW;

    if(!get32(&r, &n) || n != 2 * sizeof(CIA_t) || !get(&r, &CIAA, sizeof(CIA_t)) || !get(&r, &CIAB, sizeof(CIA_t))) return 0;

    uint32_t regs[32];
    if(!get32(&r, &n) || n != CPU_REG_COUNT) return 0;
    for(int i=0;i<CPU_REG_COUNT;++i){
        if(!get32(&r, &regs[i])) return 0;
    }

    uint32_t sel;
    if(!get32(&r, &sel)) return 0;
    driveSelected = (int)sel;
    for(int i=0;i<4;++i){
        int32_t f[6];
        if(!get(&r, f, sizeof(f))) return 0;
        df[i].pra = (uint8_t)f[0]; df[i].prb = (uint8_t)f[1]; df[i].idMode = f[2];
        df[i].index = f[3]; df[i].cylinder = f[4]; df[i].side = f[5];
    }

    if(version == 1){
        // old snapshots: 2MB chip RAM, 1.5MB slow RAM ($C00000-$D9FFFF)
        memoryConfigure(CHIP_MAX, 0x180000);
        if(!getExact(&r, &low16Meg[0], CHIP_MAX)) return 0;
        if(!getExact(&r, &low16Meg[SLOWRAM_BASE], 0x1A0000)) return 0;
    }else{
        uint32_t chipSize, slowSize;
        if(!get32(&r, &chipSize) || !get32(&r, &slowSize)) return 0;
        memoryConfigure(chipSize, slowSize);
        if(!getExact(&r, &low16Meg[0], CHIP_MAX)) return 0;
        if(!getExact(&r, &low16Meg[SLOWRAM_BASE], SLOWRAM_SIZE)) return 0;
    }

    // CPU: SR first (selects user/supervisor mode), then the stack pointers,
    // which Musashi stores in A7 or in the shadow slot depending on that mode
    m68k_set_reg(M68K_REG_SR,  regs[17]);
    m68k_set_reg(M68K_REG_USP, regs[15]);
    m68k_set_reg(M68K_REG_ISP, regs[16]);
    for(int i=0;i<15;++i){
        m68k_set_reg(cpuRegs[i], regs[i]);
    }
    m68k_set_reg(M68K_REG_PC, regs[18]);
    m68k_set_reg(M68K_REG_PREF_ADDR, 0xFFFFFFFF);   // invalidate the prefetch of the old session
    paulaReset();       // audio restarts from the restored registers (channels with DMA on)
    checkInterrupt(&chipset);
    return 1;
}
