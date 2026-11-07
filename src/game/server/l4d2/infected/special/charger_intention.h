//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_CHARGER_INTENTION_H
#define L4D2_CHARGER_INTENTION_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/NextBotIntentionInterface.h"
#include "NextBot/NextBotBehavior.h"
class Charger;
class ChargerIntention : public IIntention
{
public:
	explicit ChargerIntention( Charger *actor );
	virtual ~ChargerIntention();
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual INextBotEventResponder *FirstContainedResponder() const OVERRIDE { return m_behavior; }
	virtual INextBotEventResponder *NextContainedResponder( INextBotEventResponder *current ) const OVERRIDE { return NULL; }
private:
	Charger *m_actor;
	Behavior< Charger > *m_behavior;
};
#endif
