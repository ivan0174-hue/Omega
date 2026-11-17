//
//  DrawBridge.h
//  The Omega Project
//
//  Client for the DrawBridge (Arduino Floppy Disk Reader/Writer) by Rob Smith,
//  https://github.com/RobSmithDev/ArduinoFloppyDiskReader
//  The Arduino drives a real PC 3.5" floppy drive and talks over a USB serial
//  adapter (FTDI) at 2 Mbaud. Only reading is used here.
//
//  The serial port is supplied by the host (DbPort). Everything else is
//  portable C.
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef DrawBridge_h
#define DrawBridge_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct{
    int      (*write)(const void* data, int count);  // returns bytes written, < 0 error
    int      (*read)(void* data, int count);         // non blocking: bytes available now (0 = none)
    uint32_t (*micros)(void);                        // microsecond clock
}DbPort;

typedef enum{
    DB_NOT_FOUND = 0,   // no answer on the port
    DB_READY,
    DB_ERROR            // answered, then failed
}DbState;

int     dbOpen(const DbPort* port);         // 1 = DrawBridge found (dbVersion() tells which firmware)
void    dbClose(void);
DbState dbState(void);
const char* dbVersion(void);                // e.g. "V1.9"
int     dbDiskPresent(void);                // 1 yes, 0 no, -1 unknown (firmware < 1.8 or error)

// Read one track: raw MFM bits of ~1.1 revolutions (starting anywhere), MSB
// first. Returns the number of bits, <= 0 on error. Turns the motor on when
// needed. Matches FloppyTrackReader (Floppy.h).
int     dbReadTrack(int drive, int cylinder, int side, uint8_t* bits, int maxBytes);

// Call about once per frame: switches the motor off after a few idle seconds.
void    dbIdle(void);

// Statistics for the status line
extern int dbTracksRead, dbReadErrors;
extern int dbDataLost;                  // reads that arrived incomplete (bytes lost on the USB link)
extern int dbLastPackedBytes, dbLastBits;   // last read: bytes received, MFM bits decoded
extern uint32_t dbLastReadUs;           // last read: time taken (us)
const uint8_t* dbLastPacked(void);      // last read: the bytes as received (diagnostics)

// Decoding of the packed stream sent by the firmware (exposed for tests):
// each byte holds four 2-bit codes, 01 = "01", 10 = "001", 11 = "0001".
int     dbUnpack(const uint8_t* packed, int count, uint8_t* bits, int maxBytes);

#ifdef __cplusplus
}
#endif

#endif
