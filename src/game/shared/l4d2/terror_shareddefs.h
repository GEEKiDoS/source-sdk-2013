//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Definitions shared by the L4D2 (Terror) client and server.
//
//=============================================================================//

#ifndef TERROR_SHAREDDEFS_H
#define TERROR_SHAREDDEFS_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/cs_shareddefs.h"
#include "ai_activity.h"
#include "filesystem.h"

// S33C180/C27E680: the common console/file diagnostic output owner.
void TerrorDebugPrintf( FileHandle_t file, const char *format, ... );

enum TerrorUpgradeType
{
	TERROR_UPGRADE_INCENDIARY_AMMO = 0,
	TERROR_UPGRADE_EXPLOSIVE_AMMO = 1,
	TERROR_UPGRADE_LASER_SIGHT = 2
};

// Original activity IDs are a separate domain from native SDK Activity values.
// Compile-time policy labels and the runtime name registry share this one list.
enum TerrorOriginalActivity
{
	TERROR_ORIGINAL_UNKNOWN_ACTIVITY = -2,
	TERROR_ORIGINAL_ACT_INVALID = -1,
#define TERROR_ACTIVITY( name ) TERROR_ORIGINAL_##name,
#include "l4d2/terror_activity_names.inc"
#undef TERROR_ACTIVITY
	NUM_TERROR_ORIGINAL_ACTIVITIES
};

void RegisterTerrorActivities();
void FreeTerrorActivities();
Activity UntranslateTerrorActivity( Activity activity );
int TerrorActivityToOriginal( Activity activity );
TerrorOriginalActivity LookupTerrorOriginalActivity( Activity activity );
Activity TerrorActivityFromOriginal( int activity );
const char *GetZombieClassName( int zombieClass );
const char *GetSurvivorCharacterDisplayName( int character );
// C286530 forward display domain; MapTerrorSurvivorCharacter remains inverse.
int GetTerrorSurvivorDisplayCharacter( int character );
// S350D40/C2864E0: raw character domain, cached survivor-set remap.
int MapTerrorSurvivorCharacter( int character );
// S350EA0/C286700: raw script character key with forward survivor-set remap.
const char *GetSurvivorCharacterName( int character );

// Teams, in the order CCSGameRules creates its cs_team_manager entities (server 0x23F1E0,
// name table 0x7A2F34). TEAM_UNASSIGNED and TEAM_SPECTATOR come from shareddefs.h.
enum
{
	TEAM_SURVIVOR = 2,
	TEAM_INFECTED = 3,
	TEAM_L4D1_SURVIVOR = 4,

	TEAM_MAXCOUNT
};

// Survivor characters, as returned by the original name mapper (server 0x350F60).
enum SurvivorCharacterType
{
	SurvivorCharacter_Gambler = 0,		// Nick
	SurvivorCharacter_Producer,			// Rochelle
	SurvivorCharacter_Coach,
	SurvivorCharacter_Mechanic,			// Ellis
	SurvivorCharacter_NamVet,			// Bill
	SurvivorCharacter_TeenGirl,			// Zoey
	SurvivorCharacter_Biker,			// Francis
	SurvivorCharacter_Manager,			// Louis
	SurvivorCharacter_Unknown,

	NUM_SURVIVOR_CHARACTERS = SurvivorCharacter_Unknown
};
float GetSurvivorCharacterScale( SurvivorCharacterType character );

