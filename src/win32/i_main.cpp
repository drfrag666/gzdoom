/*
** i_main.cpp
** System-specific startup code. Eventually calls D_DoomMain.
**
**---------------------------------------------------------------------------
** Copyright 1998-2007 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

// HEADER FILES ------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#include <commctrl.h>
#include <richedit.h>

//#include <wtsapi32.h>
#define NOTIFY_FOR_THIS_SESSION 0

#include <stdlib.h>
#ifdef _MSC_VER
#include <eh.h>
#include <new.h>
#include <crtdbg.h>
#endif
#include "resource.h"

#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#define USE_WINDOWS_DWORD
#include "doomerrors.h"
#include "hardware.h"

#include "doomtype.h"
#include "m_argv.h"
#include "d_main.h"
#include "i_system.h"
#include "c_console.h"
#include "version.h"
#include "i_video.h"
#include "i_sound.h"
#include "i_input.h"
#include "w_wad.h"
#include "templates.h"
#include "cmdlib.h"
#include "g_level.h"
#include "doomstat.h"
#include "r_main.h"
// [BB] New #includes.
#include "serverconsole/serverconsole.h"
#include "network.h"
#include "p_acs.h"

#include "stats.h"
#include "st_start.h"

#include <assert.h>

// MACROS ------------------------------------------------------------------

// The main window's title.
#ifdef _M_X64
#define X64 " 64-bit"
#else
#define X64 ""
#endif

#define WINDOW_TITLE GAMESIG " " DOTVERSIONSTR X64 " (" __DATE__ ")"

// The maximum number of functions that can be registered with atterm.
#define MAX_TERMS	64

// TYPES -------------------------------------------------------------------

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

LRESULT CALLBACK WndProc (HWND, UINT, WPARAM, LPARAM);
void CreateCrashLog (char *custominfo, DWORD customsize);
void DisplayCrashLog ();
extern BYTE *ST_Util_BitsForBitmap (BITMAPINFO *bitmap_info);

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern EXCEPTION_POINTERS CrashPointers;
extern BITMAPINFO *StartupBitmap;
extern UINT TimerPeriod;
extern HCURSOR TheArrowCursor, TheInvisibleCursor;

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// The command line arguments.
DArgs *Args;

HINSTANCE		g_hInst;
DWORD			SessionID;
HANDLE			MainThread;
DWORD			MainThreadID;
HANDLE			StdOut;
bool			FancyStdOut, AttachedStdOut;

// The main window
HWND			Window;

// The subwindows used for startup and error output
HWND			ConWindow, GameTitleWindow;
HWND			ErrorPane, ProgressBar, NetStartPane, StartupScreen, ErrorIcon;

HFONT			GameTitleFont;
LONG			GameTitleFontHeight;
LONG			DefaultGUIFontHeight;
LONG			ErrorIconChar;

// [BC] New stuff for the server console.
HANDLE			hStdout;
HANDLE			hStdin;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static const char WinClassName[] = GAMENAME " MainWindow";
static HMODULE hwtsapi32;		// handle to wtsapi32.dll
static void (*TermFuncs[MAX_TERMS])(void);
static int NumTerms;

// CODE --------------------------------------------------------------------

//==========================================================================
//
// atterm
//
// Our own atexit because atexit can be problematic under Linux, though I
// forget the circumstances that cause trouble.
//
//==========================================================================

void atterm (void (*func)(void))
{
	// Make sure this function wasn't already registered.
	for (int i = 0; i < NumTerms; ++i)
	{
		if (TermFuncs[i] == func)
		{
			return;
		}
	}
	if (NumTerms == MAX_TERMS)
	{
		func ();
		I_FatalError ("Too many exit functions registered.\nIncrease MAX_TERMS in i_main.cpp");
	}
	TermFuncs[NumTerms++] = func;
}

//==========================================================================
//
// popterm
//
// Removes the most recently register atterm function.
//
//==========================================================================

void popterm ()
{
	if (NumTerms)
		NumTerms--;
}

//==========================================================================
//
// call_terms
//
//==========================================================================

static void STACK_ARGS call_terms (void)
{
	while (NumTerms > 0)
	{
		TermFuncs[--NumTerms]();
	}
}

#ifdef _MSC_VER
static int STACK_ARGS NewFailure (size_t size)
{
	I_FatalError ("Failed to allocate %d bytes from process heap", size);
	return 0;
}
#endif

//==========================================================================
//
// UnCOM
//
// Called by atterm if CoInitialize() succeeded.
//
//==========================================================================

static void UnCOM (void)
{
	CoUninitialize ();
}

//==========================================================================
//
// UnWTS
//
// Called by atterm if RegisterSessionNotification() succeeded.
//
//==========================================================================

static void UnWTS (void)
{
	if (hwtsapi32 != 0)
	{
		typedef BOOL (WINAPI *ursn)(HWND);
		ursn unreg = (ursn)GetProcAddress (hwtsapi32, "WTSUnRegisterSessionNotification");
		if (unreg != 0)
		{
			unreg (Window);
		}
		FreeLibrary (hwtsapi32);
		hwtsapi32 = 0;
	}
}

//==========================================================================
//
// FinalGC
//
// If this doesn't free everything, the debug CRT will let us know.
//
//==========================================================================

static void FinalGC()
{
	Args = NULL;
	GC::FullGC();
}

//==========================================================================
//
// LayoutErrorPane
//
// Lays out the error pane to the desired width, returning the required
// height.
//
//==========================================================================

static int LayoutErrorPane (HWND pane, int w)
{
	HWND ctl;
	RECT rectc;

	// Right-align the Quit button.
	ctl = GetDlgItem (pane, IDOK);
	GetClientRect (ctl, &rectc);	// Find out how big it is.
	MoveWindow (ctl, w - rectc.right - 1, 1, rectc.right, rectc.bottom, TRUE);
	InvalidateRect (ctl, NULL, TRUE);

	// Return the needed height for this layout
	return rectc.bottom + 2;
}

//==========================================================================
//
// LayoutNetStartPane
//
// Lays out the network startup pane to the specified width, returning
// its required height.
//
//==========================================================================

int LayoutNetStartPane (HWND pane, int w)
{
	HWND ctl;
	RECT margin, rectc;
	int staticheight, barheight;

	// Determine margin sizes.
	SetRect (&margin, 7, 7, 0, 0);
	MapDialogRect (pane, &margin);

	// Stick the message text in the upper left corner.
	ctl = GetDlgItem (pane, IDC_NETSTARTMESSAGE);
	GetClientRect (ctl, &rectc);
	MoveWindow (ctl, margin.left, margin.top, rectc.right, rectc.bottom, TRUE);

	// Stick the count text in the upper right corner.
	ctl = GetDlgItem (pane, IDC_NETSTARTCOUNT);
	GetClientRect (ctl, &rectc);
	MoveWindow (ctl, w - rectc.right - margin.left, margin.top, rectc.right, rectc.bottom, TRUE);
	staticheight = rectc.bottom;

	// Stretch the progress bar to fill the entire width.
	ctl = GetDlgItem (pane, IDC_NETSTARTPROGRESS);
	barheight = GetSystemMetrics (SM_CYVSCROLL);
	MoveWindow (ctl, margin.left, margin.top*2 + staticheight, w - margin.left*2, barheight, TRUE);

	// Center the abort button underneath the progress bar.
	ctl = GetDlgItem (pane, IDCANCEL);
	GetClientRect (ctl, &rectc);
	MoveWindow (ctl, (w - rectc.right) / 2, margin.top*3 + staticheight + barheight, rectc.right, rectc.bottom, TRUE);

	return margin.top*4 + staticheight + barheight + rectc.bottom;
}

//==========================================================================
//
// LayoutMainWindow
//
// Lays out the main window with the game title and log controls and
// possibly the error pane and progress bar.
//
//==========================================================================

void LayoutMainWindow (HWND hWnd, HWND pane)
{
	RECT rect;
	int errorpaneheight = 0;
	int bannerheight = 0;
	int progressheight = 0;
	int netpaneheight = 0;
	int leftside = 0;
	int w, h;

	GetClientRect (hWnd, &rect);
	w = rect.right;
	h = rect.bottom;

	if (DoomStartupInfo != NULL && GameTitleWindow != NULL)
	{
		bannerheight = GameTitleFontHeight + 5;
		MoveWindow (GameTitleWindow, 0, 0, w, bannerheight, TRUE);
		InvalidateRect (GameTitleWindow, NULL, FALSE);
	}
	if (ProgressBar != NULL)
	{
		// Base the height of the progress bar on the height of a scroll bar
		// arrow, just as in the progress bar example.
		progressheight = GetSystemMetrics (SM_CYVSCROLL);
		MoveWindow (ProgressBar, 0, h - progressheight, w, progressheight, TRUE);
	}
	if (NetStartPane != NULL)
	{
		netpaneheight = LayoutNetStartPane (NetStartPane, w);
		SetWindowPos (NetStartPane, HWND_TOP, 0, h - progressheight - netpaneheight, w, netpaneheight, SWP_SHOWWINDOW);
	}
	if (pane != NULL)
	{
		errorpaneheight = LayoutErrorPane (pane, w);
		SetWindowPos (pane, HWND_TOP, 0, h - progressheight - netpaneheight - errorpaneheight, w, errorpaneheight, 0);
	}
	if (ErrorIcon != NULL)
	{
		leftside = GetSystemMetrics (SM_CXICON) + 6;
		MoveWindow (ErrorIcon, 0, bannerheight, leftside, h - bannerheight - errorpaneheight - progressheight - netpaneheight, TRUE);
	}
	// If there is a startup screen, it covers the log window
	if (StartupScreen != NULL)
	{
		SetWindowPos (StartupScreen, HWND_TOP, leftside, bannerheight, w - leftside,
			h - bannerheight - errorpaneheight - progressheight - netpaneheight, SWP_SHOWWINDOW);
		InvalidateRect (StartupScreen, NULL, FALSE);
		MoveWindow (ConWindow, 0, 0, 0, 0, TRUE);
	}
	else
	{
		// The log window uses whatever space is left.
		MoveWindow (ConWindow, leftside, bannerheight, w - leftside,
			h - bannerheight - errorpaneheight - progressheight - netpaneheight, TRUE);
	}
}

//==========================================================================
//
// LConProc
//
// The main window's WndProc during startup. During gameplay, the WndProc
// in i_input.cpp is used instead.
//
//==========================================================================

LRESULT CALLBACK LConProc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HWND view;
	HDC hdc;
	HBRUSH hbr;
	HGDIOBJ oldfont;
	RECT rect;
	int titlelen;
	SIZE size;
	LOGFONT lf;
	TEXTMETRIC tm;
	HINSTANCE inst = (HINSTANCE)(LONG_PTR)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);
	DRAWITEMSTRUCT *drawitem;
	CHARFORMAT2 format;

	switch (msg)
	{
	case WM_CREATE:
		// Create game title static control
		memset (&lf, 0, sizeof(lf));
		hdc = GetDC (hWnd);
		lf.lfHeight = -MulDiv(12, GetDeviceCaps(hdc, LOGPIXELSY), 72);
		lf.lfCharSet = ANSI_CHARSET;
		lf.lfWeight = FW_BOLD;
		lf.lfPitchAndFamily = VARIABLE_PITCH | FF_ROMAN;
		strcpy (lf.lfFaceName, "Trebuchet MS");
		GameTitleFont = CreateFontIndirect (&lf);

		oldfont = SelectObject (hdc, GetStockObject (DEFAULT_GUI_FONT));
		GetTextMetrics (hdc, &tm);
		DefaultGUIFontHeight = tm.tmHeight;
		if (GameTitleFont == NULL)
		{
			GameTitleFontHeight = DefaultGUIFontHeight;
		}
		else
		{
			SelectObject (hdc, GameTitleFont);
			GetTextMetrics (hdc, &tm);
			GameTitleFontHeight = tm.tmHeight;
		}
		SelectObject (hdc, oldfont);

		// Create log read-only edit control
		view = CreateWindowEx (WS_EX_NOPARENTNOTIFY, RICHEDIT_CLASS, NULL,
			WS_CHILD | WS_VISIBLE | WS_VSCROLL |
			ES_LEFT | ES_MULTILINE | WS_CLIPSIBLINGS,
			0, 0, 0, 0,
			hWnd, NULL, inst, NULL);
		HRESULT hr;
		hr = GetLastError();
		if (view == NULL)
		{
			ReleaseDC (hWnd, hdc);
			return -1;
		}
		SendMessage (view, EM_SETREADONLY, TRUE, 0);
		SendMessage (view, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
		SendMessage (view, EM_SETBKGNDCOLOR, 0, RGB(70,70,70));
		// Setup default font for the log.
		//SendMessage (view, WM_SETFONT, (WPARAM)GetStockObject (DEFAULT_GUI_FONT), FALSE);
		format.cbSize = sizeof(format);
		format.dwMask = CFM_BOLD | CFM_COLOR | CFM_FACE | CFM_SIZE;
		format.dwEffects = 0;
		format.yHeight = 200;
		format.crTextColor = RGB(223,223,223);
		format.bPitchAndFamily = FF_SWISS | VARIABLE_PITCH;
		strcpy (format.szFaceName, "Bitstream Vera Sans");	// At least I have it. :p
		SendMessage (view, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&format);

		ConWindow = view;
		ReleaseDC (hWnd, hdc);

		view = CreateWindowEx (WS_EX_NOPARENTNOTIFY, "STATIC", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW, 0, 0, 0, 0, hWnd, NULL, inst, NULL);
		if (view == NULL)
		{
			return -1;
		}
		SetWindowLong (view, GWL_ID, IDC_STATIC_TITLE);
		GameTitleWindow = view;

		return 0;

	case WM_SIZE:
		if (wParam != SIZE_MAXHIDE && wParam != SIZE_MAXSHOW)
		{
			LayoutMainWindow (hWnd, ErrorPane);
		}
		return 0;

	case WM_DRAWITEM:
		// Draw title banner.
		if (wParam == IDC_STATIC_TITLE && DoomStartupInfo != NULL)
		{
			const PalEntry *c;

			// Draw the game title strip at the top of the window.
			drawitem = (LPDRAWITEMSTRUCT)lParam;

			// Draw the background.
			rect = drawitem->rcItem;
			rect.bottom -= 1;
			c = (const PalEntry *)&DoomStartupInfo->BkColor;
			hbr = CreateSolidBrush (RGB(c->r,c->g,c->b));
			FillRect (drawitem->hDC, &drawitem->rcItem, hbr);
			DeleteObject (hbr);

			// Calculate width of the title string.
			SetTextAlign (drawitem->hDC, TA_TOP);
			oldfont = SelectObject (drawitem->hDC, GameTitleFont != NULL ? GameTitleFont : (HFONT)GetStockObject (DEFAULT_GUI_FONT));
			titlelen = (int)strlen (DoomStartupInfo->Name);
			GetTextExtentPoint32 (drawitem->hDC, DoomStartupInfo->Name, titlelen, &size);

			// Draw the title.
			c = (const PalEntry *)&DoomStartupInfo->FgColor;
			SetTextColor (drawitem->hDC, RGB(c->r,c->g,c->b));
			SetBkMode (drawitem->hDC, TRANSPARENT);
			TextOut (drawitem->hDC, rect.left + (rect.right - rect.left - size.cx) / 2, 2, DoomStartupInfo->Name, titlelen);
			SelectObject (drawitem->hDC, oldfont);
			return TRUE;
		}
		// Draw startup screen
		else if (wParam == IDC_STATIC_STARTUP)
		{
			if (StartupScreen != NULL)
			{
				drawitem = (LPDRAWITEMSTRUCT)lParam;

				rect = drawitem->rcItem;
				// Windows expects DIBs to be bottom-up but ours is top-down,
				// so flip it vertically while drawing it.
				StretchDIBits (drawitem->hDC, rect.left, rect.bottom - 1, rect.right - rect.left, rect.top - rect.bottom,
					0, 0, StartupBitmap->bmiHeader.biWidth, StartupBitmap->bmiHeader.biHeight,
					ST_Util_BitsForBitmap(StartupBitmap), StartupBitmap, DIB_RGB_COLORS, SRCCOPY);

				// If the title banner is gone, then this is an ENDOOM screen, so draw a short prompt
				// where the command prompt would have been in DOS.
				if (GameTitleWindow == NULL)
				{
					static const char QuitText[] = "Press any key or click anywhere in the window to quit.";

					SetTextColor (drawitem->hDC, RGB(240,240,240));
					SetBkMode (drawitem->hDC, TRANSPARENT);
					oldfont = SelectObject (drawitem->hDC, (HFONT)GetStockObject (DEFAULT_GUI_FONT));
					TextOut (drawitem->hDC, 3, drawitem->rcItem.bottom - DefaultGUIFontHeight - 3, QuitText, countof(QuitText)-1);
					SelectObject (drawitem->hDC, oldfont);
				}
				return TRUE;
			}
		}
		// Draw stop icon.
		else if (wParam == IDC_ICONPIC)
		{
			HICON icon;
			POINTL char_pos;
			drawitem = (LPDRAWITEMSTRUCT)lParam;

			// This background color should match the edit control's.
			hbr = CreateSolidBrush (RGB(70,70,70));
			FillRect (drawitem->hDC, &drawitem->rcItem, hbr);
			DeleteObject (hbr);

			// Draw the icon aligned with the first line of error text.
			SendMessage (ConWindow, EM_POSFROMCHAR, (WPARAM)&char_pos, ErrorIconChar);
			icon = (HICON)LoadImage (0, IDI_ERROR, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
			DrawIcon (drawitem->hDC, 6, char_pos.y, icon);
			return TRUE;
		}
		return FALSE;

	case WM_COMMAND:
		if (ErrorIcon != NULL && (HWND)lParam == ConWindow && HIWORD(wParam) == EN_UPDATE)
		{
			// Be sure to redraw the error icon if the edit control changes.
			InvalidateRect (ErrorIcon, NULL, TRUE);
			return 0;
		}
		break;

	case WM_CLOSE:
		PostQuitMessage (0);
		break;

	case WM_DESTROY:
		if (GameTitleFont != NULL)
		{
			DeleteObject (GameTitleFont);
		}
		break;
	}
	return DefWindowProc (hWnd, msg, wParam, lParam);
}

//==========================================================================
//
// ErrorPaneProc
//
// DialogProc for the error pane.
//
//==========================================================================

INT_PTR CALLBACK ErrorPaneProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_INITDIALOG:
		// Appear in the main window.
		LayoutMainWindow (GetParent (hDlg), hDlg);
		return TRUE;

	case WM_COMMAND:
		// There is only one button, and it's "Ok" and makes us quit.
		if (HIWORD(wParam) == BN_CLICKED)
		{
			PostQuitMessage (0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

//==========================================================================
//
// I_SetWndProc
//
// Sets the main WndProc, hides all the child windows, and starts up
// in-game input.
//
//==========================================================================

void I_SetWndProc()
{
	if (GetWindowLongPtr (Window, GWLP_USERDATA) == 0)
	{
		SetWindowLongPtr (Window, GWLP_USERDATA, 1);
		SetWindowLongPtr (Window, GWLP_WNDPROC, (WLONG_PTR)WndProc);
		ShowWindow (ConWindow, SW_HIDE);
		ShowWindow (GameTitleWindow, SW_HIDE);
		I_InitInput (Window);
	}
}

//==========================================================================
//
// RestoreConView
//
// Returns the main window to its startup state.
//
//==========================================================================

void RestoreConView()
{
	// Make sure the window has a frame in case it was fullscreened.
	SetWindowLongPtr (Window, GWL_STYLE, WS_VISIBLE|WS_OVERLAPPEDWINDOW);
	if (GetWindowLong (Window, GWL_EXSTYLE) & WS_EX_TOPMOST)
	{
		SetWindowPos (Window, HWND_BOTTOM, 0, 0, 512, 384,
			SWP_DRAWFRAME | SWP_NOCOPYBITS | SWP_NOMOVE);
		SetWindowPos (Window, HWND_TOP, 0, 0, 0, 0, SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOSIZE);
	}
	else
	{
		SetWindowPos (Window, NULL, 0, 0, 512, 384,
			SWP_DRAWFRAME | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOZORDER);
	}

	SetWindowLongPtr (Window, GWLP_WNDPROC, (WLONG_PTR)LConProc);
	ShowWindow (ConWindow, SW_SHOW);
	ShowWindow (GameTitleWindow, SW_SHOW);
	I_ShutdownInput ();		// Make sure the mouse pointer is available.
	// Make sure the progress bar isn't visible.
	if (StartScreen != NULL)
	{
		delete StartScreen;
		StartScreen = NULL;
	}
}

//==========================================================================
//
// ShowErrorPane
//
// Shows an error message, preferably in the main window, but it can
// use a normal message box too.
//
//==========================================================================

void ShowErrorPane(const char *text)
{
	if (Window == NULL || ConWindow == NULL)
	{
		if (text != NULL)
		{
			MessageBox (Window, text,
				GAMESIG " Fatal Error", MB_OK|MB_ICONSTOP|MB_TASKMODAL);
		}
		return;
	}

	if (StartScreen != NULL)	// Ensure that the network pane is hidden.
	{
		StartScreen->NetDone();
	}
	if (text != NULL)
	{
		SetWindowText (Window, "Fatal Error - " WINDOW_TITLE);
		ErrorIcon = CreateWindowEx (WS_EX_NOPARENTNOTIFY, "STATIC", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW, 0, 0, 0, 0, Window, NULL, g_hInst, NULL);
		if (ErrorIcon != NULL)
		{
			SetWindowLong (ErrorIcon, GWL_ID, IDC_ICONPIC);
		}
	}
	ErrorPane = CreateDialogParam (g_hInst, MAKEINTRESOURCE(IDD_ERRORPANE), Window, ErrorPaneProc, (LONG_PTR)NULL);

	if (text != NULL)
	{
		CHARRANGE end;
		CHARFORMAT2 oldformat, newformat;
		PARAFORMAT2 paraformat;

		// Append the error message to the log.
		end.cpMax = end.cpMin = GetWindowTextLength (ConWindow);
		SendMessage (ConWindow, EM_EXSETSEL, 0, (LPARAM)&end);

		// Remember current charformat.
		oldformat.cbSize = sizeof(oldformat);
		SendMessage (ConWindow, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&oldformat);

		// Use bigger font and standout colors.
		newformat.cbSize = sizeof(newformat);
		newformat.dwMask = CFM_BOLD | CFM_COLOR | CFM_SIZE;
		newformat.dwEffects = CFE_BOLD;
		newformat.yHeight = oldformat.yHeight * 5 / 4;
		newformat.crTextColor = RGB(255,170,170);
		SendMessage (ConWindow, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&newformat);

		// Indent the rest of the text to make the error message stand out a little more.
		paraformat.cbSize = sizeof(paraformat);
		paraformat.dwMask = PFM_STARTINDENT | PFM_OFFSETINDENT | PFM_RIGHTINDENT;
		paraformat.dxStartIndent = paraformat.dxOffset = paraformat.dxRightIndent = 120;
		SendMessage (ConWindow, EM_SETPARAFORMAT, 0, (LPARAM)&paraformat);
		SendMessage (ConWindow, EM_REPLACESEL, FALSE, (LPARAM)"\n");

		// Find out where the error lines start for the error icon display control.
		SendMessage (ConWindow, EM_EXGETSEL, 0, (LPARAM)&end);
		ErrorIconChar = end.cpMax;

		// Now start adding the actual error message.
		SendMessage (ConWindow, EM_REPLACESEL, FALSE, (LPARAM)"Execution could not continue.\n\n");

		// Restore old charformat but with light yellow text.
		oldformat.crTextColor = RGB(255,255,170);
		SendMessage (ConWindow, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&oldformat);

		// Add the error text.
		SendMessage (ConWindow, EM_REPLACESEL, FALSE, (LPARAM)text);

		// Make sure the error text is not scrolled below the window.
		SendMessage (ConWindow, EM_LINESCROLL, 0, SendMessage (ConWindow, EM_GETLINECOUNT, 0, 0));
		// The above line scrolled everything off the screen, so pretend to move the scroll
		// bar thumb, which clamps to not show any extra lines if it doesn't need to.
		SendMessage (ConWindow, EM_SCROLL, SB_PAGEDOWN, 0);
	}

	BOOL bRet;
	MSG msg;

	while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0)
	{
		if (bRet == -1)
		{
			MessageBox (Window, text,
				GAMESIG " Fatal Error", MB_OK|MB_ICONSTOP|MB_TASKMODAL);
			return;
		}
		else if (!IsDialogMessage (ErrorPane, &msg))
		{
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
	}
}

//==========================================================================
//
// DoMain
//
//==========================================================================

void DoMain (HINSTANCE hInstance)
{
	LONG WinWidth, WinHeight;
	int height, width, x, y;
	RECT cRect;
	TIMECAPS tc;
	DEVMODE displaysettings;

	try
	{
#ifdef _MSC_VER
		_set_new_handler (NewFailure);
#endif

		Args = new DArgs(__argc, __argv);
		atterm(FinalGC);

		// Under XP, get our session ID so we can know when the user changes/locks sessions.
		// Since we need to remain binary compatible with older versions of Windows, we
		// need to extract the ProcessIdToSessionId function from kernel32.dll manually.
		HMODULE kernel = GetModuleHandle ("kernel32.dll");

		if (Args->CheckParm("-stdout"))
		{
			// As a GUI application, we don't normally get a console when we start.
			// If we were run from the shell and are on XP+, we can attach to its
			// console. Otherwise, we can create a new one. If we already have a
			// stdout handle, then we have been redirected and should just use that
			// handle instead of creating a console window.

			StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
			if (StdOut != NULL)
			{
				// It seems that running from a shell always creates a std output
				// for us, even if it doesn't go anywhere. (Running from Explorer
				// does not.) If we can get file information for this handle, it's
				// a file or pipe, so use it. Otherwise, pretend it wasn't there
				// and find a console to use instead.
				BY_HANDLE_FILE_INFORMATION info;
				if (!GetFileInformationByHandle(StdOut, &info))
				{
					StdOut = NULL;
				}
			}
			if (StdOut == NULL)
			{
				// AttachConsole was introduced with Windows XP. (OTOH, since we
				// have to share the console with the shell, I'm not sure if it's
				// a good idea to actually attach to it.)
				typedef BOOL (WINAPI *ac)(DWORD);
				ac attach_console = kernel != NULL ? (ac)GetProcAddress(kernel, "AttachConsole") : NULL;
				if (attach_console != NULL && attach_console(ATTACH_PARENT_PROCESS))
				{
					StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
					DWORD foo; WriteFile(StdOut, "\n", 1, &foo, NULL);
					AttachedStdOut = true;
				}
				if (StdOut == NULL && AllocConsole())
				{
					StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
				}
				FancyStdOut = true;
			}
		}

		// Set the timer to be as accurate as possible
		if (timeGetDevCaps (&tc, sizeof(tc)) != TIMERR_NOERROR)
			TimerPeriod = 1;	// Assume minimum resolution of 1 ms
		else
			TimerPeriod = tc.wPeriodMin;

		timeBeginPeriod (TimerPeriod);

		/*
		killough 1/98:

		This fixes some problems with exit handling
		during abnormal situations.

		The old code called I_Quit() to end program,
		while now I_Quit() is installed as an exit
		handler and exit() is called to exit, either
		normally or abnormally.
		*/

		atexit (call_terms);

		atterm (I_Quit);

		// Figure out what directory the program resides in.
		char *program;

