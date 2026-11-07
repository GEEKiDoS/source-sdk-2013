//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/boomer.h"
#include "l4d2/infected/special/boomer_intention.h"
#include "l4d2/infected/special/boomer_actions.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
BoomerIntention::BoomerIntention( Boomer *actor ) : IIntention( actor ), m_actor( NULL ), m_behavior( NULL ) {} // S4268D0
BoomerIntention::~BoomerIntention() { delete m_behavior; }
void BoomerIntention::Reset() // S42E060
{
	m_actor = static_cast< Boomer * >( GetBot()->GetEntity() );
	delete m_behavior;
	m_behavior = new Behavior< Boomer >( new BoomerBehavior );
}
void BoomerIntention::Update() // S42E1F0
{
	VPROF_BUDGET( "BoomerIntention::Update", "NextBot" );
	if ( m_behavior ) m_behavior->Update( m_actor, GetUpdateInterval() );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
