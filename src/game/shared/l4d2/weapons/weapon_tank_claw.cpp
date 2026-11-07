//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_tank_claw.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "SharedFunctorUtils.h"
#include "effect_dispatch_data.h"
#include "debugoverlay_shared.h"
#include "rumble_shared.h"
#include "ispatialpartition.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "c_playerresource.h"
#include "cliententitylist.h"
#include "c_te_effect_dispatch.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav_area.h"
#include "doors.h"
#include "BasePropDoor.h"
#include "ilagcompensationmanager.h"
#include "l4d2/player/terror_lagcompensation.h"
#include "team.h"
#include "te_effect_dispatch.h"
#include "obstacle_pushaway.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "l4d2/terror_trace_filter.h"
#include "l4d2/terror_physics_math.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar tank_windup_time( "tank_windup_time", ".5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Time from intent to swing that swing actually happens." ); // S:5A6040
ConVar tank_attack_range( "tank_attack_range", "50", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Distance from Survivor that tank triggers own swing." ); //5A6070
ConVar tank_fist_radius( "tank_fist_radius", "15", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 ); //5A60A0
ConVar tank_swing_interval( "tank_swing_interval", "1.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Interval between tank swings" ); //5A60D0
ConVar tank_swing_fast_interval( "tank_swing_fast_interval", "0.6", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Interval between tank swings when he is clearing zombies out of his path" ); //5A6100
ConVar tank_swing_miss_interval( "tank_swing_miss_interval", "1.0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Interval between tank swings after a miss" ); //5A6130
ConVar tank_swing_duration( "tank_swing_duration", "0.2", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Duration of the actual swing" ); //5A6160
ConVar tank_swing_range( "tank_swing_range", "56", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Range of the actual swing" ); //5A6190
ConVar tank_swing_yaw( "tank_swing_yaw", "80", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 ); //5A61C0
ConVar tank_swing_arc( "tank_swing_arc", "180", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 ); //5A61F0
ConVar tank_swing_physics_prop_force( "tank_swing_physics_prop_force", "4.0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Multiplier for tank hitting a phys prop." ); //5A6220
ConVar tank_auto_swing( "tank_auto_swing", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 ); //5A6250
ConVar tank_ground_pound_duration( "tank_ground_pound_duration", "1.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 ); //5A6280
ConVar tank_pz_forward( "tank_pz_forward", "-0.5", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 ); //5A62B0
#ifndef CLIENT_DLL
ConVar tank_ground_pound_reveal_distance( "tank_ground_pound_reveal_distance", "500", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 ); //5A62E0
ConVar z_tank_footstep_shake_amplitude( "z_tank_footstep_shake_amplitude", "5", 
#if defined( GAME_DLL )
	0x00000006
#else
	0
#endif
 ); // S native ctor, retained shake packet
ConVar z_tank_footstep_shake_duration( "z_tank_footstep_shake_duration", "2", 
#if defined( GAME_DLL )
	0x00000006
#else
	0
#endif
 );
ConVar z_tank_footstep_shake_radius( "z_tank_footstep_shake_radius", "750", 
#if defined( GAME_DLL )
	0x00000006
#else
	0
#endif
 );
#endif
extern ConVar z_show_swings;

LINK_ENTITY_TO_CLASS( weapon_tank_claw, CTankClaw );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_tank_claw );
#endif
IMPLEMENT_NETWORKCLASS_ALIASED( TankClaw, DT_WeaponTankClaw )
BEGIN_NETWORK_TABLE( CTankClaw, DT_WeaponTankClaw )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_isLowAttack ) ),
	RecvPropDataTable( RECVINFO_DT( m_lowAttackDurationTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
#else
	SendPropBool( SENDINFO( m_isLowAttack ) ),
	SendPropDataTable( SENDINFO_DT( m_lowAttackDurationTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
#endif
END_NETWORK_TABLE()
// Native C:51A2E0 has only the empty prediction sentinel. The two receive
// fields are NOT additional predicted fields; windup/pitch likewise are not.
BEGIN_PREDICTION_DATA( CTankClaw )
END_PREDICTION_DATA()
#ifndef CLIENT_DLL
BEGIN_DATADESC( CTankClaw )
END_DATADESC()
#endif

static bool TankClawClassnameIs( CBaseEntity *entity, const char *name )
{
#ifdef CLIENT_DLL
	return !Q_strcmp( entity->GetClassname(), name ); // C100121C0 exactstrcmp; wildcard-looking literals remain literal
#else
	return FClassnameIs( entity, name ); // S10052EA0 supports native wildcard class matching
#endif
}
static bool IsClawSurvivor( const CBaseEntity *entity )
{
	return entity && ( entity->GetTeamNumber() == TEAM_SURVIVOR || entity->GetTeamNumber() == TEAM_L4D1_SURVIVOR );
}
static CBaseEntity *ClawEntityFromHandle( IHandleEntity *handle )
{
#ifdef CLIENT_DLL
	return cl_entitylist->GetBaseEntityFromHandle( handle->GetRefEHandle() );
#else
	return EntityFromEntityHandle( handle );
#endif
}
static bool IsSmashablePhysicsProp( CBaseEntity *entity ) // S:12A3E0, C:148730
{
	static ConVarRef turbo( "sv_turbophysics" );
	if ( !turbo.GetBool() || entity->GetCollisionGroup() != COLLISION_GROUP_NONE ) return false;
#ifdef CLIENT_DLL
	// Client class aliases, NOT server classname strings (native asymmetry).
	return TankClawClassnameIs( entity, "class CPhysicsPropMultiplayer" ) || TankClawClassnameIs( entity, "class CPhysicsProp" );
#else
	return TankClawClassnameIs( entity, "prop_physics_multiplayer" ) || TankClawClassnameIs( entity, "prop_physics" );
#endif
}
static bool IsSmashablePushaway( CBaseEntity *entity ) // S:12A310, C:1486C0
{
	if ( entity->GetCollisionGroup() == COLLISION_GROUP_PUSHAWAY ) return true;
#ifndef CLIENT_DLL
	static ConVarRef pushPlayers( "sv_doors_push_players" );
	if ( TankClawClassnameIs( entity, "func_door_rotating" ) )
	{
		CBaseDoor *door = dynamic_cast< CBaseDoor * >( entity );
		return door && ( door->m_toggle_state == TS_GOING_UP || door->m_toggle_state == TS_GOING_DOWN ) && pushPlayers.GetBool();
	}
	if ( TankClawClassnameIs( entity, "prop_door_rotating" ) )
	{
		CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( entity );
		return door && ( door->IsDoorOpening() || door->IsDoorClosing() ) && pushPlayers.GetBool();
	}
#endif
	return false;
}

// Genuine native HulkSmashableEnumerator, S:3E62C0/3E42B0,C:30A920/30A090.
class HulkSmashableEnumerator : public IPartitionEnumerator
{
public:
	HulkSmashableEnumerator( CTerrorPlayer *owner, const QAngle &angles ) : m_owner( owner ), m_best( NULL ), m_bestDot( -1.0f ), m_angles( angles ) {}
	virtual IterationRetval_t EnumElement( IHandleEntity *handle ) OVERRIDE
	{
		CBaseEntity *entity = ClawEntityFromHandle( handle );
		if ( !entity || entity == m_owner ) return ITERATION_CONTINUE;
		m_owner->EyePosition(); // original first (unused) eye query retained
		Vector forward;
		AngleVectors( m_angles, &forward );
		const Vector eye = m_owner->EyePosition();
		Vector toward = entity->WorldSpaceCenter() - eye;
		TerrorPhysicsNormalize( toward );
		if ( DotProduct( forward, toward ) < 0.0f ) return ITERATION_CONTINUE;
		trace_t trace;
		UTIL_TraceLine( eye, entity->WorldSpaceCenter(), MASK_PLAYERSOLID, m_owner, COLLISION_GROUP_NONE, &trace );
		bool visible = IsVisibleTrace( trace, entity );
		if ( !visible && entity->IsPlayer() )
		{
			UTIL_TraceLine( eye, entity->GetAbsOrigin(), MASK_PLAYERSOLID, m_owner, COLLISION_GROUP_NONE, &trace );
			visible = IsVisibleTrace( trace, entity );
			if ( !visible ) { UTIL_TraceLine( eye, entity->EyePosition(), MASK_PLAYERSOLID, m_owner, COLLISION_GROUP_NONE, &trace ); visible = IsVisibleTrace( trace, entity ); }
		}
		if ( !visible ) return ITERATION_CONTINUE;
		CTerrorPlayer *player = ToTerrorPlayer( entity );
		if ( player && player->GetZombieClass() == 7 ) return ITERATION_CONTINUE;
		bool smashable = IsSmashablePhysicsProp( entity ) || IsSmashablePushaway( entity );
#ifndef CLIENT_DLL
		smashable = smashable || IsBreakableEntity( entity, true, false, NULL );
#endif
		if ( smashable || entity->IsPlayer() || entity->MyCombatCharacterPointer() || TankClawClassnameIs( entity, "func_door*" ) || TankClawClassnameIs( entity, "prop_door*" ) || TankClawClassnameIs( entity, "finale_trigger" ) || TankClawClassnameIs( entity, "trigger_finale" ) ) Add( entity );
		return ITERATION_CONTINUE;
	}
	void Enumerate( const Ray_t &ray )
	{
#ifdef CLIENT_DLL
		partition->EnumerateElementsAlongRay( PARTITION_CLIENT_SOLID_EDICTS, ray, false, this );
#else
		partition->EnumerateElementsAlongRay( PARTITION_ENGINE_SOLID_EDICTS, ray, false, this );
#endif
	}
	CUtlVectorFixedGrowable< CBaseEntity *, 16 > m_entities;
private:
	static bool IsVisibleTrace( const trace_t &trace, CBaseEntity *entity )
	{
		return trace.fraction == 1.0f || trace.m_pEnt == entity || !trace.m_pEnt || trace.m_pEnt->GetMoveParent() == entity; // S10031490 is single EHANDLE372, not root parent
	}
	void Add( CBaseEntity *entity )
	{
		m_owner->EyePosition();
		Vector forward;
		AngleVectors( m_angles, &forward );
		Vector toward = entity->WorldSpaceCenter() - m_owner->EyePosition();
		TerrorPhysicsNormalize( toward );
		const float dot = DotProduct( forward, toward );
		if ( dot > m_bestDot ) { m_bestDot = dot; m_best = entity; }
		if ( m_entities.Find( entity ) == m_entities.InvalidIndex() ) m_entities.AddToTail( entity );
	}
	CTerrorPlayer *m_owner;
	CBaseEntity *m_best;
	float m_bestDot;
	QAngle m_angles;
};

CTankClaw::CTankClaw() // S:3E40F0/C:3095C0
{
	m_windupTimer.Invalidate();
	m_isLowAttack = false;
	m_lowAttackDurationTimer.Invalidate();
	// Native does not initialize swing duration/pitch/last hand/hit flags.
}
bool CTankClaw::CanPerformUseAction() const // S:3E35B0/C:3091C0
{
	return m_lowAttackDurationTimer.m_timestamp <= 0.0f || static_cast< double >( gpGlobals->curtime ) >= m_lowAttackDurationTimer.m_timestamp.Get();
}
Vector CTankClaw::GetSwingOrigin() const // S:3E3480/C:308F60
{
	CBaseCombatCharacter *owner = GetOwner();
	return owner ? owner->EyePosition() - Vector( 0.0f, 0.0f, 25.0f ) : vec3_origin;
}
Vector CTankClaw::GetSwingDirection() const // S:3E33F0/C:308ED0
{
	if ( !GetTerrorPlayerOwner() ) return vec3_origin;
	QAngle angles = EyeAngles();
	angles.x = m_swingStartPitch;
	Vector forward;
	AngleVectors( angles, &forward );
	return forward;
}
float CTankClaw::GetSwingStartYaw() const { return tank_swing_arc.GetFloat() * -0.5f; } // S:3E3380
float CTankClaw::GetSwingEndYaw() const { return tank_swing_arc.GetFloat() * 0.5f; } // S:3E3390
Vector CTankClaw::GetClawDamageForce( CBaseEntity *target, const Vector &direction ) const // S:3E3E90/C:309370
{
	if ( target && ( TankClawClassnameIs( target, "prop_physics*" ) || TankClawClassnameIs( target, "prop_car_alarm" ) ) && GetTerrorPlayerOwner() )
	{
		IPhysicsObject *physics = target->VPhysicsGetObject();
		const float mass = physics ? physics->GetMass() : 500.0f;
		Vector force;
		GetTerrorPlayerOwner()->EyeVectors( &force );
		const float z = mass * 0.000066666667f;
		if ( force.z <= z ) force.z = z;
		TerrorPhysicsNormalize( force );
		return force;
	}
	return BaseClass::GetClawDamageForce( target, direction );
}
float CTankClaw::GetPhysicsPropForceScale( CBaseEntity *target ) const // S:3E3640
{
	if ( !target || !( TankClawClassnameIs( target, "prop_physics*" ) || TankClawClassnameIs( target, "prop_car_alarm" ) ) || !GetTerrorPlayerOwner() ) return 1.0f;
	IPhysicsObject *physics = target->VPhysicsGetObject();
	float mass = physics ? physics->GetMass() : 500.0f;
	if ( physics ) { if ( mass > 1500.0f ) mass = 1500.0f; else if ( mass < 100.0f ) mass = 100.0f; }
	// Keep the native x87 chain wide until its single float result spill.
	return static_cast< float >( ( static_cast< double >( tank_swing_physics_prop_force.GetFloat() ) - 1.0 ) *
		( static_cast< double >( mass ) - 100.0 ) * static_cast< double >( 0.00071428571f ) + 1.0 );
}
void CTankClaw::SecondaryAttack() {} // genuine empty native S:3E3370/C:308EB0
void CTankClaw::PrimaryAttack() // S:3E3860/C:309200
{
	if ( tank_auto_swing.GetBool() ) return;
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner ) return;
	CBaseAbility *ability = owner->GetCustomAbility();
	if ( ( ability && ability->IsActive() ) || !owner->CanAttack() ) return;
	IGameEvent *event = gameeventmanager->CreateEvent( "weapon_fire" );
	if ( event )
	{
		event->SetInt( "userid", owner->GetUserID() );
		const char *weapon = GetClassname();
		if ( weapon && !Q_strncmp( weapon, "weapon_", 7 ) ) weapon += 7;
		event->SetString( "weapon", weapon ? weapon : "" );
		event->SetInt( "weaponid", GetWeaponID() );
#ifdef CLIENT_DLL
		gameeventmanager->FireEventClientSide( event );
#else
		gameeventmanager->FireEvent( event );
#endif
	}
	if ( static_cast< double >( gpGlobals->curtime ) >= m_attackTimer.m_timestamp.Get() )
	{
		TankSwing( tank_swing_interval.GetFloat(), tank_swing_duration.GetFloat(), tank_swing_range.GetFloat(), NULL );
		if ( ability ) ability->SetRechargeTime( tank_swing_interval.GetFloat() );
	}
}
void CTankClaw::TankSwing( float interval, float duration, float range, CTerrorPlayer *target ) // S:3E43F0/C:309760
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner || IsAttacking() ) return;
	if ( target )
	{
		Vector toward = target->WorldSpaceCenter() - GetSwingOrigin();
		TerrorPhysicsNormalize( toward );
		QAngle angles;
		VectorAngles( toward, angles );
		m_swingStartPitch = angles.x;
	}
	else m_swingStartPitch = EyeAngles().x;
	const float windup = tank_windup_time.GetFloat();
	m_swingDuration = duration;
	m_attackTimer.Start( windup + interval );
	m_flNextSecondaryAttack = ( gpGlobals->curtime + interval ) + windup;
	m_flNextPrimaryAttack = m_flNextSecondaryAttack;
	m_flSwingRange = range;
	m_windupTimer.Start( windup );
#ifndef CLIENT_DLL
	owner->EmitSound( "HulkZombie.Attack" );
#endif
	OnSwing();
}
void CTankClaw::ItemPostFrame() // S:3E3DF0/C:309520, before genuine inherited frame callback
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner || owner->IsIncapacitated() ) return;
	if ( m_windupTimer.m_timestamp > 0.0f && static_cast< double >( gpGlobals->curtime ) >= m_windupTimer.m_timestamp.Get() )
	{
		m_windupTimer.Invalidate();
		OnWindupFinished();
	}
	BaseClass::ItemPostFrame();
}
void CTankClaw::OnSwing() // S:3E5080/C:3098D0, animation/windup stage, not yet hand damage
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner ) return;
	owner->SetAnimation( PLAYER_ATTACK1 );
	Vector forward;
	owner->EyeVectors( &forward );
	m_lastHandPosition = owner->EyePosition() + tank_swing_range.GetFloat() * forward;
	m_hitWorld = m_hitSurvivor = false;
