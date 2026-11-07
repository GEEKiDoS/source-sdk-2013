//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Original L4D2 weapon-script parsing and additional asset precaching.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/terror_shareddefs.h"
#include "particle_parse.h"
#include "tier1/KeyValues.h"
#include <stdio.h>
#include <stdlib.h>
#include <emmintrin.h>

#if defined( L4D2 )
#include "l4d2/effects/terror_fx_shared.h"
#endif
// memdbgon must be the last include file in a .cpp file!!!
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "weapon_parse.h"
#include "gamerules.h"
#endif
#include "tier0/memdbgon.h"

static const struct
{
	const char *pszName;
	TerrorWeaponType_t type;
} weaponTypes[] =
{
	{ "pistol", TERROR_WEAPONTYPE_PISTOL },
	{ "smg", TERROR_WEAPONTYPE_SMG },
	{ "rifle", TERROR_WEAPONTYPE_RIFLE },
	{ "shotgun", TERROR_WEAPONTYPE_SHOTGUN },
	{ "sniper_rifle", TERROR_WEAPONTYPE_SNIPER_RIFLE },
	{ "machinegun", TERROR_WEAPONTYPE_MACHINEGUN },
	{ "grenade", TERROR_WEAPONTYPE_GRENADE },
	{ "healing", TERROR_WEAPONTYPE_HEALING },
	{ "reviving", TERROR_WEAPONTYPE_REVIVING },
	{ "melee", TERROR_WEAPONTYPE_MELEE },
	{ "chainsaw", TERROR_WEAPONTYPE_CHAINSAW },
	{ "grenade_launcher", TERROR_WEAPONTYPE_GRENADE_LAUNCHER },
	{ "carried_prop", TERROR_WEAPONTYPE_CARRIED_PROP },
	{ "claw", TERROR_WEAPONTYPE_CLAW },
	{ "upgradepack", TERROR_WEAPONTYPE_UPGRADEPACK }
};

TerrorWeaponType_t TerrorWeaponTypeFromName( const char *pszName )
{
	for ( int i = 0; i < ARRAYSIZE( weaponTypes ); ++i )
	{
		if ( !Q_stricmp( pszName, weaponTypes[i].pszName ) )
			return weaponTypes[i].type;
	}
	return TERROR_WEAPONTYPE_UNKNOWN;
}

const char *WeaponTypeToString( TerrorWeaponType_t type )
{
	for ( int i = 0; i < ARRAYSIZE( weaponTypes ); ++i )
	{
		if ( type == weaponTypes[i].type )
			return weaponTypes[i].pszName;
	}
	return "__UNKNOWN__";
}

// The weapon database owns loading and caching. Its L4D2 txt-first loader
// uses GAME and escape decoding for both load paths; do not reparse here.
FileWeaponInfo_t *CreateWeaponInfo()
{
	// Original server RVA 0x3F2C70, constructor continuation 0x3F2160.
	return new CTerrorWeaponInfo;
}

CCSWeaponInfo::CCSWeaponInfo()
{
	// The original constructor (server RVA 0x24C5D0) initializes max speed
	// and empty strings. Initialize the other target fields to their typed
	// Parse defaults rather than leaving an unparsed info object indeterminate.
	m_flMaxSpeed = 1.0f;
	m_WeaponType = TERROR_WEAPONTYPE_UNKNOWN;
	m_iTeam = TEAM_UNASSIGNED;
	m_flBotAudibleRange = 2000.0f;
	m_flArmorRatio = 1.0f;
	m_iCrosshairMinDistance = 4;
	m_iCrosshairDeltaDistance = 3;
	m_bCanEquipWithShield = false;
	m_szWrongTeamMsg[0] = 0;
	m_szPlayerAnimationExtension[0] = 0;
	m_szShieldViewModel[0] = 0;
	m_szAddonModel[0] = 0;
	m_szAddonAttachment[0] = 0;
	m_flMuzzleFlashScale = 1.0f;
	m_iPenetration = 1;
	m_iDamage = 42;
	m_flRange = 8192.0f;
	m_flRangeModifier = 0.98f;
	m_iBullets = 1;
	m_flCycleTime = 0.15f;
	m_bAccuracyQuadratic = false;
	m_flAccuracyDivisor = -1.0f;
	m_flAccuracyOffset = 0.0f;
	m_flMaxInaccuracy = 0.0f;
	m_flTimeToIdleAfterFire = 2.0f;
	m_flIdleInterval = 20.0f;
	m_flNoiseFactor = 135.0f;
	m_iWeaponPrice = -1;
	m_iDefaultPrice = -1;
	m_szResponseRulesName[0] = 0;
	m_szMuzzleFlashEffect1stPerson[0] = 0;
	m_szMuzzleFlashEffect3rdPerson[0] = 0;
	m_szEjectBrassEffect[0] = 0;
	m_szDisplayName[0] = 0;
	m_szDisplayNameAllCaps[0] = 0;
	m_iTier = 0;
	m_flMusicDynamicSpeed = 0.0f;
	m_bNewInL4D2 = false;
	m_bCSWeapon = false;
	m_szAIAddon[0] = 0;
}

