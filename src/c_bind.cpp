/*
** c_bind.cpp
** Functions for using and maintaining key bindings
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
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

#include "doomtype.h"
#include "doomdef.h"
#include "c_dispatch.h"
#include "c_bind.h"
#include "g_level.h"
#include "hu_stuff.h"
#include "configfile.h"
#include "d_event.h"
#include "w_wad.h"
#include "templates.h"
#include "dobject.h"
#include "vm.h"
#include "i_time.h"
#include "menu/menu.h"
#include "v_text.h"

const char *KeyNames[NUM_KEYS] =
{
	// This array is dependant on the particular keyboard input
	// codes generated in i_input.c. If they change there, they
	// also need to change here. In this case, we use the
	// DirectInput codes and assume a qwerty keyboard layout.
	// See <dinput.h> for the DIK_* codes

	NULL,		"Escape",	"1",		"2",		"3",		"4",		"5",		"6",		//00
	"7",		"8",		"9",		"0",		"-",		"=",		"Backspace","Tab",		//08
	"Q",		"W",		"E",		"R",		"T",		"Y",		"U",		"I",		//10
	"O",		"P",		"[",		"]",		"Enter",	"Ctrl",		"A",		"S",		//18
	"D",		"F",		"G",		"H",		"J",		"K",		"L",		";",		//20
	"'",		"`",		"Shift",	"\\",		"Z",		"X",		"C",		"V",		//28
	"B",		"N",		"M",		",",		".",		"/",		"RShift",	"KP*",		//30
	"Alt",		"Space",	"CapsLock",	"F1",		"F2",		"F3",		"F4",		"F5",		//38
	"F6",		"F7",		"F8",		"F9",		"F10",		"NumLock",	"Scroll",	"KP7",		//40
	"KP8",		"KP9",		"KP-",		"KP4",		"KP5",		"KP6",		"KP+",		"KP1",		//48
	"KP2",		"KP3",		"KP0",		"KP.",		NULL,		NULL,		"OEM102",	"F11",		//50
	"F12",		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		//58
	NULL,		NULL,		NULL,		NULL,		"F13",		"F14",		"F15",		"F16",		//60
	NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		//68
	"Kana",		NULL,		NULL,		"Abnt_C1",	NULL,		NULL,		NULL,		NULL,		//70
	NULL,		"Convert",	NULL,		"NoConvert",NULL,		"Yen",		"Abnt_C2",	NULL,		//78
	NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		//80
	NULL,		NULL,		NULL,		NULL,		NULL,		"KP=",		NULL,		NULL,		//88
	"Circumflex","@",		":",		"_",		"Kanji",	"Stop",		"Ax",		"Unlabeled",//90
	NULL,		"PrevTrack",NULL,		NULL,		"KP-Enter",	"RCtrl",	NULL,		NULL,		//98
	"Mute",		"Calculator","Play",	NULL,		"Stop",		NULL,		NULL,		NULL,		//A0
	NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		"VolDown",	NULL,		//A8
	"VolUp",	NULL,		"WebHome",	"KP,",		NULL,		"KP/",		NULL,		"SysRq",	//B0
	"RAlt",		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		//B8
	NULL,		NULL,		NULL,		NULL,		NULL,		"Pause",	NULL,		"Home",		//C0
	"UpArrow",	"PgUp",		NULL,		"LeftArrow",NULL,		"RightArrow",NULL,		"End",		//C8
	"DownArrow","PgDn",		"Ins",		"Del",		NULL,		NULL,		NULL,		NULL,		//D0
#ifdef __APPLE__
	NULL,		NULL,		NULL,		"Command",	NULL,		"Apps",		"Power",	"Sleep",	//D8
#else // !__APPLE__
	NULL,		NULL,		NULL,		"LWin",		"RWin",		"Apps",		"Power",	"Sleep",	//D8
#endif // __APPLE__
	NULL,		NULL,		NULL,		"Wake",		NULL,		"Search",	"Favorites","Refresh",	//E0
	"WebStop",	"WebForward","WebBack",	"MyComputer","Mail",	"MediaSelect",NULL,		NULL,		//E8
	NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		//F0
	NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		NULL,		//F8

	// non-keyboard buttons that can be bound
	"Mouse1",	"Mouse2",	"Mouse3",	"Mouse4",		// 8 mouse buttons
	"Mouse5",	"Mouse6",	"Mouse7",	"Mouse8",

	"Joy1",		"Joy2",		"Joy3",		"Joy4",			// 128 joystick buttons!
	"Joy5",		"Joy6",		"Joy7",		"Joy8",
	"Joy9",		"Joy10",	"Joy11",	"Joy12",
	"Joy13",	"Joy14",	"Joy15",	"Joy16",
	"Joy17",	"Joy18",	"Joy19",	"Joy20",
	"Joy21",	"Joy22",	"Joy23",	"Joy24",
	"Joy25",	"Joy26",	"Joy27",	"Joy28",
	"Joy29",	"Joy30",	"Joy31",	"Joy32",
	"Joy33",	"Joy34",	"Joy35",	"Joy36",
	"Joy37",	"Joy38",	"Joy39",	"Joy40",
	"Joy41",	"Joy42",	"Joy43",	"Joy44",
	"Joy45",	"Joy46",	"Joy47",	"Joy48",
	"Joy49",	"Joy50",	"Joy51",	"Joy52",
	"Joy53",	"Joy54",	"Joy55",	"Joy56",
	"Joy57",	"Joy58",	"Joy59",	"Joy60",
	"Joy61",	"Joy62",	"Joy63",	"Joy64",
	"Joy65",	"Joy66",	"Joy67",	"Joy68",
	"Joy69",	"Joy70",	"Joy71",	"Joy72",
	"Joy73",	"Joy74",	"Joy75",	"Joy76",
	"Joy77",	"Joy78",	"Joy79",	"Joy80",
	"Joy81",	"Joy82",	"Joy83",	"Joy84",
	"Joy85",	"Joy86",	"Joy87",	"Joy88",
	"Joy89",	"Joy90",	"Joy91",	"Joy92",
	"Joy93",	"Joy94",	"Joy95",	"Joy96",
	"Joy97",	"Joy98",	"Joy99",	"Joy100",
	"Joy101",	"Joy102",	"Joy103",	"Joy104",
	"Joy105",	"Joy106",	"Joy107",	"Joy108",
	"Joy109",	"Joy110",	"Joy111",	"Joy112",
	"Joy113",	"Joy114",	"Joy115",	"Joy116",
	"Joy117",	"Joy118",	"Joy119",	"Joy120",
	"Joy121",	"Joy122",	"Joy123",	"Joy124",
	"Joy125",	"Joy126",	"Joy127",	"Joy128",

	"POV1Up",	"POV1Right","POV1Down",	"POV1Left",		// First POV hat
	"POV2Up",	"POV2Right","POV2Down",	"POV2Left",		// Second POV hat
	"POV3Up",	"POV3Right","POV3Down",	"POV3Left",		// Third POV hat
	"POV4Up",	"POV4Right","POV4Down",	"POV4Left",		// Fourth POV hat

	"MWheelUp",	"MWheelDown",							// the mouse wheel
	"MWheelRight", "MWheelLeft",

	"Axis1Plus","Axis1Minus","Axis2Plus","Axis2Minus",	// joystick axes as buttons
	"Axis3Plus","Axis3Minus","Axis4Plus","Axis4Minus",
	"Axis5Plus","Axis5Minus","Axis6Plus","Axis6Minus",
	"Axis7Plus","Axis7Minus","Axis8Plus","Axis8Minus",

	"LStickRight","LStickLeft","LStickDown","LStickUp",			// Gamepad axis-based buttons
	"RStickRight","RStickLeft","RStickDown","RStickUp",

	"DPadUp","DPadDown","DPadLeft","DPadRight",	// Gamepad buttons
	"Pad_Start","Pad_Back","LThumb","RThumb",
	"LShoulder","RShoulder","LTrigger","RTrigger",
	"Pad_A", "Pad_B", "Pad_X", "Pad_Y"
};

FKeyBindings Bindings;
FKeyBindings DoubleBindings;
FKeyBindings AutomapBindings;

DEFINE_GLOBAL(Bindings)
DEFINE_GLOBAL(AutomapBindings)

static unsigned int DClickTime[NUM_KEYS];
static uint8_t DClicked[(NUM_KEYS+7)/8];

//=============================================================================
//
//
//
//=============================================================================

static int GetKeyFromName (const char *name)
{
	int i;

	// Names of the form #xxx are translated to key xxx automatically
	if (name[0] == '#' && name[1] != 0)
	{
		return atoi (name + 1);
	}

	// Otherwise, we scan the KeyNames[] array for a matching name
	for (i = 0; i < NUM_KEYS; i++)
	{
		if (KeyNames[i] && !stricmp (KeyNames[i], name))
			return i;
	}
	return 0;
}

//=============================================================================
//
//
//
//=============================================================================

static int GetConfigKeyFromName (const char *key)
{
	int keynum = GetKeyFromName(key);
	if (keynum == 0)
	{
		if (stricmp (key, "LeftBracket") == 0)
		{
			keynum = GetKeyFromName ("[");
		}
		else if (stricmp (key, "RightBracket") == 0)
		{
			keynum = GetKeyFromName ("]");
		}
		else if (stricmp (key, "Equals") == 0)
		{
			keynum = GetKeyFromName ("=");
		}
		else if (stricmp (key, "KP-Equals") == 0)
		{
			keynum = GetKeyFromName ("kp=");
		}
	}
	return keynum;
}

//=============================================================================
//
//
//
//=============================================================================

static const char *KeyName (int key)
{
	static char name[5];

	if (KeyNames[key])
		return KeyNames[key];

	mysnprintf (name, countof(name), "Key_%d", key);
	return name;
}

//=============================================================================
//
//
//
//=============================================================================

static const char *ConfigKeyName(int keynum)
{
	const char *name = KeyName(keynum);
	if (name[1] == 0)	// Make sure given name is config-safe
	{
		if (name[0] == '[')
			return "LeftBracket";
		else if (name[0] == ']')
			return "RightBracket";
		else if (name[0] == '=')
			return "Equals";
		else if (strcmp (name, "kp=") == 0)
			return "KP-Equals";
	}
	return name;
}

//=============================================================================
//
//
//
//=============================================================================

DEFINE_ACTION_FUNCTION(FKeyBindings, SetBind)
{
	PARAM_SELF_STRUCT_PROLOGUE(FKeyBindings);
	PARAM_INT(k);
	PARAM_STRING(cmd);

	// Only menus are allowed to change bindings.
	if (DMenu::InMenu == 0)
	{
		I_FatalError("Attempt to change key bindings outside of menu code to '%s'", cmd.GetChars());
	}


	self->SetBind(k, cmd);
	return 0;
}

//=============================================================================
//
//
//
//=============================================================================

void C_NameKeys (char *str, int first, int second)
{
	int c = 0;

	*str = 0;
	if (second == first) second = 0;
	if (first)
	{
		c++;
		strcpy (str, KeyName (first));
		if (second)
			strcat (str, TEXTCOLOR_BLACK ", " TEXTCOLOR_NORMAL);
	}

	if (second)
	{
		c++;
		strcat (str, KeyName (second));
	}

	if (!c)
		*str = '\0';
}

DEFINE_ACTION_FUNCTION(FKeyBindings, NameKeys)
{
	PARAM_PROLOGUE;
	PARAM_INT(k1);
	PARAM_INT(k2);
	char buffer[120];
	C_NameKeys(buffer, k1, k2);
	ACTION_RETURN_STRING(buffer);
}

//=============================================================================
//
//
//
//=============================================================================

void FKeyBindings::DoBind (const char *key, const char *bind)
{
	int keynum = GetConfigKeyFromName (key);
	if (keynum != 0)
	{
		Binds[keynum] = bind;
	}
}

//=============================================================================
//
//
//
//=============================================================================

void FKeyBindings::UnbindAll ()
{
	for (int i = 0; i < NUM_KEYS; ++i)
	{
		Binds[i] = "";
	}
}

//=============================================================================
//
//
//
//=============================================================================

void FKeyBindings::UnbindKey(const char *key)
{
	int i;

	if ( (i = GetKeyFromName (key)) )
	{
		Binds[i] = "";
	}
	else
	{
		Printf ("Unknown key \"%s\"\n", key);
		return;
	}
}

//=============================================================================
//
//
//
//=============================================================================

void FKeyBindings::PerformBind(FCommandLine &argv, const char *msg)
{
	int i;

	if (argv.argc() > 1)
	{
		i = GetKeyFromName (argv[1]);
		if (!i)
		{
			Printf ("Unknown key \"%s\"\n", argv[1]);
			return;
		}
		if (argv.argc() == 2)
		{
			Printf ("\"%s\" = \"%s\"\n", argv[1], Binds[i].GetChars());
		}
		else
		{
			Binds[i] = argv[2];
		}
	}
	else
	{
		Printf ("%s:\n", msg);
		
		for (i = 0; i < NUM_KEYS; i++)
		{
			if (!Binds[i].IsEmpty())
				Printf ("%s \"%s\"\n", KeyName (i), Binds[i].GetChars());
		}
	}
}


//=============================================================================
//
// This function is first called for functions in custom key sections.
// In this case, matchcmd is non-NULL, and only keys bound to that command
// are stored. If a match is found, its binding is set to "\1".
// After all custom key sections are saved, it is called one more for the
// normal Bindings and DoubleBindings sections for this game. In this case
// matchcmd is NULL and all keys will be stored. The config section was not
// previously cleared, so all old bindings are still in place. If the binding
// for a key is empty, the corresponding key in the config is removed as well.
// If a binding is "\1", then the binding itself is cleared, but nothing
// happens to the entry in the config.
//
//=============================================================================

void FKeyBindings::ArchiveBindings(FConfigFile *f, const char *matchcmd)
{
	int i;

	for (i = 0; i < NUM_KEYS; i++)
	{
		if (Binds[i].IsEmpty())
		{
			if (matchcmd == NULL)
			{
				f->ClearKey(ConfigKeyName(i));
			}
		}
		else if (matchcmd == NULL || stricmp(Binds[i], matchcmd) == 0)
		{
			if (Binds[i][0] == '\1')
			{
				Binds[i] = "";
				continue;
			}
			f->SetValueForKey(ConfigKeyName(i), Binds[i]);
			if (matchcmd != NULL)
			{ // If saving a specific command, set a marker so that
			  // it does not get saved in the general binding list.
				Binds[i] = "\1";
			}
		}
	}
}

//=============================================================================
//
//
//
//=============================================================================

int FKeyBindings::GetKeysForCommand (const char *cmd, int *first, int *second)
{
	int c, i;

	*first = *second = c = i = 0;

	if (cmd[0] == '\0')
	{
		return 0;
	}

	while (i < NUM_KEYS && c < 2)
	{
		if (stricmp (cmd, Binds[i]) == 0)
		{
			if (c++ == 0)
				*first = i;
			else
				*second = i;
		}
		i++;
	}
	return c;
}

DEFINE_ACTION_FUNCTION(FKeyBindings, GetKeysForCommand)
{
	PARAM_SELF_STRUCT_PROLOGUE(FKeyBindings);
	PARAM_STRING(cmd);
	int k1, k2;
	self->GetKeysForCommand(cmd.GetChars(), &k1, &k2);
	if (numret > 0) ret[0].SetInt(k1);
	if (numret > 1) ret[1].SetInt(k2);
	return MIN(numret, 2);
}

//=============================================================================
//
//
//
//=============================================================================

void FKeyBindings::UnbindACommand (const char *str)
{
	int i;

	for (i = 0; i < NUM_KEYS; i++)
	{
		if (!stricmp (str, Binds[i]))
		{
			Binds[i] = "";
		}
	}
}

DEFINE_ACTION_FUNCTION(FKeyBindings, UnbindACommand)
{
	PARAM_SELF_STRUCT_PROLOGUE(FKeyBindings);
	PARAM_STRING(cmd);

	// Only menus are allowed to change bindings.
	if (DMenu::InMenu == 0)
	{
		I_FatalError("Attempt to unbind key bindings for '%s' outside of menu code", cmd.GetChars());
	}

	self->UnbindACommand(cmd);
	return 0;
}

//=============================================================================
//
//
//
//=============================================================================

void FKeyBindings::DefaultBind(const char *keyname, const char *cmd)
{
	int key = GetKeyFromName (keyname);
	if (key == 0)
	{
		Printf ("Unknown key \"%s\"\n", keyname);
		return;
	}
	if (!Binds[key].IsEmpty())
	{ // This key is already bound.
		return;
	}
	for (int i = 0; i < NUM_KEYS; ++i)
	{
		if (!Binds[i].IsEmpty() && stricmp (Binds[i], cmd) == 0)
		{ // This command is already bound to a key.
			return;
		}
	}
	// It is safe to do the bind, so do it.
	Binds[key] = cmd;
}

//=============================================================================
//
//
//
//=============================================================================

void C_UnbindAll ()
{
	Bindings.UnbindAll();
	DoubleBindings.UnbindAll();
	AutomapBindings.UnbindAll();
}

UNSAFE_CCMD (unbindall)
{
	C_UnbindAll ();
}

//=============================================================================
//
//
//
//=============================================================================

CCMD (unbind)
{
	if (argv.argc() > 1)
	{
		Bindings.UnbindKey(argv[1]);
	}
}

CCMD (undoublebind)
{
	if (argv.argc() > 1)
	{
		DoubleBindings.UnbindKey(argv[1]);
	}
}

CCMD (unmapbind)
{
	if (argv.argc() > 1)
	{
		AutomapBindings.UnbindKey(argv[1]);
	}
}

//=============================================================================
//
//
//
//=============================================================================

CCMD (bind)
{
	Bindings.PerformBind(argv, "Current key bindings");
}

CCMD (doublebind)
{
	DoubleBindings.PerformBind(argv, "Current key doublebindings");
}

CCMD (mapbind)
{
	AutomapBindings.PerformBind(argv, "Current automap key bindings");
}

//==========================================================================
//
// CCMD defaultbind
//
// Binds a command to a key if that key is not already bound and if
// that command is not already bound to another key.
//
//==========================================================================

CCMD (defaultbind)
{
	if (argv.argc() < 3)
	{
		Printf ("Usage: defaultbind <key> <command>\n");
	}
	else
	{
		Bindings.DefaultBind(argv[1], argv[2]);
	}
}

//=============================================================================
//
//
//
//=============================================================================

CCMD (rebind)
{
	FKeyBindings *bindings;

	if (key == 0)
	{
		Printf ("Rebind cannot be used from the console\n");
		return;
	}

	if (key & KEY_DBLCLICKED)
	{
		bindings = &DoubleBindings;
		key &= KEY_DBLCLICKED-1;
	}
	else
	{
		bindings = &Bindings;
	}

	if (argv.argc() > 1)
	{
		bindings->SetBind(key, argv[1]);
	}
}

//=============================================================================
//
//
//
//=============================================================================

void C_BindDefaults ()
{
	int lump, lastlump = 0;

	while ((lump = Wads.FindLump("DEFBINDS", &lastlump)) != -1)
	{
		FScanner sc(lump);

		while (sc.GetString())
		{
			FKeyBindings *dest = &Bindings;
			int key;

			// bind destination is optional and is the same as the console command
			if (sc.Compare("bind"))
			{
				sc.MustGetString();
			}
			else if (sc.Compare("doublebind"))
			{
				dest = &DoubleBindings;
				sc.MustGetString();
			}
			else if (sc.Compare("mapbind"))
			{
				dest = &AutomapBindings;
				sc.MustGetString();
			}
			key = GetConfigKeyFromName(sc.String);
			sc.MustGetString();
			dest->SetBind(key, sc.String);
		}
	}
}

CCMD(binddefaults)
{
	C_BindDefaults ();
}

void C_SetDefaultBindings ()
{
	C_UnbindAll ();
	C_BindDefaults ();
}

//=============================================================================
//
//
//
//=============================================================================

bool C_DoKey (event_t *ev, FKeyBindings *binds, FKeyBindings *doublebinds)
{
	FString binding;
	bool dclick;
	int dclickspot;
	uint8_t dclickmask;
	unsigned int nowtime;

	if (ev->type != EV_KeyDown && ev->type != EV_KeyUp)
		return false;

	if ((unsigned int)ev->data1 >= NUM_KEYS)
		return false;

	dclickspot = ev->data1 >> 3;
	dclickmask = 1 << (ev->data1 & 7);
	dclick = false;

	// This used level.time which didn't work outside a level.
	nowtime = (unsigned)I_msTime();
	if (doublebinds != NULL && int(DClickTime[ev->data1] - nowtime) > 0 && ev->type == EV_KeyDown)
	{
		// Key pressed for a double click
		binding = doublebinds->GetBinding(ev->data1);
		DClicked[dclickspot] |= dclickmask;
		dclick = true;
	}
	else
	{
		if (ev->type == EV_KeyDown)
		{ // Key pressed for a normal press
			binding = binds->GetBinding(ev->data1);
			DClickTime[ev->data1] = nowtime + 571;
		}
		else if (doublebinds != NULL && DClicked[dclickspot] & dclickmask)
		{ // Key released from a double click
			binding = doublebinds->GetBinding(ev->data1);
			DClicked[dclickspot] &= ~dclickmask;
			DClickTime[ev->data1] = 0;
			dclick = true;
		}
		else
		{ // Key released from a normal press
			binding = binds->GetBinding(ev->data1);
		}
	}


	if (binding.IsEmpty())
	{
		binding = binds->GetBinding(ev->data1);
		dclick = false;
	}

	if (!binding.IsEmpty() && (chatmodeon == 0 || ev->data1 < 256))
	{
		if (ev->type == EV_KeyUp && binding[0] != '+')
		{
			return false;
		}

		char *copy = binding.LockBuffer();

		if (ev->type == EV_KeyUp)
		{
			copy[0] = '-';
		}

		AddCommandString (copy, dclick ? ev->data1 | KEY_DBLCLICKED : ev->data1);
		return true;
	}
	return false;
}

