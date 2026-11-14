//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/stats/l4d_awards.h"
#include "l4d2/stats/player_reporter.h"
#include "l4d2/nav/terror_nav_area.h"
#include "tier0/memdbgon.h"

// S3036B0: process lifetime counter. Neither level restart nor disconnect resets it.
static int s_nextStatsPlayerID = 0;
int CTerrorPlayer::GetStatsPlayerID()
{
	if ( !m_statsPlayerID ) m_statsPlayerID = ++s_nextStatsPlayerID;
	return m_statsPlayerID;
}

CTerrorPlayer *CTerrorPlayer::GetMVPNominee() const { return m_MVPNominee.Get(); }
bool CTerrorPlayer::HasUsedFlashlightOutsideSafeArea() const { return m_hasUsedFlashlightOutsideSafeArea; }
void CTerrorPlayer::UpdateHighestDamagePounce( int damage )
{
	m_checkpointPZHighestDmgPounce = MAX( m_checkpointPZHighestDmgPounce.Get(), damage );
}
bool CTerrorPlayer::IsInCheckpoint() const
{
	const TerrorNavArea *area = ToTerrorNavArea( GetSpawnArea() );
	return area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) && m_missionIDSpawnedOn == TheDirector->GetMissionID();
}
void CTerrorPlayer::RemoveAward( int award )
{
	if ( (unsigned int)award < (unsigned int)m_checkpointAwardCounts.Count() ) m_checkpointAwardCounts.Set( award, (int)( (unsigned int)m_checkpointAwardCounts[award] - 1u ) );
}
void CTerrorPlayer::RecordPhysicsHit( int survivorCharacter )
{
	if ( static_cast< unsigned int >( survivorCharacter ) <= 3u )
	{
		++m_survivorPhysicsHits[survivorCharacter];
		++m_totalSurvivorPhysicsHits;
	}
}
void CTerrorPlayer::RecordSurvivorLungeShove( int survivorCharacter )
{
	if ( static_cast< unsigned int >( survivorCharacter ) <= 3u )
	{
		++m_survivorLungeShoves[survivorCharacter];
		++m_totalSurvivorLungeShoves;
		++m_checkpointPZPushes;
	}
}
void CTerrorPlayer::RecordSmokerDrag( int survivorCharacter )
{
	if ( static_cast< unsigned int >( survivorCharacter ) <= 3u )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( m_tongueVictim.Get() );
		if ( victim ) ++victim->m_survivorSmokerDrags[survivorCharacter];
		++m_survivorSmokerDrags[survivorCharacter];
		++m_totalSurvivorSmokerDrags;
		++m_checkpointPZPulled;
	}
}
void CTerrorPlayer::RecordSmokerChoke( int survivorCharacter )
{
	if ( static_cast< unsigned int >( survivorCharacter ) <= 3u )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( m_tongueVictim.Get() );
		if ( victim ) ++victim->m_survivorSmokerChokes[survivorCharacter];
		++m_survivorSmokerChokes[survivorCharacter];
		++m_totalSurvivorSmokerChokes;
		++m_checkpointPZHung;
	}
}

// S3224F0; the existing incap callback supplies the actual survivor character.
void CTerrorPlayer::RecordSurvivorIncap( int character )
{
	if ( static_cast< unsigned int >( character ) > 3u ) return;
	m_survivorIncaps[character] = static_cast< int >( static_cast< unsigned int >( m_survivorIncaps[character] ) + 1u );
	m_totalSurvivorIncaps = static_cast< int >( static_cast< unsigned int >( m_totalSurvivorIncaps ) + 1u );
	m_checkpointPZIncaps = static_cast< int >( static_cast< unsigned int >( m_checkpointPZIncaps.Get() ) + 1u );
}

// S322750; the existing Tank-rock hit callback supplies the actual character.
void CTerrorPlayer::RecordTankRockHit( int character )
{
	if ( static_cast< unsigned int >( character ) > 3u ) return;
	m_survivorTankRockHits[character] = static_cast< int >( static_cast< unsigned int >( m_survivorTankRockHits[character] ) + 1u );
	m_totalSurvivorTankRockHits = static_cast< int >( static_cast< unsigned int >( m_totalSurvivorTankRockHits ) + 1u );
	m_checkpointPZTankThrows = static_cast< int >( static_cast< unsigned int >( m_checkpointPZTankThrows.Get() ) + 1u );
}

