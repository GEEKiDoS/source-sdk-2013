//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ZOMBIEBOT_VISION_H
#define L4D2_ZOMBIEBOT_VISION_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/NextBotVisionInterface.h"


class ZombieBotVision : public IVision
{
public:
	DECLARE_CLASS( ZombieBotVision, IVision );

	explicit ZombieBotVision( INextBot *bot );
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;

	virtual CBaseCombatCharacter *GetPrimaryKnownThreat() const OVERRIDE;
	virtual float GetTimeSinceVisible( int team ) const OVERRIDE;
	virtual CBaseEntity *GetClosestKnown( int team = TEAM_ANY ) const OVERRIDE;
	virtual CBaseEntity *GetClosestKnown( const INextBotEntityFilter &filter ) const OVERRIDE;
	virtual int GetKnownCount( int team, float rangeLimit = -1.0f ) const OVERRIDE;
	void ClearRecognizedVictim();

	void CollectPotentiallyVisibleEntities( CUtlVector< CBaseEntity * > *potentiallyVisible );
	virtual float GetMaxVisionRange() const OVERRIDE;
	virtual float GetMinRecognizeTime() const OVERRIDE;
	virtual float GetDefaultFieldOfView() const OVERRIDE;
	virtual bool IsIgnored( CBaseEntity *subject ) const OVERRIDE;
	virtual bool IsVisibleEntityNoticed( CBaseEntity *subject ) const OVERRIDE;

private:
	float GetNoticeRange() const;
	CHandle< CBaseCombatCharacter > m_recognizedVictim;
	float m_recognizedRangeSquared;
	float m_neutralRecognizeTime;
	float m_alertRecognizeTime;
	CountdownTimer m_scanTimer;
	IntervalTimer m_visibleTimer;
};

#endif // L4D2_ZOMBIEBOT_VISION_H