#ifdef _MSC_VER
		if (_get_pgmptr(&program) != 0)
		{
			I_FatalError("Could not determine program location.");
		}
#else
		char progbuff[1024];
		GetModuleFileName(0, progbuff, sizeof(progbuff));
		progbuff[1023] = '\0';
		program = progbuff;
#endif

		progdir = program;
		program = progdir.LockBuffer();
		*(strrchr(program, '\\') + 1) = '\0';
		FixPathSeperator(program);
		progdir.Truncate((long)strlen(program));
		progdir.UnlockBuffer();

		// [BC] When hosting, spawn a console dialog box instead of creating a window.
		if ( Args->CheckParm( "-host" ))
		{
			// This never returns.
			DialogBox( g_hInst, MAKEINTRESOURCE( IDD_SERVERDIALOG ), NULL/*(HWND)Window*/, SERVERCONSOLE_ServerDialogBoxCallback );
		}
		else
		{
/*
			height = GetSystemMetrics (SM_CYFIXEDFRAME) * 2 +
					GetSystemMetrics (SM_CYCAPTION) + 12 * 32;
			width  = GetSystemMetrics (SM_CXFIXEDFRAME) * 2 + 8 * 78;
*/
			width = 512;
			height = 384;

			// Many Windows structures that specify their size do so with the first
			// element. DEVMODE is not one of those structures.
			memset (&displaysettings, 0, sizeof(displaysettings));
			displaysettings.dmSize = sizeof(displaysettings);
			EnumDisplaySettings (NULL, ENUM_CURRENT_SETTINGS, &displaysettings);
			x = (displaysettings.dmPelsWidth - width) / 2;
			y = (displaysettings.dmPelsHeight - height) / 2;
			if (Args->CheckParm ("-0"))
			{
				x = y = 0;
			}

			TheInvisibleCursor = LoadCursor (hInstance, MAKEINTRESOURCE(IDC_INVISIBLECURSOR));
			TheArrowCursor = LoadCursor (NULL, IDC_ARROW);

			WNDCLASS WndClass;
			WndClass.style			= 0;
			WndClass.lpfnWndProc	= LConProc;
			WndClass.cbClsExtra		= 0;
			WndClass.cbWndExtra		= 0;
			WndClass.hInstance		= hInstance;
			WndClass.hIcon			= LoadIcon (hInstance, MAKEINTRESOURCE(IDI_ICONST));
			WndClass.hCursor		= TheArrowCursor;
			WndClass.hbrBackground	= NULL;
			WndClass.lpszMenuName	= NULL;
			WndClass.lpszClassName	= (LPCTSTR)WinClassName;
		
			/* register this new class with Windows */
			if (!RegisterClass((LPWNDCLASS)&WndClass))
				I_FatalError ("Could not register window class");
		
			/* create window */
			Window = CreateWindowEx(
					WS_EX_APPWINDOW,
					(LPCTSTR)WinClassName,
					(LPCTSTR)WINDOW_TITLE,
					WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
					x, y, width, height,
					(HWND)   NULL,
					(HMENU)  NULL,
							hInstance,
					NULL);

			if (!Window)
				I_FatalError ("Could not open window");

			if (kernel != NULL)
			{
				typedef BOOL (WINAPI *pts)(DWORD, DWORD *);
				pts pidsid = (pts)GetProcAddress (kernel, "ProcessIdToSessionId");
				if (pidsid != 0)
				{
					if (!pidsid (GetCurrentProcessId(), &SessionID))
					{
						SessionID = 0;
					}
					hwtsapi32 = LoadLibraryA ("wtsapi32.dll");
					if (hwtsapi32 != 0)
					{
						FARPROC reg = GetProcAddress (hwtsapi32, "WTSRegisterSessionNotification");
						if (reg == 0 || !((BOOL(WINAPI *)(HWND, DWORD))reg) (Window, NOTIFY_FOR_THIS_SESSION))
						{
							FreeLibrary (hwtsapi32);
							hwtsapi32 = 0;
						}
						else
						{
							atterm (UnWTS);
						}
					}
				}
			}

			GetClientRect (Window, &cRect);

			WinWidth = cRect.right;
			WinHeight = cRect.bottom;
		}

		CoInitialize (NULL);
		atterm (UnCOM);

		C_InitConsole (((WinWidth / 8) + 2) * 8, (WinHeight / 12) * 8, false);

		I_DetectOS ();
		D_DoomMain ();
	}
	catch (class CNoIWADError &/*error*/) // [RC] No IWADs were found! Show a setup dialog.
	{
		ShowWindow( Window, SW_HIDE );
		I_ShutdownGraphics( );
		I_ShowNoIWADsScreen( );
		exit( 0 );
	}
	catch (class CNoRunExit &)
	{
		I_ShutdownGraphics();
		if (FancyStdOut && !AttachedStdOut)
		{ // Outputting to a new console window: Wait for a keypress before quitting.
			DWORD bytes;
			HANDLE stdinput = GetStdHandle(STD_INPUT_HANDLE);

			ShowWindow (Window, SW_HIDE);
			WriteFile(StdOut, "Press any key to exit...", 24, &bytes, NULL);
			FlushConsoleInputBuffer(stdinput);
			SetConsoleMode(stdinput, 0);
			ReadConsole(stdinput, &bytes, 1, &bytes, NULL);
		}
		else if (StdOut == NULL)
		{
			ShowErrorPane(NULL);
		}
		exit(0);
	}
	catch (class CDoomError &error)
	{
		I_ShutdownGraphics ();
		RestoreConView ();
		if (error.GetMessage ())
		{
			ShowErrorPane (error.GetMessage());
		}
		exit (-1);
	}
}

