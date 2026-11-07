//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: L4D2 predicted viewmodel identity and precache contract.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/weapons/terror_viewmodel.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_cs_player.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/terror_shareddefs.h"
#include "gamerules.h"
#include "prediction.h"
#include "ivieweffects.h"
#include "iinput.h"
#include "particle_parse.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( predicted_viewmodel, CTerrorViewModel );

IMPLEMENT_NETWORKCLASS_ALIASED( TerrorViewModel, DT_TerrorViewModel )

BEGIN_NETWORK_TABLE( CTerrorViewModel, DT_TerrorViewModel )
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
// Whole native289060. Real default member constructors produce, in order:
// CountdownTimer3192 duration0/timestamp-1; primary ref3204; secondary ref3216;
// arms EHANDLE3228 INVALID. No pose/cache/attachment/string body initialization.
CTerrorViewModel::CTerrorViewModel()
{
}

// Whole native289110: implicit secondary then primary reference unlink, followed
// by the existing PredictedVM history destruction and BaseVM/overlay destruction.
// No extra StopEmission, Release(arms), brass ownership or cache write is added.
CTerrorViewModel::~CTerrorViewModel()
{
}
#endif

void CTerrorViewModel::Precache( void )
{
	// Original server RVA 0x352EE0 precaches these systems before its base.
	PrecacheParticleSystem( "weapon_molotov_fp" );
	PrecacheParticleSystem( "weapon_molotov_fp_wick" );
	PrecacheParticleSystem( "weapon_pipebomb_fuse_fp" );

	BaseClass::Precache();
}
#ifdef CLIENT_DLL
extern ConVar r_drawviewmodel;
// C287FC0. Pose-index fields and last-facing yaw have NO native ctor writes;
// they start through real zero-filled allocation and are published here.
void CTerrorViewModel::CacheModelPoseParameters( CStudioHdr *model )
{
	m_nMoveXPoseParameter = LookupPoseParameter( model, "move_x" );
	m_nVerticalAimPoseParameter = LookupPoseParameter( model, "ver_aims" );
	m_nHorizontalAimPoseParameter = LookupPoseParameter( model, "hor_aims" );
	m_flLastFacingYaw = GetLocalAngles().y;
	const int bodyPitch = LookupPoseParameter( model, "body_pitch" );
	if ( bodyPitch >= 0 )
		SetPoseParameter( GetModelPtr(), bodyPitch, 0.0f );
	const int bodyYaw = LookupPoseParameter( model, "body_yaw" );
	if ( bodyYaw >= 0 )
		SetPoseParameter( GetModelPtr(), bodyYaw, 0.0f );
}

// C288B40. Full callback; gun/arms presentation remains the existing M4 owner.
CStudioHdr *CTerrorViewModel::OnNewModel()
{
	CStudioHdr *model = BaseClass::OnNewModel();
	CacheModelPoseParameters( model );
	m_nMuzzleAttachment = LookupAttachment( "muzzle_flash" );
	m_nMuzzleRightAttachment = LookupAttachment( "muzzle_flash_r" );
	m_nMuzzleLeftAttachment = LookupAttachment( "muzzle_flash_l" );
	m_nShellAttachment = LookupAttachment( "shell" );
	m_nShellRightAttachment = LookupAttachment( "shell_r" );
	m_nShellLeftAttachment = LookupAttachment( "shell_l" );
	return model;
}

// Whole native287DB0. Actual3296/3300/3304 OnNewModel caches, never per-shot lookup.
int CTerrorViewModel::GetMuzzleFlashAttachment( bool dual, bool right )
{
	if ( !dual )
		return m_nMuzzleAttachment;
	if ( right )
		return m_nMuzzleRightAttachment;
	return m_nMuzzleLeftAttachment;
}

