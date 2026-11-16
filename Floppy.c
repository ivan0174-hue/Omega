//
//  Floppy.c
//  The Omega Project
//  https://github.com/h5n1xp/Omega
//
//  Created by Matt Parsons on 02/02/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//  <h5n1xp@gmail.com>
//
//  Clocking and Checksum Code contributed by Dirk Hoffmann
//
//  This Source Code Form is subject to the terms of the
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <string.h>
#include <stdlib.h>
#include "Floppy.h"
#include "Chipset.h"
#include "Memory.h"



void encodeBlock(uint8_t* source, uint8_t* destination,int size){
    
    for(int i=0;i<size;i++){
        destination[i] = (source[i] >> 1) & 0x55;
        destination[i+size] = (source[i]) & 0x55;
    }
    
}

uint8_t addClockBits(uint8_t previous, uint8_t value) {
    // Clear all previously set clock bits
    value &= 0x55;
    
    // Compute clock bits (clock bit values are inverted)
    uint8_t lShifted = (value << 1);
    uint8_t rShifted = (value >> 1) | (previous << 7);
    uint8_t cBitsInv = lShifted | rShifted;
    
    // Reverse the computed clock bits
    uint64_t cBits = cBitsInv ^ 0xAA;
    
    // Return original value with the clock bits added
    return value | cBits;
}

// Convert an in-memory ADF image to MFM. The host layer is responsible for loading the file,
// so this module has no dependency on POSIX file I/O (needed for bare metal).
void ADF2MFM(const uint8_t* adf, int size, uint8_t* mfm){
    
    if(adf == NULL || size <= 0){
        return;
    }
    
    //512 bytes per sector
    int sectors = size/512;
    
    //22 sectors per track (each side 11 sectors)
    int tracks = (sectors / 22);
    
    //the MFM buffer holds at most 82 cylinders; larger images would overflow it
    if(tracks > 82){
        printf("ADF too large (%d cylinders), truncating to 82\n",tracks);
        tracks = 82;
    }
    
    uint8_t lowlevelSector[544]; //bytes per low level sector
    
    int count = 0;
    int s = 0;
    
    for(int track = 0;track<tracks;track++){
        for(int side=0;side<2;side++){
            for(int sector=0;sector<11;sector++){
                
                //int secCountDown = 11 - sector;
                
                
                //printf("%d: Track %d, Side: %d, Sector %d (%d)\n",s,track,side,sector,secCountDown);
                
                //Build the sector
                lowlevelSector[0] = 0x0;
                lowlevelSector[1] = 0x0;
                
                lowlevelSector[2] = 0xA1; // will be a sync mark
                lowlevelSector[3] = 0xA1; // will be a sync mark
                
                //sector info
                
                lowlevelSector[4] = 0xFF;
                lowlevelSector[5] = track << 1 | side;
                lowlevelSector[6] = sector;
                lowlevelSector[7] = 11 - sector;
                
                //Sector label
                lowlevelSector[8]  = 0x0;
                lowlevelSector[9]  = 0x0;
                lowlevelSector[10] = 0x0;
                lowlevelSector[11] = 0x0;
                
                lowlevelSector[12] = 0x0;
                lowlevelSector[13] = 0x0;
                lowlevelSector[14] = 0x0;
                lowlevelSector[15] = 0x0;
                
                lowlevelSector[16] = 0x0;
                lowlevelSector[17] = 0x0;
                lowlevelSector[18] = 0x0;
                lowlevelSector[19] = 0x0;
                
                lowlevelSector[20] = 0x0;
                lowlevelSector[21] = 0x0;
                lowlevelSector[22] = 0x0;
                lowlevelSector[23] = 0x0;
                
                
                //data
                for(int i=0;i<512;++i){
                    lowlevelSector[32+i] = adf[i+(count*512)];
                    
                }
                
                
                
                //***************
                
                //Encode
                
                mfm[s+0] = 0xAA;
                mfm[s+1] = 0xAA;
                mfm[s+2] = 0xAA;
                mfm[s+3] = 0xAA;
                
                mfm[s+4] = 0x44;
                mfm[s+5] = 0x89;
                mfm[s+6] = 0x44;
                mfm[s+7] = 0x89;
                
                //info
                encodeBlock(&lowlevelSector[4], &mfm[s+8], 4); // adds 8 bytes
                
                //Disklabel
                encodeBlock(&lowlevelSector[8], &mfm[s+16], 16);//adds 32 bytes
                
                //Data section
                encodeBlock(&lowlevelSector[32], &mfm[s+64], 512);
                
                
                //Header checksum
                uint8_t hcheck[4] = { 0, 0, 0, 0 };
                for(unsigned i = 8; i < 48; i += 4) {
                    hcheck[0] ^= mfm[s+i];
                    hcheck[1] ^= mfm[s+i+1];
                    hcheck[2] ^= mfm[s+i+2];
                    hcheck[3] ^= mfm[s+i+3];
                }
                
                lowlevelSector[24] = hcheck[0];
                lowlevelSector[25] = hcheck[1];
                lowlevelSector[26] = hcheck[2];
                lowlevelSector[27] = hcheck[3];
                
                //header checksum
                encodeBlock(&lowlevelSector[24], &mfm[s+48], 4); //adds 8 bytes
                
                
                // Data checksum
                uint8_t dcheck[4] = { 0, 0, 0, 0 };
                for(unsigned i = 64; i < 1088; i += 4) {
                    dcheck[0] ^= mfm[s+i];
                    dcheck[1] ^= mfm[s+i+1];
                    dcheck[2] ^= mfm[s+i+2];
                    dcheck[3] ^= mfm[s+i+3];
                }
                
                lowlevelSector[28] = dcheck[0];
                lowlevelSector[29] = dcheck[1];
                lowlevelSector[30] = dcheck[2];
                lowlevelSector[31] = dcheck[3];
                
                //Encode Data checksum
                encodeBlock(&lowlevelSector[28], &mfm[s+56], 4); //adds 8 bytes
                
                
                
                //Add clocking bits
                for(int i=8;i<1088;i++){
                    uint8_t previous = mfm[s+i-1];
                    
                    mfm[s+i] = addClockBits(previous,mfm[s+i]);
                    //mfm[s+i] = clocking(previous, mfm[s+i]);
                    
                }
                
                s += 1088;    //Why not 1088, which is the size of the data we've produced
                count +=1;
                
                
            }
            
            //Add clocking bits to the track gap
            mfm[s]   = addClockBits(mfm[s-1],0);
            mfm[s+1] = 0xA8;
            mfm[s+2] = 0x55;
            mfm[s+3] = 0x55;
            mfm[s+4] = 0xAA;
            
            for(int i=5;i<700;i++){
                uint8_t previous = mfm[s+i-1];
                
                mfm[s+i] = addClockBits(previous,0);
                //mfm[s+i] = clocking(previous, mfm[s+i]);
                
            }
            
            s += 830;   //pad track to make 12798 bytes to meet the ADF-EXT spec.
            //printf("\n");
        }
        
    }
    //printf("loaded");
}
//*******************************



