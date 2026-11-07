//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "baseviewmodel_shared.h"
#include "datacache/imdlcache.h"
#if defined( CLIENT_DLL ) && defined( L4D2 )
#include "animationlayer.h"
#include "animation.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/terror_shareddefs.h"
#endif
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/terror_shareddefs.h"
#endif

#if defined( CLIENT_DLL )
#include "iprediction.h"
#include "prediction.h"
#include "client_virtualreality.h"
#include "sourcevr/isourcevirtualreality.h"
#else
#include "vguiscreen.h"
#endif

#if defined( CLIENT_DLL ) && defined( SIXENSE )
#include "sixense/in_sixense.h"
#include "sixense/sixense_convars_extern.h"
#endif

#ifdef SIXENSE
extern ConVar in_forceuser;
#include "iclientmode.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define VIEWMODEL_ANIMATION_PARITY_BITS 3
#define SCREEN_OVERLAY_MATERIAL "vgui/screens/vgui_overlay"
#if defined( CLIENT_DLL ) && defined( L4D2 )
static ConVar viewmodel_offset_x( "viewmodel_offset_x", "0.0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00000088
#else
	FCVAR_ARCHIVE
#endif
 );
static ConVar viewmodel_offset_y( "viewmodel_offset_y", "0.0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00000088
#else
	FCVAR_ARCHIVE
#endif
 );
static ConVar viewmodel_offset_z( "viewmodel_offset_z", "0.0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x00000088
#else
	FCVAR_ARCHIVE
#endif
 );
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBaseViewModel::CBaseViewModel()
#if defined( L4D2 ) && defined( CLIENT_DLL )
	: m_nClientViewModelState( -1 )
