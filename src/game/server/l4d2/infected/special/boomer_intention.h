//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BOOMER_INTENTION_H
#define L4D2_BOOMER_INTENTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/NextBotIntentionInterface.h"
#include "NextBot/NextBotBehavior.h"
class Boomer;
class BoomerIntention : public IIntention
{
public:
	explicit BoomerIntention( Boomer *actor );
	virtual ~BoomerIntention();
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual INextBotEventResponder *FirstContainedResponder() const OVERRIDE { return m_behavior; }
private:
	Boomer *m_actor;
	Behavior< Boomer > *m_behavior;
};
#endif
