//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/hud/terror_takeover_bar.h"
#include "l4d2/ui/terror_bindpanel.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "l4d2/player/c_terrorplayer.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Label.h"
#include "tier0/memdbgon.h"

// C2E63A0. Native IKeyBindingListener only writes unread field572; omitted
// under the standing write-only rule. Real CBindPanel/layout ownership remains.
CTakeoverBar::CTakeoverBar( IViewPort *viewport ) : BaseClass( viewport, "takeover_survivor_bar" ), m_character( 8 ), m_lastTarget( NULL )
{
	m_fireMessage = new vgui::Label( this, "FireMessage", "" );
	m_fireBind = new CBindPanel( this, "FireBind" );
	m_fireBind->SetParent( this );
}
void CTakeoverBar::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	LoadControlSettings( "Resource/UI/TakeoverSurvivorBar.res" );
	C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( local && local->GetTeamNumber() != 0 ) SetControlString( "SpectatorTitle", "#L4D_s_team_join_takeover_title" );
	if ( m_fireBind ) m_fireBind->SetBind( "+attack", 0 );
}
void CTakeoverBar::SetData( KeyValues *data )
{
	m_character = data->GetInt( "character", 0 );
	m_minimumShowTimer.Start( 1.0f );
}
bool CTakeoverBar::NeedsUpdate()
{
	C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !local ) return false;
	return ( local->IsAlive() && local->GetTeamNumber() == TEAM_SURVIVOR && local->GetSurvivorCharacter() == m_character ) || m_minimumShowTimer.IsElapsed() || CinematicViewPlayer() != m_lastTarget;
}
void CTakeoverBar::Update()
{
	C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !local ) return;
	C_TerrorPlayer *view = CinematicViewPlayer();
	if ( m_minimumShowTimer.IsElapsed() && ( local->IsAlive() || !view || ( !view->IsBot() && view != local ) ) ) Close();
	if ( view != m_lastTarget )
	{
		wchar_t text[256];
		if ( view == local )
		{
			const wchar_t *format = g_pVGuiLocalize->Find( "#L4D_Zombie_UI_Press_Fire_To_Play" );
			if ( !format ) return; // Skip only native missing-format copy.
			V_wcsncpy( text, format, sizeof( text ) );
		}
		else
		{
			if ( !view ) return; // Native stale/null view prerequisite only.
			wchar_t name[256]; GetTerrorHudPlayerName( view, name, sizeof( name ) );
			const wchar_t *format = g_pVGuiLocalize->Find( "#L4D_s_bot_control_subtitle" );
			if ( !format ) return;
			g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 1, name );
		}
		SetControlString( "FireMessage", text ); m_lastTarget = view; InvalidateLayout( false, false );
	}
}
void CTakeoverBar::ShowPanel( bool state )
{
	if ( BaseClass::IsVisible() == state ) return;
	SetVisible( state ); SetMouseInputEnabled( false ); SetKeyBoardInputEnabled( false ); Update();
}
bool CTakeoverBar::IsVisible()
{
	static ConVarRef menu( "gameui_ingame_menu_active" );
	return !menu.GetBool() && BaseClass::IsVisible();
}
void CTakeoverBar::PerformLayout()
{
	BaseClass::PerformLayout();
	if ( !m_fireBind || !m_fireMessage ) return;
	m_fireBind->InvalidateLayout( true, false );
	int bx, by, bw, bh, tx, ty, tw, th, contentWidth, contentHeight;
	m_fireBind->GetBounds( bx, by, bw, bh ); m_fireMessage->GetBounds( tx, ty, tw, th );
	m_fireMessage->GetContentSize( contentWidth, contentHeight );
	if ( contentWidth > tw ) { m_fireMessage->SetBounds( tx, tw, contentWidth, th ); m_fireMessage->InvalidateLayout( false, false ); } // Native temporary Y=old width, restored below.
	int left = MAX( 0, ( GetWide() - contentWidth - bw ) / 2 );
	m_fireBind->SetPos( left, by ); m_fireMessage->SetPos( left + bw, ty );
}
