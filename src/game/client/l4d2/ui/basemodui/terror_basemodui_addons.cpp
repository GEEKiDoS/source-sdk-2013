//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_addons.h"
#include "l4d2/ui/basemodui/terror_basemodui_panellist.h"
#include "l4d2/ui/basemodui/terror_basemodui_flyout.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/ui/basemodui/terror_basemodui_ingame.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui/ISurface.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "tier1/utlbuffer.h"
#include "tier1/utldict.h"
#include "steam/steam_api.h"
#include <setjmp.h>
#include <stdio.h>
#ifdef _WIN32
#include "winlite.h"
#include <shellapi.h>
#include "tier0/vcrmode.h"
#endif
extern "C" {
#include "jpeglib/jpeglib.h"
}
#include "tier0/memdbgon.h"

namespace L4D360UI
{
// Native G15A720/G15A750. Preserve the original misspelling; the correctly
// spelled Addons checkbox is a distinct ConVarRef, not a new alias.
static ConVar cl_ignore_vpk_assocation( "cl_ignore_vpk_assocation", "0", FCVAR_ARCHIVE, "Do not ask to set vpk assocation" );
static ConVar addons_hide_conflict_dialog( "addons_hide_conflict_dialog", "0", FCVAR_ARCHIVE );
static bool s_AssociationRead, s_AssociationAttempted;
static bool s_AddonPathsChanged;
static int s_AssociationState;

static void Visible( vgui::Panel *panel, const char *name, bool visible )
{
	vgui::Panel *child = panel->FindChildByName( name, true ); if ( child ) child->SetVisible( visible );
}
static void Text( vgui::Panel *panel, const char *name, const wchar_t *text )
{
	vgui::Label *label = dynamic_cast<vgui::Label *>( panel->FindChildByName( name, true ) ); if ( label ) label->SetText( text );
}
static void Localized( const char *token, wchar_t *out, int bytes )
{
	const wchar_t *value = g_pVGuiLocalize->Find( token );
	if ( value ) V_wcsncpy( out, value, bytes ); else g_pVGuiLocalize->ConvertANSIToUnicode( token, out, bytes );
}
static void ModRoot( char *out, int bytes )
{
	char paths[MAX_PATH]; filesystem->GetSearchPath( "MOD", false, paths, sizeof( paths ) );
	const char *last = V_strrchr( paths, ';' ); Q_strncpy( out, last ? last + 1 : paths, bytes );
}
static bool OverlayAvailable()
{
	return steamapicontext && steamapicontext->SteamUser() && steamapicontext->SteamFriends() && steamapicontext->SteamUtils()
		&& steamapicontext->SteamUtils()->IsOverlayEnabled() && steamapicontext->SteamUtils()->GetConnectedUniverse() != k_EUniverseInvalid;
}
static void BrowseWorkshop()
{
	if ( !OverlayAvailable() ) return;
	char url[MAX_PATH];
	Q_snprintf( url, sizeof( url ), "http://%s.steamcommunity.com/workshop/browse?appid=%d",
		steamapicontext->SteamUtils()->GetConnectedUniverse() == k_EUniverseBeta ? "beta" : "www", steamapicontext->SteamUtils()->GetAppID() );
	steamapicontext->SteamFriends()->ActivateGameOverlayToWebPage( url );
}

static void ShowAddonNotice( vgui::Panel *parent, bool association )
{
	GenericConfirmation::UsageData_t data;
	data.pWindowTitle = association ? "#L4D360UI_AddonAssociation_Failure_Title" : "#L4D_PLUGIN_WARNING_TITLE";
	data.pMessageText = association ? "#L4D360UI_AddonAssociation_Failure_Description" : "#L4D360UI_ADDON_CONFLICT_WARNING";
	data.bOkButtonEnabled = true; data.bCancelButtonEnabled = false;
	data.bCheckBoxEnabled = association;
	data.pCheckBoxText = association ? "#L4D360UI_AddonAssociation_Failure_DoNotAsk" : NULL;
	data.pCheckBoxCvarName = association ? "cl_ignore_vpk_assocation" : NULL;
	CL4DFrame *caller = dynamic_cast<CL4DFrame *>( parent );
	GenericConfirmation *dialog = new GenericConfirmation( caller && caller->GetParent() ? caller->GetParent() : parent );
	if ( !dialog->LoadLayout( "resource/ui/l4d360ui/genericconfirmation.res" ) ) { delete dialog; return; }
	dialog->SetNavBack( caller );
	dialog->SetUsageData( data ); dialog->OnOpen();
}

AddonAssociation::AddonAssociation( vgui::Panel *parent, const char *name ) : BaseClass( parent, name, true, false, false, true ), m_bLayoutReady( false )
{
	m_pIgnore = new vgui::CheckButton( this, "CheckButtonAddonAssociation", "#L4D360UI_Cloud_KeepInSync_Tip" );
	SetFooterEnabled( false );
	m_bLayoutReady = LoadLayout( "resource/ui/l4d360ui/addonassociation.res" );
}
vgui::Panel *AddonAssociation::CreateControlByName( const char *name )
{
	if ( !Q_stricmp( name, "CvarToggleCheckButton_GameUI" ) ) return new vgui::CheckButton( this, "", "" );
	return BaseClass::CreateControlByName( name );
}
void AddonAssociation::OnOpen()
{
	if ( !m_bLayoutReady ) return;
	m_pIgnore->SetSelected( cl_ignore_vpk_assocation.GetBool() ); BaseClass::OnOpen();
}
int AddonAssociation::GetAssociationState()
{
	if ( cl_ignore_vpk_assocation.GetBool() ) return 2;
	if ( s_AssociationRead ) return s_AssociationState;
	s_AssociationRead = true; s_AssociationState = 0;
#ifdef _WIN32
	HKEY key = NULL;
	if ( VCRHook_RegOpenKeyEx( HKEY_CLASSES_ROOT, ".vpk", 0, KEY_READ, &key ) == ERROR_SUCCESS )
	{
		char value[MAX_PATH] = {}; DWORD bytes = sizeof( value );
		if ( VCRHook_RegQueryValueEx( key, NULL, NULL, NULL, reinterpret_cast<BYTE *>( value ), &bytes ) == ERROR_SUCCESS )
		{
			value[ARRAYSIZE( value ) - 1] = 0;
			s_AssociationState = Q_stricmp( value, "sourceaddonfile" ) ? 1 : 2;
		}
		VCRHook_RegCloseKey( key );
	}
#endif
	return s_AssociationState;
}
bool AddonAssociation::RegisterAssociation( vgui::Panel *parent )
{
#ifdef _WIN32
	char installer[MAX_PATH], command[MAX_PATH];
	GetModuleFileNameA( GetModuleHandleA( NULL ), installer, sizeof( installer ) );
	V_FixSlashes( installer, '\\' ); V_StripFilename( installer );
	Q_strncat( installer, "\\bin\\addoninstaller.exe", sizeof( installer ) );
	Q_snprintf( command, sizeof( command ), "\"%s\" /register", installer );
	STARTUPINFOA startup = {}; PROCESS_INFORMATION process = {}; startup.cb = sizeof( startup );
	bool launched = CreateProcessA( NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process ) != FALSE;
	DWORD error = launched ? ERROR_SUCCESS : GetLastError();
	if ( launched ) { CloseHandle( process.hThread ); CloseHandle( process.hProcess ); }
	if ( !launched && error == ERROR_ELEVATION_REQUIRED )
	{
		SHELLEXECUTEINFOA execute = {}; execute.cbSize = sizeof( execute ); execute.lpVerb = "runas";
		execute.lpFile = installer; execute.lpParameters = "/register"; execute.nShow = SW_SHOWMAXIMIZED;
		launched = ShellExecuteExA( &execute ) != FALSE; error = launched ? ERROR_SUCCESS : GetLastError();
	}
	if ( launched || error == ERROR_SUCCESS ) { s_AssociationState = 2; return true; }
	char *message = NULL;
	FormatMessageA( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, error, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), reinterpret_cast<char *>( &message ), 0, NULL );
	Msg( "Failed to execute addoninstaller(%lu): %s\n", error, message ? message : "" );
	if ( message ) LocalFree( message );
#endif
	ShowAddonNotice( parent, true ); return false;
}
void AddonAssociation::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "Yes" ) || !Q_stricmp( command, "No" ) )
	{
		if ( m_pIgnore->IsSelected() ) cl_ignore_vpk_assocation.SetValue( 1 );
		CL4DFrame *caller = GetNavBack();
		NavigateBack();
		if ( !Q_stricmp( command, "Yes" ) ) RegisterAssociation( caller );
		return;
	}
	BaseClass::OnCommand( command );
}

