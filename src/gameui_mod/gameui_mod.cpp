//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: gameui_mod.dll, served by the L4D2 launcher in place of GameUI.dll. Implements the
//          complete IGameUI interface over the original SDK GameUI, which it loads itself, and
//          hands level-loading progress to the game-owned loading screen (IGameUIMod).
//
//=============================================================================//
#include "GameUI/IGameUI.h"
#include "GameUI/IGameUIMod.h"
#include "GameUI/IGameConsole.h"
#include <windows.h>

static HMODULE s_hRealModule;
static CreateInterfaceFn s_pfnRealFactory;
static IGameUI *s_pRealGameUI;

static bool LoadRealGameUI()
{
	if ( s_pRealGameUI )
		return true;
	char path[MAX_PATH];
	const DWORD length = GetEnvironmentVariableA( "SDK_EXEC_DIR", path, sizeof( path ) );
	if ( !length || length + sizeof( "\\bin\\x64\\GameUI.dll" ) > sizeof( path ) )
		return false;
	lstrcatA( path, "\\bin\\x64\\GameUI.dll" );
	s_hRealModule = LoadLibraryExA( path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH );
	if ( !s_hRealModule )
		return false;
	s_pfnRealFactory = reinterpret_cast< CreateInterfaceFn >( GetProcAddress( s_hRealModule, "CreateInterface" ) );
	s_pRealGameUI = s_pfnRealFactory ? static_cast< IGameUI * >( s_pfnRealFactory( GAMEUI_INTERFACE_VERSION, NULL ) ) : NULL;
	return s_pRealGameUI != NULL;
}

class CGameUIMod : public IGameUI, public IGameUIMod
{
public:
	CGameUIMod() : m_pClient( NULL ), m_bLoading( false ), m_flFraction( 0.0f ) { m_szStatus[0] = '\0'; }

	// IGameUIMod
	virtual void RegisterClient( IGameUIModClient *pClient ) OVERRIDE
	{
		m_pClient = pClient;
		if ( m_pClient && m_bLoading )
		{
			m_pClient->LoadingStarted();
			m_pClient->LoadingProgress( m_flFraction, m_szStatus );
		}
	}

