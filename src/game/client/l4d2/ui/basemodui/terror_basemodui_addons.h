//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_ADDONS_H
#define TERROR_BASEMODUI_ADDONS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/basemodui/terror_basemodui_controls.h"
#include "vgui_controls/EditablePanel.h"
#include "tier1/utldict.h"
#include "tier1/utlvector.h"
#include "filesystem.h"
namespace vgui { class CheckButton; class Label; class ImagePanel; }
namespace L4D360UI
{
class GenericPanelList;
class Addons;
struct AddonInfo_t
{
	char filename[MAX_PATH], image[MAX_PATH];
	wchar_t title[128], author[120], description[1024], version[32], types[128];
	int content;
	bool enabled, conflict, archive;
};
class AddonListItem : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( AddonListItem, vgui::EditablePanel );
public:
	AddonListItem( GenericPanelList *parent, Addons *owner, int index, const AddonInfo_t &info );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnMousePressed( vgui::MouseCode code ) OVERRIDE;
	int GetAddonIndex() const { return m_Index; }
	void ToggleEnabled();
	void SetConflict( bool conflict ) { m_bConflict = conflict; Repaint(); }
protected:
	virtual void PaintBackground() OVERRIDE;
	MESSAGE_FUNC( OnPanelSelected, "PanelSelected" );
	MESSAGE_FUNC( OnPanelUnSelected, "PanelUnSelected" );
	MESSAGE_FUNC( OnCheckButtonChecked, "CheckButtonChecked" );
private:
	Addons *m_pOwner;
	vgui::Label *m_pName, *m_pType;
	vgui::CheckButton *m_pEnabled;
	int m_Index;
	vgui::HFont m_Font;
	bool m_bSelected, m_bConflict, m_bInitialized;
};
class AddonAssociation : public CL4DFrame
{
	DECLARE_CLASS_SIMPLE( AddonAssociation, CL4DFrame );
public:
	AddonAssociation( vgui::Panel *parent, const char *name = "AddonAssociation" );
	virtual void OnOpen() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
	static int GetAssociationState();
	static bool RegisterAssociation( vgui::Panel *parent );
	bool IsLayoutReady() const { return m_bLayoutReady; }
private:
	vgui::CheckButton *m_pIgnore;
	bool m_bLayoutReady;
};
class Addons : public CL4DFrame
{
	DECLARE_CLASS_SIMPLE( Addons, CL4DFrame );
public:
	Addons( vgui::Panel *parent, const char *name = "Addons" );
	virtual ~Addons();
	virtual void OnOpen() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
	void SetAddonEnabled( int index, bool enabled );
	KeyValues *GetRowLayout() const { return m_pRowLayout; }
	bool IsLayoutReady() const { return m_bLayoutReady; }
protected:
	virtual void PaintBackground() OVERRIDE;
	virtual void OnThink() OVERRIDE;
	MESSAGE_FUNC( OnItemSelected, "OnItemSelected" );
private:
	void PopulateAddonInfo();
	void RebuildRows();
	void BuildConflictMap( bool warn );
	void UpdateDetails();
	void UpdateFooter();
	bool SaveAddonList();
	void ClearImage();
	static void ImageReadComplete( const FileAsyncRequest_t &request, int bytes, FSAsyncStatus_t status );
	void LoadImageData( const void *data, int bytes );
	GenericPanelList *m_pList;
	vgui::ImagePanel *m_pIcon, *m_pIconSpinner;
	vgui::CheckButton *m_pIgnoreAssociation;
	KeyValues *m_pAddonList, *m_pRowLayout;
	CUtlVector<AddonInfo_t> m_Addons;
	CUtlDict<bool,int> m_EnabledSnapshot;
	char m_Mod[MAX_PATH], m_ListPath[MAX_PATH], m_Filter[32];
	int m_Texture, m_DefaultTexture;
	FSAsyncControl_t m_ImageRead;
	float m_ImageFade;
	bool m_bSquareImage, m_bLayoutReady, m_bReady, m_bClosing, m_bConflictWarned;
};
CL4DFrame *CreateBaseModAddonsWindow( vgui::Panel *parent, const char *name );
// G19900/G67BB0: invoke before main-menu BaseClass::OnOpen. True means the
// native changed bit was consumed, not a successful filesystem/service result.
bool RefreshBaseModAddonPaths();
// G19340, called from native main-menu OnOpen G67BB0 only.
bool CheckBaseModAddonAssociation( vgui::Panel *parent );
// G1B250 real mounted-buffer JPEG decoder; caller owns returned texture.
bool LoadBaseModJpegTexture( const void *data, int bytes, int &texture, bool &square );
}
#endif
