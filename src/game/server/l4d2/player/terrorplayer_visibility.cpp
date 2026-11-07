//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_search.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/sound/terror_expresser.h"
#include "l4d2/entities/zombie_border.h"
#include "nav_mesh.h"
#include "tier0/memdbgon.h"

static ConVar survivor_lazy_active_set( "survivor_lazy_active_set", "1", 0x00004004 );
static ConVar music_large_area_reveal_threshold( "music_large_area_reveal_threshold", "500000", 0x00004004,
	"How much new area must be revealed to trigger the reveal music" );
extern ConVar survivor_debug_active_area_set;

// S2FBA10: survivors, living non-ghost hunters, and pre-round/transition
// infected must remain available independently of the recipient's PVS.
int CTerrorPlayer::ShouldTransmit( const CCheckTransmitInfo *info )
{
	const int team = GetTeamNumber();
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ||
		( team == TEAM_INFECTED &&
			( ( GetZombieClass() == 3 && IsAlive() && !IsGhost() ) ||
				!TheDirector->HasAnySurvivorLeftSafeArea() || TheDirector->IsInTransition() ) ) )
		return FL_EDICT_ALWAYS;

	return BaseClass::ShouldTransmit( info );
}

// S30F010. STOP_SCAN is a traversal barrier. A border-excluded point is
// omitted from the collection but its neighbors must still be expanded.
class CPlayerAASCollector
{
public:
	CPlayerAASCollector() { m_areas.EnsureCapacity( 256 ); }
	bool operator()( TerrorNavArea *area )
	{
		if ( area->HasSpawnAttributes( TERROR_NAV_STOP_SCAN ) )
			return false;
		if ( !CZombieBorder::IsPointBeyondBorder( area->GetCenter() ) && !area->IsAASCollectionMarked() )
		{
			area->MarkAASCollection();
			m_areas.AddToTail( area );
		}
		return true;
	}
	CUtlVector< TerrorNavArea * > m_areas;
};

class CClearPlayerAreaVisibility
{
public:
	explicit CClearPlayerAreaVisibility( SurvivorCharacterType character ) : m_character( character ) {}
	bool operator()( CNavArea *base )
	{
		TerrorNavArea *area = ToTerrorNavArea( base );
		area->SetPotentiallyVisibleByCharacter( m_character, false );
		area->SetCompletelyVisibleByCharacter( m_character, false );
		return true;
	}
private:
	SurvivorCharacterType m_character;
};

// S30B720. Static PVS is used in this visibility/exposure phase only, NOT as
// a substitute for the separate radius/heap AAS collection above.
class CMarkPlayerAreaVisibility
{
public:
	CMarkPlayerAreaVisibility( CTerrorPlayer *player, CNavArea *area ) :
		m_player( player ), m_area( area ), m_exposedArea( 0.0f ) {}
	bool operator()( CNavArea *base )
	{
		TerrorNavArea *area = ToTerrorNavArea( base );
		area->SetPotentiallyVisibleByCharacter( m_player->GetSurvivorCharacter(), true );
		if ( m_area->IsCompletelyVisible( area ) )
		{
			area->SetCompletelyVisibleByCharacter( m_player->GetSurvivorCharacter(), true );
			if ( !area->IsCleared( m_player ) )
				m_exposedArea = area->GetSizeY() * area->GetSizeX() + m_exposedArea;
			area->MarkAsCleared( m_player );
		}
		return true;
	}
	float m_exposedArea;
private:
	CTerrorPlayer *m_player;
	CNavArea *m_area;
};

