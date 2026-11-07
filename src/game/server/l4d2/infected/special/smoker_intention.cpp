//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/smoker.h"
#include "l4d2/infected/special/smoker_intention.h"
#include "l4d2/infected/special/smoker_actions.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
// S453EF0, S453F30, S45B6A0, S45B830.
SmokerIntention::SmokerIntention( Smoker *actor ) : IIntention( actor ), m_actor( NULL ), m_behavior( NULL ) {}
SmokerIntention::~SmokerIntention() { delete m_behavior; }
void SmokerIntention::Reset()
{
	m_actor = static_cast< Smoker * >( GetBot()->GetEntity() );
	delete m_behavior;
	m_behavior = new Behavior< Smoker >( new SmokerBehavior );
}
void SmokerIntention::Update()
{
	VPROF_BUDGET( "SmokerIntention::Update", "NextBot" );
	if ( m_behavior ) m_behavior->Update( m_actor, GetUpdateInterval() );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
