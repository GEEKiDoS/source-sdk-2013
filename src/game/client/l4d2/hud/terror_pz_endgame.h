//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PZ_ENDGAME_H
#define TERROR_PZ_ENDGAME_H
#ifdef _WIN32
#pragma once
#endif
#include "hudelement.h"
#include "vgui_controls/EditablePanel.h"
#include "util_shared.h"
class VoteBarPanel;
class CPZEndGamePanel : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CPZEndGamePanel, vgui::EditablePanel );
public:
	CPZEndGamePanel( const char *name );
	virtual ~CPZEndGamePanel();
	virtual void Init() OVERRIDE;
	virtual void Reset() OVERRIDE;
	virtual void VidInit() OVERRIDE;
	virtual bool ShouldDraw() OVERRIDE;
	virtual int KeyInput( int down, ButtonCode_t code, const char *binding );
	void MsgFunc_PZEndGamePanelMsg( bf_read &message );
	void MsgFunc_PZEndGamePanelVoteRegisteredMsg( bf_read &message );
	void MsgFunc_PZEndGameVoteStatsMsg( bf_read &message );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnThink() OVERRIDE;
private:
	void UpdateVote();
	void Sound( const char *name );
	vgui::EditablePanel *m_active, *m_wait, *m_failed, *m_passed;
	VoteBarPanel *m_bar, *m_bar2;
	int m_potential[2], m_yes[2], m_no[2], m_complete[2], m_result, m_registered;
	CountdownTimer m_resultTimer;
};
bool IsPZEndGamePanelVisible();
#endif
