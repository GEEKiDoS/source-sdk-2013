//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_customcampaigns.h"
#include "l4d2/ui/basemodui/terror_basemodui_panellist.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/Label.h"
#include "tier1/fmtstr.h"
#include "tier0/memdbgon.h"

namespace L4D360UI
{
CustomCampaignListItem::CustomCampaignListItem( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name ), m_hNameFont( vgui::INVALID_FONT ), m_DisabledColor( 0, 0, 0, 255 ), m_bSelected( false )
{
	SetProportional( true );
	m_pName = new vgui::Label( this, "LblName", "" );
	m_pName->SetMouseInputEnabled( false );
	m_Mission[0] = 0;
}
void CustomCampaignListItem::SetMission( KeyValues *mission )
{
	Q_strncpy( m_Mission, mission->GetString( "Name", mission->GetName() ), sizeof( m_Mission ) );
	m_pName->SetText( mission->GetString( "DisplayTitle", "" ) );
}
void CustomCampaignListItem::SetEnabled( bool enabled )
{
	if ( !enabled && m_pName ) m_pName->SetFgColor( m_DisabledColor );
	BaseClass::SetEnabled( enabled );
}
void CustomCampaignListItem::ApplySchemeSettings( vgui::IScheme *scheme )
{
	const bool enabled = IsEnabled();
	BaseClass::ApplySchemeSettings( scheme );
	// G2B350 caches the mounted native row resource once for this family.
	static KeyValues::AutoDelete data( new KeyValues( "" ) );
	static bool loaded = false;
	if ( !loaded ) loaded = LoadTerrorUiResource( data, "resource/ui/l4d360ui/customcampaignlistitem.res" );
	if ( loaded ) LoadControlSettings( "resource/ui/l4d360ui/customcampaignlistitem.res", "GAME", data );
	BaseClass::SetEnabled( enabled );
	m_DisabledColor = scheme->GetColor( "Label.DisabledFgColor2", Color( 0, 0, 0, 255 ) );
	SetBgColor( scheme->GetColor( "Button.BgColor", Color( 64, 64, 64, 255 ) ) );
	m_hNameFont = scheme->GetFont( "DefaultLarge", true );
	if ( !IsEnabled() ) m_pName->SetFgColor( m_DisabledColor );
}
void CustomCampaignListItem::OnPanelSelected() { m_bSelected = true; }
void CustomCampaignListItem::OnPanelUnSelected() { m_bSelected = false; }
void CustomCampaignListItem::OnMousePressed( vgui::MouseCode code )
{
	if ( code != MOUSE_LEFT ) return;
	GenericPanelList *list = NULL;
	for ( vgui::Panel *panel = GetParent(); panel && !list; panel = panel->GetParent() ) list = dynamic_cast<GenericPanelList *>( panel );
	if ( !list ) return;
	for ( int i = 0; i < list->GetPanelItemCount(); ++i )
		if ( list->GetPanelItem( i ) == this ) { list->SelectPanelItem( i, true ); break; }
}
void CustomCampaignListItem::PaintBackground()
{
	if ( !m_bSelected ) return;
	vgui::ISurface *surface = vgui::surface();
	const int half = (int)( GetWide() * 0.5f );
	surface->DrawSetColor( Color( 240, 0, 0, 255 ) );
	surface->DrawFilledRectFade( 0, 0, half, 2, 0, 255, true );
	surface->DrawFilledRectFade( half, 0, GetWide(), 2, 255, 0, true );
	surface->DrawFilledRectFade( 0, GetTall() - 2, half, GetTall(), 0, 255, true );
	surface->DrawFilledRectFade( half, GetTall() - 2, GetWide(), GetTall(), 255, 0, true );
	wchar_t text[120]; m_pName->GetText( text, sizeof( text ) ); // Native G2B440 passes240 bytes.
	int textWide, textTall; surface->GetTextSize( m_hNameFont, text, textWide, textTall );
	const int wide = textWide + vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), 75 );
	const int mid = (int)( wide * 0.5f );
	surface->DrawFilledRectFade( 0, 2, mid, GetTall() - 2, 0, 50, true );
	surface->DrawFilledRectFade( mid, 2, wide, GetTall() - 2, 50, 0, true );
}

