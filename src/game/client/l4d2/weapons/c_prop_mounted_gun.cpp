//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_prop_mounted_gun.h"
#include "model_types.h"
#include "l4d2/player/c_terrorplayer.h"
#include "cdll_client_int.h"
#include "cdll_util.h"
#include "functionproxy.h"
#include "proxyentity.h"
#include "l4d2/content/terror_precache_client.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_PropMountedGun, DT_PropMountedGun, CPropMountedGun )
END_RECV_TABLE()

static ConVar mounted_gun_overheat_time( "mounted_gun_overheat_time", "15", 0x00006008 );
static ConVar mounted_gun_cooldown_time( "mounted_gun_cooldown_time", "60", 0x00006008 );

C_PropMountedGun::C_PropMountedGun()
{
	// C:0x303700. Native interpolation remains in C_BaseAnimating; poses are
	// client-owned because the server excludes DT_BaseAnimating pose data.
	UseClientSideAnimation();
}

C_PropMountedGun::~C_PropMountedGun()
{
	// C:0x303800 releases particle references, not a sound patch. The .50 cal
	// does not have the L4D1 minigun's client spin/fire audio state machine.
}

void C_PropMountedGun::Precache()
{
	// C:0x303220. ParticleTracer's dispatch callback is statically registered
	// by the target; its named particle definition still needs precaching.
	PrecacheParticleSystem( "weapon_muzzle_flash_minigun" );
	PrecacheParticleSystem( "minigun_overheat_smoke" );
	PrecacheEffect( "ParticleTracer" );
	PrecacheParticleSystem( "weapon_tracers" );
	BaseClass::Precache();
}

void C_PropMountedGun::OnDataChanged( DataUpdateType_t updateType )
{
	// C:0x303380 schedules continuous presentation, not a firing scheduler.
	BaseClass::OnDataChanged( updateType );
	SetNextClientThink( CLIENT_THINK_ALWAYS );
}

CStudioHdr *C_PropMountedGun::OnNewModel()
{
	return CacheMountedModel();
}

void C_PropMountedGun::ClientThink()
{
	// C:0x303A80. Firing flashes, casings and audio are server events for
	// this class; synthesizing the L4D1 minigun path would duplicate them.
	UpdateOverheatEffect();
}

void C_PropMountedGun::UpdateClientSideAnimation()
{
	// C:0x303180 -> C:0x303C80.
	BaseClass::UpdateClientSideAnimation();
	UpdateMountedPose();
}

unsigned int C_PropMountedGun::ComputeClientSideAnimationFlags()
{
	// C:0x303170 bypasses CDynamicProp's zero-cycle-rate optimization: an
	// occupied prop still needs eye-driven pose updates when its cycle rests.
	return C_BaseAnimating::ComputeClientSideAnimationFlags();
}

void C_PropMountedGun::StandardBlendingRules( CStudioHdr *pHdr, Vector pos[], Quaternion q[], float currentTime, int boneMask )
{
	// C:0x303190. This class deliberately does not advance the barrel.
	BaseClass::StandardBlendingRules( pHdr, pos, q, currentTime, boneMask );
	if ( m_iBarrelBone != -1 )
		AngleQuaternion( RadianEuler( 0.0f, 0.0f, 0.0f ), q[m_iBarrelBone] );
}

int C_PropMountedGun::DrawModel( int flags )
{
	// C:0x303300: suppress the viewed occupant's gun depth shadow, including IN_EYE.
	C_TerrorPlayer *pPlayer = GetUser();
	if ( flags == ( STUDIO_RENDER | STUDIO_SHADOWDEPTHTEXTURE ) && pPlayer && pPlayer == PostProcessViewPlayer() )
		return 0;
	return BaseClass::DrawModel( flags );
}

int C_PropMountedGun::ObjectCaps()
{
	// C:0x3033C0.
	return BaseClass::ObjectCaps() | ( m_owner.Get() ? 0 : FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS );
}

bool C_PropMountedGun::IsUsable( C_BaseEntity *pPlayer )
{
	// C:0x3032A0 permits either survivor team, but not an occupied prop.
	return !m_owner.Get() && pPlayer &&
		( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR );
}

const wchar_t *C_PropMountedGun::GetUseString( C_BaseEntity *pUser )
{
	return UTIL_ConstructStringWithKeyBinding( "+use", "L4D_Use_Minigun" );
}

bool C_PropMountedGun::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	// C:0x303B80, exact original LOS mask 0x02006041.
	if ( contentsMask == ( MASK_BLOCKLOS_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE ) )
		return false;
	return BaseClass::ShouldCollide( collisionGroup, contentsMask );
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
float C_PropMountedGun::GetHeatBlend() const
{
	// C:0x303410. Only the last three seconds of heating/cooling glow.
	if ( m_overheated )
	{
		const float threshold = 3.0f / mounted_gun_cooldown_time.GetFloat();
		if ( threshold == 0.0f )
			return m_heat < 0.0f ? 1.0f : 0.0f;
		float fraction = ( m_heat - threshold ) / -threshold;
		if ( fraction >= 0.0f )
		{
			if ( fraction > 1.0f )
				fraction = 1.0f;
			return 1.0f - fraction;
		}
		return 1.0f;
	}
	const float threshold = 1.0f - 3.0f / mounted_gun_overheat_time.GetFloat();
	if ( threshold == 1.0f )
		return m_heat >= 1.0f ? 1.0f : 0.0f;
	const float fraction = ( m_heat - threshold ) / ( 1.0f - threshold );
	if ( fraction >= 0.0f )
		return fraction > 1.0f ? 1.0f : fraction;
	return 0.0f;
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// C:0x303530; null binds leave the previous material value untouched.
class CProxyMountedGunHeat : public CResultProxy
{
public:
	virtual void OnBind( void *pRenderable ) OVERRIDE
	{
		if ( !pRenderable )
			return;
		C_PropMountedGun *pGun = dynamic_cast< C_PropMountedGun * >( BindArgToEntity( pRenderable ) );
		SetFloatResult( pGun ? pGun->GetHeatBlend() : 0.0f );
	}
};

EXPOSE_INTERFACE( CProxyMountedGunHeat, IMaterialProxy, "MountedGunHeat" IMATERIAL_PROXY_INTERFACE_VERSION );
