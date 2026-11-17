//
//  DrawBridge.c
//  The Omega Project
//
//  Client for the DrawBridge Arduino floppy reader (firmware 1.x by Rob Smith).
//  Protocol (2 Mbaud, 8N1), as used by the reference PC software:
//    'x' 'R' '?'   abort / reset / version  ->  "1V1.9" (or "1V1,9")
//    '+'           motor on, waits for spin-up          -> '1'
//    '-'           motor off                            -> '1'
//    '#' NN        seek to cylinder NN (two digits)     -> '1' / '0'
//    '[' / ']'     upper (head 1) / lower (head 0) side -> '1'
//    '<'           read track                           -> '1', then the host
//                  sends 0 (start now) or 1 (at the index hole), then packed data bytes
//                  terminated by 0
//    '^'           disk present? (firmware 1.8+)        -> '1'/'#', '1'/'#'(protected)
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <string.h>
#include "DrawBridge.h"

#define REPLY_TIMEOUT   2000000u    // us: motor spin-up and long seeks
#define DATA_TIMEOUT     600000u    // us without data while reading a track
#define MOTOR_IDLE_US   8000000u    // motor off after 8 s without reads
#define PACKED_MAX      16384

int dbTracksRead = 0, dbReadErrors = 0, dbDataLost = 0, dbNotSpinning = 0;
int dbLastPackedBytes = 0, dbLastBits = 0;
uint32_t dbLastReadUs = 0;

static const DbPort* port = 0;
static DbState state = DB_NOT_FOUND;
static char version[8] = "";
static int motorOn = 0;
static int headCylinder = -1;
static int headSide = -1;
static uint32_t lastUse = 0;
static int firmwareMinor = 0;

static uint8_t rxBuf[256];
static int rxHead = 0, rxCount = 0;
static uint8_t packed[PACKED_MAX];

static void rxFlush(void){
    rxHead = rxCount = 0;
    uint8_t junk[64];
    uint32_t t = port->micros();
    while(port->micros() - t < 20000){      // drain whatever is still arriving
        if(port->read(junk, sizeof junk) > 0) t = port->micros();
    }
}

static int readByte(uint32_t timeoutUs){
    if(rxHead < rxCount){
        return rxBuf[rxHead++];
    }
    uint32_t start = port->micros();
    for(;;){
        int n = port->read(rxBuf, sizeof rxBuf);
        if(n > 0){
            rxHead = 1;
            rxCount = n;
            return rxBuf[0];
        }
        if(n < 0 || port->micros() - start > timeoutUs){
            return -1;
        }
    }
}

static int send(const char* data, int count){
    return port->write(data, count) == count;
}

// one command character (+ parameters), one status reply
static int command(const char* cmd, int count, uint32_t timeoutUs){
    if(!port || !send(cmd, count)) return -1;
    return readByte(timeoutUs);
}

int dbOpen(const DbPort* p){
    port = p;
    state = DB_NOT_FOUND;
    motorOn = 0;
    headCylinder = headSide = -1;
    version[0] = '\0';
    rxHead = rxCount = 0;

    // abort any streaming, reset, ask the version; look for "1V<d>[.,]<d>"
    if(!send("xR?", 3)) return 0;
    char win[5] = {0,0,0,0,0};
    uint32_t start = port->micros(), lastKick = start;
    for(;;){
        int c = readByte(5000);
        uint32_t now = port->micros();
        if(c >= 0){
            memmove(win, win + 1, 4);
            win[4] = (char)c;
            if(win[0]=='1' && win[1]=='V' && win[2]>='1' && win[2]<='9' && (win[3]=='.' || win[3]==',') && win[4]>='0' && win[4]<='9'){
                version[0]='V'; version[1]=win[2]; version[2]='.'; version[3]=win[4]; version[4]='\0';
                firmwareMinor = (win[2]-'0') * 10 + (win[4]-'0');
                break;
            }
        }else if(now - lastKick > 50000){
            lastKick = now;
            send("?", 1);
        }
        if(now - start > 3000000){
            return 0;
        }
    }
    rxFlush();
    state = DB_READY;
    return 1;
}

void dbClose(void){
    if(port && state == DB_READY && motorOn){
        command("-", 1, REPLY_TIMEOUT);
    }
    motorOn = 0;
    state = DB_NOT_FOUND;
    port = 0;
}

DbState dbState(void){ return state; }
const char* dbVersion(void){ return version; }

int dbDiskPresent(void){
    if(state != DB_READY || firmwareMinor < 18) return -1;
    int r = command("^", 1, REPLY_TIMEOUT);
    int wp = readByte(REPLY_TIMEOUT);
    // (the firmware stops the motor after the test only if it was off before)
    if(r < 0 || wp < 0) return -1;
    return r == '1';
}

