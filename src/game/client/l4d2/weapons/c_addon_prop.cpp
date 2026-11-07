//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_addon_prop.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/effects/terror_glow_scene.h"
#include "l4d2/player/c_transitioning_player.h"
#include "debugoverlay_shared.h"
#include "tier0/memdbgon.h"

extern ConVar cl_addon_debug;

C_AddonProp::C_AddonProp()
{
	m_flFadeScale = 0.0f;
	SetClassname( "addon" );
	m_bCopyParentAnimation = false;
	m_activity = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_IDLE );
	m_cachedAbsOrigin.Init();
	m_cachedAbsAngles.Init();
}

// Original client 2410D0 / 241110. Qualifying the base calls is essential:
// these are the actual calculated parent/attachment transforms, not network
// origins or a recursive call to this cache accessor.
const Vector &C_AddonProp::GetAbsOrigin() const
{
	if ( IsAbsQueriesValid() )
		m_cachedAbsOrigin = BaseClass::GetAbsOrigin();
	return m_cachedAbsOrigin;
}

const QAngle &C_AddonProp::GetAbsAngles() const
{
	if ( IsAbsQueriesValid() )
		m_cachedAbsAngles = BaseClass::GetAbsAngles();
	return m_cachedAbsAngles;
}

// S241250 assigns the parent, then follows its bones and enables native0x80 fast-cull bounds.
void C_AddonProp::SetParent( C_BaseEntity *parent, int attachment )
{
	BaseClass::SetParent( parent, attachment );
	if ( m_bCopyParentAnimation )
	{
		FollowEntity( parent, true );
		AddEffects( EF_BONEMERGE_FASTCULL );
	}
}

Activity C_AddonProp::TranslateActivity( Activity activity ) const
{
	C_TerrorPlayer *parent = ToTerrorPlayer( GetMoveParent() );
	if ( !parent )
		return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_IDLE );

	const bool female = parent->GetSurvivorCharacter() == SurvivorCharacter_Producer;
	switch ( LookupTerrorOriginalActivity( activity ) )
	{
	case TERROR_ORIGINAL_ACT_TERROR_HANGING_FROM_TONGUE:
		return TerrorActivityFromOriginal( female ? TERROR_ORIGINAL_ACT_TERROR_HANGING_FROM_TONGUE_FEMALE : TERROR_ORIGINAL_ACT_TERROR_HANGING_FROM_TONGUE_MALE );
	case TERROR_ORIGINAL_ACT_TERROR_DRAGGING_FROM_TONGUE:
		return TerrorActivityFromOriginal( female ? TERROR_ORIGINAL_ACT_TERROR_DRAGGING_FROM_TONGUE_FEMALE : TERROR_ORIGINAL_ACT_TERROR_DRAGGING_FROM_TONGUE_MALE );
	case TERROR_ORIGINAL_ACT_TERROR_CHOKING_TONGUE_GROUND:
		return TerrorActivityFromOriginal( female ? TERROR_ORIGINAL_ACT_TERROR_CHOKING_TONGUE_GROUND_FEMALE : TERROR_ORIGINAL_ACT_TERROR_CHOKING_TONGUE_GROUND_MALE );
	case TERROR_ORIGINAL_ACT_TERROR_PULLED_RUN_RIFLE:
		return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_DRAGGING_FROM_TONGUE_DEPLOY );
	default:
		return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_IDLE );
	}
}

void C_AddonProp::SetActivityCycle( Activity activity, float cycle )
{
	const Activity translated = TranslateActivity( activity );
	if ( translated != m_activity )
	{
		m_activity = translated;
		const int sequence = SelectWeightedSequence( translated );
		if ( sequence >= 0 )
		{
			SetSequence( sequence );
			ResetSequenceInfo();
		}
	}
	SetCycle( cycle );
}

void C_AddonProp::GetRenderBounds( Vector &mins, Vector &maxs )
{
	BaseClass::GetRenderBounds( mins, maxs );
	mins -= Vector( 16.0f, 16.0f, 16.0f );
	maxs += Vector( 16.0f, 16.0f, 16.0f );
}

// C2414D0 reaches the shared C2FFF0 calculator through the existing model
// stencil virtual. Non-player parents deliberately pass NULL to that helper.
bool C_AddonProp::GetStencilState( ShaderStencilState_t &state )
{
	C_BaseEntity *parent = GetMoveParent();
	if ( parent && ( parent->IsPlayer() || !parent->IsDormant() ) )
	{
		C_TerrorPlayer *player = parent->IsPlayer() ? static_cast< C_TerrorPlayer * >( parent ) : NULL;
		if ( TerrorGlow_GetParentStencilState( player, state ) )
			return true;
	}
	return BaseClass::GetStencilState( state );
}

// C241570: the parent, not the addon's own model flags, admits this draw.
bool C_AddonProp::ShouldDraw()
{
	C_BaseEntity *parent = GetMoveParent();
	if ( !parent || parent->IsDormant() )
		return false;
	if ( !parent->IsPlayer() )
		return true;
	return static_cast< C_BasePlayer * >( parent )->GetPlayerViewType() == C_BasePlayer::PLAYER_VIEW_THIRD_PERSON;
}

// C2415F0. Keep the genuine debug branch before the qualified base draw.
int C_AddonProp::DrawModel( int flags )
{
	if ( !ShouldDraw() )
		return 0;
	if ( cl_addon_debug.GetBool() )
	{
		C_BaseEntity *parent = GetMoveParent();
		if ( !parent )
		{
			NDebugOverlay::Sphere( GetRenderOrigin(), GetRenderAngles(), 3.0f, 255, 0, 0, 255, false, 0.1f );
			return BaseClass::DrawModel( flags );
		}
		NDebugOverlay::Line( GetRenderOrigin(), parent->WorldSpaceCenter(), 0, 255, 0, true, 0.1f );
		char label[256];
		if ( parent->IsPlayer() )
		{
			Q_snprintf( label, sizeof( label ), "%d: %s%s", parent->entindex(),
				static_cast< C_BasePlayer * >( parent )->GetPlayerName(), parent->IsDormant() ? " (dormant)" : "" );
		}
		else if ( C_TransitioningPlayer *transition = dynamic_cast< C_TransitioningPlayer * >( parent ) )
		{
			Q_snprintf( label, sizeof( label ), "[TRANSITIONING] %d: %s%s", transition->entindex(),
				transition->GetPlayerName(), transition->IsDormant() ? " (dormant)" : "" );
		}
		else
		{
			Q_snprintf( label, sizeof( label ), "%d: %s%s", parent->entindex(),
				parent->GetClassname(), parent->IsDormant() ? " (dormant)" : "" );
		}
		NDebugOverlay::Text( GetRenderOrigin(), label, false, 0.1f );
	}
	return BaseClass::DrawModel( flags );
}
