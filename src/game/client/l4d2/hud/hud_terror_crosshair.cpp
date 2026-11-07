//========= Copyright Valve Corporation, All rights reserved. ============//
// Existing l4d2/hud_terror_crosshair.cpp replacement. Its separate survivor gun
// arithmetic2FA500/2F9680 remains below, not replaced by an icon or stored state.
#include "cbase.h"
#include "hud.h"
#include "l4d2/hud/hud_terror_crosshair.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/gamerules/terror_mode.h"
#include <vgui/ISurface.h>
#include <vgui_controls/CircularProgressBar.h>

#include "tier0/memdbgon.h"

ConVar cl_crosshair_red( "cl_crosshair_red", "138", 0x00000088, "Crosshair red component", true, 0, true, 255 );
ConVar cl_crosshair_green( "cl_crosshair_green", "182", 0x00000088, "Crosshair green component", true, 0, true, 255 );
ConVar cl_crosshair_blue( "cl_crosshair_blue", "220", 0x00000088, "Crosshair blue component", true, 0, true, 255 );
ConVar cl_crosshair_alpha( "cl_crosshair_alpha", "255", 0x00000088, "Crosshair alpha", true, 0, true, 255 );
ConVar cl_crosshair_thickness( "cl_crosshair_thickness", "2", 0x00000088, "Crosshair thickness in pixels", true, 1, true, 5 );
ConVar cl_crosshair_dynamic( "cl_crosshair_dynamic", "1", 0x00000088, "Crosshair scales based on accuracy" );
ConVar cl_crosshair_circle_alpha( "cl_crosshair_circle_alpha", "0", 0x0000000A, "Crosshair circle alpha" );
ConVar cl_crosshair_circle_mode( "cl_crosshair_circle_mode", "0", 0x0000000A, "Crosshair circle mode (0=actual aim error + pellet scatter, 1=green is aim error, red is pellet scatter, 2=inside of crosshair lines" );
ConVar cl_colorblind( "cl_colorblind", "0", 0x00000088, "Set to 1 for high contrast crosshairs. Set to 2 for specific color changes intended to help common types of color blindness." );

DECLARE_NAMED_HUDELEMENT( CHudTerrorCrosshair, CHudCrosshair );

CHudTerrorCrosshair *GetTerrorCrosshair()
{
	return static_cast< CHudTerrorCrosshair * >( gHUD.FindElement( "CHudCrosshair" ) );
}

CHudTerrorCrosshair::CHudTerrorCrosshair( const char *elementName ) : BaseClass( elementName )
{
	//Native constructor2CBD70: metadata registration/default values use actual
	//CPanelAnimationVars, HUD allocation supplies native omitted-field zeroes.
	m_pAbilityProgress = new vgui::CircularProgressBar( this, "AbilityProgress" );
	//Native CircularProgressBar428E40 explicitly starts direction1 (CCW).
	//SDK's constructor starts CW; preserve native direction through its real API.
	m_pAbilityProgress->SetProgressDirection( vgui::CircularProgressBar::PROGRESS_CCW );
}
CHudTerrorCrosshair::~CHudTerrorCrosshair()
{
	//2CB6D0 tears down the VGUI parent and HUD base, which destroys this real
	//CircularProgressBar child, its texture IDs and image strings. No manual
	//delete of a parent-owned panel or borrowed gHUD texture.
}
void CHudTerrorCrosshair::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	m_pAbilityProgress->SetFgImage( "HUD/PZ_charge_crosshair" );
	m_pInfectedCrosshair = gHUD.GetIcon( "PZ_crosshair_open" );
	m_pWeaponCrosshair = gHUD.GetIcon( "crosshair_default" );
}
void CHudTerrorCrosshair::SetCrosshairState( int state )
{
	//No cached enum: 2CB710 changes the actual inherited texture/color pair.
	switch ( state )
	{
	case 0: SetCrosshair( NULL, Color( 255, 255, 255, 255 ) ); break;
	case 1: ResetCrosshair(); break;
	case 2: SetCrosshair( m_pInfectedCrosshair, Color( 255, 255, 255, 255 ) ); break;
	case 3: SetCrosshair( m_pWeaponCrosshair, Color( 255, 255, 255, 255 ) ); break;
	case 4: SetCrosshair( m_pWeaponCrosshair, Color( 0, 0, 0, 0 ) ); break;
	default: break;
	}
}

