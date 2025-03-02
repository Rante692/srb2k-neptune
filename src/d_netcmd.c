// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  d_netcmd.c
/// \brief host/client network commands
///        commands are executed through the command buffer
///	       like console commands, other miscellaneous commands (at the end)

#include "doomdef.h"

#include "console.h"
#include "command.h"
#include "doomstat.h"
#include "i_time.h"
#include "i_system.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "g_input.h"
#include "m_menu.h"
#include "r_local.h"
#include "r_things.h"
#include "p_local.h"
#include "p_setup.h"
#include "s_sound.h"
#include "i_sound.h"
#include "m_misc.h"
#include "am_map.h"
#include "byteptr.h"
#include "d_netfil.h"
#include "p_spec.h"
#include "m_cheat.h"
#include "d_clisrv.h"
#include "d_main.h"
#include "m_random.h"
#include "f_finale.h"
#include "filesrch.h"
#include "mserv.h"
#include "md5.h"
#include "z_zone.h"
#include "lua_script.h"
#include "lua_hook.h"
#include "m_cond.h"
#include "m_anigif.h"
#include "k_kart.h" // SRB2kart
#include "y_inter.h"
#include "fastcmp.h"
#include "m_perfstats.h"

#ifdef NETGAME_DEVMODE
#define CV_RESTRICT CV_NETVAR
#else
#define CV_RESTRICT 0
#endif

#ifdef HAVE_DISCORDRPC
#include "discord.h"
#endif

// ------
// protos
// ------

static void Got_NameAndColor(UINT8 **cp, INT32 playernum);
static void Got_WeaponPref(UINT8 **cp, INT32 playernum);
static void Got_Mapcmd(UINT8 **cp, INT32 playernum);
static void Got_ExitLevelcmd(UINT8 **cp, INT32 playernum);
static void Got_SetupVotecmd(UINT8 **cp, INT32 playernum);
static void Got_ModifyVotecmd(UINT8 **cp, INT32 playernum);
static void Got_PickVotecmd(UINT8 **cp, INT32 playernum);
static void Got_RequestAddfilecmd(UINT8 **cp, INT32 playernum);
static void Got_Addfilecmd(UINT8 **cp, INT32 playernum);
static void Got_Pause(UINT8 **cp, INT32 playernum);
static void Got_Respawn(UINT8 **cp, INT32 playernum);
static void Got_RandomSeed(UINT8 **cp, INT32 playernum);
static void Got_RunSOCcmd(UINT8 **cp, INT32 playernum);
static void Got_Teamchange(UINT8 **cp, INT32 playernum);
static void Got_Clearscores(UINT8 **cp, INT32 playernum);
static void Got_DiscordInfo(UINT8 **cp, INT32 playernum);

static void PointLimit_OnChange(void);
static void TimeLimit_OnChange(void);
static void NumLaps_OnChange(void);
static void Mute_OnChange(void);

static void Hidetime_OnChange(void);

static void AutoBalance_OnChange(void);
static void TeamScramble_OnChange(void);

static void NetTimeout_OnChange(void);
static void JoinTimeout_OnChange(void);

static void Ringslinger_OnChange(void);
static void Gravity_OnChange(void);
static void ForceSkin_OnChange(void);

static void Name_OnChange(void);
static void Name2_OnChange(void);
static void Name3_OnChange(void);
static void Name4_OnChange(void);
static void Skin_OnChange(void);
static void Skin2_OnChange(void);
static void Skin3_OnChange(void);
static void Skin4_OnChange(void);
static void Follower_OnChange(void);
static void Follower2_OnChange(void);
static void Follower3_OnChange(void);
static void Follower4_OnChange(void);
static void Followercolor_OnChange(void);
static void Followercolor2_OnChange(void);
static void Followercolor3_OnChange(void);
static void Followercolor4_OnChange(void);
static void Color_OnChange(void);
static void Color2_OnChange(void);
static void Color3_OnChange(void);
static void Color4_OnChange(void);
static void DummyConsvar_OnChange(void);
static void SoundTest_OnChange(void);

static void BaseNumLaps_OnChange(void);
static void KartFrantic_OnChange(void);
static void KartSpeed_OnChange(void);
static void KartBattleSpeed_OnChange(void);
static void KartEncore_OnChange(void);
static void KartComeback_OnChange(void);
static void KartEliminateLast_OnChange(void);

#ifdef NETGAME_DEVMODE
static void Fishcake_OnChange(void);
#endif

static void Command_Playdemo_f(void);
static void Command_Timedemo_f(void);
static void Command_Stopdemo_f(void);
static void Command_StartMovie_f(void);
static void Command_StopMovie_f(void);
static void Command_Map_f(void);
static void Command_ResetCamera_f(void);

static void Command_View_f (void);
static void Command_SetViews_f(void);

static void Command_Addfilelocal(void);
static void Command_Addfile(void);
static void Command_Addskins(void);
static void Command_GLocalSkin(void);
static void Command_ListWADS_f(void);
static void Command_RunSOC(void);
static void Command_Pause(void);
static void Command_Respawn(void);

static void Command_ReplayMarker(void);

static void Command_Version_f(void);
#ifdef UPDATE_ALERT
static void Command_ModDetails_f(void);
#endif
static void Command_ShowGametype_f(void);
FUNCNORETURN static ATTRNORETURN void Command_Quit_f(void);
static void Command_Playintro_f(void);

static void Command_Displayplayer_f(void);

static void Command_ExitLevel_f(void);
static void Command_Showmap_f(void);
static void Command_Mapmd5_f(void);

static void Command_Teamchange_f(void);
static void Command_Teamchange2_f(void);
static void Command_Teamchange3_f(void);
static void Command_Teamchange4_f(void);

static void Command_ServerTeamChange_f(void);

static void Command_Clearscores_f(void);

// Remote Administration
static void Command_Changepassword_f(void);
static void Command_Login_f(void);
static void Got_Login(UINT8 **cp, INT32 playernum);
static void Got_Verification(UINT8 **cp, INT32 playernum);
static void Got_Removal(UINT8 **cp, INT32 playernum);
static void Command_Verify_f(void);
static void Command_RemoveAdmin_f(void);
static void Command_MotD_f(void);
static void Got_MotD_f(UINT8 **cp, INT32 playernum);

static void Command_ShowScores_f(void);
static void Command_ShowTime_f(void);

static void Command_Isgamemodified_f(void);
static void Command_Cheats_f(void);

static void Command_ListSkins(void);
static void Command_SkinSearch(void);

static void Command_FollowerSearch(void);


#ifdef _DEBUG
static void Command_Togglemodified_f(void);
static void Command_Archivetest_f(void);
#endif

// =========================================================================
//                           CLIENT VARIABLES
// =========================================================================

void SendWeaponPref(void);
void SendWeaponPref2(void);
void SendWeaponPref3(void);
void SendWeaponPref4(void);

static CV_PossibleValue_t usemouse_cons_t[] = {{0, "Off"}, {1, "On"}, {2, "Force"}, {0, NULL}};
#if defined (__unix__) || defined (__APPLE__) || defined (UNIXCOMMON)
static CV_PossibleValue_t mouse2port_cons_t[] = {{0, "/dev/gpmdata"}, {1, "/dev/ttyS0"},
	{2, "/dev/ttyS1"}, {3, "/dev/ttyS2"}, {4, "/dev/ttyS3"}, {0, NULL}};
#else
static CV_PossibleValue_t mouse2port_cons_t[] = {{1, "COM1"}, {2, "COM2"}, {3, "COM3"}, {4, "COM4"},
	{0, NULL}};
#endif

#ifdef LJOYSTICK
static CV_PossibleValue_t joyport_cons_t[] = {{1, "/dev/js0"}, {2, "/dev/js1"}, {3, "/dev/js2"},
	{4, "/dev/js3"}, {0, NULL}};
#else
// accept whatever value - it is in fact the joystick device number
#define usejoystick_cons_t NULL
#endif

static CV_PossibleValue_t autobalance_cons_t[] = {{0, "MIN"}, {4, "MAX"}, {0, NULL}};
static CV_PossibleValue_t teamscramble_cons_t[] = {{0, "Off"}, {1, "Random"}, {2, "Points"}, {0, NULL}};

static CV_PossibleValue_t startingliveslimit_cons_t[] = {{1, "MIN"}, {99, "MAX"}, {0, NULL}};
static CV_PossibleValue_t sleeping_cons_t[] = {{0, "MIN"}, {1000/TICRATE, "MAX"}, {0, NULL}};
static CV_PossibleValue_t competitionboxes_cons_t[] = {{0, "Normal"}, {1, "Random"}, {2, "Teleports"},
	{3, "None"}, {0, NULL}};

static CV_PossibleValue_t matchboxes_cons_t[] = {{0, "Normal"}, {1, "Random"}, {2, "Non-Random"},
	{3, "None"}, {0, NULL}};

//static CV_PossibleValue_t chances_cons_t[] = {{0, "MIN"}, {9, "MAX"}, {0, NULL}};
static CV_PossibleValue_t match_scoring_cons_t[] = {{0, "Normal"}, {1, "Classic"}, {0, NULL}};
static CV_PossibleValue_t pause_cons_t[] = {{0, "Server"}, {1, "All"}, {0, NULL}};

static CV_PossibleValue_t timetic_cons_t[] = {{0, "Normal"}, {1, "Tics"}, {2, "Centiseconds"}, {0, NULL}};

