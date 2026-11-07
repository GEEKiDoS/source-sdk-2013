//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/content/terror_model_precache.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_bot_path_cost.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "l4d2/player/transitioning_player.h"
#include "gameinterface.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "l4d2/player/bots/survivor_bot_console_server_cvars.inc"
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/weapons/terror_melee_weapon_parse.h"
#include "NextBot/NextBotManager.h"
#endif
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( survivor_bot, SurvivorBot );
IMPLEMENT_SERVERCLASS_ST( SurvivorBot, DT_SurvivorBot )
	SendPropExclude( "DT_BaseCombatCharacter", "bcc_localdata" ),
	SendPropExclude( "DT_CSPlayer", "cslocaldata" ),
	SendPropExclude( "DT_TerrorPlayer", "terrorlocaldata" ),
	SendPropExclude( "DT_LocalPlayerExclusive", "m_nTickBase" ),
	SendPropInt( SENDINFO( m_humanSpectatorUserID ) ),
	SendPropInt( SENDINFO( m_humanSpectatorEntIndex ) ),
END_SEND_TABLE()

extern ConVar sb_transition;

ConVar sb_reachability_cache_lifetime( "sb_reachability_cache_lifetime", "3", 0x00004004 );
ConVar sb_reachable_cache_paranoia( "sb_reachable_cache_paranoia", "0", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL );

// Retail's 96-entry process-wide cache is ordered, not keyed or per bot.
// Spawn invalidates only the timestamps, leaving the other fields alone.
struct SurvivorReachabilityCacheEntry
{
	CNavArea *from;
	CNavArea *to;
	bool reachable;
	float timestamp;
};
static SurvivorReachabilityCacheEntry s_reachabilityCache[96];

static void InsertSurvivorReachability( CNavArea *from, CNavArea *to, int index, bool reachable, float timestamp )
{
	if ( from && to )
	{
		for ( int i = ARRAYSIZE( s_reachabilityCache ) - 1; i > index; --i )
			s_reachabilityCache[i] = s_reachabilityCache[i - 1];
		s_reachabilityCache[index].from = from;
		s_reachabilityCache[index].to = to;
		s_reachabilityCache[index].reachable = reachable;
		s_reachabilityCache[index].timestamp = timestamp;
	}
}

static bool IsSurvivorReachabilityNeighbor( SurvivorBot *bot, CNavArea *area, CNavArea *cachedArea, bool forward )
{
	SurvivorBotPathCost cost( bot, true );
	if ( !cachedArea->IsConnected( area, NUM_DIRECTIONS ) )
		return false;
	return ( forward ? cost( cachedArea, area, NULL, NULL, -1.0f ) : cost( area, cachedArea, NULL, NULL, -1.0f ) ) >= 0.0f;
}

static int FindSurvivorReachabilityNeighbor( SurvivorBot *bot, CNavArea *from, CNavArea *to )
{
	for ( int i = 0; i < ARRAYSIZE( s_reachabilityCache ); ++i )
	{
		const SurvivorReachabilityCacheEntry cached = s_reachabilityCache[i];
		if ( cached.timestamp < 0.0f || gpGlobals->curtime - cached.timestamp > sb_reachability_cache_lifetime.GetFloat() )
			return 2;
		if ( ( cached.from->GetID() == from->GetID() || IsSurvivorReachabilityNeighbor( bot, from, cached.from, cached.reachable ) ) &&
			( cached.to->GetID() == to->GetID() || IsSurvivorReachabilityNeighbor( bot, to, cached.to, !cached.reachable ) ) )
		{
			InsertSurvivorReachability( from, to, i, cached.reachable, cached.timestamp );
			if ( sb_reachable_cache_paranoia.GetBool() )
			{
				SurvivorBotPathCost cost( bot, true );
				if ( cached.reachable != NavAreaBuildPath( from, to, NULL, cost ) &&
					NavAreaBuildPath( cached.from, cached.to, NULL, cost ) == cached.reachable )
				{
					Warning( "Call brian! Reachability neighbor cache screwed! from: %d (cached neighbor %d) to: %d (cached neighbor %d)\n",
						from->GetID(), cached.from->GetID(), to->GetID(), cached.to->GetID() );
				}
			}
			return !cached.reachable;
		}
	}
	return 2;
}

