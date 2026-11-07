//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/inferno.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/entities/func_elevator.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "particle_parse.h"
#include "engine/IEngineTrace.h"
#include "debugoverlay_shared.h"
#include "collisionutils.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

ConVar inferno_per_flame_spawn_duration( "inferno_per_flame_spawn_duration", "5", 0x00004004, "Duration each new flame will attempt to spawn new flames" );
ConVar inferno_initial_spawn_interval( "inferno_initial_spawn_interval", "0.05", 0x00004004, "Time between spawning flames for first fire" );
ConVar inferno_child_spawn_interval_multiplier( "inferno_child_spawn_interval_multiplier", "1", 0x00004004, "Amount spawn interval increases for each child" );
ConVar inferno_max_child_spawn_interval( "inferno_max_child_spawn_interval", "1", 0x00004004, "Largest time interval for child flame spawning" );
ConVar inferno_spawn_angle( "inferno_spawn_angle", "45", 0x00004004, "Angular change from parent" );
ConVar inferno_acid_spawn_angle( "inferno_acid_spawn_angle", "120", 0x00004004, "Angular change from parent" );
ConVar inferno_max_flames( "inferno_max_flames", "32", 0x00004004, "Maximum number of flames that can be created" );
ConVar inferno_flame_spacing( "inferno_flame_spacing", "50", 0x00004004, "Minimum distance between separate flame spawns" );
ConVar inferno_flame_lifetime( "inferno_flame_lifetime", "15", 0x00004004, "Average lifetime of each flame in seconds" );
ConVar inferno_friendly_fire_duration( "inferno_friendly_fire_duration", "6", 0x00004004, "For this long, FF is credited back to the thrower." );
ConVar inferno_debug( "inferno_debug", "0", 0x00004004 );
ConVar inferno_damage( "inferno_damage", "40", 0x00004004, "Damage per second" );
ConVar inferno_max_range( "inferno_max_range", "500", 0x00004004, "Maximum distance flames can spread from their initial ignition point" );
ConVar inferno_velocity_factor( "inferno_velocity_factor", "0.003", 0x00004004 );
ConVar inferno_velocity_decay_factor( "inferno_velocity_decay_factor", "0.2", 0x00004004 );
ConVar inferno_velocity_normal_factor( "inferno_velocity_normal_factor", "0", 0x00004004 );
ConVar inferno_surface_offset( "inferno_surface_offset", "20", 0x00004004 );
ConVar inferno_child_spawn_max_depth( "inferno_child_spawn_max_depth", "4", 0x00004004 );
ConVar inferno_scorch_decals( "inferno_scorch_decals", "0", 0x00004004 );
ConVar inferno_max_trace_per_tick( "inferno_max_trace_per_tick", "16", 0x00000006 );
ConVar inferno_forward_reduction_factor( "inferno_forward_reduction_factor", "0.9", 0x00004004 );

LINK_ENTITY_TO_CLASS( inferno, CInferno );
IMPLEMENT_SERVERCLASS_ST( CInferno, DT_Inferno )
	SendPropArray3( SENDINFO_ARRAY3( m_fireXDelta ), SendPropInt( SENDINFO_ARRAY( m_fireXDelta ), 15 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_fireYDelta ), SendPropInt( SENDINFO_ARRAY( m_fireYDelta ), 15 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_fireZDelta ), SendPropInt( SENDINFO_ARRAY( m_fireZDelta ), 15 ) ),
	SendPropInt( SENDINFO( m_fireCount ), 7, SPROP_UNSIGNED ),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( fire_cracker_blast, CFireCrackerBlast );
IMPLEMENT_SERVERCLASS_ST( CFireCrackerBlast, DT_FireCrackerBlast )
END_SEND_TABLE()

BEGIN_DATADESC( CInferno )
	DEFINE_THINKFUNC( InfernoThink ),
END_DATADESC()

CInferno::CInferno()
{
	m_fireCount = 0;
	m_flFireHalfWidth = 30.0f;
	m_flFireHeight = 80.0f;
	m_nSpreadIndex = 0;
	m_nMaxFlames = inferno_max_flames.GetInt();
	m_nDamageCustom = 0;
	m_fireType = FIRE;
	m_spreadVelocity.Init();
	m_initialPosition.Init();
	m_mins.Init();
	m_maxs.Init();
	for ( int i = 0; i < MAX_INFERNO_FIRES; ++i )
	{
		m_fireXDelta.Set( i, 0 );
		m_fireYDelta.Set( i, 0 );
		m_fireZDelta.Set( i, 0 );
	}
}

