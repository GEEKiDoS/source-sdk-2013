//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/gamerules/terror_mission_info.h"
#include "l4d2/content/terror_provider_files.h"
#include "filesystem.h"
#include "tier1/fmtstr.h"
#include "tier0/memdbgon.h"

void TerrorMergeMissionKeyValues( KeyValues *storage, KeyValues *update, bool onlyExisting )
{
	// M19970 modes1/3: recurse existing subtrees. UPDATE adds absent subtrees;
	// BORROW does not. Both replace matching scalar nodes by copy-at-tail.
	if ( !storage || !update ) return;
	for ( KeyValues *key = update->GetFirstTrueSubKey(); key; key = key->GetNextTrueSubKey() )
	{
		KeyValues *old = storage->FindKey( key->GetName() );
		if ( old ) TerrorMergeMissionKeyValues( old, key, onlyExisting );
		else if ( !onlyExisting ) storage->AddSubKey( key->MakeCopy() );
	}
	for ( KeyValues *value = update->GetFirstValue(); value; value = value->GetNextValue() )
	{
		KeyValues *old = storage->FindKey( value->GetName() );
		if ( !old && onlyExisting ) continue;
		if ( old )
		{
			storage->RemoveSubKey( old );
			old->deleteThis();
		}
		storage->AddSubKey( value->MakeCopy() );
	}
}

