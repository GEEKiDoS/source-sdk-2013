//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "vgui_int.h"
#include "ienginevgui.h"
#include "itextmessage.h"
#include "vguicenterprint.h"
#include "iloadingdisc.h"
#include "ifpspanel.h"
#include "imessagechars.h"
#include "inetgraphpanel.h"
#include "idebugoverlaypanel.h"
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui/IInput.h>
#if defined( L4D2 )
#include <vgui/ILocalize.h>
#if defined( _WIN32 ) && defined( PLATFORM_64BITS )
#include "winlite.h"
#endif
#endif // L4D2
#include "tier0/vprof.h"
#include "iclientmode.h"
#include <vgui_controls/Panel.h>
#include <KeyValues.h>
#include "filesystem.h"
#include "matsys_controls/matsyscontrols.h"

#ifdef SIXENSE
#include "sixense/in_sixense.h"
#endif

#if defined( TF_CLIENT_DLL )
#include "tf_gamerules.h"
#endif

using namespace vgui;

void MP3Player_Create( vgui::VPANEL parent );
void MP3Player_Destroy();

#include <vgui/IInputInternal.h>
vgui::IInputInternal *g_InputInternal = NULL;

#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

void GetVGUICursorPos( int& x, int& y )
{
	vgui::input()->GetCursorPos(x, y);
}

void SetVGUICursorPos( int x, int y )
{
	if ( !g_bTextMode )
	{
		vgui::input()->SetCursorPos(x, y);
	}
}

class CHudTextureHandleProperty : public vgui::IPanelAnimationPropertyConverter
{
public:
	virtual void GetData( Panel *panel, KeyValues *kv, PanelAnimationMapEntry *entry )
	{
		void *data = ( void * )( (*entry->m_pfnLookup)( panel ) );
		CHudTextureHandle *pHandle = ( CHudTextureHandle * )data;

		// lookup texture name for id
		if ( pHandle->Get() )
		{
			kv->SetString( entry->name(), pHandle->Get()->szShortName );
		}
		else
		{
			kv->SetString( entry->name(), "" );
		}
	}
	
	virtual void SetData( Panel *panel, KeyValues *kv, PanelAnimationMapEntry *entry )
	{
		void *data = ( void * )( (*entry->m_pfnLookup)( panel ) );
		
		CHudTextureHandle *pHandle = ( CHudTextureHandle * )data;

		const char *texturename = kv->GetString( entry->name() );
		if ( texturename && texturename[ 0 ] )
		{
			CHudTexture *currentTexture = gHUD.GetIcon( texturename );
			pHandle->Set( currentTexture );
		}
		else
		{
			pHandle->Set( NULL );
		}
	}

	virtual void InitFromDefault( Panel *panel, PanelAnimationMapEntry *entry )
	{
		void *data = ( void * )( (*entry->m_pfnLookup)( panel ) );

		CHudTextureHandle *pHandle = ( CHudTextureHandle * )data;

		const char *texturename = entry->defaultvalue();
		if ( texturename && texturename[ 0 ] )
		{
			CHudTexture *currentTexture = gHUD.GetIcon( texturename );
			pHandle->Set( currentTexture );
		}
		else
		{
			pHandle->Set( NULL );
		}
	}
};

static CHudTextureHandleProperty textureHandleConverter;

static void VGui_VideoMode_AdjustForModeChange( void )
{
	// Kill all our panels. We need to do this in case any of them
	//	have pointers to objects (eg: iborders) that will get freed
	//	when schemes get destroyed and recreated (eg: mode change).
	netgraphpanel->Destroy();
	debugoverlaypanel->Destroy();
#if defined( TRACK_BLOCKING_IO )
	iopanel->Destroy();
#endif
	fps->Destroy();
	messagechars->Destroy();
	loadingdisc->Destroy();

	// Recreate our panels.
	VPANEL gameToolParent = enginevgui->GetPanel( PANEL_CLIENTDLL_TOOLS );
	VPANEL toolParent = enginevgui->GetPanel( PANEL_TOOLS );
#if defined( TRACK_BLOCKING_IO )
	VPANEL gameDLLPanel = enginevgui->GetPanel( PANEL_GAMEDLL );
#endif

	loadingdisc->Create( gameToolParent );
	messagechars->Create( gameToolParent );

	// Debugging or related tool
	fps->Create( toolParent );
#if defined( TRACK_BLOCKING_IO )
	iopanel->Create( gameDLLPanel );
#endif
	netgraphpanel->Create( toolParent );
	debugoverlaypanel->Create( gameToolParent );
}

static void VGui_OneTimeInit()
{
	static bool initialized = false;
	if ( initialized )
		return;
	initialized = true;

	vgui::Panel::AddPropertyConverter( "CHudTextureHandle", &textureHandleConverter );


    g_pMaterialSystem->AddModeChangeCallBack( &VGui_VideoMode_AdjustForModeChange );
}

