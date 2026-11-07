//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_gamemodes.h"
#include "l4d2/ui/basemodui/terror_basemodui_flyout.h"
#include "l4d2/ui/basemodui/terror_basemodui_panellist.h"
#include "l4d2/ui/basemodui/terror_basemodui_addons.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "l4d2/gamerules/terror_mode.h"
#include "filesystem.h"
#include "materialsystem/imaterial.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/Label.h"
#include "tier1/fmtstr.h"
#include "tier0/platform.h"
#include "tier0/memdbgon.h"

namespace L4D360UI
{
namespace
{
ConVar ui_modes_filter_players( "ui_modes_filter_players", "", FCVAR_ARCHIVE, "Filter type for number of players on the mode select screen" );
ConVar ui_modes_filter_modetype( "ui_modes_filter_modetype", "", FCVAR_ARCHIVE, "Filter type for type on the mode select screen" );
vgui::DHANDLE<CL4DFrame> s_MutationPicker;

void ModeTitle( KeyValues *mode, wchar_t *text, int bytes )
{
	const wchar_t *title = mode->GetWString( "DisplayTitle", NULL );
	const bool addon = title || mode->GetInt( "addon", 1 );
	const wchar_t *format = g_pVGuiLocalize->Find( CFmtStr( "#L4D360UI_Mode_%s", addon ? "addon" : mode->GetName() ) );
	if ( !addon && format ) { V_wcsncpy( text, format, bytes ); return; }
	wchar_t name[64]; g_pVGuiLocalize->ConvertANSIToUnicode( mode->GetName(), name, sizeof( name ) ); name[0] = towupper( name[0] );
	if ( !title ) title = name;
	if ( format ) g_pVGuiLocalize->ConstructString( text, bytes, format, 1, title ); else V_wcsncpy( text, title, bytes );
}

void SetNumberLabel( vgui::Label *label, const char *token, int number )
{
	if ( !label ) return;
	const wchar_t *format = g_pVGuiLocalize->Find( token );
	if ( !format ) return;
	wchar_t value[16], text[260]; V_snwprintf( value, ARRAYSIZE( value ), L"%d", number );
	g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 1, value ); label->SetText( text );
}

int ModeType( const char *base )
{
	if ( !*base ) return 0;
	if ( !Q_stricmp( base, "coop" ) ) return 1;
	if ( !Q_stricmp( base, "realism" ) ) return 2;
	if ( !Q_stricmp( base, "versus" ) ) return 3;
	if ( !Q_stricmp( base, "scavenge" ) ) return 4;
	if ( !Q_stricmp( base, "survival" ) ) return 5;
	return 6;
}

int __cdecl CompareScriptedModes( KeyValues *const *left, KeyValues *const *right )
{
	const int a = (*left)->GetInt( "missions", 0 ), b = (*right)->GetInt( "missions", 0 );
	return a == b ? Q_stricmp( (*left)->GetName(), (*right)->GetName() ) : a > b ? -1 : 1;
}

void *AllocModeImage( const char *, unsigned bytes ) { return malloc( bytes ); }
}

class ScriptedModes;
class ScriptedModeListItem : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( ScriptedModeListItem, vgui::EditablePanel );
public:
	ScriptedModeListItem( GenericPanelList *list, ScriptedModes *owner, KeyValues *mode, int index );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnMousePressed( vgui::MouseCode code ) OVERRIDE;
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	KeyValues *GetMode() const { return m_pMode; }
protected:
	virtual void PaintBackground() OVERRIDE;
	MESSAGE_FUNC( OnPanelSelected, "PanelSelected" ) { m_bSelected = true; }
	MESSAGE_FUNC( OnPanelUnSelected, "PanelUnSelected" ) { m_bSelected = false; }
private:
	GenericPanelList *m_pList;
	ScriptedModes *m_pOwner;
	KeyValues *m_pMode;
	int m_nIndex;
	vgui::HFont m_hFont;
	bool m_bSelected, m_bLoaded;
};