int dbUnpack(const uint8_t* in, int count, uint8_t* bits, int maxBytes){
    int maxBits = maxBytes * 8, n = 0;
    memset(bits, 0, (size_t)maxBytes);
    for(int i=0;i<count;i++){
        for(int s=6;s>=0;s-=2){
            int code = (in[i] >> s) & 3;
            int zeros = code == 0 ? 4 : code;       // "0000", "01", "001", "0001"
            n += zeros;
            if(code && n < maxBits){
                bits[n >> 3] |= (uint8_t)(0x80 >> (n & 7));
            }
            if(code) n++;
            if(n >= maxBits) return maxBits;
        }
    }
    return n;
}

static int spinning(void){
    int r = command("^", 1, REPLY_TIMEOUT);
    int wp = readByte(REPLY_TIMEOUT);
    return r == '1' && wp >= 0;
}

static int readOnce(int cylinder, int side, uint8_t* bits, int maxBytes){
    char cmd[4];
    if(!motorOn){
        if(command("+", 1, REPLY_TIMEOUT) != '1') return -1;
        motorOn = 1;
        // The firmware's track read waits for flux transitions with no time
        // limit: if the disk is not turning (no disk, a drive that did not
        // start, power too weak for the motor) it would hang until the
        // Arduino is reset. So first check that data is coming from the head
        // ('^' gives up after ~0.2 s); if not, switch the motor off and on
        // once more.
        if(firmwareMinor >= 18 && !spinning()){
            command("-", 1, REPLY_TIMEOUT);
            motorOn = 0;
            if(command("+", 1, REPLY_TIMEOUT) != '1') return -1;
            motorOn = 1;
            if(!spinning()){
                dbNotSpinning++;
                return -1;
            }
        }
    }
    if(cylinder != headCylinder){
        cmd[0] = '#'; cmd[1] = (char)('0' + cylinder / 10); cmd[2] = (char)('0' + cylinder % 10);
        if(command(cmd, 3, REPLY_TIMEOUT) != '1'){ headCylinder = -1; return -1; }
        headCylinder = cylinder;
    }
    if(side != headSide){
        if(command(side ? "[" : "]", 1, REPLY_TIMEOUT) != '1'){ headSide = -1; return -1; }
        headSide = side;
    }
    if(command("<", 1, REPLY_TIMEOUT) != '1') return -1;
    if(!send("\0", 1)) return -1;                   // start at once (no need to wait for the index hole)
    int count = 0;
    for(;;){
        int c = readByte(DATA_TIMEOUT);
        if(c < 0) return -1;
        if(c == 0) break;
        if(count < PACKED_MAX) packed[count++] = (uint8_t)c;
    }
    dbLastPackedBytes = count;
    int n = dbUnpack(packed, count, bits, maxBytes);
    dbLastBits = n;
    // the firmware sends codes until it has 111104 bits: fewer means bytes were lost on the way
    if(n < (0x1900*2 + 0x440) * 8 - 256){
        dbDataLost++;
    }
    return n;
}

int dbReadTrack(int drive, int cylinder, int side, uint8_t* bits, int maxBytes){
    (void)drive;
    if(!port || cylinder < 0 || cylinder > 83) return -1;
    if(state != DB_READY){
        // lost contact earlier (e.g. a read went wrong): try again, at most every 2 s,
        // instead of giving up for the rest of the session
        static uint32_t lastTry = 0;
        static int tried = 0;
        if(tried && port->micros() - lastTry < 2000000u) return -1;
        tried = 1;
        lastTry = port->micros();
        rxFlush();
        if(!dbOpen(port)) return -1;
    }
    lastUse = port->micros();
    uint32_t t0 = lastUse;
    int n = readOnce(cylinder, side, bits, maxBytes);
    if(n <= 0){
        // resynchronise with the firmware and try again once
        rxFlush();
        send("x", 1);
        rxFlush();
        motorOn = 0; headCylinder = headSide = -1;
        if(dbOpen(port)){
            n = readOnce(cylinder, side, bits, maxBytes);
        }
    }
    lastUse = port ? port->micros() : 0;
    dbLastReadUs = lastUse - t0;
    if(n > 0) dbTracksRead++; else dbReadErrors++;
    if(n <= 0 && state == DB_NOT_FOUND) state = DB_ERROR;    // retried on the next read
    return n;
}

const uint8_t* dbLastPacked(void){ return packed; }

void dbIdle(void){
    if(state == DB_READY && motorOn && port->micros() - lastUse > MOTOR_IDLE_US){
        command("-", 1, REPLY_TIMEOUT);
        motorOn = 0;
    }
}
