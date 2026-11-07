//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_graphics_dx12.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "tier1/fmtstr.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "vgui_controls/Label.h"
#include "tier0/memdbgon.h"

namespace L4D360UI
{
using namespace GraphicsOptions;
static const int GRAPHICS_ROWS_PER_PAGE = 7;
static const char *s_SectionTitles[TAB_COUNT] =
{
	"#GraphicsOptions_TabDisplay", "#GraphicsOptions_TabLighting",
	"#GraphicsOptions_TabPost", "#GraphicsOptions_TabFramegen"
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
	BaseClass( parent, name, "#GraphicsOptions_Title" ), m_nSection( TAB_DISPLAY ), m_nPage( 0 ),
	m_nRowX( 0 ), m_nRowY( 0 ), m_nRowPitch( 0 ), m_bDirty( false )
{
	g_pVGuiLocalize->AddFile( "resource/graphicsoptions_%language%.txt", NULL, false );
}

void GraphicsDX12::LoadControlSettings( const char *resource, const char *pathID, KeyValues *preloaded, KeyValues *conditions )
{
	KeyValues::AutoDelete native( preloaded ? preloaded->MakeCopy() : new KeyValues( "" ) );
	if ( !preloaded && !LoadTerrorUiResource( native, resource ) ) return;
	KeyValues *frame = native->FindKey( "Video" );
	KeyValues *drop = native->FindKey( "DrpFiltering" );
	KeyValues *slider = native->FindKey( "SldFilmGrain" );
	KeyValues *flyout = native->FindKey( "FlmFiltering" );
	KeyValues *button = native->FindKey( "BtnAdvanced" );
	KeyValues *done = native->FindKey( "BtnDone" );
	if ( !frame || !drop || !slider || !flyout || !button || !done ) return;

	// Use the mounted Video screen's evaluated geometry and nested controls.
	// Only the row table supplies bindings and choices; no retail .res is copied
	// into the mod, and the normal frame scheme reload rebuilds this expansion.
	KeyValues::AutoDelete layout( new KeyValues( "GraphicsDX12" ) );
	CopyGraphicsControl( layout, frame, GetName() );
	if ( KeyValues *background = native->FindKey( "ImgBackground" ) )
		CopyGraphicsControl( layout, background, "ImgBackground" );
	KeyValues *section = CopyGraphicsControl( layout, drop, "DrpSection" );
	section->SetInt( "ypos", 110 );
	section->SetInt( "visible", 1 );
	section->SetString( "BtnDropButton/labelText", "#GraphicsOptions_Section" );
	section->SetString( "BtnDropButton/tooltiptext", "#GraphicsOptions_Title" );
	section->SetString( "BtnDropButton/command", "FlmSection" );
	CopyGraphicsControl( layout, flyout, "FlmSection" );

	int count;
	const RowDef_t *rows = GetRows( count );
	for ( int i = 0; i < count; ++i )
	{
		const RowDef_t &row = rows[i];
		if ( !IsRowAvailable( row ) ) continue;
		KeyValues *control = CopyGraphicsControl( layout, row.kind == ROW_SLIDER ? slider : drop, CFmtStr( "GraphicsRow%d", i ) );
		control->SetInt( "ypos", 140 );
		control->SetInt( "visible", 0 );
		control->SetString( "BtnDropButton/labelText", row.label );
		control->SetString( "BtnDropButton/tooltiptext", row.note ? row.note : "" );
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
	const char *names[] = { "BtnPreviousPage", "BtnNextPage", "BtnGraphicsBack" };
	const char *labels[] = { "#GraphicsOptions_PreviousPage", "#GraphicsOptions_NextPage", "#L4D360UI_Done" };
	const char *commands[] = { "GraphicsPreviousPage", "GraphicsNextPage", "Back" };
	for ( int i = 0; i < ARRAYSIZE( names ); ++i )
	{
		KeyValues *control = CopyGraphicsControl( layout, i == 2 ? done : button, names[i] );
		control->SetInt( "ypos", 300 + i * 20 );
		control->SetString( "labelText", labels[i] );
		control->SetString( "tooltiptext", "" );
		control->SetString( "command", commands[i] );
	}

	KeyValues *status = new KeyValues( "LblGraphicsPage" );
	status->SetString( "ControlName", "Label" );
	status->SetString( "fieldName", "LblGraphicsPage" );
	status->SetString( "xpos", drop->GetString( "xpos" ) );
	status->SetInt( "ypos", 280 );
	status->SetInt( "wide", drop->GetInt( "wide" ) );
	status->SetInt( "tall", 30 );
	status->SetInt( "wrap", 1 );
	status->SetString( "font", "DefaultMedium" );
	status->SetString( "labelText", "" );
	layout->AddSubKey( status );
	const bool refreshing = m_bRefreshing;
	m_bRefreshing = true;
	BaseClass::LoadControlSettings( resource, pathID, layout, conditions );
	m_Widgets.RemoveAll();
	for ( int i = 0; i < count; ++i )
	{
		if ( !IsRowAvailable( rows[i] ) ) continue;
		vgui::Panel *control = FindChildByName( CFmtStr( "GraphicsRow%d", i ) );
		if ( !control ) continue;
		Widget_t widget;
		widget.row = &rows[i];
		widget.rowIndex = i;
		widget.control = control;
		m_Widgets.AddToTail( widget );
		control->GetPos( m_nRowX, m_nRowY );
	}
	m_nRowPitch = vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), 20 );
	m_bRefreshing = refreshing;
	ReadSettings();
	RefreshPage();
}

void GraphicsDX12::OnOpen()
{
	FlyoutMenu::CloseActiveMenu();
	if ( !LoadLayout( "resource/ui/l4d360ui/video.res" ) ) { NavigateBack(); return; }
	ReadSettings();
	BaseClass::OnOpen();
	RefreshPage( true );
	CenterDialogOnScreen();
}

int GraphicsDX12::SectionRowCount( int section ) const
{
	int count = 0;
	for ( int i = 0; i < m_Widgets.Count(); ++i )
		if ( m_Widgets[i].row->tab == section ) ++count;
	return count;
}

void GraphicsDX12::SetSection( int section )
{
	m_nSection = clamp( section, 0, TAB_COUNT - 1 );
	m_nPage = 0;
}

void GraphicsDX12::ReadSettings()
{
	const bool refreshing = m_bRefreshing;
	m_bRefreshing = true;
	DropDownMenu *sections = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpSection" ) );
	FlyoutMenu *sectionMenu = sections ? sections->GetFlyout() : NULL;
	if ( sectionMenu )
	{
		sectionMenu->BeginDynamicRows();
		for ( int tab = 0; tab < TAB_COUNT; ++tab )
			if ( SectionRowCount( tab ) ) sectionMenu->AddDynamicRow( CFmtStr( "BtnSection%d", tab ), s_SectionTitles[tab], CFmtStr( "GraphicsSection_%d", tab ) );
		sectionMenu->EndDynamicRows();
		sectionMenu->SetFlyoutMenuListener( this );
	}
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		Widget_t &widget = m_Widgets[i];
		const RowDef_t &row = *widget.row;
		if ( SliderControl *slider = dynamic_cast<SliderControl *>( widget.control.Get() ) )
		{
			slider->Reset();
			continue;
		}
		DropDownMenu *drop = dynamic_cast<DropDownMenu *>( widget.control.Get() );
		FlyoutMenu *menu = drop ? drop->GetFlyout() : NULL;
		if ( !menu ) continue;
		menu->BeginDynamicRows();
		const int choices = row.kind == ROW_CHECK ? 2 : row.nChoices;
		for ( int choice = 0; choice < choices; ++choice )
		{
			const char *label = row.kind == ROW_CHECK ? ( choice ? "#GraphicsOptions_On" : "#GraphicsOptions_Off" ) : row.choices[choice].label;
			L4D360HybridButton *item = menu->AddDynamicRow( CFmtStr( "BtnChoice%d", choice ), label, CFmtStr( "GraphicsChoice_%d_%d", widget.rowIndex, choice ) );
			if ( item )
			{
				item->SetTooltip( row.note ? row.note : "" );
				item->SetWide( vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), 210 ) );
			}
		}
		int selected = row.kind == ROW_CHECK ? ( cvar->FindVar( row.cvars[0] )->GetBool() ? 1 : 0 ) : FindChoice( row );
		if ( selected < 0 ) menu->AddDynamicRow( "BtnCustom", "#GraphicsOptions_Custom", CFmtStr( "GraphicsChoice_%d_-1", widget.rowIndex ), false );
		if ( vgui::Panel *background = menu->FindChildByName( "PnlBackground" ) )
			background->SetWide( vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), 216 ) );
		menu->EndDynamicRows();
		menu->SetFlyoutMenuListener( this );
		drop->SetCurrentSelection( CFmtStr( "GraphicsChoice_%d_%d", widget.rowIndex, selected ) );
	}
	m_bRefreshing = refreshing;
}

