//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/effects/c_survivor_death_model.h"
#include "l4d2/infected/boss/c_witch.h"
#include "NextBot/C_NextBot.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/effects/terror_glow_scene.h"
#include "l4d2/effects/terror_glow_policy.h"
#include "l4d2/effects/terror_glow_visibility.h"
#include "l4d2/effects/terror_glow_entities.h"
#include "l4d2/effects/terror_glow_property.h"
#include "l4d2/effects/terror_glow_render.h"
#include "l4d2/clientmode_terror.h"
#include "l4d2/effects/glowproperty.h"
#include "l4d2/effects/glow_cvars.h"
#include "igameresources.h"
#include "view_shared.h"
#include "view.h"
#include "iviewrender.h"
#include "igamesystem.h"
#include "glow_outline_effect.h"
#include "materialsystem/imaterialsystem.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

extern ConVar spec_keep_team_glows;
static ConVar cl_glow_item_r( "cl_glow_item_r", "0.7", 0x00080008 );
static ConVar cl_glow_item_g( "cl_glow_item_g", "0.7", 0x00080008 );
static ConVar cl_glow_item_b( "cl_glow_item_b", "1.0", 0x00080008 );
static ConVar cl_glow_item_far_r( "cl_glow_item_far_r", "0.3", 0x00080008 );
static ConVar cl_glow_item_far_g( "cl_glow_item_far_g", "0.4", 0x00080008 );
static ConVar cl_glow_item_far_b( "cl_glow_item_far_b", "1.0", 0x00080008 );
static ConVar cl_glow_thirdstrike_item_r( "cl_glow_thirdstrike_item_r", "1.0", 0x00080008 );
static ConVar cl_glow_thirdstrike_item_g( "cl_glow_thirdstrike_item_g", "0.0", 0x00080008 );
static ConVar cl_glow_thirdstrike_item_b( "cl_glow_thirdstrike_item_b", "0.0", 0x00080008 );
static ConVar cl_glow_thirdstrike_item_colorblind_r( "cl_glow_thirdstrike_item_colorblind_r", "0.3", 0x00080008 );
static ConVar cl_glow_thirdstrike_item_colorblind_g( "cl_glow_thirdstrike_item_colorblind_g", "1.0", 0x00080008 );
static ConVar cl_glow_thirdstrike_item_colorblind_b( "cl_glow_thirdstrike_item_colorblind_b", "1.0", 0x00080008 );

