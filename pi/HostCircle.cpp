//
//  HostCircle.cpp - Omega host backend for Raspberry Pi bare metal (Circle)
//
//  Implements the platform functions declared in Host.h:
//   - display: the 640x200 emulated picture is line-doubled into a
//     640x480 32-bit framebuffer (the GPU scales it to the HDMI mode)
//   - speed:   Amiga 500 PAL: frames are paced to 49.92 Hz (313 lines of
//              227 colour clocks) with the 1 MHz system timer
//   - input:   USB keyboard (HID usage -> Amiga raw key) and USB mouse
//   - files:   FAT32 SD card through FatFs (long file names, folders)
//   - disks:   like a real Amiga the drives are empty at power on. F1-F4
//              open a menu that lists the .adf files on the SD card; the
//              chosen image is inserted into DF0:-DF3:. The emulation is
//              paused while the menu is open.
//   - the green ACT LED shows floppy motor activity
//   - sound: Paula output (48 kHz stereo) to HDMI or to the headphone jack
//     (F5 settings); the output rate follows the sound queue so the audio
//     never drifts from the emulated Amiga
//
//  Other keys: F11 = HELP, Page Up/Page Down = left/right Amiga,
//  Ctrl + both Amiga keys = reset.
//
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "kernel.h"
#include "pihost.h"
#include <circle/bcmframebuffer.h>
#include <circle/synchronize.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <circle/chargenerator.h>
#include <circle/cputhrottle.h>
#include <circle/string.h>
#include <fatfs/ff.h>
#include <circle/usb/usbgamepad.h>
#include <circle/sound/soundbasedevice.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/sound/pwmsoundbasedevice.h>

extern "C" {
#include "../Snapshot.h"
#include "../Host.h"
#include "../Floppy.h"
#include "../Memory.h"
#include "../CPU.h"
#include "../CIA.h"
#include "../Paula.h"
}

#define FRAME_US	20032		// PAL frame: 313 x 227 colour clocks at 3.546895 MHz = 20.032 ms
#define SCREEN_W	640
#define SCREEN_H	548		// 512 picture lines + status bar
#define SWAP_DELAY	180		// frames between eject and insert when changing disks (3 s),
					// so that Kickstart notices the disk was taken out
#define ADF_MAX		(82 * 22 * 512)
#define PI_ROWS		256		// emulated lines shown: 44..299 (a full PAL screen)
#define PICTURE_TOP	0		// the picture is line doubled: rows 0..511
#define ROOT		"SD:/"

// ---------------------------------------------------------------------------
// USB HID usage code -> Amiga raw key code (0xFF = not mapped)

static u8 s_HIDToAmiga[256];

static const u8 s_ModifierToAmiga[8] = {0x63, 0x60, 0x64, 0x66, 0x63, 0x61, 0x65, 0x67};

static void BuildKeymap (void)
{
	memset (s_HIDToAmiga, 0xFF, sizeof s_HIDToAmiga);

	static const u8 Letters[26] = {		// a..z
		0x20, 0x35, 0x33, 0x22, 0x12, 0x23, 0x24, 0x25, 0x17, 0x26, 0x27, 0x28, 0x37,
		0x36, 0x18, 0x19, 0x10, 0x13, 0x21, 0x14, 0x16, 0x34, 0x11, 0x32, 0x15, 0x31
	};
	for (unsigned i = 0; i < 26; i++)
	{
		s_HIDToAmiga[0x04 + i] = Letters[i];
	}
	for (unsigned i = 0; i < 9; i++)	// 1..9
	{
		s_HIDToAmiga[0x1E + i] = 0x01 + i;
	}
	s_HIDToAmiga[0x27] = 0x0A;		// 0

	static const struct { u8 hid; u8 amiga; } Map[] = {
		{0x28, 0x44}, {0x29, 0x45}, {0x2A, 0x41}, {0x2B, 0x42}, {0x2C, 0x40},	// return esc bs tab space
		{0x2D, 0x0B}, {0x2E, 0x0C}, {0x2F, 0x1A}, {0x30, 0x1B}, {0x31, 0x0D},	// - = [ ] backslash
		{0x32, 0x2B}, {0x33, 0x29}, {0x34, 0x2A}, {0x35, 0x00}, {0x36, 0x38},	// # ; ' ` ,
		{0x37, 0x39}, {0x38, 0x3A}, {0x39, 0x62},				// . / caps
		{0x3F, 0x55}, {0x40, 0x56}, {0x41, 0x57}, {0x42, 0x58}, {0x43, 0x59},	// F6-F10
		{0x44, 0x5F}, {0x49, 0x5F},						// F11, Insert = HELP
		{0x4C, 0x46},								// delete
		{0x4B, 0x66}, {0x4E, 0x67},						// PgUp/PgDn = Amiga keys
		{0x4F, 0x4E}, {0x50, 0x4F}, {0x51, 0x4D}, {0x52, 0x4C},			// right left down up
		{0x54, 0x5C}, {0x55, 0x5D}, {0x56, 0x4A}, {0x57, 0x5E}, {0x58, 0x43},	// keypad / * - + enter
		{0x59, 0x1D}, {0x5A, 0x1E}, {0x5B, 0x1F}, {0x5C, 0x2D}, {0x5D, 0x2E},	// keypad 1-5
		{0x5E, 0x2F}, {0x5F, 0x3D}, {0x60, 0x3E}, {0x61, 0x3F}, {0x62, 0x0F},	// keypad 6-9 0
		{0x63, 0x3C}, {0x64, 0x30},						// keypad . , ISO key left of Z
	};
	for (unsigned i = 0; i < sizeof Map / sizeof Map[0]; i++)
	{
		s_HIDToAmiga[Map[i].hid] = Map[i].amiga;
	}
	for (unsigned i = 0; i < 8; i++)	// modifiers are handled as keys 0xE0..0xE7 (HID usages)
	{
		s_HIDToAmiga[0xE0 + i] = s_ModifierToAmiga[i];
	}
}

// modifier bits: LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI

#define HID_ENTER	0x28
#define HID_ESC		0x29
#define HID_BACKSPACE	0x2A
#define HID_F1		0x3A
#define HID_F4		0x3D
#define HID_F5		0x3E
#define HID_F12		0x45
#define HID_HOME	0x4A
#define HID_PGUP	0x4B
#define HID_END		0x4D
#define HID_PGDN	0x4E
#define HID_DOWN	0x51
#define HID_UP		0x52
#define HID_KPENTER	0x58

// ---------------------------------------------------------------------------
// Input reports arrive in USB interrupt context and are queued here

#define REPORT_QUEUE 32

struct TKeyReport
{
	u8 Modifiers;
	u8 Keys[6];
};

static TKeyReport s_Reports[REPORT_QUEUE];
static volatile unsigned s_nReportHead = 0;
static volatile unsigned s_nReportTail = 0;
static TKeyReport s_Previous;
static volatile unsigned s_nKeyReports = 0;	// diagnostics: USB keyboard reports received
static boolean s_bSnapshotRequested = FALSE;	// F12 pressed

static volatile int s_nMouseDX = 0;
static volatile int s_nMouseDY = 0;
static volatile unsigned s_nMouseButtons = 0;

void piKeyboardReport (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
	unsigned nNext = (s_nReportHead + 1) % REPORT_QUEUE;
	if (nNext == s_nReportTail)
	{
		return;		// queue full
	}
	s_nKeyReports++;
	s_Reports[s_nReportHead].Modifiers = ucModifiers;
	memcpy (s_Reports[s_nReportHead].Keys, RawKeys, 6);
	DataMemBarrier ();
	s_nReportHead = nNext;
}

void piMouseReport (unsigned nButtons, int nDX, int nDY)
{
	s_nMouseDX += nDX;
	s_nMouseDY += nDY;
	s_nMouseButtons = nButtons;
}

// ---------------------------------------------------------------------------
// USB gamepads (USB interrupt context): translated to joystick bits at once

#define PAD_B		64		// B / Y (circle / triangle): role chosen in the menu
#define PAD_START	128
#define PAD_SELECT	256

static volatile unsigned s_PadBits[2] = {0, 0};
static volatile int s_PadKnown[2] = {-1, -1};	// -1 none, 0 generic, 1 known layout

void piGamePadAttached (unsigned nIndex, int bKnown)
{
	if (nIndex < 2)
	{
		s_PadKnown[nIndex] = bKnown;
	}
}

static unsigned AxisBits (const TGamePadState *pState, int nAxis, unsigned nNeg, unsigned nPos)
{
	if (nAxis >= pState->naxes)
	{
		return 0;
	}
	int nMin = pState->axes[nAxis].minimum, nMax = pState->axes[nAxis].maximum;
	if (nMax - nMin < 4)
	{
		return 0;
	}
	int nCenter = (nMin + nMax) / 2;
	int nDead = (nMax - nMin) / 4;			// half way out
	int nValue = pState->axes[nAxis].value;
	if (nValue < nCenter - nDead) return nNeg;
	if (nValue > nCenter + nDead) return nPos;
	return 0;
}

