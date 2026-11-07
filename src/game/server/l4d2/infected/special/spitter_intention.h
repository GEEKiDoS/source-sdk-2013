//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SPITTER_INTENTION_H
#define L4D2_SPITTER_INTENTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/NextBotIntentionInterface.h"
#include "NextBot/NextBotBehavior.h"
class Spitter;
class SpitterIntention : public IIntention
{
public:
	explicit SpitterIntention( Spitter *actor );
	virtual ~SpitterIntention();
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual INextBotEventResponder *FirstContainedResponder() const OVERRIDE { return m_behavior; }
private:
	Spitter *m_actor;
	Behavior< Spitter > *m_behavior;
};
#endif
