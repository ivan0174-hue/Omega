//
//  Snapshot.h
//  The Omega Project
//
//  Saves / restores the machine state (chipset, CIAs, CPU registers, floppy
//  drive mechanics, chip RAM and slow RAM) for debugging: a snapshot taken on
//  the Raspberry Pi can be loaded into the desktop build to reproduce exactly
//  what was on screen. Disk images are not included.
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef Snapshot_h
#define Snapshot_h

#include <stdint.h>
#include <stddef.h>

typedef int (*SnapshotWriter)(const void* data, size_t size, void* context);  // returns 0 on error

int snapshotSave(SnapshotWriter write, void* context);       // 1 = ok
int snapshotLoad(const uint8_t* data, size_t size);          // 1 = ok

#endif
