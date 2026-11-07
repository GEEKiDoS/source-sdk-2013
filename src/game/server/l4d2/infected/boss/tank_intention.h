//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TANK_INTENTION_H
#define L4D2_TANK_INTENTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/NextBotIntentionInterface.h"
#include "NextBot/NextBotBehavior.h"
class Tank;
class TankIntention : public IIntention
{
public:
	explicit TankIntention( Tank *actor );
	virtual ~TankIntention();
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual INextBotEventResponder *FirstContainedResponder() const OVERRIDE { return m_behavior; }
private:
	Tank *m_actor;
	Behavior< Tank > *m_behavior;
};
#endif
