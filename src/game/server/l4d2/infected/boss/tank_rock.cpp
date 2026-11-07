//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/tank_rock.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav_area.h"
#include "BasePropDoor.h"
#include "props.h"
#include "l4d2/effects/terror_physicsprop_te.h"
#include "physics.h"
#include "particle_parse.h"
#include "itempents.h"
#include "igameevents.h"
#include "NextBot/NextBotInterface.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

extern ConVar z_tank_throw_interval;
ConVar z_tank_throw_health( "z_tank_throw_health", "50", 0x00004004, "Health of the tank projectile (0 disables)" ); // S:5A0070
ConVar z_tank_rock_radius( "z_tank_rock_radius", "100", 0x00004004 ); // S:5A00A0
ConVar z_tank_rock_debug( "z_tank_rock_debug", "0", 0x00004004, "Show debug for the rock thinking about detonating on people." ); // S:5A00D0

LINK_ENTITY_TO_CLASS( tank_rock, CTankRock );
#ifndef L4D2
PRECACHE_REGISTER( tank_rock );
#endif
BEGIN_DATADESC( CTankRock ) // S:369350: functions only, no saved projectile fields.
	DEFINE_THINKFUNC( ProximityThink ),
	DEFINE_ENTITYFUNC( BounceTouch ),
END_DATADESC()

// S:367F80. In particular do not initialize material, launch flag or previous position here.
CTankRock::CTankRock() : m_rockDamageOverride( 0 ) {}

static const char *const s_tankRockModels[] =
{
	"models/props_debris/concrete_chunk01a.mdl", // S:7B0918, material 'C'.
	"models/props_foliage/tree_trunk.mdl" // S:7B0920, material 'O'.
};

// S:3671E0: set each model before precaching its prop_data break models.
void CTankRock::Precache()
{
	for ( int i = 0; i < ARRAYSIZE( s_tankRockModels ); ++i )
	{
		PrecacheModel( s_tankRockModels[i] );
		SetModel( s_tankRockModels[i] );
		PropBreakablePrecacheAll( GetModelName() );
	}
	PrecacheScriptSound( "HulkZombie.ThrownProjectileHit" );
	PrecacheScriptSound( "HulkZombie.Throw.FlyLoop" );
	BaseClass::Precache();
}

// S:368130/368030. Even the one-model material buckets consume RandomInt(0,0).
void CTankRock::Spawn()
{
	CBasePlayer *player = ToBasePlayer( GetThrower() );
	m_surfaceMaterial = player && player->GetSurfaceData() ? player->GetSurfaceData()->game.material : 'C';
	int model = m_surfaceMaterial == 'O' ? 1 : 0;
	model += random->RandomInt( 0, 0 );
	const char *name = s_tankRockModels[model];
	SetModel( name );
	DevMsg( "Tank rock %s\n", name );
	BaseClass::Spawn();
	SetModel( name );
	CBaseEntity *trail = CreateEntityByName( "info_particle_system" );
	if ( trail )
	{
		trail->KeyValue( "start_active", "1" );
		trail->KeyValue( "effect_name", m_surfaceMaterial == 'O' ? "tank_rock_throw_stump" : "tank_rock_throw_rock" );
		trail->SetParent( this );
		trail->SetLocalOrigin( vec3_origin );
		trail->SetLocalAngles( vec3_angle );
		DispatchSpawn( trail );
		trail->Activate();
	}
	UTIL_SetSize( this, Vector( -4, -4, -4 ), Vector( 4, 4, 4 ) );
	m_collisionExpansionTimer.Invalidate();
}

// S:368B30. Creation is the held, non-solid stage, not the launch stage.
CTankRock *CTankRock::Create( const Vector &origin, const QAngle &angles, const Vector &velocity, CBaseCombatCharacter *thrower )
{
	CTankRock *rock = static_cast< CTankRock * >( CBaseEntity::Create( "tank_rock", origin, angles, thrower ) );
	rock->SetAbsVelocity( velocity );
	rock->SetInitialVelocity( velocity );
	rock->SetThrower( thrower );
	rock->SetGravity( 0.55f );
	rock->SetFriction( 0.7f );
	rock->SetDamage( 100.0f );
	rock->ChangeTeam( thrower ? thrower->GetTeamNumber() : TEAM_UNASSIGNED );
	rock->SetTouch( &CTankRock::BounceTouch );
	rock->SetGravity( 0.4f );
	rock->SetFriction( 0.2f );
	rock->SetElasticity( 0.45f );
	rock->FollowEntity( thrower, true );
	rock->m_isLaunched = false;
	rock->SetSolid( SOLID_NONE );
	rock->SetTouch( NULL );
	rock->SetMoveType( MOVETYPE_NONE );
	rock->m_previousPosition = origin;
	return rock;
}

