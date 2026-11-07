//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_PANELLIST_H
#define TERROR_BASEMODUI_PANELLIST_H
#ifdef _WIN32
#pragma once
#endif
#include "vgui_controls/Panel.h"
#include "vgui_controls/PHandle.h"
#include "tier1/utlvector.h"
namespace vgui { class ScrollBar; class Label; }
namespace L4D360UI
{
// G59990. Owns supplied children. G57F70 sends PanelSelected / PanelUnSelected
// to the row and OnItemSelected(panelName,index) to its parent.
class GenericPanelList : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( GenericPanelList, vgui::Panel );
public:
	GenericPanelList( vgui::Panel *parent, const char *name );
	virtual void ApplySettings( KeyValues *data ) OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	virtual void OnMouseWheeled( int delta ) OVERRIDE;
	virtual void NavigateTo() OVERRIDE;
	virtual void NavigateFrom() OVERRIDE;
	virtual void NavigateToChild( vgui::Panel *child ) OVERRIDE;
	void AddPanelItem( vgui::Panel *item );
	void RemoveAllPanelItems();
	void SelectPanelItem( int index, bool focus = true );
	vgui::Panel *GetSelectedPanelItem() const;
	int GetSelectedIndex() const { return m_Selected; }
	int GetPanelItemCount() const { return m_Items.Count(); }
	vgui::Panel *GetPanelItem( int index ) const;
	void SetScrollBarVisible( bool visible );
	void SetScrollArrowsVisible( bool visible );
	void SetScrollProgressVisible( bool visible );
protected:
	virtual void PerformLayout() OVERRIDE;
	MESSAGE_FUNC( OnScrollBarSliderMoved, "ScrollBarSliderMoved" );
private:
	bool IsPanelItemVisible( vgui::Panel *item, bool fully ) const;
	int GetFirstVisiblePanelItem() const;
	int GetLastVisiblePanelItem() const;
	void ScrollToPanelItem( int index );
	void UpdateScrollIndicators();
	vgui::Panel *m_pClip;
	vgui::ScrollBar *m_pScroll;
	vgui::Label *m_pUp, *m_pDown, *m_pProgress;
	CUtlVector< vgui::DHANDLE<vgui::Panel> > m_Items;
	int m_Selected, m_Spacing, m_Offset;
	bool m_bShowArrows, m_bShowProgress, m_bWrap;
};
}
#endif