CustomCampaigns::CustomCampaigns( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name, true, true, false, true ), m_pSettings( NULL ), m_bAnyInstalled( false ), m_bAnyUnsupported( false ), m_bAnySupported( false )
{
	SetDeleteSelfOnClose( true );
	SetProportional( true );
	m_pCampaigns = new GenericPanelList( this, "GplCustomCampaigns" );
	m_pCampaigns->SetScrollBarVisible( true );
	LoadLayout( "resource/ui/l4d360ui/customcampaigns.res" );
}
CustomCampaigns::~CustomCampaigns()
{
	if ( m_pSettings ) m_pSettings->deleteThis(); // G2B6A0 owns the +708 copy.
}
void CustomCampaigns::SetDataSettings( KeyValues *settings )
{
	// G2B090 replaces the derived owner's copy. The input remains caller-owned.
	if ( m_pSettings ) m_pSettings->deleteThis();
	m_pSettings = settings ? settings->MakeCopy() : NULL;
}
vgui::Panel *CustomCampaigns::CreateControlByName( const char *name )
{
	if ( !Q_stricmp( name, "GenericPanelList" ) ) return new GenericPanelList( this, "" );
	return BaseClass::CreateControlByName( name );
}
void CustomCampaigns::OnOpen()
{
	BaseClass::OnOpen();
	// G5AA20 is literally return 0 in this pinned original, so G2B6F0's
	// AddonSupportRequired pre-open branch is unreachable, not a fake check.
	PopulateCampaigns();
	UpdateAvailability();
}
void CustomCampaigns::OnClose()
{
	if ( m_ReturnPanel.Get() ) { m_ReturnPanel->SetVisible( true ); m_ReturnPanel->NavigateTo(); }
	BaseClass::OnClose();
}
vgui::Panel *CustomCampaigns::NavigateBack()
{
	vgui::Panel *panel = m_ReturnPanel.Get();
	if ( panel ) { OnClose(); return panel; }
	return BaseClass::NavigateBack();
}
void CustomCampaigns::PaintBackground()
{
	DrawDialogBackground( "#L4D360UI_My_CustomCampaigns", NULL, "#L4D360UI_My_CustomCampaigns_Desc", NULL, NULL, false );
}
void CustomCampaigns::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	CenterDialogOnScreen();
	UpdateAvailability();
}

