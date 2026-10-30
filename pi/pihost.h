//
//  pihost.h - glue between the Circle kernel and the Omega host layer
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef _omega_pihost_h
#define _omega_pihost_h

#ifdef __cplusplus
extern "C" {
#endif

// Entry point of the emulator (main.c compiled with main renamed)
int omega_main (int argc, char *argv[]);

// Called from USB interrupt context
void piKeyboardReport (unsigned char ucModifiers, const unsigned char RawKeys[6]);
void piMouseReport (unsigned nButtons, int nDX, int nDY);
// Gamepads (index 0 and 1). pState is a TGamePadState (Circle), 0 = removed.
// bKnown: 1 = known button layout (Xbox, PlayStation, Switch...), 0 = generic, -1 = removed
void piGamePadAttached (unsigned nIndex, int bKnown);
void piGamePadReport (unsigned nIndex, const void *pState);

// Console output policy for printf (libc.cpp)
//  1: printf goes to the screen and the serial port (boot messages)
//  0: printf goes to the serial port only (if OMEGA_SERIAL_LOG), screen belongs to the Amiga
void piSetConsoleOnScreen (int bOnScreen);
int  piIsConsoleOnScreen (void);

#ifdef __cplusplus
}
#endif

#endif