int CTerrorPlayer::GetZombieKillCount( int zombieClass ) const { return (unsigned int)zombieClass < (unsigned int)m_missionZombieKills.Count() ? m_missionZombieKills[zombieClass] : 0; }
int CTerrorPlayer::GetIncapacitatedZombieKillCount() const { return m_incapacitatedZombieKills; }
void CTerrorPlayer::RecordZombieKilled( int zombieClass )
{
	if ( (unsigned int)zombieClass < (unsigned int)m_checkpointZombieKills.Count() ) m_checkpointZombieKills.Set( zombieClass, (int)( (unsigned int)m_checkpointZombieKills[zombieClass] + 1u ) );
	if ( IsIncapacitated() ) ++m_incapacitatedZombieKills;
}

float CTerrorPlayer::GetPounceElapsedTimeForStats() const
{
	return m_pounceStartTimer.GetStartTime() > 0.0f ? m_pounceStartTimer.GetElapsedTime() : 99999.898f;
}
float CTerrorPlayer::GetTongueElapsedTimeForStats() const
{
	return m_tongueChokeStartTimer.GetStartTime() > 0.0f ? m_tongueChokeStartTimer.GetElapsedTime() : 99999.898f;
}
float CTerrorPlayer::GetBileRemainingTimeForStats() const { return m_itTimer.GetRemainingTime(); }

void CTerrorPlayer::RecordVomitHit( int survivorCharacter )
{
	if ( static_cast< unsigned int >( survivorCharacter ) <= 3u )
	{
		++m_survivorVomitHits[survivorCharacter];
		++m_totalSurvivorVomitHits;
		++m_checkpointPZVomited;
	}
}
void CTerrorPlayer::RecordVomitExplosion( int survivorCharacter )
{
	if ( static_cast< unsigned int >( survivorCharacter ) <= 3u )
	{
		++m_survivorVomitExplosions[survivorCharacter];
		++m_totalSurvivorVomitExplosions;
		++m_checkpointPZBombed;
	}
}

// S30A790 mutates the checkpoint count BEFORE attempting event allocation.
// Award/fault response is independent of whether the game event was allocated.
bool CTerrorPlayer::OnAwardEarned( int award, CBaseEntity *subject )
{
	if ( (unsigned int)award < (unsigned int)m_checkpointAwardCounts.Count() ) m_checkpointAwardCounts.Set( award, (int)( (unsigned int)m_checkpointAwardCounts[award] + 1u ) );
	CTerrorPlayer *subjectPlayer = dynamic_cast< CTerrorPlayer * >( subject );
	IGameEvent *event = gameeventmanager->CreateEvent( "award_earned" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetInt( "entityid", entindex() );
		if ( subject ) event->SetInt( "subjectentid", subject->entindex() );
		event->SetInt( "award", award );
		gameeventmanager->FireEvent( event );
	}
	int response = GetAwardResponseType( award );
	if ( response == 1 || response == 2 )
	{
		char modifiers[256];
		Q_snprintf( modifiers, sizeof( modifiers ), "%s:%s", response == 1 ? "AwardName" : "FaultName", GetAwardResponseName( award ) );
		if ( subjectPlayer )
		{
			Q_strncat( modifiers, ",Subject:", sizeof( modifiers ) );
			Q_strncat( modifiers, ::GetResponseCharacterName( subjectPlayer->GetSurvivorCharacter() ), sizeof( modifiers ) );
		}
		SpeakIfAllowed( response == 1 ? "Award" : "Fault", 1, modifiers );
	}
	if ( subjectPlayer )
	{
		int type = -1;
		switch ( award )
		{
		case 66: case 75: case 76: type = 15; break;
		case 68: type = 16; break;
		case 69: type = 17; break;
		case 67: type = 18; break;
		case 80: type = 19; break;
		case 70: type = 20; break;
		}
		if ( type >= 0 )
		{
			SendPZDamageMessage( type, subjectPlayer, this, NULL );
			return true;
		}
	}
	return false;
}