namespace
{
C_TerrorPlayer *RenderPlayer( int slot )
{
	return ToTerrorPlayer( TerrorGlow_ResolveRenderPlayer( C_BasePlayer::GetLocalPlayer() ) );
}
void ItemColor( Vector &color, bool far )
{
	if ( far ) color.Init( cl_glow_item_far_r.GetFloat(), cl_glow_item_far_g.GetFloat(), cl_glow_item_far_b.GetFloat() );
	else color.Init( cl_glow_item_r.GetFloat(), cl_glow_item_g.GetFloat(), cl_glow_item_b.GetFloat() );
	color *= cl_glow_brightness.GetFloat();
}
bool SelectedColor( int slot, Vector &color )
{
	color.Init();
	C_TerrorPlayer *viewer = ToTerrorPlayer( TerrorGlow_ResolveItemColorPlayer( C_BasePlayer::GetLocalPlayer() ) );
	if ( !viewer ) return false;
	const bool tank = viewer->GetTeamNumber() == TEAM_INFECTED && viewer->m_zombieClass == 8;
	const int useIndex = viewer->GetGlowUseEntityIndex();
	if ( static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->GetModeSaturation() < 1.0f )
	{
		// C:26FD60: preserve the AND in this misleadingly named far-item gate.
		if ( sv_disable_glow_faritems.GetBool() && ( tank || useIndex == 0 ) ) return false;
		if ( cl_colorblind.GetInt() == 2 )
			color.Init( cl_glow_thirdstrike_item_colorblind_r.GetFloat(), cl_glow_thirdstrike_item_colorblind_g.GetFloat(), cl_glow_thirdstrike_item_colorblind_b.GetFloat() );
		else color.Init( cl_glow_thirdstrike_item_r.GetFloat(), cl_glow_thirdstrike_item_g.GetFloat(), cl_glow_thirdstrike_item_b.GetFloat() );
		color *= cl_glow_brightness.GetFloat();
	}
	else if ( tank || useIndex <= 0 )
	{
		if ( viewer->GetTeamNumber() == TEAM_INFECTED )
		{
			TerrorGlow_GetAbilityColor( color );
			color *= cl_glow_brightness.GetFloat();
		}
		else
		{
			if ( sv_disable_glow_faritems.GetBool() ) return false;
			ItemColor( color, true );
		}
	}
	else ItemColor( color, false );
	return true;
}
C_BaseEntity *SelectedEntity( C_TerrorPlayer *viewer )
{
	if ( viewer->GetSelectedGlowEntityIndex() <= 0 ) return NULL;
	C_BaseEntity *entity = cl_entitylist->GetEnt( viewer->GetSelectedGlowEntityIndex() );
	return entity && entity != viewer->GetActiveWeapon() && entity != viewer->GetViewModel( 0 ) ? entity : NULL;
}
bool IsDefibrillatorActive( C_TerrorPlayer *viewer )
{
	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( viewer->GetActiveWeapon() );
	return weapon && weapon->GetWeaponID() == 24;
}
float NativeLength( const Vector &value )
{
	return sqrtf( ( value.y * value.y + value.z * value.z ) + value.x * value.x );
}
bool OutsideFrustum( C_BaseEntity *entity, Vector &mins, Vector &maxs )
{
	entity->GetRenderBoundsWorldspace( mins, maxs );
	// C:BDF00 obtains these same world bounds and calls engine CullBox.
	return entity->GetRenderHandle() != INVALID_CLIENT_RENDER_HANDLE && engine->CullBox( mins, maxs );
}
bool OutsideNearSphere( const CViewSetup &view, const Vector &mins, const Vector &maxs )
{
	const Vector center = ( mins + maxs ) * 0.5f - view.origin;
	return view.zNear <= NativeLength( maxs - mins ) * 0.5f + NativeLength( center );
}
void DrawChildren( C_TerrorPlayer *player, unsigned char alpha )
{
	for ( C_BaseEntity *child = player->FirstMoveChild(); child; child = child->NextMovePeer() )
	{
		C_TerrorPlayer *jockey = child->IsPlayer() ? ToTerrorPlayer( child ) : NULL;
		if ( !jockey || !jockey->m_jockeyVictim.Get() ) TerrorGlow_DrawModel( child, alpha );
	}
}
struct GlowStencilGroup_t
{
	C_TerrorPlayer *m_pPlayer;
	CUtlVector< C_BaseEntity * > m_Children;
};
class CWitchColorPass
{
public:
	explicit CWitchColorPass( bool draw ) : m_found( false ), m_draw( draw ) {}
	bool operator()( C_NextBotCombatCharacter *actor )
	{
		C_Witch *witch = dynamic_cast< C_Witch * >( actor );
		if ( !witch || witch->GetTeamNumber() != TEAM_INFECTED || witch->m_lifeState != LIFE_ALIVE ) return true;
		float r, g, b, a;
		witch->GetGlowColor( &r, &g, &b, &a );
		if ( !( a > 0.0f ) ) return true;
		m_found = true;
		if ( !m_draw ) return false;
		const float color[3] = { r, g, b };
		render->SetBlend( a );
		render->SetColorModulation( color );
		TerrorGlow_DrawModel( witch, static_cast< unsigned char >( _mm_cvttss_si32( _mm_set_ss( a * 255.0f ) ) ) );
		return true;
	}
	bool m_found;
private:
	bool m_draw;
};
class CWitchStencilPass
{
public:
	explicit CWitchStencilPass( const CViewSetup &view ) : m_view( view ) {}
	bool operator()( C_NextBotCombatCharacter *actor )
	{
		C_Witch *witch = dynamic_cast< C_Witch * >( actor );
		if ( !witch || witch->GetTeamNumber() != TEAM_INFECTED || witch->m_lifeState != LIFE_ALIVE ) return true;
		float r, g, b, a;
		witch->GetGlowColor( &r, &g, &b, &a );
		Vector mins, maxs;
		if ( a > 0.0f && !OutsideFrustum( witch, mins, maxs ) &&
			( OutsideNearSphere( m_view, mins, maxs ) || witch->GetLastGlowDrawFrame( 0 ) != gpGlobals->framecount ) )
		{
			const float blend = render->GetBlend();
			render->SetBlend( 0.0f );
			TerrorGlow_DrawModel( witch, 255, 0x10000601 );
			render->SetBlend( blend );
		}
		return true;
	}
private:
	const CViewSetup &m_view;
};
}

bool TerrorGlow_ShouldRender( int slot )
{
	C_TerrorPlayer *viewer = RenderPlayer( slot );
	return viewer && sv_glowenable.GetBool() && !viewer->GetViewEntity();
}