bool CHudTerrorCrosshair::ShouldDraw()
{
	const bool nativeVisible = BaseClass::ShouldDraw(); //120D00, native first
	C_TerrorPlayer *pPlayer = static_cast< C_TerrorPlayer * >( C_BasePlayer::GetLocalPlayer() );

	if ( !nativeVisible ) return false;
	//2CB7C0 only adds this INFECTED gate to the actual native base result.
	if ( pPlayer && pPlayer->GetTeamNumber() == TEAM_INFECTED &&
		( ( pPlayer->m_lifeState == LIFE_ALIVE && pPlayer->m_isIncapacitated ) || pPlayer->IsStaggering() ) ) return false;
	return nativeVisible;
}

#pragma float_control( precise, on, push )
void CHudTerrorCrosshair::DrawCrosshair( int x, int y, int width, int height )
{
	//2CB820 is a geometry-aware post-icon hook, not a separate HUD tick or
	//a fake fraction provider. All timers/ability handles below are actual owners.
	C_TerrorPlayer *player = static_cast< C_TerrorPlayer * >( C_BasePlayer::GetLocalPlayer() );
	if ( !player || player->m_lifeState != LIFE_ALIVE || player->IsGhost() )
	{
		m_pAbilityProgress->SetVisible( false );
		return;
	}
	const int team = player->GetTeamNumber();
	if ( team != TEAM_SURVIVOR && team != TEAM_INFECTED )
	{
		m_pAbilityProgress->SetVisible( false );
		return;
	}
	//Native SUB/LEA wraps32bits, including integer-indefinite projected pixels.
	//Unsigned arithmetic retains that result without C++ signed-overflow UB.
	const uint32 padding = (uint32)m_abilitySize;
	m_pAbilityProgress->SetBounds( (int32)( (uint32)x - padding ), (int32)( (uint32)y - padding ),
		(int32)( (uint32)width + 2u * padding ), (int32)( (uint32)height + 2u * padding ) );
	m_pAbilityProgress->SetVisible( true );
	if ( team == TEAM_SURVIVOR )
	{
		m_pAbilityProgress->SetFgColor( Color( 255, 255, 255, 128 ) );
		const float lastShove = player->m_flLastShoveTime;
		const float duration = (float)( (double)player->m_flNextShoveTime - (double)lastShove );
		static ConVarRef coopMinimum( "z_gun_swing_coop_min_penalty" );
		static ConVarRef versusMinimum( "z_gun_swing_vs_min_penalty" );
		const int minimum = g_TerrorModes.IsBaseMode( "coop" ) ? coopMinimum.GetInt() : versusMinimum.GetInt();
		//C2CB925 JNB skips ordered duration<=0; unordered duration continues.
		if ( !( 0.0f >= duration ) && player->m_iShovePenalty > minimum )
		{
			const float elapsed = gpGlobals->curtime - lastShove;
			const float fraction = elapsed / duration;
			m_pAbilityProgress->SetVisible( fraction >= 0.0f && fraction <= 1.0f );
			m_pAbilityProgress->SetProgress( fraction );
		}
		else
		{
			m_pAbilityProgress->SetVisible( false );
			return; //Native does not publish a progress/background on this path.
		}
	}
	else
	{
		C_BaseAbility *ability = player->GetCustomAbility(); //real7892 EHANDLE
		if ( !ability )
		{
			m_pAbilityProgress->SetVisible( false );
			return;
		}
		const double rawProgress = (double)ability->GetRechargeProgress(); //virtual768
		float progress = (float)rawProgress;
		if ( rawProgress > 1.0 ) progress = 1.0f;
		else if ( progress < 0.0f ) progress = 0.0f; //unordered remains NaN, nativeJBE
		m_pAbilityProgress->SetProgress( progress );
		if ( ability->IsActivationSuppressed() ) m_pAbilityProgress->SetFgColor( m_abilitySurpressed );
		else if ( 1.0f > progress ) m_pAbilityProgress->SetFgColor( m_abilityCharging );
		else
		{
			//Native re-reads the local player, then three ordered253560 calls
			//(each checks team3 and the raw received class). No cached class provider.
			C_TerrorPlayer *local = static_cast< C_TerrorPlayer * >( C_BasePlayer::GetLocalPlayer() );
			if ( local &&
				( ( local->GetTeamNumber() == TEAM_INFECTED && local->m_zombieClass == 1 ) ||
				  ( local->GetTeamNumber() == TEAM_INFECTED && local->m_zombieClass == 2 ) ||
				  ( local->GetTeamNumber() == TEAM_INFECTED && local->m_zombieClass == 4 ) ) &&
				ability->IsReadyToActivate() ) //actual virtual704, not CanActivate
				m_pAbilityProgress->SetFgColor( cl_colorblind.GetInt() == 2 ? m_abilityShouldAttack_ColorBlind : m_abilityShouldAttack );
			else m_pAbilityProgress->SetFgColor( m_abilityReady );
		}
	}
	m_pAbilityProgress->SetBgColor( Color( 0, 0, 0, 0 ) );
}
#pragma float_control( pop )

