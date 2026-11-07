//========= Copyright Valve Corporation, All rights reserved. ============//
// C2DEF30/C2DF0B0/C2DFB40/C2DFBB0 and C2EF440/C2EDA10/C2EE6E0/C2EE730.
#include "cbase.h"
#include "l4d2/hud/terror_embedded_scoreboards.h"
#include "l4d2/ui/terror_legacy_controls.h"
#include "l4d2/hud/terror_versus_progress.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/ui/terror_client_panels.h"
#include "l4d2/hud/terror_hud_native_numeric.h"
#include <vgui/IVGui.h>
#include <vgui/IPanel.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Label.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
DECLARE_BUILD_FACTORY( CScavengeModeEmbeddedScoreboard );
DECLARE_BUILD_FACTORY( CVersusModeEmbeddedScoreboard );
CScavengeModeEmbeddedScoreboard::CScavengeModeEmbeddedScoreboard( vgui::Panel *parent, const char *name ) : BaseClass( parent, name )
{
	for ( int i = 0; i < 5; ++i ) { char field[32]; Q_snprintf( field, sizeof( field ), "Round%dPanel", i + 1 ); m_rounds[i] = new CScavengeRoundPanel( this, field ); } m_final = new CScavengeRoundPanel( this, "FinalScorePanel" ); vgui::ivgui()->AddTickSignal( GetVPanel(), 250 );
}
void CScavengeModeEmbeddedScoreboard::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/ScavengeModeEmbeddedScoreboard.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme ); SetPaintBackgroundEnabled( false ); SetPaintBorderEnabled( false ); for ( int i = 0; i < 5; ++i ) m_rounds[i]->MakeReadyForUse(); m_final->MakeReadyForUse(); UpdateScores();
}
void CScavengeModeEmbeddedScoreboard::OnTick() { if ( vgui::ipanel()->IsFullyVisible( GetVPanel() ) ) UpdateScores(); }
void CScavengeModeEmbeddedScoreboard::UpdateScores()
{
	CTerrorGameRules *rules = TerrorGameRules(); C_TerrorPlayerResource *r = dynamic_cast<C_TerrorPlayerResource *>( g_PR ); C_BasePlayer *local = C_BasePlayer::GetLocalPlayer(); if ( !rules || !r ) return; const int team = r->GetTeam( local ? local->entindex() : 0 ), otherTeam = ( team == TEAM_SURVIVOR ) + 2, firstID = rules->GetScavengeTeamIndex( team ), secondID = rules->GetScavengeTeamIndex( otherTeam ), firstMatch = rules->GetScavengeMatchScore( team ), secondMatch = rules->GetScavengeMatchScore( otherTeam ), current = rules->GetScavengeRoundNumber(), limit = rules->GetScavengeRoundLimit();
	wchar_t text[260], number[6]; const wchar_t *round = g_pVGuiLocalize->Find( "#L4D_Scavenge_Round_Current" ); if ( round ) { V_snwprintf( text, ARRAYSIZE( text ), L"%ls: %d", round, current ); SetControlString( "RoundLabel", text ); } const wchar_t *roundLimit = g_pVGuiLocalize->Find( "#L4D_Scavenge_RoundLimit" ); if ( roundLimit ) { V_snwprintf( number, ARRAYSIZE( number ), L"%d", limit ); g_pVGuiLocalize->ConstructString( text, sizeof( text ), roundLimit, 1, number ); SetControlString( "RoundLimitLabel", text ); }
	int lastX = 0, lastY = 0; for ( int i = 0; i < 5; ++i )
	{
		CScavengeRoundPanel *panel = m_rounds[i]; if ( !panel ) continue; panel->SetScoreColor( 1, "White" ); panel->SetScoreColor( 2, "White" ); int first = -1, second = -1; const int roundIndex = i + 1;
		if ( roundIndex <= current ) { first = rules->GetScavengeScore( firstID == 1, roundIndex ); second = rules->GetScavengeScore( firstID != 1, roundIndex ); if ( roundIndex >= current ) { if ( !rules->IsScavengeSecondHalf() ) { if ( team == TEAM_SURVIVOR ) second = -1; else first = -1; } if ( rules->GetRoundEndTime() == 0.0 ) panel->SetScoreColor( ( team != TEAM_SURVIVOR ) + 1, "Yellow" ); } else panel->SetWinnerColors( rules->GetScavengeWinningTeam( roundIndex ), firstID, secondID ); }
		panel->SetVisible( roundIndex <= limit ); if ( roundIndex <= limit ) { panel->SetScore( 1, first ); panel->SetScore( 2, second ); panel->GetPos( lastX, lastY ); }
	}
	if ( m_final ) { m_final->SetPos( TerrorHudFtol( (double)m_final->GetWide() * 1.5 + lastX ), lastY ); m_final->SetScore( 1, firstMatch ); m_final->SetScore( 2, secondMatch ); m_final->SetScoreColor( 1, "White" ); m_final->SetScoreColor( 2, "White" ); const int required = limit / 2 + 1; if ( firstMatch >= required || secondMatch >= required ) m_final->SetWinnerColors( firstMatch > secondMatch ? firstID : ( firstMatch < secondMatch ? secondID : 0 ), firstID, secondID ); }
}
CVersusModeEmbeddedScoreboard::CVersusModeEmbeddedScoreboard( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_playerSlot( -1 )
{
	vgui::ivgui()->AddTickSignal( GetVPanel(), 250 ); m_progress = new CVersusModeLevelProgressBar( this, "CompletionProgressBar" );
}
void CVersusModeEmbeddedScoreboard::SetPlayerSlot( int slot ) { m_playerSlot = slot; if ( m_progress ) m_progress->SetPlayerSlot( slot ); }
void CVersusModeEmbeddedScoreboard::ApplySchemeSettings( vgui::IScheme *scheme ) { LoadControlSettings( "Resource/UI/VersusModeEmbeddedScoreboard.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme ); SetPaintBackgroundEnabled( false ); SetPaintBorderEnabled( false ); UpdateScores(); }
void CVersusModeEmbeddedScoreboard::OnTick() { if ( IsVisible() && ( !GetParent() || GetParent()->IsVisible() ) ) UpdateScores(); }
void CVersusModeEmbeddedScoreboard::UpdateScores()
{
	// SDK2013's one supported split-screen slot, not an entity-index alias.
	if ( m_playerSlot != -1 && m_playerSlot != 0 ) return; C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ); CTerrorGameRules *rules = TerrorGameRules(); if ( !local || !rules ) return; int team = local->m_iVersusTeam; if ( team == 0 ) team = 1; else if ( team < 1 || team > 2 ) return; int yours = rules->GetVersusScore( team, true ), enemy = rules->GetVersusScore( 2 - ( team != 1 ), true ), completion = 0; for ( int i = 0; i < 4; ++i ) completion = TerrorHudAdd32( completion, MAX( rules->GetVersusDistancePerSurvivor( i, TEAM_SURVIVOR ), 0 ) ); if ( rules->IsRoundInProgress() ) { if ( local->GetTeamNumber() == TEAM_SURVIVOR ) yours = TerrorHudAdd32( yours, completion ); else enemy = TerrorHudAdd32( enemy, completion ); } SetDialogVariable( "YourSurvivor", yours ); SetDialogVariable( "EnemySurvivor", enemy ); char text[64]; Q_snprintf( text, sizeof( text ), "%d / %d", completion, rules->GetVersusMaxCompletionScore() ); SetDialogVariable( "completion", text ); vgui::Label *label = dynamic_cast<vgui::Label *>( FindChildByName( "CompletionLabel" ) ); if ( label ) { KeyValues *mission = NULL; TerrorClient_GetTransitionChapterInfo( false, &mission ); if ( mission ) label->SetText( g_pVGuiLocalize->Find( mission->GetString( "DisplayTitle", "" ) ) ); }
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
