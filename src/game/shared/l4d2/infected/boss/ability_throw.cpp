//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/ability_throw.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/terror_shareddefs.h"
#include "studio.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/boss/tank_rock.h"
#include "bone_setup.h"
#include "igameevents.h"
#include "datacache/imdlcache.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
LINK_ENTITY_TO_CLASS( ability_throw, CThrow );
#endif

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar z_tank_throw_interval( "z_tank_throw_interval", "5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "How often a tank can throw." ); // S:5A0010
ConVar z_tank_throw_fail_interval( "z_tank_throw_fail_interval", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "How soon a tank can try again after failing to throw." ); // S:5A0040
#ifndef CLIENT_DLL
ConVar tank_rock_overhead_percent( "tank_rock_overhead_percent", "100", 
#if defined( GAME_DLL )
	0x00000006
#else
	0
#endif
, "Percent of time the tank throws overhand (0-100)" ); // S:5A0100
ConVar z_tank_throw_force( "z_tank_throw_force", "800", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 ); // S:593B30
#endif

IMPLEMENT_NETWORKCLASS_ALIASED( Throw, DT_AbilityThrow )
BEGIN_NETWORK_TABLE( CThrow, DT_AbilityThrow ) // S:367460/C:28E170: base-only.
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CThrow )
END_PREDICTION_DATA()
#ifndef CLIENT_DLL
LINK_ENTITY_TO_CLASS( ability_throw, CThrow );
BEGIN_DATADESC( CThrow ) // S:368F20: no saved fields.
END_DATADESC()
#endif

// S:367A50/C:28E2E0: timers invalidate themselves; waiting flag has no ctor write.
CThrow::CThrow()
{
#ifndef CLIENT_DLL
	m_rock = NULL;
#endif
}
void CThrow::Precache() // S:367510/C:28E180
{
	PrecacheScriptSound( "HulkZombie.Throw.Fail" );
	PrecacheScriptSound( "HulkZombie.Throw.Pickup" );
	PrecacheScriptSound( "HulkZombie.Throw" );
}
void CThrow::SetOwner( CTerrorPlayer *owner ) // S:3672B0/C:28E1B0
{
	SetRechargeTime( 0.0f, 0.0f );
#ifndef CLIENT_DLL
	m_waitingForThrowAnimation = false;
#endif
	BaseClass::SetOwner( owner );
}
bool CThrow::CanActivate() // S:367540/C:28E290
{
	CTerrorPlayer *owner = GetOwner();
	return owner && !owner->IsGhost() && BaseClass::CanActivate();
}
bool CThrow::IsReadyToActivate() { return true; } // S:367330/C:28E1E0
bool CThrow::IsActive() const { return gpGlobals->curtime < m_throwTimer.m_timestamp; } // S:367700/C:28E250

bool CThrow::ActivateAbility() // S:367B60/C:28E390
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return false;
	if ( !CanActivate() || !owner->GetGroundEntity() )
	{
		owner->EmitSound( "HulkZombie.Throw.Fail" );
		return false;
	}
	if ( !owner->CanAttack() )
	{
		owner->EmitSound( "HulkZombie.Throw.Fail" );
		if ( z_tank_throw_fail_interval.GetFloat() > 0.0f ) SetRechargeTime( z_tank_throw_fail_interval.GetFloat(), 0.0f );
		return false;
	}
	m_viewmodelAttackTimer.Start( 0.7f );
#ifndef CLIENT_DLL
	m_createRockTimer.Start( 1.0f );
	m_releaseRockTimer.Start( 1.0f );
	if ( m_rock.Get() ) UTIL_Remove( m_rock.Get() );
	m_rock = NULL;
	const int overheadPercent = tank_rock_overhead_percent.GetInt();
	const int roll = random->RandomInt( 1, 100 );
	owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_THROW, roll <= overheadPercent );
	m_waitingForThrowAnimation = true;
	SetRechargeTime( z_tank_throw_interval.GetFloat() + 1.0f, 0.0f );
	owner->EmitSound( "HulkZombie.Throw.Pickup" );
	IGameEvent *event = gameeventmanager->CreateEvent( "ability_use" );
	if ( event )
	{
		event->SetInt( "userid", owner->GetUserID() );
		event->SetString( "ability", GetClassname() );
		event->SetInt( "context", 0 );
		gameeventmanager->FireEvent( event );
	}
#endif
	// C:28E390 genuinely does NOT start m_throwTimer or publish animation 93.
	BaseClass::ActivateAbility();
	return true;
}

void CThrow::ClearThrow()
{
	m_viewmodelAttackTimer.Invalidate();
#ifndef CLIENT_DLL
	m_createRockTimer.Invalidate();
	m_releaseRockTimer.Invalidate();
	if ( m_rock.Get() ) UTIL_Remove( m_rock.Get() );
	m_rock = NULL;
	m_waitingForThrowAnimation = false;
#endif
	m_throwTimer.Invalidate();
}
#ifndef CLIENT_DLL
void CThrow::StopActivation( float duration ) // S:367D90; client inherits the genuine base nullslot.
{
	ClearThrow();
	if ( duration <= z_tank_throw_interval.GetFloat() ) duration = z_tank_throw_interval.GetFloat();
	SetRechargeTime( duration, 0.0f );
}

