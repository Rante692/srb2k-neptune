// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  p_map.c
/// \brief Movement, collision handling
///
///	Shooting and aiming

#include "doomdef.h"
#include "g_game.h"
#include "m_bbox.h"
#include "m_random.h"
#include "p_local.h"
#include "p_setup.h" // NiGHTS stuff
#include "r_fps.h"
#include "r_state.h"
#include "r_main.h"
#include "r_sky.h"
#include "s_sound.h"
#include "w_wad.h"
#include "k_kart.h" // SRB2kart 011617

#include "hu_stuff.h" // SRB2kart
#include "i_system.h" // SRB2kart

#include "r_splats.h"

#include "p_slopes.h"

#include "z_zone.h"

#include "lua_hook.h"

#include "m_perfstats.h" // ps_checkposition_calls

fixed_t tmbbox[4];
mobj_t *tmthing;
static INT32 tmflags;
fixed_t tmx;
fixed_t tmy;

static precipmobj_t *tmprecipthing;
static fixed_t preciptmbbox[4];

// If "floatok" true, move would be ok
// if within "tmfloorz - tmceilingz".
boolean floatok;

fixed_t tmfloorz, tmceilingz;
static fixed_t tmdropoffz, tmdrpoffceilz; // drop-off floor/ceiling heights
mobj_t *tmfloorthing; // the thing corresponding to tmfloorz or NULL if tmfloorz is from a sector
mobj_t *tmhitthing; // the solid thing you bumped into (for collisions)
pslope_t *tmfloorslope, *tmceilingslope;

// keep track of the line that lowers the ceiling,
// so missiles don't explode against sky hack walls
line_t *ceilingline;

// set by PIT_CheckLine() for any line that stopped the PIT_CheckLine()
// that is, for any line which is 'solid'
line_t *blockingline;

msecnode_t *sector_list = NULL;
mprecipsecnode_t *precipsector_list = NULL;
camera_t *mapcampointer;

//
// TELEPORT MOVE
//

//
// P_TeleportMove
//
static boolean P_TeleportMove(mobj_t *thing, fixed_t x, fixed_t y, fixed_t z)
{
	// the move is ok,
	// so link the thing into its new position
	P_UnsetThingPosition(thing);

	// Remove touching_sectorlist from mobj.
	if (sector_list)
	{
		P_DelSeclist(sector_list);
		sector_list = NULL;
	}

	thing->x = x;
	thing->y = y;
	thing->z = z;

	P_SetThingPosition(thing);

	P_CheckPosition(thing, thing->x, thing->y);

	if (P_MobjWasRemoved(thing))
		return true;

	thing->floorz = tmfloorz;
	thing->ceilingz = tmceilingz;

	return true;
}

// P_SetOrigin - P_TeleportMove which RESETS interpolation values.
//
boolean P_SetOrigin(mobj_t *thing, fixed_t x, fixed_t y, fixed_t z)
{
	boolean result = P_TeleportMove(thing, x, y, z);

	if (result == true)
	{
		thing->old_x = thing->x;
		thing->old_y = thing->y;
		thing->old_z = thing->z;
	}

	return result;
}

//
// P_MoveOrigin - P_TeleportMove which KEEPS interpolation values.
//
boolean P_MoveOrigin(mobj_t *thing, fixed_t x, fixed_t y, fixed_t z)
{
	return P_TeleportMove(thing, x, y, z);
}

// =========================================================================
//                       MOVEMENT ITERATOR FUNCTIONS
// =========================================================================

boolean P_DoSpring(mobj_t *spring, mobj_t *object)
{
	//INT32 pflags;
	const fixed_t hscale = mapobjectscale + (mapobjectscale - object->scale);
	const fixed_t vscale = mapobjectscale + (object->scale - mapobjectscale);
	fixed_t offx, offy;
	fixed_t vertispeed = spring->info->mass;
	fixed_t horizspeed = spring->info->damage;

	if (object->eflags & MFE_SPRUNG) // Object was already sprung this tic
		return false;

	// Spectators don't trigger springs.
	if (object->player && object->player->spectator)
		return false;

	if (object->player && (object->player->pflags & PF_NIGHTSMODE))
	{
		/*Someone want to make these work like bumpers?*/
		return false;
	}

	object->standingslope = NULL; // Okay, now we can't return - no launching off at silly angles for you.

	object->eflags |= MFE_SPRUNG; // apply this flag asap!
	spring->flags &= ~(MF_SOLID|MF_SPECIAL); // De-solidify

	if (horizspeed && vertispeed) // Mimic SA
	{
		object->momx = object->momy = 0;
		P_TryMove(object, spring->x, spring->y, true);
	}

	if (spring->eflags & MFE_VERTICALFLIP)
		vertispeed *= -1;

	if (vertispeed > 0)
		object->z = spring->z + spring->height + 1;
	else if (vertispeed < 0)
		object->z = spring->z - object->height - 1;
	else
	{
		// Horizontal springs teleport you in FRONT of them.
		object->momx = object->momy = 0;

		// Overestimate the distance to position you at
		offx = P_ReturnThrustX(spring, spring->angle, (spring->radius + object->radius + 1) * 2);
		offy = P_ReturnThrustY(spring, spring->angle, (spring->radius + object->radius + 1) * 2);

		// Make it square by clipping
		if (offx > (spring->radius + object->radius + 1))
			offx = spring->radius + object->radius + 1;
		else if (offx < -(spring->radius + object->radius + 1))
			offx = -(spring->radius + object->radius + 1);

		if (offy > (spring->radius + object->radius + 1))
			offy = spring->radius + object->radius + 1;
		else if (offy < -(spring->radius + object->radius + 1))
			offy = -(spring->radius + object->radius + 1);

		// Set position!
		P_TryMove(object, spring->x + offx, spring->y + offy, true);
	}

	if (vertispeed)
		object->momz = FixedMul(vertispeed,FixedSqrt(FixedMul(vscale, spring->scale)));

	if (horizspeed)
	{
		if (!object->player)
			P_InstaThrustEvenIn2D(object, spring->angle, FixedMul(horizspeed,FixedSqrt(FixedMul(hscale, spring->scale))));
		else
		{
			fixed_t finalSpeed = FixedDiv(horizspeed, hscale);
			fixed_t pSpeed = object->player->speed;

			if (pSpeed > finalSpeed)
				finalSpeed = pSpeed;

			P_InstaThrustEvenIn2D(object, spring->angle, FixedMul(finalSpeed,FixedSqrt(FixedMul(hscale, spring->scale))));
		}
	}

	// Re-solidify
	spring->flags |= (spring->info->flags & (MF_SPECIAL|MF_SOLID));

	P_SetMobjState(spring, spring->info->raisestate);

	if (object->player)
	{
		if (spring->flags & MF_ENEMY) // Spring shells
			P_SetTarget(&spring->target, object);

		if (horizspeed && object->player->cmd.forwardmove == 0 && object->player->cmd.sidemove == 0)
		{
			object->angle = spring->angle;

			if (!demo.playback || P_AnalogMove(object->player))
			{
				if (object->player == &players[consoleplayer])
					localangle[0] = spring->angle;
				else if (object->player == &players[displayplayers[1]])
					localangle[1] = spring->angle;
				else if (object->player == &players[displayplayers[2]])
					localangle[2] = spring->angle;
				else if (object->player == &players[displayplayers[3]])
					localangle[3] = spring->angle;
			}
		}

		//pflags = object->player->pflags & (PF_JUMPED|PF_SPINNING|PF_THOKKED); // I still need these.
		P_ResetPlayer(object->player);
	}
	return true;
}

static void P_DoFanAndGasJet(mobj_t *spring, mobj_t *object)
{
	player_t *p = object->player; // will be NULL if not a player
	fixed_t zdist; // distance between bottoms
	fixed_t speed = spring->info->mass; // conveniently, both fans and gas jets use this for the vertical thrust
	SINT8 flipval = P_MobjFlip(spring); // virtually everything here centers around the thruster's gravity, not the object's!

	if (p && object->state == &states[object->info->painstate]) // can't use fans and gas jets when player is in pain!
		return;

	// is object's top below thruster's position? if not, calculate distance between their bottoms
	if (spring->eflags & MFE_VERTICALFLIP)
	{
		if (object->z > spring->z + spring->height)
			return;
		zdist = (spring->z + spring->height) - (object->z + object->height);
	}
	else
	{
		if (object->z + object->height < spring->z)
			return;
		zdist = object->z - spring->z;
	}

	object->standingslope = NULL; // No launching off at silly angles for you.

	switch (spring->type)
	{
		case MT_FAN: // fan
			if (zdist > (spring->health << FRACBITS)) // max z distance determined by health (set by map thing angle)
				break;
			if (flipval*object->momz >= FixedMul(speed, spring->scale)) // if object's already moving faster than your best, don't bother
				break;
			if (p && (p->climbing || p->pflags & PF_GLIDING)) // doesn't affect Knux when he's using his abilities!
				break;

			object->momz += flipval*FixedMul(speed/4, spring->scale);

			// limit the speed if too high
			if (flipval*object->momz > FixedMul(speed, spring->scale))
				object->momz = flipval*FixedMul(speed, spring->scale);

			break;
		case MT_STEAM: // Steam
			if (zdist > FixedMul(16*FRACUNIT, spring->scale))
				break;
			if (spring->state != &states[S_STEAM1]) // Only when it bursts
				break;

			if (spring->spawnpoint && spring->spawnpoint->options & MTF_OBJECTSPECIAL)
			{
				if (object->eflags & MFE_SPRUNG)
					break;
				if (object->player)
					object->player->kartstuff[k_pogospring] = 1;
				K_DoPogoSpring(object, 0, 0);
				return;
			}
			else
				object->momz = flipval*FixedMul(speed, FixedSqrt(FixedMul(spring->scale, object->scale))); // scale the speed with both objects' scales, just like with springs!

			break;
		default:
			break;
	}
}