#ifndef CLIENT_DLL
	m_lowAttackVictims.RemoveAll();
#endif
	const float forwardThreshold = TerrorGameRules()->IsPlayerControlledZombies() ? tank_pz_forward.GetFloat() : 0.0f;
	if ( forwardThreshold < forward.z ) { m_isLowAttack = false; m_lowAttackDurationTimer.Invalidate(); }
	else
	{
#ifdef CLIENT_DLL
		const float height = TerrorGameRules()->GetViewVectors()->m_vHullMax.z;
#else
		const float height = 71.0f;
#endif
		Vector flat;
		AngleVectors( owner->EyeAngles(), &flat );
		flat.z = 0.0f;
		const Vector start = owner->EyePosition() + flat * ( height * 0.5f ) * 0.75f;
		Vector down;
		AngleVectors( QAngle( 90.0f, 0.0f, 0.0f ), &down );
		const Vector end = start + ( down * height ) * 2.0f;
		Ray_t ray;
		ray.Init( start, end, Vector( -height, -height, -height ), Vector( height, height, height ) );
		HulkSmashableEnumerator enumerator( owner, QAngle( 90.0f, 0.0f, 0.0f ) );
		enumerator.Enumerate( ray );
		if ( z_show_swings.GetBool() ) NDebugOverlay::SweptBox( start, end, Vector( -height, -height, -height ), Vector( height, height, height ), vec3_angle, 255, 255, 255, 50, 10.1f );
		for ( int i = 0; i < enumerator.m_entities.Count(); ++i )
		{
			CTerrorPlayer *victim = ToTerrorPlayer( enumerator.m_entities[i] );
			if ( !victim || !IsClawSurvivor( victim ) || !victim->IsAlive() || !victim->IsIncapacitated() ) continue;
			CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
			Vector point = victim->EyePosition();
			trace_t trace;
			UTIL_TraceLine( owner->EyePosition(), point, owner->PhysicsSolidMaskForEntity(), &filter, &trace );
			if ( trace.fraction < 1.0f )
			{
				QAngle angles;
				victim->GetAttachment( "rfoot", point, angles );
				UTIL_TraceLine( owner->EyePosition(), point, owner->PhysicsSolidMaskForEntity(), &filter, &trace );
				if ( trace.fraction < 1.0f ) { victim->GetAttachment( "eyes", point, angles ); UTIL_TraceLine( owner->EyePosition(), point, owner->PhysicsSolidMaskForEntity(), &filter, &trace ); }
			}
			if ( trace.fraction == 1.0f )
			{
#ifdef CLIENT_DLL
				m_isLowAttack = true;
				m_lowAttackDurationTimer.Start( tank_ground_pound_duration.GetFloat() );
#else
				m_lowAttackVictims.AddToTail( victim );
#endif
			}
		}
#ifndef CLIENT_DLL
		if ( m_lowAttackVictims.Count() ) { m_isLowAttack = true; m_lowAttackDurationTimer.Start( tank_ground_pound_duration.GetFloat() ); }
#endif
		// No eligible low victim deliberately DOES NOT clear an existing low state.
	}
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	owner->DoTerrorAnimationEvent( m_isLowAttack ? TERROR_ANIMEVENT_MELEE_SWEEP : TERROR_ANIMEVENT_ATTACK, 0 ); // native34/33; client genuinely unrecognized
}
void CTankClaw::OnWindupFinished() // S:3E4E80/C:30A720
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner ) return;
	m_swingTimer.Start( m_swingDuration );
	m_vecLastSwingDirection = vec3_origin;
	m_bSwingInProgress = true;
	m_swingAffectedEntities.RemoveAll();
	m_nSwingFrames = 0;
	// Native stores a 0.8 dot cutoff; the SDK constructor takes its complement.
	TargetScan< CTerrorPlayer > scan( owner, TEAM_SURVIVOR, 0.2f, tank_swing_range.GetFloat(), 50.0f, false );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
