//
//  kernel.cpp - Omega bare metal for Raspberry Pi (Circle)
//
//  Boots straight into the Amiga emulator. Files are read from the root of
//  the FAT partition of the SD card:
//     KICK.ROM            Kickstart ROM (256K or 512K)     - required
//     DF0.ADF ... DF3.ADF floppy images                     - optional
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "kernel.h"
#include "pihost.h"
#include <circle/string.h>


static const char FromKernel[] = "omega";

CKernel *CKernel::s_pThis = 0;

CKernel::CKernel (void)
:	m_CPUThrottle (CPUSpeedMaximum),	// bare metal starts at 600 MHz; the Pi 4 can do 1500
	m_Screen (640, 548),	// 640x512 Amiga PAL picture (line doubled) + status bar
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer, TRUE),	// TRUE: plug and play
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_bFileSystem (FALSE),
	m_pKeyboard (0),
	m_pMouse (0)
{
	for (unsigned i = 0; i < MAX_GAMEPADS; i++)
	{
		m_pGamePad[i] = 0;
	}
	s_pThis = this;
	m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
	s_pThis = 0;
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Screen;
		}

		bOK = m_Logger.Initialize (pTarget);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		bOK = m_USBHCI.Initialize ();
	}

	if (bOK)
	{
		bOK = m_EMMC.Initialize ();
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Omega Amiga emulator - bare metal build " __DATE__ " " __TIME__);

	if (f_mount (&m_FileSystem, "SD:", 1) != FR_OK)
	{
		m_Logger.Write (FromKernel, LogError, "Cannot mount the SD card (FAT32 needed)");
	}
	else
	{
		m_bFileSystem = TRUE;
	}

	m_Logger.Write (FromKernel, LogNotice, "CPU clock %u MHz (max %u MHz), SoC %u C",
			m_CPUThrottle.GetClockRate () / 1000000, m_CPUThrottle.GetMaxClockRate () / 1000000,
			m_CPUThrottle.GetTemperature ());

	// give USB devices a moment to enumerate, so the keyboard works from the start
	for (unsigned i = 0; i < 20; i++)
	{
		UpdateUSB ();
		m_Timer.MsDelay (50);
	}

	static char arg0[] = "omega";
	static char arg1[] = "KICK.ROM";
	char *argv[] = {arg0, arg1, 0};	// no disks at power on: choose them with F1-F4

	int nResult = omega_main (2, argv);

	// only reached if the emulator could not start (e.g. no KICK.ROM)
	piSetConsoleOnScreen (1);
	m_Logger.Write (FromKernel, LogError, "Emulator stopped (code %d).", nResult);
	m_Logger.Write (FromKernel, LogError, "Copy KICK.ROM to the root of the SD card.");

	return ShutdownHalt;
}

void CKernel::UpdateUSB (void)
{
	// must be called from TASK_LEVEL
	boolean bUpdated = m_USBHCI.UpdatePlugAndPlay ();
	if (!bUpdated)
	{
		return;
	}

	if (m_pKeyboard == 0)
	{
		m_pKeyboard = (CUSBKeyboardDevice *) m_DeviceNameService.GetDevice ("ukbd1", FALSE);
		if (m_pKeyboard != 0)
		{
			m_pKeyboard->RegisterRemovedHandler (KeyboardRemovedHandler);
			m_pKeyboard->RegisterKeyStatusHandlerRaw (KeyStatusHandlerRaw);
			if (piIsConsoleOnScreen ())	// once the Amiga owns the screen, stay quiet
			{
				m_Logger.Write (FromKernel, LogNotice, "USB keyboard attached");
			}
		}
	}

	if (m_pMouse == 0)
	{
		m_pMouse = (CMouseDevice *) m_DeviceNameService.GetDevice ("mouse1", FALSE);
		if (m_pMouse != 0)
		{
			m_pMouse->RegisterRemovedHandler (MouseRemovedHandler);
			m_pMouse->RegisterStatusHandler (MouseStatusHandler);	// raw mode: buttons + deltas
			if (piIsConsoleOnScreen ())
			{
				m_Logger.Write (FromKernel, LogNotice, "USB mouse attached");
			}
		}
	}

	// gamepads / joysticks: "upad1" -> Amiga port 2, "upad2" -> port 1 (if chosen in F5)
	for (unsigned i = 0; i < MAX_GAMEPADS; i++)
	{
		if (m_pGamePad[i] != 0)
		{
			continue;
		}
		CUSBGamePadDevice *pPad = (CUSBGamePadDevice *) m_DeviceNameService.GetDevice ("upad", i + 1, FALSE);
		if (pPad == 0)
		{
			continue;
		}
		m_pGamePad[i] = pPad;
		piGamePadAttached (i, (pPad->GetProperties () & GamePadPropertyIsKnown) != 0);
		piGamePadReport (i, pPad->GetInitialState ());
		pPad->RegisterRemovedHandler (GamePadRemovedHandler, this);
		pPad->RegisterStatusHandler (GamePadStatusHandler);
		if (piIsConsoleOnScreen ())
		{
			m_Logger.Write (FromKernel, LogNotice, "USB gamepad %u attached", i + 1);
		}
	}
}

void CKernel::GamePadStatusHandler (unsigned nDeviceIndex, const TGamePadState *pState)
{
	piGamePadReport (nDeviceIndex, pState);
}

void CKernel::GamePadRemovedHandler (CDevice *pDevice, void *pContext)
{
	CKernel *pThis = (CKernel *) pContext;
	for (unsigned i = 0; i < MAX_GAMEPADS; i++)
	{
		if (pThis->m_pGamePad[i] == (CUSBGamePadDevice *) pDevice)
		{
			pThis->m_pGamePad[i] = 0;
			piGamePadReport (i, 0);
			piGamePadAttached (i, -1);
		}
	}
}

void CKernel::KeyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
	piKeyboardReport (ucModifiers, RawKeys);
}

void CKernel::KeyboardRemovedHandler (CDevice *pDevice, void *pContext)
{
	static const unsigned char NoKeys[6] = {0};
	piKeyboardReport (0, NoKeys);		// release everything
	s_pThis->m_pKeyboard = 0;
}

void CKernel::MouseStatusHandler (unsigned nButtons, int nDisplacementX, int nDisplacementY, int nWheelMove)
{
	piMouseReport (nButtons, nDisplacementX, nDisplacementY);
}

void CKernel::MouseRemovedHandler (CDevice *pDevice, void *pContext)
{
	piMouseReport (0, 0, 0);
	s_pThis->m_pMouse = 0;
}