AddonListItem::AddonListItem( GenericPanelList *parent, Addons *owner, int index, const AddonInfo_t &info ) : BaseClass( parent, "AddonListItem" ),
	m_pOwner( owner ), m_Index( index ), m_Font( 0 ), m_bSelected( false ), m_bConflict( info.conflict ), m_bInitialized( false )
{
	SetProportional( true ); SetPaintBackgroundEnabled( true );
	m_pName = new vgui::Label( this, "LblName", info.title ); m_pName->SetMouseInputEnabled( false );
	m_pType = new vgui::Label( this, "LblType", info.types ); m_pType->SetMouseInputEnabled( false );
	m_pType->SetWrap( true );
	m_pEnabled = new vgui::CheckButton( this, "AddonEnabledCheckbox", "" );
	m_pEnabled->SetSelected( info.enabled ); m_pEnabled->AddActionSignalTarget( this );
}
void AddonListItem::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	LoadControlSettings( "resource/ui/l4d360ui/addonlistitem.res", "GAME", m_pOwner->GetRowLayout() );
	SetBgColor( scheme->GetColor( "Button.BgColor", Color( 64, 64, 64, 255 ) ) );
	m_Font = scheme->GetFont( "DefaultLarge", true ); m_bInitialized = true;
	if ( GetParent() && GetParent()->GetParent() ) GetParent()->GetParent()->InvalidateLayout();
}
void AddonListItem::OnPanelSelected() { m_bSelected = true; Repaint(); }
void AddonListItem::OnPanelUnSelected() { m_bSelected = false; Repaint(); }
void AddonListItem::OnCheckButtonChecked() { if ( m_bInitialized ) m_pOwner->SetAddonEnabled( m_Index, m_pEnabled->IsSelected() ); }
void AddonListItem::ToggleEnabled() { m_pEnabled->SetSelected( !m_pEnabled->IsSelected() ); }
void AddonListItem::OnMousePressed( vgui::MouseCode code )
{
	if ( code == MOUSE_LEFT )
	{
		// G19590 selects explicitly; the target list's clip is a physical parent.
		for ( vgui::Panel *parent = GetParent(); parent; parent = parent->GetParent() )
		{
			GenericPanelList *list = dynamic_cast<GenericPanelList *>( parent );
			if ( !list ) continue;
			for ( int i = 0; i < list->GetPanelItemCount(); ++i )
				if ( list->GetPanelItem( i ) == this ) { list->SelectPanelItem( i, true ); break; }
			break;
		}
	}
	BaseClass::OnMousePressed( code );
}
void AddonListItem::PaintBackground()
{
	const Color text = m_bConflict ? Color( m_bSelected ? 240 : 120, 0, 0, 255 ) : Color( 255, 255, 255, 255 );
	m_pName->SetFgColor( text );
	if ( !m_bSelected ) return;
	const int wide = GetWide(), tall = GetTall();
	vgui::surface()->DrawSetColor( 240, 0, 0, 255 );
	vgui::surface()->DrawFilledRectFade( 0, 0, wide / 2, 2, 0, 255, true );
	vgui::surface()->DrawFilledRectFade( wide / 2, 0, wide, 2, 255, 0, true );
	vgui::surface()->DrawFilledRectFade( 0, tall - 2, wide / 2, tall, 0, 255, true );
	vgui::surface()->DrawFilledRectFade( wide / 2, tall - 2, wide, tall, 255, 0, true );
	wchar_t name[120]; m_pName->GetText( name, sizeof( name ) ); int textWide, textTall;
	vgui::surface()->GetTextSize( m_Font, name, textWide, textTall );
	const int glowWide = textWide + vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), 75 );
	vgui::surface()->DrawFilledRectFade( 0, 2, glowWide / 2, tall - 2, 0, 50, true );
	vgui::surface()->DrawFilledRectFade( glowWide / 2, 2, glowWide, tall - 2, 50, 0, true );
}

