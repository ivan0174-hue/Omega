//
//  Input.c
//  The Omega Project
//
//  Portable keyboard and mouse handling (moved out of the SDL host layer).
//  Host backends translate their native events into Amiga raw key codes and
//  mouse deltas and call the functions below.
//
//  Keyboard events are queued and sent to CIA-A one per emulated frame. The
//  previous code wrote each event straight into the serial data register, so
//  two events arriving in the same poll (e.g. Shift + key, or a quick
//  press/release) overwrote each other and keys were lost.
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "Host.h"
#include "CIA.h"
#include "CPU.h"
#include "Chipset.h"

#define KEY_CTRL    0x63
#define KEY_LAMIGA  0x66
#define KEY_RAMIGA  0x67
#define KEY_QUEUE   32

static uint8_t keyQueue[KEY_QUEUE];
static int keyHead = 0;
static int keyTail = 0;
static int resetMask = 0;
static void applyPorts(void);

static void queueKey(uint8_t rawCode){
    int next = (keyHead + 1) % KEY_QUEUE;
    if(next == keyTail){
        return;             // queue full: drop, as a real keyboard buffer would
    }
    keyQueue[keyHead] = rawCode;
    keyHead = next;
}

static int resetBit(uint8_t key){
    switch(key){
        case KEY_CTRL:   return 1;
        case KEY_LAMIGA: return 2;
        case KEY_RAMIGA: return 4;
    }
    return 0;
}

void inputReset(void){
    keyHead = keyTail = 0;
    resetMask = 0;
    applyPorts();       // the chipset registers were just cleared
}

void inputKeyDown(uint8_t amigaKey){
    amigaKey &= 0x7F;
    queueKey(amigaKey);

    resetMask |= resetBit(amigaKey);
    if(resetMask == 7){     // Ctrl + Amiga + Amiga
        resetMask = 0;
        keyHead = keyTail = 0;
        cpu_pulse_reset();
    }
}

void inputKeyUp(uint8_t amigaKey){
    amigaKey &= 0x7F;
    queueKey(amigaKey | 0x80);
    resetMask &= ~resetBit(amigaKey);
}

void inputFrame(void){
    applyPorts();       // e.g. after a program wrote CIA-A PRA
    if(keyTail == keyHead){
        return;
    }
    uint8_t raw = keyQueue[keyTail];
    keyTail = (keyTail + 1) % KEY_QUEUE;

    // The keyboard sends the code rotated left by one (up/down flag in bit 0), inverted
    uint8_t code = raw & 0x7F;
    uint8_t up = raw >> 7;
    CIAWrite(&CIAA, 0xC, (uint8_t)~((code << 1) | up));
    keyboardInt();
}

// ---------------------------------------------------------------------------
// Game ports
//
// Port 1 (the "mouse" port) and port 2 (the joystick port). Each has
//   - JOYxDAT: mouse counters, or the joystick switches encoded as
//     bit 1 = right, bit 9 = left, bit 0 = right XOR down, bit 8 = left XOR up
//   - fire button: CIA-A PRA bit 6 (port 1) / bit 7 (port 2), active low
//   - second button (pin 9): POTGOR bit 10 (port 1) / bit 14 (port 2), active low;
//     the right mouse button is the second button of port 1.

static int mouseLeft = 0, mouseRight = 0;
static unsigned joyBits[2] = {0, 0};
static int port1Joystick = 0;           // a joystick instead of the mouse in port 1

static uint16_t joystickDat(unsigned bits){
    int up = (bits & JOY_UP) != 0, down = (bits & JOY_DOWN) != 0;
    int left = (bits & JOY_LEFT) != 0, right = (bits & JOY_RIGHT) != 0;
    if(up && down){ up = down = 0; }            // impossible on a real stick
    if(left && right){ left = right = 0; }
    return (uint16_t)((left << 9) | ((left ^ up) << 8) | (right << 1) | (right ^ down));
}

static void applyPorts(void){
    int fire1 = mouseLeft  || (port1Joystick && (joyBits[0] & JOY_FIRE));
    int fire1b = mouseRight || (port1Joystick && (joyBits[0] & JOY_FIRE2));
    int fire2 = (joyBits[1] & JOY_FIRE) != 0;
    int fire2b = (joyBits[1] & JOY_FIRE2) != 0;

    uint8_t pra = CIAA.pra | 0xC0;
    if(fire1) pra &= (uint8_t)~0x40;
    if(fire2) pra &= (uint8_t)~0x80;
    CIAA.pra = pra;

    uint16_t pot = 0x5500;                      // nothing pressed: all data lines high
    if(fire1b) pot &= (uint16_t)~0x0400;
    if(fire2b) pot &= (uint16_t)~0x4000;
    chipset.potinp = pot;

    chipset.joy1dat = joystickDat(joyBits[1]);
    if(port1Joystick){
        chipset.joy0dat = joystickDat(joyBits[0]);
    }
}

void inputJoystick(int port, unsigned bits){
    joyBits[port ? 1 : 0] = bits;
    applyPorts();
}

void inputPort1Joystick(int on){
    if(port1Joystick && !on){
        chipset.joy0dat = 0;                    // back to mouse counters
    }
    port1Joystick = on;
    applyPorts();
}

void inputMouseMove(int dx, int dy){
    if(port1Joystick){
        return;                                 // the mouse is unplugged
    }
    uint8_t tx = chipset.joy0dat & 255;
    uint8_t ty = chipset.joy0dat >> 8;
    tx += dx;
    ty += dy;
    chipset.joy0dat = (uint16_t)((ty << 8) | tx);
}

void inputMouseButtons(int left, int right){
    // (the previous code masked PRA with 63, which also "pressed" joystick fire)
    mouseLeft = left;
    mouseRight = right;
    applyPorts();
}
