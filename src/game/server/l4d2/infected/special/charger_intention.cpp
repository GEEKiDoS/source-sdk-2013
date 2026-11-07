//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/charger.h"
#include "l4d2/infected/special/charger_intention.h"
#include "l4d2/infected/special/charger_actions.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
// S431880, S4318C0, S439010, S4391A0.
ChargerIntention::ChargerIntention( Charger *actor ) : IIntention( actor ), m_actor( NULL ), m_behavior( NULL ) {}
ChargerIntention::~ChargerIntention() { delete m_behavior; }
void ChargerIntention::Reset()
{
	m_actor = static_cast< Charger * >( GetBot()->GetEntity() );
	delete m_behavior;
	m_behavior = new Behavior< Charger >( new ChargerBehavior );
}
void ChargerIntention::Update()
{
	VPROF_BUDGET( "ChargerIntention::Update", "NextBot" );
	if ( m_behavior ) m_behavior->Update( m_actor, GetUpdateInterval() );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
