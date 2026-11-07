//========= Copyright Valve Corporation, All rights reserved. ============//
// Native C2C5BE0..2C7B47. CTargetID's normal-center branch is unreachable:
// identical rangefinder predicate admits ShouldDraw and selects debug Paint.
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/player/bots/c_survivor_bot.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "l4d2/effects/terror_glow_policy.h"
#include "l4d2/effects/terror_glow_player.h"
#include "l4d2/hud/terror_hud_native_numeric.h"
#include "l4d2/hud/terror_client_colors.h"
#include "l4d2/hud/terror_health_panel.h"
#include "view.h"
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/AnimationController.h>
#include "tier1/utlvector.h"
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
static ConVar hud_targetid_rangefinder( "hud_targetid_rangefinder", "", 0x00004008, "shows range for different masks" );
static ConVar hud_targetid_health( "hud_targetid_health", "0", 0x0000000A );
static ConVar hud_targetid_name_height( "hud_targetid_name_height", "12", 0x0000000A, "Player names are this many inches over player eye positions" );
static ConVar hud_targetid_name_height_boomer( "hud_targetid_name_height_boomer", "12", 0x0000000A, "Player names are this many inches over player eye positions" );
static ConVar hud_targetid_name_height_tank( "hud_targetid_name_height_tank", "12", 0x0000000A, "Player names are this many inches over player eye positions" );
static ConVar hud_targetid_name_height_smoker( "hud_targetid_name_height_smoker", "25", 0x0000000A, "Player names are this many inches over player eye positions" );

class CTargetID : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CTargetID, vgui::EditablePanel );
public:
	CTargetID( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "TargetID" ), m_bShown( true ) { SetHiddenBits( 64 ); LoadControlSettings( "Resource/UI/HUD/TargetID.res", "GAME" ); g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( "TargetIDNormal" ); }
	virtual bool ShouldDraw() OVERRIDE { if ( hud_targetid_rangefinder.IsFlagSet( FCVAR_NEVER_AS_STRING ) || *hud_targetid_rangefinder.GetString() ) return CHudElement::ShouldDraw(); if ( m_bShown ) { SetAlpha( 0 ); m_bShown = false; } return false; }
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { LoadControlSettings( "Resource/UI/HUD/TargetID.res", "GAME" ); m_bShown = true; g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( "TargetIDNormal" ); BaseClass::ApplySchemeSettings( scheme ); m_hFont = scheme->GetFont( "TargetID", true ); SetPaintBackgroundEnabled( false ); }
	virtual void Paint() OVERRIDE
	{
		C_BasePlayer *local = C_BasePlayer::GetLocalPlayer(); if ( !local ) return;
		const char *maskName = hud_targetid_rangefinder.GetString(); if ( hud_targetid_rangefinder.IsFlagSet( FCVAR_NEVER_AS_STRING ) ) maskName = "FCVAR_NEVER_AS_STRING";
		if ( !maskName || !*maskName ) return; // Native sole admitted paint path.
		unsigned int mask = 33579073; if ( !Q_stricmp( maskName, "MASK_SURVIVORSOLID" ) ) mask = 33636875; else if ( !Q_stricmp( maskName, "TONGUE_TARGETING_MASK" ) ) mask = 1107320961; else if ( !Q_stricmp( maskName, "TONGUE_MAINTAINING_MASK" ) ) mask = 33579139; else if ( !Q_stricmp( maskName, "TONGUE_TRAVELING_MASK" ) ) mask = 33579011; else if ( !Q_stricmp( maskName, "MASK_NPCSOLID" ) ) mask = 33701899; else if ( !Q_stricmp( maskName, "OPAQUE" ) ) mask = 33579137;
		const Vector start = MainViewForward() * 0.0f + MainViewOrigin(), end = MainViewForward() * 25000.0f + MainViewOrigin(); trace_t trace; UTIL_TraceLine( start, end, mask, C_CSPlayer::GetViewCSPlayer(), COLLISION_GROUP_NONE, &trace ); const float distance = trace.fraction > 0.0f ? ( trace.endpos - trace.startpos ).Length() : 0.0f;
		char ansi[64]; wchar_t text[256]; Q_snprintf( ansi, sizeof( ansi ), " %0.0f ", distance ); g_pVGuiLocalize->ConvertANSIToUnicode( ansi, text, sizeof( text ) ); int wide, tall; vgui::surface()->GetTextSize( m_hFont, text, wide, tall ); vgui::surface()->DrawSetTextFont( m_hFont ); vgui::surface()->DrawSetTextPos( ( ScreenWidth() - wide ) / 2, TerrorHudFtol( (double)ScreenHeight() * 0.002083333333333333 * 260.0 ) ); vgui::surface()->DrawSetTextColor( 192, 192, 192, 255 ); vgui::surface()->DrawPrintText( text, wcslen( text ) );
		vgui::Label *label = dynamic_cast<vgui::Label *>( FindChildByName( "TargetIDLabel" ) ); if ( label ) { label->SetText( text ); label->SizeToContents(); if ( !m_bShown ) { SetAlpha( 255 ); m_bShown = true; } }
	}
