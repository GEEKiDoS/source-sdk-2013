//========= Copyright Valve Corporation, All rights reserved. ============//
// Native CHudScriptedMode C2BD000..2BE218; real received rules arrays only.
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/ui/terror_client_panels.h"
#include "c_recipientfilter.h"
#include "engine/IEngineSound.h"
#include "l4d2/hud/terror_hud_round_timers.h"
#include "l4d2/hud/terror_hud_native_numeric.h"
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/AnimationController.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
class CHudScriptedMode : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudScriptedMode, vgui::EditablePanel );
public:
	CHudScriptedMode( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudScriptedMode" ) { SetHiddenBits( 0 ); SetPaintBackgroundEnabled( false ); for ( int i = 0; i < 15; ++i ) { m_labels[i] = NULL; m_survivalFont[i] = m_shouldBlink[i] = false; m_blink[i] = 0; m_blinkStart[i] = 0.0f; } }
	virtual void Init() OVERRIDE { ListenForGameEvent( "round_start" ); ListenForGameEvent( "round_end" ); ListenForGameEvent( "player_connect_full" ); ListenForGameEvent( "scriptedmode_reloadhud" ); }
	virtual bool ShouldDraw() OVERRIDE { return TerrorGameRules() && TerrorGameRules()->IsScriptedModeActive(); }
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE { if ( !Q_stricmp( event->GetName(), "round_start" ) ) InvalidateLayout( true, true ); else if ( !Q_stricmp( event->GetName(), "player_connect_full" ) ) { C_BasePlayer *p = UTIL_PlayerByUserId( event->GetInt( "userid" ) ); if ( p && p->IsLocalPlayer() ) InvalidateLayout( true, true ); } /* Native reload flag439 is write-only; round_end is a genuine empty branch. */ }
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		LoadControlSettings( "Resource/UI/HUD/HudScriptedMode.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme ); SetFgColor( GetSchemeColor( "FgColor", scheme ) ); SetBgColor( GetSchemeColor( "BgColor", scheme ) ); m_numberFont = scheme->GetFont( "HudNumbers", false ); m_survivalNumberFont = scheme->GetFont( "SurvivalNumbers", false ); for ( int i = 0; i < 15; ++i ) { char name[16]; Q_snprintf( name, sizeof( name ), "Elem%02dData", i ); m_labels[i] = dynamic_cast<vgui::Label *>( FindChildByName( name ) ); if ( m_labels[i] ) { m_survivalFont[i] = true; m_labels[i]->SetFont( m_survivalNumberFont ); } }
	}
	virtual void OnThink() OVERRIDE
	{
		CTerrorGameRules *rules = TerrorGameRules(); if ( !rules || !rules->IsScriptedModeActive() ) return; C_BasePlayer *local = C_BasePlayer::GetLocalPlayer(); const int team = local ? local->GetTeamNumber() : 0;
		for ( int i = 0; i < 15; ++i )
		{
			vgui::Label *label = m_labels[i]; if ( !label ) continue; // Only native NULL-dependent work is skipped.
			const int flags = rules->GetScriptedHUDFlags( i ), type = flags & 0x3000, teamFlags = flags & 0xC00; bool show = !( flags & 0x4000 ); if ( teamFlags && ( ( team == TEAM_SURVIVOR && teamFlags != 0x400 ) || ( team == TEAM_INFECTED && teamFlags != 0x800 ) ) ) show = false;
			if ( show != label->IsVisible() ) { label->SetVisible( show ); if ( !show || !( flags & 0x40 ) ) { char name[16]; Q_snprintf( name, sizeof( name ), "Elem%02dIm", i ); vgui::Panel *image = FindChildByName( name ); if ( image ) { image->SetVisible( show ); Q_snprintf( name, sizeof( name ), "Elem%02dFl", i ); vgui::Panel *fill = FindChildByName( name ); if ( fill ) fill->SetVisible( show ); } } }
			if ( !show ) continue; const char *data = DataString( i, flags ); if ( flags & 3 ) { SetSurvivalFont( i, false ); char text[256]; if ( flags & 1 ) Q_snprintf( text, sizeof( text ), "%s%s", rules->GetScriptedHUDString( i ), data ); else Q_snprintf( text, sizeof( text ), "%s%s", data, rules->GetScriptedHUDString( i ) ); label->SetText( text ); } else { SetSurvivalFont( i, type == 0 || type == 0x1000 ); label->SetText( data ); }
			const int align = flags & 0x300; if ( align ) label->SetContentAlignment( align == 0x100 ? vgui::Label::a_west : ( align == 0x300 ? vgui::Label::a_east : vgui::Label::a_center ) ); AdjustLayout( i, flags );
			if ( m_blink[i] ) { if ( !m_shouldBlink[i] && !( flags & 8 ) ) StopBlink( i ); } else if ( m_shouldBlink[i] || ( flags & 8 ) ) { m_blink[i] = 1; m_blinkStart[i] = gpGlobals->curtime; } UpdateBlink( i, flags );
		}
	}
private:
	void SetSurvivalFont( int i, bool survival ) { if ( survival != m_survivalFont[i] ) { m_labels[i]->SetFont( survival ? m_survivalNumberFont : m_numberFont ); m_survivalFont[i] = survival; } }
	void Sound( const char *name ) { CLocalPlayerFilter filter; C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, name ); }
	const char *TimeString( float time ) { int minutes, seconds, hundredths; SplitTerrorSurvivalTime( time, minutes, seconds, hundredths ); Q_snprintf( m_scratch, sizeof( m_scratch ), "%02d:%02d", minutes, seconds ); return m_scratch; }
	const char *DataString( int i, int flags )
	{
		CTerrorGameRules *rules = TerrorGameRules(); m_shouldBlink[i] = false; switch ( flags & 0x3000 )
		{
		case 0: Q_snprintf( m_scratch, sizeof( m_scratch ), "%d", rules->GetScriptedHUDInt( i ) ); return m_scratch;
		case 0x1000: if ( flags & 0x10 ) return TimeString( rules->GetScriptedHUDFloat( i ) ); Q_snprintf( m_scratch, sizeof( m_scratch ), "%f", (double)rules->GetScriptedHUDFloat( i ) ); return m_scratch;
		case 0x2000: return rules->GetScriptedHUDString( i );
		case 0x3000:
			{
				const int special = rules->GetScriptedHUDInt( i ); if ( special >= 0 && special <= 3 ) { if ( !rules->GetScriptedHUDTimerMode( special ) ) return "--:--"; const double wide = rules->ReadScriptedHUDTimer( special ); float time = (float)wide; if ( !( wide < 0.0 ) || ( flags & 0x80 ) ) { if ( time > 0.0f && time < 10.0f && ( flags & 0x20 ) ) m_shouldBlink[i] = true; } else time = 0.0f; return TimeString( time ); }
				if ( special == 4 ) { const float end = rules->GetHoldoutCooldownEndTime(); if ( end == 0.0f || !( end >= gpGlobals->curtime ) ) { if ( m_blink[i] ) { StopBlink( i ); Sound( "Scavenge.zero_time" ); } return "Not in Cooldown"; } const float time = end - gpGlobals->curtime; if ( time > 0.0f && time < 10.0f && ( flags & 0x20 ) ) m_shouldBlink[i] = true; return TimeString( time ); }
				if ( special == 5 ) return TimeString( gpGlobals->curtime - rules->CCSGameRules::GetRoundStartTime() ); // Native base m_fLevelStartTime40, NOT derived round272.
				if ( special == 6 ) { KeyValues *chapter = TerrorClient_GetTransitionChapterInfo( false, NULL ); return chapter ? chapter->GetString( "map", "" ) : "<MapName?>"; }
				if ( special == 7 ) { static ConVarRef mode( "mp_gamemode" ); return mode.IsFlagSet( FCVAR_NEVER_AS_STRING ) ? "FCVAR_NEVER_AS_STRING" : mode.GetString(); } return "Unknown Special";
			}
		}
		DevWarning( "How did you add a non-existant HUD Element (slot %d flags %x)?!?\n", i, flags ); return "";
	}
	void AdjustLayout( int i, int flags )
	{
		CTerrorGameRules *rules = TerrorGameRules(); const float x = rules->GetScriptedHUDPosX( i ), y = rules->GetScriptedHUDPosY( i ), wide = rules->GetScriptedHUDWidth( i ), tall = rules->GetScriptedHUDHeight( i ); if ( x == 0.0f && y == 0.0f && wide == 0.0f && tall == 0.0f ) return; vgui::Panel *parent = m_labels[i]->GetParent(); if ( !parent ) return; int oldX, oldY, oldW, oldH; m_labels[i]->GetBounds( oldX, oldY, oldW, oldH ); const int nx = TerrorHudCvttFloat( (float)parent->GetWide() * x ), ny = TerrorHudCvttFloat( (float)parent->GetTall() * y ), nw = TerrorHudCvttFloat( (float)parent->GetWide() * wide ), nh = TerrorHudCvttFloat( (float)parent->GetTall() * tall ); if ( nx == oldX && ny == oldY && nw == oldW && nh == oldH ) return; m_labels[i]->SetBounds( nx, ny, nw, nh ); if ( flags & 0x40 ) return;
		char name[16]; Q_snprintf( name, sizeof( name ), "Elem%02dIm", i ); vgui::Panel *image = FindChildByName( name ); if ( image ) { OffsetPanel( image, TerrorHudSub32( nx, oldX ), TerrorHudSub32( ny, oldY ), TerrorHudSub32( nw, oldW ), TerrorHudSub32( nh, oldH ) ); Q_snprintf( name, sizeof( name ), "Elem%02dFl", i ); vgui::Panel *fill = FindChildByName( name ); if ( fill ) OffsetPanel( fill, TerrorHudSub32( nx, oldX ), TerrorHudSub32( ny, oldY ), TerrorHudSub32( nw, oldW ), TerrorHudSub32( nh, oldH ) ); }
	}
	void OffsetPanel( vgui::Panel *panel, int dx, int dy, int dw, int dh ) { int x, y, w, h; panel->GetBounds( x, y, w, h ); panel->SetBounds( TerrorHudAdd32( x, dx ), TerrorHudAdd32( y, dy ), TerrorHudAdd32( w, dw ), TerrorHudAdd32( h, dh ) ); }
	void StopBlink( int i ) { vgui::GetAnimationController()->CancelAllAnimations(); m_labels[i]->SetFgColor( Color( 255, 255, 255, 255 ) ); m_blink[i] = 0; m_blinkStart[i] = 0.0f; }
	void UpdateBlink( int i, int flags ) { if ( !m_blink[i] ) return; if ( TerrorHudCvttFloat( ( gpGlobals->curtime - m_blinkStart[i] ) * 2.0f ) % 2 ) { if ( m_blink[i] != -1 ) { vgui::GetAnimationController()->RunAnimationCommand( m_labels[i], "FgColor", Color( 255, 255, 255, 255 ), 0.0f, 0.5f, vgui::AnimationController::INTERPOLATOR_ACCEL, 0.1f ); m_blink[i] = -1; } } else if ( m_blink[i] != 1 ) { vgui::GetAnimationController()->RunAnimationCommand( m_labels[i], "FgColor", Color( 255, 0, 0, 255 ), 0.0f, 0.5f, vgui::AnimationController::INTERPOLATOR_LINEAR ); m_blink[i] = 1; if ( flags & 4 ) Sound( "Survival.countdown" ); } }
	vgui::Label *m_labels[15]; bool m_survivalFont[15], m_shouldBlink[15]; int m_blink[15]; float m_blinkStart[15]; vgui::HFont m_numberFont, m_survivalNumberFont; char m_scratch[128];
};
DECLARE_HUDELEMENT( CHudScriptedMode );
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
