//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_GRAPHICS_DX12_H
#define TERROR_BASEMODUI_GRAPHICS_DX12_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "vgui_graphics_options.h"

namespace L4D360UI
{
class GraphicsDX12 : public NativeOptionsFrame, public FlyoutMenuListener
{
	DECLARE_CLASS_SIMPLE( GraphicsDX12, NativeOptionsFrame );
public:
	GraphicsDX12( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnClose() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
	void LoadControlSettings( const char *resource, const char *pathID = NULL, KeyValues *preloaded = NULL, KeyValues *conditions = NULL ) OVERRIDE;
	void SetSection( int section );
	void OnNotifyChildFocus( vgui::Panel *child ) OVERRIDE {}
	void OnFlyoutMenuClosed( vgui::Panel *returnTo ) OVERRIDE;
	void OnFlyoutMenuCancelled() OVERRIDE {}
protected:
	void PaintBackground() OVERRIDE;
private:
	MESSAGE_FUNC_PARAMS( OnSliderMoved, "SliderMoved", data );
	struct Widget_t
	{
		const GraphicsOptions::RowDef_t *row;
		vgui::DHANDLE<vgui::Panel> control;
		int rowIndex;
	};
	void ReadSettings();
	void RefreshPage( bool focusSection = false );
	void UpdateFooter();
	int SectionRowCount( int section ) const;
	void SaveSettings();
	CUtlVector<Widget_t> m_Widgets;
	int m_nSection, m_nPage, m_nRowX, m_nRowY, m_nRowPitch;
	bool m_bDirty;
};
}
#endif // TERROR_BASEMODUI_GRAPHICS_DX12_H