int floppySync = 0;
int driveSelected=0;
Fd_t df[4];

void floppyIndexReset(){
    
       df[driveSelected].index = 4;
    
}

static int liveTrackBytes(void);

uint8_t floppyDataRead(){ //this function should be called by the DMA
    
    int trackBytes = liveTrackBytes();  // 12668 for ADF images, the real length for a real disk
    int position   = (df[driveSelected].cylinder * (12798 * 2)) + (df[driveSelected].side  * 12798) + df[driveSelected].index;
    
    df[driveSelected].index +=1;

    if(df[driveSelected].index>=trackBytes){ //ADF track has 12798 bytes, but on a normal AmigaOS disk 12668 are used.
        df[driveSelected].index= 0;
        CIAIndex(&CIAB);    // generate CIAB index interupt
        
        //int stinkmog = (df[driveSelected].track * (12798 * 2)) + (df[driveSelected].side  * 12798) + df[driveSelected].index;
        //uint8_t* dri = &df[driveSelected].mfmData[stinkmog];
        //printf("wartest du!");
        
    }
    
    uint8_t retVal = df[driveSelected].mfmData[position];
    
    /*
    if(retVal==0){
        //printf("Uh oh!");
    }
    */
    
    return retVal;
}


// set by floppyInit() when an image has been loaded for the drive
static int imageLoaded[4] = {0,0,0,0};

