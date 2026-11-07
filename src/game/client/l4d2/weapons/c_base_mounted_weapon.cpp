//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_base_mounted_weapon.h"
#include "l4d2/player/c_terrorplayer.h"
#include "particle_parse.h"
#include "particle_property.h"
#include "particles_new.h"
#include "datacache/imdlcache.h"
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( BaseMountedWeapon, DT_BaseMountedWeapon )

BEGIN_NETWORK_TABLE( CBaseMountedWeapon, DT_BaseMountedWeapon )
	RecvPropQAngles( RECVINFO( m_initialAngles ) ),
	RecvPropEHandle( RECVINFO( m_owner ) ),
	RecvPropFloat( RECVINFO( m_maxYaw ) ),
	RecvPropFloat( RECVINFO( m_maxPitch ) ),
	RecvPropFloat( RECVINFO( m_minPitch ) ),
	RecvPropBool( RECVINFO( m_firing ) ),
	RecvPropBool( RECVINFO( m_overheated ) ),
	RecvPropFloat( RECVINFO( m_heat ) ),
END_NETWORK_TABLE()

static ConVar sb_minigun_distance( "sb_minigun_distance", "30.0", 0x00006008 );

C_BaseMountedWeapon::C_BaseMountedWeapon()
{
	// C:0x304080; child constructors C:0x303700/0x3017F0.
	m_initialAngles.Init();
	m_owner = NULL;
	m_maxYaw = m_maxPitch = m_minPitch = 0.0f;
	m_firing = m_overheated = false;
	m_heat = 0.0f;
	m_iHorizontalPose = m_iVerticalPose = m_iBarrelBone = -1;
	m_iLinkAttachment = m_iShellAttachment = m_iMuzzleAttachment = -1;
	m_flHorizontalPose = m_flVerticalPose = 0.0f;
}

C_BaseMountedWeapon::~C_BaseMountedWeapon()
{
	// CParticleProperty owns the native effect references and receives deletion
	// notifications. Its teardown releases them with the dynamic prop.
}

C_TerrorPlayer *C_BaseMountedWeapon::GetUser() const
{
	return ToTerrorPlayer( m_owner.Get() );
}

void C_BaseMountedWeapon::GetControlAngles( QAngle *pInitial, float *pMinYaw, float *pMaxYaw, float *pMinPitch, float *pMaxPitch ) const
{
	if ( pInitial )
		*pInitial = m_initialAngles;
	if ( pMinYaw )
		*pMinYaw = -m_maxYaw;
	if ( pMaxYaw )
		*pMaxYaw = m_maxYaw;
	if ( pMinPitch )
		*pMinPitch = m_minPitch;
	if ( pMaxPitch )
		*pMaxPitch = m_maxPitch;
}

// C:0x3040F0/0x303DE0. Character scaling shares the original rules-owned
// survivor-set remapping (C:0x2867C0); it is not a second local enum mapping.
void C_BaseMountedWeapon::GetUserPosition( Vector *pPosition )
{
	if ( !pPosition )
		return;

	float distance = sb_minigun_distance.GetFloat();
	C_TerrorPlayer *pPlayer = GetUser();
	if ( pPlayer )
		distance *= GetSurvivorCharacterScale( pPlayer->GetSurvivorCharacter() );

	Vector forward;
	AngleVectors( QAngle( 0.0f, GetAbsAngles().y, 0.0f ), &forward );
	*pPosition = GetAbsOrigin() - distance * forward;
}