#ifdef CLIENT_DLL
		if ( player && player->IsPlayer() && !player->IsDormant() && GameResources()->IsConnected( i ) ) scan( static_cast< CTerrorPlayer * >( player ) );
#else
		if ( player && !FNullEnt( player->edict() ) && player->IsPlayer() && static_cast< const CBasePlayer * >( player )->IsConnected() ) scan( static_cast< CTerrorPlayer * >( player ) );
#endif
	}
	if ( scan.GetTarget() ) m_lastHandPosition = scan.GetTarget()->WorldSpaceCenter();
	else { Vector forward; owner->EyeVectors( &forward ); m_lastHandPosition = owner->EyePosition() + tank_swing_range.GetFloat() * forward; }
	m_hitWorld = m_hitSurvivor = false;
	if ( m_isLowAttack ) GroundPound();
	else UpdateSwing();
}
void CTankClaw::OnSwingEnd( bool hitAny ) // S:3E5800/C:309F50
{
	m_isLowAttack = false;
	if ( hitAny )
	{
		bool hitInfectedPlayer = false, onlyInfectedPlayers = true; // native WORD0x0100
		for ( int i = 0; i < m_swingAffectedEntities.Count(); ++i )
		{
			CBaseEntity *entity = m_swingAffectedEntities[i].Get();
			if ( !entity || !entity->IsPlayer() ) continue;
			if ( entity->GetTeamNumber() != TEAM_INFECTED ) { onlyInfectedPlayers = false; break; }
			hitInfectedPlayer = true;
		}
		if ( hitInfectedPlayer && onlyInfectedPlayers )
		{
			const float interval = tank_swing_interval.GetFloat();
			const float fastInterval = tank_swing_fast_interval.GetFloat();
			m_attackTimer.Start( static_cast< float >( m_attackTimer.m_timestamp.Get() - static_cast< double >( gpGlobals->curtime ) - ( static_cast< double >( interval ) - static_cast< double >( fastInterval ) ) ) );
		}
	}
	else
	{
		const float interval = tank_swing_interval.GetFloat();
		const float missInterval = tank_swing_miss_interval.GetFloat();
		m_attackTimer.Start( static_cast< float >( m_attackTimer.m_timestamp.Get() - static_cast< double >( gpGlobals->curtime ) - ( static_cast< double >( interval ) - static_cast< double >( missInterval ) ) ) );
	}
}
void CTankClaw::UpdateSwing() // S:3E6E20/C:30B150
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner ) return;
	const char *attachment = "rhand", *bone = "ValveBiped.Bip01_R_Hand";
	CTerrorPlayerAnimState *animState = owner->GetTerrorAnimState();
	CAnimationLayer *layer = animState ? animState->GetGestureSlotLayer( GESTURE_SLOT_ATTACK_AND_RELOAD ) : NULL;
	if ( layer && ( layer->m_nSequence == 44 || layer->m_nSequence == 45 ) ) { attachment = "lhand"; bone = "ValveBiped.Bip01_L_Hand"; } // native layer+8 is MODEL sequence, not retailActivity44/45
	Vector hand;
	QAngle angles;
	if ( !owner->GetAttachment( attachment, hand, angles ) )
	{
		const int index = owner->LookupBone( bone );
		if ( index < 0 ) return;
		owner->GetBonePosition( index, hand, angles );
	}
	SweepHand( m_lastHandPosition, hand );
	m_lastHandPosition = hand;
}

