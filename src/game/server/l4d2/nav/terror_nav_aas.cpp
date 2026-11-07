//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_search.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/zombiemanager.h"
#include "ndebugoverlay.h"
#include "l4d2/nav/terror_nav_aas_console_server_cvars.inc"
#include "tier0/memdbgon.h"

ConVar z_dont_clear( "z_dont_clear", "0", 0x00004004 );
ConVar z_boundary_clear_type( "z_boundary_clear_type", "1", 0x00004004, "0 = floodfill, 1 = new creep" );
ConVar z_boundary_spread_speed( "z_boundary_spread_speed", "200", 0x00004004 );
ConVar z_boundary_max_range( "z_boundary_max_range", "5000", 0x00004004 );
ConVar z_show_clear( "z_show_clear", "0", 0x00004004 );
extern ConVar nav_edit;

unsigned int TerrorNavArea::s_aasCollectionMarker = 1;

void TerrorNavArea::OnRoundRestart()
{
	BaseClass::OnRoundRestart();
	m_aasCollectionMarker = 0;
	m_playerAASMask = 0;
	m_potentiallyVisibleCharacters = 0;
	m_completelyVisibleCharacters = 0;
	m_clearedCharacters = 0;
	m_clearedTimer.Invalidate();
	m_unclearedTimer.Start();
}

void TerrorNavArea::BeginAASCollection()
{
	++s_aasCollectionMarker;
}

void TerrorNavArea::ResetAASCollection()
{
	s_aasCollectionMarker = 1;
}

bool TerrorNavArea::IsAASCollectionMarked() const
{
	return m_aasCollectionMarker == s_aasCollectionMarker;
}

void TerrorNavArea::MarkAASCollection()
{
	m_aasCollectionMarker = s_aasCollectionMarker;
}

bool TerrorNavArea::IsInPlayerAAS( SurvivorCharacterType character ) const
{
	if ( character == SurvivorCharacter_Unknown ) return m_playerAASMask != 0;
	return ( m_playerAASMask & ( 1u << character ) ) != 0;
}

void TerrorNavArea::OnPlayerEnterAAS( SurvivorCharacterType character )
{
	if ( m_playerAASMask == 0 )
	{
		if ( m_wanderingPopulation != 0 ) TheTerrorNavMesh()->QueueWanderingPopulationArea( this );
		if ( !HasSpawnAttributes( 0x10882 ) && HasSpawnAttributes( TERROR_NAV_THREAT ) && TheDirector->SpawnThreat( this ) )
			RemoveSpawnAttributes( TERROR_NAV_THREAT );
	}
	m_playerAASMask |= 1u << character;
}

void TerrorNavArea::OnPlayerLeaveAAS( SurvivorCharacterType character )
{
	m_playerAASMask &= ~( 1u << character );
	if ( m_playerAASMask == 0 && TheZombieManager ) TheZombieManager->OnAreaLeftActiveSet( this );
}

void TerrorNavArea::SetPotentiallyVisibleByCharacter( SurvivorCharacterType character, bool visible )
{
	if ( visible ) m_potentiallyVisibleCharacters |= 1u << character;
	else m_potentiallyVisibleCharacters &= ~( 1u << character );
}

void TerrorNavArea::SetCompletelyVisibleByCharacter( SurvivorCharacterType character, bool visible )
{
	if ( visible ) m_completelyVisibleCharacters |= 1u << character;
	else m_completelyVisibleCharacters &= ~( 1u << character );
}

bool TerrorNavArea::IsPotentiallyVisibleByCharacter( SurvivorCharacterType character ) const
{
	return ( m_potentiallyVisibleCharacters & ( 1u << character ) ) != 0;
}

bool TerrorNavArea::IsCompletelyVisibleByCharacter( SurvivorCharacterType character ) const
{
	return ( m_completelyVisibleCharacters & ( 1u << character ) ) != 0;
}