	// IGameUI
	virtual void Initialize( CreateInterfaceFn factory ) OVERRIDE { s_pRealGameUI->Initialize( factory ); }
	virtual void PostInit() OVERRIDE { s_pRealGameUI->PostInit(); }
	virtual void Connect( CreateInterfaceFn gameFactory ) OVERRIDE { s_pRealGameUI->Connect( gameFactory ); }
	virtual void Start() OVERRIDE { s_pRealGameUI->Start(); }
	virtual void Shutdown() OVERRIDE { m_pClient = NULL; s_pRealGameUI->Shutdown(); }
	virtual void RunFrame() OVERRIDE { s_pRealGameUI->RunFrame(); }
	virtual void OnGameUIActivated() OVERRIDE { s_pRealGameUI->OnGameUIActivated(); }
	virtual void OnGameUIHidden() OVERRIDE { s_pRealGameUI->OnGameUIHidden(); }
	virtual void OLD_OnConnectToServer( const char *game, int IP, int port ) OVERRIDE { s_pRealGameUI->OLD_OnConnectToServer( game, IP, port ); }
	virtual void OnDisconnectFromServer_OLD( uint8 eSteamLoginFailure, const char *username ) OVERRIDE { s_pRealGameUI->OnDisconnectFromServer_OLD( eSteamLoginFailure, username ); }
	virtual void OnLevelLoadingStarted( bool bShowProgressDialog ) OVERRIDE
	{
		m_bLoading = true;
		m_flFraction = 0.0f;
		m_szStatus[0] = '\0';
		// GameUI keeps its loading bookkeeping; its own progress dialog is replaced by the client's.
		s_pRealGameUI->OnLevelLoadingStarted( m_pClient ? false : bShowProgressDialog );
		if ( !m_pClient )
			return;
		IGameConsole *pConsole = static_cast< IGameConsole * >( s_pfnRealFactory( GAMECONSOLE_INTERFACE_VERSION, NULL ) );
		if ( pConsole )
			pConsole->Hide();
		m_pClient->LoadingStarted();
	}
	virtual void OnLevelLoadingFinished( bool bError, const char *failureReason, const char *extendedReason ) OVERRIDE
	{
		s_pRealGameUI->OnLevelLoadingFinished( bError, failureReason, extendedReason );
		if ( m_bLoading && m_pClient )
			m_pClient->LoadingFinished( bError, failureReason, extendedReason );
		m_bLoading = false;
	}
	virtual bool UpdateProgressBar( float progress, const char *statusText ) OVERRIDE
	{
		m_flFraction = progress;
		if ( statusText )
			lstrcpynA( m_szStatus, statusText, sizeof( m_szStatus ) );
		if ( m_pClient && m_bLoading )
		{
			m_pClient->LoadingProgress( progress, m_szStatus );
			return true;
		}
		return s_pRealGameUI->UpdateProgressBar( progress, statusText );
	}
	virtual bool SetShowProgressText( bool show ) OVERRIDE { return s_pRealGameUI->SetShowProgressText( show ); }
	virtual void ShowNewGameDialog( int chapter ) OVERRIDE { s_pRealGameUI->ShowNewGameDialog( chapter ); }
	virtual void SessionNotification( const int notification, const int param ) OVERRIDE { s_pRealGameUI->SessionNotification( notification, param ); }
	virtual void SystemNotification( const int notification ) OVERRIDE { s_pRealGameUI->SystemNotification( notification ); }
	virtual void ShowMessageDialog( const uint nType, vgui::Panel *pOwner ) OVERRIDE { s_pRealGameUI->ShowMessageDialog( nType, pOwner ); }
	virtual void UpdatePlayerInfo( uint64 nPlayerId, const char *pName, int nTeam, byte cVoiceState, int nPlayersNeeded, bool bHost ) OVERRIDE
	{
		s_pRealGameUI->UpdatePlayerInfo( nPlayerId, pName, nTeam, cVoiceState, nPlayersNeeded, bHost );
	}
	virtual void SessionSearchResult( int searchIdx, void *pHostData, XSESSION_SEARCHRESULT *pResult, int ping ) OVERRIDE
	{
		s_pRealGameUI->SessionSearchResult( searchIdx, pHostData, pResult, ping );
	}
	virtual void OnCreditsFinished() OVERRIDE { s_pRealGameUI->OnCreditsFinished(); }
	virtual void SetLoadingBackgroundDialog( vgui::VPANEL panel ) OVERRIDE { s_pRealGameUI->SetLoadingBackgroundDialog( panel ); }
	virtual void BonusMapUnlock( const char *pchFileName, const char *pchMapName ) OVERRIDE { s_pRealGameUI->BonusMapUnlock( pchFileName, pchMapName ); }
	virtual void BonusMapComplete( const char *pchFileName, const char *pchMapName ) OVERRIDE { s_pRealGameUI->BonusMapComplete( pchFileName, pchMapName ); }
	virtual void BonusMapChallengeUpdate( const char *pchFileName, const char *pchMapName, const char *pchChallengeName, int iBest ) OVERRIDE
	{
		s_pRealGameUI->BonusMapChallengeUpdate( pchFileName, pchMapName, pchChallengeName, iBest );
	}
	virtual void BonusMapChallengeNames( char *pchFileName, char *pchMapName, char *pchChallengeName ) OVERRIDE
	{
		s_pRealGameUI->BonusMapChallengeNames( pchFileName, pchMapName, pchChallengeName );
	}
	virtual void BonusMapChallengeObjectives( int &iBronze, int &iSilver, int &iGold ) OVERRIDE { s_pRealGameUI->BonusMapChallengeObjectives( iBronze, iSilver, iGold ); }
	virtual void BonusMapDatabaseSave() OVERRIDE { s_pRealGameUI->BonusMapDatabaseSave(); }
	virtual int BonusMapNumAdvancedCompleted() OVERRIDE { return s_pRealGameUI->BonusMapNumAdvancedCompleted(); }
	virtual void BonusMapNumMedals( int piNumMedals[ 3 ] ) OVERRIDE { s_pRealGameUI->BonusMapNumMedals( piNumMedals ); }
	virtual void OnConnectToServer2( const char *game, int IP, int connectionPort, int queryPort ) OVERRIDE
	{
		s_pRealGameUI->OnConnectToServer2( game, IP, connectionPort, queryPort );
	}
	virtual bool ValidateStorageDevice( int *pStorageDeviceValidated ) OVERRIDE { return s_pRealGameUI->ValidateStorageDevice( pStorageDeviceValidated ); }
	virtual void SetProgressOnStart() OVERRIDE { s_pRealGameUI->SetProgressOnStart(); }
	virtual void OnDisconnectFromServer( uint8 eSteamLoginFailure ) OVERRIDE { s_pRealGameUI->OnDisconnectFromServer( eSteamLoginFailure ); }
	virtual void OnConfirmQuit() OVERRIDE { s_pRealGameUI->OnConfirmQuit(); }
	virtual bool IsMainMenuVisible() OVERRIDE { return s_pRealGameUI->IsMainMenuVisible(); }
	virtual void SetMainMenuOverride( vgui::VPANEL panel ) OVERRIDE { s_pRealGameUI->SetMainMenuOverride( panel ); }
	virtual void SendMainMenuCommand( const char *pszCommand ) OVERRIDE { s_pRealGameUI->SendMainMenuCommand( pszCommand ); }

private:
	IGameUIModClient *m_pClient;
	bool m_bLoading;
	float m_flFraction;
	char m_szStatus[ 256 ];
};

static CGameUIMod s_GameUIMod;

extern "C" __declspec( dllexport ) void *CreateInterface( const char *pName, int *pReturnCode )
{
	if ( !LoadRealGameUI() )
	{
		if ( pReturnCode )
			*pReturnCode = IFACE_FAILED;
		return NULL;
	}
	if ( !lstrcmpA( pName, GAMEUI_INTERFACE_VERSION ) )
	{
		if ( pReturnCode )
			*pReturnCode = IFACE_OK;
		return static_cast< IGameUI * >( &s_GameUIMod );
	}
	if ( !lstrcmpA( pName, GAMEUI_MOD_INTERFACE_VERSION ) )
	{
		if ( pReturnCode )
			*pReturnCode = IFACE_OK;
		return static_cast< IGameUIMod * >( &s_GameUIMod );
	}
	return s_pfnRealFactory( pName, pReturnCode );
}
