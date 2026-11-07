//========= Copyright Valve Corporation, All rights reserved. ============//
// Native C2DD8A0..2DDF51; existing viewport and player-resource owners only.
#include "cbase.h"
#include "l4d2/hud/terror_ready_countdown.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/hud/terror_hud_native_numeric.h"
#include <vgui_controls/Label.h>
#include <vgui_controls/ImagePanel.h>
#include <vgui_controls/ScalableImagePanel.h>
#include "tier0/memdbgon.h"
CReadyCountdown::CReadyCountdown( IViewPort *viewport ) : BaseClass( viewport, "ready_countdown" ), m_background( NULL ), m_fill( NULL ), m_seconds1( NULL ), m_seconds2( NULL ), m_countdown( NULL ) {}
void CReadyCountdown::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/ReadyCountdown.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme );
	m_background = dynamic_cast<vgui::ScalableImagePanel *>( FindChildByName( "Background" ) ); m_fill = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "BackgroundFill" ) ); m_seconds1 = dynamic_cast<vgui::Label *>( FindChildByName( "Seconds_1" ) ); m_seconds2 = dynamic_cast<vgui::Label *>( FindChildByName( "Seconds_2" ) ); m_countdown = dynamic_cast<vgui::Label *>( FindChildByName( "Countdown" ) );
	if ( m_background && m_fill && m_seconds1 && m_seconds2 )
	{
		const int oldWide = m_seconds1->GetWide(); m_seconds1->SizeToContents(); m_seconds2->SizeToContents(); const int delta = TerrorHudSub32( MAX( m_seconds1->GetWide(), m_seconds2->GetWide() ), oldWide ); int bx, by, bw, bh; m_background->GetBounds( bx, by, bw, bh ); const int wide = TerrorHudAdd32( bw, delta ); m_background->SetBounds( bx, by, wide, bh ); m_fill->SetBounds( bx, by, wide, bh ); SetWide( wide ); if ( GetParent() ) { int x, y; GetPos( x, y ); SetPos( TerrorHudSub32( GetParent()->GetWide(), GetWide() ) / 2, y ); }
	}
}
void CReadyCountdown::Update()
{
	C_TerrorPlayerResource *resource = dynamic_cast<C_TerrorPlayerResource *>( g_PR ); if ( !resource ) return;
	const bool visible = !resource->IsSurvivorTeamReadyTime() && resource->GetRoundSetupTimeRemaining() >= 0; vgui::Panel *panels[] = { m_background, m_fill, m_seconds1, m_seconds2, m_countdown }; for ( int i = 0; i < ARRAYSIZE( panels ); ++i ) if ( panels[i] && panels[i]->IsVisible() != visible ) panels[i]->SetVisible( visible );
	if ( visible && m_countdown ) { char text[16]; Q_snprintf( text, sizeof( text ), "%d", resource->GetRoundSetupTimeRemaining() ); m_countdown->SetText( text ); }
}
void CReadyCountdown::ShowPanel( bool state )
{
	if ( state == IsVisible() ) return; SetVisible( state ); SetMouseInputEnabled( false ); SetKeyBoardInputEnabled( false ); if ( state ) InvalidateLayout( false, true );
}