void CCSWeaponInfo::Parse( KeyValues *pKeyValuesData, const char *szWeaponName )
{
	// Original server RVA 0x24C700 -> FileWeaponInfo_t::Parse 0x233E00.
	FileWeaponInfo_t::Parse( pKeyValuesData, szWeaponName );

	// These are GetInt conversions in the original, not floating-point keys.
	m_flMaxSpeed = (float)pKeyValuesData->GetInt( "MaxPlayerSpeed", 1 );
	m_iWeaponPrice = pKeyValuesData->GetInt( "WeaponPrice", -1 );
	m_iDefaultPrice = m_iWeaponPrice;
	m_flArmorRatio = pKeyValuesData->GetFloat( "WeaponArmorRatio", 1.0f );
	m_iCrosshairMinDistance = pKeyValuesData->GetInt( "CrosshairMinDistance", 4 );
	m_iCrosshairDeltaDistance = pKeyValuesData->GetInt( "CrosshairDeltaDistance", 3 );
	m_bCanEquipWithShield = pKeyValuesData->GetInt( "CanEquipWithShield", 0 ) != 0;
	m_flMuzzleFlashScale = pKeyValuesData->GetFloat( "MuzzleFlashScale", 1.0f );
	m_iPenetration = pKeyValuesData->GetInt( "Penetration", 1 );
	m_iDamage = pKeyValuesData->GetInt( "Damage", 42 );
	m_flRange = pKeyValuesData->GetFloat( "Range", 8192.0f );
	m_flRangeModifier = pKeyValuesData->GetFloat( "RangeModifier", 0.98f );
	m_iBullets = pKeyValuesData->GetInt( "Bullets", 1 );
	m_flCycleTime = pKeyValuesData->GetFloat( "CycleTime", 0.15f );
	m_bAccuracyQuadratic = pKeyValuesData->GetInt( "AccuracyQuadratic", 0 ) != 0;
	m_flAccuracyDivisor = pKeyValuesData->GetFloat( "AccuracyDivisor", -1.0f );
	m_flAccuracyOffset = pKeyValuesData->GetFloat( "AccuracyOffset", 0.0f );
	m_flMaxInaccuracy = pKeyValuesData->GetFloat( "MaxInaccuracy", 0.0f );
	m_flNoiseFactor = pKeyValuesData->GetFloat( "NoiseFactor", 135.0f );
	m_flTimeToIdleAfterFire = pKeyValuesData->GetFloat( "TimeToIdle", 2.0f );
	m_flIdleInterval = pKeyValuesData->GetFloat( "IdleInterval", 20.0f );

	m_iTeam = TEAM_UNASSIGNED;
	const char *pszTeam = pKeyValuesData->GetString( "Team", "" );
	if ( !Q_stricmp( pszTeam, "Survivor" ) )
		m_iTeam = TEAM_SURVIVOR;
	else if ( !Q_stricmp( pszTeam, "Zombie" ) )
		m_iTeam = TEAM_INFECTED;

	Q_strncpy( m_szWrongTeamMsg, pKeyValuesData->GetString( "WrongTeamMsg", "" ), sizeof( m_szWrongTeamMsg ) );
	Q_strncpy( m_szShieldViewModel, pKeyValuesData->GetString( "shieldviewmodel", "" ), sizeof( m_szShieldViewModel ) );
	Q_strncpy( m_szPlayerAnimationExtension, pKeyValuesData->GetString( "PlayerAnimationExtension", "m4" ), sizeof( m_szPlayerAnimationExtension ) );
	m_flBotAudibleRange = pKeyValuesData->GetFloat( "BotAudibleRange", 2000.0f );

	m_WeaponType = TerrorWeaponTypeFromName( pKeyValuesData->GetString( "WeaponType", "" ) );

	Q_strncpy( m_szAddonModel, pKeyValuesData->GetString( "AddonModel", "" ), sizeof( m_szAddonModel ) );
	Q_strncpy( m_szAddonAttachment, pKeyValuesData->GetString( "AddonAttachment", "" ), sizeof( m_szAddonAttachment ) );
#ifndef CLIENT_DLL
	char szScriptName[MAX_PATH];
	Q_snprintf( szScriptName, sizeof( szScriptName ), "scripts/%s.txt", szWeaponName );
	engine->ForceExactFile( szScriptName );
#endif
	m_iTier = pKeyValuesData->GetInt( "Tier", 0 );
	Q_strncpy( m_szResponseRulesName, pKeyValuesData->GetString( "ResponseRulesName", "" ), sizeof( m_szResponseRulesName ) );
	Q_strncpy( m_szMuzzleFlashEffect1stPerson, pKeyValuesData->GetString( "MuzzleFlashEffect_1stPerson", "" ), sizeof( m_szMuzzleFlashEffect1stPerson ) );
	Q_strncpy( m_szMuzzleFlashEffect3rdPerson, pKeyValuesData->GetString( "MuzzleFlashEffect_3rdPerson", "" ), sizeof( m_szMuzzleFlashEffect3rdPerson ) );
	Q_strncpy( m_szEjectBrassEffect, pKeyValuesData->GetString( "EjectBrassEffect", "" ), sizeof( m_szEjectBrassEffect ) );
	m_flMusicDynamicSpeed = pKeyValuesData->GetFloat( "MusicDynamicSpeed", 0.0f );
	Q_strncpy( m_szDisplayName, pKeyValuesData->GetString( "DisplayName", "" ), sizeof( m_szDisplayName ) );
	Q_strncpy( m_szDisplayNameAllCaps, pKeyValuesData->GetString( "DisplayNameAllCaps", "" ), sizeof( m_szDisplayNameAllCaps ) );
	m_bNewInL4D2 = pKeyValuesData->GetInt( "NewInL4D2", 0 ) != 0;
	m_bCSWeapon = pKeyValuesData->GetInt( "CSWeapon", 0 ) != 0;

	// Original base ai_addon is absent from the target native base.
	const char *pszAIAddon = pKeyValuesData->GetString( "ai_addon", "ai_addon_basecombatweapon" );
	Q_strncpy( m_szAIAddon, !Q_strcmp( pszAIAddon, "None" ) ? "" : pszAIAddon, sizeof( m_szAIAddon ) );
}

