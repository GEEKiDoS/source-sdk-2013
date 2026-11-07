//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include <emmintrin.h>
#include <math.h>
#include "l4d2/gamerules/terror_gamerules_versus_console_server_cvars.inc"
#include "tier0/memdbgon.h"

static ConVar vs_survival_bonus( "vs_survival_bonus", "25", 0x00006004, "Bonus score per survivor for completing a chapter" );
static ConVar vs_defib_penalty( "vs_defib_penalty", "25", 0x00006004, "Score penalty for each time a defibrillator is used" );
static ConVar vs_tiebreak_bonus( "vs_tiebreak_bonus", "25", 0x00006004, "Score awarded for winning a versus tiebreaker" );

static int NativeCeilScore( float score )
{
	// Native101CCEE0 is CEIL(float->double), then original SSE2 ftol32.
	return _mm_cvttsd_si32( _mm_set_sd( ceil( (double)score ) ) );
}
static bool IsInCheckpointRegion( TerrorNavArea *area, bool outward )
{
	const CUtlVector< CTerrorCheckpoint * > &checkpoints = TheTerrorNavMesh()->GetCheckpoints();
	FOR_EACH_VEC( checkpoints, i )
	{
		CTerrorCheckpoint *checkpoint = checkpoints[i];
		if ( checkpoint && checkpoint->IsOutward() == outward && checkpoint->GetAreas().Find( area ) != checkpoint->GetAreas().InvalidIndex() ) return true;
	}
	return false;
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
int CTerrorGameRules::CalculateVersusPlayerDistanceScore( CTerrorPlayer *player ) const
{
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR ) return 0;
	int maximum = m_nVersusMaxCompletionScore >> 2;
	if ( maximum <= 0 ) maximum = 0;
	if ( TheDirector->IsFinaleWon() ) return maximum;
	TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
	if ( area )
	{
		if ( TheDirector->IsFinale() && TheDirector->IsRescueVehicleReady() && area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) ) return maximum;
		if ( IsInCheckpointRegion( area, false ) ) return 0;
		if ( IsInCheckpointRegion( area, true ) ) return maximum;
	}
	if ( TheTerrorNavMesh()->GetMaxNonCheckpointFlowDistance() <= 0.0f ) return 0;
	double first = player->GetFlowDistance( 0 );
	// Native's unordered first comparison ALSO re-queries, not >=0 clamping.
	float distance = first < 0.0 ? 0.0f : (float)player->GetFlowDistance( 0 );
	float fraction = distance / TheTerrorNavMesh()->GetMaxNonCheckpointFlowDistance();
	if ( fraction < 0.0f ) fraction = 0.0f;
	else if ( fraction > 1.0f ) fraction = 1.0f; // Actual COMISS branches retain NaN.
	CDirectorVersusMode *versus = TheDirector->GetVersusMode();
	int finale = versus->GetFinaleType();
	int score;
	if ( versus->IsSacrificeActive() && ( finale == 4 || finale == 2 ) )
	{
		float travel = versus->GetFinaleTravelCompletion();
		if ( TheDirector->IsFinale() )
		{
			float maximumFloat = (float)maximum;
			int travelScore = NativeCeilScore( maximumFloat * travel );
			float remaining = 1.0f - travel;
			int extra = 0;
			if ( finale == 2 )
			{
				int stages = TheDirector->GetScenario()->GetCustomStageCount() + 2;
				int stage = TheDirector->GetScenario()->GetCustomStage();
				if ( stages > 0 && stage > 0 )
				{
					float piece = 1.0f / (float)stages;
					float base = (float)( stage - 1 ) * piece;
					int step = _mm_cvttsd_si32( _mm_set_sd( TheDirector->GetScenario()->GetStageElapsedTime() * (double)0.2f ) );
					float progress = (float)step * 0.1f;
					if ( TheDirector->GetScenario()->GetFinaleStage() == 9 ) progress = TheDirector->GetScenario()->GetCustomScriptedStageProgress( progress );
					float within = progress * piece;
					if ( 0.0f > within ) within = 0.0f;
					else if ( within > piece ) within = piece; // Native2907E0 retains unordered.
					float value = (float)( ( (double)within + (double)base ) * (double)remaining * (double)maximumFloat );
					extra = NativeCeilScore( value );
				}
			}
			else if ( m_nScavengeItemsGoal > 0 )
			{
				float value = (float)GetScavengeTeamScore( TEAM_SURVIVOR ) / (float)(int)m_nScavengeItemsGoal;
				value = value * remaining; value = value * maximumFloat;
				extra = NativeCeilScore( value );
			}
			score = (int)( (unsigned int)travelScore + (unsigned int)extra );
		}
		else { float value = (float)maximum * fraction; value = value * travel; score = NativeCeilScore( value ); }
	}
	else score = NativeCeilScore( (float)maximum * fraction );
	if ( score < 0 ) return 0;
	return score > maximum ? maximum : score;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
