//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Original L4D2 melee info parsing, campaign catalog and precaching.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/weapons/terror_melee_weapon_parse.h"
#ifndef CLIENT_DLL
#include "l4d2/gamerules/terror_gamerules.h"
#endif
#include "activitylist.h"
#include "filesystem.h"
#include "networkstringtabledefs.h"
#include "tier1/KeyValues.h"
#include <stdio.h>
#ifdef CLIENT_DLL
#include "hud.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern INetworkStringTableContainer *networkstringtable;

namespace
{
	CMeleeWeaponInfoStore s_MeleeWeaponInfoStore;

	const char *const s_pszMeleeSoundKeys[MELEE_WEAPON_SOUND_COUNT] =
	{
		"empty", "single_shot", "single_shot_npc", "double_shot", "double_shot_npc",
		"burst", "reload", "reload_npc", "melee_miss", "melee_hit", "melee_hit_world",
		"special1", "special2", "special3", "taunt", "fastreload", "reload_dual",
		"reload_pump", "idle", "shoot_incendiary"
	};

	const char *const s_pszMeleeActivityKeys[MELEE_ACTIVITY_COUNT] =
	{
		"activity_idle", "activity_walk", "activity_run", "activity_crouchidle",
		"activity_crouchwalk", "activity_crouchwalk", // Original crouchrun typo.
		"activity_idleinjured", "activity_walkinjured", "activity_runinjured",
		"activity_idlecalm", "activity_walkcalm", "activity_runcalm", "activity_jump",
		"activity_deploy", "activity_shove"
	};


	MeleeAttackDirection_t ParseMeleeDirection( const char *pszDirection )
	{
		static const char *const directions[] = { "N", "S", "E", "W", "NE", "SE", "SW", "NW" };
		for ( int i = 0; i < ARRAYSIZE( directions ); ++i )
		{
			if ( !Q_stricmp( pszDirection, directions[i] ) )
				return (MeleeAttackDirection_t)i;
		}
		return MELEE_DIRECTION_INVALID;
	}

	// S:0x24C4D0/0x24C550: a partial conversion leaves the entire vector zero.
	Vector ParseMeleeVector( KeyValues *pData, const char *pszKey )
	{
		Vector value;
		if ( sscanf( pData->GetString( pszKey, "0 0 0" ), "%f %f %f", &value.x, &value.y, &value.z ) != 3 )
			value.Init();
		return value;
	}

#ifndef CLIENT_DLL
	void SplitMeleeNames( const char *pszNames, CUtlVector<CUtlString> &names )
	{
		// V_SplitString discards empty fields but does not trim whitespace.
		CUtlVector<char *> split;
		V_SplitString( pszNames, ";", split );
		FOR_EACH_VEC( split, i )
		{
			names.AddToTail( CUtlString( split[i] ) );
			delete[] split[i];
		}
	}
#endif
}

CMeleeWeaponInfoStore *MeleeWeaponInfoStore()
{
	return &s_MeleeWeaponInfoStore;
}

void MeleeAttackInfo_t::Parse( KeyValues *pData, bool bPrimary )
{
	// S:0x3D42C0 / C:0x2FECB0. Unknown activities stay ACT_INVALID.
	m_Activity = (Activity)ActivityList_IndexForName( pData->GetString( "activity", "ACT_INVALID" ) );
	m_PlayerActivity = (Activity)ActivityList_IndexForName( pData->GetString( "player_activity", "ACT_INVALID" ) );
	m_PlayerIdleActivity = (Activity)ActivityList_IndexForName( pData->GetString( "player_activity_idle", "ACT_INVALID" ) );
	m_flStartTime = pData->GetFloat( "starttime", 0.0f );
	m_flEndTime = pData->GetFloat( "endtime", 0.0f );
	m_flDuration = pData->GetFloat( "duration", 1.0f );
	m_StartDirection = ParseMeleeDirection( pData->GetString( "startdir", "E" ) );
	m_EndDirection = ParseMeleeDirection( pData->GetString( "enddir", "W" ) );
	m_bPrimaryAttack = bPrimary;
	m_vecForceDirection = ParseMeleeVector( pData, "force_dir" );
}

