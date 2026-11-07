//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/hud/terror_versus_progress.h"
#include "hud.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "vgui/ISurface.h"
#include "vgui_controls/MessageMap.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"
DECLARE_BUILD_FACTORY( CVersusModeLevelProgressBar );
CVersusModeLevelProgressBar::CVersusModeLevelProgressBar( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_playerSlot( -1 ), m_skull( NULL ), m_external( false ), m_highlight( true ) {}
void CVersusModeLevelProgressBar::SetProgressData( const int records[4][2], bool highlightLocal )
{
	Q_memcpy( m_progress, records, sizeof( m_progress ) ); m_external = true; m_highlight = highlightLocal;
}
void CVersusModeLevelProgressBar::ApplySchemeSettings( vgui::IScheme *scheme ) { BaseClass::ApplySchemeSettings( scheme ); m_skull = gHUD.GetIcon( "icon_skull" ); }
void CVersusModeLevelProgressBar::ApplySettings( KeyValues *settings ) { BaseClass::ApplySettings( settings ); m_skull = gHUD.GetIcon( "icon_skull" ); }
#pragma float_control( precise, on, push )
void CVersusModeLevelProgressBar::PaintBackground()
{
	CTerrorGameRules *rules = TerrorGameRules();
	if ( !m_external && rules ) for ( int i = 0; i < 4; ++i )
	{
		m_progress[i][0] = rules->GetVersusDistancePerSurvivor( i, 2 );
		m_progress[i][1] = rules->GetVersusSurvivorDeathDistance( i, 2 );
	}
	bool local[4] = { false,false,false,false };
	if ( m_highlight && ( m_playerSlot == -1 || m_playerSlot == 0 ) )
	{
		C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
		if ( player && player->GetTeamNumber() == 2 ) { const int character = player->GetSurvivorCharacter(); if ( character >= 0 && character < 4 ) local[character] = true; }
	}
	const float denominator = rules ? float( rules->GetVersusMaxCompletionScore() >> 2 ) : 0.0f; // Missing native denominator skips only dependent fill/skulls.
	for ( int i = 0; i < 4; ++i )
	{
		const int y = m_barY + i * ( m_barH + m_gap );
		vgui::surface()->DrawSetColor( m_backgroundColor ); vgui::surface()->DrawFilledRect( m_barX, y, m_barX + m_barW, y + m_barH );
		if ( denominator == 0.0f ) continue;
		vgui::surface()->DrawSetColor( local[i] ? m_localColor : m_barColor );
		float width = float( m_barW ) * ( float( m_progress[i][0] ) / denominator ); if ( width < 2.0f ) width = 2.0f;
		vgui::surface()->DrawFilledRect( m_barX, y, m_barX + _mm_cvttss_si32( _mm_set_ss( width ) ), y + m_barH );
	}
	if ( denominator != 0.0f && m_skull ) for ( int i = 0; i < 4; ++i ) if ( m_progress[i][1] >= 0 )
	{
		const float x = ( float( m_progress[i][1] ) / denominator ) * float( m_barW ) + float( m_barX );
		m_skull->DrawSelf( _mm_cvttss_si32( _mm_set_ss( x ) ) - ( m_skullSize >> 1 ), m_barY + m_skullY + i * ( m_barH + m_gap ), m_skullSize, m_skullSize, Color(255,255,255,255) );
	}
}
#pragma float_control( pop )
