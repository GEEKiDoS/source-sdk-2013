//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SURVIVOR_BODY_H
#define L4D2_SURVIVOR_BODY_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/Player/NextBotPlayerBody.h"

class SurvivorBody : public PlayerBody
{
public:
	explicit SurvivorBody( INextBot *bot );
	virtual void Upkeep() OVERRIDE;
	virtual float GetMaxHeadAngularVelocity() const OVERRIDE;
};

#endif // L4D2_SURVIVOR_BODY_H
