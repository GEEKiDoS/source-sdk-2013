//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client side view model implementation. Responsible for drawing
//			the view model.
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_baseviewmodel.h"
#include "model_types.h"
#include "hud.h"
#include "view_shared.h"
#include "iviewrender.h"
#include "view.h"
#include "mathlib/vmatrix.h"
#include "cl_animevent.h"
#include "eventlist.h"
#include "tools/bonelist.h"
#include <KeyValues.h>
#include "hltvcamera.h"
#ifdef TF_CLIENT_DLL
	#include "c_tf_player.h"
	#include "tf_weaponbase.h"
#endif
#if defined( L4D2 )
#include "animationlayer.h"
#include "datacache/imdlcache.h"
#include "iinput.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/effects/terror_glow_policy.h"
#endif

#if defined( REPLAY_ENABLED )
#include "replay/replaycamera.h"
#include "replay/ireplaysystem.h"
#include "replay/ienginereplay.h"
#endif

// NVNT haptics system interface
#if !defined( L4D2 )
#include "haptics/ihaptics.h"
#endif // !L4D2


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CSTRIKE_DLL
	ConVar cl_righthand( "cl_righthand", "1", FCVAR_ARCHIVE, "Use right-handed view models." );
#endif

#ifdef TF_CLIENT_DLL
	ConVar cl_flipviewmodels( "cl_flipviewmodels", "0", FCVAR_USERINFO | FCVAR_ARCHIVE | FCVAR_NOT_CONNECTED, "Flip view models." );