// S316F90: listeners see player_first_spawn BEFORE mission arrays are cleared.
void CTerrorPlayer::OnFirstSpawnInMission()
{
	IGameEvent *event = gameeventmanager->CreateEvent( "player_first_spawn" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetString( "map_name", STRING( gpGlobals->mapname ) );
		event->SetBool( "isbot", IsBot() );
		gameeventmanager->FireEvent( event );
	}
	for ( int i = 0; i < 149; ++i ) m_missionAwardCounts.Set( i, 0 );
	for ( int i = 0; i < 9; ++i ) m_missionZombieKills.Set( i, 0 );
	if ( !TerrorGameRules()->IsVersusMode() ) ResetCheckpointStatistics();
	if ( !TerrorGameRules()->IsSurvivalMode() )
	{
		m_lastPrimaryWeaponId = 0;
		m_lastSecondaryWeaponId = 1;
	}
	m_lastPrimaryWeaponAmmo = 0;
	m_lastHadElites = false;
	m_hasUsedFlashlightOutsideSafeArea = false;
}

void CTerrorPlayer::ResetCheckpointStatistics()
{
	m_checkpointMedkitsUsed = 0;
	m_checkpointPillsUsed = 0;
	m_checkpointMolotovsUsed = 0;
	m_checkpointPipebombsUsed = 0;
	m_checkpointBoomerBilesUsed = 0;
	m_checkpointAdrenalinesUsed = 0;
	m_checkpointDefibrillatorsUsed = 0;
	m_checkpointDamageTaken = 0;
	m_checkpointReviveOtherCount = 0;
	m_checkpointFirstAidShared = 0;
	m_checkpointIncaps = 0;
	m_checkpointDeaths = 0;
	m_checkpointDamageToTank = 0;
	m_checkpointDamageToWitch = 0;
	m_checkpointHeadshots = 0;
	m_checkpointAccuracy = 0;
	m_checkpointHeadshotAccuracy = 0;
	m_checkpointShotsFired = 0;
	m_checkpointShotsHit = 0;
	m_checkpointMeleeKills = 0;
	m_checkpointPZIncaps = 0;
	m_checkpointPZTankDamage = 0;
	m_checkpointPZHunterDamage = 0;
	m_checkpointPZSmokerDamage = 0;
	m_checkpointPZBoomerDamage = 0;
	m_checkpointPZJockeyDamage = 0;
	m_checkpointPZSpitterDamage = 0;
	m_checkpointPZChargerDamage = 0;
	m_checkpointPZKills = 0;
	m_checkpointPZPounces = 0;
	m_checkpointPZPushes = 0;
	m_checkpointPZTankPunches = 0;
	m_checkpointPZTankThrows = 0;
	m_checkpointPZHung = 0;
	m_checkpointPZPulled = 0;
	m_checkpointPZBombed = 0;
	m_checkpointPZVomited = 0;
	m_checkpointPZHighestDmgPounce = 0;
	m_checkpointPZLongestSmokerGrab = 0;
	m_checkpointPZLongestJockeyRide = 0;
	m_checkpointPZNumChargeVictims = 0;
	for ( int i = 0; i < 149; ++i )
		m_checkpointAwardCounts.Set( i, 0 );
	for ( int i = 0; i < 9; ++i )
		m_checkpointZombieKills.Set( i, 0 );
}

// S30F4B0: persistent reporter is the source of mission totals, not a second
// guessed event counter. Snapshot occurs before the transition save wrappers.
void CTerrorPlayer::UpdateMissionStats()
{
	m_checkpointAccuracy = m_checkpointShotsFired ? 100 * m_checkpointShotsHit / m_checkpointShotsFired : 0;
	PlayerReport_t *report = GetPlayerReporter().GetPlayerReport( this );
	if ( !report ) return;
	m_missionDeaths = report->deaths;
	m_missionMedkitsUsed = report->medkitsUsed;
	m_missionPillsUsed = report->pillsUsed;
	m_missionMolotovsUsed = report->molotovsUsed;
	m_missionPipebombsUsed = report->pipebombsUsed;
	m_missionBoomerBilesUsed = report->boomerBilesUsed;
	m_missionAdrenalinesUsed = report->adrenalineUsed;
	m_missionDefibrillatorsUsed = report->defibrillatorsUsed;
	m_missionMeleeKills = report->meleeKills;
	m_missionIncaps = report->incaps;
	m_missionReviveOtherCount = report->revives;
	m_missionFirstAidShared = report->medkitsShared;
	m_missionDamageTaken = report->DamageTaken();
	m_missionAccuracy = report->Accuracy();
	m_missionHeadshotAccuracy = report->HeadshotAccuracy();
	for ( int i = 0; i < 149; ++i ) m_missionAwardCounts.Set( i, report->awards[i] );
	for ( int i = 0; i < 9; ++i ) m_missionZombieKills.Set( i, report->zombieKills[i] );
}