// S:3694B0: angles are intentionally not assigned here (the native arg is unused).
void CTankRock::Launch( const Vector &origin, const QAngle &angles, const Vector &velocity, const AngularImpulse &angularVelocity )
{
	m_isLaunched = true;
	SetTouch( &CTankRock::BounceTouch );
	StopFollowingEntity();
	SetAbsVelocity( velocity );
	SetAbsOrigin( origin );
	SetInitialVelocity( velocity );
	SetLocalAngularVelocity( QAngle( angularVelocity.x, angularVelocity.y, angularVelocity.z ) );
	m_collisionExpansionTimer.Start();
	EmitSound( "HulkZombie.Throw.FlyLoop" );
	SetThink( &CTankRock::ProximityThink );
	SetNextThink( gpGlobals->curtime );
	SetSolid( SOLID_BBOX );
	SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_CUSTOM );
}

// S:367740. The base's Event_Killed simply invokes virtual Detonate.
void CTankRock::Event_Killed( const CTakeDamageInfo &info )
{
	IGameEvent *event = gameeventmanager->CreateEvent( "tank_rock_killed" );
	if ( event )
	{
		CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() );
		if ( attacker ) event->SetInt( "userid", attacker->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	Detonate();
}

// S:3677F0. A held rock is deliberately not removed by this virtual.
void CTankRock::Detonate()
{
	if ( !m_isLaunched ) return;
	if ( z_tank_throw_interval.GetFloat() > 1.0f )
	{
		DispatchParticleEffect( m_surfaceMaterial == 'O' ? "tank_rock_throw_impact_stump" : "tank_rock_throw_impact", GetAbsOrigin(), GetAbsAngles() );
		CPASFilter filter( WorldSpaceCenter() );
		TE_PhysicsProp( filter, -1.0f, GetModelIndex(), m_nSkin, GetAbsOrigin(), GetAbsAngles(), GetAbsVelocity(), 1, GetEffects(), 255, 255, 255 );
	}
	StopSound( "HulkZombie.Throw.FlyLoop" );
	UTIL_Remove( this );
}

void CTankRock::BounceTouch( CBaseEntity *other ) // S:3673B0
{
	GrenadeTouch( other );
}

// S:3673C0: dead actors and objects with no health retain the incoming velocity.
void CTankRock::ResolveFlyCollisionCustom( trace_t &trace, Vector &velocity )
{
	CBaseEntity *other = trace.m_pEnt;
	if ( other->GetHealth() <= 0 || ( other->MyCombatCharacterPointer() && !other->IsAlive() ) )
		SetAbsVelocity( GetAbsVelocity() );
	else
		ResolveFlyCollisionBounce( trace, velocity, 0.0f );
}

static bool IsRockSurvivor( const CBaseEntity *entity )
{
	return entity->GetTeamNumber() == TEAM_SURVIVOR || entity->GetTeamNumber() == TEAM_L4D1_SURVIVOR;
}

static CBasePropDoor *FindClosedRockCheckpointDoor( const Vector &position )
{
	CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( gEntList.FindEntityByClassnameWithin( NULL, "prop_door*", position, 40.0f ) );
	return door && door->IsCheckpointDoor() && door->IsDoorClosed() ? door : NULL;
}

// S:3682F0, native vtable +1380. Stat production is BEFORE incap/damage and
// AFTER the opposite-checkpoint/closed-door early return (S:3685D5).
void CTankRock::GrenadeTouch( CBaseEntity *other )
{
	if ( other->IsSolidFlagSet( FSOLID_TRIGGER | FSOLID_VOLUME_CONTENTS ) && other->IsSolidFlagSet( FSOLID_NOT_SOLID ) ) return;
	CTerrorPlayer *thrower = ToTerrorPlayer( GetThrower() );
	if ( other == GetThrower() ) return;
	if ( z_tank_rock_debug.GetBool() ) NDebugOverlay::Box( GetAbsOrigin(), CollisionProp()->OBBMins(), CollisionProp()->OBBMaxs(), 255, 0, 0, 0, 10.0f );
	if ( FClassnameIs( other, "prop_minigun*" ) )
	{
		// S36843F uses the genuine ascending roster collector: team2,
		// living/non-ghost state2, humans|bots3, any-class9. NOT team2|3.
		CUtlVector< CTerrorPlayer * > players;
		CollectTerrorPlayers( players, TEAM_SURVIVOR, 2, 3, 9 );
		FOR_EACH_VEC( players, i )
		{
			if ( players[i]->GetUseEntity() == other )
			{
				other = players[i];
				break;
			}
		}
	}
	if ( other->m_takedamage != DAMAGE_NO )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( other );
		if ( thrower )
		{
			// S:266050 is queried before the door/stat/incap gates.
			static ConVarRef witchHealth( "z_witch_health" );
			const float damage = victim ? victim->GetTankHitDamage() : ( other->MyCombatCharacterPointer() ? witchHealth.GetFloat() : 999.90002f );
			if ( victim && IsRockSurvivor( victim ) )
			{
				CNavArea *victimArea = victim->GetLastKnownArea();
				CNavArea *throwerArea = thrower->GetLastKnownArea();
				if ( victimArea && throwerArea && ToTerrorNavArea( victimArea )->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) != ToTerrorNavArea( throwerArea )->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) && FindClosedRockCheckpointDoor( WorldSpaceCenter() ) ) return;
				thrower->RecordTankRockHit( victim->GetSurvivorCharacter() );
			}
			if ( !victim || !victim->IsIncapacitated() )
			{
				bool wasIncapacitated = false;
				if ( victim && IsRockSurvivor( victim ) )
				{
					CTerrorPlayer *pinner = victim->GetPounceAttacker();
					if ( pinner )
					{
						trace_t trace;
						trace = GetTouchTrace();
						Vector direction;
						AngleVectors( GetLocalAngles(), &direction );
						ClearMultiDamage();
						CTakeDamageInfo info( this, GetThrower(), 250.0f, DMG_CLUB );
						CalculateMeleeDamageForce( &info, GetAbsVelocity(), GetAbsOrigin() );
						pinner->DispatchTraceAttack( info, direction, &trace );
						ApplyMultiDamage();
					}
					CTerrorPlayer *tongueOwner = victim->GetTongueOwner();
					if ( tongueOwner ) tongueOwner->ReleaseTongue( false );
					wasIncapacitated = victim->IsIncapacitated();
				}
				trace_t trace;
				trace = GetTouchTrace();
				Vector direction;
				AngleVectors( GetLocalAngles(), &direction );
				ClearMultiDamage();
				CTakeDamageInfo info( this, GetThrower(), damage, DMG_CLUB );
				CalculateMeleeDamageForce( &info, GetAbsVelocity(), GetAbsOrigin() );
				other->DispatchTraceAttack( info, direction, &trace );
				ApplyMultiDamage();
				if ( victim && IsRockSurvivor( victim ) && !victim->IsIncapacitated() ) victim->Knockdown( 2 );
				if ( victim )
				{
					if ( thrower->IsBot() ) thrower->OnTankPhysicsHitVictim( victim, !wasIncapacitated && victim->IsIncapacitated() );
					if ( IsRockSurvivor( victim ) ) thrower->ResetFrustration();
				}
			}
		}
		else if ( GetTeamNumber() == TEAM_UNASSIGNED )
		{
			static ConVarRef witchHealth( "z_witch_health" );
			float damage = m_rockDamageOverride > 0 ? static_cast< float >( m_rockDamageOverride ) : ( victim ? victim->GetTankHitDamage() : ( other->MyCombatCharacterPointer() ? witchHealth.GetFloat() : 999.90002f ) );
			if ( !victim || !victim->IsIncapacitated() )
			{
				if ( victim && victim->GetTeamNumber() == TEAM_INFECTED ) damage = 250.0f;
				trace_t trace;
				trace = GetTouchTrace();
				Vector direction;
				AngleVectors( GetLocalAngles(), &direction );
				ClearMultiDamage();
				CTakeDamageInfo info( this, GetThrower(), damage, DMG_BLAST );
				CalculateMeleeDamageForce( &info, GetAbsVelocity(), GetAbsOrigin() );
				other->DispatchTraceAttack( info, direction, &trace );
				ApplyMultiDamage();
				if ( victim && victim->GetTeamNumber() == TEAM_SURVIVOR && !victim->IsIncapacitated() ) victim->Knockdown( 2 );
			}
		}
	}
	if ( ( other->GetTeamNumber() != TEAM_INFECTED || !dynamic_cast< INextBot * >( other ) ) && other->IsAlive() ) Detonate();
	EmitSound( "HulkZombie.ThrownProjectileHit" );
	UTIL_ScreenShake( GetAbsOrigin(), 50.0f, 150.0f, 1.0f, 750.0f, SHAKE_START );
}