#endif
#if defined( L4D2 )
extern ConVar vm_draw_always;
static ConVar vm_camera( "vm_camera", "1", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar vm_camera_interp_speed( "vm_camera_interp_speed", "8.0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar vm_camera_scale( "vm_camera_scale", "1.0f", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar vm_camera_no_roll( "vm_camera_no_roll", "0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar vm_debug( "vm_debug", "0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar vm_fade( "vm_fade", "1", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 ); // C4FABE0 / object10728CF8.

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// Whole C0B4CF0 with its two identical rate arguments folded at this caller.
// This is scalar movement, NOT ApproachAngle/angle normalization or exponential
// interpolation. Ordered outer tests leave unordered target/current unchanged;
// unordered ADVANCED value is clamped to target by the native JA fallback.
static void UpdateOriginalViewModelCameraAngle( float target, float rate, float *value )
{
	if ( target > *value )
	{
		const float advanced = ( gpGlobals->frametime * rate ) + *value;
		*value = advanced;
		if ( !( target > advanced ) )
			*value = target;
	}
	else if ( *value > target )
	{
		const float advanced = *value - ( gpGlobals->frametime * rate );
		*value = advanced;
		if ( !( advanced > target ) )
			*value = target;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// C1006AB20. Resolve actual registered activities through the sole native-name map.
static bool IsTerrorViewModelFireActivity( Activity activity )
{
	switch ( LookupTerrorOriginalActivity( activity ) )
	{
	case TERROR_ORIGINAL_ACT_VM_SHOOT_DUAL_PISTOL_LAYER:
	case TERROR_ORIGINAL_ACT_VM_SHOOT_LAYER:
	case TERROR_ORIGINAL_ACT_VM_SHOOT_PISTOL_LAYER:
	case TERROR_ORIGINAL_ACT_VM_SHOOT_RIFLE_LAYER:
	case TERROR_ORIGINAL_ACT_VM_SHOOT_SMG_LAYER:
	case TERROR_ORIGINAL_ACT_VM_SHOOT_SNIPER_LAYER:
	case TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK:
	case TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK_DUAL_PISTOL:
	case TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK_LAYER:
	case TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK_PISTOL:
	case TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK_RIFLE:
	case TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK_SILENCED:
	case TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK_SMG:
	case TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK_SNIPER:
		return true;
	default:
		return false;
	}
}

// C1006AB90. These are melee identities, not generic reload/weapon-type classes.
static bool IsTerrorViewModelMeleeActivity( Activity activity )
{
	switch ( LookupTerrorOriginalActivity( activity ) )
	{
	case TERROR_ORIGINAL_ACT_VM_MELEE:
	case TERROR_ORIGINAL_ACT_VM_MELEE_LAYER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_BAREHAND:
	case TERROR_ORIGINAL_ACT_VM_MELEE_BAREHAND_LAYER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_DUAL_PISTOL:
	case TERROR_ORIGINAL_ACT_VM_MELEE_DUAL_PISTOL_LAYER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_PISTOL:
	case TERROR_ORIGINAL_ACT_VM_MELEE_PISTOL_LAYER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_RIFLE:
	case TERROR_ORIGINAL_ACT_VM_MELEE_RIFLE_LAYER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_SMG:
	case TERROR_ORIGINAL_ACT_VM_MELEE_SMG_LAYER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_SNIPER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_SNIPER_LAYER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_PIPEBOMB:
	case TERROR_ORIGINAL_ACT_VM_MELEE_PIPEBOMB_LAYER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_PAINPILLS:
	case TERROR_ORIGINAL_ACT_VM_MELEE_PAINPILLS_LAYER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_MEDKIT:
	case TERROR_ORIGINAL_ACT_VM_MELEE_MEDKIT_LAYER:
	case TERROR_ORIGINAL_ACT_VM_MELEE_GASCAN:
		return true;
	default:
		return false;
	}
}
#endif

void PostToolMessage( HTOOLHANDLE hEntity, KeyValues *msg );

void FormatViewModelAttachment( Vector &vOrigin, bool bInverse )
{
	// Presumably, SetUpView has been called so we know our FOV and render origin.
	const CViewSetup *pViewSetup = view->GetPlayerViewSetup();
	
	float worldx = tan( pViewSetup->fov * M_PI/360.0 );
	float viewx = tan( pViewSetup->fovViewmodel * M_PI/360.0 );

	// aspect ratio cancels out, so only need one factor
	// the difference between the screen coordinates of the 2 systems is the ratio
	// of the coefficients of the projection matrices (tan (fov/2) is that coefficient)
	// NOTE: viewx was coming in as 0 when folks set their viewmodel_fov to 0 and show their weapon.
	float factorX = viewx ? ( worldx / viewx ) : 0.0f;
	float factorY = factorX;
	
	// Get the coordinates in the viewer's space.
	Vector tmp = vOrigin - pViewSetup->origin;
	Vector vTransformed( MainViewRight().Dot( tmp ), MainViewUp().Dot( tmp ), MainViewForward().Dot( tmp ) );

	// Now squash X and Y.
	if ( bInverse )
	{
		if ( factorX != 0 && factorY != 0 )
		{
			vTransformed.x /= factorX;
			vTransformed.y /= factorY;
		}
		else
		{
			vTransformed.x = 0.0f;
			vTransformed.y = 0.0f;
		}
	}
	else
	{
		vTransformed.x *= factorX;
		vTransformed.y *= factorY;
	}



	// Transform back to world space.
	Vector vOut = (MainViewRight() * vTransformed.x) + (MainViewUp() * vTransformed.y) + (MainViewForward() * vTransformed.z);
	vOrigin = pViewSetup->origin + vOut;
}

#ifdef TF_CLIENT_DLL
bool TeamFortress_ShouldFlipClientViewModel( void )
{
	if ( IsLocalPlayerSpectator() )
	{
		// Use spectated client's handedness preference
		C_TFPlayer *pSpecTarget = ToTFPlayer( UTIL_PlayerByIndex( GetSpectatorTarget() ) );
		if ( pSpecTarget )
		{
			return pSpecTarget->m_bFlipViewModels;
		}
	}

	return cl_flipviewmodels.GetBool();
}
#endif //TF_CLIENT_DLL

void C_BaseViewModel::FormatViewModelAttachment( int nAttachment, matrix3x4_t &attachmentToWorld )
{
	Vector vecOrigin;
	MatrixPosition( attachmentToWorld, vecOrigin );
	::FormatViewModelAttachment( vecOrigin, false );
	PositionMatrix( vecOrigin, attachmentToWorld );
}


bool C_BaseViewModel::IsViewModel() const
{
	return true;
}

void C_BaseViewModel::UncorrectViewModelAttachment( Vector &vOrigin )
{
	// Unformat the attachment.
	::FormatViewModelAttachment( vOrigin, true );
}


//-----------------------------------------------------------------------------
// Purpose
//-----------------------------------------------------------------------------
#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// C1006AFC0, complete body. Main-approved sole-user0 projection omits only the
// native active-user flag scopes; it does not replace the genuine camera query.
void C_BaseViewModel::FireEvent( const Vector &origin, const QAngle &angles, int event, const char *options )
{
	if ( ( event == AE_CL_PLAYSOUND || event == CL_EVENT_SOUND ) && GetOwner() )
	{
		CLocalPlayerFilter filter;
		const Vector &soundOrigin = GetAbsOrigin();
		C_BaseEntity *soundOwner = GetOwner();
		const int soundSource = soundOwner->GetSoundSourceIndex();
		EmitSound( filter, soundSource, options, &soundOrigin );
		return;
	}

	if ( event == 21 )
	{
		float fireLayerWeight = -1.0f;
		for ( int i = 0; i < GetNumAnimOverlays(); ++i )
		{
			CAnimationLayer *layer = GetAnimOverlay( i );
			// C2B570 tests ONLY order!=15, not positive weight or sequence validity.
			if ( layer->m_nOrder == 15 )
				continue;
			const Activity activity = GetSequenceActivity( layer->m_nSequence );
			if ( IsTerrorViewModelFireActivity( activity ) )
				fireLayerWeight = layer->m_flWeight; // Last fire layer wins, NOT max().
			else if ( IsTerrorViewModelMeleeActivity( activity ) && fireLayerWeight > layer->m_flWeight )
				return;
		}
	}

	C_BaseEntity *owner = GetOwner();
	if ( !owner || !owner->IsPlayer() )
		return;
	C_BaseCombatWeapon *weapon = static_cast< C_BasePlayer * >( owner )->GetActiveWeapon();
	if ( !weapon || weapon->OnFireEvent( this, origin, angles, event, options ) )
		return;

	bool isZoomed = false;
	C_BaseEntity *weaponOwner = weapon->GetOwner();
	if ( weaponOwner && weaponOwner->IsPlayer() )
	{
		C_BasePlayer *player = static_cast< C_BasePlayer * >( weaponOwner );
		const float fov = player->GetFOV();
		const int defaultFOV = player->GetDefaultFOV();
		isZoomed = static_cast< double >( fov ) != static_cast< double >( defaultFOV );
	}
	if ( event != 20 || ( !input->CAM_IsThirdPerson() && !isZoomed ) )
		BaseClass::FireEvent( origin, angles, event, options );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
void C_BaseViewModel::FireEvent( const Vector& origin, const QAngle& angles, int event, const char *options )
{
	// We override sound requests so that we can play them locally on the owning player
	if ( ( event == AE_CL_PLAYSOUND ) || ( event == CL_EVENT_SOUND ) )
	{
		// Only do this if we're owned by someone
		if ( GetOwner() != NULL )
		{
			CLocalPlayerFilter filter;
			EmitSound( filter, GetOwner()->GetSoundSourceIndex(), options, &GetAbsOrigin() );
			return;
		}
	}

	// Otherwise pass the event to our associated weapon
	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
#if !defined( L4D2 )
		// NVNT notify the haptics system of our viewmodel's event
		if ( haptics )
			haptics->ProcessHapticEvent(4,"Weapons",pWeapon->GetName(),"AnimationEvents",VarArgs("%i",event));
#endif // !L4D2

		bool bResult = pWeapon->OnFireEvent( this, origin, angles, event, options );
		if ( !bResult )
		{
			BaseClass::FireEvent( origin, angles, event, options );
		}
	}
}

#endif

#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// Entire post-base phase of C6B210. Native primary120 is purefalse43070 for ALL
// three RTTI-proven VM classes (Base,Predicted,Terror); fold that call ONLY here.
// IsClientCreated is the actual other native predicate. Neither GetPredictable
// nor ShouldPredict129/+516 may be substituted into this concrete-family path.
bool C_BaseViewModel::Interpolate( float currentTime )
{
	CStudioHdr *pHdr = GetModelPtr();
	const bool result = BaseClass::Interpolate( currentTime );
	if ( !pHdr || !pHdr->IsValid() )
		return result;

	C_BaseEntity *owner = GetOwner();
	C_BasePlayer *player = owner && owner->IsPlayer() ? static_cast< C_BasePlayer * >( owner ) : NULL;
	float elapsed;
	if ( IsClientCreated() )
	{
		const float now = player ? player->GetFinalPredictedTime() : currentTime;
		elapsed = now - m_flAnimTime;
		if ( !engine->IsPaused() )
			elapsed = ( gpGlobals->interpolation_amount * TICK_INTERVAL ) + elapsed;
		if ( elapsed < 0.0f )
			elapsed = 0.0f;
	}
	else
	{
		elapsed = currentTime - m_flAnimTime;
	}
	if ( m_flPreviousElapsedTime > elapsed )
		m_flPreviousElapsedTime = elapsed;
	float delta = elapsed - m_flPreviousElapsedTime;
	m_flPreviousElapsedTime = elapsed;
	if ( engine->IsPaused() )
		delta = 0.0f;

	float playback = 1.0f;
	C_BaseCombatWeapon *weapon = GetWeapon();
	if ( weapon )
	{
		playback = static_cast< float >( weapon->GetEffectivePlaybackRate() );
		if ( playback <= 0.0f )
			playback = 1.0f;
	}
	const float rate = GetSequenceCycleRate( pHdr, GetSequence() );
	// Main-approved x87-extended/x64-double numeric boundary, NOT bit-parity.
	// C6B419 stores increment to float BEFORE the native SSE prior-cycle add.
	const double product = static_cast< double >( rate ) * static_cast< double >( delta ) * static_cast< double >( playback );
	const float increment = static_cast< float >( product );
	float cycle = m_flMainSequenceCycle + increment;
	if ( cycle >= 1.0f )
	{
		if ( IsSequenceLooping( GetSequence() ) )
			cycle = static_cast< float >( fmod( static_cast< double >( cycle ), 1.0 ) );
		else
			cycle = 0.99900001f;
	}
	SetCycle( cycle );
	m_flMainSequenceCycle = cycle;
	AdvanceTerrorLayers( currentTime, pHdr, player, playback );
	return result;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
bool C_BaseViewModel::Interpolate( float currentTime )
{
	CStudioHdr *pStudioHdr = GetModelPtr();
	// Make sure we reset our animation information if we've switch sequences
	UpdateAnimationParity();

	bool bret = BaseClass::Interpolate( currentTime );

	// Hack to extrapolate cycle counter for view model
	float elapsed_time = currentTime - m_flAnimTime;
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();

	// Predicted viewmodels have fixed up interval
	if ( GetPredictable() || IsClientCreated() )
	{
		Assert( pPlayer );
		float curtime = pPlayer ? pPlayer->GetFinalPredictedTime() : gpGlobals->curtime;
		elapsed_time = curtime - m_flAnimTime;
		// Adjust for interpolated partial frame
		if ( !engine->IsPaused() )
		{
			elapsed_time += ( gpGlobals->interpolation_amount * TICK_INTERVAL );
		}
	}

	// Prediction errors?	
	if ( elapsed_time < 0 )
	{
		elapsed_time = 0;
	}

	float dt = elapsed_time * GetSequenceCycleRate( pStudioHdr, GetSequence() ) * GetPlaybackRate();
	if ( dt >= 1.0f )
	{
		if ( !IsSequenceLooping( GetSequence() ) )
		{
			dt = 0.999f;
		}
		else
		{
			dt = fmod( dt, 1.0f );
		}
	}

	SetCycle( dt );
	#if defined( L4D2 )
	AdvanceTerrorLayers( currentTime );
	#endif
	return bret;
}

#endif


#if defined( L4D2 )
// Whole C6AD90. Parsed FileWeaponInfo owner already supplies BuiltRightHanded;
// native ignores both AllowFlipping and cl_righthand. No second parser/metadata.
bool C_BaseViewModel::ShouldFlipViewModel()
{
	C_BaseCombatWeapon *weapon = GetWeapon();
	return weapon && !weapon->GetWpnData().m_bBuiltRightHanded;
}
#else
bool C_BaseViewModel::ShouldFlipViewModel()
{
#ifdef CSTRIKE_DLL
	// If cl_righthand is set, then we want them all right-handed.
	CBaseCombatWeapon *pWeapon = m_hWeapon.Get();
	if ( pWeapon )
	{
		const FileWeaponInfo_t *pInfo = &pWeapon->GetWpnData();
		return pInfo->m_bAllowFlipping && pInfo->m_bBuiltRightHanded != cl_righthand.GetBool();
	}
#endif

#ifdef TF_CLIENT_DLL
	CBaseCombatWeapon *pWeapon = m_hWeapon.Get();
	if ( pWeapon )
	{
		return pWeapon->m_bFlipViewModel != TeamFortress_ShouldFlipClientViewModel();
	}
#endif

	return false;
}

#endif


void C_BaseViewModel::ApplyBoneMatrixTransform( matrix3x4_t& transform )
{
	if ( ShouldFlipViewModel() )
	{
		matrix3x4_t viewMatrix, viewMatrixInverse;

		// We could get MATERIAL_VIEW here, but this is called sometimes before the renderer
		// has set that matrix. Luckily, this is called AFTER the CViewSetup has been initialized.
		const CViewSetup *pSetup = view->GetPlayerViewSetup();
		AngleMatrix( pSetup->angles, pSetup->origin, viewMatrixInverse );
		MatrixInvert( viewMatrixInverse, viewMatrix );

		// Transform into view space.
		matrix3x4_t temp, temp2;
		ConcatTransforms( viewMatrix, transform, temp );
		
		// Flip it along X.
		
		// (This is the slower way to do it, and it equates to negating the top row).
		//matrix3x4_t mScale;
		//SetIdentityMatrix( mScale );
		//mScale[0][0] = 1;
		//mScale[1][1] = -1;
		//mScale[2][2] = 1;
		//ConcatTransforms( mScale, temp, temp2 );
		temp[1][0] = -temp[1][0];
		temp[1][1] = -temp[1][1];
		temp[1][2] = -temp[1][2];
		temp[1][3] = -temp[1][3];

		// Transform back out of view space.
		ConcatTransforms( viewMatrixInverse, temp, transform );
	}
}

//-----------------------------------------------------------------------------
// Purpose: check if weapon viewmodel should be drawn
//-----------------------------------------------------------------------------
bool C_BaseViewModel::ShouldDraw()
{
	if ( engine->IsHLTV() )
	{
		return ( HLTVCamera()->GetMode() == OBS_MODE_IN_EYE &&
				 HLTVCamera()->GetPrimaryTarget() == GetOwner()	);
	}
#if defined( REPLAY_ENABLED )
	else if ( g_pEngineClientReplay->IsPlayingReplayDemo() )
	{
		return ( ReplayCamera()->GetMode() == OBS_MODE_IN_EYE &&
				 ReplayCamera()->GetPrimaryTarget() == GetOwner() );
	}
#endif
#if defined( L4D2 )
	else if ( vm_draw_always.GetInt() != 0 )
	{
		return true;
	}
	else
	{
		// C6B840 uses the same IN_EYE-only owner resolver as the native view.
		return GetOwner() == TerrorGlow_ResolveRenderPlayer( C_BasePlayer::GetLocalPlayer( 0 ) ) &&
			C_BaseEntity::ShouldDraw();
	}
#else
	else
	{
		return BaseClass::ShouldDraw();
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Render the weapon. Draw the Viewmodel if the weapon's being carried
//			by this player, otherwise draw the worldmodel.
//-----------------------------------------------------------------------------
#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// Whole native C6B960, target one-argument draw adapter. Native alpha is a byte
// of RenderableInstance_t supplied by the original leaf. Main DECISION1 moves
// exact native0/1/2 per-pass alpha/color application into Move's REAL C1ED0 leaf
// collator. Read existing incoming render state here; never overwrite a forced
// opaque255 pass with GetFxBlend/255. No instance getter/context/engine ABI.
int C_BaseViewModel::DrawModel( int flags )
{
	if ( !m_bReadyToDraw )
		return 0;
	C_BaseEntity *owner = GetOwner();
	if ( owner && owner->IsPlayer() && static_cast< C_BasePlayer * >( owner )->IsThirdPersonCameraActive() )
		return 0;

	float blend = 0.0f;
	float color[3];
	if ( flags & STUDIO_RENDER )
	{
		blend = render->GetBlend(); // Real already-applied incoming per-pass state.
		if ( blend <= 0.0f )
			return 0;
	}

	Vector cameraOrigin;
	QAngle cameraAngles;
	if ( vm_camera.GetInt() != 0 && m_iCameraAttachment > 0 )
	{
		if ( !GetAttachmentLocal( m_iCameraAttachment, cameraOrigin, cameraAngles ) )
		{
			// Main decision:2DA40 leaves outputs unwritten on failure;6B960/B4CF0
			// otherwise read uninitialized angles. Use existing zero-angle arm only.
			cameraAngles.Init();
		}
	}
	else
	{
		cameraAngles.Init();
	}
	const float cameraRate = vm_camera_interp_speed.GetFloat();
	UpdateOriginalViewModelCameraAngle( cameraAngles.x, cameraRate, &m_angViewModelCamera.x );
	UpdateOriginalViewModelCameraAngle( cameraAngles.y, cameraRate, &m_angViewModelCamera.y );
	UpdateOriginalViewModelCameraAngle( cameraAngles.z, cameraRate, &m_angViewModelCamera.z );

	CMatRenderContextPtr pRenderContext( materials );
	if ( ShouldFlipViewModel() )
		pRenderContext->CullMode( MATERIAL_CULLMODE_CW );
	// Main/parent38B2: qualified nonplayer draw cannot recurse through the
	// virtual bridge. Strip ONLY native skip-stencil0x800 at this SDK boundary.
	const int result = BaseClass::DrawModel( flags & ~0x800 );
	pRenderContext->CullMode( MATERIAL_CULLMODE_CCW );
	if ( flags & STUDIO_RENDER )
	{
		if ( m_nOldAnimationParity != m_nAnimationParity )
			m_nOldAnimationParity = m_nAnimationParity;
		if ( vm_debug.GetInt() != 0 )
		{
			MDLCACHE_CRITICAL_SECTION();
			CStudioHdr *pHdr = GetModelPtr();
			const char *modelName = pHdr ? pHdr->pszName() : "(null)";
			const float cycle = GetCycle();
			const double playback = GetEffectivePlaybackRate(); // Native6BBEF FSTPf64, no f32 spill.
			const float rate = GetSequenceCycleRate( pHdr, GetSequence() );
			engine->Con_NPrintf( 16, "%s: %s(%d), cycle: %.2f cyclerate: %.2f playbackrate: %.2f\n",
				modelName, GetSequenceName( GetSequence() ), GetSequence(), cycle, rate, playback );
			int line = 17;
			for ( int i = 0; i < GetNumAnimOverlays(); ++i )
			{
				CAnimationLayer *layer = GetAnimOverlay( i );
				const bool active = layer->IsActive(); // Existing native predicate: order!=15 ONLY.
				const int order = active ? layer->m_nOrder : 0;
				const float fadeTime = active ? layer->m_flLayerFadeOuttime : 0.0f;
				const float cycle = active ? static_cast< float >( layer->m_flCycle ) : 0.0f;
				const float weight = active ? static_cast< float >( layer->m_flWeight ) : 0.0f;
				const int sequence = active ? static_cast< int >( layer->m_nSequence ) : 0;
				const char *name = active && sequence != 0 ? GetSequenceName( sequence ) : "-- ";
				engine->Con_NPrintf( line++, "%s(%d), weight: %.2f, cycle: %.2f, fadeouttime: %f order (%d)",
					name, sequence, weight, cycle, fadeTime, order );
			}
			if ( pHdr )
			{
				for ( int i = 0; i < pHdr->GetNumPoseParameters(); ++i )
					engine->Con_NPrintf( line++, "pose_param %s: %f", pHdr->pPoseParameter( i ).pszName(), GetPoseParameter( i ) );
			}
			GetColorModulation( color );
			engine->Con_NPrintf( line++, "blend=%f, color=%f,%f,%f", blend,
				color[0], color[1], color[2] );
			engine->Con_NPrintf( line++, "GetRenderMode()=%d", static_cast< int >( GetRenderMode() ) );
			engine->Con_NPrintf( line++, "m_nRenderFX=0x%8.8X", static_cast< int >( m_nRenderFX ) );
			const color32 renderColor = GetRenderColor();
			engine->Con_NPrintf( line++, "rendercolor=%d,%d,%d,%d", renderColor.r, renderColor.g, renderColor.b, renderColor.a );
			const float originZ = GetRenderOrigin().z;
			const float originY = GetRenderOrigin().y;
			const float originX = GetRenderOrigin().x;
			engine->Con_NPrintf( line++, "origin=%f, %f, %f", originX, originY, originZ );
			const float anglesZ = GetRenderAngles().z;
			const float anglesY = GetRenderAngles().y;
			const float anglesX = GetRenderAngles().x;
			engine->Con_NPrintf( line++, "angles=%f, %f, %f", anglesX, anglesY, anglesZ );
			if ( IsEffectActive( EF_NODRAW ) )
				engine->Con_NPrintf( line, "EF_NODRAW" );
		}
	}
	return result;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
int C_BaseViewModel::DrawModel( int flags )
{
	if ( !m_bReadyToDraw )
		return 0;

	if ( flags & STUDIO_RENDER )
	{
		// Determine blending amount and tell engine
		float blend = (float)( GetFxBlend() / 255.0f );

		// Totally gone
		if ( blend <= 0.0f )
			return 0;

		// Tell engine
		render->SetBlend( blend );

		float color[3];
		GetColorModulation( color );
		render->SetColorModulation(	color );
	}
	#if defined( L4D2 )
	DrawTerrorViewmodelArms( this, flags );
	#endif
		
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	C_BaseCombatWeapon *pWeapon = GetOwningWeapon();

#ifdef TF_CLIENT_DLL
	CTFWeaponBase* pTFWeapon = dynamic_cast<CTFWeaponBase*>( pWeapon );
	if ( ( flags & STUDIO_RENDER ) && pTFWeapon && pTFWeapon->m_viewmodelStatTrakAddon )
	{
		pTFWeapon->m_viewmodelStatTrakAddon->RemoveEffects( EF_NODRAW );
		pTFWeapon->m_viewmodelStatTrakAddon->DrawModel( flags );
		pTFWeapon->m_viewmodelStatTrakAddon->AddEffects( EF_NODRAW );
	}
#endif

	int ret;
	// If the local player's overriding the viewmodel rendering, let him do it
	if ( pPlayer && pPlayer->IsOverridingViewmodel() )
	{
		ret = pPlayer->DrawOverriddenViewmodel( this, flags );
	}
	else if ( pWeapon && pWeapon->IsOverridingViewmodel() )
	{
		ret = pWeapon->DrawOverriddenViewmodel( this, flags );
	}
	else
	{
		ret = BaseClass::DrawModel( flags );
	}

	// Now that we've rendered, reset the animation restart flag
	if ( flags & STUDIO_RENDER )
	{
		if ( m_nOldAnimationParity != m_nAnimationParity )
		{
			m_nOldAnimationParity = m_nAnimationParity;
		}
		// Tell the weapon itself that we've rendered, in case it wants to do something
		if ( pWeapon )
		{
			pWeapon->ViewModelDrawn( this );
		}
	}

	return ret;
}

#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int C_BaseViewModel::InternalDrawModel( int flags )
{
	CMatRenderContextPtr pRenderContext( materials );
	if ( ShouldFlipViewModel() )
		pRenderContext->CullMode( MATERIAL_CULLMODE_CW );

	int ret = BaseClass::InternalDrawModel( flags );

	pRenderContext->CullMode( MATERIAL_CULLMODE_CCW );

	return ret;
}

//-----------------------------------------------------------------------------
// Purpose: Called by the player when the player's overriding the viewmodel drawing. Avoids infinite recursion.
//-----------------------------------------------------------------------------
int C_BaseViewModel::DrawOverriddenViewmodel( int flags )
{
	return BaseClass::DrawModel( flags );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int C_BaseViewModel::GetFxBlend( void )
{
	// See if the local player wants to override the viewmodel's rendering
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer && pPlayer->IsOverridingViewmodel() )
	{
		pPlayer->ComputeFxBlend();
		return pPlayer->GetFxBlend();
	}

	C_BaseCombatWeapon *pWeapon = GetOwningWeapon();
	if ( pWeapon && pWeapon->IsOverridingViewmodel() )
	{
		pWeapon->ComputeFxBlend();
		return pWeapon->GetFxBlend();
	}

	return BaseClass::GetFxBlend();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_BaseViewModel::IsTransparent( void )
{
	// See if the local player wants to override the viewmodel's rendering
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer && pPlayer->IsOverridingViewmodel() )
	{
		return pPlayer->ViewModel_IsTransparent();
	}

	C_BaseCombatWeapon *pWeapon = GetOwningWeapon();
	if ( pWeapon && pWeapon->IsOverridingViewmodel() )
		return pWeapon->ViewModel_IsTransparent();

	return BaseClass::IsTransparent();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool C_BaseViewModel::UsesPowerOfTwoFrameBufferTexture( void )
{
#if !defined( L4D2 )
	// See if the local player wants to override the viewmodel's rendering
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer && pPlayer->IsOverridingViewmodel() )
	{
		return pPlayer->ViewModel_IsUsingFBTexture();
	}

	C_BaseCombatWeapon *pWeapon = GetOwningWeapon();
	if ( pWeapon && pWeapon->IsOverridingViewmodel() )
	{
		return pWeapon->ViewModel_IsUsingFBTexture();
	}

#endif // Native renderable4 inherits C2B710 without player/weapon overrides.
	return BaseClass::UsesPowerOfTwoFrameBufferTexture();
}

//-----------------------------------------------------------------------------
// Purpose: If the animation parity of the weapon has changed, we reset cycle to avoid popping
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void C_BaseViewModel::UpdateAnimationParity( void )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	
	// If we're predicting, then we don't use animation parity because we change the animations on the clientside
	// while predicting. When not predicting, only the server changes the animations, so a parity mismatch
	// tells us if we need to reset the animation.
	if ( m_nOldAnimationParity != m_nAnimationParity && !GetPredictable() )
	{
		float curtime = (pPlayer && IsIntermediateDataAllocated()) ? pPlayer->GetFinalPredictedTime() : gpGlobals->curtime;
		// FIXME: this is bad
		// Simulate a networked m_flAnimTime and m_flCycle
		// FIXME:  Do we need the magic 0.1?
		SetCycle( 0.0f ); // GetSequenceCycleRate( GetSequence() ) * 0.1;
		m_flAnimTime = curtime;
	}

}
#endif

//-----------------------------------------------------------------------------
// Purpose: Update global map state based on data received
// Input  : bnewentity - 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_BaseViewModel::OnDataChanged( DataUpdateType_t updateType )
{
	SetPredictionEligible( true );
	if ( GetNumAnimOverlays() != 4 )
	{
		SetNumAnimOverlays( 4 );
		for ( int i = 0; i < 4; ++i )
		{
			GetAnimOverlay( i )->Reset();
			GetAnimOverlay( i )->SetOrder( 15 );
		}
	}
	if ( vm_fade.GetInt() != 0 &&
		( m_nLastReceivedLayerSequence != m_nLayerSequence ||
		  m_flLastReceivedLayerStartTime != m_flLayerStartTime ||
		  m_nLastReceivedLayer != m_nLayer ) )
	{
		m_flLastReceivedLayerStartTime = m_flLayerStartTime;
		m_nLastReceivedLayerSequence = m_nLayerSequence;
		m_nLastReceivedLayer = m_nLayer;

		C_BaseEntity *owner = GetOwner();
		C_BasePlayer *player = owner && owner->IsPlayer() ? static_cast< C_BasePlayer * >( owner ) : NULL;
		float now = gpGlobals->curtime;
		// Original primary12043070 is purefalse for all3 concrete VM classes.
		if ( IsClientCreated() )
		{
			if ( player )
				now = player->GetFinalPredictedTime();
			if ( !engine->IsPaused() )
				now = ( gpGlobals->interpolation_amount * TICK_INTERVAL ) + now;
			if ( now < 0.0f )
				now = 0.0f;
		}

		CAnimationLayer *layer = GetAnimOverlay( m_nLayer );
		CStudioHdr *pHdr = GetModelPtr();
		bool canFadePrevious = false;
		if ( pHdr && pHdr->IsValid() && layer->m_nSequence >= 0 && layer->m_nSequence < pHdr->GetNumSeq() )
		{
			switch ( LookupTerrorOriginalActivity( static_cast< Activity >( GetSequenceActivity( layer->m_nSequence ) ) ) )
			{
			case TERROR_ORIGINAL_ACT_INVALID:
			case TERROR_ORIGINAL_ACT_VM_PULLPIN_PIPEBOMB_LAYER:
			case TERROR_ORIGINAL_ACT_VM_THROW_PIPEBOMB_LAYER:
			case TERROR_ORIGINAL_ACT_VM_PULLPIN_MOLOTOV_LAYER:
			case TERROR_ORIGINAL_ACT_VM_THROW_MOLOTOV_LAYER:
				break; // Native C1A2..C1C7 EXCLUDES these four, not all other acts.
			default:
				canFadePrevious = true;
				break;
			}
		}
		if ( layer->m_nSequence > 0 && m_nLayer != 3 &&
			 layer->m_flWeight > 0.0f && layer->m_nOrder != 15 &&
			 layer->m_flCycle < 1.0f && !IsSequenceLooping( layer->m_nSequence ) && canFadePrevious )
		{
			CAnimationLayer *previous = GetAnimOverlay( 1 );
			*previous = *layer; // Native full36-byte record copy includes owner.
			previous->SetOrder( 1 );
			previous->m_flLayerFadeOuttime = now;
		}

		const float start = m_flLayerStartTime > now ? now : static_cast< float >( m_flLayerStartTime ); // C6C251 JBE includes unordered.
		bool restart = layer->m_nSequence != m_nLayerSequence || layer->m_nOrder == 15;
		if ( !restart )
		{
			const double duration = static_cast< double >( SequenceDuration( GetModelPtr(), m_nLayerSequence ) );
			const float storedDuration = static_cast< float >( duration );
			const float error = static_cast< float >( fabs( static_cast< double >( now ) -
				( static_cast< double >( layer->m_flLayerAnimtime ) - static_cast< double >( static_cast< float >( layer->m_flCycle ) ) * duration ) ) );
			const float tolerance = duration < 1.0 ? 0.5f : 0.05f; // FCOMIP/JBE includes unordered.
			const float relativeError = error / storedDuration;
			// C6C2FE/303/308 JBE rejects BOTH boundary equality and unordered.
			restart = !( relativeError < 1.0f ) || !( error < 0.5f ) ||
				!( relativeError < tolerance ) || !IsClientCreated();
		}
		if ( restart )
		{
			layer->SetCycle( 0.0f );
			m_flOverlayPrevEventCycle[0] = -0.01f; // Native index0, even layer3.
			layer->SetSequence( m_nLayerSequence );
			layer->m_flPlaybackRate = static_cast< float >( GetEffectivePlaybackRate() );
			layer->SetWeight( 1.0f );
			layer->SetOrder( m_nLayer );
			layer->m_flPrevCycle = 0.0f;
			layer->m_flLayerAnimtime = start;
			m_nOldAnimationParity = m_nAnimationParity;
		}
	}
	BaseClass::OnDataChanged( updateType );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
void C_BaseViewModel::OnDataChanged( DataUpdateType_t updateType )
{
	SetPredictionEligible( true );
	BaseClass::OnDataChanged(updateType);
}

#endif

void C_BaseViewModel::PostDataUpdate( DataUpdateType_t updateType )
{
	BaseClass::PostDataUpdate(updateType);
	OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );
}


//-----------------------------------------------------------------------------
// Purpose: Add entity to visible view models list
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void C_BaseViewModel::AddEntity( void )
{
	// Server says don't interpolate this frame, so set previous info to new info.
	if ( IsNoInterpolationFrame() )
	{
		ResetLatched();
	}
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseViewModel::GetBoneControllers(float controllers[MAXSTUDIOBONECTRLS])
{
	BaseClass::GetBoneControllers( controllers );

	// Tell the weapon itself that we've rendered, in case it wants to do something
	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		pWeapon->GetViewmodelBoneControllers( this, controllers );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : RenderGroup_t
//-----------------------------------------------------------------------------
#if defined( L4D2 )
RenderGroup_t C_BaseViewModel::GetRenderGroup()
{
	return BaseClass::GetRenderGroup();
}
#else
RenderGroup_t C_BaseViewModel::GetRenderGroup()
{
	return RENDER_GROUP_VIEW_MODEL_OPAQUE;
}
#endif
#if defined( L4D2 )
// C6AC90. Root C++ parent already CBaseAnimatingOverlay under L4D2; the wire
// table remains NOBASE. Do not add an unrelated parity, owner or layer reset.
CStudioHdr *C_BaseViewModel::OnNewModel()
{
	CStudioHdr *model = BaseClass::OnNewModel();
	m_flPreviousElapsedTime = 0.0f;
	m_flMainSequenceCycle = 0.0f;
	m_iCameraAttachment = LookupAttachment( "attach_camera" );
	return model;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// Whole C6B8D0, native primary228/+912 in Base/Predicted/Terror VM. Actual caller
// is player CalcPlayerView20750's TWO-model loop after205D0, before roll/punch.
// origin/fov deliberately untouched; no fake camera field or unused accumulator.
void C_BaseViewModel::CalcViewModelCamera( Vector &origin, QAngle &angles, float &fov )
{
	if ( vm_camera.GetInt() != 0 && m_iCameraAttachment > 0 )
	{
		if ( vm_camera_no_roll.GetInt() != 0 )
			m_angViewModelCamera.z = 0.0f;
		const float scale = vm_camera_scale.GetFloat();
		const float yaw = ( m_angViewModelCamera.y * scale ) + angles.y;
		const float roll = ( m_angViewModelCamera.z * scale ) + angles.z;
		angles.x = angles.x + ( m_angViewModelCamera.x * scale );
		angles.y = yaw;
		angles.z = roll;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// Native6AC40 in all three viewmodel renderable families. Genuine virtual byte
// identity, unrelated to private3060; do NOT invent a ctor-enable/flag writer.
unsigned char C_BaseViewModel::OverrideAlphaModulation( unsigned char alpha )
{
	return alpha;
}

// Whole native23340, shared BaseVM/PredictedVM primary229. This zero return is
// the actual pinned native default (5bytes/2ASM), NOT an invented provider.
int C_BaseViewModel::GetMuzzleFlashAttachment( bool, bool )
{
	return 0;
}
#endif

#if defined( L4D2 )
int C_BaseViewModel::ComputeModelTranslucencyType()
{
	return BaseClass::ComputeModelTranslucencyType();
}
#endif
