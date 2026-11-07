//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_mounted_weapon.h"
#include "l4d2/player/terrorplayer.h"
#include "basecombatweapon.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar sb_minigun_distance( "sb_minigun_distance", "30.0", 0x00006004 );

IMPLEMENT_SERVERCLASS_ST( CBaseMountedWeapon, DT_BaseMountedWeapon )
	SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),
	SendPropQAngles( SENDINFO( m_initialAngles ), 10 ),
	SendPropEHandle( SENDINFO( m_owner ) ),
	SendPropFloat( SENDINFO( m_maxYaw ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_maxPitch ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_minPitch ), 0, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_firing ) ),
	SendPropBool( SENDINFO( m_overheated ) ),
	SendPropFloat( SENDINFO( m_heat ), 16, 0, 0.0f, 1.0f ),
END_SEND_TABLE()

CBaseMountedWeapon::CBaseMountedWeapon()
{
	m_owner = NULL;
	m_initialAngles.Init();
	m_parentAngles.Init();
	m_maxYaw = m_maxPitch = m_minPitch = 0.0f;
	m_firing = m_overheated = false;
	m_heat = 0.0f;
	m_lastFireTime = m_lastAngleThink = m_spinSpeed = 0.0f;
	m_horizontalPose = m_verticalPose = 0;
	m_horizontalAngle = m_verticalAngle = 0.0f;
}

// S:0x3DC880 excludes this one original visibility/LOS mask, not every
// player trace or every collision group.
bool CBaseMountedWeapon::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	if ( contentsMask == 0x02006041 )
		return false;
	return BaseClass::ShouldCollide( collisionGroup, contentsMask );
}

int CBaseMountedWeapon::ObjectCaps()
{
	return BaseClass::ObjectCaps() | FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS;
}

int CBaseMountedWeapon::UpdateTransmitState()
{
	return SetTransmitState( m_owner.Get() || m_spinSpeed > 0.0f ? FL_EDICT_ALWAYS : FL_EDICT_PVSCHECK );
}

// S:0x3DA890 / 0x3D6DA0. Distance is deliberately not part of this gate;
// ordinary PlayerUse owns the reach test.
bool CBaseMountedWeapon::OnControls( CBaseEntity *pControls )
{
	if ( pControls == m_owner.Get() )
		return true;
	if ( m_owner.Get() )
		return false;
	Vector toGun = GetAbsOrigin() - pControls->GetAbsOrigin();
	toGun.z = 0.0f;
	VectorNormalize( toGun );
	QAngle angles;
	VectorAngles( toGun, angles );
	return fabsf( AngleNormalize( angles.y - m_initialAngles.Get().y ) ) <= 45.0f;
}

CStudioHdr *CBaseMountedWeapon::OnNewModel()
{
	CStudioHdr *pStudioHdr = BaseClass::OnNewModel();
	LookupMountedPoseParameters();
	return pStudioHdr;
}

void CBaseMountedWeapon::LookupMountedPoseParameters()
{
	m_verticalPose = LookupPoseParameter( "minigun_vertical" );
	m_horizontalPose = LookupPoseParameter( "minigun_horizontal" );
	m_horizontalAngle = m_verticalAngle = 0.0f;
}

// The two original children perform this common initialization after their
// own prop spawn/model work. The map keys start at zero, including MaxYaw.
void CBaseMountedWeapon::InitializeMountedWeapon()
{
	m_owner = NULL;
	m_flFadeScale = 0.0f;
	m_spinSpeed = 0.0f;
	SetSolid( SOLID_BBOX );
	UTIL_SetSize( this, Vector( -10, -10, 0 ), Vector( 10, 10, 50 ) );
	m_initialAngles = GetAbsAngles();
	m_parentAngles = m_initialAngles;
	if ( GetMoveParent() )
		m_parentAngles = GetMoveParent()->GetAbsAngles();
	m_lastAngleThink = gpGlobals->curtime;
	if ( m_minPitch == 0.0f )
		m_minPitch = -m_maxPitch;
	float maxYaw = m_maxYaw;
	if ( !( maxYaw <= 90.0f ) ) maxYaw = 90.0f;
	else if ( maxYaw < 0.0f ) maxYaw = 0.0f;
	m_maxYaw = maxYaw;
	float maxPitch = m_maxPitch;
	if ( !( maxPitch <= 60.0f ) ) maxPitch = 60.0f;
	else if ( maxPitch < 0.0f ) maxPitch = 0.0f;
	m_maxPitch = maxPitch;
	float minPitch = m_minPitch;
	if ( !( minPitch <= 0.0f ) ) minPitch = 0.0f;
	else if ( minPitch < -30.0f ) minPitch = -30.0f;
	m_minPitch = minPitch;
	m_firing = false;
	m_overheated = false;
	m_heat = 0.0f;
}

