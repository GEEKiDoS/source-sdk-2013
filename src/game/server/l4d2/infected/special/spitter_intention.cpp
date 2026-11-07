//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/spitter.h"
#include "l4d2/infected/special/spitter_intention.h"
#include "l4d2/infected/special/spitter_actions.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
SpitterIntention::SpitterIntention( Spitter *actor ) : IIntention( actor ), m_actor( NULL ), m_behavior( NULL ) {} // S45F990
SpitterIntention::~SpitterIntention() { delete m_behavior; }
void SpitterIntention::Reset() // S467150
{
	m_actor = static_cast< Spitter * >( GetBot()->GetEntity() );
	delete m_behavior;
	m_behavior = new Behavior< Spitter >( new SpitterBehavior );
}
void SpitterIntention::Update() // S4672E0
{
	VPROF_BUDGET( "SpitterIntention::Update", "NextBot" );
	if ( m_behavior ) m_behavior->Update( m_actor, GetUpdateInterval() );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
