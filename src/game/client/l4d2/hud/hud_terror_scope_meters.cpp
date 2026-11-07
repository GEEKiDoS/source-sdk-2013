//========= Copyright Valve Corporation, All rights reserved. ============//
// Pinned client C22D190..22D98F, C293350..294D95. No HL2MP HUD behavior.
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "l4d2/hud/terror_hud_native_numeric.h"
#include "view_scene.h"
#include <vgui/ISurface.h>
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/ImagePanel.h>
#include <vgui_controls/CircularProgressBar.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
static ConVar z_frustration_blink_percent( "z_frustration_blink_percent", "0.75", 0x00004008 );
static ConVar z_frustration_blink_rate( "z_frustration_blink_rate", "10", 0x00004008 );

class CHudScope : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudScope, vgui::Panel );
public:
	CHudScope( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudScope" )
	{
		SetHiddenBits( 16 ); for ( int i = 0; i < 4; ++i ) m_iTexture[i] = 0;
	}
	virtual void Init() OVERRIDE
	{
		static const char *names[4] = { "vgui/HUD/scope_sniper_ul", "vgui/HUD/scope_sniper_ur", "vgui/HUD/scope_sniper_lr", "vgui/HUD/scope_sniper_ll" };
		for ( int i = 0; i < 4; ++i ) { m_iTexture[i] = vgui::surface()->CreateNewTextureID(); vgui::surface()->DrawSetTextureFile( m_iTexture[i], names[i], true, false ); }
	}
	virtual void VidInit() OVERRIDE { Init(); }
	virtual bool ShouldDraw() OVERRIDE
	{
		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
		CWeaponCSBase *weapon = player ? dynamic_cast<CWeaponCSBase *>( player->GetActiveWeapon() ) : NULL;
		return weapon && weapon->GetWeaponID() != 34 && weapon->HasScope() && player->GetFOV() != player->GetDefaultFOV() && CHudElement::ShouldDraw();
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		BaseClass::ApplySchemeSettings( scheme ); SetPaintBackgroundEnabled( false ); SetPaintBorderEnabled( false ); SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );
	}
	virtual void Paint() OVERRIDE
	{
		UpdateRefractTexture( true ); // Native C155CF0 -> C155C40, existing _rt_PowerOfTwoFB owner.
		const int wide = ScreenWidth(), tall = ScreenHeight(), cx = wide / 2, cy = tall / 2;
		int scopeWide = 4 * ( tall / 3 ), scopeTall = tall;
		if ( (double)wide * 0.75 > scopeWide ) scopeWide = scopeTall = TerrorHudFtol( (double)wide * 0.75 );
		const int left = cx - scopeWide / 2, right = cx + scopeWide / 2, top = cy - scopeTall / 2, bottom = cy + scopeTall / 2;
		const int rectangles[4][4] = { { left, top, cx, cy }, { cx - 1, top, right, cy + 1 }, { cx, cy, right, bottom }, { left, cy, cx, bottom } };
		vgui::surface()->DrawSetColor( 0, 0, 0, 255 );
		for ( int i = 0; i < 4; ++i )
		{
			const int *r = rectangles[i];
			vgui::Vertex_t vertices[4] = {
				vgui::Vertex_t( Vector2D( r[0], r[1] ), Vector2D( 0.001953125f, 0.001953125f ) ),
				vgui::Vertex_t( Vector2D( r[2], r[1] ), Vector2D( 0.998046875f, 0.001953125f ) ),
				vgui::Vertex_t( Vector2D( r[2], r[3] ), Vector2D( 0.998046875f, 0.998046875f ) ),
				vgui::Vertex_t( Vector2D( r[0], r[3] ), Vector2D( 0.001953125f, 0.998046875f ) ) };
			vgui::surface()->DrawSetTexture( m_iTexture[i] ); vgui::surface()->DrawTexturedPolygon( 4, vertices );
		}
		if ( scopeWide < wide ) { vgui::surface()->DrawFilledRect( 0, 0, left + 5, tall ); vgui::surface()->DrawFilledRect( right - 5, 0, wide, tall ); }
	}
private:
	int m_iTexture[4];
};
DECLARE_HUDELEMENT( CHudScope );

