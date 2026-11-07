//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_chainsaw.h"
#include "baseviewmodel_shared.h"
#ifdef CLIENT_DLL
#include "animationlayer.h"
#else
#include "BaseAnimatingOverlay.h"
#endif
#include "activitylist.h"
#include "soundenvelope.h"
#ifdef CLIENT_DLL
#include "c_recipientfilter.h"
#else
#include "recipientfilter.h"
#endif
#include "rumble_shared.h"
#include "l4d2/gamerules/terror_gamerules.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "c_te_effect_dispatch.h"
#include "particle_parse.h"
#include "particle_property.h"
#include "prediction.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "NextBot/NextBot.h"
#include "NextBot/NextBotManager.h"
#include "takedamageinfo.h"
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/infected/boss/witch.h"
#include "debugoverlay_shared.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#if defined( CLIENT_DLL )
#include "l4d2/content/terror_precache_client.h"
#endif
#include "l4d2/terror_trace_filter.h"
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( Chainsaw, DT_WeaponChainsaw )
BEGIN_NETWORK_TABLE( CChainsaw, DT_WeaponChainsaw )
#ifdef CLIENT_DLL
	RecvPropInt( RECVINFO( m_iState ) ),
	RecvPropBool( RECVINFO( m_bHitting ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_PoseParameterTargets ), RecvPropFloat( RECVINFO( m_PoseParameterTargets[0] ) ) ),
#else
	SendPropInt( SENDINFO( m_iState ), 32 ),
	SendPropBool( SENDINFO( m_bHitting ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_PoseParameterTargets ), SendPropFloat( SENDINFO_ARRAY( m_PoseParameterTargets ), 32, SPROP_NOSCALE ) ),
#endif
END_NETWORK_TABLE()

// C:0x6EF634 is sentinel-only. Inherited prediction is retained; none of the
// chainsaw's authoritative state, contact or fuel accumulators is added here.
BEGIN_PREDICTION_DATA( CChainsaw )
END_PREDICTION_DATA()
#ifndef CLIENT_DLL
BEGIN_DATADESC( CChainsaw )
END_DATADESC()
#endif
LINK_ENTITY_TO_CLASS( weapon_chainsaw, CChainsaw );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_chainsaw );
#endif

