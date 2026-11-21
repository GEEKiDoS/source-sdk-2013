//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "gpu_optimized_settings.h"
#include "l4d2/ui/basemodui/terror_basemodui_graphics_dx12.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "tier1/fmtstr.h"
#include "vgui/IInput.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/ScrollBar.h"
#include "tier0/memdbgon.h"

namespace L4D360UI
{
using namespace GraphicsOptions;
static const char *const s_SectionTitles[TAB_COUNT] =
{
	"#GraphicsOptions_TabDisplay", "#GraphicsOptions_TabLighting", "#GraphicsOptions_TabPost",
	"#GraphicsOptions_TabFramegen", "#GraphicsOptions_TabUpscaling", "#GraphicsOptions_TabNeural"
};
static const int s_SectionOrder[] = { TAB_DISPLAY, TAB_UPSCALING, TAB_LIGHTING, TAB_POST, TAB_FRAMEGEN, TAB_NEURAL };

void LoadGraphicsGpuLocalization()
{
	g_pVGuiLocalize->AddFile( "resource/graphicsoptions_gpu_%language%.txt", NULL, false );
}

static const char *GraphicsRowHelp( const RowDef_t &row )
{
	const char *tip = row.note ? row.note : PerformanceTip( row );
	return tip ? tip : "";
}

static vgui::Label *CreateGraphicsHint( vgui::Panel *parent, const char *name, const char *text )
{
	vgui::Label *label = new vgui::Label( parent, name, text );
	label->SetMouseInputEnabled( false );
	label->SetPaintBackgroundEnabled( false );
	label->SetContentAlignment( vgui::Label::a_northwest );
	label->SetWrap( true );
	return label;
}

static void SizeGraphicsHint( vgui::Label *label, vgui::HFont font, int wide, int inset, int padding, const Color &color )
{
	label->SetFont( font );
	label->SetFgColor( color );
	label->SetWide( wide );
	label->SetTextInset( inset, 0 );
	int textWide, textTall;
	label->GetContentSize( textWide, textTall );
	label->SetTall( textTall + padding );
}

// Clip rows independently of flyouts, which stay on the frame.
class GraphicsScrollViewport : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( GraphicsScrollViewport, vgui::EditablePanel );
public:
	explicit GraphicsScrollViewport( GraphicsDX12 *owner ) : BaseClass( owner, "GraphicsScrollViewport" ), m_pOwner( owner )
	{
		SetPaintBackgroundEnabled( true );
		SetKeyBoardInputEnabled( true );
		SetPaintBorderEnabled( false );
	}
	void OnCommand( const char *command ) OVERRIDE { m_pOwner->OnCommand( command ); }
	void OnMouseWheeled( int delta ) OVERRIDE { m_pOwner->OnMouseWheeled( delta ); }
	void OnMousePressed( vgui::MouseCode code ) OVERRIDE { m_pOwner->OnMousePressed( code ); }
	void NavigateToChild( vgui::Panel *row ) OVERRIDE
	{
		// A dropdown's button already focused itself on cursor entry. Do not
		// snap the scroll position as different rows pass under the pointer.
		if ( m_pOwner->m_hLastFocus.Get() && m_pOwner->m_hLastFocus.Get() != row )
			m_pOwner->m_hLastFocus->NavigateFrom();
		m_pOwner->m_hLastFocus = row;
	}
	void ApplySchemeSettings( vgui::IScheme *uiScheme ) OVERRIDE
	{
		BaseClass::ApplySchemeSettings( uiScheme );
		SetBgColor( Color( 0, 0, 0, 155 ) );
	}
	void RevealRow( vgui::Panel *row ) { m_pOwner->RevealRow( row ); }
private:
	GraphicsDX12 *m_pOwner;
};

class GraphicsPresetButton : public L4D360HybridButton
{
	DECLARE_CLASS_SIMPLE( GraphicsPresetButton, L4D360HybridButton );
public:
	explicit GraphicsPresetButton( GraphicsScrollViewport *parent ) :
		BaseClass( parent, "BtnApplyOptimized", "#GraphicsOptions_ApplyOptimized", parent, "ApplyOptimized" )
	{
		SetVisible( false );
	}
	void NavigateTo() OVERRIDE
	{
		if ( IsVisible() && IsEnabled() )
		{
			if ( GraphicsScrollViewport *viewport = dynamic_cast<GraphicsScrollViewport *>( GetParent() ) )
				viewport->RevealRow( this );
			BaseClass::NavigateTo();
		}
	}
};

class GraphicsDropDown : public DropDownMenu
{
	DECLARE_CLASS_SIMPLE( GraphicsDropDown, DropDownMenu );
public:
	explicit GraphicsDropDown( vgui::Panel *parent ) : BaseClass( parent, "" ) {}
	void OnMouseWheeled( int delta ) OVERRIDE { vgui::Panel::OnMouseWheeled( delta ); }
	void NavigateTo() OVERRIDE
	{
		if ( GraphicsScrollViewport *viewport = dynamic_cast<GraphicsScrollViewport *>( GetParent() ) )
			viewport->RevealRow( this );
		BaseClass::NavigateTo();
	}
};

