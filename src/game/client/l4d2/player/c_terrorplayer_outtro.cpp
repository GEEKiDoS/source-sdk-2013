//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include <limits.h>
#include "tier0/memdbgon.h"

int C_TerrorPlayer::GetOuttroStatistic( int statistic, int scope ) const
{
	// Native26D240, actual SAME DT_TerrorPlayer statistics fields. scope0 is
	// checkpoint,1 mission; other nonzero scope returns0 as originally written.
	if ( scope != 0 && scope != 1 ) return 0;
	if ( scope == 1 && ( ( statistic >= 6 && statistic <= 8 ) || statistic >= 34 ) ) return INT_MIN;
	const bool mission = scope == 1;
	switch ( statistic )
	{
	case 0: return mission ? m_missionAwardCounts[87] : m_checkpointAwardCounts[87];
	case 1:
	case 9:
		{
			unsigned int total = 0;
			for ( int i = 0; i < 9; ++i ) total += (unsigned int)( mission ? m_missionZombieKills[i] : m_checkpointZombieKills[i] );
			return (int)total;
		}
	case 2:
		{
			unsigned int total = 0;
			for ( int i = 1; i < 9; ++i ) total += (unsigned int)( mission ? m_missionZombieKills[i] : m_checkpointZombieKills[i] );
			return (int)total;
		}
	case 3: return mission ? m_missionReviveOtherCount : m_checkpointReviveOtherCount;
	case 4: return mission ? m_missionDamageTaken : m_checkpointDamageTaken;
	case 5: return mission ? m_missionIncaps : m_checkpointIncaps;
	case 6: return m_checkpointDamageToTank;
	case 7: return m_checkpointDamageToWitch;
	case 8: return m_checkpointHeadshots;
	case 10: return mission ? m_missionAwardCounts[67] : m_checkpointAwardCounts[67];
	case 11: return mission ? m_missionAccuracy : m_checkpointAccuracy;
	case 12: return mission ? m_missionDeaths : m_checkpointDeaths;
	case 13: return mission ? m_missionMedkitsUsed : m_checkpointMedkitsUsed;
	case 14: return mission ? m_missionPillsUsed : m_checkpointPillsUsed;
	case 15: return mission ? m_missionMolotovsUsed : m_checkpointMolotovsUsed;
	case 16: return mission ? m_missionPipebombsUsed : m_checkpointPipebombsUsed;
	case 17: return mission ? m_missionBoomerBilesUsed : m_checkpointBoomerBilesUsed;
	case 18: return mission ? m_missionAdrenalinesUsed : m_checkpointAdrenalinesUsed;
	case 19: return mission ? m_missionDefibrillatorsUsed : m_checkpointDefibrillatorsUsed;
	case 20: return mission ? m_missionZombieKills[3] : m_checkpointZombieKills[3];
	case 21: return mission ? m_missionZombieKills[2] : m_checkpointZombieKills[2];
	case 22: return mission ? m_missionZombieKills[1] : m_checkpointZombieKills[1];
	case 23: return mission ? m_missionZombieKills[6] : m_checkpointZombieKills[6];
	case 24: return mission ? m_missionZombieKills[5] : m_checkpointZombieKills[5];
	case 25: return mission ? m_missionZombieKills[4] : m_checkpointZombieKills[4];
	case 26: return mission ? m_missionZombieKills[7] : m_checkpointZombieKills[7];
	case 27: return mission ? m_missionZombieKills[8] : m_checkpointZombieKills[8];
	case 28: return mission ? m_missionZombieKills[0] : m_checkpointZombieKills[0];
	case 29: return mission ? m_missionAwardCounts[94] : m_checkpointAwardCounts[94];
	case 30: return mission ? m_missionAwardCounts[70] : m_checkpointAwardCounts[70];
	case 31: return mission ? m_missionFirstAidShared : m_checkpointFirstAidShared;
	case 32: return mission ? m_missionHeadshotAccuracy : m_checkpointHeadshotAccuracy;
	case 33: return mission ? m_missionMeleeKills : m_checkpointMeleeKills;
	case 34:
		return (int)( (unsigned int)m_checkpointPZTankDamage + (unsigned int)m_checkpointPZHunterDamage
			+ (unsigned int)m_checkpointPZSmokerDamage + (unsigned int)m_checkpointPZBoomerDamage
			+ (unsigned int)m_checkpointPZJockeyDamage + (unsigned int)m_checkpointPZSpitterDamage + (unsigned int)m_checkpointPZChargerDamage );
	case 35: return m_checkpointPZIncaps;
	case 36: return m_checkpointPZTankDamage;
	case 37: return m_checkpointPZHunterDamage;
	case 38: return m_checkpointPZSmokerDamage;
	case 39: return m_checkpointPZBoomerDamage;
	case 40: return m_checkpointPZJockeyDamage;
	case 41: return m_checkpointPZSpitterDamage;
	case 42: return m_checkpointPZChargerDamage;
	case 43: return m_checkpointPZKills;
	case 44: return m_checkpointPZPounces;
	case 45: return m_checkpointPZPushes;
	case 46: return m_checkpointPZTankPunches;
	case 47: return m_checkpointPZTankThrows;
	case 48: return m_checkpointPZHung;
	case 49: return m_checkpointPZPulled;
	case 50: return m_checkpointPZBombed;
	case 51: return m_checkpointPZVomited;
	case 52: return m_checkpointPZHighestDmgPounce;
	case 53: return m_checkpointPZLongestSmokerGrab;
	case 54: return m_checkpointPZLongestJockeyRide;
	case 55: return (int)( (unsigned int)m_checkpointPZBombed + (unsigned int)m_checkpointPZVomited );
	case 56: return m_checkpointPZNumChargeVictims;
	default: return INT_MIN;
	}
}