// Game-owned names for the reached original player animation events.
// Event IDs are not transported as the target multiplayer enum.
enum TerrorPlayerAnimEvent
{
	TERROR_ANIMEVENT_FIRE_PRIMARY,
	TERROR_ANIMEVENT_FIRE_PRIMARY_ALT,
	TERROR_ANIMEVENT_RELOAD,
	TERROR_ANIMEVENT_DEPLOY,
	TERROR_ANIMEVENT_MELEE_SWEEP,
	TERROR_ANIMEVENT_MELEE_SHOVE,
	TERROR_ANIMEVENT_MELEE_STRAIGHT,
	TERROR_ANIMEVENT_MELEE_STOMP,
	// Original reload events 5/6/38; these names are not retail enum casts.
	TERROR_ANIMEVENT_RELOAD_LOOP,
	TERROR_ANIMEVENT_RELOAD_END,
	TERROR_ANIMEVENT_RELOAD_START,
	// Native action events 42..56. These are not SDK multiplayer ordinals.
	TERROR_ANIMEVENT_HEAL_SELF,
	TERROR_ANIMEVENT_HEAL_OTHER,
	TERROR_ANIMEVENT_REVIVE_START_LOW,
	TERROR_ANIMEVENT_REVIVE_START_HIGH,
	TERROR_ANIMEVENT_CANCEL_USE,
	TERROR_ANIMEVENT_REVIVE_TARGET_START,
	TERROR_ANIMEVENT_REVIVE_TARGET_END,
	TERROR_ANIMEVENT_REVIVE_SUCCESS,
	TERROR_ANIMEVENT_DEFIBRILLATE,
	TERROR_ANIMEVENT_RETURN_TO_LIFE,
	TERROR_ANIMEVENT_DEPLOY_UPGRADE,
	TERROR_ANIMEVENT_POUR_GAS,
	TERROR_ANIMEVENT_CANCEL_GAS_USE,
	TERROR_ANIMEVENT_POUR_COLA,
	TERROR_ANIMEVENT_CANCEL_COLA_USE,
	TERROR_ANIMEVENT_STAGGER,
	TERROR_ANIMEVENT_CLEAR_STAGGER,
	TERROR_ANIMEVENT_CHARGER_IMPACT_GROUND,
	TERROR_ANIMEVENT_CHARGER_IMPACT,
	TERROR_ANIMEVENT_CHARGER_PUMMELED,
	TERROR_ANIMEVENT_CHARGER_POUNDED,
	TERROR_ANIMEVENT_CHARGER_CARRIED,
	TERROR_ANIMEVENT_CHARGER_IMPACT_WALL_VICTIM,
	TERROR_ANIMEVENT_CHARGER_IMPACT_GROUND_VICTIM,
	// Native 18 unconditionally resets slot zero, unlike guarded event46.
	TERROR_ANIMEVENT_CANCEL_ATTACK_GESTURE,
	TERROR_ANIMEVENT_JUMP,
	TERROR_ANIMEVENT_LAND,
	TERROR_ANIMEVENT_DYING,
	TERROR_ANIMEVENT_ATTACK,
	TERROR_ANIMEVENT_CUSTOM_ATTACK_GESTURE,
	TERROR_ANIMEVENT_CUSTOM_ATTACK_GESTURE_ALT,
	TERROR_ANIMEVENT_CHAINSAW_STARTUP,
	TERROR_ANIMEVENT_FLINCH_FRONT,
	TERROR_ANIMEVENT_FLINCH_BACK,
	TERROR_ANIMEVENT_GRENADE_PIN_PULL,
	TERROR_ANIMEVENT_GRENADE_RELEASE,
	TERROR_ANIMEVENT_CANCEL_GRENADE,
	TERROR_ANIMEVENT_ITEM_PICKUP,
	TERROR_ANIMEVENT_SPIT,
	TERROR_ANIMEVENT_CHARGE_START,
	TERROR_ANIMEVENT_CHARGE_END,
	TERROR_ANIMEVENT_CHARGER_PUMMEL_START,
	TERROR_ANIMEVENT_CHARGER_POUND_START,
	TERROR_ANIMEVENT_POUNCE_ACQUIRE,
	TERROR_ANIMEVENT_POUNCE_LAND,
	TERROR_ANIMEVENT_JOCKEY_RIDE,
	TERROR_ANIMEVENT_JOCKEY_RIDDEN,
	TERROR_ANIMEVENT_POUNCED,
	TERROR_ANIMEVENT_TONGUE_PREPARE,
	TERROR_ANIMEVENT_TONGUE_ATTACK_START,
	TERROR_ANIMEVENT_TONGUE_CHOKE_START,
	TERROR_ANIMEVENT_TONGUE_WALL_HIT,
	TERROR_ANIMEVENT_TONGUE_ATTACK_END,
	TERROR_ANIMEVENT_TONGUE_GRABBED,
	TERROR_ANIMEVENT_THROW,
	TERROR_ANIMEVENT_CLIMB,
	TERROR_ANIMEVENT_TANK_SPECIAL,
	TERROR_ANIMEVENT_HIT_BY_TANK,
	TERROR_ANIMEVENT_FIDGET,
	TERROR_ANIMEVENT_FIRE_GRENADE_LAUNCHER,
	// Native event97 reaches the genuine base default (no gesture/state change).
	TERROR_ANIMEVENT_USE_PUSH
};

// View vectors returned by CTerrorGameRules::GetViewVectors (server 0x2D98E0, data 0x7ACEE0).
#define TERROR_VEC_VIEW				Vector( 0, 0, 62 )
#define TERROR_VEC_HULL_MIN			Vector( -16, -16, 0 )
#define TERROR_VEC_HULL_MAX			Vector( 16, 16, 71 )
#define TERROR_VEC_DUCK_HULL_MIN	Vector( -16, -16, 0 )
#define TERROR_VEC_DUCK_HULL_MAX	Vector( 16, 16, 55 )
#define TERROR_VEC_DUCK_VIEW		Vector( 0, 0, 44 )
#define TERROR_VEC_OBS_HULL_MIN		Vector( -10, -10, -10 )
#define TERROR_VEC_OBS_HULL_MAX		Vector( 10, 10, 10 )
#define TERROR_VEC_DEAD_VIEWHEIGHT	Vector( 0, 0, 14 )

#endif // TERROR_SHAREDDEFS_H