//==========================================================================
//
// DoomSpecificInfo
//
// Called by the crash logger to get application-specific information.
//
//==========================================================================

void DoomSpecificInfo (char *buffer, size_t bufflen)
{
	const char *arg;
	char *const buffend = buffer + bufflen - 2;	// -2 for CRLF at end
	int i;

	buffer += mysnprintf (buffer, buffend - buffer, GAMENAME" version " DOTVERSIONSTR_REV " (" __DATE__ ")\r\n");
	buffer += mysnprintf (buffer, buffend - buffer, "\r\nCommand line: %s\r\n", GetCommandLine());

	for (i = 0; (arg = Wads.GetWadName (i)) != NULL; ++i)
	{
		buffer += mysnprintf (buffer, buffend - buffer, "\r\nWad %d: %s", i, arg);
	}

	if (gamestate != GS_LEVEL && gamestate != GS_TITLELEVEL)
	{
		buffer += mysnprintf (buffer, buffend - buffer, "\r\n\r\nNot in a level.");
	}
	else
	{
		char name[9];

		strncpy (name, level.mapname, 8);
		name[8] = 0;
		buffer += mysnprintf (buffer, buffend - buffer, "\r\n\r\nCurrent map: %s", name);

		// [BC] Also display the network state.
		char	szNetState[16];
		switch ( NETWORK_GetState( ))
		{
		case NETSTATE_SINGLE:

			sprintf( szNetState, "SINGLE" );
			break;
		case NETSTATE_SINGLE_MULTIPLAYER:

			sprintf( szNetState, "FAKE MULTI" );
			break;
		case NETSTATE_CLIENT:

			sprintf( szNetState, "CLIENT" );
			break;
		case NETSTATE_SERVER:

			sprintf( szNetState, "SERVER" );
			break;
		default:

			sprintf( szNetState, "UNKNOWN" );
			break;
		}
		buffer += wsprintf (buffer, "\r\n\r\nNetwork state: %s", szNetState);

		if (!viewactive)
		{
			buffer += mysnprintf (buffer, buffend - buffer, "\r\n\r\nView not active.");
		}
		else
		{
			buffer += mysnprintf (buffer, buffend - buffer, "\r\n\r\nviewx = %d", viewx);
			buffer += mysnprintf (buffer, buffend - buffer, "\r\nviewy = %d", viewy);
			buffer += mysnprintf (buffer, buffend - buffer, "\r\nviewz = %d", viewz);
			buffer += mysnprintf (buffer, buffend - buffer, "\r\nviewangle = %x", viewangle);
		}
	}
	*buffer++ = '\r';
	*buffer++ = '\n';
	*buffer++ = '\0';
}