static const struct ContentTag_t { const char *tag, *label; int bit; } s_Content[] =
{
	{ "campaigns", "#L4D360UI_Addon_Type_Campaigns", 2 }, { "miscellaneous", "#L4D360UI_Addon_Type_Miscellaneous", 4 },
	{ "skins", "#L4D360UI_Addon_Type_Skin", 8 }, { "weapons", "#L4D360UI_Addon_Type_Weapons", 16 },
	{ "mutations", "#L4D360UI_Addon_Type_Mutations", 32 }, { "infected", "#L4D360UI_Addon_Type_Infected", 64 },
	{ "survivors", "#L4D360UI_Addon_Type_Survivors", 128 }, { "sounds", "#L4D360UI_Addon_Type_Sound", 256 },
	{ "scripts", "#L4D360UI_Addon_Type_Script", 512 }, { "ui", "#L4D360UI_Addon_Type_UI", 1024 },
	{ "items", "#L4D360UI_Addon_Type_Items", 2048 }
};
static int ContentBits( KeyValues *info )
{
	int bits = 1;
	const struct { const char *field; int bit; } fields[] = {
		{ "addonContent_Campaign", 2 }, { "addonContent_Map", 2 }, { "addonContent_Skin", 8 }, { "addonContent_weapon", 16 },
		{ "addonContent_BossInfected", 64 }, { "addonContent_CommonInfected", 64 }, { "addonContent_Survivor", 128 },
		{ "addonContent_Sound", 256 }, { "addonContent_Music", 256 }, { "addonContent_Script", 512 }, { "addonContent_prop", 2048 } };
	for ( int i = 0; i < ARRAYSIZE( fields ); ++i ) if ( info->GetInt( fields[i].field ) ) bits |= fields[i].bit;
	return bits;
}
static void MetadataText( KeyValues *info, const char *key, wchar_t *out, int bytes, const wchar_t *fallback )
{
	V_wcsncpy( out, info->GetWString( key, fallback ), bytes );
}

