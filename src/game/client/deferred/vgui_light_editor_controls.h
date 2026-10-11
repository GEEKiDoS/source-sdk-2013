//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Light editor mode toolbar.
//
//===========================================================================//

#ifndef VGUI_LIGHT_EDITOR_CONTROLS_H
#define VGUI_LIGHT_EDITOR_CONTROLS_H

#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/frame.h"

class CVGUILightEditor_Controls : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE( CVGUILightEditor_Controls, vgui::Frame );

public:
	CVGUILightEditor_Controls( vgui::Panel *pParent );

	void SelectModeButton( const char *pszName );

protected:
	void ApplySchemeSettings( vgui::IScheme *pScheme );
	void OnCommand( const char *pszCommand );

	MESSAGE_FUNC_PTR( OnRadioButtonChecked, "RadioButtonChecked", panel );

private:
	void LayoutControls();
};
#endif
