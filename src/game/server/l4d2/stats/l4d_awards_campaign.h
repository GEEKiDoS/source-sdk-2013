//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D_AWARDS_CAMPAIGN_H
#define L4D_AWARDS_CAMPAIGN_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/stats/l4d_awards.h"

class CCheckpointCompleteAwardTemplate : public CAwardTemplate
{
public:
	explicit CCheckpointCompleteAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	void OnCheckpoint();
	void ResetChapter();
	void ResetCampaign();
	void OnEnemyDamage( CTerrorPlayer *player, CTerrorPlayer *victim, CBaseEntity *attacker );
	bool m_bHurt;
	bool m_bIncapacitated;
	bool m_bDied;
	bool m_bTeamHurt;
	bool m_bTeamIncapacitated;
	bool m_bTeamDied;
	bool m_bCampaignHurt;
	bool m_bCampaignIncapacitated;
	bool m_bCampaignDied;
	bool m_bCampaignTeamHurt;
	bool m_bCampaignTeamIncapacitated;
	bool m_bCampaignTeamDied;
	bool m_bStartedAtPlayerStart;
};

class CDarkMissionAwardTemplate : public CAwardTemplate
{
public:
	explicit CDarkMissionAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	bool m_bStartedAtPlayerStart;
	bool m_bDied;
};

class CGoodTeammateAwardTemplate : public CAwardTemplate
{
public:
	explicit CGoodTeammateAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	bool m_bChapterBadAward;
	bool m_bCampaignBadAward;
	bool m_bTeammateBadAward;
};

class CHeroAwardTemplate : public CAwardTemplate
{
public:
	explicit CHeroAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	bool m_bWasLastStanding;
};

class CHulkShutOutAwardTemplate : public CAwardTemplate
{
public:
	explicit CHulkShutOutAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	bool m_bTeamHurtByTank;
	float m_flLastAbortedTankDeath;
};

class CLeft4DeadAwardTemplate : public CAwardTemplate
{
public:
	explicit CLeft4DeadAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CLeftBehindAwardTemplate : public CAwardTemplate
{
public:
	explicit CLeftBehindAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CNarrowEscapeAwardTemplate : public CAwardTemplate
{
public:
	explicit CNarrowEscapeAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CPerfectTeamAwardTemplate : public CAwardTemplate
{
public:
	explicit CPerfectTeamAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	bool m_bTeamBadAward;
};

class CScenarioWinAwardTemplate : public CAwardTemplate
{
public:
	explicit CScenarioWinAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CToughGuyAwardTemplate : public CAwardTemplate
{
public:
	explicit CToughGuyAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	bool m_bReceivedHealing;
	bool m_bDied;
	bool m_bStartedAtPlayerStart;
};

#endif // L4D_AWARDS_CAMPAIGN_H