class GraphicsSlider : public SliderControl
{
	DECLARE_CLASS_SIMPLE( GraphicsSlider, SliderControl );
public:
	explicit GraphicsSlider( vgui::Panel *parent ) : BaseClass( parent, "" ), m_flDisplayedValue( 0.0f ) {}
	void SetEnabled( bool enabled ) OVERRIDE
	{
		if ( !enabled && vgui::input()->GetMouseCapture() == GetVPanel() )
			OnMouseReleased( MOUSE_LEFT );
		BaseClass::SetEnabled( enabled );
	}

	void NavigateTo() OVERRIDE
	{
		if ( GraphicsScrollViewport *viewport = dynamic_cast<GraphicsScrollViewport *>( GetParent() ) )
			viewport->RevealRow( this );
		BaseClass::NavigateTo();
	}
protected:
	void PerformLayout() OVERRIDE
	{
		BaseClass::PerformLayout();
		vgui::Label *value = static_cast<vgui::Label *>( FindChildByName( "LblSliderText" ) );
		vgui::Panel *bar = FindChildByName( "PrgValue" );
		if ( !value || !bar )
			return;
		int x, y;
		bar->GetPos( x, y );
		const int width = vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), 56 );
		value->SetBounds( x - width, 0, width - 5, GetTall() );
		value->SetContentAlignment( vgui::Label::a_east );
		value->SetFont( vgui::scheme()->GetIScheme( GetScheme() )->GetFont( "DefaultMedium", true ) );
		value->SetFgColor( Color( 216, 216, 216, 255 ) );
		RefreshValue( value );
	}
	void OnThink() OVERRIDE
	{
		BaseClass::OnThink();
		if ( GetValue() != m_flDisplayedValue )
			RefreshValue( static_cast<vgui::Label *>( FindChildByName( "LblSliderText" ) ) );
	}
private:
	void RefreshValue( vgui::Label *label )
	{
		if ( !label )
			return;
		m_flDisplayedValue = GetValue();
		label->SetText( CFmtStr( "%.3g", m_flDisplayedValue ) );
	}
	float m_flDisplayedValue;
};

static KeyValues *CopyGraphicsControl( KeyValues *layout, KeyValues *source, const char *name )
{
	KeyValues *control = source->MakeCopy();
	control->SetName( name );
	control->SetString( "fieldName", name );
	layout->AddSubKey( control );
	return control;
}

GraphicsDX12::GraphicsDX12( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name, "#GraphicsOptions_Title" ), m_nApplyY( 0 ), m_nSection( TAB_DISPLAY ), m_nCapabilities( 0 ),
	m_nContentHeight( 0 ), m_nScrollTarget( 0 ), m_flScroll( 0 ), m_flLastThink( 0 ),
	m_bLivePreview( false ), m_bDirty( false ), m_NrAvailability( DX12_NR_NEEDS_TEMPORAL_AA )
{
	g_pVGuiLocalize->AddFile( "resource/graphicsoptions_%language%.txt", NULL, false );
	LoadGraphicsGpuLocalization();
	g_pVGuiLocalize->AddFile( "resource/graphicsoptions_perf_%language%.txt", NULL, false );
	m_pViewport = new GraphicsScrollViewport( this );
	m_pScroll = new vgui::ScrollBar( this, "GraphicsScrollBar", true );
	m_pDetectedGpu = CreateGraphicsHint( m_pViewport, "GraphicsDetectedGpu", "" );
	m_pRecommendationStatus = CreateGraphicsHint( m_pViewport, "GraphicsRecommendationStatus", "" );
	m_pPerformanceSummary = CreateGraphicsHint( m_pViewport, "GraphicsPerformanceSummary", "#GraphicsOptions_PerformanceSummary" );
	m_pLiveHelp = CreateGraphicsHint( this, "GraphicsLiveHelp", "" );
	m_pApplyOptimized = new GraphicsPresetButton( m_pViewport );
	m_pApplyOptimized->SetDefaultTextColor( Color( 224, 215, 188, 255 ) );
	for ( int section = 0; section < TAB_COUNT; ++section )
	{
		m_nHeaderY[section] = -1;
		m_pHeaders[section] = new vgui::Label( m_pViewport, CFmtStr( "GraphicsHeader%d", section ), s_SectionTitles[section] );
		m_pHeaders[section]->SetMouseInputEnabled( false );
		m_pHeaders[section]->SetPaintBackgroundEnabled( false );
	}
	m_pNrRequirement = new vgui::Label( m_pViewport, "GraphicsNrRequirement", "" );
	m_pNrRequirement->SetMouseInputEnabled( false );
	m_pNrRequirement->SetPaintBackgroundEnabled( false );
	m_pNrRequirement->SetContentAlignment( vgui::Label::a_northwest );
	m_pNrRequirement->SetWrap( true );
	m_pNrRequirement->SetVisible( false );
}

vgui::Panel *GraphicsDX12::CreateControlByName( const char *name )
{
	if ( !Q_stricmp( name, "DropDownMenu" ) )
		return new GraphicsDropDown( this );
	if ( !Q_stricmp( name, "SliderControl" ) )
		return new GraphicsSlider( this );
	return BaseClass::CreateControlByName( name );
}