// Here is how the error logging system works.
//
// To catch exceptions that occur in secondary threads, CatchAllExceptions is
// set as the UnhandledExceptionFilter for this process. It records the state
// of the thread at the time of the crash using CreateCrashLog and then queues
// an APC on the primary thread. When the APC executes, it raises a software
// exception that gets caught by the __try/__except block in WinMain.
// I_GetEvent calls SleepEx to put the primary thread in a waitable state
// periodically so that the APC has a chance to execute.
//
// Exceptions on the primary thread are caught by the __try/__except block in
// WinMain. Not only does it record the crash information, it also shuts
// everything down and displays a dialog with the information present. If a
// console log is being produced, the information will also be appended to it.
//
// If a debugger is running, CatchAllExceptions never executes, so secondary
// thread exceptions will always be caught by the debugger. For the primary
// thread, IsDebuggerPresent is called to determine if a debugger is present.
// Note that this function is not present on Windows 95, so we cannot
// statically link to it.
//
// To make this work with MinGW, you will need to use inline assembly
// because GCC offers no native support for Windows' SEH.

//==========================================================================
//
// SleepForever
//
//==========================================================================

void SleepForever ()
{
	Sleep (INFINITE);
}

//==========================================================================
//
// ExitMessedUp
//
// An exception occurred while exiting, so don't do any standard processing.
// Just die.
//
//==========================================================================

