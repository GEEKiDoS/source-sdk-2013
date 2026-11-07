//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "igamesystem.h"
#include "filesystem.h"
#include "l4d2/content/terror_provider_files.h"
#include "l4d2/content/terror_addon_commands.h"
#include "tier0/memdbgon.h"

ConVar mp_gamemode( "mp_gamemode", "coop", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY
#endif
, "Current game mode, acceptable values are coop, realism, versus, survival, scavenge and holdout; changed using map command, eg: map mapname versus" );
#ifndef CLIENT_DLL
ConVar sv_gametypes( "sv_gametypes", "", 
#if defined( GAME_DLL )
	0x00080004
#else
	0x00080000
#endif
, "Allowable game types, usually set on server launch command line." ); // S599690.
#endif
CTerrorModeDescription g_TerrorModes;

class CTerrorGameModeSystem : public CAutoGameSystem, public ITerrorAddonCommandPeer
{
public:
	CTerrorGameModeSystem() : CAutoGameSystem( "TerrorGameModes" )
	{
		m_pCommands[0] = m_pCommands[1] = NULL;
	}
	virtual bool Init() OVERRIDE
	{
#ifdef CLIENT_DLL
		if ( !g_TerrorModes.Init() ) return false;
#else
		// DLLInit loaded metadata before genuine command-line cvar delivery.
		if ( !g_TerrorModes.GetModeDictionary() ) return false;
#endif
		// Both DLLs have completed ConVar_Register before InitAllSystems.
		CTerrorAddonCommand::AttachModule( this, COMMAND_MODULE, false );
		CTerrorAddonCommand::AttachModule( this, COMMAND_MODULE, true );
		return true;
	}
	virtual void Shutdown() OVERRIDE
	{
		for ( int command = 1; command >= 0; --command )
			if ( m_pCommands[command] ) m_pCommands[command]->DetachPeer( this, COMMAND_MODULE );
		g_TerrorModes.Shutdown();
	}
	virtual void ReloadMissionData() OVERRIDE { g_TerrorModes.Reload(); }
	virtual void RemountAddons() OVERRIDE
	{
#ifdef CLIENT_DLL
		// E17D810 permits signon states 0/1 only; IsConnected starts at 2.
		if ( engine->IsConnected() ) return;
#endif
		TerrorMountAddons();
	}
	virtual void CreateAddonCommand( bool updatePaths ) OVERRIDE
	{
		CTerrorAddonCommand::Create( this, COMMAND_MODULE, updatePaths );
	}
	virtual void SetAddonCommand( bool updatePaths, CTerrorAddonCommand *command ) OVERRIDE
	{
		m_pCommands[updatePaths ? 1 : 0] = command;
	}
private:
#ifdef CLIENT_DLL
	enum { COMMAND_MODULE = 1 };
#else
	enum { COMMAND_MODULE = 0 };
#endif
	CTerrorAddonCommand *m_pCommands[2];
};
static CTerrorGameModeSystem s_TerrorGameModeSystem;

CTerrorModeDescription::CTerrorModeDescription() : m_pModes( NULL ), m_pMissions( NULL ), m_bChangingMode( false ), m_bCallbackInstalled( false ) {}
CTerrorModeDescription::~CTerrorModeDescription() { Shutdown(); }
void CTerrorModeDescription::Shutdown()
{
#ifndef CLIENT_DLL
	if ( m_bCallbackInstalled )
	{
		mp_gamemode.InstallChangeCallback( NULL );
		m_bCallbackInstalled = false;
	}
#endif
	ShutdownMissions();
	if ( m_pModes )
		m_pModes->deleteThis();
	m_pModes = NULL;
}

bool CTerrorModeDescription::Init()
{
	TerrorMountAddons();
	Shutdown();
	if ( !Reload() ) return false;
#ifndef CLIENT_DLL
	// Native S0EAF60 delivers command-line values during InitializeCvars, after
	// metadata is available. Do not invent an earlier current-value delivery.
	m_bChangingMode = true;
	mp_gamemode.InstallChangeCallback( TerrorGameModeChanged );
	m_bChangingMode = false;
	m_bCallbackInstalled = true;
#endif
	return true;
}