private:
	bool m_bShown; vgui::HFont m_hFont;
};
DECLARE_HUDELEMENT( CTargetID );

class CPlayerLabel : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CPlayerLabel, vgui::EditablePanel );
public:
	CPlayerLabel( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "PlayerLabel" ), m_iPlayer( 0 ), m_flLastTarget( 0.0f ) { SetHiddenBits( 64 ); RefreshFormats(); }
	virtual void Reset() OVERRIDE { m_iPlayer = 0; m_flLastTarget = 0.0f; }
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { SetBounds( 0, 0, ScreenWidth(), ScreenHeight() ); BaseClass::ApplySchemeSettings( scheme ); SetPaintBackgroundEnabled( false ); m_hFont = scheme->GetFont( "TargetID", true ); RefreshFormats(); }
	virtual void Paint() OVERRIDE
	{
		C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
		if ( !local )
			return;

		static ConVarRef menu( "gameui_ingame_menu_active" );
		if ( menu.GetBool() || local->GetViewEntity() || sv_disable_glow_survivors.GetBool() )
			return;
		// The shipping native builds and destroys an EMPTY temporary vector before
		// this target query. It has no label iteration or side-effecting callback.
		int index = local->GetIDTarget(); if ( !local->IsAlive() && local->GetObserverTarget() ) index = local->GetObserverTarget()->entindex(); if ( local->GetGlowUseEntityIndex() ) index = local->GetGlowUseEntityIndex();
		if ( index ) m_flLastTarget = gpGlobals->curtime; else if ( m_flLastTarget != 0.0f && gpGlobals->curtime > m_flLastTarget + 0.5f ) { m_flLastTarget = 0.0f; m_iPlayer = 0; return; } else index = m_iPlayer;
		if ( index >= 1 && index <= gpGlobals->maxClients ) { C_TerrorPlayer *target = ToTerrorPlayer( UTIL_PlayerByIndex( index ) ); if ( !target ) { m_iPlayer = 0; return; } PaintPlayer( local, target, 255 ); m_iPlayer = index; }
	}
