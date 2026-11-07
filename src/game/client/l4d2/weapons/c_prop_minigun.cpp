//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_prop_minigun.h"
#include "model_types.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_recipientfilter.h"
#include "cdll_client_int.h"
#include "cdll_util.h"
#include "soundenvelope.h"
#include "particle_parse.h"
#include "particle_property.h"
#include "particles_new.h"
#include "functionproxy.h"
#include "proxyentity.h"
#include "vphysics_interface.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/content/terror_precache_client.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

IMPLEMENT_CLIENTCLASS_DT( C_PropMinigun, DT_PropMinigun, CPropMinigun )
	// C:0x302120 repeats these inherited members in the child table.
	RecvPropBool( RECVINFO( m_overheated ) ),
	RecvPropFloat( RECVINFO( m_heat ) ),
END_RECV_TABLE()

static ConVar z_minigun_particles( "z_minigun_particles", "1", 0x0000000A, "1=attachment, 2=control points (like smoker_tongue drip)" );
static ConVar z_minigun_spin_up_speed( "z_minigun_spin_up_speed", "15", 0x0000200A );
static ConVar z_minigun_spin_down_speed( "z_minigun_spin_down_speed", "4", 0x0000200A );
static ConVar z_minigun_firing_speed( "z_minigun_firing_speed", "15", 0x0000200A );
static ConVar z_minigun_stand_distance( "z_minigun_stand_distance", "36", 0x0000200A );
static ConVar z_minigun_spread( "z_minigun_spread", "7", 0x0000200A, "Minigun spray in degrees from forward" );

C_PropMinigun::C_PropMinigun()
{
	// C:0x3017F0.
	m_flFireTimestamp = -1.0f;
	m_flNextTracerTime = m_flNextRicochetTime = m_flNextImpactTime = 0.0f;
	m_flBarrelSpeed = m_flBarrelTargetSpeed = m_flBarrelRotation = 0.0f;
	m_pSound = NULL;
	m_soundState = SOUND_IDLE;
	UseClientSideAnimation();
}

C_PropMinigun::~C_PropMinigun()
{
	// C:0x3018F0. Faded patches are controller-owned; the current patch is
	// destroyed here. CParticleProperty handles effect deletion notifications.
	if ( m_pSound )
		CSoundEnvelopeController::GetController().SoundDestroy( m_pSound );
}

void C_PropMinigun::Precache()
{
	// C:0x300F10. ParticleTracer is registered by the target dispatch code.
	PrecacheParticleSystem( "weapon_muzzle_flash_minigun" );
	PrecacheParticleSystem( "minigun_overheat_smoke" );
	PrecacheEffect( "ParticleTracer" );
	PrecacheParticleSystem( "weapon_tracers" );
	BaseClass::Precache();
}

void C_PropMinigun::OnDataChanged( DataUpdateType_t updateType )
{
	// C:0x301300.
	BaseClass::OnDataChanged( updateType );
	SetNextClientThink( CLIENT_THINK_ALWAYS );
}

CStudioHdr *C_PropMinigun::OnNewModel()
{
	return CacheMountedModel();
}

void C_PropMinigun::UpdateClientSideAnimation()
{
	// C:0x300E70 -> C:0x3024C0.
	BaseClass::UpdateClientSideAnimation();
	UpdateMountedPose();
}

unsigned int C_PropMinigun::ComputeClientSideAnimationFlags()
{
	return C_BaseAnimating::ComputeClientSideAnimationFlags();
}

void C_PropMinigun::StandardBlendingRules( CStudioHdr *pHdr, Vector pos[], Quaternion q[], float currentTime, int boneMask )
{
	// C:0x300E80. The accumulator is radians, not degrees. As in retail,
	// barrel acceleration and casing transitions are driven by bone setup.
	BaseClass::StandardBlendingRules( pHdr, pos, q, currentTime, boneMask );
	if ( m_iBarrelBone != -1 )
	{
		UpdateBarrel( gpGlobals->frametime );
		AngleQuaternion( RadianEuler( 0.0f, 0.0f, m_flBarrelRotation ), q[m_iBarrelBone] );
	}
}