class ScriptedModes : public CL4DFrame
{
	DECLARE_CLASS_SIMPLE( ScriptedModes, CL4DFrame );
public:
	ScriptedModes( vgui::Panel *parent, vgui::Panel *caller ) : BaseClass( parent, "ScriptedModes", true, true, true, true ),
		m_nImageTexture( -1 ), m_nDefaultTexture( -1 ), m_ImageRead( NULL ), m_flImageFade( 0 ), m_bOwnImage( false ), m_bSquareImage( false ), m_bClosing( false )
	{
		m_hCaller = caller;
		m_pList = new GenericPanelList( this, "GplScriptedModes" );
		m_pList->SetScrollBarVisible( true );
		SetNavBack( dynamic_cast<CL4DFrame *>( caller ) );
		SetFooterEnabled( true );
	}
	virtual ~ScriptedModes() { ClearImage(); }
	virtual void OnOpen() OVERRIDE;
	virtual void OnClose() OVERRIDE;
	virtual vgui::Panel *NavigateBack() OVERRIDE { vgui::Panel *caller = m_hCaller.Get(); OnClose(); return caller; }
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE
	{
		if ( !Q_stricmp( name, "GenericPanelList" ) ) return new GenericPanelList( this, "" );
		return BaseClass::CreateControlByName( name );
	}
protected:
	virtual void OnThink() OVERRIDE;
	virtual void PaintBackground() OVERRIDE;
	MESSAGE_FUNC( OnItemSelected, "OnItemSelected" ) { UpdateDetails(); }
private:
	void RebuildRows();
	void UpdateDetails();
	void SelectMode();
	void RestoreCaller();
	void ClearImage();
	void LoadModeImage( KeyValues *mode );
	void CompleteImageRead();
	vgui::Label *Label( const char *name ) { return dynamic_cast<vgui::Label *>( FindChildByName( name, true ) ); }
	GenericPanelList *m_pList;
	vgui::DHANDLE<vgui::Panel> m_hCaller;
	CUtlVector<KeyValues *> m_Modes;
	CUtlString m_ImagePath;
	int m_nImageTexture, m_nDefaultTexture;
	FSAsyncControl_t m_ImageRead;
	float m_flImageFade;
	bool m_bOwnImage, m_bSquareImage, m_bClosing;
};

ScriptedModeListItem::ScriptedModeListItem( GenericPanelList *list, ScriptedModes *owner, KeyValues *mode, int index ) :
	BaseClass( list, "ScriptedModeListItem" ), m_pList( list ), m_pOwner( owner ), m_pMode( mode ), m_nIndex( index ), m_hFont( 0 ), m_bSelected( false ), m_bLoaded( false )
{
	SetProportional( true ); SetKeyBoardInputEnabled( true ); SetMouseInputEnabled( true );
	SetPaintBorderEnabled( false );
}
void ScriptedModeListItem::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	if ( !m_bLoaded )
	{
		KeyValues::AutoDelete data( new KeyValues( "" ) );
		if ( LoadTerrorUiResource( data, "resource/ui/l4d360ui/scriptedmodelistitem.res" ) )
		{
			LoadControlSettings( "resource/ui/l4d360ui/scriptedmodelistitem.res", "GAME", data ); m_bLoaded = true;
		}
	}
	m_hFont = scheme->GetFont( "DefaultLarge", true ); SetBgColor( scheme->GetColor( "Button.BgColor", Color( 64, 64, 64, 255 ) ) );
	vgui::Label *name = dynamic_cast<vgui::Label *>( FindChildByName( "LblName", true ) );
	if ( name )
	{
		wchar_t title[128]; ModeTitle( m_pMode, title, sizeof( title ) ); name->SetText( title ); name->SetMouseInputEnabled( false );
		if ( !m_pMode->GetInt( "missions", 0 ) ) name->SetFgColor( scheme->GetColor( "Label.DisabledFgColor2", Color( 0, 0, 0, 255 ) ) );
	}
	vgui::Panel *missions = FindChildByName( "LblMissions", true ); if ( missions ) missions->SetMouseInputEnabled( false );
}
void ScriptedModeListItem::OnMousePressed( vgui::MouseCode code )
{
	if ( code == MOUSE_LEFT ) m_pList->SelectPanelItem( m_nIndex );
	BaseClass::OnMousePressed( code );
}
void ScriptedModeListItem::OnKeyCodePressed( vgui::KeyCode code )
{
	if ( code == KEY_ENTER || GetBaseButtonCode( code ) == KEY_XBUTTON_A ) m_pOwner->OnCommand( "Select" );
	else m_pList->OnKeyCodePressed( code );
}
void ScriptedModeListItem::PaintBackground()
{
	if ( !m_bSelected ) return;
	const int wide = GetWide(), tall = GetTall(), half = wide / 2;
	vgui::surface()->DrawSetColor( 240, 0, 0, 255 );
	vgui::surface()->DrawFilledRectFade( 0, 0, half, 2, 0, 255, true );
	vgui::surface()->DrawFilledRectFade( half, 0, wide, 2, 255, 0, true );
	vgui::surface()->DrawFilledRectFade( 0, tall - 2, half, tall, 0, 255, true );
	vgui::surface()->DrawFilledRectFade( half, tall - 2, wide, tall, 255, 0, true );
	vgui::Label *name = dynamic_cast<vgui::Label *>( FindChildByName( "LblName", true ) );
	if ( !name ) return;
	wchar_t text[120]; name->GetText( text, sizeof( text ) );
	int textWide, textTall; vgui::surface()->GetTextSize( m_hFont, text, textWide, textTall );
	const int end = textWide + vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), 75 );
	vgui::surface()->DrawFilledRectFade( 0, 2, end / 2, tall - 2, 0, 50, true );
	vgui::surface()->DrawFilledRectFade( end / 2, 2, end, tall - 2, 50, 0, true );
}

