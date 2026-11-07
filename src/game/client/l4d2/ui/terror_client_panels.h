//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_CLIENT_PANELS_H
#define TERROR_CLIENT_PANELS_H
#ifdef _WIN32
#pragma once
#endif
#include "game/client/iviewport.h"
#include "vgui_controls/Frame.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/PHandle.h"
#include "materialsystem/MaterialSystemUtil.h"
class CSurvivorHighlightStatsPanel;
class CTipPanel;
class CVersusModeResults;
namespace vgui { class ImagePanel; class Label; }

// Client 2EA100. Frame and viewport ownership remain separate, as in retail.
class CTerrorViewportPanel : public vgui::Frame, public IViewPortPanel
{
	DECLARE_CLASS_SIMPLE( CTerrorViewportPanel, vgui::Frame );
public:
	CTerrorViewportPanel( IViewPort *viewport, const char *name );
	virtual const char *GetName() OVERRIDE { return BaseClass::GetName(); }
	virtual void SetData( KeyValues *data ) OVERRIDE {}
	virtual void Reset() OVERRIDE {}
	virtual void Update() OVERRIDE {}
	virtual bool NeedsUpdate() OVERRIDE { return false; }
	virtual bool HasInputElements() OVERRIDE { return true; }
	virtual void ShowPanel( bool state ) OVERRIDE;
	virtual GameActionSet_t GetPreferredActionSet() OVERRIDE { return GAME_ACTION_SET_NONE; }
	virtual vgui::VPANEL GetVPanel() OVERRIDE { return BaseClass::GetVPanel(); }
	virtual bool IsVisible() OVERRIDE { return BaseClass::IsVisible(); }
	virtual void SetParent( vgui::VPANEL parent ) OVERRIDE { BaseClass::SetParent( parent ); }
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnThink() OVERRIDE;
	IViewPort *m_pViewport;
	bool m_bChatActive;
};

class CTransitionStatsPanel : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CTransitionStatsPanel, CTerrorViewportPanel );
public:
	explicit CTransitionStatsPanel( IViewPort *viewport );
	virtual ~CTransitionStatsPanel();
	void PrepareForShow(); // 2EB7B0, called by ClientModeTerrorNormal::ShowStats.
	virtual void SetData( KeyValues *data ) OVERRIDE;
	virtual bool HasInputElements() OVERRIDE { return false; }
	virtual void ShowPanel( bool state ) OVERRIDE;
	bool IsLoadingPanel() const { return m_bLoadingPanel; } // 2EB190.
	bool AdvanceLoadingAnimation(); // 2EB2B0, wall-clock, not an OnThink timer.
	void AdvanceLoadingStats(); // 2EB1F0, called by actual loading-progress owner.
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void PerformLayout() OVERRIDE;
	virtual void OnThink() OVERRIDE;
	virtual void PaintBackground() OVERRIDE;
	virtual void OnTick() OVERRIDE; // Approved target loading-progress callback adaptation.
	MESSAGE_FUNC( OnDisconnectedFromGame, "DisconnectedFromGame" );
	MESSAGE_FUNC( OnLoadedIntoGame, "LoadedIntoGame" );
	MESSAGE_FUNC( OnHideAsLoadingPanel, "HideAsLoadingPanel" );
private:
	void RefreshNextMap(); // 2EB5E0.
	void SetPostProcessingSuppressed( bool state ); // 2EB1A0.
	CMaterialReference m_FreezeFrame;
	float m_flHideTime;
	bool m_bFreezeFrame;
	vgui::ImagePanel *m_pWorkingAnim;
	vgui::Label *m_pLoadingText;
	CTipPanel *m_pTipPanel;
	CSurvivorHighlightStatsPanel *m_pHighlightPanel;
	CVersusModeResults *m_pVersusResults;
	bool m_bUpdatedVersusResults;
	bool m_bLoadingPanel;
	int m_iNormalStatsY;
	vgui::PHandle m_PreviousParent;
	CPanelAnimationVarAliasType( float, m_flStatsYInVersus, "statpanel_y_in_vsmode", "160", "proportional_float" );
};

class CZombieIntroPanel : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CZombieIntroPanel, CTerrorViewportPanel );
public:
	explicit CZombieIntroPanel( IViewPort *viewport );
	virtual ~CZombieIntroPanel();
	virtual bool NeedsUpdate() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void ShowPanel( bool state ) OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	MESSAGE_FUNC_CHARPTR( OnMouseover, "Mouseover", panelName );
private:
	float m_flHintTimestamp;
	float m_flHintDuration;
};

class CPersonalStatsSummaryPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CPersonalStatsSummaryPanel, vgui::EditablePanel );
public:
	CPersonalStatsSummaryPanel();
	virtual ~CPersonalStatsSummaryPanel();
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnThink() OVERRIDE;
	MESSAGE_FUNC( OnLoadedIntoGame, "LoadedIntoGame" );
	MESSAGE_FUNC( OnDisconnectedFromGame, "DisconnectedFromGame" );
	// Native ShownByGameUI and DisconnectedFromGame callbacks are empty.
	MESSAGE_FUNC( OnShownByGameUI, "ShownByGameUI" );
	MESSAGE_FUNC_CHARPTR( OnProgressLevelName, "ProgressLevelName", levelName );
private:
	void SetBackground( const char *name );
	KeyValues *m_pLayout;
};

CTransitionStatsPanel *GetTransitionStatsPanel(); // 2EB040.
IViewPortPanel *CreateTerrorClientPanel( IViewPort *viewport, const char *name );
bool IsZombieIntroPanelVisible(); // 2EF870, native process-global latch.
bool IsTransitionStatsPostProcessingSuppressed(); // 1C4420 / 1C9310.
void ShutdownPersonalStatsSummary(); // 2D4620; single supported split-screen slot 0.
void ShowPersonalStatsSummary(); // 2D4DC0; native personalstats command.
// Match-framework adaptation: borrowed nodes from the existing mission dictionary.
KeyValues *TerrorClient_GetTransitionChapterInfo( bool next, KeyValues **mission );
const wchar_t *TerrorFindLocalizedTextSafe( const char *token ); // existing VGUI FindSafe adaptation, one owner
#endif
