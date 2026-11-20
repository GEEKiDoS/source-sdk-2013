//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_addons.h"
#include "l4d2/ui/basemodui/terror_basemodui_panellist.h"
#include "l4d2/ui/basemodui/terror_basemodui_flyout.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/ui/basemodui/terror_basemodui_ingame.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/ComboBox.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui/ISurface.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "tier1/utlbuffer.h"
#include "tier1/utldict.h"
#include "steam/steam_api.h"
#include <algorithm>
#include <wctype.h>
#include <time.h>
#include <setjmp.h>
#include <stdio.h>
#ifdef _WIN32
#include "winlite.h"
#include <shellapi.h>
#endif
extern "C" {
#include "jpeglib/jpeglib.h"
}
#include "tier0/memdbgon.h"

namespace L4D360UI
{
static ConVar addons_hide_conflict_dialog( "addons_hide_conflict_dialog", "0", FCVAR_ARCHIVE );
static bool s_AddonPathsChanged;
enum AddonSort_t
{
	ADDON_SORT_CUSTOM,
	ADDON_SORT_NAME_ASC, ADDON_SORT_NAME_DESC,
	ADDON_SORT_SIZE_ASC, ADDON_SORT_SIZE_DESC,
	ADDON_SORT_DATE_ASC, ADDON_SORT_DATE_DESC,
	ADDON_SORT_COUNT
};
static ConVar addons_sort( "addons_sort", "0", FCVAR_ARCHIVE, "Add-ons list view: custom, name, size, or date.", true, 0, true, ADDON_SORT_COUNT - 1 );

static int CompareAddonText( const wchar_t *left, const wchar_t *right, int count = -1 )
{
#ifdef _WIN32
	return CompareStringOrdinal( left, count, right, count, TRUE ) - CSTR_EQUAL;
#else
	for ( int i = 0; count < 0 || i < count; ++i )
	{
		int diff = towlower( left[i] ) - towlower( right[i] );
		if ( diff || !left[i] )
			return diff;
	}
	return 0;
#endif
}

static bool MatchesAddonText( const wchar_t *text, const wchar_t *query, int length )
{
	for ( int i = static_cast<int>( wcslen( text ) ) - length; i >= 0; --i )
		if ( !CompareAddonText( text + i, query, length ) )
			return true;
	return false;
}

struct AddonViewItem_t
{
	int index;
	const AddonInfo_t *info;
};

struct CompareAddonView
{
	int sort;
	bool operator()( const AddonViewItem_t &left, const AddonViewItem_t &right ) const
	{
		const AddonInfo_t &a = *left.info, &b = *right.info;
		int order;
		if ( sort == ADDON_SORT_NAME_ASC || sort == ADDON_SORT_NAME_DESC )
			order = CompareAddonText( a.title, b.title );
		else if ( sort == ADDON_SORT_SIZE_ASC || sort == ADDON_SORT_SIZE_DESC )
			order = ( a.size > b.size ) - ( a.size < b.size );
		else
			order = ( a.modified > b.modified ) - ( a.modified < b.modified );

		const bool descending = sort == ADDON_SORT_NAME_DESC || sort == ADDON_SORT_SIZE_DESC || sort == ADDON_SORT_DATE_DESC;
		return order ? ( descending ? order > 0 : order < 0 ) : left.index < right.index;
	}
};

static void Visible( vgui::Panel *panel, const char *name, bool visible )
{
	vgui::Panel *child = panel->FindChildByName( name, false );
	if ( child )
		child->SetVisible( visible );
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

static void ShowConflictNotice( vgui::Panel *parent )
{
	GenericConfirmation::UsageData_t data;
	data.pWindowTitle = "#L4D_PLUGIN_WARNING_TITLE";
	data.pMessageText = "#L4D360UI_ADDON_CONFLICT_WARNING";
	data.bOkButtonEnabled = true; data.bCancelButtonEnabled = false;
	CL4DFrame *caller = dynamic_cast<CL4DFrame *>( parent );
	GenericConfirmation *dialog = new GenericConfirmation( caller && caller->GetParent() ? caller->GetParent() : parent );
	if ( !dialog->LoadLayout( "resource/ui/l4d360ui/genericconfirmation.res" ) ) { delete dialog; return; }
	dialog->SetNavBack( caller );
	dialog->SetUsageData( data ); dialog->OnOpen();
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
	m_pManagementStrings( new KeyValues( "AddonManagement" ) ), m_pAddonList( NULL ), m_pRowLayout( new KeyValues( "" ) ),
	m_Texture( -1 ), m_DefaultTexture( -1 ), m_SelectedAddon( -1 ), m_ImageFade( 0 ), m_bSquareImage( true ),
	m_bLayoutReady( false ), m_bReady( false ), m_bClosing( false ), m_bConflictWarned( false ), m_bOrderChanged( false )
{
	m_Filter[0] = 0; m_Mod[0] = 0; m_ListPath[0] = 0;
	m_pList = new GenericPanelList( this, "GplAddons" );
	m_pList->SetScrollBarVisible( true ); m_pList->SetScrollProgressVisible( true );
	// Bind the dialog's labels, not the identically named labels in list rows.
	m_pName = new vgui::Label( this, "LblName", "" );
	m_pType = new vgui::Label( this, "LblType", "" );
	m_pAuthor = new vgui::Label( this, "LblAuthor", "" );
	m_pDescription = new vgui::Label( this, "LblDescription", "" );
	m_pNoAddons = new vgui::Label( this, "LblNoAddons", "#L4D360UI_No_Addons_Installed" );
	m_pIcon = new vgui::ImagePanel( this, "ImgAddonIcon" );
	m_pIcon->SetPaintEnabled( false ); // Bounds anchor; PaintBackground draws the JPEG.
	m_pIconSpinner = new vgui::ImagePanel( this, "ImgAddonIconSpinner" );
	char language[64];
	engine->GetUILanguage( language, sizeof( language ) );
	const char *translated = "resource/addons_management_schinese.txt";
	if ( Q_stricmp( language, "schinese" ) || !m_pManagementStrings->LoadFromFile( filesystem, translated, "MOD" ) )
		m_pManagementStrings->LoadFromFile( filesystem, "resource/addons_management_english.txt", "MOD" );

	m_pSearchLabel = new vgui::Label( this, "LblSearchAddons", ManagementText( "Search" ) );
	m_pSearchEmpty = new vgui::Label( this, "LblSearchEmpty", ManagementText( "NoMatches" ) );
	m_pSearch = new vgui::TextEntry( this, "TxtSearchAddons" );
	m_pSearch->SetKeyBoardInputEnabled( true );
	m_pSearch->SetTabPosition( 2 );
	m_pSearch->SetAllowNonAsciiCharacters( true );
	m_pSearch->SetMaximumCharCount( MAX_PATH - 1 );
	m_pSearch->AddActionSignalTarget( this );
	m_pSort = new vgui::ComboBox( this, "CmbAddonSort", ADDON_SORT_COUNT, false );
	const char *sorts[] = { "Custom", "NameAsc", "NameDesc", "SizeAsc", "SizeDesc", "DateAsc", "DateDesc" };
	COMPILE_TIME_ASSERT( ARRAYSIZE( sorts ) == ADDON_SORT_COUNT );
	for ( int i = 0; i < ADDON_SORT_COUNT; ++i )
		m_pSort->AddItem( ManagementText( sorts[i] ), NULL );
	m_pSort->SilentActivateItemByRow( addons_sort.GetInt() );
	m_pSort->AddActionSignalTarget( this );
	m_pMoveUp = new vgui::Button( this, "BtnMoveAddonUp", ManagementText( "MoveUp" ), this, "MoveAddonUp" );
	m_pMoveDown = new vgui::Button( this, "BtnMoveAddonDown", ManagementText( "MoveDown" ), this, "MoveAddonDown" );
	m_pFileInfo = new vgui::Label( this, "LblAddonFileInfo", "" );
	m_pOrderHint = new vgui::Label( this, "LblAddonOrderHint", ManagementText( "OrderHint" ) );
	m_pOrderHint->SetWrap( true );
	m_pOrderHint->SetContentAlignment( vgui::Label::a_northwest );
	SetFooterEnabled( true ); // The real window manager supplies the caller separately.
	m_bLayoutReady = LoadLayout( "resource/ui/l4d360ui/addons.res" ) && LoadTerrorUiResource( m_pRowLayout, "resource/ui/l4d360ui/addonlistitem.res" );
}
Addons::~Addons()
{
	ClearImage();
	if ( m_pAddonList )
		m_pAddonList->deleteThis();
	m_pRowLayout->deleteThis();
	m_pManagementStrings->deleteThis();
}

const wchar_t *Addons::ManagementText( const char *key ) const
{
	return m_pManagementStrings->GetWString( key, L"" );
}

void Addons::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	const vgui::HFont font = scheme->GetFont( "DefaultMedium", true );
	m_pSearch->SetFont( font );
	m_pSort->SetFont( font );
	m_pMoveUp->SetFont( font );
	m_pMoveDown->SetFont( font );
	m_pSearchLabel->SetFont( font );
	m_pSearchEmpty->SetFont( font );
	m_pOrderHint->SetFont( font );
	m_pFileInfo->SetFont( scheme->GetFont( "Default", true ) );
}

void Addons::PerformLayout()
{
	BaseClass::PerformLayout();
	const vgui::HScheme scheme = GetScheme();
	int x, y, wide, tall;
	m_pList->GetBounds( x, y, wide, tall );
	const int gap = vgui::scheme()->GetProportionalScaledValueEx( scheme, 4 );
	const int line = vgui::scheme()->GetProportionalScaledValueEx( scheme, 18 );
	const int searchLabel = vgui::scheme()->GetProportionalScaledValueEx( scheme, 70 );
	const int detailsX = m_pIcon->GetXPos();
	const int detailsWide = vgui::scheme()->GetProportionalScaledValueEx( scheme, 290 );
	const int sortWide = vgui::scheme()->GetProportionalScaledValueEx( scheme, 168 );
	const int buttonWide = vgui::scheme()->GetProportionalScaledValueEx( scheme, 76 );
	const int top = y - line - 2 * gap;
	const int bottom = y + tall + line + 2 * gap;
	m_pSearchLabel->SetBounds( x, top, searchLabel, line );
	m_pSearch->SetBounds( x + searchLabel + gap, top, MAX( 1, wide - searchLabel - gap ), line );
	m_pSort->SetBounds( detailsX, top, sortWide, line );
	m_pSearchEmpty->SetBounds( x + gap, y + gap, MAX( 1, wide - 2 * gap ), line );
	m_pMoveUp->SetBounds( detailsX, bottom, buttonWide, line );
	m_pMoveDown->SetBounds( detailsX + buttonWide + gap, bottom, buttonWide, line );
	m_pOrderHint->SetBounds( detailsX, bottom + line + gap, detailsWide, 2 * line );
	int descriptionX, descriptionY;
	m_pDescription->GetPos( descriptionX, descriptionY );
	m_pFileInfo->SetBounds( descriptionX, descriptionY + m_pDescription->GetTall(), detailsWide, line );
}
void Addons::LoadControlSettings( const char *resource, const char *pathID, KeyValues *preloaded, KeyValues *conditions )
{
	KeyValues::AutoDelete owned( preloaded ? NULL : new KeyValues( "" ) );
	KeyValues *layout = preloaded ? preloaded : static_cast<KeyValues *>( owned );
	if ( !preloaded && !LoadTerrorUiResource( layout, resource ) )
		return;

	const char *obsolete[] = { "CheckButtonAssociation", "LblCheckNoAssociation" };
	for ( int i = 0; i < ARRAYSIZE( obsolete ); ++i )
	{
		if ( KeyValues *control = layout->FindKey( obsolete[i] ) )
		{
			layout->RemoveSubKey( control );
			control->deleteThis();
		}
	}

	BaseClass::LoadControlSettings( resource, pathID, layout, conditions );
	// The resource also reloads during scheme application, restoring its
	// default visibility. Reapply selection and empty state on every load.
	Visible( this, "AddonSupportRequiredPanel", false );
	Visible( this, "InstallingAddonSupportPanel", false );
	Visible( this, "BtnInstallSupport", false );
	Visible( this, "IconInstallSupport", false );
	UpdateDetails();
}
void Addons::OnOpen()
{
	if ( !m_bLayoutReady ) return;
	const bool firstOpen = !m_bReady;
	if ( firstOpen )
	{
		ModRoot( m_Mod, sizeof( m_Mod ) ); Q_snprintf( m_ListPath, sizeof( m_ListPath ), "%saddonlist.txt", m_Mod );
		m_DefaultTexture = vgui::surface()->DrawGetTextureId( "vgui/menu_mode_campaign" );
		if ( m_DefaultTexture == -1 ) { m_DefaultTexture = vgui::surface()->CreateNewTextureID(); vgui::surface()->DrawSetTextureFile( m_DefaultTexture, "vgui/menu_mode_campaign", true, false ); }
	}
	else if ( !m_bOrderChanged )
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
	m_bReady = true;
	if ( m_bOrderChanged )
		RebuildRows( m_SelectedAddon );
	else
		PopulateAddonInfo();
	UpdateFooter();
	if ( firstOpen )
	{
		DropDownMenu *filter = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpFilterTags", true ) );
		if ( filter ) filter->SetCurrentSelection( "filter_tags_" );
	}
}
void Addons::PopulateAddonInfo()
{
	m_pList->RemoveAllPanelItems();
	m_SelectedAddon = -1;
	ClearImage();
	m_Addons.RemoveAll();
	if ( m_pAddonList )
		m_pAddonList->deleteThis();
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
		if ( info.archive )
		{
			info.size = filesystem->Size( root, NULL );
			info.modified = filesystem->GetFileTime( root, NULL );
		}
		if ( info.archive ) filesystem->AddSearchPath( root, "ADDONINFO", PATH_ADD_TO_HEAD );
		KeyValues::AutoDelete metadata( new KeyValues( "AddonInfo" ) );
		if ( metadata->LoadFromFile( filesystem, path, NULL ) || info.archive )
		{
			MetadataText( metadata, "addontitle", info.title, sizeof( info.title ), L"" );
			if ( !info.title[0] )
				V_UTF8ToUnicode( info.filename, info.title, sizeof( info.title ) );
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
void Addons::RebuildRows( int preferred )
{
	if ( !m_bReady )
		return;

	m_pList->RemoveAllPanelItems();
	int mask = 1;
	for ( int i = 0; i < ARRAYSIZE( s_Content ); ++i )
		if ( !Q_stricmp( m_Filter, s_Content[i].tag ) )
			mask = s_Content[i].bit;

	wchar_t query[MAX_PATH];
	m_pSearch->GetText( query, sizeof( query ) );
	const int length = static_cast<int>( wcslen( query ) );
	CUtlVector<AddonViewItem_t> view;
	view.EnsureCapacity( m_Addons.Count() );
	FOR_EACH_VEC( m_Addons, i )
	{
		const AddonInfo_t &info = m_Addons[i];
		if ( !( mask & info.content ) )
			continue;
		if ( length && !MatchesAddonText( info.title, query, length ) )
		{
			wchar_t filename[MAX_PATH];
			V_UTF8ToUnicode( info.filename, filename, sizeof( filename ) );
			if ( !MatchesAddonText( filename, query, length ) )
				continue;
		}
		AddonViewItem_t item = { i, &info };
		view.AddToTail( item );
	}
	const int sort = addons_sort.GetInt();
	if ( sort != ADDON_SORT_CUSTOM && view.Count() > 1 )
	{
		CompareAddonView compare = { sort };
		std::sort( view.Base(), view.Base() + view.Count(), compare );
	}

	int selected = 0;
	FOR_EACH_VEC( view, i )
	{
		const AddonViewItem_t &entry = view[i];
		m_pList->AddPanelItem( new AddonListItem( m_pList, this, entry.index, *entry.info ) );
		if ( entry.index == preferred )
			selected = i;
	}
	m_pList->InvalidateLayout( true );
	if ( m_pList->GetPanelItemCount() )
		m_pList->SelectPanelItem( selected );

	vgui::Panel *filter = FindChildByName( "DrpFilterTags", true );
	if ( filter )
		filter->SetEnabled( m_Addons.Count() > 0 );
	UpdateDetails();
}

int Addons::SelectedRow() const
{
	const vgui::Panel *selected = m_pList->GetSelectedPanelItem();
	if ( selected )
		for ( int i = 0; i < m_pList->GetPanelItemCount(); ++i )
			if ( m_pList->GetPanelItem( i ) == selected )
				return i;
	return -1;
}

void Addons::UpdateMoveButtons()
{
	const bool custom = addons_sort.GetInt() == ADDON_SORT_CUSTOM;
	const int row = SelectedRow();
	m_pMoveUp->SetEnabled( custom && row > 0 );
	m_pMoveDown->SetEnabled( custom && row >= 0 && row + 1 < m_pList->GetPanelItemCount() );
	m_pOrderHint->SetVisible( custom );
}

void Addons::MoveAddon( int step )
{
	const int selected = SelectedRow();
	if ( addons_sort.GetInt() != ADDON_SORT_CUSTOM || selected < 0 )
		return;
	AddonListItem *from = static_cast<AddonListItem *>( m_pList->GetPanelItem( selected ) );
	AddonListItem *to = static_cast<AddonListItem *>( m_pList->GetPanelItem( selected + step ) );
	if ( !to )
		return;

	const int a = from->GetAddonIndex(), b = to->GetAddonIndex();
	KeyValues *first = m_pAddonList->FindKey( m_Addons[a].filename );
	KeyValues *second = m_pAddonList->FindKey( m_Addons[b].filename );
	if ( !first || !second )
		return;

	// Keep filtered/hidden entries in their original slots. The real remount
	// consumes these ordered keys, adding each enabled archive at GAME head.
	CUtlVector<KeyValues *> entries;
	entries.EnsureCapacity( m_Addons.Count() );
	while ( KeyValues *entry = m_pAddonList->GetFirstSubKey() )
	{
		m_pAddonList->RemoveSubKey( entry );
		entries.AddToTail( entry );
	}
	KeyValues *tail = NULL;
	FOR_EACH_VEC( entries, i )
	{
		KeyValues *entry = entries[i];
		if ( entry == first )
			entry = second;
		else if ( entry == second )
			entry = first;
		if ( tail )
			tail->SetNextKey( entry );
		else
			m_pAddonList->AddSubKey( entry );
		tail = entry;
	}
	std::swap( m_Addons[a], m_Addons[b] );
	m_bOrderChanged = true;
	m_SelectedAddon = -1;
	ClearImage();
	RebuildRows( b );
}

void Addons::OnTextChanged( vgui::Panel *panel )
{
	if ( panel == m_pSort )
	{
		const int sort = m_pSort->GetActiveItem();
		if ( sort < 0 || sort >= ADDON_SORT_COUNT || sort == addons_sort.GetInt() )
			return;
		addons_sort.SetValue( sort );
	}
	else if ( panel != m_pSearch )
	{
		return;
	}
	if ( m_bReady )
		RebuildRows( m_SelectedAddon );
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
	const int index = selected ? row->GetAddonIndex() : -1;

	m_pNoAddons->SetVisible( m_bReady && m_Addons.Count() == 0 );
	wchar_t query[2];
	m_pSearch->GetText( query, sizeof( query ) );
	const bool empty = m_bReady && m_Addons.Count() > 0 && !selected;
	m_pSearchEmpty->SetVisible( empty && query[0] );
	Visible( this, "LblNoAddonsFiltered", empty && !query[0] );
	m_pFileInfo->SetVisible( selected && m_Addons[index].archive );
	UpdateMoveButtons();
	Visible( this, "ImgAddonIconOverlay", false );
	Visible( this, "ImgGlobalAddonIconSpinner", false );
	Visible( this, "BtnVisitWorkshop", false );
	Visible( this, "IconVisitWorkshop", false );
	m_pName->SetVisible( selected );
	m_pType->SetVisible( selected );
	m_pAuthor->SetVisible( selected );
	m_pDescription->SetVisible( selected );
	m_pIcon->SetVisible( selected );
	m_pIconSpinner->SetVisible( selected && Plat_FloatTime() < m_ImageFade + 0.3f );

	if ( index == m_SelectedAddon )
		return;

	m_SelectedAddon = index;
	ClearImage();
	if ( !selected )
		return;

	const AddonInfo_t &info = m_Addons[index];
	m_pName->SetText( info.title );
	m_pType->SetText( info.types );
	m_pDescription->SetText( info.description );
	wchar_t author[260];
	Localized( "#L4D360UI_Addon_By", author, sizeof( author ) );
	V_wcsncat( author, info.author, ARRAYSIZE( author ) );
	m_pAuthor->SetText( author );
	if ( info.archive )
	{
		wchar_t size[32], date[32], fileInfo[128];
		V_snwprintf( size, ARRAYSIZE( size ), L"%.2f", info.size / ( 1024.0 * 1024.0 ) );
		const time_t modified = info.modified;
		const struct tm *local = info.modified ? localtime( &modified ) : NULL;
		V_wcsncpy( date, L"-", sizeof( date ) );
		if ( local )
			wcsftime( date, ARRAYSIZE( date ), L"%Y-%m-%d %H:%M", local );
		g_pVGuiLocalize->ConstructString( fileInfo, sizeof( fileInfo ), ManagementText( "FileInfo" ), 2, size, date );
		m_pFileInfo->SetText( fileInfo );
	}
	LoadPreview( info );
}
void Addons::LoadPreview( const AddonInfo_t &info )
{
	char root[MAX_PATH];
	Q_snprintf( root, sizeof( root ), "%saddons\\%s", m_Mod, info.filename );
	if ( info.archive )
		filesystem->AddSearchPath( root, "ADDONINFO", PATH_ADD_TO_HEAD );

	CUtlBuffer bytes;
	const bool read = filesystem->ReadFile( info.image, NULL, bytes );
	if ( info.archive )
		filesystem->RemoveSearchPath( root, "ADDONINFO" );

	if ( read )
		LoadBaseModJpegTexture( bytes.Base(), bytes.TellPut(), m_Texture, m_bSquareImage );
	m_ImageFade = Plat_FloatTime() + 0.25f;
	m_pIconSpinner->SetVisible( true );
}
void Addons::ClearImage()
{
	if ( m_Texture != -1 )
	{
		vgui::surface()->DestroyTextureID( m_Texture );
		m_Texture = -1;
	}
	m_ImageFade = 0;
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
	// The SDK surface pads procedural textures to power-of-two dimensions.
	// RGBAEx sets the UV bounds to the uploaded pixels; RGBA samples the padding.
	texture = vgui::surface()->CreateNewTextureID( true );
	vgui::surface()->DrawSetTextureRGBAEx( texture, rgba.Base(), width, height, IMAGE_FORMAT_RGBA8888 );
	square = width == height;
	return true;
}
void Addons::PaintBackground()
{
	DrawDialogBackground( "#L4D360UI_My_Addons", NULL, "#L4D360UI_My_Addons_Desc", NULL, NULL, false );
	vgui::Panel *image = m_pIcon; if ( !image->IsVisible() ) return;
	int x, y; image->GetPos( x, y ); int wide = image->GetWide(), tall = image->GetTall();
	vgui::surface()->DrawSetColor( 0, 0, 0, 255 ); vgui::surface()->DrawFilledRectFade( x, y, x + wide, y + tall, 128, 0, true );
	if ( m_bSquareImage || m_Texture == -1 ) { x += ( wide - tall ) / 2; wide = tall; }
	const int texture = m_Texture == -1 ? m_DefaultTexture : m_Texture;
	const float fade = m_ImageFade ? RemapValClamped( static_cast<float>( Plat_FloatTime() ), m_ImageFade, m_ImageFade + 0.3f, 0.0f, 1.0f ) : 0.0f;
	if ( texture != -1 )
	{
		vgui::surface()->DrawSetTexture( texture );
		vgui::surface()->DrawSetColor( 255, 255, 255, static_cast<int>( fade * 255 ) );
		vgui::surface()->DrawTexturedRect( x, y, x + wide, y + tall );
	}
	m_pIconSpinner->SetAlpha( static_cast<int>( ( 1.0f - fade ) * 255 ) );
}
void Addons::OnThink()
{
	BaseClass::OnThink();
	m_pIconSpinner->SetFrame( static_cast<int>( Plat_FloatTime() * 10.0 ) );
	m_pIconSpinner->SetVisible( m_SelectedAddon != -1 && Plat_FloatTime() < m_ImageFade + 0.3f );
}
bool Addons::SaveAddonList()
{
	if ( !m_pAddonList ) return false;
	FOR_EACH_VEC( m_Addons, i ) m_pAddonList->SetInt( m_Addons[i].filename, m_Addons[i].enabled ? 1 : 0 );
	bool saved = m_pAddonList->SaveToFile( filesystem, m_ListPath, NULL );
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
	if ( !Q_stricmp( command, "MoveAddonUp" ) || !Q_stricmp( command, "MoveAddonDown" ) )
	{
		MoveAddon( !Q_stricmp( command, "MoveAddonUp" ) ? -1 : 1 );
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
	if ( collision && warn && !m_bConflictWarned && !addons_hide_conflict_dialog.GetBool() ) { m_bConflictWarned = true; ShowConflictNotice( this ); }
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
CL4DFrame *CreateBaseModAddonsWindow( vgui::Panel *parent, const char *name )
{
	if ( !Q_stricmp( name, "Addons" ) )
	{
		Addons *window = new Addons( parent ); if ( window->IsLayoutReady() ) return window; delete window; return NULL;
	}
	return NULL;
}
}