int GraphicsDX12::Scale( int value )
{
	return vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), value );
}

void GraphicsDX12::LoadControlSettings( const char *resource, const char *pathID, KeyValues *preloaded, KeyValues *conditions )
{
	KeyValues::AutoDelete native( preloaded ? preloaded->MakeCopy() : new KeyValues( "" ) );
	if ( !preloaded && !LoadTerrorUiResource( native, resource ) ) return;
	KeyValues *frame = native->FindKey( "Video" );
	KeyValues *drop = native->FindKey( "DrpFiltering" );
	KeyValues *slider = native->FindKey( "SldFilmGrain" );
	KeyValues *flyout = native->FindKey( "FlmFiltering" );
	KeyValues *done = native->FindKey( "BtnDone" );
	if ( !frame || !drop || !slider || !flyout || !done ) return;

	// Use the mounted Video screen's evaluated geometry and nested controls.
	// Only the row table supplies bindings and choices; no retail .res is copied
	// into the mod, and the normal frame scheme reload rebuilds this expansion.
	KeyValues::AutoDelete layout( new KeyValues( "GraphicsDX12" ) );
	CopyGraphicsControl( layout, frame, GetName() );
	if ( KeyValues *background = native->FindKey( "ImgBackground" ) )
		CopyGraphicsControl( layout, background, "ImgBackground" )->SetInt( "visible", !m_bLivePreview );

	int count;
	const RowDef_t *rows = GetRows( count );
	for ( int i = 0; i < count; ++i )
	{
		const RowDef_t &row = rows[i];
		if ( !IsRowAvailable( row ) && !( IsDX12Renderer() && ( row.flags & ROWF_DLSS_NR ) ) )
			continue;
		KeyValues *control = CopyGraphicsControl( layout, row.kind == ROW_SLIDER ? slider : drop, CFmtStr( "GraphicsRow%d", i ) );
		control->SetInt( "xpos", 0 );
		control->SetInt( "ypos", 0 );
		control->SetInt( "visible", 0 );
		control->SetInt( "autoResize", 0 );
		control->SetInt( "BtnDropButton/tabPosition", 0 );
		control->SetInt( "BtnDropButton/wideatopen", 0 );
		control->SetInt( "BtnDropButton/autoResize", 0 );
		control->SetString( "navUp", "" );
		control->SetString( "navDown", "" );
		control->SetString( "BtnDropButton/labelText", row.label );
		control->SetString( "BtnDropButton/tooltiptext", GraphicsRowHelp( row ) );
		control->SetString( "BtnDropButton/disabled_tooltiptext", "" );
		if ( row.kind == ROW_SLIDER )
		{
			control->SetString( "conCommand", row.cvars[0] );
			control->SetFloat( "minValue", row.sliderMin );
			control->SetFloat( "maxValue", row.sliderMax );
			control->SetFloat( "stepSize", row.sliderStep );
		}
		else
		{
			const CFmtStr flyoutName( "GraphicsFlyout%d", i );
			control->SetString( "BtnDropButton/command", flyoutName );
			CopyGraphicsControl( layout, flyout, flyoutName );
		}
	}
	KeyValues *back = CopyGraphicsControl( layout, done, "BtnGraphicsBack" );
	back->SetInt( "xpos", 0 );
	back->SetInt( "ypos", 0 );
	back->SetInt( "autoResize", 0 );
	back->SetString( "tooltiptext", "" );
	back->SetString( "navUp", "" );
	back->SetString( "navDown", "" );
	KeyValues::AutoDelete apply( back->MakeCopy() );
	apply->SetString( "fieldName", m_pApplyOptimized->GetName() );
	apply->SetInt( "visible", m_pApplyOptimized->IsVisible() );
	apply->SetInt( "tabPosition", 0 );
	apply->SetInt( "IgnoreButtonA", 0 );
	apply->SetString( "navLeft", "" );
	apply->SetString( "navRight", "" );
	apply->SetString( "labelText", "#GraphicsOptions_ApplyOptimized" );
	apply->SetString( "command", "ApplyOptimized" );
	apply->SetString( "tooltiptext", "#GraphicsOptions_ApplyOptimizedHelp" );

	FlyoutMenu::CloseActiveMenu();
	const bool refreshing = m_bRefreshing;
	m_bRefreshing = true;
	// Resource reloads bind direct children; reuse the existing clipped rows.
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		if ( m_Widgets[i].control.Get() )
			m_Widgets[i].control->SetParent( this );
	}
	m_pApplyOptimized->ApplySettings( apply );
	BaseClass::LoadControlSettings( resource, pathID, layout, conditions );
	m_Widgets.RemoveAll();
	for ( int i = 0; i < count; ++i )
	{
		vgui::Panel *control = FindChildByName( CFmtStr( "GraphicsRow%d", i ) );
		if ( !control )
			continue;
		Widget_t &widget = m_Widgets[m_Widgets.AddToTail()];
		widget.row = &rows[i];
		widget.rowIndex = i;
		widget.control = control;
		widget.y = 0;
		control->SetParent( m_pViewport );
		if ( DropDownMenu *dropdown = dynamic_cast<DropDownMenu *>( control ) )
		{
			dropdown->SetFlyoutHost( this );
			dropdown->SetFlyoutOpenCallback( PositionFlyout );
		}
	}
	m_bRefreshing = refreshing;
	ReadSettings( true );
	InvalidateLayout();
}