CMeleeWeaponInfo::CMeleeWeaponInfo( const char *pszName, KeyValues *pData )
{
	// S:0x3D6040 -> 0x3D5780 / C:0x300930 -> 0x300150.
	Q_strncpy( m_szName, pszName, sizeof( m_szName ) );
	m_flRefireDelay = pData->GetFloat( "refire_delay", 1.0f );
	Q_strncpy( m_szViewModel, pData->GetString( "viewmodel", "" ), sizeof( m_szViewModel ) );
	Q_strncpy( m_szPlayerModel, pData->GetString( "playermodel", "" ), sizeof( m_szPlayerModel ) );
	Q_strncpy( m_szAddonAttachment, pData->GetString( "addon_attachment", "melee" ), sizeof( m_szAddonAttachment ) );
	m_vecAddonOffset = ParseMeleeVector( pData, "addon_offset" );
	Vector angles = ParseMeleeVector( pData, "addon_angles" );
	m_angAddonAngles.Init( angles.x, angles.y, angles.z );
	Q_memset( m_szAnimPrefix, 0, sizeof( m_szAnimPrefix ) );
	Q_strncpy( m_szAnimPrefix, pData->GetString( "anim_prefix", "" ), sizeof( m_szAnimPrefix ) );
	m_flDamage = (float)pData->GetInt( "damage", 1 );
	m_iDamageFlags = pData->GetInt( "damage_flags", 0 ) | 0x00200000;
	m_iRumbleEffect = pData->GetInt( "rumble_effect", -1 );
	m_bDecapitates = pData->GetInt( "decapitates", 0 ) != 0;
	for ( int i = 0; i < MELEE_ACTIVITY_COUNT; ++i )
		Q_strncpy( m_szActivity[i], pData->GetString( s_pszMeleeActivityKeys[i], "" ), sizeof( m_szActivity[i] ) );

	// Reproduce the effective prefix bytes after the original oversized copy's
	// subsequent writes, without imposing the original x86 layout on native data.
	Q_memcpy( m_szAnimPrefix + 16, &m_flDamage, sizeof( m_flDamage ) );
	Q_memcpy( m_szAnimPrefix + 20, &m_iDamageFlags, sizeof( m_iDamageFlags ) );
	Q_memcpy( m_szAnimPrefix + 24, &m_iRumbleEffect, sizeof( m_iRumbleEffect ) );
	m_szAnimPrefix[28] = m_bDecapitates ? 1 : 0;
	Q_memcpy( m_szAnimPrefix + 29, m_szActivity[MELEE_ACTIVITY_IDLE], 51 );

	Q_memset( m_szShootSounds, 0, sizeof( m_szShootSounds ) );
	KeyValues *pSounds = pData->FindKey( "SoundData" );
	if ( pSounds )
	{
		for ( int i = 0; i < MELEE_WEAPON_SOUND_COUNT; ++i )
			Q_strncpy( m_szShootSounds[i], pSounds->GetString( s_pszMeleeSoundKeys[i], "" ), sizeof( m_szShootSounds[i] ) );
	}
	m_iPlayerAnimEvent = pData->GetInt( "player_anim_event", 33 );
	m_flWeaponIdleTime = pData->GetFloat( "weapon_idle_time", 2.0f );

	// The shipped strongattacks block is deliberately never parsed.
	KeyValues *pAttacks = pData->FindKey( "primaryattacks" );
	for ( KeyValues *pAttack = pAttacks ? pAttacks->GetFirstTrueSubKey() : NULL; pAttack; pAttack = pAttack->GetNextTrueSubKey() )
	{
		MeleeAttackInfo_t &attack = m_PrimaryAttacks[m_PrimaryAttacks.AddToTail()];
		attack.Parse( pAttack, true );
	}
	pAttacks = pData->FindKey( "secondaryattacks" );
	for ( KeyValues *pAttack = pAttacks ? pAttacks->GetFirstTrueSubKey() : NULL; pAttack; pAttack = pAttack->GetNextTrueSubKey() )
	{
		MeleeAttackInfo_t &attack = m_SecondaryAttacks[m_SecondaryAttacks.AddToTail()];
		attack.Parse( pAttack, false );
	}
#ifdef CLIENT_DLL
	Q_strncpy( m_szSpriteActive, pData->GetString( "sprite_active", "" ), sizeof( m_szSpriteActive ) );
	m_pSpriteActive = NULL;
#endif
}