LONG WINAPI ExitMessedUp (LPEXCEPTION_POINTERS foo)
{
	ExitProcess (1000);
}

//==========================================================================
//
// ExitFatally
//
//==========================================================================

void CALLBACK ExitFatally (ULONG_PTR dummy)
{
	// [BB] If we are the server, try to kick all players before we shut down.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		SERVER_KickAllPlayers( "The server encountered a fatal error and needs to shut down!" );
		// [BB] It's crucial that we send the packets informing the clients now.
		SERVER_SendOutPackets();
	}

	SetUnhandledExceptionFilter (ExitMessedUp);
	I_ShutdownGraphics ();
	RestoreConView ();
	DisplayCrashLog ();
	exit (-1);
}

//==========================================================================
//
// CatchAllExceptions
//
//==========================================================================

LONG WINAPI CatchAllExceptions (LPEXCEPTION_POINTERS info)
{
#ifdef _DEBUG
	if (info->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT)
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}
#endif

	static bool caughtsomething = false;

	if (caughtsomething) return EXCEPTION_EXECUTE_HANDLER;
	caughtsomething = true;

	char *custominfo = (char *)HeapAlloc (GetProcessHeap(), 0, 16384);

	CrashPointers = *info;
	DoomSpecificInfo (custominfo, 16384);
	CreateCrashLog (custominfo, (DWORD)strlen(custominfo));

	// If the main thread crashed, then make it clean up after itself.
	// Otherwise, put the crashing thread to sleep and signal the main thread to clean up.
	if (GetCurrentThreadId() == MainThreadID)
	{
#ifndef _M_X64
		info->ContextRecord->Eip = (DWORD_PTR)ExitFatally;
#else
		info->ContextRecord->Rip = (DWORD_PTR)ExitFatally;
#endif
	}
	else
	{
#ifndef _M_X64
		info->ContextRecord->Eip = (DWORD_PTR)SleepForever;
#else
		info->ContextRecord->Rip = (DWORD_PTR)SleepForever;
#endif
		QueueUserAPC (ExitFatally, MainThread, 0);
	}
	return EXCEPTION_CONTINUE_EXECUTION;
}