// S335740. The second argument is an OLD AREA POINTER, not a bool. In the
// pinned assembly ECX=oldArea is passed directly to S30B610's PVS clear pass.
void CTerrorPlayer::UpdatePotentiallyVisibleAreas( CNavArea *newArea, CNavArea *oldArea )
{
	const SurvivorCharacterType character = GetSurvivorCharacter();
	if ( !newArea )
	{
		// Death/removal scans every area, including areas no longer in this
		// player's cached vector. Character8 membership uses any-nonzero;
		// area enter/leave themselves still manipulate bit8, as in native.
		FOR_EACH_VEC( TheNavAreas, i )
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
			if ( area->IsInPlayerAAS( character ) )
				area->OnPlayerLeaveAAS( character );
			area->SetPotentiallyVisibleByCharacter( character, false );
			area->SetCompletelyVisibleByCharacter( character, false );
		}
		m_potentiallyVisibleAreas.RemoveAll();
		return;
	}
	if ( !IsAlive() || ( GetTeamNumber() != TEAM_SURVIVOR && GetTeamNumber() != TEAM_L4D1_SURVIVOR ) )
		return;
	const int oldCount = m_potentiallyVisibleAreas.Count();
	TerrorNavArea::BeginAASCollection();
	CPlayerAASCollector collect;
	static ConVarRef spawnRange( "z_spawn_range" );
	const float range = TheDirector->GetDirectorOptionFloat( "ZombieSpawnRange", spawnRange.GetFloat() );
	CollectSurroundingTerrorAreas( newArea, newArea->GetCenter(), collect, range, 1, TEAM_ANY );
	// Commit new entries before removing the saved OLD prefix, S31A320.
	FOR_EACH_VEC( collect.m_areas, i )
	{
		TerrorNavArea *area = collect.m_areas[i];
		if ( !area->IsInPlayerAAS( character ) )
		{
			area->OnPlayerEnterAAS( character );
			m_potentiallyVisibleAreas.AddToTail( area );
		}
	}
	for ( int i = oldCount - 1; i >= 0; --i )
	{
		TerrorNavArea *area = m_potentiallyVisibleAreas[i];
		if ( !area->IsAASCollectionMarked() )
		{
			m_potentiallyVisibleAreas.FastRemove( i );
			area->OnPlayerLeaveAAS( character );
		}
	}
	if ( oldArea )
	{
		CClearPlayerAreaVisibility clear( character );
		oldArea->ForAllPotentiallyVisibleAreas( clear );
	}
	CMarkPlayerAreaVisibility mark( this, newArea );
	newArea->ForAllPotentiallyVisibleAreas( mark );
	m_newlyVisibleArea = mark.m_exposedArea;
	if ( mark.m_exposedArea > music_large_area_reveal_threshold.GetFloat() )
		GetMusic()->OnLargeAreaRevealed();
}

// S3359D0. The callback is invoked by the actual Source last-known-area
// owner. Lazy work runs before the scheduled PostThink gate, S338740.
void CTerrorPlayer::OnNavAreaChanged( CNavArea *newArea, CNavArea *oldArea )
{
	m_navAreaChangedInterval.Start();
	m_newlyVisibleArea = 0.0f;
	if ( newArea && IsAlive() && GetTeamNumber() == TEAM_SURVIVOR &&
		!m_hasUsedFlashlightOutsideSafeArea && IsEffectActive( EF_DIMLIGHT ) )
	{
		TerrorNavArea *area = ToTerrorNavArea( newArea );
		if ( area && !( area->GetSpawnAttributes() & 0x10880 ) )
			m_hasUsedFlashlightOutsideSafeArea = true;
	}
	if ( survivor_lazy_active_set.GetBool() )
	{
		m_pendingAASArea = newArea;
		if ( !m_previousAASArea )
			m_previousAASArea = oldArea;
	}
	else
		UpdatePotentiallyVisibleAreas( newArea, oldArea );
	TheDirector->OnNavAreaChanged( this, newArea, oldArea );
	static_cast< CTerrorExpresser * >( GetExpresser() )->GetSurvivorSpeech()->OnNavAreaChanged( newArea, oldArea );
	BaseClass::OnNavAreaChanged( newArea, oldArea );
}


// S30C530. These are borrowed NAV pointers, not current-area aliases.
void CTerrorPlayer::OnNavAreaRemoved( CNavArea *removedArea )
{
	BaseClass::OnNavAreaRemoved( removedArea );
	if ( m_pendingAASArea == removedArea || m_previousAASArea == removedArea )
	{
		m_previousAASArea = NULL;
		m_pendingAASArea = NULL;
	}
	if ( m_potentiallyVisibleAreas.FindAndRemove( ToTerrorNavArea( removedArea ) ) &&
		survivor_debug_active_area_set.GetInt() != 0 && removedArea )
		DevMsg( "Removed area %d from %s's active area set\n", removedArea->GetID(), GetPlayerName() );
	if ( m_spawnArea == removedArea )
		m_spawnArea = NULL;
	if ( m_hangStandArea == removedArea )
		m_hangStandArea = NULL;
}

// S:30D890, reached by mesh2EA670; distinct from general30C530 pending-AAS cleanup.
void CTerrorPlayer::OnNavAreaDestroyed( TerrorNavArea *area )
{
	m_potentiallyVisibleAreas.FindAndRemove( area );
}

// S2FC730: the virtual last-known-area owner is distinct from spawn area.
double CTerrorPlayer::GetFlowDistance( int direction ) const
{
	const TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
	if ( !area )
		return -9999.0;
	if ( direction < 0 || direction >= NUM_TERROR_FLOW_DIRECTIONS )
		return 0.0;
	return area->GetFlowDistance( static_cast< TerrorFlowDirection >( direction ) );
}
