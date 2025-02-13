#include "i_interface.h"
#include "st_start.h"
#include "gamestate.h"
#include "startupinfo.h"
#include "c_cvars.h"
#include "gstrings.h"
#include "v_font.h"

EXTERN_CVAR(Int, vid_scalemode)
EXTERN_CVAR(Float, vid_scalefactor)
EXTERN_CVAR(Bool, ui_generic)
EXTERN_CVAR(String, language)

// Some global engine variables taken out of the backend code.
FStartupScreen* StartWindow;
SystemCallbacks sysCallbacks;
FString endoomName;
bool batchrun;
float menuBlurAmount;

bool AppActive = true;
int chatmodeon;
gamestate_t 	gamestate = GS_STARTUP;
bool ToggleFullscreen;
int 			paused;
bool			pauseext;

FStartupInfo GameStartupInfo;

CVAR(Bool, queryiwad, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR(String, defaultiwad, "", CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR(Bool, vid_fps, false, 0)

void DisableGenericUI(bool cvar)
{
	if (generic_ui && cvar) generic_ui = false;
	if (cvar)
	{
		CurrentConsoleFont = ConFont;
	}
	else
	{
		CurrentConsoleFont = NewConsoleFont;
	}
}

CUSTOM_CVAR(Bool, ui_classic, true, CVAR_ARCHIVE | CVAR_NOINITCALL)
{
	if (ui_generic && self)
		ui_generic = false;
	DisableGenericUI(self);
	language = "auto";
	vid_scalemode.Callback();
	vid_scalefactor.Callback();
}

CUSTOM_CVAR(String, language, "auto", CVAR_ARCHIVE | CVAR_NOINITCALL | CVAR_GLOBALCONFIG)
{
	FString str = *self;
	bool invalid = ui_classic && (!str.Compare("cs")  || !str.Compare("de") || !str.Compare("eo") || !str.Compare("fi") || !str.Compare("jp")
		|| !str.Compare("ko") || !str.Compare("nl") || !str.Compare("pl") || !str.Compare("ro") || !str.Compare("ru"));
	if (invalid && str.Compare("auto")) self = "auto";
	if (ui_classic && !str.Compare("sr")) self = "pt";
	GStrings.UpdateLanguage(self);
	UpdateGenericUI(ui_generic);
	if (ui_classic)
		DisableGenericUI(true);
	if (sysCallbacks.LanguageChanged) sysCallbacks.LanguageChanged(self);
}