void CMeleeWeaponInfo::Precache() const
{
#ifndef CLIENT_DLL
	// S:0x3D5D20: model order first person, third person, then all twenty sounds.
	if ( m_szViewModel[0] )
		CBaseEntity::PrecacheModel( m_szViewModel );
	if ( m_szPlayerModel[0] )
		CBaseEntity::PrecacheModel( m_szPlayerModel );
	for ( int i = 0; i < MELEE_WEAPON_SOUND_COUNT; ++i )
	{
		if ( m_szShootSounds[i][0] )
			CBaseEntity::PrecacheScriptSound( m_szShootSounds[i] );
	}
#else
	// C:0x300A70: sprite_active is an optional searchable HUD name. Otherwise
	// read TextureData from the same mounted script and register its active icon.
	m_pSpriteActive = gHUD.GetIcon( m_szSpriteActive );
	if ( !m_pSpriteActive )
	{
		char szFile[128];
		Q_snprintf( szFile, sizeof( szFile ), "scripts/melee/%s", m_szName );
		CUtlDict<CHudTexture *, int> textures;
		LoadHudTextures( textures, szFile, g_pGameRules->GetEncryptionKey() );
		int i = textures.Find( "sprite_active" );
		if ( i != textures.InvalidIndex() )
			m_pSpriteActive = gHUD.AddUnsearchableHudIconToList( *textures[i] );
		FOR_EACH_DICT( textures, j )
			delete textures[j];
	}
#endif
}

CMeleeWeaponInfoStore::CMeleeWeaponInfoStore() : CAutoGameSystem( "MeleeWeaponInfoStore" )
{
}

CMeleeWeaponInfoStore::~CMeleeWeaponInfoStore()
{
	Clear();
}

void CMeleeWeaponInfoStore::Clear()
{
	m_Infos.PurgeAndDeleteElements();
	m_Names.Purge();
}

void CMeleeWeaponInfoStore::Shutdown()
{
	Clear();
}

void CMeleeWeaponInfoStore::LevelInitPreEntity()
{
#ifndef CLIENT_DLL
	LoadScripts();
#endif
}

void CMeleeWeaponInfoStore::LevelInitPostEntity()
{
#ifdef CLIENT_DLL
	LoadScripts();
#endif
	FOR_EACH_VEC( m_Infos, i )
		m_Infos[i]->Precache();
}

const CMeleeWeaponInfo *CMeleeWeaponInfoStore::GetMeleeWeaponInfo( MeleeWeaponInfoHandle_t hInfo ) const
{
	return m_Infos.IsValidIndex( hInfo ) ? m_Infos[hInfo] : NULL;
}

MeleeWeaponInfoHandle_t CMeleeWeaponInfoStore::GetMeleeWeaponInfoIndex( const char *pszName ) const
{
	if ( !pszName || !pszName[0] )
		return INVALID_MELEE_WEAPON_INFO_HANDLE;
	FOR_EACH_VEC( m_Names, i )
	{
		if ( !Q_stricmp( m_Names[i].String(), pszName ) )
			return i;
	}
	return INVALID_MELEE_WEAPON_INFO_HANDLE;
}

MeleeWeaponInfoHandle_t CMeleeWeaponInfoStore::GetRandomMeleeWeaponInfo() const
{
	// S:0x3D6120: one global-stream draw, including the one-entry case.
	return m_Infos.Count() ? RandomInt( 0, m_Infos.Count() - 1 ) : INVALID_MELEE_WEAPON_INFO_HANDLE;
}

int CMeleeWeaponInfoStore::GetMeleeWeaponCount() const
{
	return m_Infos.Count();
}