#endif
{
#if defined( CLIENT_DLL )
#if defined( L4D2 )
	m_EntClientFlags |= ENTCLIENTFLAG_ALWAYS_INTERPOLATE;
	m_nOldAnimationParity = 0;
	SetRenderWithViewModels( true ); // Native507, Move's SOLE actual root owner.
	m_iCameraAttachment = -1;
#else
	m_nOldAnimationParity = 0;
	m_EntClientFlags |= ENTCLIENTFLAG_ALWAYS_INTERPOLATE;
#endif
#endif
	SetRenderColor( 255, 255, 255, 255 );
	m_sVMName = NULL_STRING;
	m_sAnimationPrefix = NULL_STRING;
	m_nViewModelIndex = 0;
	m_nAnimationParity = 0;
#if defined( L4D2 ) && !defined( CLIENT_DLL )
	m_nLayerSequence = 0;
	m_nLayer = 0;
	m_flLayerStartTime = 0.0f;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBaseViewModel::~CBaseViewModel()
{
}

void CBaseViewModel::UpdateOnRemove( void )
{
	BaseClass::UpdateOnRemove();

	DestroyControlPanels();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseViewModel::Precache( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseViewModel::Spawn( void )
{
	Precache( );
	SetSize( Vector( -8, -4, -2), Vector(8, 4, 2) );
	SetSolid( SOLID_NONE );
}


#if defined ( CSTRIKE_DLL ) && !defined ( CLIENT_DLL )
#define VGUI_CONTROL_PANELS
#endif

#if defined ( TF_DLL )
#define VGUI_CONTROL_PANELS
#endif

#ifdef INVASION_DLL
#define VGUI_CONTROL_PANELS
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseViewModel::SetControlPanelsActive( bool bState )
{
#if defined( VGUI_CONTROL_PANELS )
	// Activate control panel screens
	for ( int i = m_hScreens.Count(); --i >= 0; )
	{
		if (m_hScreens[i].Get())
		{
			m_hScreens[i]->SetActive( bState );
		}
	}
#endif
}

//-----------------------------------------------------------------------------
// This is called by the base object when it's time to spawn the control panels
//-----------------------------------------------------------------------------
void CBaseViewModel::SpawnControlPanels()
{
#if defined( VGUI_CONTROL_PANELS )
	char buf[64];

	// Destroy existing panels
	DestroyControlPanels();

	CBaseCombatWeapon *weapon = m_hWeapon.Get();

	if ( weapon == NULL )
	{
		return;
	}

	MDLCACHE_CRITICAL_SECTION();

	// FIXME: Deal with dynamically resizing control panels?

	// If we're attached to an entity, spawn control panels on it instead of use
	CBaseAnimating *pEntityToSpawnOn = this;
	char *pOrgLL = "controlpanel%d_ll";
	char *pOrgUR = "controlpanel%d_ur";
	char *pAttachmentNameLL = pOrgLL;
	char *pAttachmentNameUR = pOrgUR;
	/*
	if ( IsBuiltOnAttachment() )
	{
		pEntityToSpawnOn = dynamic_cast<CBaseAnimating*>((CBaseEntity*)m_hBuiltOnEntity.Get());
		if ( pEntityToSpawnOn )
		{
			char sBuildPointLL[64];
			char sBuildPointUR[64];
			Q_snprintf( sBuildPointLL, sizeof( sBuildPointLL ), "bp%d_controlpanel%%d_ll", m_iBuiltOnPoint );
			Q_snprintf( sBuildPointUR, sizeof( sBuildPointUR ), "bp%d_controlpanel%%d_ur", m_iBuiltOnPoint );
			pAttachmentNameLL = sBuildPointLL;
			pAttachmentNameUR = sBuildPointUR;
		}
		else
		{
			pEntityToSpawnOn = this;
		}
	}
	*/

	Assert( pEntityToSpawnOn );

	// Lookup the attachment point...
	int nPanel;
	for ( nPanel = 0; true; ++nPanel )
	{
		Q_snprintf( buf, sizeof( buf ), pAttachmentNameLL, nPanel );
		int nLLAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
		if (nLLAttachmentIndex <= 0)
		{
			// Try and use my panels then
			pEntityToSpawnOn = this;
			Q_snprintf( buf, sizeof( buf ), pOrgLL, nPanel );
			nLLAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
			if (nLLAttachmentIndex <= 0)
				return;
		}

		Q_snprintf( buf, sizeof( buf ), pAttachmentNameUR, nPanel );
		int nURAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
		if (nURAttachmentIndex <= 0)
		{
			// Try and use my panels then
			Q_snprintf( buf, sizeof( buf ), pOrgUR, nPanel );
			nURAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
			if (nURAttachmentIndex <= 0)
				return;
		}

		const char *pScreenName;
		weapon->GetControlPanelInfo( nPanel, pScreenName );
		if (!pScreenName)
			continue;

		const char *pScreenClassname;
		weapon->GetControlPanelClassName( nPanel, pScreenClassname );
		if ( !pScreenClassname )
			continue;

		// Compute the screen size from the attachment points...
		matrix3x4_t	panelToWorld;
		pEntityToSpawnOn->GetAttachment( nLLAttachmentIndex, panelToWorld );

		matrix3x4_t	worldToPanel;
		MatrixInvert( panelToWorld, worldToPanel );

		// Now get the lower right position + transform into panel space
		Vector lr, lrlocal;
		pEntityToSpawnOn->GetAttachment( nURAttachmentIndex, panelToWorld );
		MatrixGetColumn( panelToWorld, 3, lr );
		VectorTransform( lr, worldToPanel, lrlocal );

		float flWidth = lrlocal.x;
		float flHeight = lrlocal.y;

		CVGuiScreen *pScreen = CreateVGuiScreen( pScreenClassname, pScreenName, pEntityToSpawnOn, this, nLLAttachmentIndex );
		pScreen->ChangeTeam( GetTeamNumber() );
		pScreen->SetActualSize( flWidth, flHeight );
		pScreen->SetActive( false );
		pScreen->MakeVisibleOnlyToTeammates( false );
	
#ifdef INVASION_DLL
		pScreen->SetOverlayMaterial( SCREEN_OVERLAY_MATERIAL );
#endif
		pScreen->SetAttachedToViewModel( true );
		int nScreen = m_hScreens.AddToTail( );
		m_hScreens[nScreen].Set( pScreen );
	}
#endif
}

void CBaseViewModel::DestroyControlPanels()
{
#if defined( VGUI_CONTROL_PANELS )
	// Kill the control panels
	int i;
	for ( i = m_hScreens.Count(); --i >= 0; )
	{
		DestroyVGuiScreen( m_hScreens[i].Get() );
	}
	m_hScreens.RemoveAll();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity - 
//-----------------------------------------------------------------------------
void CBaseViewModel::SetOwner( CBaseEntity *pEntity )
{
	m_hOwner = pEntity;
#if !defined( CLIENT_DLL )
	// Make sure we're linked into hierarchy
	//SetParent( pEntity );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : nIndex - 
//-----------------------------------------------------------------------------
void CBaseViewModel::SetIndex( int nIndex )
{
	m_nViewModelIndex = nIndex;
	Assert( m_nViewModelIndex < (1 << VIEWMODEL_INDEX_BITS) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseViewModel::ViewModelIndex( ) const
{
	return m_nViewModelIndex;
}

//-----------------------------------------------------------------------------
// Purpose: Pass our visibility on to our child screens
//-----------------------------------------------------------------------------
void CBaseViewModel::AddEffects( int nEffects )
{
	if ( nEffects & EF_NODRAW )
	{
		SetControlPanelsActive( false );
	}

	BaseClass::AddEffects( nEffects );
}

//-----------------------------------------------------------------------------
// Purpose: Pass our visibility on to our child screens
//-----------------------------------------------------------------------------
void CBaseViewModel::RemoveEffects( int nEffects )
{
	if ( nEffects & EF_NODRAW )
	{
		SetControlPanelsActive( true );
	}

	BaseClass::RemoveEffects( nEffects );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *modelname - 
//-----------------------------------------------------------------------------
#if defined( CLIENT_DLL ) && defined( L4D2 )
void CBaseViewModel::SetWeaponModel( const char *modelName, CBaseCombatWeapon *weapon )
{
	m_nSkin = 0; // Native DIRECT1636store; NOT changed-only SetSkin+42570 refresh.
	if ( m_hWeapon.Get() != weapon )
		m_hWeapon = weapon;
	// Resolved-NULL equality deliberately preserves an already-invalid stale
	// raw handle; do NOT change this to an unconditional assignment/extra reset.
	SetModel( modelName );
	MDLCACHE_CRITICAL_SECTION();
	CStudioHdr *model = GetModelPtr();
	if ( model && model->IsValid() )
	{
		Activity idle = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_IDLE );
		if ( weapon )
			idle = static_cast< CTerrorWeapon * >( weapon )->TranslateViewModelActivity( idle );
		// Native0F2E0 is HEAVIEST, not31890 weighted/RNG selection. No Send/Apply,
		// parity/event cursor/layer/main-cycle/time mutation is added here.
		SetSequence( ::SelectHeaviestSequence( model, idle ) );
	}
}
#elif defined( L4D2 )
// S7F8C0. The server owns the received main sequence; layer sends do not
// initialize it. Select the model's translated idle only on a model change.
void CBaseViewModel::SetWeaponModel( const char *modelName, CBaseCombatWeapon *weapon )
{
	m_nSkin = 0;
	if ( m_hWeapon.Get() != weapon )
		m_hWeapon = weapon;

	const string_t model = modelName && modelName[0] ? MAKE_STRING( modelName ) : NULL_STRING;
	if ( model != m_sVMName )
	{
		m_sVMName = model;
		SetModel( STRING( m_sVMName ) );
		SpawnControlPanels();
		const bool showControlPanels = weapon && weapon->ShouldShowControlPanels();
		SetControlPanelsActive( showControlPanels );

		MDLCACHE_CRITICAL_SECTION();
		CStudioHdr *studio = GetModelPtr();
		if ( studio && studio->IsValid() )
		{
			Activity idle = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_IDLE );
			if ( weapon )
				idle = static_cast< CTerrorWeapon * >( weapon )->TranslateViewModelActivity( idle );
			SetSequence( SelectHeaviestSequence( idle ) );
		}
	}
}
#else
void CBaseViewModel::SetWeaponModel( const char *modelname, CBaseCombatWeapon *weapon )
{
	m_hWeapon = weapon;

#if defined( CLIENT_DLL )
	SetModel( modelname );
#else
	string_t str;
	if ( modelname != NULL )
	{
		str = MAKE_STRING( modelname );
	}
	else
	{
		str = NULL_STRING;
	}

	if ( str != m_sVMName )
	{
		// Msg( "SetWeaponModel %s at %f\n", modelname, gpGlobals->curtime );
		m_sVMName = str;
		SetModel( STRING( m_sVMName ) );

		// Create any vgui control panels associated with the weapon
		SpawnControlPanels();

		bool showControlPanels = weapon && weapon->ShouldShowControlPanels();
		SetControlPanelsActive( showControlPanels );
	}
#endif
}

#endif

//-----------------------------------------------------------------------------
// Purpose: 
// Output : CBaseCombatWeapon
//-----------------------------------------------------------------------------
CBaseCombatWeapon *CBaseViewModel::GetOwningWeapon( void )
{
	return m_hWeapon.Get();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : sequence - 
//-----------------------------------------------------------------------------
void CBaseViewModel::SendViewModelMatchingSequence( int sequence )
{
	#if defined( L4D2 )
	SendTerrorLayerSequence( sequence );
	#else
	// since all we do is send a sequence number down to the client, 
	// set this here so other weapons code knows which sequence is playing.
	SetSequence( sequence );

	m_nAnimationParity = ( m_nAnimationParity + 1 ) & ( (1<<VIEWMODEL_ANIMATION_PARITY_BITS) - 1 );

#if defined( CLIENT_DLL )
	m_nOldAnimationParity = m_nAnimationParity;

	// Force frame interpolation to start at exactly frame zero
	m_flAnimTime			= gpGlobals->curtime;
#else
	CBaseCombatWeapon *weapon = m_hWeapon.Get();
	bool showControlPanels = weapon && weapon->ShouldShowControlPanels();
	SetControlPanelsActive( showControlPanels );
#endif

	// Restart animation at frame 0
	SetCycle( 0 );
	ResetSequenceInfo();
	#endif
}

#if defined( CLIENT_DLL )
#include "ivieweffects.h"
#endif

#if defined( CLIENT_DLL ) && defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#pragma fp_contract( off )
#endif
// C22880, actual BASE/PREDICTED virtual222 (Terror replaces with288F40).
// Native100A84B0 is a proven3-byte purefalse on the pinned PC module: its dead
// console autoaim branch is folded, NOT implemented as a guessed runtime gate.
void CBaseViewModel::CalcViewModelView( CBasePlayer *, const Vector &eyePosition,
	const QAngle &eyeAngles )
{
	QAngle angles = eyeAngles;
	Vector origin = eyePosition;
	Vector forward, right, up;
	AngleVectors( eyeAngles, &forward, &right, &up );
	const float x = viewmodel_offset_x.GetFloat();
	const float y = viewmodel_offset_y.GetFloat();
	const float z = viewmodel_offset_z.GetFloat();
	const float rightX = right.x * x;
	const float rightY = right.y * x;
	const float rightZ = right.z * x;
	origin.x = ( ( forward.x * y + up.x * z ) + rightX ) + origin.x;
	origin.y = origin.y + ( ( forward.y * y + up.y * z ) + rightY );
	origin.z = origin.z + ( ( forward.z * y + up.z * z ) + rightZ );
	if ( !prediction->InPrediction() )
	{
		// Active split-user scope omitted ONLY under Main's real user0 projection.
		vieweffects->ApplyShake( origin, angles, 0.1f );
	}
	SetLocalOrigin( origin );
	SetLocalAngles( angles );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
void CBaseViewModel::CalcViewModelView( CBasePlayer *owner, const Vector& eyePosition, const QAngle& eyeAngles )
{
	// UNDONE: Calc this on the server?  Disabled for now as it seems unnecessary to have this info on the server
#if defined( CLIENT_DLL )
	QAngle vmangoriginal = eyeAngles;
	QAngle vmangles = eyeAngles;
	Vector vmorigin = eyePosition;

	CBaseCombatWeapon *pWeapon = m_hWeapon.Get();
	//Allow weapon lagging
	if ( pWeapon != NULL )
	{
#if defined( CLIENT_DLL )
		if ( !prediction->InPrediction() )
#endif
		{
			// add weapon-specific bob 
			pWeapon->AddViewmodelBob( this, vmorigin, vmangles );

			CalcViewModelLag( vmorigin, vmangles, vmangoriginal );
		}
	}
	// Add model-specific bob even if no weapon associated (for head bob for off hand models)
	AddViewModelBob( owner, vmorigin, vmangles );

#if defined( CLIENT_DLL )
	if ( !prediction->InPrediction() )
	{
		// Let the viewmodel shake at about 10% of the amplitude of the player's view
		vieweffects->ApplyShake( vmorigin, vmangles, 0.1 );	
	}
#endif

#if !defined( L4D2 )
	if( UseVR() )
	{
		g_ClientVirtualReality.OverrideViewModelTransform( vmorigin, vmangles, pWeapon && pWeapon->ShouldUseLargeViewModelVROverride() );
	}
#endif

	SetLocalOrigin( vmorigin );
	SetLocalAngles( vmangles );

#ifdef SIXENSE
	if( g_pSixenseInput->IsEnabled() && (owner->GetObserverMode()==OBS_MODE_NONE) && !UseVR() )
	{
		const float max_gun_pitch = 20.0f;

		float viewmodel_fov_ratio = g_pClientMode->GetViewModelFOV()/owner->GetFOV();
		QAngle gun_angles = g_pSixenseInput->GetViewAngleOffset() * -viewmodel_fov_ratio;

		// Clamp pitch a bit to minimize seeing back of viewmodel
		if( gun_angles[PITCH] < -max_gun_pitch )
		{ 
			gun_angles[PITCH] = -max_gun_pitch; 
		}

#ifdef WIN32 // ShouldFlipViewModel comes up unresolved on osx? Mabye because it's defined inline? fixme
		if( ShouldFlipViewModel() ) 
		{
			gun_angles[YAW] *= -1.0f;
		}
#endif

		vmangles = EyeAngles() +  gun_angles;

		SetLocalAngles( vmangles );
	}
#endif
#endif

}

#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float g_fMaxViewModelLag = 1.5f;

ConVar sv_viewmodel_lag_do_angles( "sv_viewmodel_lag_do_angles", "1", FCVAR_CHEAT | FCVAR_REPLICATED );

#if defined( CLIENT_DLL ) && defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#pragma fp_contract( off )
#endif
// C223E0, actual BASE virtual221. Predicted's genuine17A4F0 remains RET12;
// Terror287DA0 tail-calls that Predicted body, not this drift implementation.
void CBaseViewModel::CalcViewModelLag( Vector &origin, QAngle &angles, QAngle &original )
{
	const Vector originalOrigin = origin;
	const QAngle originalAngles = angles;
	Vector forward;
	AngleVectors( angles, &forward );
	const float frameTime = gpGlobals->frametime;
	if ( frameTime != 0.0f )
	{
		const Vector difference = forward - m_vecLastFacing;
		float speed = 5.0f;
		const float distance = sqrtf( ( difference.y * difference.y +
			difference.z * difference.z ) + difference.x * difference.x );
		if ( distance > g_fMaxViewModelLag && g_fMaxViewModelLag > 0.0f )
			speed = ( distance / g_fMaxViewModelLag ) * 5.0f;
		const float step = frameTime * speed;
		m_vecLastFacing.x = step * difference.x + m_vecLastFacing.x;
		m_vecLastFacing.y = difference.y * step + m_vecLastFacing.y;
		m_vecLastFacing.z = difference.z * step + m_vecLastFacing.z;
		VectorNormalize( m_vecLastFacing );
		const float nextY = ( difference.y * -1.0f ) * 5.0f + origin.y;
		const float nextZ = ( difference.z * -1.0f ) * 5.0f + origin.z;
		origin.x = ( difference.x * -1.0f ) * 5.0f + origin.x;
		origin.y = nextY;
		origin.z = nextZ;
	}
	Vector right, up;
	AngleVectors( original, &forward, &right, &up );
	float pitch = original.x;
	if ( pitch > 180.0f )
		pitch -= 360.0f;
	else if ( pitch < -180.0f )
		pitch += 360.0f;
	if ( g_fMaxViewModelLag == 0.0f )
	{
		origin = originalOrigin;
		angles = originalAngles;
	}
	// Native ALWAYS applies the three pitch-origin terms, even after zero-max
	// restore. No new sv_viewmodel_lag_do_angles policy/extra angle modification.
	const float forwardScale = pitch * -0.035f;
	const float initialX = forwardScale * forward.x + origin.x;
	const float initialY = forwardScale * forward.y + origin.y;
	const float initialZ = forward.z * forwardScale + origin.z;
	const float rightScale = pitch * -0.03f;
	const float upScale = pitch * -0.02f;
	const float nextX = ( right.x * rightScale + initialX ) + up.x * upScale;
	const float nextY = ( right.y * rightScale + initialY ) + up.y * upScale;
	const float nextZ = ( right.z * rightScale + initialZ ) + up.z * upScale;
	origin.y = nextY;
	origin.x = nextX;
	origin.z = nextZ;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
void CBaseViewModel::CalcViewModelLag( Vector& origin, QAngle& angles, QAngle& original_angles )
{
	Vector vOriginalOrigin = origin;
	QAngle vOriginalAngles = angles;

	// Calculate our drift
	Vector	forward;
	AngleVectors( angles, &forward, NULL, NULL );

	if ( gpGlobals->frametime != 0.0f )
	{
		Vector vDifference;
		VectorSubtract( forward, m_vecLastFacing, vDifference );

		float flSpeed = 5.0f;

		// If we start to lag too far behind, we'll increase the "catch up" speed.  Solves the problem with fast cl_yawspeed, m_yaw or joysticks
		//  rotating quickly.  The old code would slam lastfacing with origin causing the viewmodel to pop to a new position
		float flDiff = vDifference.Length();
		if ( (flDiff > g_fMaxViewModelLag) && (g_fMaxViewModelLag > 0.0f) )
		{
			float flScale = flDiff / g_fMaxViewModelLag;
			flSpeed *= flScale;
		}

		// FIXME:  Needs to be predictable?
		VectorMA( m_vecLastFacing, flSpeed * gpGlobals->frametime, vDifference, m_vecLastFacing );
		// Make sure it doesn't grow out of control!!!
		VectorNormalize( m_vecLastFacing );
		VectorMA( origin, 5.0f, vDifference * -1.0f, origin );

		Assert( m_vecLastFacing.IsValid() );
	}

	if ( sv_viewmodel_lag_do_angles.GetBool() )
	{
		Vector right, up;
		AngleVectors( original_angles, &forward, &right, &up );

		float pitch = original_angles[ PITCH ];
		if ( pitch > 180.0f )
			pitch -= 360.0f;
		else if ( pitch < -180.0f )
			pitch += 360.0f;

		if ( g_fMaxViewModelLag == 0.0f )
		{
			origin = vOriginalOrigin;
			angles = vOriginalAngles;
		}

		//FIXME: These are the old settings that caused too many exposed polys on some models
		VectorMA( origin, -pitch * 0.035f,	forward,	origin );
		VectorMA( origin, -pitch * 0.03f,		right,	origin );
		VectorMA( origin, -pitch * 0.02f,		up,		origin);
	}
}

#endif

//-----------------------------------------------------------------------------
// Stub to keep networking consistent for DEM files
//-----------------------------------------------------------------------------
#if defined( CLIENT_DLL )
  extern void RecvProxy_EffectFlags( const CRecvProxyData *pData, void *pStruct, void *pOut );
  void RecvProxy_SequenceNum( const CRecvProxyData *pData, void *pStruct, void *pOut );
#endif

//-----------------------------------------------------------------------------
// Purpose: Resets anim cycle when the server changes the weapon on us
//-----------------------------------------------------------------------------
#if defined( CLIENT_DLL )
#if defined( L4D2 )
// Whole C226D0. The original VM proxy masks EF_NODRAW0x20 before SetEffects;
// ordinary Root RecvProxy_EffectFlags remains unchanged for other entities/games.
static void RecvProxy_ViewModelEffects( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	static_cast< CBaseViewModel * >( pStruct )->SetEffects( pData->m_Value.m_Int & ~EF_NODRAW );
}

// Whole C226F0. Generic real handle decoding FIRST; preserve layer0. This does
// NOT compare old/new weapons, reset main cycle or write m_flAnimTime.
static void RecvProxy_Weapon( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	RecvProxy_IntToEHandle( pData, pStruct, pOut );
	CBaseViewModel *viewModel = static_cast< CBaseViewModel * >( pStruct );
	if ( viewModel->GetNumAnimOverlays() == 4 )
	{
		for ( int i = 1; i < 4; ++i )
		{
			viewModel->GetAnimOverlay( i )->SetOrder( 15 );
			viewModel->GetAnimOverlay( i )->SetSequence( 0 );
		}
	}
}
#else
static void RecvProxy_Weapon( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	CBaseViewModel *pViewModel = ((CBaseViewModel*)pStruct);
	CBaseCombatWeapon *pOldWeapon = pViewModel->GetOwningWeapon();

	// Chain through to the default recieve proxy ...
	RecvProxy_IntToEHandle( pData, pStruct, pOut );

	// ... and reset our cycle index if the server is switching weapons on us
	CBaseCombatWeapon *pNewWeapon = pViewModel->GetOwningWeapon();
	if ( pNewWeapon != pOldWeapon )
	{
		// Restart animation at frame 0
		pViewModel->SetCycle( 0 );
		pViewModel->m_flAnimTime = gpGlobals->curtime;
	}
}

#endif
#endif // CLIENT_DLL


LINK_ENTITY_TO_CLASS( viewmodel, CBaseViewModel );

IMPLEMENT_NETWORKCLASS_ALIASED( BaseViewModel, DT_BaseViewModel )

BEGIN_NETWORK_TABLE_NOBASE(CBaseViewModel, DT_BaseViewModel)
#if !defined( CLIENT_DLL )
#if defined( L4D2 )
	SendPropInt( SENDINFO( m_nModelIndex ), 12, 0 ),
#else
	SendPropModelIndex(SENDINFO(m_nModelIndex)),
#endif
	SendPropInt		(SENDINFO(m_nBody), 8),
	SendPropInt		(SENDINFO(m_nSkin), 10),
	SendPropInt		(SENDINFO(m_nSequence),	8, SPROP_UNSIGNED),
#if defined( L4D2 )
	SendPropInt( SENDINFO( m_nLayerSequence ), 8, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_nLayer ), 2, SPROP_UNSIGNED ),
#endif
	SendPropInt		(SENDINFO(m_nViewModelIndex), VIEWMODEL_INDEX_BITS, SPROP_UNSIGNED),
	SendPropFloat	(SENDINFO(m_flPlaybackRate),	8,	SPROP_ROUNDUP,	-4.0,	12.0f),
	SendPropInt		(SENDINFO(m_fEffects),		10, SPROP_UNSIGNED),
	SendPropInt		(SENDINFO(m_nAnimationParity), 3, SPROP_UNSIGNED ),
	SendPropEHandle (SENDINFO(m_hWeapon)),
	SendPropEHandle (SENDINFO(m_hOwner)),

	SendPropInt( SENDINFO( m_nNewSequenceParity ), EF_PARITY_BITS, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_nResetEventsParity ), EF_PARITY_BITS, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_nMuzzleFlashParity ), EF_MUZZLEFLASH_BITS, SPROP_UNSIGNED ),
#if defined( L4D2 )
	SendPropFloat( SENDINFO( m_flLayerStartTime ), 0, SPROP_NOSCALE ),
#endif

#if !defined( INVASION_DLL ) && !defined( INVASION_CLIENT_DLL ) && !defined( L4D2 )
	SendPropArray	(SendPropFloat(SENDINFO_ARRAY(m_flPoseParameter),	8, 0, 0.0f, 1.0f), m_flPoseParameter),
#endif
#else
	RecvPropInt		(RECVINFO(m_nModelIndex)),
	RecvPropInt		(RECVINFO(m_nSkin)),
	RecvPropInt		(RECVINFO(m_nBody)),
	RecvPropInt		(RECVINFO(m_nSequence), 0, RecvProxy_SequenceNum ),
#if defined( L4D2 )
	RecvPropInt( RECVINFO( m_nLayerSequence ) ),
	RecvPropInt( RECVINFO( m_nLayer ) ),
#endif
	RecvPropInt		(RECVINFO(m_nViewModelIndex)),
	RecvPropFloat	(RECVINFO(m_flPlaybackRate)),
#if defined( L4D2 )
	RecvPropInt( RECVINFO( m_fEffects ), 0, RecvProxy_ViewModelEffects ),
#else
	RecvPropInt( RECVINFO(m_fEffects), 0, RecvProxy_EffectFlags ),
#endif
	RecvPropInt		(RECVINFO(m_nAnimationParity)),
	RecvPropEHandle (RECVINFO(m_hWeapon), RecvProxy_Weapon ),
	RecvPropEHandle (RECVINFO(m_hOwner)),

	RecvPropInt( RECVINFO( m_nNewSequenceParity )),
	RecvPropInt( RECVINFO( m_nResetEventsParity )),
	RecvPropInt( RECVINFO( m_nMuzzleFlashParity )),
#if defined( L4D2 )
	RecvPropFloat( RECVINFO( m_flLayerStartTime ) ),
#endif

#if !defined( INVASION_DLL ) && !defined( INVASION_CLIENT_DLL ) && !defined( L4D2 )
	RecvPropArray(RecvPropFloat(RECVINFO(m_flPoseParameter[0]) ), m_flPoseParameter ),
#endif
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL

BEGIN_PREDICTION_DATA( CBaseViewModel )

	// Networked
	#if defined( L4D2 )
	DEFINE_PRED_FIELD( m_nModelIndex, FIELD_SHORT, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX | FTYPEDESC_OVERRIDE ),
	DEFINE_PRED_FIELD( m_nSkin, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_OVERRIDE ),
	DEFINE_PRED_FIELD( m_nBody, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_OVERRIDE ),
	#else
	DEFINE_PRED_FIELD( m_nModelIndex, FIELD_SHORT, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX ),
	DEFINE_PRED_FIELD( m_nSkin, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_nBody, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	#endif
	DEFINE_PRED_FIELD( m_nSequence, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_nViewModelIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	#if defined( L4D2 )
	DEFINE_PRED_FIELD_TOL( m_flPlaybackRate, FIELD_FLOAT, FTYPEDESC_INSENDTABLE | FTYPEDESC_OVERRIDE, 0.125f ),
	#else
	DEFINE_PRED_FIELD_TOL( m_flPlaybackRate, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.125f ),
	#endif
	DEFINE_PRED_FIELD( m_fEffects, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_OVERRIDE ),
	DEFINE_PRED_FIELD( m_nAnimationParity, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_hWeapon, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flAnimTime, FIELD_FLOAT, 0 ),
#if defined( L4D2 )
	DEFINE_PRED_FIELD( m_nLayerSequence, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_nLayer, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flLayerStartTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
#endif

	DEFINE_FIELD( m_hOwner, FIELD_EHANDLE ),
	DEFINE_FIELD( m_flTimeWeaponIdle, FIELD_FLOAT ),
	DEFINE_FIELD( m_Activity, FIELD_INTEGER ),
	DEFINE_PRED_FIELD( m_flCycle, FIELD_FLOAT, FTYPEDESC_PRIVATE | FTYPEDESC_OVERRIDE | FTYPEDESC_NOERRORCHECK ),

END_PREDICTION_DATA()

void RecvProxy_SequenceNum( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	CBaseViewModel *model = (CBaseViewModel *)pStruct;
	if (pData->m_Value.m_Int != model->GetSequence())
	{
		MDLCACHE_CRITICAL_SECTION();

		model->SetSequence(pData->m_Value.m_Int);
		model->m_flAnimTime = gpGlobals->curtime;
		model->SetCycle(0);
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int	CBaseViewModel::LookupAttachment( const char *pAttachmentName )
{
	if ( m_hWeapon.Get() && m_hWeapon.Get()->WantsToOverrideViewmodelAttachments() )
		return m_hWeapon.Get()->LookupAttachment( pAttachmentName );

	return BaseClass::LookupAttachment( pAttachmentName );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseViewModel::GetAttachment( int number, matrix3x4_t &matrix )
{
	if ( m_hWeapon.Get() && m_hWeapon.Get()->WantsToOverrideViewmodelAttachments() )
		return m_hWeapon.Get()->GetAttachment( number, matrix );

	return BaseClass::GetAttachment( number, matrix );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseViewModel::GetAttachment( int number, Vector &origin )
{
	if ( m_hWeapon.Get() && m_hWeapon.Get()->WantsToOverrideViewmodelAttachments() )
		return m_hWeapon.Get()->GetAttachment( number, origin );

	return BaseClass::GetAttachment( number, origin );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseViewModel::GetAttachment( int number, Vector &origin, QAngle &angles )
{
	if ( m_hWeapon.Get() && m_hWeapon.Get()->WantsToOverrideViewmodelAttachments() )
		return m_hWeapon.Get()->GetAttachment( number, origin, angles );

	return BaseClass::GetAttachment( number, origin, angles );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseViewModel::GetAttachmentVelocity( int number, Vector &originVel, Quaternion &angleVel )
{
	if ( m_hWeapon.Get() && m_hWeapon.Get()->WantsToOverrideViewmodelAttachments() )
		return m_hWeapon.Get()->GetAttachmentVelocity( number, originVel, angleVel );

	return BaseClass::GetAttachmentVelocity( number, originVel, angleVel );
}

#endif
