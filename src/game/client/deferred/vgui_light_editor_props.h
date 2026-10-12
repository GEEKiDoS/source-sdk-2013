//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Live property editor for DeferredLight_t.
//
//===========================================================================//

#ifndef VGUI_LIGHT_EDITOR_PROPS_H
#define VGUI_LIGHT_EDITOR_PROPS_H

#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/frame.h"

namespace vgui
{
class TextEntry;
class CheckButton;
class Label;
}

class CVGUILightEditor_Properties : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE( CVGUILightEditor_Properties, vgui::Frame );

public:
	CVGUILightEditor_Properties( vgui::Panel *pParent );

	void RefreshFromEditor();

protected:
	void ApplySchemeSettings( vgui::IScheme *pScheme );

	MESSAGE_FUNC( OnLightSelectionChanged, "LightSelectionChanged" );
	MESSAGE_FUNC_PTR( OnPropertyChanged, "TextChanged", panel );
	MESSAGE_FUNC_PTR( OnCheckButtonChecked, "CheckButtonChecked", panel );

private:
	void BuildRows();
	void LayoutRows();
	vgui::TextEntry *AddTextRow( const char *pszName, const char *pszLabel );
	vgui::CheckButton *AddCheckRow( const char *pszName, const char *pszLabel );

	bool m_bRefreshing;

	vgui::Label *m_pLabelOrigin;
	vgui::Label *m_pLabelAngles;
	vgui::Label *m_pLabelColor;
	vgui::Label *m_pLabelIntensity;
	vgui::Label *m_pLabelRadius;
	vgui::Label *m_pLabelInner;
	vgui::Label *m_pLabelOuter;
	vgui::Label *m_pLabelCookie;

	vgui::TextEntry *m_pOrigin;
	vgui::TextEntry *m_pAngles;
	vgui::TextEntry *m_pColor;
	vgui::TextEntry *m_pIntensity;
	vgui::TextEntry *m_pRadius;
	vgui::TextEntry *m_pInner;
	vgui::TextEntry *m_pOuter;
	vgui::TextEntry *m_pCookie;
	vgui::CheckButton *m_pSpot;
	vgui::CheckButton *m_pShadow;
	vgui::CheckButton *m_pVolumetric;
};
#endif