float TerrorGlow_GetCompositeScale( int slot )
{
	C_TerrorPlayer *viewer = RenderPlayer( slot );
	return viewer ? TerrorGlow_GetITCompositeScale( *viewer ) : 0.0f;
}

void TerrorGlow_UpdateView( const CViewSetup &view, int slot )
{
	// The native property manager Update callback is independent of player
	// ClientThink. The latter owns each player's alpha/selection update.
	C_TerrorPlayer *viewer = RenderPlayer( slot );
	if ( !viewer ) return; // C:1094D0 leaves prior eligibility unchanged here.
	const CUtlVector< CGlowProperty * > &properties = TerrorGlow_GetProperties();
	FOR_EACH_VEC( properties, i )
	{
		CGlowProperty *glow = properties[i];
		if ( !glow || !glow->GetOuter() ) continue;
		glow->SetEnabled( false );
		if ( !TerrorGlow_PropertyInRange( *glow, viewer ) ) continue;
		switch ( glow->GetType() )
		{
		case 1: glow->SetEnabled( TerrorGlow_PropertyUsableInView( *glow, viewer, glow_use_tolerance.GetFloat() ) ); break;
		case 2: glow->SetEnabled( TerrorGlow_IsAbleToSee( viewer, glow->GetOuter() ) ); break;
		case 3: glow->SetEnabled( true ); break;
		}
	}
}

// C2FFF0 is shared by C2414D0 addon and C30F490 weapon stencil callbacks.
// It is NOT the player's distinct C2525C0 virtual stencil policy.
bool TerrorGlow_GetParentStencilState( C_TerrorPlayer *player, ShaderStencilState_t &state )
{
	C_BasePlayer *raw = C_BasePlayer::GetLocalPlayer();
	C_BasePlayer *viewer = spec_keep_team_glows.GetBool() ? raw : TerrorGlow_ResolveRenderPlayer( raw );
	if ( !viewer || !player || ( player == viewer && player->m_lifeState == LIFE_ALIVE ) )
		return false;
	if ( player->GetTeamNumber() == TEAM_INFECTED && viewer->GetTeamNumber() != TEAM_INFECTED &&
		!( player->GetTeamNumber() == TEAM_INFECTED && player->m_zombieClass == 3 && player->m_pounceVictim.Get() ) &&
		!( player->GetTeamNumber() == TEAM_INFECTED && player->m_zombieClass == 6 && player->m_pummelVictim.Get() ) &&
		!( player->GetTeamNumber() == TEAM_INFECTED && player->m_zombieClass == 5 && player->m_jockeyVictim.Get() ) &&
		!TerrorGlow_IsIT( *player ) )
		return false;
	if ( player->IsDormant() )
		return false;
	float r, g, b, alpha;
	player->GetGlowColor( &r, &g, &b, &alpha );
	if ( alpha <= 0.0f ) // C300E1 COMISS zero,alpha / JNB: unordered is admitted here.
		return false;
	const int reference = player->IsGlowHighlightedByAbility( 0 ) || !TerrorGlow_IsSurvivorTeam( player->GetTeamNumber() ) ? 2 : 8;
	state.m_bEnable = true;
	state.m_nReferenceValue = reference;
	state.m_nWriteMask = reference;
	state.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
	state.m_PassOp = STENCILOPERATION_REPLACE;
	state.m_FailOp = STENCILOPERATION_KEEP;
	state.m_ZFailOp = STENCILOPERATION_REPLACE;
	return true;
}

bool TerrorGlow_ApplyPlayerStencil( C_TerrorPlayer *player )
{
	ShaderStencilState_t state;
	if ( !player->GetStencilState( state ) )
		return false;
	CMatRenderContextPtr context( materials );
	state.SetStencilState( context );
	return true;
}