void piGamePadReport (unsigned nIndex, const void *pStateVoid)
{
	if (nIndex >= 2)
	{
		return;
	}
	const TGamePadState *pState = (const TGamePadState *) pStateVoid;
	if (pState == 0)
	{
		s_PadBits[nIndex] = 0;
		return;
	}

	unsigned nBits = 0;
	unsigned b = pState->buttons;
	if (s_PadKnown[nIndex] == 1)
	{
		if (b & GamePadButtonUp)	nBits |= JOY_UP;
		if (b & GamePadButtonDown)	nBits |= JOY_DOWN;
		if (b & GamePadButtonLeft)	nBits |= JOY_LEFT;
		if (b & GamePadButtonRight)	nBits |= JOY_RIGHT;
		if (b & (GamePadButtonA | GamePadButtonX | GamePadButtonRB | GamePadButtonRT))	nBits |= JOY_FIRE;
		if (b & (GamePadButtonB | GamePadButtonY | GamePadButtonLB | GamePadButtonLT))	nBits |= PAD_B;
		if (b & (GamePadButtonStart | GamePadButtonPlus))	nBits |= PAD_START;
		if (b & (GamePadButtonSelect | GamePadButtonMinus))	nBits |= PAD_SELECT;
		nBits |= AxisBits (pState, GamePadAxisLeftX, JOY_LEFT, JOY_RIGHT);
		nBits |= AxisBits (pState, GamePadAxisLeftY, JOY_UP, JOY_DOWN);
	}
	else
	{
		// generic: button 1 fire, button 2 second button, 3-8 fire, 9 select, 10 start
		if (b & 0x01)	nBits |= JOY_FIRE;
		if (b & 0x02)	nBits |= PAD_B;
		if (b & 0xFC)	nBits |= JOY_FIRE;
		if (b & 0x100)	nBits |= PAD_SELECT;
		if (b & 0x200)	nBits |= PAD_START;
		nBits |= AxisBits (pState, 0, JOY_LEFT, JOY_RIGHT);
		nBits |= AxisBits (pState, 1, JOY_UP, JOY_DOWN);
	}
	if (pState->nhats > 0)
	{
		static const unsigned Hat[8] = {JOY_UP, JOY_UP | JOY_RIGHT, JOY_RIGHT, JOY_RIGHT | JOY_DOWN,
						JOY_DOWN, JOY_DOWN | JOY_LEFT, JOY_LEFT, JOY_LEFT | JOY_UP};
		int h = pState->hats[0];
		if (h >= 0 && h < 8)
		{
			nBits |= Hat[h];
		}
	}
	s_PadBits[nIndex] = nBits;
}

static boolean Contains (const u8 Keys[6], u8 ucKey)
{
	for (unsigned i = 0; i < 6; i++)
	{
		if (Keys[i] == ucKey)
		{
			return TRUE;
		}
	}
	return FALSE;
}

// ---------------------------------------------------------------------------
// Screen

static CBcmFrameBuffer *s_pFrameBuffer = 0;
static u32 *s_pScreen = 0;
static unsigned s_nPitch = 0;		// in pixels
static CCharGenerator *s_pFont = 0;

#define COLOR_TEXT	0xFFFFFFFF
#define COLOR_BACK	0xFF0055AA	// Workbench 1.3 blue
#define COLOR_SELBACK	0xFFFF8800	// Workbench 1.3 orange
#define COLOR_SELTEXT	0xFF000000
#define COLOR_DIM	0xFFAACCEE
#define COLOR_STATUS	0xFFC0C0C0
#define COLOR_BLACK	0xFF000000

static void FillRect (unsigned nX, unsigned nY, unsigned nW, unsigned nH, u32 nColor)
{
	for (unsigned y = nY; y < nY + nH && y < SCREEN_H; y++)
	{
		u32 *pLine = &s_pScreen[y * s_nPitch];
		for (unsigned x = nX; x < nX + nW && x < SCREEN_W; x++)
		{
			pLine[x] = nColor;
		}
	}
}

