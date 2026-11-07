//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "SharedFunctorUtils.h"
#include "functorutils.h"
#include "team.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

static ConVar survivor_fog_vocalize_percent( "survivor_fog_vocalize_percent", ".23", 0x00004004,
	"The percent of the fog end distance that stops vocalizations." );

class CResponseSubjectScan : public TargetScan< CTerrorPlayer >
{
public:
	CResponseSubjectScan( CTerrorPlayer *player, float range ) : TargetScan< CTerrorPlayer >( player, TEAM_ANY, 0.2f, range, 50.0f, false ) {}
	bool operator()( CBasePlayer *player ) { return TargetScan< CTerrorPlayer >::operator()( static_cast< CTerrorPlayer * >( player ) ); }
protected:
	virtual bool IsPotentialTarget( CTerrorPlayer *player ) const OVERRIDE { return !player->IsGhost(); }
};

void CTerrorPlayer::AppendSubjectCriteria( AI_CriteriaSet &criteria )
{
	if ( criteria.FindCriterionIndex( "Subject" ) >= 0 )
		return;
	// S344E20 uses actual NAV/trigger/world fog, not a weapon's bullet range.
	fogparams_t fog;
	GetFogParams( &fog );
	float range = fog.enable ? survivor_fog_vocalize_percent.GetFloat() * fog.end : 8192.0f;
	CResponseSubjectScan scan( this, range );
	ForEachPlayer( scan );
	CTerrorPlayer *subject = scan.GetTarget();
	if ( !subject )
	{
		criteria.AppendCriteria( "Subject", "None" );
		return;
	}
	bool survivor = subject->GetTeamNumber() == TEAM_SURVIVOR || subject->GetTeamNumber() == TEAM_L4D1_SURVIVOR;
	criteria.AppendCriteria( "Subject", survivor ? subject->GetResponseCharacterName() : GetZombieClassName( subject->GetZombieClass() ) );
	if ( survivor && ( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
	{
		m_vocalizationSubject = subject;
		m_vocalizationSubjectTimer.Start( 3.0f );
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void CTerrorPlayer::AppendSubjectDistanceCriteria( AI_CriteriaSet &criteria )
{
	int index = criteria.FindCriterionIndex( "Subject" );
	if ( index < 0 || !criteria.GetValue( index )[0] || !Q_stricmp( criteria.GetValue( index ), "None" ) )
		return;
	CBaseEntity *subject = ResponseQueueManager().FindSpeechTarget( criteria.GetValue( index ) );
	if ( subject )
	{
		const Vector delta = GetAbsOrigin() - subject->GetAbsOrigin();
		const float distanceSqr = ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x;
		criteria.AppendCriteria( "dist_from_subject", CFmtStr( "%f", sqrt( static_cast< double >( distanceSqr ) ) ) );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

const char *GetResponseCampaignName()
{
	const char *map = STRING( gpGlobals->mapname );
	if ( !map[0] || !map[1] )
		return NULL;
	const char *campaign = "unknown";
	switch ( map[1] )
	{
	case '1':
		switch ( map[2] )
		{
		case '0': campaign = "l4d2_10"; break;
		case '1': campaign = "l4d2_11"; break;
		case '2': campaign = "l4d2_12"; break;
		case '3': campaign = "l4d2_13"; break;
		case '4': campaign = "l4d2_14"; break;
		default: campaign = "l4d2_1"; break;
		}
		break;
	case '2': campaign = "l4d2_2"; break;
	case '3': campaign = "l4d2_3"; break;
	case '4': campaign = "l4d2_4"; break;
	case '5': campaign = "l4d2_5"; break;
	case '6': campaign = "l4d2_6"; break;
	case '7': campaign = "l4d2_7"; break;
	case '8': campaign = "l4d2_8"; break;
	case '9': campaign = "l4d2_9"; break;
	}
	return campaign;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void CTerrorPlayer::ModifyOrAppendCriteria( AI_CriteriaSet &criteria )
{
	criteria.AppendCriteria( "Team", GetTeam() ? GetTeam()->GetName() : "" );
	const bool survivor = GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR;
	if ( GetTeamNumber() == TEAM_INFECTED )
	{
		criteria.AppendCriteria( "Who", GetZombieClassName( GetZombieClass() ) );
		criteria.AppendCriteria( "InfectedState", m_zombieState == 1 ? "Alert" : m_zombieState == 2 ? "Engaged" : "Normal" );
	}
	else if ( survivor )
	{
		criteria.AppendCriteria( "Coughing", !( gpGlobals->curtime >= m_coughTimer.m_timestamp ) ? "1" : "0" );
		criteria.AppendCriteria( "OnFire", IsOnFire() ? "1" : "0" );
		criteria.AppendCriteria( "Incapacitated", IsIncapacitated() && !IsHangingFromLedge() ? "1" : "0" );
		criteria.AppendCriteria( "HangingFromLedge", IsHangingFromLedge() ? "1" : "0" );
		criteria.AppendCriteria( "PounceVictim", m_pounceAttacker.Get() ? "1" : "0" );
		criteria.AppendCriteria( "HangingFromTongue", m_isHangingFromTongue ? "1" : "0" );
		criteria.AppendCriteria( "Sneaking", IsSneaking() ? "1" : "0" );
		CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( GetActiveWeapon() );
		if ( weapon )
			criteria.AppendCriteria( "ActiveWeapon", GetTerrorWeaponInfo( weapon->GetWeaponID() )->m_szResponseRulesName );

		TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
		bool battlefield = area && area->HasSpawnAttributes( TERROR_NAV_BATTLEFIELD );
		bool checkpoint = false, start = false;
		if ( area && !battlefield )
		{
			if ( area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
			{
				checkpoint = TheTerrorNavMesh()->GetMaxNonCheckpointFlowDistance() <= area->GetFlowDistance();
				start = !checkpoint;
			}
			else
				start = area->HasSpawnAttributes( TERROR_NAV_PLAYER_START ) || area->GetFlowDistance() == 0.0f;
		}
		criteria.AppendCriteria( "InBattlefield", battlefield ? "1" : "0" );
		criteria.AppendCriteria( "InCheckpoint", checkpoint ? "1" : "0" );
		criteria.AppendCriteria( "InStartArea", start ? "1" : "0" );
		criteria.AppendCriteria( "InSafeSpot", checkpoint || start ? "1" : "0" );
		criteria.AppendCriteria( "InRescueVehicle", TheDirector->IsFinale() && TheDirector->IsRescueVehicleReady() && area && area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) ? "1" : "0" );
		criteria.AppendCriteria( "Who", GetResponseCharacterName() );
		AppendSubjectCriteria( criteria );
		criteria.AppendCriteria( "Intensity", CFmtStr( "%.2f", GetIntensity() ) );
		criteria.AppendCriteria( "TimeAveragedIntensity", CFmtStr( "%.2f", GetTimeAveragedIntensity() ) );
		for ( int zombie = 0; zombie < 9; ++zombie )
			criteria.AppendCriteria( CFmtStr( "%ssKilled", GetZombieClassName( zombie ) ), CFmtStr( "%d", GetZombieKillCount( zombie ) ) );
		criteria.AppendCriteria( "ZombiesKilledWhileIncapacitated", CFmtStr( "%d", GetIncapacitatedZombieKillCount() ) );
		AppendTeamSpeechCounts( criteria );
		criteria.AppendCriteria( "CurrentHealth", CFmtStr( "%d", GetHealth() ) );
		criteria.AppendCriteria( "CurrentTemporaryHealth", CFmtStr( "%.0f", GetHealthBuffer() ) );
		criteria.AppendCriteria( "IsIncapacitated", IsIncapacitated() ? "1" : "0" );
		criteria.AppendCriteria( "HasFirstAidKit", GetWeaponOfType( 12 ) ? "1" : "0" );
		criteria.AppendCriteria( "HasPainPills", GetWeaponOfType( 15 ) ? "1" : "0" );
		criteria.AppendCriteria( "HasGrenade", GetWeaponOfType( 14 ) ? "1" : "0" );
		criteria.AppendCriteria( "HasMolotov", GetWeaponOfType( 13 ) ? "1" : "0" );
		criteria.AppendCriteria( "IncapacitatedCount", CFmtStr( "%d", m_totalReviveCount + ( IsIncapacitated() ? 1 : 0 ) ) );
		criteria.AppendCriteria( "OnThirdStrike", IsOnThirdStrike() ? "1" : "0" );
		if ( GetTeamNumber() == TEAM_SURVIVOR )
		{
			static const char *names[] = { "Gambler", "Producer", "Coach", "Mechanic", "NamVet", "TeenGirl", "Biker", "Manager" };
			int count = TerrorGameRules()->GetSurvivorSet() == 1 ? 8 : 4;
			for ( int character = 0; character < count; ++character )
			{
				float distance = TheDirector->GetResponseSurvivorDistance( GetSurvivorCharacter(), character, true );
				criteria.AppendCriteria( CFmtStr( "DistTo%s", names[character] ), CFmtStr( "%.1f", MIN( 1000000000.0f, distance ) ) );
			}
			int closest = TheDirector->GetClosestResponseSurvivor( GetSurvivorCharacter() );
			float distance = TheDirector->GetResponseSurvivorDistance( GetSurvivorCharacter(), closest, false );
			criteria.AppendCriteria( "DistToClosestSurvivor", CFmtStr( "%.1f", MIN( 1000000000.0f, distance ) ) );
			criteria.AppendCriteria( "ClosestSurvivor", ::GetResponseCharacterName( closest ) );
		}
		criteria.AppendCriteria( "NumberInSafeSpot", CFmtStr( "%d", TheDirector->GetNumberInSafeSpot() ) );
		criteria.AppendCriteria( "NumberOutsideSafeSpot", CFmtStr( "%d", TheDirector->GetNumberOutsideSafeSpot() ) );
		bool present[9] = { false };
		CollectResponseZombiePresence( present );
		static const int classes[] = { 0, 1, 2, 3, 6, 7, 8 };
		static const char *keys[] = { "ZombiePresentNormal", "ZombiePresentSmoker", "ZombiePresentBoomer", "ZombiePresentHunter", "ZombiePresentCharger", "ZombiePresentWitch", "ZombiePresentTank" };
		for ( int i = 0; i < ARRAYSIZE( classes ); ++i )
			if ( present[classes[i]] )
				criteria.AppendCriteria( keys[i], "1" );
	}
	else
		criteria.AppendCriteria( "Team", "Spectator" );

	criteria.AppendCriteria( "gamemode", TheDirector->GetBaseGameMode() );
	criteria.AppendCriteria( "Chapter", CFmtStr( "%i", TheDirector->GetChapterNumber() ) );
	const char *campaign = GetResponseCampaignName();
	if ( campaign ) criteria.AppendCriteria( "campaign", campaign );
	criteria.AppendCriteria( "InCombat", IsInCombat() ? "1" : "0" );
	criteria.AppendCriteria( "InCombatMusic", GetMusic()->IsInCombat() ? "1" : "0" );
	criteria.AppendCriteria( "TimeSinceCombat", CFmtStr( "%f", GetTimeSinceCombat() ) );
	criteria.AppendCriteria( "TimeSinceGroupInCombat", CFmtStr( "%f", GetTimeSinceGroupInCombat() ) );
	criteria.AppendCriteria( "TankActive", TheDirector->GetTankCount() > 0 ? "1" : "0" );
	criteria.AppendCriteria( "BeingJockeyed", m_jockeyAttacker.Get() ? "1" : "0" );
	criteria.AppendCriteria( "BeingHealed", IsUseActionTarget( 1 ) ? "1" : "0" );
	criteria.AppendCriteria( "MovementSpeed", CFmtStr( "%d", _mm_cvtt_ss2si( _mm_set_ss( GetAbsVelocity().Length2D() ) ) ) );
	criteria.AppendCriteria( "LowViolence", UTIL_IsLowViolence() ? "1" : "0" );
	criteria.AppendCriteria( "speaking", GetExpresser()->IsSpeaking() ? "1" : "0" );
	criteria.AppendCriteria( "campaignrandomnum", CFmtStr( "%d", TheDirector->GetCampaignRandomNumber() ) );
	criteria.AppendCriteria( "introactor", TheDirector->GetIntroActorName() );
	BaseClass::ModifyOrAppendCriteria( criteria );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
