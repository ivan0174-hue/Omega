//
//  Paula.c
//  The Omega Project
//
//  Paula audio emulation.
//
//  Each channel plays 8 bit signed samples, two per data word (high byte
//  first). A sample lasts AUDxPER colour clocks. With DMA the channel starts
//  at AUDxLC, reads AUDxLEN words and then starts again from AUDxLC; an audio
//  interrupt is raised every time a block starts (the program then writes the
//  location/length of the next block, which is how music players chain
//  samples). Without DMA the CPU writes AUDxDAT and gets an interrupt when the
//  word has been played.
//
//  Channels 0 and 3 are left, 1 and 2 are right. Volume 0..64.
//  ADKCON attach: a channel can modulate the volume and/or period of the next
//  one instead of being heard.
//
//  Output: the channel levels are averaged over each output sample period
//  (box filter), then the Amiga 500 output filters are applied: the fixed
//  ~4.9 kHz RC low pass and, while the power LED is bright (CIA-A PRA bit 1 =
//  0), the 12 dB/oct ~3.1 kHz "LED" filter. Finally a DC blocker, like the
//  output capacitors.
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <string.h>
#include "Paula.h"
#include "Chipset.h"
#include "CIA.h"

extern Chipset_t chipset;

#define PERIOD_MIN      60          // below this the hardware cannot keep up anyway
#define RING_FRAMES     16384       // must be a power of two

typedef struct{
    uint32_t pt;        // byte address of the next word
    uint32_t lenCnt;    // words left in this block
    int      perCnt;    // colour clocks left for the current sample
    uint16_t dat;       // word being played
    uint16_t pending;   // manual mode: next word written by the CPU
    int      hasPending;
    int      state;     // 0 idle, 2 high byte, 3 low byte
    int      dma;       // DMA running
    int8_t   sample;
    int      attachToggle;  // attach vol+per: alternate words
}PaulaChannel;

static PaulaChannel ch[4];
static int level[4];            // sample * volume, cached
static int mixL, mixR;          // current mix
static int64_t sumL, sumR;
static int64_t sumCount;

// Event driven: nothing is done on a colour clock unless a channel moves to
// its next sample or an output sample is due ("toEvent"). Register writes
// that affect the sound first bring Paula up to date (paulaSync).
int paulaElapsed;               // colour clocks since the last update
int paulaToEvent = 1;           // colour clocks until the next update is needed
#define elapsed paulaElapsed
#define toEvent paulaToEvent
static int64_t sampleTimer;     // 16.16 colour clocks until the next output sample
static int64_t cyclesPerSample = ((int64_t)PAULA_CCK_HZ << 16) / PAULA_DEFAULT_RATE;
static double outputRate = PAULA_DEFAULT_RATE;
static int enabled = 1;
static int separation = 100;

static int16_t ring[RING_FRAMES * 2];
static volatile uint32_t ringWrite, ringRead;

// filters (floating point, per output sample: ~50k per second)
static float rcA, ledA;         // one pole coefficients
static float rcL, rcR;
static float led1L, led1R, led2L, led2R;
static float dcA, dcInL, dcInR, dcOutL, dcOutR;
static float filterRate;

// ---------------------------------------------------------------- helpers

static float expNeg(float x){       // e^-x for x >= 0, no libm needed
    int halvings = 0;
    while(x > 0.5f){ x *= 0.5f; halvings++; }
    float term = 1.0f, sum = 1.0f;
    for(int i=1;i<10;++i){ term *= -x / (float)i; sum += term; }
    while(halvings--){ sum *= sum; }
    return sum;
}

static float onePole(float cutoff, float rate){
    return 1.0f - expNeg(6.2831853f * cutoff / rate);
}

static void setupFilters(float rate){
    filterRate = rate;
    rcA  = onePole(4900.0f, rate);
    // two cascaded one-pole sections, each placed so that the pair is -3 dB at 3.1 kHz
    ledA = onePole(3100.0f / 0.6436f, rate);
    dcA  = 1.0f - 6.2831853f * 5.0f / rate;    // ~5 Hz high pass
}

static inline uint16_t chipWord(uint32_t byteAddr){
    uint16_t w = CHIPW(byteAddr >> 1);
    return (uint16_t)((w << 8) | (w >> 8));
}

static inline int channelVolume(int n){
    uint16_t v = (n==0) ? chipset.aud0vol : (n==1) ? chipset.aud1vol : (n==2) ? chipset.aud2vol : chipset.aud3vol;
    v &= 0x7F;
    return v > 64 ? 64 : v;
}

static inline int channelPeriod(int n){
    uint16_t p = (n==0) ? chipset.aud0per : (n==1) ? chipset.aud1per : (n==2) ? chipset.aud2per : chipset.aud3per;
    int per = p ? p : 65536;
    return per < PERIOD_MIN ? PERIOD_MIN : per;
}

