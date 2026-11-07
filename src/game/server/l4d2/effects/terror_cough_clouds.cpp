//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "igamesystem.h"
#include "GameEventListener.h"
#include "tier0/memdbgon.h"

static ConVar z_cough_cloud_radius( "z_cough_cloud_radius", "175", 0x00004004,
	"How far from center of smoke cloud makes you cough." );
// S598940 registers this cvar, but there are NO original reads of it. The
// pinned frame updater does not impose this delay; do not invent that gate.
static ConVar z_cough_cloud_initial_cough_delay( "z_cough_cloud_initial_cough_delay", "0", 0x00004004,
	"You don't cough until smoke cloud exists for this long." );
static ConVar z_cough_cloud_expire( "z_cough_cloud_expire", "14", 0x00004004,
	"You don't cough after a smoke cloud has existed this long." );

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// Actual RTTI CSmokeCloudSystem, S2CB810/2CBAE0/2CBB80/2CBD80. It is a
// process-lifetime game system listening to genuine Smoker player_death events,
// not a synthetic trigger, entity-name classifier, or generic perception bus.
class CSmokeCloudSystem : public CAutoGameSystemPerFrame, public CGameEventListener
{
public:
	CSmokeCloudSystem() : CAutoGameSystemPerFrame( "CSmokeCloudSystem" ) {}
	virtual void LevelInitPreEntity() OVERRIDE
	{
		ListenForGameEvent( "player_death" );
		m_updateTimer.Start( 1.0f );
		m_clouds.RemoveAll();
	}
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE
	{
		if ( Q_stricmp( event->GetName(), "player_death" ) )
			return;
		CTerrorPlayer *smoker = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		if ( !smoker || smoker->GetTeamNumber() != TEAM_INFECTED || smoker->GetZombieClass() != 1 )
			return;
		Cloud cloud;
		cloud.m_origin = smoker->WorldSpaceCenter();
		cloud.m_creationTime = gpGlobals->curtime;
		cloud.m_source = smoker;
		m_clouds.AddToTail( cloud );
	}
	virtual void FrameUpdatePostEntityThink() OVERRIDE
	{
		if ( !m_updateTimer.IsElapsed() )
			return;
		m_updateTimer.Start( 1.0f );
		for ( int i = m_clouds.Count() - 1; i >= 0; --i )
		{
			const Cloud &cloud = m_clouds[i];
			if ( gpGlobals->curtime > z_cough_cloud_expire.GetFloat() + cloud.m_creationTime )
			{
				m_clouds.Remove( i );
				continue;
			}
			for ( int slot = 1; slot <= gpGlobals->maxClients; ++slot )
			{
				CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( slot ) );
				if ( !player || FNullEnt( player->edict() ) || !player->IsConnected() || !player->IsAlive() ||
					( player->GetTeamNumber() != TEAM_SURVIVOR && player->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) )
					continue;
				const float range = z_cough_cloud_radius.GetFloat();
				if ( !( range * range > player->EyePosition().DistToSqr( cloud.m_origin ) ) )
					continue;
				CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
				trace_t trace;
				UTIL_TraceLine( player->EyePosition(), cloud.m_origin, 0x0200400B, &filter, &trace );
				if ( !( trace.fraction < 1.0f ) && !trace.allsolid && !trace.startsolid )
					player->OnCough( cloud.m_source.Get() );
			}
		}
	}
private:
	struct Cloud
	{
		Vector m_origin;
		float m_creationTime;
		CHandle< CTerrorPlayer > m_source;
	};
	CountdownTimer m_updateTimer;
	CUtlVector< Cloud > m_clouds;
};

static CSmokeCloudSystem s_smokeCloudSystem;

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
