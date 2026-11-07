//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_TEAM_SITUATION_H
#define SURVIVOR_TEAM_SITUATION_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/terrorplayer.h"
#include "NextBot/Path/NextBotPathFollow.h"

class SurvivorBot;
class CTerrorPlayer;
class TerrorNavArea;

class SurvivorTeamSituation
{
public:
	explicit SurvivorTeamSituation( SurvivorBot *owner );
	void Reset();
	void Update();
	void PruneInvalidTeammates();
	int GetTeammateCount() const { return m_teammateCount; }
	CTerrorPlayer *GetTeammate( int index ) const { return m_teammates[index].Get(); }
	float GetTeammateRange( int index ) const { return m_teammateRanges[index]; }
	const Vector &GetTeammateOffset( int index ) const { return m_teammateOffsets[index]; }
	CTerrorPlayer *GetDownedFriend() const { return m_downedFriend.Get(); }
	float GetDownedFriendHealth() const { return m_downedFriendHealth; }
	CTerrorPlayer *GetDownedHuman() const { return m_downedHuman.Get(); }
	CTerrorPlayer *GetWeakestFriend() const { return m_weakestFriend.Get(); }
	CTerrorPlayer *GetWeakestHuman() const { return m_weakestHuman.Get(); }
	CTerrorPlayer *GetNearestFriend() const { return m_nearestFriend.Get(); }
	CTerrorPlayer *GetNearestHuman() const { return m_nearestHuman.Get(); }
	float GetNearestHumanRange() const { return m_nearestHumanRange; }
	CTerrorPlayer *GetRecentlyFightingFriend() const { return m_recentlyFightingFriend.Get(); }
	CTerrorPlayer *GetNearestVisibleFriend() const { return m_nearestVisibleFriend.Get(); }
	CTerrorPlayer *GetFarthestFriend() const { return m_farthestFriend.Get(); }
	CTerrorPlayer *GetLeader() const { return m_leader.Get(); }
	CTerrorPlayer *GetLeadingHuman() const { return m_leadingHuman.Get(); }
	CTerrorPlayer *GetLeadingBot() const { return m_leadingBot.Get(); }
	CTerrorPlayer *GetTrailingFriend() const { return m_trailingFriend.Get(); }
	float GetTrailingFriendFlow() const { return m_trailingFriendFlow; }
	CTerrorPlayer *GetTrailingHuman() const { return m_trailingHuman.Get(); }
	CTerrorPlayer *GetFriendAhead() const { return m_friendAhead.Get(); }
	CTerrorPlayer *GetFriendBehind() const { return m_friendBehind.Get(); }
	CTerrorPlayer *GetTonguedFriend() const { return m_tonguedFriend.Get(); }
	CTerrorPlayer *GetPouncedFriend() const { return m_pouncedFriend.Get(); }
	CTerrorPlayer *GetPummeledFriend() const { return m_pummeledFriend.Get(); }
	CTerrorPlayer *GetJockeyedFriend() const { return m_jockeyedFriend.Get(); }
	CTerrorPlayer *GetSeparatedFriend() const { return m_separatedFriend.Get(); }
	int GetNeighborCount() const { return m_neighborCount; }
	int GetMeleeWeaponCount() const { return m_meleeWeaponCount; }
	bool AreAllTeammatesBots() const;
	bool IsTeamNearCheckpoint() const;
	float GetRangeToFriend( CTerrorPlayer *player ) const;
	float GetTimeSinceSeenAnyFriend() const;
	CTerrorPlayer *GetRandomRecentlySeenFriend() const;
	CTerrorPlayer *GetClosestReachableFriend();
	CTerrorPlayer *GetClosestFriendTo( CBaseEntity *subject, int flags );
	PathFollower *GetPathToFriend( CTerrorPlayer *player );
	bool HasRecentlyFiredFriend() const { return m_friendFiredTimer.GetRemainingTime() > 0.0f; }

private:
	virtual void ResetScan(); // Native sole vtable slot0, S3A5B90.
	void CollectPlayer( CTerrorPlayer *player );
	void RememberVisibleFriend( CTerrorPlayer *player );
	void SelectLeader();
	void SelectSeparatedFriend();
	SurvivorBot *m_owner;
	CHandle< CTerrorPlayer > m_downedFriend;
	float m_downedFriendHealth;
	CHandle< CTerrorPlayer > m_downedHuman;
	float m_downedHumanHealth;
	CHandle< CTerrorPlayer > m_weakestFriend;
	float m_weakestFriendHealth;
	CHandle< CTerrorPlayer > m_weakestHuman;
	float m_weakestHumanHealth;
	CHandle< CTerrorPlayer > m_nearestFriend;
	float m_nearestFriendRange;
	CHandle< CTerrorPlayer > m_nearestHuman;
	float m_nearestHumanRange;
	CHandle< CTerrorPlayer > m_recentlyFightingFriend;
	float m_recentlyFightingFriendRange;
	CHandle< CTerrorPlayer > m_nearestVisibleFriend;
	float m_nearestVisibleFriendRange;
	CHandle< CTerrorPlayer > m_farthestFriend;
	float m_farthestFriendRange;
	CHandle< CTerrorPlayer > m_leader;
	CHandle< CTerrorPlayer > m_leadingHuman;
	float m_leadingHumanScore;
	CHandle< CTerrorPlayer > m_leadingBot;
	float m_leadingBotScore;
	CHandle< CTerrorPlayer > m_trailingFriend;
	float m_trailingFriendFlow;
	CHandle< CTerrorPlayer > m_trailingHuman;
	float m_trailingHumanFlow;
	CHandle< CTerrorPlayer > m_friendAhead;
	float m_friendAheadFlow;
	CHandle< CTerrorPlayer > m_friendBehind;
	float m_friendBehindFlow;
	int m_meleeWeaponCount;
	CHandle< CTerrorPlayer > m_teammates[3];
	float m_teammateRanges[3];
	Vector m_teammateOffsets[3];
	int m_teammateCount;
	PathFollower m_friendPaths[3];
	int m_friendPathCount;
	struct SeenFriend
	{
		CHandle< CTerrorPlayer > player;
		IntervalTimer lastSeen;
	};
	SeenFriend m_seenFriends[3];
	int m_seenFriendCount;
	int m_neighborCount;
	CHandle< CTerrorPlayer > m_tonguedFriend;
	CHandle< CTerrorPlayer > m_pouncedFriend;
	CHandle< CTerrorPlayer > m_pummeledFriend;
	CHandle< CTerrorPlayer > m_jockeyedFriend;
	CHandle< CTerrorPlayer > m_separatedFriend;
	CountdownTimer m_separatedFriendTimer;
	CountdownTimer m_friendFiredTimer;
};

#endif // SURVIVOR_TEAM_SITUATION_H
