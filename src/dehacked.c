// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  dehacked.c
/// \brief Load dehacked file and change tables and text

#include "d_ticcmd.h"
#include "doomdef.h"
#include "d_main.h" // for srb2home
#include "g_game.h"
#include "sounds.h"
#include "info.h"
#include "d_think.h"
#include "m_argv.h"
#include "z_zone.h"
#include "w_wad.h"
#include "m_menu.h"
#include "m_misc.h"
#include "filesrch.h" // for refreshdirmenu
#include "f_finale.h"
#include "dehacked.h"
#include "st_stuff.h"
#include "i_system.h"
#include "p_local.h" // for var1 and var2, and some constants
#include "p_setup.h"
#include "r_data.h"
#include "r_patch.h"
#include "r_sky.h"
#include "r_draw.h" // translation colormap consts (for lua)
#include "fastcmp.h"
#include "lua_script.h"
#include "lua_glib.h"
#include "lua_hook.h"
#include "d_clisrv.h"
#include "v_video.h" // video flags (for lua)
#include "r_things.h"	// for followers

#include "lua_script.h"
#include "lua_libs.h"

#include "m_cond.h"

#define REQUIRE_MATHLIB_GUID "{748fcbc8-6480-4013-ac4e-7afce6cab766}"

// Free slot names
// The crazy word-reading stuff uses these.
static char *FREE_STATES[NUMSTATEFREESLOTS];
char *FREE_MOBJS[NUMMOBJFREESLOTS];
static UINT8 used_spr[(NUMSPRITEFREESLOTS / 8) + 1]; // Bitwise flag for sprite freeslot in use! I would use ceil() here if I could, but it only saves 1 byte of memory anyway.
#define initfreeslots() {\
memset(FREE_STATES,0,sizeof(char *) * NUMSTATEFREESLOTS);\
memset(FREE_MOBJS,0,sizeof(char *) * NUMMOBJFREESLOTS);\
memset(used_spr,0,sizeof(UINT8) * ((NUMSPRITEFREESLOTS / 8) + 1));\
}

// Crazy word-reading stuff
/// \todo Put these in a seperate file or something.
static mobjtype_t get_mobjtype(const char *word);
static statenum_t get_state(const char *word);
static spritenum_t get_sprite(const char *word);
static sfxenum_t get_sfx(const char *word);
#ifdef MUSICSLOT_COMPATIBILITY
static UINT16 get_mus(const char *word, UINT8 dehacked_mode);
#endif
static hudnum_t get_huditem(const char *word);

boolean deh_loaded = false;
static int dbg_line;

ATTRINLINE static FUNCINLINE char myfget_color(MYFILE *f)
{
	char c = *f->curpos++;
	if (c == '^') // oh, nevermind then.
		return '^';

	if (c >= '0' && c <= '9')
		return 0x80+(c-'0');
	return 0x80; // Unhandled -- default to no color
}

ATTRINLINE static FUNCINLINE char myfget_hex(MYFILE *f)
{
	char c = *f->curpos++, endchr = 0;
	if (c == '\\') // oh, nevermind then.
		return '\\';

	if (c >= '0' && c <= '9')
		endchr += (c-'0') << 4;
	else if (c >= 'A' && c <= 'F')
		endchr += ((c-'A') + 10) << 4;
	else if (c >= 'a' && c <= 'f')
		endchr += ((c-'a') + 10) << 4;
	else // invalid. stop and return a question mark.
		return '?';

	c = *f->curpos++;
	if (c >= '0' && c <= '9')
		endchr += (c-'0');
	else if (c >= 'A' && c <= 'F')
		endchr += ((c-'A') + 10);
	else if (c >= 'a' && c <= 'f')
		endchr += ((c-'a') + 10);
	else // invalid. stop and return a question mark.
		return '?';

	return endchr;
}

char *myfgets(char *buf, size_t bufsize, MYFILE *f)
{
	size_t i = 0;
	if (myfeof(f))
		return NULL;
	// we need one byte for a null terminated string
	bufsize--;
	while (i < bufsize && !myfeof(f))
	{
		char c = *f->curpos++;
		if (c == '^')
			buf[i++] = myfget_color(f);
		else if (c == '\\')
			buf[i++] = myfget_hex(f);
		else if (c != '\r')
			buf[i++] = c;
		if (c == '\n')
			break;
	}
	buf[i] = '\0';

	dbg_line++;
	return buf;
}

static char *myhashfgets(char *buf, size_t bufsize, MYFILE *f)
{
	size_t i = 0;
	if (myfeof(f))
		return NULL;
	// we need one byte for a null terminated string
	bufsize--;
	while (i < bufsize && !myfeof(f))
	{
		char c = *f->curpos++;
		if (c == '^')
			buf[i++] = myfget_color(f);
		else if (c == '\\')
			buf[i++] = myfget_hex(f);
		else if (c != '\r')
			buf[i++] = c;
		if (c == '\n') // Ensure debug line is right...
			dbg_line++;
		if (c == '#')
			break;
	}
	i++;
	buf[i] = '\0';

	return buf;
}

static INT32 deh_num_warning = 0;

FUNCPRINTF static void deh_warning(const char *first, ...)
{
	va_list argptr;
	char *buf = Z_Malloc(1000, PU_STATIC, NULL);

	va_start(argptr, first);
	vsnprintf(buf, 1000, first, argptr); // sizeof only returned 4 here. it didn't like that pointer.
	va_end(argptr);

	if(dbg_line == -1) // Not in a SOC, line number unknown.
		CONS_Alert(CONS_WARNING, "%s\n", buf);
	else
		CONS_Alert(CONS_WARNING, "Line %u: %s\n", dbg_line, buf);

	deh_num_warning++;

	Z_Free(buf);
}

static void deh_strlcpy(char *dst, const char *src, size_t size, const char *warntext)
{
	size_t len = strlen(src)+1; // Used to determine if truncation has been done
	if (len > size)
		deh_warning("%s exceeds max length of %s", warntext, sizeu1(size-1));
	strlcpy(dst, src, size);
}

/* ======================================================================== */
// Load a dehacked file format
/* ======================================================================== */
/* a sample to see
                   Thing 1 (Player)       {           // MT_PLAYER
INT32 doomednum;     ID # = 3232              -1,             // doomednum
INT32 spawnstate;    Initial frame = 32       "PLAY",         // spawnstate
INT32 spawnhealth;   Hit points = 3232        100,            // spawnhealth
INT32 seestate;      First moving frame = 32  "PLAY_RUN1",    // seestate
INT32 seesound;      Alert sound = 32         sfx_None,       // seesound
INT32 reactiontime;  Reaction time = 3232     0,              // reactiontime
INT32 attacksound;   Attack sound = 32        sfx_None,       // attacksound
INT32 painstate;     Injury frame = 32        "PLAY_PAIN",    // painstate
INT32 painchance;    Pain chance = 3232       255,            // painchance
INT32 painsound;     Pain sound = 32          sfx_plpain,     // painsound
INT32 meleestate;    Close attack frame = 32  "NULL",         // meleestate
INT32 missilestate;  Far attack frame = 32    "PLAY_ATK1",    // missilestate
INT32 deathstate;    Death frame = 32         "PLAY_DIE1",    // deathstate
INT32 xdeathstate;   Exploding frame = 32     "PLAY_XDIE1",   // xdeathstate
INT32 deathsound;    Death sound = 32         sfx_pldeth,     // deathsound
INT32 speed;         Speed = 3232             0,              // speed
INT32 radius;        Width = 211812352        16*FRACUNIT,    // radius
INT32 height;        Height = 211812352       56*FRACUNIT,    // height
INT32 dispoffset;    DispOffset = 0           0,              // dispoffset
INT32 mass;          Mass = 3232              100,            // mass
INT32 damage;        Missile damage = 3232    0,              // damage
INT32 activesound;   Action sound = 32        sfx_None,       // activesound
INT32 flags;         Bits = 3232              MF_SOLID|MF_SHOOTABLE|MF_DROPOFF|MF_PICKUP|MF_NOTDMATCH,
INT32 raisestate;    Respawn frame = 32       S_NULL          // raisestate
                                         }, */

static INT32 searchvalue(const char *s)
{
	while (s[0] != '=' && s[0])
		s++;
	if (s[0] == '=')
		return atoi(&s[1]);
	else
	{
		deh_warning("No value found");
		return 0;
	}
}

// These are for clearing all of various things
static void clear_conditionsets(void)
{
	UINT8 i;
	for (i = 0; i < MAXCONDITIONSETS; ++i)
		M_ClearConditionSet(i+1);
}

static void clear_levels(void)
{
	INT16 i;

	// This is potentially dangerous but if we're resetting these headers,
	// we may as well try to save some memory, right?
	for (i = 0; i < NUMMAPS; ++i)
	{
		if (!mapheaderinfo[i])
			continue;

		// Custom map header info
		// (no need to set num to 0, we're freeing the entire header shortly)
		Z_Free(mapheaderinfo[i]->customopts);

		P_DeleteGrades(i);
		Z_Free(mapheaderinfo[i]);
		mapheaderinfo[i] = NULL;
	}

	// Realloc the one for the current gamemap as a safeguard
	P_AllocMapHeader(gamemap-1);
}

static boolean findFreeSlot(INT32 *num)
{
	// Send the character select entry to a free slot.
	while (*num < MAXSKINS && PlayerMenu[*num].status != IT_DISABLED)
		*num = *num+1;

	// No more free slots. :(
	if (*num >= MAXSKINS)
		return false;

	// Found one! ^_^
	return true;
}

// Reads a player.
// For modifying the character select screen
static void readPlayer(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	INT32 i;
	boolean slotfound = false;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			if (fastcmp(word, "PLAYERTEXT"))
			{
				char *playertext = NULL;

				if (!slotfound && (slotfound = findFreeSlot(&num)) == false)
					goto done;
				PlayerMenu[num].status = IT_CALL;

				for (i = 0; i < MAXLINELEN-3; i++)
				{
					if (s[i] == '=')
					{
						playertext = &s[i+2];
						break;
					}
				}
				if (playertext)
				{
					strcpy(description[num].notes, playertext);
					strcat(description[num].notes, myhashfgets(playertext, sizeof (description[num].notes), f));
				}
				else
					strcpy(description[num].notes, "");

				// For some reason, cutting the string did not work above. Most likely due to strcpy or strcat...
				// It works down here, though.
				{
					INT32 numline = 0;
					for (i = 0; (size_t)i < sizeof(description[num].notes)-1; i++)
					{
						if (numline < 20 && description[num].notes[i] == '\n')
							numline++;

						if (numline >= 20 || description[num].notes[i] == '\0' || description[num].notes[i] == '#')
							break;
					}
				}
				description[num].notes[strlen(description[num].notes)-1] = '\0';
				description[num].notes[i] = '\0';
				continue;
			}

			word2 = strtok(NULL, " = ");
			if (word2)
				strupr(word2);
			else
				break;

			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';
			i = atoi(word2);

			if (fastcmp(word, "PICNAME"))
			{
				if (!slotfound && (slotfound = findFreeSlot(&num)) == false)
					goto done;
				PlayerMenu[num].status = IT_CALL;
				strncpy(description[num].picname, word2, 8);
			}
			else if (fastcmp(word, "STATUS"))
			{
				// Limit the status to only IT_DISABLED and IT_CALL
				if (i)
					i = IT_CALL;
				else
					i = IT_DISABLED;

				/*
					You MAY disable previous entries if you so desire...
					But try to enable something that's already enabled and you will be sent to a free slot.

					Because of this, you are allowed to edit any previous entrys you like, but only if you
					signal that you are purposely doing so by disabling and then reenabling the slot.

					... Or use MENUPOSITION first, that works too. Hell, you could edit multiple character
					slots in a single section that way, due to how SOC editing works.
				*/
				if (i != IT_DISABLED && !slotfound && (slotfound = findFreeSlot(&num)) == false)
					goto done;
				PlayerMenu[num].status = (INT16)i;
			}
			else if (fastcmp(word, "SKINNAME"))
			{
				// Send to free slot.
				if (!slotfound && (slotfound = findFreeSlot(&num)) == false)
					goto done;
				PlayerMenu[num].status = IT_CALL;

				strlcpy(description[num].skinname, word2, sizeof description[num].skinname);
				strlwr(description[num].skinname);
			}
			else
				deh_warning("readPlayer %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

done:
	Z_Free(s);
}

static int freeslotusage[2][2] = {{0, 0}, {0, 0}}; // [S_, MT_][max, previous .wad's max]

void DEH_UpdateMaxFreeslots(void)
{
	freeslotusage[0][1] = freeslotusage[0][0];
	freeslotusage[1][1] = freeslotusage[1][0];
}

// TODO: Figure out how to do undolines for this....
static void readfreeslots(MYFILE *f)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word,*type;
	char *tmp;
	int i;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			type = strtok(s, "_");
			if (type)
				strupr(type);
			else
				break;

			word = strtok(NULL, "\n");
			if (word)
				strupr(word);
			else
				break;

			// TODO: Check for existing freeslot mobjs/states/etc. and make errors.
			// TODO: Name too long (truncated) warnings.
			if (fastcmp(type, "SFX"))
			{
				CONS_Printf("Sound sfx_%s allocated.\n",word);
				S_AddSoundFx(word, false, 0, false);
			}
			else if (fastcmp(type, "SPR"))
			{
				for (i = SPR_FIRSTFREESLOT; i <= SPR_LASTFREESLOT; i++)
				{
					if (used_spr[(i-SPR_FIRSTFREESLOT)/8] & (1<<(i%8)))
					{
						if (!sprnames[i][4] && memcmp(sprnames[i],word,4)==0)
							sprnames[i][4] = (char)f->wad;
						continue; // Already allocated, next.
					}
					// Found a free slot!
					strncpy(sprnames[i],word,4);
					//sprnames[i][4] = 0;
					CONS_Printf("Sprite SPR_%s allocated.\n",word);

					LUA_InvalidateMathlibCache(va("SPR_%s", word));

					used_spr[(i-SPR_FIRSTFREESLOT)/8] |= 1<<(i%8); // Okay, this sprite slot has been named now.
					break;
				}
				if (i > SPR_LASTFREESLOT)
					I_Error("Out of Sprite Freeslots while allocating \"%s\"\nLoad less addons to fix this.", word);
			}
			else if (fastcmp(type, "S"))
			{
				for (i = 0; i < NUMSTATEFREESLOTS; i++)
					if (!FREE_STATES[i]) {
						CONS_Printf("State S_%s allocated.\n",word);

						LUA_InvalidateMathlibCache(va("S_%s", word));

						FREE_STATES[i] = Z_Malloc(strlen(word)+1, PU_STATIC, NULL);
						strcpy(FREE_STATES[i],word);
						freeslotusage[0][0]++;
						break;
					}

				if (i == NUMSTATEFREESLOTS)
					I_Error("Out of State Freeslots while allocating \"%s\"\nLoad less addons to fix this.", word);
			}
			else if (fastcmp(type, "MT"))
			{
				for (i = 0; i < NUMMOBJFREESLOTS; i++)
					if (!FREE_MOBJS[i]) {
						CONS_Printf("MobjType MT_%s allocated.\n",word);

						LUA_InvalidateMathlibCache(va("MT_%s", word));

						FREE_MOBJS[i] = Z_Malloc(strlen(word)+1, PU_STATIC, NULL);
						strcpy(FREE_MOBJS[i],word);
						freeslotusage[1][0]++;
						break;
					}

				if (i == NUMMOBJFREESLOTS)
					I_Error("Out of Mobj Freeslots while allocating \"%s\"\nLoad less addons to fix this.", word);
			}
			else
				deh_warning("Freeslots: unknown enum class '%s' for '%s_%s'", type, type, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

// This here is our current only way to make followers.
INT32 numfollowers = 0;

static void readfollower(MYFILE *f)
{
	char *s;
	char *word, *word2;
	char *tmp;
	char testname[SKINNAMESIZE+1];

	boolean nameset;
	INT32 fallbackstate = 0;
	INT32 res;
	INT32 i;

	if (numfollowers >= MAXFOLLOWERS)
	{
		I_Error("Out of Followers\nLoad less addons to fix this.");
		return;
	}

	s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);

	// Ready the default variables for followers. We will overwrite them as we go! We won't set the name or states RIGHT HERE as this is handled down instead.
	followers[numfollowers].mode = FOLLOWERMODE_FLOAT;
	followers[numfollowers].scale = FRACUNIT;
	followers[numfollowers].atangle =  FixedAngle(230 * FRACUNIT);
	followers[numfollowers].dist = 32*FRACUNIT;
	followers[numfollowers].zoffs = 32*FRACUNIT;
	followers[numfollowers].horzlag = 3*FRACUNIT;
	followers[numfollowers].vertlag = 6*FRACUNIT;
	followers[numfollowers].anglelag = 8*FRACUNIT;
	followers[numfollowers].bobspeed = TICRATE*2;
	followers[numfollowers].bobamp = 4;
	followers[numfollowers].hitconfirmtime = TICRATE;
	followers[numfollowers].defaultcolor = FOLLOWERCOLOR_MATCH;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			word2 = strtok(NULL, " = ");

			if (!word2)
				break;

			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';

			if (fastcmp(word, "NAME"))
			{
				strlcpy(followers[numfollowers].name, word2, SKINNAMESIZE+1);
				nameset = true;
			}
			else if (fastcmp(word, "ICON"))
			{
				deh_warning("Follower \"%s\": icon is unsupported. Please consider removing it from your SOC file.", followers[numfollowers].name);
			}
			else if (fastcmp(word, "CATEGORY"))
			{
				deh_warning("Follower \"%s\": category is unsupported. Please consider removing it from your SOC file.", followers[numfollowers].name);
			}
			else if (fastcmp(word, "MODE"))
			{
				if (word2)
					strupr(word2);

				if (fastcmp(word2, "FLOAT") || fastcmp(word2, "DEFAULT"))
					followers[numfollowers].mode = FOLLOWERMODE_FLOAT;
				else if (fastcmp(word2, "GROUND"))
					followers[numfollowers].mode = FOLLOWERMODE_GROUND;
				else
					deh_warning("Follower %d: unknown follower mode '%s'", numfollowers, word2);
			}
			else if (fastcmp(word, "DEFAULTCOLOR"))
			{
				INT32 j;
				for (j = 0; j < MAXSKINCOLORS +2; j++)	// +2 because of Match and Opposite
				{
					if (!stricmp(Followercolor_cons_t[j].strvalue, word2))
					{
						followers[numfollowers].defaultcolor = Followercolor_cons_t[j].value;
						break;
					}
				}

				if (j == MAXSKINCOLORS+2)
				{
					deh_warning("Follower %d: unknown follower color '%s'", numfollowers, word2);
				}
			}
			else if (fastcmp(word, "SCALE"))
			{
				followers[numfollowers].scale = get_number(word2);
			}
			else if (fastcmp(word, "BUBBLESCALE"))
			{
				deh_warning("Follower \"%s\": bubblescale is unsupported. Please consider removing it from your SOC file.", followers[numfollowers].name);
			}
			else if (fastcmp(word, "HORNSOUND"))
			{
				deh_warning("Follower \"%s\": hornsound is unsupported. Please consider removing it from your SOC file.", followers[numfollowers].name);
			}
			else if (fastcmp(word, "ATANGLE"))
			{
				followers[numfollowers].atangle = (angle_t)(get_number(word2) * ANG1);
			}
			else if (fastcmp(word, "HORZLAG"))
			{
				followers[numfollowers].horzlag = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "VERTLAG"))
			{
				followers[numfollowers].vertlag = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "ANGLELAG"))
			{
				followers[numfollowers].anglelag = (fixed_t)get_number(word2);;
			}
			else if (fastcmp(word, "BOBSPEED"))
			{
				followers[numfollowers].bobspeed = (tic_t)get_number(word2);
			}
			else if (fastcmp(word, "BOBAMP"))
			{
				followers[numfollowers].bobamp = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "ZOFFSET") || (fastcmp(word, "ZOFFS")))
			{
				followers[numfollowers].zoffs = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "DISTANCE") || (fastcmp(word, "DIST")))
			{
				followers[numfollowers].dist = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "HEIGHT"))
			{
				followers[numfollowers].height = (fixed_t)get_number(word2);
			}
			else if (fastcmp(word, "IDLESTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].idlestate = get_number(word2);
				fallbackstate = followers[numfollowers].idlestate;
			}
			else if (fastcmp(word, "FOLLOWSTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].followstate = get_number(word2);
			}
			else if (fastcmp(word, "HURTSTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].hurtstate = get_number(word2);
			}
			else if (fastcmp(word, "LOSESTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].losestate = get_number(word2);
			}
			else if (fastcmp(word, "WINSTATE"))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].winstate = get_number(word2);
			}
			else if (fastcmp(word, "HITSTATE") || (fastcmp(word, "HITCONFIRMSTATE")))
			{
				if (word2)
					strupr(word2);
				followers[numfollowers].hitconfirmstate = get_number(word2);
			}
			else if (fastcmp(word, "HITTIME") || (fastcmp(word, "HITCONFIRMTIME")))
			{
				followers[numfollowers].hitconfirmtime = (INT32)atoi(word2);
			}
			else
				deh_warning("Follower %d: unknown word '%s'", numfollowers, word);
		}
	} while (!myfeof(f)); // finish when the line is empty
	
	if (!nameset)
	{
		// well this is problematic.
		strlcpy(followers[numfollowers].name, va("Follower%d", numfollowers), SKINNAMESIZE+1);
		(strcpy)(testname, followers[numfollowers].name);
	}
	else
	{
		(strcpy)(testname, followers[numfollowers].name);

		// now that the skin name is ready, post process the actual name to turn the underscores into spaces!
		for (i = 0; followers[numfollowers].name[i]; i++)
		{
			if (followers[numfollowers].name[i] == '_')
				followers[numfollowers].name[i] = ' ';
		}

		res = R_FollowerAvailable(followers[numfollowers].name);
		if (res > -1)	// yikes, someone else has stolen our name already
		{
			deh_warning("Follower%d: Name \"%s\" already in use!", numfollowers, testname);
			strlcpy(followers[numfollowers].name, va("Follower%d", numfollowers), SKINNAMESIZE+1);
		}
	}

	// fallbacks for variables
	// Print a warning if the variable is on a weird value and set it back to the minimum available if that's the case.
	
	if (followers[numfollowers].mode < FOLLOWERMODE_FLOAT || followers[numfollowers].mode >= FOLLOWERMODE__MAX)
	{
		followers[numfollowers].mode = FOLLOWERMODE_FLOAT;
		deh_warning("Follower '%s': Value for 'mode' should be between %d and %d.", testname, FOLLOWERMODE_FLOAT, FOLLOWERMODE__MAX-1);
	}
	
#define FALLBACK(field, field2, threshold, set) \
if (followers[numfollowers].field < threshold) \
{ \
	followers[numfollowers].field = set; \
	deh_warning("Follower '%s': Value for '%s' is too low! Minimum should be %d. Value was overwritten to %d.", testname, field2, set, set); \
} \

	FALLBACK(dist, "DIST", 0, 0);
	FALLBACK(zoffs, "ZOFFS", 0, 0);
	FALLBACK(horzlag, "HORZLAG", FRACUNIT, FRACUNIT);
	FALLBACK(vertlag, "VERTLAG", FRACUNIT, FRACUNIT);
	FALLBACK(anglelag, "ANGLELAG", FRACUNIT, FRACUNIT);
	FALLBACK(bobamp, "BOBAMP", 0, 0);
	FALLBACK(bobspeed, "BOBSPEED", 0, 0);
	FALLBACK(scale, "SCALE", 1, 1);		
	FALLBACK(hitconfirmtime, "HITCONFIRMTIME", 1, 1);
#undef FALLBACK

	// also check if we forgot states. If we did, we will set any missing state to the follower's idlestate.
	// Print a warning in case we don't have a fallback and set the state to S_INVISIBLE (rather than S_NULL) if unavailable.

#define NOSTATE(field, field2) \
if (!followers[numfollowers].field) \
{ \
	followers[numfollowers].field = fallbackstate ? fallbackstate : S_INVISIBLE; \
	if (!fallbackstate) \
		deh_warning("Follower '%s' is missing state definition for '%s', no idlestate fallback was found", testname, field2); \
} \

	NOSTATE(idlestate, "IDLESTATE");
	NOSTATE(followstate, "FOLLOWSTATE");
	NOSTATE(hurtstate, "HURTSTATE");
	NOSTATE(losestate, "LOSESTATE");
	NOSTATE(winstate, "WINSTATE");
	NOSTATE(hitconfirmstate, "HITCONFIRMSTATE");
#undef NOSTATE

	CONS_Printf("Added follower '%s'\n", testname);
	numfollowers++;	// add 1 follower
	Z_Free(s);
}


static void readthing(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word, *word2;
	char *tmp;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			word2 = strtok(NULL, " = ");
			if (word2)
				strupr(word2);
			else
				break;
			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';

			if (fastcmp(word, "MAPTHINGNUM") || fastcmp(word, "DOOMEDNUM"))
			{
				mobjinfo[num].doomednum = (INT32)atoi(word2);
			}
			else if (fastcmp(word, "SPAWNSTATE"))
			{
				mobjinfo[num].spawnstate = get_number(word2);
			}
			else if (fastcmp(word, "SPAWNHEALTH"))
			{
				mobjinfo[num].spawnhealth = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "SEESTATE"))
			{
				mobjinfo[num].seestate = get_number(word2);
			}
			else if (fastcmp(word, "SEESOUND"))
			{
				mobjinfo[num].seesound = get_number(word2);
			}
			else if (fastcmp(word, "REACTIONTIME"))
			{
				mobjinfo[num].reactiontime = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "ATTACKSOUND"))
			{
				mobjinfo[num].attacksound = get_number(word2);
			}
			else if (fastcmp(word, "PAINSTATE"))
			{
				mobjinfo[num].painstate = get_number(word2);
			}
			else if (fastcmp(word, "PAINCHANCE"))
			{
				mobjinfo[num].painchance = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "PAINSOUND"))
			{
				mobjinfo[num].painsound = get_number(word2);
			}
			else if (fastcmp(word, "MELEESTATE"))
			{
				mobjinfo[num].meleestate = get_number(word2);
			}
			else if (fastcmp(word, "MISSILESTATE"))
			{
				mobjinfo[num].missilestate = get_number(word2);
			}
			else if (fastcmp(word, "DEATHSTATE"))
			{
				mobjinfo[num].deathstate = get_number(word2);
			}
			else if (fastcmp(word, "DEATHSOUND"))
			{
				mobjinfo[num].deathsound = get_number(word2);
			}
			else if (fastcmp(word, "XDEATHSTATE"))
			{
				mobjinfo[num].xdeathstate = get_number(word2);
			}
			else if (fastcmp(word, "SPEED"))
			{
				mobjinfo[num].speed = get_number(word2);
			}
			else if (fastcmp(word, "RADIUS"))
			{
				mobjinfo[num].radius = get_number(word2);
			}
			else if (fastcmp(word, "HEIGHT"))
			{
				mobjinfo[num].height = get_number(word2);
			}
			else if (fastcmp(word, "DISPOFFSET"))
			{
				mobjinfo[num].dispoffset = get_number(word2);
			}
			else if (fastcmp(word, "MASS"))
			{
				mobjinfo[num].mass = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "DAMAGE"))
			{
				mobjinfo[num].damage = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "ACTIVESOUND"))
			{
				mobjinfo[num].activesound = get_number(word2);
			}
			else if (fastcmp(word, "FLAGS"))
			{
				mobjinfo[num].flags = (INT32)get_number(word2);
			}
			else if (fastcmp(word, "RAISESTATE"))
			{
				mobjinfo[num].raisestate = get_number(word2);
			}
			else
				deh_warning("Thing %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

static const struct {
	const char *name;
	const UINT16 flag;
} TYPEOFLEVEL[] = {
	{"SOLO",TOL_SP},
	{"SP",TOL_SP},
	{"SINGLEPLAYER",TOL_SP},
	{"SINGLE",TOL_SP},

	{"COOP",TOL_COOP},
	{"CO-OP",TOL_COOP},

	{"COMPETITION",TOL_COMPETITION},
	{"RACE",TOL_RACE},

	{"MATCH",TOL_MATCH},
	{"BATTLE",TOL_MATCH}, // SRB2kart
	{"TAG",TOL_TAG},
	{"CTF",TOL_CTF},

	{"CUSTOM",TOL_CUSTOM},

	{"2D",TOL_2D},
	{"MARIO",TOL_MARIO},
	{"NIGHTS",TOL_NIGHTS},
	{"TV",TOL_TV},

	{"XMAS",TOL_XMAS},
	{"CHRISTMAS",TOL_XMAS},
	{"WINTER",TOL_XMAS},

	//{"KART",TOL_KART}, // SRB2kart

	{NULL, 0}
};

static void readlevelheader(MYFILE *f, INT32 num, INT32 wadnum)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	//char *word3; // Non-uppercase version of word2
	char *tmp;
	INT32 i;

	// Reset all previous map header information
	// This call automatically saves all previous information when DELFILE is defined.
	// We don't need to do it ourselves.
	P_AllocMapHeader((INT16)(num-1));

	if (!strstr(wadfiles[wadnum]->filename, ".soc"))
		mapwads[num-1] = wadnum;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Set / reset word, because some things (Lua.) move it
			word = s;

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp += 2;
			i = atoi(word2); // used for numerical settings

			// CHEAP HACK: move this over here for lowercase subtitles
			if (fastcmp(word, "SUBTITLE"))
			{
				deh_strlcpy(mapheaderinfo[num-1]->subttl, word2,
					sizeof(mapheaderinfo[num-1]->subttl), va("Level header %d: subtitle", num));
				continue;
			}
			else if (fastcmp(word, "LEVELNAME"))
			{
				deh_strlcpy(mapheaderinfo[num-1]->lvlttl, word2,
					sizeof(mapheaderinfo[num-1]->lvlttl), va("Level header %d: levelname", num));
				continue;
			}
			else if (fastcmp(word, "ZONETITLE"))
			{
				deh_strlcpy(mapheaderinfo[num-1]->zonttl, word2,
					sizeof(mapheaderinfo[num-1]->zonttl), va("Level header %d: zonetitle", num));
				continue;
			}

			// Lua custom options also go above, contents may be case sensitive.
			if (fastncmp(word, "LUA.", 4))
			{
				UINT8 j;
				customoption_t *modoption;

				// Note: we actualy strlwr word here, so things are made a little easier for Lua
				strlwr(word);
				word += 4; // move past "lua."

				// ... and do a simple name sanity check; the name must start with a letter
				if (*word < 'a' || *word > 'z')
				{
					deh_warning("Level header %d: invalid custom option name \"%s\"", num, word);
					continue;
				}

				// Sanity limit of 128 params
				if (mapheaderinfo[num-1]->numCustomOptions == 128)
				{
					deh_warning("Level header %d: too many custom parameters", num);
					continue;
				}
				j = mapheaderinfo[num-1]->numCustomOptions++;

				mapheaderinfo[num-1]->customopts =
					Z_Realloc(mapheaderinfo[num-1]->customopts,
						sizeof(customoption_t) * mapheaderinfo[num-1]->numCustomOptions, PU_STATIC, NULL);

				// Newly allocated
				modoption = &mapheaderinfo[num-1]->customopts[j];

				strncpy(modoption->option, word,  31);
				modoption->option[31] = '\0';
				strncpy(modoption->value,  word2, 255);
				modoption->value[255] = '\0';
				continue;
			}

			// Now go to uppercase
			strupr(word2);

			// NiGHTS grades
			if (fastncmp(word, "GRADES", 6))
			{
				UINT8 mare = (UINT8)atoi(word + 6);

				if (mare <= 0 || mare > 8)
				{
					deh_warning("Level header %d: unknown word '%s'", num, word);
					continue;
				}

				P_AddGradesForMare((INT16)(num-1), mare-1, word2);
			}

			// Strings that can be truncated
			else if (fastcmp(word, "SCRIPTNAME"))
			{
				deh_strlcpy(mapheaderinfo[num-1]->scriptname, word2,
					sizeof(mapheaderinfo[num-1]->scriptname), va("Level header %d: scriptname", num));
			}
			else if (fastcmp(word, "RUNSOC"))
			{
				deh_strlcpy(mapheaderinfo[num-1]->runsoc, word2,
					sizeof(mapheaderinfo[num-1]->runsoc), va("Level header %d: runsoc", num));
			}
			else if (fastcmp(word, "ACT"))
			{
				/*if (i >= 0 && i < 20) // 0 for no act number, TTL1 through TTL19
					mapheaderinfo[num-1]->actnum = (UINT8)i;
				else
					deh_warning("Level header %d: invalid act number %d", num, i);*/
				deh_strlcpy(mapheaderinfo[num-1]->actnum, word2,
					sizeof(mapheaderinfo[num-1]->actnum), va("Level header %d: actnum", num));
			}
			else if (fastcmp(word, "NEXTLEVEL"))
			{
				if      (fastcmp(word2, "TITLE"))      i = 1100;
				else if (fastcmp(word2, "EVALUATION")) i = 1101;
				else if (fastcmp(word2, "CREDITS"))    i = 1102;
				else
				// Support using the actual map name,
				// i.e., Nextlevel = AB, Nextlevel = FZ, etc.

				// Convert to map number
				if (word2[0] >= 'A' && word2[0] <= 'Z' && word2[2] == '\0')
					i = M_MapNumber(word2[0], word2[1]);

				mapheaderinfo[num-1]->nextlevel = (INT16)i;
			}
			else if (fastcmp(word, "TYPEOFLEVEL"))
			{
				if (i) // it's just a number
					mapheaderinfo[num-1]->typeoflevel = (UINT16)i;
				else
				{
					UINT16 tol = 0;
					tmp = strtok(word2,",");
					do {
						for (i = 0; TYPEOFLEVEL[i].name; i++)
							if (fastcmp(tmp, TYPEOFLEVEL[i].name))
								break;
						if (!TYPEOFLEVEL[i].name)
							deh_warning("Level header %d: unknown typeoflevel flag %s\n", num, tmp);
						tol |= TYPEOFLEVEL[i].flag;
					} while((tmp = strtok(NULL,",")) != NULL);
					mapheaderinfo[num-1]->typeoflevel = tol;
				}
			}
			else if (fastcmp(word, "MUSIC"))
			{
				if (fastcmp(word2, "NONE"))
					mapheaderinfo[num-1]->musname[0] = 0; // becomes empty string
				else
				{
					deh_strlcpy(mapheaderinfo[num-1]->musname, word2,
						sizeof(mapheaderinfo[num-1]->musname), va("Level header %d: music", num));
				}
			}
#ifdef MUSICSLOT_COMPATIBILITY
			else if (fastcmp(word, "MUSICSLOT"))
			{
				i = get_mus(word2, true);
				if (i && i <= 1035)
					snprintf(mapheaderinfo[num-1]->musname, 7, "%sM", G_BuildMapName(i));
				else if (i && i <= 1050)
					strncpy(mapheaderinfo[num-1]->musname, compat_special_music_slots[i - 1036], 6);
				else
					mapheaderinfo[num-1]->musname[0] = 0; // becomes empty string
				mapheaderinfo[num-1]->musname[6] = 0;
			}
#endif
			else if (fastcmp(word, "MUSICTRACK"))
				mapheaderinfo[num-1]->mustrack = ((UINT16)i - 1);
			else if (fastcmp(word, "MUSICPOS"))
				mapheaderinfo[num-1]->muspos = (UINT32)get_number(word2);
			else if (fastcmp(word, "MUSICINTERFADEOUT"))
				mapheaderinfo[num-1]->musinterfadeout = (UINT32)get_number(word2);
			else if (fastcmp(word, "MUSICINTER"))
				deh_strlcpy(mapheaderinfo[num-1]->musintername, word2,
					sizeof(mapheaderinfo[num-1]->musintername), va("Level header %d: intermission music", num));
			else if (fastcmp(word, "FORCECHARACTER"))
			{
				strlcpy(mapheaderinfo[num-1]->forcecharacter, word2, SKINNAMESIZE+1);
				strlwr(mapheaderinfo[num-1]->forcecharacter); // skin names are lowercase
			}
			else if (fastcmp(word, "WEATHER"))
				mapheaderinfo[num-1]->weather = (UINT8)get_number(word2);
			else if (fastcmp(word, "SKYNUM"))
				mapheaderinfo[num-1]->skynum = (INT16)i;
			else if (fastcmp(word, "INTERSCREEN"))
				memcpy(mapheaderinfo[num-1]->interscreen, word2, 8);
			else if (fastcmp(word, "PRECUTSCENENUM"))
				mapheaderinfo[num-1]->precutscenenum = (UINT8)i;
			else if (fastcmp(word, "CUTSCENENUM"))
				mapheaderinfo[num-1]->cutscenenum = (UINT8)i;
			else if (fastcmp(word, "COUNTDOWN"))
				mapheaderinfo[num-1]->countdown = (INT16)i;
			else if (fastcmp(word, "PALETTE"))
				mapheaderinfo[num-1]->palette = (UINT16)i;
			else if (fastcmp(word, "ENCOREPAL"))
				mapheaderinfo[num-1]->encorepal = (UINT16)i;
			else if (fastcmp(word, "NUMLAPS"))
				mapheaderinfo[num-1]->numlaps = (UINT8)i;
			else if (fastcmp(word, "UNLOCKABLE"))
			{
				if (i >= 0 && i <= MAXUNLOCKABLES) // 0 for no unlock required, anything else requires something
					mapheaderinfo[num-1]->unlockrequired = (SINT8)i - 1;
				else
					deh_warning("Level header %d: invalid unlockable number %d", num, i);
			}
			else if (fastcmp(word, "LEVELSELECT"))
				mapheaderinfo[num-1]->levelselect = (UINT8)i;
			else if (fastcmp(word, "SKYBOXSCALE"))
				mapheaderinfo[num-1]->skybox_scalex = mapheaderinfo[num-1]->skybox_scaley = mapheaderinfo[num-1]->skybox_scalez = (INT16)i;
			else if (fastcmp(word, "SKYBOXSCALEX"))
				mapheaderinfo[num-1]->skybox_scalex = (INT16)i;
			else if (fastcmp(word, "SKYBOXSCALEY"))
				mapheaderinfo[num-1]->skybox_scaley = (INT16)i;
			else if (fastcmp(word, "SKYBOXSCALEZ"))
				mapheaderinfo[num-1]->skybox_scalez = (INT16)i;

			else if (fastcmp(word, "BONUSTYPE"))
			{
				if      (fastcmp(word2, "NONE"))   i = -1;
				else if (fastcmp(word2, "NORMAL")) i =  0;
				else if (fastcmp(word2, "BOSS"))   i =  1;
				else if (fastcmp(word2, "ERZ3"))   i =  2;

				if (i >= -1 && i <= 2) // -1 for no bonus. Max is 2.
					mapheaderinfo[num-1]->bonustype = (SINT8)i;
				else
					deh_warning("Level header %d: invalid bonus type number %d", num, i);
			}

			else if (fastcmp(word, "SAVEOVERRIDE"))
			{
				if      (fastcmp(word2, "DEFAULT")) i = SAVE_DEFAULT;
				else if (fastcmp(word2, "ALWAYS"))  i = SAVE_ALWAYS;
				else if (fastcmp(word2, "NEVER"))   i = SAVE_NEVER;

				if (i >= SAVE_NEVER && i <= SAVE_ALWAYS)
					mapheaderinfo[num-1]->saveoverride = (SINT8)i;
				else
					deh_warning("Level header %d: invalid save override number %d", num, i);
			}

			else if (fastcmp(word, "LEVELFLAGS"))
				mapheaderinfo[num-1]->levelflags = get_number(word2);
			else if (fastcmp(word, "MENUFLAGS"))
				mapheaderinfo[num-1]->menuflags = get_number(word2);

			// SRB2Kart
			/*else if (fastcmp(word, "AUTOMAP"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->automap = true;
				else
					mapheaderinfo[num-1]->automap = false;
			}*/
			else if (fastcmp(word, "MOBJSCALE"))
				mapheaderinfo[num-1]->mobj_scale = get_number(word2);

			// Individual triggers for level flags, for ease of use (and 2.0 compatibility)
			else if (fastcmp(word, "SCRIPTISFILE"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->levelflags |= LF_SCRIPTISFILE;
				else
					mapheaderinfo[num-1]->levelflags &= ~LF_SCRIPTISFILE;
			}
			else if (fastcmp(word, "SPEEDMUSIC"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->levelflags |= LF_SPEEDMUSIC;
				else
					mapheaderinfo[num-1]->levelflags &= ~LF_SPEEDMUSIC;
			}
			else if (fastcmp(word, "NOSSMUSIC"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->levelflags |= LF_NOSSMUSIC;
				else
					mapheaderinfo[num-1]->levelflags &= ~LF_NOSSMUSIC;
			}
			else if (fastcmp(word, "NORELOAD"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->levelflags |= LF_NORELOAD;
				else
					mapheaderinfo[num-1]->levelflags &= ~LF_NORELOAD;
			}
			else if (fastcmp(word, "NOZONE"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->levelflags |= LF_NOZONE;
				else
					mapheaderinfo[num-1]->levelflags &= ~LF_NOZONE;
			}
			else if (fastcmp(word, "SECTIONRACE"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->levelflags |= LF_SECTIONRACE;
				else
					mapheaderinfo[num-1]->levelflags &= ~LF_SECTIONRACE;
			}

			// Individual triggers for menu flags
			else if (fastcmp(word, "HIDDEN"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->menuflags |= LF2_HIDEINMENU;
				else
					mapheaderinfo[num-1]->menuflags &= ~LF2_HIDEINMENU;
			}
			else if (fastcmp(word, "HIDEINSTATS"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->menuflags |= LF2_HIDEINSTATS;
				else
					mapheaderinfo[num-1]->menuflags &= ~LF2_HIDEINSTATS;
			}
			else if (fastcmp(word, "RECORDATTACK") || fastcmp(word, "TIMEATTACK"))
			{ // TIMEATTACK is an accepted alias
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->menuflags |= LF2_RECORDATTACK;
				else
					mapheaderinfo[num-1]->menuflags &= ~LF2_RECORDATTACK;
			}
			else if (fastcmp(word, "NIGHTSATTACK"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->menuflags |= LF2_NIGHTSATTACK;
				else
					mapheaderinfo[num-1]->menuflags &= LF2_NIGHTSATTACK;
			}
			else if (fastcmp(word, "NOVISITNEEDED"))
			{
				if (i || word2[0] == 'T' || word2[0] == 'Y')
					mapheaderinfo[num-1]->menuflags |= LF2_NOVISITNEEDED;
				else
					mapheaderinfo[num-1]->menuflags &= ~LF2_NOVISITNEEDED;
			}
			else
				deh_warning("Level header %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

static void readcutscenescene(MYFILE *f, INT32 num, INT32 scenenum)
{
	char *s = Z_Calloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	INT32 i;
	UINT16 usi;
	UINT8 picid;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			if (fastcmp(word, "SCENETEXT"))
			{
				char *scenetext = NULL;
				char *buffer;
				const int bufferlen = 4096;

				for (i = 0; i < MAXLINELEN; i++)
				{
					if (s[i] == '=')
					{
						scenetext = &s[i+2];
						break;
					}
				}

				if (!scenetext)
				{
					Z_Free(cutscenes[num]->scene[scenenum].text);
					cutscenes[num]->scene[scenenum].text = NULL;
					continue;
				}

				for (i = 0; i < MAXLINELEN; i++)
				{
					if (s[i] == '\0')
					{
						s[i] = '\n';
						s[i+1] = '\0';
						break;
					}
				}

				buffer = Z_Malloc(4096, PU_STATIC, NULL);
				strcpy(buffer, scenetext);

				strcat(buffer,
					myhashfgets(scenetext, bufferlen
					- strlen(buffer) - 1, f));

				// A cutscene overwriting another one...
				Z_Free(cutscenes[num]->scene[scenenum].text);

				cutscenes[num]->scene[scenenum].text = Z_StrDup(buffer);

				Z_Free(buffer);

				continue;
			}

			word2 = strtok(NULL, " = ");
			if (word2)
				strupr(word2);
			else
				break;

			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';
			i = atoi(word2);
			usi = (UINT16)i;


			if (fastcmp(word, "NUMBEROFPICS"))
			{
				cutscenes[num]->scene[scenenum].numpics = (UINT8)i;
			}
			else if (fastncmp(word, "PIC", 3))
			{
				picid = (UINT8)atoi(word + 3);
				if (picid > 8 || picid == 0)
				{
					deh_warning("CutSceneScene %d: unknown word '%s'", num, word);
					continue;
				}
				--picid;

				if (fastcmp(word+4, "NAME"))
				{
					strncpy(cutscenes[num]->scene[scenenum].picname[picid], word2, 8);
				}
				else if (fastcmp(word+4, "HIRES"))
				{
					cutscenes[num]->scene[scenenum].pichires[picid] = (UINT8)(i || word2[0] == 'T' || word2[0] == 'Y');
				}
				else if (fastcmp(word+4, "DURATION"))
				{
					cutscenes[num]->scene[scenenum].picduration[picid] = usi;
				}
				else if (fastcmp(word+4, "XCOORD"))
				{
					cutscenes[num]->scene[scenenum].xcoord[picid] = usi;
				}
				else if (fastcmp(word+4, "YCOORD"))
				{
					cutscenes[num]->scene[scenenum].ycoord[picid] = usi;
				}
				else
					deh_warning("CutSceneScene %d: unknown word '%s'", num, word);
			}
			else if (fastcmp(word, "MUSIC"))
			{
				strncpy(cutscenes[num]->scene[scenenum].musswitch, word2, 7);
				cutscenes[num]->scene[scenenum].musswitch[6] = 0;
			}
#ifdef MUSICSLOT_COMPATIBILITY
			else if (fastcmp(word, "MUSICSLOT"))
			{
				i = get_mus(word2, true);
				if (i && i <= 1035)
					snprintf(cutscenes[num]->scene[scenenum].musswitch, 7, "%sM", G_BuildMapName(i));
				else if (i && i <= 1050)
					strncpy(cutscenes[num]->scene[scenenum].musswitch, compat_special_music_slots[i - 1036], 7);
				else
					cutscenes[num]->scene[scenenum].musswitch[0] = 0; // becomes empty string
				cutscenes[num]->scene[scenenum].musswitch[6] = 0;
			}
#endif
			else if (fastcmp(word, "MUSICTRACK"))
			{
				cutscenes[num]->scene[scenenum].musswitchflags = ((UINT16)i) & MUSIC_TRACKMASK;
			}
			else if (fastcmp(word, "MUSICPOS"))
			{
				cutscenes[num]->scene[scenenum].musswitchposition = (UINT32)get_number(word2);
			}
			else if (fastcmp(word, "MUSICLOOP"))
			{
				cutscenes[num]->scene[scenenum].musicloop = (UINT8)(i || word2[0] == 'T' || word2[0] == 'Y');
			}
			else if (fastcmp(word, "TEXTXPOS"))
			{
				cutscenes[num]->scene[scenenum].textxpos = usi;
			}
			else if (fastcmp(word, "TEXTYPOS"))
			{
				cutscenes[num]->scene[scenenum].textypos = usi;
			}
			else if (fastcmp(word, "FADEINID"))
			{
				cutscenes[num]->scene[scenenum].fadeinid = (UINT8)i;
			}
			else if (fastcmp(word, "FADEOUTID"))
			{
				cutscenes[num]->scene[scenenum].fadeoutid = (UINT8)i;
			}
			else if (fastcmp(word, "FADECOLOR"))
			{
				cutscenes[num]->scene[scenenum].fadecolor = (UINT8)i;
			}
			else
				deh_warning("CutSceneScene %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

static void readcutscene(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	char *tmp;
	INT32 value;

	// Allocate memory for this cutscene if we don't yet have any
	if (!cutscenes[num])
		cutscenes[num] = Z_Calloc(sizeof (cutscene_t), PU_STATIC, NULL);

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			word2 = strtok(NULL, " ");
			if (word2)
				value = atoi(word2);
			else
			{
				deh_warning("No value for token %s", word);
				continue;
			}

			if (fastcmp(word, "NUMSCENES"))
			{
				cutscenes[num]->numscenes = value;
			}
			else if (fastcmp(word, "SCENE"))
			{
				if (1 <= value && value <= 128)
				{
					readcutscenescene(f, num, value - 1);
				}
				else
					deh_warning("Scene number %d out of range (1 - 128)", value);

			}
			else
				deh_warning("Cutscene %d: unknown word '%s', Scene <num> expected.", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

static void readhuditem(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 i;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp += 2;
			strupr(word2);

			i = atoi(word2); // used for numerical settings

			if (fastcmp(word, "X"))
			{
				hudinfo[num].x = i;
			}
			else if (fastcmp(word, "Y"))
			{
				hudinfo[num].y = i;
			}
			else
				deh_warning("Level header %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}


// IMPORTANT!
// DO NOT FORGET TO SYNC THIS LIST WITH THE ACTIONNUM ENUM IN INFO.H
actionpointer_t actionpointers[] =
{
	{{A_Explode},              "A_EXPLODE"},
	{{A_Pain},                 "A_PAIN"},
	{{A_Fall},                 "A_FALL"},
	{{A_MonitorPop},           "A_MONITORPOP"},
	{{A_Look},                 "A_LOOK"},
	{{A_Chase},                "A_CHASE"},
	{{A_FaceStabChase},        "A_FACESTABCHASE"},
	{{A_FaceTarget},           "A_FACETARGET"},
	{{A_FaceTracer},           "A_FACETRACER"},
	{{A_Scream},               "A_SCREAM"},
	{{A_BossDeath},            "A_BOSSDEATH"},
	{{A_CustomPower},          "A_CUSTOMPOWER"},
	{{A_GiveWeapon},           "A_GIVEWEAPON"},
	{{A_RingShield},           "A_RINGSHIELD"},
	{{A_RingBox},              "A_RINGBOX"},
	{{A_Invincibility},        "A_INVINCIBILITY"},
	{{A_SuperSneakers},        "A_SUPERSNEAKERS"},
	{{A_BunnyHop},             "A_BUNNYHOP"},
	{{A_BubbleSpawn},          "A_BUBBLESPAWN"},
	{{A_FanBubbleSpawn},       "A_FANBUBBLESPAWN"},
	{{A_BubbleRise},           "A_BUBBLERISE"},
	{{A_BubbleCheck},          "A_BUBBLECHECK"},
	{{A_AwardScore},           "A_AWARDSCORE"},
	{{A_ExtraLife},            "A_EXTRALIFE"},
	{{A_BombShield},           "A_BOMBSHIELD"},
	{{A_JumpShield},           "A_JUMPSHIELD"},
	{{A_WaterShield},          "A_WATERSHIELD"},
	{{A_ForceShield},          "A_FORCESHIELD"},
	{{A_PityShield},           "A_PITYSHIELD"},
	{{A_GravityBox},           "A_GRAVITYBOX"},
	{{A_ScoreRise},            "A_SCORERISE"},
	{{A_ParticleSpawn},        "A_PARTICLESPAWN"},
	{{A_AttractChase},         "A_ATTRACTCHASE"},
	{{A_DropMine},             "A_DROPMINE"},
	{{A_FishJump},             "A_FISHJUMP"},
	{{A_ThrownRing},           "A_THROWNRING"},
	{{A_GrenadeRing},          "A_GRENADERING"}, // SRB2kart
	{{A_SetSolidSteam},        "A_SETSOLIDSTEAM"},
	{{A_UnsetSolidSteam},      "A_UNSETSOLIDSTEAM"},
	{{A_SignPlayer},           "A_SIGNPLAYER"},
	{{A_OverlayThink},         "A_OVERLAYTHINK"},
	{{A_JetChase},             "A_JETCHASE"},
	{{A_JetbThink},            "A_JETBTHINK"},
	{{A_JetgThink},            "A_JETGTHINK"},
	{{A_JetgShoot},            "A_JETGSHOOT"},
	{{A_ShootBullet},          "A_SHOOTBULLET"},
	{{A_MinusDigging},         "A_MINUSDIGGING"},
	{{A_MinusPopup},           "A_MINUSPOPUP"},
	{{A_MinusCheck},           "A_MINUSCHECK"},
	{{A_ChickenCheck},         "A_CHICKENCHECK"},
	{{A_MouseThink},           "A_MOUSETHINK"},
	{{A_DetonChase},           "A_DETONCHASE"},
	{{A_CapeChase},            "A_CAPECHASE"},
	{{A_RotateSpikeBall},      "A_ROTATESPIKEBALL"},
	{{A_SlingAppear},          "A_SLINGAPPEAR"},
	{{A_MaceRotate},           "A_MACEROTATE"},
	{{A_UnidusBall},           "A_UNIDUSBALL"},
	{{A_RockSpawn},            "A_ROCKSPAWN"},
	{{A_SetFuse},              "A_SETFUSE"},
	{{A_CrawlaCommanderThink}, "A_CRAWLACOMMANDERTHINK"},
	{{A_SmokeTrailer},         "A_SMOKETRAILER"},
	{{A_RingExplode},          "A_RINGEXPLODE"},
	{{A_OldRingExplode},       "A_OLDRINGEXPLODE"},
	{{A_MixUp},                "A_MIXUP"},
	{{A_RecyclePowers},        "A_RECYCLEPOWERS"},
	{{A_Boss1Chase},           "A_BOSS1CHASE"},
	{{A_FocusTarget},          "A_FOCUSTARGET"},
	{{A_Boss2Chase},           "A_BOSS2CHASE"},
	{{A_Boss2Pogo},            "A_BOSS2POGO"},
	{{A_BossZoom},             "A_BOSSZOOM"},
	{{A_BossScream},           "A_BOSSSCREAM"},
	{{A_Boss2TakeDamage},      "A_BOSS2TAKEDAMAGE"},
	{{A_Boss7Chase},           "A_BOSS7CHASE"},
	{{A_GoopSplat},            "A_GOOPSPLAT"},
	{{A_Boss2PogoSFX},         "A_BOSS2POGOSFX"},
	{{A_Boss2PogoTarget},      "A_BOSS2POGOTARGET"},
	{{A_BossJetFume},          "A_BOSSJETFUME"},
	{{A_EggmanBox},            "A_EGGMANBOX"},
	{{A_TurretFire},           "A_TURRETFIRE"},
	{{A_SuperTurretFire},      "A_SUPERTURRETFIRE"},
	{{A_TurretStop},           "A_TURRETSTOP"},
	{{A_JetJawRoam},           "A_JETJAWROAM"},
	{{A_JetJawChomp},          "A_JETJAWCHOMP"},
	{{A_PointyThink},          "A_POINTYTHINK"},
	{{A_CheckBuddy},           "A_CHECKBUDDY"},
	{{A_HoodThink},            "A_HOODTHINK"},
	{{A_ArrowCheck},           "A_ARROWCHECK"},
	{{A_SnailerThink},         "A_SNAILERTHINK"},
	{{A_SharpChase},           "A_SHARPCHASE"},
	{{A_SharpSpin},            "A_SHARPSPIN"},
	{{A_VultureVtol},          "A_VULTUREVTOL"},
	{{A_VultureCheck},         "A_VULTURECHECK"},
	{{A_SkimChase},            "A_SKIMCHASE"},
	{{A_1upThinker},           "A_1UPTHINKER"},
	{{A_SkullAttack},          "A_SKULLATTACK"},
	{{A_LobShot},              "A_LOBSHOT"},
	{{A_FireShot},             "A_FIRESHOT"},
	{{A_SuperFireShot},        "A_SUPERFIRESHOT"},
	{{A_BossFireShot},         "A_BOSSFIRESHOT"},
	{{A_Boss7FireMissiles},    "A_BOSS7FIREMISSILES"},
	{{A_Boss1Laser},           "A_BOSS1LASER"},
	{{A_Boss4Reverse},         "A_BOSS4REVERSE"},
	{{A_Boss4SpeedUp},         "A_BOSS4SPEEDUP"},
	{{A_Boss4Raise},           "A_BOSS4RAISE"},
	{{A_SparkFollow},          "A_SPARKFOLLOW"},
	{{A_BuzzFly},              "A_BUZZFLY"},
	{{A_GuardChase},           "A_GUARDCHASE"},
	{{A_EggShield},            "A_EGGSHIELD"},
	{{A_SetReactionTime},      "A_SETREACTIONTIME"},
	{{A_Boss1Spikeballs},      "A_BOSS1SPIKEBALLS"},
	{{A_Boss3TakeDamage},      "A_BOSS3TAKEDAMAGE"},
	{{A_Boss3Path},            "A_BOSS3PATH"},
	{{A_LinedefExecute},       "A_LINEDEFEXECUTE"},
	{{A_PlaySeeSound},         "A_PLAYSEESOUND"},
	{{A_PlayAttackSound},      "A_PLAYATTACKSOUND"},
	{{A_PlayActiveSound},      "A_PLAYACTIVESOUND"},
	{{A_SpawnObjectAbsolute},  "A_SPAWNOBJECTABSOLUTE"},
	{{A_SpawnObjectRelative},  "A_SPAWNOBJECTRELATIVE"},
	{{A_ChangeAngleRelative},  "A_CHANGEANGLERELATIVE"},
	{{A_ChangeAngleAbsolute},  "A_CHANGEANGLEABSOLUTE"},
	{{A_RollAngle},            "A_ROLLANGLE"},
	{{A_ChangeRollAngleRelative},"A_CHANGEROLLANGLERELATIVE"},
	{{A_ChangeRollAngleAbsolute},"A_CHANGEROLLANGLEABSOLUTE"},
	{{A_PlaySound},            "A_PLAYSOUND"},
	{{A_FindTarget},           "A_FINDTARGET"},
	{{A_FindTracer},           "A_FINDTRACER"},
	{{A_SetTics},              "A_SETTICS"},
	{{A_SetRandomTics},        "A_SETRANDOMTICS"},
	{{A_ChangeColorRelative},  "A_CHANGECOLORRELATIVE"},
	{{A_ChangeColorAbsolute},  "A_CHANGECOLORABSOLUTE"},
	{{A_MoveRelative},         "A_MOVERELATIVE"},
	{{A_MoveAbsolute},         "A_MOVEABSOLUTE"},
	{{A_Thrust},               "A_THRUST"},
	{{A_ZThrust},              "A_ZTHRUST"},
	{{A_SetTargetsTarget},     "A_SETTARGETSTARGET"},
	{{A_SetObjectFlags},       "A_SETOBJECTFLAGS"},
	{{A_SetObjectFlags2},      "A_SETOBJECTFLAGS2"},
	{{A_RandomState},          "A_RANDOMSTATE"},
	{{A_RandomStateRange},     "A_RANDOMSTATERANGE"},
	{{A_DualAction},           "A_DUALACTION"},
	{{A_RemoteAction},         "A_REMOTEACTION"},
	{{A_ToggleFlameJet},       "A_TOGGLEFLAMEJET"},
	{{A_ItemPop},              "A_ITEMPOP"},       // SRB2kart
	{{A_JawzChase},            "A_JAWZCHASE"}, // SRB2kart
	{{A_JawzExplode},          "A_JAWZEXPLODE"}, // SRB2kart
	{{A_SPBChase},             "A_SPBCHASE"}, // SRB2kart
	{{A_MineExplode},          "A_MINEEXPLODE"}, // SRB2kart
	{{A_BallhogExplode},       "A_BALLHOGEXPLODE"}, // SRB2kart
	{{A_LightningFollowPlayer},"A_LIGHTNINGFOLLOWPLAYER"}, //SRB2kart
	{{A_FZBoomFlash},          "A_FZBOOMFLASH"}, //SRB2kart
	{{A_FZBoomSmoke},          "A_FZBOOMSMOKE"}, //SRB2kart
	{{A_RandomShadowFrame},	   "A_RANDOMSHADOWFRAME"}, //SRB2kart
	{{A_RoamingShadowThinker}, "A_ROAMINGSHADOWTHINKER"}, //SRB2kart
	{{A_MayonakaArrow}, 	   "A_MAYONAKAARROW"}, //SRB2kart
	{{A_ReaperThinker}, 	   "A_REAPERTHINKER"}, //SRB2kart
	{{A_MementosTPParticles},  "A_MEMENTOSTPPARTICLES"}, //SRB2kart
	{{A_FlameParticle},        "A_FLAMEPARTICLE"}, // SRB2kart
	{{A_OrbitNights},          "A_ORBITNIGHTS"},
	{{A_GhostMe},              "A_GHOSTME"},
	{{A_SetObjectState},       "A_SETOBJECTSTATE"},
	{{A_SetObjectTypeState},   "A_SETOBJECTTYPESTATE"},
	{{A_KnockBack},            "A_KNOCKBACK"},
	{{A_PushAway},             "A_PUSHAWAY"},
	{{A_RingDrain},            "A_RINGDRAIN"},
	{{A_SplitShot},            "A_SPLITSHOT"},
	{{A_MissileSplit},         "A_MISSILESPLIT"},
	{{A_MultiShot},            "A_MULTISHOT"},
	{{A_InstaLoop},            "A_INSTALOOP"},
	{{A_Custom3DRotate},       "A_CUSTOM3DROTATE"},
	{{A_SearchForPlayers},     "A_SEARCHFORPLAYERS"},
	{{A_CheckRandom},          "A_CHECKRANDOM"},
	{{A_CheckTargetRings},     "A_CHECKTARGETRINGS"},
	{{A_CheckRings},           "A_CHECKRINGS"},
	{{A_CheckTotalRings},      "A_CHECKTOTALRINGS"},
	{{A_CheckHealth},          "A_CHECKHEALTH"},
	{{A_CheckRange},           "A_CHECKRANGE"},
	{{A_CheckHeight},          "A_CHECKHEIGHT"},
	{{A_CheckTrueRange},       "A_CHECKTRUERANGE"},
	{{A_CheckThingCount},      "A_CHECKTHINGCOUNT"},
	{{A_CheckAmbush},          "A_CHECKAMBUSH"},
	{{A_CheckCustomValue},     "A_CHECKCUSTOMVALUE"},
	{{A_CheckCusValMemo},      "A_CHECKCUSVALMEMO"},
	{{A_SetCustomValue},       "A_SETCUSTOMVALUE"},
	{{A_UseCusValMemo},        "A_USECUSVALMEMO"},
	{{A_RelayCustomValue},     "A_RELAYCUSTOMVALUE"},
	{{A_CusValAction},         "A_CUSVALACTION"},
	{{A_ForceStop},            "A_FORCESTOP"},
	{{A_ForceWin},             "A_FORCEWIN"},
	{{A_SpikeRetract},         "A_SPIKERETRACT"},
	{{A_InfoState},            "A_INFOSTATE"},
	{{A_Repeat},               "A_REPEAT"},
	{{A_SetScale},             "A_SETSCALE"},
	{{A_RemoteDamage},         "A_REMOTEDAMAGE"},
	{{A_HomingChase},          "A_HOMINGCHASE"},
	{{A_TrapShot},             "A_TRAPSHOT"},
	{{A_VileTarget},           "A_VILETARGET"},
	{{A_VileAttack},           "A_VILEATTACK"},
	{{A_VileFire},             "A_VILEFIRE"},
	{{A_BrakChase},            "A_BRAKCHASE"},
	{{A_BrakFireShot},         "A_BRAKFIRESHOT"},
	{{A_BrakLobShot},          "A_BRAKLOBSHOT"},
	{{A_NapalmScatter},        "A_NAPALMSCATTER"},
	{{A_SpawnFreshCopy},       "A_SPAWNFRESHCOPY"},

	{{NULL},                   "NONE"},

	// This NULL entry must be the last in the list
	{{NULL},                   NULL},
};

static void readframe(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word1;
	char *word2 = NULL;
	char *tmp;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			word1 = strtok(s, " ");
			if (word1)
				strupr(word1);
			else
				break;

			word2 = strtok(NULL, " = ");
			if (word2)
				strupr(word2);
			else
				break;
			if (word2[strlen(word2)-1] == '\n')
				word2[strlen(word2)-1] = '\0';

			if (fastcmp(word1, "SPRITENUMBER") || fastcmp(word1, "SPRITENAME"))
			{
				states[num].sprite = get_sprite(word2);
			}
			else if (fastcmp(word1, "SPRITESUBNUMBER") || fastcmp(word1, "SPRITEFRAME"))
			{
				states[num].frame = (INT32)get_number(word2); // So the FF_ flags get calculated
			}
			else if (fastcmp(word1, "DURATION"))
			{
				states[num].tics = (INT32)get_number(word2); // So TICRATE can be used
			}
			else if (fastcmp(word1, "NEXT"))
			{
				states[num].nextstate = get_state(word2);
			}
			else if (fastcmp(word1, "VAR1"))
			{
				states[num].var1 = (INT32)get_number(word2);
			}
			else if (fastcmp(word1, "VAR2"))
			{
				states[num].var2 = (INT32)get_number(word2);
			}
			else if (fastcmp(word1, "ACTION"))
			{
				size_t z;
				boolean found = false;
				char actiontocompare[32];

				memset(actiontocompare, 0x00, sizeof(actiontocompare));
				strlcpy(actiontocompare, word2, sizeof (actiontocompare));
				strupr(actiontocompare);

				for (z = 0; z < 32; z++)
				{
					if (actiontocompare[z] == '\n' || actiontocompare[z] == '\r')
					{
						actiontocompare[z] = '\0';
						break;
					}
				}

				for (z = 0; actionpointers[z].name; z++)
				{
					if (actionpointers[z].action.acv == states[num].action.acv)
					{
						break;
					}
				}

				z = 0;
				found = LUA_SetLuaAction(&states[num], actiontocompare);
				if (!found)
				while (actionpointers[z].name)
				{
					if (fastcmp(actiontocompare, actionpointers[z].name))
					{
						states[num].action = actionpointers[z].action;
						states[num].action.acv = actionpointers[z].action.acv; // assign
						states[num].action.acp1 = actionpointers[z].action.acp1;
						found = true;
						break;
					}
					z++;
				}

				if (!found)
					deh_warning("Unknown action %s", actiontocompare);
			}
			else
				deh_warning("Frame %d: unknown word '%s'", num, word1);
		}
	} while (!myfeof(f));

	Z_Free(s);
}

static void readsound(MYFILE *f, INT32 num, const char *savesfxnames[])
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *tmp;
	INT32 value;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';

			value = searchvalue(s);

			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			if (fastcmp(word, "SINGULAR"))
			{
				S_sfx[num].singularity = value;
			}
			else if (fastcmp(word, "PRIORITY"))
			{
				S_sfx[num].priority = value;
			}
			else if (fastcmp(word, "FLAGS"))
			{
				S_sfx[num].pitch = value;
			}
			else
				deh_warning("Sound %d : unknown word '%s'",num,word);
		}
	} while (!myfeof(f));

	Z_Free(s);

	(void)savesfxnames;
}

/** Checks if a game data file name for a mod is good.
 * "Good" means that it contains only alphanumerics, _, and -;
 * ends in ".dat"; has at least one character before the ".dat";
 * and is not "gamedata.dat" (tested case-insensitively).
 *
 * Assumption: that gamedata.dat is the only .dat file that will
 * ever be treated specially by the game.
 *
 * Note: Check for the tail ".dat" case-insensitively since at
 * present, we get passed the filename in all uppercase.
 *
 * \param s Filename string to check.
 * \return True if the filename is good.
 * \sa readmaincfg()
 * \author Graue <graue@oceanbase.org>
 */
static boolean GoodDataFileName(const char *s)
{
	const char *p;
	const char *tail = ".dat";

	for (p = s; *p != '\0'; p++)
		if (!isalnum(*p) && *p != '_' && *p != '-' && *p != '.')
			return false;

	p = s + strlen(s) - strlen(tail);
	if (p <= s) return false; // too short
	if (!fasticmp(p, tail)) return false; // doesn't end in .dat

	if (fasticmp(s, "gamedata.dat")) return false; // Vanilla SRB2 gamedata
	if (fasticmp(s, "main.dat")) return false; // Vanilla SRB2 time attack replay folder
	if (fasticmp(s, "kartdata.dat")) return false; // SRB2Kart gamedata
	if (fasticmp(s, "kart.dat")) return false; // SRB2Kart time attack replay folder
	if (fasticmp(s, "online.dat")) return false; // SRB2Kart online replay folder

	return true;
}

static void reademblemdata(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 value;

	// Reset all data initially
	memset(&emblemlocations[num-1], 0, sizeof(emblem_t));

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp += 2;
			value = atoi(word2); // used for numerical settings

			// Up here to allow lowercase in hints
			if (fastcmp(word, "HINT"))
			{
				while ((tmp = strchr(word2, '\\')))
					*tmp = '\n';
				deh_strlcpy(emblemlocations[num-1].hint, word2, sizeof (emblemlocations[num-1].hint), va("Emblem %d: hint", num));
				continue;
			}
			strupr(word2);

			if (fastcmp(word, "TYPE"))
			{
				if (fastcmp(word2, "GLOBAL"))
					emblemlocations[num-1].type = ET_GLOBAL;
				else if (fastcmp(word2, "SKIN"))
					emblemlocations[num-1].type = ET_SKIN;
				/*else if (fastcmp(word2, "SCORE"))
					emblemlocations[num-1].type = ET_SCORE;*/
				else if (fastcmp(word2, "TIME"))
					emblemlocations[num-1].type = ET_TIME;
				/*else if (fastcmp(word2, "RINGS"))
					emblemlocations[num-1].type = ET_RINGS;
				else if (fastcmp(word2, "NGRADE"))
					emblemlocations[num-1].type = ET_NGRADE;
				else if (fastcmp(word2, "NTIME"))
					emblemlocations[num-1].type = ET_NTIME;*/
				else
					emblemlocations[num-1].type = (UINT8)value;
			}
			else if (fastcmp(word, "X"))
				emblemlocations[num-1].x = (INT16)value;
			else if (fastcmp(word, "Y"))
				emblemlocations[num-1].y = (INT16)value;
			else if (fastcmp(word, "Z"))
				emblemlocations[num-1].z = (INT16)value;
			else if (fastcmp(word, "MAPNUM"))
			{
				// Support using the actual map name,
				// i.e., Level AB, Level FZ, etc.

				// Convert to map number
				if (word2[0] >= 'A' && word2[0] <= 'Z')
					value = M_MapNumber(word2[0], word2[1]);

				emblemlocations[num-1].level = (INT16)value;
			}
			else if (fastcmp(word, "SPRITE"))
			{
				if (word2[0] >= 'A' && word2[0] <= 'Z')
					value = word2[0];
				else
					value += 'A'-1;

				if (value < 'A' || value > 'Z')
					deh_warning("Emblem %d: sprite must be from A - Z (1 - 26)", num);
				else
					emblemlocations[num-1].sprite = (UINT8)value;
			}
			else if (fastcmp(word, "COLOR"))
				emblemlocations[num-1].color = get_number(word2);
			else if (fastcmp(word, "VAR"))
				emblemlocations[num-1].var = get_number(word2);
			else
				deh_warning("Emblem %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f));

	// Default sprite and color definitions for lazy people like me
	if (!emblemlocations[num-1].sprite) switch (emblemlocations[num-1].type)
	{
		case ET_TIME: //case ET_NTIME:
			emblemlocations[num-1].sprite = 'B'; break;
		default:
			emblemlocations[num-1].sprite = 'A'; break;
	}
	if (!emblemlocations[num-1].color) switch (emblemlocations[num-1].type)
	{
		case ET_TIME: //case ET_NTIME:
			emblemlocations[num-1].color = SKINCOLOR_GREY; break;
		default:
			emblemlocations[num-1].color = SKINCOLOR_GOLD; break;
	}

	Z_Free(s);
}

static void readextraemblemdata(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 value;

	// Reset all data initially
	memset(&extraemblems[num-1], 0, sizeof(extraemblem_t));

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp += 2;
			strupr(word2);

			value = atoi(word2); // used for numerical settings

			if (fastcmp(word, "NAME"))
				deh_strlcpy(extraemblems[num-1].name, word2,
					sizeof (extraemblems[num-1].name), va("Extra emblem %d: name", num));
			else if (fastcmp(word, "OBJECTIVE"))
				deh_strlcpy(extraemblems[num-1].description, word2,
					sizeof (extraemblems[num-1].description), va("Extra emblem %d: objective", num));
			else if (fastcmp(word, "CONDITIONSET"))
				extraemblems[num-1].conditionset = (UINT8)value;
			else if (fastcmp(word, "SPRITE"))
			{
				if (word2[0] >= 'A' && word2[0] <= 'Z')
					value = word2[0];
				else
					value += 'A'-1;

				if (value < 'A' || value > 'Z')
					deh_warning("Emblem %d: sprite must be from A - Z (1 - 26)", num);
				else
					extraemblems[num-1].sprite = (UINT8)value;
			}
			else if (fastcmp(word, "COLOR"))
				extraemblems[num-1].color = get_number(word2);
			else
				deh_warning("Extra emblem %d: unknown word '%s'", num, word);
		}
	} while (!myfeof(f));

	if (!extraemblems[num-1].sprite)
		extraemblems[num-1].sprite = 'C';
	if (!extraemblems[num-1].color)
		extraemblems[num-1].color = SKINCOLOR_RED;

	Z_Free(s);
}

static void readunlockable(MYFILE *f, INT32 num)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 i;

	// Same deal with unlockables, clear all first
	memset(&unlockables[num], 0, sizeof(unlockable_t));

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp += 2;
			strupr(word2);

			i = atoi(word2); // used for numerical settings

			if (fastcmp(word, "NAME"))
				deh_strlcpy(unlockables[num].name, word2,
					sizeof (unlockables[num].name), va("Unlockable %d: name", num));
			else if (fastcmp(word, "OBJECTIVE"))
				deh_strlcpy(unlockables[num].objective, word2,
					sizeof (unlockables[num].objective), va("Unlockable %d: objective", num));
			else if (fastcmp(word, "SHOWCONDITIONSET"))
				unlockables[num].showconditionset = (UINT8)i;
			else if (fastcmp(word, "CONDITIONSET"))
				unlockables[num].conditionset = (UINT8)i;
			else if (fastcmp(word, "NOCECHO"))
				unlockables[num].nocecho = (UINT8)(i || word2[0] == 'T' || word2[0] == 'Y');
			else if (fastcmp(word, "NOCHECKLIST"))
				unlockables[num].nochecklist = (UINT8)(i || word2[0] == 'T' || word2[0] == 'Y');
			else if (fastcmp(word, "TYPE"))
			{
				if (fastcmp(word2, "NONE"))
					unlockables[num].type = SECRET_NONE;
				else if (fastcmp(word2, "ITEMFINDER"))
					unlockables[num].type = SECRET_ITEMFINDER;
				else if (fastcmp(word2, "EMBLEMHINTS"))
					unlockables[num].type = SECRET_EMBLEMHINTS;
				else if (fastcmp(word2, "PANDORA"))
					unlockables[num].type = SECRET_PANDORA;
				else if (fastcmp(word2, "CREDITS"))
					unlockables[num].type = SECRET_CREDITS;
				else if (fastcmp(word2, "RECORDATTACK"))
					unlockables[num].type = SECRET_RECORDATTACK;
				else if (fastcmp(word2, "NIGHTSMODE"))
					unlockables[num].type = SECRET_NIGHTSMODE;
				else if (fastcmp(word2, "HEADER"))
					unlockables[num].type = SECRET_HEADER;
				else if (fastcmp(word2, "LEVELSELECT"))
					unlockables[num].type = SECRET_LEVELSELECT;
				else if (fastcmp(word2, "WARP"))
					unlockables[num].type = SECRET_WARP;
				else if (fastcmp(word2, "SOUNDTEST"))
					unlockables[num].type = SECRET_SOUNDTEST;
				else if (fastcmp(word2, "ENCORE"))
					unlockables[num].type = SECRET_ENCORE;
				else if (fastcmp(word2, "HELLATTACK"))
					unlockables[num].type = SECRET_HELLATTACK;
				else if (fastcmp(word2, "HARDSPEED"))
					unlockables[num].type = SECRET_HARDSPEED;
				else
					unlockables[num].type = (INT16)i;
			}
			else if (fastcmp(word, "VAR"))
			{
				// Support using the actual map name,
				// i.e., Level AB, Level FZ, etc.

				// Convert to map number
				if (word2[0] >= 'A' && word2[0] <= 'Z')
					i = M_MapNumber(word2[0], word2[1]);

				unlockables[num].variable = (INT16)i;
			}
			else
				deh_warning("Unlockable %d: unknown word '%s'", num+1, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

#define PARAMCHECK(n) do { if (!params[n]) { deh_warning("Too few parameters, need %d", n); return; }} while (0)
static void readcondition(UINT8 set, UINT32 id, char *word2)
{
	INT32 i;
	char *params[4]; // condition, requirement, extra info, extra info
	char *spos;

	conditiontype_t ty;
	INT32 re;
	INT16 x1 = 0, x2 = 0;

	INT32 offset = 0;

	spos = strtok(word2, " ");

	for (i = 0; i < 4; ++i)
	{
		if (spos != NULL)
		{
			params[i] = spos;
			spos = strtok(NULL, " ");
		}
		else
			params[i] = NULL;
	}

	if (!params[0])
	{
		deh_warning("condition line is empty");
		return;
	}

	if				(fastcmp(params[0], "PLAYTIME")
	|| (++offset && fastcmp(params[0], "MATCHESPLAYED")))
	{
		PARAMCHECK(1);
		ty = UC_PLAYTIME + offset;
		re = atoi(params[1]);
	}
	else if ((offset=0) || fastcmp(params[0], "GAMECLEAR")
	||        (++offset && fastcmp(params[0], "ALLEMERALDS")))
	//||        (++offset && fastcmp(params[0], "ULTIMATECLEAR")))
	{
		ty = UC_GAMECLEAR + offset;
		re = (params[1]) ? atoi(params[1]) : 1;
	}
	else if ((offset=0) || fastcmp(params[0], "OVERALLTIME"))
	//||        (++offset && fastcmp(params[0], "OVERALLSCORE"))
	//||        (++offset && fastcmp(params[0], "OVERALLRINGS")))
	{
		PARAMCHECK(1);
		ty = UC_OVERALLTIME + offset;
		re = atoi(params[1]);
	}
	else if ((offset=0) || fastcmp(params[0], "MAPVISITED")
	||        (++offset && fastcmp(params[0], "MAPBEATEN"))
	||        (++offset && fastcmp(params[0], "MAPALLEMERALDS")))
	//||        (++offset && fastcmp(params[0], "MAPULTIMATE"))
	//||        (++offset && fastcmp(params[0], "MAPPERFECT")))
	{
		PARAMCHECK(1);
		ty = UC_MAPVISITED + offset;

		// Convert to map number if it appears to be one
		if (params[1][0] >= 'A' && params[1][0] <= 'Z')
			re = M_MapNumber(params[1][0], params[1][1]);
		else
			re = atoi(params[1]);

		if (re < 0 || re >= NUMMAPS)
		{
			deh_warning("Level number %d out of range (1 - %d)", re, NUMMAPS);
			return;
		}
	}
	else if ((offset=0) || fastcmp(params[0], "MAPTIME"))
	//||        (++offset && fastcmp(params[0], "MAPSCORE"))
	//||        (++offset && fastcmp(params[0], "MAPRINGS")))
	{
		PARAMCHECK(2);
		ty = UC_MAPTIME + offset;
		re = atoi(params[2]);

		// Convert to map number if it appears to be one
		if (params[1][0] >= 'A' && params[1][0] <= 'Z')
			x1 = (INT16)M_MapNumber(params[1][0], params[1][1]);
		else
			x1 = (INT16)atoi(params[1]);

		if (x1 < 0 || x1 >= NUMMAPS)
		{
			deh_warning("Level number %d out of range (1 - %d)", re, NUMMAPS);
			return;
		}
	}
	else if (fastcmp(params[0], "TRIGGER"))
	{
		PARAMCHECK(1);
		ty = UC_TRIGGER;
		re = atoi(params[1]);

		// constrained by 32 bits
		if (re < 0 || re > 31)
		{
			deh_warning("Trigger ID %d out of range (0 - 31)", re);
			return;
		}
	}
	else if (fastcmp(params[0], "TOTALEMBLEMS"))
	{
		PARAMCHECK(1);
		ty = UC_TOTALEMBLEMS;
		re = atoi(params[1]);
	}
	else if (fastcmp(params[0], "EMBLEM"))
	{
		PARAMCHECK(1);
		ty = UC_EMBLEM;
		re = atoi(params[1]);

		if (re <= 0 || re > MAXEMBLEMS)
		{
			deh_warning("Emblem %d out of range (1 - %d)", re, MAXEMBLEMS);
			return;
		}
	}
	else if (fastcmp(params[0], "EXTRAEMBLEM"))
	{
		PARAMCHECK(1);
		ty = UC_EXTRAEMBLEM;
		re = atoi(params[1]);

		if (re <= 0 || re > MAXEXTRAEMBLEMS)
		{
			deh_warning("Extra emblem %d out of range (1 - %d)", re, MAXEXTRAEMBLEMS);
			return;
		}
	}
	else if (fastcmp(params[0], "CONDITIONSET"))
	{
		PARAMCHECK(1);
		ty = UC_CONDITIONSET;
		re = atoi(params[1]);

		if (re <= 0 || re > MAXCONDITIONSETS)
		{
			deh_warning("Condition set %d out of range (1 - %d)", re, MAXCONDITIONSETS);
			return;
		}
	}
	else
	{
		deh_warning("Invalid condition name %s", params[0]);
		return;
	}

	M_AddRawCondition(set, (UINT8)id, ty, re, x1, x2);
}
#undef PARAMCHECK

static void readconditionset(MYFILE *f, UINT8 setnum)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	UINT8 id;
	UINT8 previd = 0;

	M_ClearConditionSet(setnum);

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp += 2;
			strupr(word2);

			if (fastncmp(word, "CONDITION", 9))
			{
				id = (UINT8)atoi(word + 9);
				if (id == 0)
				{
					deh_warning("Condition set %d: unknown word '%s'", setnum, word);
					continue;
				}
				else if (previd > id)
				{
					// out of order conditions can cause problems, so enforce proper order
					deh_warning("Condition set %d: conditions are out of order, ignoring this line", setnum);
					continue;
				}
				previd = id;

				readcondition(setnum, id, word2);
			}
			else
				deh_warning("Condition set %d: unknown word '%s'", setnum, word);
		}
	} while (!myfeof(f)); // finish when the line is empty

	Z_Free(s);
}

static void readpatch(MYFILE *f, const char *name, UINT16 wad)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	char *tmp;
	INT32 i = 0, j = 0, value;
	texpatch_t patch = {0, 0, UINT16_MAX, UINT16_MAX};

	// Jump to the texture this patch belongs to, which,
	// coincidentally, is always the last one on the buffer cache.
	while (textures[i+1])
		i++;

	// Jump to the next empty patch entry.
	while (memcmp(&(textures[i]->patches[j]), &patch, sizeof(patch)))
		j++;

	patch.wad = wad;
	// Set the texture number, but only if the lump exists.
	if ((patch.lump = W_CheckNumForNamePwad(name, wad, 0)) == INT16_MAX)
		I_Error("readpatch: Missing patch in texture %s", textures[i]->name);

	// note: undoing this patch will be done by other means
	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';

			value = searchvalue(s);
			word = strtok(s, " ");
			if (word)
				strupr(word);
			else
				break;

			word2 = strtok(NULL, " ");
			if (word2)
				strupr(word2);
			else
				break;

			// X position of the patch in the texture.
			if (fastcmp(word, "X"))
			{
				patch.originx = (INT16)value;
			}
			// Y position of the patch in the texture.
			else if (fastcmp(word, "Y"))
			{
				patch.originy = (INT16)value;
			}
			else
				deh_warning("readpatch: unknown word '%s'", word);
		}
	} while (!myfeof(f));

	// Set the patch as that patch number.
	textures[i]->patches[j] = patch;

	// Clean up.
	Z_Free(s);
}

static void readmaincfg(MYFILE *f)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *word2;
	char *tmp;
	INT32 value;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp += 2;
			strupr(word2);

			value = atoi(word2); // used for numerical settings

			if (fastcmp(word, "EXECCFG"))
				COM_BufAddText(va("exec %s\n", word2));

			else if (fastcmp(word, "SPSTAGE_START"))
			{
				// Support using the actual map name,
				// i.e., Level AB, Level FZ, etc.

				// Convert to map number
				if (word2[0] >= 'A' && word2[0] <= 'Z')
					value = M_MapNumber(word2[0], word2[1]);
				else
					value = get_number(word2);

				spstage_start = (INT16)value;
			}
			else if (fastcmp(word, "SSTAGE_START"))
			{
				// Support using the actual map name,
				// i.e., Level AB, Level FZ, etc.

				// Convert to map number
				if (word2[0] >= 'A' && word2[0] <= 'Z')
					value = M_MapNumber(word2[0], word2[1]);
				else
					value = get_number(word2);

				sstage_start = (INT16)value;
				sstage_end = (INT16)(sstage_start+6); // 7 special stages total
			}
			else if (fastcmp(word, "USENIGHTSSS"))
			{
				useNightsSS = (value || word2[0] == 'T' || word2[0] == 'Y');
			}
			else if (fastcmp(word, "REDTEAM"))
			{
				skincolor_redteam = (UINT8)get_number(word2);
			}
			else if (fastcmp(word, "BLUETEAM"))
			{
				skincolor_blueteam = (UINT8)get_number(word2);
			}
			else if (fastcmp(word, "REDRING"))
			{
				skincolor_redring = (UINT8)get_number(word2);
			}
			else if (fastcmp(word, "BLUERING"))
			{
				skincolor_bluering = (UINT8)get_number(word2);
			}
			else if (fastcmp(word, "INVULNTICS"))
			{
				invulntics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "SNEAKERTICS"))
			{
				sneakertics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "FLASHINGTICS"))
			{
				flashingtics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "TAILSFLYTICS"))
			{
				tailsflytics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "UNDERWATERTICS"))
			{
				underwatertics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "SPACETIMETICS"))
			{
				spacetimetics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "EXTRALIFETICS"))
			{
				extralifetics = (UINT16)get_number(word2);
			}
			else if (fastcmp(word, "GAMEOVERTICS"))
			{
				gameovertics = get_number(word2);
			}

			else if (fastcmp(word, "INTROTOPLAY"))
			{
				introtoplay = (UINT8)get_number(word2);
				// range check, you morons.
				if (introtoplay > 128)
					introtoplay = 128;
			}
			else if (fastcmp(word, "LOOPTITLE"))
			{
				looptitle = (value || word2[0] == 'T' || word2[0] == 'Y');
			}
			else if (fastcmp(word, "TITLESCROLLSPEED"))
			{
				titlescrollspeed = get_number(word2);
			}
			else if (fastcmp(word, "CREDITSCUTSCENE"))
			{
				creditscutscene = (UINT8)get_number(word2);
				// range check, you morons.
				if (creditscutscene > 128)
					creditscutscene = 128;
			}
			else if (fastcmp(word, "NUMDEMOS"))
			{
				numDemos = (UINT8)get_number(word2);
			}
			else if (fastcmp(word, "DEMODELAYTIME"))
			{
				demoDelayTime = get_number(word2);
			}
			else if (fastcmp(word, "DEMOIDLETIME"))
			{
				demoIdleTime = get_number(word2);
			}
			else if (fastcmp(word, "USE1UPSOUND"))
			{
				use1upSound = (UINT8)(value || word2[0] == 'T' || word2[0] == 'Y');
			}
			else if (fastcmp(word, "MAXXTRALIFE"))
			{
				maxXtraLife = (UINT8)get_number(word2);
			}

			else if (fastcmp(word, "GAMEDATA"))
			{
				size_t filenamelen;

				// Check the data filename so that mods
				// can't write arbitrary files.
				if (!GoodDataFileName(word2))
					I_Error("Maincfg: bad data file name '%s'\n", word2);

				G_SaveGameData(false);
				strlcpy(gamedatafilename, word2, sizeof (gamedatafilename));
				strlwr(gamedatafilename);
				savemoddata = true;
				majormods = false;

				// Also save a time attack folder
				filenamelen = strlen(gamedatafilename)-4;  // Strip off the extension
				filenamelen = min(filenamelen, sizeof (timeattackfolder));
				memcpy(timeattackfolder, gamedatafilename, filenamelen);
				timeattackfolder[min(filenamelen, sizeof (timeattackfolder) - 1)] = '\0';

				strcpy(savegamename, timeattackfolder);
				strlcat(savegamename, "%u.ssg", sizeof(savegamename));
				// can't use sprintf since there is %u in savegamename
				strcatbf(savegamename, srb2home, PATHSEP);

				refreshdirmenu |= REFRESHDIR_GAMEDATA;
			}
			else if (fastcmp(word, "RESETDATA"))
			{
				P_ResetData(value);
			}
			else if (fastcmp(word, "CUSTOMVERSION"))
			{
				strlcpy(customversionstring, word2, sizeof (customversionstring));
			}
			else
				deh_warning("Maincfg: unknown word '%s'", word);
		}
	} while (!myfeof(f));

	Z_Free(s);
}

static void readwipes(MYFILE *f)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word = s;
	char *pword = word;
	char *word2;
	char *tmp;
	INT32 value;
	INT32 wipeoffset;

	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;

			// First remove trailing newline, if there is one
			tmp = strchr(s, '\n');
			if (tmp)
				*tmp = '\0';

			tmp = strchr(s, '#');
			if (tmp)
				*tmp = '\0';
			if (s == tmp)
				continue; // Skip comment lines, but don't break.

			// Get the part before the " = "
			tmp = strchr(s, '=');
			if (tmp)
				*(tmp-1) = '\0';
			else
				break;
			strupr(word);

			// Now get the part after
			word2 = tmp += 2;
			value = atoi(word2); // used for numerical settings

			if (value < -1 || value > 99)
			{
				deh_warning("Wipes: bad value '%s'", word2);
				continue;
			}
			else if (value == -1)
				value = UINT8_MAX;

			// error catching
			wipeoffset = -1;

			if (fastncmp(word, "LEVEL_", 6))
			{
				pword = word + 6;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_level_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_level_final;
			}
			else if (fastncmp(word, "INTERMISSION_", 13))
			{
				pword = word + 13;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_intermission_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_intermission_final;
			}
			else if (fastncmp(word, "SPECINTER_", 10))
			{
				pword = word + 10;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_specinter_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_specinter_final;
			}
			else if (fastncmp(word, "VOTING_", 7))
			{
				pword = word + 7;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_specinter_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_specinter_final;
			}
			else if (fastncmp(word, "MULTINTER_", 10))
			{
				pword = word + 10;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_multinter_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_multinter_final;
			}
			else if (fastncmp(word, "CONTINUING_", 11))
			{
				pword = word + 11;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_continuing_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_continuing_final;
			}
			else if (fastncmp(word, "TITLESCREEN_", 12))
			{
				pword = word + 12;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_titlescreen_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_titlescreen_final;
			}
			else if (fastncmp(word, "TIMEATTACK_", 11))
			{
				pword = word + 11;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_timeattack_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_timeattack_final;
			}
			else if (fastncmp(word, "CREDITS_", 8))
			{
				pword = word + 8;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_credits_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_credits_final;
				else if (fastcmp(pword, "INTERMEDIATE"))
					wipeoffset = wipe_credits_intermediate;
			}
			else if (fastncmp(word, "EVALUATION_", 11))
			{
				pword = word + 11;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_evaluation_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_evaluation_final;
			}
			else if (fastncmp(word, "GAMEEND_", 8))
			{
				pword = word + 8;
				if (fastcmp(pword, "TOBLACK"))
					wipeoffset = wipe_gameend_toblack;
				else if (fastcmp(pword, "FINAL"))
					wipeoffset = wipe_gameend_final;
			}
			else if (fastncmp(word, "SPECLEVEL_", 10))
			{
				pword = word + 10;
				if (fastcmp(pword, "TOWHITE"))
					wipeoffset = wipe_speclevel_towhite;
			}

			if (wipeoffset < 0)
			{
				deh_warning("Wipes: unknown word '%s'", word);
				continue;
			}

			if (value == UINT8_MAX
			 && (wipeoffset <= wipe_level_toblack || wipeoffset >= wipe_speclevel_towhite))
			{
				 // Cannot disable non-toblack wipes
				 // (or the level toblack wipe, or the special towhite wipe)
				deh_warning("Wipes: can't disable wipe of type '%s'", word);
				continue;
			}

			wipedefs[wipeoffset] = (UINT8)value;
		}
	} while (!myfeof(f));

	Z_Free(s);
}

// Used when you do something invalid like read a bad item number
// to prevent extra unnecessary errors
static void ignorelines(MYFILE *f)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	do
	{
		if (myfgets(s, MAXLINELEN, f))
		{
			if (s[0] == '\n')
				break;
		}
	} while (!myfeof(f));
	Z_Free(s);
}

static void DEH_LoadDehackedFile(MYFILE *f, UINT16 wad)
{
	char *s = Z_Malloc(MAXLINELEN, PU_STATIC, NULL);
	char *word;
	char *word2;
	INT32 i;
	// do a copy of this for cross references probleme
	const char *savesfxnames[NUMSFX];

	if (!deh_loaded)
		initfreeslots();

	deh_num_warning = 0;
	// save values for cross reference
	/*
	for (i = 0; i < NUMSTATES; i++)
		saveactions[i] = states[i].action;
	for (i = 0; i < NUMSPRITES; i++)
		savesprnames[i] = sprnames[i];
	*/
	for (i = 0; i < NUMSFX; i++)
		savesfxnames[i] = S_sfx[i].name;

	// it doesn't test the version of SRB2 and version of dehacked file
	dbg_line = -1; // start at -1 so the first line is 0.
	while (!myfeof(f))
	{
		char origpos[128];
		INT32 size = 0;
		char *traverse;

		myfgets(s, MAXLINELEN, f);
		if (s[0] == '\n' || s[0] == '#')
			continue;

		traverse = s;

		while (traverse[0] != '\n')
		{
			traverse++;
			size++;
		}

		strncpy(origpos, s, size);
		origpos[size] = '\0';

		if (NULL != (word = strtok(s, " "))) {
			strupr(word);
			if (word[strlen(word)-1] == '\n')
				word[strlen(word)-1] = '\0';
		}
		if (word)
		{
			if (fastcmp(word, "FREESLOT"))
			{
				readfreeslots(f);
				// This is not a major mod.
				continue;
			}
			else if (fastcmp(word, "MAINCFG"))
			{
				G_SetGameModified(multiplayer, true);
				readmaincfg(f);
				continue;
			}
			else if (fastcmp(word, "WIPES"))
			{
				readwipes(f);
				// This is not a major mod.
				continue;
			}
			else if (fastcmp(word, "FOLLOWER"))
			{
				readfollower(f);	// at the same time this will be our only way to ADD followers for now. Yikes.
				// This is not a major mod either.
				continue;	// continue so that we don't error.
			}
			word2 = strtok(NULL, " ");
			if (fastcmp(word, "CHARACTER"))
			{
				if (word2) {
					strupr(word2);
					if (word2[strlen(word2)-1] == '\n')
						word2[strlen(word2)-1] = '\0';
					i = atoi(word2);
				} else
					i = 0;
				if (i >= 0 && i < 32)
					readPlayer(f, i);
				else
				{
					deh_warning("Character %d out of range (0 - 31)", i);
					ignorelines(f);
				}
				// This is not a major mod.
				continue;
			}
			if (word2)
			{
				strupr(word2);
				if (word2[strlen(word2)-1] == '\n')
					word2[strlen(word2)-1] = '\0';
				i = atoi(word2);
				if (fastcmp(word, "PATCH"))
				{
					// Read patch from spec file.
					readpatch(f, word2, wad);
					// This is not a major mod.
				}
				else if (fastcmp(word, "THING") || fastcmp(word, "MOBJ") || fastcmp(word, "OBJECT"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_mobjtype(word2); // find a thing by name
					if (i < NUMMOBJTYPES && i >= 0)
					{
						if (i < (MT_FIRSTFREESLOT+freeslotusage[1][1]))
							G_SetGameModified(multiplayer, true); // affecting something earlier than the first freeslot allocated in this .wad? DENIED
						readthing(f, i);
					}
					else
					{
						deh_warning("Thing %d out of range (0 - %d)", i, NUMMOBJTYPES-1);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "LEVEL"))
				{
					// Support using the actual map name,
					// i.e., Level AB, Level FZ, etc.

					// Convert to map number
					if (word2[0] >= 'A' && word2[0] <= 'Z')
						i = M_MapNumber(word2[0], word2[1]);

					if (i > 0 && i <= NUMMAPS)
					{
						if (mapheaderinfo[i-1])
							G_SetGameModified(multiplayer, true); // only mark as a major mod if it replaces an already-existing mapheaderinfo
						readlevelheader(f, i, wad);
					}
					else
					{
						deh_warning("Level number %d out of range (1 - %d)", i, NUMMAPS);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "CUTSCENE"))
				{
					if (i > 0 && i < 129)
						readcutscene(f, i - 1);
					else
					{
						deh_warning("Cutscene number %d out of range (1 - 128)", i);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "FRAME") || fastcmp(word, "STATE"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_state(word2); // find a state by name
					if (i < NUMSTATES && i >= 0)
					{
						if (i < (S_FIRSTFREESLOT+freeslotusage[0][1]))
							G_SetGameModified(multiplayer, true); // affecting something earlier than the first freeslot allocated in this .wad? DENIED
						readframe(f, i);
					}
					else
					{
						deh_warning("Frame %d out of range (0 - %d)", i, NUMSTATES-1);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "SOUND"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_sfx(word2); // find a sound by name
					if (i < NUMSFX && i >= 0)
						readsound(f, i, savesfxnames);
					else
					{
						deh_warning("Sound %d out of range (0 - %d)", i, NUMSFX-1);
						ignorelines(f);
					}
					// This is not a major mod.
				}
				else if (fastcmp(word, "HUDITEM"))
				{
					if (i == 0 && word2[0] != '0') // If word2 isn't a number
						i = get_huditem(word2); // find a huditem by name
					if (i >= 0 && i < NUMHUDITEMS)
						readhuditem(f, i);
					else
					{
						deh_warning("HUD item number %d out of range (0 - %d)", i, NUMHUDITEMS-1);
						ignorelines(f);
					}
					// This is not a major mod.
				}
				else if (fastcmp(word, "EMBLEM"))
				{
					if (!(refreshdirmenu & REFRESHDIR_GAMEDATA))
					{
						deh_warning("You must define a custom gamedata to use \"%s\"", word);
						ignorelines(f);
					}
					else if (i > 0 && i <= MAXEMBLEMS)
					{
						if (numemblems < i)
							numemblems = i;
						reademblemdata(f, i);
					}
					else
					{
						deh_warning("Emblem number %d out of range (1 - %d)", i, MAXEMBLEMS);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "EXTRAEMBLEM"))
				{
					if (!(refreshdirmenu & REFRESHDIR_GAMEDATA))
					{
						deh_warning("You must define a custom gamedata to use \"%s\"", word);
						ignorelines(f);
					}
					else if (i > 0 && i <= MAXEXTRAEMBLEMS)
					{
						if (numextraemblems < i)
							numextraemblems = i;
						readextraemblemdata(f, i);
					}
					else
					{
						deh_warning("Extra emblem number %d out of range (1 - %d)", i, MAXEXTRAEMBLEMS);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "UNLOCKABLE"))
				{
					if (!(refreshdirmenu & REFRESHDIR_GAMEDATA))
					{
						deh_warning("You must define a custom gamedata to use \"%s\"", word);
						ignorelines(f);
					}
					else if (i > 0 && i <= MAXUNLOCKABLES)
						readunlockable(f, i - 1);
					else
					{
						deh_warning("Unlockable number %d out of range (1 - %d)", i, MAXUNLOCKABLES);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "CONDITIONSET"))
				{
					if (!(refreshdirmenu & REFRESHDIR_GAMEDATA))
					{
						deh_warning("You must define a custom gamedata to use \"%s\"", word);
						ignorelines(f);
					}
					else if (i > 0 && i <= MAXCONDITIONSETS)
						readconditionset(f, (UINT8)i);
					else
					{
						deh_warning("Condition set number %d out of range (1 - %d)", i, MAXCONDITIONSETS);
						ignorelines(f);
					}
				}
				else if (fastcmp(word, "SRB2KART"))
				{
					INT32 ver = searchvalue(strtok(NULL, "\n"));
					if (ver != PATCHVERSION)
						deh_warning("Patch is for SRB2Kart version %d,\nonly version %d is supported", ver, PATCHVERSION);
				}
				else if (fastcmp(word, "SRB2"))
				{
					if (mainwads) // srb2.srb triggers this warning otherwise
						deh_warning("Patch is only compatible with base SRB2.");
				}
				// Clear all data in certain locations (mostly for unlocks)
				// Unless you REALLY want to piss people off,
				// define a custom gamedata /before/ doing this!!
				// (then again, modifiedgame will prevent game data saving anyway)
				else if (fastcmp(word, "CLEAR"))
				{
					boolean clearall = (fastcmp(word2, "ALL"));

					if (!(refreshdirmenu & REFRESHDIR_GAMEDATA))
					{
						deh_warning("You must define a custom gamedata to use \"%s\"", word);
						continue;
					}

					if (clearall || fastcmp(word2, "UNLOCKABLES"))
						memset(&unlockables, 0, sizeof(unlockables));

					if (clearall || fastcmp(word2, "EMBLEMS"))
					{
						memset(&emblemlocations, 0, sizeof(emblemlocations));
						numemblems = 0;
					}

					if (clearall || fastcmp(word2, "EXTRAEMBLEMS"))
					{
						memset(&extraemblems, 0, sizeof(extraemblems));
						numextraemblems = 0;
					}

					if (clearall || fastcmp(word2, "CONDITIONSETS"))
						clear_conditionsets();

					if (clearall || fastcmp(word2, "LEVELS"))
						clear_levels();
				}
				else
					deh_warning("Unknown word: %s", word);
			}
			else
				deh_warning("missing argument for '%s'", word);
		}
		else
			deh_warning("No word in this line: %s", s);
	} // end while

	dbg_line = -1;
	if (deh_num_warning)
	{
		CONS_Printf(M_GetText("%d warning%s in the SOC lump\n"), deh_num_warning, deh_num_warning == 1 ? "" : "s");
		if (devparm) {
			I_Error("%s%s",va(M_GetText("%d warning%s in the SOC lump\n"), deh_num_warning, deh_num_warning == 1 ? "" : "s"), M_GetText("See log.txt for details.\n"));
			//while (!I_GetKey())
				//I_OsPolling();
		}
	}

	deh_loaded = true;
	Z_Free(s);
}

// read dehacked lump in a wad (there is special trick for for deh
// file that are converted to wad in w_wad.c)
void DEH_LoadDehackedLumpPwad(UINT16 wad, UINT16 lump)
{
	MYFILE f;
	f.wad = wad;
	f.size = W_LumpLengthPwad(wad, lump);
	f.data = Z_Malloc(f.size + 1, PU_STATIC, NULL);
	W_ReadLumpPwad(wad, lump, f.data);
	f.curpos = f.data;
	f.data[f.size] = 0;
	DEH_LoadDehackedFile(&f, wad);
	Z_Free(f.data);
}

void DEH_LoadDehackedLump(lumpnum_t lumpnum)
{
	DEH_LoadDehackedLumpPwad(WADFILENUM(lumpnum),LUMPNUM(lumpnum));
}

////////////////////////////////////////////////////////////////////////////////
// CRAZY LIST OF STATE NAMES AND ALL FROM HERE DOWN
// TODO: Make this all a seperate file or something, like part of info.c??
// TODO: Read the list from a text lump in a WAD as necessary instead
// or something, don't just keep it all in memory like this.
// TODO: Make the lists public so we can start using actual mobj
// and state names in warning and error messages! :D

// RegEx to generate this from info.h: ^\tS_([^,]+), --> \t"S_\1",
// I am leaving the prefixes solely for clarity to programmers,
// because sadly no one remembers this place while searching for full state names.
static const char *const STATE_LIST[] = { // array length left dynamic for sanity testing later.
	"S_NULL",
	"S_UNKNOWN",
	"S_INVISIBLE", // state for invisible sprite

	"S_SPAWNSTATE",
	"S_SEESTATE",
	"S_MELEESTATE",
	"S_MISSILESTATE",
	"S_DEATHSTATE",
	"S_XDEATHSTATE",
	"S_RAISESTATE",

	// Thok
	"S_THOK",

	// SRB2kart Frames
	"S_KART_STND1",
	"S_KART_STND2",
	"S_KART_STND1_L",
	"S_KART_STND2_L",
	"S_KART_STND1_R",
	"S_KART_STND2_R",
	"S_KART_WALK1",
	"S_KART_WALK2",
	"S_KART_WALK1_L",
	"S_KART_WALK2_L",
	"S_KART_WALK1_R",
	"S_KART_WALK2_R",
	"S_KART_RUN1",
	"S_KART_RUN2",
	"S_KART_RUN1_L",
	"S_KART_RUN2_L",
	"S_KART_RUN1_R",
	"S_KART_RUN2_R",
	"S_KART_DRIFT1_L",
	"S_KART_DRIFT2_L",
	"S_KART_DRIFT1_R",
	"S_KART_DRIFT2_R",
	"S_KART_SPIN",
	"S_KART_PAIN",
	"S_KART_SQUISH",

	// technically the player goes here but it's an infinite tic state
	"S_OBJPLACE_DUMMY",

	// 1-Up Box Sprites (uses player sprite)
	"S_PLAY_BOX1",
	"S_PLAY_BOX2",
	"S_PLAY_ICON1",
	"S_PLAY_ICON2",
	"S_PLAY_ICON3",

	// Level end sign (uses player sprite)
	"S_PLAY_SIGN",

	// Blue Crawla
	"S_POSS_STND",
	"S_POSS_RUN1",
	"S_POSS_RUN2",
	"S_POSS_RUN3",
	"S_POSS_RUN4",
	"S_POSS_RUN5",
	"S_POSS_RUN6",

	// Red Crawla
	"S_SPOS_STND",
	"S_SPOS_RUN1",
	"S_SPOS_RUN2",
	"S_SPOS_RUN3",
	"S_SPOS_RUN4",
	"S_SPOS_RUN5",
	"S_SPOS_RUN6",

	// Greenflower Fish
	"S_FISH1",
	"S_FISH2",
	"S_FISH3",
	"S_FISH4",

	// Buzz (Gold)
	"S_BUZZLOOK1",
	"S_BUZZLOOK2",
	"S_BUZZFLY1",
	"S_BUZZFLY2",

	// Buzz (Red)
	"S_RBUZZLOOK1",
	"S_RBUZZLOOK2",
	"S_RBUZZFLY1",
	"S_RBUZZFLY2",

	// AquaBuzz
	"S_BBUZZFLY1",
	"S_BBUZZFLY2",

	// Jetty-Syn Bomber
	"S_JETBLOOK1",
	"S_JETBLOOK2",
	"S_JETBZOOM1",
	"S_JETBZOOM2",

	// Jetty-Syn Gunner
	"S_JETGLOOK1",
	"S_JETGLOOK2",
	"S_JETGZOOM1",
	"S_JETGZOOM2",
	"S_JETGSHOOT1",
	"S_JETGSHOOT2",

	// Crawla Commander
	"S_CCOMMAND1",
	"S_CCOMMAND2",
	"S_CCOMMAND3",
	"S_CCOMMAND4",

	// Deton
	"S_DETON1",
	"S_DETON2",
	"S_DETON3",
	"S_DETON4",
	"S_DETON5",
	"S_DETON6",
	"S_DETON7",
	"S_DETON8",
	"S_DETON9",
	"S_DETON10",
	"S_DETON11",
	"S_DETON12",
	"S_DETON13",
	"S_DETON14",
	"S_DETON15",
	"S_DETON16",

	// Skim Mine Dropper
	"S_SKIM1",
	"S_SKIM2",
	"S_SKIM3",
	"S_SKIM4",

	// THZ Turret
	"S_TURRET",
	"S_TURRETFIRE",
	"S_TURRETSHOCK1",
	"S_TURRETSHOCK2",
	"S_TURRETSHOCK3",
	"S_TURRETSHOCK4",
	"S_TURRETSHOCK5",
	"S_TURRETSHOCK6",
	"S_TURRETSHOCK7",
	"S_TURRETSHOCK8",
	"S_TURRETSHOCK9",

	// Popup Turret
	"S_TURRETLOOK",
	"S_TURRETSEE",
	"S_TURRETPOPUP1",
	"S_TURRETPOPUP2",
	"S_TURRETPOPUP3",
	"S_TURRETPOPUP4",
	"S_TURRETPOPUP5",
	"S_TURRETPOPUP6",
	"S_TURRETPOPUP7",
	"S_TURRETPOPUP8",
	"S_TURRETSHOOT",
	"S_TURRETPOPDOWN1",
	"S_TURRETPOPDOWN2",
	"S_TURRETPOPDOWN3",
	"S_TURRETPOPDOWN4",
	"S_TURRETPOPDOWN5",
	"S_TURRETPOPDOWN6",
	"S_TURRETPOPDOWN7",
	"S_TURRETPOPDOWN8",

	// Sharp
	"S_SHARP_ROAM1",
	"S_SHARP_ROAM2",
	"S_SHARP_AIM1",
	"S_SHARP_AIM2",
	"S_SHARP_AIM3",
	"S_SHARP_AIM4",
	"S_SHARP_SPIN",

	// Jet Jaw
	"S_JETJAW_ROAM1",
	"S_JETJAW_ROAM2",
	"S_JETJAW_ROAM3",
	"S_JETJAW_ROAM4",
	"S_JETJAW_ROAM5",
	"S_JETJAW_ROAM6",
	"S_JETJAW_ROAM7",
	"S_JETJAW_ROAM8",
	"S_JETJAW_CHOMP1",
	"S_JETJAW_CHOMP2",
	"S_JETJAW_CHOMP3",
	"S_JETJAW_CHOMP4",
	"S_JETJAW_CHOMP5",
	"S_JETJAW_CHOMP6",
	"S_JETJAW_CHOMP7",
	"S_JETJAW_CHOMP8",
	"S_JETJAW_CHOMP9",
	"S_JETJAW_CHOMP10",
	"S_JETJAW_CHOMP11",
	"S_JETJAW_CHOMP12",
	"S_JETJAW_CHOMP13",
	"S_JETJAW_CHOMP14",
	"S_JETJAW_CHOMP15",
	"S_JETJAW_CHOMP16",

	// Snailer
	"S_SNAILER1",

	// Vulture
	"S_VULTURE_STND",
	"S_VULTURE_VTOL1",
	"S_VULTURE_VTOL2",
	"S_VULTURE_VTOL3",
	"S_VULTURE_VTOL4",
	"S_VULTURE_ZOOM1",
	"S_VULTURE_ZOOM2",
	"S_VULTURE_ZOOM3",
	"S_VULTURE_ZOOM4",
	"S_VULTURE_ZOOM5",

	// Pointy
	"S_POINTY1",
	"S_POINTYBALL1",

	// Robo-Hood
	"S_ROBOHOOD_LOOK",
	"S_ROBOHOOD_STND",
	"S_ROBOHOOD_SHOOT",
	"S_ROBOHOOD_JUMP",
	"S_ROBOHOOD_JUMP2",
	"S_ROBOHOOD_FALL",

	// CastleBot FaceStabber
	"S_FACESTABBER_STND1",
	"S_FACESTABBER_STND2",
	"S_FACESTABBER_STND3",
	"S_FACESTABBER_STND4",
	"S_FACESTABBER_STND5",
	"S_FACESTABBER_STND6",
	"S_FACESTABBER_CHARGE1",
	"S_FACESTABBER_CHARGE2",
	"S_FACESTABBER_CHARGE3",
	"S_FACESTABBER_CHARGE4",

	// Egg Guard
	"S_EGGGUARD_STND",
	"S_EGGGUARD_WALK1",
	"S_EGGGUARD_WALK2",
	"S_EGGGUARD_WALK3",
	"S_EGGGUARD_WALK4",
	"S_EGGGUARD_MAD1",
	"S_EGGGUARD_MAD2",
	"S_EGGGUARD_MAD3",
	"S_EGGGUARD_RUN1",
	"S_EGGGUARD_RUN2",
	"S_EGGGUARD_RUN3",
	"S_EGGGUARD_RUN4",

	// Egg Shield for Egg Guard
	"S_EGGSHIELD",

	// Green Snapper
	"S_GSNAPPER_STND",
	"S_GSNAPPER1",
	"S_GSNAPPER2",
	"S_GSNAPPER3",
	"S_GSNAPPER4",

	// Minus
	"S_MINUS_STND",
	"S_MINUS_DIGGING",
	"S_MINUS_POPUP",
	"S_MINUS_UPWARD1",
	"S_MINUS_UPWARD2",
	"S_MINUS_UPWARD3",
	"S_MINUS_UPWARD4",
	"S_MINUS_UPWARD5",
	"S_MINUS_UPWARD6",
	"S_MINUS_UPWARD7",
	"S_MINUS_UPWARD8",
	"S_MINUS_DOWNWARD1",
	"S_MINUS_DOWNWARD2",
	"S_MINUS_DOWNWARD3",
	"S_MINUS_DOWNWARD4",
	"S_MINUS_DOWNWARD5",
	"S_MINUS_DOWNWARD6",
	"S_MINUS_DOWNWARD7",
	"S_MINUS_DOWNWARD8",

	// Spring Shell
	"S_SSHELL_STND",
	"S_SSHELL_RUN1",
	"S_SSHELL_RUN2",
	"S_SSHELL_RUN3",
	"S_SSHELL_RUN4",
	"S_SSHELL_SPRING1",
	"S_SSHELL_SPRING2",
	"S_SSHELL_SPRING3",
	"S_SSHELL_SPRING4",

	// Spring Shell (yellow)
	"S_YSHELL_STND",
	"S_YSHELL_RUN1",
	"S_YSHELL_RUN2",
	"S_YSHELL_RUN3",
	"S_YSHELL_RUN4",
	"S_YSHELL_SPRING1",
	"S_YSHELL_SPRING2",
	"S_YSHELL_SPRING3",
	"S_YSHELL_SPRING4",

	// Unidus
	"S_UNIDUS_STND",
	"S_UNIDUS_RUN",
	"S_UNIDUS_BALL",

	// Boss Explosion
	"S_BPLD1",
	"S_BPLD2",
	"S_BPLD3",
	"S_BPLD4",
	"S_BPLD5",
	"S_BPLD6",
	"S_BPLD7",

	// S3&K Boss Explosion
	"S_SONIC3KBOSSEXPLOSION1",
	"S_SONIC3KBOSSEXPLOSION2",
	"S_SONIC3KBOSSEXPLOSION3",
	"S_SONIC3KBOSSEXPLOSION4",
	"S_SONIC3KBOSSEXPLOSION5",
	"S_SONIC3KBOSSEXPLOSION6",

	"S_JETFUME1",
	"S_JETFUME2",

	// Boss 1
	"S_EGGMOBILE_STND",
	"S_EGGMOBILE_LATK1",
	"S_EGGMOBILE_LATK2",
	"S_EGGMOBILE_LATK3",
	"S_EGGMOBILE_LATK4",
	"S_EGGMOBILE_LATK5",
	"S_EGGMOBILE_LATK6",
	"S_EGGMOBILE_LATK7",
	"S_EGGMOBILE_LATK8",
	"S_EGGMOBILE_LATK9",
	"S_EGGMOBILE_LATK10",
	"S_EGGMOBILE_RATK1",
	"S_EGGMOBILE_RATK2",
	"S_EGGMOBILE_RATK3",
	"S_EGGMOBILE_RATK4",
	"S_EGGMOBILE_RATK5",
	"S_EGGMOBILE_RATK6",
	"S_EGGMOBILE_RATK7",
	"S_EGGMOBILE_RATK8",
	"S_EGGMOBILE_RATK9",
	"S_EGGMOBILE_RATK10",
	"S_EGGMOBILE_PANIC1",
	"S_EGGMOBILE_PANIC2",
	"S_EGGMOBILE_PANIC3",
	"S_EGGMOBILE_PANIC4",
	"S_EGGMOBILE_PANIC5",
	"S_EGGMOBILE_PANIC6",
	"S_EGGMOBILE_PANIC7",
	"S_EGGMOBILE_PANIC8",
	"S_EGGMOBILE_PANIC9",
	"S_EGGMOBILE_PANIC10",
	"S_EGGMOBILE_PAIN",
	"S_EGGMOBILE_PAIN2",
	"S_EGGMOBILE_DIE1",
	"S_EGGMOBILE_DIE2",
	"S_EGGMOBILE_DIE3",
	"S_EGGMOBILE_DIE4",
	"S_EGGMOBILE_DIE5",
	"S_EGGMOBILE_DIE6",
	"S_EGGMOBILE_DIE7",
	"S_EGGMOBILE_DIE8",
	"S_EGGMOBILE_DIE9",
	"S_EGGMOBILE_DIE10",
	"S_EGGMOBILE_DIE11",
	"S_EGGMOBILE_DIE12",
	"S_EGGMOBILE_DIE13",
	"S_EGGMOBILE_DIE14",
	"S_EGGMOBILE_FLEE1",
	"S_EGGMOBILE_FLEE2",
	"S_EGGMOBILE_BALL",
	"S_EGGMOBILE_TARGET",

	// Boss 2
	"S_EGGMOBILE2_STND",
	"S_EGGMOBILE2_POGO1",
	"S_EGGMOBILE2_POGO2",
	"S_EGGMOBILE2_POGO3",
	"S_EGGMOBILE2_POGO4",
	"S_EGGMOBILE2_POGO5",
	"S_EGGMOBILE2_POGO6",
	"S_EGGMOBILE2_POGO7",
	"S_EGGMOBILE2_PAIN",
	"S_EGGMOBILE2_PAIN2",
	"S_EGGMOBILE2_DIE1",
	"S_EGGMOBILE2_DIE2",
	"S_EGGMOBILE2_DIE3",
	"S_EGGMOBILE2_DIE4",
	"S_EGGMOBILE2_DIE5",
	"S_EGGMOBILE2_DIE6",
	"S_EGGMOBILE2_DIE7",
	"S_EGGMOBILE2_DIE8",
	"S_EGGMOBILE2_DIE9",
	"S_EGGMOBILE2_DIE10",
	"S_EGGMOBILE2_DIE11",
	"S_EGGMOBILE2_DIE12",
	"S_EGGMOBILE2_DIE13",
	"S_EGGMOBILE2_DIE14",
	"S_EGGMOBILE2_FLEE1",
	"S_EGGMOBILE2_FLEE2",

	"S_BOSSTANK1",
	"S_BOSSTANK2",
	"S_BOSSSPIGOT",

	// Boss 2 Goop
	"S_GOOP1",
	"S_GOOP2",
	"S_GOOP3",

	// Boss 3
	"S_EGGMOBILE3_STND",
	"S_EGGMOBILE3_ATK1",
	"S_EGGMOBILE3_ATK2",
	"S_EGGMOBILE3_ATK3A",
	"S_EGGMOBILE3_ATK3B",
	"S_EGGMOBILE3_ATK3C",
	"S_EGGMOBILE3_ATK3D",
	"S_EGGMOBILE3_ATK4",
	"S_EGGMOBILE3_ATK5",
	"S_EGGMOBILE3_LAUGH1",
	"S_EGGMOBILE3_LAUGH2",
	"S_EGGMOBILE3_LAUGH3",
	"S_EGGMOBILE3_LAUGH4",
	"S_EGGMOBILE3_LAUGH5",
	"S_EGGMOBILE3_LAUGH6",
	"S_EGGMOBILE3_LAUGH7",
	"S_EGGMOBILE3_LAUGH8",
	"S_EGGMOBILE3_LAUGH9",
	"S_EGGMOBILE3_LAUGH10",
	"S_EGGMOBILE3_LAUGH11",
	"S_EGGMOBILE3_LAUGH12",
	"S_EGGMOBILE3_LAUGH13",
	"S_EGGMOBILE3_LAUGH14",
	"S_EGGMOBILE3_LAUGH15",
	"S_EGGMOBILE3_LAUGH16",
	"S_EGGMOBILE3_LAUGH17",
	"S_EGGMOBILE3_LAUGH18",
	"S_EGGMOBILE3_LAUGH19",
	"S_EGGMOBILE3_LAUGH20",
	"S_EGGMOBILE3_PAIN",
	"S_EGGMOBILE3_PAIN2",
	"S_EGGMOBILE3_DIE1",
	"S_EGGMOBILE3_DIE2",
	"S_EGGMOBILE3_DIE3",
	"S_EGGMOBILE3_DIE4",
	"S_EGGMOBILE3_DIE5",
	"S_EGGMOBILE3_DIE6",
	"S_EGGMOBILE3_DIE7",
	"S_EGGMOBILE3_DIE8",
	"S_EGGMOBILE3_DIE9",
	"S_EGGMOBILE3_DIE10",
	"S_EGGMOBILE3_DIE11",
	"S_EGGMOBILE3_DIE12",
	"S_EGGMOBILE3_DIE13",
	"S_EGGMOBILE3_DIE14",
	"S_EGGMOBILE3_FLEE1",
	"S_EGGMOBILE3_FLEE2",

	// Boss 3 Propeller
	"S_PROPELLER1",
	"S_PROPELLER2",
	"S_PROPELLER3",
	"S_PROPELLER4",
	"S_PROPELLER5",
	"S_PROPELLER6",
	"S_PROPELLER7",

	// Boss 3 pinch
	"S_FAKEMOBILE_INIT",
	"S_FAKEMOBILE",
	"S_FAKEMOBILE_ATK1",
	"S_FAKEMOBILE_ATK2",
	"S_FAKEMOBILE_ATK3A",
	"S_FAKEMOBILE_ATK3B",
	"S_FAKEMOBILE_ATK3C",
	"S_FAKEMOBILE_ATK3D",
	"S_FAKEMOBILE_ATK4",
	"S_FAKEMOBILE_ATK5",

	// Boss 4
	"S_EGGMOBILE4_STND",
	"S_EGGMOBILE4_LATK1",
	"S_EGGMOBILE4_LATK2",
	"S_EGGMOBILE4_LATK3",
	"S_EGGMOBILE4_LATK4",
	"S_EGGMOBILE4_LATK5",
	"S_EGGMOBILE4_LATK6",
	"S_EGGMOBILE4_RATK1",
	"S_EGGMOBILE4_RATK2",
	"S_EGGMOBILE4_RATK3",
	"S_EGGMOBILE4_RATK4",
	"S_EGGMOBILE4_RATK5",
	"S_EGGMOBILE4_RATK6",
	"S_EGGMOBILE4_RAISE1",
	"S_EGGMOBILE4_RAISE2",
	"S_EGGMOBILE4_RAISE3",
	"S_EGGMOBILE4_RAISE4",
	"S_EGGMOBILE4_RAISE5",
	"S_EGGMOBILE4_RAISE6",
	"S_EGGMOBILE4_RAISE7",
	"S_EGGMOBILE4_RAISE8",
	"S_EGGMOBILE4_RAISE9",
	"S_EGGMOBILE4_RAISE10",
	"S_EGGMOBILE4_PAIN",
	"S_EGGMOBILE4_DIE1",
	"S_EGGMOBILE4_DIE2",
	"S_EGGMOBILE4_DIE3",
	"S_EGGMOBILE4_DIE4",
	"S_EGGMOBILE4_DIE5",
	"S_EGGMOBILE4_DIE6",
	"S_EGGMOBILE4_DIE7",
	"S_EGGMOBILE4_DIE8",
	"S_EGGMOBILE4_DIE9",
	"S_EGGMOBILE4_DIE10",
	"S_EGGMOBILE4_DIE11",
	"S_EGGMOBILE4_DIE12",
	"S_EGGMOBILE4_DIE13",
	"S_EGGMOBILE4_DIE14",
	"S_EGGMOBILE4_FLEE1",
	"S_EGGMOBILE4_FLEE2",
	"S_EGGMOBILE4_MACE",

	// Boss 4 jet flame
	"S_JETFLAME1",
	"S_JETFLAME2",

	// Black Eggman (Boss 7)
	"S_BLACKEGG_STND",
	"S_BLACKEGG_STND2",
	"S_BLACKEGG_WALK1",
	"S_BLACKEGG_WALK2",
	"S_BLACKEGG_WALK3",
	"S_BLACKEGG_WALK4",
	"S_BLACKEGG_WALK5",
	"S_BLACKEGG_WALK6",
	"S_BLACKEGG_SHOOT1",
	"S_BLACKEGG_SHOOT2",
	"S_BLACKEGG_PAIN1",
	"S_BLACKEGG_PAIN2",
	"S_BLACKEGG_PAIN3",
	"S_BLACKEGG_PAIN4",
	"S_BLACKEGG_PAIN5",
	"S_BLACKEGG_PAIN6",
	"S_BLACKEGG_PAIN7",
	"S_BLACKEGG_PAIN8",
	"S_BLACKEGG_PAIN9",
	"S_BLACKEGG_PAIN10",
	"S_BLACKEGG_PAIN11",
	"S_BLACKEGG_PAIN12",
	"S_BLACKEGG_PAIN13",
	"S_BLACKEGG_PAIN14",
	"S_BLACKEGG_PAIN15",
	"S_BLACKEGG_PAIN16",
	"S_BLACKEGG_PAIN17",
	"S_BLACKEGG_PAIN18",
	"S_BLACKEGG_PAIN19",
	"S_BLACKEGG_PAIN20",
	"S_BLACKEGG_PAIN21",
	"S_BLACKEGG_PAIN22",
	"S_BLACKEGG_PAIN23",
	"S_BLACKEGG_PAIN24",
	"S_BLACKEGG_PAIN25",
	"S_BLACKEGG_PAIN26",
	"S_BLACKEGG_PAIN27",
	"S_BLACKEGG_PAIN28",
	"S_BLACKEGG_PAIN29",
	"S_BLACKEGG_PAIN30",
	"S_BLACKEGG_PAIN31",
	"S_BLACKEGG_PAIN32",
	"S_BLACKEGG_PAIN33",
	"S_BLACKEGG_PAIN34",
	"S_BLACKEGG_PAIN35",
	"S_BLACKEGG_HITFACE1",
	"S_BLACKEGG_HITFACE2",
	"S_BLACKEGG_HITFACE3",
	"S_BLACKEGG_HITFACE4",
	"S_BLACKEGG_DIE1",
	"S_BLACKEGG_DIE2",
	"S_BLACKEGG_DIE3",
	"S_BLACKEGG_DIE4",
	"S_BLACKEGG_DIE5",
	"S_BLACKEGG_MISSILE1",
	"S_BLACKEGG_MISSILE2",
	"S_BLACKEGG_MISSILE3",
	"S_BLACKEGG_GOOP",
	"S_BLACKEGG_JUMP1",
	"S_BLACKEGG_JUMP2",
	"S_BLACKEGG_DESTROYPLAT1",
	"S_BLACKEGG_DESTROYPLAT2",
	"S_BLACKEGG_DESTROYPLAT3",

	"S_BLACKEGG_HELPER", // Collision helper

	"S_BLACKEGG_GOOP1",
	"S_BLACKEGG_GOOP2",
	"S_BLACKEGG_GOOP3",
	"S_BLACKEGG_GOOP4",
	"S_BLACKEGG_GOOP5",
	"S_BLACKEGG_GOOP6",
	"S_BLACKEGG_GOOP7",

	"S_BLACKEGG_MISSILE",

	// New Very-Last-Minute 2.1 Brak Eggman (Cy-Brak-demon)
	"S_CYBRAKDEMON_IDLE",
	"S_CYBRAKDEMON_WALK1",
	"S_CYBRAKDEMON_WALK2",
	"S_CYBRAKDEMON_WALK3",
	"S_CYBRAKDEMON_WALK4",
	"S_CYBRAKDEMON_WALK5",
	"S_CYBRAKDEMON_WALK6",
	"S_CYBRAKDEMON_CHOOSE_ATTACK1",
	"S_CYBRAKDEMON_MISSILE_ATTACK1", // Aim
	"S_CYBRAKDEMON_MISSILE_ATTACK2", // Fire
	"S_CYBRAKDEMON_MISSILE_ATTACK3", // Aim
	"S_CYBRAKDEMON_MISSILE_ATTACK4", // Fire
	"S_CYBRAKDEMON_MISSILE_ATTACK5", // Aim
	"S_CYBRAKDEMON_MISSILE_ATTACK6", // Fire
	"S_CYBRAKDEMON_FLAME_ATTACK1", // Reset
	"S_CYBRAKDEMON_FLAME_ATTACK2", // Aim
	"S_CYBRAKDEMON_FLAME_ATTACK3", // Fire
	"S_CYBRAKDEMON_FLAME_ATTACK4", // Loop
	"S_CYBRAKDEMON_CHOOSE_ATTACK2",
	"S_CYBRAKDEMON_VILE_ATTACK1",
	"S_CYBRAKDEMON_VILE_ATTACK2",
	"S_CYBRAKDEMON_VILE_ATTACK3",
	"S_CYBRAKDEMON_VILE_ATTACK4",
	"S_CYBRAKDEMON_VILE_ATTACK5",
	"S_CYBRAKDEMON_VILE_ATTACK6",
	"S_CYBRAKDEMON_NAPALM_ATTACK1",
	"S_CYBRAKDEMON_NAPALM_ATTACK2",
	"S_CYBRAKDEMON_NAPALM_ATTACK3",
	"S_CYBRAKDEMON_FINISH_ATTACK", // If just attacked, remove MF2_FRET w/out going back to spawnstate
	"S_CYBRAKDEMON_FINISH_ATTACK2", // Force a delay between attacks so you don't get bombarded with them back-to-back
	"S_CYBRAKDEMON_PAIN1",
	"S_CYBRAKDEMON_PAIN2",
	"S_CYBRAKDEMON_PAIN3",
	"S_CYBRAKDEMON_DIE1",
	"S_CYBRAKDEMON_DIE2",
	"S_CYBRAKDEMON_DIE3",
	"S_CYBRAKDEMON_DIE4",
	"S_CYBRAKDEMON_DIE5",
	"S_CYBRAKDEMON_DIE6",
	"S_CYBRAKDEMON_DIE7",
	"S_CYBRAKDEMON_DIE8",
	"S_CYBRAKDEMON_DEINVINCIBLERIZE",
	"S_CYBRAKDEMON_INVINCIBLERIZE",

	"S_CYBRAKDEMONMISSILE",
	"S_CYBRAKDEMONMISSILE_EXPLODE1",
	"S_CYBRAKDEMONMISSILE_EXPLODE2",
	"S_CYBRAKDEMONMISSILE_EXPLODE3",

	"S_CYBRAKDEMONFLAMESHOT_FLY1",
	"S_CYBRAKDEMONFLAMESHOT_FLY2",
	"S_CYBRAKDEMONFLAMESHOT_FLY3",
	"S_CYBRAKDEMONFLAMESHOT_DIE",

	"S_CYBRAKDEMONFLAMEREST",

	"S_CYBRAKDEMONELECTRICBARRIER_INIT1",
	"S_CYBRAKDEMONELECTRICBARRIER_INIT2",
	"S_CYBRAKDEMONELECTRICBARRIER_PLAYSOUND",
	"S_CYBRAKDEMONELECTRICBARRIER1",
	"S_CYBRAKDEMONELECTRICBARRIER2",
	"S_CYBRAKDEMONELECTRICBARRIER3",
	"S_CYBRAKDEMONELECTRICBARRIER4",
	"S_CYBRAKDEMONELECTRICBARRIER5",
	"S_CYBRAKDEMONELECTRICBARRIER6",
	"S_CYBRAKDEMONELECTRICBARRIER7",
	"S_CYBRAKDEMONELECTRICBARRIER8",
	"S_CYBRAKDEMONELECTRICBARRIER9",
	"S_CYBRAKDEMONELECTRICBARRIER10",
	"S_CYBRAKDEMONELECTRICBARRIER11",
	"S_CYBRAKDEMONELECTRICBARRIER12",
	"S_CYBRAKDEMONELECTRICBARRIER13",
	"S_CYBRAKDEMONELECTRICBARRIER14",
	"S_CYBRAKDEMONELECTRICBARRIER15",
	"S_CYBRAKDEMONELECTRICBARRIER16",
	"S_CYBRAKDEMONELECTRICBARRIER17",
	"S_CYBRAKDEMONELECTRICBARRIER18",
	"S_CYBRAKDEMONELECTRICBARRIER19",
	"S_CYBRAKDEMONELECTRICBARRIER20",
	"S_CYBRAKDEMONELECTRICBARRIER21",
	"S_CYBRAKDEMONELECTRICBARRIER22",
	"S_CYBRAKDEMONELECTRICBARRIER23",
	"S_CYBRAKDEMONELECTRICBARRIER24",
	"S_CYBRAKDEMONELECTRICBARRIER_DIE1",
	"S_CYBRAKDEMONELECTRICBARRIER_DIE2",
	"S_CYBRAKDEMONELECTRICBARRIER_DIE3",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOMCHECK",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOMSUCCESS",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOMCHOOSE",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM1",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM2",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM3",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM4",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM5",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM6",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM7",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM8",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM9",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM10",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM11",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOM12",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOMFAIL",
	"S_CYBRAKDEMONELECTRICBARRIER_SPARK_RANDOMLOOP",
	"S_CYBRAKDEMONELECTRICBARRIER_REVIVE1",
	"S_CYBRAKDEMONELECTRICBARRIER_REVIVE2",
	"S_CYBRAKDEMONELECTRICBARRIER_REVIVE3",

	"S_CYBRAKDEMONTARGETRETICULE1",
	"S_CYBRAKDEMONTARGETRETICULE2",
	"S_CYBRAKDEMONTARGETRETICULE3",
	"S_CYBRAKDEMONTARGETRETICULE4",
	"S_CYBRAKDEMONTARGETRETICULE5",
	"S_CYBRAKDEMONTARGETRETICULE6",
	"S_CYBRAKDEMONTARGETRETICULE7",
	"S_CYBRAKDEMONTARGETRETICULE8",
	"S_CYBRAKDEMONTARGETRETICULE9",
	"S_CYBRAKDEMONTARGETRETICULE10",
	"S_CYBRAKDEMONTARGETRETICULE11",
	"S_CYBRAKDEMONTARGETRETICULE12",
	"S_CYBRAKDEMONTARGETRETICULE13",
	"S_CYBRAKDEMONTARGETRETICULE14",

	"S_CYBRAKDEMONTARGETDOT",

	"S_CYBRAKDEMONNAPALMBOMBLARGE_FLY1",
	"S_CYBRAKDEMONNAPALMBOMBLARGE_FLY2",
	"S_CYBRAKDEMONNAPALMBOMBLARGE_FLY3",
	"S_CYBRAKDEMONNAPALMBOMBLARGE_FLY4",
	"S_CYBRAKDEMONNAPALMBOMBLARGE_DIE1", // Explode
	"S_CYBRAKDEMONNAPALMBOMBLARGE_DIE2", // Outer ring
	"S_CYBRAKDEMONNAPALMBOMBLARGE_DIE3", // Center
	"S_CYBRAKDEMONNAPALMBOMBLARGE_DIE4", // Sound

	"S_CYBRAKDEMONNAPALMBOMBSMALL",
	"S_CYBRAKDEMONNAPALMBOMBSMALL_DIE1", // Explode
	"S_CYBRAKDEMONNAPALMBOMBSMALL_DIE2", // Outer ring
	"S_CYBRAKDEMONNAPALMBOMBSMALL_DIE3", // Inner ring
	"S_CYBRAKDEMONNAPALMBOMBSMALL_DIE4", // Center
	"S_CYBRAKDEMONNAPALMBOMBSMALL_DIE5", // Sound

	"S_CYBRAKDEMONNAPALMFLAME_FLY1",
	"S_CYBRAKDEMONNAPALMFLAME_FLY2",
	"S_CYBRAKDEMONNAPALMFLAME_FLY3",
	"S_CYBRAKDEMONNAPALMFLAME_FLY4",
	"S_CYBRAKDEMONNAPALMFLAME_FLY5",
	"S_CYBRAKDEMONNAPALMFLAME_FLY6",
	"S_CYBRAKDEMONNAPALMFLAME_DIE",

	"S_CYBRAKDEMONVILEEXPLOSION1",
	"S_CYBRAKDEMONVILEEXPLOSION2",
	"S_CYBRAKDEMONVILEEXPLOSION3",

	// Metal Sonic (Race)
	// S_PLAY_STND
	"S_METALSONIC_STAND",
	// S_PLAY_TAP1
	"S_METALSONIC_WAIT1",
	"S_METALSONIC_WAIT2",
	// S_PLAY_RUN1
	"S_METALSONIC_WALK1",
	"S_METALSONIC_WALK2",
	"S_METALSONIC_WALK3",
	"S_METALSONIC_WALK4",
	"S_METALSONIC_WALK5",
	"S_METALSONIC_WALK6",
	"S_METALSONIC_WALK7",
	"S_METALSONIC_WALK8",
	// S_PLAY_SPD1
	"S_METALSONIC_RUN1",
	"S_METALSONIC_RUN2",
	"S_METALSONIC_RUN3",
	"S_METALSONIC_RUN4",
	// Metal Sonic (Battle)
	"S_METALSONIC_FLOAT",
	"S_METALSONIC_VECTOR",
	"S_METALSONIC_STUN",
	"S_METALSONIC_BLOCK",
	"S_METALSONIC_RAISE",
	"S_METALSONIC_GATHER",
	"S_METALSONIC_DASH",
	"S_METALSONIC_BOUNCE",
	"S_METALSONIC_SHOOT",
	"S_METALSONIC_PAIN",
	"S_METALSONIC_DEATH",
	"S_METALSONIC_FLEE1",
	"S_METALSONIC_FLEE2",
	"S_METALSONIC_FLEE3",
	"S_METALSONIC_FLEE4",

	"S_MSSHIELD_F1",
	"S_MSSHIELD_F2",
	"S_MSSHIELD_F3",
	"S_MSSHIELD_F4",
	"S_MSSHIELD_F5",
	"S_MSSHIELD_F6",
	"S_MSSHIELD_F7",
	"S_MSSHIELD_F8",
	"S_MSSHIELD_F9",
	"S_MSSHIELD_F10",
	"S_MSSHIELD_F11",
	"S_MSSHIELD_F12",

	// Ring
	"S_RING",

	// Blue Sphere for special stages
	"S_BLUEBALL",
	"S_BLUEBALLSPARK",

	// Gravity Wells for special stages
	"S_GRAVWELLGREEN",
	"S_GRAVWELLGREEN2",
	"S_GRAVWELLGREEN3",

	"S_GRAVWELLRED",
	"S_GRAVWELLRED2",
	"S_GRAVWELLRED3",

	// Individual Team Rings
	"S_TEAMRING",

	// Special Stage Token
	"S_EMMY",

	// Special Stage Token
	"S_TOKEN",
	"S_MOVINGTOKEN",

	// CTF Flags
	"S_REDFLAG",
	"S_BLUEFLAG",

	// Emblem
	"S_EMBLEM1",
	"S_EMBLEM2",
	"S_EMBLEM3",
	"S_EMBLEM4",
	"S_EMBLEM5",
	"S_EMBLEM6",
	"S_EMBLEM7",
	"S_EMBLEM8",
	"S_EMBLEM9",
	"S_EMBLEM10",
	"S_EMBLEM11",
	"S_EMBLEM12",
	"S_EMBLEM13",
	"S_EMBLEM14",
	"S_EMBLEM15",
	"S_EMBLEM16",
	"S_EMBLEM17",
	"S_EMBLEM18",
	"S_EMBLEM19",
	"S_EMBLEM20",
	"S_EMBLEM21",
	"S_EMBLEM22",
	"S_EMBLEM23",
	"S_EMBLEM24",
	"S_EMBLEM25",
	"S_EMBLEM26",

	// Chaos Emeralds
	"S_CEMG1",
	"S_CEMG2",
	"S_CEMG3",
	"S_CEMG4",
	"S_CEMG5",
	"S_CEMG6",
	"S_CEMG7",

	// Emeralds (for hunt)
	"S_EMER1",

	"S_FAN",
	"S_FAN2",
	"S_FAN3",
	"S_FAN4",
	"S_FAN5",

	// Bubble Source
	"S_BUBBLES1",
	"S_BUBBLES2",

	// Level End Sign
	"S_SIGN1",
	"S_SIGN2",
	"S_SIGN3",
	"S_SIGN4",
	"S_SIGN5",
	"S_SIGN6",
	"S_SIGN7",
	"S_SIGN8",
	"S_SIGN9",
	"S_SIGN10",
	"S_SIGN11",
	"S_SIGN12",
	"S_SIGN13",
	"S_SIGN14",
	"S_SIGN15",
	"S_SIGN16",
	"S_SIGN17",
	"S_SIGN18",
	"S_SIGN19",
	"S_SIGN20",
	"S_SIGN_END",

	// Steam Riser
	"S_STEAM1",
	"S_STEAM2",
	"S_STEAM3",
	"S_STEAM4",
	"S_STEAM5",
	"S_STEAM6",
	"S_STEAM7",
	"S_STEAM8",

	// Spike Ball
	"S_SPIKEBALL1",
	"S_SPIKEBALL2",
	"S_SPIKEBALL3",
	"S_SPIKEBALL4",
	"S_SPIKEBALL5",
	"S_SPIKEBALL6",
	"S_SPIKEBALL7",
	"S_SPIKEBALL8",

	// Fire Shield's Spawn
	"S_SPINFIRE1",
	"S_SPINFIRE2",
	"S_SPINFIRE3",
	"S_SPINFIRE4",
	"S_SPINFIRE5",
	"S_SPINFIRE6",

	// Spikes
	"S_SPIKE1",
	"S_SPIKE2",
	"S_SPIKE3",
	"S_SPIKE4",
	"S_SPIKE5",
	"S_SPIKE6",
	"S_SPIKED1",
	"S_SPIKED2",

	// Starpost
	"S_STARPOST_IDLE",
	"S_STARPOST_FLASH",
	"S_STARPOST_SPIN",

	// Big floating mine
	"S_BIGMINE1",
	"S_BIGMINE2",
	"S_BIGMINE3",
	"S_BIGMINE4",
	"S_BIGMINE5",
	"S_BIGMINE6",
	"S_BIGMINE7",
	"S_BIGMINE8",

	// Cannon Launcher
	"S_CANNONLAUNCHER1",
	"S_CANNONLAUNCHER2",
	"S_CANNONLAUNCHER3",

	// Super Ring Box
	"S_SUPERRINGBOX",
	"S_SUPERRINGBOX1",
	"S_SUPERRINGBOX2",
	"S_SUPERRINGBOX3",
	"S_SUPERRINGBOX4",
	"S_SUPERRINGBOX5",
	"S_SUPERRINGBOX6",

	// Red Team Ring Box
	"S_REDRINGBOX",
	"S_REDRINGBOX1",

	// Blue Team Ring Box
	"S_BLUERINGBOX",
	"S_BLUERINGBOX1",

	// Super Sneakers Box
	"S_SHTV",
	"S_SHTV1",
	"S_SHTV2",
	"S_SHTV3",
	"S_SHTV4",
	"S_SHTV5",
	"S_SHTV6",

	// Invincibility Box
	"S_PINV",
	"S_PINV1",
	"S_PINV2",
	"S_PINV3",
	"S_PINV4",
	"S_PINV5",
	"S_PINV6",

	// 1-Up Box
	"S_PRUP",
	"S_PRUP1",
	"S_PRUP2",
	"S_PRUP3",
	"S_PRUP4",
	"S_PRUP5",
	"S_PRUP6",

	// Ring Shield Box
	"S_YLTV",
	"S_YLTV1",
	"S_YLTV2",
	"S_YLTV3",
	"S_YLTV4",
	"S_YLTV5",
	"S_YLTV6",

	// Force Shield Box
	"S_BLTV1",
	"S_BLTV2",
	"S_BLTV3",
	"S_BLTV4",
	"S_BLTV5",
	"S_BLTV6",
	"S_BLTV7",

	// Bomb Shield Box
	"S_BKTV1",
	"S_BKTV2",
	"S_BKTV3",
	"S_BKTV4",
	"S_BKTV5",
	"S_BKTV6",
	"S_BKTV7",

	// Jump Shield Box
	"S_WHTV1",
	"S_WHTV2",
	"S_WHTV3",
	"S_WHTV4",
	"S_WHTV5",
	"S_WHTV6",
	"S_WHTV7",

	// Water Shield Box
	"S_GRTV",
	"S_GRTV1",
	"S_GRTV2",
	"S_GRTV3",
	"S_GRTV4",
	"S_GRTV5",
	"S_GRTV6",

	// Pity Shield Box
	"S_PITV1",
	"S_PITV2",
	"S_PITV3",
	"S_PITV4",
	"S_PITV5",
	"S_PITV6",
	"S_PITV7",

	// Eggman Box
	"S_EGGTV1",
	"S_EGGTV2",
	"S_EGGTV3",
	"S_EGGTV4",
	"S_EGGTV5",
	"S_EGGTV6",
	"S_EGGTV7",

	// Teleport Box
	"S_MIXUPBOX1",
	"S_MIXUPBOX2",
	"S_MIXUPBOX3",
	"S_MIXUPBOX4",
	"S_MIXUPBOX5",
	"S_MIXUPBOX6",
	"S_MIXUPBOX7",

	// Recycler Box
	"S_RECYCLETV1",
	"S_RECYCLETV2",
	"S_RECYCLETV3",
	"S_RECYCLETV4",
	"S_RECYCLETV5",
	"S_RECYCLETV6",
	"S_RECYCLETV7",

	// Question Box
	"S_RANDOMBOX1",
	"S_RANDOMBOX2",
	"S_RANDOMBOX3",

	// Gravity Boots Box
	"S_GBTV1",
	"S_GBTV2",
	"S_GBTV3",
	"S_GBTV4",
	"S_GBTV5",
	"S_GBTV6",
	"S_GBTV7",

	// Score boxes
	"S_SCORETVA1",
	"S_SCORETVA2",
	"S_SCORETVA3",
	"S_SCORETVA4",
	"S_SCORETVA5",
	"S_SCORETVA6",
	"S_SCORETVA7",
	"S_SCORETVB1",
	"S_SCORETVB2",
	"S_SCORETVB3",
	"S_SCORETVB4",
	"S_SCORETVB5",
	"S_SCORETVB6",
	"S_SCORETVB7",

	// Monitor Explosion
	"S_MONITOREXPLOSION1",
	"S_MONITOREXPLOSION2",

	"S_REDMONITOREXPLOSION1",
	"S_REDMONITOREXPLOSION2",

	"S_BLUEMONITOREXPLOSION1",
	"S_BLUEMONITOREXPLOSION2",

	"S_ROCKET",

	"S_LASER",

	"S_TORPEDO",

	"S_ENERGYBALL1",
	"S_ENERGYBALL2",

	// Skim Mine, also used by Jetty-Syn bomber
	"S_MINE1",
	"S_MINE_BOOM1",
	"S_MINE_BOOM2",
	"S_MINE_BOOM3",
	"S_MINE_BOOM4",

	// Jetty-Syn Bullet
	"S_JETBULLET1",
	"S_JETBULLET2",

	"S_TURRETLASER",
	"S_TURRETLASEREXPLODE1",
	"S_TURRETLASEREXPLODE2",

	// Cannonball
	"S_CANNONBALL1",

	// Arrow
	"S_ARROW",
	"S_ARROWUP",
	"S_ARROWDOWN",

	// Trapgoyle Demon fire
	"S_DEMONFIRE1",
	"S_DEMONFIRE2",
	"S_DEMONFIRE3",
	"S_DEMONFIRE4",
	"S_DEMONFIRE5",
	"S_DEMONFIRE6",

	"S_GFZFLOWERA",
	"S_GFZFLOWERA2",

	"S_GFZFLOWERB1",
	"S_GFZFLOWERB2",

	"S_GFZFLOWERC1",

	"S_BERRYBUSH",
	"S_BUSH",

	// THZ Plant
	"S_THZPLANT1",
	"S_THZPLANT2",
	"S_THZPLANT3",
	"S_THZPLANT4",

	// THZ Alarm
	"S_ALARM1",

	// Deep Sea Gargoyle
	"S_GARGOYLE",

	// DSZ Seaweed
	"S_SEAWEED1",
	"S_SEAWEED2",
	"S_SEAWEED3",
	"S_SEAWEED4",
	"S_SEAWEED5",
	"S_SEAWEED6",

	// Dripping Water
	"S_DRIPA1",
	"S_DRIPA2",
	"S_DRIPA3",
	"S_DRIPA4",
	"S_DRIPB1",
	"S_DRIPC1",
	"S_DRIPC2",

	// Coral 1
	"S_CORAL1",

	// Coral 2
	"S_CORAL2",

	// Coral 3
	"S_CORAL3",

	// Blue Crystal
	"S_BLUECRYSTAL1",

	// CEZ Chain
	"S_CEZCHAIN",

	// Flame
	"S_FLAME1",
	"S_FLAME2",
	"S_FLAME3",
	"S_FLAME4",

	// Eggman Statue
	"S_EGGSTATUE1",

	// CEZ hidden sling
	"S_SLING1",
	"S_SLING2",

	// CEZ Small Mace Chain
	"S_SMALLMACECHAIN",

	// CEZ Big Mace Chain
	"S_BIGMACECHAIN",

	// CEZ Small Mace
	"S_SMALLMACE",

	// CEZ Big Mace
	"S_BIGMACE",

	"S_CEZFLOWER1",

	// Big Tumbleweed
	"S_BIGTUMBLEWEED",
	"S_BIGTUMBLEWEED_ROLL1",
	"S_BIGTUMBLEWEED_ROLL2",
	"S_BIGTUMBLEWEED_ROLL3",
	"S_BIGTUMBLEWEED_ROLL4",
	"S_BIGTUMBLEWEED_ROLL5",
	"S_BIGTUMBLEWEED_ROLL6",
	"S_BIGTUMBLEWEED_ROLL7",
	"S_BIGTUMBLEWEED_ROLL8",

	// Little Tumbleweed
	"S_LITTLETUMBLEWEED",
	"S_LITTLETUMBLEWEED_ROLL1",
	"S_LITTLETUMBLEWEED_ROLL2",
	"S_LITTLETUMBLEWEED_ROLL3",
	"S_LITTLETUMBLEWEED_ROLL4",
	"S_LITTLETUMBLEWEED_ROLL5",
	"S_LITTLETUMBLEWEED_ROLL6",
	"S_LITTLETUMBLEWEED_ROLL7",
	"S_LITTLETUMBLEWEED_ROLL8",

	// Cacti Sprites
	"S_CACTI1",
	"S_CACTI2",
	"S_CACTI3",
	"S_CACTI4",

	// Flame jet
	"S_FLAMEJETSTND",
	"S_FLAMEJETSTART",
	"S_FLAMEJETSTOP",
	"S_FLAMEJETFLAME1",
	"S_FLAMEJETFLAME2",
	"S_FLAMEJETFLAME3",

	// Spinning flame jets
	"S_FJSPINAXISA1", // Counter-clockwise
	"S_FJSPINAXISA2",
	"S_FJSPINAXISA3",
	"S_FJSPINAXISA4",
	"S_FJSPINAXISA5",
	"S_FJSPINAXISA6",
	"S_FJSPINAXISA7",
	"S_FJSPINAXISA8",
	"S_FJSPINAXISA9",
	"S_FJSPINHELPERA1",
	"S_FJSPINHELPERA2",
	"S_FJSPINHELPERA3",
	"S_FJSPINAXISB1", // Clockwise
	"S_FJSPINAXISB2",
	"S_FJSPINAXISB3",
	"S_FJSPINAXISB4",
	"S_FJSPINAXISB5",
	"S_FJSPINAXISB6",
	"S_FJSPINAXISB7",
	"S_FJSPINAXISB8",
	"S_FJSPINAXISB9",
	"S_FJSPINHELPERB1",
	"S_FJSPINHELPERB2",
	"S_FJSPINHELPERB3",

	// Blade's flame
	"S_FLAMEJETFLAMEB1",
	"S_FLAMEJETFLAMEB2",
	"S_FLAMEJETFLAMEB3",
	"S_FLAMEJETFLAMEB4",
	"S_FLAMEJETFLAMEB5",
	"S_FLAMEJETFLAMEB6",

	// Trapgoyles
	"S_TRAPGOYLE",
	"S_TRAPGOYLE_CHECK",
	"S_TRAPGOYLE_FIRE1",
	"S_TRAPGOYLE_FIRE2",
	"S_TRAPGOYLE_FIRE3",
	"S_TRAPGOYLEUP",
	"S_TRAPGOYLEUP_CHECK",
	"S_TRAPGOYLEUP_FIRE1",
	"S_TRAPGOYLEUP_FIRE2",
	"S_TRAPGOYLEUP_FIRE3",
	"S_TRAPGOYLEDOWN",
	"S_TRAPGOYLEDOWN_CHECK",
	"S_TRAPGOYLEDOWN_FIRE1",
	"S_TRAPGOYLEDOWN_FIRE2",
	"S_TRAPGOYLEDOWN_FIRE3",
	"S_TRAPGOYLELONG",
	"S_TRAPGOYLELONG_CHECK",
	"S_TRAPGOYLELONG_FIRE1",
	"S_TRAPGOYLELONG_FIRE2",
	"S_TRAPGOYLELONG_FIRE3",
	"S_TRAPGOYLELONG_FIRE4",
	"S_TRAPGOYLELONG_FIRE5",

	// ATZ's Red Crystal/Target
	"S_TARGET_IDLE",
	"S_TARGET_HIT1",
	"S_TARGET_HIT2",
	"S_TARGET_RESPAWN",
	"S_TARGET_ALLDONE",

	// Stalagmites
	"S_STG0",
	"S_STG1",
	"S_STG2",
	"S_STG3",
	"S_STG4",
	"S_STG5",
	"S_STG6",
	"S_STG7",
	"S_STG8",
	"S_STG9",

	// Xmas-specific stuff
	"S_XMASPOLE",
	"S_CANDYCANE",
	"S_SNOWMAN",
	"S_SNOWMANHAT",
	"S_LAMPPOST1",
	"S_LAMPPOST2",
	"S_HANGSTAR",

	// Botanic Serenity's loads of scenery states
	"S_BSZTALLFLOWER_RED",
	"S_BSZTALLFLOWER_PURPLE",
	"S_BSZTALLFLOWER_BLUE",
	"S_BSZTALLFLOWER_CYAN",
	"S_BSZTALLFLOWER_YELLOW",
	"S_BSZTALLFLOWER_ORANGE",
	"S_BSZFLOWER_RED",
	"S_BSZFLOWER_PURPLE",
	"S_BSZFLOWER_BLUE",
	"S_BSZFLOWER_CYAN",
	"S_BSZFLOWER_YELLOW",
	"S_BSZFLOWER_ORANGE",
	"S_BSZSHORTFLOWER_RED",
	"S_BSZSHORTFLOWER_PURPLE",
	"S_BSZSHORTFLOWER_BLUE",
	"S_BSZSHORTFLOWER_CYAN",
	"S_BSZSHORTFLOWER_YELLOW",
	"S_BSZSHORTFLOWER_ORANGE",
	"S_BSZTULIP_RED",
	"S_BSZTULIP_PURPLE",
	"S_BSZTULIP_BLUE",
	"S_BSZTULIP_CYAN",
	"S_BSZTULIP_YELLOW",
	"S_BSZTULIP_ORANGE",
	"S_BSZCLUSTER_RED",
	"S_BSZCLUSTER_PURPLE",
	"S_BSZCLUSTER_BLUE",
	"S_BSZCLUSTER_CYAN",
	"S_BSZCLUSTER_YELLOW",
	"S_BSZCLUSTER_ORANGE",
	"S_BSZBUSH_RED",
	"S_BSZBUSH_PURPLE",
	"S_BSZBUSH_BLUE",
	"S_BSZBUSH_CYAN",
	"S_BSZBUSH_YELLOW",
	"S_BSZBUSH_ORANGE",
	"S_BSZVINE_RED",
	"S_BSZVINE_PURPLE",
	"S_BSZVINE_BLUE",
	"S_BSZVINE_CYAN",
	"S_BSZVINE_YELLOW",
	"S_BSZVINE_ORANGE",
	"S_BSZSHRUB",
	"S_BSZCLOVER",
	"S_BSZFISH",
	"S_BSZSUNFLOWER",

	"S_DBALL1",
	"S_DBALL2",
	"S_DBALL3",
	"S_DBALL4",
	"S_DBALL5",
	"S_DBALL6",
	"S_EGGSTATUE2",

	// Shield Orb
	"S_ARMA1",
	"S_ARMA2",
	"S_ARMA3",
	"S_ARMA4",
	"S_ARMA5",
	"S_ARMA6",
	"S_ARMA7",
	"S_ARMA8",
	"S_ARMA9",
	"S_ARMA10",
	"S_ARMA11",
	"S_ARMA12",
	"S_ARMA13",
	"S_ARMA14",
	"S_ARMA15",
	"S_ARMA16",

	"S_ARMF1",
	"S_ARMF2",
	"S_ARMF3",
	"S_ARMF4",
	"S_ARMF5",
	"S_ARMF6",
	"S_ARMF7",
	"S_ARMF8",
	"S_ARMF9",
	"S_ARMF10",
	"S_ARMF11",
	"S_ARMF12",
	"S_ARMF13",
	"S_ARMF14",
	"S_ARMF15",
	"S_ARMF16",

	"S_ARMB1",
	"S_ARMB2",
	"S_ARMB3",
	"S_ARMB4",
	"S_ARMB5",
	"S_ARMB6",
	"S_ARMB7",
	"S_ARMB8",
	"S_ARMB9",
	"S_ARMB10",
	"S_ARMB11",
	"S_ARMB12",
	"S_ARMB13",
	"S_ARMB14",
	"S_ARMB15",
	"S_ARMB16",

	"S_WIND1",
	"S_WIND2",
	"S_WIND3",
	"S_WIND4",
	"S_WIND5",
	"S_WIND6",
	"S_WIND7",
	"S_WIND8",

	"S_MAGN1",
	"S_MAGN2",
	"S_MAGN3",
	"S_MAGN4",
	"S_MAGN5",
	"S_MAGN6",
	"S_MAGN7",
	"S_MAGN8",
	"S_MAGN9",
	"S_MAGN10",
	"S_MAGN11",
	"S_MAGN12",

	"S_FORC1",
	"S_FORC2",
	"S_FORC3",
	"S_FORC4",
	"S_FORC5",
	"S_FORC6",
	"S_FORC7",
	"S_FORC8",
	"S_FORC9",
	"S_FORC10",

	"S_FORC11",
	"S_FORC12",
	"S_FORC13",
	"S_FORC14",
	"S_FORC15",
	"S_FORC16",
	"S_FORC17",
	"S_FORC18",
	"S_FORC19",
	"S_FORC20",

	"S_ELEM1",
	"S_ELEM2",
	"S_ELEM3",
	"S_ELEM4",
	"S_ELEM5",
	"S_ELEM6",
	"S_ELEM7",
	"S_ELEM8",
	"S_ELEM9",
	"S_ELEM10",
	"S_ELEM11",
	"S_ELEM12",

	"S_ELEMF1",
	"S_ELEMF2",
	"S_ELEMF3",
	"S_ELEMF4",
	"S_ELEMF5",
	"S_ELEMF6",
	"S_ELEMF7",
	"S_ELEMF8",

	"S_PITY1",
	"S_PITY2",
	"S_PITY3",
	"S_PITY4",
	"S_PITY5",
	"S_PITY6",
	"S_PITY7",
	"S_PITY8",
	"S_PITY9",
	"S_PITY10",

	// Invincibility Sparkles
	"S_IVSP",

	// Super Sonic Spark
	"S_SSPK1",
	"S_SSPK2",
	"S_SSPK3",
	"S_SSPK4",
	"S_SSPK5",

	// Freed Birdie
	"S_BIRD1",
	"S_BIRD2",
	"S_BIRD3",

	// Freed Bunny
	"S_BUNNY1",
	"S_BUNNY2",
	"S_BUNNY3",
	"S_BUNNY4",
	"S_BUNNY5",
	"S_BUNNY6",
	"S_BUNNY7",
	"S_BUNNY8",
	"S_BUNNY9",
	"S_BUNNY10",

	// Freed Mouse
	"S_MOUSE1",
	"S_MOUSE2",

	// Freed Chicken
	"S_CHICKEN1",
	"S_CHICKENHOP",
	"S_CHICKENFLY1",
	"S_CHICKENFLY2",

	// Freed Cow
	"S_COW1",
	"S_COW2",
	"S_COW3",
	"S_COW4",

	// Red Birdie in Bubble
	"S_RBIRD1",
	"S_RBIRD2",
	"S_RBIRD3",

	"S_YELLOWSPRING",
	"S_YELLOWSPRING2",
	"S_YELLOWSPRING3",
	"S_YELLOWSPRING4",
	"S_YELLOWSPRING5",

	"S_REDSPRING",
	"S_REDSPRING2",
	"S_REDSPRING3",
	"S_REDSPRING4",
	"S_REDSPRING5",

	// Blue Springs
	"S_BLUESPRING",
	"S_BLUESPRING2",
	"S_BLUESPRING3",
	"S_BLUESPRING4",
	"S_BLUESPRING5",

	// Yellow Diagonal Spring
	"S_YDIAG1",
	"S_YDIAG2",
	"S_YDIAG3",
	"S_YDIAG4",
	"S_YDIAG5",
	"S_YDIAG6",
	"S_YDIAG7",
	"S_YDIAG8",

	// Red Diagonal Spring
	"S_RDIAG1",
	"S_RDIAG2",
	"S_RDIAG3",
	"S_RDIAG4",
	"S_RDIAG5",
	"S_RDIAG6",
	"S_RDIAG7",
	"S_RDIAG8",

	// Rain
	"S_RAIN1",
	"S_RAINRETURN",

	// Snowflake
	"S_SNOW1",
	"S_SNOW2",
	"S_SNOW3",

	// Water Splish
	"S_SPLISH1",
	"S_SPLISH2",
	"S_SPLISH3",
	"S_SPLISH4",
	"S_SPLISH5",
	"S_SPLISH6",
	"S_SPLISH7",
	"S_SPLISH8",
	"S_SPLISH9",

	// added water splash
	"S_SPLASH1",
	"S_SPLASH2",
	"S_SPLASH3",

	// lava/slime damage burn smoke
	"S_SMOKE1",
	"S_SMOKE2",
	"S_SMOKE3",
	"S_SMOKE4",
	"S_SMOKE5",

	// Bubbles
	"S_SMALLBUBBLE",
	"S_SMALLBUBBLE1",
	"S_MEDIUMBUBBLE",
	"S_MEDIUMBUBBLE1",
	"S_LARGEBUBBLE",
	"S_EXTRALARGEBUBBLE", // breathable

	"S_POP1", // Extra Large bubble goes POP!

	"S_FOG1",
	"S_FOG2",
	"S_FOG3",
	"S_FOG4",
	"S_FOG5",
	"S_FOG6",
	"S_FOG7",
	"S_FOG8",
	"S_FOG9",
	"S_FOG10",
	"S_FOG11",
	"S_FOG12",
	"S_FOG13",
	"S_FOG14",

	"S_SEED",

	"S_PARTICLE",
	"S_PARTICLEGEN",

	// Score Logos
	"S_SCRA", // 100
	"S_SCRB", // 200
	"S_SCRC", // 500
	"S_SCRD", // 1000
	"S_SCRE", // 10000
	"S_SCRF", // 400 (mario)
	"S_SCRG", // 800 (mario)
	"S_SCRH", // 2000 (mario)
	"S_SCRI", // 4000 (mario)
	"S_SCRJ", // 8000 (mario)
	"S_SCRK", // 1UP (mario)

	// Drowning Timer Numbers
	"S_ZERO1",
	"S_ONE1",
	"S_TWO1",
	"S_THREE1",
	"S_FOUR1",
	"S_FIVE1",

	// Tag Sign
	"S_TTAG1",

	// Got Flag Sign
	"S_GOTFLAG1",
	"S_GOTFLAG2",
	"S_GOTFLAG3",
	"S_GOTFLAG4",

	// Red Ring
	"S_RRNG1",
	"S_RRNG2",
	"S_RRNG3",
	"S_RRNG4",
	"S_RRNG5",
	"S_RRNG6",
	"S_RRNG7",

	// Weapon Ring Ammo
	"S_BOUNCERINGAMMO",
	"S_RAILRINGAMMO",
	"S_INFINITYRINGAMMO",
	"S_AUTOMATICRINGAMMO",
	"S_EXPLOSIONRINGAMMO",
	"S_SCATTERRINGAMMO",
	"S_GRENADERINGAMMO",

	// Weapon pickup
	"S_BOUNCEPICKUP",
	"S_BOUNCEPICKUPFADE1",
	"S_BOUNCEPICKUPFADE2",
	"S_BOUNCEPICKUPFADE3",
	"S_BOUNCEPICKUPFADE4",
	"S_BOUNCEPICKUPFADE5",
	"S_BOUNCEPICKUPFADE6",
	"S_BOUNCEPICKUPFADE7",
	"S_BOUNCEPICKUPFADE8",

	"S_RAILPICKUP",
	"S_RAILPICKUPFADE1",
	"S_RAILPICKUPFADE2",
	"S_RAILPICKUPFADE3",
	"S_RAILPICKUPFADE4",
	"S_RAILPICKUPFADE5",
	"S_RAILPICKUPFADE6",
	"S_RAILPICKUPFADE7",
	"S_RAILPICKUPFADE8",

	"S_AUTOPICKUP",
	"S_AUTOPICKUPFADE1",
	"S_AUTOPICKUPFADE2",
	"S_AUTOPICKUPFADE3",
	"S_AUTOPICKUPFADE4",
	"S_AUTOPICKUPFADE5",
	"S_AUTOPICKUPFADE6",
	"S_AUTOPICKUPFADE7",
	"S_AUTOPICKUPFADE8",

	"S_EXPLODEPICKUP",
	"S_EXPLODEPICKUPFADE1",
	"S_EXPLODEPICKUPFADE2",
	"S_EXPLODEPICKUPFADE3",
	"S_EXPLODEPICKUPFADE4",
	"S_EXPLODEPICKUPFADE5",
	"S_EXPLODEPICKUPFADE6",
	"S_EXPLODEPICKUPFADE7",
	"S_EXPLODEPICKUPFADE8",

	"S_SCATTERPICKUP",
	"S_SCATTERPICKUPFADE1",
	"S_SCATTERPICKUPFADE2",
	"S_SCATTERPICKUPFADE3",
	"S_SCATTERPICKUPFADE4",
	"S_SCATTERPICKUPFADE5",
	"S_SCATTERPICKUPFADE6",
	"S_SCATTERPICKUPFADE7",
	"S_SCATTERPICKUPFADE8",

	"S_GRENADEPICKUP",
	"S_GRENADEPICKUPFADE1",
	"S_GRENADEPICKUPFADE2",
	"S_GRENADEPICKUPFADE3",
	"S_GRENADEPICKUPFADE4",
	"S_GRENADEPICKUPFADE5",
	"S_GRENADEPICKUPFADE6",
	"S_GRENADEPICKUPFADE7",
	"S_GRENADEPICKUPFADE8",

	// Thrown Weapon Rings
	"S_THROWNBOUNCE1",
	"S_THROWNBOUNCE2",
	"S_THROWNBOUNCE3",
	"S_THROWNBOUNCE4",
	"S_THROWNBOUNCE5",
	"S_THROWNBOUNCE6",
	"S_THROWNBOUNCE7",
	"S_THROWNINFINITY1",
	"S_THROWNINFINITY2",
	"S_THROWNINFINITY3",
	"S_THROWNINFINITY4",
	"S_THROWNINFINITY5",
	"S_THROWNINFINITY6",
	"S_THROWNINFINITY7",
	"S_THROWNAUTOMATIC1",
	"S_THROWNAUTOMATIC2",
	"S_THROWNAUTOMATIC3",
	"S_THROWNAUTOMATIC4",
	"S_THROWNAUTOMATIC5",
	"S_THROWNAUTOMATIC6",
	"S_THROWNAUTOMATIC7",
	"S_THROWNEXPLOSION1",
	"S_THROWNEXPLOSION2",
	"S_THROWNEXPLOSION3",
	"S_THROWNEXPLOSION4",
	"S_THROWNEXPLOSION5",
	"S_THROWNEXPLOSION6",
	"S_THROWNEXPLOSION7",
	"S_THROWNGRENADE1",
	"S_THROWNGRENADE2",
	"S_THROWNGRENADE3",
	"S_THROWNGRENADE4",
	"S_THROWNGRENADE5",
	"S_THROWNGRENADE6",
	"S_THROWNGRENADE7",
	"S_THROWNGRENADE8",
	"S_THROWNGRENADE9",
	"S_THROWNGRENADE10",
	"S_THROWNGRENADE11",
	"S_THROWNGRENADE12",
	"S_THROWNGRENADE13",
	"S_THROWNGRENADE14",
	"S_THROWNGRENADE15",
	"S_THROWNGRENADE16",
	"S_THROWNGRENADE17",
	"S_THROWNGRENADE18",
	"S_THROWNSCATTER",

	"S_RINGEXPLODE",

	"S_COIN1",
	"S_COIN2",
	"S_COIN3",
	"S_COINSPARKLE1",
	"S_COINSPARKLE2",
	"S_COINSPARKLE3",
	"S_COINSPARKLE4",
	"S_GOOMBA1",
	"S_GOOMBA1B",
	"S_GOOMBA2",
	"S_GOOMBA3",
	"S_GOOMBA4",
	"S_GOOMBA5",
	"S_GOOMBA6",
	"S_GOOMBA7",
	"S_GOOMBA8",
	"S_GOOMBA9",
	"S_GOOMBA_DEAD",
	"S_BLUEGOOMBA1",
	"S_BLUEGOOMBA1B",
	"S_BLUEGOOMBA2",
	"S_BLUEGOOMBA3",
	"S_BLUEGOOMBA4",
	"S_BLUEGOOMBA5",
	"S_BLUEGOOMBA6",
	"S_BLUEGOOMBA7",
	"S_BLUEGOOMBA8",
	"S_BLUEGOOMBA9",
	"S_BLUEGOOMBA_DEAD",

	// Mario-specific stuff
	"S_FIREFLOWER1",
	"S_FIREFLOWER2",
	"S_FIREFLOWER3",
	"S_FIREFLOWER4",
	"S_FIREBALL1",
	"S_FIREBALL2",
	"S_FIREBALL3",
	"S_FIREBALL4",
	"S_FIREBALLEXP1",
	"S_FIREBALLEXP2",
	"S_FIREBALLEXP3",
	"S_SHELL",
	"S_SHELL1",
	"S_SHELL2",
	"S_SHELL3",
	"S_SHELL4",
	"S_PUMA1",
	"S_PUMA2",
	"S_PUMA3",
	"S_PUMA4",
	"S_PUMA5",
	"S_PUMA6",
	"S_HAMMER1",
	"S_HAMMER2",
	"S_HAMMER3",
	"S_HAMMER4",
	"S_KOOPA1",
	"S_KOOPA2",
	"S_KOOPAFLAME1",
	"S_KOOPAFLAME2",
	"S_KOOPAFLAME3",
	"S_AXE1",
	"S_AXE2",
	"S_AXE3",
	"S_MARIOBUSH1",
	"S_MARIOBUSH2",
	"S_TOAD",

	// Nights-specific stuff
	"S_NIGHTSDRONE1",
	"S_NIGHTSDRONE2",
	"S_NIGHTSDRONE_SPARKLING1",
	"S_NIGHTSDRONE_SPARKLING2",
	"S_NIGHTSDRONE_SPARKLING3",
	"S_NIGHTSDRONE_SPARKLING4",
	"S_NIGHTSDRONE_SPARKLING5",
	"S_NIGHTSDRONE_SPARKLING6",
	"S_NIGHTSDRONE_SPARKLING7",
	"S_NIGHTSDRONE_SPARKLING8",
	"S_NIGHTSDRONE_SPARKLING9",
	"S_NIGHTSDRONE_SPARKLING10",
	"S_NIGHTSDRONE_SPARKLING11",
	"S_NIGHTSDRONE_SPARKLING12",
	"S_NIGHTSDRONE_SPARKLING13",
	"S_NIGHTSDRONE_SPARKLING14",
	"S_NIGHTSDRONE_SPARKLING15",
	"S_NIGHTSDRONE_SPARKLING16",
	"S_NIGHTSGOAL1",
	"S_NIGHTSGOAL2",
	"S_NIGHTSGOAL3",
	"S_NIGHTSGOAL4",

	"S_NIGHTSFLY1A",
	"S_NIGHTSFLY1B",
	"S_NIGHTSDRILL1A",
	"S_NIGHTSDRILL1B",
	"S_NIGHTSDRILL1C",
	"S_NIGHTSDRILL1D",
	"S_NIGHTSFLY2A",
	"S_NIGHTSFLY2B",
	"S_NIGHTSDRILL2A",
	"S_NIGHTSDRILL2B",
	"S_NIGHTSDRILL2C",
	"S_NIGHTSDRILL2D",
	"S_NIGHTSFLY3A",
	"S_NIGHTSFLY3B",
	"S_NIGHTSDRILL3A",
	"S_NIGHTSDRILL3B",
	"S_NIGHTSDRILL3C",
	"S_NIGHTSDRILL3D",
	"S_NIGHTSFLY4A",
	"S_NIGHTSFLY4B",
	"S_NIGHTSDRILL4A",
	"S_NIGHTSDRILL4B",
	"S_NIGHTSDRILL4C",
	"S_NIGHTSDRILL4D",
	"S_NIGHTSFLY5A",
	"S_NIGHTSFLY5B",
	"S_NIGHTSDRILL5A",
	"S_NIGHTSDRILL5B",
	"S_NIGHTSDRILL5C",
	"S_NIGHTSDRILL5D",
	"S_NIGHTSFLY6A",
	"S_NIGHTSFLY6B",
	"S_NIGHTSDRILL6A",
	"S_NIGHTSDRILL6B",
	"S_NIGHTSDRILL6C",
	"S_NIGHTSDRILL6D",
	"S_NIGHTSFLY7A",
	"S_NIGHTSFLY7B",
	"S_NIGHTSDRILL7A",
	"S_NIGHTSDRILL7B",
	"S_NIGHTSDRILL7C",
	"S_NIGHTSDRILL7D",
	"S_NIGHTSFLY8A",
	"S_NIGHTSFLY8B",
	"S_NIGHTSDRILL8A",
	"S_NIGHTSDRILL8B",
	"S_NIGHTSDRILL8C",
	"S_NIGHTSDRILL8D",
	"S_NIGHTSFLY9A",
	"S_NIGHTSFLY9B",
	"S_NIGHTSDRILL9A",
	"S_NIGHTSDRILL9B",
	"S_NIGHTSDRILL9C",
	"S_NIGHTSDRILL9D",
	"S_NIGHTSHURT1",
	"S_NIGHTSHURT2",
	"S_NIGHTSHURT3",
	"S_NIGHTSHURT4",
	"S_NIGHTSHURT5",
	"S_NIGHTSHURT6",
	"S_NIGHTSHURT7",
	"S_NIGHTSHURT8",
	"S_NIGHTSHURT9",
	"S_NIGHTSHURT10",
	"S_NIGHTSHURT11",
	"S_NIGHTSHURT12",
	"S_NIGHTSHURT13",
	"S_NIGHTSHURT14",
	"S_NIGHTSHURT15",
	"S_NIGHTSHURT16",
	"S_NIGHTSHURT17",
	"S_NIGHTSHURT18",
	"S_NIGHTSHURT19",
	"S_NIGHTSHURT20",
	"S_NIGHTSHURT21",
	"S_NIGHTSHURT22",
	"S_NIGHTSHURT23",
	"S_NIGHTSHURT24",
	"S_NIGHTSHURT25",
	"S_NIGHTSHURT26",
	"S_NIGHTSHURT27",
	"S_NIGHTSHURT28",
	"S_NIGHTSHURT29",
	"S_NIGHTSHURT30",
	"S_NIGHTSHURT31",
	"S_NIGHTSHURT32",

	"S_NIGHTSPARKLE1",
	"S_NIGHTSPARKLE2",
	"S_NIGHTSPARKLE3",
	"S_NIGHTSPARKLE4",
	"S_NIGHTSPARKLESUPER1",
	"S_NIGHTSPARKLESUPER2",
	"S_NIGHTSPARKLESUPER3",
	"S_NIGHTSPARKLESUPER4",
	"S_NIGHTSLOOPHELPER",

	// NiGHTS bumper
	"S_NIGHTSBUMPER1",
	"S_NIGHTSBUMPER2",
	"S_NIGHTSBUMPER3",
	"S_NIGHTSBUMPER4",
	"S_NIGHTSBUMPER5",
	"S_NIGHTSBUMPER6",
	"S_NIGHTSBUMPER7",
	"S_NIGHTSBUMPER8",
	"S_NIGHTSBUMPER9",
	"S_NIGHTSBUMPER10",
	"S_NIGHTSBUMPER11",
	"S_NIGHTSBUMPER12",

	"S_HOOP",
	"S_HOOP_XMASA",
	"S_HOOP_XMASB",

	"S_NIGHTSCORE10",
	"S_NIGHTSCORE20",
	"S_NIGHTSCORE30",
	"S_NIGHTSCORE40",
	"S_NIGHTSCORE50",
	"S_NIGHTSCORE60",
	"S_NIGHTSCORE70",
	"S_NIGHTSCORE80",
	"S_NIGHTSCORE90",
	"S_NIGHTSCORE100",
	"S_NIGHTSCORE10_2",
	"S_NIGHTSCORE20_2",
	"S_NIGHTSCORE30_2",
	"S_NIGHTSCORE40_2",
	"S_NIGHTSCORE50_2",
	"S_NIGHTSCORE60_2",
	"S_NIGHTSCORE70_2",
	"S_NIGHTSCORE80_2",
	"S_NIGHTSCORE90_2",
	"S_NIGHTSCORE100_2",

	"S_NIGHTSWING",
	"S_NIGHTSWING_XMAS",

	// NiGHTS Paraloop Powerups
	"S_NIGHTSPOWERUP1",
	"S_NIGHTSPOWERUP2",
	"S_NIGHTSPOWERUP3",
	"S_NIGHTSPOWERUP4",
	"S_NIGHTSPOWERUP5",
	"S_NIGHTSPOWERUP6",
	"S_NIGHTSPOWERUP7",
	"S_NIGHTSPOWERUP8",
	"S_NIGHTSPOWERUP9",
	"S_NIGHTSPOWERUP10",
	"S_EGGCAPSULE",

	// Orbiting Chaos Emeralds
	"S_ORBITEM1",
	"S_ORBITEM2",
	"S_ORBITEM3",
	"S_ORBITEM4",
	"S_ORBITEM5",
	"S_ORBITEM6",
	"S_ORBITEM7",
	"S_ORBITEM8",
	"S_ORBITEM9",
	"S_ORBITEM10",
	"S_ORBITEM11",
	"S_ORBITEM12",
	"S_ORBITEM13",
	"S_ORBITEM14",
	"S_ORBITEM15",
	"S_ORBITEM16",

	// "Flicky" helper
	"S_NIGHTOPIANHELPER1",
	"S_NIGHTOPIANHELPER2",
	"S_NIGHTOPIANHELPER3",
	"S_NIGHTOPIANHELPER4",
	"S_NIGHTOPIANHELPER5",
	"S_NIGHTOPIANHELPER6",
	"S_NIGHTOPIANHELPER7",
	"S_NIGHTOPIANHELPER8",

	"S_CRUMBLE1",
	"S_CRUMBLE2",

	"S_SUPERTRANS1",
	"S_SUPERTRANS2",
	"S_SUPERTRANS3",
	"S_SUPERTRANS4",
	"S_SUPERTRANS5",
	"S_SUPERTRANS6",
	"S_SUPERTRANS7",
	"S_SUPERTRANS8",
	"S_SUPERTRANS9",

	// Spark
	"S_SPRK1",
	"S_SPRK2",
	"S_SPRK3",
	"S_SPRK4",
	"S_SPRK5",
	"S_SPRK6",
	"S_SPRK7",
	"S_SPRK8",
	"S_SPRK9",
	"S_SPRK10",
	"S_SPRK11",
	"S_SPRK12",
	"S_SPRK13",
	"S_SPRK14",
	"S_SPRK15",
	"S_SPRK16",

	// Robot Explosion
	"S_XPLD1",
	"S_XPLD2",
	"S_XPLD3",
	"S_XPLD4",

	// Underwater Explosion
	"S_WPLD1",
	"S_WPLD2",
	"S_WPLD3",
	"S_WPLD4",
	"S_WPLD5",
	"S_WPLD6",

	"S_ROCKSPAWN",

	"S_ROCKCRUMBLEA",
	"S_ROCKCRUMBLEB",
	"S_ROCKCRUMBLEC",
	"S_ROCKCRUMBLED",
	"S_ROCKCRUMBLEE",
	"S_ROCKCRUMBLEF",
	"S_ROCKCRUMBLEG",
	"S_ROCKCRUMBLEH",
	"S_ROCKCRUMBLEI",
	"S_ROCKCRUMBLEJ",
	"S_ROCKCRUMBLEK",
	"S_ROCKCRUMBLEL",
	"S_ROCKCRUMBLEM",
	"S_ROCKCRUMBLEN",
	"S_ROCKCRUMBLEO",
	"S_ROCKCRUMBLEP",

	"S_SRB1_CRAWLA1",
	"S_SRB1_CRAWLA2",
	"S_SRB1_CRAWLA3",
	"S_SRB1_CRAWLA4",

	"S_SRB1_BAT1",
	"S_SRB1_BAT2",
	"S_SRB1_BAT3",
	"S_SRB1_BAT4",

	"S_SRB1_ROBOFISH1",
	"S_SRB1_ROBOFISH2",
	"S_SRB1_ROBOFISH3",

	"S_SRB1_VOLCANOGUY1",
	"S_SRB1_VOLCANOGUY2",

	"S_SRB1_HOPPY1",
	"S_SRB1_HOPPY2",

	"S_SRB1_HOPPYWATER1",
	"S_SRB1_HOPPYWATER2",

	"S_SRB1_HOPPYSKYLAB1",

	"S_SRB1_MMZFLYING1",
	"S_SRB1_MMZFLYING2",
	"S_SRB1_MMZFLYING3",
	"S_SRB1_MMZFLYING4",
	"S_SRB1_MMZFLYING5",

	"S_SRB1_UFO1",
	"S_SRB1_UFO2",

	"S_SRB1_GRAYBOT1",
	"S_SRB1_GRAYBOT2",
	"S_SRB1_GRAYBOT3",
	"S_SRB1_GRAYBOT4",
	"S_SRB1_GRAYBOT5",
	"S_SRB1_GRAYBOT6",

	"S_SRB1_ROBOTOPOLIS1",
	"S_SRB1_ROBOTOPOLIS2",

	"S_SRB1_RBZBUZZ1",
	"S_SRB1_RBZBUZZ2",

	"S_SRB1_RBZSPIKES1",
	"S_SRB1_RBZSPIKES2",

	"S_SRB1_METALSONIC1",
	"S_SRB1_METALSONIC2",
	"S_SRB1_METALSONIC3",

	"S_SRB1_GOLDBOT1",
	"S_SRB1_GOLDBOT2",
	"S_SRB1_GOLDBOT3",
	"S_SRB1_GOLDBOT4",
	"S_SRB1_GOLDBOT5",
	"S_SRB1_GOLDBOT6",

	"S_SRB1_GENREX1",
	"S_SRB1_GENREX2",

	// Gray Springs
	"S_GRAYSPRING",
	"S_GRAYSPRING2",
	"S_GRAYSPRING3",
	"S_GRAYSPRING4",
	"S_GRAYSPRING5",

	// Invis-spring - this is used just for the sproing sound.
	"S_INVISSPRING",

	// Blue Diagonal Spring
	"S_BDIAG1",
	"S_BDIAG2",
	"S_BDIAG3",
	"S_BDIAG4",
	"S_BDIAG5",
	"S_BDIAG6",
	"S_BDIAG7",
	"S_BDIAG8",

	//{ Random Item Box
	"S_RANDOMITEM1",
	"S_RANDOMITEM2",
	"S_RANDOMITEM3",
	"S_RANDOMITEM4",
	"S_RANDOMITEM5",
	"S_RANDOMITEM6",
	"S_RANDOMITEM7",
	"S_RANDOMITEM8",
	"S_RANDOMITEM9",
	"S_RANDOMITEM10",
	"S_RANDOMITEM11",
	"S_RANDOMITEM12",
	"S_RANDOMITEM13",
	"S_RANDOMITEM14",
	"S_RANDOMITEM15",
	"S_RANDOMITEM16",
	"S_RANDOMITEM17",
	"S_RANDOMITEM18",
	"S_RANDOMITEM19",
	"S_RANDOMITEM20",
	"S_RANDOMITEM21",
	"S_RANDOMITEM22",
	"S_RANDOMITEM23",
	"S_RANDOMITEM24",
	"S_DEADRANDOMITEM",

	// Random Item Pop
	"S_RANDOMITEMPOP1",
	"S_RANDOMITEMPOP2",
	"S_RANDOMITEMPOP3",
	"S_RANDOMITEMPOP4",
	//}

	"S_ITEMICON",

	// Signpost sparkles
	"S_SIGNSPARK1",
	"S_SIGNSPARK2",
	"S_SIGNSPARK3",
	"S_SIGNSPARK4",
	"S_SIGNSPARK5",
	"S_SIGNSPARK6",
	"S_SIGNSPARK7",
	"S_SIGNSPARK8",
	"S_SIGNSPARK9",
	"S_SIGNSPARK10",
	"S_SIGNSPARK11",

	// Drift Sparks
	"S_DRIFTSPARK_A1",
	"S_DRIFTSPARK_A2",
	"S_DRIFTSPARK_A3",
	"S_DRIFTSPARK_B1",
	"S_DRIFTSPARK_C1",
	"S_DRIFTSPARK_C2",

	// Brake drift sparks
	"S_BRAKEDRIFT",

	// Drift Smoke
	"S_DRIFTDUST1",
	"S_DRIFTDUST2",
	"S_DRIFTDUST3",
	"S_DRIFTDUST4",

	// Fast lines
	"S_FASTLINE1",
	"S_FASTLINE2",
	"S_FASTLINE3",
	"S_FASTLINE4",
	"S_FASTLINE5",

	// Fast dust release
	"S_FASTDUST1",
	"S_FASTDUST2",
	"S_FASTDUST3",
	"S_FASTDUST4",
	"S_FASTDUST5",
	"S_FASTDUST6",
	"S_FASTDUST7",

	// Thunder Shield Burst

	// Sneaker boost effect
	"S_BOOSTFLAME",
	"S_BOOSTSMOKESPAWNER",
	"S_BOOSTSMOKE1",
	"S_BOOSTSMOKE2",
	"S_BOOSTSMOKE3",
	"S_BOOSTSMOKE4",
	"S_BOOSTSMOKE5",
	"S_BOOSTSMOKE6",

	// Sneaker Fire Trail
	"S_KARTFIRE1",
	"S_KARTFIRE2",
	"S_KARTFIRE3",
	"S_KARTFIRE4",
	"S_KARTFIRE5",
	"S_KARTFIRE6",
	"S_KARTFIRE7",
	"S_KARTFIRE8",

	// Angel Island Drift Strat Dust (what a mouthful!)
	"S_KARTAIZDRIFTSTRAT",

	// Invincibility Sparks
	"S_KARTINVULN_SMALL1",
	"S_KARTINVULN_SMALL2",
	"S_KARTINVULN_SMALL3",
	"S_KARTINVULN_SMALL4",
	"S_KARTINVULN_SMALL5",

	"S_KARTINVULN_LARGE1",
	"S_KARTINVULN_LARGE2",
	"S_KARTINVULN_LARGE3",
	"S_KARTINVULN_LARGE4",
	"S_KARTINVULN_LARGE5",

	// Invincibility flash overlay
	"S_INVULNFLASH1",
	"S_INVULNFLASH2",
	"S_INVULNFLASH3",
	"S_INVULNFLASH4",

	// Wipeout dust trail
	"S_WIPEOUTTRAIL1",
	"S_WIPEOUTTRAIL2",
	"S_WIPEOUTTRAIL3",
	"S_WIPEOUTTRAIL4",
	"S_WIPEOUTTRAIL5",

	// Rocket sneaker
	"S_ROCKETSNEAKER_L",
	"S_ROCKETSNEAKER_R",
	"S_ROCKETSNEAKER_LVIBRATE",
	"S_ROCKETSNEAKER_RVIBRATE",

	//{ Eggman Monitor
	"S_EGGMANITEM1",
	"S_EGGMANITEM2",
	"S_EGGMANITEM3",
	"S_EGGMANITEM4",
	"S_EGGMANITEM5",
	"S_EGGMANITEM6",
	"S_EGGMANITEM7",
	"S_EGGMANITEM8",
	"S_EGGMANITEM9",
	"S_EGGMANITEM10",
	"S_EGGMANITEM11",
	"S_EGGMANITEM12",
	"S_EGGMANITEM13",
	"S_EGGMANITEM14",
	"S_EGGMANITEM15",
	"S_EGGMANITEM16",
	"S_EGGMANITEM17",
	"S_EGGMANITEM18",
	"S_EGGMANITEM19",
	"S_EGGMANITEM20",
	"S_EGGMANITEM21",
	"S_EGGMANITEM22",
	"S_EGGMANITEM23",
	"S_EGGMANITEM24",
	"S_EGGMANITEM_DEAD",
	//}

	// Banana
	"S_BANANA",
	"S_BANANA_DEAD",

	//{ Orbinaut
	"S_ORBINAUT1",
	"S_ORBINAUT2",
	"S_ORBINAUT3",
	"S_ORBINAUT4",
	"S_ORBINAUT5",
	"S_ORBINAUT6",
	"S_ORBINAUT_DEAD",
	"S_ORBINAUT_SHIELD1",
	"S_ORBINAUT_SHIELD2",
	"S_ORBINAUT_SHIELD3",
	"S_ORBINAUT_SHIELD4",
	"S_ORBINAUT_SHIELD5",
	"S_ORBINAUT_SHIELD6",
	"S_ORBINAUT_SHIELDDEAD",
	//}
	//{ Jawz
	"S_JAWZ1",
	"S_JAWZ2",
	"S_JAWZ3",
	"S_JAWZ4",
	"S_JAWZ5",
	"S_JAWZ6",
	"S_JAWZ7",
	"S_JAWZ8",
	"S_JAWZ_DUD1",
	"S_JAWZ_DUD2",
	"S_JAWZ_DUD3",
	"S_JAWZ_DUD4",
	"S_JAWZ_DUD5",
	"S_JAWZ_DUD6",
	"S_JAWZ_DUD7",
	"S_JAWZ_DUD8",
	"S_JAWZ_SHIELD1",
	"S_JAWZ_SHIELD2",
	"S_JAWZ_SHIELD3",
	"S_JAWZ_SHIELD4",
	"S_JAWZ_SHIELD5",
	"S_JAWZ_SHIELD6",
	"S_JAWZ_SHIELD7",
	"S_JAWZ_SHIELD8",
	"S_JAWZ_DEAD1",
	"S_JAWZ_DEAD2",
	//}

	"S_PLAYERRETICULE", // Player reticule

	// Special Stage Mine
	"S_SSMINE1",
	"S_SSMINE2",
	"S_SSMINE3",
	"S_SSMINE4",
	"S_SSMINE_SHIELD1",
	"S_SSMINE_SHIELD2",
	"S_SSMINE_AIR1",
	"S_SSMINE_AIR2",
	"S_SSMINE_DEPLOY1",
	"S_SSMINE_DEPLOY2",
	"S_SSMINE_DEPLOY3",
	"S_SSMINE_DEPLOY4",
	"S_SSMINE_DEPLOY5",
	"S_SSMINE_DEPLOY6",
	"S_SSMINE_DEPLOY7",
	"S_SSMINE_DEPLOY8",
	"S_SSMINE_DEPLOY9",
	"S_SSMINE_DEPLOY10",
	"S_SSMINE_DEPLOY11",
	"S_SSMINE_DEPLOY12",
	"S_SSMINE_DEPLOY13",
	"S_SSMINE_EXPLODE",
	"S_MINEEXPLOSION1",
	"S_MINEEXPLOSION2",

	// New explosion
	"S_QUICKBOOM1",
	"S_QUICKBOOM2",
	"S_QUICKBOOM3",
	"S_QUICKBOOM4",
	"S_QUICKBOOM5",
	"S_QUICKBOOM6",
	"S_QUICKBOOM7",
	"S_QUICKBOOM8",
	"S_QUICKBOOM9",
	"S_QUICKBOOM10",

	"S_SLOWBOOM1",
	"S_SLOWBOOM2",
	"S_SLOWBOOM3",
	"S_SLOWBOOM4",
	"S_SLOWBOOM5",
	"S_SLOWBOOM6",
	"S_SLOWBOOM7",
	"S_SLOWBOOM8",
	"S_SLOWBOOM9",
	"S_SLOWBOOM10",

	// Ballhog
	"S_BALLHOG1",
	"S_BALLHOG2",
	"S_BALLHOG3",
	"S_BALLHOG4",
	"S_BALLHOG5",
	"S_BALLHOG6",
	"S_BALLHOG7",
	"S_BALLHOG8",
	"S_BALLHOG_DEAD",
	"S_BALLHOGBOOM1",
	"S_BALLHOGBOOM2",
	"S_BALLHOGBOOM3",
	"S_BALLHOGBOOM4",
	"S_BALLHOGBOOM5",
	"S_BALLHOGBOOM6",
	"S_BALLHOGBOOM7",
	"S_BALLHOGBOOM8",
	"S_BALLHOGBOOM9",
	"S_BALLHOGBOOM10",
	"S_BALLHOGBOOM11",
	"S_BALLHOGBOOM12",
	"S_BALLHOGBOOM13",
	"S_BALLHOGBOOM14",
	"S_BALLHOGBOOM15",
	"S_BALLHOGBOOM16",

	// Self-Propelled Bomb - just an explosion for now...
	"S_SPB1",
	"S_SPB2",
	"S_SPB3",
	"S_SPB4",
	"S_SPB5",
	"S_SPB6",
	"S_SPB7",
	"S_SPB8",
	"S_SPB9",
	"S_SPB10",
	"S_SPB11",
	"S_SPB12",
	"S_SPB13",
	"S_SPB14",
	"S_SPB15",
	"S_SPB16",
	"S_SPB17",
	"S_SPB18",
	"S_SPB19",
	"S_SPB20",
	"S_SPB_DEAD",

	// Thunder Shield
	"S_THUNDERSHIELD1",
	"S_THUNDERSHIELD2",
	"S_THUNDERSHIELD3",
	"S_THUNDERSHIELD4",
	"S_THUNDERSHIELD5",
	"S_THUNDERSHIELD6",
	"S_THUNDERSHIELD7",
	"S_THUNDERSHIELD8",
	"S_THUNDERSHIELD9",
	"S_THUNDERSHIELD10",
	"S_THUNDERSHIELD11",
	"S_THUNDERSHIELD12",
	"S_THUNDERSHIELD13",
	"S_THUNDERSHIELD14",
	"S_THUNDERSHIELD15",
	"S_THUNDERSHIELD16",
	"S_THUNDERSHIELD17",
	"S_THUNDERSHIELD18",
	"S_THUNDERSHIELD19",
	"S_THUNDERSHIELD20",
	"S_THUNDERSHIELD21",
	"S_THUNDERSHIELD22",
	"S_THUNDERSHIELD23",
	"S_THUNDERSHIELD24",
	
	// Bubble Shield
	"S_BUBBLESHIELD1",
	"S_BUBBLESHIELD2",
	"S_BUBBLESHIELD3",
	"S_BUBBLESHIELD4",
	"S_BUBBLESHIELD5",
	"S_BUBBLESHIELD6",
	"S_BUBBLESHIELD7",
	"S_BUBBLESHIELD8",
	"S_BUBBLESHIELD9",
	"S_BUBBLESHIELD10",
	"S_BUBBLESHIELD11",
	"S_BUBBLESHIELD12",
	"S_BUBBLESHIELD13",
	"S_BUBBLESHIELD14",
	"S_BUBBLESHIELD15",
	"S_BUBBLESHIELD16",
	"S_BUBBLESHIELD17",
	"S_BUBBLESHIELD18",
	"S_BUBBLESHIELDBLOWUP",
	"S_BUBBLESHIELDTRAP1",
	"S_BUBBLESHIELDTRAP2",
	"S_BUBBLESHIELDTRAP3",
	"S_BUBBLESHIELDTRAP4",
	"S_BUBBLESHIELDTRAP5",
	"S_BUBBLESHIELDTRAP6",
	"S_BUBBLESHIELDTRAP7",
	"S_BUBBLESHIELDTRAP8",
	"S_BUBBLESHIELDWAVE1",
	"S_BUBBLESHIELDWAVE2",
	"S_BUBBLESHIELDWAVE3",
	"S_BUBBLESHIELDWAVE4",
	"S_BUBBLESHIELDWAVE5",
	"S_BUBBLESHIELDWAVE6",
	
	// Flame Shield
	"S_FLAMESHIELD1",
	"S_FLAMESHIELD2",
	"S_FLAMESHIELD3",
	"S_FLAMESHIELD4",
	"S_FLAMESHIELD5",
	"S_FLAMESHIELD6",
	"S_FLAMESHIELD7",
	"S_FLAMESHIELD8",
	"S_FLAMESHIELD9",
	"S_FLAMESHIELD10",
	"S_FLAMESHIELD11",
	"S_FLAMESHIELD12",
	"S_FLAMESHIELD13",
	"S_FLAMESHIELD14",
	"S_FLAMESHIELD15",
	"S_FLAMESHIELD16",
	"S_FLAMESHIELD17",
	"S_FLAMESHIELD18",
	"S_FLAMESHIELDDASH",	

	// The legend
	"S_SINK",
	"S_SINK_SHIELD",
	"S_SINKTRAIL1",
	"S_SINKTRAIL2",
	"S_SINKTRAIL3",

	// Battle Mode bumper
	"S_BATTLEBUMPER1",
	"S_BATTLEBUMPER2",
	"S_BATTLEBUMPER3",

	// DEZ respawn laser
	"S_DEZLASER",

	// Audience Members
	"S_RANDOMAUDIENCE",
	"S_AUDIENCE_CHAO_CHEER1",
	"S_AUDIENCE_CHAO_CHEER2",
	"S_AUDIENCE_CHAO_WIN1",
	"S_AUDIENCE_CHAO_WIN2",
	"S_AUDIENCE_CHAO_LOSE",

	// 1.0 Kart Decoratives
	"S_FLAYM1",
	"S_FLAYM2",
	"S_FLAYM3",
	"S_FLAYM4",
	"S_DEVIL",
	"S_ANGEL",
	"S_PALMTREE",
	"S_FLAG",
	"S_HEDGEHOG", // (Rimshot)
	"S_BUSH1",
	"S_TWEE",
	"S_HYDRANT",

	// New Misc Decorations
	"S_BIGPUMA1",
	"S_BIGPUMA2",
	"S_BIGPUMA3",
	"S_BIGPUMA4",
	"S_BIGPUMA5",
	"S_BIGPUMA6",
	"S_APPLE1",
	"S_APPLE2",
	"S_APPLE3",
	"S_APPLE4",
	"S_APPLE5",
	"S_APPLE6",
	"S_APPLE7",
	"S_APPLE8",

	// D00Dkart - Fall Flowers
	"S_DOOD_FLOWER1",
	"S_DOOD_FLOWER2",
	"S_DOOD_FLOWER3",
	"S_DOOD_FLOWER4",
	"S_DOOD_FLOWER5",
	"S_DOOD_FLOWER6",

	// D00Dkart - Super Circuit Box
	"S_DOOD_BOX1",
	"S_DOOD_BOX2",
	"S_DOOD_BOX3",
	"S_DOOD_BOX4",
	"S_DOOD_BOX5",

	// D00Dkart - Diddy Kong Racing Bumper
	"S_DOOD_BALLOON",

	// Chaotix Big Ring
	"S_BIGRING01",
	"S_BIGRING02",
	"S_BIGRING03",
	"S_BIGRING04",
	"S_BIGRING05",
	"S_BIGRING06",
	"S_BIGRING07",
	"S_BIGRING08",
	"S_BIGRING09",
	"S_BIGRING10",
	"S_BIGRING11",
	"S_BIGRING12",

	// SNES Objects
	"S_SNES_DONUTBUSH1",
	"S_SNES_DONUTBUSH2",
	"S_SNES_DONUTBUSH3",

	// GBA Objects
	"S_GBA_BOO1",
	"S_GBA_BOO2",
	"S_GBA_BOO3",
	"S_GBA_BOO4",

	// Sapphire Coast Mobs
	"S_BUZZBOMBER_LOOK1",
	"S_BUZZBOMBER_LOOK2",
	"S_BUZZBOMBER_FLY1",
	"S_BUZZBOMBER_FLY2",
	"S_BUZZBOMBER_FLY3",
	"S_BUZZBOMBER_FLY4",

	"S_CHOMPER_SPAWN",
	"S_CHOMPER_HOP1",
	"S_CHOMPER_HOP2",
	"S_CHOMPER_TURNAROUND",

	"S_PALMTREE2",
	"S_PURPLEFLOWER1",
	"S_PURPLEFLOWER2",
	"S_YELLOWFLOWER1",
	"S_YELLOWFLOWER2",
	"S_PLANT2",
	"S_PLANT3",
	"S_PLANT4",

	// Crystal Abyss Mobs
	"S_SKULL",
	"S_PHANTREE",
	"S_FLYINGGARG1",
	"S_FLYINGGARG2",
	"S_FLYINGGARG3",
	"S_FLYINGGARG4",
	"S_FLYINGGARG5",
	"S_FLYINGGARG6",
	"S_FLYINGGARG7",
	"S_FLYINGGARG8",
	"S_LAMPPOST",
	"S_MOSSYTREE",

	"S_SHADOW",
	"S_WHITESHADOW",

	"S_BUMP1",
	"S_BUMP2",
	"S_BUMP3",

	"S_FLINGENERGY1",
	"S_FLINGENERGY2",
	"S_FLINGENERGY3",

	"S_CLASH1",
	"S_CLASH2",
	"S_CLASH3",
	"S_CLASH4",
	"S_CLASH5",
	"S_CLASH6",

	"S_FIREDITEM1",
	"S_FIREDITEM2",
	"S_FIREDITEM3",
	"S_FIREDITEM4",

	"S_INSTASHIELDA1", // No damage instashield effect
	"S_INSTASHIELDA2",
	"S_INSTASHIELDA3",
	"S_INSTASHIELDA4",
	"S_INSTASHIELDA5",
	"S_INSTASHIELDA6",
	"S_INSTASHIELDA7",
	"S_INSTASHIELDB1",
	"S_INSTASHIELDB2",
	"S_INSTASHIELDB3",
	"S_INSTASHIELDB4",
	"S_INSTASHIELDB5",
	"S_INSTASHIELDB6",
	"S_INSTASHIELDB7",

	"S_PLAYERARROW", // Above player arrow
	"S_PLAYERARROW_BOX",
	"S_PLAYERARROW_ITEM",
	"S_PLAYERARROW_NUMBER",
	"S_PLAYERARROW_X",
	"S_PLAYERARROW_WANTED1",
	"S_PLAYERARROW_WANTED2",
	"S_PLAYERARROW_WANTED3",
	"S_PLAYERARROW_WANTED4",
	"S_PLAYERARROW_WANTED5",
	"S_PLAYERARROW_WANTED6",
	"S_PLAYERARROW_WANTED7",

	"S_PLAYERBOMB1", // Player bomb overlay
	"S_PLAYERBOMB2",
	"S_PLAYERBOMB3",
	"S_PLAYERBOMB4",
	"S_PLAYERBOMB5",
	"S_PLAYERBOMB6",
	"S_PLAYERBOMB7",
	"S_PLAYERBOMB8",
	"S_PLAYERBOMB9",
	"S_PLAYERBOMB10",
	"S_PLAYERBOMB11",
	"S_PLAYERBOMB12",
	"S_PLAYERBOMB13",
	"S_PLAYERBOMB14",
	"S_PLAYERBOMB15",
	"S_PLAYERBOMB16",
	"S_PLAYERBOMB17",
	"S_PLAYERBOMB18",
	"S_PLAYERBOMB19",
	"S_PLAYERBOMB20",
	"S_PLAYERITEM", // Player item overlay
	"S_PLAYERFAKE", // Player fake overlay

	"S_KARMAWHEEL", // Karma player wheels

	"S_BATTLEPOINT1A", // Battle point indicators
	"S_BATTLEPOINT1B",
	"S_BATTLEPOINT1C",
	"S_BATTLEPOINT1D",
	"S_BATTLEPOINT1E",
	"S_BATTLEPOINT1F",
	"S_BATTLEPOINT1G",
	"S_BATTLEPOINT1H",
	"S_BATTLEPOINT1I",

	"S_BATTLEPOINT2A",
	"S_BATTLEPOINT2B",
	"S_BATTLEPOINT2C",
	"S_BATTLEPOINT2D",
	"S_BATTLEPOINT2E",
	"S_BATTLEPOINT2F",
	"S_BATTLEPOINT2G",
	"S_BATTLEPOINT2H",
	"S_BATTLEPOINT2I",

	"S_BATTLEPOINT3A",
	"S_BATTLEPOINT3B",
	"S_BATTLEPOINT3C",
	"S_BATTLEPOINT3D",
	"S_BATTLEPOINT3E",
	"S_BATTLEPOINT3F",
	"S_BATTLEPOINT3G",
	"S_BATTLEPOINT3H",
	"S_BATTLEPOINT3I",

	// Thunder shield use stuff;
	"S_KSPARK1",	// Sparkling Radius
	"S_KSPARK2",
	"S_KSPARK3",
	"S_KSPARK4",
	"S_KSPARK5",
	"S_KSPARK6",
	"S_KSPARK7",
	"S_KSPARK8",
	"S_KSPARK9",
	"S_KSPARK10",
	"S_KSPARK11",
	"S_KSPARK12",
	"S_KSPARK13",	// ... that's an awful lot.

	"S_LZIO11",	// Straight lightning bolt
	"S_LZIO12",
	"S_LZIO13",
	"S_LZIO14",
	"S_LZIO15",
	"S_LZIO16",
	"S_LZIO17",
	"S_LZIO18",
	"S_LZIO19",

	"S_LZIO21",	// Straight lightning bolt (flipped)
	"S_LZIO22",
	"S_LZIO23",
	"S_LZIO24",
	"S_LZIO25",
	"S_LZIO26",
	"S_LZIO27",
	"S_LZIO28",
	"S_LZIO29",

	"S_KLIT1",	// Diagonal lightning. No, it not being straight doesn't make it gay.
	"S_KLIT2",
	"S_KLIT3",
	"S_KLIT4",
	"S_KLIT5",
	"S_KLIT6",
	"S_KLIT7",
	"S_KLIT8",
	"S_KLIT9",
	"S_KLIT10",
	"S_KLIT11",
	"S_KLIT12",

	"S_FZEROSMOKE1", // F-Zero NO CONTEST explosion
	"S_FZEROSMOKE2",
	"S_FZEROSMOKE3",
	"S_FZEROSMOKE4",
	"S_FZEROSMOKE5",

	"S_FZEROBOOM1",
	"S_FZEROBOOM2",
	"S_FZEROBOOM3",
	"S_FZEROBOOM4",
	"S_FZEROBOOM5",
	"S_FZEROBOOM6",
	"S_FZEROBOOM7",
	"S_FZEROBOOM8",
	"S_FZEROBOOM9",
	"S_FZEROBOOM10",
	"S_FZEROBOOM11",
	"S_FZEROBOOM12",

	"S_FZSLOWSMOKE1",
	"S_FZSLOWSMOKE2",
	"S_FZSLOWSMOKE3",
	"S_FZSLOWSMOKE4",
	"S_FZSLOWSMOKE5",

	// Various plants
	"S_SONICBUSH",

	// Marble Zone
	"S_FLAMEPARTICLE",
	"S_MARBLETORCH",
	"S_MARBLELIGHT",
	"S_MARBLEBURNER",

	// CD Special Stage
	"S_CDUFO",
	"S_CDUFO_DIE",

	// Rusty Rig
	"S_RUSTYLAMP_ORANGE",
	"S_RUSTYCHAIN",

	// D2 Balloon Panic
	"S_BALLOON",
	"S_BALLOONPOP1",
	"S_BALLOONPOP2",
	"S_BALLOONPOP3",

	// Smokin' & Vapin' (Don't try this at home, kids!)
	"S_PETSMOKE0",
	"S_PETSMOKE1",
	"S_PETSMOKE2",
	"S_PETSMOKE3",
	"S_PETSMOKE4",
	"S_PETSMOKE5",
	"S_VVVAPING0",
	"S_VVVAPING1",
	"S_VVVAPING2",
	"S_VVVAPING3",
	"S_VVVAPING4",
	"S_VVVAPING5",
	"S_VVVAPE",

	// Hill Top Zone
	"S_HTZTREE",
	"S_HTZBUSH",

	// Ports of gardens
	"S_SGVINE1",
	"S_SGVINE2",
	"S_SGVINE3",
	"S_PGTREE",
	"S_PGFLOWER1",
	"S_PGFLOWER2",
	"S_PGFLOWER3",
	"S_PGBUSH",
	"S_DHPILLAR",

	// Midnight Channel stuff:
	"S_SPOTLIGHT",	// Spotlight decoration
	"S_RANDOMSHADOW",	// Random Shadow. They're static and don't do nothing.
	"S_GARU1",
	"S_GARU2",
	"S_GARU3",
	"S_TGARU",
	"S_TGARU1",
	"S_TGARU2",
	"S_TGARU3",	// Wind attack used by Roaming Shadows on Players.
	"S_ROAMINGSHADOW",	// Roaming Shadow (the one that uses above's wind attack or smth)
	"S_MAYONAKAARROW",	// Arrow sign

	// Mementos stuff:
	"S_REAPER_INVIS",		// Reaper waiting for spawning
	"S_REAPER",			// Reaper main frame where its thinker is handled
	"S_MEMENTOSTP",		// Mementos teleporter state. (Used for spawning particles)

	// JackInTheBox
	"S_JITB1",
	"S_JITB2",
	"S_JITB3",
	"S_JITB4",
	"S_JITB5",
	"S_JITB6",

	// Color Drive
	"S_CDMOONSP",
	"S_CDBUSHSP",
	"S_CDTREEASP",
	"S_CDTREEBSP",

	// Daytona Speedway
	"S_PINETREE",
	"S_PINETREE_SIDE",

	// Egg Zeppelin
	"S_EZZPROPELLER",
	"S_EZZPROPELLER_BLADE",

	// Desert Palace
	"S_DP_PALMTREE",

	// Aurora Atoll
	"S_AAZTREE_SEG",
	"S_AAZTREE_COCONUT",
	"S_AAZTREE_LEAF",

	// Barren Badlands
	"S_BBZDUST1", // Dust
	"S_BBZDUST2",
	"S_BBZDUST3",
	"S_BBZDUST4",
	"S_FROGGER", // Frog badniks
	"S_FROGGER_ATTACK",
	"S_FROGGER_JUMP",
	"S_FROGTONGUE",
	"S_FROGTONGUE_JOINT",
	"S_ROBRA", // Black cobra badniks
	"S_ROBRA_HEAD",
	"S_ROBRA_JOINT",
	"S_ROBRASHELL_INSIDE",
	"S_ROBRASHELL_OUTSIDE",
	"S_BLUEROBRA", // Blue cobra badniks
	"S_BLUEROBRA_HEAD",
	"S_BLUEROBRA_JOINT",

	// Eerie Grove
	"S_EERIEFOG1",
	"S_EERIEFOG2",
	"S_EERIEFOG3",
	"S_EERIEFOG4",
	"S_EERIEFOG5",

	// SMK ports
	"S_SMK_PIPE1", // Generic pipes
	"S_SMK_PIPE2",
	"S_SMK_MOLE", // Donut Plains Monty Moles
	"S_SMK_THWOMP", // Bowser Castle Thwomps
	"S_SMK_SNOWBALL", // Vanilla Lake snowballs
	"S_SMK_ICEBLOCK", // Vanilla Lake breakable ice blocks
	"S_SMK_ICEBLOCK2",
	"S_SMK_ICEBLOCK_DEBRIS",
	"S_SMK_ICEBLOCK_DEBRIS2",

	// Ezo's maps
	"S_BLUEFIRE1",
	"S_BLUEFIRE2",
	"S_BLUEFIRE3",
	"S_BLUEFIRE4",
	"S_GREENFIRE1",
	"S_GREENFIRE2",
	"S_GREENFIRE3",
	"S_GREENFIRE4",
	"S_REGALCHEST",
	"S_CHIMERASTATUE",
	"S_DRAGONSTATUE",
	"S_LIZARDMANSTATUE",
	"S_PEGASUSSTATUE",
	"S_ZELDAFIRE1",
	"S_ZELDAFIRE2",
	"S_ZELDAFIRE3",
	"S_ZELDAFIRE4",
	"S_GANBARETHING",
	"S_GANBAREDUCK",
	"S_GANBARETREE",
	"S_MONOIDLE",
	"S_MONOCHASE1",
	"S_MONOCHASE2",
	"S_MONOCHASE3",
	"S_MONOCHASE4",
	"S_MONOPAIN",
	"S_REDZELDAFIRE1",
	"S_REDZELDAFIRE2",
	"S_REDZELDAFIRE3",
	"S_REDZELDAFIRE4",
	"S_BOWLINGPIN",
	"S_BOWLINGHIT1",
	"S_BOWLINGHIT2",
	"S_BOWLINGHIT3",
	"S_BOWLINGHIT4",
	"S_ARIDTOAD",
	"S_TOADHIT1",
	"S_TOADHIT2",
	"S_TOADHIT3",
	"S_TOADHIT4",
	"S_EBARRELIDLE",
	"S_EBARREL1",
	"S_EBARREL2",
	"S_EBARREL3",
	"S_EBARREL4",
	"S_EBARREL5",
	"S_EBARREL6",
	"S_EBARREL7",
	"S_EBARREL8",
	"S_EBARREL9",
	"S_EBARREL10",
	"S_EBARREL11",
	"S_EBARREL12",
	"S_EBARREL13",
	"S_EBARREL14",
	"S_EBARREL15",
	"S_EBARREL16",
	"S_EBARREL17",
	"S_EBARREL18",
	"S_MERRYHORSE",
	"S_BLUEFRUIT",
	"S_ORANGEFRUIT",
	"S_REDFRUIT",
	"S_PINKFRUIT",
	"S_ADVENTURESPIKEA1",
	"S_ADVENTURESPIKEA2",
	"S_ADVENTURESPIKEB1",
	"S_ADVENTURESPIKEB2",
	"S_ADVENTURESPIKEC1",
	"S_ADVENTURESPIKEC2",
	"S_BOOSTPROMPT1",
	"S_BOOSTPROMPT2",
	"S_BOOSTOFF1",
	"S_BOOSTOFF2",
	"S_BOOSTON1",
	"S_BOOSTON2",
	"S_LIZARDMAN",
	"S_LIONMAN",

	// Opulence
	"S_OPULENCE_PALMTREE",
	"S_OPULENCE_FERN",

	"S_TUMBLEGEM_IDLE",
	"S_TUMBLEGEM_ROLL",
	"S_TUMBLECOIN_IDLE",
	"S_TUMBLECOIN_FLIP",

	"S_KARMAFIREWORK1",
	"S_KARMAFIREWORK2",
	"S_KARMAFIREWORK3",
	"S_KARMAFIREWORK4",
	"S_KARMAFIREWORKTRAIL",

	// Opaque smoke version, to prevent lag
	"S_OPAQUESMOKE1",
	"S_OPAQUESMOKE2",
	"S_OPAQUESMOKE3",
	"S_OPAQUESMOKE4",
	"S_OPAQUESMOKE5",
	
	//Booststack
	"S_BOOSTSTACK",

	"S_WATERTRAIL1",
	"S_WATERTRAIL2",
	"S_WATERTRAIL3",
	"S_WATERTRAIL4",
	"S_WATERTRAIL5",
	"S_WATERTRAIL6",
	"S_WATERTRAIL7",
	"S_WATERTRAIL8",
	"S_WATERTRAILUNDERLAY1",
	"S_WATERTRAILUNDERLAY2",
	"S_WATERTRAILUNDERLAY3",
	"S_WATERTRAILUNDERLAY4",
	"S_WATERTRAILUNDERLAY5",
	"S_WATERTRAILUNDERLAY6",
	"S_WATERTRAILUNDERLAY7",
	"S_WATERTRAILUNDERLAY8",

#ifdef SEENAMES
	"S_NAMECHECK",
#endif
};

// RegEx to generate this from info.h: ^\tMT_([^,]+), --> \t"MT_\1",
// I am leaving the prefixes solely for clarity to programmers,
// because sadly no one remembers this place while searching for full state names.
const char *const MOBJTYPE_LIST[] = {  // array length left dynamic for sanity testing later.
	"MT_NULL",
	"MT_UNKNOWN",

	"MT_THOK", // Thok! mobj
	"MT_PLAYER",

	// Enemies
	"MT_BLUECRAWLA",
	"MT_REDCRAWLA",
	"MT_GFZFISH", // Greenflower Fish
	"MT_GOLDBUZZ",
	"MT_REDBUZZ",
	"MT_AQUABUZZ",
	"MT_JETTBOMBER", // Jetty-Syn Bomber
	"MT_JETTGUNNER", // Jetty-Syn Gunner
	"MT_CRAWLACOMMANDER", // Crawla Commander
	"MT_DETON", // Deton
	"MT_SKIM", // Skim mine dropper
	"MT_TURRET",
	"MT_POPUPTURRET",
	"MT_SHARP", // Sharp
	"MT_JETJAW", // Jet Jaw
	"MT_SNAILER", // Snailer
	"MT_VULTURE", // Vulture
	"MT_POINTY", // Pointy
	"MT_POINTYBALL", // Pointy Ball
	"MT_ROBOHOOD", // Robo-Hood
	"MT_FACESTABBER", // CastleBot FaceStabber
	"MT_EGGGUARD", // Egg Guard
	"MT_EGGSHIELD", // Egg Shield for Egg Guard
	"MT_GSNAPPER", // Green Snapper
	"MT_MINUS", // Minus
	"MT_SPRINGSHELL", // Spring Shell (no drop)
	"MT_YELLOWSHELL", // Spring Shell (yellow)
	"MT_UNIDUS", // Unidus
	"MT_UNIBALL", // Unidus Ball

	// Generic Boss Items
	"MT_BOSSEXPLODE",
	"MT_SONIC3KBOSSEXPLODE",
	"MT_BOSSFLYPOINT",
	"MT_EGGTRAP",
	"MT_BOSS3WAYPOINT",
	"MT_BOSS9GATHERPOINT",

	// Boss 1
	"MT_EGGMOBILE",
	"MT_JETFUME1",
	"MT_EGGMOBILE_BALL",
	"MT_EGGMOBILE_TARGET",
	"MT_EGGMOBILE_FIRE",

	// Boss 2
	"MT_EGGMOBILE2",
	"MT_EGGMOBILE2_POGO",
	"MT_BOSSTANK1",
	"MT_BOSSTANK2",
	"MT_BOSSSPIGOT",
	"MT_GOOP",

	// Boss 3
	"MT_EGGMOBILE3",
	"MT_PROPELLER",
	"MT_FAKEMOBILE",

	// Boss 4
	"MT_EGGMOBILE4",
	"MT_EGGMOBILE4_MACE",
	"MT_JETFLAME",

	// Black Eggman (Boss 7)
	"MT_BLACKEGGMAN",
	"MT_BLACKEGGMAN_HELPER",
	"MT_BLACKEGGMAN_GOOPFIRE",
	"MT_BLACKEGGMAN_MISSILE",

	// New Very-Last-Minute 2.1 Brak Eggman (Cy-Brak-demon)
	"MT_CYBRAKDEMON",
	"MT_CYBRAKDEMON_ELECTRIC_BARRIER",
	"MT_CYBRAKDEMON_MISSILE",
	"MT_CYBRAKDEMON_FLAMESHOT",
	"MT_CYBRAKDEMON_FLAMEREST",
	"MT_CYBRAKDEMON_TARGET_RETICULE",
	"MT_CYBRAKDEMON_TARGET_DOT",
	"MT_CYBRAKDEMON_NAPALM_BOMB_LARGE",
	"MT_CYBRAKDEMON_NAPALM_BOMB_SMALL",
	"MT_CYBRAKDEMON_NAPALM_FLAMES",
	"MT_CYBRAKDEMON_VILE_EXPLOSION",

	// Metal Sonic
	"MT_METALSONIC_RACE",
	"MT_METALSONIC_BATTLE",
	"MT_MSSHIELD_FRONT",
	"MT_MSGATHER",

	// Collectible Items
	"MT_RING",
	"MT_FLINGRING", // Lost ring
	"MT_BLUEBALL",  // Blue sphere replacement for special stages
	"MT_REDTEAMRING",  //Rings collectable by red team.
	"MT_BLUETEAMRING", //Rings collectable by blue team.
	"MT_EMMY", // emerald token for special stage
	"MT_TOKEN", // Special Stage Token (uncollectible part)
	"MT_REDFLAG", // Red CTF Flag
	"MT_BLUEFLAG", // Blue CTF Flag
	"MT_EMBLEM",
	"MT_EMERALD1",
	"MT_EMERALD2",
	"MT_EMERALD3",
	"MT_EMERALD4",
	"MT_EMERALD5",
	"MT_EMERALD6",
	"MT_EMERALD7",
	"MT_EMERHUNT", // Emerald Hunt
	"MT_EMERALDSPAWN", // Emerald spawner w/ delay
	"MT_FLINGEMERALD", // Lost emerald

	// Springs and others
	"MT_FAN",
	"MT_STEAM", // Steam riser
	"MT_BLUESPRING",
	"MT_YELLOWSPRING",
	"MT_REDSPRING",
	"MT_YELLOWDIAG", // Yellow Diagonal Spring
	"MT_REDDIAG", // Red Diagonal Spring

	// Interactive Objects
	"MT_BUBBLES", // Bubble source
	"MT_SIGN", // Level end sign
	"MT_SPIKEBALL", // Spike Ball
	"MT_SPECIALSPIKEBALL",
	"MT_SPINFIRE",
	"MT_SPIKE",
	"MT_STARPOST",
	"MT_BIGMINE",
	"MT_BIGAIRMINE",
	"MT_CANNONLAUNCHER",

	// Monitor Boxes
	"MT_SUPERRINGBOX",
	"MT_REDRINGBOX",
	"MT_BLUERINGBOX",
	"MT_SNEAKERTV",
	"MT_INV",
	"MT_PRUP", // 1up Box
	"MT_YELLOWTV", // Attract shield TV
	"MT_BLUETV", // Force shield TV
	"MT_BLACKTV", // Bomb shield TV
	"MT_WHITETV", // Jump shield TV
	"MT_GREENTV", // Elemental shield TV
	"MT_PITYTV", // Pity shield TV
	"MT_EGGMANBOX",
	"MT_MIXUPBOX",
	"MT_RECYCLETV",
	"MT_RECYCLEICO",
	"MT_QUESTIONBOX",
	"MT_GRAVITYBOX",
	"MT_SCORETVSMALL",
	"MT_SCORETVLARGE",

	// Monitor miscellany
	"MT_MONITOREXPLOSION",
	"MT_REDMONITOREXPLOSION",
	"MT_BLUEMONITOREXPLOSION",
	"MT_RINGICO",
	"MT_SHOESICO",
	"MT_INVCICO",
	"MT_1UPICO",
	"MT_YSHIELDICO",
	"MT_BSHIELDICO",
	"MT_KSHIELDICO",
	"MT_WSHIELDICO",
	"MT_GSHIELDICO",
	"MT_PITYSHIELDICO",
	"MT_EGGMANICO",
	"MT_MIXUPICO",
	"MT_GRAVITYICO",
	"MT_SCOREICOSMALL",
	"MT_SCOREICOLARGE",

	// Projectiles
	"MT_ROCKET",
	"MT_LASER",
	"MT_TORPEDO",
	"MT_TORPEDO2", // silent
	"MT_ENERGYBALL",
	"MT_MINE", // Skim/Jetty-Syn mine
	"MT_JETTBULLET", // Jetty-Syn Bullet
	"MT_TURRETLASER",
	"MT_CANNONBALL", // Cannonball
	"MT_CANNONBALLDECOR", // Decorative/still cannonball
	"MT_ARROW", // Arrow
	"MT_DEMONFIRE", // Trapgoyle fire

	// Greenflower Scenery
	"MT_GFZFLOWER1",
	"MT_GFZFLOWER2",
	"MT_GFZFLOWER3",
	"MT_BERRYBUSH",
	"MT_BUSH",

	// Techno Hill Scenery
	"MT_THZPLANT", // THZ Plant
	"MT_ALARM",

	// Deep Sea Scenery
	"MT_GARGOYLE", // Deep Sea Gargoyle
	"MT_SEAWEED", // DSZ Seaweed
	"MT_WATERDRIP", // Dripping Water source
	"MT_WATERDROP", // Water drop from dripping water
	"MT_CORAL1", // Coral 1
	"MT_CORAL2", // Coral 2
	"MT_CORAL3", // Coral 3
	"MT_BLUECRYSTAL", // Blue Crystal

	// Castle Eggman Scenery
	"MT_CHAIN", // CEZ Chain
	"MT_FLAME", // Flame
	"MT_EGGSTATUE", // Eggman Statue
	"MT_MACEPOINT", // Mace rotation point
	"MT_SWINGMACEPOINT", // Mace swinging point
	"MT_HANGMACEPOINT", // Hangable mace chain
	"MT_SPINMACEPOINT", // Spin/Controllable mace chain
	"MT_HIDDEN_SLING", // Spin mace chain (activatable)
	"MT_SMALLMACECHAIN", // Small Mace Chain
	"MT_BIGMACECHAIN", // Big Mace Chain
	"MT_SMALLMACE", // Small Mace
	"MT_BIGMACE", // Big Mace
	"MT_CEZFLOWER",

	// Arid Canyon Scenery
	"MT_BIGTUMBLEWEED",
	"MT_LITTLETUMBLEWEED",
	"MT_CACTI1",
	"MT_CACTI2",
	"MT_CACTI3",
	"MT_CACTI4",

	// Red Volcano Scenery
	"MT_FLAMEJET",
	"MT_VERTICALFLAMEJET",
	"MT_FLAMEJETFLAME",

	"MT_FJSPINAXISA", // Counter-clockwise
	"MT_FJSPINHELPERA",
	"MT_FJSPINAXISB", // Clockwise
	"MT_FJSPINHELPERB",

	"MT_FLAMEJETFLAMEB", // Blade's flame

	// Dark City Scenery

	// Egg Rock Scenery

	// Azure Temple Scenery
	"MT_TRAPGOYLE",
	"MT_TRAPGOYLEUP",
	"MT_TRAPGOYLEDOWN",
	"MT_TRAPGOYLELONG",
	"MT_TARGET",

	// Stalagmites
	"MT_STALAGMITE0",
	"MT_STALAGMITE1",
	"MT_STALAGMITE2",
	"MT_STALAGMITE3",
	"MT_STALAGMITE4",
	"MT_STALAGMITE5",
	"MT_STALAGMITE6",
	"MT_STALAGMITE7",
	"MT_STALAGMITE8",
	"MT_STALAGMITE9",

	// Christmas Scenery
	"MT_XMASPOLE",
	"MT_CANDYCANE",
	"MT_SNOWMAN",
	"MT_SNOWMANHAT",
	"MT_LAMPPOST1",
	"MT_LAMPPOST2",
	"MT_HANGSTAR",

	// Botanic Serenity
	"MT_BSZTALLFLOWER_RED",
	"MT_BSZTALLFLOWER_PURPLE",
	"MT_BSZTALLFLOWER_BLUE",
	"MT_BSZTALLFLOWER_CYAN",
	"MT_BSZTALLFLOWER_YELLOW",
	"MT_BSZTALLFLOWER_ORANGE",
	"MT_BSZFLOWER_RED",
	"MT_BSZFLOWER_PURPLE",
	"MT_BSZFLOWER_BLUE",
	"MT_BSZFLOWER_CYAN",
	"MT_BSZFLOWER_YELLOW",
	"MT_BSZFLOWER_ORANGE",
	"MT_BSZSHORTFLOWER_RED",
	"MT_BSZSHORTFLOWER_PURPLE",
	"MT_BSZSHORTFLOWER_BLUE",
	"MT_BSZSHORTFLOWER_CYAN",
	"MT_BSZSHORTFLOWER_YELLOW",
	"MT_BSZSHORTFLOWER_ORANGE",
	"MT_BSZTULIP_RED",
	"MT_BSZTULIP_PURPLE",
	"MT_BSZTULIP_BLUE",
	"MT_BSZTULIP_CYAN",
	"MT_BSZTULIP_YELLOW",
	"MT_BSZTULIP_ORANGE",
	"MT_BSZCLUSTER_RED",
	"MT_BSZCLUSTER_PURPLE",
	"MT_BSZCLUSTER_BLUE",
	"MT_BSZCLUSTER_CYAN",
	"MT_BSZCLUSTER_YELLOW",
	"MT_BSZCLUSTER_ORANGE",
	"MT_BSZBUSH_RED",
	"MT_BSZBUSH_PURPLE",
	"MT_BSZBUSH_BLUE",
	"MT_BSZBUSH_CYAN",
	"MT_BSZBUSH_YELLOW",
	"MT_BSZBUSH_ORANGE",
	"MT_BSZVINE_RED",
	"MT_BSZVINE_PURPLE",
	"MT_BSZVINE_BLUE",
	"MT_BSZVINE_CYAN",
	"MT_BSZVINE_YELLOW",
	"MT_BSZVINE_ORANGE",
	"MT_BSZSHRUB",
	"MT_BSZCLOVER",
	"MT_BSZFISH",
	"MT_BSZSUNFLOWER",

	// Misc scenery
	"MT_DBALL",
	"MT_EGGSTATUE2",

	// Powerup Indicators
	"MT_GREENORB", // Elemental shield mobj
	"MT_YELLOWORB", // Attract shield mobj
	"MT_BLUEORB", // Force shield mobj
	"MT_BLACKORB", // Armageddon shield mobj
	"MT_WHITEORB", // Whirlwind shield mobj
	"MT_PITYORB", // Pity shield mobj
	"MT_IVSP", // invincibility sparkles
	"MT_SUPERSPARK", // Super Sonic Spark

	// Freed Animals
	"MT_BIRD", // Birdie freed!
	"MT_BUNNY", // Bunny freed!
	"MT_MOUSE", // Mouse
	"MT_CHICKEN", // Chicken
	"MT_COW", // Cow
	"MT_REDBIRD", // Red Birdie in Bubble

	// Environmental Effects
	"MT_RAIN", // Rain
	"MT_SNOWFLAKE", // Snowflake
	"MT_SPLISH", // Water splish!
	"MT_SMOKE",
	"MT_SMALLBUBBLE", // small bubble
	"MT_MEDIUMBUBBLE", // medium bubble
	"MT_EXTRALARGEBUBBLE", // extra large bubble
	"MT_TFOG",
	"MT_SEED",
	"MT_PARTICLE",
	"MT_PARTICLEGEN", // For fans, etc.

	// Game Indicators
	"MT_SCORE", // score logo
	"MT_DROWNNUMBERS", // Drowning Timer
	"MT_GOTEMERALD", // Chaos Emerald (intangible)
	"MT_TAG", // Tag Sign
	"MT_GOTFLAG", // Got Flag sign
	"MT_GOTFLAG2", // Got Flag sign

	// Ambient Sounds
	"MT_AWATERA", // Ambient Water Sound 1
	"MT_AWATERB", // Ambient Water Sound 2
	"MT_AWATERC", // Ambient Water Sound 3
	"MT_AWATERD", // Ambient Water Sound 4
	"MT_AWATERE", // Ambient Water Sound 5
	"MT_AWATERF", // Ambient Water Sound 6
	"MT_AWATERG", // Ambient Water Sound 7
	"MT_AWATERH", // Ambient Water Sound 8
	"MT_RANDOMAMBIENT",
	"MT_RANDOMAMBIENT2",

	// Ring Weapons
	"MT_REDRING",
	"MT_BOUNCERING",
	"MT_RAILRING",
	"MT_INFINITYRING",
	"MT_AUTOMATICRING",
	"MT_EXPLOSIONRING",
	"MT_SCATTERRING",
	"MT_GRENADERING",

	"MT_BOUNCEPICKUP",
	"MT_RAILPICKUP",
	"MT_AUTOPICKUP",
	"MT_EXPLODEPICKUP",
	"MT_SCATTERPICKUP",
	"MT_GRENADEPICKUP",

	"MT_THROWNBOUNCE",
	"MT_THROWNINFINITY",
	"MT_THROWNAUTOMATIC",
	"MT_THROWNSCATTER",
	"MT_THROWNEXPLOSION",
	"MT_THROWNGRENADE",

	// Mario-specific stuff
	"MT_COIN",
	"MT_FLINGCOIN",
	"MT_GOOMBA",
	"MT_BLUEGOOMBA",
	"MT_FIREFLOWER",
	"MT_FIREBALL",
	"MT_SHELL",
	"MT_PUMA",
	"MT_HAMMER",
	"MT_KOOPA",
	"MT_KOOPAFLAME",
	"MT_AXE",
	"MT_MARIOBUSH1",
	"MT_MARIOBUSH2",
	"MT_TOAD",

	// NiGHTS Stuff
	"MT_AXIS",
	"MT_AXISTRANSFER",
	"MT_AXISTRANSFERLINE",
	"MT_NIGHTSDRONE",
	"MT_NIGHTSGOAL",
	"MT_NIGHTSCHAR",
	"MT_NIGHTSPARKLE",
	"MT_NIGHTSLOOPHELPER",
	"MT_NIGHTSBUMPER", // NiGHTS Bumper
	"MT_HOOP",
	"MT_HOOPCOLLIDE", // Collision detection for NiGHTS hoops
	"MT_HOOPCENTER", // Center of a hoop
	"MT_NIGHTSCORE",
	"MT_NIGHTSWING",
	"MT_NIGHTSSUPERLOOP",
	"MT_NIGHTSDRILLREFILL",
	"MT_NIGHTSHELPER",
	"MT_NIGHTSEXTRATIME",
	"MT_NIGHTSLINKFREEZE",
	"MT_EGGCAPSULE",
	"MT_NIGHTOPIANHELPER", // the actual helper object that orbits you

	// Utility Objects
	"MT_TELEPORTMAN",
	"MT_ALTVIEWMAN",
	"MT_CRUMBLEOBJ", // Sound generator for crumbling platform
	"MT_TUBEWAYPOINT",
	"MT_PUSH",
	"MT_PULL",
	"MT_GHOST",
	"MT_OVERLAY",
	"MT_POLYANCHOR",
	"MT_POLYSPAWN",
	"MT_POLYSPAWNCRUSH",

	// Skybox objects
	"MT_SKYBOX",

	// Debris
	"MT_SPARK", //spark
	"MT_EXPLODE", // Robot Explosion
	"MT_UWEXPLODE", // Underwater Explosion
	"MT_ROCKSPAWNER",
	"MT_FALLINGROCK",
	"MT_ROCKCRUMBLE1",
	"MT_ROCKCRUMBLE2",
	"MT_ROCKCRUMBLE3",
	"MT_ROCKCRUMBLE4",
	"MT_ROCKCRUMBLE5",
	"MT_ROCKCRUMBLE6",
	"MT_ROCKCRUMBLE7",
	"MT_ROCKCRUMBLE8",
	"MT_ROCKCRUMBLE9",
	"MT_ROCKCRUMBLE10",
	"MT_ROCKCRUMBLE11",
	"MT_ROCKCRUMBLE12",
	"MT_ROCKCRUMBLE13",
	"MT_ROCKCRUMBLE14",
	"MT_ROCKCRUMBLE15",
	"MT_ROCKCRUMBLE16",

	"MT_SRB1_CRAWLA",
	"MT_SRB1_BAT",
	"MT_SRB1_ROBOFISH",
	"MT_SRB1_VOLCANOGUY",
	"MT_SRB1_HOPPY",
	"MT_SRB1_HOPPYWATER",
	"MT_SRB1_HOPPYSKYLAB",
	"MT_SRB1_MMZFLYING",
	"MT_SRB1_UFO",
	"MT_SRB1_GRAYBOT",
	"MT_SRB1_ROBOTOPOLIS",
	"MT_SRB1_RBZBUZZ",
	"MT_SRB1_RBZSPIKES",
	"MT_SRB1_METALSONIC",
	"MT_SRB1_GOLDBOT",
	"MT_SRB1_GENREX",

	// SRB2kart
	"MT_GRAYSPRING",
	"MT_INVISSPRING",
	"MT_BLUEDIAG",
	"MT_RANDOMITEM",
	"MT_RANDOMITEMPOP",
	"MT_FLOATINGITEM",

	"MT_SIGNSPARKLE",

	"MT_FASTLINE",
	"MT_FASTDUST",
	"MT_BOOSTFLAME",
	"MT_BOOSTSMOKE",
	"MT_SNEAKERTRAIL",
	"MT_AIZDRIFTSTRAT",
	"MT_SPARKLETRAIL",
	"MT_INVULNFLASH",
	"MT_WIPEOUTTRAIL",
	"MT_DRIFTSPARK",
	"MT_BRAKEDRIFT",
	"MT_DRIFTDUST",

	"MT_ROCKETSNEAKER", // Rocket sneakers

	"MT_EGGMANITEM", // Eggman items
	"MT_EGGMANITEM_SHIELD",

	"MT_BANANA", // Banana Stuff
	"MT_BANANA_SHIELD",

	"MT_ORBINAUT", // Orbinaut stuff
	"MT_ORBINAUT_SHIELD",

	"MT_JAWZ", // Jawz stuff
	"MT_JAWZ_DUD",
	"MT_JAWZ_SHIELD",

	"MT_PLAYERRETICULE", // Jawz reticule

	"MT_SSMINE_SHIELD", // Special Stage Mine stuff
	"MT_SSMINE",
	"MT_MINEEXPLOSION",
	"MT_MINEEXPLOSIONSOUND",

	"MT_SMOLDERING", // New explosion
	"MT_BOOMEXPLODE",
	"MT_BOOMPARTICLE",

	"MT_BALLHOG", // Ballhog
	"MT_BALLHOGBOOM",

	"MT_SPB", // Self-Propelled Bomb
	"MT_SPBEXPLOSION",

	"MT_THUNDERSHIELD", // Shields
	"MT_BUBBLESHIELD",
	"MT_FLAMESHIELD",
	"MT_BUBBLESHIELDTRAP",

	"MT_SINK", // Kitchen Sink Stuff
	"MT_SINK_SHIELD",
	"MT_SINKTRAIL",

	"MT_BATTLEBUMPER", // Battle Mode bumper

	"MT_DEZLASER",

	"MT_WAYPOINT",

	"MT_RANDOMAUDIENCE",

	"MT_FLAYM",
	"MT_DEVIL",
	"MT_ANGEL",
	"MT_PALMTREE",
	"MT_FLAG",
	"MT_HEDGEHOG",
	"MT_BUSH1",
	"MT_TWEE",
	"MT_HYDRANT",

	"MT_BIGPUMA",
	"MT_APPLE",

	"MT_DOOD_FLOWER1",
	"MT_DOOD_FLOWER2",
	"MT_DOOD_FLOWER3",
	"MT_DOOD_FLOWER4",
	"MT_DOOD_BOX",
	"MT_DOOD_BALLOON",
	"MT_BIGRING",

	"MT_SNES_DONUTBUSH1",
	"MT_SNES_DONUTBUSH2",
	"MT_SNES_DONUTBUSH3",

	"MT_GBA_BOO",

	"MT_BUZZBOMBER",
	"MT_CHOMPER",
	"MT_PALMTREE2",
	"MT_PURPLEFLOWER1",
	"MT_PURPLEFLOWER2",
	"MT_YELLOWFLOWER1",
	"MT_YELLOWFLOWER2",
	"MT_PLANT2",
	"MT_PLANT3",
	"MT_PLANT4",

	"MT_SKULL",
	"MT_PHANTREE",
	"MT_FLYINGGARG",
	"MT_LAMPPOST",
	"MT_MOSSYTREE",

	"MT_SHADOW",

	"MT_BUMP",

	"MT_FLINGENERGY",

	"MT_ITEMCLASH",

	"MT_FIREDITEM",

	"MT_INSTASHIELDA",
	"MT_INSTASHIELDB",

	"MT_PLAYERARROW",
	"MT_PLAYERWANTED",

	"MT_KARMAHITBOX",
	"MT_KARMAWHEEL",

	"MT_BATTLEPOINT",

	"MT_FZEROBOOM",

	// Various plants
	"MT_SONICBUSH",

	// Marble Zone
	"MT_FLAMEPARTICLE",
	"MT_MARBLETORCH",
	"MT_MARBLELIGHT",
	"MT_MARBLEBURNER",

	// CD Special Stage
	"MT_CDUFO",

	// Rusty Rig
	"MT_RUSTYLAMP_ORANGE",
	"MT_RUSTYCHAIN",

	// D2 Balloon Panic
	"MT_BALLOON",

	// Smokin' & Vapin' (Don't try this at home, kids!)
	"MT_PETSMOKER",
	"MT_PETSMOKE",
	"MT_VVVAPE",

	// Hill Top Zone
	"MT_HTZTREE",
	"MT_HTZBUSH",

	// Ports of gardens
	"MT_SGVINE1",
	"MT_SGVINE2",
	"MT_SGVINE3",
	"MT_PGTREE",
	"MT_PGFLOWER1",
	"MT_PGFLOWER2",
	"MT_PGFLOWER3",
	"MT_PGBUSH",
	"MT_DHPILLAR",

	// Midnight Channel stuff:
	"MT_SPOTLIGHT",		// Spotlight Object
	"MT_RANDOMSHADOW",	// Random static Shadows.
	"MT_ROAMINGSHADOW",	// Roaming Shadows.
	"MT_MAYONAKAARROW",	// Arrow static signs for Mayonaka

	// Mementos stuff
	"MT_REAPERWAYPOINT",
	"MT_REAPER",
	"MT_MEMENTOSTP",
	"MT_MEMENTOSPARTICLE",

	"MT_JACKINTHEBOX",

	// Color Drive:
	"MT_CDMOON",
	"MT_CDBUSH",
	"MT_CDTREEA",
	"MT_CDTREEB",

	// Daytona Speedway
	"MT_PINETREE",
	"MT_PINETREE_SIDE",

	// Egg Zeppelin
	"MT_EZZPROPELLER",
	"MT_EZZPROPELLER_BLADE",

	// Desert Palace
	"MT_DP_PALMTREE",

	// Aurora Atoll
	"MT_AAZTREE_HELPER",
	"MT_AAZTREE_SEG",
	"MT_AAZTREE_COCONUT",
	"MT_AAZTREE_LEAF",

	// Barren Badlands
	"MT_BBZDUST",
	"MT_FROGGER",
	"MT_FROGTONGUE",
	"MT_FROGTONGUE_JOINT",
	"MT_ROBRA",
	"MT_ROBRA_HEAD",
	"MT_ROBRA_JOINT",
	"MT_BLUEROBRA",
	"MT_BLUEROBRA_HEAD",
	"MT_BLUEROBRA_JOINT",

	// Eerie Grove
	"MT_EERIEFOG",
	"MT_EERIEFOGGEN",

	// SMK ports
	"MT_SMK_PIPE",
	"MT_SMK_MOLESPAWNER",
	"MT_SMK_MOLE",
	"MT_SMK_THWOMP",
	"MT_SMK_SNOWBALL",
	"MT_SMK_ICEBLOCK",
	"MT_SMK_ICEBLOCK_SIDE",
	"MT_SMK_ICEBLOCK_DEBRIS",

	// Ezo's maps
	"MT_BLUEFIRE",
	"MT_GREENFIRE",
	"MT_REGALCHEST",
	"MT_CHIMERASTATUE",
	"MT_DRAGONSTATUE",
	"MT_LIZARDMANSTATUE",
	"MT_PEGASUSSTATUE",
	"MT_ZELDAFIRE",
	"MT_GANBARETHING",
	"MT_GANBAREDUCK",
	"MT_GANBARETREE",
	"MT_MONOKUMA",
	"MT_REDZELDAFIRE",
	"MT_BOWLINGPIN",
	"MT_MERRYAMBIENCE",
	"MT_TWINKLECARTAMBIENCE",
	"MT_EXPLODINGBARREL",
	"MT_MERRYHORSE",
	"MT_BLUEFRUIT",
	"MT_ORANGEFRUIT",
	"MT_REDFRUIT",
	"MT_PINKFRUIT",
	"MT_ADVENTURESPIKEA",
	"MT_ADVENTURESPIKEB",
	"MT_ADVENTURESPIKEC",
	"MT_BOOSTPROMPT",
	"MT_BOOSTOFF",
	"MT_BOOSTON",
	"MT_ARIDTOAD",
	"MT_LIZARDMAN",
	"MT_LIONMAN",

	// Opulence
	"MT_OPULENCE_PALMTREE",
	"MT_OPULENCE_FERN",

	"MT_TUMBLEGEM",
	"MT_TUMBLECOIN",

	"MT_KARMAFIREWORK",
	
	//Booststack effect
	"MT_BOOSTSTACK",

	"MT_WATERTRAIL",
	"MT_WATERTRAILUNDERLAY",

	"MT_FOLLOWER",
#ifdef SEENAMES
	"MT_NAMECHECK",
#endif
};

static const char *const MOBJFLAG_LIST[] = {
	"SPECIAL",
	"SOLID",
	"SHOOTABLE",
	"NOSECTOR",
	"NOBLOCKMAP",
	"PAPERCOLLISION",
	"PUSHABLE",
	"BOSS",
	"SPAWNCEILING",
	"NOGRAVITY",
	"AMBIENT",
	"SLIDEME",
	"NOCLIP",
	"FLOAT",
	"BOXICON",
	"MISSILE",
	"SPRING",
	"BOUNCE",
	"MONITOR",
	"NOTHINK",
	"FIRE",
	"NOCLIPHEIGHT",
	"ENEMY",
	"SCENERY",
	"PAIN",
	"STICKY",
	"NIGHTSITEM",
	"NOCLIPTHING",
	"GRENADEBOUNCE",
	"RUNSPAWNFUNC",
	"DONTENCOREMAP",
	NULL
};

// \tMF2_(\S+).*// (.+) --> \t"\1", // \2
static const char *const MOBJFLAG2_LIST[] = {
	"AXIS",			// It's a NiGHTS axis! (For faster checking)
	"TWOD",			// Moves like it's in a 2D level
	"DONTRESPAWN",	// Don't respawn this object!
	"DONTDRAW",		// Don't generate a vissprite
	"AUTOMATIC",	// Thrown ring has automatic properties
	"RAILRING",		// Thrown ring has rail properties
	"BOUNCERING",	// Thrown ring has bounce properties
	"EXPLOSION",	// Thrown ring has explosive properties
	"SCATTER",		// Thrown ring has scatter properties
	"BEYONDTHEGRAVE",// Source of this missile has died and has since respawned.
	"PUSHED",		// Mobj was already pushed this tic
	"SLIDEPUSH",	// MF_PUSHABLE that pushes continuously.
	"CLASSICPUSH",	// Drops straight down when object has negative Z.
	"STANDONME",	// While not pushable, stand on me anyway.
	"INFLOAT",		// Floating to a height for a move, don't auto float to target's height.
	"DEBRIS",		// Splash ring from explosion ring
	"NIGHTSPULL",	// Attracted from a paraloop
	"JUSTATTACKED",	// can be pushed by other moving mobjs
	"FIRING",		// turret fire
	"SUPERFIRE",	// Firing something with Super Sonic-stopping properties. Or, if mobj has MF_MISSILE, this is the actual fire from it.
	"SHADOW",		// Fuzzy draw, makes targeting harder.
	"STRONGBOX",	// Flag used for "strong" random monitors.
	"OBJECTFLIP",	// Flag for objects that always have flipped gravity.
	"SKULLFLY",		// Special handling: skull in flight.
	"FRET",			// Flashing from a previous hit
	"BOSSNOTRAP",	// No Egg Trap after boss
	"BOSSFLEE",		// Boss is fleeing!
	"BOSSDEAD",		// Boss is dead! (Not necessarily fleeing, if a fleeing point doesn't exist.)
	"AMBUSH",       // Alternate behaviour typically set by MTF_AMBUSH
	NULL
};

static const char *const MOBJEFLAG_LIST[] = {
	"ONGROUND", // The mobj stands on solid floor (not on another mobj or in air)
	"JUSTHITFLOOR", // The mobj just hit the floor while falling, this is cleared on next frame
	"TOUCHWATER", // The mobj stands in a sector with water, and touches the surface
	"UNDERWATER", // The mobj stands in a sector with water, and his waist is BELOW the water surface
	"JUSTSTEPPEDDOWN", // used for ramp sectors
	"VERTICALFLIP", // Vertically flip sprite/allow upside-down physics
	"GOOWATER", // Goo water
	"JUSTBOUNCEDWALL", // SRB2Kart: Mobj already bounced off a wall this tic
	"SPRUNG", // Mobj was already sprung this tic
	"APPLYPMOMZ", // Platform movement
	"DRAWONLYFORP1", // SRB2Kart: Splitscreen sprite draw flags
	"DRAWONLYFORP2",
	"DRAWONLYFORP3",
	"DRAWONLYFORP4",
	NULL
};

static const char *const MAPTHINGFLAG_LIST[4] = {
	NULL,
	"OBJECTFLIP", // Reverse gravity flag for objects.
	"OBJECTSPECIAL", // Special flag used with certain objects.
	"AMBUSH" // Deaf monsters/do not react to sound.
};

static const char *const PLAYERFLAG_LIST[] = {
	// Flip camera angle with gravity flip prefrence.
	"FLIPCAM",

	// Cheats
	"GODMODE",
	"NOCLIP",
	"INVIS",

	// True if button down last tic.
	"ATTACKDOWN",
	"USEDOWN",
	"JUMPDOWN",
	"WPNDOWN",

	// Unmoving states
	"STASIS", // Player is not allowed to move
	"JUMPSTASIS", // and that includes jumping.
	// (we don't include FULLSTASIS here I guess because it's just those two together...?)

	// Did you get a time-over?
	"TIMEOVER",

	// SRB2Kart: spectator that wants to join
	"WANTSTOJOIN",

	// Character action status
	"JUMPED",
	"SPINNING",
	"STARTDASH",
	"THOKKED",

	// Are you gliding?
	"GLIDING",

	// Tails pickup!
	"CARRIED",

	// Sliding (usually in water) like Labyrinth/Oil Ocean
	"SLIDING",

	// Hanging on a rope
	"ROPEHANG",

	// Hanging on an item of some kind - zipline, chain, etc. (->tracer)
	"ITEMHANG",

	// On the mace chain spinning around (->tracer)
	"MACESPIN",

	/*** NIGHTS STUFF ***/
	// Is the player in NiGHTS mode?
	"NIGHTSMODE",
	"TRANSFERTOCLOSEST",

	// Spill rings after falling
	"NIGHTSFALL",
	"DRILLING",
	"SKIDDOWN",

	/*** TAG STUFF ***/
	"TAGGED", // Player has been tagged and awaits the next round in hide and seek.
	"TAGIT", // The player is it! For Tag Mode

	/*** misc ***/
	"FORCESTRAFE", // Translate turn inputs into strafe inputs
	"ANALOGMODE", // Analog mode?

	NULL // stop loop here.
};

// Linedef flags
static const char *const ML_LIST[16] = {
	"IMPASSIBLE",
	"BLOCKMONSTERS",
	"TWOSIDED",
	"DONTPEGTOP",
	"DONTPEGBOTTOM",
	"EFFECT1",
	"NOCLIMB",
	"EFFECT2",
	"EFFECT3",
	"EFFECT4",
	"EFFECT5",
	"NOSONIC",
	"NOTAILS",
	"NOKNUX",
	"BOUNCY",
	"TFERLINE"
};

// This DOES differ from r_draw's Color_Names, unfortunately.
// Also includes Super colors
static const char *COLOR_ENUMS[] = { // Rejigged for Kart.
	"NONE",			// SKINCOLOR_NONE
	"WHITE",		// SKINCOLOR_WHITE
	"SILVER",		// SKINCOLOR_SILVER
	"GREY",			// SKINCOLOR_GREY
	"NICKEL",		// SKINCOLOR_NICKEL
	"BLACK",		// SKINCOLOR_BLACK
	"SKUNK",		// SKINCOLOR_SKUNK
	"FAIRY",		// SKINCOLOR_FAIRY
	"POPCORN",		// SKINCOLOR_POPCORN
	"ARTICHOKE",	// SKINCOLOR_ARTICHOKE
	"PIGEON",		// SKINCOLOR_PIGEON
	"SEPIA",		// SKINCOLOR_SEPIA
	"BEIGE",		// SKINCOLOR_BEIGE
	"WALNUT",		// SKINCOLOR_WALNUT
	"BROWN",		// SKINCOLOR_BROWN
	"LEATHER",		// SKINCOLOR_LEATHER
	"SALMON",		// SKINCOLOR_SALMON
	"PINK",			// SKINCOLOR_PINK
	"ROSE",			// SKINCOLOR_ROSE
	"BRICK",		// SKINCOLOR_BRICK
	"CINNAMON",		// SKINCOLOR_CINNAMON
	"RUBY",			// SKINCOLOR_RUBY
	"RASPBERRY",	// SKINCOLOR_RASPBERRY
	"CHERRY",		// SKINCOLOR_CHERRY
	"RED",			// SKINCOLOR_RED
	"CRIMSON",		// SKINCOLOR_CRIMSON
	"MAROON",		// SKINCOLOR_MAROON
	"LEMONADE",		// SKINCOLOR_LEMONADE
	"FLAME",		// SKINCOLOR_FLAME
	"SCARLET",		// SKINCOLOR_SCARLET
	"KETCHUP",		// SKINCOLOR_KETCHUP
	"DAWN",			// SKINCOLOR_DAWN
	"SUNSET",		// SKINCOLOR_SUNSET
	"CREAMSICLE",	// SKINCOLOR_CREAMSICLE
	"ORANGE",		// SKINCOLOR_ORANGE
	"PUMPKIN",		// SKINCOLOR_PUMPKIN
	"ROSEWOOD",		// SKINCOLOR_ROSEWOOD
	"BURGUNDY",		// SKINCOLOR_BURGUNDY
	"TANGERINE",	// SKINCOLOR_TANGERINE
	"PEACH",		// SKINCOLOR_PEACH
	"CARAMEL",		// SKINCOLOR_CARAMEL
	"CREAM",		// SKINCOLOR_CREAM
	"GOLD",			// SKINCOLOR_GOLD
	"ROYAL",		// SKINCOLOR_ROYAL
	"BRONZE",		// SKINCOLOR_BRONZE
	"COPPER",		// SKINCOLOR_COPPER
	"QUARRY",		// SKINCOLOR_QUARRY
	"YELLOW",		// SKINCOLOR_YELLOW
	"MUSTARD",		// SKINCOLOR_MUSTARD
	"CROCODILE",	// SKINCOLOR_CROCODILE
	"OLIVE",		// SKINCOLOR_OLIVE
	"VOMIT",		// SKINCOLOR_VOMIT
	"GARDEN",		// SKINCOLOR_GARDEN
	"LIME",			// SKINCOLOR_LIME
	"HANDHELD",		// SKINCOLOR_HANDHELD
	"TEA",			// SKINCOLOR_TEA
	"PISTACHIO",	// SKINCOLOR_PISTACHIO
	"MOSS",			// SKINCOLOR_MOSS
	"CAMOUFLAGE",	// SKINCOLOR_CAMOUFLAGE
	"ROBOHOOD",		// SKINCOLOR_ROBOHOOD
	"MINT",			// SKINCOLOR_MINT
	"GREEN",		// SKINCOLOR_GREEN
	"PINETREE",		// SKINCOLOR_PINETREE
	"EMERALD",		// SKINCOLOR_EMERALD
	"SWAMP",		// SKINCOLOR_SWAMP
	"DREAM",		// SKINCOLOR_DREAM
	"PLAGUE",		// SKINCOLOR_PLAGUE
	"ALGAE",		// SKINCOLOR_ALGAE
	"CARIBBEAN",	// SKINCOLOR_CARIBBEAN
	"AZURE",		// SKINCOLOR_AZURE
	"AQUA",			// SKINCOLOR_AQUA
	"TEAL",			// SKINCOLOR_TEAL
	"CYAN",			// SKINCOLOR_CYAN
	"JAWZ",			// SKINCOLOR_JAWZ
	"CERULEAN",		// SKINCOLOR_CERULEAN
	"NAVY",			// SKINCOLOR_NAVY
	"PLATINUM",		// SKINCOLOR_PLATINUM
	"SLATE",		// SKINCOLOR_SLATE
	"STEEL",		// SKINCOLOR_STEEL
	"THUNDER",		// SKINCOLOR_THUNDER
	"RUST",			// SKINCOLOR_RUST
	"WRISTWATCH",	// SKINCOLOR_WRISTWATCH
	"JET",			// SKINCOLOR_JET
	"SAPPHIRE",		// SKINCOLOR_SAPPHIRE
	"PERIWINKLE",	// SKINCOLOR_PERIWINKLE
	"BLUE",			// SKINCOLOR_BLUE
	"BLUEBERRY",	// SKINCOLOR_BLUEBERRY
	"NOVA",			// SKINCOLOR_NOVA
	"PASTEL",		// SKINCOLOR_PASTEL
	"MOONSLAM",		// SKINCOLOR_MOONSLAM
	"ULTRAVIOLET",	// SKINCOLOR_ULTRAVIOLET
	"DUSK",			// SKINCOLOR_DUSK
	"BUBBLEGUM",	// SKINCOLOR_BUBBLEGUM
	"PURPLE",		// SKINCOLOR_PURPLE
	"FUCHSIA",		// SKINCOLOR_FUCHSIA
	"TOXIC",		// SKINCOLOR_TOXIC
	"MAUVE",		// SKINCOLOR_MAUVE
	"LAVENDER",		// SKINCOLOR_LAVENDER
	"BYZANTIUM",	// SKINCOLOR_BYZANTIUM
	"POMEGRANATE",	// SKINCOLOR_POMEGRANATE
	"LILAC",		// SKINCOLOR_LILAC
	"BONE",         // SKINCOLOR_BONE
	"CARBON",       // SKINCOLOR_CARBON
	"INK",          // SKINCOLOR_INK
	"GHOST",        // SKINCOLOR_GHOST
	"MARBLE",       // SKINCOLOR_MARBLE
	"BLUEBELL",     // SKINCOLOR_BLUEBELL
	"CHOCOLATE",    // SKINCOLOR_CHOCOLATE
	"TAN",          // SKINCOLOR_TAN
	"PEACHY",       // SKINCOLOR_PEACHY
	"QUAIL",        // SKINCOLOR_QUAIL
	"LANTERN",      // SKINCOLOR_LANTERN
	"APRICOT",      // SKINCOLOR_APRICOT
	"SANDY",        // SKINCOLOR_SANDY
	"BANANA",       // SKINCOLOR_BANANA
	"SUNFLOWER",    // SKINCOLOR_SUNFLOWER
	"OLIVINE",      // SKINCOLOR_OLIVINE
	"PERIDOT",      // SKINCOLOR_PERIDOT
	"APPLE",        // SKINCOLOR_APPLE
	"SEAFOAM",      // SKINCOLOR_SEAFOAM
	"FOREST",       // SKINCOLOR_FOREST
	"TOPAZ",        // SKINCOLOR_TOPAZ
	"FROST",        // SKINCOLOR_FROST
	"WAVE",         // SKINCOLOR_WAVE
	"ICY",          // SKINCOLOR_ICY
	"PEACOCK",      // SKINCOLOR_PEACOCK
	"VAPOR",        // SKINCOLOR_VAPOR
	"GEMSTONE",     // SKINCOLOR_GEMSTONE
	"NEON",         // SKINCOLOR_NEON
	"PLUM",         // SKINCOLOR_PLUM
	"VIOLET",       // SKINCOLOR_VIOLET
	"MAGENTA",      // SKINCOLOR_MAGENTA
	"THISTLE",      // SKINCOLOR_THISTLE
	"DIAMOND",      // SKINCOLOR_DIAMOND
	"RAVEN",        // SKINCOLOR_RAVEN
	"MUD",          // SKINCOLOR_MUD
	"EARTHWORM",    // SKINCOLOR_EARTHWORM
	"YOGURT",       // SKINCOLOR_YOGURT
	"PEARL",        // SKINCOLOR_PEARL
	"STRAWBERRY",   // SKINCOLOR_STRAWBERRY
	"SODA",         // SKINCOLOR_SODA
	"BLOODCELL",    // SKINCOLOR_BLOODCELL
	"MAHOGANY",     // SKINCOLOR_MAHOGANY
	"FIERY",        // SKINCOLOR_FIERY
	"SPICE",        // SKINCOLOR_SPICE
	"KING",         // SKINCOLOR_KING
	"HOTDOG",       // SKINCOLOR_HOTDOG
	"CARNATION",    // SKINCOLOR_CARNATION
	"CANDY",        // SKINCOLOR_CANDY
	"NEBULA",       // SKINCOLOR_NEBULA
	"STEAMPUNK",    // SKINCOLOR_STEAMPUNK
	"AMBER",        // SKINCOLOR_AMBER
	"CARROT",       // SKINCOLOR_CARROT
	"CHEESE",       // SKINCOLOR_CHEESE
	"DUNE",         // SKINCOLOR_DUNE
	"BRASS",        // SKINCOLOR_BRASS
	"CITRINE",      // SKINCOLOR_CITRINE
	"LEMON",        // SKINCOLOR_LEMON
	"CASKET",       // SKINCOLOR_CASKET
	"KHAKI",        // SKINCOLOR_KHAKI
	"LIGHT",        // SKINCOLOR_LIGHT
	"PEPPERMINT",   // SKINCOLOR_PEPPERMINT
	"LASER",        // SKINCOLOR_LASER
	"ASPARAGUS",    // SKINCOLOR_ASPARAGUS
	"ARMY",         // SKINCOLOR_ARMY
	"CROW",         // SKINCOLOR_CROW
	"CHARTEUSE",    // SKINCOLOR_CHARTEUSE
	"SLIME",        // SKINCOLOR_SLIME
	"LEAF",         // SKINCOLOR_LEAF
	"JUNGLE",       // SKINCOLOR_JUNGLE
	"EVERGREEN",    // SKINCOLOR_EVERGREEN
	"TROPIC",       // SKINCOLOR_TROPIC
	"IGUANA",       // SKINCOLOR_IGUANA
	"SPEARMINT",    // SKINCOLOR_SPEARMINT
	"PATINA",       // SKINCOLOR_PATINA
	"LAKESIDE",     // SKINCOLOR_LAKESIDE
	"ELECTRIC",     // SKINCOLOR_ELECTRIC
	"TURQUOISE",    // SKINCOLOR_TURQUOISE
	"PEGASUS",      // SKINCOLOR_PEGASUS
	"PLASMA",       // SKINCOLOR_PLASMA
	"COMET",        // SKINCOLOR_COMET
	"LIGHTNING",    // SKINCOLOR_LIGHTNING
	"VACATION",     // SKINCOLOR_VACATION
	"ULTRAMARINE",  // SKINCOLOR_ULTRAMARINE
	"DEPTHS",       // SKINCOLOR_DEPTHS
	"DIANNE",       // SKINCOLOR_DIANNE
	"EXOTIC",       // SKINCOLOR_EXOTIC
	"SNOW",         // SKINCOLOR_SNOW
	"MOON",         // SKINCOLOR_MOON
	"LUNAR",        // SKINCOLOR_LUNAR
	"ONYX",         // SKINCOLOR_ONYX
	"LAPIS",        // SKINCOLOR_LAPIS
	"ORCA",         // SKINCOLOR_ORCA
	"STORM",        // SKINCOLOR_STORM
	"MIDNIGHT",     // SKINCOLOR_MIDNIGHT
	"COTTONCANDY",  // SKINCOLOR_COTTONCANDY
	"CYBER",        // SKINCOLOR_CYBER
	"AMETHYST",     // SKINCOLOR_AMETHYST
	"IRIS",         // SKINCOLOR_IRIS
	"GOTHIC",       // SKINCOLOR_GOTHIC
	"GRAPE",        // SKINCOLOR_GRAPE
	"INDIGO",       // SKINCOLOR_INDIGO
	"SAKURA",       // SKINCOLOR_SAKURA
	"DISCO",        // SKINCOLOR_DISCO
	"MULBERRY",     // SKINCOLOR_MULBERRY
	"BOYSENBERRY",  // SKINCOLOR_BOYSENBERRY
	"MYSTIC",       // SKINCOLOR_MYSTIC
	"WICKED",        // SKINCOLOR_WICKED

	// Special super colors
	// Super Sonic Yellow
	"SUPER1",		// SKINCOLOR_SUPER1
	"SUPER2",		// SKINCOLOR_SUPER2,
	"SUPER3",		// SKINCOLOR_SUPER3,
	"SUPER4",		// SKINCOLOR_SUPER4,
	"SUPER5",		// SKINCOLOR_SUPER5,

	// Super Tails Orange
	"TSUPER1",		// SKINCOLOR_TSUPER1,
	"TSUPER2",		// SKINCOLOR_TSUPER2,
	"TSUPER3",		// SKINCOLOR_TSUPER3,
	"TSUPER4",		// SKINCOLOR_TSUPER4,
	"TSUPER5",		// SKINCOLOR_TSUPER5,

	// Super Knuckles Red
	"KSUPER1",		// SKINCOLOR_KSUPER1,
	"KSUPER2",		// SKINCOLOR_KSUPER2,
	"KSUPER3",		// SKINCOLOR_KSUPER3,
	"KSUPER4",		// SKINCOLOR_KSUPER4,
	"KSUPER5",		// SKINCOLOR_KSUPER5,

	// Hyper Sonic Pink
	"PSUPER1",		// SKINCOLOR_PSUPER1,
	"PSUPER2",		// SKINCOLOR_PSUPER2,
	"PSUPER3",		// SKINCOLOR_PSUPER3,
	"PSUPER4",		// SKINCOLOR_PSUPER4,
	"PSUPER5",		// SKINCOLOR_PSUPER5,

	// Hyper Sonic Blue
	"BSUPER1",		// SKINCOLOR_BSUPER1,
	"BSUPER2",		// SKINCOLOR_BSUPER2,
	"BSUPER3",		// SKINCOLOR_BSUPER3,
	"BSUPER4",		// SKINCOLOR_BSUPER4,
	"BSUPER5",		// SKINCOLOR_BSUPER5,

	// Aqua Super
	"ASUPER1",		// SKINCOLOR_ASUPER1,
	"ASUPER2",		// SKINCOLOR_ASUPER2,
	"ASUPER3",		// SKINCOLOR_ASUPER3,
	"ASUPER4",		// SKINCOLOR_ASUPER4,
	"ASUPER5",		// SKINCOLOR_ASUPER5,

	// Hyper Sonic Green
	"GSUPER1",		// SKINCOLOR_GSUPER1,
	"GSUPER2",		// SKINCOLOR_GSUPER2,
	"GSUPER3",		// SKINCOLOR_GSUPER3,
	"GSUPER4",		// SKINCOLOR_GSUPER4,
	"GSUPER5",		// SKINCOLOR_GSUPER5,

	// Hyper Sonic White
	"WSUPER1",		// SKINCOLOR_WSUPER1,
	"WSUPER2",		// SKINCOLOR_WSUPER2,
	"WSUPER3",		// SKINCOLOR_WSUPER3,
	"WSUPER4",		// SKINCOLOR_WSUPER4,
	"WSUPER5",		// SKINCOLOR_WSUPER5,

	// Creamy Super (Shadow?)
	"CSUPER1",		// SKINCOLOR_CSUPER1,
	"CSUPER2",		// SKINCOLOR_CSUPER2,
	"CSUPER3",		// SKINCOLOR_CSUPER3,
	"CSUPER4",		// SKINCOLOR_CSUPER4,
	"CSUPER5"		// SKINCOLOR_CSUPER5,
};

static const char *const POWERS_LIST[] = {
	"INVULNERABILITY",
	"SNEAKERS",
	"FLASHING",
	"SHIELD",
	"TAILSFLY", // tails flying
	"UNDERWATER", // underwater timer
	"SPACETIME", // In space, no one can hear you spin!
	"EXTRALIFE", // Extra Life timer

	"SUPER", // Are you super?
	"GRAVITYBOOTS", // gravity boots

	// Weapon ammunition
	"INFINITYRING",
	"AUTOMATICRING",
	"BOUNCERING",
	"SCATTERRING",
	"GRENADERING",
	"EXPLOSIONRING",
	"RAILRING",

	// Power Stones
	"EMERALDS", // stored like global 'emeralds' variable

	// NiGHTS powerups
	"NIGHTS_SUPERLOOP",
	"NIGHTS_HELPER",
	"NIGHTS_LINKFREEZE",

	//for linedef exec 427
	"NOCONTROL",
	"INGOOP" // In goop
};

static const char *const KARTSTUFF_LIST[] = {
	"POSITION",
	"OLDPOSITION",
	"POSITIONDELAY",
	"PREVCHECK",
	"NEXTCHECK",
	"WAYPOINT",
	"STARPOSTWP",
	"STARPOSTFLIP",
	"RESPAWN",
	"DROPDASH",

	"THROWDIR",
	"LAPANIMATION",
	"LAPHAND",
	"CARDANIMATION",
	"VOICES",
	"TAUNTVOICES",
	"INSTASHIELD",
	"ENGINESND",

	"FLOORBOOST",
	"SPINOUTTYPE",

	"DRIFT",
	"DRIFTEND",
	"DRIFTCHARGE",
	"DRIFTBOOST",
	"BOOSTCHARGE",
	"STARTBOOST",
	"JMP",
	"OFFROAD",
	"POGOSPRING",
	"BRAKESTOP",
	"WATERSKIP",
	"DASHPADCOOLDOWN",
	"BOOSTPOWER",
	"SPEEDBOOST",
	"ACCELBOOST",
	"BOOSTANGLE",
	"BOOSTCAM",
	"DESTBOOSTCAM",
	"TIMEOVERCAM",
	"AIZDRIFTSTRAT",
	"BRAKEDRIFT",

	"ITEMROULETTE",
	"ROULETTETYPE",

	"ITEMTYPE",
	"ITEMAMOUNT",
	"ITEMHELD",
	"HOLDREADY",

	"CURSHIELD",
	"HYUDOROTIMER",
	"STEALINGTIMER",
	"STOLENTIMER",
	"SNEAKERTIMER",
	"GROWSHRINKTIMER",
	"SQUISHEDTIMER",
	"ROCKETSNEAKERTIMER",
	"INVINCIBILITYTIMER",
	"BUBBLECOOL",
	"BUBBLEBLOWUP",
	"FLAMEDASH",
	"EGGMANHELD",
	"EGGMANEXPLODE",
	"EGGMANBLAME",
	"LASTJAWZTARGET",
	"BANANADRAG",
	"SPINOUTTIMER",
	"WIPEOUTSLOW",
	"JUSTBUMPED",
	"COMEBACKTIMER",
	"SADTIMER",

	"BUMPER",
	"COMEBACKPOINTS",
	"COMEBACKMODE",
	"WANTED",
	"YOUGOTEM",

	"ITEMBLINK",
	"ITEMBLINKMODE",
	"GETSPARKS",
	"JAWZTARGETDELAY",
	"SPECTATEWAIT",
	"GROWCANCEL",
	"SNEAKERSTACK",
	"DRIFTSTACK",
	"STARTSTACK",
	"INVINCIBILITYSTACK",
	"SSSTACK",
	"TRICKSTACK",
	"TOTALSTACKS",
	"SSSPEEDBOOST",
	"SSACCELBOOST",
	"SLOPESPEEDBOOST",
	"SLOPEACCELBOOST",
	"TRICKSPEEDBOOST",
	"TRICKACCELBOOST",
	"REALSNEAKERTIMER",
	"HPHEALTH",
	"PANELTIMER",
	"REALPANELTIMER",
	"PANELSTACK",
	"CHAINSOUND",
	"DRIFTLOCK"
	
};

static const char *const HUDITEMS_LIST[] = {
	"LIVESNAME",
	"LIVESPIC",
	"LIVESNUM",
	"LIVESX",

	"RINGS",
	"RINGSSPLIT",
	"RINGSNUM",
	"RINGSNUMSPLIT",

	"SCORE",
	"SCORENUM",

	"TIME",
	"TIMESPLIT",
	"MINUTES",
	"MINUTESSPLIT",
	"TIMECOLON",
	"TIMECOLONSPLIT",
	"SECONDS",
	"SECONDSSPLIT",
	"TIMETICCOLON",
	"TICS",

	"SS_TOTALRINGS",
	"SS_TOTALRINGS_SPLIT",

	"GETRINGS",
	"GETRINGSNUM",
	"TIMELEFT",
	"TIMELEFTNUM",
	"TIMEUP",
	"HUNTPICS",
	"GRAVBOOTSICO",
	"LAP"
};

struct {
	const char *n;
	// has to be able to hold both fixed_t and angle_t, so drastic measure!!
	lua_Integer v;
} const INT_CONST[] = {
	// If a mod removes some variables here,
	// please leave the names in-tact and just set
	// the value to 0 or something.

	// integer type limits, from doomtype.h
	// INT64 and UINT64 limits not included, they're too big for most purposes anyway
	// signed
	{"INT8_MIN",INT8_MIN},
	{"INT16_MIN",INT16_MIN},
	{"INT32_MIN",INT32_MIN},
	{"INT8_MAX",INT8_MAX},
	{"INT16_MAX",INT16_MAX},
	{"INT32_MAX",INT32_MAX},
	// unsigned
	{"UINT8_MAX",UINT8_MAX},
	{"UINT16_MAX",UINT16_MAX},
	{"UINT32_MAX",UINT32_MAX},

	// fixed_t constants, from m_fixed.h
	{"FRACUNIT",FRACUNIT},
	{"FRACBITS",FRACBITS},

	// doomdef.h constants
	{"TICRATE",TICRATE},
	{"MUSICRATE",MUSICRATE},
	{"RING_DIST",RING_DIST},
	{"PUSHACCEL",PUSHACCEL},
	{"MODID",MODID}, // I don't know, I just thought it would be cool for a wad to potentially know what mod it was loaded into.
	{"CODEBASE",CODEBASE}, // or what release of SRB2 this is.
	{"VERSION",VERSION}, // Grab the game's version!
	{"SUBVERSION",SUBVERSION}, // more precise version number

	// Special linedef executor tag numbers!
	{"LE_PINCHPHASE",LE_PINCHPHASE}, // A boss entered pinch phase (and, in most cases, is preparing their pinch phase attack!)
	{"LE_ALLBOSSESDEAD",LE_ALLBOSSESDEAD}, // All bosses in the map are dead (Egg capsule raise)
	{"LE_BOSSDEAD",LE_BOSSDEAD}, // A boss in the map died (Chaos mode boss tally)
	{"LE_BOSS4DROP",LE_BOSS4DROP}, // CEZ boss dropped its cage
	{"LE_BRAKVILEATACK",LE_BRAKVILEATACK}, // Brak's doing his LOS attack, oh noes

	/// \todo Get all this stuff into its own sections, maybe. Maybe.

	// Frame settings
	{"FF_FRAMEMASK",FF_FRAMEMASK},
	{"FF_HORIZONTALFLIP",FF_HORIZONTALFLIP},
	{"FF_PAPERSPRITE",FF_PAPERSPRITE},
	{"FF_ANIMATE",FF_ANIMATE},
	{"FF_FULLBRIGHT",FF_FULLBRIGHT},
	{"FF_TRANSMASK",FF_TRANSMASK},
	{"FF_TRANSSHIFT",FF_TRANSSHIFT},
	// new preshifted translucency (used in source)
	{"FF_TRANS10",FF_TRANS10},
	{"FF_TRANS20",FF_TRANS20},
	{"FF_TRANS30",FF_TRANS30},
	{"FF_TRANS40",FF_TRANS40},
	{"FF_TRANS50",FF_TRANS50},
	{"FF_TRANS60",FF_TRANS60},
	{"FF_TRANS70",FF_TRANS70},
	{"FF_TRANS80",FF_TRANS80},
	{"FF_TRANS90",FF_TRANS90},
	// compatibility
	// Transparency for SOCs is pre-shifted
	{"TR_TRANS10",tr_trans10<<FF_TRANSSHIFT},
	{"TR_TRANS20",tr_trans20<<FF_TRANSSHIFT},
	{"TR_TRANS30",tr_trans30<<FF_TRANSSHIFT},
	{"TR_TRANS40",tr_trans40<<FF_TRANSSHIFT},
	{"TR_TRANS50",tr_trans50<<FF_TRANSSHIFT},
	{"TR_TRANS60",tr_trans60<<FF_TRANSSHIFT},
	{"TR_TRANS70",tr_trans70<<FF_TRANSSHIFT},
	{"TR_TRANS80",tr_trans80<<FF_TRANSSHIFT},
	{"TR_TRANS90",tr_trans90<<FF_TRANSSHIFT},
	// Transparency for Lua is not, unless capitalized as above.
	{"tr_trans10",tr_trans10},
	{"tr_trans20",tr_trans20},
	{"tr_trans30",tr_trans30},
	{"tr_trans40",tr_trans40},
	{"tr_trans50",tr_trans50},
	{"tr_trans60",tr_trans60},
	{"tr_trans70",tr_trans70},
	{"tr_trans80",tr_trans80},
	{"tr_trans90",tr_trans90},
	{"NUMTRANSMAPS",NUMTRANSMAPS},

	// Type of levels
	{"TOL_SP",TOL_SP},
	{"TOL_COOP",TOL_COOP},
	{"TOL_COMPETITION",TOL_COMPETITION},
	{"TOL_RACE",TOL_RACE},
	{"TOL_MATCH",TOL_MATCH},
	{"TOL_TAG",TOL_TAG},
	{"TOL_CTF",TOL_CTF},
	{"TOL_CUSTOM",TOL_CUSTOM},
	{"TOL_2D",TOL_2D},
	{"TOL_MARIO",TOL_MARIO},
	{"TOL_NIGHTS",TOL_NIGHTS},
	{"TOL_TV",TOL_TV},
	{"TOL_XMAS",TOL_XMAS},
	//{"TOL_KART",TOL_KART},

	// Level flags
	{"LF_SCRIPTISFILE",LF_SCRIPTISFILE},
	{"LF_SPEEDMUSIC",LF_SPEEDMUSIC},
	{"LF_NOSSMUSIC",LF_NOSSMUSIC},
	{"LF_NORELOAD",LF_NORELOAD},
	{"LF_NOZONE",LF_NOZONE},
	{"LF_SECTIONRACE",LF_SECTIONRACE},
	// And map flags
	{"LF2_HIDEINMENU",LF2_HIDEINMENU},
	{"LF2_HIDEINSTATS",LF2_HIDEINSTATS},
	{"LF2_RECORDATTACK",LF2_RECORDATTACK},
	{"LF2_NIGHTSATTACK",LF2_NIGHTSATTACK},
	{"LF2_NOVISITNEEDED",LF2_NOVISITNEEDED},

	// Save override
	{"SAVE_NEVER",SAVE_NEVER},
	{"SAVE_DEFAULT",SAVE_DEFAULT},
	{"SAVE_ALWAYS",SAVE_ALWAYS},

	// NiGHTS grades
	{"GRADE_F",GRADE_F},
	{"GRADE_E",GRADE_E},
	{"GRADE_D",GRADE_D},
	{"GRADE_C",GRADE_C},
	{"GRADE_B",GRADE_B},
	{"GRADE_A",GRADE_A},
	{"GRADE_S",GRADE_S},

	// Emeralds
	{"EMERALD1",EMERALD1},
	{"EMERALD2",EMERALD2},
	{"EMERALD3",EMERALD3},
	{"EMERALD4",EMERALD4},
	{"EMERALD5",EMERALD5},
	{"EMERALD6",EMERALD6},
	{"EMERALD7",EMERALD7},

	// SKINCOLOR_ doesn't include these..!
	{"MAXSKINCOLORS",MAXSKINCOLORS},
	{"MAXTRANSLATIONS",MAXTRANSLATIONS},

	// Precipitation
	{"PRECIP_NONE",PRECIP_NONE},
	{"PRECIP_STORM",PRECIP_STORM},
	{"PRECIP_SNOW",PRECIP_SNOW},
	{"PRECIP_RAIN",PRECIP_RAIN},
	{"PRECIP_BLANK",PRECIP_BLANK},
	{"PRECIP_STORM_NORAIN",PRECIP_STORM_NORAIN},
	{"PRECIP_STORM_NOSTRIKES",PRECIP_STORM_NOSTRIKES},

	// Shields
	// These ones use the lower 8 bits
	{"SH_NONE",SH_NONE},
	{"SH_JUMP",SH_JUMP},
	{"SH_ATTRACT",SH_ATTRACT},
	{"SH_ELEMENTAL",SH_ELEMENTAL},
	{"SH_BOMB",SH_BOMB},
	{"SH_BUBBLEWRAP",SH_BUBBLEWRAP},
	{"SH_THUNDERCOIN",SH_THUNDERCOIN},
	{"SH_FLAMEAURA",SH_FLAMEAURA},
	{"SH_PITY",SH_PITY},
	// These ones are special and use the upper bits
	{"SH_FIREFLOWER",SH_FIREFLOWER}, // Lower bits are a normal shield stacked on top of the fire flower
	{"SH_FORCE",SH_FORCE}, // Lower bits are how many hits left, 0 is the last hit
	// Stack masks
	{"SH_STACK",SH_STACK},
	{"SH_NOSTACK",SH_NOSTACK},

	// Ring weapons (ringweapons_t)
	// Useful for A_GiveWeapon
	{"RW_AUTO",RW_AUTO},
	{"RW_BOUNCE",RW_BOUNCE},
	{"RW_SCATTER",RW_SCATTER},
	{"RW_GRENADE",RW_GRENADE},
	{"RW_EXPLODE",RW_EXPLODE},
	{"RW_RAIL",RW_RAIL},

	// Character flags (skinflags_t)
	{"SF_HIRES",SF_HIRES},

	// Sound flags
	{"SF_TOTALLYSINGLE",SF_TOTALLYSINGLE},
	{"SF_NOMULTIPLESOUND",SF_NOMULTIPLESOUND},
	{"SF_OUTSIDESOUND",SF_OUTSIDESOUND},
	{"SF_X4AWAYSOUND",SF_X4AWAYSOUND},
	{"SF_X8AWAYSOUND",SF_X8AWAYSOUND},
	{"SF_NOINTERRUPT",SF_NOINTERRUPT},
	{"SF_X2AWAYSOUND",SF_X2AWAYSOUND},

	// p_local.h constants
	{"FLOATSPEED",FLOATSPEED},
	{"MAXSTEPMOVE",MAXSTEPMOVE},
	{"USERANGE",USERANGE},
	{"MELEERANGE",MELEERANGE},
	{"MISSILERANGE",MISSILERANGE},
	{"ONFLOORZ",ONFLOORZ}, // INT32_MIN
	{"ONCEILINGZ",ONCEILINGZ}, //INT32_MAX
	// for P_FlashPal
	{"PAL_WHITE",PAL_WHITE},
	{"PAL_MIXUP",PAL_MIXUP},
	{"PAL_RECYCLE",PAL_RECYCLE},
	{"PAL_NUKE",PAL_NUKE},

	// Gametypes, for use with global var "gametype"
	{"GT_RACE",GT_RACE},
	{"GT_MATCH",GT_MATCH},

	// Player state (playerstate_t)
	{"PST_LIVE",PST_LIVE}, // Playing or camping.
	{"PST_DEAD",PST_DEAD}, // Dead on the ground, view follows killer.
	{"PST_REBORN",PST_REBORN}, // Ready to restart/respawn???

	// Player animation (panim_t)
	{"PA_ETC",PA_ETC},
	{"PA_IDLE",PA_IDLE},
	{"PA_WALK",PA_WALK},
	{"PA_RUN",PA_RUN},
	{"PA_ROLL",PA_ROLL},
	{"PA_FALL",PA_FALL},
	{"PA_ABILITY",PA_ABILITY},

	// Current weapon
	{"WEP_AUTO",WEP_AUTO},
	{"WEP_BOUNCE",WEP_BOUNCE},
	{"WEP_SCATTER",WEP_SCATTER},
	{"WEP_GRENADE",WEP_GRENADE},
	{"WEP_EXPLODE",WEP_EXPLODE},
	{"WEP_RAIL",WEP_RAIL},
	{"NUM_WEAPONS",NUM_WEAPONS},

	// Got Flags, for player->gotflag!
	// Used to be MF_ for some stupid reason, now they're GF_ to stop them looking like mobjflags
	{"GF_REDFLAG",GF_REDFLAG},
	{"GF_BLUEFLAG",GF_BLUEFLAG},

	// Customisable sounds for Skins, from sounds.h
	{"SKSSPIN",SKSSPIN},
	{"SKSPUTPUT",SKSPUTPUT},
	{"SKSPUDPUD",SKSPUDPUD},
	{"SKSPLPAN1",SKSPLPAN1}, // Ouchies
	{"SKSPLPAN2",SKSPLPAN2},
	{"SKSPLPAN3",SKSPLPAN3},
	{"SKSPLPAN4",SKSPLPAN4},
	{"SKSPLDET1",SKSPLDET1}, // Deaths
	{"SKSPLDET2",SKSPLDET2},
	{"SKSPLDET3",SKSPLDET3},
	{"SKSPLDET4",SKSPLDET4},
	{"SKSPLVCT1",SKSPLVCT1}, // Victories
	{"SKSPLVCT2",SKSPLVCT2},
	{"SKSPLVCT3",SKSPLVCT3},
	{"SKSPLVCT4",SKSPLVCT4},
	{"SKSTHOK",SKSTHOK},
	{"SKSSPNDSH",SKSSPNDSH},
	{"SKSZOOM",SKSZOOM},
	{"SKSSKID",SKSSKID},
	{"SKSGASP",SKSGASP},
	{"SKSJUMP",SKSJUMP},
	// SRB2kart
	{"SKSKWIN",SKSKWIN}, // Win quote
	{"SKSKLOSE",SKSKLOSE}, // Lose quote
	{"SKSKPAN1",SKSKPAN1}, // Pain
	{"SKSKPAN2",SKSKPAN2},
	{"SKSKATK1",SKSKATK1}, // Offense item taunt
	{"SKSKATK2",SKSKATK2},
	{"SKSKBST1",SKSKBST1}, // Boost item taunt
	{"SKSKBST2",SKSKBST2},
	{"SKSKSLOW",SKSKSLOW}, // Overtake taunt
	{"SKSKHITM",SKSKHITM}, // Hit confirm taunt
	{"SKSKPOWR",SKSKPOWR}, // Power item taunt

	// 3D Floor/Fake Floor/FOF/whatever flags
	{"FF_EXISTS",FF_EXISTS},                   ///< Always set, to check for validity.
	{"FF_BLOCKPLAYER",FF_BLOCKPLAYER},         ///< Solid to player, but nothing else
	{"FF_BLOCKOTHERS",FF_BLOCKOTHERS},         ///< Solid to everything but player
	{"FF_SOLID",FF_SOLID},                     ///< Clips things.
	{"FF_RENDERSIDES",FF_RENDERSIDES},         ///< Renders the sides.
	{"FF_RENDERPLANES",FF_RENDERPLANES},       ///< Renders the floor/ceiling.
	{"FF_RENDERALL",FF_RENDERALL},             ///< Renders everything.
	{"FF_SWIMMABLE",FF_SWIMMABLE},             ///< Is a water block.
	{"FF_NOSHADE",FF_NOSHADE},                 ///< Messes with the lighting?
	{"FF_CUTSOLIDS",FF_CUTSOLIDS},             ///< Cuts out hidden solid pixels.
	{"FF_CUTEXTRA",FF_CUTEXTRA},               ///< Cuts out hidden translucent pixels.
	{"FF_CUTLEVEL",FF_CUTLEVEL},               ///< Cuts out all hidden pixels.
	{"FF_CUTSPRITES",FF_CUTSPRITES},           ///< Final step in making 3D water.
	{"FF_BOTHPLANES",FF_BOTHPLANES},           ///< Renders both planes all the time.
	{"FF_EXTRA",FF_EXTRA},                     ///< Gets cut by ::FF_CUTEXTRA.
	{"FF_TRANSLUCENT",FF_TRANSLUCENT},         ///< See through!
	{"FF_FOG",FF_FOG},                         ///< Fog "brush."
	{"FF_INVERTPLANES",FF_INVERTPLANES},       ///< Reverse the plane visibility rules.
	{"FF_ALLSIDES",FF_ALLSIDES},               ///< Render inside and outside sides.
	{"FF_INVERTSIDES",FF_INVERTSIDES},         ///< Only render inside sides.
	{"FF_DOUBLESHADOW",FF_DOUBLESHADOW},       ///< Make two lightlist entries to reset light?
	{"FF_FLOATBOB",FF_FLOATBOB},               ///< Floats on water and bobs if you step on it.
	{"FF_NORETURN",FF_NORETURN},               ///< Used with ::FF_CRUMBLE. Will not return to its original position after falling.
	{"FF_CRUMBLE",FF_CRUMBLE},                 ///< Falls 2 seconds after being stepped on, and randomly brings all touching crumbling 3dfloors down with it, providing their master sectors share the same tag (allows crumble platforms above or below, to also exist).
	{"FF_SHATTERBOTTOM",FF_SHATTERBOTTOM},     ///< Used with ::FF_BUSTUP. Like FF_SHATTER, but only breaks from the bottom. Good for springing up through rubble.
	{"FF_MARIO",FF_MARIO},                     ///< Acts like a question block when hit from underneath. Goodie spawned at top is determined by master sector.
	{"FF_BUSTUP",FF_BUSTUP},                   ///< You can spin through/punch this block and it will crumble!
	{"FF_QUICKSAND",FF_QUICKSAND},             ///< Quicksand!
	{"FF_PLATFORM",FF_PLATFORM},               ///< You can jump up through this to the top.
	{"FF_REVERSEPLATFORM",FF_REVERSEPLATFORM}, ///< A fall-through floor in normal gravity, a platform in reverse gravity.
	{"FF_INTANGABLEFLATS",FF_INTANGABLEFLATS}, ///< Both flats are intangable, but the sides are still solid.
	{"FF_SHATTER",FF_SHATTER},                 ///< Used with ::FF_BUSTUP. Thinks everyone's Knuckles.
	{"FF_SPINBUST",FF_SPINBUST},               ///< Used with ::FF_BUSTUP. Jump or fall onto it while curled in a ball.
	{"FF_ONLYKNUX",FF_ONLYKNUX},               ///< Used with ::FF_BUSTUP. Only Knuckles can break this rock.
	{"FF_RIPPLE",FF_RIPPLE},                   ///< Ripple the flats
	{"FF_COLORMAPONLY",FF_COLORMAPONLY},       ///< Only copy the colormap, not the lightlevel
	{"FF_GOOWATER",FF_GOOWATER},               ///< Used with ::FF_SWIMMABLE. Makes thick bouncey goop.

	// Slope flags
	{"SL_NOPHYSICS",SL_NOPHYSICS},      // Don't do momentum adjustment with this slope
	{"SL_NODYNAMIC",SL_NODYNAMIC},      // Slope will never need to move during the level, so don't fuss with recalculating it
	{"SL_ANCHORVERTEX",SL_ANCHORVERTEX},// Slope is using a Slope Vertex Thing to anchor its position
	{"SL_VERTEXSLOPE",SL_VERTEXSLOPE},  // Slope is built from three Slope Vertex Things

	// Angles
	{"ANG1",ANG1},
	{"ANG2",ANG2},
	{"ANG10",ANG10},
	{"ANG15",ANG15},
	{"ANG20",ANG20},
	{"ANG30",ANG30},
	{"ANG60",ANG60},
	{"ANG64h",ANG64h},
	{"ANG105",ANG105},
	{"ANG210",ANG210},
	{"ANG255",ANG255},
	{"ANG340",ANG340},
	{"ANG350",ANG350},
	{"ANGLE_11hh",ANGLE_11hh},
	{"ANGLE_22h",ANGLE_22h},
	{"ANGLE_45",ANGLE_45},
	{"ANGLE_67h",ANGLE_67h},
	{"ANGLE_90",ANGLE_90},
	{"ANGLE_112h",ANGLE_112h},
	{"ANGLE_135",ANGLE_135},
	{"ANGLE_157h",ANGLE_157h},
	{"ANGLE_180",ANGLE_180},
	{"ANGLE_202h",ANGLE_202h},
	{"ANGLE_225",ANGLE_225},
	{"ANGLE_247h",ANGLE_247h},
	{"ANGLE_270",ANGLE_270},
	{"ANGLE_292h",ANGLE_292h},
	{"ANGLE_315",ANGLE_315},
	{"ANGLE_337h",ANGLE_337h},
	{"ANGLE_MAX",ANGLE_MAX},

	// P_Chase directions (dirtype_t)
	{"DI_NODIR",DI_NODIR},
	{"DI_EAST",DI_EAST},
	{"DI_NORTHEAST",DI_NORTHEAST},
	{"DI_NORTH",DI_NORTH},
	{"DI_NORTHWEST",DI_NORTHWEST},
	{"DI_WEST",DI_WEST},
	{"DI_SOUTHWEST",DI_SOUTHWEST},
	{"DI_SOUTH",DI_SOUTH},
	{"DI_SOUTHEAST",DI_SOUTHEAST},
	{"NUMDIRS",NUMDIRS},

	// Buttons (ticcmd_t)	// SRB2kart
	{"BT_ACCELERATE",BT_ACCELERATE},
	{"BT_DRIFT",BT_DRIFT},
	{"BT_BRAKE",BT_BRAKE},
	{"BT_ATTACK",BT_ATTACK},
	{"BT_FORWARD",BT_FORWARD},
	{"BT_BACKWARD",BT_BACKWARD},
	{"BT_LOOKBACK",BT_LOOKBACK},
	{"BT_CUSTOM1",BT_CUSTOM1}, // Lua customizable
	{"BT_CUSTOM2",BT_CUSTOM2}, // Lua customizable
	{"BT_CUSTOM3",BT_CUSTOM3}, // Lua customizable

	// Lua command registration flags
	{"COM_ADMIN",COM_ADMIN},
	{"COM_SPLITSCREEN",COM_SPLITSCREEN},
	{"COM_LOCAL",COM_LOCAL},

	// cvflags_t
	{"CV_SAVE",CV_SAVE},
	{"CV_CALL",CV_CALL},
	{"CV_NETVAR",CV_NETVAR},
	{"CV_NOINIT",CV_NOINIT},
	{"CV_FLOAT",CV_FLOAT},
	{"CV_NOTINNET",CV_NOTINNET},
	{"CV_MODIFIED",CV_MODIFIED},
	{"CV_SHOWMODIF",CV_SHOWMODIF},
	{"CV_SHOWMODIFONETIME",CV_SHOWMODIFONETIME},
	{"CV_NOSHOWHELP",CV_NOSHOWHELP},
	{"CV_HIDEN",CV_HIDEN},
	{"CV_HIDDEN",CV_HIDEN},
	{"CV_CHEAT",CV_CHEAT},

	// v_video flags
	{"V_NOSCALEPATCH",V_NOSCALEPATCH},
	{"V_SMALLSCALEPATCH",V_SMALLSCALEPATCH},
	{"V_MEDSCALEPATCH",V_MEDSCALEPATCH},
	{"V_6WIDTHSPACE",V_6WIDTHSPACE},
	{"V_OLDSPACING",V_OLDSPACING},
	{"V_MONOSPACE",V_MONOSPACE},
	{"V_PURPLEMAP",V_PURPLEMAP},
	{"V_YELLOWMAP",V_YELLOWMAP},
	{"V_GREENMAP",V_GREENMAP},
	{"V_BLUEMAP",V_BLUEMAP},
	{"V_REDMAP",V_REDMAP},
	{"V_GRAYMAP",V_GRAYMAP},
	{"V_ORANGEMAP",V_ORANGEMAP},
	{"V_SKYMAP",V_SKYMAP},
	{"V_LAVENDERMAP",V_LAVENDERMAP},
	{"V_GOLDMAP",V_GOLDMAP},
	{"V_TEAMAP",V_TEAMAP},
	{"V_STEELMAP",V_STEELMAP},
	{"V_PINKMAP",V_PINKMAP},
	{"V_BROWNMAP",V_BROWNMAP},
	{"V_PEACHMAP",V_PEACHMAP},
	{"V_TRANSLUCENT",V_TRANSLUCENT},
	{"V_10TRANS",V_10TRANS},
	{"V_20TRANS",V_20TRANS},
	{"V_30TRANS",V_30TRANS},
	{"V_40TRANS",V_40TRANS},
	{"V_50TRANS",V_TRANSLUCENT}, // alias
	{"V_60TRANS",V_60TRANS},
	{"V_70TRANS",V_70TRANS},
	{"V_80TRANS",V_80TRANS},
	{"V_90TRANS",V_90TRANS},
	{"V_HUDTRANSHALF",V_HUDTRANSHALF},
	{"V_HUDTRANS",V_HUDTRANS},
	{"V_AUTOFADEOUT",V_AUTOFADEOUT},
	{"V_RETURN8",V_RETURN8},
	{"V_OFFSET",V_OFFSET},
	{"V_ALLOWLOWERCASE",V_ALLOWLOWERCASE},
	{"V_FLIP",V_FLIP},
	{"V_SNAPTOTOP",V_SNAPTOTOP},
	{"V_SNAPTOBOTTOM",V_SNAPTOBOTTOM},
	{"V_SNAPTOLEFT",V_SNAPTOLEFT},
	{"V_SNAPTORIGHT",V_SNAPTORIGHT},
	{"V_WRAPX",V_WRAPX},
	{"V_WRAPY",V_WRAPY},
	{"V_NOSCALESTART",V_NOSCALESTART},
	{"V_SPLITSCREEN",V_SPLITSCREEN},
	{"V_HORZSCREEN",V_HORZSCREEN},

	{"V_PARAMMASK",V_PARAMMASK},
	{"V_SCALEPATCHMASK",V_SCALEPATCHMASK},
	{"V_SPACINGMASK",V_SPACINGMASK},
	{"V_CHARCOLORMASK",V_CHARCOLORMASK},
	{"V_ALPHAMASK",V_ALPHAMASK},

	{"V_CHARCOLORSHIFT",V_CHARCOLORSHIFT},
	{"V_ALPHASHIFT",V_ALPHASHIFT},

	//Kick Reasons
	{"KR_KICK",KR_KICK},
	{"KR_PINGLIMIT",KR_PINGLIMIT},
	{"KR_SYNCH",KR_SYNCH},
	{"KR_TIMEOUT",KR_TIMEOUT},
	{"KR_BAN",KR_BAN},
	{"KR_LEAVE",KR_LEAVE},

	// SRB2Kart
	// kartitems_t
	{"KITEM_SAD",KITEM_SAD}, // Actual items (can be set for k_itemtype)
	{"KITEM_NONE",KITEM_NONE},
	{"KITEM_SNEAKER",KITEM_SNEAKER},
	{"KITEM_ROCKETSNEAKER",KITEM_ROCKETSNEAKER},
	{"KITEM_INVINCIBILITY",KITEM_INVINCIBILITY},
	{"KITEM_BANANA",KITEM_BANANA},
	{"KITEM_EGGMAN",KITEM_EGGMAN},
	{"KITEM_ORBINAUT",KITEM_ORBINAUT},
	{"KITEM_JAWZ",KITEM_JAWZ},
	{"KITEM_MINE",KITEM_MINE},
	{"KITEM_BALLHOG",KITEM_BALLHOG},
	{"KITEM_SPB",KITEM_SPB},
	{"KITEM_GROW",KITEM_GROW},
	{"KITEM_SHRINK",KITEM_SHRINK},
	{"KITEM_THUNDERSHIELD",KITEM_THUNDERSHIELD},
	{"KITEM_BUBBLESHIELD",KITEM_BUBBLESHIELD},
	{"KITEM_FLAMESHIELD",KITEM_FLAMESHIELD},
	{"KITEM_HYUDORO",KITEM_HYUDORO},
	{"KITEM_POGOSPRING",KITEM_POGOSPRING},
	{"KITEM_KITCHENSINK",KITEM_KITCHENSINK},
	{"NUMKARTITEMS",NUMKARTITEMS},
	{"KRITEM_TRIPLESNEAKER",KRITEM_TRIPLESNEAKER}, // Additional roulette IDs (not usable for much in Lua besides K_GetItemPatch)
	{"KRITEM_TRIPLEBANANA",KRITEM_TRIPLEBANANA},
	{"KRITEM_TENFOLDBANANA",KRITEM_TENFOLDBANANA},
	{"KRITEM_TRIPLEORBINAUT",KRITEM_TRIPLEORBINAUT},
	{"KRITEM_QUADORBINAUT",KRITEM_QUADORBINAUT},
	{"KRITEM_DUALJAWZ",KRITEM_DUALJAWZ},
	{"NUMKARTRESULTS",NUMKARTRESULTS},
	
	// kartshields_t
	{"KSHIELD_NONE",KSHIELD_NONE},
	{"KSHIELD_THUNDER",KSHIELD_THUNDER},
	{"KSHIELD_BUBBLE",KSHIELD_BUBBLE},
	{"KSHIELD_FLAME",KSHIELD_FLAME},
	{"NUMKARTSHIELDS",NUMKARTSHIELDS},

	// translation colormaps
	{"TC_DEFAULT",TC_DEFAULT},
	{"TC_BOSS",TC_BOSS},
	{"TC_METALSONIC",TC_METALSONIC},
	{"TC_ALLWHITE",TC_ALLWHITE},
	{"TC_RAINBOW",TC_RAINBOW},
	{"TC_BLINK",TC_BLINK},

	// Custom client features exposed to lua
	{"FEATURE_INTERMISSIONHUD",1},
	{"FEATURE_VOTEHUD",1},
	{"FEATURE_TITLEHUD",1},
	
	// followermode_t
	{"FOLLOWERMODE_FLOAT",FOLLOWERMODE_FLOAT},
	{"FOLLOWERMODE_GROUND",FOLLOWERMODE_GROUND},


	{NULL,0}
};

static mobjtype_t get_mobjtype(const char *word)
{ // Returns the vlaue of MT_ enumerations
	mobjtype_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("MT_",word,3))
		word += 3; // take off the MT_
	for (i = 0; i < NUMMOBJFREESLOTS; i++) {
		if (!FREE_MOBJS[i])
			break;
		if (fastcmp(word, FREE_MOBJS[i]))
			return MT_FIRSTFREESLOT+i;
	}
	for (i = 0; i < MT_FIRSTFREESLOT; i++)
		if (fastcmp(word, MOBJTYPE_LIST[i]+3))
			return i;
	deh_warning("Couldn't find mobjtype named 'MT_%s'",word);
	return MT_BLUECRAWLA;
}

static statenum_t get_state(const char *word)
{ // Returns the value of S_ enumerations
	statenum_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("S_",word,2))
		word += 2; // take off the S_
	for (i = 0; i < NUMSTATEFREESLOTS; i++) {
		if (!FREE_STATES[i])
			break;
		if (fastcmp(word, FREE_STATES[i]))
			return S_FIRSTFREESLOT+i;
	}
	for (i = 0; i < S_FIRSTFREESLOT; i++)
		if (fastcmp(word, STATE_LIST[i]+2))
			return i;
	deh_warning("Couldn't find state named 'S_%s'",word);
	return S_NULL;
}

static spritenum_t get_sprite(const char *word)
{ // Returns the value of SPR_ enumerations
	spritenum_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("SPR_",word,4))
		word += 4; // take off the SPR_
	for (i = 0; i < NUMSPRITES; i++)
		if (!sprnames[i][4] && memcmp(word,sprnames[i],4)==0)
			return i;
	deh_warning("Couldn't find sprite named 'SPR_%s'",word);
	return SPR_NULL;
}

static sfxenum_t get_sfx(const char *word)
{ // Returns the value of SFX_ enumerations
	sfxenum_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("SFX_",word,4))
		word += 4; // take off the SFX_
	else if (fastncmp("DS",word,2))
		word += 2; // take off the DS
	for (i = 0; i < NUMSFX; i++)
		if (S_sfx[i].name && fasticmp(word, S_sfx[i].name))
			return i;
	deh_warning("Couldn't find sfx named 'SFX_%s'",word);
	return sfx_None;
}

#ifdef MUSICSLOT_COMPATIBILITY
static UINT16 get_mus(const char *word, UINT8 dehacked_mode)
{ // Returns the value of MUS_ enumerations
	UINT16 i;
	char lumptmp[4];

	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (!word[2] && toupper(word[0]) >= 'A' && toupper(word[0]) <= 'Z')
		return (UINT16)M_MapNumber(word[0], word[1]);

	if (fastncmp("MUS_",word,4))
		word += 4; // take off the MUS_
	else if (fastncmp("O_",word,2) || fastncmp("D_",word,2))
		word += 2; // take off the O_ or D_

	strncpy(lumptmp, word, 4);
	lumptmp[3] = 0;
	if (fasticmp("MAP",lumptmp))
	{
		word += 3;
		if (toupper(word[0]) >= 'A' && toupper(word[0]) <= 'Z')
			return (UINT16)M_MapNumber(word[0], word[1]);
		else if ((i = atoi(word)))
			return i;

		word -= 3;
		if (dehacked_mode)
			deh_warning("Couldn't find music named 'MUS_%s'",word);
		return 0;
	}
	for (i = 0; compat_special_music_slots[i][0]; ++i)
		if (fasticmp(word, compat_special_music_slots[i]))
			return i + 1036;
	if (dehacked_mode)
		deh_warning("Couldn't find music named 'MUS_%s'",word);
	return 0;
}
#endif

static hudnum_t get_huditem(const char *word)
{ // Returns the value of HUD_ enumerations
	hudnum_t i;
	if (*word >= '0' && *word <= '9')
		return atoi(word);
	if (fastncmp("HUD_",word,4))
		word += 4; // take off the HUD_
	for (i = 0; i < NUMHUDITEMS; i++)
		if (fastcmp(word, HUDITEMS_LIST[i]))
			return i;
	deh_warning("Couldn't find huditem named 'HUD_%s'",word);
	return HUD_LIVESNAME;
}

// Loops through every constant and operation in word and performs its calculations, returning the final value.
fixed_t get_number(const char *word)
{
	return LUA_EvalMath(word);
}

void DEH_Check(void)
{
#if defined(_DEBUG) || defined(PARANOIA)
	const size_t dehstates = sizeof(STATE_LIST)/sizeof(const char*);
	const size_t dehmobjs  = sizeof(MOBJTYPE_LIST)/sizeof(const char*);
	const size_t dehpowers = sizeof(POWERS_LIST)/sizeof(const char*);
	const size_t dehkartstuff = sizeof(KARTSTUFF_LIST)/sizeof(const char*);
	const size_t dehcolors = sizeof(COLOR_ENUMS)/sizeof(const char*);

	if (dehstates != S_FIRSTFREESLOT)
		I_Error("You forgot to update the Dehacked states list, you dolt!\n(%d states defined, versus %s in the Dehacked list)\n", S_FIRSTFREESLOT, sizeu1(dehstates));

	if (dehmobjs != MT_FIRSTFREESLOT)
		I_Error("You forgot to update the Dehacked mobjtype list, you dolt!\n(%d mobj types defined, versus %s in the Dehacked list)\n", MT_FIRSTFREESLOT, sizeu1(dehmobjs));

	if (dehpowers != NUMPOWERS)
		I_Error("You forgot to update the Dehacked powers list, you dolt!\n(%d powers defined, versus %s in the Dehacked list)\n", NUMPOWERS, sizeu1(dehpowers));

	if (dehkartstuff != NUMKARTSTUFF)
		I_Error("You forgot to update the Dehacked powers list, you dolt!\n(%d kartstuff defined, versus %s in the Dehacked list)\n", NUMKARTSTUFF, sizeu1(dehkartstuff));

	if (dehcolors != MAXTRANSLATIONS)
		I_Error("You forgot to update the Dehacked colors list, you dolt!\n(%d colors defined, versus %s in the Dehacked list)\n", MAXTRANSLATIONS, sizeu1(dehcolors));
#endif
}

// freeslot takes a name (string only!)
// and allocates it to the appropriate free slot.
// Returns the slot number allocated for it or nil if failed.
// ex. freeslot("MT_MYTHING","S_MYSTATE1","S_MYSTATE2")
// TODO: Error checking! @.@; There's currently no way to know which ones failed and why!
//
static inline int lib_freeslot(lua_State *L)
{
	int n = lua_gettop(L);
	int r = 0; // args returned
	char *s, *type,*word;

  while (n-- > 0)
  {
		s = Z_StrDup(luaL_checkstring(L,1));
		type = strtok(s, "_");
		if (type)
			strupr(type);
		else {
			Z_Free(s);
			return luaL_error(L, "Unknown enum type in '%s'\n", luaL_checkstring(L, 1));
		}

		word = strtok(NULL, "\n");
		if (word)
			strupr(word);
		else {
			Z_Free(s);
			return luaL_error(L, "Missing enum name in '%s'\n", luaL_checkstring(L, 1));
		}
		if (fastcmp(type, "SFX")) {
			sfxenum_t sfx;
			strlwr(word);
			CONS_Printf("Sound sfx_%s allocated.\n",word);
			sfx = S_AddSoundFx(word, false, 0, false);
			if (sfx != sfx_None) {
				lua_pushinteger(L, sfx);
				r++;
			} else
				I_Error("Out of Sfx Freeslots while allocating \"%s\"\nLoad less addons to fix this.", word); //Should never get here since S_AddSoundFx was changed to throw I_Error when it can't allocate
		}
		else if (fastcmp(type, "SPR"))
		{
			char wad;
			spritenum_t j;
			lua_getfield(L, LUA_REGISTRYINDEX, "WAD");
			wad = (char)lua_tointeger(L, -1);
			lua_pop(L, 1);
			for (j = SPR_FIRSTFREESLOT; j <= SPR_LASTFREESLOT; j++)
			{
				if (used_spr[(j-SPR_FIRSTFREESLOT)/8] & (1<<(j%8)))
				{
					if (!sprnames[j][4] && memcmp(sprnames[j],word,4)==0)
						sprnames[j][4] = wad;
					continue; // Already allocated, next.
				}
				// Found a free slot!
				CONS_Printf("Sprite SPR_%s allocated.\n",word);

				lua_pushcfunction(L, lua_glib_invalidate_cache);
				lua_pushfstring(L, "SPR_%s", word);
				lua_call(L, 1, 0);

				strncpy(sprnames[j],word,4);
				//sprnames[j][4] = 0;
				used_spr[(j-SPR_FIRSTFREESLOT)/8] |= 1<<(j%8); // Okay, this sprite slot has been named now.
				lua_pushinteger(L, j);
				r++;
				break;
			}
			if (j > SPR_LASTFREESLOT)
				I_Error("Out of Sprite Freeslots while allocating \"%s\"\nLoad less addons to fix this.", word);
		}
		else if (fastcmp(type, "S"))
		{
			statenum_t i;
			for (i = 0; i < NUMSTATEFREESLOTS; i++)
				if (!FREE_STATES[i]) {
					CONS_Printf("State S_%s allocated.\n",word);

					lua_pushcfunction(L, lua_glib_invalidate_cache);
					lua_pushfstring(L, "S_%s", word);
					lua_call(L, 1, 0);

					FREE_STATES[i] = Z_Malloc(strlen(word)+1, PU_STATIC, NULL);
					strcpy(FREE_STATES[i],word);
					freeslotusage[0][0]++;
					lua_pushinteger(L, i);
					r++;
					break;
				}
			if (i == NUMSTATEFREESLOTS)
				I_Error("Out of State Freeslots while allocating \"%s\"\nLoad less addons to fix this.", word);
		}
		else if (fastcmp(type, "MT"))
		{
			mobjtype_t i;
			for (i = 0; i < NUMMOBJFREESLOTS; i++)
				if (!FREE_MOBJS[i]) {
					CONS_Printf("MobjType MT_%s allocated.\n",word);

					lua_pushcfunction(L, lua_glib_invalidate_cache);
					lua_pushfstring(L, "MT_%s", word);
					lua_call(L, 1, 0);

					FREE_MOBJS[i] = Z_Malloc(strlen(word)+1, PU_STATIC, NULL);
					strcpy(FREE_MOBJS[i],word);
					freeslotusage[1][0]++;
					lua_pushinteger(L, i);
					r++;
					break;
				}
			if (i == NUMMOBJFREESLOTS)
				I_Error("Out of Mobj Freeslots while allocating \"%s\"\nLoad less addons to fix this.", word);
		}
		Z_Free(s);
		lua_remove(L, 1);
		continue;
	}
	return r;
}

// Wrapper for ALL A_Action functions.
// Upvalue: actionf_t to represent
// Arguments: mobj_t actor, int var1, int var2
static inline int lib_action(lua_State *L)
{
	actionf_t *action = lua_touserdata(L,lua_upvalueindex(1));
	mobj_t *actor = *((mobj_t **)luaL_checkudata(L,1,META_MOBJ));
	var1 = (INT32)luaL_optinteger(L,2,0);
	var2 = (INT32)luaL_optinteger(L,3,0);
	if (!actor)
		return LUA_ErrInvalid(L, "mobj_t");
	action->acp1(actor);
	return 0;
}

// Hardcoded A_Action name to call for super() or NULL if super() would be invalid.
// Set in lua_infolib.
const char *superactions[MAXRECURSION];
UINT8 superstack = 0;

static int lib_dummysuper(lua_State *L)
{
	return luaL_error(L, "Can't call super() outside of hardcode-replacing A_Action functions being called by state changes!"); // convoluted, I know. @_@;;
}

FUNCINLINE static ATTRINLINE int lib_getenum(lua_State *L)
{
	const int UV_MATHLIB = lua_upvalueindex(1);
	const int GLIB_PROXY = lua_upvalueindex(2);

	const char *word;
	boolean mathlib = lua_toboolean(L, UV_MATHLIB);
	if (lua_type(L,2) != LUA_TSTRING)
		return 0;
	word = lua_tostring(L,2);

	/* First check actions, as they can be overridden. */
	if (!mathlib && fastncmp("A_",word,2)) {
		char *caps;
		// Try to get a Lua action first.
		/// \todo Push a closure that sets superactions[] and superstack.
		lua_getfield(L, LUA_REGISTRYINDEX, LREG_ACTIONS);
		// actions are stored in all uppercase.
		caps = Z_StrDup(word);
		strupr(caps);
		lua_getfield(L, -1, caps);
		Z_Free(caps);
		if (!lua_isnil(L, -1))
			return 1; // Success! :D That was easy.
		// Welp, that failed.
		lua_pop(L, 2); // pop nil and LREG_ACTIONS
		// Hardcoded actions are handled by the proxy.
	}

	/* Then check the globals proxy table. */
	lua_pushvalue(L, 2);
	lua_gettable(L, GLIB_PROXY);
	if (!lua_isnil(L, -1))
	{
		return 1;
	}

	if (mathlib) return luaL_error(L, "constant '%s' could not be parsed.\n", word);

	return 0;
}

static int lua_enumlib_basic_fallback(lua_State* L)
{
	const int NAME = lua_upvalueindex(1);

	if (lua_isnil(L, NAME))
	{
		lua_pushliteral(L, "enum");
		lua_insert(L, NAME);
	}

	lua_pushliteral(L, REQUIRE_MATHLIB_GUID);
	lua_gettable(L, LUA_REGISTRYINDEX);

	if (lua_toboolean(L,-1))
	{
		return luaL_error(L, "%s '%s' could not be found.\n", lua_tostring(L, NAME), lua_tostring(L, 1));
	}

	return 0;
}

static int lua_enumlib_mariomode_get(lua_State *L)
{
	lua_pushboolean(L, mariomode != 0);
	return 1;
}

static int lua_enumlib_twodlevel_get(lua_State *L)
{
	lua_pushboolean(L, twodlevel);
	return 1;
}

static int lua_enumlib_modifiedgame_get(lua_State *L)
{
	lua_pushboolean(L, modifiedgame && !savemoddata);
	return 1;
}

static int lua_enumlib_server_get(lua_State *L)
{
	if ((!multiplayer || !(netgame || demo.playback)) && !playeringame[serverplayer])
		return 0;
	LUA_PushUserdata(L, &players[serverplayer], META_PLAYER);
	return 1;
}

static int lua_enumlib_consoleplayer_get(lua_State *L)
{
	if (consoleplayer < 0 || !playeringame[consoleplayer])
		return 0;
	LUA_PushUserdata(L, &players[consoleplayer], META_PLAYER);
	return 1;
}

static int lua_enumlib_state_get(lua_State *L)
{
	// Linear search for the enum name.
	const char *s = lua_tostring(L, 1);
	for (int i = 0; i < NUMSTATEFREESLOTS && FREE_STATES[i]; i++)
	{
		if (fastcmp(s+2 /* Skip S_ */, FREE_STATES[i]))
		{
			lua_pushinteger(L, S_FIRSTFREESLOT+i);
			return 1;
		}
	}

	return luaL_error(L, "state '%s' does not exist.\n", s);
}

static int lua_enumlib_mobjtype_get(lua_State *L)
{
	const char *s = lua_tostring(L, 1);
	for (mobjtype_t i = 0; i < NUMMOBJFREESLOTS; i++)
	{
		if (!FREE_MOBJS[i])
			break;
		if (fastcmp(s+3, FREE_MOBJS[i]))
		{
			lua_pushinteger(L, MT_FIRSTFREESLOT+i);
			return 1;
		}
	}

	for (mobjtype_t i = 0; i < MT_FIRSTFREESLOT; i++)
	{
		if (fastcmp(s, MOBJTYPE_LIST[i]))
		{
			lua_pushinteger(L, i);
			return 1;
		}
	}

	return luaL_error(L, "mobjtype '%s' does not exist.\n", s);
}

static int lua_enumlib_sprite_get(lua_State *L)
{
	const char *s = lua_tostring(L, 1);
	for (int i = 0; i < NUMSPRITES; i++)
	{
		if (!sprnames[i][4] && fastncmp(s+4, sprnames[i], 4))
		{
			lua_pushinteger(L, i);
			return 1;
		}
	}

	lua_pushliteral(L, REQUIRE_MATHLIB_GUID);
	lua_gettable(L, LUA_REGISTRYINDEX);

	if (lua_toboolean(L,-1))
	{
		return luaL_error(L, "sprite '%s' could not be found.\n", lua_tostring(L, 1));
	}

	return 0;
}

static int lua_enumlib_sfx_get(lua_State* L)
{
	const char *sfx = lua_tostring(L, 1);
	for (int i = 0; i < NUMSFX; i++)
	{
		if (S_sfx[i].name && fastcmp(S_sfx[i].name, &sfx[4]))
		{
			lua_pushinteger(L, i);
			return 1;
		}
	}

	return 0;
}

static int lua_enumlib_sfx_get_uppercase(lua_State *L)
{
	const char *sfx = lua_tostring(L, 1);
	for (int i = 0; i < NUMSFX; i++)
	{
		if (S_sfx[i].name && fasticmp(S_sfx[i].name, &sfx[4]))
		{
			lua_pushinteger(L, i);
			return 1;
		}
	}

	return luaL_error(L, "sfx '%s' could not be found.", sfx);
}

static int lua_enumlib_sfx_get_ds(lua_State *L)
{
	const char *sfx = lua_tostring(L, 1);
	for (int i = 0; i < NUMSFX; i++)
	{
		if (S_sfx[i].name && fasticmp(S_sfx[i].name, &sfx[2]))
		{
			lua_pushinteger(L, i);
			return 1;
		}
	}

	return luaL_error(L, "sfx '%s' could not be found.", sfx);
}

#ifdef MUSICSLOT_COMPATIBILITY

	static int lua_enumlib_mus_get(lua_State *L)
	{
		const char *name = strchr(lua_tostring(L, 1), '_') + 1;
		int mus = get_mus(name, false);

		if (mus == 0)
			return 0;

		lua_pushinteger(L, mus);
		return 1;
	}

	static int lua_enumlib_mus_get_mathlib(lua_State *L)
	{
		const char *name = strchr(lua_tostring(L, 1), '_') + 1;
		int mus = get_mus(name, false);

		if (mus == 0)
			return luaL_error(L, "music '%s' could not be found.\n", lua_tostring(L, 1));

		lua_pushinteger(L, mus);
		return 1;
	}
#endif

static int lua_enumlib_power_get(lua_State *L)
{
	const char *s = lua_tostring(L, 1);
	for (int i = 0; i < NUMPOWERS; i++)
	{
		if (fasticmp(&s[3], POWERS_LIST[i]))
		{
			lua_pushinteger(L, i);
			return 1;
		}
	}

	lua_pushliteral(L, REQUIRE_MATHLIB_GUID);
	lua_gettable(L, LUA_REGISTRYINDEX);

	if (lua_toboolean(L,-1))
	{
		return luaL_error(L, "power '%s' could not be found.\n", lua_tostring(L, 1));
	}

	return 0;
}

static int lua_enumlib_kartstuff_get(lua_State *L)
{
	const char *s = lua_tostring(L, 1);
	for (int i = 0; i < NUMKARTSTUFF; i++)
	{
		if (fasticmp(&s[2], KARTSTUFF_LIST[i]))
		{
			lua_pushinteger(L, i);
			return 1;
		}
	}

	lua_pushliteral(L, REQUIRE_MATHLIB_GUID);
	lua_gettable(L, LUA_REGISTRYINDEX);

	if (lua_toboolean(L,-1))
	{
		return luaL_error(L, "kartstuff '%s' could not be found.\n", lua_tostring(L, 1));
	}

	return 0;
}

static int lua_enumlib_action_get(lua_State *L)
{
	const char *action = lua_tostring(L, 1);

	for (int i = 0; actionpointers[i].name; i++)
		if (fasticmp(action, actionpointers[i].name)) {
			// push lib_action as a C closure with the actionf_t* as an upvalue.
			lua_pushlightuserdata(L, &actionpointers[i].action);
			lua_pushcclosure(L, lib_action, 1);
			return 1;
		}

	return 0;
}

/**
 * Create a formatted string with uppercase value.
 * @param[in,out] buf Pointer to buffer pointer. Start with NULL.
 * @param[in,out] size Pointer to size.
 * @param[in] format Printf C format string.
 * @param ... Format parameters.
 * @returns Pointer to the buffer.
*/
static char *lua_enumlib_sprintf_upper(char **buf, int* size, const char *format, ...)
{
	va_list va;
	va_start(va, format);

	int length = vsnprintf(*buf, *size, format, va);
	if (length > *size)
	{
		void *ptr = realloc(*buf, length+1);
		if (!ptr)
		{
			va_end(va);
			return NULL;
		}
		*buf = ptr;
		*size = length+1;

		va_start(va, format);
		vsnprintf(*buf, *size, format, va);
	}
	va_end(va);

	for (int i = 0; i < length; i++)
	{
		(*buf)[i] = toupper((*buf)[i]);
	}

	return *buf;
}

static int lua_enumlib_super_get(lua_State *L)
{
	/**
	 *  TODO: I think there is still some improvement to be made here
	 * Not exactly sure how
	 */

	if (!superstack)
	{
		lua_pushcfunction(L, lib_dummysuper);
		return 1;
	}

	for (int i = 0; actionpointers[i].name; i++)
		if (fasticmp(superactions[superstack-1], actionpointers[i].name)) {
			lua_pushlightuserdata(L, &actionpointers[i].action);
			lua_pushcclosure(L, lib_action, 1);
			return 1;
		}

	return 0;
}

#define ARRAYLEN(arr) (sizeof(arr)/sizeof(*arr))

int LUA_EnumLib(lua_State *L)
{
	if (lua_gettop(L) == 0)
		lua_pushboolean(L, 0);

	int mathlib = lua_toboolean(L, 1);

	/* Store mathlib requirement to the registry. */
	lua_pushliteral(L, REQUIRE_MATHLIB_GUID);
	lua_pushvalue(L, 1);
	lua_settable(L, LUA_REGISTRYINDEX);

	lua_pushcfunction(L, lua_glib_require);
	lua_pushboolean(L, 0);	/* Inhibit setting the globals table. */
	lua_call(L, 1, 0);

	/* Although lua_glib_require already adds a metatable, make a new one here. */
	lua_newtable(L);
		lua_pushliteral(L, "__index");
			lua_pushvalue(L ,1);
			lua_glib_get_proxy(L);
		lua_pushcclosure(L, lib_getenum, 2);
		lua_settable(L, -3);
	lua_setmetatable(L, LUA_GLOBALSINDEX);

	lua_pushcfunction(L, lua_glib_append_cache);
	lua_createtable(L, 0, ('~'-'A'+1) + ARRAYLEN(INT_CONST));

		/* Push frame constants. */
		for (char chr = 'A'; chr <= '~'; chr++)
		{
			lua_pushfstring(L, "%c", chr);
			lua_pushinteger(L, chr-'A');
			lua_settable(L, -3);
		}

		/* Push other constants. */
		for (int i = 0; INT_CONST[i].n; i++)
		{
			lua_pushstring(L, INT_CONST[i].n);
			lua_pushinteger(L, INT_CONST[i].v);
			lua_settable(L, -3);
		}

		lua_pushliteral(L, "VERSIONSTRING");
		lua_pushliteral(L, VERSIONSTRING);
		lua_settable(L, -3);

		// TODO: ADD constant errors.
	lua_call(L, 1, 0);

	/* MOBJFLAG (MF_) */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_glib_push_bitfield_cache(L, "MF_", MOBJFLAG_LIST, ARRAYLEN(MOBJFLAG_LIST));
	lua_pushliteral(L, "MF_");
		lua_pushliteral(L, "mobjflag");
	lua_pushcclosure(L, lua_enumlib_basic_fallback, 1);
	lua_call(L, 3, 0);

	/* MOBJFLAG2 (MF2_) */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_glib_push_bitfield_cache(L, "MF2_", MOBJFLAG2_LIST, ARRAYLEN(MOBJFLAG2_LIST));
	lua_pushliteral(L, "MF2_");
		lua_pushliteral(L, "mobjflag2");
	lua_pushcclosure(L, lua_enumlib_basic_fallback, 1);
	lua_call(L, 3, 0);

	/* MOBJEFLAG (MFE_) */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_glib_push_bitfield_cache(L, "MFE_", MOBJEFLAG_LIST, ARRAYLEN(MOBJEFLAG_LIST));
	lua_pushliteral(L, "MFE_");
		lua_pushliteral(L, "mobjeflag");
	lua_pushcclosure(L, lua_enumlib_basic_fallback, 1);
	lua_call(L, 3, 0);

	/* mapthingflag (MTF_) */
	/* Because MTF_ flags array is stinky you have to do it manually :(*/
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_createtable(L, 0, 3);
	for (int i = 1; i < 4; i++)
	{
		lua_pushfstring(L, "MTF_%s", MAPTHINGFLAG_LIST[i]);
		lua_pushinteger(L, (lua_Integer)1 << i);
		lua_settable(L, -3);
	}
	lua_pushliteral(L, "MTF_");
		lua_pushliteral(L, "mapthingflag");
	lua_pushcclosure(L, lua_enumlib_basic_fallback, 1);
	lua_call(L, 3, 0);

	/* playerflag (PF_) */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_glib_push_bitfield_cache(L, "PF_", PLAYERFLAG_LIST, ARRAYLEN(PLAYERFLAG_LIST));
		lua_pushliteral(L, "PF_FULLSTASIS");
		lua_pushinteger(L, PF_FULLSTASIS);
		lua_settable(L, -3);
	lua_pushliteral(L, "PF_");
		lua_pushliteral(L, "playerflag");
	lua_pushcclosure(L, lua_enumlib_basic_fallback, 1);
	lua_call(L, 3, 0);

	/* linedef flag (ML_) */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_glib_push_bitfield_cache(L, "ML_", ML_LIST, 16);
		lua_pushliteral(L, "ML_NETONLY");
		lua_pushinteger(L, ML_NETONLY);
		lua_settable(L, -3);
	lua_pushliteral(L, "ML_");
		lua_pushliteral(L, "linedef flag");
	lua_pushcclosure(L, lua_enumlib_basic_fallback, 1);
	lua_call(L, 3, 0);

	/* States (S_) */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_glib_push_linear_cache(L, "", STATE_LIST, S_FIRSTFREESLOT);
	lua_pushliteral(L, "S_");
	lua_pushcfunction(L, lua_enumlib_state_get);
	lua_call(L, 3, 0);

	/* Mobj Types (MT_) */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_glib_push_linear_cache(L, "", MOBJTYPE_LIST, MT_FIRSTFREESLOT);
	lua_pushliteral(L, "MT_");
	lua_pushcfunction(L, lua_enumlib_mobjtype_get);
	lua_call(L, 3, 0);

	/* Sprites (SPR_) */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_newtable(L);
		for (int i = 0; i < SPR_FIRSTFREESLOT; i++)
		{
			lua_pushfstring(L,"SPR_%4s", &sprnames[i][0]);
			lua_pushinteger(L, i);
			lua_settable(L, -3);
		}
	lua_pushliteral(L, "SPR_");
	lua_pushcfunction(L, lua_enumlib_sprite_get);
	lua_call(L, 3, 0);

	if (mathlib)
	{
		char *str = NULL;
		int size = 0;

		// Sound enum tables preallocated for sfx_freeslot0 elements because using NUMSFX may be too
		// much if number of skins increased a lot (because of skinsound freeslots)

		/* SFX (SFX_) */
		lua_pushcfunction(L, lua_glib_new_enum);
		lua_createtable(L, 0, sfx_freeslot0);
			for (int i = 0; i < NUMSFX; i++)
			{
				if (!S_sfx[i].name || !S_sfx[i].priority) continue;

				if (lua_enumlib_sprintf_upper(&str, &size, "SFX_%s", S_sfx[i].name) == NULL)
					return luaL_error(L, "Ran out of memory.");

				lua_pushstring(L, str);
				lua_pushinteger(L, i);
				lua_settable(L, -3);
			}
		lua_pushliteral(L, "SFX_");
		lua_pushcfunction(L, lua_enumlib_sfx_get_uppercase);
		lua_call(L, 3, 0);

		/* SFX (DS) */
		lua_pushcfunction(L, lua_glib_new_enum);
		lua_createtable(L, 0, sfx_freeslot0);
			for (int i = 0; i < NUMSFX; i++)
			{
				if (!S_sfx[i].name || !S_sfx[i].priority) continue;

				if (lua_enumlib_sprintf_upper(&str, &size, "DS%s", S_sfx[i].name) == NULL)
					return luaL_error(L, "Ran out of memory.");

				lua_pushstring(L, str);
				lua_pushinteger(L, i);
				lua_settable(L, -3);
			}
		lua_pushliteral(L, "DS");
		lua_pushcfunction(L, lua_enumlib_sfx_get_ds);
		lua_call(L, 3, 0);

		#ifdef MUSICSLOT_COMPATIBILITY
			/* Music (MUS_) */
			lua_pushcfunction(L, lua_glib_new_enum);
			lua_pushnil(L);
			lua_pushliteral(L, "MUS_");
			lua_pushcfunction(L, lua_enumlib_mus_get_mathlib);
			lua_call(L, 3, 0);

			/* Music (O_) */
			lua_pushcfunction(L, lua_glib_new_enum);
			lua_pushnil(L);
			lua_pushliteral(L, "O_");
			lua_pushcfunction(L, lua_enumlib_mus_get_mathlib);
			lua_call(L, 3, 0);
		#endif

		free(str);
	}
	else
	{
		/* SFX (sfx_) */
		lua_pushcfunction(L, lua_glib_new_enum);
		lua_createtable(L, 0, sfx_freeslot0);
			for (int i = 0; i < NUMSFX; i++)
			{
				if (!S_sfx[i].name || !S_sfx[i].priority) continue;

				lua_pushfstring(L, "sfx_%s", S_sfx[i].name);
				lua_pushinteger(L, i);
				lua_settable(L, -3);
			}
		lua_pushliteral(L, "sfx_");
		lua_pushcfunction(L, lua_enumlib_sfx_get);
		lua_call(L, 3, 0);

		#ifdef MUSICSLOT_COMPATIBILITY
			/* SFX (mus_) */
			lua_pushcfunction(L, lua_glib_new_enum);
			lua_pushnil(L);
			lua_pushliteral(L, "mus_");
			lua_pushcfunction(L, lua_enumlib_mus_get);
			lua_call(L, 3, 0);
		#endif
	}

	/* Powers (pw_/PW_)*/
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_createtable(L, 0, NUMPOWERS);
		for (int i = 0; i < NUMPOWERS; i++)
		{
			lua_pushfstring(L, mathlib ? "PW_%s": "pw_%s", POWERS_LIST[i]);
			lua_pushinteger(L, i);
			lua_settable(L, -3);
		}
	lua_pushstring(L, mathlib ? "PW_" : "pw_");
	lua_pushcfunction(L, lua_enumlib_power_get);
	lua_call(L, 3, 0);

	/* Kart Stuff (K_/k_)*/
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_createtable(L, 0, NUMKARTSTUFF);
		for (int i = 0; i < NUMKARTSTUFF; i++)
		{
			lua_pushfstring(L, mathlib ? "K_%s": "k_%s", KARTSTUFF_LIST[i]);
			lua_pushinteger(L, i);
			lua_settable(L, -3);
		}
	lua_pushstring(L, mathlib ? "K_" : "k_");
	lua_pushcfunction(L, lua_enumlib_kartstuff_get);
	lua_call(L, 3, 0);

	/* Head up display (HUD_) */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_glib_push_linear_cache(L, "HUD_", HUDITEMS_LIST, NUMHUDITEMS);
	lua_pushliteral(L, "HUD_");
		lua_pushliteral(L, "huditem");
	lua_pushcclosure(L, lua_enumlib_basic_fallback, 1);
	lua_call(L, 3, 0);

	/* Skin Color (SKINCOLOR_) */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_glib_push_linear_cache(L, "SKINCOLOR_", COLOR_ENUMS, MAXTRANSLATIONS);
	lua_pushliteral(L, "SKINCOLOR_");
		lua_pushliteral(L, "skincolor");
	lua_pushcclosure(L, lua_enumlib_basic_fallback, 1);
	lua_call(L, 3, 0);

	/* Actions (A_)
	 * These are only the hard coded actions are intended as a fast lookup path
	 * when possible.
	 */
	lua_pushcfunction(L, lua_glib_new_enum);
	lua_createtable(L, 0, ARRAYLEN(actionpointers));
		for (int i = 0; actionpointers[i].name; i++)
		{
			lua_pushfstring(L, "A_%s", actionpointers[i].name);
				lua_pushlightuserdata(L, &actionpointers[i].action);
			lua_pushcclosure(L, lib_action, 1);
			lua_settable(L, -3);
		}
	lua_pushliteral(L, "A_");
	lua_pushcfunction(L, lua_enumlib_action_get);
	lua_call(L, 3, 0);

	/* Getters :) */

#define PUSHGETTER(what, t) do {\
	lua_pushcfunction(L, lua_glib_new_getter); \
	lua_pushliteral(L, #what); \
	lua_glib_push_##t##_getter(L, &what); \
	lua_call(L, 2, 0); \
} while(0)

	PUSHGETTER(gamemap, i16);
	PUSHGETTER(maptol, i16);
	PUSHGETTER(ultimatemode, b8);
	PUSHGETTER(circuitmap, bool);
	PUSHGETTER(netgame, bool);
	PUSHGETTER(multiplayer, bool);
	PUSHGETTER(modeattacking, b8);
	PUSHGETTER(splitscreen, u8);
	PUSHGETTER(gamecomplete, bool);
	PUSHGETTER(devparm, bool);
	PUSHGETTER(majormods, bool);
	PUSHGETTER(menuactive, bool);
	PUSHGETTER(paused, b8);
	PUSHGETTER(gametype, i16);
	PUSHGETTER(leveltime, u32);
	PUSHGETTER(curWeather, i32);
	PUSHGETTER(globalweather, u8);
	PUSHGETTER(levelskynum, i32);
	PUSHGETTER(globallevelskynum, i32);
	PUSHGETTER((*mapmusname), str);
	PUSHGETTER(mapmusflags, u16);
	PUSHGETTER(mapmusposition, u32);
	PUSHGETTER(gravity, fxp);
	PUSHGETTER(gamespeed, u8);
	PUSHGETTER(encoremode, bool);
	PUSHGETTER(franticitems, bool);
	PUSHGETTER(comeback, bool);
	PUSHGETTER(wantedcalcdelay, u32);
	PUSHGETTER(indirectitemcooldown, u32);
	PUSHGETTER(hyubgone, u32);
	PUSHGETTER(thwompsactive, bool);
	PUSHGETTER(spbplace, i8);
	PUSHGETTER(mapobjectscale, fxp);
	PUSHGETTER(racecountdown, u32);
	PUSHGETTER(exitcountdown, u32);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "mariomode");
	lua_pushcfunction(L, lua_enumlib_mariomode_get);
	lua_call(L, 2, 0);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "twodlevel");
	lua_pushcfunction(L, lua_enumlib_twodlevel_get);
	lua_call(L, 2, 0);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "modifiedgame");
	lua_pushcfunction(L, lua_enumlib_modifiedgame_get);
	lua_call(L, 2, 0);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "defrosting");
	lua_glib_push_i32_getter(L, &hook_defrosting);
	lua_call(L, 2, 0);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "server");
	lua_pushcfunction(L, lua_enumlib_server_get);
	lua_call(L, 2, 0);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "consoleplayer");
	lua_pushcfunction(L, lua_enumlib_consoleplayer_get);
	lua_call(L, 2, 0);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "isserver");
	lua_glib_push_bool_getter(L, &server);
	lua_call(L, 2, 0);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "isdedicatedserver");
	lua_glib_push_bool_getter(L, &dedicated);
	lua_call(L, 2, 0);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "numlaps");
	lua_glib_push_i32_getter(L, &cv_numlaps.value);
	lua_call(L, 2, 0);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "replayplayback");
	lua_glib_push_bool_getter(L, &demo.playback);
	lua_call(L, 2, 0);

	lua_pushcfunction(L, lua_glib_new_getter);
	lua_pushliteral(L, "replayfreecam");
	lua_glib_push_bool_getter(L, &demo.freecam);
	lua_call(L, 2, 0);

	if (!mathlib)
	{
		lua_pushcfunction(L, lua_glib_new_getter);
		lua_pushliteral(L, "super");
		lua_pushcfunction(L, lua_enumlib_super_get);
		lua_call(L, 2, 0);
	}

#undef PUSHGETTER

    // lua_pushliteral(L, "{cf75e032-00c8-4887-b15f-87b755784ad3}");
    // lua_gettable(L, LUA_REGISTRYINDEX);
    // lua_pushliteral(L, "cache");
    // lua_gettable(L, -2);

    // lua_pushnil(L);
    // while (lua_next(L, -2))
    // {
    //     lua_pushvalue(L, -2);
    //     CONS_Printf("%s: %s\n", lua_tostring(L, -1), lua_tostring(L, -2));
    //     lua_pop(L, 2);
    // }
	return 0;
}

int LUA_SOCLib(lua_State *L)
{
	lua_register(L,"freeslot",lib_freeslot);
	return 0;
}

const char *LUA_GetActionName(void *action)
{
	actionf_t *act = (actionf_t *)action;
	size_t z;
	for (z = 0; actionpointers[z].name; z++)
	{
		if (actionpointers[z].action.acv == act->acv)
			return actionpointers[z].name;
	}
	return NULL;
}

void LUA_SetActionByName(void *state, const char *actiontocompare)
{
	state_t *st = (state_t *)state;
	size_t z;
	for (z = 0; actionpointers[z].name; z++)
	{
		if (fasticmp(actiontocompare, actionpointers[z].name))
		{
			st->action = actionpointers[z].action;
			st->action.acv = actionpointers[z].action.acv; // assign
			st->action.acp1 = actionpointers[z].action.acp1;
			return;
		}
	}
}

enum actionnum LUA_GetActionNumByName(const char *actiontocompare)
{
	size_t z;
	for (z = 0; actionpointers[z].name; z++)
		if (fasticmp(actiontocompare, actionpointers[z].name))
			return z;
	return z;
}