static inline uint32_t channelLocation(int n){   // registers keep word addresses
    uint32_t lc = (n==0) ? chipset.aud0lc : (n==1) ? chipset.aud1lc : (n==2) ? chipset.aud2lc : chipset.aud3lc;
    return (lc << 1) & 0x1FFFFE;
}

static inline uint32_t channelLength(int n){
    uint16_t l = (n==0) ? chipset.aud0len : (n==1) ? chipset.aud1len : (n==2) ? chipset.aud2len : chipset.aud3len;
    return l ? l : 65536;
}

static void raiseInterrupt(int n){
    putChipReg16[INTREQ](0x8000 | (0x80 << n));
}

static void updateMix(void){
    int l = level[0] + level[3];
    int r = level[1] + level[2];
    if(separation >= 100){
        mixL = l; mixR = r;
    }else{
        // blend: separation 0 = mono
        int s = separation;
        mixL = (l * (100 + s) + r * (100 - s)) / 200;
        mixR = (r * (100 + s) + l * (100 - s)) / 200;
    }
}

static int attachedVolume(int n){ return n < 3 && (chipset.adkconr & (1 << n)); }
static int attachedPeriod(int n){ return n < 3 && (chipset.adkconr & (0x10 << n)); }

static void setLevel(int n){
    PaulaChannel* c = &ch[n];
    // a stopped channel holds its last level; modulators are not heard
    int l = (attachedVolume(n) || attachedPeriod(n)) ? 0 : c->sample * channelVolume(n);
    if(l != level[n]){
        level[n] = l;
        updateMix();
    }
}

// A word of an attached (modulating) channel goes to the next channel's registers.
static void modulate(int n, uint16_t word){
    int v = attachedVolume(n), p = attachedPeriod(n);
    int toVolume = v && (!p || ch[n].attachToggle == 0);
    if(v && p) ch[n].attachToggle ^= 1;
    int t = n + 1;
    if(toVolume){
        if(t==1) chipset.aud1vol = word; else if(t==2) chipset.aud2vol = word; else chipset.aud3vol = word;
    }else{
        if(t==1) chipset.aud1per = word; else if(t==2) chipset.aud2per = word; else chipset.aud3per = word;
    }
}

// DMA: fetch the next word, restarting the block (and interrupting) when it ends.
static uint16_t fetchWord(int n){
    PaulaChannel* c = &ch[n];
    uint16_t w = chipWord(c->pt);
    c->pt = (c->pt + 2) & 0x1FFFFE;
    if(--c->lenCnt == 0){
        c->pt = channelLocation(n);
        c->lenCnt = channelLength(n);
        raiseInterrupt(n);
    }
    return w;
}

static void startWord(int n, uint16_t w){
    PaulaChannel* c = &ch[n];
    c->dat = w;
    if(attachedVolume(n) || attachedPeriod(n)){
        modulate(n, w);
    }
    c->state = 2;
    c->sample = (int8_t)(w >> 8);
    c->perCnt = channelPeriod(n);
    setLevel(n);
}

// ---------------------------------------------------------------- API

void paulaReset(void){
    memset(ch, 0, sizeof(ch));
    memset(level, 0, sizeof(level));
    mixL = mixR = 0;
    sumL = sumR = 0; sumCount = 0;
    elapsed = 0; toEvent = 1;
    sampleTimer = cyclesPerSample;
    rcL = rcR = led1L = led1R = led2L = led2R = 0;
    dcInL = dcInR = dcOutL = dcOutR = 0;
    setupFilters((float)outputRate);
}

void paulaSetRate(double hz){
    if(hz < 8000) hz = 8000;
    if(hz > 96000) hz = 96000;
    outputRate = hz;
    cyclesPerSample = (int64_t)((double)((int64_t)PAULA_CCK_HZ << 16) / hz);
    float f = (float)hz;
    if(f > filterRate * 1.05f || f < filterRate * 0.95f){
        setupFilters(f);
    }
}

void paulaSetEnabled(int on){ enabled = on; }

void paulaFollowQueue(int queuedFrames, int targetFrames){
    static float smooth = -1;
    if(targetFrames <= 0) return;
    if(smooth < 0) smooth = (float)queuedFrames;
    smooth += 0.05f * ((float)queuedFrames - smooth);
    float err = (targetFrames - smooth) / (float)targetFrames;     // >0: queue too empty -> produce more
    float adj = err * 0.005f;
    if(adj > 0.005f) adj = 0.005f;
    if(adj < -0.005f) adj = -0.005f;
    paulaSetRate(PAULA_DEFAULT_RATE * (1.0 + adj));
}

void paulaSetStereoSeparation(int percent){
    separation = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    updateMix();
}