// Whole native287DE0, direct caller2F94D3 after real TerrorVM RTTI success.
int CTerrorViewModel::GetEjectBrassAttachment( bool dual, bool right )
{
	if ( !dual )
		return m_nShellAttachment;
	if ( right )
		return m_nShellRightAttachment;
	return m_nShellLeftAttachment;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#pragma fp_contract( off )
#endif
// C2880D0. Formal owner parameter is genuinely unused: native queries GetOwner.
void CTerrorViewModel::UpdateViewModelPoseParameters( CBasePlayer * )
{
	CStudioHdr *studio = GetModelPtr();
	if ( !studio || !studio->IsValid() )
		return;
	C_CSPlayer *viewPlayer = C_CSPlayer::GetViewCSPlayer();
	if ( !viewPlayer )
		return;
	CBaseEntity *ownerEntity = GetOwner();
	if ( !ownerEntity || !ownerEntity->IsPlayer() )
		return;
	C_TerrorPlayer *owner = static_cast< C_TerrorPlayer * >( ownerEntity );
	C_TerrorPlayer *local = static_cast< C_TerrorPlayer * >( C_BasePlayer::GetLocalPlayer() );
	if ( !local || r_drawviewmodel.GetInt() == 0 )
		return;
	SetBodygroup( 0, 0 );
	SetBodygroup( 1, owner->GetSurvivorCharacter() );
	const float frameTime = gpGlobals->frametime;
	if ( m_nMoveXPoseParameter >= 0 )
	{
		Vector velocity( local->GetLocalVelocity().x, local->GetLocalVelocity().y, 0.0f );
		const float speed = sqrtf( velocity.y * velocity.y + velocity.x * velocity.x );
		QAngle view = viewPlayer->EyeAngles();
		view.x = 0.0f;
		view.z = 0.0f;
		Vector forward;
		AngleVectors( view, &forward );
		VectorNormalize( velocity );
		VectorNormalize( forward );
		const bool backwards = ( ( forward.y * velocity.y + forward.x * velocity.x ) +
			forward.z * velocity.z ) < -0.1f;
		const float runSpeed = owner->GetPlayerMaxSpeed(); // Actual client primary341.
		const float walkSpeed = owner->GetWalkTopSpeed();
		float low, high;
		GetPoseParameterRange( m_nMoveXPoseParameter, low, high );
		const float middle = ( high - low ) * 0.5f + low;
		const float upper = high - middle;
		const float halfway = upper * 0.5f + middle;
		float goal;
		// Actual ASM: .75 walk deadzone, half-upper at walk, then linear run arm.
		// No clamped speed fraction or normalization/center repair is native.
		if ( walkSpeed == runSpeed )
		{
			if ( walkSpeed * 0.75f > speed )
				goal = middle;
			else
				goal = ( ( speed / walkSpeed ) * upper ) * 0.5f + middle;
		}
		else
		{
			if ( walkSpeed * 0.75f > speed )
				goal = middle;
			else if ( walkSpeed > speed )
				goal = ( ( speed / walkSpeed ) * upper ) * 0.5f + middle;
			else
				goal = ( ( speed - walkSpeed ) / ( runSpeed - walkSpeed ) ) *
					( high - halfway ) + halfway;
		}
		if ( backwards )
			goal = -goal;
		const float normalized = GetPoseParameter( m_nMoveXPoseParameter );
		float current = normalized * ( high - low ) + low;
		const float rate = fabsf( current ) > fabsf( goal ) ? 2.0f : 1.0f;
		if ( goal > current )
		{
			current += rate * frameTime;
			if ( !( goal > current ) )
				current = goal;
		}
		else if ( current > goal )
		{
			current -= rate * frameTime;
			if ( !( current > goal ) )
				current = goal;
		}
		SetPoseParameter( studio, m_nMoveXPoseParameter, current );
	}
	if ( m_nVerticalAimPoseParameter >= 0 )
	{
		float low, high;
		GetPoseParameterRange( m_nVerticalAimPoseParameter, low, high );
		const float pitch = AngleNormalize( viewPlayer->EyeAngles().x );
		const float negativePitch = -fabsf( pitch );
		const Vector &offset = owner->GetViewOffset();
		const CViewVectors *duckVectors = GameRules()->GetViewVectors();
		const float height = offset.z - duckVectors->m_vDuckView.z;
		const CViewVectors *standingVectors = GameRules()->GetViewVectors();
		const float *standingZ = &standingVectors->m_vView.z;
		const CViewVectors *secondDuckVectors = GameRules()->GetViewVectors();
		float standingFraction = height / ( *standingZ - secondDuckVectors->m_vDuckView.z );
		CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( GetOwningWeapon() );
		if ( weapon && weapon->GetWeaponID() == TERROR_WEAPON_MOLOTOV )
			standingFraction = 1.0f;
		const float angle = ( 1.0f - standingFraction ) * negativePitch + standingFraction * pitch;
		const float goal = ( ( angle + 90.0f ) * ( 1.0f / 180.0f ) ) *
			( high - low ) + low;
		const float normalized = GetPoseParameter( m_nVerticalAimPoseParameter );
		float current = normalized * ( high - low ) + low;
		if ( goal > current )
		{
			current += frameTime * 10.0f;
			if ( !( goal > current ) )
				current = goal;
		}
		else if ( current > goal )
		{
			current -= frameTime * 10.0f;
			if ( !( current > goal ) )
				current = goal;
		}
		SetPoseParameter( studio, m_nVerticalAimPoseParameter, current );
	}
	if ( m_nHorizontalAimPoseParameter >= 0 )
	{
		const float yaw = viewPlayer->EyeAngles().y;
		const float fraction = static_cast< float >( static_cast< double >(
			AngleNormalize( yaw - m_flLastFacingYaw ) ) * static_cast< double >( 1.0f / 90.0f ) );
		float nextYaw = ( fraction * 0.15f ) * 90.0f + m_flLastFacingYaw;
		// Proven reversed native clamp. Ordinary understeps snap to requested yaw;
		// do NOT replace with Approach/ApproachAngle or correct the overshoot bug.
		if ( yaw > m_flLastFacingYaw )
		{
			if ( !( nextYaw > yaw ) )
				nextYaw = yaw;
		}
		else if ( m_flLastFacingYaw > yaw )
		{
			if ( !( yaw > nextYaw ) )
				nextYaw = yaw;
		}
		m_flLastFacingYaw = AngleNormalize( nextYaw );
		SetPoseParameter( studio, m_nHorizontalAimPoseParameter, fraction );
	}
	OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );
	if ( local->m_lifeState != LIFE_ALIVE )
		ResetPoseParameterInterpolation(); // ONLY actual inherited pose watcher.
}

