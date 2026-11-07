// NextBotCombatCharacter.cpp
// Next generation bot system
// Author: Michael Booth, April 2005
//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"

#include "team.h"
#include "CRagdollMagnet.h"

#include "NextBot.h"
#include "NextBotLocomotionInterface.h"
#include "NextBotBodyInterface.h"

#if defined( L4D2 )
#include "l4d2/gamerules/terror_gamerules.h"
#elif defined( TERROR )
#include "TerrorGamerules.h"
#endif

#include "vprof.h"
#include "datacache/imdlcache.h"
#include "EntityFlame.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar NextBotStop( "nb_stop", "0", 
#if defined( L4D2 )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Stop all NextBots" );


//--------------------------------------------------------------------------------------------------------
class CSendBotCommand
{
public:
	CSendBotCommand( const char *command )
	{
		m_command = command;
	}

	bool operator() ( INextBot *bot )
	{
		bot->OnCommandString( m_command );
		return true;
	}

	const char *m_command;
};



#if defined( L4D2 )
CON_COMMAND_F( nb_command, "Sends a command string to all bots", 0x00004004 )
#else
CON_COMMAND_F( nb_command, "Sends a command string to all bots", FCVAR_CHEAT )
#endif
{
	if ( args.ArgC() <= 1 )
	{
		Msg( "Missing command string" );
		return;
	}

	CSendBotCommand sendCmd( args.ArgS() );
	TheNextBots().ForEachBot( sendCmd );
}



//-----------------------------------------------------------------------------------------------------
BEGIN_DATADESC( NextBotCombatCharacter )

	DEFINE_THINKFUNC( DoThink ),

END_DATADESC()


//-----------------------------------------------------------------------------------------------------
IMPLEMENT_SERVERCLASS_ST( NextBotCombatCharacter, DT_NextBot )
END_SEND_TABLE()

//-----------------------------------------------------------------------------------------------------

#if !defined( L4D2 )
BEGIN_ENT_SCRIPTDESC( NextBotCombatCharacter, CBaseCombatCharacter, "Nextbot combat character" )
	DEFINE_SCRIPTFUNC( GetBotId, "Get this bot's id" )
	DEFINE_SCRIPTFUNC( FlagForUpdate, "Flag this bot for update" )
	DEFINE_SCRIPTFUNC( IsFlaggedForUpdate, "Is this bot flagged for update" )
	DEFINE_SCRIPTFUNC( GetTickLastUpdate, "Get last update tick" )
	DEFINE_SCRIPTFUNC_WRAPPED( GetLocomotionInterface, "Get this bot's locomotion interface" )
	DEFINE_SCRIPTFUNC_WRAPPED( GetBodyInterface, "Get this bot's body interface" )
	DEFINE_SCRIPTFUNC_WRAPPED( GetIntentionInterface, "Get this bot's intention interface" )
	DEFINE_SCRIPTFUNC_WRAPPED( GetVisionInterface, "Get this bot's vision interface" )
	DEFINE_SCRIPTFUNC_WRAPPED( IsEnemy, "Return true if given entity is our enemy" )
	DEFINE_SCRIPTFUNC_WRAPPED( IsFriend, "Return true if given entity is our friend" )
	DEFINE_SCRIPTFUNC( IsImmobile, "Return true if we haven't moved in awhile" )
	DEFINE_SCRIPTFUNC( GetImmobileDuration, "How long have we been immobile" )
	DEFINE_SCRIPTFUNC( ClearImmobileStatus, "Clear immobile status" )
	DEFINE_SCRIPTFUNC( GetImmobileSpeedThreshold, "Return units/second below which this actor is considered immobile" )
END_SCRIPTDESC();
#endif

//-----------------------------------------------------------------------------------------------------
NextBotDestroyer::NextBotDestroyer( int team )
{
	m_team = team;
}