void CTerrorModeDescription::ShutdownMissions()
{
	if ( m_pMissions ) m_pMissions->deleteThis();
	m_pMissions = NULL;
	m_MissionFiles.Purge();
}
void CTerrorModeDescription::InitMissions()
{
	// M2C80: use the existing mode dictionary; do not parse a second set of modes.
	DevMsg( "Loading Mission Data\n" );
	ShutdownMissions();
	m_pMissions = new KeyValues( "Missions" );
	for ( int campaign = 1; campaign <= 14; ++campaign )
	{
		char path[MAX_PATH];
		Q_snprintf( path, sizeof( path ), "missions/campaign%d.txt", campaign );
		LoadMissionFile( path, true, 0 );
	}
	LoadMissionFile( "missions/credits.txt", true, 0 );
	CUtlVector< CUtlString > files;
	TerrorFindProviderFiles( "missions/*.txt", files );
	FOR_EACH_VEC( files, i )
	{
		char absolute[MAX_PATH], basename[MAX_PATH], relative[MAX_PATH];
		Q_strncpy( absolute, files[i].String(), sizeof( absolute ) );
		V_FixSlashes( absolute, '\\' );
		uint64 workshopID = 0;
		const char *workshop = V_stristr( absolute, "addons\\workshop\\" );
		if ( workshop )
			sscanf( workshop + Q_strlen( "addons\\workshop\\" ), "%llu.vpk", &workshopID );
		V_FileBase( absolute, basename, sizeof( basename ) );
		Q_snprintf( relative, sizeof( relative ), "missions/%s.txt", basename );
		// Native M2C80 intentionally loads the effective RELATIVE file here,
		// not each absolute payload. The attempted-file registry folds repeats.
		LoadMissionFile( relative, false, workshopID );
	}
}
void CTerrorModeDescription::LoadMissionFile( const char *path, bool builtIn, uint64 workshopID )
{
	if ( m_MissionFiles.Find( path ) != m_MissionFiles.InvalidIndex() ) return;
	unsigned short fileIndex = m_MissionFiles.Insert( path, NULL );
	KeyValues *mission = new KeyValues( "mission" );
	if ( !mission->LoadFromFile( filesystem, path, "GAME" ) )
	{
		Warning( "MissionManager:  Mission file \"%s\" is malformed, failed to parse.\n", path );
		mission->deleteThis();
		return;
	}
	const char *name = mission->GetString( "name", NULL );
	const char *version = mission->GetString( "version", NULL );
	if ( !name || !version )
	{
		Warning( "MissionManager:  Mission file \"%s\" is missing name and version\n", path );
		mission->deleteThis();
		return;
	}
	for ( const char *c = name; *c; ++c )
	{
		if ( !( ( *c >= 'a' && *c <= 'z' ) || ( *c >= 'A' && *c <= 'Z' ) || ( *c >= '0' && *c <= '9' ) ) )
		{
			Warning( "MissionManager:  Only alphanumeric characters allowed in mission name: \"%s\" in \"%s\"\n", name, path );
			mission->deleteThis();
			return;
		}
	}
	for ( const char *c = version; *c; ++c )
	{
		if ( *c < '0' || *c > '9' )
		{
			Warning( "MissionManager:  Only numeric characters allowed in mission version: \"%s\" in \"%s\"\n", version, path );
			mission->deleteThis();
			return;
		}
	}
	KeyValues *missionModes = mission->FindKey( "modes" );
	if ( !missionModes )
	{
		Warning( "MissionManager:  Mission file \"%s\" is missing data for any game modes\n", path );
		mission->deleteThis();
		return;
	}
	KeyValues *duplicate = m_pMissions->FindKey( name );
	if ( duplicate )
	{
		Warning( "MissionManager:  Duplicate mission \"%s\" in file \"%s\", already loaded from \"%s\".\n", name, path, duplicate->GetString( "CfgFile", "" ) );
		mission->deleteThis();
		return;
	}
	DevMsg( "\tMission %s ver %s loading...\n", name, version );
	if ( !*mission->GetString( "DisplayTitle", "" ) ) mission->SetString( "DisplayTitle", name );
	mission->SetName( name );
	mission->SetInt( "BuiltIn", builtIn );
	mission->SetInt( "addon", !builtIn );
	mission->SetString( "CfgFile", path );
	mission->SetString( "CfgTag", CFmtStr( "%s_%s", name, version ) );
	mission->SetUint64( "workshopid", workshopID );
	mission->SetUint64( "dlcmask", 0 );
	mission->SetInt( "dlcrequired", 0 );
	int validModes = 0;
	for ( KeyValues *mode = m_pModes->GetFirstTrueSubKey(); mode; mode = mode->GetNextTrueSubKey() )
	{
		KeyValues *chapters = missionModes->FindKey( mode->GetName() );
		if ( !chapters ) continue;
		int count = 0;
		bool valid = true;
		for ( int number = 1; ; ++number )
		{
			KeyValues *chapter = chapters->FindKey( CFmtStr( "%d", number ) );
			if ( !chapter ) break;
			if ( !*chapter->GetString( "map", "" ) )
			{
				Warning( "MissionManager:  Mission file \"%s\" has invalid map specified for modes/%s/%s\n", path, mode->GetName(), chapter->GetName() );
				valid = false;
				break;
			}
			if ( !*chapter->GetString( "DisplayName", "" ) ) chapter->SetString( "DisplayName", CFmtStr( "%s-%s", name, chapter->GetName() ) );
			if ( !*chapter->GetString( "image", "" ) ) chapter->SetString( "image", "maps/unknown" );
			chapter->SetInt( "chapter", number );
			++count;
		}
		if ( !valid || !count )
		{
			missionModes->RemoveSubKey( chapters );
			chapters->deleteThis();
			Warning( "MissionManager:  Mission file \"%s\" has invalid settings for game mode %s\n", path, mode->GetName() );
			continue;
		}
		if ( Q_stricmp( name, "credits" ) )
			mode->SetInt( "missions", mode->GetInt( "missions", 0 ) + ( mode->GetInt( "singlechapter", 0 ) ? count : 1 ) );
		chapters->SetInt( "chapters", count );
		DevMsg( "\t\tloaded %d %s chapters.\n", count, mode->GetName() );
		++validModes;
	}
	if ( !validModes )
	{
		Warning( "MissionManager:  Mission file \"%s\" does not have valid data for any supported game mode\n", path );
		mission->deleteThis();
		return;
	}
	m_MissionFiles[fileIndex] = mission;
	m_pMissions->AddSubKey( mission );
	for ( KeyValues *mode = m_pModes->GetFirstTrueSubKey(); mode; mode = mode->GetNextTrueSubKey() )
	{
		if ( missionModes->FindKey( mode->GetName() ) )
			mode->SetPtr( CFmtStr( "missions/%s", name ), mission );
	}
	DevMsg( "\tMission %s ver %s loaded %d game modes.\n", name, version, validModes );
}

