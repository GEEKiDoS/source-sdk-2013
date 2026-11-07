//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_INFECTED_INTENTION_H
#define L4D2_INFECTED_INTENTION_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/NextBotIntentionInterface.h"

class Infected;
template < typename Actor > class Behavior;
template < typename Actor > class Action;

class InfectedIntention : public IIntention
{
public:
	explicit InfectedIntention( Infected *me );
	virtual ~InfectedIntention();
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual INextBotEventResponder *FirstContainedResponder() const OVERRIDE;
	virtual INextBotEventResponder *NextContainedResponder( INextBotEventResponder *current ) const OVERRIDE { return NULL; }
protected:
	virtual Action< Infected > *CreateInitialAction();
private:
	Behavior< Infected > *m_behavior;
};

#endif // L4D2_INFECTED_INTENTION_H