void CCSWeaponInfo::Precache() const
{
	// Original server RVA 0x24C640. The weapon's base Precache already owns
	// FileWeaponInfo_t view/player models and native sound categories.
	if ( m_szAddonModel[0] )
		CBaseEntity::PrecacheModel( m_szAddonModel );
	if ( m_szMuzzleFlashEffect1stPerson[0] )
		PrecacheParticleSystem( m_szMuzzleFlashEffect1stPerson );
	if ( m_szMuzzleFlashEffect3rdPerson[0] )
		PrecacheParticleSystem( m_szMuzzleFlashEffect3rdPerson );
	if ( m_szEjectBrassEffect[0] )
		PrecacheParticleSystem( m_szEjectBrassEffect );

}

CTerrorWeaponInfo::CTerrorWeaponInfo() : m_CharacterViewmodelAddons( k_eDictCompareTypeCaseInsensitive )
{
	// Original server RVA 0x3F2160. Durations of zero request the model's
	// real sequence duration; rifle reload/deploy times are not parser defaults.
	m_iLoadoutSlots = 1;
	m_flMaxAutoAimDeflection1 = 0.08f;
	m_flMaxAutoAimRange1 = 2160.0f;
	m_flWeaponAutoAimScale = 1.0f;
	m_flVerticalPunch = 1.0f;
	m_flHorizontalPunch = 0.5f;
	m_flHorizontalPunchDirChance = 10.0f;
	m_flSpreadPerShot = 1.0f;
	m_flMaxSpread = 30.0f;
	m_flSpreadDecay = 5.0f;
	m_flMinDuckingSpread = 0.0f;
	m_flMinStandingSpread = 0.8f;
	m_flMinInAirSpread = 1.5f;
	m_flMaxMovementSpread = 5.0f;
	m_flPelletScatterPitch = 0.0f;
	m_flPelletScatterYaw = 0.0f;
	m_flReloadDuration = 0.0f;
	m_flDualReloadDuration = 0.0f;
	m_flDeployDuration = 0.0f;
	m_flDualDeployDuration = 0.0f;
	m_iPenetrationNumLayers = 1;
	m_flPenetrationPower = 0.0f;
	m_flPenetrationMaxDistance = 0.0f;
	m_flCharacterPenetrationMaxDistance = 0.0f;
	m_flGainRange = 0.0f;
	m_szDualViewModel[0] = 0;
	m_szDualPlayerModel[0] = 0;
	m_szWorldModel[0] = 0;
	m_vecAddonOffset.Invalidate();
	m_angAddonAngles.Invalidate();
}