//
// PIT_CheckThing
//
static boolean PIT_CheckThing(mobj_t *thing)
{
	fixed_t blockdist;
	boolean iwassprung = false;

	// don't clip against self
	if (thing == tmthing)
		return true;

	// Ignore... things.
	if (!tmthing || !thing || P_MobjWasRemoved(thing))
		return true;

	I_Assert(!P_MobjWasRemoved(tmthing));
	I_Assert(!P_MobjWasRemoved(thing));

	// Ignore spectators
	if ((tmthing->player && tmthing->player->spectator)
	|| (thing->player && thing->player->spectator))
		return true;

#ifdef SEENAMES
  // Do name checks all the way up here
  // So that NOTHING ELSE can see MT_NAMECHECK because it is client-side.
	if (tmthing->type == MT_NAMECHECK)
	{
	  // Ignore things that aren't players, ignore spectators, ignore yourself.
		// (also don't bother to check that tmthing->target->player is non-NULL because we're not actually using it here.)
		if (!thing->player || thing->player->spectator || (tmthing->target && thing->player == tmthing->target->player))
			return true;

		// Now check that you actually hit them.
		blockdist = thing->radius + tmthing->radius;
		if (abs(thing->x - tmx) >= blockdist || abs(thing->y - tmy) >= blockdist)
			return true; // didn't hit it
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		seenplayer = thing->player;
		return false;
	}
#endif

	if (!(thing->flags & (MF_SOLID|MF_SPECIAL|MF_PAIN|MF_SHOOTABLE)) || (thing->flags & MF_NOCLIPTHING))
		return true;

	// Don't collide with your buddies while NiGHTS-flying.
	if (tmthing->player && thing->player && (maptol & TOL_NIGHTS)
		&& ((tmthing->player->pflags & PF_NIGHTSMODE) || (thing->player->pflags & PF_NIGHTSMODE)))
		return true;

	blockdist = thing->radius + tmthing->radius;

	if (abs(thing->x - tmx) >= blockdist || abs(thing->y - tmy) >= blockdist)
		return true; // didn't hit it

	if (thing->flags & MF_PAPERCOLLISION) // CAUTION! Very easy to get stuck inside MF_SOLID objects. Giving the player MF_PAPERCOLLISION is a bad idea unless you know what you're doing.
	{
		fixed_t cosradius, sinradius;
		vertex_t v1, v2; // fake vertexes
		line_t junk; // fake linedef
		cosradius = FixedMul(thing->radius, FINECOSINE(thing->angle>>ANGLETOFINESHIFT));
		sinradius = FixedMul(thing->radius, FINESINE(thing->angle>>ANGLETOFINESHIFT));

		v1.x = thing->x - cosradius;
		v1.y = thing->y - sinradius;
		v2.x = thing->x + cosradius;
		v2.y = thing->y + sinradius;

		junk.v1 = &v1;
		junk.v2 = &v2;
		junk.dx = v2.x - v1.x;
		junk.dy = v2.y - v1.y;

		if (tmthing->flags & MF_PAPERCOLLISION) // more strenuous checking to prevent clipping issues
		{
			INT32 check1, check2, check3, check4;
			cosradius = FixedMul(tmthing->radius, FINECOSINE(tmthing->angle>>ANGLETOFINESHIFT));
			sinradius = FixedMul(tmthing->radius, FINESINE(tmthing->angle>>ANGLETOFINESHIFT));
			check1 = P_PointOnLineSide(tmx - cosradius, tmy - sinradius, &junk);
			check2 = P_PointOnLineSide(tmx + cosradius, tmy + sinradius, &junk);
			check3 = P_PointOnLineSide(tmx + tmthing->momx - cosradius, tmy + tmthing->momy - sinradius, &junk);
			check4 = P_PointOnLineSide(tmx + tmthing->momx + cosradius, tmy + tmthing->momy + sinradius, &junk);
			if ((check1 == check2) && (check2 == check3) && (check3 == check4))
				return true; // the line doesn't cross between collider's start or end
		}
		else
		{
			if ((P_PointOnLineSide(tmx - tmthing->radius, tmy - tmthing->radius, &junk)
			== P_PointOnLineSide(tmx + tmthing->radius, tmy + tmthing->radius, &junk))
			&& (P_PointOnLineSide(tmx + tmthing->radius, tmy - tmthing->radius, &junk)
			== P_PointOnLineSide(tmx - tmthing->radius, tmy + tmthing->radius, &junk)))
				return true; // the line doesn't cross between either pair of opposite corners
		}
	}
	else if (tmthing->flags & MF_PAPERCOLLISION)
	{
		fixed_t cosradius, sinradius;
		vertex_t v1, v2; // fake vertexes
		line_t junk; // fake linedef

		cosradius = FixedMul(tmthing->radius, FINECOSINE(tmthing->angle>>ANGLETOFINESHIFT));
		sinradius = FixedMul(tmthing->radius, FINESINE(tmthing->angle>>ANGLETOFINESHIFT));

		v1.x = tmx - cosradius;
		v1.y = tmy - sinradius;
		v2.x = tmx + cosradius;
		v2.y = tmy + sinradius;

		junk.v1 = &v1;
		junk.v2 = &v2;
		junk.dx = v2.x - v1.x;
		junk.dy = v2.y - v1.y;

		// no need to check whether thing has MF_PAPERCOLLISION, since checked above

		if ((P_PointOnLineSide(thing->x - thing->radius, thing->y - thing->radius, &junk)
		== P_PointOnLineSide(thing->x + thing->radius, thing->y + thing->radius, &junk))
		&& (P_PointOnLineSide(thing->x + thing->radius, thing->y - thing->radius, &junk)
		== P_PointOnLineSide(thing->x - thing->radius, thing->y + thing->radius, &junk)))
			return true; // the line doesn't cross between either pair of opposite corners
	}

	{
		UINT8 shouldCollide = LUAh_MobjCollide(thing, tmthing); // checks hook for thing's type
		if (P_MobjWasRemoved(tmthing) || P_MobjWasRemoved(thing))
			return true; // one of them was removed???
		if (shouldCollide == 1)
			return false; // force collide
		else if (shouldCollide == 2)
			return true; // force no collide
	}
	{
		UINT8 shouldCollide = LUAh_MobjMoveCollide(tmthing, thing); // checks hook for tmthing's type
		if (P_MobjWasRemoved(tmthing) || P_MobjWasRemoved(thing))
			return true; // one of them was removed???
		if (shouldCollide == 1)
			return false; // force collide
		else if (shouldCollide == 2)
			return true; // force no collide
	}

	// When solid spikes move, assume they just popped up and teleport things on top of them to hurt.
	if (tmthing->type == MT_SPIKE && tmthing->flags & MF_SOLID)
	{
		if (thing->z > tmthing->z + tmthing->height)
			return true; // overhead
		if (thing->z + thing->height < tmthing->z)
			return true; // underneath

		if (tmthing->eflags & MFE_VERTICALFLIP)
			thing->z = thing->old_z = tmthing->z - thing->height - FixedMul(FRACUNIT, tmthing->scale);
		else
			thing->z = thing->old_z = tmthing->z + tmthing->height + FixedMul(FRACUNIT, tmthing->scale);
		if (thing->flags & MF_SHOOTABLE)
			P_DamageMobj(thing, tmthing, tmthing, 1);
		return true;
	}

	if (thing->flags & MF_PAIN)
	{ // Player touches painful thing sitting on the floor
		// see if it went over / under
		if (thing->z > tmthing->z + tmthing->height)
			return true; // overhead
		if (thing->z + thing->height < tmthing->z)
			return true; // underneath
		if (tmthing->player && tmthing->flags & MF_SHOOTABLE && thing->health > 0)
			P_DamageMobj(tmthing, thing, thing, 1);
		return true;
	}
	else if (tmthing->flags & MF_PAIN)
	{ // Painful thing splats player in the face
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath
		if (thing->player && thing->flags & MF_SHOOTABLE && tmthing->health > 0)
			P_DamageMobj(thing, tmthing, tmthing, 1);
		return true;
	}

	if (thing->type == MT_HOOPCOLLIDE && thing->flags & MF_SPECIAL && tmthing->player)
	{
		P_TouchSpecialThing(thing, tmthing, true);
		return true;
	}

	// check for skulls slamming into things
	if (tmthing->flags2 & MF2_SKULLFLY)
	{
		{
			// see if it went over / under
			if (tmthing->z > thing->z + thing->height)
				return true; // overhead
			if (tmthing->z + tmthing->height < thing->z)
				return true; // underneath

			tmthing->flags2 &= ~MF2_SKULLFLY;
			tmthing->momx = tmthing->momy = tmthing->momz = 0;
			return false; // stop moving
		}
	}

	// SRB2kart 011617 - Colission[sic] code for kart items //{

	if (thing->type == MT_SMK_ICEBLOCK)
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		if (!(tmthing->flags & MF_SOLID || tmthing->flags & MF_SHOOTABLE || tmthing->flags & MF_BOUNCE))
			return true;

		if (!(tmthing->health))
			return true;

		if (tmthing->type == MT_BANANA || tmthing->type == MT_BANANA_SHIELD
			|| tmthing->type == MT_EGGMANITEM || tmthing->type == MT_EGGMANITEM_SHIELD
			|| tmthing->type == MT_SSMINE || tmthing->type == MT_SSMINE_SHIELD
			|| tmthing->type == MT_ORBINAUT_SHIELD || tmthing->type == MT_JAWZ_SHIELD)
			return false;

		if (thing->health)
			P_KillMobj(thing, tmthing, tmthing);

		/*if (tmthing->player && (tmthing->player->kartstuff[k_invincibilitytimer] > 0
			|| tmthing->player->kartstuff[k_growshrinktimer] > 0))
			return true;*/

		K_KartBouncing(tmthing, thing, false, true);
		return false;
	}

	// Push fakes out of other items
	if (tmthing->type == MT_EGGMANITEM && (thing->type == MT_RANDOMITEM || thing->type == MT_EGGMANITEM))
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		P_InstaThrust(tmthing, R_PointToAngle2(thing->x, thing->y, tmthing->x, tmthing->y), thing->radius/4);
		return true;
	}
	else if (thing->type == MT_EGGMANITEM && (tmthing->type == MT_RANDOMITEM || tmthing->type == MT_EGGMANITEM))
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		P_InstaThrust(thing, R_PointToAngle2(tmthing->x, tmthing->y, thing->x, thing->y), tmthing->radius/4);
		return true;
	}

	if (tmthing->type == MT_RANDOMITEM)
		return true;

	// Bubble Shield reflect
	if (((thing->type == MT_BUBBLESHIELD && thing->target->player && thing->target->player->kartstuff[k_bubbleblowup])
		|| (thing->player && thing->player->kartstuff[k_bubbleblowup]))
		&& (tmthing->type == MT_ORBINAUT || tmthing->type == MT_JAWZ || tmthing->type == MT_JAWZ_DUD
		|| tmthing->type == MT_BANANA || tmthing->type == MT_EGGMANITEM || tmthing->type == MT_BALLHOG
		|| tmthing->type == MT_SSMINE || tmthing->type == MT_SINK
		|| (tmthing->type == MT_PLAYER && thing->target != tmthing)))
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath
			
		if (tmthing->type == MT_PLAYER)
		{
			if (tmthing->player->kartstuff[k_spinouttimer] || tmthing->player->kartstuff[k_squishedtimer]
				|| tmthing->player->powers[pw_flashing] || tmthing->player->kartstuff[k_hyudorotimer]
				|| tmthing->player->kartstuff[k_justbumped] || tmthing->scale > thing->scale + (mapobjectscale/8))
				return true;
			
			// Player Damage
			K_SpinPlayer(tmthing->player, thing, 0, ((thing->type == MT_BUBBLESHIELD) ? thing->target : thing), false);
			S_StartSound(thing, sfx_s3k44);
		}
		else
		{
			if (!tmthing->threshold)
			{
				if (!tmthing->momx && !tmthing->momy)
				{
					tmthing->momz += (24*tmthing->scale) * P_MobjFlip(tmthing);
				}
				else
				{
					tmthing->momx = -tmthing->momx;
					tmthing->momy = -tmthing->momy;
					tmthing->momz = -tmthing->momz;
					tmthing->angle += ANGLE_180;
				}
				if (tmthing->type == MT_JAWZ)
					P_SetTarget(&tmthing->tracer, tmthing->target); // Back to the source!
				tmthing->threshold = 10;
				S_StartSound(thing, sfx_s3k44);
			}
		}

		// no interaction
		return true;
	}
	else if (((tmthing->type == MT_BUBBLESHIELD && tmthing->target->player && tmthing->target->player->kartstuff[k_bubbleblowup])
		|| (tmthing->player && tmthing->player->kartstuff[k_bubbleblowup]))
		&& (thing->type == MT_ORBINAUT || thing->type == MT_JAWZ || thing->type == MT_JAWZ_DUD
		|| thing->type == MT_BANANA || thing->type == MT_EGGMANITEM || thing->type == MT_BALLHOG
		|| thing->type == MT_SSMINE || thing->type == MT_SINK
		|| (thing->type == MT_PLAYER && tmthing->target != thing)))
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		if (thing->type == MT_PLAYER)
		{
			if (thing->player->kartstuff[k_spinouttimer] || thing->player->kartstuff[k_squishedtimer]
				|| thing->player->powers[pw_flashing] || thing->player->kartstuff[k_hyudorotimer]
				|| thing->player->kartstuff[k_justbumped] || thing->scale > tmthing->scale + (mapobjectscale/8))
				return true;

			// Player Damage
			K_SpinPlayer(thing->player, tmthing, 0, ((tmthing->type == MT_BUBBLESHIELD) ? tmthing->target : tmthing), false);
			S_StartSound(tmthing, sfx_s3k44);
		}
		else
		{
			if (!thing->threshold)
			{
				if (!thing->momx && !thing->momy)
				{
					thing->momz += (24*thing->scale) * P_MobjFlip(thing);
				}
				else
				{
					thing->momx = -thing->momx;
					thing->momy = -thing->momy;
					thing->momz = -thing->momz;
					tmthing->angle += ANGLE_180;
				}
				if (thing->type == MT_JAWZ)
					P_SetTarget(&thing->tracer, thing->target); // Back to the source!
				thing->threshold = 10;
				S_StartSound(tmthing, sfx_s3k44);
			}
		}

		// no interaction
		return true;
	}

	// double make sure bubbles won't collide with anything else
	if (thing->type == MT_BUBBLESHIELD || tmthing->type == MT_BUBBLESHIELD)
		return true;

	if (tmthing->type == MT_ORBINAUT || tmthing->type == MT_JAWZ || tmthing->type == MT_JAWZ_DUD
		|| tmthing->type == MT_ORBINAUT_SHIELD || tmthing->type == MT_JAWZ_SHIELD)
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		if (((tmthing->target == thing) || (tmthing->target == thing->target)) && (tmthing->threshold > 0 || (thing->type != MT_PLAYER && thing->threshold > 0)))
			return true;

		if (tmthing->health <= 0 || thing->health <= 0)
			return true;

		if ((tmthing->type == MT_ORBINAUT_SHIELD || tmthing->type == MT_JAWZ_SHIELD) && tmthing->lastlook
			&& (thing->type == MT_ORBINAUT_SHIELD || thing->type == MT_JAWZ_SHIELD) && thing->lastlook
			&& (tmthing->target == thing->target)) // Don't hit each other if you have the same target
			return true;

		if (thing->player && thing->player->powers[pw_flashing]
			&& !(tmthing->type == MT_ORBINAUT || tmthing->type == MT_JAWZ || tmthing->type == MT_JAWZ_DUD))
			return true;

		if (thing->player && thing->player->kartstuff[k_hyudorotimer])
			return true; // no interaction

		if (thing->type == MT_PLAYER)
		{
			// Player Damage
			P_DamageMobj(thing, tmthing, tmthing->target, 1);
			K_KartBouncing(thing, tmthing, false, false);
			S_StartSound(thing, sfx_s3k7b);

			// This Item Damage
			if (tmthing->eflags & MFE_VERTICALFLIP)
				tmthing->z -= tmthing->height;
			else
				tmthing->z += tmthing->height;

			S_StartSound(tmthing, tmthing->info->deathsound);
			P_KillMobj(tmthing, thing, thing);

			P_SetObjectMomZ(tmthing, 8*FRACUNIT, false);
			P_InstaThrust(tmthing, R_PointToAngle2(thing->x, thing->y, tmthing->x, tmthing->y)+ANGLE_90, 16*FRACUNIT);
		}
		else if (thing->type == MT_ORBINAUT || thing->type == MT_JAWZ || thing->type == MT_JAWZ_DUD
			|| thing->type == MT_ORBINAUT_SHIELD || thing->type == MT_JAWZ_SHIELD
			|| thing->type == MT_BANANA || thing->type == MT_BANANA_SHIELD
			|| thing->type == MT_BALLHOG)
		{
			// Other Item Damage
			if (thing->eflags & MFE_VERTICALFLIP)
				thing->z -= thing->height;
			else
				thing->z += thing->height;

			S_StartSound(thing, thing->info->deathsound);
			P_KillMobj(thing, tmthing, tmthing);

			P_SetObjectMomZ(thing, 8*FRACUNIT, false);
			P_InstaThrust(thing, R_PointToAngle2(tmthing->x, tmthing->y, thing->x, thing->y)+ANGLE_90, 16*FRACUNIT);

			P_SpawnMobj(thing->x/2 + tmthing->x/2, thing->y/2 + tmthing->y/2, thing->z/2 + tmthing->z/2, MT_ITEMCLASH);

			// This Item Damage
			if (tmthing->eflags & MFE_VERTICALFLIP)
				tmthing->z -= tmthing->height;
			else
				tmthing->z += tmthing->height;

			S_StartSound(tmthing, tmthing->info->deathsound);
			P_KillMobj(tmthing, thing, thing);

			P_SetObjectMomZ(tmthing, 8*FRACUNIT, false);
			P_InstaThrust(tmthing, R_PointToAngle2(thing->x, thing->y, tmthing->x, tmthing->y)+ANGLE_90, 16*FRACUNIT);
		}
		else if (thing->type == MT_SSMINE_SHIELD || thing->type == MT_SSMINE)
		{
			// This Item Damage
			if (tmthing->eflags & MFE_VERTICALFLIP)
				tmthing->z -= tmthing->height;
			else
				tmthing->z += tmthing->height;

			S_StartSound(tmthing, tmthing->info->deathsound);
			P_KillMobj(tmthing, thing, thing);

			P_SetObjectMomZ(tmthing, 8*FRACUNIT, false);
			P_InstaThrust(tmthing, R_PointToAngle2(thing->x, thing->y, tmthing->x, tmthing->y)+ANGLE_90, 16*FRACUNIT);

			// Bomb death
			P_KillMobj(thing, tmthing, tmthing);
		}
		else if (thing->flags & MF_SPRING && (tmthing->type == MT_JAWZ || tmthing->type == MT_JAWZ_DUD || tmthing->type == MT_ORBINAUT))
			P_DoSpring(thing, tmthing);

		return true;
	}
	else if (tmthing->flags & MF_SPRING && (thing->type == MT_JAWZ || thing->type == MT_JAWZ_DUD || thing->type == MT_ORBINAUT))
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		if (thing->health <= 0)
			return true;

		P_DoSpring(tmthing, thing);

		return true;
	}
	else if (tmthing->type == MT_SINK)
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		if (((tmthing->target == thing) || (tmthing->target == thing->target)) && (tmthing->threshold > 0 || (thing->type != MT_PLAYER && thing->threshold > 0)))
			return true;

		if (thing->player && thing->player->powers[pw_flashing])
			return true;

		if (thing->type == MT_PLAYER)
		{
			S_StartSound(NULL, sfx_bsnipe); //let all players hear it.
			HU_SetCEchoFlags(0);
			HU_SetCEchoDuration(5);
			HU_DoCEcho(va("%s\\was hit by a kitchen sink.\\\\\\\\", player_names[thing->player-players]));
			I_OutputMsg("%s was hit by a kitchen sink.\n", player_names[thing->player-players]);
			P_DamageMobj(thing, tmthing, tmthing->target, 10000);
			P_KillMobj(tmthing, thing, thing);
		}

		return true;
	}
	else if (tmthing->type == MT_MINEEXPLOSION)
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		if (!(thing->type == MT_PLAYER))
			return true;

		if (thing->player && thing->player->powers[pw_flashing])
			return true;

		if (thing->type == MT_PLAYER && thing->player)
		{
			if (tmthing->state == &states[S_MINEEXPLOSION1])
				K_ExplodePlayer(thing->player, tmthing->target, tmthing);
			else
				K_SpinPlayer(thing->player, tmthing->target, 0, tmthing, false);
		}

		return true; // This doesn't collide with anything, but we want it to effect the player anyway.
	}
	else if (tmthing->type == MT_BANANA_SHIELD || tmthing->type == MT_BANANA
		|| tmthing->type == MT_BALLHOG)
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		if (((tmthing->target == thing) || (tmthing->target == thing->target)) && (tmthing->threshold > 0 || (thing->type != MT_PLAYER && thing->threshold > 0)))
			return true;

		if (tmthing->health <= 0 || thing->health <= 0)
			return true;

		if (((tmthing->type == MT_BANANA_SHIELD) && (thing->type == MT_BANANA_SHIELD))
			&& (tmthing->target == thing->target)) // Don't hit each other if you have the same target
			return true;

		if (tmthing->type == MT_BALLHOG && thing->type == MT_BALLHOG)
			return true; // Ballhogs don't collide with eachother

		if (thing->player && thing->player->powers[pw_flashing])
			return true;

		if (thing->type == MT_PLAYER)
		{
			// Banana snipe!
			if (tmthing->type == MT_BANANA && tmthing->health > 1)
				S_StartSound(thing, sfx_bsnipe);

			// Player Damage
			K_SpinPlayer(thing->player, tmthing->target, 0, tmthing, (tmthing->type == MT_BANANA || tmthing->type == MT_BANANA_SHIELD));

			// This Item Damage
			if (tmthing->eflags & MFE_VERTICALFLIP)
				tmthing->z -= tmthing->height;
			else
				tmthing->z += tmthing->height;

			S_StartSound(tmthing, tmthing->info->deathsound);
			P_KillMobj(tmthing, thing, thing);

			P_SetObjectMomZ(tmthing, 8*FRACUNIT, false);
			P_InstaThrust(tmthing, R_PointToAngle2(thing->x, thing->y, tmthing->x, tmthing->y)+ANGLE_90, 16*FRACUNIT);
		}
		else if (thing->type == MT_BANANA || thing->type == MT_BANANA_SHIELD
			|| thing->type == MT_ORBINAUT || thing->type == MT_ORBINAUT_SHIELD
			|| thing->type == MT_JAWZ || thing->type == MT_JAWZ_DUD || thing->type == MT_JAWZ_SHIELD
			|| thing->type == MT_BALLHOG)
		{
			// Other Item Damage
			if (thing->eflags & MFE_VERTICALFLIP)
				thing->z -= thing->height;
			else
				thing->z += thing->height;

			S_StartSound(thing, thing->info->deathsound);
			P_KillMobj(thing, tmthing, tmthing);

			P_SetObjectMomZ(thing, 8*FRACUNIT, false);
			P_InstaThrust(thing, R_PointToAngle2(tmthing->x, tmthing->y, thing->x, thing->y)+ANGLE_90, 16*FRACUNIT);

			P_SpawnMobj(thing->x/2 + tmthing->x/2, thing->y/2 + tmthing->y/2, thing->z/2 + tmthing->z/2, MT_ITEMCLASH);

			// This Item Damage
			if (tmthing->eflags & MFE_VERTICALFLIP)
				tmthing->z -= tmthing->height;
			else
				tmthing->z += tmthing->height;

			S_StartSound(tmthing, tmthing->info->deathsound);
			P_KillMobj(tmthing, thing, thing);

			P_SetObjectMomZ(tmthing, 8*FRACUNIT, false);
			P_InstaThrust(tmthing, R_PointToAngle2(thing->x, thing->y, tmthing->x, tmthing->y)+ANGLE_90, 16*FRACUNIT);
		}

		return true;
	}
	else if (tmthing->type == MT_SSMINE_SHIELD || tmthing->type == MT_SSMINE)
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		if (((tmthing->target == thing) || (tmthing->target == thing->target)) && (tmthing->threshold > 0 || (thing->type != MT_PLAYER && thing->threshold > 0)))
			return true;

		if (tmthing->health <= 0 || thing->health <= 0)
			return true;

		if (thing->player && thing->player->powers[pw_flashing])
			return true;

		if (thing->type == MT_PLAYER)
		{
			// Bomb punting
			if ((tmthing->state >= &states[S_SSMINE1] && tmthing->state <= &states[S_SSMINE4])
				|| (tmthing->state >= &states[S_SSMINE_DEPLOY8] && tmthing->state <= &states[S_SSMINE_DEPLOY13]))
				P_KillMobj(tmthing, thing, thing);
			else
				K_PuntMine(tmthing, thing);
		}
		else if (thing->type == MT_ORBINAUT || thing->type == MT_JAWZ || thing->type == MT_JAWZ_DUD
			|| thing->type == MT_ORBINAUT_SHIELD || thing->type == MT_JAWZ_SHIELD)
		{
			P_KillMobj(tmthing, thing, thing);

			// Other Item Damage
			if (thing->eflags & MFE_VERTICALFLIP)
				thing->z -= thing->height;
			else
				thing->z += thing->height;

			S_StartSound(thing, thing->info->deathsound);
			P_KillMobj(thing, tmthing, tmthing);

			P_SetObjectMomZ(thing, 8*FRACUNIT, false);
			P_InstaThrust(thing, R_PointToAngle2(tmthing->x, tmthing->y, thing->x, thing->y)+ANGLE_90, 16*FRACUNIT);
		}

		return true;
	}
	else if (tmthing->type == MT_PLAYER &&
			(thing->type == MT_ORBINAUT_SHIELD || thing->type == MT_ORBINAUT
			|| thing->type == MT_JAWZ_SHIELD || thing->type == MT_JAWZ || thing->type == MT_JAWZ_DUD
			|| thing->type == MT_BANANA_SHIELD || thing->type == MT_BANANA
			|| thing->type == MT_SSMINE_SHIELD || thing->type == MT_SSMINE
			|| thing->type == MT_MINEEXPLOSION
			|| thing->type == MT_SINK || thing->type == MT_BALLHOG
			))
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		if (tmthing->player && tmthing->player->powers[pw_flashing]
			&& !(thing->type == MT_ORBINAUT || thing->type == MT_JAWZ || thing->type == MT_JAWZ_DUD))
			return true;

		if (tmthing->player && tmthing->player->kartstuff[k_hyudorotimer]) // I thought about doing this for just the objects below but figured it should apply to everything.
			return true; // no interaction

		if (thing->type == MT_ORBINAUT_SHIELD || thing->type == MT_JAWZ_SHIELD
			|| thing->type == MT_ORBINAUT || thing->type == MT_JAWZ || thing->type == MT_JAWZ_DUD)
		{
			if ((thing->target == tmthing) && (thing->threshold > 0))
				return true;

			if (tmthing->health <= 0 || thing->health <= 0)
				return true;

			// Player Damage
			P_DamageMobj(tmthing, thing, thing->target, 1);
			K_KartBouncing(tmthing, thing, false, false);
			S_StartSound(tmthing, sfx_s3k7b);

			// Other Item Damage
			if (thing->eflags & MFE_VERTICALFLIP)
				thing->z -= thing->height;
			else
				thing->z += thing->height;

			S_StartSound(thing, thing->info->deathsound);
			P_KillMobj(thing, tmthing, tmthing);

			P_SetObjectMomZ(thing, 8*FRACUNIT, false);
			P_InstaThrust(thing, R_PointToAngle2(tmthing->x, tmthing->y, thing->x, thing->y)+ANGLE_90, 16*FRACUNIT);
		}
		else if (thing->type == MT_BANANA_SHIELD || thing->type == MT_BANANA
			|| thing->type == MT_BALLHOG)
		{
			if ((thing->target == tmthing) && (thing->threshold > 0))
				return true;

			if (tmthing->health <= 0 || thing->health <= 0)
				return true;

			// Banana snipe!
			if (thing->type == MT_BANANA && thing->health > 1)
				S_StartSound(tmthing, sfx_bsnipe);

			// Player Damage
			K_SpinPlayer(tmthing->player, thing->target, 0, thing, (thing->type == MT_BANANA || thing->type == MT_BANANA_SHIELD));

			// Other Item Damage
			if (thing->eflags & MFE_VERTICALFLIP)
				thing->z -= thing->height;
			else
				thing->z += thing->height;

			S_StartSound(thing, thing->info->deathsound);
			P_KillMobj(thing, tmthing, tmthing);

			P_SetObjectMomZ(thing, 8*FRACUNIT, false);
			P_InstaThrust(thing, R_PointToAngle2(tmthing->x, tmthing->y, thing->x, thing->y)+ANGLE_90, 16*FRACUNIT);
		}
		else if (thing->type == MT_SSMINE_SHIELD || thing->type == MT_SSMINE)
		{
			if ((thing->target == tmthing) && (thing->threshold > 0))
				return true;

			if (tmthing->health <= 0 || thing->health <= 0)
				return true;

			// Bomb punting
			if ((thing->state >= &states[S_SSMINE1] && thing->state <= &states[S_SSMINE4])
				|| (thing->state >= &states[S_SSMINE_DEPLOY8] && thing->state <= &states[S_SSMINE_DEPLOY13]))
				P_KillMobj(thing, tmthing, tmthing);
			else
				K_PuntMine(thing, tmthing);
		}
		else if (thing->type == MT_MINEEXPLOSION && tmthing->player)
		{
			// Player Damage
			if (thing->state == &states[S_MINEEXPLOSION1])
				K_ExplodePlayer(tmthing->player, thing->target, thing);
			else
				K_SpinPlayer(tmthing->player, thing->target, 0, thing, false);

			return true;
		}
		else if (thing->type == MT_SINK)
		{
			if ((thing->target == tmthing) && (thing->threshold > 0))
				return true;

			S_StartSound(NULL, sfx_cgot); //let all players hear it.
			HU_SetCEchoFlags(0);
			HU_SetCEchoDuration(5);
			HU_DoCEcho(va("%s\\was hit by a kitchen sink.\\\\\\\\", player_names[tmthing->player-players]));
			I_OutputMsg("%s was hit by a kitchen sink.\n", player_names[tmthing->player-players]);
			P_DamageMobj(tmthing, thing, thing->target, 10000);
			P_KillMobj(thing, tmthing, tmthing);
		}

		return true;
	}

	//}

	if ((thing->type == MT_FALLINGROCK && (tmthing->player || tmthing->type == MT_FALLINGROCK))
		|| (tmthing->type == MT_FALLINGROCK && (thing->player || thing->type == MT_FALLINGROCK)))
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath
		K_KartBouncing(thing, tmthing, false, false);
	}

	if ((thing->type == MT_SPRINGSHELL || thing->type == MT_YELLOWSHELL) && thing->health > 0
	 && (tmthing->player || (tmthing->flags & MF_PUSHABLE)) && tmthing->health > 0)
	{
		// Multiplying by -1 inherently flips "less than" and "greater than"
		fixed_t tmz     = ((thing->eflags & MFE_VERTICALFLIP) ? -(tmthing->z + tmthing->height) : tmthing->z);
		fixed_t tmznext = ((thing->eflags & MFE_VERTICALFLIP) ? -tmthing->momz : tmthing->momz) + tmz;
		fixed_t thzh    = ((thing->eflags & MFE_VERTICALFLIP) ? -thing->z : thing->z + thing->height);
		//fixed_t sprarea = FixedMul(8*FRACUNIT, thing->scale) * P_MobjFlip(thing);

		//if ((tmznext <= thzh && tmz > thzh) || (tmznext > thzh - sprarea && tmznext < thzh))
		if (tmznext <= thzh)
		{
			P_DoSpring(thing, tmthing);
		//	return true;
		}
		//else if (tmz > thzh - sprarea && tmz < thzh) // Don't damage people springing up / down
			return true;
	}
	// missiles can hit other things
	if (tmthing->flags & MF_MISSILE || tmthing->type == MT_SHELL)
	{
		// see if it went over / under
		if (tmthing->z > thing->z + thing->height)
			return true; // overhead
		if (tmthing->z + tmthing->height < thing->z)
			return true; // underneath

		if (tmthing->type != MT_SHELL && tmthing->target && tmthing->target->type == thing->type)
		{
			// Don't hit same species as originator.
			if (thing == tmthing->target)
				return true;

			if (thing->type != MT_PLAYER)
			{
				// Explode, but do no damage.
				// Let players missile other players.
				return false;
			}
		}

		// Special case for bounce rings so they don't get caught behind solid objects.
		if ((tmthing->type == MT_THROWNBOUNCE && tmthing->fuse > 8*TICRATE) && thing->flags & MF_SOLID)
			return true;

		if (!(thing->flags & MF_SHOOTABLE))
		{
			// didn't do any damage
			return !(thing->flags & MF_SOLID);
		}

		if (tmthing->flags & MF_MISSILE && thing->player && tmthing->target && tmthing->target->player
		&& thing->player->ctfteam == tmthing->target->player->ctfteam
		&& thing->player->pflags & PF_CARRIED && thing->tracer == tmthing->target)
			return true; // Don't give rings to your carry player by accident.

		if (tmthing->type == MT_SHELL && tmthing->threshold > TICRATE)
			return true;
		// damage / explode
		if (tmthing->flags & MF_ENEMY) // An actual ENEMY! (Like the deton, for example)
			P_DamageMobj(thing, tmthing, tmthing, 1);
		else
			P_DamageMobj(thing, tmthing, tmthing->target, 1);

		// don't traverse any more

		return (tmthing->type == MT_SHELL);
	}

	if (thing->flags & MF_PUSHABLE && (tmthing->player || tmthing->flags & MF_PUSHABLE)
		&& tmthing->z + tmthing->height > thing->z && tmthing->z < thing->z + thing->height
		&& !(netgame && tmthing->player && tmthing->player->spectator)) // Push thing!
	{
		if (thing->flags2 & MF2_SLIDEPUSH) // Make it slide
		{
			if (tmthing->momy > 0 && tmthing->momy > FixedMul(4*FRACUNIT, thing->scale) && tmthing->momy > thing->momy)
			{
				thing->momy += FixedMul(PUSHACCEL, thing->scale);
				tmthing->momy -= FixedMul(PUSHACCEL, thing->scale);
			}
			else if (tmthing->momy < 0 && tmthing->momy < FixedMul(-4*FRACUNIT, thing->scale)
				&& tmthing->momy < thing->momy)
			{
				thing->momy -= FixedMul(PUSHACCEL, thing->scale);
				tmthing->momy += FixedMul(PUSHACCEL, thing->scale);
			}
			if (tmthing->momx > 0 && tmthing->momx > FixedMul(4*FRACUNIT, thing->scale)
				&& tmthing->momx > thing->momx)
			{
				thing->momx += FixedMul(PUSHACCEL, thing->scale);
				tmthing->momx -= FixedMul(PUSHACCEL, thing->scale);
			}
			else if (tmthing->momx < 0 && tmthing->momx < FixedMul(-4*FRACUNIT, thing->scale)
				&& tmthing->momx < thing->momx)
			{
				thing->momx -= FixedMul(PUSHACCEL, thing->scale);
				tmthing->momx += FixedMul(PUSHACCEL, thing->scale);
			}

			if (thing->momx > FixedMul(thing->info->speed, thing->scale))
				thing->momx = FixedMul(thing->info->speed, thing->scale);
			else if (thing->momx < -FixedMul(thing->info->speed, thing->scale))
				thing->momx = -FixedMul(thing->info->speed, thing->scale);
			if (thing->momy > FixedMul(thing->info->speed, thing->scale))
				thing->momy = FixedMul(thing->info->speed, thing->scale);
			else if (thing->momy < -FixedMul(thing->info->speed, thing->scale))
				thing->momy = -FixedMul(thing->info->speed, thing->scale);
		}
		else
		{
			if (tmthing->momx > FixedMul(4*FRACUNIT, thing->scale))
				tmthing->momx = FixedMul(4*FRACUNIT, thing->scale);
			else if (tmthing->momx < FixedMul(-4*FRACUNIT, thing->scale))
				tmthing->momx = FixedMul(-4*FRACUNIT, thing->scale);
			if (tmthing->momy > FixedMul(4*FRACUNIT, thing->scale))
				tmthing->momy = FixedMul(4*FRACUNIT, thing->scale);
			else if (tmthing->momy < FixedMul(-4*FRACUNIT, thing->scale))
				tmthing->momy = FixedMul(-4*FRACUNIT, thing->scale);

			thing->momx = tmthing->momx;
			thing->momy = tmthing->momy;
		}

		if (thing->type != MT_GARGOYLE || P_IsObjectOnGround(thing))
			S_StartSound(thing, thing->info->activesound);

		P_SetTarget(&thing->target, tmthing);
	}

	// check for special pickup
	if (thing->flags & MF_SPECIAL && tmthing->player)
	{
		P_TouchSpecialThing(thing, tmthing, true); // can remove thing
		return true;
	}
	// check again for special pickup
	if (tmthing->flags & MF_SPECIAL && thing->player)
	{
		P_TouchSpecialThing(tmthing, thing, true); // can remove thing
		return true;
	}

	// Sprite Spikes!
	// Do not return because solidity code comes below.
	if (tmthing->type == MT_SPIKE && tmthing->flags & MF_SOLID && thing->player) // moving spike rams into player?!
	{
		if (tmthing->eflags & MFE_VERTICALFLIP)
		{
			if (thing->z + thing->height <= tmthing->z + FixedMul(FRACUNIT, tmthing->scale)
			&& thing->z + thing->height + thing->momz  >= tmthing->z + FixedMul(FRACUNIT, tmthing->scale) + tmthing->momz)
				P_DamageMobj(thing, tmthing, tmthing, 1);
		}
		else if (thing->z >= tmthing->z + tmthing->height - FixedMul(FRACUNIT, tmthing->scale)
		&& thing->z + thing->momz <= tmthing->z + tmthing->height - FixedMul(FRACUNIT, tmthing->scale) + tmthing->momz)
			P_DamageMobj(thing, tmthing, tmthing, 1);
	}
	else if (thing->type == MT_SPIKE && thing->flags & MF_SOLID && tmthing->player) // unfortunate player falls into spike?!
	{
		if (thing->eflags & MFE_VERTICALFLIP)
		{
			if (tmthing->z + tmthing->height <= thing->z - FixedMul(FRACUNIT, thing->scale)
			&& tmthing->z + tmthing->height + tmthing->momz >= thing->z - FixedMul(FRACUNIT, thing->scale))
				P_DamageMobj(tmthing, thing, thing, 1);
		}
		else if (tmthing->z >= thing->z + thing->height + FixedMul(FRACUNIT, thing->scale)
		&& tmthing->z + tmthing->momz <= thing->z + thing->height + FixedMul(FRACUNIT, thing->scale))
			P_DamageMobj(tmthing, thing, thing, 1);
	}

	if (thing->flags & MF_PUSHABLE)
	{
		if (tmthing->type == MT_FAN || tmthing->type == MT_STEAM)
			P_DoFanAndGasJet(tmthing, thing);
	}

	if (tmthing->flags & MF_PUSHABLE)
	{
		if (thing->type == MT_FAN || thing->type == MT_STEAM)
			P_DoFanAndGasJet(thing, tmthing);
		else if (thing->flags & MF_SPRING)
		{
			if ( thing->z <= tmthing->z + tmthing->height
			&& tmthing->z <= thing->z + thing->height)
				iwassprung = P_DoSpring(thing, tmthing);
		}
	}

	// Damage other players when invincible
	if (tmthing->player && thing->player
	// Make sure they aren't able to damage you ANYWHERE along the Z axis, you have to be TOUCHING the person.
		&& !(thing->z + thing->height < tmthing->z || thing->z > tmthing->z + tmthing->height))
	{

		if (tmthing->scale > thing->scale + (mapobjectscale/8)) // SRB2kart - Handle squishes first!
			K_SquishPlayer(thing->player, tmthing, tmthing);
		else if (thing->scale > tmthing->scale + (mapobjectscale/8))
			K_SquishPlayer(tmthing->player, thing, tmthing);
		else if (tmthing->player->kartstuff[k_invincibilitytimer] && !thing->player->kartstuff[k_invincibilitytimer]) // SRB2kart - Then invincibility!
			P_DamageMobj(thing, tmthing, tmthing, 1);
		else if (thing->player->kartstuff[k_invincibilitytimer] && !tmthing->player->kartstuff[k_invincibilitytimer])
			P_DamageMobj(tmthing, thing, thing, 1);
		else if (tmthing->player->kartstuff[k_flamedash] && !thing->player->kartstuff[k_flamedash]) // SRB2kart - Then flame shield!
			P_DamageMobj(thing, tmthing, tmthing, 1);
		else if (thing->player->kartstuff[k_flamedash] && !tmthing->player->kartstuff[k_flamedash])
			P_DamageMobj(tmthing, thing, thing, 1);

	}

	if (thing->player)
	{
		if (tmthing->type == MT_FAN || tmthing->type == MT_STEAM)
			P_DoFanAndGasJet(tmthing, thing);
	}

	if (tmthing->player) // Is the moving/interacting object the player?
	{
		if (!tmthing->health)
			return true;

		if (thing->type == MT_FAN || thing->type == MT_STEAM)
			P_DoFanAndGasJet(thing, tmthing);
		else if (thing->flags & MF_SPRING)
		{
			if ( thing->z <= tmthing->z + tmthing->height
			&& tmthing->z <= thing->z + thing->height)
				iwassprung = P_DoSpring(thing, tmthing);
		}
		else if (thing->player) // bounce when players collide
		{
			// see if it went over / under
			if (tmthing->z > thing->z + thing->height)
				return true; // overhead
			if (tmthing->z + tmthing->height < thing->z)
				return true; // underneath

			if (thing->player->kartstuff[k_squishedtimer] || thing->player->kartstuff[k_hyudorotimer]
				|| thing->player->kartstuff[k_justbumped] || thing->scale > tmthing->scale + (mapobjectscale/8)
				|| tmthing->player->kartstuff[k_squishedtimer] || tmthing->player->kartstuff[k_hyudorotimer]
				|| tmthing->player->kartstuff[k_justbumped] || tmthing->scale > thing->scale + (mapobjectscale/8))
			{
				return true;
			}

			if (G_BattleGametype()
				&& ((thing->player->kartstuff[k_bumper] && !tmthing->player->kartstuff[k_bumper])
				|| (tmthing->player->kartstuff[k_bumper] && !thing->player->kartstuff[k_bumper])))
			{
				return true;
			}

			if (P_IsObjectOnGround(thing) && tmthing->momz < 0)
			{
				K_KartBouncing(tmthing, thing, true, false);
				if (G_BattleGametype() && tmthing->player->kartstuff[k_pogospring])
				{
					K_StealBumper(tmthing->player, thing->player, false);
					K_SpinPlayer(thing->player, tmthing, 0, tmthing, false);
				}
			}
			else if (P_IsObjectOnGround(tmthing) && thing->momz < 0)
			{
				K_KartBouncing(thing, tmthing, true, false);
				if (G_BattleGametype() && thing->player->kartstuff[k_pogospring])
				{
					K_StealBumper(thing->player, tmthing->player, false);
					K_SpinPlayer(tmthing->player, thing, 0, thing, false);
				}
			}
			else
				K_KartBouncing(tmthing, thing, false, false);

			if (G_BattleGametype())
			{
				if ((thing->player->kartstuff[k_sneakertimer] || thing->player->kartstuff[k_paneltimer]) && !(tmthing->player->kartstuff[k_sneakertimer] || tmthing->player->kartstuff[k_paneltimer]) && !(thing->player->powers[pw_flashing])) // Don't steal bumpers while intangible
				{
					K_StealBumper(thing->player, tmthing->player, false);
					K_SpinPlayer(tmthing->player, thing, 0, tmthing, false);
				}
				else if ((tmthing->player->kartstuff[k_sneakertimer] || tmthing->player->kartstuff[k_paneltimer]) && !(thing->player->kartstuff[k_sneakertimer] || thing->player->kartstuff[k_paneltimer]) && !(tmthing->player->powers[pw_flashing]))
				{
					K_StealBumper(tmthing->player, thing->player, false);
					K_SpinPlayer(thing->player, tmthing, 0, thing, false);
				}
			}

			return true;
		}
		else if (thing->type == MT_BLUEROBRA_HEAD || thing->type == MT_BLUEROBRA_JOINT)
		{
			// see if it went over / under
			if (tmthing->z > thing->z + thing->height)
				return true; // overhead
			if (tmthing->z + tmthing->height < thing->z)
				return true; // underneath

			if (!thing->health)
				return true; // dead

			if (tmthing->player->kartstuff[k_invincibilitytimer] > 0
				|| tmthing->player->kartstuff[k_growshrinktimer] > 0)
			{
				if (thing->type == MT_BLUEROBRA_JOINT)
					P_KillMobj(thing->target, tmthing, tmthing);
				else
					P_KillMobj(thing, tmthing, tmthing);
				return true;
			}
			else
			{
				K_KartBouncing(tmthing, thing, false, true);
				return false;
			}
		}
		else if (thing->type == MT_SMK_PIPE)
		{
			// see if it went over / under
			if (tmthing->z > thing->z + thing->height)
				return true; // overhead
			if (tmthing->z + tmthing->height < thing->z)
				return true; // underneath

			if (!thing->health)
				return true; // dead

			if (tmthing->player->kartstuff[k_invincibilitytimer] > 0
				|| tmthing->player->kartstuff[k_growshrinktimer] > 0)
			{
				P_KillMobj(thing, tmthing, tmthing);
				return true; // kill
			}

			K_KartBouncing(tmthing, thing, false, true);
			return false;
		}
		else if (thing->type == MT_SMK_THWOMP)
		{
			if (!thing->health)
				return true; // dead

			if (!thwompsactive)
				return true; // not active yet

			if ((tmthing->z < thing->z) && (thing->z >= thing->movefactor-(256<<FRACBITS)))
			{
				thing->extravalue1 = 1; // purposely try to stomp on players early
				//S_StartSound(thing, sfx_s1bb);
			}

			// see if it went over / under
			if (tmthing->z > thing->z + thing->height)
				return true; // overhead
			if (tmthing->z + tmthing->height < thing->z)
				return true; // underneath

			// kill
			if (tmthing->player->kartstuff[k_invincibilitytimer] > 0
				|| tmthing->player->kartstuff[k_growshrinktimer] > 0)
			{
				P_KillMobj(thing, tmthing, tmthing);
				return true;
			}

			// continue to squish
			if (tmthing->player->kartstuff[k_squishedtimer])
			{
				tmthing->player->kartstuff[k_squishedtimer] = 2*TICRATE;
				tmthing->player->powers[pw_flashing] = K_GetKartFlashing(tmthing->player);
				return true;
			}

			// no interaction
			if (tmthing->player->powers[pw_flashing] > 0 || tmthing->player->kartstuff[k_hyudorotimer] > 0
				|| tmthing->player->kartstuff[k_spinouttimer] > 0) //|| tmthing->player->kartstuff[k_squishedtimer] > 0
				return true;

			// collide
			if (tmthing->z < thing->z && thing->momz < 0)
				K_SquishPlayer(tmthing->player, thing, thing);
			else
			{
				if (thing->flags2 & MF2_AMBUSH)
					P_DamageMobj(tmthing, thing, thing, 1);
				K_KartBouncing(tmthing, thing, false, true);
			}

			return false;
		}
		else if (thing->flags & MF_SOLID)
		{
			// see if it went over / under
			if (tmthing->z > thing->z + thing->height)
				return true; // overhead
			if (tmthing->z + tmthing->height < thing->z)
				return true; // underneath

			if (P_IsObjectOnGround(thing) && tmthing->momz < 0)
				K_KartBouncing(tmthing, thing, true, true);
			else
				K_KartBouncing(tmthing, thing, false, true);

			return false;
		}
		// Are you touching the side of the object you're interacting with?
		else if (thing->z - FixedMul(FRACUNIT, thing->scale) <= tmthing->z + tmthing->height
			&& thing->z + thing->height + FixedMul(FRACUNIT, thing->scale) >= tmthing->z)
		{
			if (thing->flags & MF_MONITOR
				&& tmthing->player->pflags & (PF_JUMPED|PF_SPINNING|PF_GLIDING))
			{
				SINT8 flipval = P_MobjFlip(thing); // Save this value in case monitor gets removed.
				fixed_t *momz = &tmthing->momz; // tmthing gets changed by P_DamageMobj, so we need a new pointer?! X_x;;
				P_DamageMobj(thing, tmthing, tmthing, 1); // break the monitor
				// Going down? Then bounce back up.
				if ((P_MobjWasRemoved(thing) // Monitor was removed
					|| !thing->health) // or otherwise popped
				&& (flipval*(*momz) < 0)) // monitor is on the floor and you're going down, or on the ceiling and you're going up
					*momz = -*momz; // Therefore, you should be thrust in the opposite direction, vertically.
				return false;
			}
		}
	}

	if (thing->flags & MF_SPRING && (tmthing->player || tmthing->flags & MF_PUSHABLE))
	{
		if (iwassprung) // this spring caused you to gain MFE_SPRUNG just now...
			return false; // "cancel" P_TryMove via blocking so you keep your current position
	}
	else if (tmthing->flags & MF_SPRING && (thing->player || thing->flags & MF_PUSHABLE))
		; // Fix a few nasty spring-jumping bugs that happen sometimes.
	// Monitors are not treated as solid to players who are jumping, spinning or gliding,
	// unless it's a CTF team monitor and you're on the wrong team

	// z checking at last
	// Treat noclip things as non-solid!
	else if ((thing->flags & (MF_SOLID|MF_NOCLIP)) == MF_SOLID
		&& (tmthing->flags & (MF_SOLID|MF_NOCLIP)) == MF_SOLID)
	{
		fixed_t topz, tmtopz;

		if (tmthing->eflags & MFE_VERTICALFLIP)
		{
			// pass under
			tmtopz = tmthing->z;

			if (tmtopz > thing->z + thing->height)
			{
				if (thing->z + thing->height > tmfloorz)
				{
					tmfloorz = thing->z + thing->height;
					tmfloorslope = NULL;
				}
				return true;
			}

			topz = thing->z - thing->scale; // FixedMul(FRACUNIT, thing->scale), but thing->scale == FRACUNIT in base scale anyways

			if (thing->flags & MF_SPRING)
				;
			// block only when jumping not high enough,
			// (dont climb max. 24units while already in air)
			// since return false doesn't handle momentum properly,
			// we lie to P_TryMove() so it's always too high
			else if (tmthing->player && tmthing->z + tmthing->height > topz
				&& tmthing->z + tmthing->height < tmthing->ceilingz)
			{
				tmfloorz = tmceilingz = topz; // block while in air
				tmceilingslope = NULL;
				tmfloorthing = thing; // needed for side collision
			}
			else if (topz < tmceilingz && tmthing->z <= thing->z+thing->height)
			{
				tmceilingz = topz;
				tmceilingslope = NULL;
				tmfloorthing = thing; // thing we may stand on
			}
		}
		else
		{
			// pass under
			tmtopz = tmthing->z + tmthing->height;

			if (tmtopz < thing->z)
			{
				if (thing->z < tmceilingz)
				{
					tmceilingz = thing->z;
					tmceilingslope = NULL;
				}
				return true;
			}

			topz = thing->z + thing->height + thing->scale; // FixedMul(FRACUNIT, thing->scale), but thing->scale == FRACUNIT in base scale anyways

			if (thing->flags & MF_SPRING)
				;
			// block only when jumping not high enough,
			// (dont climb max. 24units while already in air)
			// since return false doesn't handle momentum properly,
			// we lie to P_TryMove() so it's always too high
			else if (tmthing->player && tmthing->z < topz
				&& tmthing->z > tmthing->floorz)
			{
				tmfloorz = tmceilingz = topz; // block while in air
				tmfloorslope = NULL;
				tmfloorthing = thing; // needed for side collision
			}
			else if (topz > tmfloorz && tmthing->z+tmthing->height >= thing->z)
			{
				tmfloorz = topz;
				tmfloorslope = NULL;
				tmfloorthing = thing; // thing we may stand on
			}
		}
	}

	// not solid not blocked
	return true;
}