// draws at most nMaxChars characters (pads with spaces when bPad)
static void DrawText (unsigned nX, unsigned nY, const char *pText, u32 nFG, u32 nBG,
		      unsigned nMaxChars = 80, boolean bPad = FALSE)
{
	if (s_pFont == 0)
	{
		return;
	}
	unsigned nW = s_pFont->GetCharWidth ();
	unsigned nH = s_pFont->GetCharHeight ();
	for (unsigned n = 0; n < nMaxChars && nX + nW <= SCREEN_W; n++, nX += nW)
	{
		char ch = *pText;
		if (ch == '\0')
		{
			if (!bPad)
			{
				break;
			}
			ch = ' ';
		}
		else
		{
			pText++;
		}
		if ((unsigned char) ch < ' ' || (unsigned char) ch > '~')
		{
			ch = '?';	// the 8x16 font only has ASCII
		}
		for (unsigned y = 0; y < nH; y++)
		{
			u32 *pLine = &s_pScreen[(nY + y) * s_nPitch + nX];
			for (unsigned x = 0; x < nW; x++)
			{
				pLine[x] = s_pFont->GetPixel (ch, x, y) ? nFG : nBG;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Small string helpers

static char ToUpper (char c)
{
	return ('a' <= c && c <= 'z') ? c - 'a' + 'A' : c;
}

static int CompareNoCase (const char *pA, const char *pB)
{
	while (*pA && ToUpper (*pA) == ToUpper (*pB))
	{
		pA++;
		pB++;
	}
	return (int) (unsigned char) ToUpper (*pA) - (int) (unsigned char) ToUpper (*pB);
}

static boolean HasExtension (const char *pName, const char *pExt)
{
	const char *pDot = 0;
	for (const char *p = pName; *p; p++)
	{
		if (*p == '.')
		{
			pDot = p;
		}
	}
	return pDot != 0 && CompareNoCase (pDot + 1, pExt) == 0;
}

static void CopyString (char *pDest, const char *pSrc, unsigned nSize)
{
	unsigned i = 0;
	for (; i + 1 < nSize && pSrc[i]; i++)
	{
		pDest[i] = pSrc[i];
	}
	pDest[i] = '\0';
}

// ---------------------------------------------------------------------------
// Files

static long ReadFile (const char *pPath, uint8_t *pBuffer, long nMaxSize)
{
	FIL File;
	if (f_open (&File, pPath, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		return -1;
	}

	long nTotal = 0;
	while (nTotal < nMaxSize)
	{
		UINT nChunk = nMaxSize - nTotal > 0x10000 ? 0x10000 : (UINT) (nMaxSize - nTotal);
		UINT nRead = 0;
		if (f_read (&File, pBuffer + nTotal, nChunk, &nRead) != FR_OK)
		{
			f_close (&File);
			return -1;
		}
		if (nRead == 0)
		{
			break;
		}
		nTotal += nRead;
	}
	f_close (&File);
	return nTotal;
}

// KICK.ROM, or else the first 256K/512K *.rom file in the root directory
long hostLoadFile (const char *pPath, uint8_t *pBuffer, long nMaxSize)
{
	if (!CKernel::Get ()->HasFileSystem ())
	{
		return -1;
	}

	CString Path;
	Path.Format ("%s%s", ROOT, pPath);
	long nTotal = ReadFile ((const char *) Path, pBuffer, nMaxSize);
	if (nTotal > 0 || !HasExtension (pPath, "ROM"))
	{
		return nTotal;
	}

	DIR Dir;
	FILINFO Info;
	if (f_opendir (&Dir, ROOT) != FR_OK)
	{
		return -1;
	}
	while (f_readdir (&Dir, &Info) == FR_OK && Info.fname[0] != '\0')
	{
		if (   !(Info.fattrib & AM_DIR) && HasExtension (Info.fname, "ROM")
		    && (Info.fsize == 256*1024 || Info.fsize == 512*1024))
		{
			printf ("%s non trovato, uso %s\n", pPath, Info.fname);
			Path.Format ("%s%s", ROOT, Info.fname);
			nTotal = ReadFile ((const char *) Path, pBuffer, nMaxSize);
			break;
		}
	}
	f_closedir (&Dir);
	return nTotal;
}

// ---------------------------------------------------------------------------
// Drives

struct TDrive
{
	char Name[48];		// shown in the status bar
	int nInsertCountdown;	// frames until the new disk goes in (disk change)
	boolean bPendingInsert;	// retry until the drive accepts the disk
};

static TDrive s_Drive[4];
static uint8_t s_ImageBuffer[ADF_MAX + 1];

static const char *LoadDisk (unsigned nDrive, const char *pPath, const char *pName)
{
	long nSize = ReadFile (pPath, s_ImageBuffer, sizeof s_ImageBuffer);
	if (nSize <= 0)
	{
		return "Impossibile leggere il file";
	}
	if (nSize > ADF_MAX || nSize < 11 * 512 * 2)
	{
		return "Non e' un'immagine ADF (dimensione sbagliata)";
	}

	boolean bWasInserted = floppyIsInserted (nDrive);
	floppyEjectDisk (nDrive);
	if (!floppyLoadImage (nDrive, s_ImageBuffer, (int) nSize))
	{
		return "Errore nel caricamento";
	}

	CopyString (s_Drive[nDrive].Name, pName, sizeof s_Drive[nDrive].Name);
	if (bWasInserted)
	{
		// give Kickstart time to see the old disk go out
		s_Drive[nDrive].nInsertCountdown = SWAP_DELAY;
		s_Drive[nDrive].bPendingInsert = FALSE;
	}
	else
	{
		s_Drive[nDrive].nInsertCountdown = 0;
		s_Drive[nDrive].bPendingInsert = TRUE;
	}
	return 0;
}

static void EjectDisk (unsigned nDrive)
{
	floppyEjectDisk (nDrive);
	s_Drive[nDrive].Name[0] = '\0';
	s_Drive[nDrive].nInsertCountdown = 0;
	s_Drive[nDrive].bPendingInsert = FALSE;
}

static void UpdateDrives (void)
{
	for (unsigned i = 0; i < 4; i++)
	{
		TDrive *pDrive = &s_Drive[i];
		if (pDrive->nInsertCountdown > 0 && --pDrive->nInsertCountdown == 0)
		{
			pDrive->bPendingInsert = TRUE;
		}
		if (pDrive->bPendingInsert && floppyInsertDisk (i))
		{
			pDrive->bPendingInsert = FALSE;
		}
	}
}

// ---------------------------------------------------------------------------
// Disk menu

#define MENU_MAX	256
#define MENU_ROWS	28			// visible list rows
#define MENU_TOP	PICTURE_TOP

enum TEntryType
{
	EntryEject,
	EntryParent,
	EntryFolder,
	EntryFile
};

struct TEntry
{
	TEntryType Type;
	char Name[FF_MAX_LFN + 1];
};

static TEntry s_Entries[MENU_MAX];
static unsigned s_nEntries = 0;
static unsigned s_nSelected = 0;
static unsigned s_nFirstVisible = 0;
static char s_Folder[256] = ROOT;
static boolean s_bMenu = FALSE;
static boolean s_bMenuDirty = FALSE;
static unsigned s_nMenuDrive = 0;
static const char *s_pMenuMessage = 0;

static void ReadFolder (void)
{
	s_nEntries = 0;
	s_nSelected = 0;
	s_nFirstVisible = 0;

	s_Entries[s_nEntries].Type = EntryEject;
	CopyString (s_Entries[s_nEntries++].Name, "<< nessun disco (estrai) >>", FF_MAX_LFN + 1);

	if (strcmp (s_Folder, ROOT) != 0)
	{
		s_Entries[s_nEntries].Type = EntryParent;
		CopyString (s_Entries[s_nEntries++].Name, "..", FF_MAX_LFN + 1);
	}
	unsigned nFirstSorted = s_nEntries;

	DIR Dir;
	FILINFO Info;
	if (f_opendir (&Dir, s_Folder) != FR_OK)
	{
		s_pMenuMessage = "Impossibile leggere la cartella";
		return;
	}
	while (s_nEntries < MENU_MAX && f_readdir (&Dir, &Info) == FR_OK && Info.fname[0] != '\0')
	{
		if ((Info.fattrib & (AM_HID | AM_SYS)) || Info.fname[0] == '.')
		{
			continue;
		}
		TEntryType Type;
		if (Info.fattrib & AM_DIR)
		{
			Type = EntryFolder;
		}
		else if (HasExtension (Info.fname, "ADF"))
		{
			Type = EntryFile;
		}
		else
		{
			continue;
		}

		// insert sorted: folders first, then files, alphabetically
		unsigned i = s_nEntries++;
		while (i > nFirstSorted)
		{
			const TEntry &Prev = s_Entries[i-1];
			boolean bBefore = Type < Prev.Type
					  || (Type == Prev.Type && CompareNoCase (Info.fname, Prev.Name) < 0);
			if (!bBefore)
			{
				break;
			}
			s_Entries[i] = s_Entries[i-1];
			i--;
		}
		s_Entries[i].Type = Type;
		CopyString (s_Entries[i].Name, Info.fname, FF_MAX_LFN + 1);
	}
	f_closedir (&Dir);

	// start on the first disk image, so F1 + Enter is enough with one .adf file
	for (unsigned i = 0; i < s_nEntries; i++)
	{
		if (s_Entries[i].Type == EntryFile)
		{
			s_nSelected = i;
			break;
		}
	}
	if (s_nEntries == nFirstSorted)
	{
		s_pMenuMessage = "Nessun file .adf in questa cartella";
	}
}

static void DrawMenu (void)
{
	const unsigned nTop = MENU_TOP;
	FillRect (0, nTop, SCREEN_W, 2 * PI_ROWS, COLOR_BACK);

	CString Title;
	Title.Format (" DF%u: scegli un disco", s_nMenuDrive);
	DrawText (0, nTop, (const char *) Title, COLOR_SELTEXT, COLOR_TEXT, 80, TRUE);
	DrawText (8, nTop + 18, s_Folder, COLOR_DIM, COLOR_BACK, 78);

	if (s_nSelected < s_nFirstVisible)
	{
		s_nFirstVisible = s_nSelected;
	}
	if (s_nSelected >= s_nFirstVisible + MENU_ROWS)
	{
		s_nFirstVisible = s_nSelected - MENU_ROWS + 1;
	}

	for (unsigned nRow = 0; nRow < MENU_ROWS; nRow++)
	{
		unsigned nIndex = s_nFirstVisible + nRow;
		if (nIndex >= s_nEntries)
		{
			break;
		}
		const TEntry &Entry = s_Entries[nIndex];
		CString Line;
		if (Entry.Type == EntryFolder)
		{
			Line.Format (" [%s]", Entry.Name);
		}
		else
		{
			Line.Format (" %s", Entry.Name);
		}
		boolean bSel = nIndex == s_nSelected;
		DrawText (8, nTop + 40 + nRow * 16, (const char *) Line,
			  bSel ? COLOR_SELTEXT : COLOR_TEXT, bSel ? COLOR_SELBACK : COLOR_BACK, 78, TRUE);
	}

	const char *pHelp = s_pMenuMessage != 0 ? s_pMenuMessage
			  : "Su/Giu: scegli   Invio: inserisci   Esc: chiudi   Backspace: cartella su";
	DrawText (0, nTop + 2 * PI_ROWS - 16, pHelp,
		  s_pMenuMessage != 0 ? COLOR_SELBACK : COLOR_DIM, COLOR_BACK, 80, TRUE);
}

static void OpenMenu (unsigned nDrive)
{
	s_nMenuDrive = nDrive;
	s_pMenuMessage = 0;
	ReadFolder ();
	s_bMenu = TRUE;
	s_bMenuDirty = TRUE;
}

static void FolderUp (void)
{
	unsigned nLen = strlen (s_Folder);
	if (nLen <= strlen (ROOT))
	{
		return;
	}
	s_Folder[nLen - 1] = '\0';		// remove trailing '/'
	char *pSlash = 0;
	for (char *p = s_Folder; *p; p++)
	{
		if (*p == '/')
		{
			pSlash = p;
		}
	}
	if (pSlash != 0)
	{
		pSlash[1] = '\0';
	}
	ReadFolder ();
}

static void MenuKey (u8 ucKey)
{
	s_bMenuDirty = TRUE;
	s_pMenuMessage = 0;

	switch (ucKey)
	{
	case HID_UP:	if (s_nSelected > 0) s_nSelected--;			break;
	case HID_DOWN:	if (s_nSelected + 1 < s_nEntries) s_nSelected++;	break;
	case HID_PGUP:	s_nSelected = s_nSelected > MENU_ROWS ? s_nSelected - MENU_ROWS : 0;	break;
	case HID_PGDN:	s_nSelected += MENU_ROWS;
			if (s_nSelected >= s_nEntries) s_nSelected = s_nEntries - 1;	break;
	case HID_HOME:	s_nSelected = 0;					break;
	case HID_END:	s_nSelected = s_nEntries - 1;				break;
	case HID_BACKSPACE:	FolderUp ();					break;

	case HID_ESC:
		s_bMenu = FALSE;
		break;

	case HID_ENTER:
	case HID_KPENTER: {
		const TEntry &Entry = s_Entries[s_nSelected];
		switch (Entry.Type)
		{
		case EntryEject:
			EjectDisk (s_nMenuDrive);
			s_bMenu = FALSE;
			break;

		case EntryParent:
			FolderUp ();
			break;

		case EntryFolder:
			if (strlen (s_Folder) + strlen (Entry.Name) + 2 < sizeof s_Folder)
			{
				strcat (s_Folder, Entry.Name);
				strcat (s_Folder, "/");
				ReadFolder ();
			}
			break;

		case EntryFile: {
			CString Path;
			Path.Format ("%s%s", s_Folder, Entry.Name);
			s_pMenuMessage = LoadDisk (s_nMenuDrive, (const char *) Path, Entry.Name);
			if (s_pMenuMessage == 0)
			{
				s_bMenu = FALSE;
			}
			} break;
		}
		} break;

	default:
		if (ucKey >= HID_F1 && ucKey <= HID_F4)
		{
			unsigned nDrive = ucKey - HID_F1;
			if (nDrive == s_nMenuDrive)
			{
				s_bMenu = FALSE;		// same key closes the menu
			}
			else
			{
				s_nMenuDrive = nDrive;		// switch drive
			}
		}
		else if (ucKey >= 0x04 && ucKey <= 0x1D)	// letter: jump to the next entry with it
		{
			char ch = 'A' + (ucKey - 0x04);
			for (unsigned n = 1; n <= s_nEntries; n++)
			{
				unsigned i = (s_nSelected + n) % s_nEntries;
				if (s_Entries[i].Type >= EntryFolder && ToUpper (s_Entries[i].Name[0]) == ch)
				{
					s_nSelected = i;
					break;
				}
			}
		}
		break;
	}
}

// ---------------------------------------------------------------------------
// Keyboard report processing

// ---------------------------------------------------------------------------
// Settings menu (F5): memory configuration, saved to SD:/OMEGA.CFG
//   chip=<KB>   256, 512, 1024, 2048        (Amiga 500 default 512)
//   slow=<KB>   0 ... 1792 in 256K steps      (default 512, the A501 trapdoor card)

#define CONFIG_FILE	ROOT "OMEGA.CFG"

static const unsigned s_ChipOptions[] = {256, 512, 1024, 2048};
static const unsigned s_SlowOptions[] = {0, 256, 512, 768, 1024, 1280, 1536, 1792};
#define CHIP_OPTIONS (sizeof s_ChipOptions / sizeof s_ChipOptions[0])
#define SLOW_OPTIONS (sizeof s_SlowOptions / sizeof s_SlowOptions[0])

//   audio=hdmi|jack|off   (default hdmi)
//   stereo=<percent>      100 = Amiga hard stereo, 70, 50, 0 = mono   (default 70)

enum TAudioOut { AudioHDMI, AudioJack, AudioOff, AUDIO_OPTIONS };
static const char *s_AudioNames[AUDIO_OPTIONS] = {"HDMI (TV/monitor)", "Jack cuffie", "Spento"};
static const char *s_AudioKeys[AUDIO_OPTIONS] = {"hdmi", "jack", "off"};
static const unsigned s_StereoOptions[] = {100, 70, 50, 0};
#define STEREO_OPTIONS (sizeof s_StereoOptions / sizeof s_StereoOptions[0])

#define SET_ROWS	7			// chip, slow, audio, stereo, joystick page, apply, cancel
#define ROW_JOY		4
#define ROW_APPLY	5
#define ROW_CANCEL	6

// Joysticks (page opened from F5)
//   joykb=0|1            keyboard joystick in port 2 (default off: the keys go to the Amiga)
//   joykeys=u,d,l,r,f,f2 HID codes of the keys (modifiers 224-231)
//   padb=0|1|2           gamepad B/Y: second button / up (jump) / fire
//   padstart=n           gamepad Start sends an Amiga key (see s_StartKeys)
//   port1=mouse|pad      second gamepad in port 1 instead of the mouse
enum { JK_UP, JK_DOWN, JK_LEFT, JK_RIGHT, JK_FIRE, JK_FIRE2, JK_COUNT };
static const unsigned s_JoyKeyBits[JK_COUNT] = {JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_FIRE, JOY_FIRE2};
static const char *s_JoyKeyNames[JK_COUNT] = {"Su", "Giu'", "Sinistra", "Destra", "Fuoco", "Fuoco 2"};
static const u8 s_DefaultJoyKeys[JK_COUNT] = {0x52, 0x51, 0x50, 0x4F, 0xE4, 0xE6};	// arrows, right Ctrl, Alt Gr
static u8 s_JoyKeys[JK_COUNT] = {0x52, 0x51, 0x50, 0x4F, 0xE4, 0xE6};
static boolean s_bKeyboardJoy = FALSE;
static unsigned s_nPadBRole = 0;
static const char *s_PadBRoles[3] = {"secondo pulsante", "su (salto)", "fuoco"};
static const struct { const char *pName; u8 ucAmiga; } s_StartKeys[] = {
	{"nessuno", 0xFF}, {"Spazio", 0x40}, {"Invio", 0x44}, {"F1", 0x50}, {"Esc", 0x45}, {"P", 0x19}
};
#define START_KEYS (sizeof s_StartKeys / sizeof s_StartKeys[0])
static unsigned s_nPadStart = 1;
static boolean s_bPort1Pad = FALSE;

static boolean s_bJoyPage = FALSE;
static unsigned s_nJoyRow = 0;
static int s_nCapture = -1;		// joystick key being redefined, -1 none
static unsigned s_nKbJoyBits = 0;	// directions/fire from the keyboard

static boolean s_bSettings = FALSE;
static unsigned s_nSettingsRow = 0;
static unsigned s_nChipChoice = 1;
static unsigned s_nSlowChoice = 2;
static unsigned s_nAudioChoice = AudioHDMI;
static unsigned s_nStereoChoice = 1;
static unsigned s_nAudioActive = AudioOff;	// output currently running
static unsigned s_nStereoActive = 1;
static boolean s_bResetRequested = FALSE;

static void StartAudio (unsigned nOut);

static unsigned IndexOf (const unsigned *pOptions, unsigned nCount, unsigned nValue)
{
	for (unsigned i = 0; i < nCount; i++)
	{
		if (pOptions[i] == nValue)
		{
			return i;
		}
	}
	return 0;
}

static void LoadConfig (void)
{
	unsigned nChip = 512, nSlow = 512;		// Amiga 500 + A501 expansion
	s_nAudioChoice = AudioHDMI;
	s_nStereoChoice = 1;

	FIL File;
	if (f_open (&File, CONFIG_FILE, FA_READ | FA_OPEN_EXISTING) == FR_OK)
	{
		char Buffer[256];
		UINT nRead = 0;
		f_read (&File, Buffer, sizeof Buffer - 1, &nRead);
		f_close (&File);
		Buffer[nRead] = '\0';

		for (char *p = Buffer; *p; p++)
		{
			if (strncmp (p, "chip=", 5) == 0)
			{
				nChip = atoi (p + 5);
			}
			else if (strncmp (p, "slow=", 5) == 0)
			{
				nSlow = atoi (p + 5);
			}
			else if (strncmp (p, "audio=", 6) == 0)
			{
				for (unsigned i = 0; i < AUDIO_OPTIONS; i++)
				{
					if (strncmp (p + 6, s_AudioKeys[i], strlen (s_AudioKeys[i])) == 0)
					{
						s_nAudioChoice = i;
					}
				}
			}
			else if (strncmp (p, "stereo=", 7) == 0)
			{
				s_nStereoChoice = IndexOf (s_StereoOptions, STEREO_OPTIONS, atoi (p + 7));
			}
			else if (strncmp (p, "joykb=", 6) == 0)
			{
				s_bKeyboardJoy = atoi (p + 6) != 0;
			}
			else if (strncmp (p, "joykeys=", 8) == 0)
			{
				const char *q = p + 8;
				for (unsigned k = 0; k < JK_COUNT && *q >= '0' && *q <= '9'; k++)
				{
					unsigned v = atoi (q);
					if (v > 3 && v < 256)
					{
						s_JoyKeys[k] = (u8) v;
					}
					while (*q >= '0' && *q <= '9') q++;
					if (*q == ',') q++;
				}
			}
			else if (strncmp (p, "padb=", 5) == 0)
			{
				s_nPadBRole = atoi (p + 5) % 3;
			}
			else if (strncmp (p, "padstart=", 9) == 0)
			{
				s_nPadStart = atoi (p + 9) % START_KEYS;
			}
			else if (strncmp (p, "port1=", 6) == 0)
			{
				s_bPort1Pad = strncmp (p + 6, "pad", 3) == 0;
			}
		}
	}

	memoryConfigure (nChip * 1024, nSlow * 1024);
	s_nChipChoice = IndexOf (s_ChipOptions, CHIP_OPTIONS, chipRamSize / 1024);
	s_nSlowChoice = IndexOf (s_SlowOptions, SLOW_OPTIONS, slowRamSize / 1024);
	s_nAudioActive = s_nAudioChoice;	// started in hostInit()
	s_nStereoActive = s_nStereoChoice;
}

static boolean SaveConfig (unsigned nChipKB, unsigned nSlowKB, unsigned nAudio, unsigned nStereo)
{
	CString Text;
	Text.Format ("chip=%u\nslow=%u\naudio=%s\nstereo=%u\n", nChipKB, nSlowKB, s_AudioKeys[nAudio], s_StereoOptions[nStereo]);
	CString Joy;
	Joy.Format ("joykb=%u\njoykeys=%u,%u,%u,%u,%u,%u\npadb=%u\npadstart=%u\nport1=%s\n",
		    s_bKeyboardJoy ? 1 : 0, s_JoyKeys[0], s_JoyKeys[1], s_JoyKeys[2], s_JoyKeys[3], s_JoyKeys[4], s_JoyKeys[5],
		    s_nPadBRole, s_nPadStart, s_bPort1Pad ? "pad" : "mouse");
	Text.Append ((const char *) Joy);

	FIL File;
	if (f_open (&File, CONFIG_FILE, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
	{
		return FALSE;
	}
	UINT nWritten = 0;
	boolean bOK = f_write (&File, (const char *) Text, Text.GetLength (), &nWritten) == FR_OK
		      && nWritten == Text.GetLength ();
	return f_close (&File) == FR_OK && bOK;
}

static void OpenSettings (void)
{
	s_nChipChoice = IndexOf (s_ChipOptions, CHIP_OPTIONS, chipRamSize / 1024);
	s_nSlowChoice = IndexOf (s_SlowOptions, SLOW_OPTIONS, slowRamSize / 1024);
	s_nAudioChoice = s_nAudioActive;
	s_nStereoChoice = s_nStereoActive;
	s_nSettingsRow = 0;
	s_bSettings = TRUE;
	s_bMenuDirty = TRUE;
	s_pMenuMessage = 0;
}

static CString SizeText (unsigned nKB)
{
	CString Text;
	if (nKB == 0)
	{
		Text = "nessuna";
	}
	else if (nKB % 1024 == 0)
	{
		Text.Format ("%u MB", nKB / 1024);
	}
	else if (nKB > 1024)
	{
		Text.Format ("%u,%02u MB", nKB / 1024, (nKB % 1024) * 100 / 1024);
	}
	else
	{
		Text.Format ("%u KB", nKB);
	}
	return Text;
}

static void DrawSettings (void)
{
	const unsigned nTop = MENU_TOP;
	FillRect (0, nTop, SCREEN_W, 2 * PI_ROWS, COLOR_BACK);
	DrawText (0, nTop, " Impostazioni", COLOR_SELTEXT, COLOR_TEXT, 80, TRUE);

	boolean bMemoryChanged = s_ChipOptions[s_nChipChoice] != chipRamSize / 1024
			      || s_SlowOptions[s_nSlowChoice] != slowRamSize / 1024;

	CString Rows[SET_ROWS];
	Rows[0].Format (" Chip RAM:   <  %s  >", (const char *) SizeText (s_ChipOptions[s_nChipChoice]));
	Rows[1].Format (" Slow RAM:   <  %s  >", (const char *) SizeText (s_SlowOptions[s_nSlowChoice]));
	Rows[2].Format (" Audio:      <  %s  >", s_AudioNames[s_nAudioChoice]);
	if (s_StereoOptions[s_nStereoChoice] == 100)
	{
		Rows[3] = " Stereo:     <  100% (Amiga originale)  >";
	}
	else if (s_StereoOptions[s_nStereoChoice] == 0)
	{
		Rows[3] = " Stereo:     <  mono  >";
	}
	else
	{
		Rows[3].Format (" Stereo:     <  %u%%  >", s_StereoOptions[s_nStereoChoice]);
	}
	Rows[ROW_JOY] = " Joystick, gamepad e tasti...";
	Rows[ROW_APPLY] = bMemoryChanged ? " Salva e riavvia l'Amiga" : " Salva";
	Rows[ROW_CANCEL] = " Annulla";
	for (unsigned i = 0; i < SET_ROWS; i++)
	{
		boolean bSel = i == s_nSettingsRow;
		DrawText (8, nTop + 48 + i * 24 + (i >= ROW_JOY ? 16 : 0) + (i >= ROW_APPLY ? 16 : 0), (const char *) Rows[i],
			  bSel ? COLOR_SELTEXT : COLOR_TEXT, bSel ? COLOR_SELBACK : COLOR_BACK, 48, TRUE);
	}

	CString Info;
	Info.Format (" Ora: chip %s, slow %s.   Amiga 500 originale: chip 512 KB, slow 512 KB.",
		     (const char *) SizeText (chipRamSize / 1024), (const char *) SizeText (slowRamSize / 1024));
	DrawText (0, nTop + 280, (const char *) Info, COLOR_DIM, COLOR_BACK, 80, TRUE);
	DrawText (0, nTop + 304, " Chip RAM oltre 512 KB: come un A500 con Agnus aggiornato (1 MB / 2 MB ECS).",
		  COLOR_DIM, COLOR_BACK, 80, TRUE);
	DrawText (0, nTop + 320, " Cambiare la memoria riavvia l'Amiga (i dischi restano nei drive).",
		  COLOR_DIM, COLOR_BACK, 80, TRUE);
	DrawText (0, nTop + 344, " Audio: HDMI = dal TV/monitor; Jack = uscita cuffie del Pi 4 (il Pi 400 non ce l'ha).",
		  COLOR_DIM, COLOR_BACK, 80, TRUE);
	DrawText (0, nTop + 360, " Stereo: l'Amiga separa completamente i canali; in cuffia 70% e' piu' piacevole.",
		  COLOR_DIM, COLOR_BACK, 80, TRUE);

	const char *pHelp = s_pMenuMessage != 0 ? s_pMenuMessage
			  : "Su/Giu: voce   Sinistra/Destra: valore   Invio: conferma   Esc: chiudi";
	DrawText (0, nTop + 2 * PI_ROWS - 16, pHelp,
		  s_pMenuMessage != 0 ? COLOR_SELBACK : COLOR_DIM, COLOR_BACK, 80, TRUE);
}

#define HID_RIGHT	0x4F
#define HID_LEFT	0x50

static void SettingsKey (u8 ucKey)
{
	s_bMenuDirty = TRUE;
	s_pMenuMessage = 0;

	switch (ucKey)
	{
	case HID_UP:	if (s_nSettingsRow > 0) s_nSettingsRow--;	break;
	case HID_DOWN:	if (s_nSettingsRow < SET_ROWS - 1) s_nSettingsRow++;	break;

	case HID_LEFT:
	case HID_RIGHT: {
		int nDelta = ucKey == HID_RIGHT ? 1 : -1;
		if (s_nSettingsRow == 0)
		{
			s_nChipChoice = (s_nChipChoice + CHIP_OPTIONS + nDelta) % CHIP_OPTIONS;
		}
		else if (s_nSettingsRow == 1)
		{
			s_nSlowChoice = (s_nSlowChoice + SLOW_OPTIONS + nDelta) % SLOW_OPTIONS;
		}
		else if (s_nSettingsRow == 2)
		{
			s_nAudioChoice = (s_nAudioChoice + AUDIO_OPTIONS + nDelta) % AUDIO_OPTIONS;
		}
		else if (s_nSettingsRow == 3)
		{
			s_nStereoChoice = (s_nStereoChoice + STEREO_OPTIONS + nDelta) % STEREO_OPTIONS;
		}
		} break;

	case HID_ENTER:
	case HID_KPENTER:
		if (s_nSettingsRow == ROW_CANCEL)
		{
			s_bSettings = FALSE;
		}
		else if (s_nSettingsRow == ROW_JOY)
		{
			s_bSettings = FALSE;		// the choices made so far stay pending
			s_bJoyPage = TRUE;
			s_nJoyRow = 0;
			s_nCapture = -1;
		}
		else if (s_nSettingsRow == ROW_APPLY)
		{
			if (!SaveConfig (s_ChipOptions[s_nChipChoice], s_SlowOptions[s_nSlowChoice], s_nAudioChoice, s_nStereoChoice))
			{
				s_pMenuMessage = "Impossibile salvare OMEGA.CFG sulla scheda";
				break;
			}
			if (   s_ChipOptions[s_nChipChoice] != chipRamSize / 1024
			    || s_SlowOptions[s_nSlowChoice] != slowRamSize / 1024)
			{
				memoryConfigure (s_ChipOptions[s_nChipChoice] * 1024, s_SlowOptions[s_nSlowChoice] * 1024);
				s_bResetRequested = TRUE;	// done between two frames
			}
			s_nStereoActive = s_nStereoChoice;
			paulaSetStereoSeparation (s_StereoOptions[s_nStereoActive]);
			if (s_nAudioChoice != s_nAudioActive)
			{
				StartAudio (s_nAudioChoice);
			}
			s_bSettings = FALSE;
		}
		else
		{
			s_nSettingsRow = ROW_APPLY;		// Enter on a value: go to "Salva"
		}
		break;

	case HID_ESC:
	case HID_F5:
		s_bSettings = FALSE;
		break;
	}
}

// ---------------------------------------------------------------------------
// Joystick page

static CString KeyName (u8 ucKey)
{
	CString Name;
	if (ucKey >= 0x04 && ucKey <= 0x1D)
	{
		Name.Format ("%c", 'A' + ucKey - 0x04);
	}
	else if (ucKey >= 0x1E && ucKey <= 0x26)
	{
		Name.Format ("%c", '1' + ucKey - 0x1E);
	}
	else if (ucKey >= 0x3A && ucKey <= 0x45)
	{
		Name.Format ("F%u", ucKey - 0x3A + 1);
	}
	else if (ucKey >= 0x59 && ucKey <= 0x61)
	{
		Name.Format ("Tast. num. %u", ucKey - 0x59 + 1);
	}
	else
	{
		static const struct { u8 ucKey; const char *pName; } Names[] = {
			{0x27, "0"}, {0x28, "Invio"}, {0x29, "Esc"}, {0x2A, "Backspace"}, {0x2B, "Tab"},
			{0x2C, "Spazio"}, {0x2D, "-"}, {0x2E, "="}, {0x2F, "["}, {0x30, "]"}, {0x31, "\\"},
			{0x32, "#"}, {0x33, ";"}, {0x34, "'"}, {0x35, "`"}, {0x36, ","}, {0x37, "."},
			{0x38, "/"}, {0x39, "Bloc Maiusc"}, {0x49, "Ins"}, {0x4A, "Inizio"}, {0x4B, "Pag Su"},
			{0x4C, "Canc"}, {0x4D, "Fine"}, {0x4E, "Pag Giu'"}, {0x4F, "Freccia destra"},
			{0x50, "Freccia sinistra"}, {0x51, "Freccia giu'"}, {0x52, "Freccia su"},
			{0x54, "Tast. num. /"}, {0x55, "Tast. num. *"}, {0x56, "Tast. num. -"},
			{0x57, "Tast. num. +"}, {0x58, "Tast. num. Invio"}, {0x62, "Tast. num. 0"},
			{0x63, "Tast. num. ."}, {0x64, "<"},
			{0xE0, "Ctrl sinistro"}, {0xE1, "Maiusc sinistro"}, {0xE2, "Alt"}, {0xE3, "Win sinistro"},
			{0xE4, "Ctrl destro"}, {0xE5, "Maiusc destro"}, {0xE6, "Alt Gr"}, {0xE7, "Win destro"},
		};
		Name.Format ("tasto %02X", ucKey);
		for (unsigned i = 0; i < sizeof Names / sizeof Names[0]; i++)
		{
			if (Names[i].ucKey == ucKey)
			{
				Name = Names[i].pName;
			}
		}
	}
	return Name;
}

#define JOY_ROWS	13	// 0 on/off, 1-6 keys, 7 defaults, 8 pad B, 9 pad Start, 10 port 1, 11 save, 12 back
#define JROW_KEYS	1
#define JROW_DEFAULTS	7
#define JROW_PADB	8
#define JROW_START	9
#define JROW_PORT1	10
#define JROW_SAVE	11
#define JROW_BACK	12

static void DrawJoyPage (void)
{
	const unsigned nTop = MENU_TOP;
	FillRect (0, nTop, SCREEN_W, 2 * PI_ROWS, COLOR_BACK);
	DrawText (0, nTop, " Joystick, gamepad e tasti", COLOR_SELTEXT, COLOR_TEXT, 80, TRUE);

	CString Rows[JOY_ROWS];
	Rows[0].Format (" Joystick da tastiera (porta 2):  <  %s  >", s_bKeyboardJoy ? "acceso" : "spento");
	for (unsigned k = 0; k < JK_COUNT; k++)
	{
		if (s_nCapture == (int) k)
		{
			Rows[JROW_KEYS + k].Format ("   %-9s premi il nuovo tasto... (Esc annulla)", s_JoyKeyNames[k]);
		}
		else
		{
			Rows[JROW_KEYS + k].Format ("   %-9s %s", s_JoyKeyNames[k], (const char *) KeyName (s_JoyKeys[k]));
		}
	}
	Rows[JROW_DEFAULTS] = "   Tasti predefiniti (frecce, Ctrl destro, Alt Gr)";
	Rows[JROW_PADB].Format (" Gamepad, pulsante B/Y:  <  %s  >", s_PadBRoles[s_nPadBRole]);
	Rows[JROW_START].Format (" Gamepad, pulsante Start:  <  %s  >", s_StartKeys[s_nPadStart].pName);
	Rows[JROW_PORT1].Format (" Porta 1:  <  %s  >", s_bPort1Pad ? "secondo gamepad (niente mouse)" : "mouse");
	Rows[JROW_SAVE] = " Salva";
	Rows[JROW_BACK] = " Indietro";

	unsigned y = nTop + 28;
	for (unsigned i = 0; i < JOY_ROWS; i++)
	{
		if (i == JROW_PADB || i == JROW_SAVE)
		{
			y += 10;
		}
		boolean bSel = i == s_nJoyRow;
		boolean bDim = i >= JROW_KEYS && i <= JROW_DEFAULTS && !s_bKeyboardJoy;
		DrawText (8, y, (const char *) Rows[i],
			  bSel ? COLOR_SELTEXT : (bDim ? COLOR_DIM : COLOR_TEXT), bSel ? COLOR_SELBACK : COLOR_BACK, 62, TRUE);
		y += 22;
	}

	CString Pads;
	Pads.Format (" Gamepad USB:  1 = %s (porta 2)    2 = %s%s",
		     s_PadKnown[0] < 0 ? "nessuno" : s_PadKnown[0] ? "collegato" : "collegato (generico)",
		     s_PadKnown[1] < 0 ? "nessuno" : s_PadKnown[1] ? "collegato" : "collegato (generico)",
		     s_PadKnown[1] < 0 ? "" : s_bPort1Pad ? " (porta 1)" : " (inattivo)");
	y += 8;
	DrawText (0, y,      (const char *) Pads, COLOR_TEXT, COLOR_BACK, 80, TRUE);
	DrawText (0, y + 20, " Gamepad: croce o levetta = direzioni, A/X/R = fuoco, Select = menu dischi DF0:.",
		  COLOR_DIM, COLOR_BACK, 80, TRUE);
	DrawText (0, y + 36, " Con il joystick da tastiera acceso, quei tasti non arrivano all'Amiga.",
		  COLOR_DIM, COLOR_BACK, 80, TRUE);
	DrawText (0, y + 52, " Quasi tutti i giochi usano la porta 2; la porta 1 serve per i giochi a due.",
		  COLOR_DIM, COLOR_BACK, 80, TRUE);

	const char *pHelp = s_pMenuMessage != 0 ? s_pMenuMessage
			  : "Su/Giu': voce   Sinistra/Destra: valore   Invio: cambia tasto   Esc: indietro";
	DrawText (0, nTop + 2 * PI_ROWS - 16, pHelp,
		  s_pMenuMessage != 0 ? COLOR_SELBACK : COLOR_DIM, COLOR_BACK, 80, TRUE);
}

static void JoyKey (u8 ucKey)
{
	s_bMenuDirty = TRUE;
	s_pMenuMessage = 0;

	if (s_nCapture >= 0)
	{
		if (ucKey != HID_ESC)
		{
			s_JoyKeys[s_nCapture] = ucKey;
		}
		s_nCapture = -1;
		return;
	}

	switch (ucKey)
	{
	case HID_UP:	if (s_nJoyRow > 0) s_nJoyRow--;			break;
	case HID_DOWN:	if (s_nJoyRow < JOY_ROWS - 1) s_nJoyRow++;	break;

	case HID_LEFT:
	case HID_RIGHT: {
		int nDelta = ucKey == HID_RIGHT ? 1 : -1;
		if (s_nJoyRow == 0)		s_bKeyboardJoy = !s_bKeyboardJoy;
		else if (s_nJoyRow == JROW_PADB)	s_nPadBRole = (s_nPadBRole + 3 + nDelta) % 3;
		else if (s_nJoyRow == JROW_START)	s_nPadStart = (s_nPadStart + START_KEYS + nDelta) % START_KEYS;
		else if (s_nJoyRow == JROW_PORT1)	s_bPort1Pad = !s_bPort1Pad;
		} break;

	case HID_ENTER:
	case HID_KPENTER:
		if (s_nJoyRow == 0)
		{
			s_bKeyboardJoy = !s_bKeyboardJoy;
		}
		else if (s_nJoyRow >= JROW_KEYS && s_nJoyRow < JROW_KEYS + JK_COUNT)
		{
			s_nCapture = s_nJoyRow - JROW_KEYS;
		}
		else if (s_nJoyRow == JROW_DEFAULTS)
		{
			memcpy (s_JoyKeys, s_DefaultJoyKeys, sizeof s_JoyKeys);
		}
		else if (s_nJoyRow == JROW_SAVE)
		{
			unsigned nChip = IndexOf (s_ChipOptions, CHIP_OPTIONS, chipRamSize / 1024);
			unsigned nSlow = IndexOf (s_SlowOptions, SLOW_OPTIONS, slowRamSize / 1024);
			if (!SaveConfig (s_ChipOptions[nChip], s_SlowOptions[nSlow], s_nAudioActive, s_nStereoActive))
			{
				s_pMenuMessage = "Impossibile salvare OMEGA.CFG sulla scheda";
				break;
			}
			s_bJoyPage = FALSE;		// back to the game
		}
		else if (s_nJoyRow == JROW_BACK)
		{
			s_bJoyPage = FALSE;
			s_bSettings = TRUE;
		}
		else
		{
			JoyKey (HID_RIGHT);
		}
		break;

	case HID_ESC:
		s_bJoyPage = FALSE;
		s_bSettings = TRUE;
		break;
	}
}

static boolean s_SentToAmiga[256];	// keys the Amiga has seen pressed

static int JoyKeyIndex (u8 ucKey)
{
	for (unsigned k = 0; k < JK_COUNT; k++)
	{
		if (s_JoyKeys[k] == ucKey)
		{
			return k;
		}
	}
	return -1;
}

static boolean MenuOpen (void)
{
	return s_bMenu || s_bSettings || s_bJoyPage;
}

// One key (HID usage; modifiers are 0xE0..0xE7) pressed or released
static void KeyEvent (u8 ucKey, boolean bDown)
{
	if (!bDown)
	{
		if (s_SentToAmiga[ucKey])
		{
			s_SentToAmiga[ucKey] = FALSE;
			inputKeyUp (s_HIDToAmiga[ucKey]);
		}
		int k = JoyKeyIndex (ucKey);
		if (k >= 0)
		{
			s_nKbJoyBits &= ~s_JoyKeyBits[k];
		}
		return;
	}

	if (MenuOpen ())
	{
		if (s_bJoyPage)		JoyKey (ucKey);
		else if (s_bSettings)	SettingsKey (ucKey);
		else			MenuKey (ucKey);
		return;
	}

	if (s_bKeyboardJoy)
	{
		int k = JoyKeyIndex (ucKey);
		if (k >= 0)
		{
			s_nKbJoyBits |= s_JoyKeyBits[k];
			return;			// the key is the joystick now
		}
	}

	if (ucKey >= HID_F1 && ucKey <= HID_F4)
	{
		OpenMenu (ucKey - HID_F1);
		return;
	}
	if (ucKey == HID_F5)
	{
		OpenSettings ();
		return;
	}
	if (ucKey == HID_F12)
	{
		s_bSnapshotRequested = TRUE;	// saved between two frames
		return;
	}
	if (s_HIDToAmiga[ucKey] != 0xFF && !s_SentToAmiga[ucKey])
	{
		s_SentToAmiga[ucKey] = TRUE;
		inputKeyDown (s_HIDToAmiga[ucKey]);
	}
}

static unsigned ReportKeys (const TKeyReport &Report, u8 *pKeys)
{
	unsigned n = 0;
	for (unsigned i = 0; i < 8; i++)
	{
		if (Report.Modifiers & (1 << i))
		{
			pKeys[n++] = 0xE0 + i;
		}
	}
	for (unsigned i = 0; i < 6; i++)
	{
		if (Report.Keys[i] > 3)
		{
			pKeys[n++] = Report.Keys[i];
		}
	}
	return n;
}

static boolean InList (const u8 *pKeys, unsigned n, u8 ucKey)
{
	for (unsigned i = 0; i < n; i++)
	{
		if (pKeys[i] == ucKey)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void ProcessReport (const TKeyReport &New)
{
	// a report full of 0x01 means "too many keys pressed": ignore it
	if (New.Keys[0] == 0x01)
	{
		return;
	}

	u8 Old[14], Now[14];
	unsigned nOld = ReportKeys (s_Previous, Old);
	unsigned nNow = ReportKeys (New, Now);
	s_Previous = New;

	for (unsigned i = 0; i < nOld; i++)		// released keys
	{
		if (!InList (Now, nNow, Old[i]))
		{
			KeyEvent (Old[i], FALSE);
		}
	}
	for (unsigned i = 0; i < nNow; i++)		// pressed keys
	{
		if (!InList (Old, nOld, Now[i]))
		{
			KeyEvent (Now[i], TRUE);
		}
	}
}

static void ProcessKeyboard (void)
{
	while (s_nReportTail != s_nReportHead)
	{
		DataMemBarrier ();
		TKeyReport Report = s_Reports[s_nReportTail];
		s_nReportTail = (s_nReportTail + 1) % REPORT_QUEUE;
		ProcessReport (Report);
	}
}

// ---------------------------------------------------------------------------
// Status bar in the black band below the Amiga picture

#define STATUS_Y	(SCREEN_H - 34)

static unsigned s_nLastFrame = 0;
static int s_nFrames = 0;
static boolean s_bLED = FALSE;
static unsigned s_nFrameEnd = 0;		// time the previous hostDisplay() finished
static unsigned s_nBusyUs = 0;			// emulation time in the current second
static unsigned s_nSecondStart = 0;
static unsigned s_nFramesThisSecond = 0;
static unsigned s_nFPS = 0;
static unsigned s_nLoad = 0;			// % of real time spent emulating

// ---------------------------------------------------------------------------
// F12: save a snapshot of the machine to the SD card (SNAP0001.BIN, ...) so the
// screen can be reproduced and debugged on a PC (see Snapshot.c)

static CString s_Message;
static int s_nMessageFrames = 0;

static int FatWriter (const void *pData, size_t nSize, void *pContext)
{
	UINT nWritten = 0;
	return f_write ((FIL *) pContext, pData, (UINT) nSize, &nWritten) == FR_OK && nWritten == nSize;
}

static void SaveSnapshot (void)
{
	CString Name;
	FILINFO Info;
	unsigned n;
	for (n = 1; n < 10000; n++)
	{
		Name.Format ("%sSNAP%04u.BIN", ROOT, n);
		if (f_stat ((const char *) Name, &Info) != FR_OK)
		{
			break;		// free name
		}
	}

	FIL File;
	boolean bOK = f_open (&File, (const char *) Name, FA_WRITE | FA_CREATE_NEW) == FR_OK;
	if (bOK)
	{
		bOK = snapshotSave (FatWriter, &File);
		bOK = f_close (&File) == FR_OK && bOK;
	}

	if (bOK)
	{
		s_Message.Format ("Istantanea salvata: SNAP%04u.BIN", n);
	}
	else
	{
		s_Message = "Errore: impossibile salvare l'istantanea sulla scheda";
	}
	s_nMessageFrames = 4 * 60;
}

static void DrawStatus (void)
{
	CKernel *pKernel = CKernel::Get ();
	CCPUThrottle *pCPU = pKernel->GetCPUThrottle ();

	CString Line1;
	Line1.Format ("F1-4 dischi F5 opzioni %2u fps carico %3u%% %uMHz %uC Tast:%s Mouse:%s",
		      s_nFPS, s_nLoad, pCPU->GetClockRate () / 1000000, pCPU->GetTemperature (),
		      pKernel->HasKeyboard () ? "si" : "NO", pKernel->HasMouse () ? "si" : "NO");

	CString Line2;
	if (s_nMessageFrames > 0)
	{
		Line2 = s_Message;
	}
	else for (unsigned i = 0; i < 4; i++)
	{
		const TDrive &Drive = s_Drive[i];
		if (Drive.Name[0] == '\0' && i > 0)
		{
			continue;
		}
		CString Part;
		Part.Format ("DF%u:%s%s  ", i,
			     Drive.Name[0] != '\0' ? Drive.Name : "vuoto",
			     Drive.nInsertCountdown > 0 || Drive.bPendingInsert ? " (inserimento...)" : "");
		Line2.Append ((const char *) Part);
	}

	DrawText (0, STATUS_Y,      (const char *) Line1, COLOR_STATUS, COLOR_BLACK, 80, TRUE);
	DrawText (0, STATUS_Y + 17, (const char *) Line2, COLOR_STATUS, COLOR_BLACK, 80, TRUE);
}

// ---------------------------------------------------------------------------
// Sound

#define SOUND_RATE		PAULA_DEFAULT_RATE
#define SOUND_CHUNK		(384 * 6)	// samples per DMA block (HDMI needs a multiple of 384)
#define SOUND_QUEUE_MS		200
#define SOUND_TARGET_FRAMES	(SOUND_RATE * 60 / 1000)	// keep ~60 ms queued

static CSoundBaseDevice *s_pSound = 0;
static int16_t s_SoundBuffer[4096 * 2];

static void StopAudio (void)
{
	if (s_pSound == 0)
	{
		return;
	}
	if (s_pSound->IsActive ())
	{
		s_pSound->Cancel ();
		unsigned nStart = CTimer::GetClockTicks ();
		while (s_pSound->IsActive () && CTimer::GetClockTicks () - nStart < 500000)
		{
			// wait for the DMA to stop
		}
	}
	delete s_pSound;
	s_pSound = 0;
}

static void QueueSilence (unsigned nFrames)
{
	static const int16_t Zero[256 * 2] = {0};
	while (s_pSound != 0 && nFrames > 0)
	{
		unsigned n = nFrames > 256 ? 256 : nFrames;
		s_pSound->Write (Zero, n * 4);
		nFrames -= n;
	}
}

static void StartAudio (unsigned nOut)
{
	StopAudio ();
	s_nAudioActive = AudioOff;

	CInterruptSystem *pInterrupt = CKernel::Get ()->GetInterrupt ();
	if (nOut == AudioHDMI)
	{
		s_pSound = new CHDMISoundBaseDevice (pInterrupt, SOUND_RATE, SOUND_CHUNK);
	}
	else if (nOut == AudioJack)
	{
		s_pSound = new CPWMSoundBaseDevice (pInterrupt, SOUND_RATE, SOUND_CHUNK);
	}

	if (s_pSound != 0)
	{
		if (   !s_pSound->AllocateQueue (SOUND_QUEUE_MS))
		{
			delete s_pSound;
			s_pSound = 0;
		}
		else
		{
			s_pSound->SetWriteFormat (SoundFormatSigned16, 2);
			QueueSilence (SOUND_TARGET_FRAMES);
			if (!s_pSound->Start ())
			{
				delete s_pSound;
				s_pSound = 0;
			}
		}
		if (s_pSound == 0)
		{
			s_Message = "Uscita audio non disponibile";
			s_nMessageFrames = 4 * 60;
		}
	}

	if (s_pSound != 0)
	{
		s_nAudioActive = nOut;
	}
	paulaSetEnabled (s_pSound != 0);
	paulaSetRate (SOUND_RATE);
	paulaRead (s_SoundBuffer, 4096);	// drop anything produced meanwhile
}

static void FeedAudio (void)
{
	if (s_pSound == 0)
	{
		return;
	}
	unsigned nQueued = s_pSound->GetQueueFramesAvail ();
	int n;
	while ((n = paulaRead (s_SoundBuffer, 4096)) > 0)
	{
		s_pSound->Write (s_SoundBuffer, n * 4);	// a full queue simply drops the rest
	}
	paulaFollowQueue ((int) nQueued, SOUND_TARGET_FRAMES);
}

// ---------------------------------------------------------------------------
// Joysticks: keyboard and gamepads -> Amiga ports, once per frame

static unsigned s_nPadPrevious[2] = {0, 0};
static boolean s_bStartDown = FALSE;
static u8 s_ucStartAmigaKey = 0xFF;

static unsigned PadToJoy (unsigned nPad)
{
	unsigned nBits = nPad & (JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT | JOY_FIRE);
	if (nPad & PAD_B)
	{
		nBits |= s_nPadBRole == 0 ? JOY_FIRE2 : s_nPadBRole == 1 ? JOY_UP : JOY_FIRE;
	}
	return nBits;
}

static void UpdateJoysticks (void)
{
	unsigned nPad0 = s_PadBits[0], nPad1 = s_PadBits[1];

	// Select on either gamepad: disk menu for DF0:
	if (((nPad0 & ~s_nPadPrevious[0]) | (nPad1 & ~s_nPadPrevious[1])) & PAD_SELECT)
	{
		OpenMenu (0);
	}

	// Start: an Amiga key (many games wait for Space, F1, ...)
	boolean bStart = ((nPad0 | nPad1) & PAD_START) != 0;
	if (bStart != s_bStartDown)
	{
		s_bStartDown = bStart;
		if (bStart && s_StartKeys[s_nPadStart].ucAmiga != 0xFF)
		{
			s_ucStartAmigaKey = s_StartKeys[s_nPadStart].ucAmiga;
			inputKeyDown (s_ucStartAmigaKey);
		}
		else if (!bStart && s_ucStartAmigaKey != 0xFF)
		{
			inputKeyUp (s_ucStartAmigaKey);
			s_ucStartAmigaKey = 0xFF;
		}
	}
	s_nPadPrevious[0] = nPad0;
	s_nPadPrevious[1] = nPad1;

	if (!s_bKeyboardJoy)
	{
		s_nKbJoyBits = 0;
	}
	boolean bPort1 = s_bPort1Pad && s_PadKnown[1] >= 0;
	inputJoystick (1, s_nKbJoyBits | PadToJoy (nPad0) | (bPort1 ? 0 : PadToJoy (nPad1)));
	inputPort1Joystick (bPort1);
	inputJoystick (0, bPort1 ? PadToJoy (nPad1) : 0);
}

// Gamepad in the menus: directions = arrows, A = Enter, B = Esc
static unsigned s_nPadMenuPrevious = 0;

static void PadMenuKeys (void)
{
	unsigned nPad = s_PadBits[0] | s_PadBits[1];
	unsigned nPressed = nPad & ~s_nPadMenuPrevious;
	s_nPadMenuPrevious = nPad;
	if (s_nCapture >= 0)
	{
		return;
	}
	if (nPressed & JOY_UP)		KeyEvent (HID_UP, TRUE);
	if (nPressed & JOY_DOWN)	KeyEvent (HID_DOWN, TRUE);
	if (nPressed & JOY_LEFT)	KeyEvent (HID_LEFT, TRUE);
	if (nPressed & JOY_RIGHT)	KeyEvent (HID_RIGHT, TRUE);
	if (nPressed & JOY_FIRE)	KeyEvent (HID_ENTER, TRUE);
	if (nPressed & (PAD_B | PAD_SELECT))	KeyEvent (HID_ESC, TRUE);
	s_nPadPrevious[0] = s_PadBits[0];	// no Select edge when the menu closes
	s_nPadPrevious[1] = s_PadBits[1];
}

// ---------------------------------------------------------------------------
// Host interface

int hostInit (void)
{
	BuildKeymap ();
	LoadConfig ();
	printf ("Memoria: chip %u KB, slow %u KB (F5 per cambiare)\n", chipRamSize / 1024, slowRamSize / 1024);
	memset (&s_Previous, 0, sizeof s_Previous);
	memset (s_Drive, 0, sizeof s_Drive);

	CKernel *pKernel = CKernel::Get ();
	s_pFrameBuffer = pKernel->GetScreen ()->GetFrameBuffer ();
	if (s_pFrameBuffer == 0 || s_pFrameBuffer->GetDepth () != 32)
	{
		printf ("A 32 bit framebuffer is required (build Circle with DEPTH=32)\n");
		return 0;
	}

	s_pScreen = (u32 *) (uintptr) s_pFrameBuffer->GetBuffer ();
	s_nPitch  = s_pFrameBuffer->GetPitch () / 4;

	// the internal drive DF0: is attached by main.c; add three external drives
	for (int i = 1; i < 4; i++)
	{
		floppyAttach (i);
	}

	CCPUThrottle *pCPU = pKernel->GetCPUThrottle ();
	printf ("CPU %u MHz, tastiera: %s, mouse: %s\n",
		pCPU->GetClockRate () / 1000000,
		pKernel->HasKeyboard () ? "si" : "no", pKernel->HasMouse () ? "si" : "no");
	printf ("Avvio tra 3 secondi. Premi F1 per inserire un disco in DF0:\n");
	CTimer::SimpleMsDelay (3000);	// leave the boot messages readable

	s_pFont = new CCharGenerator (Font8x16);

	piSetConsoleOnScreen (0);	// from now on the screen belongs to the Amiga
	pKernel->LogToSerial ();	// USB messages (e.g. a mouse plugged in) must not write over it

	FillRect (0, 0, s_pFrameBuffer->GetWidth (), s_pFrameBuffer->GetHeight (), COLOR_BLACK);

	paulaSetStereoSeparation (s_StereoOptions[s_nStereoActive]);
	StartAudio (s_nAudioActive);

	s_nLastFrame = CTimer::GetClockTicks ();
	s_nFrameEnd = s_nLastFrame;
	s_nSecondStart = s_nLastFrame;
	return 1;
}

int hostPollEvents (void)
{
	CKernel::Get ()->UpdateUSB ();
	ProcessKeyboard ();
	if (s_bResetRequested)
	{
		s_bResetRequested = FALSE;
		cpu_pulse_reset ();	// new memory size: restart the Amiga (disks stay in)
	}
	return 1;	// there is nothing to quit to
}

// The emulation is paused while the disk menu is open
static void RunMenu (void)
{
	// the Amiga must not see the joystick held while the menu is open
	s_nKbJoyBits = 0;
	inputJoystick (0, 0);
	inputJoystick (1, 0);
	s_nPadMenuPrevious = s_PadBits[0] | s_PadBits[1];	// buttons already held don't count

	while (MenuOpen ())
	{
		CKernel::Get ()->UpdateUSB ();
		ProcessKeyboard ();
		PadMenuKeys ();
		if (s_bMenuDirty)
		{
			s_bMenuDirty = FALSE;
			if (s_bJoyPage)
			{
				DrawJoyPage ();
				DrawStatus ();
			}
			else if (s_bSettings)
			{
				DrawSettings ();
				DrawStatus ();
			}
			else if (s_bMenu)
			{
				DrawMenu ();
				DrawStatus ();
			}
		}
	}

	// the sound queue ran dry while paused: start again with a little silence
	if (s_pSound != 0)
	{
		unsigned nQueued = s_pSound->GetQueueFramesAvail ();
		if (nQueued < SOUND_TARGET_FRAMES)
		{
			QueueSilence (SOUND_TARGET_FRAMES - nQueued);
		}
	}

	// don't let the paused time count as "late"
	s_nLastFrame = CTimer::GetClockTicks ();
	s_nFrameEnd = s_nLastFrame;
	s_nSecondStart = s_nLastFrame;
	s_nBusyUs = 0;
	s_nFramesThisSecond = 0;
}

void hostDisplay (void)
{
	unsigned nStart = CTimer::GetClockTicks ();
	s_nBusyUs += nStart - s_nFrameEnd;
	s_nFrames++;
	s_nFramesThisSecond++;

	UpdateDrives ();

	if (nStart - s_nSecondStart >= 1000000)
	{
		unsigned nElapsed = nStart - s_nSecondStart;
		s_nFPS = s_nFramesThisSecond * 1000000 / nElapsed;
		s_nLoad = (unsigned) ((u64) s_nBusyUs * 100 / nElapsed);
		s_nFramesThisSecond = 0;
		s_nBusyUs = 0;
		s_nSecondStart = nStart;
		CKernel::Get ()->GetCPUThrottle ()->Update ();	// keeps the SoC below its maximum temperature
	}

	// mouse
	EnterCritical ();
	int nDX = s_nMouseDX;
	int nDY = s_nMouseDY;
	unsigned nButtons = s_nMouseButtons;
	s_nMouseDX = 0;
	s_nMouseDY = 0;
	LeaveCritical ();

	inputMouseButtons ((nButtons & MOUSE_BUTTON_LEFT) != 0, (nButtons & MOUSE_BUTTON_RIGHT) != 0);
	inputMouseMove (nDX, nDY);

	// floppy motor -> ACT LED
	boolean bMotor = (CIAB.prb & 0x80) == 0;
	if (bMotor != s_bLED)
	{
		s_bLED = bMotor;
		if (bMotor)
		{
			CKernel::Get ()->GetActLED ()->On ();
		}
		else
		{
			CKernel::Get ()->GetActLED ()->Off ();
		}
	}

	// copy 640x256 -> 640x512 (line doubled) at the top of the 640x548 screen
	const unsigned nTop = PICTURE_TOP;
	for (unsigned y = 0; y < PI_ROWS; y++)
	{
		const u32 *pSrc = &host.pixels[y * FB_WIDTH];
		u32 *pDst = &s_pScreen[(nTop + 2 * y) * s_nPitch];
		memcpy (pDst, pSrc, FB_WIDTH * 4);
		memcpy (pDst + s_nPitch, pSrc, FB_WIDTH * 4);
	}

	if (s_nMessageFrames > 0)
	{
		s_nMessageFrames--;
	}

	if (s_bSnapshotRequested)
	{
		s_bSnapshotRequested = FALSE;
		SaveSnapshot ();
		DrawStatus ();
		s_nLastFrame = CTimer::GetClockTicks ();	// the save took a while: don't catch up
	}
	else if ((s_nFrames % 15) == 0)
	{
		DrawStatus ();
	}

	UpdateJoysticks ();

	if (MenuOpen ())
	{
		RunMenu ();
		DrawStatus ();
		s_nFrameEnd = CTimer::GetClockTicks ();
		return;
	}

	FeedAudio ();

	// pace to real time; if we are late (e.g. USB enumeration) don't try to catch up
	unsigned nNow = CTimer::GetClockTicks ();
	while (nNow - s_nLastFrame < FRAME_US)
	{
		nNow = CTimer::GetClockTicks ();
	}
	if (nNow - s_nLastFrame > 4 * FRAME_US)
	{
		s_nLastFrame = nNow;
	}
	else
	{
		s_nLastFrame += FRAME_US;
	}
	s_nFrameEnd = CTimer::GetClockTicks ();
}

void hostShutdown (void)
{
}

void toggleLEDs (void)
{
}