void CTankClaw::GroundPound() // S:3E45D0/C:30A1D0
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner ) return;
#ifndef CLIENT_DLL
	UTIL_ScreenShake( owner->GetAbsOrigin(), z_tank_footstep_shake_amplitude.GetFloat(), 150.0f, z_tank_footstep_shake_duration.GetFloat(), z_tank_footstep_shake_radius.GetFloat(), SHAKE_START );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->IsConnected() && player->GetTeamNumber() == TEAM_SURVIVOR && ( player->WorldSpaceCenter() - owner->GetAbsOrigin() ).Length() < tank_ground_pound_reveal_distance.GetFloat() ) player->SetNoiseLevel( 255.0f ); //3E3740/314000
	}
#endif
#ifdef CLIENT_DLL
	const float height = TerrorGameRules()->GetViewVectors()->m_vHullMax.z;
#else
	const float height = 71.0f;
#endif
	Vector flat;
	AngleVectors( owner->EyeAngles(), &flat );
	flat.z = 0.0f;
	const Vector start = owner->EyePosition() + flat * ( height * 0.5f ) * 0.75f;
	Vector down;
	AngleVectors( QAngle( 90.0f, 0.0f, 0.0f ), &down );
	const Vector end = start + down * height;
	const Vector mins( -height * 0.5f, -height * 0.5f, 0.0f ), maxs( height * 0.5f, height * 0.5f, 0.0f );
	Ray_t ray;
	ray.Init( start, end, mins, maxs );
	HulkSmashableEnumerator enumerator( owner, QAngle( 90.0f, 0.0f, 0.0f ) );
	enumerator.Enumerate( ray );
	bool hitAny = false;
