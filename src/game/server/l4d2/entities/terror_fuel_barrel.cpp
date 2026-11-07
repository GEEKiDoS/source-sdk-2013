#include "cbase.h"
#include "props.h"
#include "l4d2/weapons/inferno.h"
#include "l4d2/terror_shareddefs.h"
#include "particle_parse.h"
#include "ndebugoverlay.h"
#include "tier0/memdbgon.h"

ConVar fuel_barrel_debug( "fuel_barrel_debug", "0", 0x00004004 );
ConVar fuel_barrel_health( "fuel_barrel_health", "20", 0x00004004 );
ConVar fuel_barrel_damage_radius( "fuel_barrel_damage_radius", "400", 0x00004004 );
ConVar fuel_barrel_damage_amount( "fuel_barrel_damage_amount", "25", 0x00004004 );
ConVar fuel_barrel_screen_shake_amplitude( "fuel_barrel_screen_shake_amplitude", "20", 0x00004004 );
ConVar fuel_barrel_screen_shake_radius( "fuel_barrel_screen_shake_radius", "1000", 0x00004004 );
ConVar fuel_barrel_screen_shake_frequency( "fuel_barrel_screen_shake_frequency", "2.5", 0x00004004 );
ConVar fuel_barrel_screen_shake_duration( "fuel_barrel_screen_shake_duration", "1.5", 0x00004004 );
ConVar fuel_barrel_flame_instances( "fuel_barrel_flame_instances", "8", 0x00004004 );

static float FuelBarrelRandomSpin()
{
	const float unit = static_cast< float >( RandomInt( 0, 32767 ) ) * 0.000030518509f;
	return unit * 40.0f - 20.0f;
}
class CFuelBarrelPiece : public CPhysicsProp
{
public:
	DECLARE_CLASS( CFuelBarrelPiece, CPhysicsProp );
	CFuelBarrelPiece() { m_hFlyingParticle = NULL; }
	void Spawn() OVERRIDE { BaseClass::Spawn(); m_takedamage = DAMAGE_NO; }
	void UpdateOnRemove() OVERRIDE { UTIL_Remove( m_hFlyingParticle.Get() ); BaseClass::UpdateOnRemove(); }
	void VPhysicsCollision( int index, gamevcollisionevent_t *event ) OVERRIDE;
	void SetFlyingParticle( CBaseEntity *particle ) { m_hFlyingParticle = particle; }
protected:
	void OnBreak( const Vector &velocity, const AngularImpulse &angular, CBaseEntity *breaker ) OVERRIDE
	{
		CInferno *inferno = static_cast< CInferno * >( CBaseEntity::Create( "inferno", GetAbsOrigin(), GetAbsAngles(), NULL ) );
		if ( inferno )
		{
			inferno->SetMaxFlames( fuel_barrel_flame_instances.GetInt() );
			inferno->StartBurning( GetAbsOrigin(), Vector( 0, 0, 1 ), Vector( 0, 0, 1 ), 0 );
		}
		BaseClass::OnBreak( velocity, angular, breaker );
	}
private:
	EHANDLE m_hFlyingParticle; // native5760, intentionally no datamap field
};
LINK_ENTITY_TO_CLASS( prop_fuel_barrel_piece, CFuelBarrelPiece );

