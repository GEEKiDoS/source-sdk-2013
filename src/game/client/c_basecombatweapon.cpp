//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client side implementation of CBaseCombatWeapon.
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "history_resource.h"
#include "iclientmode.h"
#include "iinput.h"
#include "weapon_selection.h"
#include "hud_crosshair.h"
#include "engine/ivmodelinfo.h"
#include "tier0/vprof.h"
#include "hltvcamera.h"
#include "tier1/KeyValues.h"
#include "toolframework/itoolframework.h"
#include "toolframework_client.h"

#if defined( L4D2 )
#include "c_baseanimating.h"
#include "bone_setup.h"
#include "datacache/imdlcache.h"
#include "l4d2/terror_shareddefs.h"
#include "cliententitylist.h"
#include <xmmintrin.h>
#endif
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
#if defined( L4D2 )
static ClientWeaponList_t s_ClientWeaponList;
ClientWeaponList_t &GetClientWeaponList()
{
	return s_ClientWeaponList;
}

#endif
#if defined( L4D2 )
extern ConVar cl_viewmodelsclonedasworld;

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

class C_CombatWeaponClone : public C_BaseAnimating
{
public:
	DECLARE_CLASS( C_CombatWeaponClone, C_BaseAnimating );
	explicit C_CombatWeaponClone( C_BaseCombatWeapon *weapon );
	virtual ~C_CombatWeaponClone();
	void UpdateClone();
	virtual bool ShouldDraw() OVERRIDE;
	virtual int DrawModel( int flags ) OVERRIDE;
	virtual bool SetupBones( matrix3x4_t *bones, int maximum, int mask, float time ) OVERRIDE;
	virtual bool GetStencilState( ShaderStencilState_t &state ) OVERRIDE;

private:
	C_BaseCombatWeapon *m_pOriginalWeapon; //2392, genuine raw weapon owner pointer.
	int m_nLastUpdateTime; //2396, INTEGER curtime cache, no native ctor write.
};

C_CombatWeaponClone::C_CombatWeaponClone( C_BaseCombatWeapon *weapon )
{
	m_pOriginalWeapon = weapon;
	InitializeAsClientEntity( weapon->GetWorldModel(), false ); // Native ignores bool.
}

C_CombatWeaponClone::~C_CombatWeaponClone()
{
	m_pOriginalWeapon->OnWeaponCloneDestroyed(); // C40E60 BEFORE base/M7 lifetime.
}

void C_CombatWeaponClone::UpdateClone()
{
	const float now = gpGlobals->curtime;
	if ( static_cast< float >( m_nLastUpdateTime ) == now )
		return;
	// Preserve original bug: integer storage compared to FLOAT curtime, NOT a
	// frame/tick cache. CVTT has the real INT_MIN indefinite for out-of-range.
	m_nLastUpdateTime = _mm_cvttss_si32( _mm_set_ss( now ) );
	const int attachment = m_pOriginalWeapon->GetParentAttachment();
	C_BaseEntity *parent = m_pOriginalWeapon->GetMoveParent();
	SetParent( parent, attachment );
	SetLocalOrigin( m_pOriginalWeapon->GetLocalOrigin() );
	SetLocalAngles( m_pOriginalWeapon->GetLocalAngles() );
	SetCycle( m_pOriginalWeapon->GetCycle() );
	SetEffects( m_pOriginalWeapon->GetEffects() );
	m_flAnimTime = m_pOriginalWeapon->m_flAnimTime; // Real root friend; NO setter/watch reset.
	SetBody( m_pOriginalWeapon->GetBody() ); // Whole changed-only2C130, same42570 owner.
	SetSkin( m_pOriginalWeapon->GetSkin() ); // Reuse existing2C110 owner, no extra refresh.
	m_flPlaybackRate = static_cast< float >( m_pOriginalWeapon->GetEffectivePlaybackRate() );
	const int worldIndex = m_pOriginalWeapon->GetWorldModelIndex();
	if ( GetModelIndex() != worldIndex )
		SetModelIndex( m_pOriginalWeapon->GetWorldModelIndex() ); // Genuine SECOND query.
	CStudioHdr *studio = GetModelPtr();
	if ( studio && m_pOriginalWeapon->GetSequence() >= studio->GetNumSeq() )
		SetSequence( 0 );
	else
		SetSequence( m_pOriginalWeapon->GetSequence() );
	SetBodygroup( 1, m_pOriginalWeapon->GetOwnerEntity() != NULL ? 1 : 0 );
	GlowProp()->SetType( m_pOriginalWeapon->GlowProp()->GetType() );
	GlowProp()->SetRange( m_pOriginalWeapon->GlowProp()->GetRange() );
	// Native109810 raw packed override DOES NOT refresh cached float RGB.
	GlowProp()->SetColorOverride( m_pOriginalWeapon->GlowProp()->GetColorOverride().GetRawColor() );
	SetRenderAlphaModulation( m_pOriginalWeapon->m_nRenderAlphaModulation );
	SetRenderAlphaFadeDistances( m_pOriginalWeapon->m_flRenderAlphaFadeScale,
		static_cast< float >( m_pOriginalWeapon->m_nRenderAlphaFadeMin ),
		static_cast< float >( m_pOriginalWeapon->m_nRenderAlphaFadeMax ) );
}