void floppyInsert(int drive){
    
    if(drive < 0 || drive > 3 || !imageLoaded[drive]){
        //no disk image for this drive: inserting an empty disk would make
        //Kickstart retry reading it forever with the motor running
        printf("No disk image for df%d:\n",drive);
        return;
    }

    if(df[drive].idMode !=0){
        //Only Vaild drives have an ID Mode == 0 
        return;
    }
    
    if( (df[drive].pra & 0x4) == 0x4){
        df[drive].pra &= 0xFB;      // eject disk;
        printf("Disk ejected from df%d:\n",drive);
    }else{
        df[drive].pra |= 0x04;      // insert disk
        printf("Disk inserted in df%d:\n",drive);
    }
    
}

static int attached[4] = {0,0,0,0};

// The drive is connected to the Amiga but has no disk image yet
void floppyAttach(int drive){
    if(drive < 0 || drive > 3) return;
    attached[drive] = 1;
    imageLoaded[drive] = 0;
    df[drive].idMode = -1;
    df[drive].index = 0;
    df[drive].cylinder = 0;
    df[drive].side = 0;
    df[drive].pra  &= 0xFB;      // no disk
    df[drive].pra  &= 0xEF;      // cylinder 0
}

// Attach the drive and return its MFM buffer; the caller fills it with an image
uint8_t* floppyInit(int drive){
    floppyAttach(drive);
    imageLoaded[drive] = 1;
    return df[drive].mfmData;
}

// Load a new image while the emulator runs (the disk must be out of the drive).
// Does not reset the drive identification, so it can be used at any time.
static int live[4] = {0,0,0,0};

int floppyLoadImage(int drive, const uint8_t* adf, int size){
    if(drive < 0 || drive > 3 || (df[drive].pra & 0x04)){
        return 0;
    }
    memset(df[drive].mfmData, 0, sizeof(df[drive].mfmData));
    ADF2MFM(adf, size, df[drive].mfmData);
    imageLoaded[drive] = 1;
    live[drive] = 0;
    df[drive].pra |= 0x08;          // not write protected (as before)
    return 1;
}

// ---------------------------------------------------------------------------
// Real disk

#define LIVE_TRACKS     (82*2)
#define TRACK_AREA      12798       // bytes per track in df[].mfmData
#define REV_MIN_BITS    90000       // one revolution: 300 rpm, 2 us cells = 100000 bits (+-10 %)
#define REV_MAX_BITS    110000

static FloppyTrackReader trackReader = NULL;

// OMEGA_FLOPPY_DEBUG=1 (desktop builds): report every real track read
#ifdef AARCH
static int floppyDebug(void){ return 0; }
#else
static int floppyDebug(void){ static int d = -1; if(d < 0) d = getenv("OMEGA_FLOPPY_DEBUG") != NULL; return d; }
#endif
static uint8_t liveRaw[LIVE_TRACKS][FLOPPY_RAW_TRACK_BYTES];   // raw stream as read (one revolution)
static int liveRawBits[LIVE_TRACKS];        // 0 = not read yet, -1 = read failed
static int liveLen[4][LIVE_TRACKS];         // bytes of the prepared (sync aligned) track, 0 = not prepared
static uint16_t liveSync[4][LIVE_TRACKS];   // DSKSYNC the track was prepared for
static int liveDrive = -1;                  // the drive the raw cache belongs to

void floppySetTrackReader(FloppyTrackReader reader){
    trackReader = reader;
}

void floppyLiveFlush(void){
    memset(liveRawBits, 0, sizeof(liveRawBits));
    memset(liveLen, 0, sizeof(liveLen));
}

int floppyIsLive(int drive){
    return drive >= 0 && drive <= 3 && live[drive];
}

int floppyLoadLive(int drive){
    if(drive < 0 || drive > 3 || (df[drive].pra & 0x04) || trackReader == NULL){
        return 0;
    }
    for(int i=0;i<4;i++) live[i] = 0;   // one real drive
    floppyLiveFlush();
    memset(df[drive].mfmData, 0, sizeof(df[drive].mfmData));
    liveDrive = drive;
    live[drive] = 1;
    imageLoaded[drive] = 1;
    df[drive].pra &= 0xF7;          // write protected: writes to the real disk are not supported
    return 1;
}

