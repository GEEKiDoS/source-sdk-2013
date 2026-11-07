//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/hunter.h"
#include "l4d2/infected/special/hunter_intention.h"
#include "l4d2/infected/special/hunter_actions.h"
#include "tier0/memdbgon.h"

HunterIntention::HunterIntention( Hunter *actor ) : IIntention( actor ), m_actor( NULL ), m_behavior( NULL ) {}
HunterIntention::~HunterIntention() { delete m_behavior; }
// S4442F0 refreshes the actual component owner before destroying the old stack.
void HunterIntention::Reset()
{
	m_actor = static_cast< Hunter * >( GetBot()->GetEntity() );
	delete m_behavior;
	m_behavior = new Behavior< Hunter >( new HunterBehavior );
}
void HunterIntention::Update()
{
	VPROF_BUDGET( "HunterIntention::Update", "NextBot" );
	if ( m_behavior )
		m_behavior->Update( m_actor, GetUpdateInterval() );
}
