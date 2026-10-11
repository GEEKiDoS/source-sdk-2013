//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Fullscreen light-editor input surface.
//
//===========================================================================//

#ifndef VGUI_LIGHT_EDITOR_ROOT_H
#define VGUI_LIGHT_EDITOR_ROOT_H

#ifdef _WIN32
#pragma once
#endif
#include "cbase.h"
#include "vgui_controls/panel.h"
#include "vgui_controls/label.h"
class CVGUILightEditor_Controls;
class CVGUILightEditor_Properties;

class CVGUILightEditor : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CVGUILightEditor, vgui::Panel );

public:
	static void DestroyEditor();
	static void ToggleEditor();
	static bool IsEditorVisible();
	static vgui::VPANEL GetEditorPanel();
	static bool IsTextEntryFocused();

	~CVGUILightEditor();

protected:
	void ApplySchemeSettings( vgui::IScheme *pScheme );
	void PerformLayout();
	void OnThink();
	void Paint();

	MESSAGE_FUNC( OnToggleEditorProperties, "ToggleEditorProperties" );
	MESSAGE_FUNC( OnSaveEditorLights, "SaveEditorLights" );
	MESSAGE_FUNC( OnReloadEditorLights, "ReloadEditorLights" );

	void OnMouseReleased( vgui::MouseCode code );
	void OnMouseCaptureLost();

private:
	CVGUILightEditor( vgui::VPANEL pParent );

	void OnMousePressed_Internal( vgui::MouseCode code );
	void OnMouseReleased_Internal( vgui::MouseCode code );
	void OnCursorMoved_Internal( int &x, int &y );
	void OnKeyCodePressed_Internal( vgui::KeyCode code );
	void OnKeyCodeReleased_Internal( vgui::KeyCode code );

	void StartDrag( int mode );
	void StopDrag();
	void GetDragBounds( int *pi2_min, int *pi2_max );
	Vector GetDragWorldPos( int x, int y );
	bool IsWorldTarget( vgui::VPANEL over ) const;
	bool ActivateEditorInteractionMode( int mode );
	void UpdateMapName();
	void NotifySelectionChanged();

	enum
	{
		EDITORDRAG_NONE = 0,
		EDITORDRAG_VIEW,
		EDITORDRAG_TRANSLATE,
		EDITORDRAG_ROTATE,
		EDITORDRAG_SELECT,
	};

	static CVGUILightEditor *s_pInstance;

	int m_iDragMode;
	int m_iLastMouseX;
	int m_iLastMouseY;
	int m_iDragStart[2];
	bool m_bDragSelectArmed;
	bool m_bMouseDown[2];
	bool m_bKeyDown[16];

	int m_iLastInteractionMode;
	bool m_bModeKeyHeld;
	float m_flLastInteractionTypeKeyPress;
	Vector m_vecLastDragWorldPos;

	vgui::Label *m_pMapName;
	CVGUILightEditor_Controls *m_pControls;
	CVGUILightEditor_Properties *m_pProps;
};

#endif
