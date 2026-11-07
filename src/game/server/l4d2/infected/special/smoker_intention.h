//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SMOKER_INTENTION_H
#define L4D2_SMOKER_INTENTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/NextBotIntentionInterface.h"
#include "NextBot/NextBotBehavior.h"
class Smoker;
class SmokerIntention : public IIntention
{
public:
	explicit SmokerIntention( Smoker *actor );
	virtual ~SmokerIntention();
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual INextBotEventResponder *FirstContainedResponder() const OVERRIDE { return m_behavior; }
	virtual INextBotEventResponder *NextContainedResponder( INextBotEventResponder *current ) const OVERRIDE { return NULL; }
private:
	Smoker *m_actor;
	Behavior< Smoker > *m_behavior;
};
#endif