bool TerrorNavArea::IsCleared( const CTerrorPlayer *player ) const
{
	if ( HasSpawnAttributes( TERROR_NAV_PLAYER_START | TERROR_NAV_CHECKPOINT ) ) return true;
	if ( HasSpawnAttributes( TERROR_NAV_NOT_CLEARABLE ) ) return false;
	if ( !player ) return m_clearedCharacters != 0;
	if ( player->GetTeamNumber() != TEAM_SURVIVOR ) return false;
	unsigned int character = player->GetSurvivorCharacter();
	return character < 4 && ( m_clearedCharacters & ( 1u << character ) ) != 0;
}

void TerrorNavArea::MarkAsCleared( CTerrorPlayer *player )
{
	if ( TheDirector->GetDirectorOptionInt( "ZombieDontClear", z_dont_clear.GetInt() ) ) return;
	bool wasCleared = IsCleared();
	if ( player )
	{
		if ( player->GetTeamNumber() == TEAM_SURVIVOR )
		{
			unsigned int character = player->GetSurvivorCharacter();
			if ( character < 4 )
			{
				m_clearedCharacters |= 1u << character;
				m_clearedTimer.Start();
				m_wanderingPopulation = 0;
			}
		}
	}
	else
	{
		m_clearedCharacters = 15;
		m_clearedTimer.Start();
		m_wanderingPopulation = 0;
	}
	m_unclearedTimer.Invalidate();
	if ( wasCleared ) TheTerrorNavMesh()->OnAreaCleared( this );
}

void TerrorNavArea::MarkAsUncleared()
{
	m_unclearedTimer.Start();
	m_clearedTimer.Invalidate();
	m_clearedCharacters = 0;
}

void TerrorNavMesh::InitializeBoundaryDistances()
{
	m_boundarySeeds.RemoveAll();
	m_boundarySpreadIndex = 0;
	m_boundarySpreadAreas.RemoveAll();
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		area->m_boundaryDistance = -1.0f;
		if ( area->HasSpawnAttributes( TERROR_NAV_NOT_CLEARABLE ) )
		{
			m_boundarySeeds.AddToTail( area );
			m_boundarySpreadAreas.AddToTail( area );
		}
	}
	struct DistanceCollector
	{
		bool operator()( TerrorNavArea *area )
		{
			if ( area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) return false;
			if ( area->m_boundaryDistance > area->GetCostSoFar() || area->m_boundaryDistance < 0.0f )
				area->m_boundaryDistance = area->GetCostSoFar();
			return true;
		}
	} collector;
	FOR_EACH_VEC( m_boundarySeeds, i )
	{
		TerrorNavArea *seed = m_boundarySeeds[i];
		CollectSurroundingTerrorAreas( seed, seed->GetCenter(), collector, z_boundary_max_range.GetFloat() );
	}
}

void TerrorNavMesh::OnAreaCleared( TerrorNavArea *area )
{
	if ( !z_boundary_clear_type.GetBool() || !( area->m_boundaryDistance > 0.0f ) || area->HasSpawnAttributes( TERROR_NAV_NOT_CLEARABLE ) ) return;
	m_boundarySpreadAreas.FindAndFastRemove( area );
	if ( z_show_clear.GetBool() ) area->DrawFilled( 100, 255, 0, 255, 1.0f, true );
	for ( int direction = 0; direction < NUM_DIRECTIONS; ++direction )
	{
		const NavConnectVector *connections = area->GetAdjacentAreas( (NavDirType)direction );
		FOR_EACH_VEC( (*connections), i )
		{
			TerrorNavArea *next = ToTerrorNavArea( (*connections)[i].area );
			if ( next->IsCleared() || m_boundarySpreadAreas.Find( next ) != m_boundarySpreadAreas.InvalidIndex() ) continue;
			m_boundarySpreadAreas.AddToTail( next );
			if ( z_show_clear.GetBool() ) next->DrawFilled( 0, 0, 255, 128, 1.0f, true );
		}
	}
}