// S:368CC0. Returns false after a near segment encounter even if the trace was blocked.
bool CTankRock::CheckProximity( CBasePlayer *basePlayer )
{
	CTerrorPlayer *player = ToTerrorPlayer( basePlayer );
	// The native functor assumes the collected entity is a player.
	if ( !player->IsAlive() || player->IsIncapacitated() || player == GetThrower() ) return true;
	if ( GetThrower() && GetThrower()->GetTeamNumber() == player->GetTeamNumber() ) return true;
	CNavArea *area = player->GetLastKnownArea();
	if ( area && ToTerrorNavArea( area )->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) return true;
	float radius = z_tank_rock_radius.GetFloat();
	if ( TerrorGameRules()->GetDifficultyLevel() == 0 ) radius *= 0.75f;
	Vector segment = GetAbsOrigin() - m_previousPosition;
	const float length = VectorNormalize( segment );
	const float along = DotProduct( player->WorldSpaceCenter() - m_previousPosition, segment );
	const Vector closest = along < 0.0f ? m_previousPosition : along > length ? GetAbsOrigin() : m_previousPosition + segment * along;
	if ( z_tank_rock_debug.GetBool() ) NDebugOverlay::Line( m_previousPosition, GetAbsOrigin(), 255, 0, 0, true, 10.0f );
	if ( !( radius * radius > ( player->WorldSpaceCenter() - closest ).LengthSqr() ) ) return true;
	CTraceFilterSkipTwoEntities filter( this, player, COLLISION_GROUP_NONE );
	trace_t trace;
	UTIL_TraceLine( closest, player->WorldSpaceCenter(), MASK_SOLID, &filter, &trace );
	if ( !trace.startsolid && trace.fraction >= 1.0f )
	{
		SetAbsOrigin( closest );
		GrenadeTouch( player );
	}
	return false;
}