void paulaWriteDat(int n, uint16_t value){
    paulaSync();
    PaulaChannel* c = &ch[n];
    if(c->dma){
        return;             // DMA owns the data register
    }
    if(c->state == 0){
        startWord(n, value);
    }else{
        c->pending = value;
        c->hasPending = 1;
    }
    paulaRefresh();
}

static void emitSample(void){
    float l = (float)(sumCount ? sumL / sumCount : 0);
    float r = (float)(sumCount ? sumR / sumCount : 0);
    sumL = sumR = 0; sumCount = 0;

    rcL += rcA * (l - rcL);  rcR += rcA * (r - rcR);
    l = rcL; r = rcR;
    if((CIAA.pra & 2) == 0){            // power LED bright: audio filter on
        led1L += ledA * (l - led1L);    led1R += ledA * (r - led1R);
        led2L += ledA * (led1L - led2L); led2R += ledA * (led1R - led2R);
        l = led2L; r = led2R;
    }else{
        led1L = led2L = l; led1R = led2R = r;
    }
    // DC blocker
    float ol = l - dcInL + dcA * dcOutL;  dcInL = l; dcOutL = ol;
    float or_ = r - dcInR + dcA * dcOutR; dcInR = r; dcOutR = or_;

    // 4 channels at full volume: 2 x 127 x 64 = 16256 per side -> scale x2
    int il = (int)(ol * 2.0f), ir = (int)(or_ * 2.0f);
    if(il > 32767) il = 32767; if(il < -32768) il = -32768;
    if(ir > 32767) ir = 32767; if(ir < -32768) ir = -32768;

    uint32_t w = ringWrite;
    if(w - ringRead >= RING_FRAMES){
        return;                         // host is not reading: drop
    }
    ring[(w & (RING_FRAMES-1)) * 2]     = (int16_t)il;
    ring[(w & (RING_FRAMES-1)) * 2 + 1] = (int16_t)ir;
    ringWrite = w + 1;
}

// DMA on/off edges, volumes and the time of the next event.
void paulaRefresh(void){
    uint16_t dmacon = chipset.dmaconr;
    int master = dmacon & 0x200;

    for(int n=0;n<4;++n){
        PaulaChannel* c = &ch[n];
        int dmaOn = master && (dmacon & (1 << n));
        if(dmaOn != c->dma){
            c->dma = dmaOn;
            if(dmaOn){
                // DMA start: latch location and length, interrupt, first word
                c->pt = channelLocation(n);
                c->lenCnt = channelLength(n);
                c->hasPending = 0;
                raiseInterrupt(n);
                startWord(n, fetchWord(n));
            }else{
                c->state = 0;   // stops; the output holds the last level
            }
        }
        setLevel(n);
    }

    int next = (int)((sampleTimer + 0xFFFF) >> 16);
    for(int n=0;n<4;++n){
        if(ch[n].state && ch[n].perCnt < next){
            next = ch[n].perCnt;
        }
    }
    toEvent = next < 1 ? 1 : next;
}

// Bring Paula up to the current colour clock.
void paulaSync(void){
    int e = elapsed;
    elapsed = 0;
    if(e <= 0){
        return;
    }

    if(enabled){
        sumL += (int64_t)mixL * e; sumR += (int64_t)mixR * e; sumCount += e;
        sampleTimer -= (int64_t)e << 16;
        if(sampleTimer <= 0){
            emitSample();
            sampleTimer += cyclesPerSample;
            if(sampleTimer <= 0) sampleTimer = cyclesPerSample;
        }
    }else{
        sampleTimer = cyclesPerSample;
    }

    for(int n=0;n<4;++n){
        PaulaChannel* c = &ch[n];
        if(c->state == 0){
            continue;
        }
        c->perCnt -= e;
        if(c->perCnt > 0){
            continue;
        }
        if(c->state == 2){
            c->state = 3;
            c->sample = (int8_t)(c->dat & 0xFF);
            c->perCnt = channelPeriod(n);
            setLevel(n);
        }else if(c->dma){
            startWord(n, fetchWord(n));
        }else{
            raiseInterrupt(n);  // the CPU may write the next word
            if(c->hasPending){
                c->hasPending = 0;
                startWord(n, c->pending);
            }else{
                c->state = 0;
                setLevel(n);
            }
        }
    }
}

void paulaUpdate(void){
    paulaSync();
    paulaRefresh();
}

void paulaTick(void){
    if(++elapsed >= toEvent){
        paulaUpdate();
    }
}

int paulaAvailable(void){
    return (int)(ringWrite - ringRead);
}

int paulaRead(int16_t* dst, int maxFrames){
    uint32_t r = ringRead;
    int n = (int)(ringWrite - r);
    if(n > maxFrames) n = maxFrames;
    for(int i=0;i<n;++i){
        uint32_t k = ((r + i) & (RING_FRAMES-1)) * 2;
        dst[i*2]   = ring[k];
        dst[i*2+1] = ring[k+1];
    }
    ringRead = r + n;
    return n;
}