CInferno::~CInferno()
{
	if ( m_fireType == FIRE )
	{
		EmitSound( "Inferno.FadeOut" );
		StopSound( "Inferno.Loop" );
	}
	else if ( m_fireType == ACID )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *pBasePlayer = UTIL_PlayerByIndex( i );
			if ( !pBasePlayer || !pBasePlayer->edict() || !pBasePlayer->IsPlayer() )
				continue;
			CTerrorPlayer *pPlayer = dynamic_cast< CTerrorPlayer * >( pBasePlayer );
			if ( pPlayer && pPlayer->IsConnected() && pPlayer->GetTeamNumber() == TEAM_SURVIVOR )
				pPlayer->GetMusic()->OnSpitterSpitEnded();
		}
		EmitSound( "SpitterZombie.Acid.fadeout" );
		StopSound( "SpitterZombie.Acid" );
	}
	else if ( m_fireType == FIREWORKS )
	{
		EmitSound( "FireworksCrate.Stop" );
		StopSound( "FireworksCrate.Start" );
	}
}

void CInferno::Precache()
{
	PrecacheScriptSound( "Inferno.Start" );
	PrecacheScriptSound( "Inferno.StartSweeten" );
	PrecacheScriptSound( "Inferno.Loop" );
	PrecacheScriptSound( "Inferno.Fire.Ignite" );
	PrecacheScriptSound( "Inferno.FadeOut" );
	PrecacheScriptSound( "SpitterZombie.Acid" );
	PrecacheScriptSound( "SpitterZombie.Acid.fadeout" );
	PrecacheScriptSound( "FireworksCrate.Start" );
	PrecacheScriptSound( "FireworksCrate.Stop" );
	if ( GetLoopParticleName() )
		PrecacheParticleSystem( GetLoopParticleName() );
	if ( GetStartParticleName() )
		PrecacheParticleSystem( GetStartParticleName() );
}

void CInferno::Spawn()
{
	Precache();
	m_fireCount = 0;
	m_flFireHalfWidth = 30.0f;
	m_flFireHeight = 80.0f;
	m_damageRampTimer.Start( 2.0f );
	m_spreadTimer.Start( GetSpreadInterval() );
	SetThink( &CInferno::InfernoThink );
	SetNextThink( gpGlobals->curtime );
	m_fireType = FIRE;
}

int CInferno::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_PVSCHECK );
}

CInferno *CInferno::Create( CBaseEntity *pOwner, const Vector &position, const Vector &normal,
	const Vector &velocity, int nDepth )
{
	CInferno *pInferno = static_cast< CInferno * >( CBaseEntity::Create( "inferno", position, vec3_angle, pOwner ) );
	if ( pInferno )
		pInferno->StartBurning( position, normal, velocity, nDepth );
	return pInferno;
}

float CInferno::GetFlameLifetime() const { return inferno_flame_lifetime.GetFloat(); }
double CInferno::GetDamagePerSecond() const { return inferno_damage.GetFloat(); }
const char *CInferno::GetLoopParticleName() const { return "molotov_groundfire"; }
const char *CInferno::GetStartParticleName() const { return "molotov_explosion"; }
bool CInferno::CanHarm( CBaseEntity *pEntity ) const { return !pEntity->IsDormant(); }

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CInferno::StartBurning( const Vector &position, const Vector &normal, const Vector &velocity, int nDepth )
{
	if ( m_fireType == ACID && GetMoveParent() )
		m_nMaxFlames = 2;
	m_initialPosition.x = position.x + inferno_surface_offset.GetFloat() * normal.x;
	m_initialPosition.y = position.y + inferno_surface_offset.GetFloat() * normal.y;
	m_initialPosition.z = position.z;
	float flNormalVelocity = DotProduct( velocity, normal );
	Vector normalVelocity = normal * flNormalVelocity;
	m_spreadVelocity = velocity - normalVelocity -
		( normal * inferno_velocity_normal_factor.GetFloat() ) * flNormalVelocity;
	QAngle angles;
	VectorAngles( velocity, angles );
	if ( GetStartParticleName() )
		DispatchParticleEffect( GetStartParticleName(), position, angles );
	if ( inferno_debug.GetBool() )
	{
		NDebugOverlay::Sphere( position, m_flFireHalfWidth * 0.5f, 0, 255, 0, true, 10.0f );
		NDebugOverlay::Sphere( m_initialPosition, m_flFireHalfWidth * 0.5f, 255, 255, 0, true, 10.0f );
	}
	if ( AddFire( m_initialPosition, normal, NULL, nDepth ) )
	{
		if ( m_fireType == FIREWORKS )
			EmitSound( "FireworksCrate.Start" );
		else if ( m_fireType == ACID )
			EmitSound( "SpitterZombie.Acid" );
		else
		{
			EmitSound( "Inferno.Start" );
			EmitSound( "Inferno.StartSweeten" );
			EmitSound( "Inferno.Loop" );
		}
		m_initialPosition = m_fire[0].m_pos;
		SetAbsOrigin( m_initialPosition );
	}
	// The original starts this AFTER creating the first cell. At world time zero the
	// IntervalTimer remains unstarted; do not replace its >0 timestamp semantics.
	m_burnTimer.Start();
}

