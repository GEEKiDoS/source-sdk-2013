//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/base_ability.h"
#include "in_buttons.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
EXTERN_RECV_TABLE( DT_CountdownTimer );
#else
EXTERN_SEND_TABLE( DT_CountdownTimer );
#endif

IMPLEMENT_NETWORKCLASS_ALIASED( BaseAbility, DT_AbilityBase )
// S360790/C28A980: deliberately no inherited entity table.
BEGIN_NETWORK_TABLE_NOBASE( CBaseAbility, DT_AbilityBase )
#ifdef CLIENT_DLL
	RecvPropEHandle( RECVINFO( m_hOwnerEntity ) ),
	RecvPropBool( RECVINFO( m_hasBeenUsed ) ),
	RecvPropEHandle( RECVINFO( m_owner ) ),
	RecvPropDataTable( RECVINFO_DT( m_nextActivationTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropDataTable( RECVINFO_DT( m_activationSupressedTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
#else
	SendPropEHandle( SENDINFO( m_hOwnerEntity ) ),
	SendPropBool( SENDINFO( m_hasBeenUsed ) ),
	SendPropEHandle( SENDINFO( m_owner ) ),
	SendPropDataTable( SENDINFO_DT( m_nextActivationTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropDataTable( SENDINFO_DT( m_activationSupressedTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA_NO_BASE( CBaseAbility )
	// Original map 6E9A24 has exactly these two descriptors. Suppression and
	// owner are received, not independently predicted fields.
	DEFINE_PRED_FIELD( m_hasBeenUsed, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
#ifndef NO_ENTITY_PREDICTION
	{ FIELD_EMBEDDED, "m_nextActivationTimer", { (int)_hacky_datamap_offsetof( classNameTypedef, m_nextActivationTimer ), 0 },
		1, FTYPEDESC_SAVE | FTYPEDESC_KEY, NULL, NULL, NULL, &CountdownTimer::m_PredMap },
#else
	DEFINE_FIELD_NULL,
#endif
END_PREDICTION_DATA()
#endif
#ifndef CLIENT_DLL
BEGIN_DATADESC( CBaseAbility )
	DEFINE_FIELD( m_owner, FIELD_EHANDLE ),
END_DATADESC()
#endif

CBaseAbility::CBaseAbility()
{
#ifdef CLIENT_DLL
	SetPredictionEligible( true ); // C28B193, before the constructor-body state reset.
#endif
	m_activationSupressedTimer.Invalidate();
	m_owner = NULL;
	m_nextActivationTimer.Invalidate();
	m_hasBeenUsed = false;
}

CTerrorPlayer *CBaseAbility::GetOwner() const
{
	return m_owner.Get();
}

#ifndef CLIENT_DLL
void CBaseAbility::RebindOwner( CTerrorPlayer *owner )
{
	// S361030: transfer preserves concrete state; do not call SetOwner's
	// family reset callback here.
	m_owner = owner;
	SetOwnerEntity( owner );
}
#endif

void CBaseAbility::SetOwner( CTerrorPlayer *owner )
{
	// S360FD0/C28AF70. Factory creation already set the engine owner.
	m_owner = owner;
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
bool CBaseAbility::CanActivate()
{
	// COMISS/JBE skips suppression for an unordered timestamp, while the
	// positive-timestamp FCOMIP/JB rejects an unordered current time.
	if ( m_activationSupressedTimer.m_timestamp > 0.0f &&
		!( static_cast< double >( gpGlobals->curtime ) >= m_activationSupressedTimer.m_timestamp.Get() ) )
		return false;
	CTerrorPlayer *owner = GetOwner();
	if ( owner && owner->IsIncapacitated() )
		return false;
	// Native creation/ownership guarantees an owner before activation. In
	// particular, a detached entity is not an always-ready fallback ability.
	const Activity activity = owner->GetTerrorActivity();
	if ( CTerrorPlayer::IsAttackBlockingActivity( activity ) || CTerrorPlayer::IsClimbingActivity( activity ) )
		return false;
	return gpGlobals->curtime >= m_nextActivationTimer.m_timestamp;
}

bool CBaseAbility::ActivateAbility()
{
	m_hasBeenUsed = true;
	return true;
}

bool CBaseAbility::TryActivate()
{
	return CanActivate() && ActivateAbility();
}

void CBaseAbility::SuppressActivation( float duration )
{
	const double expiry = static_cast< double >( gpGlobals->curtime ) + duration;
	if ( m_activationSupressedTimer.m_timestamp.Get() != expiry )
		m_activationSupressedTimer.m_timestamp.GetForModify() = static_cast< float >( expiry );
	m_activationSupressedTimer.m_duration = duration;
}

bool CBaseAbility::IsActivationSuppressed() const
{
	return m_activationSupressedTimer.m_timestamp > 0.0f && !( gpGlobals->curtime >= m_activationSupressedTimer.m_timestamp );
}

void CBaseAbility::SetRechargeTime( float recharge, float elapsed )
{
	if ( elapsed == 0.0f )
	{
		const double expiry = static_cast< double >( gpGlobals->curtime ) + recharge;
		if ( m_nextActivationTimer.m_timestamp.Get() != expiry )
			m_nextActivationTimer.m_timestamp.GetForModify() = static_cast< float >( expiry );
		m_nextActivationTimer.m_duration = recharge;
	}
	else
	{
		// S360E00/C28AFA0: duration is the second argument and the apparently
		// cancelling subtraction/addition is retained for float parity.
		const float remaining = gpGlobals->curtime - elapsed;
		const float next = remaining + recharge;
		m_nextActivationTimer.m_timestamp = next + elapsed;
		m_nextActivationTimer.m_duration = elapsed;
	}
}

double CBaseAbility::GetActivationTimerRemaining() const
{
	// S360BA0/C28AE30 have no float spill before returning ST0.
	return static_cast< double >( m_nextActivationTimer.m_timestamp.Get() ) - gpGlobals->curtime;
}

float CBaseAbility::GetRechargeDuration() const
{
	return m_nextActivationTimer.m_timestamp > 0.0f ? m_nextActivationTimer.m_duration : 0.0f;
}

#ifdef CLIENT_DLL
float CBaseAbility::GetRechargeProgress() const
{
	// C28AE50/EF88: direct real countdown/clock reads, not virtual wrappers.
	// The first remaining-time comparison is WIDE; the second difference,
	// division and subtraction each spill F32, including the return.
	const double now = static_cast< double >( gpGlobals->curtime );
	if ( static_cast< double >( static_cast< float >( m_nextActivationTimer.m_timestamp ) ) - now > 0.0 )
	{
		const double secondNow = static_cast< double >( gpGlobals->curtime );
		const double remainingWide =
			static_cast< double >( static_cast< float >( m_nextActivationTimer.m_timestamp ) ) - secondNow;
		const float timestamp = m_nextActivationTimer.m_timestamp;
		const float remaining = static_cast< float >( remainingWide );
		const float duration = timestamp > 0.0f ? static_cast< float >( m_nextActivationTimer.m_duration ) : 0.0f;
		const float fraction = remaining / duration;
		return 1.0f - fraction;
	}
	return 1.0f;
}
#endif

int CBaseAbility::GetActivationButton() const { return IN_ATTACK2; }
bool CBaseAbility::IsActive() const { return false; }
bool CBaseAbility::IsReadyToActivate() { return false; }
Vector *CBaseAbility::GetQueuedMovement( bool markChanged ) { return NULL; }
float CBaseAbility::GetMovementSpeed() const { return -1.0f; }
float CBaseAbility::GetMaxSpeed() const { return -1.0f; }
float CBaseAbility::GetFrictionMultiplier() const { return 1.0f; }
bool CBaseAbility::CanJump() const { return true; }
// S3606B0/C28AB30 is the genuine true policy inherited by every family.
bool CBaseAbility::CanActivateOnGround() const { return true; }
#ifdef _MSC_VER
#pragma float_control( pop )
#endif
// S3605A0..620/C28AA90..AB70 are genuine base defaults; concrete family
// activation, movement, injury and interruption implementations override them.
void CBaseAbility::OnButtonReleased() {}
void CBaseAbility::Update() {}
void CBaseAbility::OnTouch( CBaseEntity *other ) {}
void CBaseAbility::OnOwnerJumped() {}
void CBaseAbility::OnOwnerLanded() {}
void CBaseAbility::OnOwnerDamaged( const CTakeDamageInfo &info ) {}
void CBaseAbility::StopActivation( float duration ) {}
void CBaseAbility::OnRemove() {}
bool CBaseAbility::OnPlayerMoveCollision( CBaseEntity *hit, const Vector &hitPosition,
	const Vector &direction, const trace_t &trace, CMoveData *move )
{
	return false;
}

#ifdef CLIENT_DLL
bool CBaseAbility::ShouldPredict()
{
	CTerrorPlayer *owner = GetOwner();
	return owner && owner->IsLocalPlayer();
}
const char *CBaseAbility::GetCooldownBackground() const { return "sprites/Timers/lunge_clock"; }
const char *CBaseAbility::GetCooldownForeground() const { return "sprites/Timers/lunge_clock_fill"; }
#else
CBaseAbility *CBaseAbility::CreateForPlayer( CTerrorPlayer *owner )
{
	if ( !owner || owner->GetTeamNumber() != TEAM_INFECTED )
		return NULL;
	const char *classname;
	switch ( owner->GetZombieClass() )
	{
	case 1: classname = "ability_tongue"; break;
	case 2: classname = owner->m_nVariantType == 1 ? "ability_selfdestruct" : "ability_vomit"; break;
	case 3: classname = "ability_lunge"; break;
	case 4: classname = "ability_spit"; break;
	case 5: classname = "ability_leap"; break;
	case 6: classname = "ability_charge"; break;
	case 8: classname = "ability_throw"; break;
	default: return NULL;
	}
	CBaseAbility *ability = static_cast<CBaseAbility *>( CBaseEntity::Create( classname, owner->EyePosition(), owner->GetAbsAngles(), owner ) );
	if ( ability )
		ability->SetOwner( owner );
	return ability;
}

int CBaseAbility::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_FULLCHECK );
}

int CBaseAbility::ShouldTransmit( const CCheckTransmitInfo *info )
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner )
		return FL_EDICT_DONTSEND;
	// The target engine supports only slot 0. The original split-owner
	// relationship branch is unreachable here (Main-approved adaptation).
	if ( info->m_pClientEnt == owner->edict() )
		return FL_EDICT_ALWAYS;
	CTerrorPlayer *recipient = ToTerrorPlayer( CBaseEntity::Instance( info->m_pClientEnt ) );
	return recipient && recipient->GetTeamNumber() == owner->GetTeamNumber() ? FL_EDICT_ALWAYS : FL_EDICT_DONTSEND;
}

void CBaseAbility::RemoveAbility()
{
	UTIL_Remove( this );
}

void CBaseAbility::UpdateOnRemove()
{
	OnRemove();
	BaseClass::UpdateOnRemove();
}
#endif
