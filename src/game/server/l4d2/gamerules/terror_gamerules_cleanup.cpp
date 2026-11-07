//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "l4d2/player/cs_player.h"
#include "entitylist.h"
#include "eventqueue.h"
#include "gameinterface.h"
#include "mapentities.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "tier0/memdbgon.h"

// Existing CommentarySystem owner supplies both over its actual spawned-handle
// vector and owned modification KV. Neither function has a missing-host default.
extern bool CommentarySystem_IsEntity( CBaseEntity *entity );
extern void CommentarySystem_ApplyMapModifications();

namespace
{
	// Native107ACE40's terminator is an EMPTY string, not a null pointer.
	const char *const s_preservedTerrorEntities[] =
	{
		"ai_network", "ai_hint", "cs_team_manager", "terror_gamerules", "terror_player_manager", "survivor_bot",
		"env_tonemap_controller", "env_tonemap_controller_infected", "env_tonemap_controller_ghost", "vote_controller",
		"env_soundscape", "env_soundscape_proxy", "env_soundscape_triggerable", "env_sun", "env_wind", "env_fog_controller",
		"func_wall", "infodecal", "info_projecteddecal", "info_node", "info_node_hint", "info_map_parameters",
		"info_map_parameters_versus", "info_ladder", "func_simpleladder", "player", "point_viewcontrol", "scene_manager",
		"shadow_control", "sky_camera", "soundent", "trigger_soundscape", "viewmodel", "predicted_viewmodel", "worldspawn",
		"point_devshot_camera", "weapon_scavenge_item_spawn", "logic_versus_random", ""
	};
	bool IsPreservedTerrorEntity( const char *classname )
	{
		for ( int i = 0; *s_preservedTerrorEntities[i]; ++i )
			if ( !Q_stricmp( classname, s_preservedTerrorEntities[i] ) ) return true;
		return false;
	}
	bool IsReplayedInVersus( const char *classname )
	{
		// Actual27B940 excludes these spawn classes in versus, not every
		// weapon_* classname and not the item-manager's live weapon registry.
		static const char *const excluded[] =
		{
			"weapon_spawn", "weapon_melee_spawn", "weapon_item_spawn", "weapon_pain_pills_spawn", "weapon_first_aid_kit_spawn",
			"weapon_molotov_spawn", "weapon_pipe_bomb_spawn", "weapon_pistol_spawn", "weapon_pistol_magnum_spawn", "weapon_ammo_spawn",
			"upgrade_laser_sight", "weapon_adrenaline_spawn", "weapon_defibrillator_spawn", "weapon_vomitjar_spawn", "weapon_gascan_spawn",
			"weapon_upgradepack_incendiary_spawn", "weapon_upgradepack_explosive_spawn", "weapon_grenade_launcher_spawn",
			"weapon_chainsaw_spawn", "weapon_rifle_m60_spawn"
		};
		for ( int i = 0; i < ARRAYSIZE( excluded ); ++i ) if ( !Q_stricmp( classname, excluded[i] ) ) return false;
		return true;
	}
	class CTerrorMapEntityFilter : public IMapEntityFilter
	{
	public:
		CTerrorMapEntityFilter() : m_iIterator( g_MapEntityRefs.Head() ) {}
		virtual bool ShouldCreateEntity( const char *classname ) OVERRIDE
		{
			// Native2DEE90: excluded-versus classes DO NOT advance the cursor.
			if ( !g_TerrorModes.IsBaseMode( "versus" ) || IsReplayedInVersus( classname ) )
			{
				if ( !IsPreservedTerrorEntity( classname ) ) return true;
				if ( m_iIterator != g_MapEntityRefs.InvalidIndex() ) m_iIterator = g_MapEntityRefs.Next( m_iIterator );
			}
			return false;
		}
		virtual CBaseEntity *CreateNextEntity( const char *classname ) OVERRIDE
		{
			// Native2DEF00: exhausted actual ref list returns NULL, no append
			// or fake entity. Advance once BEFORE allocating the replacement.
			if ( m_iIterator == g_MapEntityRefs.InvalidIndex() ) return NULL;
			const int edictIndex = g_MapEntityRefs[m_iIterator].m_iEdict;
			m_iIterator = g_MapEntityRefs.Next( m_iIterator );
			if ( edictIndex == -1 ) return CreateEntityByName( classname );
			edict_t *slot = (unsigned int)edictIndex <= 0x7ffu ? INDEXENT( edictIndex ) : NULL;
			if ( slot && !slot->IsFree() ) return CreateEntityByName( classname );
			return CreateEntityByName( classname, edictIndex );
		}
	private:
		unsigned short m_iIterator;
	};
}

bool CWeaponCSBase::ShouldRemoveOnRoundRestart() const
{
	// Native24EF40. Printed CS-weapon RTTI primary descendants use this
	// exact1524 slot; no guessed child override policy or mutable SDK flag.
	return dynamic_cast< CCSPlayer * >( GetOwner() ) == NULL;
}

void CTerrorGameRules::CleanUpMap()
{
	// Actual2DDB10. Native655 is the existing restore-by-character/map-replay
	// gate. Rules598 is actual info_player_logo state, NOT commentary mode.
	if ( !TheDirector->IsRestoringByCharacter() || m_bLogoMap ) return;
	TheDirector->GetScriptedMode()->RemoveScriptSpawnedEntities();
	for ( CBaseEntity *entity = gEntList.FirstEnt(); entity; entity = gEntList.NextEnt( entity ) )
	{
		CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( entity );
		if ( weapon )
		{
			if ( weapon->ShouldRemoveOnRoundRestart() ) UTIL_Remove( entity );
		}
		else if ( !IsPreservedTerrorEntity( entity->GetClassname() ) && !CommentarySystem_IsEntity( entity ) ) UTIL_Remove( entity );
	}
	gEntList.CleanupDeleteList();
	engine->AllowImmediateEdictReuse();
	g_EventQueue.Clear();
	FOR_EACH_VEC( TheNavAreas, i ) TheNavAreas[i]->UnblockArea(); // Actual10478280, before scripts/map parse.
	TheDirector->InitScripts(); // Rules virtual476 ->2D94C0 ->actual Director virtual72/26EF40.
	CTerrorMapEntityFilter filter;
	MapEntity_ParseAllEntities( engine->GetMapEntitiesString(), &filter, true );
	CommentarySystem_ApplyMapModifications(); // Native S99FD0 -> S99E20, AFTER replay/activation.
}

void CDirectorScriptedMode::RecordScriptSpawnedEntity( CBaseEntity *entity )
{
	m_ScriptSpawnedEntities.AddToTail( entity ); // Native27AE30 retains NULL/duplicates and exact order.
}

void CDirectorScriptedMode::RemoveScriptSpawnedEntities()
{
	FOR_EACH_VEC( m_ScriptSpawnedEntities, i ) UTIL_Remove( m_ScriptSpawnedEntities[i].Get() );
	m_ScriptSpawnedEntities.RemoveAll(); // Native2791A0 count only, not Purge or delete-list cleanup here.
}
