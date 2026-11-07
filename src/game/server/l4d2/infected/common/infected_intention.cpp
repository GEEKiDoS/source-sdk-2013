//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/common/infected_behavior.h"
#include "l4d2/infected/common/infected_intention.h"

#include "tier0/memdbgon.h"

// Pinned server RVAs: ctor 3F69E0, destructor 3F6A20, Reset 3F7EE0,
// Update 3F8060 -> Behavior<Infected>::Update 3F7F70, initial action 3F6900.
InfectedIntention::InfectedIntention( Infected *me ) : IIntention( me ), m_behavior( NULL )
{
}

InfectedIntention::~InfectedIntention()
{
	delete m_behavior;
}

void InfectedIntention::Reset()
{
	IIntention::Reset();
	delete m_behavior;
	m_behavior = new Behavior< Infected >( CreateInitialAction() );
}

void InfectedIntention::Update()
{
	if ( m_behavior )
		m_behavior->Update( static_cast< Infected * >( GetBot() ), GetUpdateInterval() );
}

INextBotEventResponder *InfectedIntention::FirstContainedResponder() const
{
	return m_behavior;
}

Action< Infected > *InfectedIntention::CreateInitialAction()
{
	return new InfectedExecAction;
}