void GraphicsDX12::OnOpen()
{
	FlyoutMenu::CloseActiveMenu();
	m_bLivePreview = IsDX12Renderer() && engine->IsInGame() && !engine->IsLevelMainMenuBackground();
	SetFooterEnabled( !m_bLivePreview );
	UpscalerDX12_RequestCapabilityProbe();
	if ( !LoadLayout( "resource/ui/l4d360ui/video.res" ) ) { NavigateBack(); return; }
	ReadRecommendation();
	ReadSettings();
	BaseClass::OnOpen();
	PerformLayout();
	FocusSection();
	UpdateFooter();
}

void GraphicsDX12::SetSection( int section )
{
	m_nSection = clamp( section, 0, TAB_COUNT - 1 );
}

void GraphicsDX12::ReadSettings( bool rebuildChoices )
{
	const bool refreshing = m_bRefreshing;
	m_bRefreshing = true;
	const int previousCapabilities = m_nCapabilities;
	m_nCapabilities = UpscalerDX12_Capabilities();
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		Widget_t &widget = m_Widgets[i];
		const RowDef_t &row = *widget.row;
		if ( !IsRowAvailable( row ) )
			continue;
		if ( SliderControl *slider = dynamic_cast<SliderControl *>( widget.control.Get() ) )
		{
			slider->Reset();
			continue;
		}
		DropDownMenu *drop = dynamic_cast<DropDownMenu *>( widget.control.Get() );
		FlyoutMenu *menu = drop ? drop->GetFlyout() : NULL;
		if ( !menu ) continue;
		const int selected = row.kind == ROW_CHECK ? ( cvar->FindVar( row.cvars[0] )->GetBool() ? 1 : 0 ) : FindChoice( row );
		const CFmtStr selectedCommand( "GraphicsChoice_%d_%d", widget.rowIndex, selected );
		const bool providerChoicesChanged = previousCapabilities != m_nCapabilities && ( row.flags & ( ROWF_UPSCALER | ROWF_DLSS_NR ) );
		if ( rebuildChoices || providerChoicesChanged || !menu->FindChildButtonByCommand( selectedCommand ) )
		{
			menu->BeginDynamicRows();
			const int choices = row.kind == ROW_CHECK ? 2 : row.nChoices;
			for ( int choice = 0; choice < choices; ++choice )
			{
				if ( !IsChoiceAvailable( row, choice ) )
					continue;
				const char *label = row.kind == ROW_CHECK ? ( choice ? "#GraphicsOptions_On" : "#GraphicsOptions_Off" ) : row.choices[choice].label;
				L4D360HybridButton *item = menu->AddDynamicRow( CFmtStr( "BtnChoice%d", choice ), label, CFmtStr( "GraphicsChoice_%d_%d", widget.rowIndex, choice ) );
				if ( item )
				{
					item->SetTooltip( GraphicsRowHelp( row ) );
					item->SetWide( Scale( 210 ) );
				}
			}
			if ( selected < 0 )
				menu->AddDynamicRow( "BtnCustom", "#GraphicsOptions_Custom", selectedCommand, false );
			if ( vgui::Panel *background = menu->FindChildByName( "PnlBackground" ) )
				background->SetWide( Scale( 216 ) );
			menu->EndDynamicRows();
			menu->SetFlyoutMenuListener( this );
		}
		drop->SetCurrentSelection( selectedCommand );
	}
	m_bRefreshing = refreshing;
}

void GraphicsDX12::ReadRecommendation()
{
	using namespace GpuOptimizedSettings;
	const bool dx12 = IsDX12Renderer();
	Recommendation_t recommendation = {};
	const bool ready = dx12 && GetRecommendation( recommendation );
	m_pDetectedGpu->SetVisible( dx12 );
	m_pRecommendationStatus->SetVisible( ready );
	m_pPerformanceSummary->SetVisible( dx12 );
	m_pApplyOptimized->SetVisible( ready );
	m_pApplyOptimized->SetEnabled( ready );
	if ( !ready )
	{
		m_pDetectedGpu->SetText( "#GraphicsOptions_DetectedGpuUnavailable" );
		return;
	}

	wchar_t adapter[512] = {}, text[768];
	V_UTF8ToUnicode( recommendation.m_szAdapter, adapter, sizeof( adapter ) );
	const wchar_t *gpu = adapter[0] ? adapter : g_pVGuiLocalize->Find( "#GraphicsOptions_UnidentifiedGpu" );
	const wchar_t *detected = g_pVGuiLocalize->Find( "#GraphicsOptions_DetectedGpu" );
	if ( detected && gpu )
	{
		g_pVGuiLocalize->ConstructString( text, sizeof( text ), detected, 1, gpu );
		m_pDetectedGpu->SetText( text );
	}
	else
		m_pDetectedGpu->SetText( adapter );

	const char *sourceToken = "#GraphicsOptions_RecommendationUnknown";
	switch ( recommendation.m_Source )
	{
	case SOURCE_DATABASE:
		if ( recommendation.m_szEntry[0] )
			sourceToken = "#GraphicsOptions_RecommendationDatabase";
		break;
	case SOURCE_ESTIMATE: sourceToken = "#GraphicsOptions_RecommendationEstimate"; break;
	case SOURCE_INTEGRATED: sourceToken = "#GraphicsOptions_RecommendationIntegrated"; break;
	case SOURCE_UNKNOWN: break;
	}
	const char *presetToken = PresetToken( recommendation.m_Preset );
	const wchar_t *preset = g_pVGuiLocalize->Find( presetToken );
	const wchar_t *status = g_pVGuiLocalize->Find( sourceToken );
	if ( status && preset )
	{
		wchar_t entry[128] = {};
		V_UTF8ToUnicode( recommendation.m_szEntry, entry, sizeof( entry ) );
		g_pVGuiLocalize->ConstructString( text, sizeof( text ), status, 2, preset, entry );
		m_pRecommendationStatus->SetText( text );
	}
	else
		m_pRecommendationStatus->SetText( presetToken );
}

