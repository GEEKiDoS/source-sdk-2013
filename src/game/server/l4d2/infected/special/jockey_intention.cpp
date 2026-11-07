//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/jockey.h"
#include "l4d2/infected/special/jockey_intention.h"
#include "l4d2/infected/special/jockey_actions.h"
#include "tier0/memdbgon.h"

JockeyIntention::JockeyIntention( Jockey *actor ) : IIntention( actor ), m_actor( NULL ), m_behavior( NULL ) {}
JockeyIntention::~JockeyIntention() { delete m_behavior; }
void JockeyIntention::Reset()
{
	m_actor = static_cast< Jockey * >( GetBot()->GetEntity() );
	delete m_behavior;
	m_behavior = new Behavior< Jockey >( new JockeyBehavior );
}
void JockeyIntention::Update()
{
	VPROF_BUDGET( "JockeyIntention::Update", "NextBot" );
	if ( m_behavior )
		m_behavior->Update( m_actor, GetUpdateInterval() );
}