//==========================================================================
//
// infiniterecursion
//
// Debugging routine for testing the crash logger.
//
//==========================================================================

#ifdef _DEBUG
static void infiniterecursion(int foo)
{
	if (foo)
	{
		infiniterecursion(foo);
	}
}
#endif

//==========================================================================
//
// WinMain
//
//==========================================================================

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE nothing, LPSTR cmdline, int nCmdShow)
{
	g_hInst = hInstance;

	InitCommonControls ();			// Load some needed controls and be pretty under XP

	if (NULL == LoadLibrary ("riched20.dll"))
	{
		// Technically, it isn't really Internet Explorer that is needed, but this
		// is an example of a specific program that will provide riched20.dll.
		// But considering how much extra stuff needs to be installed to make Windows 95
		// useable with pretty much any recent software, the chances are high that
		// the user already has riched20.dll installed.
		I_FatalError ("Sorry, you need to install Internet Explorer 3 or higher to play " GAMENAME " on Windows 95.");
	}

#if !defined(__GNUC__) && defined(_DEBUG)
	if (__argc == 2 && strcmp (__argv[1], "TestCrash") == 0)
	{
		__try
		{
			*(int *)0 = 0;
		}
		__except(CrashPointers = *GetExceptionInformation(),
			CreateCrashLog (__argv[1], 9), EXCEPTION_EXECUTE_HANDLER)
		{
		}
		DisplayCrashLog ();
		exit (0);
	}
	if (__argc == 2 && strcmp (__argv[1], "TestStackCrash") == 0)
	{
		__try
		{
			infiniterecursion(1);
		}
		__except(CrashPointers = *GetExceptionInformation(),
			CreateCrashLog (__argv[1], 14), EXCEPTION_EXECUTE_HANDLER)
		{
		}
		DisplayCrashLog ();
		exit (0);
	}
#endif

	MainThread = INVALID_HANDLE_VALUE;
	DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &MainThread,
		0, FALSE, DUPLICATE_SAME_ACCESS);
	MainThreadID = GetCurrentThreadId();