void CFuelBarrelPiece::VPhysicsCollision( int index, gamevcollisionevent_t *event )
{
	BaseClass::VPhysicsCollision( index, event );
	CBaseEntity *other = event->pEntities[!index];
	if ( fuel_barrel_debug.GetInt() >= 2 )
	{
		Msg( "====> prop_fuel_barrel_piece collision <====\n" );
		Msg( "other: %s\n", other ? other->GetClassname() : "NULL" );
		Msg( "preVelocity this  : %f, %f, %f\n", event->preVelocity[index].x, event->preVelocity[index].y, event->preVelocity[index].z );
		Msg( "preVelocity other : %f, %f, %f\n", event->preVelocity[!index].x, event->preVelocity[!index].y, event->preVelocity[!index].z );
		Msg( "postVelocity this : %f, %f, %f\n", event->postVelocity[index].x, event->postVelocity[index].y, event->postVelocity[index].z );
		Msg( "postVelocity other: %f, %f, %f\n", event->postVelocity[!index].x, event->postVelocity[!index].y, event->postVelocity[!index].z );
	}
	if ( !other ) return;
	Vector contact, normal;
	event->pInternalData->GetContactPoint( contact );
	event->pInternalData->GetSurfaceNormal( normal );
	if ( index == 0 ) normal = -normal;
	trace_t trace;
	UTIL_TraceLine( contact + normal, contact - normal, 0x0200400B, this, COLLISION_GROUP_NONE, &trace );
	if ( trace.surface.flags & SURF_SKY ) return;
	if ( other->IsSolidFlagSet( FSOLID_TRIGGER | FSOLID_VOLUME_CONTENTS ) ) return;
	if ( fuel_barrel_debug.GetBool() )
	{
		NDebugOverlay::Cross3D( contact, 10.0f, 255, 0, 0, true, 5.0f );
		NDebugOverlay::Line( contact, contact + normal * 20.0f, 255, 0, 0, true, 5.0f );
	}
	if ( event->preVelocity[index].z < 0.0f && event->postVelocity[index].z > event->preVelocity[index].z )
	{
		m_hBreaker = other;
		SetThink( &CBreakableProp::BreakThink );
		SetNextThink( gpGlobals->curtime );
	}
}

class CFuelBarrelProp : public CPhysicsProp
{
public:
	DECLARE_CLASS( CFuelBarrelProp, CPhysicsProp );
	DECLARE_DATADESC();
	CFuelBarrelProp() : m_hAttacker( NULL ), m_BasePiece( NULL_STRING ), m_DetonateParticles( NULL_STRING ), m_FlyingParticles( NULL_STRING ), m_DetonateSound( NULL_STRING )
	{
		for ( int i = 0; i < ARRAYSIZE( m_FlyingPiece ); ++i ) m_FlyingPiece[i] = NULL_STRING;
	}
	void Precache() OVERRIDE;
	void Spawn() OVERRIDE
	{
		BaseClass::Spawn();
		m_iHealth = fuel_barrel_health.GetInt();
		m_takedamage = DAMAGE_YES;
		m_flBurnDamageMultiplier = static_cast< float >( RandomInt( 1, 4 ) );
		RemoveFlag( FL_AIMTARGET );
	}
	int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;
	void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE;
protected:
	void OnBreak( const Vector &velocity, const AngularImpulse &angular, CBaseEntity *breaker ) OVERRIDE
	{
		BaseClass::OnBreak( velocity, angular, breaker );
		if ( !m_hAttacker.Get() ) m_hAttacker = breaker ? breaker->MyCombatCharacterPointer() : NULL;
		Detonate();
	}
private:
	void Detonate();
	string_t m_BasePiece;
	string_t m_FlyingPiece[4];
	string_t m_DetonateParticles, m_FlyingParticles, m_DetonateSound;
	float m_flBurnDamageMultiplier; // native5792, no SAVE/wire
	EHANDLE m_hAttacker; // native5796, no SAVE/wire
};
LINK_ENTITY_TO_CLASS( prop_fuel_barrel, CFuelBarrelProp );
BEGIN_DATADESC( CFuelBarrelProp )
	DEFINE_KEYFIELD( m_BasePiece, FIELD_MODELNAME, "BasePiece" ),
	DEFINE_KEYFIELD( m_FlyingPiece[0], FIELD_MODELNAME, "FlyingPiece01" ),
	DEFINE_KEYFIELD( m_FlyingPiece[1], FIELD_MODELNAME, "FlyingPiece02" ),
	DEFINE_KEYFIELD( m_FlyingPiece[2], FIELD_MODELNAME, "FlyingPiece03" ),
	DEFINE_KEYFIELD( m_FlyingPiece[3], FIELD_MODELNAME, "FlyingPiece04" ),
	DEFINE_KEYFIELD( m_DetonateParticles, FIELD_STRING, "DetonateParticles" ),
	DEFINE_KEYFIELD( m_FlyingParticles, FIELD_STRING, "FlyingParticles" ),
	DEFINE_KEYFIELD( m_DetonateSound, FIELD_SOUNDNAME, "DetonateSound" ),