// C6E500 calls the weapon's PRIMARY visibility policy, not C40CD0's
// renderable wrapper, which necessarily hides the original while this exists.
bool C_CombatWeaponClone::ShouldDraw()
{
	return m_pOriginalWeapon->ShouldDrawWeapon() && m_pOriginalWeapon->GetOwner() &&
		!m_pOriginalWeapon->IsDormant() && C_BaseEntity::ShouldDraw();
}

// Whole C6E8B0, projected onto existing one-arg DrawModel. Incoming per-pass
// blend/color were applied by the caller. Never substitute GetFxBlend()/255.
int C_CombatWeaponClone::DrawModel( int flags )
{
	if ( m_pOriginalWeapon->IsCarriedByObservedPlayer() )
		return 0;
	UpdateClone();
	if ( !ShouldDraw() )
		return 0;
	return BaseClass::DrawModel( flags );
}

bool C_CombatWeaponClone::SetupBones( matrix3x4_t *bones, int maximum, int mask, float time )
{
	UpdateClone();
	return BaseClass::SetupBones( bones, maximum, mask, time );
}

bool C_CombatWeaponClone::GetStencilState( ShaderStencilState_t &state )
{
	return m_pOriginalWeapon->GetStencilState( state );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#endif

//-----------------------------------------------------------------------------
// Purpose: Gets the local client's active weapon, if any.
//-----------------------------------------------------------------------------
C_BaseCombatWeapon *GetActiveWeapon( void )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();

	if ( !player )
		return NULL;

	return player->GetActiveWeapon();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::SetDormant( bool bDormant )
{
	// If I'm going from active to dormant and I'm carried by another player, holster me.
	if ( !IsDormant() && bDormant && GetOwner() && !IsCarriedByLocalPlayer() )
	{
		Holster( NULL );
	}

	BaseClass::SetDormant( bDormant );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::NotifyShouldTransmit( ShouldTransmitState_t state )
{
	BaseClass::NotifyShouldTransmit(state);

	if (state == SHOULDTRANSMIT_END)
	{
		if (m_iState == WEAPON_IS_ACTIVE)
		{
			m_iState = WEAPON_IS_CARRIED_BY_PLAYER;
		}
	}
	else if( state == SHOULDTRANSMIT_START )
	{
		if( m_iState == WEAPON_IS_CARRIED_BY_PLAYER )
		{
			if( GetOwner() && GetOwner()->GetActiveWeapon() == this )
			{
				// Restore the Activeness of the weapon if we client-twiddled it off in the first case above.
				m_iState = WEAPON_IS_ACTIVE;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Select the local viewmodel using the game's local-model policy.
//-----------------------------------------------------------------------------
static inline bool ShouldDrawLocalPlayerViewModel( void )
{
#if defined( L4D2 )
	C_BasePlayer *localPlayer = C_BasePlayer::GetLocalPlayer();
	return localPlayer && !localPlayer->ShouldDrawLocalPlayerModel();
#elif defined( PORTAL )
	return false;
#else
	return !C_BasePlayer::ShouldDrawLocalPlayer();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::OnRestore()
{
	BaseClass::OnRestore();

	// if the player is holding this weapon, 
	// mark it as just restored so it won't show as a new pickup
	if (GetOwner() == C_BasePlayer::GetLocalPlayer())
	{
		m_bJustRestored = true;
	}
}

#if defined( L4D2 )
#if defined( L4D2 )
int C_BaseCombatWeapon::GetWorldModelIndex()
{
	return m_iWorldModelIndex;
}
#else
int C_BaseCombatWeapon::GetWorldModelIndex()
{
	return m_iWorldModelIndex;
}
#endif
#else
int C_BaseCombatWeapon::GetWorldModelIndex( void )
{
	if ( GameRules() )
	{
		const char *pBaseName = modelinfo->GetModelName( modelinfo->GetModel( m_iWorldModelIndex ) );
		const char *pTranslatedName = GameRules()->TranslateEffectForVisionFilter( "weapons", pBaseName );

		if ( pTranslatedName != pBaseName )
		{
			return modelinfo->GetModelIndex( pTranslatedName );
		}
	}

	return m_iWorldModelIndex;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : bnewentity - 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_BaseCombatWeapon::OnDataChanged( DataUpdateType_t updateType )
{
	UpdateVisibility();
	BaseClass::OnDataChanged( updateType );
	C_BasePlayer *owner = ToBasePlayer( GetOwnerEntity() );
	if ( owner && owner->IsLocalPlayer() )
	{
		if ( m_iState != WEAPON_NOT_CARRIED && m_iOldState == WEAPON_NOT_CARRIED && ShouldDrawPickup() )
		{
			CBaseHudWeaponSelection *selection = GetHudWeaponSelection();
			if ( selection )
				selection->OnWeaponPickup( this );
			owner->EmitSound( "Player.PickupWeapon" );
		}
	}
	m_iOldState = m_iState;
	m_bJustRestored = false;
	bool removeClone = !cl_viewmodelsclonedasworld.GetBool();
	if ( !removeClone )
	{
		const int worldIndex = GetWorldModelIndex();
		removeClone = GetModelIndex() == worldIndex;
		if ( !removeClone )
			removeClone = IsDormant();
	}
	if ( removeClone )
	{
		if ( m_pWeaponClone )
		{
			ClientEntityList().AddToDeleteList( m_pWeaponClone );
			m_pWeaponClone = NULL;
		}
	}
	else if ( !m_pWeaponClone )
	{
		m_pWeaponClone = new C_CombatWeaponClone( this ); // Plain SDK allocation.
	}
	if ( m_pWeaponClone )
	{
		m_pWeaponClone->UpdateClone();
		m_pWeaponClone->UpdateVisibility();
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
void C_BaseCombatWeapon::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged(updateType);

	CHandle< C_BaseCombatWeapon > handle = this;

	// If it's being carried by the *local* player, on the first update,
	// find the registered weapon for this ID

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	C_BaseCombatCharacter *pOwner = GetOwner();

	// check if weapon is carried by local player
	bool bIsLocalPlayer = pPlayer && pPlayer == pOwner;
	if ( bIsLocalPlayer && ShouldDrawLocalPlayerViewModel() )		// TODO: figure out the purpose of the ShouldDrawLocalPlayer() test.
	{
		// If I was just picked up, or created & immediately carried, add myself to this client's list of weapons
		if ( (m_iState != WEAPON_NOT_CARRIED ) && (m_iOldState == WEAPON_NOT_CARRIED) )
		{
			// Tell the HUD this weapon's been picked up
			if ( ShouldDrawPickup() )
			{
				CBaseHudWeaponSelection *pHudSelection = GetHudWeaponSelection();
				if ( pHudSelection )
				{
					pHudSelection->OnWeaponPickup( this );
				}

				pPlayer->EmitSound( "Player.PickupWeapon" );
			}
		}
	}
	else // weapon carried by other player or not at all
	{
		int overrideModelIndex = CalcOverrideModelIndex();
		if( overrideModelIndex != -1 && overrideModelIndex != GetModelIndex() )
		{
			SetModelIndex( overrideModelIndex );
		}
	}

	if ( updateType == DATA_UPDATE_CREATED )
	{
		UpdateVisibility();
	}

	m_iOldState = m_iState;

	m_bJustRestored = false;
}
#endif

//-----------------------------------------------------------------------------
// Is anyone carrying it?
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::IsBeingCarried() const
{
	return ( m_hOwner.Get() != NULL );
}

//-----------------------------------------------------------------------------
// Is the carrier alive?
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::IsCarrierAlive() const
{
	if ( !m_hOwner.Get() )
		return false;

	return m_hOwner.Get()->GetHealth() > 0;
}

//-----------------------------------------------------------------------------
// Should this object cast shadows?
//-----------------------------------------------------------------------------
ShadowType_t C_BaseCombatWeapon::ShadowCastType()
{
	if ( IsEffectActive( /*EF_NODRAW |*/ EF_NOSHADOW ) )
		return SHADOWS_NONE;

#if defined( L4D2 )
	// C41380: inactive carried weapons have no world-model shadow.
	if ( !GetOwner() )
		return SHADOWS_RENDER_TO_TEXTURE;
	if ( ShouldDrawUsingViewModel() )
		return SHADOWS_NONE;
	return m_iState == WEAPON_IS_CARRIED_BY_PLAYER ? SHADOWS_NONE : SHADOWS_RENDER_TO_TEXTURE;
#else
	if (!IsBeingCarried())
		return SHADOWS_RENDER_TO_TEXTURE;

	if (IsCarriedByLocalPlayer() && !C_BasePlayer::ShouldDrawLocalPlayer())
		return SHADOWS_NONE;

	return SHADOWS_RENDER_TO_TEXTURE;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: This weapon is the active weapon, and it should now draw anything
//			it wants to. This gets called every frame.
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::Redraw()
{
	if ( g_pClientMode->ShouldDrawCrosshair() )
	{
		DrawCrosshair();
	}

	// ammo drawing has been moved into hud_ammo.cpp
}

//-----------------------------------------------------------------------------
// Purpose: Draw the weapon's crosshair
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::DrawCrosshair()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player )
		return;

	Color clr = gHUD.m_clrNormal;
/*

	// TEST: if the thing under your crosshair is on a different team, light the crosshair with a different color.
	Vector vShootPos, vShootAngles;
	GetShootPosition( vShootPos, vShootAngles );

	Vector vForward;
	AngleVectors( vShootAngles, &vForward );
	
	
	// Change the color depending on if we're looking at a friend or an enemy.
	CPartitionFilterListMask filter( PARTITION_ALL_CLIENT_EDICTS );	
	trace_t tr;
	traceline->TraceLine( vShootPos, vShootPos + vForward * 10000, COLLISION_GROUP_NONE, MASK_SHOT, &tr, true, ~0, &filter );

	if ( tr.index != 0 && tr.index != INVALID_CLIENTENTITY_HANDLE )
	{
		C_BaseEntity *pEnt = ClientEntityList().GetBaseEntityFromHandle( tr.index );
		if ( pEnt )
		{
			if ( pEnt->GetTeamNumber() != player->GetTeamNumber() )
			{
				g = b = 0;
			}
		}
	}		 
*/

	CHudCrosshair *pCrosshair = GET_HUDELEMENT( CHudCrosshair );
	if ( !pCrosshair )
		return;

	// Find out if this weapon's auto-aimed onto a target
	bool bOnTarget = ( m_iState == WEAPON_IS_ONTARGET );
	
	if ( player->GetFOV() >= 90 )
	{ 
		// normal crosshairs
		if ( bOnTarget && GetWpnData().iconAutoaim )
		{
			clr[3] = 255;

			pCrosshair->SetCrosshair( GetWpnData().iconAutoaim, clr );
		}
		else if ( GetWpnData().iconCrosshair )
		{
			clr[3] = 255;
			pCrosshair->SetCrosshair( GetWpnData().iconCrosshair, clr );
		}
		else
		{
			pCrosshair->ResetCrosshair();
		}
	}
	else
	{ 
		Color white( 255, 255, 255, 255 );

		// zoomed crosshairs
		if (bOnTarget && GetWpnData().iconZoomedAutoaim)
			pCrosshair->SetCrosshair(GetWpnData().iconZoomedAutoaim, white);
		else if ( GetWpnData().iconZoomedCrosshair )
			pCrosshair->SetCrosshair( GetWpnData().iconZoomedCrosshair, white );
		else
			pCrosshair->ResetCrosshair();
	}
}

//-----------------------------------------------------------------------------
// Purpose: This weapon is the active weapon, and the viewmodel for it was just drawn.
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::ViewModelDrawn( C_BaseViewModel *pViewModel )
{
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if this client's carrying this weapon
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::IsCarriedByLocalPlayer( void )
{
	if ( !GetOwner() )
		return false;

	return ( GetOwner() == C_BasePlayer::GetLocalPlayer() );
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if this client is carrying this weapon and is
//			using the view models
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::ShouldDrawUsingViewModel( void )
{
#if defined( L4D2 )
	return IsCarriedByLocalPlayer() && ShouldDrawLocalPlayerViewModel();
#else
	return IsCarriedByLocalPlayer() && !C_BasePlayer::ShouldDrawLocalPlayer();
#endif
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if this weapon is the local client's currently wielded weapon
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::IsActiveByLocalPlayer( void )
{
	if ( IsCarriedByLocalPlayer() )
	{
		return (m_iState == WEAPON_IS_ACTIVE);
	}

	return false;
}

bool C_BaseCombatWeapon::GetShootPosition( Vector &vOrigin, QAngle &vAngles )
{
	// Get the entity because the weapon doesn't have the right angles.
	C_BaseCombatCharacter *pEnt = ToBaseCombatCharacter( GetOwner() );
	if ( pEnt )
	{
		if ( pEnt == C_BasePlayer::GetLocalPlayer() )
		{
			vAngles = pEnt->EyeAngles();
		}
		else
		{
			vAngles = pEnt->GetRenderAngles();	
		}
	}
	else
	{
		vAngles.Init();
	}

	QAngle vDummy;
	if ( IsActiveByLocalPlayer() && ShouldDrawLocalPlayerViewModel() )
	{
		C_BasePlayer *player = ToBasePlayer( pEnt );
		C_BaseViewModel *vm = player ? player->GetViewModel( 0 ) : NULL;
		if ( vm )
		{
			int iAttachment = vm->LookupAttachment( "muzzle" );
			if ( vm->GetAttachment( iAttachment, vOrigin, vDummy ) )
			{
				return true;
			}
		}
	}
	else
	{
		// Thirdperson
		int iAttachment = LookupAttachment( "muzzle" );
		if ( GetAttachment( iAttachment, vOrigin, vDummy ) )
		{
			return true;
		}
	}

	vOrigin = GetRenderOrigin();
	return false;
}


#if defined( L4D2 )
int C_BaseCombatWeapon::ObjectCaps()
{
	// C040BA0 retains the base query before adding both use capabilities.
	return C_BaseEntity::ObjectCaps() | FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
#if defined( L4D2 )
bool C_BaseCombatWeapon::ShouldDraw()
{
	return m_pWeaponClone == NULL && ShouldDrawWeapon(); // C40CD0.
}

// C41020, primary363. Clone and particle consumers bypass C40CD0.
bool C_BaseCombatWeapon::ShouldDrawWeapon()
{
	if ( m_iWorldModelIndex == 0 || IsEffectActive( EF_NODRAW ) ) return false;
	C_BaseCombatCharacter *owner = GetOwner();
	if ( !owner ) return true;
	if ( owner->GetRenderTeamNumber() == TEAM_INFECTED ) return false;
	const bool active = m_iState == WEAPON_IS_ACTIVE;
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	if ( owner == local ) return active && local->ShouldDrawLocalPlayerModel();
	if ( !owner->IsPlayer() ) return true;
	return active;
}
#else
bool C_BaseCombatWeapon::ShouldDraw( void )
{
	if ( m_iWorldModelIndex == 0 )
		return false;

	// FIXME: All weapons with owners are set to transmit in CBaseCombatWeapon::UpdateTransmitState,
	// even if they have EF_NODRAW set, so we have to check this here. Ideally they would never
	// transmit except for the weapons owned by the local player.
	if ( IsEffectActive( EF_NODRAW ) )
		return false;

	C_BaseCombatCharacter *pOwner = GetOwner();

	// weapon has no owner, always draw it
	if ( !pOwner )
		return true;

	bool bIsActive = ( m_iState == WEAPON_IS_ACTIVE );

	C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

	 // carried by local player?
	if ( pOwner == pLocalPlayer )
	{
		// Only ever show the active weapon
		if ( !bIsActive )
			return false;

		if ( !pOwner->ShouldDraw() )
		{
			// Our owner is invisible.
			// This also tests whether the player is zoomed in, in which case you don't want to draw the weapon.
			return false;
		}

		// 3rd person mode?
		if ( !ShouldDrawLocalPlayerViewModel() )
			return true;

		// don't draw active weapon if not in some kind of 3rd person mode, the viewmodel will do that
		return false;
	}

	// If it's a player, then only show active weapons
	if ( pOwner->IsPlayer() )
	{
		// Show it if it's active...
		return bIsActive;
	}

	// FIXME: We may want to only show active weapons on NPCs
	// These are carried by AIs; always show them
	return true;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Return true if a weapon-pickup icon should be displayed when this weapon is received
//-----------------------------------------------------------------------------
#if defined( L4D2 )
bool C_BaseCombatWeapon::ShouldDrawPickup()
{
	return static_cast< signed char >( GetPosition() ) >= 0 && !m_bJustRestored;
}
#else
bool C_BaseCombatWeapon::ShouldDrawPickup( void )
{
	if ( GetWeaponFlags() & ITEM_FLAG_NOITEMPICKUP )
		return false;

	if ( m_bJustRestored )
		return false;

	return true;
}
#endif
		   
//-----------------------------------------------------------------------------
// Purpose: Render the weapon. Draw the Viewmodel if the weapon's being carried
//			by this player, otherwise draw the worldmodel.
//-----------------------------------------------------------------------------
#if defined( L4D2 )
int C_BaseCombatWeapon::DrawModel( int flags )
{
	VPROF_BUDGET( "C_BaseCombatWeapon::DrawModel", VPROF_BUDGETGROUP_MODEL_RENDERING );
	if ( !m_bReadyToDraw ) return 0;
	if ( !IsVisible() ) return 0;
	if ( IsCarriedByObservedPlayer() ) return 0; // B's sole410B0 NONCONST provider.
	EnsureCorrectRenderingModel();
	return BaseClass::DrawModel( flags );
}
#else
int C_BaseCombatWeapon::DrawModel( int flags )
{
	VPROF_BUDGET( "C_BaseCombatWeapon::DrawModel", VPROF_BUDGETGROUP_MODEL_RENDERING );
	if ( !m_bReadyToDraw )
		return 0;

	if ( !IsVisible() )
		return 0;

	// check if local player chases owner of this weapon in first person
	C_BasePlayer *localplayer = C_BasePlayer::GetLocalPlayer();

	if ( localplayer && localplayer->IsObserver() && GetOwner() )
	{
		// don't draw weapon if chasing this guy as spectator
		// we don't check that in ShouldDraw() since this may change
		// without notification 
		
		if ( localplayer->GetObserverMode() == OBS_MODE_IN_EYE &&
			 localplayer->GetObserverTarget() == GetOwner() ) 
			return false;
	}

	return BaseClass::DrawModel( flags );
}
#endif


//-----------------------------------------------------------------------------
// Allows the client-side entity to override what the network tells it to use for
// a model. This is used for third person mode, specifically in HL2 where the
// the weapon timings are on the view model and not the world model. That means the
// server needs to use the view model, but the client wants to use the world model.
//-----------------------------------------------------------------------------
int C_BaseCombatWeapon::CalcOverrideModelIndex() 
{ 
	C_BasePlayer *localplayer = C_BasePlayer::GetLocalPlayer();
	if ( localplayer && 
		localplayer == GetOwner() &&
		ShouldDrawLocalPlayerViewModel() )
	{
		return BaseClass::CalcOverrideModelIndex();
	}
	else
	{
		return GetWorldModelIndex();
	}
}

bool C_BaseCombatWeapon::PredictionErrorShouldResetLatchedForAllPredictables( void )
{
#ifdef HL2MP
	// misyl: Although I have tried to fix many of the pred errors in HL2MP.
	// Still many remain, and many things remain unpredictable without a rewrite of the weapon code.
	//
	// As a workaround, if this weapon is not tangible (ie. has an owner/in inventory).
	// Don't reset every latched var for every predictable ever if we have a pred error on a weapon.
	//
	// This might be useful for other games as well, but needs wider testing there.
	if ( GetOwner() )
		return false;
#endif

	return BaseClass::PredictionErrorShouldResetLatchedForAllPredictables();
}

//-----------------------------------------------------------------------------
// tool recording
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::GetToolRecordingState( KeyValues *msg )
{
	if ( !ToolsEnabled() )
		return;

	int nModelIndex = GetModelIndex();
	int nWorldModelIndex = GetWorldModelIndex();
	if ( nModelIndex != nWorldModelIndex )
	{
		SetModelIndex( nWorldModelIndex );
	}

	BaseClass::GetToolRecordingState( msg );

	if ( m_iState == WEAPON_NOT_CARRIED )
	{
		BaseEntityRecordingState_t *pBaseEntity = (BaseEntityRecordingState_t*)msg->GetPtr( "baseentity" );
		pBaseEntity->m_nOwner = -1;
	}
	else
	{
		msg->SetInt( "worldmodel", 1 );
		if ( m_iState == WEAPON_IS_ACTIVE )
		{
			BaseEntityRecordingState_t *pBaseEntity = (BaseEntityRecordingState_t*)msg->GetPtr( "baseentity" );
			pBaseEntity->m_bVisible = true;
		}
	}

	if ( nModelIndex != nWorldModelIndex )
	{
		SetModelIndex( nModelIndex );
	}
}
#if defined( L4D2 )
bool C_BaseCombatWeapon::IsCarriedByObservedPlayer()
{
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	if ( local && local->GetObserverMode() != OBS_MODE_NONE && GetOwnerEntity() &&
		local->GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_BaseEntity *target = local->GetObserverTarget();
		if ( target == GetOwnerEntity() )
			return true;
	}
	return false;
}

void C_BaseCombatWeapon::OnWeaponCloneDestroyed()
{
	m_pWeaponClone = NULL; // Whole C40E60, do not compare current clone identity.
}

void C_BaseCombatWeapon::UpdateOnRemove()
{
	if ( m_pWeaponClone )
		m_pWeaponClone->Release();
	m_pWeaponClone = NULL;
	BaseClass::UpdateOnRemove();
}

// Native C19210 resolves weapon attachment names against the world model,
// matching the clone used by GetAttachment, not the networked viewmodel.
int C_BaseCombatWeapon::LookupAttachment( const char *attachmentName )
{
	const int worldModelIndex = GetWorldModelIndex();
	if ( worldModelIndex != GetModelIndex() )
	{
		const model_t *worldModel = modelinfo->GetModel( worldModelIndex );
		if ( worldModel )
		{
			const MDLHandle_t handle = modelinfo->GetCacheHandle( worldModel );
			if ( handle != MDLHANDLE_INVALID )
			{
				const studiohdr_t *header = mdlcache->GetStudioHdr( handle );
				if ( header )
				{
					CStudioHdr studioHdr( header, mdlcache );
					return Studio_FindAttachment( &studioHdr, attachmentName ) + 1;
				}
			}
		}
	}
	return BaseClass::LookupAttachment( attachmentName );
}

bool C_BaseCombatWeapon::GetAttachment( int number, Vector &origin )
{
	if ( !m_pWeaponClone )
		return BaseClass::GetAttachment( number, origin );
	m_pWeaponClone->UpdateClone();
	return m_pWeaponClone->GetAttachment( number, origin );
}

bool C_BaseCombatWeapon::GetAttachment( int number, Vector &origin, QAngle &angles )
{
	if ( !m_pWeaponClone )
		return BaseClass::GetAttachment( number, origin, angles );
	m_pWeaponClone->UpdateClone();
	return m_pWeaponClone->GetAttachment( number, origin, angles );
}

bool C_BaseCombatWeapon::GetAttachment( int number, matrix3x4_t &matrix )
{
	if ( !m_pWeaponClone )
		return BaseClass::GetAttachment( number, matrix );
	m_pWeaponClone->UpdateClone();
	return m_pWeaponClone->GetAttachment( number, matrix );
}

bool C_BaseCombatWeapon::GetAttachmentVelocity( int number, Vector &origin, Quaternion &angles )
{
	if ( !m_pWeaponClone )
		return BaseClass::GetAttachmentVelocity( number, origin, angles );
	m_pWeaponClone->UpdateClone();
	return m_pWeaponClone->GetAttachmentVelocity( number, origin, angles );
}

void C_BaseCombatWeapon::InvalidateAttachments()
{
	if ( m_pWeaponClone )
	{
		m_pWeaponClone->UpdateClone();
		m_pWeaponClone->InvalidateAttachments();
	}
	BaseClass::InvalidateAttachments();
}
#endif

#if defined( L4D2 )
ConVar cl_viewmodelsclonedasworld( "cl_viewmodelsclonedasworld", "1", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
void C_BaseCombatWeapon::EnsureCorrectRenderingModel()
{
	if ( cl_viewmodelsclonedasworld.GetBool() ) return;
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	if ( !local || local != GetOwner() || local->ShouldDrawLocalPlayerModel() )
	{
		SetModelIndex( static_cast< short >( GetWorldModelIndex() ) );
		CStudioHdr *header = GetModelPtr();
		if ( header && header->IsValid() && GetSequence() >= header->GetNumSeq() )
			SetSequence( 0 );
		SetBodygroup( 1, GetOwner() ? 1 : 0 ); // Native FRESH owner after model/sequence callbacks.
	}
}
#endif