SurvivorBot::SurvivorBot() : m_teamSituation( this )
{
	m_locomotor = new SurvivorLocomotion( this );
	m_body = new SurvivorBody( this );
	m_intention = new SurvivorIntention( this );
	m_vision = new SurvivorVision( this );
	m_humanSpectatorUserID = 0;
	m_humanSpectatorEntIndex = 0;
	m_humanLoadingProgress = 0;
	m_humanSpectatorName[0] = '\0';
	m_isHumanTransitioning = false;
	m_hasHumanTransitionReservation = false;
	m_isLowOnAmmo = false;
	m_pendingSpeechConcept = NULL;
	m_speechHistoryIndex = 0;
	m_speechHistoryCount = 0;
	m_alternateForcedFire = false;
	m_creationTimer.Start();
	m_healFriendTimer.Start( RandomFloat( 1.0f, 1.5f ) );
	// In particular, SurvivorVision's recognition RNG occurs AFTER the
	// healing delay RNG above, through the genuine component reset chain.
	INextBot::Reset();
}

void SurvivorBot::OnPlayerDisconnected( CTerrorPlayer * )
{
	// S38B830 ignores the disconnected actor and resets this same snapshot.
	m_teamSituation.Reset();
}

SurvivorBot::~SurvivorBot()
{
	delete m_locomotor;
	m_locomotor = NULL;
	delete m_body;
	m_body = NULL;
	delete m_intention;
	m_intention = NULL;
	delete m_vision;
	m_vision = NULL;
	// CUtl and Path members then destroy in native reverse ownership order.
}

CBasePlayer *SurvivorBot::AllocatePlayerEntity( edict_t *edict, const char *name )
{
	return CBasePlayer::CreatePlayer( "survivor_bot", edict );
}

SurvivorBot *SurvivorBot::MySurvivorBotPointer() const { return const_cast< SurvivorBot * >( this ); }
SurvivorLocomotion *SurvivorBot::GetLocomotionInterface() const { return m_locomotor; }
SurvivorBody *SurvivorBot::GetBodyInterface() const { return m_body; }
SurvivorIntention *SurvivorBot::GetIntentionInterface() const { return m_intention; }
SurvivorVision *SurvivorBot::GetVisionInterface() const { return m_vision; }

SurvivorTeamSituation &SurvivorBot::GetTeamSituation() const
{
	m_teamSituation.PruneInvalidTeammates();
	return m_teamSituation;
}

bool SurvivorBot::SetHumanSpectator( CTerrorPlayer *human )
{
	if ( m_humanSpectatorUserID > 0 )
		return false;
	// Native 398640 has no null-human authorization path. TakeOverBot's
	// SetHumanSpectator(NULL) call is refused by the positive-userid guard.
	m_humanSpectatorUserID = human->GetUserID();
	m_humanSpectatorEntIndex = human->edict() ? ENTINDEX( human->edict() ) : 0;
	m_humanLoadingProgress = 0;
	m_isHumanTransitioning = false;
	m_humanSpectatorName[0] = '\0';
	m_hasHumanTransitionReservation = false;
	return true;
}

bool SurvivorBot::SetHumanSpectator( int userID, const char *name )
{
	// S398760 reserves the real human userid and a32-byte name.17228 is
	// engine loading progress, not a second user/admission identifier.
	if ( m_humanSpectatorUserID > 0 ) return false;
	m_humanSpectatorUserID = userID;
	CBasePlayer *human = UTIL_PlayerByUserId( userID );
	m_humanSpectatorEntIndex = human && human->edict() ? ENTINDEX( human->edict() ) : 0;
	m_humanLoadingProgress = 0;
	m_hasHumanTransitionReservation = true;
	Q_strncpy( m_humanSpectatorName, name, sizeof( m_humanSpectatorName ) );
	m_isHumanTransitioning = userID > 0 && !g_bTerrorRestartAfterDeath;
	return true;
}

bool SurvivorBot::UpdateTransitionReservation()
{
	// S399800 after397410/38F370. true returns to its original blocked
	// waiting-human branch; false continues the real bot action update.
	if ( m_humanSpectatorUserID <= 0 ) return false;
	CTerrorPlayer *human = ToTerrorPlayer( UTIL_PlayerByUserId( m_humanSpectatorUserID ) );
	if ( human )
	{
		if ( human->GetTeamNumber() == TEAM_SPECTATOR ) return false;
	}
	else if ( m_hasHumanTransitionReservation )
	{
		if ( TerrorEngine_IsUserIDInServer( m_humanSpectatorUserID ) )
		{
			m_humanLoadingProgress = TerrorEngine_GetLoadingProgressForUserID( m_humanSpectatorUserID );
			return true;
		}
		if ( sb_transition.GetBool() )
		{
			g_SavedTerrorPlayerStates.MarkPlayerRestoreDone( m_humanSpectatorUserID );
			m_isHumanTransitioning = false;
		}
	}
	ClearHumanSpectator();
	return false;
}