// Original helpers at server RVAs 0x24C4D0/0x24C550 clear the entire
// destination unless all three values convert, rather than accepting a prefix.
static void ReadWeaponAddonVector( KeyValues *pData, const char *pszKey, float *pDestination )
{
	pDestination[0] = pDestination[1] = pDestination[2] = 0.0f;
	float x, y, z;
	if ( sscanf( pData->GetString( pszKey, "0 0 0" ), "%f %f %f", &x, &y, &z ) == 3 )
	{
		pDestination[0] = x;
		pDestination[1] = y;
		pDestination[2] = z;
	}
}

static int ReadWeaponPenetrationLayers( KeyValues *pData, int defaultValue )
{
	// S3F319F/C312CEF consume GetFloat's x87 return without a FLOAT spill.
	// All other weapon float fields are explicitly stored as FLOAT.
	KeyValues *pValue = pData->FindKey( "PenetrationNumLayers" );
	double value = static_cast< float >( defaultValue );
	if ( pValue )
	{
		switch ( pValue->GetDataType() )
		{
		case KeyValues::TYPE_STRING:
			value = atof( pValue->GetString() );
			break;
		case KeyValues::TYPE_WSTRING:
			value = _wtof( pValue->GetWString() );
			break;
		case KeyValues::TYPE_FLOAT:
			value = pValue->GetFloat();
			break;
		case KeyValues::TYPE_INT:
			value = pValue->GetInt();
			break;
		case KeyValues::TYPE_UINT64:
			value = static_cast< double >( pValue->GetUint64() );
			break;
		default:
			value = 0.0;
			break;
		}
	}
	// Native __ftol2_sse spills DOUBLE then converts to signed32, including
	// INT_MIN for unordered/out-of-range input (standing numeric rule).
	return _mm_cvttsd_si32( _mm_set_sd( value ) );
}