struct CustomCampaignRecord
{
	KeyValues *mission;
	bool enabled;
};
static int __cdecl CompareCustomCampaigns( const CustomCampaignRecord *a, const CustomCampaignRecord *b )
{
	if ( a->enabled != b->enabled ) return a->enabled ? -1 : 1;
	return Q_stricmp( a->mission->GetString( "Name", a->mission->GetName() ), b->mission->GetString( "Name", b->mission->GetName() ) );
}
void CustomCampaigns::PopulateCampaigns()
{
	m_pCampaigns->RemoveAllPanelItems();
	m_bAnyInstalled = m_bAnyUnsupported = m_bAnySupported = false;
	const char *mode = m_pSettings ? m_pSettings->GetString( "Game/mode", "coop" ) : "coop";
	KeyValues *missions = g_TerrorModes.GetMissionDictionary();
	CUtlVector<CustomCampaignRecord> records;
	for ( KeyValues *mission = missions ? missions->GetFirstTrueSubKey() : NULL; mission; mission = mission->GetNextTrueSubKey() )
	{
		if ( mission->GetBool( "BuiltIn", false ) ) continue;
		m_bAnyInstalled = true;
		CustomCampaignRecord record;
		record.mission = mission;
		record.enabled = mission->FindKey( CFmtStr( "Modes/%s/1", mode ) ) != NULL;
		m_bAnySupported |= record.enabled; m_bAnyUnsupported |= !record.enabled;
		records.AddToTail( record );
	}
	// G2B000: enabled campaigns first, then the actual mission identifier,
	// case-insensitively. No 14-row/add-on limit and no localized-title sort.
	records.Sort( CompareCustomCampaigns );
	FOR_EACH_VEC( records, i )
	{
		CustomCampaignListItem *row = new CustomCampaignListItem( m_pCampaigns, "CustomCampaignListItem" );
		m_pCampaigns->AddPanelItem( row );
		row->InvalidateLayout( true, true );
		row->SetMission( records[i].mission ); row->SetEnabled( records[i].enabled );
	}
}
void CustomCampaigns::UpdateAvailability()
{
	vgui::Label *notice = dynamic_cast<vgui::Label *>( FindChildByName( "LblNoCampaignsFound" ) );
	const char *mode = m_pSettings ? m_pSettings->GetString( "Game/mode", "coop" ) : "coop";
	if ( notice )
	{
		if ( !m_bAnySupported )
		{
			notice->SetVisible( true );
			if ( !m_bAnyInstalled ) notice->SetText( "#L4D360UI_No_CustomCampaigns_Installed" );
			else
			{
				char token[256]; Q_snprintf( token, sizeof( token ), "#L4D360UI_No_CustomCampaigns_Installed_%s", mode );
				notice->SetText( g_pVGuiLocalize->Find( token ) ? token : "#L4D360UI_No_CustomCampaigns_Installed_Generic" );
			}
		}
		else if ( m_bAnyUnsupported )
		{
			KeyValues *descriptor = g_TerrorModes.FindMode( mode );
			notice->SetVisible( true );
			notice->SetText( descriptor && descriptor->GetBool( "mutation" ) ? "#L4D360UI_Some_CustomCampaigns_Installed_Generic" : CFmtStr( "#L4D360UI_Some_CustomCampaigns_Installed_%s", mode ) );
		}
		else notice->SetVisible( false );
	}
	SetControlEnabled( "BtnSelect", m_bAnySupported );
	if ( m_bAnySupported )
	{
		m_pCampaigns->NavigateTo();
		m_pCampaigns->SelectPanelItem( 0, true );
		UpdateSelection();
	}
	else
	{
		const char *details[] = { "ImgLevelImage", "LblName", "LblAuthor", "LblWebsite", "LblDescription" };
		for ( int i = 0; i < ARRAYSIZE( details ); ++i ) SetControlVisible( details[i], false );
		vgui::Panel *cancel = FindChildByName( "BtnCancel" ); if ( cancel ) cancel->NavigateTo();
	}
}
static void SetCampaignDetail( vgui::Panel *owner, const char *name, const char *token, const char *value )
{
	vgui::Label *label = dynamic_cast<vgui::Label *>( owner->FindChildByName( name ) );
	if ( !label ) return;
	label->SetVisible( value != NULL );
	if ( !value ) return;
	wchar_t converted[260], text[260];
	g_pVGuiLocalize->ConvertANSIToUnicode( value, converted, sizeof( converted ) );
	wchar_t *format = g_pVGuiLocalize->Find( token );
	if ( format ) { g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 1, converted ); label->SetText( text ); }
}
void CustomCampaigns::UpdateSelection()
{
	CustomCampaignListItem *row = dynamic_cast<CustomCampaignListItem *>( m_pCampaigns->GetSelectedPanelItem() );
	if ( !row ) return;
	KeyValues *mission = g_TerrorModes.FindMission( row->GetMissionName() );
	const char *mode = m_pSettings ? m_pSettings->GetString( "Game/mode", "coop" ) : "coop";
	KeyValues *chapter = mission ? mission->FindKey( CFmtStr( "Modes/%s/1", mode ) ) : NULL;
	if ( !mission || !chapter ) return;
	SetControlString( "LblName", mission->GetString( "DisplayTitle", "" ) );
	SetControlVisible( "LblName", true );
	SetCampaignDetail( this, "LblAuthor", "#L4D360UI_CustomCampaign_Author", mission->GetString( "Author", NULL ) );
	SetCampaignDetail( this, "LblWebsite", "#L4D360UI_CustomCampaign_Website", mission->GetString( "Website", NULL ) );
	const char *description = mission->GetString( "Description", NULL );
	SetControlVisible( "LblDescription", description != NULL );
	if ( description ) SetControlString( "LblDescription", description );
	vgui::ImagePanel *image = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "ImgLevelImage" ) );
	if ( image ) { image->SetVisible( true ); image->SetImage( chapter->GetString( "image", "maps/unknown" ) ); }
}
void CustomCampaigns::OnItemSelected( KeyValues *data ) { UpdateSelection(); }
void CustomCampaigns::SubmitSelection()
{
	CustomCampaignListItem *row = dynamic_cast<CustomCampaignListItem *>( m_pCampaigns->GetSelectedPanelItem() );
	if ( !row || !m_SelectionTarget.Get() ) return;
	KeyValues *mission = g_TerrorModes.FindMission( row->GetMissionName() );
	const char *mode = m_pSettings ? m_pSettings->GetString( "Game/mode", "coop" ) : "coop";
	if ( !mission || !mission->FindKey( CFmtStr( "Modes/%s/1", mode ) ) ) return;
	KeyValues *message = new KeyValues( "OnCustomCampaignSelected", "campaign", row->GetMissionName() );
	message->SetInt( "chapter", 0 );
	PostMessage( m_SelectionTarget.Get(), message );
}
void CustomCampaigns::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "Select" ) ) { SubmitSelection(); OnKeyCodePressed( KEY_XBUTTON_B ); return; }
	if ( !Q_stricmp( command, "Back" ) ) { OnKeyCodePressed( KEY_XBUTTON_B ); return; }
	BaseClass::OnCommand( command );
}
CustomCampaigns *OpenCustomCampaigns( vgui::Panel *parent, vgui::Panel *returnTo, KeyValues *settings, vgui::Panel *selectionTarget )
{
	CustomCampaigns *window = new CustomCampaigns( parent );
	window->SetDataSettings( settings ); window->SetSelectionTarget( selectionTarget ); window->SetReturnPanel( returnTo );
	if ( returnTo ) returnTo->SetVisible( false );
	window->OnOpen();
	return window;
}
} // namespace L4D360UI