#ifndef CLIENT_DLL
	for ( int i = 0; i < m_lowAttackVictims.Count(); ++i )
	{
		CBaseEntity *entity = m_lowAttackVictims[i].Get();
		CTerrorPlayer *victim = ToTerrorPlayer( entity );
		if ( !entity || ( m_hitSurvivor && TerrorGameRules()->IsPlayerControlledZombies() && IsClawSurvivor( entity ) ) || !entity->IsAlive() || ( victim && IsClawSurvivor( victim ) && !victim->IsIncapacitated() ) ) continue;
		trace_t trace;
		CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
		const Vector shoot = owner->Weapon_ShootPosition(), goal = entity->WorldSpaceCenter();
		UTIL_TraceLine( shoot, goal, MASK_SHOT_HULL, &filter, &trace );
		trace.m_pEnt = entity;
		OnSwingHit( trace, goal - shoot, true );
		if ( m_swingAffectedEntities.Count() < 128 ) m_swingAffectedEntities.AddToTail( entity );
		hitAny = true;
		if ( victim && IsClawSurvivor( victim ) ) m_hitSurvivor = true;
	}
#endif
	for ( int i = 0; i < enumerator.m_entities.Count(); ++i )
	{
		CBaseEntity *entity = enumerator.m_entities[i];
		CTerrorPlayer *victim = ToTerrorPlayer( entity );
		if ( m_hitSurvivor && TerrorGameRules()->IsPlayerControlledZombies() && IsClawSurvivor( entity ) ) continue;
		if ( victim && IsClawSurvivor( victim ) && !victim->IsIncapacitated() ) continue;
#ifndef CLIENT_DLL
		if ( entity->IsAlive() && m_swingAffectedEntities.Find( entity ) != m_swingAffectedEntities.InvalidIndex() ) continue;
#endif
		const Vector shoot = owner->Weapon_ShootPosition(), goal = entity->WorldSpaceCenter();
		trace_t trace;
		CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
		UTIL_TraceLine( shoot, goal, MASK_SHOT_HULL, &filter, &trace );
		if ( trace.fraction != 1.0f ) continue;
		trace.m_pEnt = entity;
		OnSwingHit( trace, goal - shoot, true );
		if ( m_swingAffectedEntities.Count() < 128 ) m_swingAffectedEntities.AddToTail( entity );
		hitAny = true;
		if ( victim && IsClawSurvivor( victim ) ) m_hitSurvivor = true;
	}
	m_bSwingInProgress = false;
	OnSwingEnd( hitAny );
}
void CTankClaw::SweepHand( const Vector &from, const Vector &to ) // S:3E6660/C:30AC20
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner ) return;
#ifndef CLIENT_DLL
	StartTerrorLagCompensation( owner, TERROR_LAG_COMPENSATE_BOUNDS, vec3_origin, vec3_angle, 0.0f );
