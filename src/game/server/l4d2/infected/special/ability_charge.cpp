//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/special/ability_charge.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/infected/special_infected_target_scan.h"
#include "particle_parse.h"
#include "vphysics_interface.h"
#include "in_buttons.h"
#include "l4d2/sound/terror_music.h"
#include "ipredictionsystem.h"
#include "datacache/imdlcache.h"
#include "tier0/memdbgon.h"

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

extern ConVar z_charge_duration, z_charge_max_speed, z_charge_interval, z_charge_max_force, z_charge_min_force, z_charge_max_damage, z_charge_tooshort;
ConVar z_charge_prop_damage( "z_charge_prop_damage", "20", 0x00004004 );
ConVar z_charger_max_prop_force( "z_charger_max_prop_force", "3000", 0x00004004 );
ConVar z_charge_impact_radius( "z_charge_impact_radius", "120", 0x00004004 );
ConVar z_charger_impact_epsilon( "z_charger_impact_epsilon", "8", 0x00004004 );
ConVar z_charger_probe_alone( "z_charger_probe_alone", "6", 0x00004004 );
ConVar z_charger_probe_attack( "z_charger_probe_attack", "24", 0x00004004 );
ConVar z_charge_impact_angle( "z_charge_impact_angle", ".7", 0x00004004 );
LINK_ENTITY_TO_CLASS( ability_charge, CCharge );

static float ChargeProgress( float start )
{
	float progress = ( gpGlobals->curtime - start ) / z_charge_duration.GetFloat();
	if ( progress > 1.0f ) progress = 1.0f;
	else if ( progress < 0.5f ) progress = 0.5f;
	return progress;
}
static void LaunchChargeVictim( CTerrorPlayer *charger, CTerrorPlayer *victim, float progress, bool doDamage )
{
	Vector direction = charger->GetAbsVelocity();
	direction.z = 0.0f;
	VectorNormalize( direction );
	direction.z = 0.9f;
	VectorNormalize( direction );
	if ( victim->GetMoveType() == MOVETYPE_LADDER ) victim->SetMoveType( MOVETYPE_WALK );
	float damage = z_charge_max_damage.GetFloat();
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 0: damage *= 0.5f; break;
	case 2: damage *= 1.5f; break;
	case 3: damage *= 2.0f; break;
	}
	const float force = MAX( z_charge_min_force.GetFloat(), z_charge_max_force.GetFloat() * progress );
	if ( doDamage )
	{
		CTakeDamageInfo info( charger, charger, damage, DMG_CLUB );
		CalculateMeleeDamageForce( &info, direction, charger->GetAbsOrigin(), 0.01f );
		victim->TakeDamage( info );
	}
	victim->Launch( direction * force, TERROR_ANIMEVENT_CHARGER_IMPACT, charger, 3.0f );
	IGameEvent *event = gameeventmanager->CreateEvent( "charger_impact" );
	if ( event )
	{
		event->SetInt( "userid", charger->GetUserID() );
		event->SetInt( "victim", victim->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
}
bool CCharge::HasHitSurvivor() const
{
	for ( int i = 0; i < 8; ++i ) if ( m_hitSurvivor[i] ) return true;
	return false;
}
void CCharge::StaggerFromImpact()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	Vector forward;
	owner->GetVectors( &forward, NULL, NULL );
	const Vector position = owner->GetAbsOrigin() + forward * 12.0f;
	owner->StaggerPlayer( NULL, &position );
	owner->EmitInfectedSound( "ChargerZombie.Stagger", 0.0f, 0.0f );
}
void CCharge::ImpactShock()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	UTIL_ScreenShake( owner->GetAbsOrigin(), 5.0f, 150.0f, 1.5f, z_charge_impact_radius.GetFloat(), SHAKE_START );
	const float capturedRadius = z_charge_impact_radius.GetFloat();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *target = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !target || !target->IsConnected() || !target->IsAlive() || target->IsIncapacitated() ) continue;
		const int team = target->GetTeamNumber();
		if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) && m_hitSurvivor[target->GetSurvivorCharacter()] ) continue;
		CTerrorPlayer *playerOwner = GetOwner();
		if ( !playerOwner ) continue;
		const Vector &ownerCenter = playerOwner->WorldSpaceCenter();
		const Vector delta = target->WorldSpaceCenter() - ownerCenter;
		const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		if ( !( capturedRadius >= distance ) ) continue;
		const Vector position = playerOwner->GetAbsOrigin();
		target->StaggerPlayer( playerOwner, &position );
		CTakeDamageInfo info( playerOwner, playerOwner, 2.0f, DMG_CLUB );
		// S361510 genuinely uses negative world origin, not target-source.
		CalculateMeleeDamageForce( &info, -position, playerOwner->GetAbsOrigin(), 0.01f );
		target->TakeDamage( info );
	}
}
void CCharge::UpdatePotentialTarget()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || owner->IsGhost() || m_isCharging ) return;
	MDLCACHE_CRITICAL_SECTION();
	ChargeTargetScan scan( owner, ( z_charge_duration.GetFloat() * z_charge_max_speed.GetFloat() ) * 0.9f );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *target = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		// S361BD0's iterator, unlike Tongue's, admits TEAM2 only.
		if ( target && target->edict() && target->entindex() != 0 && target->IsConnected() &&
			target->GetTeamNumber() == TEAM_SURVIVOR && !scan( target ) ) break;
	}
	m_hPotentialTarget = scan.GetTarget();
}

