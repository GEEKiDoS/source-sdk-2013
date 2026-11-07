//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_TRANSITION_SUPPORT_H
#define TERROR_TRANSITION_SUPPORT_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/stats/terror_client_stats.h"
#include "vgui_controls/EditablePanel.h"
#include "tier1/utlvector.h"
class CTerrorAvatarImagePanel;
class CIconPanel;
namespace vgui { class Label; }
class CTipPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CTipPanel, vgui::EditablePanel );
public:
	explicit CTipPanel( vgui::Panel *parent );
	void SetTeam( bool infected ) { m_nTeam = infected ? 1 : 0; }
	void AdvanceTip();
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	struct Tip_t { char text[64], icon[64]; };
	static void LoadTips( KeyValues *root, CUtlVector< Tip_t > &tips );
	CIconPanel *m_pIcon;
	CUtlVector< Tip_t > m_survivorTips, m_infectedTips;
	int m_iSurvivorTip, m_iInfectedTip, m_nTeam;
};
class CSurvivorHighlightStatsPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CSurvivorHighlightStatsPanel, vgui::EditablePanel );
public:
	CSurvivorHighlightStatsPanel( vgui::Panel *parent, const char *name );
	virtual ~CSurvivorHighlightStatsPanel();
	void CapturePlayers();
	void AdvanceCategory();
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	struct Category_t { int statistic, total; };
	static int __cdecl CompareCategories( const Category_t *a, const Category_t *b );
	void RebuildCategories( bool positiveOnly );
	CTerrorPlayerStats m_stats;
	CUtlVector< Category_t > m_categories;
	KeyValues *m_pLayout;
	CTerrorAvatarImagePanel *m_pAvatars[4];
	vgui::Label *m_pNames[4], *m_pNamesNoAvatar[4];
};
class CVersusModeResults : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CVersusModeResults, vgui::EditablePanel );
public:
	CVersusModeResults( vgui::Panel *parent, const char *name );
	void UpdateResults();
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	vgui::Panel *m_pYourHighlight, *m_pEnemyHighlight;
	int m_nLocalPlayerSlot, m_nYourScore, m_nEnemyScore;
	bool m_bFinalChapter;
};
#endif