#if defined( L4D2 ) && defined( _WIN32 ) && defined( PLATFORM_64BITS )
namespace
{
using AddLocalizationFileFn = bool ( * )( ::ILocalize *, const char *, const char *, bool );

AddLocalizationFileFn s_pSDKAddFile;
IFileSystem *s_pLocalizationFS;
void **s_pAddFileSlot;
char s_AddonsRoot[MAX_PATH];
int s_nAddonsRootLength;

// GetSearchPath returns the required byte count including the terminator.
// If a mount changes between the two calls, don't consume a truncated list.
bool GetLocalizationSearchPaths( const char *pathID, bool packs, CUtlVector< char > &paths )
{
	int length = s_pLocalizationFS->GetSearchPath( pathID, packs, NULL, 0 );
	while ( length > 0 )
	{
		paths.SetCount( length );
		length = s_pLocalizationFS->GetSearchPath( pathID, packs, paths.Base(), paths.Count() );
		if ( length > 0 && length <= paths.Count() )
			return true;
	}
	return false;
}

bool LoadLocalizationProvider( ::ILocalize *localize, const char *absolute )
{
	FileHandle_t file = s_pLocalizationFS->Open( absolute, "rb", NULL );
	if ( file == FILESYSTEM_INVALID_HANDLE )
		return false;

	const unsigned int length = s_pLocalizationFS->Size( file );
	unsigned char bom[2];
	const bool valid = length > sizeof( bom ) && !( length & 1 ) &&
		s_pLocalizationFS->Read( bom, sizeof( bom ), file ) == sizeof( bom ) &&
		bom[0] == 0xff && bom[1] == 0xfe;
	s_pLocalizationFS->Close( file );
	if ( !valid )
	{
		Warning( "Ignoring invalid UTF-16LE localization file \"%s\".\n", absolute );
		return false;
	}

	// The name already identifies the provider. MOD keeps the native parser's
	// internal path query small; absolute member opens do not filter by path ID.
	return s_pSDKAddFile( localize, absolute, "MOD", false );
}

bool AddLocalizationFile( ::ILocalize *localize, const char *name, const char *pathID, bool fallbacks )
{
	// Keep the SDK's language selection, English-before-translation ordering and
	// -all_languages behavior. It dispatches each concrete name through AddFile.
	if ( V_IsAbsolutePath( name ) || V_strstr( name, "%language%" ) )
		return s_pSDKAddFile( localize, name, pathID, fallbacks );

	CUtlVector< char > paths;
	if ( !GetLocalizationSearchPaths( pathID, true, paths ) )
		return s_pSDKAddFile( localize, name, pathID, fallbacks );

	char relative[MAX_PATH];
	V_strncpy( relative, name, sizeof( relative ) );
	V_strlower( relative );
	const int relativeLength = V_strlen( relative );
	CUtlVector< const char * > overrides;
	bool loaded = false;
	for ( char *path = paths.Base(); *path; )
	{
		char *next = V_strchr( path, ';' );
		if ( next )
			*next++ = '\0';
		V_FixSlashes( path );
		const int pathLength = V_strlen( path );
		if ( pathLength && pathLength + 1 + relativeLength < MAX_PATH )
		{
			char absolute[MAX_PATH];
			if ( fallbacks || ( s_nAddonsRootLength && !V_strnicmp( path, s_AddonsRoot, s_nAddonsRootLength ) ) )
			{
				overrides.AddToTail( path );
			}
			else
			{
				V_ComposeFileName( path, relative, absolute, sizeof( absolute ) );
				if ( LoadLocalizationProvider( localize, absolute ) )
				{
					// A first-match request still selects one stock provider.
					// Add-ons above it only override tokens they actually supply.
					loaded = true;
					break;
				}
			}
		}
		if ( !next )
			break;
		path = next;
	}

	// Search paths are highest precedence first; parse in reverse so a token in
	// the last-mounted add-on wins. Each native call handles just one file, so a
	// broken copy cannot short-circuit parsing the other providers.
	for ( int i = overrides.Count() - 1; i >= 0; --i )
	{
		char absolute[MAX_PATH];
		V_ComposeFileName( overrides[i], relative, absolute, sizeof( absolute ) );
		loaded |= LoadLocalizationProvider( localize, absolute );
	}

	if ( !loaded )
		Warning( "CLocalizedStringTable::AddFile() failed to load file \"%s\".\n", name );
	return loaded;
}

// The engine and GameUI share this object with the client. Patch its AddFile
// slot, not the client pointer, so their native and reload callers also bypass
// the shipped SDK vgui2.dll's 13,000-byte relative search-path buffer.
class CLocalizationSearchPatch
{
public:
	void Install()
	{
		if ( s_pAddFileSlot )
			return;

		HMODULE module = GetModuleHandleW( L"vgui2.dll" );
		CreateInterfaceFn vguiFactory = Sys_GetFactory( "vgui2.dll" );
		CreateInterfaceFn fsFactory = Sys_GetFactory( "filesystem_stdio.dll" );
		if ( !module || !vguiFactory || !fsFactory )
			return;
		void *localize = vguiFactory( VGUI_LOCALIZE_INTERFACE_VERSION, NULL );
		IFileSystem *fs = static_cast< IFileSystem * >( fsFactory( FILESYSTEM_INTERFACE_VERSION, NULL ) );
		if ( !localize || !fs )
			return;
		void **slot = *reinterpret_cast< void *** >( localize );
		HMODULE owner = NULL;
		HMODULE tableOwner = NULL;
		const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
		if ( !GetModuleHandleExW( flags, reinterpret_cast< LPCWSTR >( slot ), &tableOwner ) || tableOwner != module ||
			!GetModuleHandleExW( flags, reinterpret_cast< LPCWSTR >( *slot ), &owner ) || owner != module )
		{
			Warning( "Unable to verify SDK VGUI localization interface ownership\n" );
			return;
		}

		// The x64 SDK implementation accepts absolute pak members; retail's
		// different implementation rejects them. Fail closed on an unknown DLL.
		static const unsigned char expected[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x44, 0x88, 0x4C,
			0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48 };
		if ( *slot != reinterpret_cast< unsigned char * >( module ) + 0x8EC0 ||
			memcmp( *slot, expected, sizeof( expected ) ) )
		{
			Warning( "Unrecognized SDK VGUI localization loader; using its native search behavior\n" );
			return;
		}

		s_pLocalizationFS = fs;
		// Match TerrorMountAddons: add-ons live under the last physical MOD root.
		CUtlVector< char > mods;
		s_nAddonsRootLength = 0;
		if ( GetLocalizationSearchPaths( "MOD", false, mods ) && mods[0] )
		{
			const char *mod = V_strrchr( mods.Base(), ';' );
			V_ComposeFileName( mod ? mod + 1 : mods.Base(), "addons", s_AddonsRoot, sizeof( s_AddonsRoot ) );
			V_AppendSlash( s_AddonsRoot, sizeof( s_AddonsRoot ) );
			V_FixSlashes( s_AddonsRoot );
			s_nAddonsRootLength = V_strlen( s_AddonsRoot );
		}

		DWORD protection;
		if ( !VirtualProtect( slot, sizeof( *slot ), PAGE_READWRITE, &protection ) )
		{
			Warning( "Unable to adapt SDK VGUI localization search paths\n" );
			return;
		}
		s_pSDKAddFile = reinterpret_cast< AddLocalizationFileFn >( *slot );
		s_pAddFileSlot = slot;
		m_hModule = module;
		InterlockedExchangePointer( slot, reinterpret_cast< void * >( AddLocalizationFile ) );
		if ( !VirtualProtect( slot, sizeof( *slot ), protection, &protection ) )
			Warning( "Unable to restore SDK VGUI localization table protection\n" );
	}