private:
	void RefreshFormats()
	{
		static const char *const tokens[] = { "#L4D_TargetID_Player_Health", "#L4D_TargetID_Player" };
		for ( int i = 0; i < ARRAYSIZE( m_Formats ); ++i )
		{
			// Native 2C7410 owns both strings; Find returns borrowed table storage.
			m_Formats[i].RemoveAll();
			const wchar_t *format = g_pVGuiLocalize->Find( tokens[i] );
			if ( format )
				m_Formats[i].AddMultipleToTail( wcslen( format ) + 1, format );
		}
	}
	void PaintPlayer( C_TerrorPlayer *local, C_TerrorPlayer *target, int alpha )
	{
		if ( !( gpGlobals->curtime >= local->m_itTimer.m_timestamp ) ) { const float scale = TerrorGlow_GetITCompositeScale( *local ); if ( scale <= 0.0f ) return; alpha = TerrorHudCvttFloat( (float)alpha * scale ); }
		if ( !target->InSameTeam( local ) && ( local->GetTeamNumber() == TEAM_SURVIVOR || local->GetTeamNumber() == TEAM_INFECTED ) ) return;
		Color color = GetClientColor( target->entindex() ); color[3] = alpha;
		wchar_t name[32], text[256], healthText[32]; GetTerrorHudPlayerName( target, name, sizeof( name ) ); const wchar_t *display = name; wchar_t idleText[100]; C_SurvivorBot *bot = dynamic_cast<C_SurvivorBot *>( target );
		if ( bot && bot->GetTeamNumber() == TEAM_SURVIVOR && bot->GetHumanSpectatorUserID() > 0 ) { const wchar_t *idle = g_pVGuiLocalize->Find( "L4D_Scoreboard_Idle" ); if ( idle ) { V_snwprintf( idleText, ARRAYSIZE( idleText ), L"%ls: %ls", idle, name ); display = idleText; } }
		Vector position = target->GetRenderOrigin(); position.z += target->m_flCurrentEyeHeight; float height = hud_targetid_name_height.GetFloat(); if ( target->GetTeamNumber() == TEAM_INFECTED ) { switch ( target->GetZombieClass() ) { case 1: case 4: height = hud_targetid_name_height_smoker.GetFloat(); break; case 2: case 3: case 5: height = hud_targetid_name_height_boomer.GetFloat(); break; case 6: case 8: height = hud_targetid_name_height_tank.GetFloat(); break; } } else if ( local->GetTeamNumber() == TEAM_INFECTED ) color[3] = TerrorHudCvttFloat( (float)color.a() * target->m_GlowView[0].m_flAlpha ); position += CurrentViewUp() * height;
		int x, y; if ( !GetVectorInScreenSpace( position, x, y ) ) return; const wchar_t *format = m_Formats[hud_targetid_health.GetBool() ? 0 : 1].Base(); if ( !format ) return; if ( hud_targetid_health.GetBool() ) { V_snwprintf( healthText, ARRAYSIZE( healthText ), L"%d", target->GetHealth() ); g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 2, display, healthText ); } else g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 1, display );
		int wide, tall; vgui::surface()->GetTextSize( m_hFont, text, wide, tall ); x = TerrorHudSub32( x, wide / 2 ); y = TerrorHudSub32( y, tall / 2 ); vgui::surface()->DrawSetTextFont( m_hFont ); vgui::surface()->DrawSetTextPos( TerrorHudAdd32( x, 1 ), TerrorHudAdd32( y, 1 ) ); vgui::surface()->DrawSetTextColor( 0, 0, 0, color.a() ); vgui::surface()->DrawPrintText( text, wcslen( text ) ); vgui::surface()->DrawSetTextPos( x, y ); vgui::surface()->DrawSetTextColor( color ); vgui::surface()->DrawPrintText( text, wcslen( text ) );
		float permanent = 0.0f, temporary = 0.0f; bool progress = false; Color bar( 255, 255, 255, 255 );
		if ( ( target->m_iCurrentUseAction == 10 || target->m_iCurrentUseAction == 11 ) && target->m_flProgressBarDuration > 0.0f ) { permanent = ( target->GetSimulationTime() - target->m_flProgressBarStartTime ) / target->m_flProgressBarDuration; if ( permanent > 1.0f ) permanent = 1.0f; else if ( permanent < 0.0f ) permanent = 0.0f; progress = true; }
		CWeaponCSBase *weapon = dynamic_cast<CWeaponCSBase *>( local->GetActiveWeapon() );
		if ( !target->IsIncapacitated() && target->GetTeamNumber() == TEAM_SURVIVOR && weapon && ( weapon->GetWeaponID() == 12 || weapon->GetWeaponID() == 15 || weapon->GetWeaponID() == 23 ) ) { permanent = (float)target->GetHealth() * 0.01f; temporary = target->GetHealthBuffer() * 0.01f; float ratio = (float)target->GetHealth() / (float)target->GetMaxHealth(); if ( ratio > 1.0f ) ratio = 1.0f; else if ( ratio < 0.0f ) ratio = 0.0f; static ConVarRef limp( "survivor_limp_health" ); const int level = target->m_isGoingToDie && target->GetHealth() <= 1 ? 0 : ( ratio < 0.25f ? 1 : ( !( limp.GetFloat() * 0.01f > ratio ) ? 3 : 2 ) ); bar = TerrorHealthPalette( level ); } else if ( !progress ) return;
		const int barTall = MAX( tall >> 2, 6 ), barWide = MAX( 15 * barTall, 104 ); float filled = permanent * (float)barWide; if ( filled < 2.0f ) filled = 2.0f; const int fill = TerrorHudCvttFloat( filled ), temp = TerrorHudCvttFloat( temporary * (float)barWide ), top = TerrorHudAdd32( y, tall ), bottom = TerrorHudAdd32( top, barTall ), end = TerrorHudAdd32( x, fill ), tempEnd = TerrorHudAdd32( end, temp ); vgui::surface()->DrawSetColor( bar ); vgui::surface()->DrawOutlinedRect( x, top, TerrorHudAdd32( x, barWide ), bottom ); vgui::surface()->DrawFilledRect( TerrorHudAdd32( x, 2 ), TerrorHudAdd32( top, 2 ), TerrorHudSub32( end, 2 ), TerrorHudSub32( bottom, 2 ) ); for ( int start = end; start < tempEnd; start = TerrorHudAdd32( start, 4 ) ) vgui::surface()->DrawFilledRect( start, TerrorHudAdd32( top, 2 ), TerrorHudAdd32( start, 2 ), TerrorHudSub32( bottom, 2 ) );
	}
	int m_iPlayer; float m_flLastTarget; vgui::HFont m_hFont; CUtlVector< wchar_t > m_Formats[2];
};
DECLARE_HUDELEMENT( CPlayerLabel );
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