#ifndef _DEBUG
	if (MainThread != INVALID_HANDLE_VALUE)
	{
		SetUnhandledExceptionFilter (CatchAllExceptions);
	}
#endif

#if defined(_DEBUG) && defined(_MSC_VER)
	// Uncomment this line to make the Visual C++ CRT check the heap before
	// every allocation and deallocation. This will be slow, but it can be a
	// great help in finding problem areas.
	//_CrtSetDbgFlag (_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF);

	// Enable leak checking at exit.
	_CrtSetDbgFlag (_CrtSetDbgFlag(0) | _CRTDBG_LEAK_CHECK_DF);

	// Use this to break at a specific allocation number.
	//_crtBreakAlloc = 5501;
#endif

	DoMain (hInstance);

	CloseHandle (MainThread);
	MainThread = INVALID_HANDLE_VALUE;
	return 0;
}

//==========================================================================
//
//	[BC] MainDoomThread
//
//	This is the new thread created by the server console.
//
//==========================================================================
DWORD WINAPI MainDoomThread( LPVOID )
{
	// Setup our main thread ID so that if we have a crash, it crashes properly.
	MainThread = INVALID_HANDLE_VALUE;
	DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &MainThread,
		0, FALSE, DUPLICATE_SAME_ACCESS);
	MainThreadID = GetCurrentThreadId();

	try
	{
		D_DoomMain( );
	}
	catch (class CNoIWADError &/*error*/) // [RC] No IWADs were found! Show a setup dialog.
	{
		SERVERCONSOLE_Hide( );
		I_ShowNoIWADsScreen( );
		exit( 0 );
	}
	catch (class CDoomError &error)
	{
//		I_ShutdownGraphics ();
//		RestoreConView ();
		if (error.GetMessage ())
		{
			ShowErrorPane (error.GetMessage());
		}
		exit (-1);
	}

	return ( 0 );
}

//==========================================================================
//
// CCMD crashout
//
// Debugging routine for testing the crash logger.
// Useless in a debug build, because that doesn't enable the crash logger.
//
//==========================================================================

#ifndef _DEBUG
#include "c_dispatch.h"
CCMD (crashout)
{
	// [BB] This function may not be used by ConsoleCommand.
	if ( ACS_IsCalledFromConsoleCommand( ))
		return;

	*(int *)0 = 0;
}
#endif
