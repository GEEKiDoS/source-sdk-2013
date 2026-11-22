//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_GRAPHICS_DX12_H
#define TERROR_BASEMODUI_GRAPHICS_DX12_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "upscaler_dx12.h"
#include "vgui_graphics_options.h"

namespace vgui { class Label; class ScrollBar; }
namespace L4D360UI
{
class GraphicsScrollViewport;
void LoadGraphicsGpuLocalization();

class GraphicsDX12 : public NativeOptionsFrame, public FlyoutMenuListener
{
	DECLARE_CLASS_SIMPLE( GraphicsDX12, NativeOptionsFrame );
public:
	GraphicsDX12( vgui::Panel *parent, const char *name );
	void OnOpen() OVERRIDE;
	void OnClose() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
	void LoadControlSettings( const char *resource, const char *pathID = NULL, KeyValues *preloaded = NULL, KeyValues *conditions = NULL ) OVERRIDE;
	vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
	void SetSection( int section );
	bool HasLivePreview() const { return m_bLivePreview; }
	void OnMouseWheeled( int delta ) OVERRIDE;
	void OnNotifyChildFocus( vgui::Panel *child ) OVERRIDE {}
	void OnFlyoutMenuClosed( vgui::Panel *returnTo ) OVERRIDE;
	void OnFlyoutMenuCancelled() OVERRIDE {}
protected:
	void PaintBackground() OVERRIDE;
	void PerformLayout() OVERRIDE;
	void OnThink() OVERRIDE;
private:
	friend class GraphicsScrollViewport;
	MESSAGE_FUNC_PARAMS( OnSliderMoved, "SliderMoved", data );
	struct Widget_t
	{
		const GraphicsOptions::RowDef_t *row;
		vgui::DHANDLE<vgui::Panel> control;
		int rowIndex;
		int y;
	};
	void ReadSettings( bool rebuildChoices = false );
	void ReadRecommendation();
	int LayoutRecommendation( vgui::IScheme *uiScheme );
	void UpdateLiveHelp();
	void RevealControl( int y, int tall );
	void LayoutRows();
	void PositionRows();
	void FocusSection();
	void RevealRow( vgui::Panel *row );
	void SetScroll( int value );
	void UpdateFooter();
	int Scale( int value );
	static void PositionFlyout( DropDownMenu *drop, FlyoutMenu *flyout );
	void SaveSettings();
	CUtlVector<Widget_t> m_Widgets;
	GraphicsScrollViewport *m_pViewport;
	vgui::ScrollBar *m_pScroll;
	vgui::Label *m_pHeaders[GraphicsOptions::TAB_COUNT];
	vgui::Label *m_pNrRequirement;
	vgui::Label *m_pDetectedGpu, *m_pRecommendationStatus, *m_pPerformanceSummary, *m_pLiveHelp;
	L4D360HybridButton *m_pApplyOptimized;
	CUtlString m_LiveHelpToken;
	int m_nApplyY;
	int m_nSection, m_nCapabilities, m_nContentHeight, m_nScrollTarget;
	int m_nHeaderY[GraphicsOptions::TAB_COUNT];
	float m_flScroll;
	double m_flLastThink;
	bool m_bLivePreview, m_bDirty;
	UpscalerDX12NrAvailability_t m_NrAvailability;
};
}
#endif // TERROR_BASEMODUI_GRAPHICS_DX12_H