bool CMeleeWeaponInfoStore::IsValidAttackInfo( const MeleeAttackInfo_t *pAttack ) const
{
	if ( !pAttack )
		return false;
	const uintp address = reinterpret_cast< uintp >( pAttack );
	FOR_EACH_VEC( m_Infos, i )
	{
		const CMeleeWeaponInfo *pInfo = m_Infos[i];
		const CUtlVector< MeleeAttackInfo_t > *attacks[] = { &pInfo->m_PrimaryAttacks, &pInfo->m_SecondaryAttacks };
		for ( int j = 0; j < ARRAYSIZE( attacks ); ++j )
		{
			const uintp offset = address - reinterpret_cast< uintp >( attacks[j]->Base() );
			if ( offset < static_cast< uintp >( attacks[j]->Count() ) * sizeof( MeleeAttackInfo_t ) &&
				offset % sizeof( MeleeAttackInfo_t ) == 0 )
				return true;
		}
	}
	return false;
}

void CMeleeWeaponInfoStore::AddScript( const char *pszName, KeyValues *pData )
{
	// S:0x3D6340: case-insensitive pooled name, first occurrence owns the index.
	if ( GetMeleeWeaponInfoIndex( pszName ) != INVALID_MELEE_WEAPON_INFO_HANDLE )
		return;
	m_Names.AddToTail( CUtlString( pszName ) );
	m_Infos.AddToTail( new CMeleeWeaponInfo( pszName, pData ) );
}

#ifndef CLIENT_DLL

bool CMeleeWeaponInfoStore::IsScriptAllowed( const char *pszName ) const
{
	// S:0x27BC90: only names with the five-character knife prefix are filtered.
	if ( Engine_IsCreatingXboxReslist() || Q_strnicmp( pszName, "knife", 5 ) != 0 )
		return true;
	KeyValues *pMission = TerrorGameRules()->GetMissionInfo();
	return !pMission || pMission->GetInt( "no_cs_weapons", 0 ) == 0;
}

void CMeleeWeaponInfoStore::LoadNamedScripts( const CUtlVector<CUtlString> &names )
{
	INetworkStringTable *pTable = networkstringtable->FindTable( "MeleeWeapons" );
	FOR_EACH_VEC( names, i )
	{
		const char *pszName = names[i].String();
		if ( !IsScriptAllowed( pszName ) )
			continue;
		char szFile[MAX_PATH];
		Q_snprintf( szFile, sizeof( szFile ), "scripts/melee/%s.txt", pszName );
		KeyValues *pData = new KeyValues( "meleeweapon" );
		if ( pData->LoadFromFile( filesystem, szFile, "GAME" ) )
		{
			AddScript( pszName, pData );
			pTable->AddString( true, pszName );
		}
		pData->deleteThis();
	}
}

void CMeleeWeaponInfoStore::LoadManifest( const CUtlVector<CUtlString> &names )
{
	INetworkStringTable *pTable = networkstringtable->FindTable( "MeleeWeapons" );
	KeyValues *pManifest = new KeyValues( "meleeweaponinfo" );
	pManifest->UsesEscapeSequences( true );
	if ( pManifest->LoadFromFile( filesystem, "scripts/melee/melee_manifest.txt", "GAME" ) )
	{
		for ( KeyValues *pFile = pManifest->GetFirstSubKey(); pFile; pFile = pFile->GetNextKey() )
		{
			if ( Q_stricmp( pFile->GetName(), "file" ) )
			{
				Error( "Expecting 'file', got %s\n", pFile->GetName() );
				continue;
			}
			char szName[512];
			V_FileBase( pFile->GetString(), szName, sizeof( szName ) );
			if ( names.Count() )
			{
				bool bFound = false;
				FOR_EACH_VEC( names, i )
				{
					if ( !Q_strcmp( szName, names[i].String() ) )
					{
						bFound = true;
						break;
					}
				}
				if ( !bFound )
					continue;
			}
			if ( !IsScriptAllowed( szName ) )
				continue;
			// Original bug: publish a manifest name even if its file cannot load.
			pTable->AddString( true, szName );
			KeyValues *pData = new KeyValues( "meleeweapon" );
			if ( pData->LoadFromFile( filesystem, pFile->GetString(), "GAME" ) )
				AddScript( szName, pData );
			pData->deleteThis();
		}
	}
	pManifest->deleteThis();
}
#endif

