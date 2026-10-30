//
//  kernel.h - Omega bare metal for Raspberry Pi (Circle)
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef _omega_kernel_h
#define _omega_kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/cputhrottle.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include <circle/usb/usbgamepad.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <circle/types.h>

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);
	TShutdownMode Run (void);

	static CKernel *Get (void) { return s_pThis; }

	// used by HostCircle.cpp
	void UpdateUSB (void);
	void LogToSerial (void) { m_Logger.SetNewTarget (&m_Serial); }	// screen belongs to the Amiga
	CScreenDevice *GetScreen (void) { return &m_Screen; }
	CSerialDevice *GetSerial (void) { return &m_Serial; }
	CActLED *GetActLED (void) { return &m_ActLED; }
	boolean HasFileSystem (void) const { return m_bFileSystem; }
	CCPUThrottle *GetCPUThrottle (void) { return &m_CPUThrottle; }
	CInterruptSystem *GetInterrupt (void) { return &m_Interrupt; }
	boolean HasKeyboard (void) const { return m_pKeyboard != 0; }
	boolean HasMouse (void) const { return m_pMouse != 0; }

private:
	static void KeyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6]);
	static void KeyboardRemovedHandler (CDevice *pDevice, void *pContext);
	static void MouseStatusHandler (unsigned nButtons, int nDisplacementX, int nDisplacementY, int nWheelMove);
	static void MouseRemovedHandler (CDevice *pDevice, void *pContext);
	static void GamePadStatusHandler (unsigned nDeviceIndex, const TGamePadState *pState);
	static void GamePadRemovedHandler (CDevice *pDevice, void *pContext);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CCPUThrottle		m_CPUThrottle;
	CScreenDevice		m_Screen;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;
	CUSBHCIDevice		m_USBHCI;
	CEMMCDevice		m_EMMC;
	FATFS			m_FileSystem;

	boolean			m_bFileSystem;
	CUSBKeyboardDevice * volatile m_pKeyboard;
	CMouseDevice * volatile m_pMouse;

#define MAX_GAMEPADS 2
	CUSBGamePadDevice * volatile m_pGamePad[MAX_GAMEPADS];

	static CKernel *s_pThis;
};

#endif
