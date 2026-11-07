//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/c_buttons.h"

#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_BaseToggle, DT_BaseToggle, CBaseToggle )
END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_DT( C_BaseButton, DT_BaseButton, CBaseButton )
	RecvPropEHandle( RECVINFO( m_glowEntity ) ),
	RecvPropBool( RECVINFO( m_usable ) ),
END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_DT( C_ButtonTimed, DT_ButtonTimed, CButtonTimed )
	RecvPropString( RECVINFO( m_sUseString ) ),
	RecvPropString( RECVINFO( m_sUseSubString ) ),
END_RECV_TABLE()

void C_BaseToggle::GetGroundVelocityToApply( Vector &vecGroundVel )
{
	// Native C06A650 deliberately suppresses LOCAL vertical velocity.
	const Vector &velocity = GetLocalVelocity();
	vecGroundVel.Init( velocity.x, velocity.y, 0.0f );
}

C_BaseButton::C_BaseButton()
{
	m_glowEntity = NULL;
}

int C_BaseButton::ObjectCaps()
{
	// Native C06A510 tests the real proxy, not merely the received bool.
	return GetUseRedirectEntity() ?
		BaseClass::ObjectCaps() | FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS :
		BaseClass::ObjectCaps();
}

C_BaseEntity *C_BaseButton::GetUseRedirectEntity()
{
	// Native C06A720, paired with server S0869D0.
	return m_usable ? m_glowEntity.Get() : NULL;
}

bool C_BaseButton::IsUsable( C_BaseEntity *pUser )
{
	// Native C06A500 is true even disabled or without a resolved glow handle.
	return true;
}

unsigned int C_BaseButton::PhysicsSolidMaskForEntity() const
{
	// Native C06A610: 0x0200 is a CONTENTS bit, not a capability bit.
	return BaseClass::PhysicsSolidMaskForEntity() | 0x0200;
}

C_ButtonTimed::C_ButtonTimed()
{
	V_memset( m_sUseString, 0, sizeof( m_sUseString ) );
	V_memset( m_sUseSubString, 0, sizeof( m_sUseSubString ) );
}