void CTerrorWeaponInfo::Parse( KeyValues *pKeyValuesData, const char *szWeaponName )
{
	// Original server RVA 0x3F2FB0. GetFloat returns the parsed value, even
	// where the original decompiler loses the x87 return before the store.
	CCSWeaponInfo::Parse( pKeyValuesData, szWeaponName );
	m_flVerticalPunch = pKeyValuesData->GetFloat( "VerticalPunch", m_flVerticalPunch );
	m_flHorizontalPunch = pKeyValuesData->GetFloat( "HorizontalPunch", m_flHorizontalPunch );
	m_flHorizontalPunchDirChance = pKeyValuesData->GetFloat( "HorizontalPunchDirChance", m_flHorizontalPunchDirChance );
	m_flSpreadPerShot = pKeyValuesData->GetFloat( "SpreadPerShot", m_flSpreadPerShot );
	m_flMaxSpread = pKeyValuesData->GetFloat( "MaxSpread", m_flMaxSpread );
	m_flSpreadDecay = pKeyValuesData->GetFloat( "SpreadDecay", m_flSpreadDecay );
	m_flMinDuckingSpread = pKeyValuesData->GetFloat( "MinDuckingSpread", m_flMinDuckingSpread );
	m_flMinStandingSpread = pKeyValuesData->GetFloat( "MinStandingSpread", m_flMinStandingSpread );
	m_flMinInAirSpread = pKeyValuesData->GetFloat( "MinInAirSpread", m_flMinInAirSpread );
	m_flMaxMovementSpread = pKeyValuesData->GetFloat( "MaxMovementSpread", m_flMaxMovementSpread );
	m_flPelletScatterPitch = pKeyValuesData->GetFloat( "PelletScatterPitch", m_flPelletScatterPitch );
	m_flPelletScatterYaw = pKeyValuesData->GetFloat( "PelletScatterYaw", m_flPelletScatterYaw );
	m_flReloadDuration = pKeyValuesData->GetFloat( "ReloadDuration", m_flReloadDuration );
	m_flDualReloadDuration = pKeyValuesData->GetFloat( "DualReloadDuration", m_flDualReloadDuration );
	m_flDeployDuration = pKeyValuesData->GetFloat( "DeployDuration", m_flDeployDuration );
	m_flDualDeployDuration = pKeyValuesData->GetFloat( "DualDeployDuration", m_flDualDeployDuration );
	m_iPenetrationNumLayers = ReadWeaponPenetrationLayers( pKeyValuesData, m_iPenetrationNumLayers );
	m_flPenetrationPower = pKeyValuesData->GetFloat( "PenetrationPower", m_flPenetrationPower );
	m_flPenetrationMaxDistance = pKeyValuesData->GetFloat( "PenetrationMaxDistance", m_flPenetrationMaxDistance );
	m_flCharacterPenetrationMaxDistance = pKeyValuesData->GetFloat( "CharacterPenetrationMaxDistance", m_flCharacterPenetrationMaxDistance );
	m_flGainRange = pKeyValuesData->GetFloat( "GainRange", m_flGainRange );
	m_flMaxAutoAimDeflection1 = pKeyValuesData->GetFloat( "MaxAutoAimDeflection1", m_flMaxAutoAimDeflection1 );
	m_flMaxAutoAimRange1 = pKeyValuesData->GetFloat( "MaxAutoAimRange1", m_flMaxAutoAimRange1 );
	m_flWeaponAutoAimScale = pKeyValuesData->GetFloat( "WeaponAutoAimScale", m_flWeaponAutoAimScale );
	m_iLoadoutSlots = pKeyValuesData->GetInt( "LoadoutSlots", m_iLoadoutSlots );
	Q_strncpy( m_szDualViewModel, pKeyValuesData->GetString( "viewmodel_dual", "" ), sizeof( m_szDualViewModel ) );
	Q_strncpy( m_szDualPlayerModel, pKeyValuesData->GetString( "playermodel_dual", "" ), sizeof( m_szDualPlayerModel ) );
	Q_strncpy( m_szWorldModel, pKeyValuesData->GetString( "worldmodel", "" ), sizeof( m_szWorldModel ) );

	// The original frees both names and model strings before replacing the
	// map; repeated case-insensitive names keep the last model value.
	m_CharacterViewmodelAddons.RemoveAll();
	KeyValues *pAddons = pKeyValuesData->FindKey( "CharacterViewmodelAddon" );
	if ( pAddons )
	{
		for ( KeyValues *pAddon = pAddons->GetFirstValue(); pAddon; pAddon = pAddon->GetNextValue() )
		{
			unsigned short iAddon = m_CharacterViewmodelAddons.Find( pAddon->GetName() );
			if ( iAddon == m_CharacterViewmodelAddons.InvalidIndex() )
				iAddon = m_CharacterViewmodelAddons.Insert( pAddon->GetName() );
			m_CharacterViewmodelAddons[iAddon] = pAddon->GetString();
		}
	}
	ReadWeaponAddonVector( pKeyValuesData, "addon_offset", m_vecAddonOffset.Base() );
	ReadWeaponAddonVector( pKeyValuesData, "addon_angles", m_angAddonAngles.Base() );
}

const char *CTerrorWeaponInfo::GetCharacterViewmodelAddon( const char *pszCharacter, bool ) const
{
	unsigned short iAddon = m_CharacterViewmodelAddons.Find( pszCharacter );
	return iAddon != m_CharacterViewmodelAddons.InvalidIndex() ? m_CharacterViewmodelAddons[iAddon].String() : NULL;
}

void CTerrorWeaponInfo::Precache() const
{
	// Original server RVA 0x3F1F30 -> CCSWeaponInfo::Precache 0x24C640.
	CCSWeaponInfo::Precache();
	FOR_EACH_DICT( m_CharacterViewmodelAddons, iAddon )
	{
		CBaseEntity::PrecacheModel( m_CharacterViewmodelAddons[iAddon].String() );
	}
	if ( m_szDualViewModel[0] )
		CBaseEntity::PrecacheModel( m_szDualViewModel );
	if ( m_szWorldModel[0] )
		CBaseEntity::PrecacheModel( m_szWorldModel );
}

// S3EEF50/C30FE70 resolves the character VIEWMODEL, not the arms addon.
const char *CTerrorWeaponInfo::GetCharacterViewModel( const char *character, bool dual ) const
{
	if ( dual && m_szDualViewModel[0] ) return m_szDualViewModel;
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( chapter )
	{
		char key[256];
		Q_snprintf( key, sizeof( key ), "%sArmsVariant", character );
		const char *model = chapter->GetString( key, "" );
		if ( model && model[0] ) return model;
	}
	return szViewModel;
}
#if defined( L4D2 ) && defined( CLIENT_DLL )
#include "l4d2/weapons/terror_weapon_parse_console.inc"
#include "l4d2/weapons/terror_weapon_parse_console_registrations.inc"
#endif

#include "l4d2/weapons/terror_weapon_parse_console_server_callbacks.inc"
