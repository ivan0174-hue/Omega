//
//  Paula.h
//  The Omega Project
//
//  Paula audio: 4 channels (DMA and CPU driven), interrupts, stereo mix,
//  Amiga 500 output filters, resampled for the host.
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef Paula_h
#define Paula_h

#include <stdint.h>

#define PAULA_CCK_HZ        3546895     // PAL colour clock
#define PAULA_DEFAULT_RATE  48000

#ifdef __cplusplus
extern "C" {
#endif

void paulaReset(void);
void paulaTick(void);                               // once per colour clock
// inline version of paulaTick() for the colour clock loop
extern int paulaElapsed, paulaToEvent;
void paulaUpdate(void);
#define PAULA_TICK() do{ if(++paulaElapsed >= paulaToEvent) paulaUpdate(); }while(0)
void paulaWriteDat(int channel, uint16_t value);    // CPU write to AUDxDAT
// Register writes that change the sound (DMACON, ADKCON, AUDxVOL):
// paulaSync() before the register changes, paulaRefresh() after.
void paulaSync(void);
void paulaRefresh(void);

// Output: interleaved stereo 16 bit frames. The host takes everything that was
// produced (normally once per video frame, ~961 frames at 48 kHz).
int  paulaRead(int16_t* dst, int maxFrames);
int  paulaAvailable(void);
void paulaSetRate(double hz);       // output rate; hosts trim it slightly to follow their sound clock
// Keeps the host's sound queue near targetFrames: called once per video frame
// with the frames still queued in the sound device; nudges the output rate by
// at most +-0.5 % so the emulator clock and the sound card clock never drift
// apart (no clicks from underruns, no growing delay).
void paulaFollowQueue(int queuedFrames, int targetFrames);
void paulaSetEnabled(int on);       // 0: produce no samples (no audio device)
void paulaSetStereoSeparation(int percent);   // 100 = hard Amiga stereo, 0 = mono

#ifdef __cplusplus
}
#endif

#endif