void CCharge::ProbeAhead()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || !( owner->GetFlags() & FL_ONGROUND ) ) return;
	Vector forward;
	owner->GetVectors( &forward, NULL, NULL );
	CTerrorPlayer *victim = owner->GetCarryVictim();
	const Vector start = owner->WorldSpaceCenter();
	const float distance = victim ? z_charger_probe_attack.GetFloat() : z_charger_probe_alone.GetFloat();
	unsigned int mask = owner->PlayerSolidMask();
	if ( victim ) mask |= victim->PlayerSolidMask();
	CTraceFilterSimple filter( NULL, COLLISION_GROUP_PLAYER_MOVEMENT );
	trace_t trace;
	UTIL_TraceHull( start, start + forward * distance, Vector( -15, -15, -12 ), Vector( 15, 15, 24 ), mask, &filter, &trace );
	CBaseEntity *hit = trace.m_pEnt;
	if ( hit && hit->VPhysicsGetObject() )
	{
		if ( victim && FClassnameIs( hit, "prop_car_alarm" ) )
		{
			CTakeDamageInfo info( victim, victim, 1.0f, DMG_CLUB );
			info.SetDamageForce( owner->GetAbsVelocity() );
			info.SetDamagePosition( hit->WorldSpaceCenter() );
			hit->TakeDamage( info );
			return;
		}
		if ( hit->VPhysicsGetObject()->GetMass() < 250.0f )
		{
			// S362927 INC wraps all 32 bits before signed CVTSI2SS.
			const uint32 damageHealthBits = static_cast< uint32 >( hit->GetHealth() ) + uint32( 1 );
			int32 damageHealth;
			Q_memcpy( &damageHealth, &damageHealthBits, sizeof( damageHealth ) );
			CTakeDamageInfo info( owner, owner, static_cast< float >( damageHealth ), DMG_CLUB );
			info.SetDamageForce( owner->GetAbsVelocity() );
			info.SetDamagePosition( hit->WorldSpaceCenter() );
			hit->TakeDamage( info );
			return;
		}
	}
	if ( trace.fraction < 1.0f && ( !hit || !hit->IsPlayer() ) &&
		( victim || !( static_cast< double >( z_charge_impact_angle.GetFloat() ) >=
			fabs( ( static_cast< double >( trace.plane.normal.y ) * forward.y +
				static_cast< double >( trace.plane.normal.x ) * forward.x ) +
				static_cast< double >( trace.plane.normal.z ) * forward.z ) ) ) )
	{
		if ( victim ) owner->ImpactChargerVictim( victim, true, m_carryStartPosition.z - owner->GetAbsOrigin().z > 360.0f );
		else
		{
			if ( !HasHitSurvivor() ) StaggerFromImpact();
			owner->EmitSound( "ChargerZombie.ImpactHard" );
			DispatchParticleEffect( "charger_wall_impact", owner->WorldSpaceCenter(), vec3_angle );
			EndCharge();
			ImpactShock();
		}
	}
}