//-----------------------------------------------------------------------------------------------------
bool NextBotDestroyer::operator() ( INextBot *bot  )
{
	if ( m_team == TEAM_ANY || bot->GetEntity()->GetTeamNumber() == m_team )
	{
		// players need to be kicked, not deleted
		if ( bot->GetEntity()->IsPlayer() )
		{
			CBasePlayer *player = dynamic_cast< CBasePlayer * >( bot->GetEntity() );
			engine->ServerCommand( UTIL_VarArgs( "kickid %d\n", player->GetUserID() ) );
		}
		else
		{
			UTIL_Remove( bot->GetEntity() );
		}
	}
	return true;
}


//-----------------------------------------------------------------------------------------------------

#if defined( L4D2 )
CON_COMMAND_F( nb_delete_all, "Delete all non-player NextBot entities.", 0x00004004 )
#else
CON_COMMAND_F( nb_delete_all, "Delete all non-player NextBot entities.", FCVAR_CHEAT )
#endif
{
	// Listenserver host or rcon access only!
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	CTeam *team = NULL;

	if ( args.ArgC() == 2 )
	{
		const char *teamName = args[1];

		for( int i=0; i < g_Teams.Count(); ++i )
		{
			if ( FStrEq( teamName, g_Teams[i]->GetName() ) )
			{
				// delete all bots on this team
				team = g_Teams[i];
				break;
			}
		}

		if ( team == NULL )
		{
			Msg( "Invalid team '%s'\n", teamName );
			return;
		}
	}

	// delete all bots on all teams
	NextBotDestroyer destroyer( team ? team->GetTeamNumber() : TEAM_ANY );
	TheNextBots().ForEachBot( destroyer );
}


//-----------------------------------------------------------------------------------------------------
class NextBotApproacher
{
public:
	NextBotApproacher( void )
	{
		CBasePlayer *player = UTIL_GetListenServerHost();
		if ( player )
		{
			Vector forward;
			player->EyeVectors( &forward );

			trace_t result;
			unsigned int mask = MASK_BLOCKLOS_AND_NPCS|CONTENTS_IGNORE_NODRAW_OPAQUE | CONTENTS_GRATE | CONTENTS_WINDOW;
			UTIL_TraceLine( player->EyePosition(), player->EyePosition() + 999999.9f * forward, mask, player, COLLISION_GROUP_NONE, &result );
			if ( result.DidHit() )
			{
				NDebugOverlay::Cross3D( result.endpos, 5, 0, 255, 0, true, 10.0f );
				m_isGoalValid = true;
				m_goal = result.endpos;
			}
			else
			{
				m_isGoalValid = false;
			}
		}
	}

	bool operator() ( INextBot *bot )
	{
		if ( TheNextBots().IsDebugFilterMatch( bot ) )
		{
			bot->OnCommandApproach( m_goal );
		}
		return true;
	}

	bool m_isGoalValid;
	Vector m_goal;
};