// C288BF0. Actual3192 fidget deadline, not a sequence-idle alias/constant provider.
void CTerrorViewModel::UpdateViewModelFidget( CBasePlayer * )
{
	if ( GetNumAnimOverlays() < 4 )
		return;
	if ( GetAnimOverlay( 0 )->m_flWeight > 0.0f )
	{
		m_viewmodelFidgetTimer.Invalidate();
		return;
	}
	CAnimationLayer *layer = GetAnimOverlay( 2 );
	if ( layer->m_nOrder != 15 )
	{
		m_viewmodelFidgetTimer.Invalidate();
		return;
	}
	if ( !( m_viewmodelFidgetTimer.m_timestamp > 0.0f ) )
	{
		m_viewmodelFidgetTimer.Start( RandomFloat( 1.0f, 3.0f ) );
		return;
	}
	if ( !( gpGlobals->curtime >= m_viewmodelFidgetTimer.m_timestamp ) )
		return;
	m_viewmodelFidgetTimer.Invalidate();
	CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( GetOwningWeapon() );
	if ( !weapon )
		return;
	const int sequence = SelectWeightedSequence( weapon->TranslateViewModelActivity(
		TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_FIDGET_LAYER ) ) );
	layer->SetSequence( sequence );
	if ( sequence < 0 )
	{
		layer->SetSequence( 0 );
		layer->SetOrder( 15 );
		layer->SetWeight( 0.0f );
		return;
	}
	layer->SetWeight( 1.0f );
	layer->SetOrder( 1 );
	layer->m_flPrevCycle = 0.0f;
	layer->SetCycle( 0.0f );
	layer->m_flLayerAnimtime = gpGlobals->curtime;
}

// C288F40. No base bob, weapon AddViewmodelBob, VR/Sixense or extra lag backend.
void CTerrorViewModel::CalcViewModelView( CBasePlayer *owner,
	const Vector &eyePosition, const QAngle &eyeAngles )
{
	QAngle original = eyeAngles;
	QAngle angles = eyeAngles;
	Vector origin = eyePosition;
	if ( !prediction->InPrediction() )
	{
		UpdateViewModelPoseParameters( owner );
		UpdateViewModelFidget( owner );
	}
	CalcViewModelLag( origin, angles, original );
	if ( !prediction->InPrediction() )
	{
		// Native active-split-user guard omitted ONLY under Main's real user0
		// projection; existing real SDK vieweffects is that sole user's owner.
		vieweffects->ApplyShake( origin, angles, 0.1f );
	}
	SetLocalOrigin( origin );
	SetLocalAngles( angles );
}