void GraphicsDX12::RefreshPage( bool focusSection )
{
	if ( !SectionRowCount( m_nSection ) )
		for ( int tab = 0; tab < TAB_COUNT; ++tab ) if ( SectionRowCount( tab ) ) { m_nSection = tab; break; }
	const int pageCount = MAX( 1, ( SectionRowCount( m_nSection ) + GRAPHICS_ROWS_PER_PAGE - 1 ) / GRAPHICS_ROWS_PER_PAGE );
	m_nPage = clamp( m_nPage, 0, pageCount - 1 );
	DropDownMenu *section = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpSection" ) );
	if ( !section ) return;
	Select( "DrpSection", CFmtStr( "GraphicsSection_%d", m_nSection ) );
	section->SetVisible( m_Widgets.Count() > 0 );
	vgui::Panel *navigation[GRAPHICS_ROWS_PER_PAGE + 4];
	int navigationCount = 0;
	if ( m_Widgets.Count() ) navigation[navigationCount++] = section;
	int ordinal = 0, visible = 0;
	for ( int i = 0; i < m_Widgets.Count(); ++i )
	{
		Widget_t &widget = m_Widgets[i];
		const bool inSection = widget.row->tab == m_nSection;
		const bool show = inSection && ordinal / GRAPHICS_ROWS_PER_PAGE == m_nPage;
		if ( inSection ) ++ordinal;
		if ( !widget.control.Get() ) continue;
		widget.control->SetVisible( show );
		if ( show )
		{
			widget.control->SetPos( m_nRowX, m_nRowY + visible++ * m_nRowPitch );
			navigation[navigationCount++] = widget.control.Get();
		}
	}
	const char *buttons[] = { "BtnPreviousPage", "BtnNextPage", "BtnGraphicsBack" };
	for ( int i = 0; i < ARRAYSIZE( buttons ); ++i )
	{
		vgui::Panel *button = FindChildByName( buttons[i] );
		const bool show = i == 2 || ( i == 0 ? m_nPage > 0 : m_nPage + 1 < pageCount );
		if ( button ) { button->SetVisible( show ); if ( show ) navigation[navigationCount++] = button; }
	}
	for ( int i = 0; i < navigationCount; ++i )
	{
		navigation[i]->SetNavUp( navigation[( i + navigationCount - 1 ) % navigationCount] );
		navigation[i]->SetNavDown( navigation[( i + 1 ) % navigationCount] );
	}
	if ( vgui::Label *status = dynamic_cast<vgui::Label *>( FindChildByName( "LblGraphicsPage" ) ) )
	{
		if ( !m_Widgets.Count() ) status->SetText( "#GraphicsOptions_Empty" );
		else if ( pageCount > 1 )
		{
			wchar_t page[16], total[16], text[64];
			g_pVGuiLocalize->ConvertANSIToUnicode( CFmtStr( "%d", m_nPage + 1 ), page, sizeof( page ) );
			g_pVGuiLocalize->ConvertANSIToUnicode( CFmtStr( "%d", pageCount ), total, sizeof( total ) );
			g_pVGuiLocalize->ConstructString( text, sizeof( text ), g_pVGuiLocalize->Find( "#GraphicsOptions_Page" ), 2, page, total );
			status->SetText( text );
		}
		else status->SetText( "" );
	}
	UpdateFooter();
	if ( focusSection )
	{
		if ( m_hLastFocus.Get() ) m_hLastFocus->NavigateFrom();
		navigation[0]->NavigateTo();
		m_hLastFocus = navigation[0];
	}
}

