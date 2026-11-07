//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/ability_selfdestruct.h"
#include "in_buttons.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

LINK_ENTITY_TO_CLASS( ability_selfdestruct, CSelfDestruct );
IMPLEMENT_NETWORKCLASS_ALIASED( SelfDestruct, DT_SelfDestruct )
BEGIN_NETWORK_TABLE( CSelfDestruct, DT_SelfDestruct )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_bHasBeenActivated ) ),
#else
	SendPropBool( SENDINFO( m_bHasBeenActivated ) ),
#endif
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CSelfDestruct )
#ifdef CLIENT_DLL
	DEFINE_PRED_FIELD( m_bHasBeenActivated, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
#endif
END_PREDICTION_DATA()

CSelfDestruct::CSelfDestruct()
{
	m_bHasBeenActivated = false;
	m_detonateTimer.Invalidate();
}

void CSelfDestruct::SetOwner( CTerrorPlayer *owner )
{
	BaseClass::SetOwner( owner );
	SetRechargeTime( 2.0f );
}

bool CSelfDestruct::CanActivate()
{
	CTerrorPlayer *owner = GetOwner();
	return !m_bHasBeenActivated && ( !owner || !owner->IsGhost() ) && BaseClass::CanActivate();
}

bool CSelfDestruct::ActivateAbility()
{
	m_bHasBeenActivated = true;
	m_detonateTimer.Start( 2.0f );
	CTerrorPlayer *owner = GetOwner();
	if ( owner )
		owner->AddFlag( FL_FROZEN );
#ifndef CLIENT_DLL
	IGameEvent *event = gameeventmanager->CreateEvent( "ability_use" );
	if ( event )
	{
		// S366000 re-resolves after CreateEvent; skip only a vanished owner's ID.
		owner = GetOwner();
		if ( owner )
			event->SetInt( "userid", owner->GetUserID() );
		event->SetString( "ability", GetClassname() );
		event->SetInt( "context", 0 );
		gameeventmanager->FireEvent( event );
	}
#endif
	return BaseClass::ActivateAbility();
}

void CSelfDestruct::Update()
{
#ifndef CLIENT_DLL
	if ( m_bHasBeenActivated && gpGlobals->curtime >= m_detonateTimer.m_timestamp )
	{
		CTerrorPlayer *owner = GetOwner();
		// S365E60's NULL TakeDamage receiver is undefined; no other work is skipped.
		if ( !owner )
			return;
		CTakeDamageInfo info( owner, owner, 1000.0f, 0 );
		info.SetDamageForce( Vector( 0, 0, 1 ) );
		owner->TakeDamage( info );
	}
#endif
}

int CSelfDestruct::GetActivationButton() const { return IN_ATTACK; }
bool CSelfDestruct::IsActive() const { return m_bHasBeenActivated; }
bool CSelfDestruct::CanActivateOnGround() const { return true; } // S365C90.
#ifdef CLIENT_DLL
float CSelfDestruct::GetRechargeProgress() const { return 1.0f; }
const char *CSelfDestruct::GetCooldownBackground() const { return "HUD/PZ_charge_bg"; }
// The pinned client deliberately uses the lunge icon for this Boomer variant.
const char *CSelfDestruct::GetCooldownForeground() const { return "HUD/PZ_charge_lunge"; }
#endif
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CSelfDestruct )
END_DATADESC()
#endif