void C_PropMinigun::UpdateBarrel( float deltaTime )
{
	// C:0x3021F0. Casing start/stop uses exact speed transitions; do not
	// restart it every think or collapse the spin-up into the firing flag.
	const float previousSpeed = m_flBarrelSpeed;
	const float firingSpeed = z_minigun_firing_speed.GetFloat();
	m_flBarrelTargetSpeed = m_firing ? firingSpeed : 0.0f;
	const float acceleration = m_firing ? z_minigun_spin_up_speed.GetFloat() : z_minigun_spin_down_speed.GetFloat();
	if ( m_flBarrelSpeed != m_flBarrelTargetSpeed )
		m_flBarrelSpeed = Approach( m_flBarrelTargetSpeed, m_flBarrelSpeed, acceleration * deltaTime );

	if ( m_flBarrelSpeed != previousSpeed )
	{
		if ( m_flBarrelSpeed == m_flBarrelTargetSpeed && m_flBarrelTargetSpeed > 0.0f )
			StartShells();
		else if ( previousSpeed == firingSpeed && m_flBarrelTargetSpeed == 0.0f )
			StopShells();
	}
	m_flBarrelRotation += deltaTime * m_flBarrelSpeed;
}

void C_PropMinigun::StartShells()
{
	// C:0x3019E0.
	if ( m_iShellAttachment != -1 && !FindMountedEffect( "weapon_shell_casing_minigun" ) )
		ParticleProp()->Create( "weapon_shell_casing_minigun", PATTACH_POINT_FOLLOW, m_iShellAttachment );
}

void C_PropMinigun::StopShells()
{
	// C:0x301A90 allows the emitted casings to finish normally.
	CNewParticleEffect *pShells = FindMountedEffect( "weapon_shell_casing_minigun" );
	if ( pShells )
		ParticleProp()->StopEmission( pShells );
}

void C_PropMinigun::UpdateSound()
{
	// C:0x301080. Overheated-at-rest is a real silent state, distinct from
	// idle, and overheated spin-down uses the same SpinDown sound.
	SoundState state = SOUND_IDLE;
	const char *pSoundName = NULL;
	if ( m_firing )
	{
		if ( m_flBarrelSpeed < z_minigun_firing_speed.GetFloat() )
		{
			state = SOUND_SPIN_UP;
			pSoundName = "Minigun.SpinUp";
		}
		else
		{
			state = SOUND_FIRING;
			pSoundName = "Minigun.Fire";
		}
	}
	else if ( !( m_flBarrelSpeed <= 0.0f ) )
	{
		state = m_overheated ? SOUND_SPIN_DOWN_OVERHEATED : SOUND_SPIN_DOWN;
		pSoundName = "Minigun.SpinDown";
	}
	else if ( m_overheated )
	{
		state = SOUND_OVERHEATED;
	}

	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	if ( state == m_soundState )
	{
		if ( m_pSound && state == SOUND_FIRING )
			controller.SoundChangeVolume( m_pSound, RandomFloat( 0.9f, 1.0f ), 0.1f );
		return;
	}

	if ( m_pSound )
	{
		if ( ( m_soundState == SOUND_SPIN_DOWN || m_soundState == SOUND_SPIN_DOWN_OVERHEATED ) && state == SOUND_IDLE )
			controller.SoundFadeOut( m_pSound, 0.2f, true );
		else
			controller.SoundDestroy( m_pSound );
		m_pSound = NULL;
	}
	m_soundState = state;
	if ( pSoundName )
	{
		CPASAttenuationFilter filter( GetAbsOrigin() );
		m_pSound = controller.SoundCreate( filter, entindex(), pSoundName );
		controller.Play( m_pSound, 1.0f, 100.0f, 0.0f );
		controller.SoundChangeVolume( m_pSound, 1.0f, 0.1f );
	}
}