int GraphicsDX12::LayoutRecommendation( vgui::IScheme *uiScheme )
{
	const int wide = m_pViewport->GetWide();
	const int inset = Scale( 5 );
	const vgui::HFont medium = uiScheme->GetFont( "DefaultMedium", true );
	SizeGraphicsHint( m_pDetectedGpu, uiScheme->GetFont( "DefaultBold", true ), wide, inset, Scale( 2 ), Color( 224, 215, 188, 255 ) );
	int y = Scale( 4 ) + m_pDetectedGpu->GetTall();
	if ( m_pRecommendationStatus->IsVisible() )
	{
		SizeGraphicsHint( m_pRecommendationStatus, medium, wide, inset, Scale( 2 ), Color( 216, 216, 216, 255 ) );
		y += m_pRecommendationStatus->GetTall();
	}
	m_nApplyY = y;
	if ( m_pApplyOptimized->IsVisible() )
	{
		m_pApplyOptimized->SetWide( wide );
		y += m_pApplyOptimized->GetTall() + Scale( 3 );
	}
	SizeGraphicsHint( m_pPerformanceSummary, medium, wide, inset, Scale( 4 ), Color( 190, 190, 190, 255 ) );
	return y + m_pPerformanceSummary->GetTall();
}

void GraphicsDX12::PerformLayout()
{
	int wide, tall;
	vgui::surface()->GetScreenSize( wide, tall );
	if ( m_bLivePreview )
	{
		// The frame ends at the left pane edge; no UI texture covers the right.
		wide = clamp( Scale( 382 ), wide * 2 / 5, wide * 3 / 5 );
		SetBounds( 0, 0, wide, tall );
		m_nContentTall = tall;
	}
	else
	{
		m_nContentTall = MIN( tall, Scale( 448 ) );
		CenterDialogOnScreen();
	}
	BaseClass::PerformLayout();
	const int margin = Scale( 18 );
	const int listWide = MIN( GetWide() - 2 * margin, Scale( 428 ) );
	const int x = m_bLivePreview ? margin : ( GetWide() - listWide ) / 2;
	const int top = Scale( m_bLivePreview ? 64 : 98 );
	// Live preview has no full-screen footer; keep focused-row help in its pane.
	const int helpTall = m_bLivePreview ? Scale( 48 ) : 0;
	const int bottom = MAX( top + 1, m_nContentTall - Scale( 41 ) - helpTall );
	const int barWide = Scale( 12 );
	m_pViewport->SetBounds( x, top, MAX( 1, listWide - barWide - Scale( 6 ) ), bottom - top );
	m_pScroll->SetBounds( x + listWide - barWide, top, barWide, bottom - top );
	m_pScroll->SetButtonPressedScrollValue( Scale( 21 ) );
	if ( vgui::Panel *done = FindChildByName( "BtnGraphicsBack" ) )
		done->SetPos( x, bottom + helpTall + Scale( 9 ) );
	m_pLiveHelp->SetVisible( m_bLivePreview );
	m_pLiveHelp->SetBounds( x, bottom + Scale( 3 ), m_pViewport->GetWide(), helpTall );
	m_pLiveHelp->SetTextInset( Scale( 5 ), 0 );
	m_pLiveHelp->SetFont( vgui::scheme()->GetIScheme( GetScheme() )->GetFont( "DefaultMedium", true ) );
	m_pLiveHelp->SetFgColor( Color( 216, 216, 216, 255 ) );
	if ( vgui::Panel *background = FindChildByName( "ImgBackground" ) )
		background->SetVisible( !m_bLivePreview );
	LayoutRows();
}