#ifndef CLIENT_DLL
static ConVar chainsaw_attract_distance( "chainsaw_attract_distance", "500.0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
static ConVar chainsaw_attack_cone( "chainsaw_attack_cone", "30.0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
static ConVar chainsaw_attack_force( "chainsaw_attack_force", "400.0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
static ConVar chainsaw_attack_distance( "chainsaw_attack_distance", "50.0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
static ConVar chainsaw_hit_interval( "chainsaw_hit_interval", "0.1", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
static ConVar chainsaw_startup_fadeout_time( "chainsaw_startup_fadeout_time", "0.1", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
static ConVar chainsaw_damage( "chainsaw_damage", "100.0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
#endif
static ConVar chainsaw_debug( "chainsaw_debug", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );

static Activity ChainsawActivity( const char *name )
{
	return (Activity)ActivityList_IndexForName( name );
}

CChainsaw::CChainsaw()
{
	m_bDeployed = false;
	m_iState = INACTIVE;
	m_bHitting = false;
	m_flAmmoUseAccum = 0.0f;
	m_flHitIntervalAccum = 0.0f;
	m_pStartSound = NULL;
	m_pIdleSound = NULL;
	m_pActiveSound = NULL;
	m_pStopSound = NULL;
	m_nPitch = 100;
	m_flNextGoreSound = 0.0f;
	for ( int i = 0; i < 2; ++i )
	{
		m_PoseParameters[i] = 0.0f;
		m_PoseParameterTargets.Set( i, 0.0f );
		m_nPoseParameterIndices[i] = -1;
	}
	m_pitchDownTimer.Invalidate();
#ifndef CLIENT_DLL
	m_usedTimer.Invalidate();
#else
	m_bloodDecalTimer.Invalidate();
#endif
}

CChainsaw::~CChainsaw()
{
	StopAllSounds();
#ifdef CLIENT_DLL
	RemoveParticles();
#endif
}

void CChainsaw::Spawn()
{
	BaseClass::Spawn(); // S:0x3C95E0 is exactly the base Spawn thunk.
}

void CChainsaw::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Chainsaw.Start" );
	PrecacheScriptSound( "Chainsaw.FullThrottle" );
	PrecacheScriptSound( "Chainsaw.Stop" );
	PrecacheScriptSound( "Chainsaw.Die" );
	PrecacheScriptSound( "Chainsaw.Gore" );
	// Client particle resources belong to the native global C_Chainsaw precacher.
}

void CChainsaw::CacheOwnerAndViewModel()
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	m_hChainsawOwner = owner;
	CBaseViewModel *viewModel = owner ? owner->GetViewModel( m_nViewModelIndex ) : NULL;
	m_hChainsawViewModel = viewModel;
	if ( viewModel )
	{
		if ( m_nPoseParameterIndices[0] == -1 )
			m_nPoseParameterIndices[0] = viewModel->LookupPoseParameter( "engine_on" );
		if ( m_nPoseParameterIndices[1] == -1 )
			m_nPoseParameterIndices[1] = viewModel->LookupPoseParameter( "attack" );
	}
}

void CChainsaw::StopSound( CSoundPatch *&sound, float fadeTime )
{
	if ( !sound )
		return;
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	if ( fadeTime > 0.0f )
		controller.SoundFadeOut( sound, fadeTime, true );
	else
		controller.SoundDestroy( sound );
	sound = NULL;
}

void CChainsaw::StartSound( CSoundPatch *&sound, const char *name )
{
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( !owner )
		return;
	StopSound( sound );
#ifdef CLIENT_DLL
	CPASFilter filter( owner->GetAbsOrigin() );
#else
	CPASAttenuationFilter filter( owner, 0.8f );
#endif
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	sound = controller.SoundCreate( filter, entindex(), name );
	controller.Play( sound, 1.0f, 100.0f, 0.0f );
}

void CChainsaw::StopAllSounds()
{
	StopSound( m_pStartSound );
	StopSound( m_pIdleSound );
	StopSound( m_pActiveSound );
	StopSound( m_pStopSound );
}

void CChainsaw::DisplayDebug( bool serverLane )
{
	int row = serverLane ? 5 : 20;
#ifdef CLIENT_DLL
	engine->Con_NPrintf( row++, "-------------- CLIENT (%d)frame %d --------------", entindex(), gpGlobals->framecount );
#else
	engine->Con_NPrintf( row++, "-------------- SERVER (%d)frame %d --------------", entindex(), gpGlobals->framecount );
#endif
	static const char *states[] = { "INACTIVE", "STARTUP", "IDLE", "ACTIVE" };
	if ( m_iState >= INACTIVE && m_iState <= ACTIVE )
		engine->Con_NPrintf( row++, "State: %s", states[m_iState] );
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
#ifndef CLIENT_DLL
	if ( owner )
		engine->Con_NPrintf( row++, "Owner: %s (%d)", owner->GetDebugName(), owner->entindex() );
	else
		engine->Con_NPrintf( row++, "Owner: NONE" );
#endif
	engine->Con_NPrintf( row++, "PP_IDLE: %2.2f\n", m_PoseParameters[0] );
	engine->Con_NPrintf( row++, "PP_ATTACK: %2.2f\n", m_PoseParameters[1] );
#ifndef CLIENT_DLL
	engine->Con_NPrintf( row++, "Deployed: %s", m_bDeployed ? "YES" : "NO" );
	engine->Con_NPrintf( row++, "Visible: %s", IsWeaponVisible() ? "YES" : "NO" );
#endif
	engine->Con_NPrintf( row++, "Hitting: %s", m_bHitting ? "YES" : "NO" );
#ifdef CLIENT_DLL
	if ( owner )
		engine->Con_NPrintf( row++, "Owner: %s (%d)", owner->GetDebugName(), owner->entindex() );
	else
		engine->Con_NPrintf( row++, "Owner: NONE" );
#else
	engine->Con_NPrintf( row++, "AmmoUseAccum: %2.2f", m_flAmmoUseAccum );
	engine->Con_NPrintf( row++, "HitIntervalAccum: %2.2f", m_flHitIntervalAccum );
	engine->Con_NPrintf( row++, "Pitch: %d%%", m_nPitch );
	engine->Con_NPrintf( row++, "PitchDownTimer: %2.2f\n",
		static_cast< double >( m_pitchDownTimer.m_timestamp ) - gpGlobals->curtime );
#endif
	CBaseViewModel *viewModel = m_hChainsawViewModel.Get();
	if ( !viewModel )
		return;
	static const int layers[] = { 0, 2 };
	static const char *prefixes[] = { "FIRE_LAYER", "FIDGET_LAYER" };
	for ( int i = 0; i < ARRAYSIZE( layers ); ++i, ++row )
	{
		if ( layers[i] >= viewModel->GetNumAnimOverlays() )
			continue;
		CAnimationLayer *layer = viewModel->GetAnimOverlay( layers[i] );
#ifdef CLIENT_DLL
		if ( layer->m_flWeight <= 0.0f )
			engine->Con_NPrintf( row, "%s: --", prefixes[i] );
		else
			engine->Con_NPrintf( row, "%s: %s (c%2.2f, w%2.2f)", prefixes[i],
				viewModel->GetSequenceName( layer->m_nSequence ), layer->m_flCycle, layer->m_flWeight );
#else
		// S3C9E7D/3C9ECF omit the argument for their stray %c. The trailing
		// conversions consequently read undefined varargs; retain only defined text.
		engine->Con_NPrintf( row, "%s: %s", prefixes[i], viewModel->GetSequenceName( layer->m_nSequence ) );
#endif
	}
}

bool CChainsaw::Deploy()
{
	CacheOwnerAndViewModel();
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( owner )
	{
		if ( m_iClip1 > 0 )
		{
			m_flAmmoUseAccum = 0.0f;
			m_PoseParameterTargets.Set( 0, 1.0f );
			m_PoseParameterTargets.Set( 1, 0.0f );
			owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHAINSAW_STARTUP, 0 );
			StartSound( m_pStartSound, "Chainsaw.Start" );
			m_iState = STARTUP;
			m_bDeployed = true;
#ifndef CLIENT_DLL
			owner->RumbleEffect( RUMBLE_CHAINSAW_START, 0, RUMBLE_FLAG_RESTART );
			if ( Q_stricmp( TerrorGameRules()->GetActiveGameMode(), "chainsaws" ) != 0 )
				owner->RumbleEffect( RUMBLE_CHAINSAW_IDLE, 0, RUMBLE_FLAG_LOOP );
#endif
		}
		else
		{
			m_PoseParameterTargets.Set( 0, 0.0f );
			m_PoseParameterTargets.Set( 1, 0.0f );
			m_iState = INACTIVE;
		}
	}
	return BaseClass::Deploy();
}

bool CChainsaw::Holster( CBaseCombatWeapon *switchingTo )
{
	CacheOwnerAndViewModel();
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( owner )
	{
		m_PoseParameterTargets.Set( 0, 0.0f );
		m_PoseParameterTargets.Set( 1, 0.0f );
		if ( m_iState != INACTIVE )
		{
			StopSound( m_pStartSound );
			StopSound( m_pIdleSound );
			StopSound( m_pActiveSound );
			if ( m_iState != STARTUP )
				StartSound( m_pStopSound, "Chainsaw.Stop" );
#ifndef CLIENT_DLL
			owner->RumbleEffect( RUMBLE_CHAINSAW_START, 0, RUMBLE_FLAG_STOP );
			owner->RumbleEffect( RUMBLE_CHAINSAW_IDLE, 0, RUMBLE_FLAG_STOP );
			owner->RumbleEffect( RUMBLE_CHAINSAW_ACTIVE, 0, RUMBLE_FLAG_STOP );
#endif
			m_iState = INACTIVE;
		}
		m_bHitting = false;
		m_bDeployed = false;
#ifdef CLIENT_DLL
		UpdateParticles();
#endif
	}
	return BaseClass::Holster( switchingTo );
}

void CChainsaw::Drop( const Vector &velocity )
{
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( owner )
	{
		StopAllSounds();
#ifndef CLIENT_DLL
		owner->RumbleEffect( RUMBLE_CHAINSAW_IDLE, 0, RUMBLE_FLAG_STOP );
		owner->RumbleEffect( RUMBLE_CHAINSAW_ACTIVE, 0, RUMBLE_FLAG_STOP );
#endif
	}
	BaseClass::Drop( velocity );
}

void CChainsaw::UpdateOnRemove()
{
	StopAllSounds();
#ifdef CLIENT_DLL
	RemoveParticles();
#endif
	BaseClass::UpdateOnRemove();
}

#pragma float_control( precise, on, push )
void CChainsaw::UpdatePoseParameters()
{
	if ( !m_hChainsawOwner.Get() )
		return;
	CBaseViewModel *viewModel = m_hChainsawViewModel.Get();
	static const float rates[2] = { 1.0f, 8.0f };
	for ( int i = 0; i < 2; ++i )
	{
		const float target = m_PoseParameterTargets[i];
		float current = m_PoseParameters[i];
		if ( target != current )
		{
			const float step = rates[i] * gpGlobals->frametime;
			if ( target > current )
			{
				current += step;
				if ( current > target )
					current = target;
			}
			else
			{
				current -= step;
				if ( target > current )
					current = target;
			}
			m_PoseParameters[i] = current;
		}
		if ( viewModel )
			viewModel->SetPoseParameter( m_nPoseParameterIndices[i], m_PoseParameters[i] );
	}
}
#pragma float_control( pop )

void CChainsaw::PrimaryAttack()
{
	if ( !IsWeaponVisible() || m_iState == INACTIVE )
		return;
	CacheOwnerAndViewModel();
#ifndef CLIENT_DLL
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( owner && m_iState == IDLE )
	{
		StopSound( m_pIdleSound );
		StopSound( m_pActiveSound );
		StartSound( m_pActiveSound, "Chainsaw.FullThrottle" );
		m_PoseParameterTargets.Set( 1, 1.0f );
		m_flHitIntervalAccum = 0.0f;
		m_iState = ACTIVE;
		owner->RumbleEffect( RUMBLE_CHAINSAW_IDLE, 0, RUMBLE_FLAG_STOP );
		owner->RumbleEffect( RUMBLE_CHAINSAW_ACTIVE, 0, RUMBLE_FLAG_LOOP );
		m_usedTimer.Start( 2.0f );
	}
#endif
}

void CChainsaw::EnterIdle()
{
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( !owner )
		return;
	StopSound( m_pActiveSound );
	StartSound( m_pIdleSound, "Chainsaw.Idle" );
	StartSound( m_pStopSound, "Chainsaw.Stop" );
	m_PoseParameterTargets.Set( 1, 0.0f );
	m_iState = IDLE;
	m_bHitting = false;
#ifndef CLIENT_DLL
	if ( Q_stricmp( TerrorGameRules()->GetActiveGameMode(), "chainsaws" ) != 0 )
		owner->RumbleEffect( RUMBLE_CHAINSAW_IDLE, 0, RUMBLE_FLAG_LOOP );
	owner->RumbleEffect( RUMBLE_CHAINSAW_ACTIVE, 0, RUMBLE_FLAG_STOP );
	m_usedTimer.Invalidate();
#endif
}

void CChainsaw::WeaponIdle()
{
#ifndef CLIENT_DLL
	if ( m_iState == ACTIVE )
		EnterIdle();
#endif
}

void CChainsaw::SecondaryAttack()
{
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( !owner )
		return;
#ifndef CLIENT_DLL
	if ( m_iState == ACTIVE )
		EnterIdle();
#endif
	if ( owner->IsShoveAvailable() )
		SendWeaponAnim( ACT_VM_SECONDARYATTACK );
	BaseClass::SecondaryAttack();
}

Activity CChainsaw::GetDeployActivity()
{
	return m_iClip1 <= 0 ? ChainsawActivity( "ACT_VM_CANCEL" ) : ACT_VM_DRAW;
}

Activity CChainsaw::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP
		? ChainsawActivity( "ACT_MELEE_SWEEP_CHAINSAW" ) : ChainsawActivity( "ACT_SHOOT_W2E_AXE" );
}

Activity CChainsaw::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return ChainsawActivity( "ACT_RELOAD_RIFLE" );
}

int CChainsaw::GetViewModelLayer( Activity activity )
{
	return activity == ACT_VM_HITLEFT || activity == ACT_VM_SWINGHIT ? 2 : 0;
}

acttable_t *CChainsaw::ActivityList( int &count )
{
	static const char *names[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_CHAINSAW" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_CHAINSAW" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_CHAINSAW" },
		{ "ACT_WALK", "ACT_WALK_CHAINSAW" },
		{ "ACT_RUN", "ACT_RUN_CHAINSAW" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_CHAINSAW" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_CHAINSAW" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_CHAINSAW" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CHAINSAW" },
		{ "ACT_WALK_CALM", "ACT_WALK_CHAINSAW" },
		{ "ACT_RUN_CALM", "ACT_RUN_CHAINSAW" },
		{ "ACT_JUMP", "ACT_JUMP_CHAINSAW" }
	};
	static acttable_t table[ARRAYSIZE( names )];
	static bool initialized = false;
	if ( !initialized )
	{
		for ( int i = 0; i < ARRAYSIZE( names ); ++i )
		{
			table[i].baseAct = ChainsawActivity( names[i][0] );
			table[i].weaponAct = ChainsawActivity( names[i][1] );
			table[i].required = false;
		}
		initialized = true;
	}
	count = ARRAYSIZE( table );
	return table;
}

#ifndef CLIENT_DLL
int CChainsaw::ObjectCaps()
{
	return m_iClip1 != 0 ? BaseClass::ObjectCaps() : BaseClass::ObjectCaps() & ~FCAP_IMPULSE_USE;
}

#pragma float_control( precise, on, push )
class CChainsawAttractScan
{
public:
	CChainsawAttractScan( CTerrorPlayer *source, float range ) : m_source( source ), m_rangeSquared( range * range ) {}
	bool operator()( INextBot *bot )
	{
		CBaseCombatCharacter *entity = bot->GetEntity();
		if ( entity != m_source && entity->GetTeamNumber() == TEAM_INFECTED && entity->IsAlive()
			&& !dynamic_cast< Witch * >( bot ) && ( bot->GetPosition() - m_source->GetAbsOrigin() ).LengthSqr() < m_rangeSquared )
			bot->OnCommandAttack( m_source );
		return true;
	}
private:
	CTerrorPlayer *m_source;
	float m_rangeSquared;
};

void CChainsaw::NotifyInfected()
{
	float range = chainsaw_attract_distance.GetFloat();
	if ( range == 0.0f )
	{
		static ConVarRef z_notice_it_range( "z_notice_it_range" );
		range = z_notice_it_range.GetFloat();
	}
	CChainsawAttractScan scan( m_hChainsawOwner.Get(), range );
	TheNextBots().ForEachBot( scan );
}
#pragma float_control( pop )

class CChainsawTraceFilter : public CTraceFilterSimpleListNoGhosts
{
public:
	CChainsawTraceFilter( CTerrorPlayer *owner ) : CTraceFilterSimpleListNoGhosts( COLLISION_GROUP_NONE )
	{
		AddEntityToIgnore( owner );
		// Native8048 != PlayerDisconnected, not a team or ghost predicate.
		for ( int i = 0; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( player && player->edict() && player->entindex() != 0 &&
				player->IsConnected() && player->IsIncapacitated() )
				AddEntityToIgnore( player );
		}
	}
};

#pragma float_control( precise, on, push )
void CChainsaw::TraceContact()
{
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( !owner )
		return;
	Vector direction = GetSwingDirection();
	QAngle angles;
	VectorAngles( direction, angles );
	const float halfCone = chainsaw_attack_cone.GetFloat() * 0.5f;
	angles.y = static_cast< float >( static_cast< double >( RandomFloat( -halfCone, halfCone ) ) + angles.y );
	AngleVectors( angles, &direction );
	const Vector start = owner->EyePosition();
	const Vector end = start + direction * chainsaw_attack_distance.GetFloat();
	CChainsawTraceFilter filter( owner );
	trace_t trace;
	UTIL_TraceHull( start, end, Vector( -4, -4, -4 ), Vector( 4, 4, 4 ), 0x4600400Bu, &filter, &trace );
	static ConVarRef melee_show_swing( "melee_show_swing" );
	if ( melee_show_swing.GetInt() != 0 )
		NDebugOverlay::SweptBox( start, end, Vector( -4, -4, -4 ), Vector( 4, 4, 4 ), vec3_angle,
			255, trace.DidHit() ? 0 : 255, 0, 255, 3.0f );
	CBaseEntity *target = trace.m_pEnt;
	if ( trace.DidHit() && target )
	{
		CTakeDamageInfo damage( this, owner, chainsaw_damage.GetFloat(), 0x04000000 );
		damage.SetWeapon( this );
		damage.SetDamagePosition( owner->Weapon_ShootPosition() );
		damage.SetDamageForce( direction * chainsaw_attack_force.GetFloat() );
		ClearMultiDamage();
		target->DispatchTraceAttack( damage, direction, &trace );
		ApplyMultiDamage();
		if ( m_nPitch == 100 && !trace.DidHitWorld() )
			SendWeaponAnim( ACT_VM_SWINGHIT );
		m_nPitch = MAX( m_nPitch - 10, 50 );
		CSoundEnvelopeController::GetController().SoundChangePitch( m_pActiveSound, (float)m_nPitch, 0.2f );
		m_pitchDownTimer.Start( 0.4f );
		if ( target->MyCombatCharacterPointer() )
		{
			if ( gpGlobals->curtime > m_flNextGoreSound )
			{
				target->EmitSound( "Chainsaw.Gore" );
				m_flNextGoreSound = static_cast< float >( static_cast< double >( RandomFloat( 0.5f, 1.5f ) ) + gpGlobals->curtime );
			}
			owner->AddTerrorSplatter( 2, 0.0f, 1 );
			m_bHitting = true;
		}
	}
	TraceTerrorMeleeTongue( owner, start, end, 0x04000000, end - start );
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
void CChainsaw::UpdateActive()
{
	if ( gpGlobals->curtime >= m_pitchDownTimer.m_timestamp )
	{
		m_nPitch = 100;
		CSoundEnvelopeController::GetController().SoundChangePitch( m_pActiveSound, 100.0f, 0.5f );
		m_bHitting = false;
	}
	if ( !TerrorGameRules()->IsInfiniteChainsawFuel() )
	{
		m_flAmmoUseAccum += gpGlobals->frametime;
		while ( m_flAmmoUseAccum > 1.0f )
		{
			m_flAmmoUseAccum -= 1.0f;
			--m_iClip1;
		}
	}
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( m_iClip1 == 0 )
	{
		StopSound( m_pIdleSound );
		StopSound( m_pActiveSound );
		StartSound( m_pStopSound, "Chainsaw.Die" );
		m_PoseParameterTargets.Set( 0, 0.0f );
		m_PoseParameterTargets.Set( 1, 0.0f );
		owner->RumbleEffect( RUMBLE_CHAINSAW_IDLE, 0, RUMBLE_FLAG_STOP );
		owner->RumbleEffect( RUMBLE_CHAINSAW_ACTIVE, 0, RUMBLE_FLAG_STOP );
		m_iState = INACTIVE;
		return;
	}
	m_flHitIntervalAccum += gpGlobals->frametime;
	// Nonpositive intervals do not terminate in the original. Skip only that
	// contact loop; fuel, speech and common frame work retain their native order.
	while ( chainsaw_hit_interval.GetFloat() > 0.0f && m_flHitIntervalAccum > chainsaw_hit_interval.GetFloat() )
	{
		m_flHitIntervalAccum -= chainsaw_hit_interval.GetFloat();
		if ( m_bDeployed )
			TraceContact();
	}
	if ( m_usedTimer.m_timestamp > 0.0f && gpGlobals->curtime >= m_usedTimer.m_timestamp )
		owner->SpeakIfAllowed( "ChainsawUsed", 1, NULL, NULL, 0, NULL );
}
#pragma float_control( pop )
#endif // !CLIENT_DLL

#pragma float_control( precise, on, push )
void CChainsaw::ItemPostFrame()
{
#ifndef CLIENT_DLL
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( owner )
	{
		if ( m_iClip1 == 0 )
		{
			owner->DropWeapon( this, true, NULL );
			owner->GiveNamedItemInternal( "weapon_pistol", 0, true, NULL );
			owner->SwitchToNextBestWeapon( NULL );
			SetThink( &CBaseEntity::SUB_Remove );
			SetNextThink( gpGlobals->curtime + 2.0f );
		}
		if ( chainsaw_debug.GetInt() == 1 )
			DisplayDebug( true );
		UpdatePoseParameters();
		if ( ( m_iState == IDLE || m_iState == ACTIVE ) && gpGlobals->curtime >= m_attractTimer.m_timestamp )
		{
			NotifyInfected();
			m_attractTimer.Start( 1.0f );
		}
		if ( m_iState == STARTUP )
		{
			CBaseViewModel *viewModel = m_hChainsawViewModel.Get();
			if ( viewModel && viewModel->GetNumAnimOverlays() > 0 && viewModel->GetAnimOverlay( 0 )->m_flCycle > 0.95f )
			{
				StartSound( m_pIdleSound, "Chainsaw.Idle" );
				NotifyInfected();
				m_iState = IDLE;
			}
			m_bHitting = false;
		}
		else if ( m_iState == ACTIVE )
			UpdateActive();
		if ( m_bHitting )
			owner->RumbleEffect( RUMBLE_CHAINSAW_HIT, 0, RUMBLE_FLAG_RESTART );
	}
#endif
	BaseClass::ItemPostFrame();
}
#pragma float_control( pop )

#ifdef CLIENT_DLL
bool CChainsaw::Simulate()
{
	// C:0x2F75D0 runs for remote/holstered entities too, not just predicted input.
	CacheOwnerAndViewModel();
	if ( chainsaw_debug.GetInt() == 1 && m_hChainsawOwner.Get() )
		DisplayDebug( false );
	UpdateBloodDecals();
	UpdatePoseParameters();
	UpdateParticles();
	return BaseClass::Simulate();
}

void CChainsaw::RemoveParticles()
{
	HPARTICLEFFECT *effects[] = { &m_hBloodFirstPerson, &m_hBloodThirdPerson, &m_hSmokeFirstPerson, &m_hSmokeThirdPerson };
	for ( int i = 0; i < ARRAYSIZE( effects ); ++i )
	{
		if ( *effects[i] )
			( *effects[i] )->StopEmission( false, true, true );
		*effects[i] = NULL;
	}
}

#pragma float_control( precise, on, push )
void CChainsaw::UpdateParticles()
{
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	CBaseViewModel *viewModel = m_hChainsawViewModel.Get();
	if ( !owner )
	{
		if ( m_hSmokeFirstPerson && !m_hSmokeFirstPerson->m_bEmissionStopped ) m_hSmokeFirstPerson->SetDormant( true );
		if ( m_hBloodFirstPerson && !m_hBloodFirstPerson->m_bEmissionStopped ) m_hBloodFirstPerson->SetDormant( true );
		return;
	}
	if ( viewModel )
	{
		if ( !m_hSmokeFirstPerson )
		{
			m_hSmokeFirstPerson = viewModel->ParticleProp()->Create( GetParticleSystemNameFromIndex( g_nChainsawSmokeFirstPersonIndex ), PATTACH_POINT_FOLLOW, viewModel->LookupAttachment( "attach_smoke" ) );
			if ( m_hSmokeFirstPerson ) m_hSmokeFirstPerson->SetDormant( true );
		}
		if ( !m_hBloodFirstPerson )
		{
			m_hBloodFirstPerson = viewModel->ParticleProp()->Create( GetParticleSystemNameFromIndex( g_nChainsawBloodFirstPersonIndex ), PATTACH_POINT_FOLLOW, viewModel->LookupAttachment( "attach_blood" ) );
			if ( m_hBloodFirstPerson ) m_hBloodFirstPerson->SetDormant( true );
		}
	}
	if ( !m_hSmokeThirdPerson )
	{
		m_hSmokeThirdPerson = ParticleProp()->Create( GetParticleSystemNameFromIndex( g_nChainsawSmokeThirdPersonIndex ), PATTACH_POINT_FOLLOW, LookupAttachment( "attach_smoke" ) );
		if ( m_hSmokeThirdPerson ) m_hSmokeThirdPerson->SetDormant( true );
	}
	if ( !m_hBloodThirdPerson )
	{
		m_hBloodThirdPerson = ParticleProp()->Create( GetParticleSystemNameFromIndex( g_nChainsawBloodThirdPersonIndex ), PATTACH_POINT_FOLLOW, LookupAttachment( "attach_blood" ) );
		if ( m_hBloodThirdPerson ) m_hBloodThirdPerson->SetDormant( true );
	}
	const C_BasePlayer::PlayerViewType_t viewType = owner->GetPlayerViewType();
	const bool active = owner->GetActiveWeapon() == this && m_iState != INACTIVE;
	// C2F7F30 queries IsEmitting (not m_bDormant), and updates TP before FP.
	if ( viewType == C_BasePlayer::PLAYER_VIEW_THIRD_PERSON )
	{
		if ( m_hSmokeThirdPerson && m_hSmokeThirdPerson->m_bEmissionStopped == active )
			m_hSmokeThirdPerson->SetDormant( !active );
		if ( m_hBloodThirdPerson )
		{
			if ( !active || !m_bHitting )
			{
				if ( !m_hBloodThirdPerson->m_bEmissionStopped ) m_hBloodThirdPerson->SetDormant( true );
			}
			else if ( m_hBloodThirdPerson->m_bEmissionStopped && !UTIL_IsLowViolence() )
				m_hBloodThirdPerson->SetDormant( false );
		}
	}
	else
	{
		if ( m_hSmokeThirdPerson ) m_hSmokeThirdPerson->SetDormant( true );
		if ( m_hBloodThirdPerson ) m_hBloodThirdPerson->SetDormant( true );
	}
	if ( viewType == C_BasePlayer::PLAYER_VIEW_FIRST_PERSON )
	{
		if ( m_hSmokeFirstPerson && m_hSmokeFirstPerson->m_bEmissionStopped == active )
			m_hSmokeFirstPerson->SetDormant( !active );
		if ( m_hBloodFirstPerson )
		{
			if ( !active || !m_bHitting )
			{
				if ( !m_hBloodFirstPerson->m_bEmissionStopped ) m_hBloodFirstPerson->SetDormant( true );
			}
			else if ( m_hBloodFirstPerson->m_bEmissionStopped && !UTIL_IsLowViolence() )
				m_hBloodFirstPerson->SetDormant( false );
		}
	}
	else
	{
		if ( m_hSmokeFirstPerson ) m_hSmokeFirstPerson->SetDormant( true );
		if ( m_hBloodFirstPerson ) m_hBloodFirstPerson->SetDormant( true );
	}
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
void CChainsaw::UpdateBloodDecals()
{
	CTerrorPlayer *owner = m_hChainsawOwner.Get();
	if ( !owner || UTIL_IsLowViolence() || !m_bHitting || !( gpGlobals->curtime >= m_bloodDecalTimer.m_timestamp ) )
		return;
	QAngle angles;
	angles.x = ( (float)RandomInt( 0, 0x7FFF ) * 0.000030518509f ) * 360.0f;
	angles.y = ( (float)RandomInt( 0, 0x7FFF ) * 0.000030518509f ) * 360.0f;
	angles.z = ( (float)RandomInt( 0, 0x7FFF ) * 0.000030518509f ) * 360.0f;
	Vector direction;
	AngleVectors( angles, &direction );
	const Vector offset = direction * 150.0f;
	const Vector end = owner->WorldSpaceCenter() + offset;
	const Vector start = owner->WorldSpaceCenter(); // Native genuine second virtual query.
	trace_t trace;
	CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
	UTIL_TraceLine( start, end, 0x0200400Bu, &filter, &trace );
	if ( trace.DidHit() )
		UTIL_BloodDecalTrace( &trace, BLOOD_COLOR_RED );
	m_bloodDecalTimer.Start( 0.1f );
}
#pragma float_control( pop )
#endif // CLIENT_DLL

// Native server 0x3c93f0; 0x5c4bac is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_chainsawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_chainsaw", 0x5c4bac ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_chainsaw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