// S:369010. A timer stamped at exactly time zero never expands: retain >0 test.
void CTankRock::ProximityThink()
{
	if ( m_collisionExpansionTimer.GetStartTime() > 0.0f )
	{
		const float elapsed = m_collisionExpansionTimer.GetElapsedTime();
		if ( elapsed >= 0.5f )
		{
			m_collisionExpansionTimer.Invalidate();
			UTIL_SetSize( this, Vector( -16, -16, -16 ), Vector( 16, 16, 16 ) );
			if ( z_tank_throw_health.GetInt() > 0 )
			{
				SetHealth( z_tank_throw_health.GetInt() );
				m_takedamage = DAMAGE_YES;
			}
		}
		else
		{
			const float size = ( 1.0f - ( 0.5f - elapsed ) * 2.0f ) * 12.0f + 4.0f;
			UTIL_SetSize( this, Vector( -size, -size, -size ), Vector( size, size, size ) );
		}
	}
	SetNextThink( gpGlobals->curtime );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( player && player->edict() && player->IsConnected() && !CheckProximity( player ) ) return;
	}
	CBasePropDoor *door = FindClosedRockCheckpointDoor( WorldSpaceCenter() );
	if ( door )
	{
		GrenadeTouch( door );
		return;
	}
	m_previousPosition = GetAbsOrigin();
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x3671a0; 0x5c4684 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace tank_rockPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "tank_rock", 0x5c4684 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "tank_rock", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
