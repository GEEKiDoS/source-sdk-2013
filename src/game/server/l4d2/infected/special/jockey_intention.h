//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_JOCKEY_INTENTION_H
#define L4D2_JOCKEY_INTENTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/NextBotIntentionInterface.h"
#include "NextBot/NextBotBehavior.h"
class Jockey;
class JockeyIntention : public IIntention
{
public:
	explicit JockeyIntention( Jockey *actor );
	virtual ~JockeyIntention();
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual INextBotEventResponder *FirstContainedResponder() const OVERRIDE { return m_behavior; }
private:
	Jockey *m_actor;
	Behavior< Jockey > *m_behavior;
};
#endif // L4D2_JOCKEY_INTENTION_H