bool CTerrorModeDescription::Reload()
{
	// M32E0 -> M2C80 rebuilds data only. Keep the real mode-change callback and
	// current convar values; mission_reload neither remounts nor reapplies them.
	ShutdownMissions();
	if ( m_pModes ) m_pModes->deleteThis();
	m_pModes = new KeyValues( "GameModes" );
	if ( !m_pModes->LoadFromFile( filesystem, "scripts/gamemodes.txt", "GAME" ) )
	{
		m_pModes->deleteThis();
		m_pModes = NULL;
		return false;
	}
	// Matchmaking_ds:14F0 tags built-in mutations without inheriting their
	// convar or player-controlled-zombie properties.
	for ( KeyValues *mode = m_pModes->GetFirstTrueSubKey(); mode; mode = mode->GetNextTrueSubKey() )
	{
		mode->SetUint64( "dlcmask", 0 );
		const char *name = mode->GetName();
		const char *prefix = StringAfterPrefix( name, "team" ) ? "team" : "";
		char baseName[64];
		Q_snprintf( baseName, sizeof( baseName ), "%s%s", prefix, mode->GetString( "base", name ) );
		bool mutation = Q_stricmp( name, baseName ) != 0;
		if ( mutation )
			mode->SetInt( "mutation", 1 );
		bool addon = mutation;
		const char *number = StringAfterPrefix( name, "mutation" );
		if ( number && (unsigned)( atoi( number ) - 1 ) <= 19 )
			addon = false;
		number = StringAfterPrefix( name, "community" );
		if ( number && (unsigned)( atoi( number ) - 1 ) <= 5 )
			addon = false;
		mode->SetInt( "addon", addon );
	}
	// Mode files are independent named roots; do not synthesize their metadata
	// from their filenames, or inherit playercontrolledzombies from a base name.
	CUtlVector< CUtlString > modeFiles;
	TerrorFindProviderFiles( "modes/*.txt", modeFiles );
	FOR_EACH_VEC( modeFiles, i )
	{
		const char *path = modeFiles[i].String();
		KeyValues *mode = new KeyValues( "Mode" );
		if ( !mode->LoadFromFile( filesystem, path, NULL ) || !mode->GetFirstSubKey() )
		{
			Warning( "CMatchExtL4D:  Unable to load game mode from \"%s\".\n", path );
			mode->deleteThis();
			continue;
		}
		if ( V_stristr( path, "workshop" ) )
		{
			char directory[MAX_PATH], workshopName[MAX_PATH];
			Q_strncpy( directory, path, sizeof( directory ) );
			V_StripFilename( directory );
			V_StripFilename( directory );
			V_StripTrailingSlash( directory );
			V_FileBase( directory, workshopName, sizeof( workshopName ) );
			uint64 workshopID = 0;
			sscanf( workshopName, "%llu", &workshopID );
			mode->SetUint64( "workshopid", workshopID );
		}
		char name[MAX_PATH];
		V_FileBase( path, name, sizeof( name ) );
		mode->SetString( "CfgFile", name );
		mode->SetInt( "mutation", 1 );
		mode->SetInt( "addon", 1 );
		if ( mode->GetUint64( "dlcmask", 0 ) )
			mode->SetUint64( "dlcmask", 0 );
		// Matchmaking_ds:2930 does not override an existing built-in entry.
		if ( m_pModes->FindKey( name ) )
			mode->deleteThis();
		else
			m_pModes->AddSubKey( mode );
	}
	InitMissions();
	// Matchmaking_ds:2C80/1320 creates missing team variants by copying the
	// descriptor, while leaving existing mutation descriptors independent.
	for ( KeyValues *mode = m_pModes->GetFirstTrueSubKey(); mode; mode = mode->GetNextTrueSubKey() )
	{
		if ( mode->GetInt( "maxplayers", 0 ) > 4 && !StringAfterPrefix( mode->GetName(), "team" ) )
		{
			char teamName[64];
			Q_snprintf( teamName, sizeof( teamName ), "team%s", mode->GetName() );
			KeyValues *team = m_pModes->FindKey( teamName );
			if ( !team )
			{
				team = mode->MakeCopy();
				team->SetName( teamName );
				m_pModes->AddSubKey( team );
			}
			team->SetInt( "team", 1 );
			team->SetInt( "x360ctx", mode->GetInt( "x360ctx", 0 ) + 1 );
		}
		mode->SetInt( "team", StringAfterPrefix( mode->GetName(), "team" ) != NULL );
	}
	InheritAllMissionData();
	return true;
}

KeyValues *CTerrorModeDescription::FindMode( const char *name ) const
{
	return m_pModes ? m_pModes->FindKey( name ) : NULL;
}
KeyValues *CTerrorModeDescription::GetCurrentMode() const { return FindMode( mp_gamemode.GetString() ); }
const char *CTerrorModeDescription::GetName() const
{
	const char *name = mp_gamemode.GetString();
	const char *team = StringAfterPrefix( name, "team" );
	return team ? team : name;
}
const char *CTerrorModeDescription::GetBaseName() const
{
	KeyValues *mode = GetCurrentMode();
	return mode ? mode->GetString( "base", "" ) : mp_gamemode.GetString();
}
bool CTerrorModeDescription::IsBaseMode( const char *base ) const
{
	KeyValues *mode = GetCurrentMode();
	return mode && !Q_stricmp( mode->GetString( "base", "" ), base );
}
bool CTerrorModeDescription::IsPlayerControlledZombies() const
{
	KeyValues *mode = GetCurrentMode();
	return mode && mode->GetInt( "playercontrolledzombies", 0 ) > 0;
}
bool CTerrorModeDescription::HasDifficulty() const
{
	KeyValues *mode = GetCurrentMode();
	if ( !mode ) return false;
	KeyValues *base = FindMode( mode->GetString( "base", mp_gamemode.GetString() ) );
	return base && mode->GetInt( "hasdifficulty", base->GetInt( "hasdifficulty", 0 ) != 0 ) != 0;
}