void GraphicsDX12::LayoutRows()
{
	vgui::Panel *previous = NULL;
	vgui::Panel *first = NULL;
	vgui::IScheme *uiScheme = vgui::scheme()->GetIScheme( GetScheme() );
	m_NrAvailability = UpscalerDX12_NrAvailability();
	const bool bNrEnabled = m_NrAvailability == DX12_NR_READY;
	m_pNrRequirement->SetVisible( false );
	bool bMoveFocusToDone = false;
	int y = m_pDetectedGpu->IsVisible() ? LayoutRecommendation( uiScheme ) : 0;
	if ( m_pApplyOptimized->IsVisible() )
		first = previous = m_pApplyOptimized;
	for ( int order = 0; order < ARRAYSIZE( s_SectionOrder ); ++order )
	{
		const int section = s_SectionOrder[order];
		vgui::Label *header = m_pHeaders[section];
		header->SetVisible( false );
		m_nHeaderY[section] = -1;
		for ( int i = 0; i < m_Widgets.Count(); ++i )
		{
			Widget_t &widget = m_Widgets[i];
			vgui::Panel *row = widget.control.Get();
			if ( widget.row->tab != section || !row )
				continue;
			const bool show = IsRowAvailable( *widget.row );
			row->SetVisible( show );
			if ( !show )
				continue;
			if ( m_nHeaderY[section] < 0 )
			{
				if ( y > 0 )
					y += Scale( 9 );
				m_nHeaderY[section] = y;
				header->SetVisible( true );
				header->SetSize( m_pViewport->GetWide(), Scale( 22 ) );
				header->SetFont( uiScheme->GetFont( "DefaultBold", true ) );
				header->SetFgColor( Color( 224, 215, 188, 255 ) );
				y += header->GetTall();
				if ( section == TAB_NEURAL && !bNrEnabled )
				{
					const char *note = "#GraphicsOptions_NrRequiresTemporalAA";
					const vgui::HFont font = uiScheme->GetFont( "DefaultMedium", true );
					int textWide, textTall;
					wchar_t text[256];
					m_pNrRequirement->SetText( note );
					m_pNrRequirement->GetText( text, sizeof( text ) );
					vgui::surface()->GetTextSize( font, text, textWide, textTall );
					if ( textWide > m_pViewport->GetWide() )
						textTall *= 2;
					m_pNrRequirement->SetSize( m_pViewport->GetWide(), textTall + Scale( 5 ) );
					m_pNrRequirement->SetFont( font );
					m_pNrRequirement->SetFgColor( Color( 190, 190, 190, 255 ) );
					m_pNrRequirement->SetVisible( true );
					y += m_pNrRequirement->GetTall();
				}
			}
			widget.y = y;
			row->SetWide( m_pViewport->GetWide() );
			if ( L4D360HybridButton *button = dynamic_cast<L4D360HybridButton *>( row->FindChildByName( "BtnDropButton" ) ) )
			{
				button->SetWide( row->GetWide() );
				// Native enabled text is gray, not disabled; improve contrast
				// over the main-menu artwork without changing the focus style.
				button->SetDefaultTextColor( Color( 210, 210, 210, 255 ) );
			}
			y += MAX( Scale( 21 ), row->GetTall() + Scale( 3 ) );
			if ( widget.row->flags & ROWF_DLSS_NR )
			{
				if ( !bNrEnabled )
				{
					FlyoutMenu *menu = FlyoutMenu::GetActiveMenu();
					if ( menu && menu->GetParentButton() && menu->GetParentButton()->GetParent() == row )
						menu->CloseMenu();
					if ( m_hLastFocus.Get() == row )
					{
						row->NavigateFrom();
						m_hLastFocus = NULL;
						bMoveFocusToDone = true;
					}
				}
				row->SetEnabled( bNrEnabled );
				row->SetMouseInputEnabled( bNrEnabled );
				row->SetKeyBoardInputEnabled( bNrEnabled );
				if ( vgui::Panel *button = row->FindChildByName( "BtnDropButton" ) )
				{
					// The hybrid disabled font blurs away thin glyphs. Dim the
					// normal text with the row while suppressing both inputs.
					button->SetEnabled( true );
					button->SetMouseInputEnabled( bNrEnabled && widget.row->kind != ROW_SLIDER );
					button->SetKeyBoardInputEnabled( bNrEnabled );
				}
				const int kDisabledNrRowAlpha = 135;
				row->SetAlpha( bNrEnabled ? 255 : kDisabledNrRowAlpha );
				if ( !bNrEnabled )
					continue;
			}
			row->SetNavUp( previous );
			if ( vgui::Panel *button = row->FindChildByName( "BtnDropButton" ) )
				button->SetNavUp( previous );
			if ( previous )
			{
				previous->SetNavDown( row );
				if ( vgui::Panel *button = previous->FindChildByName( "BtnDropButton" ) )
					button->SetNavDown( row );
			}
			else
				first = row;
			previous = row;
		}
	}
	if ( vgui::Panel *done = FindChildByName( "BtnGraphicsBack" ) )
	{
		done->SetNavUp( previous );
		done->SetNavDown( first );
		if ( previous )
		{
			previous->SetNavDown( done );
			if ( vgui::Panel *button = previous->FindChildByName( "BtnDropButton" ) )
				button->SetNavDown( done );
		}
		if ( first )
		{
			first->SetNavUp( done );
			if ( vgui::Panel *button = first->FindChildByName( "BtnDropButton" ) )
				button->SetNavUp( done );
		}
		if ( bMoveFocusToDone )
			done->NavigateTo();
	}
	m_nContentHeight = y;
	const int view = m_pViewport->GetTall();
	m_pScroll->SetRange( 0, MAX( view, y ) );
	m_pScroll->SetRangeWindow( view );
	m_pScroll->SetVisible( y > view );
	m_nScrollTarget = clamp( m_nScrollTarget, 0, MAX( 0, y - view ) );
	m_pScroll->SetValue( m_nScrollTarget );
	m_flScroll = MIN( m_flScroll, float( MAX( 0, y - view ) ) );
	PositionRows();
}