// S:0x3DB070 / 0x3D8710 keep a moving parent's delta in the networked
// initial angles. Only pitch/yaw are normalized by the original.
void CBaseMountedWeapon::UpdateParentAngles()
{
	CBaseEntity *pParent = GetMoveParent();
	if ( !pParent )
		return;
	const QAngle &angles = pParent->GetAbsAngles();
	QAngle delta = angles - m_parentAngles;
	const float magnitude = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
	if ( !( magnitude >= 0.01f ) )
		return;
	QAngle initial = m_initialAngles.Get() + delta;
	initial.y = AngleNormalize( initial.y );
	initial.x = AngleNormalize( initial.x );
	m_initialAngles = initial;
	m_parentAngles = angles;
}

void CBaseMountedWeapon::GetControlAngles( QAngle *pInitial, float *pMinYaw, float *pMaxYaw, float *pMinPitch, float *pMaxPitch ) const
{
	*pInitial = m_initialAngles;
	*pMinYaw = -m_maxYaw;
	*pMaxYaw = m_maxYaw;
	*pMinPitch = m_minPitch;
	*pMaxPitch = m_maxPitch;
}

// S:0x3DC950; character scaling belongs to the shared survivor-set owner.
void CBaseMountedWeapon::GetUserPosition( Vector *pPosition )
{
	if ( !pPosition )
		return;
	Vector forward;
	AngleVectors( QAngle( 0, GetAbsAngles().y, 0 ), &forward );
	float distance = sb_minigun_distance.GetFloat();
	if ( m_owner.Get() )
		distance *= GetSurvivorCharacterScale( m_owner->GetSurvivorCharacter() );
	*pPosition = GetAbsOrigin() - forward * distance;
}

// S:0x3DCAB0 checks the VERTICAL index even when approaching the horizontal
// parameter. Do not repair that original model-edge behavior.
float CBaseMountedWeapon::ApproachPoseParameter( int index, float current, float target, float speed, float interval )
{
	if ( m_verticalPose < 0 )
		return target;
	float minimum, maximum;
	GetPoseParameterRange( index, minimum, maximum );
	if ( target > current )
	{
		current += speed * interval;
		if ( target <= current )
			current = target;
	}
	else if ( current > target )
	{
		current -= speed * interval;
		if ( current <= target )
			current = target;
	}
	SetPoseParameter( index, current );
	return current;
}

// S:0x3DCB70. Server pose speed uses the fire-think interval, not elapsed
// AngleThink time; these clocks intentionally remain separate.
void CBaseMountedWeapon::UpdateMountedPose( float interval )
{
	float speed = 50.0f;
	QAngle delta( 0, 0, 0 );
	if ( m_owner.Get() )
	{
		speed = 1000.0f;
		delta = m_owner->EyeAngles() - EyeAngles();
	}
	m_verticalAngle = ApproachPoseParameter( m_verticalPose, m_verticalAngle, AngleNormalize( delta.x ), speed, interval );
	m_horizontalAngle = ApproachPoseParameter( m_horizontalPose, m_horizontalAngle, AngleNormalize( delta.y ), speed, interval );
}

void CBaseMountedWeapon::RestoreUserWeapon( CTerrorPlayer *pPlayer )
{
	CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	if ( pWeapon )
	{
		pPlayer->m_Local.m_iHideHUD &= ~HIDEHUD_WEAPONSELECTION;
		pWeapon->Deploy();
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