// PIT_CheckCameraLine
// Adjusts tmfloorz and tmceilingz as lines are contacted - FOR CAMERA ONLY
static boolean PIT_CheckCameraLine(line_t *ld)
{
	if (ld->polyobj && !(ld->polyobj->flags & POF_SOLID))
		return true;

	if (tmbbox[BOXRIGHT] <= ld->bbox[BOXLEFT] || tmbbox[BOXLEFT] >= ld->bbox[BOXRIGHT]
		|| tmbbox[BOXTOP] <= ld->bbox[BOXBOTTOM] || tmbbox[BOXBOTTOM] >= ld->bbox[BOXTOP])
	{
		return true;
	}

	if (P_BoxOnLineSide(tmbbox, ld) != -1)
		return true;

	// A line has been hit

	// The moving thing's destination position will cross
	// the given line.
	// If this should not be allowed, return false.
	// If the line is special, keep track of it
	// to process later if the move is proven ok.
	// NOTE: specials are NOT sorted by order,
	// so two special lines that are only 8 pixels apart
	// could be crossed in either order.

	// this line is out of the if so upper and lower textures can be hit by a splat
	blockingline = ld;
	if (!ld->backsector) // one sided line
	{
		if (P_PointOnLineSide(mapcampointer->x, mapcampointer->y, ld))
			return true; // don't hit the back side
		return false;
	}

	// set openrange, opentop, openbottom
	P_CameraLineOpening(ld);

	// adjust floor / ceiling heights
	if (opentop < tmceilingz)
	{
		tmceilingz = opentop;
		ceilingline = ld;
	}

	if (openbottom > tmfloorz)
	{
		tmfloorz = openbottom;
	}

	if (highceiling > tmdrpoffceilz)
		tmdrpoffceilz = highceiling;

	if (lowfloor < tmdropoffz)
		tmdropoffz = lowfloor;

	return true;
}