class CHudAbilityTimer : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudAbilityTimer, vgui::EditablePanel );
public:
	CHudAbilityTimer( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "CHudAbilityTimer" ), m_bActive( false ), m_pszBackground( NULL ), m_pszForeground( NULL )
	{
		SetHiddenBits( 16 ); m_pBackground = new vgui::ImagePanel( this, "BackgroundImage" ); m_pAbility = new vgui::ImagePanel( this, "AbilityImage" ); m_pProgress = new vgui::CircularProgressBar( this, "Progress" );
	}
	virtual void Reset() OVERRIDE { m_bActive = false; }
	virtual bool ShouldDraw() OVERRIDE
	{
		C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
		C_BaseAbility *ability = player ? player->GetCustomAbility() : NULL;
		if ( !ability ) return false;
		if ( player->IsAlive() && !player->IsGhost() && !player->IsThirdPersonCameraActive() )
		{
			const char *background = ability->GetCooldownBackground(), *foreground = ability->GetCooldownForeground();
			if ( !m_bActive || !m_pszBackground || !m_pszForeground || !background || !foreground || Q_stricmp( background, m_pszBackground ) || Q_stricmp( foreground, m_pszForeground ) )
			{
				m_pszBackground = background; m_pszForeground = foreground; m_bActive = true;
				if ( background ) m_pBackground->SetImage( background ); if ( foreground ) m_pAbility->SetImage( foreground );
			}
		}
		else m_bActive = false;
		return m_bActive && CHudElement::ShouldDraw();
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		LoadControlSettings( "Resource/UI/HUD/AbilityTimerHud.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme );
		m_pAbility->SetDrawColor( m_abilityCharging ); m_pProgress->SetProgressDirection( vgui::CircularProgressBar::PROGRESS_CCW ); m_pProgress->SetFgColor( m_abilityCharging );
	}
	virtual void Paint() OVERRIDE
	{
		C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ); C_BaseAbility *ability = player ? player->GetCustomAbility() : NULL; if ( !ability ) return;
		float progress = ability->GetRechargeProgress(); if ( progress > 1.0f ) progress = 1.0f; else if ( progress < 0.0f ) progress = 0.0f; m_pProgress->SetProgress( progress );
		const float suppression = ability->GetActivationSuppressionTimestamp();
		const Color color = suppression > 0.0f && !( gpGlobals->curtime >= suppression ) ? m_abilitySurpressed : ( !( progress < 1.0f ) ? m_abilityReady : m_abilityCharging );
		m_pProgress->SetFgColor( color ); m_pAbility->SetDrawColor( color );
	}
private:
	bool m_bActive; const char *m_pszBackground, *m_pszForeground;
	vgui::ImagePanel *m_pBackground, *m_pAbility; vgui::CircularProgressBar *m_pProgress;
	CPanelAnimationVar( Color, m_abilitySurpressed, "ability_surpressed_color", "127 127 127 255" );
	CPanelAnimationVar( Color, m_abilityCharging, "ability_charging_color", "127 127 127 255" );
	CPanelAnimationVar( Color, m_abilityReady, "ability_ready_color", "255 255 255 255" );
};
DECLARE_HUDELEMENT( CHudAbilityTimer );

// C294100/C2941A0. The orientation test and integer truncation are native.
static Color ScaleMeterColor( Color color, float scale )
{
	for ( int i = 0; i < 3; ++i ) { float component = (float)color[i] * scale; if ( component > 255.0f ) component = 255.0f; else if ( component < 0.0f ) component = 0.0f; color[i] = TerrorHudCvttFloat( component ); } return color;
}
static void PaintMeterRectangle( int x0, int y0, int x1, int y1, Color color, float fraction )
{
	const int wide = TerrorHudSub32( x1, x0 ), tall = TerrorHudSub32( y1, y0 );
	if ( wide <= tall ) y0 = TerrorHudCvttFloat( (float)( (float)( 1.0f - fraction ) * tall ) + y0 ); else x1 = TerrorHudCvttFloat( (float)( wide * fraction ) + x0 );
	vgui::surface()->DrawSetColor( color ); vgui::surface()->DrawFilledRect( x0, y0, x1, y1 );
	if ( wide > 2 && tall > 2 && color.a() == 255 )
	{
		vgui::surface()->DrawSetColor( ScaleMeterColor( color, 1.25f ) ); vgui::surface()->DrawLine( x0, y0, x1, y0 ); vgui::surface()->DrawLine( x0, y0, x0, y1 );
		vgui::surface()->DrawSetColor( ScaleMeterColor( color, 0.75f ) ); vgui::surface()->DrawLine( x0, y1, x1, y1 ); vgui::surface()->DrawLine( x1, y0, x1, y1 );
	}
}
class FinaleMeterBarPanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( FinaleMeterBarPanel, vgui::Panel );
public:
	FinaleMeterBarPanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_flFraction( 0.0f ), m_Color( 255, 255, 255, 255 ) {}
	float m_flFraction; Color m_Color;