void CInferno::RecomputeBounds()
{
	m_mins.Init( 999999.9f, 999999.9f, 999999.9f );
	m_maxs.Init( -999999.9f, -999999.9f, -999999.9f );
	for ( int i = 0; i < m_fireCount; ++i )
	{
		const Vector &pos = m_fire[i].m_pos;
		// S38341F..3834A2: unordered candidates never replace a bound.
		const float flMinX = pos.x - m_flFireHalfWidth;
		const float flMaxX = pos.x + m_flFireHalfWidth;
		const float flMinY = pos.y - m_flFireHalfWidth;
		const float flMaxY = pos.y + m_flFireHalfWidth;
		const float flMaxZ = pos.z + m_flFireHeight;
		if ( m_mins.x > flMinX )
			m_mins.x = flMinX;
		if ( flMaxX > m_maxs.x )
			m_maxs.x = flMaxX;
		if ( m_mins.y > flMinY )
			m_mins.y = flMinY;
		if ( flMaxY > m_maxs.y )
			m_maxs.y = flMaxY;
		if ( m_mins.z > pos.z )
			m_mins.z = pos.z;
		if ( flMaxZ > m_maxs.z )
			m_maxs.z = flMaxZ;
	}
}

bool CInferno::IsTouching( const Vector &start, const Vector &end, Vector *pClosestPoint ) const
{
	Vector direction = end - start;
	float flLength = VectorNormalize( direction );
	float flRadius = m_flFireHalfWidth * 2.0f;
	for ( int i = 0; i < m_fireCount; ++i )
	{
		float flAlong = DotProduct( m_fire[i].m_center - start, direction );
		Vector closest = flAlong < 0.0f ? start : ( flAlong > flLength ? end : start + direction * flAlong );
		if ( ( closest - m_fire[i].m_center ).LengthSqr() < flRadius * flRadius )
		{
			if ( pClosestPoint )
				*pClosestPoint = closest;
			return true;
		}
	}
	return false;
}