//
// PIT_CheckLine
// Adjusts tmfloorz and tmceilingz as lines are contacted
//
static boolean PIT_CheckLine(line_t *ld)
{
	if (ld->polyobj && !(ld->polyobj->flags & POF_SOLID))
		return true;

	if (tmbbox[BOXRIGHT] <= ld->bbox[BOXLEFT] || tmbbox[BOXLEFT] >= ld->bbox[BOXRIGHT]
	|| tmbbox[BOXTOP] <= ld->bbox[BOXBOTTOM] || tmbbox[BOXBOTTOM] >= ld->bbox[BOXTOP])
		return true;

	if (P_BoxOnLineSide(tmbbox, ld) != -1)
		return true;

	if (tmthing->flags & MF_PAPERCOLLISION) // Caution! Turning whilst up against a wall will get you stuck. You probably shouldn't give the player this flag.
	{
		fixed_t cosradius, sinradius;
		cosradius = FixedMul(tmthing->radius, FINECOSINE(tmthing->angle>>ANGLETOFINESHIFT));
		sinradius = FixedMul(tmthing->radius, FINESINE(tmthing->angle>>ANGLETOFINESHIFT));
		if (P_PointOnLineSide(tmx - cosradius, tmy - sinradius, ld)
		== P_PointOnLineSide(tmx + cosradius, tmy + sinradius, ld))
			return true; // the line doesn't cross between collider's start or end
	}

	// A line has been hit

	// The moving thing's destination position will cross
	// the given line.
	// If this should not be allowed, return false.
	// If the line is special, keep track of it
	// to process later if the move is proven ok.
	// NOTE: specials are NOT sorted by order,
	// so two special lines that are only 8 pixels apart
	// could be crossed in either order.

	// this line is out of the if so upper and lower textures can be hit by a splat
	blockingline = ld;
	if (!ld->backsector) // one sided line
	{
		if (P_PointOnLineSide(tmthing->x, tmthing->y, ld))
			return true; // don't hit the back side
		return false;
	}

	// missiles can cross uncrossable lines
	if (!(tmthing->flags & MF_MISSILE))
	{
		if (ld->flags & ML_IMPASSIBLE) // block objects from moving through this linedef.
			return false;
		if ((tmthing->flags & (MF_ENEMY|MF_BOSS)) && ld->flags & ML_BLOCKMONSTERS)
			return false; // block monsters only
	}

	// set openrange, opentop, openbottom
	P_LineOpening(ld, tmthing);

	// adjust floor / ceiling heights
	if (opentop < tmceilingz)
	{
		tmceilingz = opentop;
		ceilingline = ld;
		tmceilingslope = opentopslope;
	}

	if (openbottom > tmfloorz)
	{
		tmfloorz = openbottom;
		tmfloorslope = openbottomslope;
	}

	if (highceiling > tmdrpoffceilz)
		tmdrpoffceilz = highceiling;

	if (lowfloor < tmdropoffz)
		tmdropoffz = lowfloor;

	return true;
}

// =========================================================================
//                         MOVEMENT CLIPPING
// =========================================================================

//
// P_CheckPosition
// This is purely informative, nothing is modified
// (except things picked up).
//
// in:
//  a mobj_t (can be valid or invalid)
//  a position to be checked
//   (doesn't need to be related to the mobj_t->x,y)
//
// during:
//  special things are touched if MF_PICKUP
//  early out on solid lines?
//
// out:
//  newsubsec
//  tmfloorz
//  tmceilingz
//  tmdropoffz
//  tmdrpoffceilz
//   the lowest point contacted
//   (monsters won't move to a dropoff)
//  speciallines[]
//  numspeciallines
//

// tmfloorz
//     the nearest floor or thing's top under tmthing
// tmceilingz
//     the nearest ceiling or thing's bottom over tmthing
//
boolean P_CheckPosition(mobj_t *thing, fixed_t x, fixed_t y)
{
	INT32 xl, xh, yl, yh, bx, by;
	subsector_t *newsubsec;
	boolean blockval = true;
	
	ps_checkposition_calls.value.i++;

	I_Assert(thing != NULL);
#ifdef PARANOIA
	if (P_MobjWasRemoved(thing))
		I_Error("Previously-removed Thing of type %u crashes P_CheckPosition!", thing->type);
#endif

	P_SetTarget(&tmthing, thing);
	tmflags = thing->flags;

	tmx = x;
	tmy = y;

	tmbbox[BOXTOP] = y + tmthing->radius;
	tmbbox[BOXBOTTOM] = y - tmthing->radius;
	tmbbox[BOXRIGHT] = x + tmthing->radius;
	tmbbox[BOXLEFT] = x - tmthing->radius;

	newsubsec = R_PointInSubsector(x, y);
	ceilingline = blockingline = NULL;

	// The base floor / ceiling is from the subsector
	// that contains the point.
	// Any contacted lines the step closer together
	// will adjust them.
	tmfloorz = tmdropoffz = P_GetFloorZ(thing, newsubsec->sector, x, y, NULL); //newsubsec->sector->floorheight;
	tmceilingz = P_GetCeilingZ(thing, newsubsec->sector, x, y, NULL); //newsubsec->sector->ceilingheight;
	tmfloorslope = newsubsec->sector->f_slope;
	tmceilingslope = newsubsec->sector->c_slope;

	// Check list of fake floors and see if tmfloorz/tmceilingz need to be altered.
	if (newsubsec->sector->ffloors)
	{
		ffloor_t *rover;
		fixed_t delta1, delta2;
		INT32 thingtop = thing->z + thing->height;

		for (rover = newsubsec->sector->ffloors; rover; rover = rover->next)
		{
			fixed_t topheight, bottomheight;

			if (!(rover->flags & FF_EXISTS))
				continue;

			topheight = P_GetFOFTopZ(thing, newsubsec->sector, rover, x, y, NULL);
			bottomheight = P_GetFOFBottomZ(thing, newsubsec->sector, rover, x, y, NULL);

			if ((rover->flags & (FF_SWIMMABLE|FF_GOOWATER)) == (FF_SWIMMABLE|FF_GOOWATER) && !(thing->flags & MF_NOGRAVITY))
			{
				// If you're inside goowater and slowing down
				fixed_t sinklevel = FixedMul(thing->info->height/6, thing->scale);
				fixed_t minspeed = FixedMul(thing->info->height/9, thing->scale);
				if (thing->z < topheight && bottomheight < thingtop
				&& abs(thing->momz) < minspeed)
				{
					// Oh no! The object is stick in between the surface of the goo and sinklevel! help them out!
					if (!(thing->eflags & MFE_VERTICALFLIP) && thing->z > topheight - sinklevel
					&& thing->momz >= 0 && thing->momz < (minspeed>>2))
						thing->momz += minspeed>>2;
					else if (thing->eflags & MFE_VERTICALFLIP && thingtop < bottomheight + sinklevel
					&& thing->momz <= 0 && thing->momz > -(minspeed>>2))
						thing->momz -= minspeed>>2;

					// Land on the top or the bottom, depending on gravity flip.
					if (!(thing->eflags & MFE_VERTICALFLIP) && thing->z >= topheight - sinklevel && thing->momz <= 0)
					{
						if (tmfloorz < topheight - sinklevel) {
							tmfloorz = topheight - sinklevel;
							tmfloorslope = *rover->t_slope;
						}
					}
					else if (thing->eflags & MFE_VERTICALFLIP && thingtop <= bottomheight + sinklevel && thing->momz >= 0)
					{
						if (tmceilingz > bottomheight + sinklevel) {
							tmceilingz = bottomheight + sinklevel;
							tmceilingslope = *rover->b_slope;
						}
					}
				}
				continue;
			}

			if (thing->player && P_CheckSolidLava(thing, rover))
				;
			else if (thing->type == MT_SKIM && (rover->flags & FF_SWIMMABLE))
				;
			else if (!((rover->flags & FF_BLOCKPLAYER && thing->player)
				|| (rover->flags & FF_BLOCKOTHERS && !thing->player)
				|| rover->flags & FF_QUICKSAND))
				continue;

			if (rover->flags & FF_QUICKSAND)
			{
				if (thing->z < topheight && bottomheight < thingtop)
				{
					if (tmfloorz < thing->z) {
						tmfloorz = thing->z;
						tmfloorslope = NULL;
					}
				}
				// Quicksand blocks never change heights otherwise.
				continue;
			}

			delta1 = thing->z - (bottomheight
				+ ((topheight - bottomheight)/2));
			delta2 = thingtop - (bottomheight
				+ ((topheight - bottomheight)/2));

			if (topheight > tmfloorz && abs(delta1) < abs(delta2)
				&& !(rover->flags & FF_REVERSEPLATFORM))
			{
				tmfloorz = tmdropoffz = topheight;
				tmfloorslope = *rover->t_slope;
			}
			if (bottomheight < tmceilingz && abs(delta1) >= abs(delta2)
				&& !(rover->flags & FF_PLATFORM)
				&& !(thing->type == MT_SKIM && (rover->flags & FF_SWIMMABLE)))
			{
				tmceilingz = tmdrpoffceilz = bottomheight;
				tmceilingslope = *rover->b_slope;
			}
		}
	}

	// The bounding box is extended by MAXRADIUS
	// because mobj_ts are grouped into mapblocks
	// based on their origin point, and can overlap
	// into adjacent blocks by up to MAXRADIUS units.

	xl = (unsigned)(tmbbox[BOXLEFT] - bmaporgx - MAXRADIUS)>>MAPBLOCKSHIFT;
	xh = (unsigned)(tmbbox[BOXRIGHT] - bmaporgx + MAXRADIUS)>>MAPBLOCKSHIFT;
	yl = (unsigned)(tmbbox[BOXBOTTOM] - bmaporgy - MAXRADIUS)>>MAPBLOCKSHIFT;
	yh = (unsigned)(tmbbox[BOXTOP] - bmaporgy + MAXRADIUS)>>MAPBLOCKSHIFT;

	BMBOUNDFIX(xl, xh, yl, yh);

	// Check polyobjects and see if tmfloorz/tmceilingz need to be altered
	{
		validcount++;

		for (by = yl; by <= yh; by++)
			for (bx = xl; bx <= xh; bx++)
			{
				INT32 offset;
				polymaplink_t *plink; // haleyjd 02/22/06

				if (bx < 0 || by < 0 || bx >= bmapwidth || by >= bmapheight)
					continue;

				offset = by*bmapwidth + bx;

				// haleyjd 02/22/06: consider polyobject lines
				plink = polyblocklinks[offset];

				while (plink)
				{
					polyobj_t *po = plink->po;

					if (po->validcount != validcount) // if polyobj hasn't been checked
					{
						sector_t *polysec;
						fixed_t delta1, delta2, thingtop;
						fixed_t polytop, polybottom;

						po->validcount = validcount;

						if (!P_BBoxInsidePolyobj(po, tmbbox)
							|| !(po->flags & POF_SOLID))
						{
							plink = (polymaplink_t *)(plink->link.next);
							continue;
						}

						// We're inside it! Yess...
						polysec = po->lines[0]->backsector;

						if (po->flags & POF_CLIPPLANES)
						{
							polytop = polysec->ceilingheight;
							polybottom = polysec->floorheight;
						}
						else
						{
							polytop = INT32_MAX;
							polybottom = INT32_MIN;
						}

						thingtop = thing->z + thing->height;
						delta1 = thing->z - (polybottom + ((polytop - polybottom)/2));
						delta2 = thingtop - (polybottom + ((polytop - polybottom)/2));

						if (polytop > tmfloorz && abs(delta1) < abs(delta2)) {
							tmfloorz = tmdropoffz = polytop;
							tmfloorslope = NULL;
						}

						if (polybottom < tmceilingz && abs(delta1) >= abs(delta2)) {
							tmceilingz = tmdrpoffceilz = polybottom;
							tmceilingslope = NULL;
						}
					}
					plink = (polymaplink_t *)(plink->link.next);
				}
			}
	}

	// tmfloorthing is set when tmfloorz comes from a thing's top
	tmfloorthing = NULL;
	tmhitthing = NULL;

	validcount++;

	if (tmflags & MF_NOCLIP)
		return true;

	// Check things first, possibly picking things up.

	// MF_NOCLIPTHING: used by camera to not be blocked by things
	if (!(thing->flags & MF_NOCLIPTHING))
	{
		for (bx = xl; bx <= xh; bx++)
			for (by = yl; by <= yh; by++)
			{
				if (!P_BlockThingsIterator(bx, by, PIT_CheckThing))
					blockval = false;
				if (P_MobjWasRemoved(tmthing))
					return false;
			}
	}

	validcount++;

	// check lines
	for (bx = xl; bx <= xh; bx++)
		for (by = yl; by <= yh; by++)
			if (!P_BlockLinesIterator(bx, by, PIT_CheckLine))
				blockval = false;

	return blockval;
}