protected:
	virtual void Paint() OVERRIDE { PaintMeterRectangle( 0, 0, GetWide(), GetTall(), m_Color, m_flFraction ); }
};
class CHudFinaleMeter : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudFinaleMeter, vgui::EditablePanel );
public:
	CHudFinaleMeter( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudFinaleMeter" ), m_bActive( false ), m_iRushes( 0 ), m_iRemaining( 0 ) { SetHiddenBits( 64 ); m_pBar = new FinaleMeterBarPanel( this, "RescueBar" ); LoadControlSettings( "Resource/UI/HUD/FinaleMeter.res", "GAME" ); }
	virtual void Reset() OVERRIDE { m_bActive = false; m_iRushes = m_iRemaining = 0; }
	// C2940F0 is unconditionally false. There is no native live HUD registration
	// of the four finale events: retain the public handler, do not invent a feed.
	virtual bool ShouldDraw() OVERRIDE { return false; }
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE
	{
		const char *name = event->GetName();
		if ( !Q_stricmp( name, "finale_start" ) ) { m_bActive = true; m_iRushes = m_iRemaining = MAX( 1, event->GetInt( "rushes" ) ); m_pBar->m_flFraction = (float)m_iRushes / (float)m_iRushes; }
		else if ( !Q_stricmp( name, "finale_rush" ) ) { m_iRemaining = TerrorHudSub32( m_iRemaining, 1 ); m_pBar->m_flFraction = (float)m_iRemaining / (float)m_iRushes; }
		else if ( !Q_stricmp( name, "finale_win" ) ) { m_iRemaining = 0; m_pBar->m_flFraction = 0.0f / (float)m_iRushes; }
		else if ( !Q_stricmp( name, "round_start" ) ) Reset();
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { LoadControlSettings( "Resource/UI/HUD/FinaleMeter.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme ); SetBgColor( scheme->GetColor( "TransparentLightBlack", Color( 0, 0, 0, 0 ) ) ); m_pBar->m_Color = scheme->GetColor( "BrightGray", Color( 255, 0, 0, 255 ) ); SetPaintBackgroundEnabled( true ); SetPaintEnabled( true ); SetBorder( NULL ); }
private:
	bool m_bActive; int m_iRushes, m_iRemaining; FinaleMeterBarPanel *m_pBar;
};
DECLARE_HUDELEMENT( CHudFinaleMeter );

class CFrustrationMeterBarPanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CFrustrationMeterBarPanel, vgui::Panel );
public:
	CFrustrationMeterBarPanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_flFraction( 0.0f ), m_Color( 255, 255, 255, 255 ) {}
	float m_flFraction; Color m_Color;
protected:
	virtual void Paint() OVERRIDE
	{
		vgui::surface()->DrawSetColor( m_Color ); vgui::surface()->DrawOutlinedRect( 0, 0, GetWide(), GetTall() );
		if ( m_bEastAligned ) PaintMeterRectangle( TerrorHudFtol( ( 1.0 - (double)m_flFraction ) * ( GetWide() - 3 ) + 2.0 ), 2, GetWide() - 3, GetTall() - 3, m_Color, 1.0f );
		else PaintMeterRectangle( 2, 2, GetWide() - 3, GetTall() - 3, m_Color, m_flFraction );
	}
	CPanelAnimationVar( bool, m_bEastAligned, "east_aligned", "1" );
};
class CHudFrustrationMeter : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudFrustrationMeter, vgui::EditablePanel );
public:
	CHudFrustrationMeter( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudFrustrationMeter" ) { SetHiddenBits( 64 ); SetProportional( true ); m_pBar = new CFrustrationMeterBarPanel( this, "FrustrationBar" ); LoadControlSettings( "Resource/UI/HUD/FrustrationMeter.res", "GAME" ); }
	virtual bool ShouldDraw() OVERRIDE
	{
		C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
		return player && (float)player->m_frustration > 0.0f && player->IsAlive() && player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 8 && player->m_burnPercent == 0.0f && !player->IsIncapacitated() && CHudElement::ShouldDraw();
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { LoadControlSettings( "Resource/UI/HUD/FrustrationMeter.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme ); }
	virtual void Paint() OVERRIDE
	{
		C_TerrorPlayer *player = CinematicViewPlayer(); if ( !player ) return;
		const float frustration = (float)player->m_frustration * 0.01f; m_pBar->m_flFraction = 1.0f - frustration; Color color( 255, 255, 255, 255 );
		if ( frustration >= z_frustration_blink_percent.GetFloat() ) { const float scale = (float)( ( sin( (double)z_frustration_blink_rate.GetFloat() * (double)gpGlobals->curtime ) + 1.0 ) * 0.5 ); for ( int i = 0; i < 3; ++i ) color[i] = TerrorHudCvttFloat( (float)color[i] * scale ); }
		m_pBar->m_Color = color;
	}
private:
	CFrustrationMeterBarPanel *m_pBar;
};
DECLARE_HUDELEMENT( CHudFrustrationMeter );
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