bool CInferno::AddFire( const Vector &position, const Vector &normal, FireInfo *pParent, int nDepth )
{
	if ( m_fireCount >= MIN( m_nMaxFlames, MAX_INFERNO_FIRES ) || IsTouching( position, position ) )
		return false;
	Vector firePos = position;
	bool bOnWater = false;
	if ( enginetrace->GetPointContents( position ) & ( CONTENTS_WATER | CONTENTS_SLIME ) )
	{
		trace_t waterTrace;
		const float flWaterTraceHeight = FClassnameIs( this, "insect_swarm" ) ? 60.0f : 30.0f;
		UTIL_TraceLine( position + Vector( 0, 0, flWaterTraceHeight ), position, 0x403B, NULL, COLLISION_GROUP_NONE, &waterTrace );
		if ( waterTrace.startsolid )
			return false;
		firePos = waterTrace.endpos;
		bOnWater = true;
	}

	FireInfo &fire = m_fire[m_fireCount];
	fire.m_pos = firePos;
	fire.m_center = firePos + Vector( 0, 0, m_flFireHeight * 0.5f );
	fire.m_normal = normal;
	fire.m_pParent = pParent;
	fire.m_nDepth = nDepth;
	fire.m_nSpawnCredits = 0;
	fire.m_flWaterOffset = firePos.z - position.z;
	if ( nDepth > 0 && m_fireType == ACID &&
		!TerrorGameRules()->IsSurvivalMode() && !TerrorGameRules()->IsScavengeMode() )
	{
		CNavArea *pArea = TheNavMesh->GetNearestNavArea( firePos, false, 10000.0f, false, true, TEAM_UNASSIGNED );
		if ( pArea && ToTerrorNavArea( pArea )->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
			fire.m_nDepth = inferno_child_spawn_max_depth.GetInt();
	}
	fire.m_lifetime.Start( GetFlameLifetime() - ( m_burnTimer.HasStarted() ? m_burnTimer.GetElapsedTime() : 0.0f ) );
	if ( pParent )
	{
		fire.m_spawnTimer.Start( pParent->m_spawnTimer.GetCountdownDuration() );
		float flChildInterval = pParent->m_spawnInterval.GetCountdownDuration() *
			inferno_child_spawn_interval_multiplier.GetFloat();
		const float flMaxChildInterval = inferno_max_child_spawn_interval.GetFloat();
		if ( flChildInterval > flMaxChildInterval )
			flChildInterval = flMaxChildInterval;
		fire.m_spawnInterval.Start( flChildInterval );
	}
	else
	{
		fire.m_spawnTimer.Start( inferno_per_flame_spawn_duration.GetFloat() );
		fire.m_spawnInterval.Start( inferno_initial_spawn_interval.GetFloat() );
	}
	Vector delta = firePos - GetAbsOrigin();
	// S384616/S384635/S38465C: CVTTSS2SI retains native INT_MIN on invalid input.
	m_fireXDelta.Set( m_fireCount, _mm_cvtt_ss2si( _mm_set_ss( delta.x ) ) );
	m_fireYDelta.Set( m_fireCount, _mm_cvtt_ss2si( _mm_set_ss( delta.y ) ) );
	m_fireZDelta.Set( m_fireCount, _mm_cvtt_ss2si( _mm_set_ss( delta.z ) ) );
	++m_fireCount;
	RecomputeBounds();

	if ( m_fireType == FIRE )
	{
		CPASAttenuationFilter filter( firePos, 0.8f );
		EmitSound_t sound;
		sound.m_pSoundName = "Inferno.Fire.Ignite";
		sound.m_pOrigin = &fire.m_pos;
		EmitSound( filter, entindex(), sound );
		if ( inferno_scorch_decals.GetBool() && !bOnWater )
		{
			trace_t scorch;
			UTIL_TraceLine( firePos, firePos - Vector( 0, 0, 100 ), 0x4081, NULL, COLLISION_GROUP_NONE, &scorch );
			UTIL_DecalTrace( &scorch, "MolotovScorch" );
		}
	}
	return true;
}

void CInferno::Spread()
{
	// S384B97/JB rejects unordered now; a nonpositive/unordered deadline bypasses it.
	if ( m_spreadTimer.m_timestamp > 0.0f && !( gpGlobals->curtime >= m_spreadTimer.m_timestamp ) )
		return;
	m_spreadTimer.Start( GetSpreadInterval() );
	for ( int i = 0; i < m_fireCount; ++i )
	{
		FireInfo &fire = m_fire[i];
		if ( !( gpGlobals->curtime >= fire.m_spawnTimer.m_timestamp ) && gpGlobals->curtime >= fire.m_spawnInterval.m_timestamp )
		{
			fire.m_spawnInterval.Reset();
			++fire.m_nSpawnCredits;
		}
	}

	int nTraces = inferno_max_trace_per_tick.GetInt();
	int nLastIndex = m_nSpreadIndex + 1;
	for ( int i = 0; i < m_fireCount && nTraces > 0; ++i )
	{
		int nIndex = ( i + m_nSpreadIndex ) % m_fireCount;
		nLastIndex = nIndex;
		FireInfo &fire = m_fire[nIndex];
		int nDepth = fire.m_nDepth + 1;
		if ( !fire.m_nSpawnCredits || nDepth >= inferno_child_spawn_max_depth.GetInt() )
			continue;
		--fire.m_nSpawnCredits;
		// The trace allowance is checked per cell, not per attempt. A cell may spend
		// its remaining four attempts after the allowance has reached zero.
		for ( int attempt = 0; attempt < 4; ++attempt )
		{
			Vector direction;
			if ( fire.m_pParent )
			{
				Vector fromParent = fire.m_pos - fire.m_pParent->m_pos;
				VectorNormalize( fromParent );
				QAngle angles;
				VectorAngles( fromParent, angles );
				const float flSpawnAngle = m_fireType == ACID ? inferno_acid_spawn_angle.GetFloat() : inferno_spawn_angle.GetFloat();
				angles.y += RandomFloat( -flSpawnAngle, flSpawnAngle );
				AngleVectors( angles, &direction );
			}
			else
			{
				const double angle = static_cast< double >( RandomFloat( -3.1415901f, 3.1415901f ) );
				direction.Init( static_cast< float >( cos( angle ) ), static_cast< float >( sin( angle ) ), 0.0f );
			}
			// S384D4F..384D64 and S384E4E..384E59 spill the x87 results only here.
			const float flDecay = static_cast< float >( pow( static_cast< double >( inferno_velocity_decay_factor.GetFloat() ),
				static_cast< double >( fire.m_nDepth ) ) );
			float flRemaining = fire.m_lifetime.m_timestamp > 0.0f ?
				clamp( fire.m_lifetime.GetRemainingTime() / fire.m_lifetime.m_duration, 0.0f, 1.0f ) : 0.0f;
			direction += ( m_spreadVelocity * flRemaining ) * flDecay * inferno_velocity_factor.GetFloat();
			Vector across, tangent;
			CrossProduct( fire.m_normal, direction, across );
			CrossProduct( across, fire.m_normal, tangent );
			Vector candidate = fire.m_pos + tangent * RandomFloat( 50.0f, 75.0f );
			Vector fromOrigin = candidate - m_initialPosition;
			float flRange = inferno_max_range.GetFloat();
			if ( fromOrigin.LengthSqr() > flRange * flRange )
			{
				VectorNormalize( fromOrigin );
				candidate = m_initialPosition + fromOrigin * flRange;
			}
			trace_t ground;
			CTraceFilterSimple filter( NULL, COLLISION_GROUP_NONE );
			UTIL_TraceLine( candidate + Vector( 0, 0, 50 ),
				Vector( candidate.x, candidate.y, fire.m_pos.z - 200.0f ), 0x4003, &filter, &ground );
			--nTraces;
			if ( ground.DidHit() )
			{
				candidate.z = ground.endpos.z;
				Vector normal = ground.plane.normal;
				trace_t barrier;
				UTIL_TraceLine( fire.m_pos + Vector( 0, 0, 30 ), candidate + Vector( 0, 0, 30 ), 0x4003, &filter, &barrier );
				--nTraces;
				if ( barrier.fraction >= 1.0f )
				{
					if ( AddFire( candidate, normal, &fire, nDepth ) )
						break;
					continue;
				}
				if ( inferno_debug.GetBool() )
					NDebugOverlay::Line( fire.m_pos + Vector( 0, 0, 30 ), candidate + Vector( 0, 0, 30 ), 255, 0, 0, true, 1.0f );
			}
			else if ( inferno_debug.GetBool() )
				NDebugOverlay::Line( fire.m_pos, candidate, 255, 255, 0, true, 1.0f );
			m_spreadVelocity *= inferno_forward_reduction_factor.GetFloat();
		}
	}
	m_nSpreadIndex = nLastIndex + 1;
}

bool CInferno::IsTouching( CBaseEntity *pEntity, float flRadius, bool bCheckLOS ) const
{
	if ( !pEntity )
		return false;
	for ( int i = 0; i < m_fireCount; ++i )
	{
		Vector closest;
		if ( m_fireType == ACID && pEntity->GetGroundEntity() )
		{
			pEntity->CollisionProp()->CalcNearestPoint( m_fire[i].m_pos, &closest );
			if ( closest.z - m_fire[i].m_pos.z > flRadius * 0.33333334f )
				continue;
		}
		pEntity->CollisionProp()->CalcNearestPoint( m_fire[i].m_center, &closest );
		if ( !( ( closest - m_fire[i].m_center ).LengthSqr() < flRadius * flRadius ) )
			continue;
		if ( bCheckLOS )
		{
			trace_t trace;
			UTIL_TraceLine( m_fire[i].m_center, closest, 0x4003, NULL, COLLISION_GROUP_NONE, &trace );
			if ( inferno_debug.GetBool() )
				NDebugOverlay::Line( m_fire[i].m_center, closest, 255, 0, trace.fraction == 1.0f ? 255 : 0, true, 50.2f );
			if ( trace.fraction != 1.0f )
				continue;
		}
		else if ( inferno_debug.GetBool() )
			NDebugOverlay::Line( m_fire[i].m_center, closest, 255, 0, 255, true, 0.2f );
		return true;
	}
	return false;
}

bool CInferno::IsTouching( CNavArea *pArea ) const
{
	if ( !pArea )
		return false;
	float flRadius = m_flFireHalfWidth * 2.0f;
	if ( m_fireType == ACID )
		flRadius *= 2.0f;
	for ( int i = 0; i < m_fireCount; ++i )
	{
		Vector closest;
		pArea->GetClosestPointOnArea( m_fire[i].m_center, &closest );
		closest.z += m_fire[i].m_flWaterOffset;
		if ( ( closest - m_fire[i].m_center ).LengthSqr() < flRadius * flRadius )
			return true;
	}
	return false;
}

void CInferno::DamageEntities()
{
	m_damageTimer.Start( 0.2f );
	CBaseEntity *entities[256];
	CBaseEntity *victims[256];
	int nVictims = 0;
	int nEntities = UTIL_EntitiesInBox( entities, ARRAYSIZE( entities ), m_mins, m_maxs, 0 );
	for ( int i = 0; i < nEntities; ++i )
	{
		CBaseEntity *pEntity = entities[i];
		if ( pEntity && pEntity != this && pEntity->IsAlive() &&
			IsTouching( pEntity, m_flFireHalfWidth * 2.0f, pEntity->IsPlayer() ) )
			victims[nVictims++] = pEntity;
	}
	CBaseEntity *pOwner = GetOwnerEntity();
	int nDamageType = GetDamageType();
	// 0x800 is the ORIGINAL L4D2 attribution bit; it is not target SDK damage policy.
	if ( m_burnTimer.GetElapsedTime() > inferno_friendly_fire_duration.GetFloat() || !pOwner )
		nDamageType |= 0x800;
	// S385CDD..385CE8: multiply the x87-returned DPS, then spill once to float.
	float flDamage = static_cast< float >( GetDamagePerSecond() *
		static_cast< double >( m_damageTimer.GetCountdownDuration() ) );
	if ( !( gpGlobals->curtime >= m_damageRampTimer.m_timestamp ) )
	{
		// S385D04..385D23: subtract/add in x87, then one float spill before division.
		const float flRampElapsed = static_cast< float >( static_cast< double >( gpGlobals->curtime ) -
			static_cast< double >( static_cast< float >( m_damageRampTimer.m_timestamp ) ) +
			static_cast< double >( static_cast< float >( m_damageRampTimer.m_duration ) ) );
		flDamage *= flRampElapsed / m_damageRampTimer.GetCountdownDuration();
	}
	for ( int i = 0; i < nVictims; ++i )
	{
		CBaseEntity *pEntity = victims[i];
		if ( !CanHarm( pEntity ) )
			continue;
		CTakeDamageInfo info( this, pOwner, flDamage, nDamageType, GetDamageCustom() );
		if ( m_fireType != ACID )
		{
			CTerrorPlayer *pPlayer = ToTerrorPlayer( pEntity );
			if ( pPlayer && ( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
				info.SetDamage( flDamage * pPlayer->GetFriendlyFireDamageMultiplier( info ) );
			else if ( pEntity->MyInfectedPointer() != NULL )
				info.SetDamage( flDamage * 0.2f );
		}
		pEntity->TakeDamage( info );
		if ( m_fireType == ACID )
		{
			CTerrorPlayer *pPlayer = ToTerrorPlayer( pEntity );
			if ( pPlayer && ( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
			{
				CFuncElevator *pElevator = dynamic_cast< CFuncElevator * >( pPlayer->GetGroundEntity() );
				if ( pElevator && pElevator->IsMoving() )
					UTIL_Remove( this );
			}
		}
	}
}

void CInferno::MarkNavAreas()
{
	Extent extent;
	const float flPadding = m_fireType == ACID ? 64.0f : 32.0f;
	extent.lo = m_mins - Vector( flPadding, flPadding, flPadding );
	extent.hi = m_maxs + Vector( flPadding, flPadding, flPadding );
	m_damagingAreas.RemoveAll();
	TheNavMesh->CollectAreasOverlappingExtent( extent, &m_damagingAreas );
	FOR_EACH_VEC( m_damagingAreas, i )
	{
		if ( IsTouching( m_damagingAreas[i] ) )
			m_damagingAreas[i]->MarkAsDamaging( 1.0f );
	}
}

void CInferno::InfernoThink()
{
	bool bAllExpired = true;
	for ( int i = 0; i < m_fireCount; ++i )
	{
		FireInfo &fire = m_fire[i];
		if ( !( gpGlobals->curtime >= fire.m_lifetime.m_timestamp ) )
		{
			fire.m_pos = GetAbsOrigin() + Vector( m_fireXDelta[i], m_fireYDelta[i], m_fireZDelta[i] );
			bAllExpired = false;
			if ( GetMoveParent() )
				RecomputeBounds();
			if ( inferno_debug.GetBool() )
				NDebugOverlay::Sphere( fire.m_pos, m_flFireHalfWidth * 2.0f, 255, 100, 0, true, 0.1f );
		}
		else if ( fire.m_pos != vec3_origin )
		{
			fire.m_pos.Init();
			m_fireXDelta.Set( i, 0 );
			m_fireYDelta.Set( i, 0 );
			m_fireZDelta.Set( i, 0 );
		}
	}
	if ( bAllExpired )
	{
		UTIL_Remove( this );
		return;
	}
	if ( m_fireCount < MIN( m_nMaxFlames, MAX_INFERNO_FIRES ) )
		Spread();
	if ( gpGlobals->curtime >= m_damageTimer.m_timestamp )
		DamageEntities();
	MarkNavAreas();
	SetNextThink( gpGlobals->curtime + 0.1f );
}

bool CInferno::Extinguish( CBaseEntity *pExtinguisher )
{
	if ( pExtinguisher )
	{
		Vector mins, maxs;
		pExtinguisher->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
		if ( IsBoxIntersectingBox( mins, maxs, m_mins, m_maxs ) )
		{
			for ( int i = 0; i < m_fireCount; ++i )
			{
				if ( m_fire[i].m_lifetime.HasStarted() && !( gpGlobals->curtime >= m_fire[i].m_lifetime.m_timestamp ) )
				{
					Ray_t ray;
					ray.Init( m_fire[i].m_pos, m_fire[i].m_pos,
						Vector( -m_flFireHalfWidth, -m_flFireHalfWidth, -m_flFireHalfWidth ),
						Vector( m_flFireHalfWidth, m_flFireHalfWidth, m_flFireHalfWidth ) );
					trace_t trace;
					enginetrace->ClipRayToCollideable( ray, 0x200400B, pExtinguisher->GetCollideable(), &trace );
				}
			}
		}
	}
	// S:0x383750 deliberately discards the clip results and unconditionally returns
	// false. This is the pinned original's extinguisher bug, not an omitted producer.
	return false;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CFireCrackerBlast::Spawn()
{
	BaseClass::Spawn();
	m_fireType = FIREWORKS;
}

CFireCrackerBlast *CFireCrackerBlast::Create( CBaseEntity *pOwner, const Vector &position,
	const Vector &normal, const Vector &velocity, int nDepth )
{
	CFireCrackerBlast *pFireworks = static_cast< CFireCrackerBlast * >(
		CBaseEntity::Create( "fire_cracker_blast", position, vec3_angle, pOwner ) );
	if ( pFireworks )
		pFireworks->StartBurning( position, normal, velocity, nDepth );
	return pFireworks;
}

const char *CFireCrackerBlast::GetLoopParticleName() const { return "firework_crate_ground_effect"; }
const char *CFireCrackerBlast::GetStartParticleName() const { return "firework_crate_explosion_01"; }

// Native server 0x382ec0; 0x5c4800 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace fire_cracker_blastPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "fire_cracker_blast", 0x5c4800 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "fire_cracker_blast", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

// Native server 0x382e20; 0x5c47d8 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace infernoPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "inferno", 0x5c47d8 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "inferno", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