// S:369590 first block. Event cycles53/54 divide by CPS, not duration.
void CThrow::StartThrowAnimationTimers()
{
	CTerrorPlayer *owner = GetOwner();
	MDLCACHE_CRITICAL_SECTION();
	float duration = 2.0f;
	CStudioHdr *hdr = owner->GetModelPtr();
	if ( hdr && hdr->GetRenderHdr() && owner->GetSequence() >= 0 )
	{
		const int sequenceIndex = owner->GetSequence();
		mstudioseqdesc_t &sequence = hdr->pSeqdesc( sequenceIndex );
		float poseParameters[MAXSTUDIOPOSEPARAM] = { 0.0f };
		const float cyclesPerSecond = Studio_CPS( hdr, sequence, sequenceIndex, poseParameters );
		duration = static_cast< float >( Studio_Duration( hdr, sequenceIndex, poseParameters ) );
		for ( int i = 0; i < sequence.numevents; ++i )
		{
			const mstudioevent_t *event = sequence.pEvent( i );
			if ( event->event == 53 ) m_createRockTimer.Start( event->cycle / cyclesPerSecond );
			else if ( event->event == 54 ) m_releaseRockTimer.Start( event->cycle / cyclesPerSecond );
		}
	}
	m_throwTimer.Start( duration );
	m_waitingForThrowAnimation = false;
}

// S:3672E0: ignore combat characters before the ordinary owner/pass filter.
class CThrowTraceFilter : public CTraceFilterSimple
{
public:
	CThrowTraceFilter( CBaseEntity *owner ) : CTraceFilterSimple( owner, COLLISION_GROUP_NONE ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int contentsMask )
	{
		CBaseEntity *entity = EntityFromEntityHandle( handle );
		return !entity->MyCombatCharacterPointer() && BaseClass::ShouldHitEntity( handle, contentsMask );
	}
private:
	typedef CTraceFilterSimple BaseClass;
};

void CThrow::ReleaseRock() // S:369B1F..369CDF; clear handle BEFORE attachment and traces.
{
	CTerrorPlayer *owner = GetOwner();
	CTankRock *rock = m_rock.Get();
	m_rock = NULL;
	if ( rock )
	{
		Vector direction;
		owner->EyeVectors( &direction );
		Vector origin;
		QAngle angles;
		owner->GetAttachment( "debris", origin, angles );
		CThrowTraceFilter filter( owner );
		trace_t trace;
		UTIL_TraceHull( owner->WorldSpaceCenter(), origin, rock->WorldAlignMins(), rock->WorldAlignMaxs(), MASK_SOLID, &filter, &trace );
		if ( trace.fraction != 1.0f && trace.m_pEnt != rock ) origin = trace.endpos;
		AngularImpulse angularVelocity( 60.0f, static_cast< float >( random->RandomInt( -120, 120 ) ), 0.0f );
		Vector velocity = owner->GetAbsVelocity() + direction * z_tank_throw_force.GetFloat();
		rock->Launch( origin, angles, velocity, angularVelocity );
	}
	owner->EmitSound( "HulkZombie.Throw" );
}
#endif

void CThrow::Update() // S:369590/C:28E490
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
#ifndef CLIENT_DLL
	if ( m_waitingForThrowAnimation &&
		( owner->GetTerrorActivity() == TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TANK_OVERHEAD_THROW ) ||
		  owner->GetTerrorActivity() == TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_HULK_THROW ) ) )
		StartThrowAnimationTimers();
#endif
	if ( !( m_throwTimer.m_timestamp > 0.0f ) ) return;
#ifdef CLIENT_DLL
	if ( !owner->IsDormant() && owner->m_isIncapacitated )
#else
	if ( owner->IsIncapacitated() )
#endif
	{
		ClearThrow();
		return;
	}
	if ( owner->IsStaggering() )
	{
		ClearThrow();
		SetRechargeTime( z_tank_throw_interval.GetFloat(), 0.0f );
		return;
	}
	if ( m_viewmodelAttackTimer.m_timestamp > 0.0f && m_viewmodelAttackTimer.IsElapsed() )
	{
		m_viewmodelAttackTimer.Invalidate();
		CBaseCombatWeapon *weapon = owner->GetActiveWeapon();
		if ( weapon ) weapon->SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	}
#ifndef CLIENT_DLL
	if ( m_createRockTimer.m_timestamp > 0.0f && m_createRockTimer.IsElapsed() )
	{
		m_createRockTimer.Invalidate();
		m_rock = CTankRock::Create( owner->EyePosition(), owner->EyeAngles(), vec3_origin, owner );
	}
	if ( m_releaseRockTimer.m_timestamp > 0.0f && m_releaseRockTimer.IsElapsed() )
	{
		m_releaseRockTimer.Invalidate();
		ReleaseRock();
	}
#endif
	owner->SetStamina( 3000.0f );
	if ( gpGlobals->curtime >= m_throwTimer.m_timestamp )
	{
		owner->SetStamina( 0.0f );
		ClearThrow();
	}
}
#ifdef CLIENT_DLL
const char *CThrow::GetCooldownBackground() const { return "HUD/PZ_charge_bg"; } // C:28E1F0
const char *CThrow::GetCooldownForeground() const { return "HUD/PZ_charge_tank"; } // C:28E200
#endif

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x367270; 0x5c469c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace ability_throwPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "ability_throw", 0x5c469c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "ability_throw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