static const fixed_t hoopblockdist = 16*FRACUNIT + 8*FRACUNIT;
static const fixed_t hoophalfheight = (56*FRACUNIT)/2;

// P_CheckPosition optimized for the MT_HOOPCOLLIDE object. This needs to be as fast as possible!
void P_CheckHoopPosition(mobj_t *hoopthing, fixed_t x, fixed_t y, fixed_t z, fixed_t radius)
{
	INT32 i;

	(void)radius; //unused
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || !players[i].mo || players[i].spectator)
			continue;

		if (abs(players[i].mo->x - x) >= hoopblockdist ||
			abs(players[i].mo->y - y) >= hoopblockdist ||
			abs((players[i].mo->z+hoophalfheight) - z) >= hoopblockdist)
			continue; // didn't hit it

		// can remove thing
		P_TouchSpecialThing(hoopthing, players[i].mo, false);
		break;
	}

	return;
}

//
// P_CheckCameraPosition
//
boolean P_CheckCameraPosition(fixed_t x, fixed_t y, camera_t *thiscam)
{
	INT32 xl, xh, yl, yh, bx, by;
	subsector_t *newsubsec;

	tmx = x;
	tmy = y;

	tmbbox[BOXTOP] = y + thiscam->radius;
	tmbbox[BOXBOTTOM] = y - thiscam->radius;
	tmbbox[BOXRIGHT] = x + thiscam->radius;
	tmbbox[BOXLEFT] = x - thiscam->radius;

	newsubsec = R_PointInSubsector(x, y);
	ceilingline = blockingline = NULL;

	mapcampointer = thiscam;

	if (GETSECSPECIAL(newsubsec->sector->special, 4) == 12)
	{ // Camera noclip on entire sector.
		tmfloorz = tmdropoffz = thiscam->z;
		tmceilingz = tmdrpoffceilz = thiscam->z + thiscam->height;
		return true;
	}

	// The base floor / ceiling is from the subsector
	// that contains the point.
	// Any contacted lines the step closer together
	// will adjust them.
	tmfloorz = tmdropoffz = P_CameraGetFloorZ(thiscam, newsubsec->sector, x, y, NULL);

	tmceilingz = P_CameraGetCeilingZ(thiscam, newsubsec->sector, x, y, NULL);

	// Cameras use the heightsec's heights rather then the actual sector heights.
	// If you can see through it, why not move the camera through it too?
	if (newsubsec->sector->heightsec >= 0)
	{
		tmfloorz = tmdropoffz = sectors[newsubsec->sector->heightsec].floorheight;
		tmceilingz = tmdrpoffceilz = sectors[newsubsec->sector->heightsec].ceilingheight;
	}

	// Use preset camera clipping heights if set with Sector Special Parameters whose control sector has Camera Intangible special -Red
	if (newsubsec->sector->camsec >= 0)
	{
		tmfloorz = tmdropoffz = sectors[newsubsec->sector->camsec].floorheight;
		tmceilingz = tmdrpoffceilz = sectors[newsubsec->sector->camsec].ceilingheight;
	}

	// Check list of fake floors and see if tmfloorz/tmceilingz need to be altered.
	if (newsubsec->sector->ffloors)
	{
		ffloor_t *rover;
		fixed_t delta1, delta2;
		INT32 thingtop = thiscam->z + thiscam->height;

		for (rover = newsubsec->sector->ffloors; rover; rover = rover->next)
		{
			fixed_t topheight, bottomheight;
			if (!(rover->flags & FF_BLOCKOTHERS) || !(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERALL) || GETSECSPECIAL(rover->master->frontsector->special, 4) == 12)
				continue;

			topheight = P_CameraGetFOFTopZ(thiscam, newsubsec->sector, rover, x, y, NULL);
			bottomheight = P_CameraGetFOFBottomZ(thiscam, newsubsec->sector, rover, x, y, NULL);

			delta1 = thiscam->z - (bottomheight
				+ ((topheight - bottomheight)/2));
			delta2 = thingtop - (bottomheight
				+ ((topheight - bottomheight)/2));
			if (topheight > tmfloorz && abs(delta1) < abs(delta2))
			{
				tmfloorz = tmdropoffz = topheight;
			}
			if (bottomheight < tmceilingz && abs(delta1) >= abs(delta2))
			{
				tmceilingz = tmdrpoffceilz = bottomheight;
			}
		}
	}

	// The bounding box is extended by MAXRADIUS
	// because mobj_ts are grouped into mapblocks
	// based on their origin point, and can overlap
	// into adjacent blocks by up to MAXRADIUS units.

	xl = (unsigned)(tmbbox[BOXLEFT] - bmaporgx)>>MAPBLOCKSHIFT;
	xh = (unsigned)(tmbbox[BOXRIGHT] - bmaporgx)>>MAPBLOCKSHIFT;
	yl = (unsigned)(tmbbox[BOXBOTTOM] - bmaporgy)>>MAPBLOCKSHIFT;
	yh = (unsigned)(tmbbox[BOXTOP] - bmaporgy)>>MAPBLOCKSHIFT;

	BMBOUNDFIX(xl, xh, yl, yh);

	// Check polyobjects and see if tmfloorz/tmceilingz need to be altered
	{
		validcount++;

		for (by = yl; by <= yh; by++)
			for (bx = xl; bx <= xh; bx++)
			{
				INT32 offset;
				polymaplink_t *plink; // haleyjd 02/22/06

				if (bx < 0 || by < 0 || bx >= bmapwidth || by >= bmapheight)
					continue;

				offset = by*bmapwidth + bx;

				// haleyjd 02/22/06: consider polyobject lines
				plink = polyblocklinks[offset];

				while (plink)
				{
					polyobj_t *po = plink->po;

					if (po->validcount != validcount) // if polyobj hasn't been checked
					{
						sector_t *polysec;
						fixed_t delta1, delta2, thingtop;
						fixed_t polytop, polybottom;

						po->validcount = validcount;

						if (!P_PointInsidePolyobj(po, x, y) || !(po->flags & POF_SOLID))
						{
							plink = (polymaplink_t *)(plink->link.next);
							continue;
						}

						// We're inside it! Yess...
						polysec = po->lines[0]->backsector;

						if (GETSECSPECIAL(polysec->special, 4) == 12)
						{ // Camera noclip polyobj.
							plink = (polymaplink_t *)(plink->link.next);
							continue;
						}

						if (po->flags & POF_CLIPPLANES)
						{
							polytop = polysec->ceilingheight;
							polybottom = polysec->floorheight;
						}
						else
						{
							polytop = INT32_MAX;
							polybottom = INT32_MIN;
						}

						thingtop = thiscam->z + thiscam->height;
						delta1 = thiscam->z - (polybottom + ((polytop - polybottom)/2));
						delta2 = thingtop - (polybottom + ((polytop - polybottom)/2));

						if (polytop > tmfloorz && abs(delta1) < abs(delta2))
							tmfloorz = tmdropoffz = polytop;

						if (polybottom < tmceilingz && abs(delta1) >= abs(delta2))
							tmceilingz = tmdrpoffceilz = polybottom;
					}
					plink = (polymaplink_t *)(plink->link.next);
				}
			}
	}

	// check lines
	for (bx = xl; bx <= xh; bx++)
		for (by = yl; by <= yh; by++)
			if (!P_BlockLinesIterator(bx, by, PIT_CheckCameraLine))
				return false;

	return true;
}

// The highest the camera will "step up" onto another floor.
#define MAXCAMERASTEPMOVE MAXSTEPMOVE

//
// P_TryCameraMove
//
// Attempt to move the camera to a new position
//
// Return true if the move succeeded and no sliding should be done.
//
boolean P_TryCameraMove(fixed_t x, fixed_t y, camera_t *thiscam)
{
	subsector_t *s = R_PointInSubsector(x, y);
	boolean retval = true;
	boolean itsatwodlevel = false;
	UINT8 i;

	floatok = false;

	if (dedicated) // this crashes so don't even try it
		return false;

	if (twodlevel)
		itsatwodlevel = true;
	else
	{
		for (i = 0; i <= splitscreen; i++)
		{
			if (thiscam == &camera[i] && players[displayplayers[i]].mo
				&& (players[displayplayers[i]].mo->flags2 & MF2_TWOD))
			{
				itsatwodlevel = true;
				break;
			}
		}
	}

	if (!itsatwodlevel && players[displayplayers[0]].mo)
	{
		fixed_t tryx = thiscam->x;
		fixed_t tryy = thiscam->y;

		for (i = 0; i <= splitscreen; i++)
		{
#ifndef NOCLIPCAM
			if ((thiscam == &camera[i] && (players[displayplayers[i]].pflags & PF_NOCLIP)) || (leveltime < introtime)) // Noclipping player camera noclips too!!
#else
			if (thiscam == &camera[i] && !(players[displayplayers[i]].pflags & PF_TIMEOVER)) // Time Over should not clip through walls
#endif
			{
				floatok = true;
				thiscam->floorz = thiscam->z;
				thiscam->ceilingz = thiscam->z + thiscam->height;
				thiscam->x = x;
				thiscam->y = y;
				thiscam->subsector = s;
				return true;
			}
		}

		do {
			if (x-tryx > MAXRADIUS)
				tryx += MAXRADIUS;
			else if (x-tryx < -MAXRADIUS)
				tryx -= MAXRADIUS;
			else
				tryx = x;
			if (y-tryy > MAXRADIUS)
				tryy += MAXRADIUS;
			else if (y-tryy < -MAXRADIUS)
				tryy -= MAXRADIUS;
			else
				tryy = y;

			if (!P_CheckCameraPosition(tryx, tryy, thiscam))
				return false; // solid wall or thing

			if (tmceilingz - tmfloorz < thiscam->height)
				return false; // doesn't fit

			floatok = true;

			if (tmceilingz - thiscam->z < thiscam->height)
			{
				if (s == thiscam->subsector && tmceilingz >= thiscam->z)
				{
					floatok = true;
					thiscam->floorz = tmfloorz;
					thiscam->ceilingz = tmfloorz + thiscam->height;
					thiscam->x = x;
					thiscam->y = y;
					thiscam->subsector = s;
					return true;
				}
				else
					return false; // mobj must lower itself to fit
			}

			if ((tmfloorz - thiscam->z > MAXCAMERASTEPMOVE))
				return false; // too big a step up
		} while(tryx != x || tryy != y);
	}
	else
	{
		tmfloorz = P_CameraGetFloorZ(thiscam, thiscam->subsector->sector, x, y, NULL);
		tmceilingz = P_CameraGetCeilingZ(thiscam, thiscam->subsector->sector, x, y, NULL);
	}

	// the move is ok,
	// so link the thing into its new position

	thiscam->floorz = tmfloorz;
	thiscam->ceilingz = tmceilingz;
	thiscam->x = x;
	thiscam->y = y;
	thiscam->subsector = s;

	return retval;
}

//
// PIT_PushableMoved
//
// Move things standing on top
// of pushable things being pushed.
//
static mobj_t *stand;
static fixed_t standx, standy;

boolean PIT_PushableMoved(mobj_t *thing)
{
	fixed_t blockdist;

	if (!(thing->flags & MF_SOLID)
		|| (thing->flags & MF_NOGRAVITY))
		return true; // Don't move something non-solid!

	// Only pushables are supported... in 2.0. Now players can be moved too!
	if (!(thing->flags & MF_PUSHABLE || thing->player))
		return true;

	if (thing == stand)
		return true;

	blockdist = stand->radius + thing->radius;

	if (abs(thing->x - stand->x) >= blockdist || abs(thing->y - stand->y) >= blockdist)
		return true; // didn't hit it

	if ((!(stand->eflags & MFE_VERTICALFLIP) && thing->z != stand->z + stand->height + FixedMul(FRACUNIT, stand->scale))
	|| ((stand->eflags & MFE_VERTICALFLIP) && thing->z + thing->height != stand->z - FixedMul(FRACUNIT, stand->scale)))
		return true; // Not standing on top

	if (!stand->momx && !stand->momy)
		return true;

	// Move this guy!
	if (thing->player)
	{
		// Monster Iestyn - 29/11/13
		// Ridiculous amount of newly declared stuff so players can't get stuck in walls AND so gargoyles don't break themselves at the same time either
		// These are all non-static map variables that are changed for each and every single mobj
		// See, changing player's momx/y would possibly trigger stuff as if the player were running somehow, so this must be done to keep the player standing
		// All this so players can ride gargoyles!
		boolean oldfltok = floatok;
		fixed_t oldflrz = tmfloorz;
		fixed_t oldceilz = tmceilingz;
		mobj_t *oldflrthing = tmfloorthing;
		mobj_t *oldthing = tmthing;
		line_t *oldceilline = ceilingline;
		line_t *oldblockline = blockingline;
		pslope_t *oldfslope = tmfloorslope;
		pslope_t *oldcslope = tmceilingslope;

		// Move the player
		P_TryMove(thing, thing->x+stand->momx, thing->y+stand->momy, true);

		// Now restore EVERYTHING so the gargoyle doesn't keep the player's tmstuff and break
		floatok = oldfltok;
		tmfloorz = oldflrz;
		tmceilingz = oldceilz;
		tmfloorthing = oldflrthing;
		P_SetTarget(&tmthing, oldthing);
		ceilingline = oldceilline;
		blockingline = oldblockline;
		tmfloorslope = oldfslope;
		tmceilingslope = oldcslope;
		thing->momz = stand->momz;
	}
	else
	{
		thing->momx = stand->momx;
		thing->momy = stand->momy;
		thing->momz = stand->momz;
	}
	return true;
}

//
// P_TryMove
// Attempt to move to a new position.
//
boolean P_TryMove(mobj_t *thing, fixed_t x, fixed_t y, boolean allowdropoff)
{
	fixed_t tryx = thing->x;
	fixed_t tryy = thing->y;
	fixed_t radius = thing->radius;
	fixed_t thingtop ;//= thing->z + thing->height;
	fixed_t startingonground = P_IsObjectOnGround(thing);
	floatok = false;

	// This makes sure that there are no freezes from computing extremely small movements.
	// Originally was MAXRADIUS/2, but that can cause some bad inconsistencies for small players.
	radius = max(radius, thing->scale);

	// And we also have to prevent Big Large (tm) movements, as those can skip too far
	// across slopes and cause us to fail step up checks on them when we otherwise shouldn't.
	radius = min(radius, 16 * thing->scale);

	// (This whole "step" system is flawed; it was OK before, but the addition of slopes has
	// exposed the problems with doing it like this. The right thing to do would be to use
	// raycasting for physics to fix colliding in weird order, double-checking collisions,
	// randomly colliding with slopes instead of going up them, etc. I don't feel like porting
	// that from RR, as its both a huge sweeping change and still incomplete at the time of
	// writing. Clamping radius to make our steps more precise will work just fine as long
	// as you keep all of your crazy intentions to poke any of the other deep-rooted movement
	// code to yourself. -- Sal 6/5/2023)

	do {
		if (thing->flags & MF_NOCLIP) {
			tryx = x;
			tryy = y;
		} else {
			if (x-tryx > radius)
				tryx += radius;
			else if (x-tryx < -radius)
				tryx -= radius;
			else
				tryx = x;
			if (y-tryy > radius)
				tryy += radius;
			else if (y-tryy < -radius)
				tryy -= radius;
			else
				tryy = y;
		}

		if (!P_CheckPosition(thing, tryx, tryy))
			return false; // solid wall or thing

		if (!(thing->flags & MF_NOCLIP))
		{
			//All things are affected by their scale.
			fixed_t maxstep = FixedMul(MAXSTEPMOVE, mapobjectscale);

			if (thing->player)
			{
				// If using type Section1:13, double the maxstep.
				if (P_PlayerTouchingSectorSpecial(thing->player, 1, 13)
				|| GETSECSPECIAL(R_PointInSubsector(x, y)->sector->special, 1) == 13)
					maxstep <<= 1;
				// If using type Section1:12, no maxstep. For ledges you don't want the player to climb! (see: Egg Zeppelin & SMK port walls)
				else if (P_PlayerTouchingSectorSpecial(thing->player, 1, 12)
				|| GETSECSPECIAL(R_PointInSubsector(x, y)->sector->special, 1) == 12)
					maxstep = 0;
			}

			if (thing->type == MT_SKIM)
				maxstep = 0;

			if (tmceilingz - tmfloorz < thing->height)
			{
				if (tmfloorthing)
					tmhitthing = tmfloorthing;
				return false; // doesn't fit
			}

			floatok = true;

			thingtop = thing->z + thing->height;

			// Step up
			if (thing->z < tmfloorz)
			{
				if (tmfloorz - thing->z <= maxstep)
				{
					thing->z = thing->floorz = tmfloorz;
					thing->eflags |= MFE_JUSTSTEPPEDDOWN;
				}
				else
				{
					return false; // mobj must raise itself to fit
				}
			}
			else if (tmceilingz < thingtop)
			{
				if (thingtop - tmceilingz <= maxstep)
				{
					thing->z = ( thing->ceilingz = tmceilingz ) - thing->height;
					thing->eflags |= MFE_JUSTSTEPPEDDOWN;
				}
				else
				{
					return false; // mobj must lower itself to fit
				}
			}
			else if (maxstep > 0 && !(
				thing->player && (
				P_PlayerTouchingSectorSpecial(thing->player, 1, 14)
				|| GETSECSPECIAL(R_PointInSubsector(x, y)->sector->special, 1) == 14)
				)) // Step down
			{
				// If the floor difference is MAXSTEPMOVE or less, and the sector isn't Section1:14, ALWAYS
				// step down! Formerly required a Section1:13 sector for the full MAXSTEPMOVE, but no more.

				if (thingtop == thing->ceilingz && tmceilingz > thingtop && tmceilingz - thingtop <= maxstep)
				{
					thing->z = (thing->ceilingz = tmceilingz) - thing->height;
					thing->eflags |= MFE_JUSTSTEPPEDDOWN;
				}
				else if (thing->z == thing->floorz && tmfloorz < thing->z && thing->z - tmfloorz <= maxstep)
				{
					thing->z = thing->floorz = tmfloorz;
					thing->eflags |= MFE_JUSTSTEPPEDDOWN;
				}
			}

			if (!allowdropoff && !(thing->flags & MF_FLOAT) && thing->type != MT_SKIM && !tmfloorthing)
			{
				if (thing->eflags & MFE_VERTICALFLIP)
				{
					if (tmdrpoffceilz - tmceilingz > maxstep)
						return false;
				}
				else if (tmfloorz - tmdropoffz > maxstep)
					return false; // don't stand over a dropoff
			}
		}
	} while (tryx != x || tryy != y);

	// The move is ok!

	// If it's a pushable object, check if anything is
	// standing on top and move it, too.
	if (thing->flags & MF_PUSHABLE)
	{
		INT32 bx, by, xl, xh, yl, yh;

		yh = (unsigned)(thing->y + MAXRADIUS - bmaporgy)>>MAPBLOCKSHIFT;
		yl = (unsigned)(thing->y - MAXRADIUS - bmaporgy)>>MAPBLOCKSHIFT;
		xh = (unsigned)(thing->x + MAXRADIUS - bmaporgx)>>MAPBLOCKSHIFT;
		xl = (unsigned)(thing->x - MAXRADIUS - bmaporgx)>>MAPBLOCKSHIFT;

		BMBOUNDFIX(xl, xh, yl, yh);

		stand = thing;
		standx = x;
		standy = y;

		for (by = yl; by <= yh; by++)
			for (bx = xl; bx <= xh; bx++)
				P_BlockThingsIterator(bx, by, PIT_PushableMoved);
	}

	// Link the thing into its new position
	P_UnsetThingPosition(thing);

	thing->floorz = tmfloorz;
	thing->ceilingz = tmceilingz;

	if (!(thing->flags & MF_NOCLIPHEIGHT))
	{
		// Assign thing's standingslope if needed
		if (thing->z <= tmfloorz && !(thing->eflags & MFE_VERTICALFLIP)) {
			if (!startingonground && tmfloorslope)
				P_HandleSlopeLanding(thing, tmfloorslope);

			if (thing->momz <= 0)
				thing->standingslope = tmfloorslope;
		}
		else if (thing->z+thing->height >= tmceilingz && (thing->eflags & MFE_VERTICALFLIP)) {
			if (!startingonground && tmceilingslope)
				P_HandleSlopeLanding(thing, tmceilingslope);

			if (thing->momz >= 0)
				thing->standingslope = tmceilingslope;
		}
	}
	else // don't set standingslope if you're not going to clip against it
		thing->standingslope = NULL;

	thing->x = x;
	thing->y = y;

	if (tmfloorthing)
		thing->eflags &= ~MFE_ONGROUND; // not on real floor
	else
		thing->eflags |= MFE_ONGROUND;

	P_SetThingPosition(thing);
	return true;
}