#endif
	CTraceFilterSimpleListNoGhosts filter( COLLISION_GROUP_NONE );
	filter.AddEntityToIgnore( owner );
	const float radius = tank_fist_radius.GetFloat();
	const Vector mins( -radius, -radius, -radius ), maxs( radius, radius, radius );
	trace_t trace;
	UTIL_TraceHull( from, to, mins, maxs, MASK_SHOT_HULL, &filter, &trace );
	CBaseEntity *entity = trace.DidHitNonWorldEntity() ? trace.m_pEnt : NULL;
	if ( !entity )
	{
		const float width = TerrorGameRules()->GetViewVectors()->m_vHullMax.x - 1.0f;
		UTIL_TraceHull( owner->WorldSpaceCenter(), to, Vector( -width, -width, -width ), Vector( width, width, width ), MASK_SHOT_HULL, &filter, &trace );
		entity = !trace.allsolid && !trace.startsolid && trace.DidHitNonWorldEntity() ? trace.m_pEnt : NULL;
	}
	if ( TerrorGameRules()->IsPlayerControlledZombies() && entity )
	{
		trace_t sight;
		UTIL_TraceLine( owner->EyePosition(), trace.endpos, MASK_SHOT_HULL, &filter, &sight );
		if ( sight.DidHit() ) entity = NULL;
	}
	const bool survivor = entity && entity->IsPlayer() && IsClawSurvivor( entity );
	if ( entity )
	{
		// S3E6A06/C30AFA0 call the survival predicate, not the realism predicate.
		const bool multiHitMode = TerrorGameRules()->IsPlayerControlledZombies() || TerrorGameRules()->IsSurvivalMode();
		if ( !survivor || !m_hitSurvivor || multiHitMode )
		{
			Vector direction = entity->GetAbsOrigin() - owner->GetAbsOrigin();
			TerrorPhysicsNormalize( direction );
			OnSwingHit( trace, direction, m_swingAffectedEntities.Find( entity ) == m_swingAffectedEntities.InvalidIndex() );
			if ( m_swingAffectedEntities.Count() < 128 ) m_swingAffectedEntities.AddToTail( entity );
			if ( survivor ) m_hitSurvivor = true;
		}
	}
	else if ( trace.DidHit() )
	{
		Vector direction = to - owner->WorldSpaceCenter();
		TerrorPhysicsNormalize( direction );
		OnSwingHit( trace, direction, !m_hitWorld );
		m_hitWorld = true;
	}