// The local player's CreateMove owns applying these to both engine and cmd
// angles (C:0x2550F0). Never change another player's received eye angles here.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_BaseMountedWeapon::ClampViewAngles( QAngle &angles ) const
{
	float yaw = AngleNormalize( angles.y - m_initialAngles.y );
	float pitch = AngleNormalize( angles.x - m_initialAngles.x );
	if ( yaw > m_maxYaw )
		yaw = m_maxYaw;
	else if ( -m_maxYaw > yaw )
		yaw = -m_maxYaw;
	if ( pitch > m_maxPitch )
		pitch = m_maxPitch;
	else if ( m_minPitch > pitch )
		pitch = m_minPitch;
	angles.x = m_initialAngles.x + pitch;
	angles.y = yaw + m_initialAngles.y;
	angles.z = m_initialAngles.z;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// C:0x3035F0/0x3016E0. Attachment and pose lookups belong to model changes.
CStudioHdr *C_BaseMountedWeapon::CacheMountedModel()
{
	CStudioHdr *pHdr = BaseClass::OnNewModel();
	m_iVerticalPose = LookupPoseParameter( "minigun_vertical" );
	m_iHorizontalPose = LookupPoseParameter( "minigun_horizontal" );
	m_flHorizontalPose = m_flVerticalPose = 0.0f;
	m_iBarrelBone = LookupBone( "ValveBiped.barrels" );
	m_iLinkAttachment = LookupAttachment( "link" );
	m_iShellAttachment = LookupAttachment( "shell" );
	m_iMuzzleAttachment = LookupAttachment( "muzzle_flash" );
	return pHdr;
}

// C:0x303BB0/0x3023F0. The original tests the vertical pose's validity for
// both axes, and stores the approached angle, not the model-clamped result.
#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
float C_BaseMountedWeapon::ApproachMountedPose( int iPose, float current, float target, float speed )
{
	if ( m_iVerticalPose < 0 )
		return target;

	const float step = speed * gpGlobals->frametime;
	if ( target > current )
	{
		current += step;
		if ( target <= current )
			current = target;
	}
	else if ( current > target )
	{
		current -= step;
		if ( current <= target )
			current = target;
	}
	SetPoseParameter( iPose, current );
	return current;
}

void C_BaseMountedWeapon::UpdateMountedPose()
{
	// C:0x303C80/0x3024C0. Remote owners use their native interpolated eye
	// angles; unoccupied guns return to zero at 50 degrees/second.
	C_TerrorPlayer *pPlayer = GetUser();
	float pitch = 0.0f;
	float yaw = 0.0f;
	float speed = 50.0f;
	if ( pPlayer )
	{
		const QAngle &mountAngles = EyeAngles();
		const QAngle &playerAngles = pPlayer->EyeAngles();
		pitch = playerAngles.x - mountAngles.x;
		yaw = playerAngles.y - mountAngles.y;
		speed = 1000.0f;
	}

	m_flVerticalPose = ApproachMountedPose( m_iVerticalPose, m_flVerticalPose, AngleNormalize( pitch ), speed );
	m_flHorizontalPose = ApproachMountedPose( m_iHorizontalPose, m_flHorizontalPose, AngleNormalize( yaw ), speed );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

CNewParticleEffect *C_BaseMountedWeapon::FindMountedEffect( const char *pName )
{
	// Retail has auto-null particle references (C:0x3038D0/0x301AE0 and
	// C:0x3039A0/0x301BB0). The target's native property is its deletion-aware
	// owner; do not keep raw pointers or unnotified CSmartPtrs across frames.
	for ( int i = ParticleProp()->FindEffect( pName ); i >= 0; i = ParticleProp()->FindEffect( pName, i + 1 ) )
	{
		CNewParticleEffect *pEffect = ParticleProp()->GetParticleEffectFromIdx( i );
		if ( !pEffect->m_bEmissionStopped && !pEffect->GetRemoveFlag() )
			return pEffect;
	}
	return NULL;
}

void C_BaseMountedWeapon::UpdateOverheatEffect()
{
	CNewParticleEffect *pSmoke = FindMountedEffect( "minigun_overheat_smoke" );
	if ( m_overheated )
	{
		if ( !pSmoke )
		{
			// C303A80/C301C00 capture the native model-cache lock around
			// this smoke creation only, not the whole client think.
			CMDLCacheCriticalSection mdlCacheCriticalSection( mdlcache );
			ParticleProp()->Create( "minigun_overheat_smoke", PATTACH_POINT_FOLLOW, m_iMuzzleAttachment );
		}
	}
	else if ( pSmoke )
	{
		ParticleProp()->StopEmission( pSmoke );
	}
}