Addons::Addons( vgui::Panel *parent, const char *name ) : BaseClass( parent, name, false, true, true, true ),
	m_pAddonList( NULL ), m_pRowLayout( new KeyValues( "" ) ), m_Texture( -1 ), m_DefaultTexture( -1 ), m_ImageRead( NULL ), m_ImageFade( 0 ), m_bSquareImage( true ), m_bLayoutReady( false ), m_bReady( false ), m_bClosing( false ), m_bConflictWarned( false )
{
	m_Filter[0] = 0; m_Mod[0] = 0; m_ListPath[0] = 0;
	m_pList = new GenericPanelList( this, "GplAddons" );
	m_pList->SetScrollBarVisible( true ); m_pList->SetScrollProgressVisible( true );
	m_pIgnoreAssociation = new vgui::CheckButton( this, "CheckButtonAssociation", "" );
	m_pIcon = new vgui::ImagePanel( this, "ImgAddonIcon" );
	m_pIconSpinner = new vgui::ImagePanel( this, "ImgAddonIconSpinner" );
	SetFooterEnabled( true ); // The real window manager supplies the caller separately.
	m_bLayoutReady = LoadLayout( "resource/ui/l4d360ui/addons.res" ) && LoadTerrorUiResource( m_pRowLayout, "resource/ui/l4d360ui/addonlistitem.res" );
}
Addons::~Addons() { ClearImage(); if ( m_pAddonList ) m_pAddonList->deleteThis(); m_pRowLayout->deleteThis(); }
vgui::Panel *Addons::CreateControlByName( const char *name )
{
	if ( !Q_stricmp( name, "GenericPanelList" ) ) return new GenericPanelList( this, "" );
	if ( !Q_stricmp( name, "CvarToggleCheckButton_GameUI" ) ) return new vgui::CheckButton( this, "", "" );
	return BaseClass::CreateControlByName( name );
}
void Addons::OnOpen()
{
	if ( !m_bLayoutReady ) return;
	const bool firstOpen = !m_bReady;
	if ( firstOpen )
	{
		ModRoot( m_Mod, sizeof( m_Mod ) ); Q_snprintf( m_ListPath, sizeof( m_ListPath ), "%saddonlist.txt", m_Mod );
		ConVarRef ignore( "cl_ignore_vpk_association", true );
		m_pIgnoreAssociation->SetSelected( ignore.IsValid() && ignore.GetBool() );
		m_pIgnoreAssociation->SetVisible( ignore.IsValid() && ignore.GetBool() ); Visible( this, "LblCheckNoAssociation", m_pIgnoreAssociation->IsVisible() );
		// G5AA20 is literal false in this native build. Local add-ons are
		// supported; no install state or metadata dependency gate is invented.
		Visible( this, "AddonSupportRequiredPanel", false ); Visible( this, "InstallingAddonSupportPanel", false );
		Visible( this, "BtnInstallSupport", false ); Visible( this, "IconInstallSupport", false );
		m_DefaultTexture = vgui::surface()->DrawGetTextureId( "vgui/menu_mode_campaign" );
		if ( m_DefaultTexture == -1 ) { m_DefaultTexture = vgui::surface()->CreateNewTextureID(); vgui::surface()->DrawSetTextureFile( m_DefaultTexture, "vgui/menu_mode_campaign", true, false ); }
	}
	else
	{
		// G1FFD0/G1EA50 preserve unsaved enables through modal OnOpen.
		// The native key is the ANSI title, deliberately not a new file-ID key.
		m_EnabledSnapshot.RemoveAll();
		FOR_EACH_VEC( m_Addons, i )
		{
			char title[128]; g_pVGuiLocalize->ConvertUnicodeToANSI( m_Addons[i].title, title, sizeof( title ) );
			int entry = m_EnabledSnapshot.Find( title );
			if ( entry == m_EnabledSnapshot.InvalidIndex() ) m_EnabledSnapshot.Insert( title, m_Addons[i].enabled );
			else m_EnabledSnapshot[entry] = m_Addons[i].enabled;
		}
	}
	CenterDialogOnScreen(); BaseClass::OnOpen();
	m_bReady = true; PopulateAddonInfo(); UpdateFooter();
	if ( firstOpen )
	{
		DropDownMenu *filter = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpFilterTags", true ) );
		if ( filter ) filter->SetCurrentSelection( "filter_tags_" );
	}
}
void Addons::PopulateAddonInfo()
{
	m_pList->RemoveAllPanelItems(); m_Addons.RemoveAll(); if ( m_pAddonList ) m_pAddonList->deleteThis();
	m_pAddonList = new KeyValues( "AddonList" );
	CUtlBuffer bytes;
	if ( !filesystem->ReadFile( m_ListPath, NULL, bytes ) ) { RebuildRows(); return; }
	bytes.PutChar( 0 );
	if ( !m_pAddonList->LoadFromBuffer( m_ListPath, static_cast<const char *>( bytes.Base() ) ) ) { RebuildRows(); return; }
	wchar_t none[128]; Localized( "#L4D360UI_Addon_None_Specified", none, sizeof( none ) );
	for ( KeyValues *entry = m_pAddonList->GetFirstValue(); entry; )
	{
		KeyValues *next = entry->GetNextValue();
		AddonInfo_t info = {}; Q_strncpy( info.filename, entry->GetName(), sizeof( info.filename ) ); V_FixSlashes( info.filename, '\\' );
		// Native workshop metadata is a separate UGC producer. No UGC provider
		// is fabricated from disk names, completion flags, or App550 IDs.
		if ( !Q_strnicmp( info.filename, "workshop\\", 9 ) ) { entry = next; continue; }
		info.archive = Q_stristr( info.filename, ".vpk" ) != NULL; info.enabled = entry->GetInt() != 0;
		char root[MAX_PATH], path[MAX_PATH]; Q_snprintf( root, sizeof( root ), "%saddons\\%s", m_Mod, info.filename );
		Q_snprintf( path, sizeof( path ), "%s\\addoninfo.txt", root );
		if ( info.archive ) filesystem->AddSearchPath( root, "ADDONINFO", PATH_ADD_TO_HEAD );
		KeyValues::AutoDelete metadata( new KeyValues( "AddonInfo" ) );
		if ( metadata->LoadFromFile( filesystem, path, NULL ) )
		{
			MetadataText( metadata, "addontitle", info.title, sizeof( info.title ), none );
			MetadataText( metadata, "addonauthor", info.author, sizeof( info.author ), none );
			MetadataText( metadata, "addonDescription", info.description, sizeof( info.description ), none );
			MetadataText( metadata, "addonversion", info.version, sizeof( info.version ), L"0.0" );
			info.content = ContentBits( metadata );
			for ( int i = 0; i < ARRAYSIZE( s_Content ); ++i ) if ( info.content & s_Content[i].bit )
			{
				wchar_t label[128]; Localized( s_Content[i].label, label, sizeof( label ) ); V_wcsncat( info.types, label, ARRAYSIZE( info.types ) );
			}
			wchar_t *comma = wcsrchr( info.types, L',' ); if ( comma ) *comma = 0; // G19700 removes the final comma.
			Q_snprintf( info.image, sizeof( info.image ), "%s\\addonimage.jpg", root );
			char title[128]; g_pVGuiLocalize->ConvertUnicodeToANSI( info.title, title, sizeof( title ) );
			const int saved = m_EnabledSnapshot.Find( title ); if ( saved != m_EnabledSnapshot.InvalidIndex() ) info.enabled = m_EnabledSnapshot[saved];
			m_Addons.AddToTail( info );
		}
		else
		{
			Msg( "[WORKSHOP] Failed to load add-on info for: %s\n", path );
			entry->Clear(); // G21510 -> GC1F90: retain the named TYPE_NONE node, not delete/SetInt(0).
		}
		if ( info.archive ) filesystem->RemoveSearchPath( root, "ADDONINFO" );
		entry = next;
	}
	BuildConflictMap( true ); RebuildRows();
}
void Addons::RebuildRows()
{
	if ( !m_bReady ) return;
	m_pList->RemoveAllPanelItems(); int mask = 1;
	for ( int i = 0; i < ARRAYSIZE( s_Content ); ++i ) if ( !Q_stricmp( m_Filter, s_Content[i].tag ) ) mask = s_Content[i].bit;
	FOR_EACH_VEC( m_Addons, i ) if ( mask & m_Addons[i].content ) m_pList->AddPanelItem( new AddonListItem( m_pList, this, i, m_Addons[i] ) );
	m_pList->InvalidateLayout( true ); if ( m_pList->GetPanelItemCount() ) m_pList->SelectPanelItem( 0 );
	Visible( this, "LblNoAddons", m_Addons.Count() == 0 );
	wchar_t noAddons[128]; Localized( "#L4D360UI_No_Addons_Installed", noAddons, sizeof( noAddons ) ); Text( this, "LblNoAddons", noAddons );
	Visible( this, "LblNoAddonsFiltered", m_Addons.Count() > 0 && !m_pList->GetPanelItemCount() );
	vgui::Panel *filter = FindChildByName( "DrpFilterTags", true ); if ( filter ) filter->SetEnabled( m_Addons.Count() > 0 );
	UpdateDetails();
}
void Addons::SetAddonEnabled( int index, bool enabled )
{
	if ( !m_Addons.IsValidIndex( index ) || m_Addons[index].enabled == enabled ) return;
	m_Addons[index].enabled = enabled; BuildConflictMap( true );
}
void Addons::OnItemSelected() { UpdateDetails(); }
void Addons::UpdateFooter()
{
	if ( !IsVisible() ) return;
	CL4DFooterPanel *footer = GetL4DFooterPanel( this ); if ( !footer ) return;
	footer->SetButtons( 11, 13, false ); footer->SetButtonText( FB_BUTTON_A, "#L4D360UI_Modify" );
	footer->SetButtonText( FB_BUTTON_B, "#L4D360UI_Done" ); footer->SetButtonText( FB_BUTTON_Y, "#L4D360UI_ADDON_BROWSE_WORKSHOP" );
	footer->SetButtonText( FB_BUTTON_X, "#L4D360UI_ADDON_VISIT_WORKSHOP" );
}
void Addons::UpdateDetails()
{
	AddonListItem *row = dynamic_cast<AddonListItem *>( m_pList->GetSelectedPanelItem() );
	const bool selected = row && m_Addons.IsValidIndex( row->GetAddonIndex() );
	const char *details[] = { "LblName", "LblType", "LblAuthor", "LblDescription", "ImgAddonIcon" };
	for ( int i = 0; i < ARRAYSIZE( details ); ++i ) Visible( this, details[i], selected );
	Visible( this, "ImgAddonIconOverlay", false ); m_pIconSpinner->SetVisible( selected ); Visible( this, "ImgGlobalAddonIconSpinner", false );
	Visible( this, "BtnVisitWorkshop", false ); Visible( this, "IconVisitWorkshop", false );
	if ( !selected ) { ClearImage(); return; }
	const AddonInfo_t &info = m_Addons[row->GetAddonIndex()];
	Text( this, "LblName", info.title ); Text( this, "LblType", info.types ); Text( this, "LblDescription", info.description );
	wchar_t author[260]; Localized( "#L4D360UI_Addon_By", author, sizeof( author ) ); V_wcsncat( author, info.author, ARRAYSIZE( author ) ); Text( this, "LblAuthor", author );
	ClearImage();
	char root[MAX_PATH]; Q_snprintf( root, sizeof( root ), "%saddons\\%s", m_Mod, info.filename );
	if ( info.archive ) filesystem->AddSearchPath( root, "ADDONINFO", PATH_ADD_TO_HEAD );
	FileAsyncRequest_t request; request.pszFilename = info.image; request.pfnCallback = ImageReadComplete; request.pContext = this;
	request.flags = FSASYNC_FLAGS_SYNC | FSASYNC_FLAGS_FREEDATAPTR;
	filesystem->AsyncRead( request, &m_ImageRead );
	if ( info.archive ) filesystem->RemoveSearchPath( root, "ADDONINFO" );
}
void Addons::ClearImage()
{
	if ( m_ImageRead ) { filesystem->AsyncFinish( m_ImageRead, true ); filesystem->AsyncRelease( m_ImageRead ); m_ImageRead = NULL; }
	if ( m_Texture != -1 ) { vgui::surface()->DestroyTextureID( m_Texture ); m_Texture = -1; }
	m_ImageFade = 0;
}
void Addons::ImageReadComplete( const FileAsyncRequest_t &request, int bytes, FSAsyncStatus_t status )
{
	if ( status == FSASYNC_OK ) static_cast<Addons *>( request.pContext )->LoadImageData( request.pData, bytes );
}
struct AddonJpegError_t { jpeg_error_mgr error; jmp_buf jump; };
static void JpegError( j_common_ptr jpeg )
{
	char message[JMSG_LENGTH_MAX]; jpeg->err->format_message( jpeg, message ); Warning( "%s\n", message );
	longjmp( reinterpret_cast<AddonJpegError_t *>( jpeg->err )->jump, 1 );
}
// G19EE0/G19F00/G19F20 deliberately leave the real source buffer untouched.
static void JpegInitSource( j_decompress_ptr ) {}
static void JpegSkipInputData( j_decompress_ptr, long ) {}
static void JpegTermSource( j_decompress_ptr ) {}
// G19EF0/G19F10 suspend/fail instead of fabricating EOI or resynchronizing.
static boolean JpegFillInputBuffer( j_decompress_ptr ) { return FALSE; }
static boolean JpegResyncToRestart( j_decompress_ptr, int ) { return FALSE; }
bool LoadBaseModJpegTexture( const void *data, int bytes, int &texture, bool &square )
{
	jpeg_decompress_struct jpeg = {}; AddonJpegError_t error; CUtlVector<unsigned char> rgba;
	jpeg.err = jpeg_std_error( &error.error ); error.error.error_exit = JpegError;
	if ( setjmp( error.jump ) ) { jpeg_destroy_decompress( &jpeg ); return false; }
	jpeg_create_decompress( &jpeg );
	jpeg_source_mgr source;
	source.next_input_byte = static_cast<const JOCTET *>( data );
	source.bytes_in_buffer = static_cast<size_t>( bytes );
	source.init_source = JpegInitSource;
	source.fill_input_buffer = JpegFillInputBuffer;
	source.skip_input_data = JpegSkipInputData;
	source.resync_to_restart = JpegResyncToRestart;
	source.term_source = JpegTermSource;
	jpeg.src = &source;
	if ( jpeg_read_header( &jpeg, TRUE ) != JPEG_HEADER_OK || !jpeg_start_decompress( &jpeg ) ) { jpeg_destroy_decompress( &jpeg ); return false; }
	if ( jpeg.out_color_space != JCS_RGB || jpeg.output_components != 3 ) { jpeg_destroy_decompress( &jpeg ); return false; }
	const int width = jpeg.output_width, height = jpeg.output_height;
	if ( width <= 0 || height <= 0 || static_cast<uint64>( width ) * height > INT_MAX / 4 ) { jpeg_destroy_decompress( &jpeg ); return false; }
	rgba.SetCount( width * height * 4 );
	while ( jpeg.output_scanline < jpeg.output_height )
	{
		unsigned char *row = rgba.Base() + jpeg.output_scanline * width * 4; JSAMPROW scan = row;
		if ( !jpeg_read_scanlines( &jpeg, &scan, 1 ) ) { jpeg_destroy_decompress( &jpeg ); return false; }
		for ( int x = width - 1; x >= 0; --x ) { row[x * 4 + 3] = 255; row[x * 4 + 2] = row[x * 3 + 2]; row[x * 4 + 1] = row[x * 3 + 1]; row[x * 4] = row[x * 3]; }
	}
	jpeg_finish_decompress( &jpeg ); jpeg_destroy_decompress( &jpeg );
	texture = vgui::surface()->CreateNewTextureID( true ); vgui::surface()->DrawSetTextureRGBA( texture, rgba.Base(), width, height, 1, false );
	square = width == height; return true;
}
void Addons::LoadImageData( const void *data, int bytes )
{
	LoadBaseModJpegTexture( data, bytes, m_Texture, m_bSquareImage );
	// G1E1B0 starts the fade after successful I/O even when decoding fails.
	m_ImageFade = Plat_FloatTime() + 0.25f;
}
void Addons::PaintBackground()
{
	DrawDialogBackground( "#L4D360UI_My_Addons", NULL, "#L4D360UI_My_Addons_Desc", NULL, NULL, false );
	vgui::Panel *image = m_pIcon; if ( !image->IsVisible() ) return;
	int x, y; image->GetPos( x, y ); int wide = image->GetWide(), tall = image->GetTall();
	vgui::surface()->DrawSetColor( 0, 0, 0, 255 ); vgui::surface()->DrawFilledRectFade( x, y, x + wide, y + tall, 128, 0, true );
	if ( m_bSquareImage || m_Texture == -1 ) { x += ( wide - tall ) / 2; wide = tall; }
	vgui::surface()->DrawSetTexture( m_Texture == -1 ? m_DefaultTexture : m_Texture );
	const float fade = m_ImageFade ? RemapValClamped( static_cast<float>( Plat_FloatTime() ), m_ImageFade, m_ImageFade + 0.3f, 0.0f, 1.0f ) : 0.0f;
	vgui::surface()->DrawSetColor( 255, 255, 255, static_cast<int>( fade * 255 ) ); vgui::surface()->DrawTexturedRect( x, y, x + wide, y + tall );
	m_pIconSpinner->SetAlpha( static_cast<int>( ( 1.0f - fade ) * 255 ) );
}
void Addons::OnThink()
{
	BaseClass::OnThink();
	m_pIconSpinner->SetFrame( static_cast<int>( Plat_FloatTime() * 10.0 ) );
	m_pIconSpinner->SetVisible( m_pList->GetPanelItemCount() != 0 );
}
bool Addons::SaveAddonList()
{
	if ( !m_pAddonList ) return false;
	FOR_EACH_VEC( m_Addons, i ) m_pAddonList->SetInt( m_Addons[i].filename, m_Addons[i].enabled ? 1 : 0 );
	bool saved = m_pAddonList->SaveToFile( filesystem, m_ListPath, NULL );
	ConVarRef ignore( "cl_ignore_vpk_association", true ); if ( ignore.IsValid() ) ignore.SetValue( m_pIgnoreAssociation->IsSelected() ? 1 : 0 );
	// G1EC70 sets the changed bit even when SaveToFile fails. The real owner
	// later re-reads the actual disk bytes, never this unsaved UI proposal.
	s_AddonPathsChanged = true;
	return saved;
}
void Addons::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "Back" ) || !Q_stricmp( command, "Done" ) )
	{
		if ( !m_bClosing ) { m_bClosing = true; SaveAddonList(); }
		if ( GetNavBack() ) NavigateBack(); else { SetVisible( false ); if ( GetParent() ) GetParent()->OnCommand( "Back" ); }
		return;
	}
	if ( !Q_stricmp( command, "BrowseWorkshop" ) ) { BrowseWorkshop(); return; }
	if ( !Q_stricmp( command, "InstallSupport" ) )
	{
#ifdef _WIN32
		ShellExecuteA( NULL, "open", "steam://install/564", NULL, NULL, SW_SHOW );
#endif
		return;
	}
	if ( !Q_strnicmp( command, "filter_tags_", 12 ) ) { Q_strncpy( m_Filter, command + 12, sizeof( m_Filter ) ); RebuildRows(); return; }
	if ( !Q_stricmp( command, "VisitWorkshop" ) ) return; // No UGC metadata producer means no selected published-file ID.
	BaseClass::OnCommand( command );
}
void Addons::OnKeyCodePressed( vgui::KeyCode code )
{
	if ( code == KEY_ESCAPE || code == KEY_XBUTTON_B || code == KEY_XBUTTON_START ) { OnCommand( "Back" ); return; }
	if ( code == KEY_XBUTTON_A ) { AddonListItem *row = dynamic_cast<AddonListItem *>( m_pList->GetSelectedPanelItem() ); if ( row ) row->ToggleEnabled(); return; }
	if ( code == KEY_XBUTTON_Y ) { BrowseWorkshop(); return; }
	BaseClass::OnKeyCodePressed( code );
}