void C_PropMinigun::UpdateFiringEffects()
{
	// C:0x301C00. Flash state follows the audio state, including spin-up.
	CNewParticleEffect *pFlash = FindMountedEffect( "weapon_muzzle_flash_minigun" );
	C_TerrorPlayer *pPlayer = GetUser();
	if ( m_soundState == SOUND_FIRING && m_owner.Get() )
	{
		if ( z_minigun_particles.GetInt() == 2 )
		{
			Vector muzzle;
			QAngle muzzleAngles;
			if ( GetAttachment( m_iMuzzleAttachment, muzzle, muzzleAngles ) )
			{
				if ( pFlash && pPlayer )
				{
					Vector origin, forward;
					pPlayer->EyePositionAndVectors( &origin, &forward, NULL, NULL );
					CTraceFilterNoNPCsOrPlayer filter( this, COLLISION_GROUP_NONE );
					trace_t trace;
					UTIL_TraceLine( origin, origin + 10000.0f * forward, MASK_SOLID, &filter, &trace );
					pFlash->SetControlPoint( 0, muzzle );
					pFlash->SetControlPoint( 1, trace.endpos );
				}
				else if ( !pFlash )
				{
					DispatchParticleEffect( "weapon_muzzle_flash_minigun", muzzle, muzzleAngles, this );
				}
			}
		}
		else if ( !pFlash )
		{
			ParticleProp()->Create( "weapon_muzzle_flash_minigun", PATTACH_POINT_FOLLOW, m_iMuzzleAttachment );
		}
	}
	else if ( pFlash )
	{
		ParticleProp()->StopEmission( pFlash );
	}
	UpdateOverheatEffect();
}

void C_PropMinigun::ClientThink()
{
	// C:0x301ED0. This .033 presentation loop neither spends ammunition nor
	// deals damage. Preserve the leftover interval and strict first test.
	UpdateSound();
	UpdateFiringEffects();
	if ( m_firing && m_flBarrelSpeed >= z_minigun_firing_speed.GetFloat() )
	{
		if ( m_flFireTimestamp > 0.0f )
		{
			// First test compares the unspilled x87 difference. The catch-up
			// loop then uses the separately spilled F32 elapsed/remainder.
			if ( static_cast< double >( gpGlobals->curtime ) - m_flFireTimestamp <= static_cast< double >( 0.033f ) )
				return;
			float elapsed = static_cast< float >( static_cast< double >( gpGlobals->curtime ) - m_flFireTimestamp );
			while ( elapsed >= 0.033f )
			{
				DoMuzzleFlash();
				FireEffects();
				elapsed -= 0.033f;
			}
			m_flFireTimestamp = gpGlobals->curtime - elapsed;
		}
		else
		{
			m_flFireTimestamp = gpGlobals->curtime;
			DoMuzzleFlash();
			FireEffects();
		}
	}
	else
	{
		m_flFireTimestamp = -1.0f;
		m_flNextTracerTime = m_flNextRicochetTime = m_flNextImpactTime = 0.0f;
	}
}