#ifndef CLIENT_DLL
	if ( survivor && ( TerrorGameRules()->IsPlayerControlledZombies() || TerrorGameRules()->IsSurvivalMode() ) )
	{
		CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
		for ( int i = 0; team && i < team->GetNumPlayers(); ++i )
		{
			CBasePlayer *player = team->GetPlayer( i );
			if ( !player || m_swingAffectedEntities.Find( player ) != m_swingAffectedEntities.InvalidIndex() || !player->IsAlive() ) continue;
			Ray_t ray;
			ray.Init( from, to, mins, maxs );
			trace_t clipped;
			enginetrace->ClipRayToEntity( ray, MASK_SHOT_HULL, player, &clipped );
			if ( clipped.m_pEnt != player ) continue;
			trace_t sight;
			UTIL_TraceLine( owner->EyePosition(), clipped.endpos, MASK_SHOT_HULL, &filter, &sight );
			if ( sight.DidHit() ) continue;
			Vector direction = player->GetAbsOrigin() - owner->GetAbsOrigin();
			TerrorPhysicsNormalize( direction );
			OnSwingHit( clipped, direction, m_swingAffectedEntities.Find( player ) == m_swingAffectedEntities.InvalidIndex() );
			if ( m_swingAffectedEntities.Count() < 128 ) m_swingAffectedEntities.AddToTail( player );
		}
	}
	lagcompensation->FinishLagCompensation( owner );
#endif
}
#ifdef CLIENT_DLL
bool CTankClaw::OnSwingHit( trace_t &trace, const Vector &direction, bool newHit ) // C:309000, effects only
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner ) return false;
	CEffectData data;
	data.m_vOrigin = trace.endpos;
	data.m_vStart = trace.startpos;
	data.m_nSurfaceProp = trace.surface.surfaceProps;
	data.m_nDamageType = DMG_SLASH;
	data.m_nHitBox = trace.hitbox;
	if ( trace.m_pEnt ) data.m_hEntity = trace.m_pEnt->GetRefEHandle();
	data.m_vAngles = owner->GetAbsAngles();
	data.m_fFlags = 1;
	DispatchEffect( "KnifeSlash", data );
	return false;
}
#endif

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x3e3320; 0x5c5180 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_tank_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_tank_claw", 0x5c5180 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_tank_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
