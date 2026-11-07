//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_CLIENT_SCOREBOARD_H
#define TERROR_CLIENT_SCOREBOARD_H
#ifdef _WIN32
#pragma once
#endif
#include "clientscoreboarddialog.h"
namespace vgui { class ImagePanel; }
class BasePlayerStatsPanel;
class CVersusModeEmbeddedScoreboard;
class CScavengeModeEmbeddedScoreboard;
class C_TransitioningPlayer;
class CTerrorClientScoreBoardDialog : public CClientScoreBoardDialog
{
	DECLARE_CLASS_SIMPLE( CTerrorClientScoreBoardDialog, CClientScoreBoardDialog );
public:
	explicit CTerrorClientScoreBoardDialog( IViewPort *viewport );
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void ShowPanel( bool show ) OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE;
	virtual void PaintBackground() OVERRIDE {} // Native2D72A0 genuine empty override.
private:
	void ReloadLayout(); void UpdateSurvivors(); void UpdateInfected(); void UpdateSpectators(); void UpdateMission(); void UpdateMedals(); void UpdateServerName(); void DeleteData( CUtlVector<KeyValues *> &data );
	KeyValues *TransitionData( C_TransitioningPlayer *player );
	void CenterMarker( const char *name, int center );
	void SetMapImage( vgui::ImagePanel *panel, const char *image );
	void PositionScoreboard();
	CUtlVector<KeyValues *> m_survivors, m_infected; BasePlayerStatsPanel *m_rows[9]; CVersusModeEmbeddedScoreboard *m_versus; CScavengeModeEmbeddedScoreboard *m_scavenge; char m_hostname[128]; int m_selected, m_playerSlot, m_alignment;
	CPanelAnimationVarAliasType( int, m_infectedAvatarSize, "infected_avatar_size", "32", "proportional_int" ); CPanelAnimationVarAliasType( int, m_infectedNameWidth, "infected_name_width", "32", "proportional_int" ); CPanelAnimationVarAliasType( int, m_infectedStatusWidth, "infected_status_width", "32", "proportional_int" ); CPanelAnimationVarAliasType( int, m_infectedScoreWidth, "infected_score_width", "32", "proportional_int" ); CPanelAnimationVarAliasType( int, m_infectedDeathWidth, "infected_death_width", "32", "proportional_int" ); CPanelAnimationVarAliasType( int, m_infectedPingWidth, "infected_ping_width", "32", "proportional_int" );
};
#endif
