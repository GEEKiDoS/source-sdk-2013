//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SMOKER_LOCOMOTION_H
#define L4D2_SMOKER_LOCOMOTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/Player/NextBotPlayerLocomotion.h"
class SmokerLocomotion : public PlayerLocomotion
{
public:
	explicit SmokerLocomotion( INextBot *bot ) : PlayerLocomotion( bot ) {}
	virtual void Reset() OVERRIDE {}
	virtual void Update() OVERRIDE { PlayerLocomotion::Update(); }
	virtual bool IsAreaTraversable( const CNavArea *area ) const OVERRIDE;
	virtual bool IsEntityTraversable( CBaseEntity *entity, TraverseWhenType when = EVENTUALLY ) const OVERRIDE;
};
#endif