void ScriptedModes::OnOpen()
{
	if ( !LoadLayout( "resource/ui/l4d360ui/scriptedmodes.res" ) ) { MarkForDeletion(); return; }
	m_nDefaultTexture = vgui::surface()->DrawGetTextureId( "vgui/menu_mode_campaign" );
	if ( m_nDefaultTexture == -1 ) { m_nDefaultTexture = vgui::surface()->CreateNewTextureID(); vgui::surface()->DrawSetTextureFile( m_nDefaultTexture, "vgui/menu_mode_campaign", 1, false ); }
	// The original Workshop/profile providers do not exist here. Do not show a
	// successful service action or animate a fictitious pending download.
	SetControlVisible( "IconVisitWorkshop", false, true ); SetControlVisible( "BtnVisitWorkshop", false, true );
	vgui::Panel *spinner = FindChildByName( "ImgAddonIconSpinner", true ); if ( spinner ) spinner->SetVisible( false );
	DropDownMenu *players = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpFilterPlayers", true ) );
	DropDownMenu *type = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpFilterModeType", true ) );
	if ( players ) players->SetCurrentSelection( CFmtStr( "filter_players_%s", ui_modes_filter_players.GetString() ) );
	if ( type ) type->SetCurrentSelection( CFmtStr( "filter_modetype_%s", ui_modes_filter_modetype.GetString() ) );
	RebuildRows();
	if ( m_hCaller.Get() ) m_hCaller->SetVisible( false );
	BaseClass::OnOpen();
	CL4DFooterPanel *footer = GetL4DFooterPanel( this );
	if ( footer ) { footer->SetButtons( FB_BUTTON_A | FB_BUTTON_B, 13, false ); footer->SetButtonText( FB_BUTTON_A, "#L4D360UI_Select" ); footer->SetButtonText( FB_BUTTON_B, "#L4D360UI_Done" ); }
	if ( m_pList->GetPanelItemCount() ) m_pList->SelectPanelItem( 0 );
}
void ScriptedModes::RestoreCaller()
{
	vgui::Panel *caller = m_hCaller.Get();
	if ( caller )
	{
		caller->SetVisible( true );
		CL4DFrame *frame = dynamic_cast<CL4DFrame *>( caller );
		if ( frame ) frame->OnOpen();
		GameModes *carousel = dynamic_cast<GameModes *>( caller->FindChildByName( "BtnGameModes", true ) );
		if ( carousel ) carousel->NavigateTo(); else caller->NavigateTo();
		if ( carousel && !frame )
		{
			CL4DFooterPanel *footer = GetL4DFooterPanel( this );
			if ( footer ) { footer->SetButtons( FB_BUTTON_A | FB_BUTTON_X, 1, false ); footer->SetButtonText( FB_BUTTON_A, "#L4D360UI_Select" ); footer->SetButtonText( FB_BUTTON_X, "#L4D360UI_MainMenu_SeeAll" ); }
		}
		m_hCaller = NULL;
	}
}
void ScriptedModes::OnClose()
{
	m_bClosing = true; ClearImage(); BaseClass::OnClose(); RestoreCaller();
}
void ScriptedModes::RebuildRows()
{
	m_pList->RemoveAllPanelItems(); m_Modes.RemoveAll();
	KeyValues *dictionary = g_TerrorModes.GetModeDictionary();
	const int players = ui_modes_filter_players.GetInt(), type = ModeType( ui_modes_filter_modetype.GetString() );
	for ( KeyValues *mode = dictionary ? dictionary->GetFirstTrueSubKey() : NULL; mode; mode = mode->GetNextTrueSubKey() )
	{
		if ( !mode->GetInt( "mutation" ) || mode->GetInt( "team" ) || mode->GetInt( "hidden" ) ) continue;
		if ( players && mode->GetInt( "maxplayers" ) != players ) continue;
		if ( type && ModeType( mode->GetString( "base", "" ) ) != type ) continue;
		m_Modes.AddToTail( mode );
	}
	m_Modes.Sort( CompareScriptedModes );
	FOR_EACH_VEC( m_Modes, i ) m_pList->AddPanelItem( new ScriptedModeListItem( m_pList, this, m_Modes[i], i ) );
	const bool any = m_Modes.Count() != 0;
	SetControlVisible( "LblNoModes", !any, true );
	const char *details[] = { "LblName", "LblAuthor", "LblDescription", "LblPlayers", "LblMissions", "ImgAddonIcon" };
	for ( int i = 0; i < ARRAYSIZE( details ); ++i ) SetControlVisible( details[i], any, true );
	if ( any ) m_pList->SelectPanelItem( 0 );
	else { SetControlEnabled( "BtnSelect", false, true ); ClearImage(); }
}
void ScriptedModes::SelectMode()
{
	ScriptedModeListItem *item = dynamic_cast<ScriptedModeListItem *>( m_pList->GetSelectedPanelItem() );
	if ( !item || !item->GetMode()->GetInt( "missions", 0 ) || !m_hCaller.Get() ) return;
	KeyValues *message = new KeyValues( "OnMutationModeSelected", "campaign", item->GetMode()->GetName() );
	PostMessage( m_hCaller.Get(), message );
	OnClose();
}
void ScriptedModes::OnCommand( const char *command )
{
	const char *value;
	if ( !Q_stricmp( command, "Select" ) ) { SelectMode(); return; }
	if ( !Q_stricmp( command, "Back" ) || !Q_stricmp( command, "Cancel" ) ) { OnClose(); return; }
	if ( !Q_stricmp( command, "VisitWorkshop" ) || !Q_stricmp( command, "BrowseWorkshop" ) ) return; // Absent real service.
	if ( ( value = StringAfterPrefix( command, "filter_players_" ) ) != NULL )
	{
		// G6FE10 owns only the ConVar and row rebuild. DropDownMenu already
		// selected the row before forwarding this command to its parent.
		ui_modes_filter_players.SetValue( value );
		RebuildRows(); return;
	}
	if ( ( value = StringAfterPrefix( command, "filter_modetype_" ) ) != NULL )
	{
		ui_modes_filter_modetype.SetValue( value );
		RebuildRows(); return;
	}
	FlyoutMenu *flyout = dynamic_cast<FlyoutMenu *>( FindChildByName( command, true ) );
	if ( flyout )
	{
		DropDownMenu *drop = dynamic_cast<DropDownMenu *>( FindChildByName( !Q_stricmp( command, "FlmFilterPlayers" ) ? "DrpFilterPlayers" : "DrpFilterModeType", true ) );
		if ( drop ) flyout->OpenMenu( drop->GetButton() ); return;
	}
	BaseClass::OnCommand( command );
}
void ScriptedModes::OnKeyCodePressed( vgui::KeyCode code )
{
	const vgui::KeyCode key = GetBaseButtonCode( code );
	if ( key == KEY_XBUTTON_A || code == KEY_ENTER ) { SelectMode(); return; }
	if ( key == KEY_XBUTTON_B || code == KEY_ESCAPE ) { OnClose(); return; }
	if ( key == KEY_XBUTTON_X || key == KEY_XBUTTON_Y ) return; // No Workshop action without an actual provider.
	BaseClass::OnKeyCodePressed( code );
}
void ScriptedModes::UpdateDetails()
{
	ScriptedModeListItem *item = dynamic_cast<ScriptedModeListItem *>( m_pList->GetSelectedPanelItem() );
	if ( !item ) return;
	KeyValues *mode = item->GetMode();
	wchar_t title[128]; ModeTitle( mode, title, sizeof( title ) ); if ( Label( "LblName" ) ) Label( "LblName" )->SetText( title );
	const wchar_t *description = mode->GetWString( "Description", mode->GetWString( "ShortDescription", g_pVGuiLocalize->Find( CFmtStr( "#L4D360UI_Mode_verbose_%s", mode->GetName() ) ) ) );
	if ( Label( "LblDescription" ) ) Label( "LblDescription" )->SetText( description ? description : L"" );
	const wchar_t *author = mode->GetWString( "Author", L"" ), *by = g_pVGuiLocalize->Find( "#L4D360UI_Addon_By" );
	wchar_t authorText[260]; V_snwprintf( authorText, ARRAYSIZE( authorText ), L"%ls%ls", *author && by ? by : L"", author );
	if ( Label( "LblAuthor" ) ) Label( "LblAuthor" )->SetText( authorText );
	const int missions = mode->GetInt( "missions", 0 ), players = mode->GetInt( "maxplayers", 0 );
	const bool oneMap = mode->GetInt( "singlechapter", 0 ) != 0;
	SetNumberLabel( Label( "LblMissions" ), oneMap ? ( missions == 1 ? "#L4D360UI_AddonMode_OneMap" : "#L4D360UI_AddonMode_NumMaps" ) : ( missions == 1 ? "#L4D360UI_AddonMode_OneMission" : "#L4D360UI_AddonMode_NumMissions" ), missions );
	if ( players == 1 || players == 4 || players == 8 )
	{
		if ( Label( "LblPlayers" ) ) Label( "LblPlayers" )->SetText( players == 1 ? "#L4D360UI_AddonMode_OnePlayer" : players == 4 ? "#L4D360UI_AddonMode_FourPlayers" : "#L4D360UI_AddonMode_EightPlayers" );
	}
	else SetNumberLabel( Label( "LblPlayers" ), "#L4D360UI_AddonMode_NumPlayers", players );
	SetControlEnabled( "BtnSelect", missions > 0, true );
	LoadModeImage( mode );
}
void ScriptedModes::ClearImage()
{
	if ( m_ImageRead )
	{
		filesystem->AsyncFinish( m_ImageRead, true );
		void *data = NULL; int bytes = 0; filesystem->AsyncGetResult( m_ImageRead, &data, &bytes );
		filesystem->AsyncRelease( m_ImageRead ); m_ImageRead = NULL; free( data );
	}
	if ( m_bOwnImage && m_nImageTexture != -1 ) vgui::surface()->DestroyTextureID( m_nImageTexture );
	m_nImageTexture = -1; m_bOwnImage = false; m_bSquareImage = false; m_flImageFade = 0;
}
void ScriptedModes::LoadModeImage( KeyValues *mode )
{
	ClearImage(); m_ImagePath = mode->GetString( "Image", "" );
	// G70890's Workshop preview path requires a real UGC owner. Neither a cache
	// hit nor completion is fabricated when Image is empty and workshopid exists.
	if ( !m_ImagePath.Length() && mode->GetUint64( "workshopid", 0 ) ) return;
	const char *extension = V_GetFileExtension( m_ImagePath.String() );
	if ( extension && !Q_stricmp( extension, "jpg" ) )
	{
		FileAsyncRequest_t request; request.pszFilename = m_ImagePath.String(); request.pszPathID = "GAME";
		request.flags = FSASYNC_FLAGS_ALLOCNOFREE; request.pfnAlloc = AllocModeImage;
		filesystem->AsyncRead( request, &m_ImageRead );
	}
	else
	{
		if ( m_ImagePath.Length() && !materials->FindMaterial( m_ImagePath.String(), TEXTURE_GROUP_VGUI )->IsErrorMaterial() )
		{
			m_nImageTexture = vgui::surface()->DrawGetTextureId( m_ImagePath.String() );
			if ( m_nImageTexture == -1 ) { m_nImageTexture = vgui::surface()->CreateNewTextureID(); vgui::surface()->DrawSetTextureFile( m_nImageTexture, m_ImagePath.String(), 1, false ); }
		}
		m_flImageFade = Plat_FloatTime() + 0.25f;
	}
}
void ScriptedModes::CompleteImageRead()
{
	if ( !m_ImageRead ) return;
	const FSAsyncStatus_t status = filesystem->AsyncStatus( m_ImageRead );
	if ( status == FSASYNC_STATUS_PENDING || status == FSASYNC_STATUS_INPROGRESS || status == FSASYNC_STATUS_UNSERVICED ) return;
	void *data = NULL; int bytes = 0;
	const FSAsyncStatus_t result = filesystem->AsyncGetResult( m_ImageRead, &data, &bytes );
	if ( result == FSASYNC_OK && LoadBaseModJpegTexture( data, bytes, m_nImageTexture, m_bSquareImage ) ) m_bOwnImage = true;
	filesystem->AsyncRelease( m_ImageRead ); m_ImageRead = NULL; free( data );
	m_flImageFade = Plat_FloatTime() + 0.25f;
}
void ScriptedModes::OnThink()
{
	BaseClass::OnThink(); if ( !m_bClosing ) CompleteImageRead();
}
void ScriptedModes::PaintBackground()
{
	DrawDialogBackground( "#L4D360UI_My_MutationModes", NULL, "#L4D360UI_My_MutationModes_Desc", NULL, NULL, false );
	vgui::Panel *image = FindChildByName( "ImgAddonIcon", true );
	if ( !image || !image->IsVisible() ) return;
	int x, y, wide, tall; image->GetBounds( x, y, wide, tall );
	vgui::surface()->DrawSetColor( 0, 0, 0, 255 ); vgui::surface()->DrawFilledRectFade( x, y, x + wide, y + tall, 128, 0, true );
	const float fade = m_flImageFade ? clamp( ( (float)Plat_FloatTime() - m_flImageFade ) / 0.3f, 0.0f, 1.0f ) : 0;
	if ( m_bSquareImage || m_nImageTexture == -1 ) { x += ( wide - tall ) / 2; wide = tall; }
	vgui::surface()->DrawSetColor( 255, 255, 255, (int)( fade * 255 ) );
	vgui::surface()->DrawSetTexture( m_nImageTexture == -1 ? m_nDefaultTexture : m_nImageTexture );
	vgui::surface()->DrawTexturedRect( x, y, x + wide, y + tall );
}

bool UsesBaseModScriptedSoloLayout( KeyValues *mode )
{
	// G161D0 classifies the descriptor's base, not its mutation flag.
	return mode && ModeType( mode->GetString( "base", mode->GetName() ) ) == 6;
}

void OpenBaseModMutationPicker( vgui::Panel *parent, vgui::Panel *caller )
{
	if ( s_MutationPicker.Get() ) { s_MutationPicker->Activate(); return; }
	ScriptedModes *picker = new ScriptedModes( parent, caller ); s_MutationPicker = picker;
	picker->OnOpen();
}
}