void GraphicsDX12::PaintBackground()
{
	DrawDialogBackground( "#GraphicsOptions_Title", NULL, "#L4D360UI_AudioVideo_Desc", NULL, NULL );
}

void GraphicsDX12::UpdateFooter()
{
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
	if ( const char *section = StringAfterPrefix( command, "GraphicsSection_" ) )
	{
		SetSection( atoi( section ) );
		RefreshPage( true );
		return;
	}
	if ( !Q_stricmp( command, "GraphicsPreviousPage" ) || !Q_stricmp( command, "GraphicsNextPage" ) )
	{
		m_nPage += !Q_stricmp( command, "GraphicsNextPage" ) ? 1 : -1;
		RefreshPage( true );
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
		if ( !IsRowAvailable( row ) || choice >= ( row.kind == ROW_CHECK ? 2 : row.nChoices ) ) return;
		int cvars = RowCvarCount( row );
		if ( ( row.flags & ROWF_OFF_ENABLE_ONLY ) && choice == 0 ) cvars = 1;
		for ( int c = 0; c < cvars; ++c )
			cvar->FindVar( row.cvars[c] )->SetValue( row.kind == ROW_CHECK ? (float)choice : row.choices[choice].v[c] );
		m_bDirty = true;
		Select( CFmtStr( "GraphicsRow%d", rowIndex ), command );
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