#ifdef NETGAME_DEVMODE
static consvar_t cv_fishcake = {"fishcake", "Off", CV_CALL|CV_NOSHOWHELP|CV_RESTRICT, CV_OnOff, Fishcake_OnChange, 0, NULL, NULL, 0, 0, NULL};
#endif
static consvar_t cv_dummyconsvar = {"dummyconsvar", "Off", CV_CALL|CV_NOSHOWHELP, CV_OnOff,
	DummyConsvar_OnChange, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_restrictskinchange = {"restrictskinchange", "No", CV_NETVAR|CV_CHEAT, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_allowteamchange = {"allowteamchange", "Yes", CV_NETVAR, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t ingamecap_cons_t[] = {{0, "MIN"}, {MAXPLAYERS-1, "MAX"}, {0, NULL}};
consvar_t cv_ingamecap = {"ingamecap", "0", CV_NETVAR, ingamecap_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t spectatorreentry_cons_t[] = {{0, "MIN"}, {10*60, "MAX"}, {0, NULL}};
consvar_t cv_spectatorreentry = {"spectatorreentry", "30", CV_NETVAR, spectatorreentry_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t antigrief_cons_t[] = {{20, "MIN"}, {180, "MAX"}, {0, "Off"}, {0, NULL}};
consvar_t cv_antigrief = {"antigrief", "30", CV_NETVAR, antigrief_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_startinglives = {"startinglives", "3", CV_NETVAR|CV_CHEAT|CV_NOSHOWHELP, startingliveslimit_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t respawntime_cons_t[] = {{0, "MIN"}, {30, "MAX"}, {0, NULL}};
consvar_t cv_respawntime = {"respawndelay", "1", CV_NETVAR|CV_CHEAT, respawntime_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_competitionboxes = {"competitionboxes", "Random", CV_NETVAR|CV_CHEAT|CV_NOSHOWHELP, competitionboxes_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

#ifdef SEENAMES
static CV_PossibleValue_t seenames_cons_t[] = {{0, "Off"}, {1, "Colorless"}, {2, "Team"}, {3, "Ally/Foe"}, {0, NULL}};
consvar_t cv_seenames = {"seenames", "Off", CV_SAVE, seenames_cons_t, 0, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_allowseenames = {"allowseenames", "No", CV_NETVAR, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};
#endif

// these are just meant to be saved to the config
consvar_t cv_playername = {"name", "Sonic", CV_SAVE|CV_CALL|CV_NOINIT, NULL, Name_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_playername2 = {"name2", "Tails", CV_SAVE|CV_CALL|CV_NOINIT, NULL, Name2_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_playername3 = {"name3", "Knuckles", CV_SAVE|CV_CALL|CV_NOINIT, NULL, Name3_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_playername4 = {"name4", "Dr. Eggman", CV_SAVE|CV_CALL|CV_NOINIT, NULL, Name4_OnChange, 0, NULL, NULL, 0, 0, NULL};
// player colors
consvar_t cv_playercolor = {"color", "Blue", CV_SAVE|CV_CALL|CV_NOINIT, Color_cons_t, Color_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_playercolor2 = {"color2", "Orange", CV_SAVE|CV_CALL|CV_NOINIT, Color_cons_t, Color2_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_playercolor3 = {"color3", "Red", CV_SAVE|CV_CALL|CV_NOINIT, Color_cons_t, Color3_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_playercolor4 = {"color4", "Red", CV_SAVE|CV_CALL|CV_NOINIT, Color_cons_t, Color4_OnChange, 0, NULL, NULL, 0, 0, NULL};
// player's skin, saved for commodity, when using a favorite skins wad..
consvar_t cv_skin = {"skin", DEFAULTSKIN, CV_SAVE|CV_CALL|CV_NOINIT, NULL, Skin_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_skin2 = {"skin2", DEFAULTSKIN2, CV_SAVE|CV_CALL|CV_NOINIT, NULL, Skin2_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_skin3 = {"skin3", DEFAULTSKIN3, CV_SAVE|CV_CALL|CV_NOINIT, NULL, Skin3_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_skin4 = {"skin4", DEFAULTSKIN4, CV_SAVE|CV_CALL|CV_NOINIT, NULL, Skin4_OnChange, 0, NULL, NULL, 0, 0, NULL};
// player's followers. Also saved.
consvar_t cv_follower = {"follower", "None", CV_SAVE|CV_CALL|CV_NOINIT, NULL, Follower_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_follower2 = {"follower2", "None", CV_SAVE|CV_CALL|CV_NOINIT, NULL, Follower2_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_follower3 = {"follower3", "None", CV_SAVE|CV_CALL|CV_NOINIT, NULL, Follower3_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_follower4 = {"follower4", "None", CV_SAVE|CV_CALL|CV_NOINIT, NULL, Follower4_OnChange, 0, NULL, NULL, 0, 0, NULL};

// player's follower color. Also saved...
consvar_t cv_followercolor = {"followercolor", "Match", CV_SAVE|CV_CALL|CV_NOINIT, Followercolor_cons_t, Followercolor_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_followercolor2 = {"followercolor2", "Match", CV_SAVE|CV_CALL|CV_NOINIT, Followercolor_cons_t, Followercolor2_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_followercolor3 = {"followercolor3", "Match", CV_SAVE|CV_CALL|CV_NOINIT, Followercolor_cons_t, Followercolor3_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_followercolor4 = {"followercolor4", "Match", CV_SAVE|CV_CALL|CV_NOINIT, Followercolor_cons_t, Followercolor4_OnChange, 0, NULL, NULL, 0, 0, NULL};

// haha I've beaten you now, ONLINE
consvar_t cv_localskin = {"internal___localskin", "none", CV_HIDEN, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_showlocalskinmenus = {"showlocalskinmenus", "Yes", CV_SAVE, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_skipmapcheck = {"skipmapcheck", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

INT32 cv_debug;

consvar_t cv_usemouse = {"use_mouse", "Off", CV_SAVE|CV_CALL,usemouse_cons_t, I_StartupMouse, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_usemouse2 = {"use_mouse2", "Off", CV_SAVE|CV_CALL,usemouse_cons_t, I_StartupMouse2, 0, NULL, NULL, 0, 0, NULL};
//WTF
consvar_t cv_mouseturn = {"mouseturn", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_spectatestrafe = {"spectatestrafe", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};



// Lagless camera! Yay!
consvar_t cv_laglesscam = {"laglesscamera", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

#if defined(HAVE_SDL) || defined(_WINDOWS) //joystick 1 and 2
consvar_t cv_usejoystick = {"use_joystick", "1", CV_SAVE|CV_CALL, usejoystick_cons_t,
	I_InitJoystick, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_usejoystick2 = {"use_joystick2", "2", CV_SAVE|CV_CALL, usejoystick_cons_t,
	I_InitJoystick2, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_usejoystick3 = {"use_joystick3", "3", CV_SAVE|CV_CALL, usejoystick_cons_t,
	I_InitJoystick3, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_usejoystick4 = {"use_joystick4", "4", CV_SAVE|CV_CALL, usejoystick_cons_t,
	I_InitJoystick4, 0, NULL, NULL, 0, 0, NULL};
#endif

#if (defined (LJOYSTICK) || defined (HAVE_SDL))
#ifdef LJOYSTICK
consvar_t cv_joyport = {"joyport", "/dev/js0", CV_SAVE, joyport_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_joyport2 = {"joyport2", "/dev/js0", CV_SAVE, joyport_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL}; //Alam: for later
#endif
consvar_t cv_joyscale = {"joyscale", "1", CV_SAVE|CV_CALL, NULL, I_JoyScale, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_joyscale2 = {"joyscale2", "1", CV_SAVE|CV_CALL, NULL, I_JoyScale2, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_joyscale3 = {"joyscale3", "1", CV_SAVE|CV_CALL, NULL, I_JoyScale3, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_joyscale4 = {"joyscale4", "1", CV_SAVE|CV_CALL, NULL, I_JoyScale4, 0, NULL, NULL, 0, 0, NULL};
#else
consvar_t cv_joyscale = {"joyscale", "1", CV_SAVE|CV_HIDEN, NULL, NULL, 0, NULL, NULL, 0, 0, NULL}; //Alam: Dummy for save
consvar_t cv_joyscale2 = {"joyscale2", "1", CV_SAVE|CV_HIDEN, NULL, NULL, 0, NULL, NULL, 0, 0, NULL}; //Alam: Dummy for save
#endif
#if defined (__unix__) || defined (__APPLE__) || defined (UNIXCOMMON)
consvar_t cv_mouse2port = {"mouse2port", "/dev/gpmdata", CV_SAVE, mouse2port_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_mouse2opt = {"mouse2opt", "0", CV_SAVE, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};
#else
consvar_t cv_mouse2port = {"mouse2port", "COM2", CV_SAVE, mouse2port_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
#endif

consvar_t cv_matchboxes = {"matchboxes", "Normal", CV_NETVAR|CV_CHEAT|CV_NOSHOWHELP, matchboxes_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_specialrings = {"specialrings", "On", CV_NETVAR|CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_powerstones = {"powerstones", "On", CV_NETVAR|CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

/*consvar_t cv_recycler =      {"tv_recycler",      "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_teleporters =   {"tv_teleporter",    "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_superring =     {"tv_superring",     "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_supersneakers = {"tv_supersneaker",  "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_invincibility = {"tv_invincibility", "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_jumpshield =    {"tv_jumpshield",    "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_watershield =   {"tv_watershield",   "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ringshield =    {"tv_ringshield",    "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_forceshield =   {"tv_forceshield",   "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_bombshield =    {"tv_bombshield",    "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_1up =           {"tv_1up",           "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_eggmanbox =     {"tv_eggman",        "5", CV_NETVAR|CV_CHEAT, chances_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};*/

// SRB2kart
consvar_t cv_sneaker = 				{"sneaker", 			"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_rocketsneaker = 		{"rocketsneaker", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_invincibility = 		{"invincibility", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_banana = 				{"banana", 				"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_eggmanmonitor = 		{"eggmanmonitor", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_orbinaut = 			{"orbinaut", 			"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_jawz = 				{"jawz", 				"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_mine = 				{"mine", 				"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ballhog = 				{"ballhog", 			"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_selfpropelledbomb =	{"selfpropelledbomb", 	"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_grow = 				{"grow", 				"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_shrink = 				{"shrink", 				"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_thundershield = 		{"thundershield", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_bubbleshield = 		{"bubbleshield", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_flameshield = 			{"flameshield", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_hyudoro = 				{"hyudoro", 			"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_pogospring = 			{"pogospring", 			"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kitchensink = 			{"kitchensink", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_triplesneaker = 		{"triplesneaker", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_triplebanana = 		{"triplebanana", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_decabanana = 			{"decabanana", 			"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_tripleorbinaut = 		{"tripleorbinaut", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_quadorbinaut = 		{"quadorbinaut", 		"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_dualjawz = 			{"dualjawz", 			"On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

// Sneaker Extender
static CV_PossibleValue_t extensiontype_cons_t[] = {{1, "bs"}, {2, "zbl"}, {0, NULL}};
consvar_t cv_sneakerextend = {"sneakerextend", "On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_sneakerextendtype = {"sneakerextendtype", "zbl", CV_NETVAR|CV_CHEAT, extensiontype_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

// I really need to add types to this chirst
static CV_PossibleValue_t chainoffroadtype_cons_t[] = {{0, "Off"}, {1, "Both"}, {2, "Sneaker Only"}, {3, "Panel Only"}, {0, NULL}};
consvar_t cv_chainoffroad = {"chainoffroad", "Both", CV_NETVAR|CV_CHEAT, chainoffroadtype_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

// Additiveminiturbos
consvar_t cv_additivemt  = 	{"additivemt", 	"Off", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

// Mini-turbo adjustment cvars
static CV_PossibleValue_t sparktics_cons_t[] = {{1, "MIN"}, {INT32_MAX, "MAX"}, {0, NULL}};
consvar_t cv_bluesparktics = {"bluesparktics", "20", CV_NETVAR|CV_CHEAT, sparktics_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_redsparktics = {"redsparktics", "50", CV_NETVAR|CV_CHEAT, sparktics_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_rainbowsparktics = {"rainbowsparktics", "125", CV_NETVAR|CV_CHEAT, sparktics_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

// Stacking
consvar_t cv_stacking = {"stacking", "On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_stackingdim = {"stacking_dim", "On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_stackingdimval = {"stacking_dimval", "1.25", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_stackingoldcompat = {"stacking_oldcompat", "Off", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_sneakerstack = {"stacking_sneakerstack", "5", CV_NETVAR|CV_CHEAT, CV_Natural, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_panel = {"stacking_paneltimer", "Off", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_panelsharestack = {"stacking_panelsharestack", "On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_panelstack = {"stacking_panelstack", "2", CV_NETVAR|CV_CHEAT, CV_Natural, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_stackingbrakemod = {"stackingbrakemod", "1.25", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_stackinglowspeedbuff = {"stacking_lowspeedbuff", "On", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

// Speed of boosts
consvar_t cv_sneakerspeedeasy = {"stacking_sneakerspeedeasy", "0.8317", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_sneakerspeednormal = {"stacking_sneakerspeednormal", "0.5", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_sneakerspeedhard = {"stacking_sneakerspeedhard", "0.2756", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_sneakerspeedexpert = {"stacking_sneakerspeedexpert", "0.2243", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_sneakeraccel = {"stacking_sneakeraccel", "8.0", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_invincibilityspeed = {"stacking_invincibilitypeed", "0.375", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_invincibilityaccel = {"stacking_invincibilityaccel", "3.0", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_flamespeed = {"stacking_flamespeedbase", "0.5", CV_NETVAR|CV_FLOAT|CV_CHEAT, speed_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_flameaccel = {"stacking_flameaccel", "2.0", CV_NETVAR|CV_FLOAT|CV_CHEAT, speed_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_growspeed = {"stacking_growspeed", "0.30", CV_NETVAR|CV_FLOAT|CV_CHEAT, speed_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_growaccel = {"stacking_growaccel", "0.5", CV_NETVAR|CV_FLOAT|CV_CHEAT, speed_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_growmult = {"stacking_growmult", "0.4", CV_NETVAR|CV_FLOAT|CV_CHEAT, mult_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_growspeed = {"stacking_growspeed", "0.3", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_growaccel = {"stacking_growaccel", "0.5", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_growmult = {"stacking_growmult", "-0.3", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Signed, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_driftspeed = {"stacking_drfitspeed", "0.25", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_driftaccel = {"stacking_drfitaccel", "4.0", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_startspeed = {"stacking_startspeed", "0.25", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_startaccel = {"stacking_startaccel", "6.0", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_hyuudorospeed = {"stacking_hyuudorospeed", "0.1", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_hyuudoroaccel = {"stacking_hyuudoroaccel", "0.5", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_speedcap = {"stacking_speedcap", "Off", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_speedcapval = {"stacking_speedcapval", "128", CV_NETVAR|CV_FLOAT|CV_CHEAT, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};

// Fuckal Odds`
static CV_PossibleValue_t itemoddstype_cons_t[] = {{1, "Uranus"}, {2, "CEP"}, {0, NULL}};
consvar_t cv_itemodds = {"itemoddsystem", "CEP", CV_NETVAR|CV_CHEAT, itemoddstype_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t itemtable_cons_t[] = {{0, "MIN"}, {100, "MAX"}, {0, NULL}};
// Item table customization 220 (Yes really)
consvar_t cv_customodds = {"customodds", "Off", CV_NETVAR|CV_CHEAT, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t distvar_cons_t[] = {{0, "MIN"}, {INT32_MAX, "MAX"}, {0, NULL}};
consvar_t cv_cepdistvar = {"CEPDISTVAR", "1280", CV_NETVAR|CV_CHEAT, distvar_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cepdisthalf = {"CEPDISTHALF", "640", CV_NETVAR|CV_CHEAT, distvar_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cepspbdistvar = {"CEPSPBDISTVAR", "1280", CV_NETVAR|CV_CHEAT, distvar_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_uranusdistvar = {"URANUSDISTVAR", "896", CV_NETVAR|CV_CHEAT, distvar_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Sneaker
consvar_t cv_SITBL1 = {"SITBL0", "20", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SITBL2 = {"SITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SITBL3 = {"SITBL2", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SITBL4 = {"SITBL3", "5", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SITBL5 = {"SITBL4", "6", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SITBL6 = {"SITBL5", "7", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SITBL7 = {"SITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SITBL8 = {"SITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SITBL9 = {"SITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SITBL10 = {"SITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Rocket Sneaker
consvar_t cv_RSITBL1 = {"RSITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_RSITBL2 = {"RSITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_RSITBL3 = {"RSITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_RSITBL4 = {"RSITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_RSITBL5 = {"RSITBL4", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_RSITBL6 = {"RSITBL5", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_RSITBL7 = {"RSITBL6", "6", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_RSITBL8 = {"RSITBL7", "7", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_RSITBL9 = {"RSITBL8", "3", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_RSITBL10 = {"RSITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Invincivility
consvar_t cv_INITBL1 = {"INITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_INITBL2 = {"INITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_INITBL3 = {"INITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_INITBL4 = {"INITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_INITBL5 = {"INITBL4", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_INITBL6 = {"INITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_INITBL7 = {"INITBL6", "3", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_INITBL8 = {"INITBL7", "5", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_INITBL9 = {"INITBL8", "10", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_INITBL10 = {"INITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Banana
consvar_t cv_BANITBL1 = {"BANITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BANITBL2 = {"BANITBL1", "9", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BANITBL3 = {"BANITBL2", "3", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BANITBL4 = {"BANITBL3", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BANITBL5 = {"BANITBL4", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BANITBL6 = {"BANITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BANITBL7 = {"BANITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BANITBL8 = {"BANITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BANITBL9 = {"BANITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BANITBL10 = {"BANITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Eggmanbomb
consvar_t cv_EGGITBL1 = {"EGGITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_EGGITBL2 = {"EGGITBL1", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_EGGITBL3 = {"EGGITBL2", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_EGGITBL4 = {"EGGITBL3", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_EGGITBL5 = {"EGGITBL4", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_EGGITBL6 = {"EGGITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_EGGITBL7 = {"EGGITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_EGGITBL8 = {"EGGITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_EGGITBL9 = {"EGGITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_EGGITBL10 = {"EGGITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Orbinaut
consvar_t cv_ORBITBL1 = {"ORBITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ORBITBL2 = {"ORBITBL1", "7", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ORBITBL3 = {"ORBITBL2", "7", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ORBITBL4 = {"ORBITBL3", "4", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ORBITBL5 = {"ORBITBL4", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ORBITBL6 = {"ORBITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ORBITBL7 = {"ORBITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ORBITBL8 = {"ORBITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ORBITBL9 = {"ORBITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_ORBITBL10 = {"ORBITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Jawz
consvar_t cv_JAWITBL1 = {"JAWITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_JAWITBL2 = {"JAWITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_JAWITBL3 = {"JAWITBL2", "3", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_JAWITBL4 = {"JAWITBL3", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_JAWITBL5 = {"JAWITBL4", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_JAWITBL6 = {"JAWITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_JAWITBL7 = {"JAWITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_JAWITBL8 = {"JAWITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_JAWITBL9 = {"JAWITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_JAWITBL10 = {"JAWITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//MINE
consvar_t cv_MINITBL1 = {"MINITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_MINITBL2 = {"MINITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_MINITBL3 = {"MINITBL2", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_MINITBL4 = {"MINITBL3", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_MINITBL5 = {"MINITBL4", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_MINITBL6 = {"MINITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_MINITBL7 = {"MINITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_MINITBL8 = {"MINITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_MINITBL9 = {"MINITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_MINITBL10 = {"MINITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Ball Hog
consvar_t cv_BALITBL1 = {"BALITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BALITBL2 = {"BALITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BALITBL3 = {"BALITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BALITBL4 = {"BALITBL3", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BALITBL5 = {"BALITBL4", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BALITBL6 = {"BALITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BALITBL7 = {"BALITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BALITBL8 = {"BALITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BALITBL9 = {"BALITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_BALITBL10 = {"BALITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//SPB
consvar_t cv_SPBITBL1 = {"SPBITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SPBITBL2 = {"SPBITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SPBITBL3 = {"SPBITBL2", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SPBITBL4 = {"SPBITBL3", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SPBITBL5 = {"SPBITBL4", "3", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SPBITBL6 = {"SPBITBL5", "4", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SPBITBL7 = {"SPBITBL6", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SPBITBL8 = {"SPBITBL7", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SPBITBL9 = {"SPBITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SPBITBL10 = {"SPBITBL9", "20", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Grow
consvar_t cv_GROITBL1 = {"GROITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_GROITBL2 = {"GROITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_GROITBL3 = {"GROITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_GROITBL4 = {"GROITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_GROITBL5 = {"GROITBL4", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_GROITBL6 = {"GROITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_GROITBL7 = {"GROITBL6", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_GROITBL8 = {"GROITBL7", "4", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_GROITBL9 = {"GROITBL8", "7", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_GROITBL10 = {"GROITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Shrink
consvar_t cv_SHRITBL1 = {"SHRITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SHRITBL2 = {"SHRITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SHRITBL3 = {"SHRITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SHRITBL4 = {"SHRITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SHRITBL5 = {"SHRITBL4", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SHRITBL6 = {"SHRITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SHRITBL7 = {"SHRITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SHRITBL8 = {"SHRITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SHRITBL9 = {"SHRITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_SHRITBL10 = {"SHRITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Thundershield
consvar_t cv_THUITBL1 = {"THUITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_THUITBL2 = {"THUITBL1", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_THUITBL3 = {"THUITBL2", "2", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_THUITBL4 = {"THUITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_THUITBL5 = {"THUITBL4", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_THUITBL6 = {"THUITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_THUITBL7 = {"THUITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_THUITBL8 = {"THUITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_THUITBL9 = {"THUITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_THUITBL10 = {"THUITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Hyuu
consvar_t cv_HYUITBL1 = {"HYUITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_HYUITBL2 = {"HYUITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_HYUITBL3 = {"HYUITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_HYUITBL4 = {"HYUITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_HYUITBL5 = {"HYUITBL4", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_HYUITBL6 = {"HYUITBL5", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_HYUITBL7 = {"HYUITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_HYUITBL8 = {"HYUITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_HYUITBL9 = {"HYUITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_HYUITBL10 = {"HYUITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

// Pogo spring
consvar_t cv_POGITBL1 = {"POGITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_POGITBL2 = {"POGITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_POGITBL3 = {"POGITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_POGITBL4 = {"POGITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_POGITBL5 = {"POGITBL4", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_POGITBL6 = {"POGITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_POGITBL7 = {"POGITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_POGITBL8 = {"POGITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_POGITBL9 = {"POGITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_POGITBL10 = {"POGITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Kitchen Sink
consvar_t cv_KITITBL1 = {"KITITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_KITITBL2 = {"KITITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_KITITBL3 = {"KITITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_KITITBL4 = {"KITITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_KITITBL5 = {"KITITBL4", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_KITITBL6 = {"KITITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_KITITBL7 = {"KITITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_KITITBL8 = {"KITITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_KITITBL9 = {"KITITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_KITITBL10 = {"KITITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Triple Sneaker
consvar_t cv_TSITBL1 = {"TSITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TSITBL2 = {"TSITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TSITBL3 = {"TSITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TSITBL4 = {"TSITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TSITBL5 = {"TSITBL4", "4", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TSITBL6 = {"TSITBL5", "9", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TSITBL7 = {"TSITBL6", "9", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TSITBL8 = {"TSITBL7", "3", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TSITBL9 = {"TSITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TSITBL10 = {"TSITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Triplebanana
consvar_t cv_TBAITBL1 = {"TBAITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TBAITBL2 = {"TBAITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TBAITBL3 = {"TBAITBL2", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TBAITBL4 = {"TBAITBL3", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TBAITBL5 = {"TBAITBL4", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TBAITBL6 = {"TBAITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TBAITBL7 = {"TBAITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TBAITBL8 = {"TBAITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TBAITBL9 = {"TBAITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TBAITBL10 = {"TBAITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//DecaBanana
consvar_t cv_TENITBL1 = {"TENITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TENITBL2 = {"TENITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TENITBL3 = {"TENITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TENITBL4 = {"TENITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TENITBL5 = {"TENITBL4", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TENITBL6 = {"TENITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TENITBL7 = {"TENITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TENITBL8 = {"TENITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TENITBL9 = {"TENITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TENITBL10 = {"TENITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Triple Orbi
consvar_t cv_TORITBL1 = {"TORITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TORITBL2 = {"TORITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TORITBL3 = {"TORITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TORITBL4 = {"TORITBL3", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TORITBL5 = {"TORITBL4", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TORITBL6 = {"TORITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TORITBL7 = {"TORITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TORITBL8 = {"TORITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TORITBL9 = {"TORITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_TORITBL10 = {"TORITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Quad orbi
consvar_t cv_QUOITBL1 = {"QUOITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_QUOITBL2 = {"QUOITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_QUOITBL3 = {"QUOITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_QUOITBL4 = {"QUOITBL3", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_QUOITBL5 = {"QUOITBL4", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_QUOITBL6 = {"QUOITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_QUOITBL7 = {"QUOITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_QUOITBL8 = {"QUOITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_QUOITBL9 = {"QUOITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_QUOITBL10 = {"QUOITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Dual Jawz
consvar_t cv_DJAITBL1 = {"DJAITBL0", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_DJAITBL2 = {"DJAITBL1", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_DJAITBL3 = {"DJAITBL2", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_DJAITBL4 = {"DJAITBL3", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_DJAITBL5 = {"DJAITBL4", "1", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_DJAITBL6 = {"DJAITBL5", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_DJAITBL7 = {"DJAITBL6", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_DJAITBL8 = {"DJAITBL7", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_DJAITBL9 = {"DJAITBL8", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_DJAITBL10 = {"DJAITBL9", "0", CV_NETVAR|CV_CHEAT, itemtable_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

//Save server ip
consvar_t cv_lastserver = {"lastserver", "", CV_SAVE, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};



static CV_PossibleValue_t kartminimap_cons_t[] = {{0, "MIN"}, {10, "MAX"}, {0, NULL}};
consvar_t cv_kartminimap = {"kartminimap", "4", CV_SAVE, kartminimap_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartcheck = {"kartcheck", "Yes", CV_SAVE, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t kartinvinsfx_cons_t[] = {{0, "Music"}, {1, "SFX"}, {0, NULL}};
consvar_t cv_kartinvinsfx = {"kartinvinsfx", "SFX", CV_SAVE, kartinvinsfx_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartspeed = {"kartspeed", "Normal", CV_NETVAR|CV_CALL|CV_NOINIT, kartspeed_cons_t, KartSpeed_OnChange, 0, NULL, NULL, 0, 0, NULL};
//BattleSpeed
consvar_t cv_kartbattlespeed = {"kartbattlespeed", "Normal", CV_NETVAR|CV_CALL|CV_NOINIT, kartspeed_cons_t, KartBattleSpeed_OnChange, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t kartbumpers_cons_t[] = {{1, "MIN"}, {12, "MAX"}, {0, NULL}};
consvar_t cv_kartbumpers = {"kartbumpers", "3", CV_NETVAR|CV_CHEAT, kartbumpers_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartfrantic = {"kartfrantic", "Off", CV_NETVAR|CV_CHEAT|CV_CALL|CV_NOINIT, CV_OnOff, KartFrantic_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartcomeback = {"kartcomeback", "On", CV_NETVAR|CV_CHEAT|CV_CALL|CV_NOINIT, CV_OnOff, KartComeback_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartencore = {"kartencore", "Off", CV_NETVAR|CV_CALL|CV_NOINIT, CV_OnOff, KartEncore_OnChange, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t kartvoterulechanges_cons_t[] = {{0, "Never"}, {1, "Sometimes"}, {2, "Frequent"}, {3, "Always"}, {0, NULL}};
consvar_t cv_kartvoterulechanges = {"kartvoterulechanges", "Frequent", CV_NETVAR, kartvoterulechanges_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t kartgametypepreference_cons_t[] = {{-1, "None"}, {GT_RACE, "Race"}, {GT_MATCH, "Battle"}, {0, NULL}};
consvar_t cv_kartgametypepreference = {"kartgametypepreference", "None", CV_NETVAR, kartgametypepreference_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t kartspeedometer_cons_t[] = {{0, "Off"}, {1, "Kilometers"}, {2, "Miles"}, {3, "Fracunits"}, {4, "Percent"}, {0, NULL}};
consvar_t cv_kartspeedometer = {"kartdisplayspeed", "Off", CV_SAVE, kartspeedometer_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL}; // use tics in display
static CV_PossibleValue_t kartvoices_cons_t[] = {{0, "Never"}, {1, "Tasteful"}, {2, "Meme"}, {0, NULL}};
consvar_t cv_kartvoices = {"kartvoices", "Tasteful", CV_SAVE, kartvoices_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_karthitemdialog = {"karthitemdialog", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_karteliminatelast = {"karteliminatelast", "Yes", CV_NETVAR|CV_CHEAT|CV_CALL|CV_NOSHOWHELP, CV_YesNo, KartEliminateLast_OnChange, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t kartdebugitem_cons_t[] = {{-1, "MIN"}, {NUMKARTITEMS-1, "MAX"}, {0, NULL}};
consvar_t cv_kartdebugitem = {"kartdebugitem", "0", CV_NETVAR|CV_CHEAT|CV_NOSHOWHELP, kartdebugitem_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t kartdebugamount_cons_t[] = {{1, "MIN"}, {255, "MAX"}, {0, NULL}};
consvar_t cv_kartdebugamount = {"kartdebugamount", "1", CV_NETVAR|CV_CHEAT|CV_NOSHOWHELP, kartdebugamount_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartdebugshrink = {"kartdebugshrink", "Off", CV_NETVAR|CV_CHEAT|CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartdebugdistribution = {"kartdebugdistribution", "Off", CV_NETVAR|CV_CHEAT|CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartdebughuddrop = {"kartdebughuddrop", "Off", CV_NETVAR|CV_CHEAT|CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_kartdebugcheckpoint = {"kartdebugcheckpoint", "Off", CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartdebugnodes = {"kartdebugnodes", "Off", CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartdebugcolorize = {"kartdebugcolorize", "Off", CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t votetime_cons_t[] = {{1, "MIN"}, {3600, "MAX"}, {0, NULL}};
consvar_t cv_votetime = {"votetime", "20", CV_NETVAR, votetime_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_ringslinger = {"ringslinger", "No", CV_NETVAR|CV_NOSHOWHELP|CV_CALL|CV_CHEAT, CV_YesNo,
	Ringslinger_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_gravity = {"gravity", "0.8", CV_RESTRICT|CV_FLOAT|CV_CALL, NULL, Gravity_OnChange, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_soundtest = {"soundtest", "0", CV_CALL, NULL, SoundTest_OnChange, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t minitimelimit_cons_t[] = {{15, "MIN"}, {9999, "MAX"}, {0, NULL}};
consvar_t cv_countdowntime = {"countdowntime", "30", CV_NETVAR|CV_CHEAT, minitimelimit_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_touchtag = {"touchtag", "Off", CV_NETVAR|CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_hidetime = {"hidetime", "30", CV_NETVAR|CV_CALL|CV_NOSHOWHELP, minitimelimit_cons_t, Hidetime_OnChange, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_autobalance = {"autobalance", "0", CV_NETVAR|CV_CALL, autobalance_cons_t, AutoBalance_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_teamscramble = {"teamscramble", "Off", CV_NETVAR|CV_CALL|CV_NOINIT, teamscramble_cons_t, TeamScramble_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_scrambleonchange = {"scrambleonchange", "Off", CV_NETVAR, teamscramble_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_friendlyfire = {"friendlyfire", "Off", CV_NETVAR|CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_itemfinder = {"itemfinder", "Off", CV_CALL|CV_NOSHOWHELP, CV_OnOff, ItemFinder_OnChange, 0, NULL, NULL, 0, 0, NULL};

// Scoring type options
consvar_t cv_match_scoring = {"matchscoring", "Normal", CV_NETVAR|CV_CHEAT|CV_NOSHOWHELP, match_scoring_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_overtime = {"overtime", "Yes", CV_NETVAR, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_rollingdemos = {"rollingdemos", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_timetic = {"timerres", "Normal", CV_SAVE|CV_NOSHOWHELP, timetic_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL}; // use tics in display

static CV_PossibleValue_t pointlimit_cons_t[] = {{0, "MIN"}, {999999990, "MAX"}, {0, NULL}};
consvar_t cv_pointlimit = {"pointlimit", "0", CV_NETVAR|CV_CALL|CV_NOINIT, pointlimit_cons_t,
	PointLimit_OnChange, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t timelimit_cons_t[] = {{0, "MIN"}, {30, "MAX"}, {0, NULL}};
consvar_t cv_timelimit = {"timelimit", "0", CV_NETVAR|CV_CALL|CV_NOINIT, timelimit_cons_t,
	TimeLimit_OnChange, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t numlaps_cons_t[] = {{1, "MIN"}, {50, "MAX"}, {0, NULL}};
consvar_t cv_numlaps = {"numlaps", "3", CV_NETVAR|CV_CALL|CV_NOINIT, numlaps_cons_t,
	NumLaps_OnChange, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t basenumlaps_cons_t[] = {{1, "MIN"}, {50, "MAX"}, {0, "Map default"}, {0, NULL}};
consvar_t cv_basenumlaps = {"basenumlaps", "Map default", CV_NETVAR|CV_CALL|CV_CHEAT, basenumlaps_cons_t, BaseNumLaps_OnChange, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_forceskin = {"forceskin", "Off", CV_NETVAR|CV_CALL|CV_CHEAT, Forceskin_cons_t, ForceSkin_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_fakelocalskin = {"fakelocalskin", "None", CV_SAVE, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_downloading = {"downloading", "On", 0, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_allowexitlevel = {"allowexitlevel", "No", CV_NETVAR, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t votemaxrows_cons_t[] = {{1, "MIN"}, {3, "MAX"}, {0, NULL}};
consvar_t cv_votemaxrows = {"votemaxrows", "2", CV_SAVE|CV_NETVAR, votemaxrows_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_killingdead = {"killingdead", "Off", CV_NETVAR|CV_NOSHOWHELP, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_netstat = {"netstat", "Off", 0, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL}; // show bandwidth statistics
static CV_PossibleValue_t nettimeout_cons_t[] = {{TICRATE/7, "MIN"}, {60*TICRATE, "MAX"}, {0, NULL}};
consvar_t cv_nettimeout = {"nettimeout", "210", CV_CALL|CV_SAVE, nettimeout_cons_t, NetTimeout_OnChange, 0, NULL, NULL, 0, 0, NULL};
//static CV_PossibleValue_t jointimeout_cons_t[] = {{5*TICRATE, "MIN"}, {60*TICRATE, "MAX"}, {0, NULL}};
consvar_t cv_jointimeout = {"jointimeout", "210", CV_CALL|CV_SAVE, nettimeout_cons_t, JoinTimeout_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_maxping = {"maxdelay", "20", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t pingtimeout_cons_t[] = {{8, "MIN"}, {120, "MAX"}, {0, NULL}};
consvar_t cv_pingtimeout = {"maxdelaytimeout", "10", CV_SAVE, pingtimeout_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

// show your ping on the HUD next to framerate. Defaults to warning only (shows up if your ping is > maxping)
static CV_PossibleValue_t showping_cons_t[] = {{0, "Off"}, {1, "Always"}, {2, "Warning"}, {0, NULL}};
consvar_t cv_showping = {"showping", "Always", CV_SAVE, showping_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t pingmeasurement_cons_t[] = {{0, "Frames"}, {1, "Milliseconds"}, {0, NULL}};
consvar_t cv_pingmeasurement = {"pingmeasurement", "Frames", CV_SAVE, pingmeasurement_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_pingicon = {"pingicon", "On", CV_SAVE, CV_OnOff, 0, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t cv_pingstyle_cons_t[] = {{0, "New"}, {1, "Old"}, {0, NULL}};
consvar_t cv_pingstyle = {"pingstyle", "New", CV_SAVE, cv_pingstyle_cons_t, 0, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_luaimmersion = {"luaimmersion", "On", CV_SAVE, CV_OnOff, 0, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_minihead = {"smallminimapplayers", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_showminimapnames = {"showminimapnames", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_showlapemblem = {"showlapemblem", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_showviewpointtext = {"showviewpointtext", "On", CV_SAVE, CV_OnOff, 0, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_showdownloadprompt = {"showdownloadprompt", "On", CV_SAVE, CV_OnOff, 0, 0, NULL, NULL, 0, 0, NULL};

// Intermission time Tails 04-19-2002
static CV_PossibleValue_t inttime_cons_t[] = {{0, "MIN"}, {3600, "MAX"}, {0, NULL}};
consvar_t cv_inttime = {"inttime", "20", CV_NETVAR, inttime_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t advancemap_cons_t[] = {{0, "Same"}, {1, "Next"}, {2, "Random"}, {3, "Vote"}, {0, NULL}};
consvar_t cv_advancemap = {"advancemap", "Vote", CV_NETVAR, advancemap_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t playersforexit_cons_t[] = {{0, "One"}, {1, "All"}, {0, NULL}};
consvar_t cv_playersforexit = {"playersforexit", "One", CV_NETVAR, playersforexit_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_runscripts = {"runscripts", "Yes", 0, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_pause = {"pausepermission", "Server", CV_NETVAR, pause_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_mute = {"mute", "Off", CV_NETVAR|CV_CALL, CV_OnOff, Mute_OnChange, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_sleep = {"cpusleep", "1", CV_SAVE, sleeping_cons_t, NULL, -1, NULL, NULL, 0, 0, NULL};

//Less Battlevotes and 1 encore
consvar_t cv_lessbattlevotes = {"lessbattlevotes", "No", CV_SAVE, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t encorevotes_cons_t[] = {{0, "One"}, {1, "Except One"}, {0, NULL}};
consvar_t cv_encorevotes = {"encorevotes", "One", CV_SAVE, encorevotes_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t nametagtrans_cons_t[] = {
	{0, "Never"}, {1, "Far only"}, {2, "Dynamic"}, {3, "Always"}, {4, "HUD Trans."}, {0, NULL}};
/*static CV_PossibleValue_t nametagscaling_cons_t[] = {
	{0, "MIN"},  {320, "MAX"}, {0, NULL}};*/
static CV_PossibleValue_t nametagdistance_cons_t[] = {
	{0, "MIN"},  {640, "MAX"}, {0, NULL}};
static CV_PossibleValue_t nametagmaxplayer_cons_t[] = {
    {1, "MIN"}, {MAXPLAYERS, "MAX"}, {0, NULL}};
static CV_PossibleValue_t nametagsize_cons_t[] = {
	{0, "Off"}, {1, "Small"}, {2, "Minimal"}, {0, NULL}};
static CV_PossibleValue_t nametagrestat_cons_t[] = {
	{0, "Off"}, {1, "Restat"}, {2, "Always"}, {0, NULL}};

consvar_t cv_nametag = {"kartnametag", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_nametagtrans = {"kartnametagtransparency", "Dynamic", CV_SAVE, nametagtrans_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_nametagfacerank = {"kartnametagfacerank", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_nametagrestat = {"kartnametagrestat", "Restat", CV_SAVE, nametagrestat_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_nametagdist = {"kartnametagdist", "320", CV_SAVE, nametagdistance_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_nametagmaxplayers = {"kartnametagmaxplayers", "3", CV_SAVE, nametagmaxplayer_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_nametagmaxlenght = {"kartnametagmaxlenght", "12", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
//consvar_t cv_nametagscaling = {"nametagscaling", "160", CV_SAVE, nametagscaling_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_showownnametag = {"kartnametagshowown", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_smallnametags = {"kartnametagsmall", "Off", CV_SAVE, nametagsize_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_nametaghop = {"kartnametaghop", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_nametagscore = {"kartnametagscore", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_nametaghealth = {"kartnametaghealth", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_shownametagfinish = {"kartshownametagfinished", "No", CV_SAVE, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_shownametagspectator = {"kartshownametagspectator", "No", CV_SAVE, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t driftgaugeoffset_cons_t[] = {
	{-FRACUNIT*128, "MIN"}, {FRACUNIT*128, "MAX"}, {0, NULL}};

static CV_PossibleValue_t driftgaugestyle_cons_t[] = {
	{1, "Default"}, {2, "Small"}, {3, "Big Numbers"}, {4, "Numbers Only"}, {0, NULL}};

consvar_t cv_driftgauge = {"kartdriftgauge", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_driftgaugeofs = {"kartdriftgaugeoffset", "-20", CV_FLOAT|CV_SAVE, driftgaugeoffset_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_driftgaugetrans = {"kartdriftgaugetransparency", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_driftgaugestyle = {"kartdriftgaugestyle", "1", CV_SAVE, driftgaugestyle_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_sneakerfire = {"kartsneakerfire", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t skinselectspin_cons_t[] = {
	{0, "Off"}, {1, "Slow"}, {2, "2"}, {3, "3"}, {4, "4"}, {5, "5"}, {6, "6"}, {7, "7"}, {8, "8"}, {9, "9"}, {10, "Fast"}, {SKINSELECTSPIN_PAIN, "Pain"}, {0, NULL}};
consvar_t cv_skinselectspin = {"skinselectspin", "5", CV_SAVE, skinselectspin_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t perfstats_cons_t[] = {
	{0, "Off"}, {1, "Rendering"}, {2, "Logic"}, {3, "ThinkFrame"}, {4, "PreThinkFrame"}, {5, "PostThinkFrame"}, {0, NULL}};
consvar_t cv_perfstats = {"perfstats", "Off", CV_CALL, perfstats_cons_t, PS_PerfStats_OnChange, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_ps_thinkframe_page = {"ps_thinkframe_page", "1", CV_CALL, CV_Natural, PS_ThinkFrame_Page_OnChange, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t ps_samplesize_cons_t[] = {
	{1, "MIN"}, {1000, "MAX"}, {0, NULL}};
consvar_t cv_ps_samplesize = {"ps_samplesize", "1", CV_CALL, ps_samplesize_cons_t, PS_SampleSize_OnChange, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t ps_descriptor_cons_t[] = {
	{1, "Average"}, {2, "SD"}, {3, "Minimum"}, {4, "Maximum"}, {0, NULL}};
consvar_t cv_ps_descriptor = {"ps_descriptor", "Average", 0, ps_descriptor_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_director = {"director", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_kartdebugdirector = {"debugdirector", "Off", 0, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_showdirectorhud = {"showdirectorhud", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_showtrackaddon = {"showtrackaddon", "Yes", CV_SAVE, CV_YesNo, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t skinselectmenu_t[] = {{SKINMENUTYPE_SCROLL, "Scroll"}, {SKINMENUTYPE_2D, "2d"}, {SKINMENUTYPE_GRID, "Grid"}, {SKINMENUTYPE_EXTENDED, "Extended"}, {0, NULL}};
consvar_t cv_skinselectmenu = {"skinselectmenu", "Extended", CV_SAVE, skinselectmenu_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t skinselectgridsort_t[] ={
	{ SKINMENUSORT_REALNAME, "Real name" },
	{ SKINMENUSORT_NAME, "Internal name" },
	{ SKINMENUSORT_SPEED, "Speed" },
	{ SKINMENUSORT_WEIGHT, "Weight" },
	{ SKINMENUSORT_PREFCOLOR, "Preferred Color" }, // get stickbugged lol
	{ SKINMENUSORT_ID, "ID" },
	{ 0, NULL }
};
consvar_t cv_skinselectgridsort ={ "skinselectgridsort", "Real name", CV_SAVE|CV_CALL|CV_NOINIT, skinselectgridsort_t, sortSkinGrid, 0, NULL, NULL, 0, 0, NULL };

static CV_PossibleValue_t betainterscreen_t[] = {{0, "Off"}, {1, "On"}, {2, "NoBG"}, {0, NULL}};
consvar_t cv_betainterscreen = {"betaintermissionscreen", "Off", CV_SAVE, betainterscreen_t, NULL, 0, NULL, NULL, 0, 0, NULL};

INT16 gametype = GT_RACE; // SRB2kart
boolean forceresetplayers = false;
boolean deferencoremode = false;
UINT8 splitscreen = 0;
boolean circuitmap = true; // SRB2kart
INT32 adminplayers[MAXPLAYERS];

#define VOTEROWS ((cv_votemaxrows.value*3) + ((cv_votemaxrows.value > 1) ? (cv_votemaxrows.value - 1) : 0))
#define VOTEROWSADDSONE ((cv_votemaxrows.value*3) + 1 + ((cv_votemaxrows.value > 1) ? (cv_votemaxrows.value - 1) : 0))

/// \warning Keep this up-to-date if you add/remove/rename net text commands
const char *netxcmdnames[MAXNETXCMD - 1] =
{
	"NAMEANDCOLOR",
	"WEAPONPREF",
	"KICK",
	"NETVAR",
	"SAY",
	"MAP",
	"EXITLEVEL",
	"ADDFILE",
	"PAUSE",
	"ADDPLAYER",
	"TEAMCHANGE",
	"CLEARSCORES",
	"LOGIN",
	"VERIFIED",
	"RANDOMSEED",
	"RUNSOC",
	"REQADDFILE",
	"DELFILE", // sigh well this prob needs to be still here to not screw up maxnetxcmd
	"SETMOTD",
	"RESPAWN",
	"DEMOTED",
	"SETUPVOTE",
	"MODIFYVOTE",
	"PICKVOTE",
	"REMOVEPLAYER",
	"LUACMD",
	"LUAVAR"
};

// =========================================================================
//                           SERVER STARTUP
// =========================================================================

/** Registers server commands and variables.
  * Anything required by a dedicated server should probably go here.
  *
  * \sa D_RegisterClientCommands
  */
void D_RegisterServerCommands(void)
{
	INT32 i;
	Forceskin_cons_t[0].value = -1;
	Forceskin_cons_t[0].strvalue = "Off";

	for (i = 0; i < NUMGAMETYPES; i++)
	{
		gametype_cons_t[i].value = i;
		gametype_cons_t[i].strvalue = Gametype_Names[i];
	}
	gametype_cons_t[NUMGAMETYPES].value = 0;
	gametype_cons_t[NUMGAMETYPES].strvalue = NULL;

	// Set the values to 0/NULL, it will be overwritten later when a skin is assigned to the slot.
	for (i = 1; i < MAXSKINS; i++)
	{
		Forceskin_cons_t[i].value = 0;
		Forceskin_cons_t[i].strvalue = NULL;
	}
	
	RegisterNetXCmd(XD_NAMEANDCOLOR, Got_NameAndColor);
	RegisterNetXCmd(XD_WEAPONPREF, Got_WeaponPref);
	RegisterNetXCmd(XD_MAP, Got_Mapcmd);
	RegisterNetXCmd(XD_EXITLEVEL, Got_ExitLevelcmd);
	RegisterNetXCmd(XD_ADDFILE, Got_Addfilecmd);
	RegisterNetXCmd(XD_REQADDFILE, Got_RequestAddfilecmd);
	RegisterNetXCmd(XD_PAUSE, Got_Pause);
	RegisterNetXCmd(XD_RESPAWN, Got_Respawn);
	RegisterNetXCmd(XD_RUNSOC, Got_RunSOCcmd);
	RegisterNetXCmd(XD_LUACMD, Got_Luacmd);

	RegisterNetXCmd(XD_SETUPVOTE, Got_SetupVotecmd);
	RegisterNetXCmd(XD_MODIFYVOTE, Got_ModifyVotecmd);
	RegisterNetXCmd(XD_PICKVOTE, Got_PickVotecmd);

	// Remote Administration
	COM_AddCommand("password", Command_Changepassword_f);
	RegisterNetXCmd(XD_LOGIN, Got_Login);
	COM_AddCommand("login", Command_Login_f); // useful in dedicated to kick off remote admin
	COM_AddCommand("promote", Command_Verify_f);
	RegisterNetXCmd(XD_VERIFIED, Got_Verification);
	COM_AddCommand("demote", Command_RemoveAdmin_f);
	RegisterNetXCmd(XD_DEMOTED, Got_Removal);

	COM_AddCommand("motd", Command_MotD_f);
	RegisterNetXCmd(XD_SETMOTD, Got_MotD_f); // For remote admin

	RegisterNetXCmd(XD_TEAMCHANGE, Got_Teamchange);
	COM_AddCommand("serverchangeteam", Command_ServerTeamChange_f);

	RegisterNetXCmd(XD_CLEARSCORES, Got_Clearscores);
	COM_AddCommand("clearscores", Command_Clearscores_f);
	COM_AddCommand("map", Command_Map_f);

	COM_AddCommand("exitgame", Command_ExitGame_f);
	COM_AddCommand("retry", Command_Retry_f);
	COM_AddCommand("exitlevel", Command_ExitLevel_f);
	COM_AddCommand("showmap", Command_Showmap_f);
	COM_AddCommand("mapmd5", Command_Mapmd5_f);

	COM_AddCommand("addfilelocal", Command_Addfilelocal);
	COM_AddCommand("addfile", Command_Addfile);
	COM_AddCommand("addskins", Command_Addskins);
	COM_AddCommand("localskin", Command_GLocalSkin);
	COM_AddCommand("listwad", Command_ListWADS_f);

	COM_AddCommand("runsoc", Command_RunSOC);
	COM_AddCommand("pause", Command_Pause);
	COM_AddCommand("respawn", Command_Respawn);

	COM_AddCommand("gametype", Command_ShowGametype_f);
	COM_AddCommand("version", Command_Version_f);
#ifdef UPDATE_ALERT
	COM_AddCommand("mod_details", Command_ModDetails_f);
#endif
	COM_AddCommand("quit", Command_Quit_f);

	COM_AddCommand("saveconfig", Command_SaveConfig_f);
	COM_AddCommand("loadconfig", Command_LoadConfig_f);
	COM_AddCommand("changeconfig", Command_ChangeConfig_f);
	COM_AddCommand("isgamemodified", Command_Isgamemodified_f); // test
	COM_AddCommand("showscores", Command_ShowScores_f);
	COM_AddCommand("showtime", Command_ShowTime_f);
	COM_AddCommand("cheats", Command_Cheats_f); // test
#ifdef _DEBUG
	COM_AddCommand("togglemodified", Command_Togglemodified_f);
	COM_AddCommand("archivetest", Command_Archivetest_f);
#endif

	COM_AddCommand("replaymarker", Command_ReplayMarker);

	// for master server connection
	AddMServCommands();

	// p_mobj.c
	CV_RegisterVar(&cv_itemrespawntime);
	CV_RegisterVar(&cv_itemrespawn);
	CV_RegisterVar(&cv_flagtime);
	CV_RegisterVar(&cv_suddendeath);

	// misc
	CV_RegisterVar(&cv_friendlyfire);
	CV_RegisterVar(&cv_pointlimit);
	CV_RegisterVar(&cv_numlaps);
	CV_RegisterVar(&cv_basenumlaps);

	CV_RegisterVar(&cv_autobalance);
	CV_RegisterVar(&cv_teamscramble);
	CV_RegisterVar(&cv_scrambleonchange);

	CV_RegisterVar(&cv_touchtag);
	CV_RegisterVar(&cv_hidetime);

	CV_RegisterVar(&cv_inttime);
	CV_RegisterVar(&cv_advancemap);
	CV_RegisterVar(&cv_playersforexit);
	CV_RegisterVar(&cv_timelimit);
	CV_RegisterVar(&cv_playbackspeed);
	CV_RegisterVar(&cv_forceskin);
	CV_RegisterVar(&cv_downloading);
	CV_RegisterVar(&cv_votemaxrows);

	CV_RegisterVar(&cv_specialrings);
	CV_RegisterVar(&cv_powerstones);
	CV_RegisterVar(&cv_competitionboxes);
	CV_RegisterVar(&cv_matchboxes);

	K_RegisterKartStuff(); // SRB2kart

	CV_RegisterVar(&cv_ringslinger);

	CV_RegisterVar(&cv_startinglives);
	CV_RegisterVar(&cv_countdowntime);
	CV_RegisterVar(&cv_runscripts);
	CV_RegisterVar(&cv_match_scoring);
	CV_RegisterVar(&cv_overtime);
	CV_RegisterVar(&cv_pause);
	CV_RegisterVar(&cv_mute);

	RegisterNetXCmd(XD_RANDOMSEED, Got_RandomSeed);

	CV_RegisterVar(&cv_allowexitlevel);
	CV_RegisterVar(&cv_restrictskinchange);
	CV_RegisterVar(&cv_allowteamchange);
	CV_RegisterVar(&cv_ingamecap);
	CV_RegisterVar(&cv_spectatorreentry);
	CV_RegisterVar(&cv_antigrief);
	CV_RegisterVar(&cv_respawntime);
	CV_RegisterVar(&cv_killingdead);

	// d_clisrv
	CV_RegisterVar(&cv_maxplayers);
	CV_RegisterVar(&cv_gamestateattempts);
	CV_RegisterVar(&cv_resynchcooldown);
	CV_RegisterVar(&cv_maxsend);
	CV_RegisterVar(&cv_noticedownload);
	CV_RegisterVar(&cv_downloadspeed);
    CV_RegisterVar(&cv_connectawaittime);
	CV_RegisterVar(&cv_httpsource);
#ifndef NONET
	CV_RegisterVar(&cv_allownewplayer);
	CV_RegisterVar(&cv_joinrefusemessage);
	CV_RegisterVar(&cv_chatlogsize);
#ifdef VANILLAJOINNEXTROUND
	CV_RegisterVar(&cv_joinnextround);
#endif
	CV_RegisterVar(&cv_showjoinaddress);
	CV_RegisterVar(&cv_shownodeip);
	CV_RegisterVar(&cv_blamecfail);
#endif

	COM_AddCommand("ping", Command_Ping_f);
	CV_RegisterVar(&cv_nettimeout);
	CV_RegisterVar(&cv_jointimeout);
	CV_RegisterVar(&cv_kicktime);
	CV_RegisterVar(&cv_skipmapcheck);
	CV_RegisterVar(&cv_sleep);
	CV_RegisterVar(&cv_maxping);
	CV_RegisterVar(&cv_pingtimeout);
	CV_RegisterVar(&cv_showping);
	CV_RegisterVar(&cv_pingmeasurement);

#ifdef SEENAMES
	CV_RegisterVar(&cv_allowseenames);
#endif

	CV_RegisterVar(&cv_dummyconsvar);
	//Lessbattlevotes and encorevotes
	CV_RegisterVar(&cv_lessbattlevotes);
	CV_RegisterVar(&cv_encorevotes);
	


#ifdef USE_STUN
	CV_RegisterVar(&cv_stunserver);
#endif

	CV_RegisterVar(&cv_discordinvites);
	RegisterNetXCmd(XD_DISCORD, Got_DiscordInfo);

	CV_RegisterVar(&cv_recordmultiplayerdemos);
	CV_RegisterVar(&cv_netdemosyncquality);
	CV_RegisterVar(&cv_maxdemosize);
	CV_RegisterVar(&cv_demochangemap);

	CV_RegisterVar(&cv_keyboardlayout);
}

// =========================================================================
//                           CLIENT STARTUP
// =========================================================================

/** Registers client commands and variables.
  * Nothing needed for a dedicated server should be registered here.
  *
  * \sa D_RegisterServerCommands
  */
void D_RegisterClientCommands(void)
{
	INT32 i;

	for (i = 0; i < MAXSKINCOLORS; i++)
	{
		Color_cons_t[i].value = i;
		Color_cons_t[i].strvalue = KartColor_Names[i];				// SRB2kart
	}
	Color_cons_t[MAXSKINCOLORS].value = 0;
	Color_cons_t[MAXSKINCOLORS].strvalue = NULL;

	if (dedicated)
		return;

	COM_AddCommand("numthinkers", Command_Numthinkers_f);
	COM_AddCommand("countmobjs", Command_CountMobjs_f);

	COM_AddCommand("changeteam", Command_Teamchange_f);
	COM_AddCommand("changeteam2", Command_Teamchange2_f);
	COM_AddCommand("changeteam3", Command_Teamchange3_f);
	COM_AddCommand("changeteam4", Command_Teamchange4_f);

	COM_AddCommand("playdemo", Command_Playdemo_f);
	COM_AddCommand("timedemo", Command_Timedemo_f);
	COM_AddCommand("stopdemo", Command_Stopdemo_f);
	COM_AddCommand("playintro", Command_Playintro_f);

	COM_AddCommand("resetcamera", Command_ResetCamera_f);

	COM_AddCommand("view", Command_View_f);
	COM_AddCommand("view2", Command_View_f);
	COM_AddCommand("view3", Command_View_f);
	COM_AddCommand("view4", Command_View_f);

	COM_AddCommand("setviews", Command_SetViews_f);

	COM_AddCommand("setcontrol", Command_Setcontrol_f);
	COM_AddCommand("setcontrol2", Command_Setcontrol2_f);
	COM_AddCommand("setcontrol3", Command_Setcontrol3_f);
	COM_AddCommand("setcontrol4", Command_Setcontrol4_f);

	COM_AddCommand("screenshot", M_ScreenShot);
	COM_AddCommand("startmovie", Command_StartMovie_f);
	COM_AddCommand("stopmovie", Command_StopMovie_f);

	CV_RegisterVar(&cv_screenshot_option);
	CV_RegisterVar(&cv_screenshot_folder);
	CV_RegisterVar(&cv_moviemode);
	// PNG variables
	CV_RegisterVar(&cv_zlib_level);
	CV_RegisterVar(&cv_zlib_memory);
	CV_RegisterVar(&cv_zlib_strategy);
	CV_RegisterVar(&cv_zlib_window_bits);
	// APNG variables
	CV_RegisterVar(&cv_zlib_levela);
	CV_RegisterVar(&cv_zlib_memorya);
	CV_RegisterVar(&cv_zlib_strategya);
	CV_RegisterVar(&cv_zlib_window_bitsa);
	CV_RegisterVar(&cv_apng_delay);
	// GIF variables
	CV_RegisterVar(&cv_gif_optimize);
	CV_RegisterVar(&cv_gif_downscale);

#ifdef WALLSPLATS
	CV_RegisterVar(&cv_splats);
#endif
	
	// stacking boostflame color
	CV_RegisterVar(&cv_stackingboostflamecolor);

	// register these so it is saved to config
	CV_RegisterVar(&cv_playername);
	CV_RegisterVar(&cv_playercolor);
	CV_RegisterVar(&cv_skin); // r_things.c (skin NAME)
	CV_RegisterVar(&cv_follower);
	CV_RegisterVar(&cv_followercolor);
	CV_RegisterVar(&cv_localskin);
	// secondary player (splitscreen)
	CV_RegisterVar(&cv_playername2);
	CV_RegisterVar(&cv_playercolor2);
	CV_RegisterVar(&cv_skin2);
	CV_RegisterVar(&cv_follower2);
	CV_RegisterVar(&cv_followercolor2);
	// third player
	CV_RegisterVar(&cv_playername3);
	CV_RegisterVar(&cv_playercolor3);
	CV_RegisterVar(&cv_skin3);
	CV_RegisterVar(&cv_follower3);
	CV_RegisterVar(&cv_followercolor3);
	// fourth player
	CV_RegisterVar(&cv_playername4);
	CV_RegisterVar(&cv_playercolor4);
	CV_RegisterVar(&cv_skin4);
	CV_RegisterVar(&cv_follower4);
	CV_RegisterVar(&cv_followercolor4);
	// preferred number of players
	CV_RegisterVar(&cv_splitplayers);

	CV_RegisterVar(&cv_skinselectmenu);
	CV_RegisterVar(&cv_skinselectgridsort);

#ifdef SEENAMES
	CV_RegisterVar(&cv_seenames);
#endif
	CV_RegisterVar(&cv_rollingdemos);
	CV_RegisterVar(&cv_netstat);
	CV_RegisterVar(&cv_netticbuffer);

#ifdef NETGAME_DEVMODE
	CV_RegisterVar(&cv_fishcake);
#endif

	// HUD
	CV_RegisterVar(&cv_timetic);
	CV_RegisterVar(&cv_itemfinder);

	CV_RegisterVar(&cv_pingicon);
	CV_RegisterVar(&cv_pingstyle);

	CV_RegisterVar(&cv_cechotoggle);

	// time attack ghost options are also saved to config
	CV_RegisterVar(&cv_ghost_besttime);
	CV_RegisterVar(&cv_ghost_bestlap);
	CV_RegisterVar(&cv_ghost_last);
	CV_RegisterVar(&cv_ghost_guest);
	CV_RegisterVar(&cv_ghost_staff);

	COM_AddCommand("displayplayer", Command_Displayplayer_f);

	CV_RegisterVar(&cv_audbuffersize); 

	// m_menu.c
	CV_RegisterVar(&cv_chatheight);
	CV_RegisterVar(&cv_chatwidth);
	CV_RegisterVar(&cv_chattime);
	CV_RegisterVar(&cv_chatspamprotection);
	CV_RegisterVar(&cv_consolechat);
	CV_RegisterVar(&cv_chatnotifications);
	CV_RegisterVar(&cv_chatbacktint);
	CV_RegisterVar(&cv_songcredits);
	CV_RegisterVar(&cv_showfreeplay);	
	CV_RegisterVar(&cv_skinselectspin);
	CV_RegisterVar(&cv_showallmaps);
	
	CV_RegisterVar(&cv_growmusic);
	CV_RegisterVar(&cv_supermusic);

	CV_RegisterVar(&cv_showtrackaddon);
	CV_RegisterVar(&cv_showviewpointtext);
	CV_RegisterVar(&cv_showdownloadprompt);

	CV_RegisterVar(&cv_director);
	CV_RegisterVar(&cv_kartdebugdirector);
	CV_RegisterVar(&cv_showdirectorhud);

	CV_RegisterVar(&cv_luaimmersion);
	CV_RegisterVar(&cv_fakelocalskin);

	CV_RegisterVar(&cv_showlocalskinmenus);

	//CV_RegisterVar(&cv_crosshair);
	//CV_RegisterVar(&cv_crosshair2);
	//CV_RegisterVar(&cv_crosshair3);
	//CV_RegisterVar(&cv_crosshair4);
	//CV_RegisterVar(&cv_alwaysfreelook);
	//CV_RegisterVar(&cv_alwaysfreelook2);
	//CV_RegisterVar(&cv_chasefreelook);
	//CV_RegisterVar(&cv_chasefreelook2);
	CV_RegisterVar(&cv_replaysearchrate);
	CV_RegisterVar(&cv_showfocuslost);
	CV_RegisterVar(&cv_pauseifunfocused);

	// g_input.c
	CV_RegisterVar(&cv_turnaxis);
	CV_RegisterVar(&cv_turnaxis2);
	CV_RegisterVar(&cv_turnaxis3);
	CV_RegisterVar(&cv_turnaxis4);
	CV_RegisterVar(&cv_moveaxis);
	CV_RegisterVar(&cv_moveaxis2);
	CV_RegisterVar(&cv_moveaxis3);
	CV_RegisterVar(&cv_moveaxis4);
	CV_RegisterVar(&cv_brakeaxis);
	CV_RegisterVar(&cv_brakeaxis2);
	CV_RegisterVar(&cv_brakeaxis3);
	CV_RegisterVar(&cv_brakeaxis4);
	CV_RegisterVar(&cv_aimaxis);
	CV_RegisterVar(&cv_aimaxis2);
	CV_RegisterVar(&cv_aimaxis3);
	CV_RegisterVar(&cv_aimaxis4);
	CV_RegisterVar(&cv_lookaxis);
	CV_RegisterVar(&cv_lookaxis2);
	CV_RegisterVar(&cv_lookaxis3);
	CV_RegisterVar(&cv_lookaxis4);
	CV_RegisterVar(&cv_fireaxis);
	CV_RegisterVar(&cv_fireaxis2);
	CV_RegisterVar(&cv_fireaxis3);
	CV_RegisterVar(&cv_fireaxis4);
	CV_RegisterVar(&cv_driftaxis);
	CV_RegisterVar(&cv_driftaxis2);
	CV_RegisterVar(&cv_driftaxis3);
	CV_RegisterVar(&cv_driftaxis4);
	CV_RegisterVar(&cv_lookbackaxis);
	CV_RegisterVar(&cv_lookbackaxis2);
	CV_RegisterVar(&cv_lookbackaxis3);
	CV_RegisterVar(&cv_lookbackaxis4);
	CV_RegisterVar(&cv_custom1axis);
	CV_RegisterVar(&cv_custom1axis2);
	CV_RegisterVar(&cv_custom1axis3);
	CV_RegisterVar(&cv_custom1axis4);
	CV_RegisterVar(&cv_custom2axis);
	CV_RegisterVar(&cv_custom2axis2);
	CV_RegisterVar(&cv_custom2axis3);
	CV_RegisterVar(&cv_custom2axis4);
	CV_RegisterVar(&cv_custom3axis);
	CV_RegisterVar(&cv_custom3axis2);
	CV_RegisterVar(&cv_custom3axis3);
	CV_RegisterVar(&cv_custom3axis4);
	CV_RegisterVar(&cv_xdeadzone);
	CV_RegisterVar(&cv_ydeadzone);
	CV_RegisterVar(&cv_xdeadzone2);
	CV_RegisterVar(&cv_ydeadzone2);
	CV_RegisterVar(&cv_xdeadzone3);
	CV_RegisterVar(&cv_ydeadzone3);
	CV_RegisterVar(&cv_xdeadzone4);
	CV_RegisterVar(&cv_ydeadzone4);

	// filesrch.c
	CV_RegisterVar(&cv_addons_option);
	CV_RegisterVar(&cv_addons_folder);
	CV_RegisterVar(&cv_addons_md5);
	CV_RegisterVar(&cv_addons_showall);
	CV_RegisterVar(&cv_addons_search_type);
	CV_RegisterVar(&cv_addons_search_case);

	// WARNING: the order is important when initialising mouse2
	// we need the mouse2port
	CV_RegisterVar(&cv_mouse2port);
#if defined (__unix__) || defined (__APPLE__) || defined (UNIXCOMMON)
	CV_RegisterVar(&cv_mouse2opt);
#endif
	CV_RegisterVar(&cv_controlperkey);
	CV_RegisterVar(&cv_turnsmooth);

	for (i = 0; i < MAXSPLITSCREENPLAYERS; i++)
	{
		CV_RegisterVar(&cv_rumble[i]);
		CV_RegisterVar(&cv_gamepadled[i]);
		CV_RegisterVar(&cv_ledpowerup[i]);
	}

	CV_RegisterVar(&cv_usemouse);
	CV_RegisterVar(&cv_usemouse2);
	CV_RegisterVar(&cv_invertmouse);
	CV_RegisterVar(&cv_invertmouse2);
	CV_RegisterVar(&cv_mousesens);
	CV_RegisterVar(&cv_mousesens2);
	CV_RegisterVar(&cv_mouseysens);
	CV_RegisterVar(&cv_mouseysens2);
	//CV_RegisterVar(&cv_mousemove);
	//CV_RegisterVar(&cv_mousemove2);

	CV_RegisterVar(&cv_usejoystick);
	CV_RegisterVar(&cv_usejoystick2);
	CV_RegisterVar(&cv_usejoystick3);
	CV_RegisterVar(&cv_usejoystick4);
#ifdef LJOYSTICK
	CV_RegisterVar(&cv_joyport);
	CV_RegisterVar(&cv_joyport2);
#endif
	CV_RegisterVar(&cv_joyscale);
	CV_RegisterVar(&cv_joyscale2);
	CV_RegisterVar(&cv_joyscale3);
	CV_RegisterVar(&cv_joyscale4);

	// Analog Control
	/*CV_RegisterVar(&cv_analog);
	CV_RegisterVar(&cv_analog2);
	CV_RegisterVar(&cv_analog3);
	CV_RegisterVar(&cv_analog4);
	CV_RegisterVar(&cv_useranalog);
	CV_RegisterVar(&cv_useranalog2);
	CV_RegisterVar(&cv_useranalog3);
	CV_RegisterVar(&cv_useranalog4);*/

	// s_sound.c
	CV_RegisterVar(&cv_soundvolume);
	CV_RegisterVar(&cv_digmusicvolume);
#ifndef NO_MIDI
	CV_RegisterVar(&cv_midimusicvolume);
#endif
	CV_RegisterVar(&cv_numChannels);
	
#ifdef HAVE_OPENMPT
	CV_RegisterVar(&cv_modfilter);
	CV_RegisterVar(&cv_stereosep);
	CV_RegisterVar(&cv_amigafilter);
#if OPENMPT_API_VERSION_MAJOR < 1 && OPENMPT_API_VERSION_MINOR > 4
	CV_RegisterVar(&cv_amigatype);
#endif
#endif

	// screen.c
	CV_RegisterVar(&cv_fullscreen);
	CV_RegisterVar(&cv_renderview);
	CV_RegisterVar(&cv_vhseffect);
	CV_RegisterVar(&cv_shittyscreen);
	CV_RegisterVar(&cv_scr_depth);
	CV_RegisterVar(&cv_scr_width);
	CV_RegisterVar(&cv_scr_height);

	CV_RegisterVar(&cv_soundtest);
	
	CV_RegisterVar(&cv_nametag);
	CV_RegisterVar(&cv_nametagtrans);
	CV_RegisterVar(&cv_nametagfacerank);
	CV_RegisterVar(&cv_nametagmaxplayers);
	CV_RegisterVar(&cv_nametagmaxlenght);
	//CV_RegisterVar(&cv_nametagscaling);
	CV_RegisterVar(&cv_nametagdist);
	CV_RegisterVar(&cv_showownnametag);
	CV_RegisterVar(&cv_smallnametags);
	CV_RegisterVar(&cv_nametagrestat);
	CV_RegisterVar(&cv_nametaghop);
	CV_RegisterVar(&cv_nametagscore);
	
	// If you take this for a vanilla-compat client remove hpmod stuff.
	CV_RegisterVar(&cv_nametaghealth);
	CV_RegisterVar(&cv_shownametagfinish);
	CV_RegisterVar(&cv_shownametagspectator);
	
	CV_RegisterVar(&cv_driftgauge);
	CV_RegisterVar(&cv_driftgaugeofs);
	CV_RegisterVar(&cv_driftgaugetrans);
	CV_RegisterVar(&cv_driftgaugestyle);

	CV_RegisterVar(&cv_sneakerfire);

	CV_RegisterVar(&cv_perfstats);
	CV_RegisterVar(&cv_ps_thinkframe_page);
	CV_RegisterVar(&cv_ps_samplesize);
	CV_RegisterVar(&cv_ps_descriptor);

	//Value used to store last server player has joined
	CV_RegisterVar(&cv_lastserver);

	CV_RegisterVar(&cv_showmusicfilename);

	CV_RegisterVar(&cv_betainterscreen);

	CV_RegisterVar(&cv_laglesscam);

	// ingame object placing
	COM_AddCommand("objectplace", Command_ObjectPlace_f);
	COM_AddCommand("writethings", Command_Writethings_f);
	CV_RegisterVar(&cv_speed);
	CV_RegisterVar(&cv_opflags);
	CV_RegisterVar(&cv_mapthingnum);

	// add cheat commands
	COM_AddCommand("noclip", Command_CheatNoClip_f);
	COM_AddCommand("god", Command_CheatGod_f);
	COM_AddCommand("notarget", Command_CheatNoTarget_f);
	COM_AddCommand("devmode", Command_Devmode_f);
	COM_AddCommand("savecheckpoint", Command_Savecheckpoint_f);
	COM_AddCommand("scale", Command_Scale_f);
	COM_AddCommand("gravflip", Command_Gravflip_f);
	COM_AddCommand("hurtme", Command_Hurtme_f);
	COM_AddCommand("teleport", Command_Teleport_f);
	COM_AddCommand("rteleport", Command_RTeleport_f);
	COM_AddCommand("skynum", Command_Skynum_f);
	COM_AddCommand("weather", Command_Weather_f);
#ifdef _DEBUG
	COM_AddCommand("causecfail", Command_CauseCfail_f);
#endif
#if defined(LUA_ALLOW_BYTECODE)
	COM_AddCommand("dumplua", Command_Dumplua_f);
#endif

#ifdef HAVE_DISCORDRPC
	CV_RegisterVar(&cv_discordrp);
	CV_RegisterVar(&cv_discordstreamer);
	CV_RegisterVar(&cv_discordasks);
#endif

	CV_RegisterVar(&cv_showspecstuff);

	COM_AddCommand("listskins", Command_ListSkins);
	COM_AddCommand("skinsearch", Command_SkinSearch);
	
	COM_AddCommand("followersearch", Command_FollowerSearch);
}

/**
 * Searches the skins list for a skin of the "realname", if found, changes
 * the CVAR value to be the "name" of the skin.
 * \sa cv_skin, Skin_OnChange, Skin2_OnChange
 * \author karen
 */
static void Skin_FindRealNameSkin(consvar_t *cvar)
{
	INT32 matches[MAXSKINS] = {0}; // Skin numbers of matching skins
	INT32 matches_pos[MAXSKINS] = {0}; // Index of matching substring
	INT32 nummatches = 0;

	INT32 i;
	const char *value = cvar->string;

	for (i = 0; i < numskins; i++)
	{
		// Perfect match with some skin name, no need to find matching realname
		if (strncasecmp(value, skins[i].name, sizeof skins[i].name) == 0)
			return;

		const char *match = strcasestr(skins[i].realname, value);
		if (match != NULL)
		{
			matches[nummatches] = i;
			matches_pos[nummatches] = match - skins[i].realname;
			++nummatches;
		}
	}

	if (nummatches == 1)
	{
		// Change the cvar to be the value of the name.
		CV_StealthSet(cvar, skins[matches[0]].name);
	}
	else if (nummatches > 1)
	{
		size_t query_length = strlen(value);

		char start[SKINNAMESIZE+1] = {0}; // Start of name, printed with normal color
		char match[SKINNAMESIZE+1] = {0}; // Matching part of name, highlighted in red
		const char *end = NULL; // End of the name, printed with normal color. Can be just a pointer into realname part

		CONS_Printf("\x86Multiple matches found:\n");

		for (i = 0; i < nummatches; ++i)
		{
			const char *realname = skins[matches[i]].realname;

			if (matches_pos[i] == 0)
				start[0] = 0;
			else
			{
				memcpy(start, realname, matches_pos[i]);
				start[matches_pos[i]] = 0;
			}

			memcpy(match, realname+matches_pos[i], query_length);
			match[matches_pos[i] + query_length] = 0;

			end = realname + matches_pos[i] + query_length;

			CONS_Printf("%s\x85%s\x80%s (%s)\n", start, match, end, skins[matches[i]].name);
		}

		CONS_Printf("\x86Try be more specific\n");
	}
	else
	{
		CONS_Printf("\x86Skin not found\n");
	}
}


/** Checks if a name (as received from another player) is okay.
  * A name is okay if it is no fewer than 1 and no more than ::MAXPLAYERNAME
  * chars long (not including NUL), it does not begin or end with a space,
  * it does not contain non-printing characters (according to isprint(), which
  * allows space), it does not start with a digit, and no other player is
  * currently using it.
  * \param name      Name to check.
  * \param playernum Player who wants the name, so we can check if they already
  *                  have it, and let them keep it if so.
  * \sa CleanupPlayerName, SetPlayerName, Got_NameAndColor
  * \author Graue <graue@oceanbase.org>
  */

static boolean AllowedPlayerNameChar(char ch)
{
	if (!isprint(ch) || ch == ';' || ch == '"' || (UINT8)(ch) >= 0x80)
		return false;

	return true;
}

static boolean EnsurePlayerNameIsGood(char *name, INT32 playernum)
{
	INT32 ix;

	if (strlen(name) == 0 || strlen(name) > MAXPLAYERNAME)
		return false; // Empty or too long.
	if (name[0] == ' ' || name[strlen(name)-1] == ' ')
		return false; // Starts or ends with a space.
	if (isdigit(name[0]))
		return false; // Starts with a digit.
	if (name[0] == '@' || name[0] == '~')
		return false; // Starts with an admin symbol.

	// Check if it contains a non-printing character.
	// Note: ANSI C isprint() considers space a printing character.
	// Also don't allow semicolons, since they are used as
	// console command separators.

	// Also, anything over 0x80 is disallowed too, since compilers love to
	// differ on whether they're printable characters or not.
	for (ix = 0; name[ix] != '\0'; ix++)
		if (!AllowedPlayerNameChar(name[ix]))
			return false;

	// Check if a player is currently using the name, case-insensitively.
	for (ix = 0; ix < MAXPLAYERS; ix++)
	{
		if (ix != playernum && playeringame[ix]
			&& strcasecmp(name, player_names[ix]) == 0)
		{
			// We shouldn't kick people out just because
			// they joined the game with the same name
			// as someone else -- modify the name instead.
			size_t len = strlen(name);

			// Recursion!
			// Slowly strip characters off the end of the
			// name until we no longer have a duplicate.
			if (len > 1)
			{
				name[len-1] = '\0';
				if (!EnsurePlayerNameIsGood(name, playernum))
					return false;
			}
			else if (len == 1) // Agh!
			{
				// Last ditch effort...
				sprintf(name, "%d", M_RandomKey(10));
				if (!EnsurePlayerNameIsGood(name, playernum))
					return false;
			}
			else
				return false;
		}
	}

	return true;
}

/** Cleans up a local player's name before sending a name change.
  * Spaces at the beginning or end of the name are removed. Then if the new
  * name is identical to another player's name, ignoring case, the name change
  * is canceled, and the name in cv_playername.value or cv_playername2.value
  * is restored to what it was before.
  *
  * We assume that if playernum is ::consoleplayer or ::secondarydisplayplayer
  * the console variable ::cv_playername or ::cv_playername2 respectively is
  * already set to newname. However, the player name table is assumed to
  * contain the old name.
  *
  * \param playernum Player number who has requested a name change.
  *                  Should be ::consoleplayer or ::secondarydisplayplayer.
  * \param newname   New name for that player; should already be in
  *                  ::cv_playername or ::cv_playername2 if player is the
  *                  console or secondary display player, respectively.
  * \sa cv_playername, cv_playername2, SendNameAndColor, SendNameAndColor2,
  *     SetPlayerName
  * \author Graue <graue@oceanbase.org>
  */
static void CleanupPlayerName(INT32 playernum, const char *newname)
{
	char *buf;
	char *p;
	char *tmpname = NULL;
	INT32 i;
	boolean namefailed = true;

	buf = Z_StrDup(newname);

	do
	{
		p = buf;

		while (*p == ' ')
			p++; // remove leading spaces

		if (strlen(p) == 0)
			break; // empty names not allowed

		if (isdigit(*p))
			break; // names starting with digits not allowed

		if (*p == '@' || *p == '~')
			break; // names that start with @ or ~ (admin symbols) not allowed

		tmpname = p;

		do
		{
			if (!AllowedPlayerNameChar(*p))
				break;
		}
		while (*++p) ;

		if (*p)/* bad char found */
			break;

		// Remove trailing spaces.
		p = &tmpname[strlen(tmpname)-1]; // last character
		while (*p == ' ' && p >= tmpname)
		{
			*p = '\0';
			p--;
		}

		if (strlen(tmpname) == 0)
			break; // another case of an empty name

		// Truncate name if it's too long (max MAXPLAYERNAME chars
		// excluding NUL).
		if (strlen(tmpname) > MAXPLAYERNAME)
			tmpname[MAXPLAYERNAME] = '\0';

		// Remove trailing spaces again.
		p = &tmpname[strlen(tmpname)-1]; // last character
		while (*p == ' ' && p >= tmpname)
		{
			*p = '\0';
			p--;
		}

		// no stealing another player's name
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (i != playernum && playeringame[i]
				&& strcasecmp(tmpname, player_names[i]) == 0)
			{
				break;
			}
		}

		if (i < MAXPLAYERS)
			break;

		// name is okay then
		namefailed = false;
	} while (0);

	if (namefailed)
		tmpname = player_names[playernum];

	// set consvars whether namefailed or not, because even if it succeeded,
	// spaces may have been removed
	if (playernum == consoleplayer)
		CV_StealthSet(&cv_playername, tmpname);
	else if (playernum == displayplayers[1] || (!netgame && playernum == 1))
		CV_StealthSet(&cv_playername2, tmpname);
	else if (playernum == displayplayers[2] || (!netgame && playernum == 2))
		CV_StealthSet(&cv_playername3, tmpname);
	else if (playernum == displayplayers[3] || (!netgame && playernum == 3))
		CV_StealthSet(&cv_playername4, tmpname);
	else I_Assert(((void)"CleanupPlayerName used on non-local player", 0));

	Z_Free(buf);
}

/** Sets a player's name, if it is good.
  * If the name is not good (indicating a modified or buggy client), it is not
  * set, and if we are the server in a netgame, the player responsible is
  * kicked with a consistency failure.
  *
  * This function prints a message indicating the name change, unless the game
  * is currently showing the intro, e.g. when processing autoexec.cfg.
  *
  * \param playernum Player number who has requested a name change.
  * \param newname   New name for that player. Should be good, but won't
  *                  necessarily be if the client is maliciously modified or
  *                  buggy.
  * \sa CleanupPlayerName, EnsurePlayerNameIsGood
  * \author Graue <graue@oceanbase.org>
  */
static void SetPlayerName(INT32 playernum, char *newname)
{
	if (EnsurePlayerNameIsGood(newname, playernum))
	{
		if (strcasecmp(newname, player_names[playernum]) != 0)
		{
			if (netgame)
				HU_AddChatText(va("\x82*%s renamed to %s", player_names[playernum], newname), false);

			player_name_changes[playernum]++;

			strcpy(player_names[playernum], newname);
			demo_extradata[playernum] |= DXD_NAME;
		}
	}
	else
	{
		CONS_Printf(M_GetText("Player %d sent a bad name change\n"), playernum+1);
		if (server && netgame)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
	}
}

UINT8 CanChangeSkin(INT32 playernum)
{
	// Of course we can change if we're not playing
	if (!Playing() || !addedtogame)
		return true;

	// Force skin in effect.
	if (client && (cv_forceskin.value != -1) && !(IsPlayerAdmin(playernum) && serverplayer == -1))
		return false;

	// Can change skin in intermission and whatnot.
	if (gamestate != GS_LEVEL)
		return true;

	// Server has skin change restrictions.
	if (cv_restrictskinchange.value)
	{
		if (gametype == GT_COOP)
			return true;

		// Can change skin during initial countdown.
		if (leveltime < starttime)
			return true;

		if (G_TagGametype())
		{
			// Can change skin during hidetime.
			if (leveltime < hidetime * TICRATE)
				return true;

			// IT players can always change skins to persue players hiding in character only locations.
			if (players[playernum].pflags & PF_TAGIT)
				return true;
		}

		if (players[playernum].spectator || players[playernum].playerstate == PST_DEAD || players[playernum].playerstate == PST_REBORN)
			return true;

		return false;
	}

	return true;
}

static void ForceAllSkins(INT32 forcedskin)
{
	INT32 i;
	for (i = 0; i < MAXPLAYERS; ++i)
	{
		if (!playeringame[i])
			continue;

		SetPlayerSkinByNum(i, forcedskin);

		// If it's me (or my brother (or my sister (or my trusty pet dog))), set appropriate skin value in cv_skin
		if (!dedicated) // But don't do this for dedicated servers, of course.
		{
			if (i == consoleplayer)
				CV_StealthSet(&cv_skin, skins[forcedskin].name);
			else if (i == displayplayers[1])
				CV_StealthSet(&cv_skin2, skins[forcedskin].name);
			else if (i == displayplayers[2])
				CV_StealthSet(&cv_skin3, skins[forcedskin].name);
			else if (i == displayplayers[3])
				CV_StealthSet(&cv_skin4, skins[forcedskin].name);
		}
	}
}

static INT32 snacpending = 0, snac2pending = 0, snac3pending = 0, snac4pending = 0, chmappending = 0;

// name, color, or skin has changed
//
static void SendNameAndColor(void)
{
	char buf[MAXPLAYERNAME+2];
	char *p;

	p = buf;

	// normal player colors
	if (G_GametypeHasTeams())
	{
		if (players[consoleplayer].ctfteam == 1 && cv_playercolor.value != skincolor_redteam)
			CV_StealthSetValue(&cv_playercolor, skincolor_redteam);
		else if (players[consoleplayer].ctfteam == 2 && cv_playercolor.value != skincolor_blueteam)
			CV_StealthSetValue(&cv_playercolor, skincolor_blueteam);
	}

	// never allow the color "none"
	if (!cv_playercolor.value)
	{
		if (players[consoleplayer].skincolor)
			CV_StealthSetValue(&cv_playercolor, players[consoleplayer].skincolor);
		else if (skins[players[consoleplayer].skin].prefcolor)
			CV_StealthSetValue(&cv_playercolor, skins[players[consoleplayer].skin].prefcolor);
		else
			CV_StealthSet(&cv_playercolor, cv_playercolor.defaultvalue);
	}
	
	// ditto for follower colour:
	if (!cv_followercolor.value)
		CV_StealthSet(&cv_followercolor, "Match"); // set it to "Match". I don't care about your stupidity!
		
	// We'll handle it later if we're not playing.
	if (!Playing())
		return;
	
	cv_follower.value = R_FollowerAvailable(cv_follower.string);
	if (cv_follower.value < 0)
	{
		CV_StealthSet(&cv_follower, "None");
		cv_follower.value = -1;
	}

	if (!strcmp(cv_playername.string, player_names[consoleplayer])
		&& cv_playercolor.value == players[consoleplayer].skincolor
		&& !strcmp(cv_skin.string, skins[players[consoleplayer].skin].name)
		&& !strcmp(cv_follower.string,
			(players[consoleplayer].followerskin < 0 ? "None" : followers[players[consoleplayer].followerskin].name))
		&& cv_followercolor.value == players[consoleplayer].followercolor)
		return;

	// If you're not in a netgame, merely update the skin, color, and name.
	if (!netgame)
	{
		INT32 foundskin;

		CleanupPlayerName(consoleplayer, cv_playername.zstring);
		strcpy(player_names[consoleplayer], cv_playername.zstring);

		players[consoleplayer].skincolor = cv_playercolor.value;
		
		players[consoleplayer].followercolor = cv_followercolor.value;

		if (players[consoleplayer].mo)
			players[consoleplayer].mo->color = players[consoleplayer].skincolor;
		
		if (cv_follower.value >= -1 && cv_follower.value != players[consoleplayer].followerskin)
			SetFollower(consoleplayer, cv_follower.value);

		if (metalrecording)
		{ // Metal Sonic is Sonic, obviously.
			SetPlayerSkinByNum(consoleplayer, 0);
			CV_StealthSet(&cv_skin, skins[0].name);
		}
		else if ((foundskin = R_SkinAvailable(cv_skin.string)) != -1)
		{
			//boolean notsame;

			cv_skin.value = foundskin;

			//notsame = (cv_skin.value != players[consoleplayer].skin);

			SetPlayerSkin(consoleplayer, cv_skin.string);
			CV_StealthSet(&cv_skin, skins[cv_skin.value].name);

			// SRB2Kart
			/*if (notsame)
			{
				CV_StealthSetValue(&cv_playercolor, skins[cv_skin.value].prefcolor);

				players[consoleplayer].skincolor = (cv_playercolor.value&0x3F) % MAXSKINCOLORS;

				if (players[consoleplayer].mo)
					players[consoleplayer].mo->color = (UINT8)players[consoleplayer].skincolor;
			}*/
		}
		else
		{
			cv_skin.value = players[consoleplayer].skin;
			CV_StealthSet(&cv_skin, skins[players[consoleplayer].skin].name);
			// will always be same as current
			SetPlayerSkin(consoleplayer, cv_skin.string);
		}

		return;
	}

	snacpending++;

	// Don't change name if muted
	if (player_name_changes[consoleplayer] >= MAXNAMECHANGES)
	{
		CV_StealthSet(&cv_playername, player_names[consoleplayer]);
		HU_AddChatText("\x85*You must wait to change your name again", false);
	}
	else if (cv_mute.value && !(server || IsPlayerAdmin(consoleplayer)))
		CV_StealthSet(&cv_playername, player_names[consoleplayer]);
	else // Cleanup name if changing it
		CleanupPlayerName(consoleplayer, cv_playername.zstring);

	// Don't change skin if the server doesn't want you to.
	if (!CanChangeSkin(consoleplayer))
		CV_StealthSet(&cv_skin, skins[players[consoleplayer].skin].name);

	// check if player has the skin loaded (cv_skin may have
	// the name of a skin that was available in the previous game)
	cv_skin.value = R_SkinAvailable(cv_skin.string);
	if (cv_skin.value < 0)
	{
		INT32 skinnum = players[consoleplayer].skin;
		CV_StealthSet(&cv_skin, skins[skinnum].name);
		cv_skin.value = skinnum;
	}

	// Finally write out the complete packet and send it off.
	WRITESTRINGN(p, cv_playername.zstring, MAXPLAYERNAME);
	WRITEUINT8(p, (UINT8)cv_playercolor.value);
	WRITEUINT16(p, (UINT16)cv_skin.value);
	WRITEINT32(p, (INT32)cv_follower.value);
	WRITEUINT8(p, (UINT8)cv_followercolor.value);
	SendNetXCmd(XD_NAMEANDCOLOR, buf, p - buf);
}

// splitscreen
static void SendNameAndColor2(void)
{
	INT32 secondplaya = -1;
	char buf[MAXPLAYERNAME+2];
	char *p;

	if (splitscreen < 1 && !botingame)
		return; // can happen if skin2/color2/name2 changed

	if (displayplayers[1] != consoleplayer)
		secondplaya = displayplayers[1];
	else if (!netgame) // HACK
		secondplaya = 1;

	if (secondplaya == -1)
		return;

	p = buf;

	// normal player colors
	if (G_GametypeHasTeams())
	{
		if (players[secondplaya].ctfteam == 1 && cv_playercolor2.value != skincolor_redteam)
			CV_StealthSetValue(&cv_playercolor2, skincolor_redteam);
		else if (players[secondplaya].ctfteam == 2 && cv_playercolor2.value != skincolor_blueteam)
			CV_StealthSetValue(&cv_playercolor2, skincolor_blueteam);
	}

	// never allow the color "none"
	if (!cv_playercolor2.value)
	{
		if (players[secondplaya].skincolor)
			CV_StealthSetValue(&cv_playercolor2, players[secondplaya].skincolor);
		else if (skins[players[secondplaya].skin].prefcolor)
			CV_StealthSetValue(&cv_playercolor2, skins[players[secondplaya].skin].prefcolor);
		else
			CV_StealthSet(&cv_playercolor2, cv_playercolor2.defaultvalue);
	}

	// ditto for follower colour:
	if (!cv_followercolor2.value)
		CV_StealthSet(&cv_followercolor2, "Match"); // set it to "Match". I don't care about your stupidity!
	
	// We'll handle it later if we're not playing.
	if (!Playing())
		return;
	
	cv_follower2.value = R_FollowerAvailable(cv_follower2.string);
	if (cv_follower2.value < 0)
	{
		CV_StealthSet(&cv_follower2, "None");
		cv_follower2.value = -1;
	}

	// If you're not in a netgame, merely update the skin, color, and name.
	if (botingame)
	{
		players[secondplaya].skincolor = botcolor;
		if (players[secondplaya].mo)
			players[secondplaya].mo->color = players[secondplaya].skincolor;
		SetPlayerSkinByNum(secondplaya, botskin-1);
		return;
	}
	else if (!netgame)
	{
		INT32 foundskin;

		CleanupPlayerName(secondplaya, cv_playername2.zstring);
		strcpy(player_names[secondplaya], cv_playername2.zstring);

		// don't use displayplayers[1]: the second player must be 1
		players[secondplaya].skincolor = cv_playercolor2.value;
		players[secondplaya].followercolor = cv_followercolor2.value;
		if (players[secondplaya].mo)
			players[secondplaya].mo->color = players[secondplaya].skincolor;
		
		if (cv_follower.value >= -1 && cv_follower2.value != players[secondplaya].followerskin)
			SetFollower(secondplaya, cv_follower2.value);

		if ((foundskin = R_SkinAvailable(cv_skin2.string)) != -1)
		{
			//boolean notsame;

			cv_skin2.value = foundskin;

			//notsame = (cv_skin2.value != players[secondplaya].skin);

			SetPlayerSkin(secondplaya, cv_skin2.string);

			// SRB2Kart
			/*if (notsame)
			{
				CV_StealthSetValue(&cv_playercolor2, skins[players[secondplaya].skin].prefcolor);

				players[secondplaya].skincolor = (cv_playercolor2.value&0x3F) % MAXSKINCOLORS;

				if (players[secondplaya].mo)
					players[secondplaya].mo->color = players[secondplaya].skincolor;
			}*/
		}
		else
		{
			cv_skin2.value = players[secondplaya].skin;
			CV_StealthSet(&cv_skin2, skins[players[secondplaya].skin].name);
			// will always be same as current
			SetPlayerSkin(secondplaya, cv_skin2.string);
		}
		return;
	}

	snac2pending++;

	// Don't change name if muted
	if (player_name_changes[displayplayers[1]] >= MAXNAMECHANGES)
	{
		CV_StealthSet(&cv_playername2, player_names[displayplayers[1]]);
		HU_AddChatText("\x85*You must wait to change your name again", false);
	}
	else if (cv_mute.value && !(server || IsPlayerAdmin(displayplayers[1])))
		CV_StealthSet(&cv_playername2, player_names[displayplayers[1]]);
	else // Cleanup name if changing it
		CleanupPlayerName(displayplayers[1], cv_playername2.zstring);

	// Don't change skin if the server doesn't want you to.
	if (!CanChangeSkin(displayplayers[1]))
		CV_StealthSet(&cv_skin2, skins[players[displayplayers[1]].skin].name);

	// check if player has the skin loaded (cv_skin2 may have
	// the name of a skin that was available in the previous game)
	cv_skin2.value = R_SkinAvailable(cv_skin2.string);
	if (cv_skin2.value < 0)
	{
		INT32 skinnum = players[displayplayers[1]].skin;
		CV_StealthSet(&cv_skin2, skins[skinnum].name);
		cv_skin2.value = skinnum;
	}

	// Finally write out the complete packet and send it off.
	WRITESTRINGN(p, cv_playername2.zstring, MAXPLAYERNAME);
	WRITEUINT8(p, (UINT8)cv_playercolor2.value);
	WRITEUINT16(p, (UINT16)cv_skin2.value);
	WRITEINT32(p, (INT32)cv_follower2.value);
	WRITEUINT8(p, (UINT8)cv_followercolor2.value);
	SendNetXCmd2(XD_NAMEANDCOLOR, buf, p - buf);
}

static void SendNameAndColor3(void)
{
	INT32 thirdplaya = -1;
	char buf[MAXPLAYERNAME+2];
	char *p;

	if (splitscreen < 2)
		return; // can happen if skin3/color3/name3 changed

	if (displayplayers[2] != consoleplayer)
		thirdplaya = displayplayers[2];
	else if (!netgame) // HACK
		thirdplaya = 2;

	if (thirdplaya == -1)
		return;

	p = buf;

	// normal player colors
	if (G_GametypeHasTeams())
	{
		if (players[thirdplaya].ctfteam == 1 && cv_playercolor3.value != skincolor_redteam)
			CV_StealthSetValue(&cv_playercolor3, skincolor_redteam);
		else if (players[thirdplaya].ctfteam == 2 && cv_playercolor3.value != skincolor_blueteam)
			CV_StealthSetValue(&cv_playercolor3, skincolor_blueteam);
	}

	// never allow the color "none"
	if (!cv_playercolor3.value)
	{
		if (players[thirdplaya].skincolor)
			CV_StealthSetValue(&cv_playercolor3, players[thirdplaya].skincolor);
		else if (skins[players[thirdplaya].skin].prefcolor)
			CV_StealthSetValue(&cv_playercolor3, skins[players[thirdplaya].skin].prefcolor);
		else
			CV_StealthSet(&cv_playercolor3, cv_playercolor3.defaultvalue);
	}
	
	// ditto for follower colour:
	if (!cv_followercolor3.value)
		CV_StealthSet(&cv_followercolor3, "Match"); // set it to "Match". I don't care about your stupidity!

	// We'll handle it later if we're not playing.
	if (!Playing())
		return;
	
	cv_follower3.value = R_FollowerAvailable(cv_follower3.string);
	if (cv_follower3.value < 0)
	{
		CV_StealthSet(&cv_follower3, "None");
		cv_follower3.value = -1;
	}
	
	// If you're not in a netgame, merely update the skin, color, and name.
	if (!netgame)
	{
		INT32 foundskin;

		CleanupPlayerName(thirdplaya, cv_playername3.zstring);
		strcpy(player_names[thirdplaya], cv_playername3.zstring);

		// don't use displayplayers[2]: the third player must be 2
		players[thirdplaya].skincolor = cv_playercolor3.value;
		players[thirdplaya].followercolor = cv_followercolor3.value;
		if (players[thirdplaya].mo)
			players[thirdplaya].mo->color = players[thirdplaya].skincolor;
		
		
		if (cv_follower3.value >= -1 && cv_follower3.value != players[thirdplaya].followerskin)
			SetFollower(thirdplaya, cv_follower3.value);

		if ((foundskin = R_SkinAvailable(cv_skin3.string)) != -1)
		{
			//boolean notsame;

			cv_skin3.value = foundskin;

			//notsame = (cv_skin3.value != players[thirdplaya].skin);

			SetPlayerSkin(thirdplaya, cv_skin3.string);

			// SRB2Kart
			/*if (notsame)
			{
				CV_StealthSetValue(&cv_playercolor3, skins[players[thirdplaya].skin].prefcolor);

				players[thirdplaya].skincolor = (cv_playercolor3.value&0x3F) % MAXSKINCOLORS;

				if (players[thirdplaya].mo)
					players[thirdplaya].mo->color = players[thirdplaya].skincolor;
			}*/
		}
		else
		{
			cv_skin3.value = players[thirdplaya].skin;
			CV_StealthSet(&cv_skin3, skins[players[thirdplaya].skin].name);
			// will always be same as current
			SetPlayerSkin(thirdplaya, cv_skin3.string);
		}
		return;
	}

	snac3pending++;

	// Don't change name if muted
	if (player_name_changes[displayplayers[2]] >= MAXNAMECHANGES)
	{
		CV_StealthSet(&cv_playername3, player_names[displayplayers[2]]);
		HU_AddChatText("\x85*You must wait to change your name again", false);
	}
	else if (cv_mute.value && !(server || IsPlayerAdmin(displayplayers[2])))
		CV_StealthSet(&cv_playername3, player_names[displayplayers[2]]);
	else // Cleanup name if changing it
		CleanupPlayerName(displayplayers[2], cv_playername3.zstring);

	// Don't change skin if the server doesn't want you to.
	if (!CanChangeSkin(displayplayers[2]))
		CV_StealthSet(&cv_skin3, skins[players[displayplayers[2]].skin].name);

	// check if player has the skin loaded (cv_skin3 may have
	// the name of a skin that was available in the previous game)
	cv_skin3.value = R_SkinAvailable(cv_skin3.string);
	if (cv_skin3.value < 0)
	{
		INT32 skinnum = players[displayplayers[2]].skin;
		CV_StealthSet(&cv_skin3, skins[skinnum].name);
		cv_skin3.value = skinnum;
	}

	// Finally write out the complete packet and send it off.
	WRITESTRINGN(p, cv_playername3.zstring, MAXPLAYERNAME);
	WRITEUINT8(p, (UINT8)cv_playercolor3.value);
	WRITEUINT16(p, (UINT16)cv_skin3.value);
	WRITEINT32(p, (INT32)cv_follower3.value);
	WRITEUINT8(p, (UINT8)cv_followercolor3.value);
	SendNetXCmd3(XD_NAMEANDCOLOR, buf, p - buf);
}

static void SendNameAndColor4(void)
{
	INT32 fourthplaya = -1;
	char buf[MAXPLAYERNAME+2];
	char *p;

	if (splitscreen < 3)
		return; // can happen if skin4/color4/name4 changed

	if (displayplayers[3] != consoleplayer)
		fourthplaya = displayplayers[3];
	else if (!netgame) // HACK
		fourthplaya = 3;

	if (fourthplaya == -1)
		return;

	p = buf;

	// normal player colors
	if (G_GametypeHasTeams())
	{
		if (players[fourthplaya].ctfteam == 1 && cv_playercolor4.value != skincolor_redteam)
			CV_StealthSetValue(&cv_playercolor4, skincolor_redteam);
		else if (players[fourthplaya].ctfteam == 2 && cv_playercolor4.value != skincolor_blueteam)
			CV_StealthSetValue(&cv_playercolor4, skincolor_blueteam);
	}

	// never allow the color "none"
	if (!cv_playercolor4.value)
	{
		if (players[fourthplaya].skincolor)
			CV_StealthSetValue(&cv_playercolor4, players[fourthplaya].skincolor);
		else if (skins[players[fourthplaya].skin].prefcolor)
			CV_StealthSetValue(&cv_playercolor4, skins[players[fourthplaya].skin].prefcolor);
		else
			CV_StealthSet(&cv_playercolor4, cv_playercolor4.defaultvalue);
	}

	// ditto for follower colour:
	if (!cv_followercolor4.value)
		CV_StealthSet(&cv_followercolor4, "Match"); // set it to "Match". I don't care about your stupidity!
	
	// We'll handle it later if we're not playing.
	if (!Playing())
		return;

	cv_follower4.value = R_FollowerAvailable(cv_follower4.string);
	if (cv_follower4.value < 0)
	{
		CV_StealthSet(&cv_follower4, "None");
		cv_follower4.value = -1;
	}
	
	// If you're not in a netgame, merely update the skin, color, and name.
	if (botingame)
	{
		players[fourthplaya].skincolor = botcolor;
		if (players[fourthplaya].mo)
			players[fourthplaya].mo->color = players[fourthplaya].skincolor;
		SetPlayerSkinByNum(fourthplaya, botskin-1);
		return;
	}
	else if (!netgame)
	{
		INT32 foundskin;

		CleanupPlayerName(fourthplaya, cv_playername4.zstring);
		strcpy(player_names[fourthplaya], cv_playername4.zstring);

		// don't use displayplayers[3]: the second player must be 4
		players[fourthplaya].skincolor = cv_playercolor4.value;
		players[fourthplaya].followercolor = cv_followercolor4.value;
		if (players[fourthplaya].mo)
			players[fourthplaya].mo->color = players[fourthplaya].skincolor;
		
		if (cv_follower.value >= -1 && cv_follower.value != players[consoleplayer].followerskin)
			SetFollower(consoleplayer, cv_follower.value);

		if ((foundskin = R_SkinAvailable(cv_skin4.string)) != -1)
		{
			//boolean notsame;

			cv_skin4.value = foundskin;

			//notsame = (cv_skin4.value != players[fourthplaya].skin);

			SetPlayerSkin(fourthplaya, cv_skin4.string);

			// SRB2Kart
			/*if (notsame)
			{
				CV_StealthSetValue(&cv_playercolor4, skins[players[fourthplaya].skin].prefcolor);

				players[fourthplaya].skincolor = (cv_playercolor4.value&0x3F) % MAXSKINCOLORS;

				if (players[fourthplaya].mo)
					players[fourthplaya].mo->color = players[fourthplaya].skincolor;
			}*/
		}
		else
		{
			cv_skin4.value = players[fourthplaya].skin;
			CV_StealthSet(&cv_skin4, skins[players[fourthplaya].skin].name);
			// will always be same as current
			SetPlayerSkin(fourthplaya, cv_skin4.string);
		}
		return;
	}

	snac4pending++;

	// Don't change name if muted
	if (player_name_changes[displayplayers[3]] >= MAXNAMECHANGES)
	{
		CV_StealthSet(&cv_playername4, player_names[displayplayers[3]]);
		HU_AddChatText("\x85*You must wait to change your name again", false);
	}
	else if (cv_mute.value && !(server || IsPlayerAdmin(displayplayers[3])))
		CV_StealthSet(&cv_playername4, player_names[displayplayers[3]]);
	else // Cleanup name if changing it
		CleanupPlayerName(displayplayers[3], cv_playername4.zstring);

	// Don't change skin if the server doesn't want you to.
	if (!CanChangeSkin(displayplayers[3]))
		CV_StealthSet(&cv_skin4, skins[players[displayplayers[3]].skin].name);

	// check if player has the skin loaded (cv_skin4 may have
	// the name of a skin that was available in the previous game)
	cv_skin4.value = R_SkinAvailable(cv_skin4.string);
	if (cv_skin4.value < 0)
	{
		INT32 skinnum = players[displayplayers[3]].skin;
		CV_StealthSet(&cv_skin4, skins[skinnum].name);
		cv_skin4.value = skinnum;
	}

	// Finally write out the complete packet and send it off.
	WRITESTRINGN(p, cv_playername4.zstring, MAXPLAYERNAME);
	WRITEUINT8(p, (UINT8)cv_playercolor4.value);
	WRITEUINT16(p, (UINT16)cv_skin4.value);
	WRITEINT32(p, (INT32)cv_follower4.value);
	WRITEUINT8(p, (UINT8)cv_followercolor4.value);
	SendNetXCmd4(XD_NAMEANDCOLOR, buf, p - buf);
}

static void Got_NameAndColor(UINT8 **cp, INT32 playernum)
{
	player_t *p = &players[playernum];
	char name[MAXPLAYERNAME+1];
	UINT8 color, followercolor; 
	UINT16 skin;
	INT32 follower;
	

#ifdef PARANOIA
	if (playernum < 0 || playernum > MAXPLAYERS)
		I_Error("There is no player %d!", playernum);
#endif

	if (playernum == consoleplayer)
		snacpending--; // TODO: make snacpending an array instead of 4 separate vars?
	else if (playernum == displayplayers[1])
		snac2pending--;
	else if (playernum == displayplayers[2])
		snac3pending--;
	else if (playernum == displayplayers[3])
		snac4pending--;

#ifdef PARANOIA
	if (snacpending < 0 || snac2pending < 0 || snac3pending < 0 || snac4pending < 0)
		I_Error("snacpending negative!");
#endif

	READSTRINGN(*cp, name, MAXPLAYERNAME);
	color = READUINT8(*cp);
	skin = READUINT16(*cp);
	follower = READINT32(*cp);
	followercolor = READUINT8(*cp);

	// set name
	if (player_name_changes[playernum] < MAXNAMECHANGES)
	{
		if (strcasecmp(player_names[playernum], name) != 0)
			SetPlayerName(playernum, name);
	}

	// set color
	p->skincolor = color % MAXSKINCOLORS;
	if (p->mo)
		p->mo->color = (UINT8)p->skincolor;
	demo_extradata[playernum] |= DXD_COLOR;

	// normal player colors
	if (server && (p != &players[consoleplayer] && p != &players[displayplayers[1]]
		&& p != &players[displayplayers[2]] && p != &players[displayplayers[3]]))
	{
		boolean kick = false;

		// team colors
		if (G_GametypeHasTeams())
		{
			if (p->ctfteam == 1 && p->skincolor != skincolor_redteam)
				kick = true;
			else if (p->ctfteam == 2 && p->skincolor != skincolor_blueteam)
				kick = true;
		}

		// don't allow color "none"
		if (!p->skincolor)
			kick = true;

		if (kick)
		{
			UINT8 buf[2];
			CONS_Alert(CONS_WARNING, M_GetText("Illegal color change received from %s (team: %d), color: %d)\n"), player_names[playernum], p->ctfteam, p->skincolor);

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
			return;
		}
	}

	// set skin
	if (cv_forceskin.value >= 0 && (netgame || multiplayer)) // Server wants everyone to use the same player
	{
		const INT32 forcedskin = cv_forceskin.value;
		SetPlayerSkinByNum(playernum, forcedskin);

		if (playernum == consoleplayer)
			CV_StealthSet(&cv_skin, skins[forcedskin].name);
		else if (playernum == displayplayers[1])
			CV_StealthSet(&cv_skin2, skins[forcedskin].name);
		else if (playernum == displayplayers[2])
			CV_StealthSet(&cv_skin3, skins[forcedskin].name);
		else if (playernum == displayplayers[3])
			CV_StealthSet(&cv_skin4, skins[forcedskin].name);
	}
	else
		SetPlayerSkinByNum(playernum, skin);
	
	
	// set follower colour:
	// Don't bother doing garbage and kicking if we receive None, this is both silly and a waste of time, this will be handled properly in P_HandleFollower.
	p->followercolor = followercolor;

	SetFollower(playernum, follower);

#ifdef HAVE_DISCORDRPC
	if (playernum == consoleplayer)
		DRPC_UpdatePresence();
#endif
}

void SendWeaponPref(void)
{
	UINT8 buf[1];

	buf[0] = 0;
	if (cv_flipcam.value)
		buf[0] |= 1;
	if (cv_analog.value)
		buf[0] |= 2;
	SendNetXCmd(XD_WEAPONPREF, buf, 1);
}

void SendWeaponPref2(void)
{
	UINT8 buf[1];

	buf[0] = 0;
	if (cv_flipcam2.value)
		buf[0] |= 1;
	if (cv_analog2.value)
		buf[0] |= 2;
	SendNetXCmd2(XD_WEAPONPREF, buf, 1);
}

void SendWeaponPref3(void)
{
	UINT8 buf[1];

	buf[0] = 0;
	if (cv_flipcam3.value)
		buf[0] |= 1;
	if (cv_analog3.value)
		buf[0] |= 2;
	SendNetXCmd3(XD_WEAPONPREF, buf, 1);
}

void SendWeaponPref4(void)
{
	UINT8 buf[1];

	buf[0] = 0;
	if (cv_flipcam4.value)
		buf[0] |= 1;
	if (cv_analog4.value)
		buf[0] |= 2;
	SendNetXCmd4(XD_WEAPONPREF, buf, 1);
}

static void Got_WeaponPref(UINT8 **cp,INT32 playernum)
{
	UINT8 prefs = READUINT8(*cp);

	players[playernum].pflags &= ~(PF_FLIPCAM|PF_ANALOGMODE);
	if (prefs & 1)
		players[playernum].pflags |= PF_FLIPCAM;
	if (prefs & 2)
		players[playernum].pflags |= PF_ANALOGMODE;
}

void D_SendPlayerConfig(void)
{
	SendNameAndColor();
	if (splitscreen || botingame)
		SendNameAndColor2();
	if (splitscreen > 1)
		SendNameAndColor3();
	if (splitscreen > 2)
		SendNameAndColor4();
	SendWeaponPref();
	if (splitscreen)
		SendWeaponPref2();
	if (splitscreen > 1)
		SendWeaponPref3();
	if (splitscreen > 2)
		SendWeaponPref4();
}

// Only works for displayplayer, sorry!
static void Command_ResetCamera_f(void)
{
	P_ResetCamera(&players[displayplayers[0]], &camera[0]);
}

/* Consider replacing nametonum with this */
static INT32 LookupPlayer(const char *s)
{
	INT32 playernum;

	if (*s == '0')/* clever way to bypass atoi */
		return 0;

	if (( playernum = atoi(s) ))
	{
		playernum = max(min(playernum, MAXPLAYERS-1), 0);/* not out of range */
		return playernum;
	}

	for (playernum = 0; playernum < MAXPLAYERS; ++playernum)
	{
		/* Match name case-insensitively: fully, or partially the start. */
		if (playeringame[playernum])
			if (strnicmp(player_names[playernum], s, strlen(s)) == 0)
		{
			return playernum;
		}
	}
	return -1;
}

static INT32 FindPlayerByPlace(INT32 place)
{
	INT32 playernum;
	for (playernum = 0; playernum < MAXPLAYERS; ++playernum)
		if (playeringame[playernum])
	{
		if (players[playernum].kartstuff[k_position] == place)
		{
			return playernum;
		}
	}
	return -1;
}

//
// GetViewablePlayerPlaceRange
// Return in first and last, that player available to view, sorted by placement
// in the race.
//
static void GetViewablePlayerPlaceRange(INT32 *first, INT32 *last)
{
	INT32 i;
	INT32 place;

	(*first) = MAXPLAYERS;
	(*last) = 0;

	for (i = 0; i < MAXPLAYERS; ++i)
		if (G_CouldView(i))
	{
		place = players[i].kartstuff[k_position];
		if (place < (*first))
			(*first) = place;
		if (place > (*last))
			(*last) = place;
	}
}

#define PRINTVIEWPOINT( pre,suf ) \
	CONS_Printf(pre"viewing \x84(%d) \x83%s\x80"suf".\n",\
			(*displayplayerp), player_names[(*displayplayerp)]);
static void Command_View_f(void)
{
	INT32 *displayplayerp;
	INT32 olddisplayplayer;
	int viewnum;
	const char *playerparam;
	INT32 placenum;
	INT32 playernum;
	INT32 firstplace, lastplace;
	char c;
	/* easy peasy */
	c = COM_Argv(0)[strlen(COM_Argv(0))-1];/* may be digit */
	switch (c)
	{
		case '2': viewnum = 2; break;
		case '3': viewnum = 3; break;
		case '4': viewnum = 4; break;
		default:  viewnum = 1;
	}

	if (viewnum > 1 && !( multiplayer && demo.playback ))
	{
		CONS_Alert(CONS_NOTICE,
				"You must be viewing a multiplayer replay to use this.\n");
		return;
	}

	if (demo.freecam)
		return;

	displayplayerp = &displayplayers[viewnum-1];

	if (COM_Argc() > 1)/* switch to player */
	{
		playerparam = COM_Argv(1);
		if (playerparam[0] == '#')/* search by placement */
		{
			placenum = atoi(&playerparam[1]);
			playernum = FindPlayerByPlace(placenum);
			if (playernum == -1 || !G_CouldView(playernum))
			{
				GetViewablePlayerPlaceRange(&firstplace, &lastplace);
				if (playernum == -1)
				{
					CONS_Alert(CONS_WARNING, "There is no player in that place! ");
				}
				else
				{
					CONS_Alert(CONS_WARNING,
							"That player cannot be viewed currently! "
							"The first player that you can view is \x82#%d\x80; ",
							firstplace);
				}
				CONS_Printf("Last place is \x82#%d\x80.\n", lastplace);
				return;
			}
		}
		else
		{
			if (( playernum = LookupPlayer(COM_Argv(1)) ) == -1)
			{
				CONS_Alert(CONS_WARNING, "There is no player by that name!\n");
				return;
			}
			if (!playeringame[playernum])
			{
				CONS_Alert(CONS_WARNING, "There is no player using that slot!\n");
				return;
			}
		}

		olddisplayplayer = (*displayplayerp);
		G_ResetView(viewnum, playernum, false);

		/* The player we wanted was corrected to who it already was. */
		if ((*displayplayerp) == olddisplayplayer)
			return;

		if ((*displayplayerp) != playernum)/* differ parameter */
		{
			/* skipped some */
			CONS_Alert(CONS_NOTICE, "That player cannot be viewed currently.\n");
			PRINTVIEWPOINT ("Now "," instead")
		}
		else
			PRINTVIEWPOINT ("Now ",)
	}
	else/* print current view */
	{
		if (splitscreen < viewnum-1)/* We can't see those guys! */
			return;
		PRINTVIEWPOINT ("Currently ",)
	}
}
#undef PRINTVIEWPOINT

static void Command_SetViews_f(void)
{
	UINT8 splits;
	UINT8 newsplits;

	if (!( demo.playback && multiplayer ))
	{
		CONS_Alert(CONS_NOTICE,
				"You must be viewing a multiplayer replay to use this.\n");
		return;
	}

	if (COM_Argc() != 2)
	{
		CONS_Printf("setviews <views>: set the number of split screens\n");
		return;
	}

	splits = splitscreen+1;

	newsplits = atoi(COM_Argv(1));
	newsplits = min(max(newsplits, 1), 4);
	if (newsplits > splits)
		G_AdjustView(newsplits, 0, true);
	else
	{
		splitscreen = newsplits-1;
		R_ExecuteSetViewSize();
	}
}

// ========================================================================

// play a demo, add .lmp for external demos
// eg: playdemo demo1 plays the internal game demo
//
// UINT8 *demofile; // demo file buffer
static void Command_Playdemo_f(void)
{
	char name[256];

	if (COM_Argc() < 2)
	{
		CONS_Printf("playdemo <demoname> [-addfiles / -force]:\n");
		CONS_Printf(M_GetText(
					"Play back a demo file. The full path from your Kart directory must be given.\n\n"

					"* With \"-addfiles\", any required files are added from a list contained within the demo file.\n"
					"* With \"-force\", the demo is played even if the necessary files have not been added.\n"));
		return;
	}

	if (netgame)
	{
		CONS_Printf(M_GetText("You can't play a demo while in a netgame.\n"));
		return;
	}

	// disconnect from server here?
	if (demo.playback)
		G_StopDemo();
	if (metalplayback)
		G_StopMetalDemo();

	// open the demo file
	strcpy(name, COM_Argv(1));
	// dont add .lmp so internal game demos can be played

	CONS_Printf(M_GetText("Playing back demo '%s'.\n"), name);

	demo.loadfiles = strcmp(COM_Argv(2), "-addfiles") == 0;
	demo.ignorefiles = strcmp(COM_Argv(2), "-force") == 0;

	// Internal if no extension, external if one exists
	// If external, convert the file name to a path in SRB2's home directory
	if (FIL_CheckExtension(name))
		G_DoPlayDemo(va("%s"PATHSEP"%s", srb2home, name));
	else
		G_DoPlayDemo(name);
}

static void Command_Timedemo_f(void)
{
	char name[256];

	if (COM_Argc() != 2)
	{
		CONS_Printf(M_GetText("timedemo <demoname>: time a demo\n"));
		return;
	}

	if (netgame)
	{
		CONS_Printf(M_GetText("You can't play a demo while in a netgame.\n"));
		return;
	}

	// disconnect from server here?
	if (demo.playback)
		G_StopDemo();
	if (metalplayback)
		G_StopMetalDemo();

	// open the demo file
	strcpy (name, COM_Argv(1));
	// dont add .lmp so internal game demos can be played

	CONS_Printf(M_GetText("Timing demo '%s'.\n"), name);

	G_TimeDemo(name);
}

// stop current demo
static void Command_Stopdemo_f(void)
{
	G_CheckDemoStatus();
	CONS_Printf(M_GetText("Stopped demo.\n"));
}

static void Command_StartMovie_f(void)
{
	M_StartMovie();
}

static void Command_StopMovie_f(void)
{
	M_StopMovie();
}

INT32 mapchangepending = 0;

tic_t driftsparkGrowTimer[MAXPLAYERS];

/** Runs a map change.
  * The supplied data are assumed to be good. If provided by a user, they will
  * have already been checked in Command_Map_f().
  *
  * Do \b NOT call this function directly from a menu! M_Responder() is called
  * from within the event processing loop, and this function calls
  * SV_SpawnServer(), which calls CL_ConnectToServer(), which gives you "Press
  * ESC to abort", which calls I_GetKey(), which adds an event. In other words,
  * 63 old events will get reexecuted, with ridiculous results. Just don't do
  * it (without setting delay to 1, which is the current solution).
  *
  * \param mapnum          Map number to change to.
  * \param gametype        Gametype to switch to.
  * \param pencoremode     Is this 'Encore Mode'?
  * \param resetplayers    1 to reset player scores and lives and such, 0 not to.
  * \param delay           Determines how the function will be executed: 0 to do
  *                        it all right now (must not be done from a menu), 1 to
  *                        do step one and prepare step two, 2 to do step two.
  * \param skipprecutscene To skip the precutscence or not?
  * \sa D_GameTypeChanged, Command_Map_f
  * \author Graue <graue@oceanbase.org>
  */
void D_MapChange(INT32 mapnum, INT32 newgametype, boolean pencoremode, boolean resetplayers, INT32 delay, boolean skipprecutscene, boolean FLS)
{
	static char buf[2+MAX_WADPATH+1+4];
	static char *buf_p = buf;

	// The supplied data are assumed to be good.
	I_Assert(delay >= 0 && delay <= 2);

	CONS_Debug(DBG_GAMELOGIC, "Map change: mapnum=%d gametype=%d encoremode=%d resetplayers=%d delay=%d skipprecutscene=%d\n",
	           mapnum, newgametype, pencoremode, resetplayers, delay, skipprecutscene);

	// just gonna hack in a timer reset here...
	memset(driftsparkGrowTimer, 0, sizeof(driftsparkGrowTimer));

	if (netgame || multiplayer)
		FLS = false;

	if (delay != 2)
	{
		UINT8 flags = 0;
		const char *mapname = G_BuildMapName(mapnum);

		I_Assert(W_CheckNumForName(mapname) != LUMPERROR);

		buf_p = buf;
		if (pencoremode)
			flags |= 1;
		if (!resetplayers)
			flags |= 1<<1;
		if (skipprecutscene)
			flags |= 1<<2;
		if (FLS)
			flags |= 1<<3;
		WRITEUINT8(buf_p, flags);

		// new gametype value
		WRITEUINT8(buf_p, newgametype);

		WRITESTRINGN(buf_p, mapname, MAX_WADPATH);
	}

	if (delay == 1)
		mapchangepending = 1;
	else
	{
		mapchangepending = 0;
		// spawn the server if needed
		// reset players if there is a new one
		if (!IsPlayerAdmin(consoleplayer))
		{
			if (SV_SpawnServer())
				buf[0] &= ~(1<<1);
			if (!Playing()) // you failed to start a server somehow, so cancel the map change
				return;
		}

		// Kick bot from special stages
		if (botskin)
		{
			if (G_IsSpecialStage(mapnum))
			{
				if (botingame)
				{
					//CL_RemoveSplitscreenPlayer();
					botingame = false;
					playeringame[1] = false;
				}
			}
			else if (!botingame)
			{
				//CL_AddSplitscreenPlayer();
				botingame = true;
				displayplayers[1] = 1;
				playeringame[1] = true;
				players[1].bot = 1;
				SendNameAndColor2();
			}
		}

		chmappending++;
		if (netgame)
			WRITEUINT32(buf_p, M_RandomizedSeed()); // random seed
		SendNetXCmd(XD_MAP, buf, buf_p - buf);
	}
}

void D_SetupVote(void)
{
	UINT8 buf[13*2]; // twelve UINT16 maps (at twice the width of a UINT8), and two gametypes
	UINT8 *p = buf;
	INT32 i;
	UINT8 gt = (cv_kartgametypepreference.value == -1) ? gametype : cv_kartgametypepreference.value;
	//UINT8 secondgt = G_SometimesGetDifferentGametype(gt);
	UINT8 secondgt = 0;
	INT16 votebuffer[4] = {-1,-1,-1,0};

	if (G_RaceGametype() && cv_kartencore.value)
	{
		if (cv_encorevotes.value == 0)
		{
			secondgt = 0x80;
		}
		//else
		//{
		//	gt = 0x80;
		//	secondgt = G_SometimesGetDifferentGametype(gt);
		//}
	}
	else
	{
		secondgt = G_SometimesGetDifferentGametype(gt);
	}

	if (G_RaceGametype())
	{
		//if (cv_encorevotes.value == 1)
		//{
		//	gt |= ( secondgt & 0x80 );
		//	secondgt &= ~(0x80);
		//}
	}
	else
	{
		if (cv_lessbattlevotes.value)
		{
			gt = GT_RACE;
			secondgt = GT_MATCH;
		}
	}

	
	if (cv_kartencore.value && cv_encorevotes.value == 1 && gt == GT_RACE)
		WRITEUINT8(p, (gt|0x80));
	else
		WRITEUINT8(p, gt);
	WRITEUINT8(p, secondgt);
	secondgt &= ~0x80;

	for (i = 0; i < VOTEROWSADDSONE; i++)
	{
		UINT16 m;
		UINT16 hellpick = 0;

		hellpick = ((i == ((VOTEROWS) + 1) ) ? 2 : 0);

		if (i == VOTEROWS)
			hellpick = 1;

		if (i == 2) // sometimes a different gametype
			m = G_RandMap(G_TOLFlag(secondgt), prevmap, false, 0, true, votebuffer);
		else if (i >= VOTEROWS) // unknown-random and force-unknown MAP HELL
			m = G_RandMap(G_TOLFlag(gt), prevmap, false, hellpick, (i < VOTEROWSADDSONE), votebuffer); // let's TRY to make this simpler
		else
			m = G_RandMap(G_TOLFlag(gt), prevmap, false, 0, true, votebuffer);
		if (i < VOTEROWS)
			votebuffer[min(i, 2)] = m; // min() is a dumb workaround for gcc 4.4 array-bounds error
		WRITEUINT16(p, m);
	}

	SendNetXCmd(XD_SETUPVOTE, buf, p - buf);
}

void D_ModifyClientVote(SINT8 voted, UINT8 splitplayer)
{
	char buf[2];
	char *p = buf;

	WRITESINT8(p, voted);
	WRITEUINT8(p, splitplayer);
	SendNetXCmd(XD_MODIFYVOTE, &buf, 2);
}

void D_PickVote(void)
{
	char buf[2];
	char* p = buf;
	SINT8 temppicks[MAXPLAYERS];
	SINT8 templevels[MAXPLAYERS];
	SINT8 votecompare = -1;
	UINT8 numvotes = 0, key = 0;
	INT32 i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || players[i].spectator)
			continue;
		if (votes[i] != -1)
		{
			temppicks[numvotes] = i;
			templevels[numvotes] = votes[i];
			numvotes++;
			if (votecompare == -1)
				votecompare = votes[i];
		}
	}

	key = M_RandomKey(numvotes);

	if (numvotes > 0)
	{
		WRITESINT8(p, temppicks[key]);
		WRITESINT8(p, templevels[key]);
	}
	else
	{
		WRITESINT8(p, -1);
		WRITESINT8(p, 0);
	}

	SendNetXCmd(XD_PICKVOTE, &buf, 2);
}

static char *
ConcatCommandArgv (int start, int end)
{
	char *final;

	size_t size;

	int i;
	char *p;

	size = 0;

	for (i = start; i < end; ++i)
	{
		/*
		one space after each argument, but terminating
		character on final argument
		*/
		size += strlen(COM_Argv(i)) + 1;
	}

	final = ZZ_Alloc(size);
	p = final;

	--end;/* handle the final argument separately */
	for (i = start; i < end; ++i)
	{
		p += sprintf(p, "%s ", COM_Argv(i));
	}
	/* at this point "end" is actually the last argument's position */
	strcpy(p, COM_Argv(end));

	return final;
}


static tic_t last_map_cmd = 0;
//
// Warp to map code.
// Called either from map <mapname> console command, or idclev cheat.
//
// Largely rewritten by James.
//
// Added small cooldown which prevents from spamming map command and crashing
// game. It is not wery good fix but oh well i can't find yet where are mobj's
// freed/allocated between map loads and why they crash game - Indev
//
static void Command_Map_f(void)
{
	if (I_GetTime() - last_map_cmd < 20) {
		CONS_Alert(CONS_WARNING, "Map command is used too frequently!\n");
		return;
	}

	last_map_cmd = I_GetTime();

	size_t first_option;
	size_t option_force;
	size_t option_gametype;
	size_t option_encore;
	const char *gametypename;
	boolean newresetplayers;

	boolean mustmodifygame;

	INT32 newmapnum;

	char   *    mapname;
	char   *realmapname = NULL;

	INT32   newgametype   = gametype;
	boolean newencoremode = cv_kartencore.value;

	INT32 d;

	if (client && !IsPlayerAdmin(consoleplayer))
	{
		CONS_Printf(M_GetText("Only the server or a remote admin can use this.\n"));
		return;
	}

	option_force    =   COM_CheckPartialParm("-f");
	option_gametype =   COM_CheckPartialParm("-g");
	option_encore   =   COM_CheckPartialParm("-e");
	newresetplayers = ! COM_CheckParm("-noresetplayers");

	mustmodifygame = !( netgame || multiplayer || majormods );

	if (mustmodifygame && !option_force)
	{
		/* May want to be more descriptive? */
		CONS_Printf(M_GetText("Sorry, level change disabled in single player.\n"));
		return;
	}

	if (!newresetplayers && !cv_debug)
	{
		CONS_Printf(M_GetText("DEVMODE must be enabled.\n"));
		return;
	}

	if (option_gametype)
	{
		if (!multiplayer)
		{
			CONS_Printf(M_GetText(
						"You can't switch gametypes in single player!\n"));
			return;
		}
		else if (COM_Argc() < option_gametype + 2)/* no argument after? */
		{
			CONS_Alert(CONS_ERROR,
					"No gametype name follows parameter '%s'.\n",
					COM_Argv(option_gametype));
			return;
		}
	}

	if (!( first_option = COM_FirstOption() ))
		first_option = COM_Argc();

	if (first_option < 2)
	{
		/* I'm going over the fucking lines and I DON'T CAREEEEE */
		CONS_Printf("map <name / [MAP]code / number> [-gametype <type>] [-encore] [-force]:\n");
		CONS_Printf(M_GetText(
					"Warp to a map, by its name, two character code, with optional \"MAP\" prefix, or by its number (though why would you).\n"
					"All parameters are case-insensitive and may be abbreviated.\n"));
		return;
	}

	mapname = ConcatCommandArgv(1, first_option);

	newmapnum = G_FindMapByNameOrCode(mapname, &realmapname);

	if (newmapnum == 0)
	{
		CONS_Alert(CONS_ERROR, M_GetText("Could not find any map described as '%s'.\n"), mapname);
		Z_Free(mapname);
		return;
	}

	if (mustmodifygame && option_force)
	{
		G_SetGameModified(false, true);
	}

	// new gametype value
	// use current one by default
	if (option_gametype)
	{
		gametypename = COM_Argv(option_gametype + 1);

		newgametype = G_GetGametypeByName(gametypename);

		if (newgametype == -1) // reached end of the list with no match
		{
			/* Did they give us a gametype number? That's okay too! */
			if (isdigit(gametypename[0]))
			{
				d = atoi(gametypename);
				if (d >= 0 && d < NUMGAMETYPES)
					newgametype = d;
				else
				{
					CONS_Alert(CONS_ERROR,
							"Gametype number %d is out of range. Use a number between"
							" 0 and %d inclusive. ...Or just use the name. :v\n",
							d,
							NUMGAMETYPES-1);
					Z_Free(realmapname);
					Z_Free(mapname);
					return;
				}
			}
			else
			{
				CONS_Alert(CONS_ERROR,
						"'%s' is not a gametype.\n",
						gametypename);
				Z_Free(realmapname);
				Z_Free(mapname);
				return;
			}
		}
	}

	if (!option_force && newgametype == gametype) // SRB2Kart
		newresetplayers = false; // if not forcing and gametypes is the same

	// don't use a gametype the map doesn't support
	if (cv_debug || option_force || cv_skipmapcheck.value)
		fromlevelselect = false; // The player wants us to trek on anyway.  Do so.
	// G_TOLFlag handles both multiplayer gametype and ignores it for !multiplayer
	else
	{
		if (!(mapheaderinfo[newmapnum-1]->typeoflevel & G_TOLFlag(newgametype)))
		{
			CONS_Alert(CONS_WARNING, M_GetText("Course %s (%s) doesn't support %s mode!\n(Use -force to override)\n"), realmapname, G_BuildMapName(newmapnum),
				(multiplayer ? gametype_cons_t[newgametype].strvalue : "Single Player"));
			Z_Free(realmapname);
			Z_Free(mapname);
			return;
		}
	}

	// Prevent warping to locked levels
	// ... unless you're in a dedicated server.  Yes, technically this means you can view any level by
	// running a dedicated server and joining it yourself, but that's better than making dedicated server's
	// lives hell.
	if (!dedicated && M_MapLocked(newmapnum))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You need to unlock this level before you can warp to it!\n"));
		Z_Free(realmapname);
		Z_Free(mapname);
		return;
	}

	if (option_encore)
	{
		newencoremode = ! newencoremode;
		if (! M_SecretUnlocked(SECRET_ENCORE) && newencoremode)
		{
			CONS_Alert(CONS_NOTICE, M_GetText("You haven't unlocked Encore Mode yet!\n"));
			return;
		}
	}

	// spend atleast 35 seconds in one map
	if (cv_demochangemap.value && demo.recording && demo.savemode != DSM_NOTSAVING && (timeinmap > 1463) && ((cv_demochangemap.value == 2 && newmapnum == gamemap) || newmapnum != gamemap))
		G_SaveDemo();

	fromlevelselect = false;
	D_MapChange(newmapnum, newgametype, newencoremode, newresetplayers, 0, false, false);

	Z_Free(realmapname);
}

/** Receives a map command and changes the map.
  *
  * \param cp        Data buffer.
  * \param playernum Player number responsible for the message. Should be
  *                  ::serverplayer or ::adminplayer.
  * \sa D_MapChange
  */
static void Got_Mapcmd(UINT8 **cp, INT32 playernum)
{
	char mapname[MAX_WADPATH+1];
	UINT8 flags;
	INT32 resetplayer = 1, lastgametype;
	UINT8 skipprecutscene, FLS;
	boolean pencoremode;
	//INT16 mapnumber;

	forceresetplayers = deferencoremode = false;

	if (playernum != serverplayer && !IsPlayerAdmin(playernum))
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal map change received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	if (chmappending)
		chmappending--;

	flags = READUINT8(*cp);

	pencoremode = ((flags & 1) != 0);

	resetplayer = ((flags & (1<<1)) == 0);

	lastgametype = gametype;
	gametype = READUINT8(*cp);

	if (gametype < 0 || gametype >= NUMGAMETYPES)
		gametype = lastgametype;
	else if (gametype != lastgametype)
		D_GameTypeChanged(lastgametype); // emulate consvar_t behavior for gametype

	if (!G_RaceGametype())
		pencoremode = false;

	skipprecutscene = ((flags & (1<<2)) != 0);

	FLS = ((flags & (1<<3)) != 0);

	READSTRINGN(*cp, mapname, MAX_WADPATH);

	if (netgame)
		P_SetRandSeed(READUINT32(*cp));

	if (!skipprecutscene)
	{
		DEBFILE(va("Warping to %s [resetplayer=%d lastgametype=%d gametype=%d cpnd=%d]\n",
			mapname, resetplayer, lastgametype, gametype, chmappending));
		CON_LogMessage(M_GetText("Speeding off to level...\n"));
	}

	if (demo.playback && !demo.timing)
		precache = false;

	if (resetplayer)
	{
		if (!FLS || (netgame || multiplayer))
			emeralds = 0;
	}

	if (modeattacking) // i remember moving this here in internal fixed a heisenbug so
		SetPlayerSkinByNum(0, cv_chooseskin.value-1);

	//mapnumber = M_MapNumber(mapname[3], mapname[4]);
	//LUAh_MapChange(mapnumber);

	demo.savemode = (cv_recordmultiplayerdemos.value == 2) ? DSM_WILLAUTOSAVE : DSM_NOTSAVING;
	demo.savebutton = 0;
	G_InitNew(pencoremode, mapname, resetplayer, skipprecutscene);
	if (demo.playback && !demo.timing)
		precache = true;
	if (demo.timing)
		G_DoneLevelLoad();

	if (metalrecording)
		G_BeginMetal();
	if (demo.recording) // Okay, level loaded, character spawned and skinned,
		G_BeginRecording(); // I AM NOW READY TO RECORD.
	demo.deferstart = true;

#ifdef HAVE_DISCORDRPC
	DRPC_UpdatePresence();
#endif
}

static void Command_Pause(void)
{
	UINT8 buf[2];
	UINT8 *cp = buf;

	if (COM_Argc() > 1)
		WRITEUINT8(cp, (char)(atoi(COM_Argv(1)) != 0));
	else
		WRITEUINT8(cp, (char)(!paused));

	if (dedicated)
		WRITEUINT8(cp, 1);
	else
		WRITEUINT8(cp, 0);

	if (cv_pause.value || server || (IsPlayerAdmin(consoleplayer)))
	{
		if (!paused && (!(gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_VOTING || gamestate == GS_WAITINGPLAYERS)))
		{
			CONS_Printf(M_GetText("You can't pause here.\n"));
			return;
		}
		else if (modeattacking)	// in time attack, pausing restarts the map
		{
			M_ModeAttackRetry(0);	// directly call from m_menu;
			return;
		}

		SendNetXCmd(XD_PAUSE, &buf, 2);
	}
	else
		CONS_Printf(M_GetText("Only the server or a remote admin can use this.\n"));
}

static void Got_Pause(UINT8 **cp, INT32 playernum)
{
	UINT8 dedicatedpause = false;
	const char *playername;

	if (netgame && !cv_pause.value && playernum != serverplayer && !IsPlayerAdmin(playernum))
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal pause command received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	if (modeattacking && !demo.playback)
		return;

	paused = READUINT8(*cp);
	dedicatedpause = READUINT8(*cp);

	if (!demo.playback)
	{
		if (netgame)
		{
			if (dedicatedpause)
				playername = "SERVER";
			else
				playername = player_names[playernum];

			if (paused)
				CONS_Printf(M_GetText("Game paused by %s\n"), playername);
			else
				CONS_Printf(M_GetText("Game unpaused by %s\n"), playername);
		}

		if (paused)
		{
			if ((!menuactive || netgame) && (!cv_pausemusic.value))
				S_PauseAudio();
		}
		else
			S_ResumeAudio();
	}

	G_ResetAllDeviceRumbles();
}

static void Command_ReplayMarker(void)
{
	if (gamestate != GS_LEVEL)
	{
		CONS_Printf(M_GetText("You must be in a level to use this.\n"));
		return;
	}
	if (demo.savemode == DSM_WILLAUTOSAVE) {
		demo.savemode = DSM_NOTSAVING;
		CONS_Printf("Replay unmarked.\n");
	} else {
		int adjustedleveltime = leveltime - starttime;
		demo.savemode = DSM_WILLAUTOSAVE;
		if (adjustedleveltime < 0)
			adjustedleveltime = 0;
		char *title = G_BuildMapTitle(gamemap);
		snprintf(demo.titlename, 64, "%s [%i:%02d/%.5s]", title, G_TicsToMinutes(adjustedleveltime, false), G_TicsToSeconds(adjustedleveltime), modeattacking ? "Record Attack" : connectedservername);
		if (title)
			Z_Free(title);
		CONS_Printf("Replay will be saved!\n");
	}
}

// Command for stuck characters in netgames, griefing, etc.
static void Command_Respawn(void)
{
	UINT8 buf[4];
	UINT8 *cp = buf;

	WRITEINT32(cp, consoleplayer);

	if (!(gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_VOTING))
	{
		CONS_Printf(M_GetText("You must be in a level to use this.\n"));
		return;
	}

	if (players[consoleplayer].mo && !P_IsObjectOnGround(players[consoleplayer].mo))	// KART: Nice try, but no, you won't be cheesing spb anymore.
	{
		CONS_Printf(M_GetText("You must be on the floor to use this.\n"));
		return;
	}

	if (players[consoleplayer].mo && (players[consoleplayer].kartstuff[k_spinouttimer] || spbplace == players[consoleplayer].kartstuff[k_position])) // KART: Nice try, but no, you won't be cheesing spb anymore (x2)
	{
		CONS_Printf(M_GetText("Nice try.\n"));
		return;
	}

	/*if (!G_RaceGametype()) // srb2kart: not necessary, respawning makes you lose a bumper in battle, so it's not desirable to use as a way to escape a hit
	{
		CONS_Printf(M_GetText("You may only use this in co-op, race, and competition!\n"));
		return;
	}*/

	// Retry is quicker.  Probably should force people to use it.
	// nope, this is srb2kart - a complete retry is overkill
	/*if (!(netgame || multiplayer))
	{
		CONS_Printf(M_GetText("You can't use this in Single Player! Use \"retry\" instead.\n"));
		return;
	}*/

	SendNetXCmd(XD_RESPAWN, &buf, 4);
}

static void Got_Respawn(UINT8 **cp, INT32 playernum)
{
	INT32 respawnplayer = READINT32(*cp);

	// You can't respawn someone else. Nice try, there.
	if (respawnplayer != playernum || players[respawnplayer].kartstuff[k_spinouttimer] || spbplace == players[respawnplayer].kartstuff[k_position]) // srb2kart: "|| (!G_RaceGametype())"
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal respawn command received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	// incase the above checks were modified to allow sending a respawn on these occasions:
	if (players[respawnplayer].mo && !P_IsObjectOnGround(players[respawnplayer].mo))
		return;

	if (players[respawnplayer].mo)
		P_DamageMobj(players[respawnplayer].mo, NULL, NULL, 10000);
	demo_extradata[playernum] |= DXD_RESPAWN;
}

/** Deals with an ::XD_RANDOMSEED message in a netgame.
  * These messages set the position of the random number LUT and are crucial to
  * correct synchronization.
  *
  * Such a message should only ever come from the ::serverplayer. If it comes
  * from any other player, it is ignored.
  *
  * \param cp        Data buffer.
  * \param playernum Player responsible for the message. Must be ::serverplayer.
  * \author Graue <graue@oceanbase.org>
  */
static void Got_RandomSeed(UINT8 **cp, INT32 playernum)
{
	UINT32 seed;

	seed = READUINT32(*cp);

	if (playernum != serverplayer) // it's not from the server, wtf?
		return;

	P_SetRandSeed(seed);
}

/** Clears all players' scores in a netgame.
  * Only the server or a remote admin can use this command, for obvious reasons.
  *
  * \sa XD_CLEARSCORES, Got_Clearscores
  * \author SSNTails <http://www.ssntails.org>
  */
static void Command_Clearscores_f(void)
{
	if (!(server || (IsPlayerAdmin(consoleplayer))))
		return;

	SendNetXCmd(XD_CLEARSCORES, NULL, 1);
}

/** Handles an ::XD_CLEARSCORES message, which resets all players' scores in a
  * netgame to zero.
  *
  * \param cp        Data buffer.
  * \param playernum Player responsible for the message. Must be ::serverplayer
  *                  or ::adminplayer.
  * \sa XD_CLEARSCORES, Command_Clearscores_f
  * \author SSNTails <http://www.ssntails.org>
  */
static void Got_Clearscores(UINT8 **cp, INT32 playernum)
{
	INT32 i;

	(void)cp;
	if (playernum != serverplayer && !IsPlayerAdmin(playernum))
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal clear scores command received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	for (i = 0; i < MAXPLAYERS; i++)
		players[i].score = 0;

	CONS_Printf(M_GetText("Scores have been reset by the server.\n"));
}

// Team changing functions
static void Command_Teamchange_f(void)
{
	changeteam_union NetPacket;
	boolean error = false;
	UINT16 usvalue;
	NetPacket.value.l = NetPacket.value.b = 0;

	//      0         1
	// changeteam  <color>

	if (COM_Argc() <= 1)
	{
		if (G_GametypeHasTeams())
			CONS_Printf(M_GetText("changeteam <team>: switch to a new team (%s)\n"), "red, blue or spectator");
		else if (G_GametypeHasSpectators())
			CONS_Printf(M_GetText("changeteam <team>: switch to a new team (%s)\n"), "spectator or playing");
		else
			CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		return;
	}

	if (G_GametypeHasTeams())
	{
		if (!strcasecmp(COM_Argv(1), "red") || !strcasecmp(COM_Argv(1), "1"))
			NetPacket.packet.newteam = 1;
		else if (!strcasecmp(COM_Argv(1), "blue") || !strcasecmp(COM_Argv(1), "2"))
			NetPacket.packet.newteam = 2;
		else if (!strcasecmp(COM_Argv(1), "spectator") || !strcasecmp(COM_Argv(1), "0"))
			NetPacket.packet.newteam = 0;
		else
			error = true;
	}
	else if (G_GametypeHasSpectators())
	{
		if (!strcasecmp(COM_Argv(1), "spectator") || !strcasecmp(COM_Argv(1), "0"))
			NetPacket.packet.newteam = 0;
		else if (!strcasecmp(COM_Argv(1), "playing") || !strcasecmp(COM_Argv(1), "1"))
			NetPacket.packet.newteam = 3;
		else
			error = true;
	}
	else
	{
		CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		return;
	}

	if (error)
	{
		if (G_GametypeHasTeams())
			CONS_Printf(M_GetText("changeteam <team>: switch to a new team (%s)\n"), "red, blue or spectator");
		else if (G_GametypeHasSpectators())
			CONS_Printf(M_GetText("changeteam <team>: switch to a new team (%s)\n"), "spectator or playing");
		return;
	}

	if (players[consoleplayer].spectator)
		error = !(NetPacket.packet.newteam || (players[consoleplayer].pflags & PF_WANTSTOJOIN)); // :lancer:
	else if (G_GametypeHasTeams())
		error = (NetPacket.packet.newteam == (unsigned)players[consoleplayer].ctfteam);
	else if (G_GametypeHasSpectators() && !players[consoleplayer].spectator)
		error = (NetPacket.packet.newteam == 3);
#ifdef PARANOIA
	else
		I_Error("Invalid gametype after initial checks!");
#endif

	if (error)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You're already on that team!\n"));
		return;
	}

	if (!cv_allowteamchange.value && NetPacket.packet.newteam) // allow swapping to spectator even in locked teams.
	{
		CONS_Alert(CONS_NOTICE, M_GetText("The server is not allowing team changes at the moment.\n"));
		return;
	}

	//additional check for hide and seek. Don't allow change of status after hidetime ends.
	if (gametype == GT_HIDEANDSEEK && leveltime >= (hidetime * TICRATE))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Hiding time expired; no Hide and Seek status changes allowed!\n"));
		return;
	}

	usvalue = SHORT(NetPacket.value.l|NetPacket.value.b);
	SendNetXCmd(XD_TEAMCHANGE, &usvalue, sizeof(usvalue));
}

static void Command_Teamchange2_f(void)
{
	changeteam_union NetPacket;
	boolean error = false;
	UINT16 usvalue;
	NetPacket.value.l = NetPacket.value.b = 0;

	//      0         1
	// changeteam2 <color>

	if (COM_Argc() <= 1)
	{
		if (G_GametypeHasTeams())
			CONS_Printf(M_GetText("changeteam2 <team>: switch to a new team (%s)\n"), "red, blue or spectator");
		else if (G_GametypeHasSpectators())
			CONS_Printf(M_GetText("changeteam2 <team>: switch to a new team (%s)\n"), "spectator or playing");
		else
			CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		return;
	}

	if (G_GametypeHasTeams())
	{
		if (!strcasecmp(COM_Argv(1), "red") || !strcasecmp(COM_Argv(1), "1"))
			NetPacket.packet.newteam = 1;
		else if (!strcasecmp(COM_Argv(1), "blue") || !strcasecmp(COM_Argv(1), "2"))
			NetPacket.packet.newteam = 2;
		else if (!strcasecmp(COM_Argv(1), "spectator") || !strcasecmp(COM_Argv(1), "0"))
			NetPacket.packet.newteam = 0;
		else
			error = true;
	}
	else if (G_GametypeHasSpectators())
	{
		if (!strcasecmp(COM_Argv(1), "spectator") || !strcasecmp(COM_Argv(1), "0"))
			NetPacket.packet.newteam = 0;
		else if (!strcasecmp(COM_Argv(1), "playing") || !strcasecmp(COM_Argv(1), "1"))
			NetPacket.packet.newteam = 3;
		else
			error = true;
	}

	else
	{
		CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		return;
	}

	if (error)
	{
		if (G_GametypeHasTeams())
			CONS_Printf(M_GetText("changeteam2 <team>: switch to a new team (%s)\n"), "red, blue or spectator");
		else if (G_GametypeHasSpectators())
			CONS_Printf(M_GetText("changeteam2 <team>: switch to a new team (%s)\n"), "spectator or playing");
		return;
	}

	if (players[displayplayers[1]].spectator)
		error = !(NetPacket.packet.newteam || (players[displayplayers[1]].pflags & PF_WANTSTOJOIN));
	else if (G_GametypeHasTeams())
		error = (NetPacket.packet.newteam == (unsigned)players[displayplayers[1]].ctfteam);
	else if (G_GametypeHasSpectators() && !players[displayplayers[1]].spectator)
		error = (NetPacket.packet.newteam == 3);
#ifdef PARANOIA
	else
		I_Error("Invalid gametype after initial checks!");
#endif

	if (error)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You're already on that team!\n"));
		return;
	}

	if (!cv_allowteamchange.value && NetPacket.packet.newteam) // allow swapping to spectator even in locked teams.
	{
		CONS_Alert(CONS_NOTICE, M_GetText("The server is not allowing team changes at the moment.\n"));
		return;
	}

	//additional check for hide and seek. Don't allow change of status after hidetime ends.
	if (gametype == GT_HIDEANDSEEK && leveltime >= (hidetime * TICRATE))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Hiding time expired; no Hide and Seek status changes allowed!\n"));
		return;
	}

	usvalue = SHORT(NetPacket.value.l|NetPacket.value.b);
	SendNetXCmd2(XD_TEAMCHANGE, &usvalue, sizeof(usvalue));
}

static void Command_Teamchange3_f(void)
{
	changeteam_union NetPacket;
	boolean error = false;
	UINT16 usvalue;
	NetPacket.value.l = NetPacket.value.b = 0;

	//      0         1
	// changeteam3 <color>

	if (COM_Argc() <= 1)
	{
		if (G_GametypeHasTeams())
			CONS_Printf(M_GetText("changeteam3 <team>: switch to a new team (%s)\n"), "red, blue or spectator");
		else if (G_GametypeHasSpectators())
			CONS_Printf(M_GetText("changeteam3 <team>: switch to a new team (%s)\n"), "spectator or playing");
		else
			CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		return;
	}

	if (G_GametypeHasTeams())
	{
		if (!strcasecmp(COM_Argv(1), "red") || !strcasecmp(COM_Argv(1), "1"))
			NetPacket.packet.newteam = 1;
		else if (!strcasecmp(COM_Argv(1), "blue") || !strcasecmp(COM_Argv(1), "2"))
			NetPacket.packet.newteam = 2;
		else if (!strcasecmp(COM_Argv(1), "spectator") || !strcasecmp(COM_Argv(1), "0"))
			NetPacket.packet.newteam = 0;
		else
			error = true;
	}
	else if (G_GametypeHasSpectators())
	{
		if (!strcasecmp(COM_Argv(1), "spectator") || !strcasecmp(COM_Argv(1), "0"))
			NetPacket.packet.newteam = 0;
		else if (!strcasecmp(COM_Argv(1), "playing") || !strcasecmp(COM_Argv(1), "1"))
			NetPacket.packet.newteam = 3;
		else
			error = true;
	}

	else
	{
		CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		return;
	}

	if (error)
	{
		if (G_GametypeHasTeams())
			CONS_Printf(M_GetText("changeteam3 <team>: switch to a new team (%s)\n"), "red, blue or spectator");
		else if (G_GametypeHasSpectators())
			CONS_Printf(M_GetText("changeteam3 <team>: switch to a new team (%s)\n"), "spectator or playing");
		return;
	}

	if (players[displayplayers[2]].spectator)
		error = !(NetPacket.packet.newteam || (players[displayplayers[2]].pflags & PF_WANTSTOJOIN));
	else if (G_GametypeHasTeams())
		error = (NetPacket.packet.newteam == (unsigned)players[displayplayers[2]].ctfteam);
	else if (G_GametypeHasSpectators() && !players[displayplayers[2]].spectator)
		error = (NetPacket.packet.newteam == 3);
#ifdef PARANOIA
	else
		I_Error("Invalid gametype after initial checks!");
#endif

	if (error)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You're already on that team!\n"));
		return;
	}

	if (!cv_allowteamchange.value && NetPacket.packet.newteam) // allow swapping to spectator even in locked teams.
	{
		CONS_Alert(CONS_NOTICE, M_GetText("The server is not allowing team changes at the moment.\n"));
		return;
	}

	//additional check for hide and seek. Don't allow change of status after hidetime ends.
	if (gametype == GT_HIDEANDSEEK && leveltime >= (hidetime * TICRATE))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Hiding time expired; no Hide and Seek status changes allowed!\n"));
		return;
	}

	usvalue = SHORT(NetPacket.value.l|NetPacket.value.b);
	SendNetXCmd3(XD_TEAMCHANGE, &usvalue, sizeof(usvalue));
}

static void Command_Teamchange4_f(void)
{
	changeteam_union NetPacket;
	boolean error = false;
	UINT16 usvalue;
	NetPacket.value.l = NetPacket.value.b = 0;

	//      0         1
	// changeteam4 <color>

	if (COM_Argc() <= 1)
	{
		if (G_GametypeHasTeams())
			CONS_Printf(M_GetText("changeteam4 <team>: switch to a new team (%s)\n"), "red, blue or spectator");
		else if (G_GametypeHasSpectators())
			CONS_Printf(M_GetText("changeteam4 <team>: switch to a new team (%s)\n"), "spectator or playing");
		else
			CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		return;
	}

	if (G_GametypeHasTeams())
	{
		if (!strcasecmp(COM_Argv(1), "red") || !strcasecmp(COM_Argv(1), "1"))
			NetPacket.packet.newteam = 1;
		else if (!strcasecmp(COM_Argv(1), "blue") || !strcasecmp(COM_Argv(1), "2"))
			NetPacket.packet.newteam = 2;
		else if (!strcasecmp(COM_Argv(1), "spectator") || !strcasecmp(COM_Argv(1), "0"))
			NetPacket.packet.newteam = 0;
		else
			error = true;
	}
	else if (G_GametypeHasSpectators())
	{
		if (!strcasecmp(COM_Argv(1), "spectator") || !strcasecmp(COM_Argv(1), "0"))
			NetPacket.packet.newteam = 0;
		else if (!strcasecmp(COM_Argv(1), "playing") || !strcasecmp(COM_Argv(1), "1"))
			NetPacket.packet.newteam = 3;
		else
			error = true;
	}

	else
	{
		CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		return;
	}

	if (error)
	{
		if (G_GametypeHasTeams())
			CONS_Printf(M_GetText("changeteam4 <team>: switch to a new team (%s)\n"), "red, blue or spectator");
		else if (G_GametypeHasSpectators())
			CONS_Printf(M_GetText("changeteam4 <team>: switch to a new team (%s)\n"), "spectator or playing");
		return;
	}

	if (players[displayplayers[3]].spectator)
		error = !(NetPacket.packet.newteam || (players[displayplayers[3]].pflags & PF_WANTSTOJOIN));
	else if (G_GametypeHasTeams())
		error = (NetPacket.packet.newteam == (unsigned)players[displayplayers[3]].ctfteam);
	else if (G_GametypeHasSpectators() && !players[displayplayers[3]].spectator)
		error = (NetPacket.packet.newteam == 3);
#ifdef PARANOIA
	else
		I_Error("Invalid gametype after initial checks!");
#endif

	if (error)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You're already on that team!\n"));
		return;
	}

	if (!cv_allowteamchange.value && NetPacket.packet.newteam) // allow swapping to spectator even in locked teams.
	{
		CONS_Alert(CONS_NOTICE, M_GetText("The server is not allowing team changes at the moment.\n"));
		return;
	}

	//additional check for hide and seek. Don't allow change of status after hidetime ends.
	if (gametype == GT_HIDEANDSEEK && leveltime >= (hidetime * TICRATE))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Hiding time expired; no Hide and Seek status changes allowed!\n"));
		return;
	}

	usvalue = SHORT(NetPacket.value.l|NetPacket.value.b);
	SendNetXCmd4(XD_TEAMCHANGE, &usvalue, sizeof(usvalue));
}

static void Command_ServerTeamChange_f(void)
{
	changeteam_union NetPacket;
	boolean error = false;
	UINT16 usvalue;
	NetPacket.value.l = NetPacket.value.b = 0;

	if (!(server || (IsPlayerAdmin(consoleplayer))))
	{
		CONS_Printf(M_GetText("Only the server or a remote admin can use this.\n"));
		return;
	}

	//        0              1         2
	// serverchangeteam <playernum>  <team>

	if (COM_Argc() < 3)
	{
		if (G_TagGametype())
			CONS_Printf(M_GetText("serverchangeteam <playername/playernum> <team>: switch player to a new team (%s)\n"), "it, notit, playing, or spectator");
		else if (G_GametypeHasTeams())
			CONS_Printf(M_GetText("serverchangeteam <playername/playernum> <team>: switch player to a new team (%s)\n"), "red, blue or spectator");
		else if (G_GametypeHasSpectators())
			CONS_Printf(M_GetText("serverchangeteam <playername/playernum> <team>: switch player to a new team (%s)\n"), "spectator or playing");
		else
			CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		return;
	}

	if (G_TagGametype())
	{
		if (!strcasecmp(COM_Argv(2), "it") || !strcasecmp(COM_Argv(2), "1"))
			NetPacket.packet.newteam = 1;
		else if (!strcasecmp(COM_Argv(2), "notit") || !strcasecmp(COM_Argv(2), "2"))
			NetPacket.packet.newteam = 2;
		else if (!strcasecmp(COM_Argv(2), "playing") || !strcasecmp(COM_Argv(2), "3"))
			NetPacket.packet.newteam = 3;
		else if (!strcasecmp(COM_Argv(2), "spectator") || !strcasecmp(COM_Argv(2), "0"))
			NetPacket.packet.newteam = 0;
		else
			error = true;
	}
	else if (G_GametypeHasTeams())
	{
		if (!strcasecmp(COM_Argv(2), "red") || !strcasecmp(COM_Argv(2), "1"))
			NetPacket.packet.newteam = 1;
		else if (!strcasecmp(COM_Argv(2), "blue") || !strcasecmp(COM_Argv(2), "2"))
			NetPacket.packet.newteam = 2;
		else if (!strcasecmp(COM_Argv(2), "spectator") || !strcasecmp(COM_Argv(2), "0"))
			NetPacket.packet.newteam = 0;
		else
			error = true;
	}
	else if (G_GametypeHasSpectators())
	{
		if (!strcasecmp(COM_Argv(2), "spectator") || !strcasecmp(COM_Argv(2), "0"))
			NetPacket.packet.newteam = 0;
		else if (!strcasecmp(COM_Argv(2), "playing") || !strcasecmp(COM_Argv(2), "1"))
			NetPacket.packet.newteam = 3;
		else
			error = true;
	}
	else
	{
		CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		return;
	}

	if (error)
	{
		if (G_TagGametype())
			CONS_Printf(M_GetText("serverchangeteam <playername/playernum> <team>: switch player to a new team (%s)\n"), "it, notit, playing, or spectator");
		else if (G_GametypeHasTeams())
			CONS_Printf(M_GetText("serverchangeteam <playername/playernum> <team>: switch player to a new team (%s)\n"), "red, blue or spectator");
		else if (G_GametypeHasSpectators())
			CONS_Printf(M_GetText("serverchangeteam <playername/playernum> <team>: switch player to a new team (%s)\n"), "spectator or playing");
		return;
	}

	NetPacket.packet.playernum = nametonum(COM_Argv(1));

	if (NetPacket.packet.playernum == -1 || !playeringame[NetPacket.packet.playernum])
	{
		CONS_Alert(CONS_NOTICE, M_GetText("There is no player %s!\n"), COM_Argv(1));
		return;
	}

	if (G_TagGametype())
	{
		if (( (players[NetPacket.packet.playernum].pflags & PF_TAGIT) && NetPacket.packet.newteam == 1) ||
			(!(players[NetPacket.packet.playernum].pflags & PF_TAGIT) && NetPacket.packet.newteam == 2) ||
			( players[NetPacket.packet.playernum].spectator && !NetPacket.packet.newteam) ||
			(!players[NetPacket.packet.playernum].spectator && NetPacket.packet.newteam == 3))
			error = true;
	}
	else if (G_GametypeHasTeams())
	{
		if (NetPacket.packet.newteam == (unsigned)players[NetPacket.packet.playernum].ctfteam ||
			(players[NetPacket.packet.playernum].spectator && !NetPacket.packet.newteam))
			error = true;
	}
	else if (G_GametypeHasSpectators())
	{
		if ((players[NetPacket.packet.playernum].spectator && !NetPacket.packet.newteam) ||
			(!players[NetPacket.packet.playernum].spectator && NetPacket.packet.newteam == 3))
			error = true;
	}
#ifdef PARANOIA
	else
		I_Error("Invalid gametype after initial checks!");
#endif

	if (error)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("That player is already on that team!\n"));
		return;
	}

	//additional check for hide and seek. Don't allow change of status after hidetime ends.
	if (gametype == GT_HIDEANDSEEK && leveltime >= (hidetime * TICRATE))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("Hiding time expired; no Hide and Seek status changes allowed!\n"));
		return;
	}

	NetPacket.packet.verification = true; // This signals that it's a server change

	usvalue = SHORT(NetPacket.value.l|NetPacket.value.b);
	SendNetXCmd(XD_TEAMCHANGE, &usvalue, sizeof(usvalue));
}

//todo: This and the other teamchange functions are getting too long and messy. Needs cleaning.
static void Got_Teamchange(UINT8 **cp, INT32 playernum)
{
	changeteam_union NetPacket;
	boolean error = false, wasspectator = false;
	NetPacket.value.l = NetPacket.value.b = READINT16(*cp);

	if (!G_GametypeHasTeams() && !G_GametypeHasSpectators()) //Make sure you're in the right gametype.
	{
		// this should never happen unless the client is hacked/buggy
		CONS_Alert(CONS_WARNING, M_GetText("Illegal team change received from player %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
	}

	if (NetPacket.packet.verification) // Special marker that the server sent the request
	{
		if (playernum != serverplayer && (!IsPlayerAdmin(playernum)))
		{
			CONS_Alert(CONS_WARNING, M_GetText("Illegal team change received from player %s\n"), player_names[playernum]);
			if (server)
			{
				UINT8 buf[2];

				buf[0] = (UINT8)playernum;
				buf[1] = KICK_MSG_CON_FAIL;
				SendNetXCmd(XD_KICK, &buf, 2);
			}
			return;
		}
		playernum = NetPacket.packet.playernum;
	}

	// Prevent multiple changes in one go.
	if (players[playernum].spectator && !(players[playernum].pflags & PF_WANTSTOJOIN) && !NetPacket.packet.newteam)
		return;
	else if (G_TagGametype())
	{
		if (((players[playernum].pflags & PF_TAGIT) && NetPacket.packet.newteam == 1) ||
			(!(players[playernum].pflags & PF_TAGIT) && NetPacket.packet.newteam == 2) ||
			(!players[playernum].spectator && NetPacket.packet.newteam == 3))
			return;
	}
	else if (G_GametypeHasTeams())
	{
		if (NetPacket.packet.newteam && (NetPacket.packet.newteam == (unsigned)players[playernum].ctfteam))
			return;
	}
	else if (G_GametypeHasSpectators())
	{
		if (!players[playernum].spectator && NetPacket.packet.newteam == 3)
			return;
	}
	else
	{
		if (playernum != serverplayer && (!IsPlayerAdmin(playernum)))
		{
			CONS_Alert(CONS_WARNING, M_GetText("Illegal team change received from player %s\n"), player_names[playernum]);
			if (server)
			{
				UINT8 buf[2];

				buf[0] = (UINT8)playernum;
				buf[1] = KICK_MSG_CON_FAIL;
				SendNetXCmd(XD_KICK, &buf, 2);
			}
		}
		return;
	}

	//Make sure that the right team number is sent. Keep in mind that normal clients cannot change to certain teams in certain gametypes.
	switch (gametype)
	{
	case GT_HIDEANDSEEK:
		//no status changes after hidetime
		if (leveltime >= (hidetime * TICRATE))
		{
			error = true;
			break;
		}
		/* FALLTHRU */
	case GT_TAG:
		switch (NetPacket.packet.newteam)
		{
		case 0:
			break;
		case 1: case 2:
			if (!NetPacket.packet.verification)
				error = true; //Only admin can change player's IT status' in tag.
			break;
		case 3: //Join game via console.
			if (!NetPacket.packet.verification && !cv_allowteamchange.value)
				error = true;
			break;
		}

		break;
	default:
#ifdef PARANOIA
		if (!G_GametypeHasTeams() && !G_GametypeHasSpectators())
			I_Error("Invalid gametype after initial checks!");
#endif

		if (!cv_allowteamchange.value)
		{
			if (!NetPacket.packet.verification && NetPacket.packet.newteam)
				error = true; //Only admin can change status, unless changing to spectator.
		}
		break; //Otherwise, you don't need special permissions.
	}

	if (server && ((NetPacket.packet.newteam < 0 || NetPacket.packet.newteam > 3) || error))
	{
		UINT8 buf[2];

		buf[0] = (UINT8)playernum;
		buf[1] = KICK_MSG_CON_FAIL;
		CONS_Alert(CONS_WARNING, M_GetText("Illegal team change received from player %s\n"), player_names[playernum]);
		SendNetXCmd(XD_KICK, &buf, 2);
	}

	//Safety first!
	// (not respawning spectators here...)
	if (!players[playernum].spectator)
	{
		if (players[playernum].mo)
		{
			//if (!players[playernum].spectator)
				P_DamageMobj(players[playernum].mo, NULL, NULL, 10000);
			/*else
			{
				if (players[playernum].mo)
				{
					P_RemoveMobj(players[playernum].mo);
					players[playernum].mo = NULL;
				}
				players[playernum].playerstate = PST_REBORN;
			}*/
		}
		else
			players[playernum].playerstate = PST_REBORN;
	}
	else
		wasspectator = true;

	players[playernum].pflags &= ~PF_WANTSTOJOIN;

	//Now that we've done our error checking and killed the player
	//if necessary, put the player on the correct team/status.
	if (G_TagGametype())
	{
		if (!NetPacket.packet.newteam)
		{
			players[playernum].spectator = true;
			players[playernum].pflags &= ~PF_TAGIT;
			players[playernum].pflags &= ~PF_TAGGED;
		}
		else if (NetPacket.packet.newteam != 3) // .newteam == 1 or 2.
		{
			players[playernum].pflags |= PF_WANTSTOJOIN; //players[playernum].spectator = false;
			players[playernum].pflags &= ~PF_TAGGED;//Just in case.

			if (NetPacket.packet.newteam == 1) //Make the player IT.
				players[playernum].pflags |= PF_TAGIT;
			else
				players[playernum].pflags &= ~PF_TAGIT;
		}
		else // Just join the game.
		{
			players[playernum].pflags |= PF_WANTSTOJOIN; //players[playernum].spectator = false;

			//If joining after hidetime in normal tag, default to being IT.
			if (gametype == GT_TAG && (leveltime > (hidetime * TICRATE)))
			{
				NetPacket.packet.newteam = 1; //minor hack, causes the "is it" message to be printed later.
				players[playernum].pflags |= PF_TAGIT; //make the player IT.
			}
		}
	}
	else if (G_GametypeHasTeams())
	{
		if (!NetPacket.packet.newteam)
		{
			players[playernum].ctfteam = 0;
			players[playernum].spectator = true;
		}
		else
		{
			players[playernum].ctfteam = NetPacket.packet.newteam;
			players[playernum].pflags |= PF_WANTSTOJOIN; //players[playernum].spectator = false;
		}
	}
	else if (G_GametypeHasSpectators())
	{
		if (!NetPacket.packet.newteam)
			players[playernum].spectator = true;
		else
			players[playernum].pflags |= PF_WANTSTOJOIN; //players[playernum].spectator = false;
	}

	if (NetPacket.packet.autobalance)
	{
		if (NetPacket.packet.newteam == 1)
			CONS_Printf(M_GetText("%s was autobalanced to the %c%s%c.\n"), player_names[playernum], '\x85', M_GetText("Red Team"), '\x80');
		else if (NetPacket.packet.newteam == 2)
			CONS_Printf(M_GetText("%s was autobalanced to the %c%s%c.\n"), player_names[playernum], '\x84', M_GetText("Blue Team"), '\x80');
	}
	else if (NetPacket.packet.scrambled)
	{
		if (NetPacket.packet.newteam == 1)
			CONS_Printf(M_GetText("%s was scrambled to the %c%s%c.\n"), player_names[playernum], '\x85', M_GetText("Red Team"), '\x80');
		else if (NetPacket.packet.newteam == 2)
			CONS_Printf(M_GetText("%s was scrambled to the %c%s%c.\n"), player_names[playernum], '\x84', M_GetText("Blue Team"), '\x80');
	}
	else if (NetPacket.packet.newteam == 1)
	{
		if (G_TagGametype())
			CONS_Printf(M_GetText("%s is now IT!\n"), player_names[playernum]);
		else
			CONS_Printf(M_GetText("%s switched to the %c%s%c.\n"), player_names[playernum], '\x85', M_GetText("Red Team"), '\x80');
	}
	else if (NetPacket.packet.newteam == 2)
	{
		if (G_TagGametype())
			CONS_Printf(M_GetText("%s is no longer IT!\n"), player_names[playernum]);
		else
			CONS_Printf(M_GetText("%s switched to the %c%s%c.\n"), player_names[playernum], '\x84', M_GetText("Blue Team"), '\x80');
	}
	else if (NetPacket.packet.newteam == 0 && !wasspectator)
		HU_AddChatText(va("\x82*%s became a spectator.", player_names[playernum]), false); // "entered the game" text was moved to P_SpectatorJoinGame

	//reset view if you are changed, or viewing someone who was changed.
	if (playernum == consoleplayer || displayplayers[0] == playernum)
		displayplayers[0] = consoleplayer;

	if (G_GametypeHasTeams())
	{
		if (NetPacket.packet.newteam)
		{
			if (playernum == consoleplayer) //CTF and Team Match colors.
				CV_SetValue(&cv_playercolor, NetPacket.packet.newteam + 5);
			else if (playernum == displayplayers[1])
				CV_SetValue(&cv_playercolor2, NetPacket.packet.newteam + 5);
			else if (playernum == displayplayers[2])
				CV_SetValue(&cv_playercolor3, NetPacket.packet.newteam + 5);
			else if (playernum == displayplayers[3])
				CV_SetValue(&cv_playercolor4, NetPacket.packet.newteam + 5);
		}
	}

	if (gamestate != GS_LEVEL)
		return;

	demo_extradata[playernum] |= DXD_PLAYSTATE;

	// Clear player score and rings if a spectator.
	if (players[playernum].spectator)
	{
		players[playernum].spectatorreentry = (cv_spectatorreentry.value * TICRATE);

		if (G_BattleGametype()) // SRB2kart
		{
			players[playernum].marescore = 0;
			if (K_IsPlayerWanted(&players[playernum]))
				K_CalculateBattleWanted();
		}

		players[playernum].health = 1;
		if (players[playernum].mo)
			players[playernum].mo->health = 1;
	}

	// In tag, check to see if you still have a game.
	/*if (G_TagGametype())
		P_CheckSurvivors();
	else*/ if (G_BattleGametype())
		K_CheckBumpers(); // SRB2Kart
	else if (G_RaceGametype())
		P_CheckRacers(); // also SRB2Kart
}

//
// Attempts to make password system a little sane without
// rewriting the entire goddamn XD_file system
//
#include "md5.h"
static void D_MD5PasswordPass(const UINT8 *buffer, size_t len, const char *salt, void *dest)
{
#ifdef NOMD5
	(void)buffer;
	(void)len;
	(void)salt;
	memset(dest, 0, 16);
#else
	char tmpbuf[256];
	const size_t sl = strlen(salt);

	if (len > 256-sl)
		len = 256-sl;

	memcpy(tmpbuf, buffer, len);
	memmove(&tmpbuf[len], salt, sl);
	//strcpy(&tmpbuf[len], salt);
	len += strlen(salt);
	if (len < 256)
		memset(&tmpbuf[len],0,256-len);

	// Yes, we intentionally md5 the ENTIRE buffer regardless of size...
	md5_buffer(tmpbuf, 256, dest);
#endif
}

#define BASESALT "basepasswordstorage"
static UINT8 adminpassmd5[MD5_LEN];
static boolean adminpasswordset = false;

void D_SetPassword(const char *pw)
{
	D_MD5PasswordPass((const UINT8 *)pw, strlen(pw), BASESALT, &adminpassmd5);
	adminpasswordset = true;
}

// Remote Administration
static void Command_Changepassword_f(void)
{
#ifdef NOMD5
	// If we have no MD5 support then completely disable XD_LOGIN responses for security.
	CONS_Alert(CONS_NOTICE, "Remote administration commands are not supported in this build.\n");
#else
	if (client) // cannot change remotely
	{
		CONS_Printf(M_GetText("Only the server can use this.\n"));
		return;
	}

	if (COM_Argc() != 2)
	{
		CONS_Printf(M_GetText("password <password>: change remote admin password\n"));
		return;
	}

	D_SetPassword(COM_Argv(1));
	CONS_Printf(M_GetText("Password set.\n"));
#endif
}

static void Command_Login_f(void)
{
#ifdef NOMD5
	// If we have no MD5 support then completely disable XD_LOGIN responses for security.
	CONS_Alert(CONS_NOTICE, "Remote administration commands are not supported in this build.\n");
#else
	UINT8 finalmd5[MD5_LEN];
	const char *pw;

	if (!netgame)
	{
		CONS_Printf(M_GetText("This only works in a netgame.\n"));
		return;
	}

	// If the server uses login, it will effectively just remove admin privileges
	// from whoever has them. This is good.
	if (COM_Argc() != 2)
	{
		CONS_Printf(M_GetText("login <password>: Administrator login\n"));
		return;
	}

	pw = COM_Argv(1);

	// Do the base pass to get what the server has (or should?)
	D_MD5PasswordPass((const UINT8 *)pw, strlen(pw), BASESALT, &finalmd5);

	// Do the final pass to get the comparison the server will come up with
	D_MD5PasswordPass(finalmd5, MD5_LEN, va("PNUM%02d", consoleplayer), &finalmd5);

	CONS_Printf(M_GetText("Sending login... (Notice only given if password is correct.)\n"));

	SendNetXCmd(XD_LOGIN, finalmd5, MD5_LEN);
#endif
}

static void Got_Login(UINT8 **cp, INT32 playernum)
{
#ifdef NOMD5
	// If we have no MD5 support then completely disable XD_LOGIN responses for security.
	(void)cp;
	(void)playernum;
#else
	UINT8 sentmd5[MD5_LEN], finalmd5[MD5_LEN];

	READMEM(*cp, sentmd5, MD5_LEN);

	if (client)
		return;

	if (!adminpasswordset)
	{
		CONS_Printf(M_GetText("Password from %s failed (no password set).\n"), player_names[playernum]);
		return;
	}

	// Do the final pass to compare with the sent md5
	D_MD5PasswordPass(adminpassmd5, MD5_LEN, va("PNUM%02d", playernum), &finalmd5);

	if (!memcmp(sentmd5, finalmd5, MD5_LEN))
	{
		CONS_Printf(M_GetText("%s passed authentication.\n"), player_names[playernum]);
		COM_BufInsertText(va("promote %d\n", playernum)); // do this immediately
	}
	else
		CONS_Printf(M_GetText("Password from %s failed.\n"), player_names[playernum]);
#endif
}

boolean IsPlayerAdmin(INT32 playernum)
{
	INT32 i;
	for (i = 0; i < MAXPLAYERS; i++)
		if (playernum == adminplayers[i])
			return true;

	return false;
}

void SetAdminPlayer(INT32 playernum)
{
	INT32 i;
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playernum == adminplayers[i])
			return; // Player is already admin

		if (adminplayers[i] == -1)
		{
			adminplayers[i] = playernum; // Set the player to a free spot
			break; // End the loop now. If it keeps going, the same player might get assigned to two slots.
		}
	}
}

void ClearAdminPlayers(void)
{
	INT32 i;
	for (i = 0; i < MAXPLAYERS; i++)
		adminplayers[i] = -1;
}

void RemoveAdminPlayer(INT32 playernum)
{
	INT32 i;
	for (i = 0; i < MAXPLAYERS; i++)
		if (playernum == adminplayers[i])
			adminplayers[i] = -1;
}

static void Command_Verify_f(void)
{
	char buf[8]; // Should be plenty
	char *temp;
	INT32 playernum;

	if (client)
	{
		CONS_Printf(M_GetText("Only the server can use this.\n"));
		return;
	}

	if (!netgame)
	{
		CONS_Printf(M_GetText("This only works in a netgame.\n"));
		return;
	}

	if (COM_Argc() != 2)
	{
		CONS_Printf(M_GetText("promote <playername/playernum>: give admin privileges to a player\n"));
		return;
	}

	playernum = nametonum(COM_Argv(1));
	if (playernum == -1)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("There is no player %s!\n"), COM_Argv(1));
		return;
	}

	temp = buf;

	WRITEUINT8(temp, playernum);

	if (playeringame[playernum])
		SendNetXCmd(XD_VERIFIED, buf, 1);
}

static void Got_Verification(UINT8 **cp, INT32 playernum)
{
	INT16 num = READUINT8(*cp);

	if (playernum != serverplayer) // it's not from the server (hacker or bug)
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal verification received from %s (serverplayer is %s)\n"), player_names[playernum], player_names[serverplayer]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	SetAdminPlayer(num);

	if (num != consoleplayer)
		return;

	CONS_Printf(M_GetText("You are now a server administrator.\n"));
}

static void Command_RemoveAdmin_f(void)
{
	char buf[8]; // Should be plenty
	char *temp;
	INT32 playernum;

	if (client)
	{
		CONS_Printf(M_GetText("Only the server can use this.\n"));
		return;
	}

	if (COM_Argc() != 2)
	{
		CONS_Printf(M_GetText("demote <playernum>: remove admin privileges from a player\n"));
		return;
	}

	playernum = nametonum(COM_Argv(1));
	if (playernum == -1)
	{
		CONS_Alert(CONS_NOTICE, M_GetText("There is no player %s!\n"), COM_Argv(1));
		return;
	}

	temp = buf;

	WRITEUINT8(temp, playernum);

	if (playeringame[playernum])
		SendNetXCmd(XD_DEMOTED, buf, 1);
}

static void Got_Removal(UINT8 **cp, INT32 playernum)
{
	INT16 num = READUINT8(*cp);

	if (playernum != serverplayer) // it's not from the server (hacker or bug)
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal demotion received from %s (serverplayer is %s)\n"), player_names[playernum], player_names[serverplayer]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	RemoveAdminPlayer(num);

	if (num != consoleplayer)
		return;

	CONS_Printf(M_GetText("You are no longer a server administrator.\n"));
}

static void Command_MotD_f(void)
{
	size_t i, j;
	char *mymotd;

	if ((j = COM_Argc()) < 2)
	{
		CONS_Printf(M_GetText("motd <message>: Set a message that clients see upon join.\n"));
		return;
	}

	if (!(server || (IsPlayerAdmin(consoleplayer))))
	{
		CONS_Printf(M_GetText("Only the server or a remote admin can use this.\n"));
		return;
	}

	mymotd = Z_Malloc(sizeof(motd), PU_STATIC, NULL);

	strlcpy(mymotd, COM_Argv(1), sizeof motd);
	for (i = 2; i < j; i++)
	{
		strlcat(mymotd, " ", sizeof motd);
		strlcat(mymotd, COM_Argv(i), sizeof motd);
	}

	// Disallow non-printing characters and semicolons.
	for (i = 0; mymotd[i] != '\0'; i++)
		if (!isprint(mymotd[i]) || mymotd[i] == ';')
		{
			Z_Free(mymotd);
			return;
		}

	if ((netgame || multiplayer) && client)
		SendNetXCmd(XD_SETMOTD, mymotd, i); // send the actual size of the motd string, not the full buffer's size
	else
	{
		strcpy(motd, mymotd);
		CONS_Printf(M_GetText("Message of the day set.\n"));
	}

	Z_Free(mymotd);
}

static void Got_MotD_f(UINT8 **cp, INT32 playernum)
{
	char *mymotd = Z_Malloc(sizeof(motd), PU_STATIC, NULL);
	INT32 i;
	boolean kick = false;

	READSTRINGN(*cp, mymotd, sizeof(motd));

	// Disallow non-printing characters and semicolons.
	for (i = 0; mymotd[i] != '\0'; i++)
		if (!isprint(mymotd[i]) || mymotd[i] == ';')
			kick = true;

	if ((playernum != serverplayer && !IsPlayerAdmin(playernum)) || kick)
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal motd change received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}

		Z_Free(mymotd);
		return;
	}

	strcpy(motd, mymotd);

	CONS_Printf(M_GetText("Message of the day set.\n"));

	Z_Free(mymotd);
}

static void Command_RunSOC(void)
{
	const char *fn;
	char buf[255];
	size_t length = 0;

	if (COM_Argc() != 2)
	{
		CONS_Printf(M_GetText("runsoc <socfile.soc> or <lumpname>: run a soc\n"));
		return;
	}
	else
		fn = COM_Argv(1);

	if (netgame && !(server || IsPlayerAdmin(consoleplayer)))
	{
		CONS_Printf(M_GetText("Only the server or a remote admin can use this.\n"));
		return;
	}

	if (!(netgame || multiplayer))
	{
		if (!P_RunSOC(fn))
			CONS_Printf(M_GetText("Could not find SOC.\n"));
		else
			G_SetGameModified(multiplayer, false);
		return;
	}

	nameonly(strcpy(buf, fn));
	length = strlen(buf)+1;

	SendNetXCmd(XD_RUNSOC, buf, length);
}

static void Got_RunSOCcmd(UINT8 **cp, INT32 playernum)
{
	char filename[256];
	filestatus_t ncs = FS_NOTCHECKED;

	if (playernum != serverplayer && !IsPlayerAdmin(playernum))
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal runsoc command received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	READSTRINGN(*cp, filename, 255);

	// Maybe add md5 support?
	if (strstr(filename, ".soc") != NULL)
	{
		ncs = findfile(filename,NULL,true);

		if (ncs != FS_FOUND)
		{
			Command_ExitGame_f();
			if (ncs == FS_NOTFOUND)
			{
				CONS_Printf(M_GetText("The server tried to add %s,\nbut you don't have this file.\nYou need to find it in order\nto play on this server.\n"), filename);
				M_StartMessage(va("The server added a file\n(%s)\nthat you do not have.\n\nPress ESC\n",filename), NULL, MM_NOTHING);
			}
			else
			{
				CONS_Printf(M_GetText("Unknown error finding soc file (%s) the server added.\n"), filename);
				M_StartMessage(va("Unknown error trying to load a file\nthat the server added\n(%s).\n\nPress ESC\n",filename), NULL, MM_NOTHING);
			}
			return;
		}
	}

	P_RunSOC(filename);
	G_SetGameModified(true, false);
}

static void Command_Addfilelocal(void)
{
	const char *fn;
	INT32 i;

	if (COM_Argc() != 2)
	{
		CONS_Printf(M_GetText("addfilelocal <wadfile.wad>: load wad file\n"));
		return;
	}
	else
		fn = COM_Argv(1);

	// Disallow non-printing characters and semicolons.
	for (i = 0; fn[i] != '\0'; i++)
		if (!isprint(fn[i]) || fn[i] == ';')
			return;

	// Add any wad file, ignoring checks for if it contains complex things like
	// lua. Great for complex but client-side customizations, like different
	// level cards or anything like that.
	P_AddWadFileLocal(fn);
}


/** Adds a pwad at runtime.
  * Searches for sounds, maps, music, new images.
  */
static void Command_Addfile(void)
{
	const char *fn, *p;
	char buf[256];
	char *buf_p = buf;
	INT32 i;
	int musiconly; // W_VerifyNMUSlumps isn't boolean

	if (COM_Argc() != 2)
	{
		CONS_Printf(M_GetText("addfile <wadfile.wad>: load wad file\n"));
		return;
	}
	else
		fn = COM_Argv(1);

	// Disallow non-printing characters and semicolons.
	for (i = 0; fn[i] != '\0'; i++)
		if (!isprint(fn[i]) || fn[i] == ';')
			return;

	musiconly = W_VerifyNMUSlumps(fn);

	if (!musiconly)
	{
		// ... But only so long as they contain nothing more then music and sprites.
		if (netgame && !(server || IsPlayerAdmin(consoleplayer)))
		{
			CONS_Printf(M_GetText("Only the server or a remote admin can use this.\n"));
			return;
		}
		G_SetGameModified(multiplayer, false);
	}

	// Add file on your client directly if it is trivial, or you aren't in a netgame.
	if (!(netgame || multiplayer) || musiconly)
	{
		P_AddWadFile(fn, false);
		return;
	}

	p = fn+strlen(fn);
	while(--p >= fn)
		if (*p == '\\' || *p == '/' || *p == ':')
			break;
	++p;

	WRITESTRINGN(buf_p,p,240);

	// calculate and check md5
	{
		UINT8 md5sum[16];
#ifdef NOMD5
		memset(md5sum,0,16);
#else
		FILE *fhandle;

		if ((fhandle = W_OpenWadFile(&fn, true)) != NULL)
		{
			tic_t t = I_GetTime();
			CONS_Debug(DBG_SETUP, "Making MD5 for %s\n",fn);
			md5_stream(fhandle, md5sum);
			CONS_Debug(DBG_SETUP, "MD5 calc for %s took %f second\n", fn, (float)(I_GetTime() - t)/TICRATE);
			fclose(fhandle);
		}
		else // file not found
			return;

		for (i = 0; i < numwadfiles; i++)
		{
			if (!memcmp(wadfiles[i]->md5sum, md5sum, 16))
			{
				CONS_Alert(CONS_ERROR, M_GetText("%s is already loaded\n"), fn);
				return;
			}
		}
#endif
		WRITEMEM(buf_p, md5sum, 16);
	}

	if (IsPlayerAdmin(consoleplayer) && (!server)) // Request to add file
		SendNetXCmd(XD_REQADDFILE, buf, buf_p - buf);
	else
		SendNetXCmd(XD_ADDFILE, buf, buf_p - buf);
}

/** Adds something at runtime.
  */
static void
Command_Addskins (void)
{
	if (COM_Argc() > 3)
	{
		CONS_Printf(
				"addskins <file>: Load a skin file.\n");
		return;
	}
	// no forcing?
	/*
	if (fasticmp(COM_Argv(2), "-force") || fasticmp(COM_Argv(2), "-f")) {
		CONS_Alert(CONS_NOTICE, M_GetText("Adding file %s. May or may not be a skin.\n"), COM_Argv(1));
		P_AddWadFile(COM_Argv(1), 0, true);	
	} else {
		if (DumbStartsWith("KC_", COM_Argv(1))) {
			P_AddWadFile(COM_Argv(1), 0, true);
		} else if (DumbStartsWith("KCL_", COM_Argv(1))) {
			if (!demo.playback) {
				CONS_Alert(CONS_ERROR, M_GetText("Cannot add file %s as it is a skin with lua. Include -force or -f to force it to load.\n"), COM_Argv(1));
				return;
			} else {
	*/
				P_AddWadFile(COM_Argv(1), true);
	/*
			}
		} else {
			CONS_Alert(CONS_ERROR, M_GetText("Cannot add file %s as it is not a skin.\n"), COM_Argv(1));
			return;
		}
	}
	*/
}

static void Command_GLocalSkin (void)
{
	size_t first_option;
	size_t option_display;
	size_t option_all;
	size_t option_player;

	option_player	= COM_CheckPartialParm("-p");
	option_display 	= COM_CheckPartialParm("-d");
	option_all		= COM_CheckPartialParm("-a");

	char* fuck; // local skin name

	if (!( first_option = COM_FirstOption() ))
		first_option = COM_Argc();

	if (first_option < 2)
	{
		/* holy fucking shit */
		CONS_Printf("localskin <name> [-player <name>] [-display <number>] [-all]:\n");
		CONS_Printf(M_GetText("Set a localskin via its internal name (usually printed on the console).\n\n\
* Using \"-player\" will set a localskin to a specified player.\n\
  Defaults to yourself.\n\
* Using \"-display\" will set a localskin to the displayed player.\n\
  Defaults to 0, which is the first player displayed.\n\
  Can go up to 3 for splitscreen.\n\
* Using \"-all\" will set a localskin to ALL players.\n\
* \"localskin none\" removes the localskin, just like how\n\
  \"forceskin none\" does.\n"));
		return;
	}

	fuck = ConcatCommandArgv(1, first_option);

	const char* player_name = player_names[consoleplayer];
	const char* display_num = "0";
	size_t dnum = 0;

	if (option_display)  // -display
	{
		// handle default: 0
		if (COM_Argc() >= option_display)
			display_num = COM_Argv(option_display + 1);

		dnum = atoi(display_num);
		if (dnum > 3) // nuh uh
			dnum = 0;

		SetLocalPlayerSkin(displayplayers[dnum], fuck, NULL);

		CONS_Printf("Successfully applied localskin to displayed player.\n");

		Z_Free(fuck);
		return;
	}
	else if (option_all) // -all
	{
		int i;

		for (i = 0; i < MAXPLAYERS; ++i) 
		{
			if (!playeringame[i])
				continue;
			SetLocalPlayerSkin(i, fuck, NULL);
		}
		CONS_Printf("Successfully applied localskin to all players.\n");

		Z_Free(fuck);
		return;
	}
	else // -player or no other arguments
	{
		if (option_player)
		{
			int i;
			player_name = COM_Argv(option_player + 1);

			for (i = 0; i < MAXPLAYERS; ++i)
			{
				if (fasticmp(player_names[i], player_name))
					SetLocalPlayerSkin(i, fuck, NULL);
			}
			CONS_Printf("Successfully applied localskin to specified player.\n");

			Z_Free(fuck);
			return;
		}
		else
		{
			SetLocalPlayerSkin(consoleplayer, fuck, &cv_localskin);
			CONS_Printf("Successfully applied localskin.\n");

			Z_Free(fuck);
			return;
		}

		CONS_Printf("Could not apply localskin.\n");

		Z_Free(fuck);
		return;
	}
}

static void Got_RequestAddfilecmd(UINT8 **cp, INT32 playernum)
{
	char filename[241];
	filestatus_t ncs = FS_NOTCHECKED;
	UINT8 md5sum[16];
	boolean kick = false;
	boolean toomany = false;
	INT32 i,j;
	serverinfo_pak *dummycheck = NULL;

	// Shut the compiler up.
	(void)dummycheck;

	READSTRINGN(*cp, filename, 240);
	READMEM(*cp, md5sum, 16);

	// Only the server processes this message.
	if (client)
		return;

	// Disallow non-printing characters and semicolons.
	for (i = 0; filename[i] != '\0'; i++)
		if (!isprint(filename[i]) || filename[i] == ';')
			kick = true;

	if ((playernum != serverplayer && !IsPlayerAdmin(playernum)) || kick)
	{
		UINT8 buf[2];

		CONS_Alert(CONS_WARNING, M_GetText("Illegal addfile command received from %s\n"), player_names[playernum]);

		buf[0] = (UINT8)playernum;
		buf[1] = KICK_MSG_CON_FAIL;
		SendNetXCmd(XD_KICK, &buf, 2);
		return;
	}

	// See W_LoadWadFile in w_wad.c
	if (numwadfiles >= MAX_WADFILES)
		toomany = true;
	else
		ncs = findfile(filename,md5sum,true);

	if (ncs != FS_FOUND || toomany)
	{
		char message[275];

		if (toomany)
			sprintf(message, M_GetText("Too many files loaded to add %s\n"), filename);
		else if (ncs == FS_NOTFOUND)
			sprintf(message, M_GetText("The server doesn't have %s\n"), filename);
		else if (ncs == FS_MD5SUMBAD)
			sprintf(message, M_GetText("Checksum mismatch on %s\n"), filename);
		else
			sprintf(message, M_GetText("Unknown error finding wad file (%s)\n"), filename);

		CONS_Printf("%s",message);

		for (j = 0; j < MAXPLAYERS; j++)
			if (adminplayers[j])
				COM_BufAddText(va("sayto %d %s", adminplayers[j], message));

		return;
	}

	COM_BufAddText(va("addfile %s\n", filename));
}

static void Got_Addfilecmd(UINT8 **cp, INT32 playernum)
{
	char filename[241];
	filestatus_t ncs = FS_NOTCHECKED;
	UINT8 md5sum[16];

	READSTRINGN(*cp, filename, 240);
	READMEM(*cp, md5sum, 16);

	if (playernum != serverplayer)
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal addfile command received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	ncs = findfile(filename,md5sum,true);

	if (ncs != FS_FOUND || !P_AddWadFile(filename, false))
	{
		Command_ExitGame_f();
		if (ncs == FS_FOUND)
		{
			CONS_Printf(M_GetText("The server tried to add %s,\nbut you have too many files added.\nRestart the game to clear loaded files\nand play on this server."), filename);
			M_StartMessage(va("The server added a file \n(%s)\nbut you have too many files added.\nRestart the game to clear loaded files.\n\nPress ESC\n",filename), NULL, MM_NOTHING);
		}
		else if (ncs == FS_NOTFOUND)
		{
			CONS_Printf(M_GetText("The server tried to add %s,\nbut you don't have this file.\nYou need to find it in order\nto play on this server."), filename);
			M_StartMessage(va("The server added a file \n(%s)\nthat you do not have.\n\nPress ESC\n",filename), NULL, MM_NOTHING);
		}
		else if (ncs == FS_MD5SUMBAD)
		{
			CONS_Printf(M_GetText("Checksum mismatch while loading %s.\nMake sure you have the copy of\nthis file that the server has.\n"), filename);
			M_StartMessage(va("Checksum mismatch while loading \n%s.\nThe server seems to have a\ndifferent version of this file.\n\nPress ESC\n",filename), NULL, MM_NOTHING);
		}
		else
		{
			CONS_Printf(M_GetText("Unknown error finding wad file (%s) the server added.\n"), filename);
			M_StartMessage(va("Unknown error trying to load a file\nthat the server added \n(%s).\n\nPress ESC\n",filename), NULL, MM_NOTHING);
		}
		return;
	}

	G_SetGameModified(true, false);
}

static void Command_ListWADS_f(void)
{
	INT32 i = numwadfiles;
	char *tempname;
	CONS_Printf(M_GetText("There are %d wads loaded:\n"),numwadfiles);
	for (i--; i >= 0; i--)
	{
		nameonly(tempname = va("%s", wadfiles[i]->filename));
		if (!i)
			CONS_Printf("\x82 IWAD\x80: %s\n", tempname);
		else if (i <= mainwads)
			CONS_Printf("\x82 * %.2d\x80: %s\n", i, tempname);
		else
			CONS_Printf("   %.2d: %s\n", i, tempname);
	}
}

// =========================================================================
//                            MISC. COMMANDS
// =========================================================================

/** Prints program version.
  */
static void Command_Version_f(void)
{
#ifdef DEVELOP
	CONS_Printf("SRB2Kart %s-%s (%s %s)\n", compbranch, comprevision, compdate, comptime);
#else
	CONS_Printf("SRB2Kart %s (%s %s %s)\n", VERSIONSTRING, compdate, comptime, comprevision);
#endif

	// Base library
#if defined( HAVE_SDL)
	CONS_Printf("SDL ");
#endif

	// OS
	// Would be nice to use SDL_GetPlatform for this
#if defined (_WIN32) || defined (_WIN64)
	CONS_Printf("Windows ");
#elif defined(__linux__)
	CONS_Printf("Linux ");
#elif defined(MACOSX)
	CONS_Printf("macOS ");
#elif defined(UNIXCOMMON)
	CONS_Printf("Unix (Common) ");
#else
	CONS_Printf("Other OS ");
#endif

	// Bitness
	if (sizeof(void*) == 4)
		CONS_Printf("32-bit ");
	else if (sizeof(void*) == 8)
		CONS_Printf("64-bit ");
	else // 16-bit? 128-bit?
		CONS_Printf("Bits Unknown ");

	// Debug build
#ifdef _DEBUG
	CONS_Printf("\x85" "DEBUG " "\x80");
#endif

	// DEVELOP build
#ifdef DEVELOP
	CONS_Printf("\x87" "DEVELOP " "\x80");
#endif

	CONS_Printf("\n");
}

#ifdef UPDATE_ALERT
static void Command_ModDetails_f(void)
{
	CONS_Printf(M_GetText("Mod ID: %d\nMod Version: %d\nCode Base:%d\n"), MODID, MODVERSION, CODEBASE);
}
#endif

// Returns current gametype being used.
//
static void Command_ShowGametype_f(void)
{
	const char *gametypestr = NULL;

	if (!(netgame || multiplayer)) // print "Single player" instead of "Race"
	{
		CONS_Printf(M_GetText("Current gametype is %s\n"), "Single Player");
		return;
	}

	// get name string for current gametype
	if (gametype >= 0 && gametype < NUMGAMETYPES)
		gametypestr = Gametype_Names[gametype];

	if (gametypestr)
		CONS_Printf(M_GetText("Current gametype is %s\n"), gametypestr);
	else // string for current gametype was not found above (should never happen)
		CONS_Printf(M_GetText("Unknown gametype set (%d)\n"), gametype);
}

/** Plays the intro.
  */
static void Command_Playintro_f(void)
{
	if (netgame)
		return;

	if (dirmenu)
		closefilemenu(true);

	F_StartIntro();
}

/** Quits the game immediately.
  */
FUNCNORETURN static ATTRNORETURN void Command_Quit_f(void)
{
	I_Quit();
}

void ItemFinder_OnChange(void)
{
	if (!cv_itemfinder.value)
		return; // it's fine.

	if (!M_SecretUnlocked(SECRET_ITEMFINDER))
	{
		CONS_Printf(M_GetText("You haven't earned this yet.\n"));
		CV_StealthSetValue(&cv_itemfinder, 0);
		return;
	}
	else if (netgame || multiplayer)
	{
		CONS_Printf(M_GetText("This only works in single player.\n"));
		CV_StealthSetValue(&cv_itemfinder, 0);
		return;
	}
}

/** Deals with a pointlimit change by printing the change to the console.
  * If the gametype is single player, cooperative, or race, the pointlimit is
  * silently disabled again.
  *
  * Timelimit and pointlimit can be used at the same time.
  *
  * We don't check immediately for the pointlimit having been reached,
  * because you would get "caught" when turning it up in the menu.
  * \sa cv_pointlimit, TimeLimit_OnChange
  * \author Graue <graue@oceanbase.org>
  */
static void PointLimit_OnChange(void)
{
	// Don't allow pointlimit in Single Player/Co-Op/Race!
	if (server && Playing() && G_RaceGametype())
	{
		if (cv_pointlimit.value)
			CV_StealthSetValue(&cv_pointlimit, 0);
		return;
	}

	if (cv_pointlimit.value)
	{
		CONS_Printf(M_GetText("Levels will end after %s scores %d point%s.\n"),
			G_GametypeHasTeams() ? M_GetText("a team") : M_GetText("someone"),
			cv_pointlimit.value,
			cv_pointlimit.value > 1 ? "s" : "");
	}
	else if (netgame || multiplayer)
		CONS_Printf(M_GetText("Point limit disabled\n"));
}

static void NumLaps_OnChange(void)
{
	if (!G_RaceGametype() || (modeattacking || demo.playback))
		return;

	if (server && Playing()
		&& (netgame || multiplayer)
		&& (mapheaderinfo[gamemap - 1]->levelflags & LF_SECTIONRACE)
		&& (cv_numlaps.value > mapheaderinfo[gamemap - 1]->numlaps))
		CV_StealthSetValue(&cv_numlaps, mapheaderinfo[gamemap - 1]->numlaps);

	// Just don't be verbose
	CONS_Printf(M_GetText("Number of laps set to %d\n"), cv_numlaps.value);
}

static void NetTimeout_OnChange(void)
{
	connectiontimeout = (tic_t)cv_nettimeout.value;
}

static void JoinTimeout_OnChange(void)
{
	jointimeout = (tic_t)cv_jointimeout.value;
}

UINT32 timelimitintics = 0;

/** Deals with a timelimit change by printing the change to the console.
  * If the gametype is single player, cooperative, or race, the timelimit is
  * silently disabled again.
  *
  * Timelimit and pointlimit can be used at the same time.
  *
  * \sa cv_timelimit, PointLimit_OnChange
  */
static void TimeLimit_OnChange(void)
{
	// Don't allow timelimit in Single Player/Co-Op/Race!
	if (server && Playing() && cv_timelimit.value != 0 && G_RaceGametype())
	{
		CV_SetValue(&cv_timelimit, 0);
		return;
	}

	if (cv_timelimit.value != 0)
	{
		CONS_Printf(M_GetText("Levels will end after %d minute%s.\n"),cv_timelimit.value,cv_timelimit.value == 1 ? "" : "s"); // Graue 11-17-2003
		timelimitintics = cv_timelimit.value * 60 * TICRATE;

		//add hidetime for tag too!
		if (G_TagGametype())
			timelimitintics += hidetime * TICRATE;

		// Note the deliberate absence of any code preventing
		//   pointlimit and timelimit from being set simultaneously.
		// Some people might like to use them together. It works.
	}
	else if (netgame || multiplayer)
		CONS_Printf(M_GetText("Time limit disabled\n"));

#ifdef HAVE_DISCORDRPC
	DRPC_UpdatePresence();
#endif
}

/** Adjusts certain settings to match a changed gametype.
  *
  * \param lastgametype The gametype we were playing before now.
  * \sa D_MapChange
  * \author Graue <graue@oceanbase.org>
  * \todo Get rid of the hardcoded stuff, ugly stuff, etc.
  */
void D_GameTypeChanged(INT32 lastgametype)
{
	if (netgame)
	{
		const char *oldgt = NULL, *newgt = NULL;

		if (lastgametype >= 0 && lastgametype < NUMGAMETYPES)
			oldgt = Gametype_Names[lastgametype];
		if (gametype >= 0 && lastgametype < NUMGAMETYPES)
			newgt = Gametype_Names[gametype];

		if (oldgt && newgt)
			CONS_Printf(M_GetText("Gametype was changed from %s to %s\n"), oldgt, newgt);
	}
	// Only do the following as the server, not as remote admin.
	// There will always be a server, and this only needs to be done once.
	if (server && (multiplayer || netgame))
	{
		if (gametype == GT_COMPETITION || gametype == GT_COOP)
			CV_SetValue(&cv_itemrespawn, 0);
		else if (!cv_itemrespawn.changed)
			CV_SetValue(&cv_itemrespawn, 1);

		switch (gametype)
		{
			case GT_MATCH:
			case GT_TEAMMATCH:
				if (!cv_timelimit.changed && !cv_pointlimit.changed) // user hasn't changed limits
				{
					// default settings for match: no timelimit, no pointlimit
					CV_SetValue(&cv_pointlimit, 0);
					CV_SetValue(&cv_timelimit,  0);
				}
				if (!cv_itemrespawntime.changed)
					CV_Set(&cv_itemrespawntime, cv_itemrespawntime.defaultvalue); // respawn normally
				break;
			case GT_TAG:
			case GT_HIDEANDSEEK:
				if (!cv_timelimit.changed && !cv_pointlimit.changed) // user hasn't changed limits
				{
					// default settings for tag: 5 mins, no pointlimit
					// Note that tag mode also uses an alternate timing mechanism in tandem with timelimit.
					CV_SetValue(&cv_timelimit, 5);
					CV_SetValue(&cv_pointlimit, 0);
				}
				if (!cv_itemrespawntime.changed)
					CV_Set(&cv_itemrespawntime, cv_itemrespawntime.defaultvalue); // respawn normally
				break;
			case GT_CTF:
				if (!cv_timelimit.changed && !cv_pointlimit.changed) // user hasn't changed limits
				{
					// default settings for CTF: no timelimit, pointlimit 5
					CV_SetValue(&cv_timelimit, 0);
					CV_SetValue(&cv_pointlimit, 5);
				}
				if (!cv_itemrespawntime.changed)
					CV_Set(&cv_itemrespawntime, cv_itemrespawntime.defaultvalue); // respawn normally
				break;
		}
	}
	else if (!multiplayer && !netgame)
	{
		gametype = GT_RACE; // SRB2kart
		// These shouldn't matter anymore
		//CV_Set(&cv_itemrespawntime, cv_itemrespawntime.defaultvalue);
		//CV_SetValue(&cv_itemrespawn, 0);
	}

	// reset timelimit and pointlimit in race/coop, prevent stupid cheats
	if (server)
	{
		if (G_RaceGametype())
		{
			if (cv_timelimit.value)
				CV_SetValue(&cv_timelimit, 0);
			if (cv_pointlimit.value)
				CV_SetValue(&cv_pointlimit, 0);
		}
		else if ((cv_pointlimit.changed || cv_timelimit.changed) && cv_pointlimit.value)
		{
			if (lastgametype != GT_CTF && gametype == GT_CTF)
				CV_SetValue(&cv_pointlimit, cv_pointlimit.value / 500);
			else if (lastgametype == GT_CTF && gametype != GT_CTF)
				CV_SetValue(&cv_pointlimit, cv_pointlimit.value * 500);
		}
	}

	// When swapping to a gametype that supports spectators,
	// make everyone a spectator initially.
	/*if (G_GametypeHasSpectators())
	{
		INT32 i;
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i])
			{
				players[i].ctfteam = 0;
				players[i].spectator = true;
			}
	}*/

	// don't retain teams in other modes or between changes from ctf to team match.
	// also, stop any and all forms of team scrambling that might otherwise take place.
	if (G_GametypeHasTeams())
	{
		INT32 i;
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i])
				players[i].ctfteam = 0;

		if (server || (IsPlayerAdmin(consoleplayer)))
		{
			CV_StealthSetValue(&cv_teamscramble, 0);
			teamscramble = 0;
		}
	}
}

static void Ringslinger_OnChange(void)
{
	if (!M_SecretUnlocked(SECRET_PANDORA) && !netgame && cv_ringslinger.value && !cv_debug)
	{
		CONS_Printf(M_GetText("You haven't earned this yet.\n"));
		CV_StealthSetValue(&cv_ringslinger, 0);
		return;
	}

	if (cv_ringslinger.value) // Only if it's been turned on
		G_SetGameModified(multiplayer, true);
}

static void Gravity_OnChange(void)
{
	if (!M_SecretUnlocked(SECRET_PANDORA) && !netgame && !cv_debug
		&& strcmp(cv_gravity.string, cv_gravity.defaultvalue))
	{
		CONS_Printf(M_GetText("You haven't earned this yet.\n"));
		CV_StealthSet(&cv_gravity, cv_gravity.defaultvalue);
		return;
	}
#ifndef NETGAME_GRAVITY
	if(netgame)
	{
		CV_StealthSet(&cv_gravity, cv_gravity.defaultvalue);
		return;
	}
#endif

	if (!CV_IsSetToDefault(&cv_gravity))
		G_SetGameModified(multiplayer, true);
	gravity = cv_gravity.value;
}

static void SoundTest_OnChange(void)
{
	if (cv_soundtest.value < 0)
	{
		CV_SetValue(&cv_soundtest, NUMSFX-1);
		return;
	}

	if (cv_soundtest.value >= NUMSFX)
	{
		CV_SetValue(&cv_soundtest, 0);
		return;
	}

	S_StopSounds();
	S_StartSound(NULL, cv_soundtest.value);
}

static void AutoBalance_OnChange(void)
{
	autobalance = (INT16)cv_autobalance.value;
}

static void TeamScramble_OnChange(void)
{
	INT16 i = 0, j = 0, playercount = 0;
	boolean repick = true;
	INT32 blue = 0, red = 0;
	INT32 maxcomposition = 0;
	INT16 newteam = 0;
	INT32 retries = 0;
	boolean success = false;

	// Don't trigger outside level or intermission!
	if (!(gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_VOTING))
		return;

	if (!cv_teamscramble.value)
		teamscramble = 0;

	if (!G_GametypeHasTeams() && (server || IsPlayerAdmin(consoleplayer)))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("This command cannot be used in this gametype.\n"));
		CV_StealthSetValue(&cv_teamscramble, 0);
		return;
	}

	// If a team scramble is already in progress, do not allow another one to be started!
	if (teamscramble)
		return;

retryscramble:

	// Clear related global variables. These will get used again in p_tick.c/y_inter.c as the teams are scrambled.
	memset(&scrambleplayers, 0, sizeof(scrambleplayers));
	memset(&scrambleteams, 0, sizeof(scrambleplayers));
	scrambletotal = scramblecount = 0;
	blue = red = maxcomposition = newteam = playercount = 0;
	repick = true;

	// Put each player's node in the array.
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && !players[i].spectator)
		{
			scrambleplayers[playercount] = i;
			playercount++;
		}
	}

	if (playercount < 2)
	{
		CV_StealthSetValue(&cv_teamscramble, 0);
		return; // Don't scramble one or zero players.
	}

	// Randomly place players on teams.
	if (cv_teamscramble.value == 1)
	{
		maxcomposition = playercount / 2;

		// Now randomly assign players to teams.
		// If the teams get out of hand, assign the rest to the other team.
		for (i = 0; i < playercount; i++)
		{
			if (repick)
				newteam = (INT16)((M_RandomByte() % 2) + 1);

			// One team has the most players they can get, assign the rest to the other team.
			if (red == maxcomposition || blue == maxcomposition)
			{
				if (red == maxcomposition)
					newteam = 2;
				else if (blue == maxcomposition)
					newteam = 1;

				repick = false;
			}

			scrambleteams[i] = newteam;

			if (newteam == 1)
				red++;
			else
				blue++;
		}
	}
	else if (cv_teamscramble.value == 2) // Same as before, except split teams based on current score.
	{
		// Now, sort the array based on points scored.
		for (i = 1; i < playercount; i++)
		{
			for (j = i; j < playercount; j++)
			{
				INT16 tempplayer = 0;

				if ((players[scrambleplayers[i-1]].score > players[scrambleplayers[j]].score))
				{
					tempplayer = scrambleplayers[i-1];
					scrambleplayers[i-1] = scrambleplayers[j];
					scrambleplayers[j] = tempplayer;
				}
			}
		}

		// Now assign players to teams based on score. Scramble in pairs.
		// If there is an odd number, one team will end up with the unlucky slob who has no points. =(
		for (i = 0; i < playercount; i++)
		{
			if (repick)
			{
				newteam = (INT16)((M_RandomByte() % 2) + 1);
				repick = false;
			}
			else
			{
				// We will only randomly pick the team for the first guy.
				// Otherwise, just alternate back and forth, distributing players.
				if (newteam == 1)
					newteam = 2;
				else
					newteam = 1;
			}

			scrambleteams[i] = newteam;
		}
	}

	// Check to see if our random selection actually
	// changed anybody. If not, we run through and try again.
	for (i = 0; i < playercount; i++)
	{
		if (players[scrambleplayers[i]].ctfteam != scrambleteams[i])
			success = true;
	}

	if (!success && retries < 5)
	{
		retries++;
		goto retryscramble; //try again
	}

	// Display a witty message, but only during scrambles specifically triggered by an admin.
	if (cv_teamscramble.value)
	{
		scrambletotal = playercount;
		teamscramble = (INT16)cv_teamscramble.value;

		if (!(gamestate == GS_INTERMISSION && cv_scrambleonchange.value))
			CONS_Printf(M_GetText("Teams will be scrambled next round.\n"));
	}
}

static void Hidetime_OnChange(void)
{
	if (Playing() && G_TagGametype() && leveltime >= (hidetime * TICRATE))
	{
		// Don't allow hidetime changes after it expires.
		CV_StealthSetValue(&cv_hidetime, hidetime);
		return;
	}
	hidetime = cv_hidetime.value;

	//uh oh, gotta change timelimitintics now too
	if (G_TagGametype())
		timelimitintics = (cv_timelimit.value * 60 * TICRATE) + (hidetime * TICRATE);
}

static void Command_Showmap_f(void)
{
	if (gamestate == GS_LEVEL)
	{
		if (mapheaderinfo[gamemap-1]->zonttl[0] && !(mapheaderinfo[gamemap-1]->levelflags & LF_NOZONE))
		{
			if (mapheaderinfo[gamemap-1]->actnum[0])
				CONS_Printf("%s (%d): %s %s %s\n", G_BuildMapName(gamemap), gamemap, mapheaderinfo[gamemap-1]->lvlttl, mapheaderinfo[gamemap-1]->zonttl, mapheaderinfo[gamemap-1]->actnum);
			else
				CONS_Printf("%s (%d): %s %s\n", G_BuildMapName(gamemap), gamemap, mapheaderinfo[gamemap-1]->lvlttl, mapheaderinfo[gamemap-1]->zonttl);
		}
		else
		{
			if (mapheaderinfo[gamemap-1]->actnum[0])
				CONS_Printf("%s (%d): %s %s\n", G_BuildMapName(gamemap), gamemap, mapheaderinfo[gamemap-1]->lvlttl, mapheaderinfo[gamemap-1]->actnum);
			else
				CONS_Printf("%s (%d): %s\n", G_BuildMapName(gamemap), gamemap, mapheaderinfo[gamemap-1]->lvlttl);
		}
	}
	else
		CONS_Printf(M_GetText("You must be in a level to use this.\n"));
}

static void Command_Mapmd5_f(void)
{
	if (gamestate == GS_LEVEL)
	{
		INT32 i;
		char md5tmp[33];
		for (i = 0; i < 16; ++i)
			sprintf(&md5tmp[i*2], "%02x", mapmd5[i]);
		CONS_Printf("%s: %s\n", G_BuildMapName(gamemap), md5tmp);
	}
	else
		CONS_Printf(M_GetText("You must be in a level to use this.\n"));
}

static void Command_ExitLevel_f(void)
{
	if (!(netgame || (multiplayer && gametype != GT_COOP)) && !cv_debug)
		CONS_Printf(M_GetText("This only works in a netgame.\n"));
	else if (!(server || (IsPlayerAdmin(consoleplayer))))
		CONS_Printf(M_GetText("Only the server or a remote admin can use this.\n"));
	else if (gamestate != GS_LEVEL || demo.playback)
		CONS_Printf(M_GetText("You must be in a level to use this.\n"));
	else
		SendNetXCmd(XD_EXITLEVEL, NULL, 0);
}

static void Got_ExitLevelcmd(UINT8 **cp, INT32 playernum)
{
	(void)cp;

	// Ignore duplicate XD_EXITLEVEL commands.
	if (gameaction == ga_completed)
		return;

	if (playernum != serverplayer && !IsPlayerAdmin(playernum))
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal exitlevel command received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	G_ExitLevel();
}

static void Got_SetupVotecmd(UINT8 **cp, INT32 playernum)
{
	INT32 i;
	UINT8 gt, secondgt;

	if (playernum != serverplayer && !IsPlayerAdmin(playernum))
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal vote setup received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	// Get gametype data.
	gt = (UINT8)READUINT8(*cp);
	secondgt = (UINT8)READUINT8(*cp);

	// Strip illegal Encore flag.
	if (gt == (GT_MATCH|0x80))
	{
		gt &= ~0x80;
	}

	// Apply most data.
	for (i = 0; i < VOTEROWSADDSONE; i++)
	{
		votelevels[i][0] = (UINT16)READUINT16(*cp);
		votelevels[i][1] = gt;
		if (!mapheaderinfo[votelevels[i][0]])
			P_AllocMapHeader(votelevels[i][0]);
	}

	// Correct third entry's gametype/Encore status.
	votelevels[2][1] = secondgt;

	// If third entry has an illelegal Encore flag...
	if (secondgt == (GT_MATCH|0x80))
	{
		votelevels[2][1] &= ~0x80;
		// Apply it to the second entry instead, gametype permitting!
		if (gt != GT_MATCH)
		{
			votelevels[1][1] |= 0x80;
		}
	}

	G_SetGamestate(GS_VOTING);
	Y_StartVote();
}

static void Got_ModifyVotecmd(UINT8 **cp, INT32 playernum)
{
	SINT8 voted = READSINT8(*cp);
	UINT8 p = READUINT8(*cp);

	(void)playernum;
	votes[p] = voted;
}

static void Got_PickVotecmd(UINT8 **cp, INT32 playernum)
{
	SINT8 pick = READSINT8(*cp);
	SINT8 level = READSINT8(*cp);

	if (playernum != serverplayer && !IsPlayerAdmin(playernum))
	{
		CONS_Alert(CONS_WARNING, M_GetText("Illegal vote setup received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	Y_SetupVoteFinish(pick, level);
}

/** Prints the number of displayplayers[0].
  *
  * \todo Possibly remove this; it was useful for debugging at one point.
  */
static void Command_Displayplayer_f(void)
{
	CONS_Printf(M_GetText("Displayplayer is %d\n"), displayplayers[0]);
}

/** Quits a game and returns to the title screen.
  *
  */
void Command_ExitGame_f(void)
{
	INT32 i;

	if (dedicated)
	{
		CONS_Printf("This command cannot be used on dedicated server\n");
		return;
	}

	D_QuitNetGame();
	CL_Reset();
	CV_ClearChangedFlags();

	for (i = 0; i < MAXPLAYERS; i++)
		CL_ClearPlayer(i);

	splitscreen = 0;
	SplitScreen_OnChange();
	botingame = false;
	botskin = 0;
	cv_debug = 0;
	emeralds = 0;

	if (dirmenu)
		closefilemenu(true);

	if (!modeattacking)
		D_StartTitle();
}

void Command_Retry_f(void)
{
	if (!(gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_VOTING))
		CONS_Printf(M_GetText("You must be in a level to use this.\n"));
	else if (netgame || multiplayer)
		CONS_Printf(M_GetText("This only works in single player.\n"));
	/*else if (!&players[consoleplayer] || players[consoleplayer].lives <= 1)
		CONS_Printf(M_GetText("You can't retry without any lives remaining!\n"));
	else if (G_IsSpecialStage(gamemap))
		CONS_Printf(M_GetText("You can't retry special stages!\n"));*/
	else
	{
		M_ClearMenus(true);
		G_SetRetryFlag();
	}
}

#ifdef NETGAME_DEVMODE
// Allow the use of devmode in netgames.
static void Fishcake_OnChange(void)
{
	cv_debug = cv_fishcake.value;
	// consvar_t's get changed to default when registered
	// so don't make modifiedgame always on!
	if (cv_debug)
	{
		G_SetGameModified(multiplayer, true);
	}

	else if (cv_debug != cv_fishcake.value)
		CV_SetValue(&cv_fishcake, cv_debug);
}
#endif

/** Reports to the console whether or not the game has been modified.
  *
  * \todo Make it obvious, so a console command won't be necessary.
  * \sa modifiedgame
  * \author Graue <graue@oceanbase.org>
  */
static void Command_Isgamemodified_f(void)
{
	if (majormods)
		CONS_Printf("The game has been modified with major addons, so you cannot play Record Attack.\n");
	else if (savemoddata)
		CONS_Printf("The game has been modified with an addon with its own save data, so you can play Record Attack and earn medals.\n");
	else if (modifiedgame)
		CONS_Printf("The game has been modified with only minor addons. You can play Record Attack, earn medals and unlock extras.\n");
	else
		CONS_Printf("The game has not been modified. You can play Record Attack, earn medals and unlock extras.\n");
}

static void Command_Cheats_f(void)
{
	if (COM_CheckParm("off"))
	{
		if (!(server || (IsPlayerAdmin(consoleplayer))))
			CONS_Printf(M_GetText("Only the server or a remote admin can use this.\n"));
		else
			CV_ResetCheatNetVars();
		return;
	}

	if (CV_CheatsEnabled())
	{
		CONS_Printf(M_GetText("At least one CHEAT-marked variable has been changed -- Cheats are enabled.\n"));
		if (server || (IsPlayerAdmin(consoleplayer)))
			CONS_Printf(M_GetText("Type CHEATS OFF to reset all cheat variables to default.\n"));
	}
	else
		CONS_Printf(M_GetText("No CHEAT-marked variables are changed -- Cheats are disabled.\n"));
}

#ifdef _DEBUG
static void Command_Togglemodified_f(void)
{
	modifiedgame = !modifiedgame;
}

static void Command_Archivetest_f(void)
{
	savebuffer_t save;
	UINT32 i, wrote;
	thinker_t *th;
	if (gamestate != GS_LEVEL)
	{
		CONS_Printf("This command only works in-game, you dummy.\n");
		return;
	}

	// assign mobjnum
	i = 1;
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
		if (th->function.acp1 == (actionf_p1)P_MobjThinker)
			((mobj_t *)th)->mobjnum = i++;

	// allocate buffer
	save.buffer = save.p = ZZ_Alloc(1024);

	// test archive
	CONS_Printf("LUA_Archive...\n");
	LUA_Archive(&save, true);
	WRITEUINT8(save.p, 0x7F);
	wrote = (UINT32)(save.p - save.buffer);

	// clear Lua state, so we can really see what happens!
	CONS_Printf("Clearing state!\n");
	LUA_ClearExtVars();

	// test unarchive
	save.p = save.buffer;
	CONS_Printf("LUA_UnArchive...\n");
	LUA_UnArchive(&save, true);
	i = READUINT8(save.p);
	if (i != 0x7F || wrote != (UINT32)(save.p - save.buffer))
		CONS_Printf("Savegame corrupted. (write %u, read %u)\n", wrote, (UINT32)(save.p - save.buffer));

	// free buffer
	Z_Free(save.buffer);
	CONS_Printf("Done. No crash.\n");
}
#endif

/** Makes a change to ::cv_forceskin take effect immediately.
  *
  * \sa Command_SetForcedSkin_f, cv_forceskin, forcedskin
  * \author Graue <graue@oceanbase.org>
  */
static void ForceSkin_OnChange(void)
{
	// NOT in SP, silly!
	if (!(netgame || multiplayer))
		return;

	if (cv_forceskin.value < 0)
		CONS_Printf("The server has lifted the forced skin restrictions.\n");
	else
	{
		CONS_Printf("The server is restricting all players to skin \"%s\".\n",cv_forceskin.string);
		ForceAllSkins(cv_forceskin.value);
	}
}

//Allows the player's name to be changed if cv_mute is off.
static void Name_OnChange(void)
{
	if (cv_mute.value && !(server || IsPlayerAdmin(consoleplayer)))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You may not change your name when chat is muted.\n"));
		CV_StealthSet(&cv_playername, player_names[consoleplayer]);
	}
	else
		SendNameAndColor();

}

static void Name2_OnChange(void)
{
	if (cv_mute.value) //Secondary player can't be admin.
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You may not change your name when chat is muted.\n"));
		CV_StealthSet(&cv_playername2, player_names[displayplayers[1]]);
	}
	else
		SendNameAndColor2();
}

static void Name3_OnChange(void)
{
	if (cv_mute.value) //Third player can't be admin.
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You may not change your name when chat is muted.\n"));
		CV_StealthSet(&cv_playername3, player_names[displayplayers[2]]);
	}
	else
		SendNameAndColor3();
}

static void Name4_OnChange(void)
{
	if (cv_mute.value) //Secondary player can't be admin.
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You may not change your name when chat is muted.\n"));
		CV_StealthSet(&cv_playername4, player_names[displayplayers[3]]);
	}
	else
		SendNameAndColor4();
}

// sends the follower change for players
static void Follower_OnChange(void)
{

	if (!Playing())
		return; // don't send anything there.
		
	if (P_PlayerMoving(consoleplayer))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You can't change your follower at the moment.\n"));
		
		if (players[consoleplayer].followerskin == -1)
			CV_StealthSet(&cv_follower, "None");
		else
			CV_StealthSet(&cv_follower, followers[players[consoleplayer].followerskin].name);
		return;
	}

	SendNameAndColor();
}

// sends the follower change for players
static void Follower2_OnChange(void)
{
	if (!Playing() || !splitscreen)
		return; // don't send anything there.
		
	if (P_PlayerMoving(displayplayers[1]))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You can't change your follower at the moment.\n"));
		if (players[displayplayers[1]].followerskin == -1)
			CV_StealthSet(&cv_follower2, "None");
		else
			CV_StealthSet(&cv_follower2, followers[players[displayplayers[1]].followerskin].name);
		return;
	}

	SendNameAndColor2();
}

// sends the follower change for players
static void Follower3_OnChange(void)
{

	if (!Playing() || splitscreen < 2)
		return; // don't send anything there.
		
	if (P_PlayerMoving(displayplayers[2]))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You can't change your follower at the moment.\n"));
		if (players[displayplayers[2]].followerskin == -1)
			CV_StealthSet(&cv_follower3, "None");
		else
			CV_StealthSet(&cv_follower3, followers[players[displayplayers[2]].followerskin].name);
		return;
	}

	SendNameAndColor3();
}

// sends the follower change for players
static void Follower4_OnChange(void)
{

	if (!Playing() || splitscreen < 3)
		return; // don't send anything there.
		
	if (P_PlayerMoving(displayplayers[3]))
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You can't change your follower at the moment.\n"));
		if (players[displayplayers[3]].followerskin == -1)
			CV_StealthSet(&cv_follower4, "None");
		else
			CV_StealthSet(&cv_follower4, followers[players[displayplayers[3]].followerskin].name);
		return;
	}

	SendNameAndColor4();
}

// About the same as Color_OnChange but for followers.
static void Followercolor_OnChange(void)
{
	if (!Playing())
		return; // do whatever you want if you aren't in the game or don't have a follower.

	if (!P_PlayerMoving(consoleplayer))
	{
		// Color change menu scrolling fix is no longer necessary
		SendNameAndColor();
	}
	else
	{
		CV_StealthSetValue(&cv_followercolor, players[consoleplayer].followercolor);
	}
}

static void Followercolor2_OnChange(void)
{
	if (!Playing() || !splitscreen)
		return; // do whatever you want if you aren't in the game or don't have a follower.

	if (!P_PlayerMoving(displayplayers[1]))
	{
		// Color change menu scrolling fix is no longer necessary
		SendNameAndColor2();
	}
	else
	{
		CV_StealthSetValue(&cv_followercolor, players[displayplayers[1]].followercolor);
	}
}

static void Followercolor3_OnChange(void)
{
	if (!Playing() || splitscreen < 2)
		return; // do whatever you want if you aren't in the game or don't have a follower.

	if (!P_PlayerMoving(displayplayers[2]))
	{
		// Color change menu scrolling fix is no longer necessary
		SendNameAndColor3();
	}
	else
	{
		CV_StealthSetValue(&cv_followercolor, players[displayplayers[2]].followercolor);
	}
}

static void Followercolor4_OnChange(void)
{
	if (!Playing() || splitscreen < 3)
		return; // do whatever you want if you aren't in the game or don't have a follower.

	if (!P_PlayerMoving(displayplayers[3]))
	{
		// Color change menu scrolling fix is no longer necessary
		SendNameAndColor4();
	}
	else
	{
		CV_StealthSetValue(&cv_followercolor, players[displayplayers[3]].followercolor);
	}
}

/** Sends a skin change for the console player, unless that player is moving.
  * \sa cv_skin, Skin2_OnChange, Color_OnChange
  * \author Graue <graue@oceanbase.org>
  */
static void Skin_OnChange(void)
{
	Skin_FindRealNameSkin(&cv_skin);

	if (!Playing())
		return; // do whatever you want

	if (!(cv_debug || devparm) && !(multiplayer || netgame) // In single player.
		&& (gamestate != GS_WAITINGPLAYERS)) // allows command line -warp x +skin y
	{
		CV_StealthSet(&cv_skin, skins[players[consoleplayer].skin].name);
		return;
	}

	if (CanChangeSkin(consoleplayer) && !P_PlayerMoving(consoleplayer))
		SendNameAndColor();
	else
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You can't change your skin at the moment.\n"));
		CV_StealthSet(&cv_skin, skins[players[consoleplayer].skin].name);
	}
}

/** Sends a skin change for the secondary splitscreen player, unless that
  * player is moving.
  * \sa cv_skin2, Skin_OnChange, Color2_OnChange
  * \author Graue <graue@oceanbase.org>
  */
static void Skin2_OnChange(void)
{
	Skin_FindRealNameSkin(&cv_skin2);

	if (!Playing() || !splitscreen)
		return; // do whatever you want

	if (CanChangeSkin(displayplayers[1]) && !P_PlayerMoving(displayplayers[1]))
		SendNameAndColor2();
	else
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You can't change your skin at the moment.\n"));
		CV_StealthSet(&cv_skin2, skins[players[displayplayers[1]].skin].name);
	}
}

static void Skin3_OnChange(void)
{
	Skin_FindRealNameSkin(&cv_skin3);

	if (!Playing() || splitscreen < 2)
		return; // do whatever you want

	if (CanChangeSkin(displayplayers[2]) && !P_PlayerMoving(displayplayers[2]))
		SendNameAndColor3();
	else
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You can't change your skin at the moment.\n"));
		CV_StealthSet(&cv_skin3, skins[players[displayplayers[2]].skin].name);
	}
}

static void Skin4_OnChange(void)
{
	Skin_FindRealNameSkin(&cv_skin4);

	if (!Playing() || splitscreen < 3)
		return; // do whatever you want

	if (CanChangeSkin(displayplayers[3]) && !P_PlayerMoving(displayplayers[3]))
		SendNameAndColor4();
	else
	{
		CONS_Alert(CONS_NOTICE, M_GetText("You can't change your skin at the moment.\n"));
		CV_StealthSet(&cv_skin4, skins[players[displayplayers[3]].skin].name);
	}
}

static void Command_ListSkins(void)
{
	int i;
	int page = 0;
	int numpages = numskins / 10 + (numskins % 10 ? 1 : 0);
	int longest_name = 0;

	if (COM_Argc() > 1)
	{
		if (sscanf(COM_Argv(1), " %d", &page) == 0)
		{
			CONS_Printf("Expected a number.\n");
			return;
		}
		else if (page > numpages)
		{
			CONS_Printf("There are only %d pages.\n", numpages);
			return;
		}
		else if (page < 0)
		{
			CONS_Printf("Page number cannot be negative.\n");
			return;
		}
		else if (page == 0)
		{
			CONS_Printf("There is no 0th page. (What are you a programmer?)\n");
			return;
		}

		--page;
	}

	CONS_Printf("Total %d skins. Page (%d/%d)\n", numskins, page + 1, numpages);

	//  Find the longest name and realname in the skins list. (used for alignment)
	for (i = page * 10; i < numskins && i < (page + 1) * 10; i++)
	{
		int length;
		skin_t *skin = &skins[i];

		if ((length = strlen(skin->name)) > longest_name)
			longest_name = length;
	}

	for (i = page * 10; i < numskins && i < (page + 1) * 10; i++)
	{
		skin_t *skin = &skins[i];

		CONS_Printf(
			"%s[%-*s]\x80 %s\n",
			HU_SkinColorToConsoleColor(skin->prefcolor),
			longest_name,
			skin->name,
			skin->realname);
	}

	if (page + 1 != numpages)
	{
		CONS_Printf("Use \"listskins %d\" to see the next page.\n", page + 2);
	}
	else
	{
		CONS_Printf("There are no more pages.\n");
	}
}

static void Command_SkinSearch(void)
{	
	size_t i;
	UINT16 s;
	UINT16 ic = 0;
	//skin_t *skininput = &skins[s];
	for (i = 1; i < COM_Argc(); i++){
		for( s = 0 ; s <  numallskins ; s++ )
		{
			skin_t *skininput = &skins[s];
			if (strcasestr(skininput->realname,COM_Argv(i)))
			{	
				ic++;
				CONS_Printf("%d. %s%s:\x80 %s\n", ic,HU_SkinColorToConsoleColor(skininput->prefcolor),skininput->realname,skininput->name);
			}
		}
	}
				CONS_Printf("Total %d skins.\n", ic);
}

static void Command_FollowerSearch(void)
{	
	size_t i;
	UINT16 s;
	UINT16 ic = 0;
	for (i = 1; i < COM_Argc(); i++){
		for( s = 0 ; s <  numfollowers ; s++ )
		{
			follower_t *followerinput = &followers[s];
			if (strcasestr(followerinput->name,COM_Argv(i)))
			{	
				ic++;
				CONS_Printf("%d. %s\"%s\"\x80\n", ic,HU_SkinColorToConsoleColor(followerinput->defaultcolor),followerinput->name);
			}
		}
	}
				CONS_Printf("Total %d followers.\n", ic);
}

/** Sends a color change for the console player, unless that player is moving.
  * \sa cv_playercolor, Color2_OnChange, Skin_OnChange
  * \author Graue <graue@oceanbase.org>
  */
static void Color_OnChange(void)
{
	if (!Playing())
		return; // do whatever you want

	if (!(cv_debug || devparm) && !(multiplayer || netgame || modeattacking)) // In single player.
	{
		CV_StealthSet(&cv_skin, skins[players[consoleplayer].skin].name);
		return;
	}

	if (!P_PlayerMoving(consoleplayer))
	{
		// Color change menu scrolling fix is no longer necessary
		SendNameAndColor();
	}
	else
	{
		CV_StealthSetValue(&cv_playercolor,
			players[consoleplayer].skincolor);
	}
}

/** Sends a color change for the secondary splitscreen player, unless that
  * player is moving.
  * \sa cv_playercolor2, Color_OnChange, Skin2_OnChange
  * \author Graue <graue@oceanbase.org>
  */
static void Color2_OnChange(void)
{
	if (!Playing() || !splitscreen)
		return; // do whatever you want

	if (!P_PlayerMoving(displayplayers[1]))
	{
		// Color change menu scrolling fix is no longer necessary
		SendNameAndColor2();
	}
	else
	{
		CV_StealthSetValue(&cv_playercolor2,
			players[displayplayers[1]].skincolor);
	}
}

static void Color3_OnChange(void)
{
	if (!Playing() || splitscreen < 2)
		return; // do whatever you want

	if (!P_PlayerMoving(displayplayers[2]))
	{
		// Color change menu scrolling fix is no longer necessary
		SendNameAndColor3();
	}
	else
	{
		CV_StealthSetValue(&cv_playercolor3,
			players[displayplayers[2]].skincolor);
	}
}

static void Color4_OnChange(void)
{
	if (!Playing() || splitscreen < 3)
		return; // do whatever you want

	if (!P_PlayerMoving(displayplayers[3]))
	{
		// Color change menu scrolling fix is no longer necessary
		SendNameAndColor4();
	}
	else
	{
		CV_StealthSetValue(&cv_playercolor4,
			players[displayplayers[3]].skincolor);
	}
}

/** Displays the result of the chat being muted or unmuted.
  * The server or remote admin should already know and be able to talk
  * regardless, so this is only displayed to clients.
  *
  * \sa cv_mute
  * \author Graue <graue@oceanbase.org>
  */
static void Mute_OnChange(void)
{
	/*if (server || (IsPlayerAdmin(consoleplayer)))
		return;*/
	// Kinda dumb IMO, you should be able to see confirmation for having muted the chat as the host or admin.

	if (leveltime <= 1)
		return;	// avoid having this notification put in our console / log when we boot the server.

	if (cv_mute.value)
		HU_AddChatText(M_GetText("\x82*Chat has been muted."), false);
	else
		HU_AddChatText(M_GetText("\x82*Chat is no longer muted."), false);
}

/** Hack to clear all changed flags after game start.
  * A lot of code (written by dummies, obviously) uses COM_BufAddText() to run
  * commands and change consvars, especially on game start. This is problematic
  * because CV_ClearChangedFlags() needs to get called on game start \b after
  * all those commands are run.
  *
  * Here's how it's done: the last thing in COM_BufAddText() is "dummyconsvar
  * 1", so we end up here, where dummyconsvar is reset to 0 and all the changed
  * flags are set to 0.
  *
  * \todo Fix the aforementioned code and make this hack unnecessary.
  * \sa cv_dummyconsvar
  * \author Graue <graue@oceanbase.org>
  */
static void DummyConsvar_OnChange(void)
{
	if (cv_dummyconsvar.value == 1)
	{
		CV_SetValue(&cv_dummyconsvar, 0);
		CV_ClearChangedFlags();
	}
}

static void Command_ShowScores_f(void)
{
	UINT8 i;

	if (!(netgame || multiplayer))
	{
		CONS_Printf(M_GetText("This only works in a netgame.\n"));
		return;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
			// FIXME: %lu? what's wrong with %u? ~Callum (produces warnings...)
			CONS_Printf(M_GetText("%s's score is %u\n"), player_names[i], players[i].score);
	}
	CONS_Printf(M_GetText("The pointlimit is %d\n"), cv_pointlimit.value);

}

static void Command_ShowTime_f(void)
{
	if (!(netgame || multiplayer))
	{
		CONS_Printf(M_GetText("This only works in a netgame.\n"));
		return;
	}

	CONS_Printf(M_GetText("The current time is %f.\nThe timelimit is %f\n"), (double)leveltime/TICRATE, (double)timelimitintics/TICRATE);
}

// SRB2Kart: On change messages
static void BaseNumLaps_OnChange(void)
{
	if (gamestate == GS_LEVEL)
	{
		if (cv_basenumlaps.value)
			CONS_Printf(M_GetText("Number of laps will be changed to %d next round.\n"), cv_basenumlaps.value);
		else
			CONS_Printf(M_GetText("Number of laps will be changed to map defaults next round.\n"));
	}
}


static void KartFrantic_OnChange(void)
{
	if ((boolean)cv_kartfrantic.value != franticitems && gamestate == GS_LEVEL && leveltime > starttime)
		CONS_Printf(M_GetText("Frantic items will be turned %s next round.\n"), cv_kartfrantic.value ? M_GetText("on") : M_GetText("off"));
	else
	{
		CONS_Printf(M_GetText("Frantic items has been turned %s.\n"), cv_kartfrantic.value ? M_GetText("on") : M_GetText("off"));
		franticitems = (boolean)cv_kartfrantic.value;
	}
}

static void KartSpeed_OnChange(void)
{
	if (G_RaceGametype())
	{
		if ((UINT8)cv_kartspeed.value != gamespeed && gamestate == GS_LEVEL && leveltime > starttime)
			CONS_Printf(M_GetText("Game speed will be changed to \"%s\" next round.\n"), cv_kartspeed.string);
		else
		{
			CONS_Printf(M_GetText("Game speed has been changed to \"%s\".\n"), cv_kartspeed.string);
			gamespeed = (UINT8)cv_kartspeed.value;
		}
	}
}

//Battle Speed
static void KartBattleSpeed_OnChange(void)
{
	if (G_BattleGametype())
	{
		if ((UINT8)cv_kartbattlespeed.value != gamespeed && gamestate == GS_LEVEL && leveltime > starttime)
			CONS_Printf(M_GetText("Game speed will be changed to \"%s\" next Battle.\n"), cv_kartbattlespeed.string);
		else
		{
			CONS_Printf(M_GetText("Game speed has been changed to \"%s\".\n"),cv_kartbattlespeed.string);
			gamespeed = (UINT8)cv_kartbattlespeed.value;
		}
	}
}





static void KartEncore_OnChange(void)
{
	if (G_RaceGametype())
	{
		if ((boolean)cv_kartencore.value != encoremode && gamestate == GS_LEVEL /*&& leveltime > starttime*/)
			CONS_Printf(M_GetText("Encore Mode will be turned %s next round.\n"), cv_kartencore.value ? M_GetText("on") : M_GetText("off"));
		else
			CONS_Printf(M_GetText("Encore Mode has been turned %s.\n"), cv_kartencore.value ? M_GetText("on") : M_GetText("off"));
	}
}

static void KartComeback_OnChange(void)
{
	if (G_BattleGametype())
	{
		if ((boolean)cv_kartcomeback.value != comeback && gamestate == GS_LEVEL && leveltime > starttime)
			CONS_Printf(M_GetText("Karma Comeback will be turned %s next round.\n"), cv_kartcomeback.value ? M_GetText("on") : M_GetText("off"));
		else
		{
			CONS_Printf(M_GetText("Karma Comeback has been turned %s.\n"), cv_kartcomeback.value ? M_GetText("on") : M_GetText("off"));
			comeback = (boolean)cv_kartcomeback.value;
		}
	}
}

static void KartEliminateLast_OnChange(void)
{
	if (G_RaceGametype() && cv_karteliminatelast.value)
		P_CheckRacers();
}

void Got_DiscordInfo(UINT8 **p, INT32 playernum)
{
	if (playernum != serverplayer /*&& !IsPlayerAdmin(playernum)*/)
	{
		// protect against hacked/buggy client
		CONS_Alert(CONS_WARNING, M_GetText("Illegal Discord info command received from %s\n"), player_names[playernum]);
		if (server)
		{
			UINT8 buf[2];

			buf[0] = (UINT8)playernum;
			buf[1] = KICK_MSG_CON_FAIL;
			SendNetXCmd(XD_KICK, &buf, 2);
		}
		return;
	}

	// Don't do anything with the information if we don't have Discord RP support
#ifdef HAVE_DISCORDRPC
	discordInfo.maxPlayers = READUINT8(*p);
	discordInfo.joinsAllowed = (boolean)READUINT8(*p);
	discordInfo.everyoneCanInvite = (boolean)READUINT8(*p);

	DRPC_UpdatePresence();
#else
	(*p) += 3;
#endif
}

#undef VOTEROWS
#undef VOTEROWSADDSONE
