//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_JOCKEY_LOCOMOTION_H
#define L4D2_JOCKEY_LOCOMOTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/Player/NextBotPlayerLocomotion.h"
class JockeyLocomotion : public PlayerLocomotion
{
public:
	explicit JockeyLocomotion( INextBot *bot ) : PlayerLocomotion( bot ) {}
	virtual void Reset() OVERRIDE {}
	virtual void Update() OVERRIDE { PlayerLocomotion::Update(); }
	virtual bool IsAreaTraversable( const CNavArea *area ) const OVERRIDE;
	virtual bool IsEntityTraversable( CBaseEntity *entity, TraverseWhenType when = EVENTUALLY ) const OVERRIDE;
};
#endif // L4D2_JOCKEY_LOCOMOTION_H
