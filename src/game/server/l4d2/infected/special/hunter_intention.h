//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_HUNTER_INTENTION_H
#define L4D2_HUNTER_INTENTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/NextBotIntentionInterface.h"
#include "NextBot/NextBotBehavior.h"
class Hunter;
class HunterIntention : public IIntention
{
public:
	explicit HunterIntention( Hunter *actor );
	virtual ~HunterIntention();
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual INextBotEventResponder *FirstContainedResponder() const OVERRIDE { return m_behavior; }
private:
	Hunter *m_actor;
	Behavior< Hunter > *m_behavior;
};
#endif // L4D2_HUNTER_INTENTION_H