	void Uninstall()
	{
		if ( !s_pAddFileSlot || GetModuleHandleW( L"vgui2.dll" ) != m_hModule )
			return;
		DWORD protection;
		if ( !VirtualProtect( s_pAddFileSlot, sizeof( *s_pAddFileSlot ), PAGE_READWRITE, &protection ) )
			Error( "Unable to detach SDK VGUI localization handler before client unload\n" );
		InterlockedCompareExchangePointer( s_pAddFileSlot,
			reinterpret_cast< void * >( s_pSDKAddFile ), reinterpret_cast< void * >( AddLocalizationFile ) );
		if ( !VirtualProtect( s_pAddFileSlot, sizeof( *s_pAddFileSlot ), protection, &protection ) )
			Warning( "Unable to restore SDK VGUI localization table protection\n" );
		s_pAddFileSlot = NULL;
	}

	~CLocalizationSearchPatch() { Uninstall(); }

private:
	HMODULE m_hModule = NULL;
};

CLocalizationSearchPatch s_LocalizationSearchPatch;
}

void VGui_InstallLocalizationSearchPatch()
{
	s_LocalizationSearchPatch.Install();
}
#endif

bool VGui_Startup( CreateInterfaceFn appSystemFactory )
{
	if ( !vgui::VGui_InitInterfacesList( "CLIENT", &appSystemFactory, 1 ) )
		return false;

#ifdef L4D2
	g_pVGuiLocalize->AddFile( "resource/left4dead2_%language%.txt", "GAME", true );
#endif

	if ( !vgui::VGui_InitMatSysInterfacesList( "CLIENT", &appSystemFactory, 1 ) )
		return false;

	g_InputInternal = (IInputInternal *)appSystemFactory( VGUI_INPUTINTERNAL_INTERFACE_VERSION,  NULL );
	if ( !g_InputInternal )
	{
		return false; // c_vguiscreen.cpp needs this!
	}

	VGui_OneTimeInit();

	// Create any root panels for .dll
	VGUI_CreateClientDLLRootPanel();

	// Make sure we have a panel
	VPANEL root = VGui_GetClientDLLRootPanel();
	if ( !root )
	{
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void VGui_CreateGlobalPanels( void )
{
	VPANEL gameToolParent = enginevgui->GetPanel( PANEL_CLIENTDLL_TOOLS );
	VPANEL toolParent = enginevgui->GetPanel( PANEL_TOOLS );
#if defined( TRACK_BLOCKING_IO )
	VPANEL gameDLLPanel = enginevgui->GetPanel( PANEL_GAMEDLL );
#endif
	// Part of game
	internalCenterPrint->Create( gameToolParent );
	loadingdisc->Create( gameToolParent );
	messagechars->Create( gameToolParent );

	// Debugging or related tool
	fps->Create( toolParent );
#if defined( TRACK_BLOCKING_IO )
	iopanel->Create( gameDLLPanel );
#endif
	netgraphpanel->Create( toolParent );
	debugoverlaypanel->Create( gameToolParent );

#ifndef _X360
	// Create mp3 player off of tool parent panel
	MP3Player_Create( toolParent );
#endif
#ifdef SIXENSE
	g_pSixenseInput->CreateGUI( gameToolParent );
#endif
}

void VGui_Shutdown()
{
	VGUI_DestroyClientDLLRootPanel();

#ifndef _X360
	MP3Player_Destroy();
#endif

	netgraphpanel->Destroy();
	debugoverlaypanel->Destroy();
#if defined( TRACK_BLOCKING_IO )
	iopanel->Destroy();
#endif
	fps->Destroy();

	messagechars->Destroy();
	loadingdisc->Destroy();
	internalCenterPrint->Destroy();

	if ( g_pClientMode )
	{
		g_pClientMode->VGui_Shutdown();
	}

	// Make sure anything "marked for deletion"
	//  actually gets deleted before this dll goes away
	vgui::ivgui()->RunFrame();

#if defined( L4D2 ) && defined( _WIN32 ) && defined( PLATFORM_64BITS )
	s_LocalizationSearchPatch.Uninstall();
#endif
}

static ConVar cl_showpausedimage( "cl_showpausedimage", "1", 
#if defined( L4D2 )
	0x0000000A
#else
	0
#endif
, "Show the 'Paused' image when game is paused." );

//-----------------------------------------------------------------------------
// Things to do before rendering vgui stuff...
//-----------------------------------------------------------------------------
void VGui_PreRender()
{
	VPROF( "VGui_PreRender" );
	tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__ );

	// 360 does not use these plaques
	if ( IsPC() )
	{
		loadingdisc->SetLoadingVisible( engine->IsDrawingLoadingImage() && !engine->IsPlayingDemo() );
		
		bool bShowPausedImage = !enginevgui->IsGameUIVisible() && cl_showpausedimage.GetBool() && engine->IsPaused() && !engine->IsTakingScreenshot() && !engine->IsPlayingDemo();
#if !defined( TF_CLIENT_DLL )
		loadingdisc->SetPausedVisible( bShowPausedImage, engine->GetPausedExpireTime()  );
#else
		bShowPausedImage &= ( TFGameRules() && !TFGameRules()->IsInTraining() );
		loadingdisc->SetPausedVisible( bShowPausedImage, engine->GetPausedExpireTime() );
#endif
	}
}

void VGui_PostRender()
{
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : cl_panelanimation - 
//-----------------------------------------------------------------------------

#if defined( L4D2 )
CON_COMMAND_F( cl_panelanimation, "Shows panel animation variables: <panelname | blank for all panels>." , 0x00000008)
#else
CON_COMMAND( cl_panelanimation, "Shows panel animation variables: <panelname | blank for all panels>." )
#endif
{
	if ( args.ArgC() == 2 )
	{
		PanelAnimationDumpVars( args[1] );
	}
	else
	{
		PanelAnimationDumpVars( NULL );
	}
}

void GetHudSize( int& w, int &h )
{
	vgui::surface()->GetScreenSize( w, h );

	VPANEL hudParent = enginevgui->GetPanel( PANEL_CLIENTDLL );
	if ( hudParent )
	{
		vgui::ipanel()->GetSize( hudParent, w, h );
	}
}
