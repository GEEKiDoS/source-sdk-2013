//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/ability_vomit.h"
#include "in_buttons.h"
#include "rumble_shared.h"
#include "activitylist.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "SharedFunctorUtils.h"
#include "c_baseplayer.h"
#include "c_playerresource.h"
#include "c_rumble.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/effects/vomit_particle.h"
#include "te_effect_dispatch.h"
#include "ilagcompensationmanager.h"
#include "ipredictionsystem.h"
#include "l4d2/player/terror_lagcompensation.h"
#include "debugoverlay_shared.h"
extern ConVar z_vomit_vecrand;
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "l4d2/terror_trace_filter.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar z_vomit_interval( "z_vomit_interval", "30", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "How often a puker can puke." );
ConVar z_vomit_duration( "z_vomit_duration", "1.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "How long a puker continuously pukes for." );
ConVar z_vomit_fatigue( "z_vomit_fatigue", "3000", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Stamina impact of puking. High number will pin in place for a long time, lower will just slow." );
ConVar z_vomit_range( "z_vomit_range", "300", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_vomit_slide_rate( "z_vomit_slide_rate", "0.1", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Percentage of screen height per second" );
ConVar z_vomit_slide_mult( "z_vomit_slide_mult", "0.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Multiplier for second texture slide rate" );
ConVar z_vomit_debug( "z_vomit_debug", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Visualize the vomit damage." );

#ifndef CLIENT_DLL
LINK_ENTITY_TO_CLASS( ability_vomit, CVomit );
EXTERN_SEND_TABLE( DT_CountdownTimer );
#else
EXTERN_RECV_TABLE( DT_CountdownTimer );
#endif
IMPLEMENT_NETWORKCLASS_ALIASED( Vomit, DT_AbilityVomit )
BEGIN_NETWORK_TABLE( CVomit, DT_AbilityVomit )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_isSpraying ) ),
	RecvPropDataTable( RECVINFO_DT( m_attackDuration ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropDataTable( RECVINFO_DT( m_nextSpray ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
#else
	SendPropBool( SENDINFO( m_isSpraying ) ),
	SendPropDataTable( SENDINFO_DT( m_attackDuration ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropDataTable( SENDINFO_DT( m_nextSpray ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
#endif
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CVomit )
END_PREDICTION_DATA()

CVomit::CVomit()
{
	m_isSpraying = false;
#ifndef CLIENT_DLL
	ChangeTeam( TEAM_INFECTED );
#endif
}

#ifndef CLIENT_DLL
void CVomit::Precache()
{
	PrecacheScriptSound( "Vomit.Use" );
	PrecacheScriptSound( "Vomit.Cancel" );
	PrecacheEffect( "startvomit" );
	PrecacheEffect( "stopvomit" );
}
#endif

void CVomit::SetOwner( CTerrorPlayer *owner )
{
	SetRechargeTime( 0.0f );
	BaseClass::SetOwner( owner );
}

bool CVomit::CanActivate()
{
	CTerrorPlayer *owner = GetOwner();
	return owner && !owner->IsGhost() && !( owner->m_nButtons & IN_ATTACK2 ) &&
		!owner->IsStaggering() && owner->GetGroundEntity() && BaseClass::CanActivate();
}

bool CVomit::ActivateAbility()
{
	if ( !CanActivate() )
		return false;
	CTerrorPlayer *owner = GetOwner();
	if ( !owner )
		return false;
	m_isSpraying = true;
#ifndef CLIENT_DLL
	StartVomitEffect();
#endif
	m_attackDuration.Start( z_vomit_duration.GetFloat() );
	m_nextSpray.Start( 0.25f );
	SetRechargeTime( z_vomit_interval.GetFloat() );
	owner->EmitSound( "Vomit.Use" );
#ifdef CLIENT_DLL
	::RumbleEffect( 35, 0, RUMBLE_FLAG_RESTART );
#endif
	owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_FIRE_PRIMARY_ALT, 0 );
	CBaseCombatWeapon *weapon = owner->GetActiveWeapon();
	if ( weapon )
		weapon->SendWeaponAnim( (Activity)ActivityList_IndexForName( "ACT_VM_VOMIT" ) );
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
	Update();
	return true;
}

int CVomit::GetActivationButton() const { return IN_ATTACK; }
bool CVomit::IsActive() const { return m_isSpraying; }

#ifndef CLIENT_DLL
unsigned int CVomit::PhysicsSolidMaskForEntity() const
{
	return 0x02004003; // S370EF0, actual server virtual queried for every segment.
}


void CVomit::StartVomitEffect()
{
	CTerrorPlayer *owner = GetOwner();
	if ( owner )
	{
		CDisablePredictionFiltering disablePredictionFiltering;
		CEffectData data;
		data.m_nEntIndex = owner->entindex();
		data.m_vOrigin = owner->EyePosition();
		DispatchEffect( "startvomit", data );
	}
}

void CVomit::StopVomitEffect()
{
	CTerrorPlayer *owner = GetOwner();
	if ( owner )
	{
		CDisablePredictionFiltering disablePredictionFiltering;
		CEffectData data;
		data.m_nEntIndex = owner->entindex();
		data.m_vOrigin = owner->GetAbsOrigin();
		DispatchEffect( "stopvomit", data );
	}
}
#endif

void CVomit::Update()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || !m_isSpraying )
		return;
#ifdef CLIENT_DLL
	const bool expired = gpGlobals->curtime >= m_attackDuration.m_timestamp;
#else
	// S3728A0 tests the active arm with ordered '<'; unordered expires.
	const bool expired = !( static_cast< double >( gpGlobals->curtime ) < m_attackDuration.m_timestamp.Get() );
#endif
	if ( expired )
	{
#ifndef CLIENT_DLL
		owner->SetStamina( 0.0f );
#endif
		m_isSpraying = false;
#ifndef CLIENT_DLL
		StopVomitEffect();
#else
		::RumbleEffect( 35, 0, RUMBLE_FLAG_STOP );
#endif
		m_nextSpray.Invalidate();
		m_attackDuration.Invalidate();
		SetRechargeTime( z_vomit_interval.GetFloat() );
		return;
	}
#ifndef CLIENT_DLL
	owner->SetStamina( z_vomit_fatigue.GetFloat() );
	if ( gpGlobals->frametime == 0.0f )
		return;
	if ( owner->IsBot() )
	{
		if ( m_nextSpray.GetRemainingTime() <= 0.0f )
			CVomitParticle::Create( owner->EyePosition(), owner->EyeAngles(), owner, owner );
		return;
	}
	StartTerrorLagCompensation( owner, TERROR_LAG_COMPENSATE_BOUNDS, vec3_origin, vec3_angle, 0.0f );
	if ( m_nextSpray.GetRemainingTime() <= 0.0f )
	{
		Vector forward;
		AngleVectors( owner->EyeAngles(), &forward );
		Vector start = owner->EyePosition();
		Vector velocity = forward * 2100.0f;
		const float perturbationScale = z_vomit_vecrand.GetFloat();
		float minimum = perturbationScale * -2100.0f;
		float maximum = perturbationScale * 2100.0f;
		float range = maximum - minimum;
		Vector perturbation;
		perturbation.x = ( static_cast< float >( RandomInt( 0, 32767 ) ) * 0.000030518509f ) * range + minimum;
		perturbation.y = ( static_cast< float >( RandomInt( 0, 32767 ) ) * 0.000030518509f ) * range + minimum;
		perturbation.z = ( static_cast< float >( RandomInt( 0, 32767 ) ) * 0.000030518509f ) * range + minimum;
		velocity += perturbation;
		float lifetime = RandomFloat( 0.9f, 1.1f ) * 0.5f;
		Vector end = start + velocity * gpGlobals->frametime;
		float elapsed = gpGlobals->frametime;
		// S372C71 JA exits, S372E42 JBE repeats: unordered stays in the loop.
		while ( !( elapsed > lifetime ) )
		{
			velocity.x *= 0.86f;
			velocity.y *= 0.86f;
			velocity.z = velocity.z * 0.86f - 110.0f;
			if ( z_vomit_debug.GetBool() )
				NDebugOverlay::Line( start, end, 0, 255, 0, true, 5.0f );
			CTraceFilterSimpleList filterAll( COLLISION_GROUP_NONE );
			CTraceFilterSimpleNoInfectedTeamOrGhosts filterPlayers( COLLISION_GROUP_NONE );
			CTraceFilterSimpleList *filter = TerrorGameRules()->IsPlayerControlledZombies() ? &filterPlayers : &filterAll;
			filter->AddEntityToIgnore( this );
			filter->AddEntityToIgnore( owner );
			trace_t trace;
			UTIL_TraceLine( start, end, PhysicsSolidMaskForEntity(), filter, &trace );
			if ( trace.fraction < 1.0f && trace.m_pEnt )
			{
				if ( z_vomit_debug.GetBool() )
					NDebugOverlay::Line( start, end, 255, 100, 0, true, 10.0f );
				ApplyVomitHit( owner, trace.m_pEnt, NULL, owner->GetTeamNumber(), velocity, end );
				break;
			}
			start = end;
			end += velocity * gpGlobals->frametime;
			elapsed += gpGlobals->frametime;
		}
	}
	lagcompensation->FinishLagCompensation( owner );
#endif
}

#ifndef CLIENT_DLL
void CVomit::StopActivation( float duration )
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner )
		return;
	if ( !m_isSpraying )
	{
		SuppressActivation( duration );
		return;
	}
	owner->SetStamina( 0.0f );
	m_isSpraying = false;
	StopVomitEffect();
	m_nextSpray.Invalidate();
	m_attackDuration.Invalidate();
	SetRechargeTime( z_vomit_interval.GetFloat() );
	owner->EmitSound( "Vomit.Cancel" );
	// S372500 removes ALL native vomit particles, including other Boomers'.
	CBaseEntity *particle = NULL;
	while ( ( particle = gEntList.FindEntityByClassname( particle, "vomit_particle" ) ) != NULL )
		UTIL_Remove( particle );
}
#else
float CVomit::GetRechargeProgress() const
{
	return m_isSpraying ? 0.0f : BaseClass::GetRechargeProgress();
}
const char *CVomit::GetCooldownBackground() const { return "HUD/PZ_charge_bg"; }
const char *CVomit::GetCooldownForeground() const { return "HUD/PZ_charge_boomer"; }

bool CVomit::CollectPotentialTargets( CUtlVector< CTerrorPlayer * > *targets )
{
	if ( targets )
		targets->RemoveAll();
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || !CanActivate() || !targets )
		return false;
	TargetScan< CTerrorPlayer > scan( owner, TEAM_SURVIVOR,
		1.0f - z_vomit_slide_mult.GetFloat(), z_vomit_range.GetFloat(),
		z_vomit_range.GetFloat(), z_vomit_debug.GetBool() );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && !player->IsDormant() && g_PR->IsConnected( i ) )
			scan( player );
	}
	const CUtlVector< CTerrorPlayer * > &found = scan.GetAllTargets();
	FOR_EACH_VEC( found, i )
	{
		if ( !found[i]->IsDormant() )
			targets->AddToTail( found[i] );
	}
	return true;
}
#endif
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x370e60; 0x5c47a4 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace ability_vomitPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "ability_vomit", 0x5c47a4 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "ability_vomit", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CVomit )
END_DATADESC()
#endif
