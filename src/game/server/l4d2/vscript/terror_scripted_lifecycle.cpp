//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_search.h"
#include "triggers.h"
#include "entitylist.h"
#include "entityoutput.h"
#include "tier1/KeyValues.h"
#include "tier0/memdbgon.h"

namespace
{
	struct ShowScriptedPanel
	{
		int team; const char *name; bool show; KeyValues *data;
		bool operator()( CBasePlayer *player )
		{
			if ( team == -1 || player->GetTeamNumber() == team ) player->ShowViewPortPanel( name, show, data );
			return true;
		}
	};
	struct CollectGnomeAreas
	{
		CUtlVector< TerrorNavArea * > list;
		bool operator()( TerrorNavArea *area ) { list.AddToTail( area ); return true; }
	};
	float GnomeRandomAngle()
	{
		// S1B49B0 is RandomInt(0,32767), NOT the CRT rand() stream.
		return ( (float)RandomInt( 0, 32767 ) * 0.000030518509f ) * 360.0f - 180.0f;
	}
	void TransferOutputActions( CBaseEntity *entity, const char *fromName, const char *toName )
	{
		CBaseEntityOutput *from = entity->FindNamedOutput( fromName );
		CBaseEntityOutput *to = entity->FindNamedOutput( toName );
		if ( !from || !to ) return; // S1F7CE0's actual missing-output guard.
		while ( CEventAction *action = from->GetFirstAction() )
		{
			from->RemoveEventAction( action );
			to->AddEventAction( action );
		}
	}
}
void TerrorShowViewPortPanel( int team, const char *name, bool show, KeyValues *data )
{
	// S25E640. The existing ForEachPlayer owns edict/connected iteration.
	ShowScriptedPanel action = { team, name, show, data };
	ForEachPlayer( action );
}
void CDirectorScriptedMode::SpawnMutationGnome()
{
	CBaseEntity *gnome = CreateEntityByName( "weapon_gnome", -1, true );
	if ( !gnome ) return;
	gnome->Spawn();
	Vector position = vec3_origin;
	TerrorGameRules()->GetPlayerSpawnPosition( SurvivorCharacter_Unknown, &position, NULL, NULL );
	if ( position == vec3_origin ) return; // Native retains the created entity here.
	bool findCheckpointSpace = false;
	CBaseEntity *spawn = gEntList.FindEntityByClassnameNearest( "weapon_melee_spawn", position, 600.0f );
	if ( !spawn )
	{
		findCheckpointSpace = true;
		spawn = gEntList.FindEntityByClassnameNearest( "weapon_ammo_spawn", position, 600.0f );
		if ( !spawn ) spawn = gEntList.FindEntityByClassnameNearest( "weapon_first_aid_kit_spawn", position, 600.0f );
	}
	if ( spawn ) position = spawn->WorldSpaceCenter();
	if ( position == vec3_origin ) return;
	if ( findCheckpointSpace )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheTerrorNavMesh()->GetNearestNavArea( position, true, 10000.0f, false, true, 0 ) );
		if ( !area ) return;
		CollectGnomeAreas collected;
		CollectSurroundingTerrorAreas( area, area->GetCenter(), collected, 1000.0f, 0, -1 );
		collected.list.AddToHead( area );
		FOR_EACH_VEC( collected.list, i )
		{
			TerrorNavArea *candidate = collected.list[i];
			if ( !candidate || !candidate->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) continue;
			Vector point;
			candidate->GetClosestPointOnArea( position, &point );
			Vector raised = point; raised.z += 1.0f;
			trace_t trace;
			UTIL_TraceHull( raised, raised, Vector( -8.0f, -8.0f, -0.0f ), Vector( 8.0f, 8.0f, 72.0f ),
				0x0200400B, NULL, COLLISION_GROUP_NONE, &trace );
			if ( trace.fraction == 1.0f && !trace.allsolid && !trace.startsolid && !trace.m_pEnt )
			{
				position = point;
				break;
			}
		}
	}
	position.z += 32.0f;
	// Preserve native independent x/y/z RNG calls and evaluation order.
	QAngle angles;
	angles.x = GnomeRandomAngle(); angles.y = GnomeRandomAngle(); angles.z = GnomeRandomAngle();
	gnome->Teleport( &position, &angles, &vec3_origin );
}
CBaseEntity *CDirectorScriptedMode::FindRescueTrigger()
{
	TerrorNavArea *area = TheTerrorNavMesh()->GetRescueVehicleArea(); // native1544, not goal1548.
	if ( !area ) return NULL;
	CBaseEntity *trigger = NULL;
	while ( ( trigger = gEntList.FindEntityByClassname( trigger, "trigger_multiple" ) ) != NULL )
	{
		Vector low, high;
		trigger->CollisionProp()->WorldSpaceAABB( &low, &high );
		const Vector &center = area->GetCenter();
		if ( center.x >= low.x && high.x >= center.x && center.y >= low.y && high.y >= center.y && trigger->GetAbsOrigin().z >= center.z )
		{
			Msg( "Found rescue trigger \"%s\"\n", STRING( trigger->GetEntityName() ) );
			m_hRescueTrigger = trigger;
			return trigger;
		}
	}
	return NULL;
}
void CDirectorScriptedMode::OnGameplayStart()
{
	if ( !m_bEnabled ) return;
	if ( QueryOptionBool( "cm_VIPTarget", false ) || QueryOptionBool( "cm_HealingGnome", false ) )
	{
		if ( TheDirector->IsFirstChapter() ) SpawnMutationGnome();
		FindRescueTrigger();
	}
	if ( TheDirector->GetDirectorOptionInt( "cm_FirstManOut", 0 ) != 0 )
		if ( CBaseEntity *trigger = FindRescueTrigger() ) TransferOutputActions( trigger, "OnEntireTeamStartTouch", "OnStartTouch" );
	unsigned char activate = 1;
	HSCRIPT scope = TheDirector->GetScriptScope( 1 );
	HSCRIPT function = g_pScriptVM->LookupFunction( "ScriptMode_OnGameplayStart", scope );
	ScriptVariant_t value;
	if ( function )
	{
		if ( g_pScriptVM->Call( function, scope, true, &value, TheDirector->GetGameMode(), STRING( gpGlobals->mapname ) ) != SCRIPT_ERROR )
		{
			activate = *reinterpret_cast< const unsigned char * >( &value );
			TheDirector->BindDirectorOptions();
		}
		g_pScriptVM->ReleaseFunction( function );
	}
	if ( !m_bActive && activate ) Activate();
	value.Free();
}
bool CDirectorScriptedMode::CanTriggerVIP( CBaseEntity *entity ) const
{
	if ( entity != m_hRescueTrigger.Get() || !m_bEnabled || ( !QueryOptionBool( "cm_VIPTarget", false ) && !QueryOptionBool( "cm_HealingGnome", false ) ) ) return true;
	CBaseTrigger *trigger = dynamic_cast< CBaseTrigger * >( m_hRescueTrigger.Get() );
	if ( !trigger ) return true;
	const CUtlVector< EHANDLE > &touching = trigger->GetTouchingEntities();
	FOR_EACH_VEC( touching, i )
	{
		CBaseEntity *actor = touching[i].Get();
		if ( actor && actor->IsPlayer() )
		{
			CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( static_cast< CTerrorPlayer * >( actor )->GetActiveWeapon() );
			if ( weapon && weapon->GetWeaponID() == TERROR_WEAPON_GNOME ) return true;
		}
	}
	return false;
}
bool CDirectorScriptedMode::HasRequiredGnome() const
{
	if ( !m_bEnabled || ( !QueryOptionBool( "cm_VIPTarget", false ) && !QueryOptionBool( "cm_HealingGnome", false ) ) ) return true;
	for ( int i = 1; i <= 32; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( player && player->IsPlayer() )
		{
			CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( player->GetActiveWeapon() );
			if ( weapon && weapon->GetWeaponID() == TERROR_WEAPON_GNOME ) return true;
		}
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "explain_need_gnome_to_continue" );
	if ( event ) gameeventmanager->FireEvent( event );
	return false;
}
void CDirectorScriptedMode::OnFinaleVehicleLeaving( const CUtlVector< CTerrorPlayer * > &escaped )
{
	if ( !m_bActive || !TheDirector->GetDirectorOptionInt( "cm_FirstManOut", 0 ) ) return;
	CBaseTrigger *trigger = dynamic_cast< CBaseTrigger * >( m_hRescueTrigger.Get() );
	if ( !trigger ) return;
	const CUtlVector< EHANDLE > &touching = trigger->GetTouchingEntities();
	if ( !touching.Count() ) return;
	// Native chooses the last gas-can scorer when more than one is touching,
	// even if that scorer is not in this touching list. Do not repair the quirk.
	CTerrorPlayer *player = touching.Count() > 1 ? ToTerrorPlayer( TheDirector->m_hLastGasCanPourer.Get() ) : NULL;
	if ( !player ) player = ToTerrorPlayer( touching[0].Get() );
	if ( player ) player->m_bIsFirstManOut = true;
}
bool CDirectorScriptedMode::TryEndScriptedMode()
{
	HSCRIPT scope = TheDirector->GetOptionsScope();
	HSCRIPT function = g_pScriptVM->LookupFunction( "EndScriptedMode", scope );
	g_pScriptVM->ReleaseFunction( function );
	if ( !function ) return false;
	function = g_pScriptVM->LookupFunction( "EndScriptedMode", scope );
	ScriptVariant_t value;
	ScriptStatus_t status = function ? g_pScriptVM->Call( function, scope, true, &value ) : SCRIPT_ERROR;
	if ( function ) g_pScriptVM->ReleaseFunction( function );
	if ( status != SCRIPT_DONE ) { value.Free(); return false; }
	if ( value.GetType() == FIELD_INTEGER ) m_nEndScriptReason = (int)value;
	else { float raw; Q_memcpy( &raw, &value, sizeof( raw ) ); m_nEndScriptReason = (int)raw; }
	function = g_pScriptVM->LookupFunction( "GetScoreboardFilename", scope );
	if ( function )
	{
		g_pScriptVM->Call( function, scope, true, &value, m_nEndScriptReason );
		g_pScriptVM->ReleaseFunction( function );
	}
	if ( value.GetType() == FIELD_CSTRING )
	{
		Q_strncpy( m_szScoreboardFilename, (const char *)value, sizeof( m_szScoreboardFilename ) );
		TheDirector->m_bDelayMissionEndScoreboard = true;
	}
	TheDirector->EndRound( m_nEndScriptReason );
	value.Free();
	return true;
}
void CDirectorScriptedMode::ShowScoreboard()
{
	if ( !m_szScoreboardFilename[0] ) return;
	KeyValues *data = new KeyValues( "data" );
	data->SetString( "res", m_szScoreboardFilename );
	TerrorShowViewPortPanel( -1, "message_box", true, data );
	data->deleteThis();
}

#include "l4d2/vscript/terror_scripted_lifecycle_console_server_callbacks.inc"
