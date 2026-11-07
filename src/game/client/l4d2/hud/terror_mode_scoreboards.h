//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_MODE_SCOREBOARDS_H
#define TERROR_MODE_SCOREBOARDS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/terror_client_panels.h"
class CScavengeRoundPanel;
class CVersusModeLevelProgressBar;
namespace vgui { class Label; }

// Native C2E0CB0. This is the survival record row, not BasePlayerStatsPanel.
class CScoreboardEntry : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CScoreboardEntry, vgui::EditablePanel );
public:
	CScoreboardEntry( vgui::Panel *parent, const char *name );
	void SetSelected( bool selected );
	void SetPlayerName( const char *name, bool presentAtStart );
	void SetPlayerIndex( int index );
	void SetTime( const char *time );
	void SetMedal( const char *icon );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void PaintBackground() OVERRIDE;
private:
	bool m_selected, m_haveNameMetrics;
	int m_nameWidth;
};

class CScavengeModeScoreboard : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CScavengeModeScoreboard, vgui::EditablePanel );
public:
	CScavengeModeScoreboard( vgui::Panel *parent, const char *name );
	virtual ~CScavengeModeScoreboard();
	void RefreshRoundInfo(); // C2E0070, scheme application and hidden->shown.
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnTick() OVERRIDE;
private:
	struct Effect_t { int type; float delay; };
	static const Effect_t s_firstHalf[4], s_nextRound[4], s_nextRoundTie[8], s_matchTie[8], s_matchFinal[4];
	void NextEffect();
	void AnimateScores();
	void RefreshMatchScore();
	void SetTickInterval( int milliseconds );
	CScavengeRoundPanel *m_rounds[5], *m_final, *m_current;
	vgui::EditablePanel *m_tie;
	vgui::Label *m_time[2];
	int m_teamID[2], m_matchScore[2];
	float m_roundTime[2];
	int m_counter, m_targetScore, m_targetSlot, m_effectKind, m_effectIndex;
	float m_effectEnd, m_nextIncrement, m_effectStart, m_incrementDelay, m_timeRate, m_nextSound, m_soundInterval;
	const Effect_t *m_effects;
};

class CSurvivalModeScoreboard : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CSurvivalModeScoreboard, vgui::EditablePanel );
public:
	CSurvivalModeScoreboard( vgui::Panel *parent, const char *name );
	virtual ~CSurvivalModeScoreboard();
	void RefreshRoundInfo(); // C2E3BC0; no periodic native refresh.
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	CScoreboardEntry *m_entries[4];
};

class CVersusModeScoreboard : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CVersusModeScoreboard, vgui::EditablePanel );
public:
	CVersusModeScoreboard( vgui::Panel *parent, const char *name );
	virtual ~CVersusModeScoreboard();
	virtual void SetData( KeyValues *data ); // C2EDF90, native virtual236.
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnTick() OVERRIDE;
private:
	struct Display_t
	{
		int progress[4][2];
		int completion, survivors, defibs, chapter, enemyChapter, campaign, enemyCampaign;
		int enemyCompletion, enemySurvivors, enemyDefibs, damage, enemyDamage, damageWinner, enemyDamageWinner;
	};
	void ResetDisplay( Display_t &display );
	void StartScoreAnimation();
	void AnimateScores();
	void PublishScores();
	void SetTickInterval( int milliseconds );
	vgui::EditablePanel *m_left, *m_right, *m_survivor, *m_infected, *m_tie;
	vgui::Label *m_campaignLeft, *m_campaignRight, *m_campaignSurvivor, *m_campaignInfected, *m_damage[2];
	CVersusModeLevelProgressBar *m_progress;
	int m_baseCampaign[2], m_baseChapter[2];
	int m_phase, m_total, m_oldTotal, m_steps, m_lastSoundScore, m_damageTarget[2];
	float m_phaseEnd, m_phaseStart, m_nextSound;
	bool m_secondHalf, m_tiebreak, m_localSurvivor, m_animationStarted;
	Display_t m_display, m_previous;
};

class CFullscreenScavengeModeScoreboard : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CFullscreenScavengeModeScoreboard, CTerrorViewportPanel );
public:
	explicit CFullscreenScavengeModeScoreboard( IViewPort *viewport );
	virtual bool HasInputElements() OVERRIDE { return false; }
	virtual void ShowPanel( bool state ) OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	CScavengeModeScoreboard *m_scoreboard;
};
class CFullscreenSurvivalModeScoreboard : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CFullscreenSurvivalModeScoreboard, CTerrorViewportPanel );
public:
	explicit CFullscreenSurvivalModeScoreboard( IViewPort *viewport );
	virtual bool HasInputElements() OVERRIDE { return false; }
	virtual void ShowPanel( bool state ) OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	CSurvivalModeScoreboard *m_scoreboard;
};
class CFullscreenVersusModeScoreboard : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CFullscreenVersusModeScoreboard, CTerrorViewportPanel );
public:
	explicit CFullscreenVersusModeScoreboard( IViewPort *viewport );
	virtual bool HasInputElements() OVERRIDE { return false; }
	virtual void ShowPanel( bool state ) OVERRIDE;
	virtual void SetData( KeyValues *data ) OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	CVersusModeScoreboard *m_scoreboard;
};
IViewPortPanel *CreateTerrorModeScoreboardPanel( IViewPort *viewport, const char *name );
#endif