void CHudTerrorCrosshair::Paint()
{
	// The native icon/ability-ring path is independent from gun redraw.
	if ( !m_pCrosshair ) m_pAbilityProgress->SetVisible( false );
	BaseClass::Paint();
}

// C2F9680: called only from the admitted native gun redraw path.
void CHudTerrorCrosshair::DrawGunCrosshair( float scatter, float aimError, float fov, bool shotgun )
{
	const int nWidth = ScreenWidth();
	const int x = nWidth / 2;
	const int y = ScreenHeight() / 2;
	const int nThickness = cl_crosshair_thickness.GetInt();
	const int nHalfThickness = (int)( nThickness * 0.5f );
	const float flRadius = nWidth * ( scatter + aimError ) / fov;

	// Existing native integer/truncation and width-cap geometry is unchanged.
	int nLength = nWidth / 128;
	int nGap = nLength / 2;
	if ( cl_crosshair_dynamic.GetBool() )
	{
		nGap = MAX( (int)( flRadius * 0.4f ), 2 );
		nLength = MAX( (int)MAX( (float)nLength, ( flRadius * 2.0f ) * 0.1f ), 2 * nGap );
		nLength = (int)( nWidth * MIN( (double)nLength / nWidth, 0.0125 ) );
	}

	struct CrosshairRect
	{
		int left, top, right, bottom;
	};
	const CrosshairRect rectangles[5] =
	{
		{ x - nGap - nLength, y - nHalfThickness, x - nGap, y - nHalfThickness + nThickness },
		{ x + nGap, y - nHalfThickness, x + nGap + nLength, y - nHalfThickness + nThickness },
		{ x - nHalfThickness, y - nGap - nLength, x - nHalfThickness + nThickness, y - nGap },
		{ x - nHalfThickness, y + nGap, x - nHalfThickness + nThickness, y + nGap + nLength },
		{ x - nHalfThickness, y - nHalfThickness, x - nHalfThickness + nThickness, y - nHalfThickness + nThickness }
	};
	const int count = shotgun ? 5 : 4;
	if ( cl_colorblind.GetInt() >= 1 )
	{
		vgui::surface()->DrawSetColor( 0, 0, 0, cl_crosshair_alpha.GetInt() );
		for ( int i = 0; i < count; ++i )
		{
			const CrosshairRect &rect = rectangles[i];
			vgui::surface()->DrawFilledRect( rect.left - 1, rect.top - 1, rect.right + 1, rect.bottom + 1 );
		}
	}
	if ( cl_colorblind.GetInt() >= 1 )
		vgui::surface()->DrawSetColor( 255, 255, 255, cl_crosshair_alpha.GetInt() );
	else
		vgui::surface()->DrawSetColor( cl_crosshair_red.GetInt(), cl_crosshair_green.GetInt(), cl_crosshair_blue.GetInt(), cl_crosshair_alpha.GetInt() );
	for ( int i = 0; i < count; ++i )
	{
		const CrosshairRect &rect = rectangles[i];
		vgui::surface()->DrawFilledRect( rect.left, rect.top, rect.right, rect.bottom );
	}

	if ( flRadius >= 0.5f && cl_crosshair_circle_alpha.GetInt() != 0 )
	{
		float flCircleRadius = flRadius;
		const int nMode = cl_crosshair_circle_mode.GetInt();
		if ( nMode == 2 )
			flCircleRadius = nGap;
		vgui::surface()->DrawSetColor( 255, 255, 255, cl_crosshair_circle_alpha.GetInt() );
		if ( flCircleRadius > 2.0f )
			vgui::surface()->DrawOutlinedCircle( x, y, (int)flCircleRadius, 64 );
		if ( nMode == 1 )
		{
			const float flAimRadius = nWidth * scatter / fov;
			const float flScatterRadius = nWidth * aimError / fov;
			vgui::surface()->DrawSetColor( 255, 0, 0, cl_crosshair_circle_alpha.GetInt() );
			if ( flAimRadius > 2.0f )
				vgui::surface()->DrawOutlinedCircle( x, y, (int)flAimRadius, 64 );
			vgui::surface()->DrawSetColor( 0, 255, 0, cl_crosshair_circle_alpha.GetInt() );
			if ( flScatterRadius > 2.0f )
				vgui::surface()->DrawOutlinedCircle( x, y, (int)flScatterRadius, 64 );
		}
	}
}