END_DATADESC()
void CFuelBarrelProp::Precache()
{
	BaseClass::Precache();
	if ( m_BasePiece != NULL_STRING ) PrecacheModel( STRING( m_BasePiece ) );
	for ( int i = 0; i < ARRAYSIZE( m_FlyingPiece ); ++i ) if ( m_FlyingPiece[i] != NULL_STRING ) PrecacheModel( STRING( m_FlyingPiece[i] ) );
	if ( m_DetonateParticles != NULL_STRING ) PrecacheParticleSystem( STRING( m_DetonateParticles ) );
	if ( m_FlyingParticles != NULL_STRING ) PrecacheParticleSystem( STRING( m_FlyingParticles ) );
	if ( m_DetonateSound != NULL_STRING ) PrecacheScriptSound( STRING( m_DetonateSound ) );
}
#pragma float_control( precise, on, push )
int CFuelBarrelProp::OnTakeDamage( const CTakeDamageInfo &info )
{
	const int type = info.GetDamageType();
	if ( !( type & 0x2000004A ) ) return 0;
	CBaseEntity *inflictor = info.GetInflictor();
	if ( ( type & DMG_BLAST ) && inflictor && FClassnameIs( inflictor, "prop_fuel_barrel" ) ) return 0;
	CBaseEntity *attacker = info.GetAttacker();
	if ( ( type & 0x20000002 ) && attacker && attacker->IsPlayer() && attacker->GetTeamNumber() == TEAM_SURVIVOR )
	{
		const float nearDistance = ( type & 0x20000000 ) ? 600.0f : 1000.0f;
		const float farDistance = ( type & 0x20000000 ) ? 1000.0f : 1800.0f;
		const Vector delta = GetAbsOrigin() - attacker->GetAbsOrigin();
		const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		if ( distance > farDistance ) return 0;
		if ( distance > nearDistance )
		{
			CTakeDamageInfo scaled( info );
			scaled.ScaleDamage( 1.0f - ( distance - nearDistance ) / ( farDistance - nearDistance ) );
			return BaseClass::OnTakeDamage( scaled );
		}
	}
	if ( !( type & DMG_BURN ) || !( type & DMG_DIRECT ) ) return BaseClass::OnTakeDamage( info );
	CTakeDamageInfo scaled( info );
	float damage = m_flBurnDamageMultiplier * scaled.GetDamage();
	if ( damage < 1.0f ) damage = 1.0f;
	scaled.SetDamage( damage );
	Ignite( 60.0f, false, 0.0f, false );
	return BaseClass::OnTakeDamage( scaled );
}
#pragma float_control( pop )
void CFuelBarrelProp::Event_Killed( const CTakeDamageInfo &info )
{
	BaseClass::Event_Killed( info );
	CBaseEntity *attacker = info.GetAttacker();
	CBaseCombatCharacter *character = attacker ? attacker->MyCombatCharacterPointer() : NULL;
	if ( !character )
	{
		CBaseEntity *inflictor = info.GetInflictor();
		character = inflictor ? inflictor->MyCombatCharacterPointer() : NULL;
	}
	if ( character ) m_hAttacker = character; // native stores AFTER base OnBreak/Detonate
}
static CFuelBarrelPiece *CreateFuelBarrelPiece( const Vector &origin, const QAngle &angles, string_t model, string_t effect )
{
	CFuelBarrelPiece *piece = static_cast< CFuelBarrelPiece * >( CBaseEntity::CreateNoSpawn( "prop_fuel_barrel_piece", origin, angles, NULL ) );
	if ( !piece ) return NULL;
	piece->SetModelName( model );
	DispatchSpawn( piece );
	if ( effect != NULL_STRING )
	{
		CBaseEntity *particle = CreateEntityByName( "info_particle_system" );
		if ( particle )
		{
			particle->KeyValue( "start_active", "1" );
			particle->KeyValue( "effect_name", STRING( effect ) );
			particle->SetParent( piece, -1 );
			particle->SetLocalOrigin( vec3_origin );
			particle->SetLocalAngles( vec3_angle );
			DispatchSpawn( particle );
			particle->Activate();
			piece->SetFlyingParticle( particle );
		}
	}
	return piece;
}
#pragma float_control( precise, on, push )
void CFuelBarrelProp::Detonate()
{
	const QAngle angles = GetAbsAngles();
	const Vector origin = WorldSpaceCenter();
	trace_t trace;
	UTIL_TraceLine( origin, origin - Vector( 0, 0, 128 ), 0x0600400B, this, COLLISION_GROUP_NONE, &trace );
	if ( fuel_barrel_debug.GetBool() )
	{
		CBaseEntity *attacker = m_hAttacker.Get();
		Msg( "prop_fuel_barrel exploding with attacker %s %s \n", attacker ? attacker->GetClassname() : "UNKNOWN", attacker ? STRING( attacker->GetEntityName() ) : "" );
		NDebugOverlay::Cross3D( origin, 10.0f, 0, 0, 255, true, 5.0f );
	}
	CBasePlayer *breaker = ToBasePlayer( m_hBreaker.Get() );
	if ( breaker )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "hegrenade_detonate" );
		if ( event )
		{
			event->SetInt( "userid", breaker->GetUserID() );
			event->SetFloat( "x", GetAbsOrigin().x );
			event->SetFloat( "y", GetAbsOrigin().y );
			event->SetFloat( "z", GetAbsOrigin().z );
			gameeventmanager->FireEvent( event );
		}
	}
	UTIL_ScreenShake( origin, fuel_barrel_screen_shake_amplitude.GetFloat(), fuel_barrel_screen_shake_frequency.GetFloat(), fuel_barrel_screen_shake_duration.GetFloat(), fuel_barrel_screen_shake_radius.GetFloat(), SHAKE_START, false );
	if ( m_DetonateParticles != NULL_STRING ) DispatchParticleEffect( STRING( m_DetonateParticles ), origin, vec3_angle );
	if ( m_DetonateSound != NULL_STRING ) EmitSound( STRING( m_DetonateSound ) );
	CTakeDamageInfo damage( this, m_hAttacker.Get(), vec3_origin, origin, fuel_barrel_damage_amount.GetFloat(), DMG_BLAST, 3 );
	RadiusDamage( damage, origin, fuel_barrel_damage_radius.GetFloat(), CLASS_NONE, this );
	if ( m_BasePiece != NULL_STRING )
	{
		CBaseEntity *piece = CBaseEntity::CreateNoSpawn( "prop_physics", origin, angles, this );
		if ( piece )
		{
			piece->SetModelName( m_BasePiece );
			DispatchSpawn( piece );
			piece->SetAbsVelocity( GetAbsVelocity() );
			Vector up;
			AngleVectors( angles, NULL, NULL, &up );
			up = -up;
			const float speed = RandomFloat( 700.0f, 1000.0f );
			Vector impulse( up.x * speed, up.y * speed, ( up.z * speed ) * 1.5f );
			const float x = FuelBarrelRandomSpin(), y = FuelBarrelRandomSpin(), z = FuelBarrelRandomSpin();
			piece->ApplyLocalAngularVelocityImpulse( AngularImpulse( x, y, z ) );
			piece->ApplyAbsVelocityImpulse( impulse );
			if ( fuel_barrel_debug.GetBool() ) NDebugOverlay::Line( GetAbsOrigin(), GetAbsOrigin() + impulse, 0, 0, 255, true, 5.0f );
		}
	}
	for ( int i = 0; i < ARRAYSIZE( m_FlyingPiece ); ++i )
	{
		if ( m_FlyingPiece[i] == NULL_STRING ) continue;
		CFuelBarrelPiece *piece = CreateFuelBarrelPiece( origin, angles, m_FlyingPiece[i], m_FlyingParticles );
		if ( piece )
		{
			piece->SetAbsVelocity( GetAbsVelocity() );
			const float x = FuelBarrelRandomSpin(), y = FuelBarrelRandomSpin(), z = FuelBarrelRandomSpin();
			piece->ApplyLocalAngularVelocityImpulse( AngularImpulse( x, y, z ) );
			const float vx = RandomFloat( -200.0f, 200.0f );
			const float vy = RandomFloat( -200.0f, 200.0f );
			const float vz = RandomFloat( 700.0f, 1000.0f );
			piece->ApplyAbsVelocityImpulse( Vector( vx, vy, vz ) );
		}
	}
	CInferno *inferno = static_cast< CInferno * >( CBaseEntity::Create( "inferno", origin, angles, m_hAttacker.Get() ) );
	if ( inferno )
	{
		inferno->SetMaxFlames( fuel_barrel_flame_instances.GetInt() );
		inferno->SetDamageCustom( 3 );
		inferno->StartBurning( origin, trace.fraction < 1.0f || trace.startsolid || trace.allsolid ? trace.plane.normal : Vector( 0, 0, 1 ), vec3_origin, 0 );
	}
}
#pragma float_control( pop )