void CTerrorModeDescription::RevertConVars( KeyValues *mode, const char *name )
{
	KeyValues *vars = mode ? mode->FindKey( "convar" ) : NULL;
	for ( KeyValues *value = vars ? vars->GetFirstValue() : NULL; value; value = value->GetNextValue() )
	{
		const char *varName = value->GetName();
		const char *consoleName = StringAfterPrefix( varName, "360_" );
		if ( consoleName )
			varName = consoleName;
		ConVarRef var( varName );
		if ( var.IsValid() )
		{
			var.SetValue( var.GetDefault() );
			DevMsg( 2, "%s reverting %s\n", name, varName );
		}
	}
}
void CTerrorModeDescription::ApplyConVars( KeyValues *mode, const char *name )
{
	KeyValues *vars = mode ? mode->FindKey( "convar" ) : NULL;
	for ( KeyValues *value = vars ? vars->GetFirstValue() : NULL; value; value = value->GetNextValue() )
	{
		const char *varName = value->GetName();
		if ( StringAfterPrefix( varName, "360_" ) )
			continue; // Pinned Windows PC path; no Xbox session capability is fabricated.
		// Keep native S2D8DB0's early missing-registration diagnostic.
		ConVarRef var( varName );
		if ( var.IsValid() )
		{
			var.SetValue( value->GetString() );
			DevMsg( 2, "%s setting %s to %s\n", name, varName, value->GetString() );
		}
	}
}
void CTerrorModeDescription::OnModeChanged( IConVar *var, const char *oldValue )
{
	if ( m_bChangingMode || !m_pModes )
		return;
	m_bChangingMode = true;
	ConVar *modeVar = static_cast<ConVar *>( var );
	char newName[32];
	const char *newValue = modeVar->GetString();
	KeyValues *selected = FindMode( newValue );
	if ( !selected )
	{
		Msg( "Invalid mp_gamemode value '%s', keeping old value '%s'.\n", newValue, oldValue );
		modeVar->SetValue( oldValue );
		m_bChangingMode = false;
		return;
	}
	if ( Q_strlen( newValue ) >= sizeof( newName ) )
		Error( "Game mode name too long for buffer size\n" );
	Q_strncpy( newName, newValue, sizeof( newName ) );
	Q_strlower( newName );
	modeVar->SetValue( newName );
	if ( Q_stricmp( newName, oldValue ) )
	{
		KeyValues *oldMode = FindMode( oldValue );
		if ( oldMode )
		{
			RevertConVars( oldMode, oldValue );
			const char *base = oldMode->GetString( "base", NULL );
			if ( base && Q_stricmp( base, oldValue ) )
				RevertConVars( FindMode( base ), base );
		}
	}
	selected = FindMode( modeVar->GetString() );
	if ( selected )
	{
		const char *base = selected->GetString( "base", NULL );
		if ( base && Q_stricmp( base, modeVar->GetString() ) )
			ApplyConVars( FindMode( base ), base );
		ApplyConVars( selected, newName );
	}
	m_bChangingMode = false;
}
void TerrorGameModeChanged( IConVar *var, const char *oldValue, float )
{
	g_TerrorModes.OnModeChanged( var, oldValue );
}
const char *CTerrorGameRules::GetActiveGameMode() const { return g_TerrorModes.GetName(); }
bool CTerrorGameRules::IsCoopMode() const { return g_TerrorModes.IsBaseMode( "coop" ); }
bool CTerrorGameRules::IsRealismMode() const { return g_TerrorModes.IsBaseMode( "realism" ); }
bool CTerrorGameRules::IsVersusMode() const { return g_TerrorModes.IsBaseMode( "versus" ); }
bool CTerrorGameRules::IsSurvivalMode() const { return g_TerrorModes.IsBaseMode( "survival" ); }
bool CTerrorGameRules::IsScavengeMode() const { return g_TerrorModes.IsBaseMode( "scavenge" ); }
bool CTerrorGameRules::IsPlayerControlledZombies() const { return g_TerrorModes.IsPlayerControlledZombies(); }