void TerrorGlow_DrawStencil( const CViewSetup &view, int slot )
{
	C_TerrorPlayer *viewer = RenderPlayer( slot );
	C_BasePlayer *stencilViewer = spec_keep_team_glows.GetBool() ? C_BasePlayer::GetLocalPlayer() : viewer;
	if ( !viewer || !stencilViewer ) return;
	CUtlVector< GlowStencilGroup_t > groups;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || player->IsDormant() || !GameResources()->IsConnected( i ) ) continue;
		const bool survivor = TerrorGlow_IsSurvivorTeam( player->GetTeamNumber() );
		if ( !survivor && player->m_lifeState != LIFE_ALIVE ) continue;
		if ( player == stencilViewer && player->m_lifeState == LIFE_ALIVE ) continue;
		if ( player->GetTeamNumber() == TEAM_INFECTED && stencilViewer->GetTeamNumber() != TEAM_INFECTED &&
			!( ( player->m_zombieClass == 3 && player->m_pounceVictim.Get() ) ||
			( player->m_zombieClass == 6 && player->m_pummelVictim.Get() ) ||
			( player->m_zombieClass == 5 && player->m_jockeyVictim.Get() ) || TerrorGlow_IsIT( *player ) ) ) continue;
		float r, g, b, a;
		player->GetGlowColor( &r, &g, &b, &a );
		if ( !( a > 0.0f ) ) continue;
		Vector mins, maxs;
		const bool culled = OutsideFrustum( player, mins, maxs );
		if ( player->m_lifeState == LIFE_ALIVE && culled ) continue;
		if ( !OutsideNearSphere( view, mins, maxs ) && player->WasDrawnForGlow( slot ) ) continue;
		const float blend = render->GetBlend();
		render->SetBlend( 0.0f );
		TerrorGlow_DrawModel( player, 255, 0x10000601 );
		render->SetBlend( blend );
		if ( survivor && player->FirstMoveChild() )
		{
			GlowStencilGroup_t &group = groups[groups.AddToTail()];
			group.m_pPlayer = player;
			for ( C_BaseEntity *child = player->FirstMoveChild(); child; child = child->NextMovePeer() )
			{
				C_TerrorPlayer *jockey = child->IsPlayer() ? ToTerrorPlayer( child ) : NULL;
				if ( !jockey || !jockey->m_jockeyVictim.Get() ) group.m_Children.AddToTail( child );
			}
		}
	}
	if ( groups.Count() )
	{
		Frustum frustum;
		render->Push3DView( view, 0, NULL, frustum );
		const float blend = render->GetBlend();
		render->SetBlend( 0.0f );
		FOR_EACH_VEC( groups, i )
		{
			const GlowStencilGroup_t &group = groups[i];
			C_TerrorPlayer *player = group.m_pPlayer;
			const int reference = player->IsGlowHighlightedByAbility( slot ) || !TerrorGlow_IsSurvivorTeam( player->GetTeamNumber() ) ? 2 : 8;
			// C:271910 initializes BOTH pass and zfail to REPLACE (3).
			TerrorGlow_BeginStencilMask( reference, true, reference );
			FOR_EACH_VEC( group.m_Children, j ) TerrorGlow_DrawModel( group.m_Children[j], 0 );
		}
		TerrorGlow_EndStencilMask();
		render->SetBlend( blend );
		render->PopView( frustum );
	}
	if ( TerrorGameRules()->IsPlayerControlledZombies() && stencilViewer->GetTeamNumber() == TEAM_INFECTED )
	{
		CWitchStencilPass pass( view );
		TheClientNextBots().ForEachCombatCharacter( pass );
	}
	const float savedBlend = render->GetBlend();
	render->SetBlend( 0.0f );
	C_BaseEntity *selected = SelectedEntity( viewer );
	Vector selectedColor;
	if ( selected && SelectedColor( slot, selectedColor ) )
	{
		TerrorGlow_BeginStencilMask( 2, true, 2 );
		TerrorGlow_DrawModel( selected, 255 );
		TerrorGlow_EndStencilMask();
	}
	const CUtlVector< CGlowProperty * > &properties = TerrorGlow_GetProperties();
	FOR_EACH_VEC( properties, i )
	{
		CGlowProperty *glow = properties[i];
		if ( glow->IsEnabled() && TerrorGlow_PropertyMatchesTeam( *glow, viewer ) )
		{
			TerrorGlow_BeginStencilMask( 2, true, 2 );
			TerrorGlow_DrawModel( glow->GetOuter(), 255 );
			TerrorGlow_EndStencilMask();
		}
	}
	if ( IsDefibrillatorActive( viewer ) )
	{
		const CUtlVector< C_SurvivorDeathModel * > &bodies = C_SurvivorDeathModel::GetAll();
		FOR_EACH_VEC( bodies, i ) if ( bodies[i] )
		{
			TerrorGlow_BeginStencilMask( 2, true, 2 );
			TerrorGlow_DrawModel( bodies[i], 255 );
			TerrorGlow_EndStencilMask();
		}
	}
	render->SetBlend( savedBlend );
}

