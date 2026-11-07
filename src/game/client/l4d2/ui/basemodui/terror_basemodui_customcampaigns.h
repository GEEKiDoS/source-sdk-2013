//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_CUSTOMCAMPAIGNS_H
#define TERROR_BASEMODUI_CUSTOMCAMPAIGNS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/basemodui/terror_basemodui_controls.h"
#include "vgui_controls/EditablePanel.h"
namespace vgui { class Label; class ImagePanel; }
namespace L4D360UI
{
class GenericPanelList;
class CustomCampaignListItem : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CustomCampaignListItem, vgui::EditablePanel );
public:
	CustomCampaignListItem( vgui::Panel *parent, const char *name );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void SetEnabled( bool enabled ) OVERRIDE;
	virtual void OnMousePressed( vgui::MouseCode code ) OVERRIDE;
	void SetMission( KeyValues *mission );
	const char *GetMissionName() const { return m_Mission; }
protected:
	virtual void PaintBackground() OVERRIDE;
	MESSAGE_FUNC( OnPanelSelected, "PanelSelected" );
	MESSAGE_FUNC( OnPanelUnSelected, "PanelUnSelected" );
private:
	vgui::Label *m_pName;
	vgui::HFont m_hNameFont;
	Color m_DisabledColor;
	char m_Mission[256];
	bool m_bSelected;
};

class CustomCampaigns : public CL4DFrame
{
	DECLARE_CLASS_SIMPLE( CustomCampaigns, CL4DFrame );
public:
	CustomCampaigns( vgui::Panel *parent, const char *name = "CustomCampaigns" );
	virtual ~CustomCampaigns();
	virtual void SetDataSettings( KeyValues *settings ) OVERRIDE;
	virtual void OnOpen() OVERRIDE;
	virtual void OnClose() OVERRIDE;
	virtual vgui::Panel *NavigateBack() OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	void SetSelectionTarget( vgui::Panel *target ) { m_SelectionTarget = target; }
	void SetReturnPanel( vgui::Panel *panel ) { m_ReturnPanel = panel; }
protected:
	virtual void PaintBackground() OVERRIDE;
	MESSAGE_FUNC_PARAMS( OnItemSelected, "OnItemSelected", data );
private:
	void PopulateCampaigns();
	void UpdateAvailability();
	void UpdateSelection();
	void SubmitSelection();
	GenericPanelList *m_pCampaigns;
	KeyValues *m_pSettings; // Owned native G2B090 copy; never a common-frame store.
	vgui::DHANDLE<vgui::Panel> m_SelectionTarget, m_ReturnPanel;
	bool m_bAnyInstalled, m_bAnyUnsupported, m_bAnySupported;
};

CustomCampaigns *OpenCustomCampaigns( vgui::Panel *parent, vgui::Panel *returnTo, KeyValues *settings, vgui::Panel *selectionTarget );
}
#endif // TERROR_BASEMODUI_CUSTOMCAMPAIGNS_H