boolean P_SceneryTryMove(mobj_t *thing, fixed_t x, fixed_t y)
{
	fixed_t tryx, tryy;

	tryx = thing->x;
	tryy = thing->y;
	do {
		if (x-tryx > MAXRADIUS)
			tryx += MAXRADIUS;
		else if (x-tryx < -MAXRADIUS)
			tryx -= MAXRADIUS;
		else
			tryx = x;
		if (y-tryy > MAXRADIUS)
			tryy += MAXRADIUS;
		else if (y-tryy < -MAXRADIUS)
			tryy -= MAXRADIUS;
		else
			tryy = y;

		if (!P_CheckPosition(thing, tryx, tryy))
			return false; // solid wall or thing

		if (!(thing->flags & MF_NOCLIP))
		{
			const fixed_t maxstep = FixedMul(MAXSTEPMOVE, mapobjectscale);

			if (tmceilingz - tmfloorz < thing->height)
				return false; // doesn't fit

			if (tmceilingz - thing->z < thing->height)
				return false; // mobj must lower itself to fit

			if (tmfloorz - thing->z > maxstep)
				return false; // too big a step up
		}
	} while(tryx != x || tryy != y);

	// the move is ok,
	// so link the thing into its new position
	P_UnsetThingPosition(thing);

	thing->floorz = tmfloorz;
	thing->ceilingz = tmceilingz;
	thing->x = x;
	thing->y = y;

	if (tmfloorthing)
		thing->eflags &= ~MFE_ONGROUND; // not on real floor
	else
		thing->eflags |= MFE_ONGROUND;

	P_SetThingPosition(thing);
	return true;
}

//
// P_ThingHeightClip
// Takes a valid thing and adjusts the thing->floorz,
// thing->ceilingz, and possibly thing->z.
// This is called for all nearby monsters
// whenever a sector changes height.
// If the thing doesn't fit,
// the z will be set to the lowest value
// and false will be returned.
//
static boolean P_ThingHeightClip(mobj_t *thing)
{
	boolean floormoved;
	fixed_t oldfloorz = thing->floorz;
	boolean onfloor = P_IsObjectOnGround(thing);//(thing->z <= thing->floorz);

	if (thing->flags & MF_NOCLIPHEIGHT)
		return true;

	P_CheckPosition(thing, thing->x, thing->y);

	if (P_MobjWasRemoved(thing))
		return true;

	floormoved = (thing->eflags & MFE_VERTICALFLIP && tmceilingz != thing->ceilingz)
		|| (!(thing->eflags & MFE_VERTICALFLIP) && tmfloorz != thing->floorz);

	thing->floorz = tmfloorz;
	thing->ceilingz = tmceilingz;

	// Ugly hack?!?! As long as just ceilingz is the lowest,
	// you'll still get crushed, right?
	if (tmfloorz > oldfloorz+thing->height)
		return true;

	if (onfloor && !(thing->flags & MF_NOGRAVITY) && floormoved)
	{
		if (thing->eflags & MFE_VERTICALFLIP)
			thing->pmomz = thing->ceilingz - (thing->z + thing->height);
		else
			thing->pmomz = thing->floorz - thing->z;
		thing->eflags |= MFE_APPLYPMOMZ;

		if (thing->eflags & MFE_VERTICALFLIP)
			thing->z = thing->ceilingz - thing->height;
		else
			thing->z = thing->floorz;
	}
	else if (!tmfloorthing)
	{
		// don't adjust a floating monster unless forced to
		if (thing->eflags & MFE_VERTICALFLIP)
		{
			if (!onfloor && thing->z < tmfloorz)
				thing->z = thing->floorz;
		}
		else if (!onfloor && thing->z + thing->height > tmceilingz)
			thing->z = thing->ceilingz - thing->height;
	}

	// debug: be sure it falls to the floor
	thing->eflags &= ~MFE_ONGROUND;

	if (thing->ceilingz - thing->floorz < thing->height && thing->z >= thing->floorz)
		// BP: i know that this code cause many trouble but this also fixes
		// a lot of problems, mainly this is implementation of the stepping
		// for mobj (walk on solid corpse without jumping or fake 3d bridge)
		// problem is imp into imp at map01 and monster going at top of others
		return false;

	return true;
}

//
// SLIDE MOVE
// Allows the player to slide along any angled walls.
//
static fixed_t bestslidefrac, secondslidefrac;
static line_t *bestslideline;
static line_t *secondslideline;
static mobj_t *slidemo;
static fixed_t tmxmove, tmymove;

//
// P_HitCameraSlideLine
//
static void P_HitCameraSlideLine(line_t *ld, camera_t *thiscam)
{
	INT32 side;
	angle_t lineangle, moveangle, deltaangle;
	fixed_t movelen, newlen;

	if (ld->slopetype == ST_HORIZONTAL)
	{
		tmymove = 0;
		return;
	}

	if (ld->slopetype == ST_VERTICAL)
	{
		tmxmove = 0;
		return;
	}

	side = P_PointOnLineSide(thiscam->x, thiscam->y, ld);
	lineangle = R_PointToAngle2(0, 0, ld->dx, ld->dy);

	if (side == 1)
		lineangle += ANGLE_180;

	moveangle = R_PointToAngle2(0, 0, tmxmove, tmymove);
	deltaangle = moveangle-lineangle;

	if (deltaangle > ANGLE_180)
		deltaangle += ANGLE_180;

	lineangle >>= ANGLETOFINESHIFT;
	deltaangle >>= ANGLETOFINESHIFT;

	movelen = P_AproxDistance(tmxmove, tmymove);
	newlen = FixedMul(movelen, FINECOSINE(deltaangle));

	tmxmove = FixedMul(newlen, FINECOSINE(lineangle));
	tmymove = FixedMul(newlen, FINESINE(lineangle));
}

//
// P_HitSlideLine
// Adjusts the xmove / ymove
// so that the next move will slide along the wall.
//
static void P_HitSlideLine(line_t *ld)
{
	INT32 side;
	angle_t lineangle, moveangle, deltaangle;
	fixed_t movelen, newlen;

	if (ld->slopetype == ST_HORIZONTAL)
	{
		tmymove = 0;
		return;
	}

	if (ld->slopetype == ST_VERTICAL)
	{
		tmxmove = 0;
		return;
	}

	side = P_PointOnLineSide(slidemo->x, slidemo->y, ld);

	lineangle = R_PointToAngle2(0, 0, ld->dx, ld->dy);

	if (side == 1)
		lineangle += ANGLE_180;

	moveangle = R_PointToAngle2(0, 0, tmxmove, tmymove);
	deltaangle = moveangle-lineangle;

	if (deltaangle > ANGLE_180)
		deltaangle += ANGLE_180;

	lineangle >>= ANGLETOFINESHIFT;
	deltaangle >>= ANGLETOFINESHIFT;

	movelen = P_AproxDistance(tmxmove, tmymove);
	newlen = FixedMul(movelen, FINECOSINE(deltaangle));

	tmxmove = FixedMul(newlen, FINECOSINE(lineangle));
	tmymove = FixedMul(newlen, FINESINE(lineangle));
}

//
// P_PlayerHitBounceLine
//
// HitBounceLine, for players
//
static void P_PlayerHitBounceLine(line_t *ld)
{
	INT32 side;
	angle_t lineangle;
	fixed_t movelen;

	side = P_PointOnLineSide(slidemo->x, slidemo->y, ld);
	lineangle = R_PointToAngle2(0, 0, ld->dx, ld->dy)-ANGLE_90;

	if (side == 1)
		lineangle += ANGLE_180;

	lineangle >>= ANGLETOFINESHIFT;

	movelen = P_AproxDistance(tmxmove, tmymove);

	if (slidemo->player && movelen < (15*mapobjectscale))
		movelen = (15*mapobjectscale);

	tmxmove += FixedMul(movelen, FINECOSINE(lineangle));
	tmymove += FixedMul(movelen, FINESINE(lineangle));
}

//
// P_HitBounceLine
//
// Adjusts the xmove / ymove so that the next move will bounce off the wall.
//
static void P_HitBounceLine(line_t *ld)
{
	angle_t lineangle, moveangle, deltaangle;
	fixed_t movelen;

	if (ld->slopetype == ST_HORIZONTAL)
	{
		tmymove = -tmymove;
		return;
	}

	if (ld->slopetype == ST_VERTICAL)
	{
		tmxmove = -tmxmove;
		return;
	}

	lineangle = R_PointToAngle2(0, 0, ld->dx, ld->dy);

	if (lineangle >= ANGLE_180)
		lineangle -= ANGLE_180;

	moveangle = R_PointToAngle2(0, 0, tmxmove, tmymove);
	deltaangle = moveangle + 2*(lineangle - moveangle);

	lineangle >>= ANGLETOFINESHIFT;
	deltaangle >>= ANGLETOFINESHIFT;

	movelen = P_AproxDistance(tmxmove, tmymove);

	tmxmove = FixedMul(movelen, FINECOSINE(deltaangle));
	tmymove = FixedMul(movelen, FINESINE(deltaangle));

	deltaangle = R_PointToAngle2(0, 0, tmxmove, tmymove);
}

//
// PTR_SlideCameraTraverse
//
static boolean PTR_SlideCameraTraverse(intercept_t *in)
{
	line_t *li;

	I_Assert(in->isaline);

	li = in->d.line;

	// one-sided linedef
	if (!li->backsector)
	{
		if (P_PointOnLineSide(mapcampointer->x, mapcampointer->y, li))
			return true; // don't hit the back side
		goto isblocking;
	}

	// set openrange, opentop, openbottom
	P_CameraLineOpening(li);

	if (openrange < mapcampointer->height)
		goto isblocking; // doesn't fit

	if (opentop - mapcampointer->z < mapcampointer->height)
		goto isblocking; // mobj is too high

	if (openbottom - mapcampointer->z > 0) // We don't want to make the camera step up.
		goto isblocking; // too big a step up

	// this line doesn't block movement
	return true;

	// the line does block movement,
	// see if it is closer than best so far
isblocking:
	{
		if (in->frac < bestslidefrac)
		{
			secondslidefrac = bestslidefrac;
			secondslideline = bestslideline;
			bestslidefrac = in->frac;
			bestslideline = li;
		}
	}

	return false; // stop
}

//
// PTR_SlideTraverse
//
static boolean PTR_SlideTraverse(intercept_t *in)
{
	line_t *li;

	I_Assert(in->isaline);

	li = in->d.line;

	// one-sided linedefs are always solid to sliding movement.
	// one-sided linedef
	if (!li->backsector)
	{
		if (P_PointOnLineSide(slidemo->x, slidemo->y, li))
			return true; // don't hit the back side
		goto isblocking;
	}

	if (!(slidemo->flags & MF_MISSILE))
	{
		if (li->flags & ML_IMPASSIBLE)
			goto isblocking;

		if ((slidemo->flags & (MF_ENEMY|MF_BOSS)) && li->flags & ML_BLOCKMONSTERS)
			goto isblocking;
	}

	// set openrange, opentop, openbottom
	P_LineOpening(li, slidemo);

	if (openrange < slidemo->height)
		goto isblocking; // doesn't fit

	if (opentop - slidemo->z < slidemo->height)
		goto isblocking; // mobj is too high

	if (openbottom - slidemo->z > FixedMul(MAXSTEPMOVE, mapobjectscale))
		goto isblocking; // too big a step up

	// this line doesn't block movement
	return true;

	// the line does block movement,
	// see if it is closer than best so far
isblocking:
	if (li->polyobj && slidemo->player)
	{
		if ((li->polyobj->lines[0]->backsector->flags & SF_TRIGGERSPECIAL_TOUCH) && !(li->polyobj->flags & POF_NOSPECIALS))
			P_ProcessSpecialSector(slidemo->player, slidemo->subsector->sector, li->polyobj->lines[0]->backsector);
	}

	if (in->frac < bestslidefrac)
	{
		secondslidefrac = bestslidefrac;
		secondslideline = bestslideline;
		bestslidefrac = in->frac;
		bestslideline = li;
	}

	return false; // stop
}

//
// P_SlideCameraMove
//
// Tries to slide the camera along a wall.
//
void P_SlideCameraMove(camera_t *thiscam)
{
	fixed_t leadx, leady, trailx, traily, newx, newy;
	INT32 hitcount = 0;
	INT32 retval = 0;

	bestslideline = NULL;

retry:
	if (++hitcount == 3)
		goto stairstep; // don't loop forever

	// trace along the three leading corners
	if (thiscam->momx > 0)
	{
		leadx = thiscam->x + thiscam->radius;
		trailx = thiscam->x - thiscam->radius;
	}
	else
	{
		leadx = thiscam->x - thiscam->radius;
		trailx = thiscam->x + thiscam->radius;
	}

	if (thiscam->momy > 0)
	{
		leady = thiscam->y + thiscam->radius;
		traily = thiscam->y - thiscam->radius;
	}
	else
	{
		leady = thiscam->y - thiscam->radius;
		traily = thiscam->y + thiscam->radius;
	}

	bestslidefrac = FRACUNIT+1;

	mapcampointer = thiscam;

	P_PathTraverse(leadx, leady, leadx + thiscam->momx, leady + thiscam->momy,
		PT_ADDLINES, PTR_SlideCameraTraverse);
	P_PathTraverse(trailx, leady, trailx + thiscam->momx, leady + thiscam->momy,
		PT_ADDLINES, PTR_SlideCameraTraverse);
	P_PathTraverse(leadx, traily, leadx + thiscam->momx, traily + thiscam->momy,
		PT_ADDLINES, PTR_SlideCameraTraverse);

	// move up to the wall
	if (bestslidefrac == FRACUNIT+1)
	{
		retval = P_TryCameraMove(thiscam->x, thiscam->y + thiscam->momy, thiscam);
		// the move must have hit the middle, so stairstep
stairstep:
		if (!retval) // Allow things to drop off.
			P_TryCameraMove(thiscam->x + thiscam->momx, thiscam->y, thiscam);
		return;
	}

	// fudge a bit to make sure it doesn't hit
	bestslidefrac -= 0x800;
	if (bestslidefrac > 0)
	{
		newx = FixedMul(thiscam->momx, bestslidefrac);
		newy = FixedMul(thiscam->momy, bestslidefrac);

		retval = P_TryCameraMove(thiscam->x + newx, thiscam->y + newy, thiscam);

		if (!retval)
			goto stairstep;
	}

	// Now continue along the wall.
	// First calculate remainder.
	bestslidefrac = FRACUNIT - (bestslidefrac+0x800);

	if (bestslidefrac > FRACUNIT)
		bestslidefrac = FRACUNIT;

	if (bestslidefrac <= 0)
		return;

	tmxmove = FixedMul(thiscam->momx, bestslidefrac);
	tmymove = FixedMul(thiscam->momy, bestslidefrac);

	P_HitCameraSlideLine(bestslideline, thiscam); // clip the moves

	thiscam->momx = tmxmove;
	thiscam->momy = tmymove;

	retval = P_TryCameraMove(thiscam->x + tmxmove, thiscam->y + tmymove, thiscam);

	if (!retval)
		goto retry;
}