static inline int getBit(const uint8_t* b, int i){
    return (b[i >> 3] >> (7 - (i & 7))) & 1;
}

static inline void putBit(uint8_t* b, int i, int v){
    if(v) b[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
    else  b[i >> 3] &= (uint8_t)~(0x80 >> (i & 7));
}

static inline uint64_t get64(const uint8_t* b, int bit){    // 64 bits starting at any bit position
    uint64_t v = 0;
    int byte = bit >> 3, shift = bit & 7;
    for(int i=0;i<8;i++) v = (v << 8) | b[byte + i];
    if(shift){
        v = (v << shift) | (uint64_t)(b[byte + 8] >> (8 - shift));
    }
    return v;
}

// The reader returns a little more than one revolution (the drive starts at the
// index hole and reads ~1.1 turns). Find the revolution length: the offset at
// which the stream starts repeating itself.
static int findRevolutionBySector(const uint8_t* bits, int nbits){
    // An AmigaDOS sector header (sync + info + label + checksum, 416 bits) is
    // unique on the track: find one, then the same header one turn later.
    const int hdr = 416;
    uint16_t sr = 0;
    for(int i=0;i<nbits - REV_MIN_BITS - hdr;i++){
        sr = (uint16_t)((sr << 1) | getBit(bits, i));
        if(i < 15 || sr != 0x4489) continue;
        int s1 = i - 15;
        uint64_t ref[7];
        for(int w=0;w<6;w++) ref[w] = get64(bits, s1 + 32 + w*64);   // after the two sync words
        if(getBit(bits, s1 + 16) == 0 && get64(bits, s1 + 16) >> 48 == 0x4489){
            int best = -1, bestDiff = 6;            // allow a few misread bits
            for(int p=REV_MIN_BITS; p<=REV_MAX_BITS && s1 + p + 32 + 6*64 + 72 < nbits; p++){
                int diff = 0;
                for(int w=0;w<6 && diff < bestDiff;w++) diff += __builtin_popcountll(ref[w] ^ get64(bits, s1 + p + 32 + w*64));
                if(diff < bestDiff){ bestDiff = diff; best = p; if(!diff) break; }
            }
            if(best > 0) return best;
        }
        i += 15;
    }
    return -1;
}

int floppyFindRevolution(const uint8_t* bits, int nbits){
    int bySector = findRevolutionBySector(bits, nbits);
    if(bySector > 0){
        return bySector;
    }
    // Other formats. The bits come from counting the gaps between flux transitions, so the
    // second turn repeats the first bit for bit (apart from rare misreads).
    // Compare the whole overlap: it always contains sector headers, which are
    // unique (a gap or an empty sector alone would match at many offsets).
    int maxP = nbits - 1024;
    if(maxP > REV_MAX_BITS) maxP = REV_MAX_BITS;
    if(maxP < REV_MIN_BITS){
        return nbits;
    }
    int best = -1;
    double bestRatio = 0.02;                    // accept up to 2 % misread bits
    for(int p=REV_MIN_BITS; p<=maxP; p++){
        int len = (nbits - p - 72) & ~63;
        int limit = (int)(len * bestRatio);
        int diff = 0;
        for(int x=0; x<len && diff <= limit; x+=64){
            diff += __builtin_popcountll(get64(bits, x) ^ get64(bits, x + p));
        }
        if(diff <= limit){
            double ratio = (double)diff / len;
            if(best < 0 || ratio < bestRatio){
                bestRatio = ratio;
                best = p;
                if(diff == 0) break;
            }
        }
    }
    if(best < 0){                               // no repetition (unformatted / noise)
        return nbits < REV_MAX_BITS ? nbits : 100000;
    }
    return best;
}

// Paula finds the sync word at any bit position and then reads whole words
// from there. The emulated DMA compares whole words, so re-pack the circular
// stream so that every sync word ends on a word boundary (a few bits of gap
// are added before each sync). Returns the track length in bytes (even).
int floppyAlignTrack(const uint8_t* bits, int nbits, uint16_t sync, uint8_t* out, int maxBytes){
    int maxBits = maxBytes * 8;
    memset(out, 0, (size_t)maxBytes);
    if(nbits <= 16){
        return 0;
    }
    // Put the seam (end of the revolution) in the middle of the track gap: the
    // longest stretch between two sync words. Sectors follow each other with
    // no gap, so the seam must not fall just before any other sync.
    int start = 0;
    {
        int first = -1, prev = -1, bestGap = -1;
        uint16_t sr = 0;
        for(int i=0;i<nbits + 15;i++){
            sr = (uint16_t)((sr << 1) | getBit(bits, i % nbits));
            if(i < 15 || sr != sync) continue;
            int pos = (i - 15) % nbits;
            if(prev >= 0 && pos - prev > 32 && pos - prev > bestGap){ bestGap = pos - prev; start = prev + (pos - prev) / 2 + 4000; }
            if(first < 0) first = pos;
            if(pos - prev > 32 || prev < 0) prev = pos;
            if(i >= nbits) break;
        }
        if(first >= 0 && prev >= 0){                 // the stretch across the end of the buffer
            int across = first + nbits - prev;
            if(across > bestGap){ bestGap = across; start = prev + across / 2 + 4000; }
        }
        if(bestGap < 0) start = 0;
        start %= nbits;
    }
    uint16_t sr;
    int o = 0;
    sr = 0;
    for(int k=0;k<nbits && o < maxBits;k++){
        int i = start + k; if(i >= nbits) i -= nbits;
        int bit = getBit(bits, i);
        sr = (uint16_t)((sr << 1) | bit);
        putBit(out, o++, bit);
        if(k >= 15 && sr == sync && (o & 15) != 0){
            // insert the padding before the 32 bits ahead of the sync (the
            // sector preamble, $AAAA $AAAA): Kickstart 1.3 checks them too
            int syncStart = o - 16;
            int keep = syncStart >= 32 ? 32 : syncStart;
            int from = syncStart - keep;
            int pad = (16 - (syncStart & 15)) & 15;
            if(o + pad > maxBits) break;
            uint64_t pre = 0;
            for(int j=0;j<keep;j++) pre = (pre << 1) | (uint64_t)getBit(out, from + j);
            o = from;
            for(int j=0;j<pad;j++) putBit(out, o++, (pad - 1 - j) & 1);   // more of the "1010" pattern
            for(int j=keep-1;j>=0;j--) putBit(out, o++, (int)((pre >> j) & 1));
            for(int j=15;j>=0;j--) putBit(out, o++, (sync >> j) & 1);
        }
    }
    o &= ~15;
    return o / 8;
}

static inline uint32_t mfmLong(const uint8_t* p){
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Scan a sync-aligned track for AmigaDOS sectors. For each sector found:
// where its first sync word is, its number (from the header) and whether
// both checksums are right. Returns the number of sectors found.
#define MAX_FOUND 40
typedef struct{ int pos; int sector; int ok; }FoundSector;

static int scanSectors(const uint8_t* m, int len, FoundSector* out){
    int n = 0;
    for(int i=2;i + 2 + 1080 <= len && n < MAX_FOUND;i+=2){
        if(m[i] != 0x44 || m[i+1] != 0x89 || (m[i+2] == 0x44 && m[i+3] == 0x89)){
            continue;
        }
        const uint8_t* h = m + i + 2;           // info, label, checksums, data
        uint32_t sum = 0;
        for(int k=0;k<40;k+=4) sum ^= mfmLong(h + k);
        sum &= 0x55555555;
        uint32_t hsum = ((mfmLong(h + 40) & 0x55555555) << 1) | (mfmLong(h + 44) & 0x55555555);
        int ok = sum == hsum;
        sum = 0;
        for(int k=56;k<56+1024;k+=4) sum ^= mfmLong(h + k);
        sum &= 0x55555555;
        uint32_t dsum = ((mfmLong(h + 48) & 0x55555555) << 1) | (mfmLong(h + 52) & 0x55555555);
        ok = ok && sum == dsum;
        uint32_t info = ((mfmLong(h) & 0x55555555) << 1) | (mfmLong(h + 4) & 0x55555555);
        out[n].pos = (m[i-2] == 0x44 && m[i-1] == 0x89) ? i - 2 : i;
        out[n].sector = (int)((info >> 8) & 0xFF);
        out[n].ok = ok;
        n++;
        i += 1080;
    }
    return n;
}

// A misread bit in the preamble or in the first sync word of a good sector
// is harmless for the data but Kickstart 1.3 rejects the sector: write them
// back as they are on a disk ($AAAA $AAAA $4489 $4489).
static void normalizeSectors(uint8_t* m, int len){
    FoundSector f[MAX_FOUND];
    int n = scanSectors(m, len, f);
    for(int k=0;k<n;k++){
        if(!f[k].ok) continue;
        int first = (m[f[k].pos + 2] == 0x44 && m[f[k].pos + 3] == 0x89) ? f[k].pos : f[k].pos - 2;
        if(first < 4) continue;
        uint8_t* p = m + first - 4;
        int prevBit = first >= 5 ? (p[-1] & 1) : 0;
        p[0] = prevBit ? 0x2A : 0xAA;
        p[1] = 0xAA; p[2] = 0xAA; p[3] = 0xAA;
        p[4] = 0x44; p[5] = 0x89; p[6] = 0x44; p[7] = 0x89;
    }
}

int floppyGoodSectors(const uint8_t* m, int len){
    FoundSector f[MAX_FOUND];
    int n = scanSectors(m, len, f), seen = 0, count = 0;
    for(int k=0;k<n;k++){
        if(f[k].ok && f[k].sector < 22 && !(seen & (1 << f[k].sector))){
            seen |= 1 << f[k].sector;
            count++;
        }
    }
    return count;
}

// Read a track from the real drive. An AmigaDOS track with damaged sectors is
// read again (up to 8 times) and the good copies of each sector are put
// together, like the DrawBridge software does; otherwise a misread bit would
// stay in the cache for good.
#define READ_TRIES 8
static uint8_t tryBuf[FLOPPY_RAW_TRACK_BYTES];
static uint8_t alignTry[READ_TRIES][TRACK_AREA];

static uint8_t mergeBuf[TRACK_AREA];

// Start from the read in which all sectors were found (even if some are
// damaged), and replace each damaged sector with a good copy from another read.
static int combineReads(int tries, const int* alignLen, int* outLen){
    int base = -1, baseScore = -1;
    for(int r=0;r<tries;r++){
        FoundSector f[MAX_FOUND];
        int nf = scanSectors(alignTry[r], alignLen[r], f);
        int score = (nf >= 11 ? 1000 : nf * 10) + floppyGoodSectors(alignTry[r], alignLen[r]);
        if(score > baseScore){ baseScore = score; base = r; }
    }
    if(base < 0) return -1;
    memcpy(mergeBuf, alignTry[base], TRACK_AREA);
    int len = alignLen[base];
    FoundSector f[MAX_FOUND];
    int nf = scanSectors(mergeBuf, len, f);
    for(int k=0;k<nf;k++){
        if(f[k].ok) continue;
        int sector = f[k].sector;
        if(sector >= 11){                       // header unreadable: guess from the neighbours
            if(k > 0 && f[k-1].sector < 11) sector = (f[k-1].sector + 1) % 11;
            else if(k + 1 < nf && f[k+1].sector < 11) sector = (f[k+1].sector + 10) % 11;
            else continue;
        }
        for(int r=0;r<tries;r++){
            if(r == base) continue;
            FoundSector g[MAX_FOUND];
            int ng = scanSectors(alignTry[r], alignLen[r], g), done = 0;
            for(int j=0;j<ng;j++){
                if(g[j].ok && g[j].sector == sector && f[k].pos + 1084 <= len && g[j].pos + 1084 <= alignLen[r]){
                    memcpy(mergeBuf + f[k].pos, alignTry[r] + g[j].pos, 1084);
                    done = 1;
                    break;
                }
            }
            if(done) break;
        }
    }
    normalizeSectors(mergeBuf, len);
    *outLen = len;
    return floppyGoodSectors(mergeBuf, len);
}

static void readLiveTrack(int d, int t){
    int alignLen[READ_TRIES];
    int tries = 0, bestGood = -1, bestBits = 0;
    for(int attempt=0; attempt<READ_TRIES; attempt++){
        int n = trackReader ? trackReader(d, t >> 1, t & 1, tryBuf, FLOPPY_RAW_TRACK_BYTES) : -1;
        if(n <= 0){
            if(attempt == 0) break;             // drive error / no disk
            continue;
        }
        n = floppyFindRevolution(tryBuf, n);
        alignLen[tries] = floppyAlignTrack(tryBuf, n, 0x4489, alignTry[tries], TRACK_AREA);
        normalizeSectors(alignTry[tries], alignLen[tries]);
        int good = floppyGoodSectors(alignTry[tries], alignLen[tries]);
        if(floppyDebug()) fprintf(stderr, "real track %d: %d bits per revolution, %d good sectors\n", t, n, good);
        if(good > bestGood){
            bestGood = good; bestBits = n;
            memcpy(liveRaw[t], tryBuf, FLOPPY_RAW_TRACK_BYTES);
        }
        tries++;
        if(good == 0 || good >= 11) break;      // not AmigaDOS, or complete
        int len = 0;
        int good2 = combineReads(tries, alignLen, &len);
        if(floppyDebug()) fprintf(stderr, "real track %d: %d good sectors after combining %d reads\n", t, good2, tries);
        if(good2 > bestGood){
            // the combined track is already sync aligned: keep it as the "raw" stream
            memset(liveRaw[t], 0, FLOPPY_RAW_TRACK_BYTES);
            memcpy(liveRaw[t], mergeBuf, (size_t)len);
            bestBits = len * 8;
            bestGood = good2;
        }
        if(bestGood >= 11) break;
    }
    liveRawBits[t] = bestBits > 0 ? bestBits : -1;
}

// Make sure the track under the head of the selected (real) drive is ready
static void prepareLiveTrack(void){
    int d = driveSelected;
    int t = df[d].cylinder * 2 + df[d].side;
    if(t < 0 || t >= LIVE_TRACKS) return;
    if(liveLen[d][t] && liveSync[d][t] == chipset.dsksync){
        return;
    }
    if(liveRawBits[t] == 0){
        readLiveTrack(d, t);
    }
    uint8_t* dst = &df[d].mfmData[df[d].cylinder * (TRACK_AREA * 2) + df[d].side * TRACK_AREA];
    int len;
    if(liveRawBits[t] > 0){
        len = floppyAlignTrack(liveRaw[t], liveRawBits[t], chipset.dsksync, dst, TRACK_AREA);
        if(chipset.dsksync == 0x4489) normalizeSectors(dst, len);
    }else{
        memset(dst, 0, TRACK_AREA);     // unreadable: no sync at all, like an unformatted track
        len = 12668;
    }
    if(len < 1000) len = 12668;
    if(floppyDebug()) fprintf(stderr, "track %d prepared for sync %04x: %d bytes, %d good sectors\n", t, chipset.dsksync, len, floppyGoodSectors(dst, len));
    liveLen[d][t] = len;
    liveSync[d][t] = chipset.dsksync;
    if(df[d].index >= len) df[d].index = 0;
}

static int liveTrackBytes(void){
    int d = driveSelected;
    if(!live[d]){
        return 12668;
    }
    prepareLiveTrack();
    int t = df[d].cylinder * 2 + df[d].side;
    return (t >= 0 && t < LIVE_TRACKS && liveLen[d][t]) ? liveLen[d][t] : 12668;
}

// Machine reset: the disks stay in the drives, the drive electronics restart
// (motor off, not ready, identification sequence again on the next motor-off)
void floppyReset(void){
    for(int i=0;i<4;++i){
        if(!attached[i]){
            continue;
        }
        df[i].idMode = -1;
        df[i].index = 0;
        df[i].side = 0;
        df[i].prb = 0xFF;
        df[i].pra = (uint8_t)((df[i].pra & 0x04) | 0x20 | (live[i] ? 0 : 0x08));   // keep disk-in, not ready
        if(df[i].cylinder == 0){
            df[i].pra &= 0xEF;      // track 0
        }else{
            df[i].pra |= 0x10;
        }
    }
    driveSelected = 0;
    floppySync = 0;
}

int floppyHasImage(int drive){
    return drive >= 0 && drive <= 3 && imageLoaded[drive];
}

int floppyIsInserted(int drive){
    return drive >= 0 && drive <= 3 && (df[drive].pra & 0x04) != 0;
}

// Insert the loaded image; returns 0 if the drive is not ready to accept it yet
// (during the first moments after reset the drive is being identified)
int floppyInsertDisk(int drive){
    if(!floppyHasImage(drive) || df[drive].idMode != 0){
        return 0;
    }
    if(!floppyIsInserted(drive)){
        floppyInsert(drive);
    }
    return 1;
}

void floppyEjectDisk(int drive){
    if(floppyIsInserted(drive)){
        df[drive].pra &= 0xFB;
        printf("Disk ejected from df%d:\n",drive);
    }
}

void floppyState(){
    CIAA.pra &= 0xC3;
    CIAA.pra |= df[driveSelected].pra;
}

void floppySetState(){            //To be called when Writes to CIAB prb happen.
    
    static uint8_t PRB;
    static int count = 0;
    
    PRB = CIAB.prb;
    
    switch (PRB & 0x78) {
        case 0x78:
            CIAA.pra &=0xC3;    //Need to take the Drives off the floppy bus.
            return;
            break;
            
        case 0x70:
            driveSelected = 0;
            break;

        case 0x68:
            driveSelected = 1;
            break;
            
        case 0x58:
            driveSelected = 2;
            break;
            
        case 0x38:
            driveSelected = 3;
            break;
            
        default:
            break;
    }
    //count++;

    //printf("%d: ",count);
    
    //ID mode... to identify external drives...
     if(df[driveSelected].idMode>0){   // Id mode
         //printf("DF%d ID Mode: %d\n",driveSelected,df[driveSelected].idMode); // The Disk Ready sitnal is pusled 32 times to signle a drive is present on the bus
         df[driveSelected].pra  &= 0xDF;     //Drive ready flag signals the drive is there
         df[driveSelected].idMode -=1;
         CIAA.pra |= df[driveSelected].pra & 0x3C;
         return;
     }
    
    

    
    // If no change in state just return
    if(PRB == df[driveSelected].prb){
        floppyState();
        //printf("---\n");
        return;
    }
    


    

    //printf("DF%d - ",driveSelected);
    
    if(PRB & 0x80){
        //printf(" Motor Off ");
        //df[driveSelected].prb  |= 0x80;
        
        if(df[driveSelected].idMode==-1){   //is this the first time the motor has been turned off?
            df[driveSelected].idMode = 32;  //if so activate ID Mode
        }
        
        df[driveSelected].pra  |= 0x20;     //Drive not ready
        
    }else{
        //printf(" Motor On ");
        //df[driveSelected].prb  &= 0x7F;
        
        if(df[driveSelected].pra & 0x4){     // if disk is inserted  then
            df[driveSelected].pra  &= 0xDF;  // Drive ready
        }
        //floppySync=0;
    }
    

    
    //Step head (don't step again if we've already stepped)
    if( (PRB & 0x1) && !(df[driveSelected].prb & 0x1) ){
        printf("%04x - DF%d Click\n",count,driveSelected);
        
        if(PRB & 0x2){
            df[driveSelected].cylinder -=1;
            //printf("DF%d Head Stepped back: %d\n",driveSelected,df[driveSelected].cylinder);
        }else{
            df[driveSelected].cylinder +=1;
           //printf("DF%d Head Stepped forward: %d\n",driveSelected,df[driveSelected].cylinder);
        }
        
        if(df[driveSelected].cylinder < 0){
            df[driveSelected].cylinder = 0;
        }
    
        
        if(df[driveSelected].cylinder >79){    //not sure why sometimes the drive tries to go up to track 80.. with ks1.3
            df[driveSelected].cylinder = 79;
        }
        
        //floppySync = 0;
        
        //printf(" to track %d|",df[driveSelected].track);
        
    }
    
    
    if(df[driveSelected].cylinder==0){
        df[driveSelected].pra  &= 0xEF; //Track 0 reached
    }else{
        df[driveSelected].pra  |= 0X10; // not track 0;
    }
    
    
    if(PRB & 0x4){
        df[driveSelected].side = 0;
        //df[driveSelected].prb |= 0x4;
        //printf(" -lower surface.\n");
        //floppySync = 0;
        
    }else{
        df[driveSelected].side = 1;
        //df[driveSelected].prb &= 0xFB;
        //printf(" -upper surface.\n");
        //floppySync = 0;
    }
    
    
    df[driveSelected].prb = PRB;
    
    //printf("\n");
    CIAA.pra &= 0xC3;
    CIAA.pra |= df[driveSelected].pra;
    
    //CIAB.prb |= df[driveSelected].prb & 0x87;
    

}

