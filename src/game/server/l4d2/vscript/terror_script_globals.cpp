//========= Copyright Valve Corporation, All rights reserved. ============//
// P0.2: native server22E470 global callbacks. No alternate gameplay owners.
#include "cbase.h"
#include "l4d2/vscript/terror_script_globals.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/boss/witch.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/base_backpack_item.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/infected/special/spitter_projectile.h"
#include "l4d2/weapons/molotov_projectile.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "vscript_server.h"
#include "NextBot/NextBotManager.h"
#include "NextBot/NextBotInterface.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

namespace
{
// Native CVTTSS2SI, including its INT_MIN result for unordered/out-of-range
// operands. This must not become a C++ float->int conversion with undefined
// overflow. The original accepts integer or the raw float32 payload here.
int ScriptRawInteger( const ScriptVariant_t &value )
{
	if ( value.GetType() == FIELD_INTEGER ) return (int)value;
	unsigned int bits = value.GetRawPayload32();
	float raw;
	Q_memcpy( &raw, &bits, sizeof( raw ) );
	return _mm_cvtt_ss2si( _mm_set_ss( raw ) );
}
bool ScriptVectorValue( const ScriptVariant_t &value, Vector *position )
{
	if ( value.GetType() == FIELD_VECTOR ) { *position = (const Vector &)value; return true; }
	int type = value.GetType();
	bool scalar = type == FIELD_INTEGER || type == FIELD_FLOAT || type == FIELD_BOOLEAN ||
		type == FIELD_CHARACTER || type == FIELD_SHORT || type == FIELD_UINT ||
		type == FIELD_UINT64 || type == FIELD_FLOAT64;
	if ( type == FIELD_VOID || ( scalar && value.GetRawPayload32() == 0 ) ||
		( type == FIELD_HSCRIPT && !(HSCRIPT)value ) || ( type == FIELD_CSTRING && !(const char *)value ) )
	{
		*position = vec3_origin; // Actual native NULL-payload vector conversion.
		return true;
	}
	return false; // Ledger306: skip only the work using an invalid raw pointer.
}
CBaseEntity *ScriptEntityValue( const ScriptVariant_t &value )
{
	return value.GetType() == FIELD_HSCRIPT ? ToEnt( (HSCRIPT)value ) : NULL;
}
bool ScriptCheckType( const ScriptVariant_t &value, int type, const char *name )
{
	if ( value.GetType() == type ) return true;
	Warning( "Wrong field type (%s) for <%s> in HUD Table", VariantFieldTypeName( value.GetType() ), name );
	return false;
}
struct CountScriptSpecials
{
	int count;
	CountScriptSpecials() : count( 0 ) {}
	bool operator()( CBasePlayer *player )
	{
		if ( player->IsAlive() && player->GetTeamNumber() == TEAM_INFECTED ) ++count;
		return true;
	}
};
struct ScriptAssault
{
	bool operator()( INextBot *bot )
	{
		if ( bot->GetEntity()->GetTeamNumber() == TEAM_INFECTED && bot->GetEntity()->IsAlive() ) bot->OnCommandAssault();
		return true;
	}
};
struct ScriptBotCommand
{
	const Vector *position;
	CBaseEntity *target;
	bool retreat, attack, reset;
	bool operator()( INextBot *bot )
	{
		if ( !bot ) Warning( "Null bot passed to botcmd from script\n" );
		else if ( reset ) bot->OnCommandResume();
		else if ( retreat && target ) bot->OnCommandRetreat( target, 0.0f );
		else if ( attack && target ) bot->OnCommandAttack( target );
		else if ( position ) bot->OnCommandApproach( *position, 0.0f );
		else Warning( "No valid command for bot functor in CommandABot\n" );
		return true;
	}
};
struct ScriptInfectedFlags
{
	int flags;
	bool operator()( INextBot *bot )
	{
		if ( !bot ) Warning( "Null bot passed to botcmd from script\n" );
		else if ( Infected *infected = bot->GetEntity()->MyInfectedPointer() )
		{
			infected->ClearInfectedFlags( 0xE000 );
			infected->SetInfectedFlags( flags ); // Not masked: native ORs every supplied bit.
		}
		return true;
	}
};
}