//
// P_SlideMove
// The momx / momy move is bad, so try to slide
// along a wall.
// Find the first line hit, move flush to it,
// and slide along it
//
// This is a kludgy mess.
//
void P_SlideMove(mobj_t *mo, boolean forceslide)
{
	fixed_t leadx, leady, trailx, traily, newx, newy;
	INT16 hitcount = 0;
	boolean success = false;

	if (tmhitthing && mo->z + mo->height > tmhitthing->z && mo->z < tmhitthing->z + tmhitthing->height)
	{
		// Don't mess with your momentum if it's a pushable object. Pushables do their own crazy things already.
		if (tmhitthing->flags & MF_PUSHABLE)
			return;

		// Thankfully box collisions are a lot simpler than arbitrary lines. There's only four possible cases.
		if (mo->y + mo->radius <= tmhitthing->y - tmhitthing->radius)
		{
			mo->momy = 0;
			P_TryMove(mo, mo->x + mo->momx, tmhitthing->y - tmhitthing->radius - mo->radius, true);
		}
		else if (mo->y - mo->radius >= tmhitthing->y + tmhitthing->radius)
		{
			mo->momy = 0;
			P_TryMove(mo, mo->x + mo->momx, tmhitthing->y + tmhitthing->radius + mo->radius, true);
		}
		else if (mo->x + mo->radius <= tmhitthing->x - tmhitthing->radius)
		{
			mo->momx = 0;
			P_TryMove(mo, tmhitthing->x - tmhitthing->radius - mo->radius, mo->y + mo->momy, true);
		}
		else if (mo->x - mo->radius >= tmhitthing->x + tmhitthing->radius)
		{
			mo->momx = 0;
			P_TryMove(mo, tmhitthing->x + tmhitthing->radius + mo->radius, mo->y + mo->momy, true);
		}
		else
			mo->momx = mo->momy = 0;
		return;
	}

	slidemo = mo;
	bestslideline = NULL;

retry:
	if (++hitcount == 3)
		goto stairstep; // don't loop forever

	// trace along the three leading corners
	if (mo->momx > 0)
	{
		leadx = mo->x + mo->radius;
		trailx = mo->x - mo->radius;
	}
	else
	{
		leadx = mo->x - mo->radius;
		trailx = mo->x + mo->radius;
	}

	if (mo->momy > 0)
	{
		leady = mo->y + mo->radius;
		traily = mo->y - mo->radius;
	}
	else
	{
		leady = mo->y - mo->radius;
		traily = mo->y + mo->radius;
	}

	bestslidefrac = FRACUNIT+1;

	P_PathTraverse(leadx, leady, leadx + mo->momx, leady + mo->momy,
		PT_ADDLINES, PTR_SlideTraverse);
	P_PathTraverse(trailx, leady, trailx + mo->momx, leady + mo->momy,
		PT_ADDLINES, PTR_SlideTraverse);
	P_PathTraverse(leadx, traily, leadx + mo->momx, traily + mo->momy,
		PT_ADDLINES, PTR_SlideTraverse);

	// Some walls are bouncy even if you're not
	if (!forceslide && bestslideline && !(bestslideline->flags & ML_BOUNCY)) // SRB2kart - All walls are bouncy unless specified otherwise
	{
		P_BounceMove(mo);
		return;
	}

	// move up to the wall
	if (bestslidefrac == FRACUNIT+1)
	{
		// the move must have hit the middle, so stairstep
stairstep:
		if (!P_TryMove(mo, mo->x, mo->y + mo->momy, true)) //Allow things to drop off.
			P_TryMove(mo, mo->x + mo->momx, mo->y, true);
		return;
	}

	// fudge a bit to make sure it doesn't hit
	bestslidefrac -= 0x800;
	if (bestslidefrac > 0)
	{
		newx = FixedMul(mo->momx, bestslidefrac);
		newy = FixedMul(mo->momy, bestslidefrac);

		if (!P_TryMove(mo, mo->x + newx, mo->y + newy, true))
			goto stairstep;
	}

	// Now continue along the wall.
	// First calculate remainder.
	bestslidefrac = FRACUNIT - (bestslidefrac+0x800);

	if (bestslidefrac > FRACUNIT)
		bestslidefrac = FRACUNIT;

	if (bestslidefrac <= 0)
		return;

	tmxmove = FixedMul(mo->momx, bestslidefrac);
	tmymove = FixedMul(mo->momy, bestslidefrac);

	P_HitSlideLine(bestslideline); // clip the moves

	if ((twodlevel || (mo->flags2 & MF2_TWOD)) && mo->player)
	{
		mo->momx = tmxmove;
		tmymove = 0;
	}
	else
	{
		mo->momx = tmxmove;
		mo->momy = tmymove;
	}

	do {
		if (tmxmove > mo->radius) {
			newx = mo->x + mo->radius;
			tmxmove -= mo->radius;
		} else if (tmxmove < -mo->radius) {
			newx = mo->x - mo->radius;
			tmxmove += mo->radius;
		} else {
			newx = mo->x + tmxmove;
			tmxmove = 0;
		}
		if (tmymove > mo->radius) {
			newy = mo->y + mo->radius;
			tmymove -= mo->radius;
		} else if (tmymove < -mo->radius) {
			newy = mo->y - mo->radius;
			tmymove += mo->radius;
		} else {
			newy = mo->y + tmymove;
			tmymove = 0;
		}
		if (!P_TryMove(mo, newx, newy, true)) {
			if (success)
				return; // Good enough!!
			else
				goto retry;
		}
		success = true;
	} while(tmxmove || tmymove);
}

//
// P_BouncePlayerMove
//
// Bounce move, for players.
//

void P_BouncePlayerMove(mobj_t *mo)
{
	fixed_t leadx, leady;
	fixed_t trailx, traily;
	fixed_t mmomx = 0, mmomy = 0;
	fixed_t oldmomx = mo->momx, oldmomy = mo->momy;

	if (!mo->player)
		return;

	if (mo->player->spectator)
	{
		P_SlideMove(mo, true);
		return;
	}

	slidemo = mo;

	mmomx = mo->player->rmomx;
	mmomy = mo->player->rmomy;

	mo->player->kartstuff[k_drift] = 0;
	mo->player->kartstuff[k_driftcharge] = 0;
	mo->player->kartstuff[k_pogospring] = 0;

	// trace along the three leading corners
	if (mo->momx > 0)
	{
		leadx = mo->x + mo->radius;
		trailx = mo->x - mo->radius;
	}
	else
	{
		leadx = mo->x - mo->radius;
		trailx = mo->x + mo->radius;
	}

	if (mo->momy > 0)
	{
		leady = mo->y + mo->radius;
		traily = mo->y - mo->radius;
	}
	else
	{
		leady = mo->y - mo->radius;
		traily = mo->y + mo->radius;
	}

	bestslidefrac = FRACUNIT + 1;

	P_PathTraverse(leadx, leady, leadx + mmomx, leady + mmomy, PT_ADDLINES, PTR_SlideTraverse);
	P_PathTraverse(trailx, leady, trailx + mmomx, leady + mmomy, PT_ADDLINES, PTR_SlideTraverse);
	P_PathTraverse(leadx, traily, leadx + mmomx, traily + mmomy, PT_ADDLINES, PTR_SlideTraverse);

	// Now continue along the wall.
	// First calculate remainder.
	bestslidefrac = FRACUNIT - bestslidefrac;

	if (bestslidefrac > FRACUNIT)
		bestslidefrac = FRACUNIT;

	if (bestslidefrac <= 0)
		return;

	if (mo->eflags & MFE_JUSTBOUNCEDWALL) // Stronger push-out
	{
		tmxmove = mmomx;
		tmymove = mmomy;
	}
	else
	{
		tmxmove = FixedMul(mmomx, (FRACUNIT - (FRACUNIT>>2) - (FRACUNIT>>3)));
		tmymove = FixedMul(mmomy, (FRACUNIT - (FRACUNIT>>2) - (FRACUNIT>>3)));
	}

	{
		mobj_t *fx = P_SpawnMobj(mo->x, mo->y, mo->z, MT_BUMP);
		if (mo->eflags & MFE_VERTICALFLIP)
			fx->eflags |= MFE_VERTICALFLIP;
		else
			fx->eflags &= ~MFE_VERTICALFLIP;
		fx->scale = mo->scale;

		S_StartSound(mo, sfx_s3k49);
	}

	P_PlayerHitBounceLine(bestslideline);
	mo->eflags |= MFE_JUSTBOUNCEDWALL;

	mo->momx = tmxmove;
	mo->momy = tmymove;
	mo->player->cmomx = tmxmove;
	mo->player->cmomy = tmymove;

	if (!P_TryMove(mo, mo->x + tmxmove, mo->y + tmymove, true)) {
		P_TryMove(mo, mo->x - oldmomx, mo->y - oldmomy, true);
	}
}

//
// P_BounceMove
//
// The momx / momy move is bad, so try to bounce off a wall.
//
void P_BounceMove(mobj_t *mo)
{
	fixed_t leadx, leady;
	fixed_t trailx, traily;
	fixed_t newx, newy;
	INT32 hitcount;
	fixed_t mmomx = 0, mmomy = 0;

	if (mo->player)
	{
		P_BouncePlayerMove(mo);
		return;
	}

	if (mo->eflags & MFE_JUSTBOUNCEDWALL)
	{
		P_SlideMove(mo, true);
		return;
	}

	slidemo = mo;
	hitcount = 0;

retry:
	if (++hitcount == 3)
		goto bounceback; // don't loop forever

	mmomx = mo->momx;
	mmomy = mo->momy;

	// trace along the three leading corners
	if (mo->momx > 0)
	{
		leadx = mo->x + mo->radius;
		trailx = mo->x - mo->radius;
	}
	else
	{
		leadx = mo->x - mo->radius;
		trailx = mo->x + mo->radius;
	}

	if (mo->momy > 0)
	{
		leady = mo->y + mo->radius;
		traily = mo->y - mo->radius;
	}
	else
	{
		leady = mo->y - mo->radius;
		traily = mo->y + mo->radius;
	}

	bestslidefrac = FRACUNIT + 1;

	P_PathTraverse(leadx, leady, leadx + mmomx, leady + mmomy, PT_ADDLINES, PTR_SlideTraverse);
	P_PathTraverse(trailx, leady, trailx + mmomx, leady + mmomy, PT_ADDLINES, PTR_SlideTraverse);
	P_PathTraverse(leadx, traily, leadx + mmomx, traily + mmomy, PT_ADDLINES, PTR_SlideTraverse);

	// move up to the wall
	if (bestslidefrac == FRACUNIT + 1)
	{
		// the move must have hit the middle, so bounce straight back
bounceback:
		if (P_TryMove(mo, mo->x - mmomx, mo->y - mmomy, true))
		{
			mo->momx *= -1;
			mo->momy *= -1;
			mo->momx = FixedMul(mo->momx, (FRACUNIT - (FRACUNIT>>2) - (FRACUNIT>>3)));
			mo->momy = FixedMul(mo->momy, (FRACUNIT - (FRACUNIT>>2) - (FRACUNIT>>3)));
		}
		return;
	}

	// fudge a bit to make sure it doesn't hit
	bestslidefrac -= 0x800;
	if (bestslidefrac > 0)
	{
		newx = FixedMul(mmomx, bestslidefrac);
		newy = FixedMul(mmomy, bestslidefrac);

		if (!P_TryMove(mo, mo->x + newx, mo->y + newy, true))
			goto bounceback;
	}

	// Now continue along the wall.
	// First calculate remainder.
	bestslidefrac = FRACUNIT - bestslidefrac;

	if (bestslidefrac > FRACUNIT)
		bestslidefrac = FRACUNIT;

	if (bestslidefrac <= 0)
		return;

	if (mo->type == MT_SHELL)
	{
		tmxmove = mmomx;
		tmymove = mmomy;
	}
	else if (mo->type == MT_THROWNBOUNCE)
	{
		tmxmove = FixedMul(mmomx, (FRACUNIT - (FRACUNIT>>6) - (FRACUNIT>>5)));
		tmymove = FixedMul(mmomy, (FRACUNIT - (FRACUNIT>>6) - (FRACUNIT>>5)));
	}
	else if (mo->type == MT_THROWNGRENADE || mo->type == MT_CYBRAKDEMON_NAPALM_BOMB_LARGE)
	{
		// Quickly decay speed as it bounces
		tmxmove = FixedDiv(mmomx, 2*FRACUNIT);
		tmymove = FixedDiv(mmomy, 2*FRACUNIT);
	}
	else
	{
		tmxmove = FixedMul(mmomx, (FRACUNIT - (FRACUNIT>>2) - (FRACUNIT>>3)));
		tmymove = FixedMul(mmomy, (FRACUNIT - (FRACUNIT>>2) - (FRACUNIT>>3)));
	}

	P_HitBounceLine(bestslideline); // clip the moves

	mo->momx = tmxmove;
	mo->momy = tmymove;

	if (!P_TryMove(mo, mo->x + tmxmove, mo->y + tmymove, true))
		goto retry;
}

//
// RADIUS ATTACK
//
static fixed_t bombdamage;
static mobj_t *bombsource;
static mobj_t *bombspot;

//
// PIT_RadiusAttack
// "bombsource" is the creature
// that caused the explosion at "bombspot".
//
static boolean PIT_RadiusAttack(mobj_t *thing)
{
	fixed_t dx, dy, dz, dist;

	if (thing == bombspot // ignore the bomb itself (Deton fix)
	|| (bombsource && thing->type == bombsource->type)) // ignore the type of guys who dropped the bomb (Jetty-Syn Bomber or Skim can bomb eachother, but not themselves.)
		return true;

	if (!(thing->flags & MF_SHOOTABLE))
		return true;

	if (thing->flags & MF_BOSS)
		return true;

	if (thing->flags & MF_MONITOR)
		return true;

	dx = abs(thing->x - bombspot->x);
	dy = abs(thing->y - bombspot->y);
	dz = abs(thing->z + (thing->height>>1) - bombspot->z);

	dist = P_AproxDistance(P_AproxDistance(dx, dy), dz);
	dist -= thing->radius;

	if (dist < 0)
		dist = 0;

	if (dist >= bombdamage)
		return true; // out of range

	if (thing->floorz > bombspot->z && bombspot->ceilingz < thing->z)
		return true;

	if (thing->ceilingz < bombspot->z && bombspot->floorz > thing->z)
		return true;

	if (P_CheckSight(thing, bombspot))
	{	// must be in direct path
		P_DamageMobj(thing, bombspot, bombsource, 1); // Tails 01-11-2001
	}

	return true;
}

//
// P_RadiusAttack
// Source is the creature that caused the explosion at spot.
//
void P_RadiusAttack(mobj_t *spot, mobj_t *source, fixed_t damagedist)
{
	INT32 x, y;
	INT32 xl, xh, yl, yh;
	fixed_t dist;

	dist = FixedMul(damagedist, spot->scale) + MAXRADIUS;
	yh = (unsigned)(spot->y + dist - bmaporgy)>>MAPBLOCKSHIFT;
	yl = (unsigned)(spot->y - dist - bmaporgy)>>MAPBLOCKSHIFT;
	xh = (unsigned)(spot->x + dist - bmaporgx)>>MAPBLOCKSHIFT;
	xl = (unsigned)(spot->x - dist - bmaporgx)>>MAPBLOCKSHIFT;

	BMBOUNDFIX(xl, xh, yl, yh);

	bombspot = spot;
	bombsource = source;
	bombdamage = FixedMul(damagedist, spot->scale);

	for (y = yl; y <= yh; y++)
		for (x = xl; x <= xh; x++)
			P_BlockThingsIterator(x, y, PIT_RadiusAttack);
}

//
// SECTOR HEIGHT CHANGING
// After modifying a sectors floor or ceiling height,
// call this routine to adjust the positions
// of all things that touch the sector.
//
// If anything doesn't fit anymore, true will be returned.
// If crunch is true, they will take damage
//  as they are being crushed.
// If Crunch is false, you should set the sector height back
//  the way it was and call P_CheckSector (? was P_ChangeSector - Graue) again
//  to undo the changes.
//
static boolean crushchange;
static boolean nofit;

//
// PIT_ChangeSector
//
static boolean PIT_ChangeSector(mobj_t *thing, boolean realcrush)
{
	mobj_t *killer = NULL;

	if (P_ThingHeightClip(thing))
	{
		//thing fits, check next thing
		return true;
	}

	if (!(thing->flags & (MF_SHOOTABLE|MF_PUSHABLE))
	|| thing->flags & MF_NOCLIPHEIGHT)
	{
		//doesn't interact with the crusher, just ignore it
		return true;
	}

	// Thing doesn't fit. If thing is vulnerable, that means it's being crushed. If thing is pushable, it might
	// just be blocked by another object - check if it's really a ceiling!
	if (thing->z + thing->height > thing->ceilingz && thing->z <= thing->ceilingz)
	{
		if (thing->flags & MF_PUSHABLE && thing->z + thing->height > thing->subsector->sector->ceilingheight)
		{
			//Thing is a pushable and blocks the moving ceiling
			nofit = true;
			return false;
		}

		//Check FOFs in the sector
		if (thing->subsector->sector->ffloors && (realcrush || thing->flags & MF_PUSHABLE))
		{
			ffloor_t *rover;
			fixed_t topheight, bottomheight;
			fixed_t delta1, delta2;
			INT32 thingtop = thing->z + thing->height;

			for (rover = thing->subsector->sector->ffloors; rover; rover = rover->next)
			{
				if (!(((rover->flags & FF_BLOCKPLAYER) && thing->player)
				|| ((rover->flags & FF_BLOCKOTHERS) && !thing->player)) || !(rover->flags & FF_EXISTS))
					continue;

				topheight = *rover->topheight;
				bottomheight = *rover->bottomheight;

				delta1 = thing->z - (bottomheight + topheight)/2;
				delta2 = thingtop - (bottomheight + topheight)/2;
				if (bottomheight <= thing->ceilingz && abs(delta1) >= abs(delta2))
				{
					if (thing->flags & MF_PUSHABLE)
					{
						//FOF is blocked by pushable
						nofit = true;
						return false;
					}
					else
					{
						//If the thing was crushed by a crumbling FOF, reward the player who made it crumble!
						thinker_t *think;
						elevator_t *crumbler;

						for (think = thinkercap.next; think != &thinkercap; think = think->next)
						{
							if (think->function.acp1 != (actionf_p1)T_StartCrumble)
								continue;

							crumbler = (elevator_t *)think;

							if (crumbler->player && crumbler->player->mo
								&& crumbler->player->mo != thing
								&& crumbler->actionsector == thing->subsector->sector
								&& crumbler->sector == rover->master->frontsector
								&& (crumbler->type == elevateBounce
								|| crumbler->type == elevateContinuous))
							{
								killer = crumbler->player->mo;
							}
						}
					}
				}
			}
		}

		if (realcrush)
		{
			// Crush the object
			if (netgame && thing->player && thing->player->spectator)
				P_DamageMobj(thing, NULL, NULL, 42000); // Respawn crushed spectators
			else
			{
				if (!killer)
				{
					//Nobody is responsible for crushing the object, so give a generic crush message
					killer = P_SpawnMobj(thing->x, thing->y, thing->z, MT_NULL);
					killer->threshold = 44; // Special flag for crushing
				}
				if (!thing->player)
					P_DamageMobj(thing, killer, killer, 10000);
				else
					K_SquishPlayer(thing->player, killer, killer); // SRB2kart - Squish instead of kill
			}
		}
	}

	if (realcrush && crushchange)
		P_DamageMobj(thing, NULL, NULL, 1);

	// keep checking (crush other things)
	return true;
}

//
// P_CheckSector
//
boolean P_CheckSector(sector_t *sector, boolean crunch)
{
	msecnode_t *n;

	nofit = false;
	crushchange = crunch;

	// killough 4/4/98: scan list front-to-back until empty or exhausted,
	// restarting from beginning after each thing is processed. Avoids
	// crashes, and is sure to examine all things in the sector, and only
	// the things which are in the sector, until a steady-state is reached.
	// Things can arbitrarily be inserted and removed and it won't mess up.
	//
	// killough 4/7/98: simplified to avoid using complicated counter


	// First, let's see if anything will keep it from crushing.
	if (sector->numattached)
	{
		size_t i;
		sector_t *sec;
		for (i = 0; i < sector->numattached; i++)
		{
			sec = &sectors[sector->attached[i]];
			for (n = sec->touching_thinglist; n; n = n->m_thinglist_next)
				n->visited = false;

			sec->moved = true;

			P_RecalcPrecipInSector(sec);

			if (!sector->attachedsolid[i])
				continue;

			do
			{
				for (n = sec->touching_thinglist; n; n = n->m_thinglist_next)
				if (!n->visited)
				{
					n->visited = true;
					if (!(n->m_thing->flags & MF_NOBLOCKMAP))
					{
						if (!PIT_ChangeSector(n->m_thing, false))
						{
							nofit = true;
							return nofit;
						}
					}
					break;
				}
			} while (n);
		}
	}

	// Mark all things invalid
	sector->moved = true;

	for (n = sector->touching_thinglist; n; n = n->m_thinglist_next)
		n->visited = false;

	do
	{
		for (n = sector->touching_thinglist; n; n = n->m_thinglist_next) // go through list
			if (!n->visited) // unprocessed thing found
			{
				n->visited = true; // mark thing as processed
				if (!(n->m_thing->flags & MF_NOBLOCKMAP)) //jff 4/7/98 don't do these
				{
					if (!PIT_ChangeSector(n->m_thing, false)) // process it
					{
						nofit = true;
						return nofit;
					}
				}
				break; // exit and start over
			}
	} while (n); // repeat from scratch until all things left are marked valid

	// Nothing blocked us, so lets crush for real!
	if (sector->numattached)
	{
		size_t i;
		sector_t *sec;
		for (i = 0; i < sector->numattached; i++)
		{
			sec = &sectors[sector->attached[i]];
			for (n = sec->touching_thinglist; n; n = n->m_thinglist_next)
				n->visited = false;

			sec->moved = true;

			P_RecalcPrecipInSector(sec);

			if (!sector->attachedsolid[i])
				continue;

			do
			{
				for (n = sec->touching_thinglist; n; n = n->m_thinglist_next)
				if (!n->visited)
				{
					n->visited = true;
					if (!(n->m_thing->flags & MF_NOBLOCKMAP))
					{
						PIT_ChangeSector(n->m_thing, true);
						return nofit;
					}
					break;
				}
			} while (n);
		}
	}

	// Mark all things invalid
	sector->moved = true;

	for (n = sector->touching_thinglist; n; n = n->m_thinglist_next)
		n->visited = false;

	do
	{
		for (n = sector->touching_thinglist; n; n = n->m_thinglist_next) // go through list
			if (!n->visited) // unprocessed thing found
			{
				n->visited = true; // mark thing as processed
				if (!(n->m_thing->flags & MF_NOBLOCKMAP)) //jff 4/7/98 don't do these
				{
					PIT_ChangeSector(n->m_thing, true); // process it
					return nofit;
				}
				break; // exit and start over
			}
	} while (n); // repeat from scratch until all things left are marked valid

	return nofit;
}