// G202D0 exclusions. These are filename collisions, not dependency or CRC rules.
static bool ConflictExcluded( const char *name )
{
	static const char *files[] = { "addoninfo.txt", "addonimage.jpg", "addonimage.vtf", "sound/sound.cache", "scripts/vscripts/mapspawn_addon.nut", "scripts/vscripts/director_base_addon.nut", "scripts/vscripts/scriptedmode_addon.nut", "scripts/vscripts/response_testbed_addon.nut" };
	for ( int i = 0; i < ARRAYSIZE( files ); ++i ) if ( !Q_stricmp( name, files[i] ) ) return true;
	const char *extension = V_GetFileExtension( name ); return extension && ( !Q_stricmp( extension, "smd" ) || !Q_stricmp( extension, "qc" ) || !Q_stricmp( extension, "qci" ) );
}
static void AddConflictFiles( const char *directory, CUtlVector<CUtlString> &files )
{
	char pattern[MAX_PATH]; Q_snprintf( pattern, sizeof( pattern ), "%s*", directory );
	FileFindHandle_t find = FILESYSTEM_INVALID_FIND_HANDLE;
	for ( const char *name = filesystem->FindFirstEx( pattern, "ADDONCONFLICT", &find ); name; name = filesystem->FindNext( find ) )
	{
		if ( !Q_strcmp( name, "." ) || !Q_strcmp( name, ".." ) ) continue;
		char relative[MAX_PATH]; Q_snprintf( relative, sizeof( relative ), "%s%s", directory, name ); V_FixSlashes( relative, '/' );
		if ( filesystem->FindIsDirectory( find ) ) { Q_strncat( relative, "/", sizeof( relative ) ); AddConflictFiles( relative, files ); }
		else if ( !ConflictExcluded( relative ) ) files.AddToTail( relative );
	}
	filesystem->FindClose( find );
}
void Addons::BuildConflictMap( bool warn )
{
	CUtlDict<int,int> first; bool collision = false;
	FOR_EACH_VEC( m_Addons, i ) m_Addons[i].conflict = false;
	for ( int pass = 0; pass < 2; ++pass ) FOR_EACH_VEC( m_Addons, i )
	{
		AddonInfo_t &info = m_Addons[i];
		if ( !info.enabled || !info.archive || ( ( info.content & 2 ) != 0 ) != ( pass == 0 ) ) continue;
		char archive[MAX_PATH]; Q_snprintf( archive, sizeof( archive ), "%saddons\\%s", m_Mod, info.filename );
		filesystem->AddSearchPath( archive, "ADDONCONFLICT", PATH_ADD_TO_HEAD );
		CUtlVector<CUtlString> files; AddConflictFiles( "", files );
		filesystem->RemoveSearchPath( archive, "ADDONCONFLICT" );
		FOR_EACH_VEC( files, f )
		{
			int existing = first.Find( files[f].String() );
			if ( existing == first.InvalidIndex() ) first.Insert( files[f].String(), i );
			else if ( first[existing] != i )
			{
				m_Addons[first[existing]].conflict = true; info.conflict = true; collision = true;
				Msg( "File %s collides in VPKs:\n", files[f].String() );
			}
		}
	}
	for ( int i = 0; i < m_pList->GetPanelItemCount(); ++i )
	{
		AddonListItem *row = dynamic_cast<AddonListItem *>( m_pList->GetPanelItem( i ) ); if ( row ) row->SetConflict( m_Addons[row->GetAddonIndex()].conflict );
	}
	if ( collision && warn && !m_bConflictWarned && !addons_hide_conflict_dialog.GetBool() ) { m_bConflictWarned = true; ShowAddonNotice( this, false ); }
}
bool RefreshBaseModAddonPaths()
{
	// G19900 -> G67BB0: consume once, before the main-menu base OnOpen.
	// This is the existing M7Addons owner, not a second mount registry.
	if ( !s_AddonPathsChanged ) return false;
	s_AddonPathsChanged = false;
	engine->ClientCmd_Unrestricted( "update_addon_paths; mission_reload" ); // Real native commands; M7Addons owns their target bodies.
	return true;
}
bool CheckBaseModAddonAssociation( vgui::Panel *parent )
{
	// G67BB0 -> G19340, once per process; this is a main-menu open check,
	// not an artificial gate on Addons or CustomCampaigns.
	if ( s_AssociationAttempted ) return false;
	s_AssociationAttempted = true;
	const int state = AddonAssociation::GetAssociationState();
	if ( state == 0 ) { Msg( "Executing addoninstaller to register with vpk files\n" ); AddonAssociation::RegisterAssociation( parent ); }
	return state == 1; // G19340 lets the real window manager open window42.
}
CL4DFrame *CreateBaseModAddonsWindow( vgui::Panel *parent, const char *name )
{
	if ( !Q_stricmp( name, "Addons" ) )
	{
		Addons *window = new Addons( parent ); if ( window->IsLayoutReady() ) return window; delete window; return NULL;
	}
	if ( !Q_stricmp( name, "AddonAssociation" ) )
	{
		AddonAssociation *window = new AddonAssociation( parent ); if ( window->IsLayoutReady() ) return window; delete window; return NULL;
	}
	return NULL;
}
}
