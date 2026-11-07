//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef ACHIEVEMENTS_L4D2_H
#define ACHIEVEMENTS_L4D2_H
#ifdef _WIN32
#pragma once
#endif
#include "baseachievement.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"
class C_TerrorPlayer;
// Native client2330A0..233AA0. Target SDK owns the manager and Steam API.
class CLeft4DeadAchievement : public CBaseAchievement
{
public:
 CLeft4DeadAchievement();
 virtual void ListenForEvents() OVERRIDE;
 virtual bool IsActive() OVERRIDE;
 virtual void PrintAdditionalStatus() OVERRIDE;
 void ResetNativeState();
 void OnNativeAchievementEvent() { IncrementCount(); }
 virtual bool LocalPlayerCanEarn() OVERRIDE;
 void Configure( int internalID, int l4dID, const char *name, int points, int goal, int flags, int team, int category );
 int GetL4DAchievementID() const { return m_l4dID; }
 int GetCategory() const { return m_category; }
 const char *GetIcon();
 virtual const char *GetComponentName( int index ) const;
 virtual const wchar_t *GetComponentLabel( int index );
 virtual void UpdateSurvivalBestTimes( KeyValues *records );
protected:
 virtual void FireGameEvent_Internal( IGameEvent *event ) OVERRIDE;
 virtual void IncrementCount( int increment = 0 );
 virtual bool RequiresAlive() const { return false; } // Native233360.
 virtual void OnLocalEvent( C_TerrorPlayer *player, const char *name, IGameEvent *event ) { IncrementCount(); } //2333E0.
 virtual void OnEvent( const char *name, IGameEvent *event ) {} //2333F0.
 virtual void OnCampaignStart(); //233AA0.
 virtual void OnMapStart() {} //233400.
 virtual void OnMapEnd() {} //233410.
 virtual void OnCampaignFinish( const char *map, int difficulty ) {} //233420.
 virtual void OnDifficultyChanged( int now, int old ) {} //233430.
 void OnComponentEvent( const char *name );
 void EnsureComponentBitSetAndEvaluate( int bit );
 bool PlayedEntireCampaign() const;
 void BeginCampaignIfFirstChapter();
 void SetEvents( const char **events, int count ) { m_events = events; m_eventCount = count; }
 void SetComponents( const char **names, int count, const char **labels ) { m_pszComponentNames = names; m_iNumComponents = count; m_componentLabels=labels; }
 void AddDistinct( CUtlVector<int> &list, int value );
 const char *MapName() const;
 int m_l4dID, m_team, m_category;
 bool m_failed;
 const char **m_events;
 const char **m_componentLabels;
 int m_eventCount;
 CUtlVector<CUtlString> m_completedMaps;
};
class CLeft4DeadCompetitiveAchievement : public CLeft4DeadAchievement
{
public:
 virtual void ListenForEvents() OVERRIDE;
protected:
 virtual void FireGameEvent_Internal( IGameEvent *event ) OVERRIDE;
 virtual void IncrementCount( int increment = 0 ) OVERRIDE;
 virtual void OnScoreAnimation();
 virtual void OnCompetitiveRoundStart();
 virtual void ResetCompetitive() {} //Native235210.
};
// Native manager's KeyValues callback; records are borrowed, never persisted here.
void TerrorAchievementsUpdateSurvivalBestTimes( KeyValues *records );
#endif