// C287DA0 is the genuine tail jump to inherited PredictedVM17A4F0 RET12.
void CTerrorViewModel::CalcViewModelLag( Vector &origin, QAngle &angles, QAngle &original )
{
	BaseClass::CalcViewModelLag( origin, angles, original );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// C289230. Native SetDrawOnlyForSplitScreenUser(owner.GetViewSlot()) at289265
// projects to the one genuine target user0; no fake effect slot/filter is made.
void CTerrorViewModel::OnNewParticleEffect( const char *name, CNewParticleEffect *effect )
{
	if ( !V_stricmp( name, "weapon_molotov_fp" ) )
		m_hThrowableEffect = effect;
	else if ( !V_stricmp( name, "weapon_molotov_fp_wick" ) )
		m_hMolotovWickEffect = effect;
	else if ( !V_stricmp( name, "weapon_pipebomb_fuse_fp" ) )
		m_hThrowableEffect = effect;
}

// C289370. The genuine property deletion callback invokes this before removing
// its effect entry; CUtlReference assignment unlinks exactly the matching handle.
void CTerrorViewModel::OnParticleEffectDeleted( CNewParticleEffect *effect )
{
	BaseClass::OnParticleEffectDeleted( effect );
	if ( effect == m_hThrowableEffect )
		m_hThrowableEffect = NULL;
	else if ( effect == m_hMolotovWickEffect )
		m_hMolotovWickEffect = NULL;
}

// C2893C0, complete body. The native user-slot argument is eliminated under the
// approved single-user0 projection, not routed through an SDK static draw policy.
void CTerrorViewModel::UpdateThrowableParticles()
{
	C_BaseCombatWeapon *weapon = GetOwningWeapon();
	const int weaponID = weapon ? static_cast< CWeaponCSBase * >( weapon )->GetWeaponID() : TERROR_WEAPON_NONE;
	C_BaseEntity *owner = GetOwner();
	if ( !owner || !owner->IsPlayer() )
		return; // Native early return deliberately does NOT stop existing effects.
	C_TerrorPlayer *player = static_cast< C_TerrorPlayer * >( owner );
	const bool hide = ( player->IsThirdPersonCameraActive() || input->CAM_IsThirdPerson() ) &&
		( player->m_lifeState != LIFE_ALIVE || !player->m_isIncapacitated ) &&
		!player->m_isHangingFromLedge;

	if ( r_drawviewmodel.GetInt() != 0 && !hide )
	{
		if ( weaponID == TERROR_WEAPON_MOLOTOV )
		{
			if ( !m_hThrowableEffect )
				DispatchParticleEffect( "weapon_molotov_fp", PATTACH_POINT_FOLLOW, this, "Wick" );
			if ( !m_hMolotovWickEffect )
				DispatchParticleEffect( "weapon_molotov_fp_wick", PATTACH_POINT_FOLLOW, this, "Wick" );
			return;
		}
		if ( weaponID == TERROR_WEAPON_PIPE_BOMB )
		{
			if ( !m_hThrowableEffect )
				DispatchParticleEffect( "weapon_pipebomb_fuse_fp", PATTACH_POINT_FOLLOW, this, "Fuse" );
			if ( m_hMolotovWickEffect )
			{
				m_hMolotovWickEffect->StopEmission( false, true, false );
				m_hMolotovWickEffect->SetRemoveFlag();
				m_hMolotovWickEffect = NULL;
			}
			return;
		}
	}

	if ( m_hThrowableEffect )
	{
		m_hThrowableEffect->StopEmission( false, true, false );
		m_hThrowableEffect->SetRemoveFlag();
		m_hThrowableEffect = NULL;
	}
	if ( m_hMolotovWickEffect )
	{
		m_hMolotovWickEffect->StopEmission( false, true, false );
		m_hMolotovWickEffect->SetRemoveFlag();
		m_hMolotovWickEffect = NULL;
	}
}

// C2895B0. Reuse the complete capture simulation_hierarchy_native.json#16FD.
// Native's active-user save/restore is omitted only by the approved user0 mapping.
// Its base is the REAL inherited BaseAnimating Simulate, never an AddEntity wrapper.
bool CTerrorViewModel::Simulate()
{
	UpdateThrowableParticles();
	BaseClass::Simulate();
	return true;
}
#endif // CLIENT_DLL

// Native server 0x352e90; 0x5c43bc is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace predicted_viewmodelPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "predicted_viewmodel", 0x5c43bc ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "predicted_viewmodel", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
