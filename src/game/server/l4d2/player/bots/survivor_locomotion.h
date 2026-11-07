//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SURVIVOR_LOCOMOTION_H
#define L4D2_SURVIVOR_LOCOMOTION_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/Player/NextBotPlayerLocomotion.h"

class SurvivorLocomotion : public PlayerLocomotion
{
public:
	explicit SurvivorLocomotion( INextBot *bot );
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void Approach( const Vector &pos, float goalWeight = 1.0f ) OVERRIDE;
	virtual bool IsScrambling() const OVERRIDE;
	virtual bool IsAreaTraversable( const CNavArea *area ) const OVERRIDE;
	virtual bool IsEntityTraversable( CBaseEntity *obstacle, TraverseWhenType when = EVENTUALLY ) const OVERRIDE;
	virtual float GetRunSpeed() const OVERRIDE;
	virtual float GetWalkSpeed() const OVERRIDE;

private:
	Vector m_approachDirection;
	float m_approachWeight;
	int m_approachCount;
	Vector m_previousFeet;
	bool m_hasMoved;
	CountdownTimer m_waitTimer;
};

#endif // L4D2_SURVIVOR_LOCOMOTION_H
