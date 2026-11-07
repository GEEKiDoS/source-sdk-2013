//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "l4d2/infected/special/ability_charge.h"
#include "in_buttons.h"
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
LINK_ENTITY_TO_CLASS( ability_charge, CCharge );
#endif

ConVar z_charge_start_speed( "z_charge_start_speed", "250", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_charge_max_speed( "z_charge_max_speed", "500", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_charge_duration( "z_charge_duration", "2.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_charge_warmup( "z_charge_warmup", "0.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_charge_interval( "z_charge_interval", "12", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_charge_max_force( "z_charge_max_force", "800", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_charge_min_force( "z_charge_min_force", "550", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_charge_max_damage( "z_charge_max_damage", "10", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_charge_tooshort( "z_charge_tooshort", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );

IMPLEMENT_NETWORKCLASS_ALIASED( Charge, DT_Charge )
BEGIN_NETWORK_TABLE( CCharge, DT_Charge )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_isCharging ) ),
	RecvPropFloat( RECVINFO( m_chargeStartTime ) ),
	RecvPropEHandle( RECVINFO( m_hPotentialTarget ) ),
#else
	SendPropBool( SENDINFO( m_isCharging ) ),
	SendPropFloat( SENDINFO( m_chargeStartTime ), 0, SPROP_NOSCALE ),
	SendPropEHandle( SENDINFO( m_hPotentialTarget ) ),
#endif
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CCharge )
#ifdef CLIENT_DLL
	DEFINE_PRED_FIELD( m_isCharging, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_chargeStartTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
#endif
END_PREDICTION_DATA()

CCharge::CCharge() { m_hPotentialTarget = NULL; }
int CCharge::GetActivationButton() const { return IN_ATTACK; }
bool CCharge::IsActive() const { return m_isCharging; }
bool CCharge::IsReadyToActivate() { return m_hPotentialTarget.Get() != NULL; }

bool CCharge::CanActivate()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || owner->IsGhost() || owner->GetPummelVictim() || owner->GetPummelTarget() ) return false;
#ifndef CLIENT_DLL
	if ( owner->GetMoveType() == MOVETYPE_LADDER ) return false;
#endif
	return !m_isCharging && BaseClass::CanActivate();
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

// S361A90/C28B610. Do not replace this with time/warmup: the native
// numerator is time/charge_duration, clamped to [.5,1] BEFORE remapping.
float CCharge::GetMovementSpeed() const
{
	if ( !m_isCharging ) return -1.0f;
	const float warmup = z_charge_warmup.GetFloat();
	const float elapsed = ( gpGlobals->curtime - m_chargeStartTime ) / z_charge_duration.GetFloat();
	float fraction = 1.0f;
	if ( elapsed <= 1.0f )
	{
		fraction = elapsed;
		if ( elapsed < 0.5f ) fraction = 0.5f;
	}
	const float maximum = z_charge_max_speed.GetFloat();
	if ( warmup >= fraction )
	{
		const float start = z_charge_start_speed.GetFloat();
		if ( warmup == 0.0f )
		{
			if ( fraction < warmup ) return start;
		}
		else return ( ( maximum - start ) * fraction ) / warmup + start;
	}
	return maximum;
}
float CCharge::GetMaxSpeed() const { return m_isCharging ? z_charge_max_speed.GetFloat() : -1.0f; }

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CCharge::StartCharge()
{
	CTerrorPlayer *owner = GetOwner();
	m_isCharging = true;
	Q_memset( m_hitSurvivor, 0, sizeof( m_hitSurvivor ) );
	m_chargeStartPosition = owner->GetAbsOrigin();
	owner->AddFlag( FL_FROZEN );
	owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGE_START );
	m_probeTimer.Start( 0.1f );
	m_lastProbePosition.Init();
	m_hadVictim = false;
	IGameEvent *event = gameeventmanager->CreateEvent( "charger_charge_start" );
	if ( event )
	{
		CTerrorPlayer *eventOwner = GetOwner();
		event->SetInt( "userid", eventOwner ? eventOwner->GetUserID() : 0 );
		gameeventmanager->FireEvent( event );
	}
#ifndef CLIENT_DLL
	CTerrorPlayer *soundOwner = GetOwner(); // Native re-read after charge-start event.
	if ( soundOwner ) soundOwner->EmitInfectedSound( "ChargerZombie.Charge", 0.0f, 0.0f );
#endif
}

void CCharge::EndCharge()
{
	float interval = z_charge_interval.GetFloat();
	CTerrorPlayer *owner = GetOwner();
	if ( owner )
	{
		if ( !m_isCharging ) return;
		owner->RemoveFlag( FL_FROZEN );
		owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGE_END );
#ifndef CLIENT_DLL
		owner->ReleaseCarry( true, false, false );
#endif
		IGameEvent *event = gameeventmanager->CreateEvent( "charger_charge_end" );
		if ( event )
		{
			CTerrorPlayer *eventOwner = GetOwner();
			event->SetInt( "userid", eventOwner ? eventOwner->GetUserID() : 0 );
			gameeventmanager->FireEvent( event );
		}
		CTerrorPlayer *currentOwner = GetOwner(); // Native re-read after both carry/end events.
		if ( currentOwner )
		{
			CTerrorPlayer *victim = currentOwner->GetPummelVictim();
			if ( !victim ) victim = currentOwner->GetPummelTarget();
			if ( victim )
			{
#ifdef CLIENT_DLL
				interval = victim->pl.deadflag ? 0.2f : 3600.0f;
#else
				interval = victim->IsAlive() ? 3600.0f : 0.2f;
#endif
			}
		}
	}
	SetRechargeTime( interval, 0.0f );
	m_isCharging = false;
}

bool CCharge::ActivateAbility()
{
	if ( !CanActivate() ) return false;
	CTerrorPlayer *owner = GetOwner();
	if ( owner->IsIncapacitated() || !owner->m_stunTimer.IsElapsed() ||
		( !owner->GetGroundEntity() && owner->GetMoveType() != MOVETYPE_LADDER ) ) return false;
	StartCharge();
	m_chargeStartTime = gpGlobals->curtime;
	SetRechargeTime( 3600.0f, 0.0f );
#ifndef CLIENT_DLL
	IGameEvent *event = gameeventmanager->CreateEvent( "ability_use" );
	if ( event )
	{
		event->SetInt( "userid", owner->GetUserID() );
		event->SetString( "ability", GetClassname() );
		event->SetInt( "context", 0 );
		gameeventmanager->FireEvent( event );
	}
#endif
	BaseClass::ActivateAbility();
	return true;
}

void CCharge::SetOwner( CTerrorPlayer *owner )
{
	EndCharge();
	BaseClass::SetOwner( owner );
	SetRechargeTime( 0.2f, 0.0f );
	m_collisionTimer.Start( 0.0f );
	m_chargeStartTime = gpGlobals->curtime;
}
void CCharge::StopActivation( float duration )
{
	if ( m_isCharging )
	{
		EndCharge();
		m_chargeStartTime = 0.0f;
	}
	SuppressActivation( duration );
}
void CCharge::OnPummelEnded() { SetRechargeTime( 0.2f, 0.0f ); }
// Original361430/28B470 genuinely empty, unlike CTongue's injury callback.
void CCharge::OnOwnerDamaged( const CTakeDamageInfo &info ) {}
#ifdef CLIENT_DLL
// C_Charge slot174 is the inherited/update nullsub28B490, not Activate28BC90.
void CCharge::Update() {}
// C28B4E0, the real HUD virtual consumed by CHudTerrorCrosshair.
float CCharge::GetRechargeProgress() const
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || m_isCharging || owner->GetPummelVictim() || owner->GetPummelTarget() ) return 0.0f;
	return BaseClass::GetRechargeProgress();
}
const char *CCharge::GetCooldownBackground() const { return "HUD/PZ_charge_bg"; }
const char *CCharge::GetCooldownForeground() const { return "HUD/pz_charge_charger_charge"; }
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CCharge )
END_DATADESC()
#endif
