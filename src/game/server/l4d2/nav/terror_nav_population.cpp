//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/director/zombie_spawn.h"

#include "tier0/memdbgon.h"

extern ConVar z_background_limit;
extern ConVar z_debug;
ConVar director_output_population_visit( "director_output_population_visit", "0", 0x00004004 );
CUtlVector< TerrorNavArea * > TerrorNavMesh::m_wanderingPopulationAreas;
int TerrorNavMesh::s_nextWanderingCharacter = 0;

void TerrorNavMesh::QueueWanderingPopulationArea( TerrorNavArea *area )
{
	m_wanderingPopulationAreas.AddToTail( area );
}

// S:2E5DC0 clears the entire pending queue before the existing area owner destroys an area.
void TerrorNavMesh::DestroyArea( CNavArea *area ) const
{
	m_wanderingPopulationAreas.RemoveAll();
	BaseClass::DestroyArea( area );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
Vector TerrorNavArea::GetRandomSpawnPoint() const
{
	Extent extent;
	GetExtent( &extent );
	const float width = extent.hi.x - extent.lo.x;
	const float height = extent.hi.y - extent.lo.y;
	Vector position;
	if ( width < 50.0f || height < 50.0f )
	{
		position = GetCenter();
		position.z += 10.0f;
	}
	else
	{
		position.x = (float)( (double)RandomFloat( 0.0f, width - 50.0f ) + ( (double)extent.lo.x + 25.0 ) );
		position.y = (float)( (double)RandomFloat( 0.0f, height - 50.0f ) + ( (double)extent.lo.y + 25.0 ) );
		position.z = (float)( GetZ( position.x, position.y ) + 10.0 );
	}
	return position;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

bool TerrorNavArea::IsValidForWanderingPopulation() const
{
	return !( GetAttributes() & 3 ) && !IsBlocked( 3, false ) &&
		!( GetSpawnAttributes() & 0x10882 ) && !( GetFlowDistanceFromStart() < 0.0f ) &&
		!( GetAttributes() & 0x30000 );
}

bool TerrorNavArea::IsAvailableForSpawn() const
{
	if ( GetSpawnAttributes() & 0x880 )
		return false;
	if ( GetSpawnAttributes() & 0x400 )
		return true;
	return m_clearedCharacters == 0;
}

void TerrorNavArea::PopulateWanderers()
{
	if ( director_output_population_visit.GetBool() )
	{
		static int visitFrame = 0;
		static int visitCount = 0;
		if ( visitFrame == gpGlobals->framecount )
			++visitCount;
		else
		{
			if ( visitCount )
				Msg( "MWP %d: %d\n", visitFrame, visitCount );
			visitCount = 1;
			visitFrame = gpGlobals->framecount;
		}
	}
	if ( !TheDirector->ShouldSpawnWanderers() )
	{
		m_wanderingPopulation = 0;
		return;
	}
	if ( m_wanderingPopulation <= 0 || TheDirector->IsBackgroundMap() || TerrorGameRules()->IsSurvivalMode() )
		return;
	if ( TheDirector->IsFinale() || !IsAvailableForSpawn() || IsPotentiallyVisibleToAnyCharacter() )
		return;
	int limit = TheDirector->GetCommonLimit();
	while ( m_wanderingPopulation != 0 && GetCommonInfectedCount() < limit )
	{
		Vector position = GetRandomSpawnPoint();
		if ( IsTerrorSpawnPointVisibleToTeam( position + Vector( 0, 0, 1 ), 2, 2, 0.0f, this ) ||
			IsTerrorSpawnPointVisibleToTeam( position + Vector( 0, 0, 35.5f ), 2, 2, 0.0f, this ) ||
			IsTerrorSpawnPointVisibleToTeam( position + Vector( 0, 0, 56.8f ), 2, 2, 0.0f, this ) )
		{
			if ( !TheDirector->IsSpawnPositionInFog( position ) )
			{
				MarkAsCleared( NULL );
				return;
			}
		}
		if ( !TheZombieManager->SpawnCommonZombie( this, position, ZombieManager::WANDERER ) )
		{
			CUtlVector< TerrorNavArea * > adjacent;
			for ( int direction = 0; direction < NUM_DIRECTIONS; ++direction )
			{
				const NavConnectVector *connections = GetAdjacentAreas( static_cast< NavDirType >( direction ) );
				FOR_EACH_VEC( ( *connections ), i )
				{
					TerrorNavArea *other = ToTerrorNavArea( ( *connections )[i].area );
					if ( other->IsValidForWanderingPopulation() )
						adjacent.AddToTail( other );
				}
			}
			if ( adjacent.Count() )
			{
				TerrorNavArea *other = adjacent[RandomInt( 0, adjacent.Count() - 1 )];
				int population = other->GetWanderingPopulation() + 1;
				other->SetWanderingPopulation( population < 0 ? 0 : population );
				--m_wanderingPopulation;
			}
			return;
		}
		if ( --m_wanderingPopulation == 0 )
			return;
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void TerrorNavMesh::MaintainWanderingPopulation( bool force )
{
	if ( TheDirector->IsFinale() || !TheDirector->ShouldSpawnWanderers() || TerrorGameRules()->IsSurvivalMode() )
	{
		m_wanderingPopulationAreas.RemoveAll();
		return;
	}
	int limit = TheDirector->GetCommonLimit();
	if ( force || ( GetCommonInfectedCount() < limit &&
		( ( m_wanderingPopulationAreas.Count() && gpGlobals->curtime >= m_wanderingSpawnTimer.m_timestamp ) ||
		gpGlobals->curtime >= m_wanderingPopulationTimer.m_timestamp ) ) )
	{
		const CUtlVector< TerrorNavArea * > *areas = NULL;
		if ( force || gpGlobals->curtime >= m_wanderingPopulationTimer.m_timestamp )
		{
			// S2F0AB0/S2E36A0 keeps four raw-character slots. Main's standing
			// bounds rule omits only an out-of-array store, without remapping IDs.
			const CUtlVector< TerrorNavArea * > *byCharacter[4] = { NULL, NULL, NULL, NULL };
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
				if ( player && player->edict() && player->entindex() != 0 &&
					player->IsConnected() && player->GetTeamNumber() == TEAM_SURVIVOR )
				{
					const bool alive = player->IsAlive();
					const int character = player->GetSurvivorCharacter();
					if ( (unsigned int)character < ARRAYSIZE( byCharacter ) )
						byCharacter[character] = alive ? &player->GetPotentiallyVisibleAreas() : NULL;
				}
			}
			for ( int i = 0; i < 4; ++i )
			{
				int character = s_nextWanderingCharacter;
				s_nextWanderingCharacter = ( s_nextWanderingCharacter + 1 ) % 4;
				if ( byCharacter[character] )
				{
					areas = byCharacter[character];
					break;
				}
			}
		}
		else if ( m_wanderingPopulationAreas.Count() )
			areas = &m_wanderingPopulationAreas;
		m_wanderingSpawnTimer.Start( gpGlobals->interval_per_tick * 3.0f );
		m_wanderingPopulationTimer.Start( 1.0f );
		if ( areas )
		{
			TerrorNavArea::BeginAASCollection();
			int attempts = 0;
			FOR_EACH_VEC( ( *areas ), i )
			{
				if ( GetCommonInfectedCount() >= limit || attempts >= 100 )
					break;
				TerrorNavArea *area = ( *areas )[i];
				if ( area->GetWanderingPopulation() != 0 && !area->IsAASCollectionMarked() &&
					!area->IsPotentiallyVisibleToAnyCharacter() && area->IsInAnyPlayerAAS() )
				{
					area->PopulateWanderers();
					++attempts;
					area->MarkAASCollection();
				}
			}
			if ( areas == &m_wanderingPopulationAreas )
				m_wanderingPopulationAreas.RemoveAll();
		}
	}
	else if ( m_wanderingSpawnTimer.m_timestamp > 0.0f &&
		(double)m_wanderingSpawnTimer.m_timestamp - (double)gpGlobals->curtime < -20.0 )
		m_wanderingPopulationAreas.RemoveAll();
	if ( TheDirector->IsBackgroundMap() )
	{
		int deficit = z_background_limit.GetInt() - GetCommonInfectedCount();
		if ( deficit > 0 )
			PopulateBackground( deficit, false );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void TerrorNavMesh::PopulateBackground( int count, bool restricted )
{
	float totalArea = 0.0f;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( ( ( area->GetSpawnAttributes() & 0x802 ) != 0 ) == restricted )
			totalArea += area->GetSizeY() * area->GetSizeX();
	}
	while ( count != 0 )
	{
		if ( GetCommonInfectedCount() >= z_background_limit.GetInt() )
			break;
		float remaining = RandomFloat( 0.0f, totalArea );
		TerrorNavArea *selected = NULL;
		FOR_EACH_VEC( TheNavAreas, i )
		{
			selected = ToTerrorNavArea( TheNavAreas[i] );
			if ( ( ( selected->GetSpawnAttributes() & 0x802 ) != 0 ) == restricted )
			{
				remaining -= selected->GetSizeY() * selected->GetSizeX();
				if ( remaining <= 0.0f )
					break;
			}
		}
		if ( remaining > 0.0f || !selected )
			break;
		// Native failure does not decrement count or impose a retry ceiling.
		if ( TheZombieManager->SpawnCommonZombie( selected, selected->GetRandomSpawnPoint() ) )
			--count;
	}
}