// S2215D0/21F3B0: Specials includes alive ghosts and Tanks, not just classes1..6.
void Script_GetInfectedStats( HSCRIPT table )
{
	CountScriptSpecials specials;
	ForEachPlayer( specials );
	g_pScriptVM->SetValue( table, "Witches", TheDirector->GetWitchCount() );
	g_pScriptVM->SetValue( table, "Tanks", TheDirector->GetTankCount() );
	g_pScriptVM->SetValue( table, "Specials", specials.count );
	g_pScriptVM->SetValue( table, "Commons", GetCommonInfectedCount( 0 ) );
	g_pScriptVM->SetValue( table, "SpecialsSpawned", TheZombieManager->GetPanicSpecialSpawnCount( 0 ) );
	g_pScriptVM->SetValue( table, "PanicWaveSpecials", TheZombieManager->GetTotalPanicSpecialSpawns() );
}
void Script_StartAssault()
{
	ScriptAssault assault;
	TheNextBots().ForEachBot( assault );
}
// S35FA60. Missing position is NOT always an error: class0 uses actual
// background population; classes1..8 and bride11 use the existing5-try selector.
bool Script_ZSpawn( HSCRIPT table )
{
	ScriptVariant_t value;
	if ( !g_pScriptVM->GetValue( table, "type", &value ) || !ScriptCheckType( value, FIELD_INTEGER, "type" ) )
	{
		Msg( "ScriptWarning: Can't ZSpawn without a type\n" );
		g_pScriptVM->ReleaseValue( value );
		return false;
	}
	int zombieClass = (int)value;
	g_pScriptVM->ReleaseValue( value );
	Vector position;
	if ( g_pScriptVM->GetValue( table, "pos", &value ) )
	{
		if ( !ScriptCheckType( value, FIELD_VECTOR, "pos" ) )
		{
			Msg( "ScriptWarning: Can't ZSpawn without a position\n" );
			g_pScriptVM->ReleaseValue( value );
			return false;
		}
		position = (const Vector &)value;
		g_pScriptVM->ReleaseValue( value );
	}
	else
	{
		int placementClass = zombieClass == 11 ? 7 : zombieClass;
		if ( zombieClass == 0 )
		{
			TheTerrorNavMesh()->PopulateBackground( 1, false );
			return true; // Native success even if population does not materialize.
		}
		if ( !( placementClass > 0 && placementClass < 9 ) )
		{
			Warning( "Can't auto_pos for type %d\n", zombieClass );
			return false;
		}
		if ( !TheZombieManager->GetRandomPZSpawnPosition( placementClass, 5, NULL, &position ) )
		{
			Warning( "Couldn't find a spawn pos for type %d in %d tries\n", placementClass, 5 );
			return false;
		}
	}
	QAngle angles = vec3_angle;
	if ( g_pScriptVM->GetValue( table, "ang", &value ) )
	{
		if ( !ScriptCheckType( value, FIELD_QANGLE, "ang" ) )
		{
			g_pScriptVM->ReleaseValue( value );
			// Ledger306: native35FB89 uses three unwritten stack floats.
			// Do not invent zero angles or execute the dependent spawn.
			return false;
		}
		angles = (const QAngle &)value;
		g_pScriptVM->ReleaseValue( value );
	}
	return SpawnZombie( zombieClass, position, angles, -1, false, false );
}
const char *Script_GetCharacterDisplayName( HSCRIPT entity )
{
	CTerrorPlayer *player = ToTerrorPlayer( ToEnt( entity ) );
	return player && player->GetTeamNumber() == TEAM_SURVIVOR ? GetSurvivorCharacterDisplayName( player->GetSurvivorCharacter() ) : "";
}
HSCRIPT Script_GetPlayerFromCharacter( int character )
{
	CTerrorPlayer *player = FindSurvivorByCharacter( (SurvivorCharacterType)character );
	return player ? player->GetScriptInstance() : NULL;
}
float Script_GetCurrentFlowDistanceForPlayer( HSCRIPT entity )
{
	CTerrorPlayer *player = ToTerrorPlayer( ToEnt( entity ) );
	return player ? (float)player->GetFlowDistance( 0 ) : 0.0f;
}
#pragma float_control( precise, on, push )
float Script_GetCurrentFlowPercentForPlayer( HSCRIPT entity )
{
	CTerrorPlayer *player = ToTerrorPlayer( ToEnt( entity ) );
	if ( !player ) return 0.0f;
	float maximum = TheTerrorNavMesh()->GetMaxFlowDistance();
	if ( !( maximum > 0.0f ) ) return 0.0f; // COMISS/JBE rejects unordered.
	return (float)( player->GetFlowDistance( 0 ) * 100.0 / (double)maximum ); // S21EADD x87 chain; ledger270.
}
float Script_GetFlowDistanceForPosition( Vector position )
{
	TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( position, false, 10000.0f, false, true, TEAM_ANY ) );
	return area ? area->GetFlowDistanceFromStart() : 0.0f;
}
float Script_GetFlowPercentForPosition( Vector position, bool fromStart )
{
	TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( position, false, 10000.0f, false, true, TEAM_ANY ) );
	if ( !area ) return 0.0f;
	float maximum = TheTerrorNavMesh()->GetMaxFlowDistance();
	if ( !( maximum > 0.0f ) ) return 0.0f;
	float flow = fromStart ? area->GetFlowDistanceFromStart() : area->GetFlowDistanceTowardGoal();
	float scaled = flow * 100.0f; // S21C385 MULSS then DIVSS: both round to float32.
	return scaled / maximum;
}
#pragma float_control( pop )
float Script_GetMaxFlowDistance() { return TheTerrorNavMesh()->GetMaxFlowDistance(); }
void Script_GetInvTable( HSCRIPT entity, HSCRIPT table )
{
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( ToEnt( entity ) );
	if ( !player ) return; // Ledger306: native2213E0 dereferences the failed cast.
	for ( int weaponID = 1; weaponID < 56; ++weaponID )
	{
		CTerrorWeapon *weapon = player->GetWeaponOfType( weaponID );
		if ( !weapon ) continue;
		char key[64];
		Q_snprintf( key, sizeof( key ), "slot%d", weapon->GetSlot() );
		g_pScriptVM->SetValue( table, key, weapon->GetScriptInstance() );
		if ( weapon->GetSlot() == 3 )
		{
			CBaseBackpackItem *backpack = dynamic_cast< CBaseBackpackItem * >( weapon );
			if ( backpack && backpack->IsPerformingAction() ) DevMsg( "   is performing action!\n" );
		}
	}
	CBaseEntity *held = player->GetHeldEntity();
	if ( held )
	{
		DevMsg( "Holding %s\n", held->GetClassname() );
		g_pScriptVM->SetValue( table, "Held", held->GetScriptInstance() );
	}
}
void Script_RestoreTable( const char *name, HSCRIPT table ) { TheDirector->GetScriptedMode()->RestoreTable( name, table ); }
void Script_SaveTable( const char *name, HSCRIPT table ) { TheDirector->GetScriptedMode()->SaveTable( name, table ); }
void Script_ClearSavedTables() { TheDirector->GetScriptedMode()->ClearSavedTables(); }
void Script_DropSpit( Vector position )
{
	AngularImpulse spin;
	spin.x = RandomFloat( -10.0f, 10.0f );
	spin.y = RandomFloat( -10.0f, 10.0f );
	spin.z = 0.0f;
	CSpitterProjectile::Create( position, QAngle( -90.0f, 0.0f, 0.0f ), Vector( 0.0f, 0.0f, -5.0f ), spin, NULL );
}
void Script_DropFire( Vector position )
{
	AngularImpulse spin;
	spin.x = RandomFloat( -10.0f, 10.0f );
	spin.y = RandomFloat( -10.0f, 10.0f );
	spin.z = 0.0f;
	CMolotovProjectile::Create( position, QAngle( -90.0f, 0.0f, 0.0f ), Vector( 0.0f, 0.0f, -5.0f ), spin, NULL );
}
bool Script_CommandABot( HSCRIPT table )
{
	ScriptVariant_t value;
	if ( !g_pScriptVM->GetValue( table, "cmd", &value ) )
	{
		Warning( "You can't CommandABot w/o a Command!\n" );
		return false;
	}
	int command = ScriptRawInteger( value );
	g_pScriptVM->ReleaseValue( value );
	CBaseEntity *botEntity = NULL;
	bool invalidBot = false;
	if ( g_pScriptVM->GetValue( table, "bot", &value ) )
	{
		botEntity = ScriptEntityValue( value );
		g_pScriptVM->ReleaseValue( value );
		if ( botEntity && !botEntity->MyNextBotPointer() )
		{
			Warning( "You can't CommandABot a non-Bot! [%s]\n", botEntity->GetClassname() );
			return false;
		}
		invalidBot = !botEntity; // Ledger306: do not turn an invalid explicit bot into a broadcast.
	}
	CBaseEntity *target = NULL;
	if ( g_pScriptVM->GetValue( table, "target", &value ) )
	{
		target = ScriptEntityValue( value );
		g_pScriptVM->ReleaseValue( value );
	}
	Vector position = vec3_origin;
	bool validPosition = true;
	if ( g_pScriptVM->GetValue( table, "pos", &value ) )
	{
		validPosition = ScriptVectorValue( value, &position );
		g_pScriptVM->ReleaseValue( value );
	}
	ScriptBotCommand action = { NULL, NULL, false, false, false };
	switch ( command )
	{
	case 0:
		if ( !target ) { Warning( "Cannot CommandABot to attack w/o a Target!\n" ); return false; }
		action.target = target; action.attack = true;
		break;
	case 1:
		if ( validPosition && target ) position += target->GetAbsOrigin();
		if ( validPosition ) action.position = &position;
		break;
	case 2:
		if ( !target ) { Warning( "Cannot CommandABot to retreat w/o a Threat entity!\n" ); return false; }
		action.target = target; action.retreat = true;
		break;
	case 3: action.reset = true; break;
	default: Warning( "Invalid Command passed to CommandABot\n" ); return false;
	}
	if ( invalidBot || ( command == 1 && !validPosition ) ) return true; // Skip only undefined dependent dispatch.
	if ( botEntity ) action( botEntity->MyNextBotPointer() );
	else TheNextBots().ForEachBot( action ); // Native list order, no team/alive filtering.
	return true;
}
void Script_ReapplyInfectedFlags( int flags, HSCRIPT entity )
{
	CBaseEntity *target = ToEnt( entity );
	ScriptInfectedFlags action = { flags };
	if ( target ) action( target->MyNextBotPointer() );
	else TheNextBots().ForEachBot( action );
}
bool Script_HasPlayerControlledZombies() { return g_pGameRules && TerrorGameRules()->IsPlayerControlledZombies(); }
void Script_ForceVersusStart()
{
	if ( g_TerrorModes.IsBaseMode( "versus" ) ) TheDirector->GetVersusMode()->ForceSurvivorsOutOfCheckpoint();
}
void Script_ForceSurvivalStart()
{
	if ( g_TerrorModes.IsBaseMode( "survival" ) && !TheDirector->GetSurvivalMode()->HasStarted() )
		TheDirector->GetSurvivalMode()->OnRoundStarted( 1, true );
}
#pragma float_control( precise, on, push )
void Script_RushVictim( HSCRIPT entity, float range )
{
	CBaseEntity *target = ToEnt( entity );
	CBaseCombatCharacter *victim = target ? target->MyCombatCharacterPointer() : NULL;
	if ( !victim ) victim = PickLivingSurvivorForForceStart();
	if ( !victim ) return;
	struct Rush
	{
		CBaseCombatCharacter *victim;
		float range;
		bool operator()( INextBot *bot )
		{
			if ( bot->GetEntity()->GetTeamNumber() != TEAM_INFECTED || !bot->GetEntity()->IsAlive() ) return true;
			const Vector &origin = victim->GetAbsOrigin();
			const Vector &position = bot->GetPosition();
			float x = position.x - origin.x, y = position.y - origin.y, z = position.z - origin.z;
			float distanceSqr = ( x * x + y * y ) + z * z;
			if ( range * range > distanceSqr && !dynamic_cast< Witch * >( bot ) && bot->GetEntity() != victim )
				bot->OnCommandAttack( victim );
			return true;
		}
	} rush = { victim, range };
	TheNextBots().ForEachBot( rush );
}
#pragma float_control( pop )
bool Script_IsMissionFinalMap() { return TerrorGameRules()->IsFinalChapter(); }
HSCRIPT Script_FindRescueAreaTrigger()
{
	CBaseEntity *trigger = TheDirector->GetScriptedMode()->FindRescueTrigger();
	return trigger ? trigger->GetScriptInstance() : NULL;
}
int Script_GetDifficulty() { return TerrorGameRules()->GetDifficultyLevel(); }
int Script_GetScavengeItemsGoal() { return TerrorGameRules()->GetScavengeItemsGoal(); }
int Script_GetScavengeItemsRemaining() { return TerrorGameRules()->GetScavengeItemsRemaining(); }
void Script_SetScavengeItemsGoal( int count ) { TerrorGameRules()->SetScavengeItemsGoal( count ); }
void Script_SetScavengeItemsRemaining( int count ) { TerrorGameRules()->SetScavengeItemsRemaining( count ); }
bool Script_HasConfigurableDifficultySetting() { return g_TerrorModes.HasDifficulty(); }