void TerrorNavMesh::UpdateBoundarySpread()
{
	if ( nav_edit.GetBool() )
	{
		m_boundarySeeds.RemoveAll();
		m_boundarySpreadAreas.RemoveAll();
		return;
	}
	if ( !m_boundarySpreadTimer.IsElapsed() || m_boundarySeeds.Count() == 0 ) return;
	if ( z_boundary_clear_type.GetBool() )
	{
		if ( z_show_clear.GetBool() ) engine->Con_NPrintf( 10, "%d cleared areas", m_boundarySpreadAreas.Count() );
		for ( int index = m_boundarySpreadAreas.Count() - 1; index >= 0; --index )
		{
			TerrorNavArea *area = m_boundarySpreadAreas[index];
			bool adjacentCleared = false;
			if ( z_show_clear.GetBool() ) area->DrawFilled( 0, 255, 0, 100, 0.5f, true );
			for ( int direction = 0; direction < NUM_DIRECTIONS; ++direction )
			{
				const NavConnectVector *connections = area->GetAdjacentAreas( (NavDirType)direction );
				FOR_EACH_VEC( (*connections), i )
				{
					TerrorNavArea *next = ToTerrorNavArea( (*connections)[i].area );
					if ( !next->IsCleared() ) continue;
					adjacentCleared = true;
					if ( !next->m_clearedCharacters || next->m_potentiallyVisibleCharacters || !( next->m_clearedTimer.GetElapsedTime() >= 2.0f ) ||
						!( next->m_boundaryDistance >= area->m_boundaryDistance ) ) continue;
					const Vector &from = area->GetCenter();
					const Vector &to = next->GetCenter();
					float x = to.x - from.x, y = to.y - from.y, z = to.z - from.z;
					float travel = sqrtf( ( z * z + y * y ) + x * x ) / z_boundary_spread_speed.GetFloat();
					if ( next->m_unclearedTimer.GetElapsedTime() > travel && area->m_unclearedTimer.GetElapsedTime() > travel )
					{
						next->MarkAsUncleared();
						m_boundarySpreadAreas.AddToTail( next );
						if ( z_show_clear.GetBool() ) next->DrawFilled( 255, 100, 0, 255, 5.0f, true );
					}
				}
			}
			if ( !adjacentCleared && !area->HasSpawnAttributes( TERROR_NAV_NOT_CLEARABLE ) )
			{
				if ( z_show_clear.GetBool() ) area->DrawFilled( 0, 100, 255, 255, 0.5f, true );
				m_boundarySpreadAreas.FastRemove( index );
			}
		}
	}
	else
	{
		m_boundarySpreadTimer.Start( 2.0f / m_boundarySeeds.Count() );
		TerrorNavArea *seed = m_boundarySeeds[m_boundarySpreadIndex];
		if ( z_show_clear.GetBool() ) seed->DrawFilled( 255, 0, 255, 255, 1.0f, true );
		struct SpreadCollector
		{
			TerrorNavArea *seed;
			bool operator()( TerrorNavArea *area )
			{
				if ( !area->IsCleared() ) return true;
				float travel = area->GetCostSoFar() / z_boundary_spread_speed.GetFloat();
				float elapsed = area->m_clearedCharacters && !area->m_potentiallyVisibleCharacters ? area->m_clearedTimer.GetElapsedTime() : 0.0f;
				if ( !( elapsed > travel ) ) return false;
				area->MarkAsUncleared();
				if ( z_show_clear.GetBool() )
				{
					area->DrawFilled( 255, 100, 0, 255, 1.0f, true );
					NDebugOverlay::Line( seed->GetCenter(), area->GetCenter(), 255, 255, 0, true, 1.0f );
				}
				return true;
			}
		} collector = { seed };
		CollectSurroundingTerrorAreas( seed, seed->GetCenter(), collector, z_boundary_max_range.GetFloat() );
		if ( ++m_boundarySpreadIndex >= m_boundarySeeds.Count() ) m_boundarySpreadIndex = 0;
	}
}

void TerrorNavMesh::Update()
{
	BaseClass::Update();
	UpdateBoundarySpread();
	UpdateFlowRecomputeTimer();
	UpdateDebugSelectedArea();
	UpdateBreakableNavAttributes();
}
