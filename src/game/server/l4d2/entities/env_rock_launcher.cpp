//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/env_rock_launcher.h"
#include "l4d2/infected/boss/tank_rock.h"
#include "entitylist.h"
#include "movevars_shared.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

extern ConVar z_tank_throw_force;
LINK_ENTITY_TO_CLASS( env_rock_launcher, CEnvRockLauncher ); // S:5A0230
BEGIN_DATADESC( CEnvRockLauncher )
	DEFINE_KEYFIELD( m_targetName, FIELD_STRING, "RockTargetName" ),
	DEFINE_KEYFIELD( m_rockDamageOverride, FIELD_INTEGER, "RockDamageOverride" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "LaunchRock", InputLaunchRock ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetTarget", InputSetTarget ),
END_DATADESC()

CEnvRockLauncher::CEnvRockLauncher() // S:3676A0, inlined factory construction.
{
	m_targetName = NULL_STRING;
	m_rockDamageOverride = 0;
}

void CEnvRockLauncher::Spawn() // S:3675C0
{
	BaseClass::Spawn(); // Native CPointEntity1E77B0 sets SOLID_NONE.
	SetSolid( SOLID_NONE );
	AddEffects( EF_NODRAW );
}

void CEnvRockLauncher::InputSetTarget( inputdata_t &input ) // S:367350
{
	// StringID returns NULL_STRING for every non-string variant, as native.
	m_targetName = input.value.StringID();
}

void CEnvRockLauncher::InputLaunchRock( inputdata_t &input ) // S:36A1F0
{
	// Native B42F0 is the pooled-string exact-name search, not the SDK's
	// char* wildcard/context-token search. Keep first active-list match.
	if ( m_targetName != NULL_STRING && STRING( m_targetName )[0] != '\0' )
	{
		for ( CBaseEntity *target = gEntList.FirstEnt(); target; target = gEntList.NextEnt( target ) )
		{
			if ( target->GetEntityName() == m_targetName )
			{
				LaunchAtTarget( target );
				return;
			}
		}
	}
	LaunchForward();
}

void CEnvRockLauncher::LaunchForward() // S:36A0D0
{
	const Vector origin = GetAbsOrigin();
	const QAngle angles = GetAbsAngles();
	CTankRock *rock = CTankRock::Create( origin, angles, vec3_origin, NULL );
	rock->m_rockDamageOverride = m_rockDamageOverride;
	Vector direction;
	AngleVectors( angles, &direction );
	const float force = z_tank_throw_force.GetFloat();
	const Vector velocity( direction.x * force, direction.y * force, direction.z * force );
	const AngularImpulse angularVelocity( 60.0f, static_cast< float >( random->RandomInt( -120, 120 ) ), 0.0f );
	rock->Launch( origin, angles, velocity, angularVelocity );
}

void CEnvRockLauncher::LaunchAtTarget( CBaseEntity *target ) // S:369E90
{
	const Vector origin = GetAbsOrigin();
	const QAngle angles = GetAbsAngles();
	CTankRock *rock = CTankRock::Create( origin, angles, vec3_origin, NULL );
	rock->m_rockDamageOverride = m_rockDamageOverride;
	const Vector targetOrigin = target->GetAbsOrigin();
	const float dy = targetOrigin.y - origin.y;
	const float dz = targetOrigin.z - origin.z;
	const float dx = targetOrigin.x - origin.x;
	const float force = z_tank_throw_force.GetFloat();
	const float dySquared = dy * dy;
	const float dzSquared = dz * dz;
	const float dxSquared = dx * dx;
	const float distance = sqrtf( ( dySquared + dzSquared ) + dxSquared );
	const float horizontalDistance = sqrtf( ( dySquared + 0.0f * 0.0f ) + dxSquared );
	const float inverseHorizontal = 1.0f / horizontalDistance;
	const float directionX = inverseHorizontal * dx;
	const float directionY = inverseHorizontal * dy;
	const float directionZ = inverseHorizontal * 0.0f;
	float verticalSpeed = rock->GetGravity() * sv_gravity.GetFloat();
	verticalSpeed *= 0.5f;
	const float flightTime = distance / force;
	const float timeSquared = flightTime * flightTime;
	verticalSpeed *= timeSquared;
	verticalSpeed += dz;
	verticalSpeed /= flightTime;
	// S36A045 subtracts verticalSpeed from force, NOT sqrt(force^2-vz^2).
	// S36A04C/063/067 retain verticalSpeed*0 additions, even for Inf/NaN.
	const float remainingForce = force - verticalSpeed;
	const float horizontalAddition = verticalSpeed * 0.0f;
	const Vector velocity( directionX * remainingForce + horizontalAddition,
		directionY * remainingForce + horizontalAddition, directionZ * remainingForce + verticalSpeed );
	const AngularImpulse angularVelocity( 60.0f, static_cast< float >( random->RandomInt( -120, 120 ) ), 0.0f );
	rock->Launch( origin, angles, velocity, angularVelocity );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