bool CCharge::OnPlayerMoveCollision( CBaseEntity *hit, const Vector &hitPosition, const Vector &direction, const trace_t &trace, CMoveData *move )
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || !m_isCharging ) return false;
	const float travelSquared = ( m_chargeStartPosition - owner->GetAbsOrigin() ).LengthSqr();
	const int team = hit->GetTeamNumber();
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( hit );
		if ( !victim || victim->IsIncapacitated() ) return false;
		bool &alreadyHit = m_hitSurvivor[victim->GetSurvivorCharacter()];
		if ( alreadyHit ) return false;
		alreadyHit = true;
		if ( z_charge_tooshort.GetFloat() * z_charge_tooshort.GetFloat() <= travelSquared )
		{
			victim->GetMusic()->OnStruck();
			const float progress = ChargeProgress( m_chargeStartTime );
			owner->EmitSound( "ChargerZombie.HitPerson" );
			++owner->m_checkpointPZNumChargeVictims;
			if ( owner->GetCarryVictim() ) LaunchChargeVictim( owner, victim, progress, true );
			else
			{
				trace_t carryTrace;
				CTraceFilterSimple filter( victim, COLLISION_GROUP_PLAYER_MOVEMENT );
				const Vector start = victim->WorldSpaceCenter();
				UTIL_TraceHull( start, start + direction * 34.0f, Vector( -13, -13, -24 ), Vector( 13, 13, 24 ), owner->PlayerSolidMask(), &filter, &carryTrace );
				if ( carryTrace.fraction >= 1.0f )
				{
					owner->CarryVictim( victim );
					m_carryStartPosition = owner->GetAbsOrigin();
				}
				else owner->ImpactChargerVictim( victim, true, false );
			}
		}
		else
		{
			EndCharge();
			owner->StaggerPlayer( victim, NULL );
			victim->StaggerPlayer( owner, NULL );
			owner->EmitSound( "Zombie.Punch" );
			SetRechargeTime( z_charge_interval.GetFloat() * 0.5f, 0.0f );
		}
		return true;
	}
	if ( team == TEAM_INFECTED ) return true;
	ProbeAhead();
	if ( m_collisionTimer.IsElapsed() ) m_collisionTimer.Start( 0.25f );
	if ( hit->IsWorld() ) return true;
	if ( FClassnameIs( hit, "prop_door_rotating" ) )
	{
		CDisablePredictionFiltering disablePredictionFiltering;
		CTakeDamageInfo info( owner, owner, 1000.0f, DMG_CLUB );
		CalculateMeleeDamageForce( &info, owner->BodyDirection3D(), owner->GetAbsOrigin(), 0.01f );
		hit->TakeDamage( info );
	}
	else if ( FClassnameIs( hit, "prop_physics" ) && hit->VPhysicsGetObject() )
	{
		Vector force = hit->WorldSpaceCenter() - owner->GetAbsOrigin();
		VectorNormalize( force );
		force *= z_charger_max_prop_force.GetFloat();
		CDisablePredictionFiltering disablePredictionFiltering;
		CTakeDamageInfo info( owner, owner, force, owner->GetAbsOrigin(), z_charge_prop_damage.GetFloat(), DMG_CLUB );
		hit->TakeDamage( info );
		if ( owner->GetCarryVictim() && FStrEq( STRING( gpGlobals->mapname ), "c6m1_riverbank" ) &&
			FStrEq( STRING( hit->GetModelName() ), "models/props_urban/plastic_chair001_debris.mdl" ) )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "chair_charged" );
			if ( event ) { event->SetInt( "userid", owner->GetUserID() ); gameeventmanager->FireEvent( event ); }
		}
	}
	return true;
}

void CCharge::Update()
{
	if ( !m_isCharging ) { UpdatePotentialTarget(); return; }
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	CTerrorPlayer *victim = owner->GetCarryVictim();
	if ( m_hadVictim && !victim ) { EndCharge(); return; }
	if ( gpGlobals->curtime - m_chargeStartTime >= z_charge_duration.GetFloat() && ( owner->GetFlags() & FL_ONGROUND ) )
	{
		if ( !victim ) { EndCharge(); return; }
		owner->ImpactChargerVictim( victim, false, m_carryStartPosition.z - owner->GetAbsOrigin().z > 360.0f );
		return;
	}
	if ( m_probeTimer.IsElapsed() && !victim )
	{
		const Vector delta = m_lastProbePosition - owner->GetAbsOrigin();
		const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		if ( z_charger_impact_epsilon.GetFloat() >= distance )
		{
			ImpactShock();
			EndCharge();
			if ( !HasHitSurvivor() ) StaggerFromImpact();
			return;
		}
		m_lastProbePosition = owner->GetAbsOrigin();
		m_probeTimer.Start( 0.1f );
	}
	else if ( victim )
	{
		// S36399E: the owner velocity is compared EXACTLY to zero; it is not
		// cleared here. The final ground flag is on the ABILITY, not owner.
		const float progress = ( gpGlobals->curtime - m_chargeStartTime ) / z_charge_duration.GetFloat();
		if ( progress >= 1.0f && owner->GetAbsVelocity() == vec3_origin && !( GetFlags() & FL_ONGROUND ) )
		{
			owner->ImpactChargerVictim( victim, false, false );
			return;
		}
	}
	m_hadVictim = victim != NULL;
	if ( victim ) ProbeAhead();
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