void C_PropMinigun::FireEffects()
{
	// C:0x3013F0. This runs for received remote owners too, using native
	// interpolated eyes and a cosmetic trace which excludes NPCs/players.
	C_TerrorPlayer *pPlayer = GetUser();
	if ( !pPlayer )
		return;
	pPlayer->OnMountedWeaponFired();

	Vector origin, forward, muzzle;
	QAngle muzzleAngles;
	pPlayer->EyePositionAndVectors( &origin, &forward, NULL, NULL );
	const bool hasMuzzle = GetAttachment( m_iMuzzleAttachment, muzzle, muzzleAngles );

	QAngle angles = pPlayer->EyeAngles();
	const float theta = RandomFloat( 0.0f, 2.0f * M_PI );
	const float radius = RandomFloat( 0.0f, z_minigun_spread.GetFloat() * 0.5f );
	angles.y = static_cast< float >( cos( static_cast< double >( theta ) ) * radius + angles.y );
	angles.x = static_cast< float >( sin( static_cast< double >( theta ) ) * radius + angles.x );
	AngleVectors( angles, &forward );
	CTraceFilterNoNPCsOrPlayer filter( this, COLLISION_GROUP_NONE );
	trace_t trace;
	UTIL_TraceLine( origin, origin + 3072.0f * forward, MASK_SOLID, &filter, &trace );

	const float now = gpGlobals->curtime;
	if ( !( trace.surface.flags & ( SURF_SKY | SURF_NODRAW | SURF_HINT | SURF_SKIP ) ) &&
		trace.fraction != 1.0f && now > m_flNextImpactTime )
	{
		if ( trace.m_pEnt && !trace.m_pEnt->MyCombatCharacterPointer() )
			UTIL_ImpactTrace( &trace, DMG_BULLET | 0x80000000 );
		m_flNextImpactTime = now + 0.09f;
	}

	if ( now > m_flNextTracerTime )
	{
		// Attachment failure only makes this tracer origin undefined, not the
		// independent timer, RNG, trace, impact or ricochet phases.
		if ( hasMuzzle )
			UTIL_ParticleTracer( "weapon_tracers", muzzle, trace.endpos, 0, -1, true );
		m_flNextTracerTime = now + 0.11f;
	}

	if ( now > m_flNextRicochetTime && trace.fraction != 1.0f )
	{
		// C2F3260 is the SAME actual rifle/minigun ricochet owner.
		TerrorRicochet( trace, 66 );
		m_flNextRicochetTime = now + 0.5f;
	}
}

int C_PropMinigun::DrawModel( int flags )
{
	// C:0x301280.
	C_TerrorPlayer *pPlayer = GetUser();
	if ( flags == ( STUDIO_RENDER | STUDIO_SHADOWDEPTHTEXTURE ) && pPlayer && pPlayer == C_BasePlayer::GetLocalPlayer() )
		return 0;
	return BaseClass::DrawModel( flags );
}

int C_PropMinigun::ObjectCaps()
{
	return BaseClass::ObjectCaps() | ( m_owner.Get() ? 0 : FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS );
}

bool C_PropMinigun::IsUsable( C_BaseEntity *pPlayer )
{
	return !m_owner.Get() && pPlayer &&
		( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR );
}

const wchar_t *C_PropMinigun::GetUseString( C_BaseEntity *pUser )
{
	return UTIL_ConstructStringWithKeyBinding( "+use", "L4D_Use_Minigun" );
}

bool C_PropMinigun::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	// C:0x3021C0.
	if ( contentsMask == ( MASK_BLOCKLOS_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE ) )
		return false;
	return BaseClass::ShouldCollide( collisionGroup, contentsMask );
}

void C_PropMinigun::GetUserPosition( Vector *pPosition )
{
	// C:0x302320. Unlike the base/.50 cal, there is no character scaling.
	if ( pPosition )
	{
		Vector forward;
		AngleVectors( QAngle( 0.0f, GetAbsAngles().y, 0.0f ), &forward );
		*pPosition = GetAbsOrigin() - z_minigun_stand_distance.GetFloat() * forward;
	}
}

// C:0x300F90 and registration C:0x5189B0. The L4D1 proxy uses raw heat.
class CProxyHeat : public CResultProxy
{
public:
	virtual void OnBind( void *pRenderable ) OVERRIDE
	{
		if ( !pRenderable )
			return;
		C_PropMinigun *pGun = dynamic_cast< C_PropMinigun * >( BindArgToEntity( pRenderable ) );
		SetFloatResult( pGun ? pGun->GetHeat() : 0.0f );
	}
};

EXPOSE_INTERFACE( CProxyHeat, IMaterialProxy, "Heat" IMATERIAL_PROXY_INTERFACE_VERSION );

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
