//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/tank.h"
#include "l4d2/infected/boss/tank_intention.h"
#include "l4d2/infected/boss/tank_actions.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
TankIntention::TankIntention( Tank *actor ) : IIntention( actor ), m_actor( actor ), m_behavior( NULL ) // S:46A730
{
	Reset();
}
TankIntention::~TankIntention() // S:46A790
{
	delete m_behavior;
}
void TankIntention::Reset() // S:471FA0
{
	m_actor = static_cast< Tank * >( GetBot()->GetEntity() );
	delete m_behavior;
	m_behavior = new Behavior< Tank >( new TankBehavior );
}
void TankIntention::Update() // S:472040
{
	if ( m_behavior ) m_behavior->Update( m_actor, GetUpdateInterval() );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