bool TerrorGlow_DrawColor( int slot, bool draw )
{
	C_TerrorPlayer *viewer = RenderPlayer( slot );
	if ( !viewer ) return false;
	bool found = false;
	for ( int i = 1; i <= 32; ++i )
	{
		C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || player->IsDormant() || !GameResources()->IsConnected( i ) ) continue;
		float r, g, b, a;
		player->GetGlowColor( &r, &g, &b, &a );
		if ( !( a > 0.0f ) ) continue;
		if ( !draw ) return true;
		found = true;
		const float color[3] = { a * r, a * g, a * b };
		render->SetBlend( 1.0f );
		render->SetColorModulation( color );
		const unsigned char alpha = static_cast< unsigned char >( _mm_cvttss_si32( _mm_set_ss( a * 255.0f ) ) );
		TerrorGlow_DrawModel( player, alpha );
		if ( TerrorGlow_IsSurvivorTeam( player->GetTeamNumber() ) ) DrawChildren( player, alpha );
	}
	C_BaseEntity *selected = SelectedEntity( viewer );
	if ( selected )
	{
		if ( !draw ) return true; // Native probe does not apply the color gate.
		Vector color;
		if ( SelectedColor( slot, color ) )
		{
			float scale = 1.0f;
			const float multiplier = selected->GetGlowBackfaceMult();
			// C:273210: selected-item <=0 BYPASSES its backface test. This is
			// deliberately different from property backface-zero rejection.
			if ( multiplier > 0.0f )
			{
				Vector forward;
				AngleVectors( selected->GetAbsAngles(), &forward );
				if ( DotProduct( forward, viewer->GetAbsOrigin() - selected->GetAbsOrigin() ) < 0.0f ) scale = multiplier;
			}
			render->SetBlend( 1.0f );
			if ( scale != 1.0f ) color *= scale;
			render->SetColorModulation( color.Base() );
			TerrorGlow_DrawModel( selected, 255 );
			found = true;
		}
	}
	const CUtlVector< CGlowProperty * > &properties = TerrorGlow_GetProperties();
	FOR_EACH_VEC( properties, i )
	{
		CGlowProperty *glow = properties[i];
		if ( !glow->IsEnabled() || !TerrorGlow_PropertyMatchesTeam( *glow, viewer ) ) continue;
		const float scale = TerrorGlow_PropertyBackfaceScale( *glow, viewer, glow->GetOuter()->GetGlowBackfaceMult() );
		if ( scale == 0.0f ) continue;
		if ( !draw ) return true;
		Vector color;
		ItemColor( color, true );
		glow->GetColor( &color.x, &color.y, &color.z );
		if ( glow->GetOuter() == viewer->GetGlowUseTarget() ) ItemColor( color, false );
		if ( scale < 1.0f ) color *= scale;
		render->SetBlend( 1.0f );
		render->SetColorModulation( color.Base() );
		TerrorGlow_DrawModel( glow->GetOuter(), 255 );
		found = true;
	}
	if ( IsDefibrillatorActive( viewer ) && !sv_disable_glow_survivors.GetBool() )
	{
		const CUtlVector< C_SurvivorDeathModel * > &bodies = C_SurvivorDeathModel::GetAll();
		FOR_EACH_VEC( bodies, i ) if ( bodies[i] )
		{
			found = true;
			if ( draw )
			{
				const float color[3] = { 1.0f, 0.0f, 0.0f };
				render->SetBlend( 0.5f );
				render->SetColorModulation( color );
				TerrorGlow_DrawModel( bodies[i], 255 );
			}
		}
	}
	C_BasePlayer *witchViewer = spec_keep_team_glows.GetBool() ? C_BasePlayer::GetLocalPlayer() : viewer;
	if ( TerrorGameRules()->IsPlayerControlledZombies() && witchViewer && witchViewer->GetTeamNumber() == TEAM_INFECTED )
	{
		CWitchColorPass pass( draw );
		TheClientNextBots().ForEachCombatCharacter( pass );
		found = found || pass.m_found;
	}
	return found;
}

namespace
{
// C:109610 is the native per-frame game-system thunk into C:1094D0.
class CGlowPropertyFrame : public CAutoGameSystemPerFrame
{
public:
	CGlowPropertyFrame() : CAutoGameSystemPerFrame( "CGlowManager" ) {}
	virtual void Update( float frametime ) OVERRIDE
	{
		TerrorGlow_UpdateView( *view->GetPlayerViewSetup(), 0 );
	}
};
CGlowPropertyFrame s_GlowPropertyFrame;
}

#pragma float_control( pop )