int CTerrorGameRules::UpdateVersusDistanceCache( bool markDeath )
{
	int sum = 0;
	for ( int character = 0; character < 4; ++character )
	{
		CTerrorPlayer *player = FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( MapTerrorSurvivorCharacter( character ) ) );
		if ( !player || !player->IsAlive() )
		{
			int value = m_iVersusDistancePerSurvivor[character + 4 * ( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1 )];
			m_iVersusSurvivorDeathDistance.Set( character + 4 * ( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1 ), value );
		}
		else
		{
			int score = CalculateVersusPlayerDistanceScore( player );
			int slot = character + 4 * ( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1 );
			if ( score > m_iVersusDistancePerSurvivor[slot] ) m_iVersusDistancePerSurvivor.Set( character + 4 * ( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1 ), score );
			slot = character + 4 * ( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1 );
			if ( m_iVersusSurvivorDeathDistance[slot] != -1 ) m_iVersusSurvivorDeathDistance.Set( character + 4 * ( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1 ), -1 );
		}
		if ( markDeath && ( TheDirector->IsFinale() ? !m_finaleEscapees[character] : GetVersusSurvivalMultiplier( TEAM_SURVIVOR ) == 0 ) )
		{
			int value = m_iVersusDistancePerSurvivor[character + 4 * ( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1 )];
			m_iVersusSurvivorDeathDistance.Set( character + 4 * ( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1 ), value );
		}
		sum = (int)( (unsigned int)sum + (unsigned int)m_iVersusDistancePerSurvivor[character + 4 * ( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1 )] );
	}
	return sum;
}
void CTerrorGameRules::CacheVersusScores( bool markDeath )
{
	if ( !TheDirector->GetVersusMode()->IsFinaleActive() ) return;
	int distance = UpdateVersusDistanceCache( markDeath );
	if ( distance > m_nCachedVersusDistanceScore ) m_nCachedVersusDistanceScore = distance;
	unsigned int bonus = (unsigned int)vs_survival_bonus.GetInt() * (unsigned int)GetVersusSurvivalMultiplier( TEAM_SURVIVOR );
	unsigned int penalty = (unsigned int)vs_defib_penalty.GetInt() * (unsigned int)GetVersusDefibsUsed( TEAM_SURVIVOR );
	int score = (int)( (unsigned int)m_nCachedVersusDistanceScore - penalty + bonus );
	if ( score < 0 ) score = 0;
	int index = GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1;
	m_iSurvivorScore.Set( index, score ); m_iSurvivorScore.Set( index == 0 ? 1 : 0, 0 );
}
void CTerrorGameRules::IncrementVersusDefibsUsed()
{
	// S2DC270 increments the current survivor role's existing counter.
	const int index = GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1;
	m_iVersusDefibsUsed.Set( index, (int)( (unsigned int)m_iVersusDefibsUsed[index] + 1u ) );
}
void CTerrorGameRules::BreakVersusTie()
{
	if ( m_iChapterDamage[1] > m_iChapterDamage[0] ) m_iSurvivorScore.Set( 1, (int)( (unsigned int)m_iSurvivorScore[1] + (unsigned int)vs_tiebreak_bonus.GetInt() ) );
	else if ( m_iChapterDamage[1] < m_iChapterDamage[0] ) m_iSurvivorScore.Set( 0, (int)( (unsigned int)m_iSurvivorScore[0] + (unsigned int)vs_tiebreak_bonus.GetInt() ) );
}
void CTerrorGameRules::RecordFinaleEscapees( const CUtlVector< CTerrorPlayer * > &players )
{
	Q_memset( m_finaleEscapees, 0, sizeof( m_finaleEscapees ) );
	FOR_EACH_VEC( players, i )
	{
		CTerrorPlayer *player = players[i];
		if ( player && player->IsAlive() && !player->IsIncapacitated() && (unsigned int)player->GetSurvivorCharacter() <= 3 ) m_finaleEscapees[player->GetSurvivorCharacter()] = true;
	}
}
void CTerrorGameRules::UpdateVersusSurvivalMultiplier( bool completed )
{
	int count = 0;
	if ( completed )
	{
		if ( TheDirector->IsFinale() ) for ( int i = 0; i < 4; ++i ) { if ( m_finaleEscapees[i] ) ++count; }
		else for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
			if ( player->IsIncapacitated() )
			{
				TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
				if ( !area || !area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) continue;
				CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
				if ( checkpoint && checkpoint->GetAreas().Find( area ) != checkpoint->GetAreas().InvalidIndex() ) continue;
			}
			++count;
		}
	}
	m_iVersusSurvivalMultiplier.Set( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1, count );
}
