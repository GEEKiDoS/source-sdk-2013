//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_GAMEMODES_H
#define TERROR_BASEMODUI_GAMEMODES_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/basemodui/terror_basemodui_controls.h"
#include "tier1/utlvector.h"

class CGameModesPaint;
// G161A34: this class is global, and its native base is vgui::Button.
class GameModes : public vgui::Button
{
	DECLARE_CLASS_SIMPLE( GameModes, vgui::Button );
public:
	GameModes( vgui::Panel *parent, const char *name );
	virtual ~GameModes();
	virtual void ApplySettings( KeyValues *data ) OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	virtual void OnMousePressed( vgui::MouseCode code ) OVERRIDE;
	virtual void NavigateTo() OVERRIDE;
	virtual void NavigateFrom() OVERRIDE;
	bool ScrollLeft();
	bool ScrollRight( int count = 0 );
	bool SetActiveMode( const char *id, bool focus = true );
	int GetModeCount() const { return m_Modes.Count(); }
	L4D360UI::L4D360HybridButton *GetActiveButton() const;
	L4D360UI::L4D360HybridButton *GetButtonForCommand( const char *command ) const;
	void UpdateCustomModeHint();
protected:
	virtual void Paint() OVERRIDE;
private:
	struct Mode_t
	{
		CUtlString id, name, command, title, hint, disabledHint;
		int texture, customMode;
		bool enabled;
		L4D360UI::L4D360HybridButton *button;
	};
	void ClearModes();
	void SetActiveMode( int index, bool focus );
	int Scale( int value );
	void DrawSmear( CGameModesPaint &paint, int x0, int y0, int x1, int y1 );
	void DrawSmallCard( CGameModesPaint &paint, int index, int x, int y, int shade, int alpha );
	CUtlVector<Mode_t> m_Modes;
	int m_nSelected, m_nQueuedSteps;
	float m_flScrollStart;
	bool m_bScrollRight, m_bHideLabels;
	int m_nPicX, m_nPicWide, m_nPicTall;
	int m_nTitleX, m_nTitleY, m_nTitleWide, m_nTitleTall, m_nButtonTall;
	int m_nSubPics, m_nSubGap, m_nSubOffsetX, m_nSubOffsetY, m_nSubWide, m_nSubTall, m_nSubX, m_nSubY;
	int m_nArrowWide, m_nArrowTall, m_nArrowOffsetY, m_nRightArrowOffsetX;
	int m_nLeftX, m_nLeftY, m_nRightX, m_nRightY;
	int m_nBorderTexture, m_nTopTexture, m_nBottomTexture, m_nLeftTexture, m_nRightTexture;
	vgui::HFont m_hSubFont;
	int m_nSubFontTall;
	Color m_SmearColor;
};

namespace L4D360UI
{
// Native ui_mainmenu_challenge / G67230 alias resolution. Borrowed mode name,
// except teamchallenge, which is written into the supplied caller buffer.
const char *ResolveBaseModChallengeMode( const char *name, char *buffer, int bytes );
const char *GetBaseModChallengeFlyout();
void SetBaseModChallengeMode( const char *name );
bool UsesBaseModScriptedSoloLayout( KeyValues *mode );
// Original ScriptedModes/ScriptedModeListItem resources; no MatchSession owner.
void OpenBaseModMutationPicker( vgui::Panel *parent, vgui::Panel *caller );
}
#endif