// S2217A0: not the SDK's three-argument TraceLine or extended TraceLineEx.
bool Script_TraceLine( HSCRIPT table )
{
	Vector start, end;
	ScriptVariant_t value;
	bool haveStart = g_pScriptVM->GetValue( table, "start", &value );
	bool validStart = haveStart && ScriptVectorValue( value, &start );
	g_pScriptVM->ReleaseValue( value );
	bool haveEnd = g_pScriptVM->GetValue( table, "end", &value );
	bool validEnd = haveEnd && ScriptVectorValue( value, &end );
	g_pScriptVM->ReleaseValue( value );
	int mask = 0x02006081;
	if ( g_pScriptVM->GetValue( table, "mask", &value ) ) mask = ScriptRawInteger( value );
	g_pScriptVM->ReleaseValue( value );
	if ( !haveStart || !haveEnd )
	{
		Warning( "Didnt supply start and end to Script TraceLine call, failing, setting called to false\n" );
		DevMsg( "Inputs: start, end, mask, ignore  -- outputs: pos, fraction, hit, enthit, startsolid" );
		return false;
	}
	if ( !validStart || !validEnd ) return false; // Ledger306, invalid native raw vector pointers.
	CBaseEntity *ignore = NULL;
	if ( g_pScriptVM->GetValue( table, "ignore", &value ) ) ignore = ScriptEntityValue( value );
	g_pScriptVM->ReleaseValue( value );
	trace_t trace;
	UTIL_TraceLine( start, end, mask, ignore, COLLISION_GROUP_NONE, &trace );
	g_pScriptVM->SetValue( table, "fraction", trace.fraction );
	g_pScriptVM->SetValue( table, "hit", trace.fraction != 1.0f || trace.startsolid );
	if ( trace.m_pEnt ) g_pScriptVM->SetValue( table, "enthit", trace.m_pEnt->GetScriptInstance() );
	if ( trace.startsolid ) g_pScriptVM->SetValue( table, "startsolid", true );
	Vector position = start + ( end - start ) * trace.fraction;
	g_pScriptVM->SetValue( table, "pos", position );
	return true;
}
void Script_QueueSpeak( HSCRIPT entity, const char *concept, float delay, const char *context )
{
	CBaseEntity *target = ToEnt( entity );
	if ( !target ) return;
	AI_CriteriaSet criteria;
	if ( context && *context ) AppendSpeechContext( criteria, context );
	QueueResponseConceptForEntity( AIConcept_t( concept ), criteria, gpGlobals->curtime + delay, target, NULL );
}