void GraphicsDX12::PositionRows()
{
	const int offset = (int)( m_flScroll + 0.5f );
	for ( int section = 0; section < TAB_COUNT; ++section )
		m_pHeaders[section]->SetPos( 0, m_nHeaderY[section] - offset );
	m_pNrRequirement->SetPos( 0, m_nHeaderY[TAB_NEURAL] + m_pHeaders[TAB_NEURAL]->GetTall() - offset );
	m_pDetectedGpu->SetPos( 0, Scale( 4 ) - offset );
	m_pRecommendationStatus->SetPos( 0, Scale( 4 ) + m_pDetectedGpu->GetTall() - offset );
	m_pApplyOptimized->SetPos( 0, m_nApplyY - offset );
	const int applyTall = m_pApplyOptimized->IsVisible() ? m_pApplyOptimized->GetTall() + Scale( 3 ) : 0;
	m_pPerformanceSummary->SetPos( 0, m_nApplyY + applyTall - offset );
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		if ( m_Widgets[i].control.Get() )
			m_Widgets[i].control->SetPos( 0, m_Widgets[i].y - offset );
	}
}

void GraphicsDX12::OnMouseWheeled( int delta )
{
	FlyoutMenu::CloseActiveMenu();
	m_nScrollTarget = clamp( m_pScroll->GetValue() - delta * Scale( 42 ), 0, MAX( 0, m_nContentHeight - m_pViewport->GetTall() ) );
	m_pScroll->SetValue( m_nScrollTarget );
}

void GraphicsDX12::OnThink()
{
	BaseClass::OnThink();
	if ( !IsVisible() )
		return;
	if ( m_nCapabilities != UpscalerDX12_Capabilities() )
	{
		FlyoutMenu::CloseActiveMenu();
		ReadSettings();
		LayoutRows();
	}
	else if ( m_NrAvailability != UpscalerDX12_NrAvailability() )
	{
		LayoutRows();
	}
	if ( m_bLivePreview )
		UpdateLiveHelp();
	const double now = Plat_FloatTime();
	const float dt = clamp( float( now - m_flLastThink ), 0.0f, 0.05f );
	m_flLastThink = now;
	if ( m_pScroll->GetValue() != m_nScrollTarget )
	{
		FlyoutMenu::CloseActiveMenu();
		SetScroll( m_pScroll->GetValue() );
	}
	else if ( m_flScroll != m_nScrollTarget )
	{
		m_flScroll += ( m_nScrollTarget - m_flScroll ) * MIN( 1.0f, dt * 18.0f );
		if ( fabsf( m_flScroll - m_nScrollTarget ) < 0.5f )
			m_flScroll = m_nScrollTarget;
		PositionRows();
	}
}

void GraphicsDX12::UpdateLiveHelp()
{
	vgui::Panel *row = m_hLastFocus.Get();
	L4D360HybridButton *button = dynamic_cast<L4D360HybridButton *>( row );
	if ( row && !button )
		button = dynamic_cast<L4D360HybridButton *>( row->FindChildByName( "BtnDropButton" ) );
	const char *help = button ? button->GetTooltip() : "";
	if ( Q_strcmp( m_LiveHelpToken.String(), help ) )
	{
		m_LiveHelpToken = help;
		m_pLiveHelp->SetText( help );
	}
}

void GraphicsDX12::SetScroll( int value )
{
	m_nScrollTarget = clamp( value, 0, MAX( 0, m_nContentHeight - m_pViewport->GetTall() ) );
	m_pScroll->SetValue( m_nScrollTarget );
	m_flScroll = m_nScrollTarget;
	PositionRows();
}

void GraphicsDX12::RevealRow( vgui::Panel *row )
{
	if ( m_hLastFocus.Get() && m_hLastFocus.Get() != row )
		m_hLastFocus->NavigateFrom();
	m_hLastFocus = row;
	if ( row == m_pApplyOptimized )
	{
		RevealControl( m_nApplyY, row->GetTall() );
		return;
	}
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		if ( m_Widgets[i].control.Get() != row )
			continue;
		RevealControl( m_Widgets[i].y, row->GetTall() );
		break;
	}
}

void GraphicsDX12::RevealControl( int y, int tall )
{
	if ( y < m_flScroll )
		SetScroll( y );
	else if ( y + tall > m_flScroll + m_pViewport->GetTall() )
		SetScroll( y + tall - m_pViewport->GetTall() );
	else
		SetScroll( (int)m_flScroll );
}

