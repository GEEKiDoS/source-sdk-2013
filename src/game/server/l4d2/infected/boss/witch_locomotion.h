//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WITCH_LOCOMOTION_H
#define L4D2_WITCH_LOCOMOTION_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/infected/common/zombiebot_locomotion.h"

class WitchLocomotion : public ZombieBotLocomotion
{
public:
	DECLARE_CLASS( WitchLocomotion, ZombieBotLocomotion );
	explicit WitchLocomotion( Infected *me );
	virtual void SetSpeedLimit( float speed ) OVERRIDE;
	virtual float GetSpeedLimit() const OVERRIDE;
	virtual float GetRunSpeed() const OVERRIDE;
	virtual float GetMaxAcceleration() const OVERRIDE;
	virtual float GetMaxDeceleration() const OVERRIDE;
	virtual bool IsAreaTraversable( const CNavArea *area ) const OVERRIDE;
	virtual void SetVelocity( const Vector &velocity ) OVERRIDE;

private:
	float m_speedLimit;
};

#endif // L4D2_WITCH_LOCOMOTION_H