void SurvivorBot::ClearHumanSpectator()
{
	m_humanSpectatorUserID = 0;
	m_humanSpectatorEntIndex = 0;
	m_humanLoadingProgress = 0;
	m_isHumanTransitioning = false;
	m_humanSpectatorName[0] = '\0';
	m_hasHumanTransitionReservation = false;
}

bool SurvivorBot::HasConnectedHumanSpectator() const
{
	return UTIL_PlayerByUserId( m_humanSpectatorUserID ) != NULL;
}

bool SurvivorBot::CanReachArea( CNavArea *from, CNavArea *to )
{
	if ( !from || !to || to->IsBlocked( TEAM_ANY, false ) )
		return false;
	if ( from == to )
		return true;
	int cachedResult = 2;
	for ( int i = 0; i < ARRAYSIZE( s_reachabilityCache ); ++i )
	{
		const SurvivorReachabilityCacheEntry &cached = s_reachabilityCache[i];
		if ( cached.timestamp < 0.0f || gpGlobals->curtime - cached.timestamp > sb_reachability_cache_lifetime.GetFloat() )
			break;
		if ( cached.from->GetID() == from->GetID() && cached.to->GetID() == to->GetID() )
		{
			cachedResult = !cached.reachable;
			break;
		}
	}
	if ( cachedResult == 2 )
		cachedResult = FindSurvivorReachabilityNeighbor( this, from, to );
	if ( cachedResult != 2 )
		return cachedResult == 0;
	SurvivorBotPathCost cost( this, true );
	bool reachable = NavAreaBuildPath( from, to, NULL, cost );
	InsertSurvivorReachability( from, to, 0, reachable, gpGlobals->curtime );
	return reachable;
}

bool SurvivorBot::CanReachSurvivor( CTerrorPlayer *player )
{
	if ( !player || !player->IsAlive() )
		return false;
	if ( player == this )
		return true;
	CNavArea *area = player->GetLastKnownArea();
	if ( player->m_isHangingFromLedge && player->m_hangStandArea )
		area = player->m_hangStandArea;
	if ( !area )
		return false;
	Vector closest;
	area->GetClosestPointOnArea( player->GetAbsOrigin(), &closest );
	if ( ( player->GetAbsOrigin() - closest ).AsVector2D().LengthSqr() > 10000.0f )
		return false;
	return CanReachArea( GetLastKnownArea(), area );
}

bool SurvivorBot::IsReachable( CBaseEntity *entity )
{
	CNavArea *area = TheNavMesh->GetNearestNavArea( entity->WorldSpaceCenter(), true, 200.0f, true, false );
	return CanReachArea( GetLastKnownArea(), area );
}

bool SurvivorBot::IsImmediateThreat( CBaseCombatCharacter *threat ) const
{
	return m_intention->IsImmediateThreat( const_cast< SurvivorBot * >( this ), threat );
}

const SurvivorKnownEntity *SurvivorBot::GetPrimaryThreat() const
{
	return m_intention->GetPrimaryKnownThreat();
}


// S3971D0: reset the genuine NextBot/player first. In particular, reservation,
// creation time and healing-delay state survive this reset.
void SurvivorBot::Spawn()
{
	BaseClass::Spawn();
	m_debugMoveGoal.Init();
	m_escapePath.Invalidate();
	m_unclearedAreas.RemoveAll();
	m_lookAroundTimer.Invalidate();
	m_lookAroundInterval.Invalidate();
	m_witchFlashlightTimer.Invalidate();
	m_teamSituation.Reset();
	m_farthestVisibleArea = NULL;
	m_nearestVisibleArea = NULL;
	m_isInNarrowCorridor = false;
	m_isReadyToLeaveCheckpoint = false;
	m_pendingSpeechConcept = NULL;
	m_pendingSpeechPriority = 0;
	m_pendingSpeechTimer.Invalidate();
	m_pendingSpeechTarget = NULL;
	for ( int i = 0; i < ARRAYSIZE( s_reachabilityCache ); ++i )
		s_reachabilityCache[i].timestamp = -1.0f;
	m_speechHistoryIndex = 0;
	m_speechHistoryCount = 0;
	if ( !IsInCheckpoint() )
		SpeakConcept( "SurvivorBotRescued", 2, RandomFloat( 1.0f, 2.0f ), NULL );
	for ( int i = 0; i < ARRAYSIZE( m_friendActionTimer ); ++i )
		m_friendActionTimer[i].Invalidate();
	m_attentionSubject = NULL;
	m_attentionSubjectTimer.Invalidate();
	m_resetIntention = false;
	m_humanProximityLookAtTimer.Invalidate();
}

#include "l4d2/player/bots/survivor_bot_console_server_callbacks.inc"

BEGIN_DATADESC( SurvivorBot )
END_DATADESC()