/*
 SoM: 3/15/2000
 Lots of new Boom functions that work faster and add functionality.
*/

static msecnode_t *headsecnode = NULL;
static mprecipsecnode_t *headprecipsecnode = NULL;

void P_Initsecnode(void)
{
	headsecnode = NULL;
	headprecipsecnode = NULL;
}

// P_GetSecnode() retrieves a node from the freelist. The calling routine
// should make sure it sets all fields properly.

static msecnode_t *P_GetSecnode(void)
{
	msecnode_t *node;

	if (headsecnode)
	{
		node = headsecnode;
		headsecnode = headsecnode->m_thinglist_next;
	}
	else
		node = Z_Calloc(sizeof (*node), PU_LEVEL, NULL);
	return node;
}

static mprecipsecnode_t *P_GetPrecipSecnode(void)
{
	mprecipsecnode_t *node;

	if (headprecipsecnode)
	{
		node = headprecipsecnode;
		headprecipsecnode = headprecipsecnode->m_thinglist_next;
	}
	else
		node = Z_Calloc(sizeof (*node), PU_LEVEL, NULL);
	return node;
}

// P_PutSecnode() returns a node to the freelist.

static inline void P_PutSecnode(msecnode_t *node)
{
	node->m_thinglist_next = headsecnode;
	headsecnode = node;
}

// Tails 08-25-2002
static inline void P_PutPrecipSecnode(mprecipsecnode_t *node)
{
	node->m_thinglist_next = headprecipsecnode;
	headprecipsecnode = node;
}

// P_AddSecnode() searches the current list to see if this sector is
// already there. If not, it adds a sector node at the head of the list of
// sectors this object appears in. This is called when creating a list of
// nodes that will get linked in later. Returns a pointer to the new node.

static msecnode_t *P_AddSecnode(sector_t *s, mobj_t *thing, msecnode_t *nextnode)
{
	msecnode_t *node;

	node = nextnode;
	while (node)
	{
		if (node->m_sector == s) // Already have a node for this sector?
		{
			node->m_thing = thing; // Yes. Setting m_thing says 'keep it'.
			return nextnode;
		}
		node = node->m_sectorlist_next;
	}

	// Couldn't find an existing node for this sector. Add one at the head
	// of the list.

	node = P_GetSecnode();

	// mark new nodes unvisited.
	node->visited = 0;

	node->m_sector = s; // sector
	node->m_thing = thing; // mobj
	node->m_sectorlist_prev = NULL; // prev node on Thing thread
	node->m_sectorlist_next = nextnode; // next node on Thing thread
	if (nextnode)
		nextnode->m_sectorlist_prev = node; // set back link on Thing

	// Add new node at head of sector thread starting at s->touching_thinglist

	node->m_thinglist_prev = NULL; // prev node on sector thread
	node->m_thinglist_next = s->touching_thinglist; // next node on sector thread
	if (s->touching_thinglist)
		node->m_thinglist_next->m_thinglist_prev = node;
	s->touching_thinglist = node;
	return node;
}

// More crazy crap Tails 08-25-2002
static mprecipsecnode_t *P_AddPrecipSecnode(sector_t *s, precipmobj_t *thing, mprecipsecnode_t *nextnode)
{
	mprecipsecnode_t *node;

	node = nextnode;
	while (node)
	{
		if (node->m_sector == s) // Already have a node for this sector?
		{
			node->m_thing = thing; // Yes. Setting m_thing says 'keep it'.
			return nextnode;
		}
		node = node->m_sectorlist_next;
	}

	// Couldn't find an existing node for this sector. Add one at the head
	// of the list.

	node = P_GetPrecipSecnode();

	// mark new nodes unvisited.
	node->visited = 0;

	node->m_sector = s; // sector
	node->m_thing = thing; // mobj
	node->m_sectorlist_prev = NULL; // prev node on Thing thread
	node->m_sectorlist_next = nextnode; // next node on Thing thread
	if (nextnode)
		nextnode->m_sectorlist_prev = node; // set back link on Thing

	// Add new node at head of sector thread starting at s->touching_thinglist

	node->m_thinglist_prev = NULL; // prev node on sector thread
	node->m_thinglist_next = s->touching_preciplist; // next node on sector thread
	if (s->touching_preciplist)
		node->m_thinglist_next->m_thinglist_prev = node;
	s->touching_preciplist = node;
	return node;
}

// P_DelSecnode() deletes a sector node from the list of
// sectors this object appears in. Returns a pointer to the next node
// on the linked list, or NULL.

static msecnode_t *P_DelSecnode(msecnode_t *node)
{
	msecnode_t *tp; // prev node on thing thread
	msecnode_t *tn; // next node on thing thread
	msecnode_t *sp; // prev node on sector thread
	msecnode_t *sn; // next node on sector thread

	if (!node)
		return NULL;

	// Unlink from the Thing thread. The Thing thread begins at
	// sector_list and not from mobj_t->touching_sectorlist.

	tp = node->m_sectorlist_prev;
	tn = node->m_sectorlist_next;
	if (tp)
		tp->m_sectorlist_next = tn;
	if (tn)
		tn->m_sectorlist_prev = tp;

	// Unlink from the sector thread. This thread begins at
	// sector_t->touching_thinglist.

	sp = node->m_thinglist_prev;
	sn = node->m_thinglist_next;
	if (sp)
		sp->m_thinglist_next = sn;
	else
		node->m_sector->touching_thinglist = sn;
	if (sn)
		sn->m_thinglist_prev = sp;

	// Return this node to the freelist

	P_PutSecnode(node);
	return tn;
}

// Tails 08-25-2002
static mprecipsecnode_t *P_DelPrecipSecnode(mprecipsecnode_t *node)
{
	mprecipsecnode_t *tp; // prev node on thing thread
	mprecipsecnode_t *tn; // next node on thing thread
	mprecipsecnode_t *sp; // prev node on sector thread
	mprecipsecnode_t *sn; // next node on sector thread

	if (!node)
		return NULL;

	// Unlink from the Thing thread. The Thing thread begins at
	// sector_list and not from mobj_t->touching_sectorlist.

	tp = node->m_sectorlist_prev;
	tn = node->m_sectorlist_next;
	if (tp)
		tp->m_sectorlist_next = tn;
	if (tn)
		tn->m_sectorlist_prev = tp;

	// Unlink from the sector thread. This thread begins at
	// sector_t->touching_thinglist.

	sp = node->m_thinglist_prev;
	sn = node->m_thinglist_next;
	if (sp)
		sp->m_thinglist_next = sn;
	else
		node->m_sector->touching_preciplist = sn;
	if (sn)
		sn->m_thinglist_prev = sp;

	// Return this node to the freelist

	P_PutPrecipSecnode(node);
	return tn;
}

// Delete an entire sector list
void P_DelSeclist(msecnode_t *node)
{
	while (node)
		node = P_DelSecnode(node);
}

// Tails 08-25-2002
void P_DelPrecipSeclist(mprecipsecnode_t *node)
{
	while (node)
		node = P_DelPrecipSecnode(node);
}

// PIT_GetSectors
// Locates all the sectors the object is in by looking at the lines that
// cross through it. You have already decided that the object is allowed
// at this location, so don't bother with checking impassable or
// blocking lines.

static inline boolean PIT_GetSectors(line_t *ld)
{
	if (tmbbox[BOXRIGHT] <= ld->bbox[BOXLEFT] ||
		tmbbox[BOXLEFT] >= ld->bbox[BOXRIGHT] ||
		tmbbox[BOXTOP] <= ld->bbox[BOXBOTTOM] ||
		tmbbox[BOXBOTTOM] >= ld->bbox[BOXTOP])
		return true;

	if (P_BoxOnLineSide(tmbbox, ld) != -1)
		return true;

	if (ld->polyobj) // line belongs to a polyobject, don't add it
		return true;

	// This line crosses through the object.

	// Collect the sector(s) from the line and add to the
	// sector_list you're examining. If the Thing ends up being
	// allowed to move to this position, then the sector_list
	// will be attached to the Thing's mobj_t at touching_sectorlist.

	sector_list = P_AddSecnode(ld->frontsector,tmthing,sector_list);

	// Don't assume all lines are 2-sided, since some Things
	// like MT_TFOG are allowed regardless of whether their radius takes
	// them beyond an impassable linedef.

	// Use sidedefs instead of 2s flag to determine two-sidedness.
	if (ld->backsector)
		sector_list = P_AddSecnode(ld->backsector, tmthing, sector_list);

	return true;
}

// Tails 08-25-2002
static inline boolean PIT_GetPrecipSectors(line_t *ld)
{
	if (preciptmbbox[BOXRIGHT] <= ld->bbox[BOXLEFT] ||
		preciptmbbox[BOXLEFT] >= ld->bbox[BOXRIGHT] ||
		preciptmbbox[BOXTOP] <= ld->bbox[BOXBOTTOM] ||
		preciptmbbox[BOXBOTTOM] >= ld->bbox[BOXTOP])
		return true;

	if (P_BoxOnLineSide(preciptmbbox, ld) != -1)
		return true;

	if (ld->polyobj) // line belongs to a polyobject, don't add it
		return true;

	// This line crosses through the object.

	// Collect the sector(s) from the line and add to the
	// sector_list you're examining. If the Thing ends up being
	// allowed to move to this position, then the sector_list
	// will be attached to the Thing's mobj_t at touching_sectorlist.

	precipsector_list = P_AddPrecipSecnode(ld->frontsector, tmprecipthing, precipsector_list);

	// Don't assume all lines are 2-sided, since some Things
	// like MT_TFOG are allowed regardless of whether their radius takes
	// them beyond an impassable linedef.

	// Use sidedefs instead of 2s flag to determine two-sidedness.
	if (ld->backsector)
		precipsector_list = P_AddPrecipSecnode(ld->backsector, tmprecipthing, precipsector_list);

	return true;
}

// P_CreateSecNodeList alters/creates the sector_list that shows what sectors
// the object resides in.

void P_CreateSecNodeList(mobj_t *thing, fixed_t x, fixed_t y)
{
	INT32 xl, xh, yl, yh, bx, by;
	msecnode_t *node = sector_list;
	mobj_t *saved_tmthing = tmthing; /* cph - see comment at func end */
	fixed_t saved_tmx = tmx, saved_tmy = tmy; /* ditto */

	// First, clear out the existing m_thing fields. As each node is
	// added or verified as needed, m_thing will be set properly. When
	// finished, delete all nodes where m_thing is still NULL. These
	// represent the sectors the Thing has vacated.

	while (node)
	{
		node->m_thing = NULL;
		node = node->m_sectorlist_next;
	}

	P_SetTarget(&tmthing, thing);
	tmflags = thing->flags;

	tmx = x;
	tmy = y;

	tmbbox[BOXTOP] = y + tmthing->radius;
	tmbbox[BOXBOTTOM] = y - tmthing->radius;
	tmbbox[BOXRIGHT] = x + tmthing->radius;
	tmbbox[BOXLEFT] = x - tmthing->radius;

	validcount++; // used to make sure we only process a line once

	xl = (unsigned)(tmbbox[BOXLEFT] - bmaporgx)>>MAPBLOCKSHIFT;
	xh = (unsigned)(tmbbox[BOXRIGHT] - bmaporgx)>>MAPBLOCKSHIFT;
	yl = (unsigned)(tmbbox[BOXBOTTOM] - bmaporgy)>>MAPBLOCKSHIFT;
	yh = (unsigned)(tmbbox[BOXTOP] - bmaporgy)>>MAPBLOCKSHIFT;

	BMBOUNDFIX(xl, xh, yl, yh);

	for (bx = xl; bx <= xh; bx++)
		for (by = yl; by <= yh; by++)
			P_BlockLinesIterator(bx, by, PIT_GetSectors);

	// Add the sector of the (x, y) point to sector_list.
	sector_list = P_AddSecnode(thing->subsector->sector, thing, sector_list);

	// Now delete any nodes that won't be used. These are the ones where
	// m_thing is still NULL.
	node = sector_list;
	while (node)
	{
		if (!node->m_thing)
		{
			if (node == sector_list)
				sector_list = node->m_sectorlist_next;
			node = P_DelSecnode(node);
		}
		else
			node = node->m_sectorlist_next;
	}

	/* cph -
	* This is the strife we get into for using global variables. tmthing
	*  is being used by several different functions calling
	*  P_BlockThingIterator, including functions that can be called *from*
	*  P_BlockThingIterator. Using a global tmthing is not reentrant.
	* OTOH for Boom/MBF demos we have to preserve the buggy behavior.
	*  Fun. We restore its previous value unless we're in a Boom/MBF demo.
	*/
	P_SetTarget(&tmthing, saved_tmthing);

	/* And, duh, the same for tmx/y - cph 2002/09/22
	* And for tmbbox - cph 2003/08/10 */
	tmx = saved_tmx, tmy = saved_tmy;

	if (tmthing)
	{
		tmbbox[BOXTOP]  = tmy + tmthing->radius;
		tmbbox[BOXBOTTOM] = tmy - tmthing->radius;
		tmbbox[BOXRIGHT]  = tmx + tmthing->radius;
		tmbbox[BOXLEFT]   = tmx - tmthing->radius;
	}
}

// More crazy crap Tails 08-25-2002
void P_CreatePrecipSecNodeList(precipmobj_t *thing,fixed_t x,fixed_t y)
{
	INT32 xl, xh, yl, yh, bx, by;
	mprecipsecnode_t *node = precipsector_list;
	precipmobj_t *saved_tmthing = tmprecipthing; /* cph - see comment at func end */

	// First, clear out the existing m_thing fields. As each node is
	// added or verified as needed, m_thing will be set properly. When
	// finished, delete all nodes where m_thing is still NULL. These
	// represent the sectors the Thing has vacated.

	while (node)
	{
		node->m_thing = NULL;
		node = node->m_sectorlist_next;
	}

	tmprecipthing = thing;

	preciptmbbox[BOXTOP] = y + 2*FRACUNIT;
	preciptmbbox[BOXBOTTOM] = y - 2*FRACUNIT;
	preciptmbbox[BOXRIGHT] = x + 2*FRACUNIT;
	preciptmbbox[BOXLEFT] = x - 2*FRACUNIT;

	validcount++; // used to make sure we only process a line once

	xl = (unsigned)(preciptmbbox[BOXLEFT] - bmaporgx)>>MAPBLOCKSHIFT;
	xh = (unsigned)(preciptmbbox[BOXRIGHT] - bmaporgx)>>MAPBLOCKSHIFT;
	yl = (unsigned)(preciptmbbox[BOXBOTTOM] - bmaporgy)>>MAPBLOCKSHIFT;
	yh = (unsigned)(preciptmbbox[BOXTOP] - bmaporgy)>>MAPBLOCKSHIFT;

	BMBOUNDFIX(xl, xh, yl, yh);

	for (bx = xl; bx <= xh; bx++)
		for (by = yl; by <= yh; by++)
			P_BlockLinesIterator(bx, by, PIT_GetPrecipSectors);

	// Add the sector of the (x, y) point to sector_list.
	precipsector_list = P_AddPrecipSecnode(thing->subsector->sector, thing, precipsector_list);

	// Now delete any nodes that won't be used. These are the ones where
	// m_thing is still NULL.
	node = precipsector_list;
	while (node)
	{
		if (!node->m_thing)
		{
			if (node == precipsector_list)
				precipsector_list = node->m_sectorlist_next;
			node = P_DelPrecipSecnode(node);
		}
		else
			node = node->m_sectorlist_next;
	}

	/* cph -
	* This is the strife we get into for using global variables. tmthing
	*  is being used by several different functions calling
	*  P_BlockThingIterator, including functions that can be called *from*
	*  P_BlockThingIterator. Using a global tmthing is not reentrant.
	* OTOH for Boom/MBF demos we have to preserve the buggy behavior.
	*  Fun. We restore its previous value unless we're in a Boom/MBF demo.
	*/
	tmprecipthing = saved_tmthing;
}

/* cphipps 2004/08/30 -
 * Must clear tmthing at tic end, as it might contain a pointer to a removed thinker, or the level might have ended/been ended and we clear the objects it was pointing too. Hopefully we don't need to carry this between tics for sync. */
void P_MapStart(void)
{
	if (tmthing)
		I_Error("P_MapStart: tmthing set!");
}

void P_MapEnd(void)
{
	P_SetTarget(&tmthing, NULL);
}

// P_FloorzAtPos
// Returns the floorz of the XYZ position // TODO: Need ceilingpos function too
// Tails 05-26-2003
fixed_t P_FloorzAtPos(fixed_t x, fixed_t y, fixed_t z, fixed_t height)
{
	sector_t *sec = R_PointInSubsector(x, y)->sector;
	fixed_t floorz = sec->floorheight;

	if (sec->f_slope)
		floorz = P_GetZAt(sec->f_slope, x, y);

	// Intercept the stupid 'fall through 3dfloors' bug Tails 03-17-2002
	if (sec->ffloors)
	{
		ffloor_t *rover;
		fixed_t delta1, delta2, thingtop = z + height;

		for (rover = sec->ffloors; rover; rover = rover->next)
		{
			fixed_t topheight, bottomheight;
			if (!(rover->flags & FF_EXISTS))
				continue;

			if ((!(rover->flags & FF_SOLID || rover->flags & FF_QUICKSAND) || (rover->flags & FF_SWIMMABLE)))
				continue;

			topheight = *rover->topheight;
			bottomheight = *rover->bottomheight;

			if (*rover->t_slope)
				topheight = P_GetZAt(*rover->t_slope, x, y);
			if (*rover->b_slope)
				bottomheight = P_GetZAt(*rover->b_slope, x, y);

			if (rover->flags & FF_QUICKSAND)
			{
				if (z < topheight && bottomheight < thingtop)
				{
					if (floorz < z)
						floorz = z;
				}
				continue;
			}

			delta1 = z - (bottomheight + ((topheight - bottomheight)/2));
			delta2 = thingtop - (bottomheight + ((topheight - bottomheight)/2));
			if (topheight > floorz && abs(delta1) < abs(delta2))
				floorz = topheight;
		}
	}

	return floorz;
}