#if defined( L4D2 )
CON_COMMAND_F( nb_move_to_cursor, "Tell all NextBots to move to the cursor position", 0x00004004 )
#else
CON_COMMAND_F( nb_move_to_cursor, "Tell all NextBots to move to the cursor position", FCVAR_CHEAT )
#endif
{
	// Listenserver host or rcon access only!
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	NextBotApproacher approach;
	TheNextBots().ForEachBot( approach );
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
bool IgnoreActorsTraceFilterFunction( IHandleEntity *pServerEntity, int contentsMask )
{
	CBaseEntity *entity = EntityFromEntityHandle( pServerEntity );
#ifdef L4D2
	return entity->MyNextBotPointer() == NULL && !entity->IsPlayer();
#else
	return ( entity->MyCombatCharacterPointer() == NULL );
#endif
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
bool VisionTraceFilterFunction( IHandleEntity *pServerEntity, int contentsMask )
{
	// Honor BlockLOS also to allow seeing through partially-broken doors
	CBaseEntity *entity = EntityFromEntityHandle( pServerEntity );
	return ( entity->MyCombatCharacterPointer() == NULL && entity->BlocksLOS() );
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

NextBotCombatCharacter::NextBotCombatCharacter( void )

{
	m_lastAttacker = NULL;
	m_didModelChange = false;
}


//----------------------------------------------------------------------------------------------------------
void NextBotCombatCharacter::Spawn( void )
{
	BaseClass::Spawn();
	
	// reset bot components
	Reset();

	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_STANDABLE );
	
	SetMoveType( MOVETYPE_CUSTOM );	
	
	SetCollisionGroup( COLLISION_GROUP_PLAYER );

	m_iMaxHealth = m_iHealth;
	m_takedamage = DAMAGE_YES;

	MDLCACHE_CRITICAL_SECTION();
	InitBoneControllers( ); 

	// set up think callback
	SetThink( &NextBotCombatCharacter::DoThink );
	SetNextThink( gpGlobals->curtime );

	m_lastAttacker = NULL;
}


bool NextBotCombatCharacter::IsAreaTraversable( const CNavArea *area ) const
{
	if ( !area )
		return false;
	ILocomotion *mover = GetLocomotionInterface();
	if ( mover && !mover->IsAreaTraversable( area ) )
		return false;
	return BaseClass::IsAreaTraversable( area );
}


//----------------------------------------------------------------------------------------------------------
void NextBotCombatCharacter::DoThink( void )
{
	VPROF_BUDGET( "NextBotCombatCharacter::DoThink", "NextBot" );

	SetNextThink( gpGlobals->curtime );

	if ( BeginUpdate() )
	{
		// emit model change event	
		if ( m_didModelChange )
		{
			m_didModelChange = false;

			OnModelChanged();

			// propagate model change into NextBot event responders
			for ( INextBotEventResponder *sub = FirstContainedResponder(); sub; sub = NextContainedResponder( sub ) )
			{
				sub->OnModelChanged();
			}	
		}

		UpdateLastKnownArea();	

		// update bot components
		if ( !NextBotStop.GetBool() && (GetFlags() & FL_FROZEN) == 0 )
		{
			Update();
		}

		EndUpdate();
	}
}


//----------------------------------------------------------------------------------------------------------
void NextBotCombatCharacter::Touch( CBaseEntity *other )
{
	if ( ShouldTouch( other ) )
	{
		// propagate touch into NextBot event responders
		trace_t result;
		result = GetTouchTrace();

		// OnContact refers to *physical* contact, not triggers or other non-physical entities
		if ( result.DidHit() || other->MyCombatCharacterPointer() != NULL )
		{
			OnContact( other, &result );
		}
	}
	
	BaseClass::Touch( other );
}


//----------------------------------------------------------------------------------------------------------
void NextBotCombatCharacter::SetModel( const char *szModelName )
{
	// actually change the model
	BaseClass::SetModel( szModelName );
	
	// need to do a lazy-check because precache system also invokes this
	m_didModelChange = true;
}


//----------------------------------------------------------------------------------------------------------
void NextBotCombatCharacter::Ignite( float flFlameLifetime, bool bNPCOnly, float flSize, bool bCalledByLevelDesigner )
{
	BaseClass::Ignite( flFlameLifetime, bNPCOnly, flSize, bCalledByLevelDesigner );
	
	// propagate event to components
	OnIgnite();
}


//----------------------------------------------------------------------------------------------------------
void NextBotCombatCharacter::Ignite( float flFlameLifetime, CBaseEntity *pAttacker )
{
	if ( IsOnFire() )
		return;

	// BaseClass::Ignite stuff, plus SetAttacker on the flame, so our attacker gets credit
#if defined( L4D2 )
	CEntityFlame *pFlame = CEntityFlame::Create( this, flFlameLifetime, 0.0f, true );
#else
	CEntityFlame *pFlame = CEntityFlame::Create( this );
#endif
	if ( pFlame )
	{
#if defined( L4D2 )
		pFlame->SetAttacker( pAttacker );
#else
		pFlame->SetLifetime( flFlameLifetime );
#endif
		AddFlag( FL_ONFIRE );

		SetEffectEntity( pFlame );
	}
	m_OnIgnite.FireOutput( this, this );

	// propagate event to components
	OnIgnite();
}


//----------------------------------------------------------------------------------------------------------
int NextBotCombatCharacter::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	// track our last attacker
	if ( info.GetAttacker() && info.GetAttacker()->MyCombatCharacterPointer() )
	{
		m_lastAttacker = info.GetAttacker()->MyCombatCharacterPointer();
	}

	// propagate event to components
	OnInjured( info );
	
	return CBaseCombatCharacter::OnTakeDamage_Alive( info );
}


//----------------------------------------------------------------------------------------------------------
int NextBotCombatCharacter::OnTakeDamage_Dying( const CTakeDamageInfo &info )
{
	// track our last attacker	
	if ( info.GetAttacker()->MyCombatCharacterPointer() )
	{
		m_lastAttacker = info.GetAttacker()->MyCombatCharacterPointer();
	}

	// propagate event to components
	OnInjured( info );

	return CBaseCombatCharacter::OnTakeDamage_Dying( info );
}

//----------------------------------------------------------------------------------------------------------
/**
 * Can't use CBaseCombatCharacter's Event_Killed because it will immediately ragdoll us
 */
static int g_DeathStartEvent = 0;
void NextBotCombatCharacter::Event_Killed( const CTakeDamageInfo &info )
{
	// track our last attacker
	if ( info.GetAttacker() && info.GetAttacker()->MyCombatCharacterPointer() )
	{
		m_lastAttacker = info.GetAttacker()->MyCombatCharacterPointer();
	}

	// propagate event to my components
	OnKilled( info );
	
	// Advance life state to dying
	m_lifeState = LIFE_DYING;

#if defined( TERROR ) || defined( L4D2 )
	// Native4AA230: generic entity notice precedes the infected-specific notice.
	TerrorGameRules()->DeathNoticeForEntity( this, info );
	TerrorGameRules()->DeathNoticeForInfected( this, info );
#endif

	if ( GetOwnerEntity() != NULL )
	{
		GetOwnerEntity()->DeathNotice( this );
	}

	// inform the other bots
	TheNextBots().OnKilled( this, info );
}


//----------------------------------------------------------------------------------------------------------
void NextBotCombatCharacter::PerformCustomPhysics( Vector *pNewPosition, Vector *pNewVelocity, QAngle *pNewAngles, QAngle *pNewAngVelocity )
{
	ILocomotion *mover = GetLocomotionInterface();
	if ( mover )
	{
		// hack to keep ground entity from being NULL'd when Z velocity is positive
		SetGroundEntity( mover->GetGround() );
	}
}


//----------------------------------------------------------------------------------------------------------
bool NextBotCombatCharacter::BecomeRagdoll( const CTakeDamageInfo &info, const Vector &forceVector )
{
	// See if there's a ragdoll magnet that should influence our force.
	Vector adjustedForceVector = forceVector;
	CRagdollMagnet *magnet = CRagdollMagnet::FindBestMagnet( this );
	if ( magnet )
	{
		adjustedForceVector += magnet->GetForceVector( this );
	}

	// clear the deceased's sound channels.(may have been firing or reloading when killed)
	EmitSound( "BaseCombatCharacter.StopWeaponSounds" );

	return BaseClass::BecomeRagdoll( info, adjustedForceVector );
}


//----------------------------------------------------------------------------------------------------------
void NextBotCombatCharacter::HandleAnimEvent( animevent_t *event )
{
	// propagate event to components
	OnAnimationEvent( event );
}


//----------------------------------------------------------------------------------------------------------
/**
 * Propagate event into NextBot event responders
 */
void NextBotCombatCharacter::OnNavAreaChanged( CNavArea *enteredArea, CNavArea *leftArea )
{
	INextBotEventResponder::OnNavAreaChanged( enteredArea, leftArea );

	BaseClass::OnNavAreaChanged( enteredArea, leftArea );
}


//----------------------------------------------------------------------------------------------------------
Vector NextBotCombatCharacter::EyePosition( void )
{
	if ( GetBodyInterface() )
	{
		return GetBodyInterface()->GetEyePosition();
	}

	return BaseClass::EyePosition();
}


//----------------------------------------------------------------------------------------------------------
/**
 * Return true if this object can be +used by the bot
 */
bool NextBotCombatCharacter::IsUseableEntity( CBaseEntity *entity, unsigned int requiredCaps )
{
	if ( entity )
	{
		int caps = entity->ObjectCaps();
		if ( caps & (FCAP_IMPULSE_USE|FCAP_CONTINUOUS_USE|FCAP_ONOFF_USE|FCAP_DIRECTIONAL_USE) )
		{
			if ( (caps & requiredCaps) == requiredCaps )
			{
				return true;
			}
		}
	}

	return false;
}


//----------------------------------------------------------------------------------------------------------
void NextBotCombatCharacter::UseEntity( CBaseEntity *entity, USE_TYPE useType )
{
	if ( IsUseableEntity( entity ) )
	{
		variant_t emptyVariant;
		entity->AcceptInput( "Use", this, this, emptyVariant, useType );
	}
}

#if defined( L4D2 )
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "NextBotLocomotionInterface.h"
#include "NextBotIntentionInterface.h"
#include "filesystem.h"
#include "tier1/fmtstr.h"
#include <time.h>

static ConVar nb_stuck_dump_threshold( "nb_stuck_dump_threshold", "-1", 
#if defined( L4D2 )
	0x00080004
#else
	0x80000
#endif
,
	"If a bot is stuck equal to or longer than this value in seconds, dump debug data to console and logfile. -1 to disable" ); // S5ABED0

// S4AD670..4AD7F4. One quiet256 cache, keyed by the native PREFIX comparison
// against its map substring, not an exact map-name key and not the current date.
static const char *GetTerrorStuckLogFilename()
{
	static CFmtStrN< 256, true > filename;
	if ( filename.Get()[0] )
	{
		const size_t mapLength = V_strlen( STRING( gpGlobals->mapname ) );
		if ( V_strncmp( STRING( gpGlobals->mapname ), filename.Get() + 5, mapLength ) == 0 )
			return filename.Get();
	}
	char timeText[128];
	char dateText[128];
	_strtime_s( timeText, sizeof( timeText ) );
	for ( int i = 0; timeText[i]; ++i )
	{
		if ( timeText[i] == ':' )
		{
			if ( i > 3 )
			{
				timeText[i] = '\0';
				break;
			}
			timeText[i] = '-';
		}
	}
	_strdate_s( dateText, sizeof( dateText ) );
	for ( int i = 0; dateText[i]; ++i )
	{
		if ( dateText[i] == '\\' || dateText[i] == '/' )
		{
			if ( i > 3 )
			{
				dateText[i] = '\0';
				break;
			}
			dateText[i] = '-';
		}
	}
	filename.Clear();
	filename.sprintf( "logs\\%s_%s_%s.stuck", STRING( gpGlobals->mapname ), dateText, timeText );
	return filename.Get();
}

// S4AD800..4ADC2D. The inherited responder implementation is exactly the
// original FirstContainedResponder/NextContainedResponder OnStuck fanout.
void INextBot::OnStuck( void )
{
	INextBotEventResponder::OnStuck();
	if ( nb_stuck_dump_threshold.GetFloat() >= 0.0f )
	{
		ILocomotion *locomotion = GetLocomotionInterface();
		const float threshold = nb_stuck_dump_threshold.GetFloat();
		if ( locomotion->GetStuckDuration() >= static_cast< double >( threshold ) )
		{
			FileHandle_t file = filesystem->Open( GetTerrorStuckLogFilename(), "at", NULL );
			// Shared S33C180 owner: format -> console -> optional file. Main's
			// standing formatted-text rule uses Msg("%s",buffer), never Msg(buffer).
			TerrorDebugPrintf( file, "\n" );
			const double duration = GetLocomotionInterface()->GetStuckDuration();
			const char *identifier = GetDebugIdentifier();
			TerrorDebugPrintf( file, "STUCK: %s for %2.2f seconds\n", identifier, duration );
			TerrorDebugPrintf( file, "   Entindex: %d\n", GetEntity()->entindex() );

			// The three GetEntity/absolute-transform resolutions are distinct.
			const Vector &originForZ = GetEntity()->GetAbsOrigin();
			const Vector &originForY = GetEntity()->GetAbsOrigin();
			const Vector &originForX = GetEntity()->GetAbsOrigin();
			const double originZ = static_cast< double >( originForZ.z );
			const double originY = static_cast< double >( originForY.y );
			const double originX = static_cast< double >( originForX.x );
			TerrorDebugPrintf( file, "   Origin: %2.2f %2.2f %2.2f\n", originX, originY, originZ );
			const float yaw = GetEntity()->GetAbsAngles().y;
			const float pitch = GetEntity()->GetAbsAngles().x;
			const double roll = static_cast< double >( GetEntity()->GetAbsAngles().z );
			// Native S4ADA38 prints ROLL, PITCH, YAW in this diagnostic.
			TerrorDebugPrintf( file, "   Angles: %2.2f %2.2f %2.2f\n", roll,
				static_cast< double >( pitch ), static_cast< double >( yaw ) );
			const char *behavior = GetIntentionInterface()->DebugString();
			TerrorDebugPrintf( file, "   Behavior: %s\n", behavior );

			ILocomotion *first = GetLocomotionInterface();
			ILocomotion *second = GetLocomotionInterface();
			ILocomotion *third = GetLocomotionInterface();
			const Vector &goalForX = third->GetApproachGoal();
			const Vector &goalForZ = first->GetApproachGoal();
			const double goalZ = static_cast< double >( goalForZ.z );
			const Vector &goalForY = second->GetApproachGoal();
			const double goalY = static_cast< double >( goalForY.y );
			const double goalX = static_cast< double >( goalForX.x );
			TerrorDebugPrintf( file, "   Approaching: %2.2f %2.2f %2.2f\n", goalX, goalY, goalZ );
			TerrorDebugPrintf( file, "   Time: %3.2f\n", static_cast< double >( gpGlobals->curtime ) );
			if ( file )
				filesystem->Close( file );
		}
	}
	if ( GetLocomotionInterface()->GetStuckDuration() >= 3.0 )
	{
		CBaseCombatCharacter *entity = GetEntity(); // S4ADB5F: adjusted PRIMARY actor, not this INextBot subobject.
		if ( entity && entity->IsPlayer() )
		{
			CTerrorPlayer *player = static_cast< CTerrorPlayer * >( entity );
			const int zombieClass = player->GetZombieClass();
			if ( ( ( zombieClass >= 1 && zombieClass <= 6 ) || zombieClass == 8 ) && !player->m_hasReportedStuck )
			{
				player->m_hasReportedStuck = true;
				L4DGameStats().RecordStuckZombie( player );
			}
		}
	}
	if ( TerrorGameRules()->IsCoopMode() && !TerrorGameRules()->IsPlayerControlledZombies() )
	{
		if ( GetLocomotionInterface()->GetStuckDuration() >= 120.0 )
		{
			CBaseCombatCharacter *entity = GetEntity();
			if ( entity && entity->IsPlayer() )
			{
				CTerrorPlayer *player = static_cast< CTerrorPlayer * >( entity );
				if ( static_cast< unsigned int >( player->GetZombieClass() ) - 1U <= 5U )
					player->CommitSuicide( false ); // Native virtual2268 S2FACE0; no extra alive/team gate.
			}
		}
	}
}
#endif // L4D2