void CTerrorModeDescription::InheritMissionData( const char *source, const char *destination )
{
	KeyValues *base = FindMode( source );
	KeyValues *mode = FindMode( destination );
	if ( !mode && base )
	{
		mode = base->MakeCopy();
		mode->SetName( destination );
		m_pModes->AddSubKey( mode );
	}
	if ( mode && base )
	{
		TerrorMergeMissionKeyValues( mode->FindKey( "missions", true ), base->FindKey( "missions" ), false );
		mode->SetInt( "missions", base->GetInt( "missions", 0 ) );
	}
	if ( mode ) mode->SetInt( "team", StringAfterPrefix( destination, "team" ) != NULL );
	for ( KeyValues *mission = m_pMissions->GetFirstTrueSubKey(); mission; mission = mission->GetNextTrueSubKey() )
	{
		KeyValues *modes = mission->FindKey( "Modes" );
		if ( !modes || modes->FindKey( destination ) ) continue;
		KeyValues *chapters = modes->FindKey( source );
		if ( chapters )
		{
			KeyValues *copy = chapters->MakeCopy();
			copy->SetName( destination );
			modes->AddSubKey( copy );
		}
	}
}
void CTerrorModeDescription::InheritAllMissionData()
{
	InheritMissionData( "coop", "realism" );
	for ( KeyValues *mode = m_pModes->GetFirstTrueSubKey(); mode; mode = mode->GetNextTrueSubKey() )
	{
		const char *name = mode->GetName();
		const char *base = mode->GetString( "base", name );
		if ( Q_stricmp( name, base ) ) InheritMissionData( base, name );
		if ( mode->GetInt( "maxplayers", 0 ) > 4 && !StringAfterPrefix( name, "team" ) )
			InheritMissionData( name, CFmtStr( "team%s", name ) );
	}
	KeyValues *config = new KeyValues( "AddonConfig" );
	if ( config->LoadFromFile( filesystem, "cfg/addonconfig.cfg", NULL ) )
	{
		for ( KeyValues *mode = m_pModes->GetFirstTrueSubKey(); mode; mode = mode->GetNextTrueSubKey() )
		{
			const char *name = mode->GetName();
			const char *base = mode->GetString( "base", name );
			// Native M314F formats a team-prefixed base then never uses it.
			// Actual reads are default -> RAW base -> selected name.
			bool vanilla = config->GetInt( "default", 0 ) != 0;
			vanilla = config->GetInt( base, vanilla ) != 0;
			vanilla = config->GetInt( name, vanilla ) != 0;
			mode->SetInt( "vanilla", vanilla );
		}
	}
	config->deleteThis();
	DevMsg( "Loading Mission Data Finished\n" );
}
KeyValues *CTerrorModeDescription::GetChapterInfo( KeyValues *settings, KeyValues **mission ) const
{
	// M1710: borrowed dictionary nodes, never children of caller-owned settings.
	if ( !m_pMissions ) return NULL;
	const char *modeName = settings->GetString( "Game/mode", NULL );
	const char *campaign = settings->GetString( "Game/campaign", NULL );
	int number = settings->GetInt( "Game/chapter", 0 );
	if ( !modeName || !*modeName || !campaign || !*campaign || number <= 0 ) return NULL;
	KeyValues *mode = FindMode( modeName );
	KeyValues *owner = mode ? static_cast< KeyValues * >( mode->GetPtr( CFmtStr( "missions/%s", campaign ) ) ) : NULL;
	if ( !owner ) return NULL;
	KeyValues *chapters = owner->FindKey( CFmtStr( "Modes/%s", modeName ) );
	if ( !chapters || number > chapters->GetInt( "chapters", 0 ) ) return NULL;
	KeyValues *chapter = chapters->FindKey( CFmtStr( "%d", number ) );
	if ( chapter && mission ) *mission = owner;
	return chapter;
}
KeyValues *CTerrorModeDescription::GetChapterInfoByBspName( KeyValues *settings, const char *mapName, KeyValues **mission ) const
{
	// M1870: first mode-mission reference and first numbered chapter wins.
	if ( !m_pMissions || !mapName || !*mapName ) return NULL;
	const char *modeName = settings->GetString( "Game/mode", NULL );
	if ( !modeName || !*modeName ) return NULL;
	KeyValues *mode = FindMode( modeName );
	KeyValues *references = mode ? mode->FindKey( "missions" ) : NULL;
	for ( KeyValues *entry = references ? references->GetFirstValue() : NULL; entry; entry = entry->GetNextValue() )
	{
		KeyValues *owner = static_cast< KeyValues * >( entry->GetPtr() );
		KeyValues *chapters = owner ? owner->FindKey( CFmtStr( "Modes/%s", modeName ) ) : NULL;
		if ( !chapters ) continue;
		for ( int number = 1; number <= chapters->GetInt( "chapters", 0 ); ++number )
		{
			KeyValues *chapter = chapters->FindKey( CFmtStr( "%d", number ) );
			if ( !chapter ) break;
			if ( !Q_stricmp( chapter->GetString( "map", "" ), mapName ) )
			{
				if ( mission ) *mission = owner;
				return chapter;
			}
		}
	}
	return NULL;
}