void GraphicsDX12::FocusSection()
{
	const int section = m_nHeaderY[m_nSection] >= 0 ? m_nSection : TAB_DISPLAY;
	SetScroll( section == TAB_DISPLAY ? 0 : MAX( 0, m_nHeaderY[section] ) );
	if ( m_hLastFocus.Get() )
		m_hLastFocus->NavigateFrom();
	if ( section == TAB_DISPLAY && m_pApplyOptimized->IsVisible() )
	{
		m_pApplyOptimized->NavigateTo();
		return;
	}
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		vgui::Panel *row = m_Widgets[i].control.Get();
		if ( m_Widgets[i].row->tab == section && row && row->IsVisible() && row->IsEnabled() )
		{
			row->NavigateTo();
			return;
		}
	}
	if ( vgui::Panel *done = FindChildByName( "BtnGraphicsBack" ) )
		done->NavigateTo();
}

void GraphicsDX12::PositionFlyout( DropDownMenu *, FlyoutMenu *flyout )
{
	GraphicsDX12 *frame = static_cast<GraphicsDX12 *>( flyout->GetParent() );
	int x, y;
	flyout->GetPos( x, y );
	if ( x + flyout->GetWide() > frame->GetWide() || frame->m_bLivePreview )
		x = frame->GetWide() - flyout->GetWide() - frame->Scale( 18 );
	const int minY = frame->Scale( 54 );
	y = clamp( y, minY, MAX( minY, frame->m_nContentTall - flyout->GetTall() - frame->Scale( 12 ) ) );
	flyout->SetPos( MAX( 0, x ), y );
}

void GraphicsDX12::PaintBackground()
{
	DrawDialogBackground( "#GraphicsOptions_Title", NULL, m_bLivePreview ? NULL : "#L4D360UI_AudioVideo_Desc" );
}

void GraphicsDX12::UpdateFooter()
{
	if ( m_bLivePreview )
		return;
	if ( CL4DFooterPanel *footer = GetL4DFooterPanel( this ) )
	{
		footer->SetButtons( FB_BUTTON_A | FB_BUTTON_B, 4, true );
		footer->SetButtonText( FB_BUTTON_A, "#L4D360UI_Select" );
		footer->SetButtonText( FB_BUTTON_B, "#L4D360UI_Controller_Done" );
	}
}

void GraphicsDX12::OnFlyoutMenuClosed( vgui::Panel *returnTo ) { UpdateFooter(); }

void GraphicsDX12::OnCommand( const char *command )
{
	if ( m_bRefreshing ) return;
	if ( !Q_stricmp( command, "ApplyOptimized" ) )
	{
		if ( !m_pApplyOptimized->IsVisible() || !m_pApplyOptimized->IsEnabled() )
			return;
		FlyoutMenu::CloseActiveMenu();
		if ( GpuOptimizedSettings::ApplyRecommended() )
		{
			m_bDirty = true;
			SaveSettings();
			ReadSettings();
			LayoutRows();
			UpdateFooter();
		}
		return;
	}
	if ( const char *selection = StringAfterPrefix( command, "GraphicsChoice_" ) )
	{
		const int rowIndex = atoi( selection );
		const char *choiceText = Q_strstr( selection, "_" );
		if ( !choiceText ) return;
		const int choice = atoi( choiceText + 1 );
		int count;
		const RowDef_t *rows = GetRows( count );
		if ( rowIndex < 0 || rowIndex >= count || choice < 0 ) return;
		const RowDef_t &row = rows[rowIndex];
		if ( !IsRowAvailable( row ) || !IsChoiceAvailable( row, choice ) ) return;
		if ( ( row.flags & ROWF_DLSS_NR ) && UpscalerDX12_NrAvailability() != DX12_NR_READY )
			return;
		int cvars = RowCvarCount( row );
		if ( ( row.flags & ROWF_OFF_ENABLE_ONLY ) && choice == 0 ) cvars = 1;
		for ( int c = 0; c < cvars; ++c )
			cvar->FindVar( row.cvars[c] )->SetValue( row.kind == ROW_CHECK ? (float)choice : row.choices[choice].v[c] );
		m_bDirty = true;
		Select( CFmtStr( "GraphicsRow%d", rowIndex ), command );
		if ( m_NrAvailability != UpscalerDX12_NrAvailability() )
			LayoutRows();
		return;
	}
	BaseClass::OnCommand( command );
}

void GraphicsDX12::OnSliderMoved( KeyValues *data ) { m_bDirty = true; }

void GraphicsDX12::SaveSettings()
{
	if ( !m_bDirty ) return;
	if ( IsDX12Renderer() )
	{
		if ( ConVar *aa = cvar->FindVar( "mat_antialias" ) ) aa->SetValue( 0 );
		if ( ConVar *quality = cvar->FindVar( "mat_aaquality" ) ) quality->SetValue( 0 );
	}
	engine->ClientCmd_Unrestricted( "mat_savechanges\n" );
	SaveConfig();
	m_bDirty = false;
}

void GraphicsDX12::OnClose()
{
	SaveSettings();
	CloseWithoutSaving();
}
}