void CMeleeWeaponInfoStore::LoadScripts()
{
	Clear();
	INetworkStringTable *pTable = networkstringtable->FindTable( "MeleeWeapons" );
	if ( !pTable )
	{
		Error( "Melee weapons string table not initialized\n" );
		return;
	}
#ifdef CLIENT_DLL
	// C:0x300D00: the server catalog, not the client's mission/manifest, is law.
	const int tableCount = pTable->GetNumStrings();
	for ( int i = 0; i < tableCount; ++i )
	{
		const char *pszName = pTable->GetString( i );
		if ( !pszName )
			continue;
		char szFile[MAX_PATH];
		Q_snprintf( szFile, sizeof( szFile ), "scripts/melee/%s.txt", pszName );
		KeyValues *pData = new KeyValues( "meleeweapon" );
		if ( pData->LoadFromFile( filesystem, szFile, "GAME" ) )
			AddScript( pszName, pData );
		pData->deleteThis();
	}
#else
	// S:0x3D6700: preserve the campaign order, commentary append and mutation5
	// replacement. Failed named files do not fall back to the full manifest.
	CUtlVector<CUtlString> names;
	KeyValues *pMission = TerrorGameRules()->GetMissionInfo();
	const char *pszNames = pMission ? pMission->GetString( "meleeweapons", NULL ) : NULL;
	if ( pszNames )
		SplitMeleeNames( pszNames, names );
	if ( engine->IsInCommentaryMode() )
		names.AddToTail( CUtlString( "fireaxe" ) );
	ConVarRef mode( "mp_gamemode" );
	// S:0x25BE20 strips only a case-insensitive leading "team"; unlike
	// 0x25BE60, this getter does not resolve the mode description's base.
	const char *pszMode = mode.GetString();
	if ( !Q_strnicmp( pszMode, "team", 4 ) )
		pszMode += 4;
	if ( !Q_stricmp( pszMode, "mutation5" ) )
	{
		names.RemoveAll();
		names.AddToTail( CUtlString( "katana" ) );
	}
	if ( names.Count() )
		LoadNamedScripts( names );
	else
		LoadManifest( names );
#endif
}

void CMeleeWeaponInfoStore::Dump() const
{
#ifdef CLIENT_DLL
	const char *pszSide = "Client";
#else
	const char *pszSide = "Server";
#endif
	FOR_EACH_VEC( m_Infos, i )
		Msg( "(%s)%d: %s (hash=%d) (%p)\n", pszSide, i, m_Infos[i]->m_szName, i, m_Infos[i] );
}

#ifdef CLIENT_DLL

#if defined( CLIENT_DLL )
CON_COMMAND_F( melee_dump_weapons, "Spew the melee weapon infos that are currently loaded", 0x00004008 )
#else
CON_COMMAND_F( melee_dump_weapons, "Spew the melee weapon infos that are currently loaded", FCVAR_CHEAT )
#endif
{
	MeleeWeaponInfoStore()->Dump();
	engine->ClientCmd( "melee_dump_weapons_server" );
}


#if defined( CLIENT_DLL )
CON_COMMAND_F( melee_reload_info, "Reloads Melee weapon scripts to the MeleeWeaponInfoStore", 0x00004008 )
#else
CON_COMMAND_F( melee_reload_info, "Reloads Melee weapon scripts to the MeleeWeaponInfoStore", FCVAR_CHEAT )
#endif
{
	MeleeWeaponInfoStore()->LoadScripts();
	engine->ClientCmd( "melee_reload_info_server" );
}
#else

#if defined( GAME_DLL )
CON_COMMAND_F( melee_dump_weapons_server, "Spew the melee weapon infos that are currently loaded", 0x00004004 )
#else
CON_COMMAND_F( melee_dump_weapons_server, "Spew the melee weapon infos that are currently loaded", FCVAR_CHEAT )
#endif
{
	MeleeWeaponInfoStore()->Dump();
}


#if defined( GAME_DLL )
CON_COMMAND_F( melee_reload_info_server, "Reloads Melee weapon scripts to the MeleeWeaponInfoStore", 0x00004004 )
#else
CON_COMMAND_F( melee_reload_info_server, "Reloads Melee weapon scripts to the MeleeWeaponInfoStore", FCVAR_CHEAT )
#endif
{
	MeleeWeaponInfoStore()->LoadScripts();
}
#endif
