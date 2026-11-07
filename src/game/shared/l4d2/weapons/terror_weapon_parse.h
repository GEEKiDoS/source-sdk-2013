//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: L4D2 weapon-script data, shared by the client and server.
//
//=============================================================================//

#ifndef TERROR_WEAPON_PARSE_H
#define TERROR_WEAPON_PARSE_H
#ifdef _WIN32
#pragma once
#endif

#include "weapon_parse.h"
#include "mathlib/vector.h"
#include "tier1/utldict.h"
#include "tier1/utlstring.h"

// Values recovered from the original weapon-type name table (server RVA 0x663640).
// The table has no name for value 6.
enum TerrorWeaponType_t
{
	TERROR_WEAPONTYPE_PISTOL = 0,
	TERROR_WEAPONTYPE_SMG = 1,
	TERROR_WEAPONTYPE_RIFLE = 2,
	TERROR_WEAPONTYPE_SHOTGUN = 3,
	TERROR_WEAPONTYPE_SNIPER_RIFLE = 4,
	TERROR_WEAPONTYPE_MACHINEGUN = 5,
	TERROR_WEAPONTYPE_GRENADE = 7,
	TERROR_WEAPONTYPE_HEALING = 8,
	TERROR_WEAPONTYPE_REVIVING = 9,
	TERROR_WEAPONTYPE_MELEE = 10,
	TERROR_WEAPONTYPE_CHAINSAW = 11,
	TERROR_WEAPONTYPE_GRENADE_LAUNCHER = 12,
	TERROR_WEAPONTYPE_CARRIED_PROP = 13,
	TERROR_WEAPONTYPE_CLAW = 14,
	TERROR_WEAPONTYPE_UPGRADEPACK = 15,
	TERROR_WEAPONTYPE_UNKNOWN = 16
};

TerrorWeaponType_t TerrorWeaponTypeFromName( const char *pszName );
const char *WeaponTypeToString( TerrorWeaponType_t type );

class CCSWeaponInfo : public FileWeaponInfo_t
{
public:
	CCSWeaponInfo();

	virtual void Parse( KeyValues *pKeyValuesData, const char *szWeaponName ) OVERRIDE;
	virtual void Precache() const;

	float m_flMaxSpeed;
	TerrorWeaponType_t m_WeaponType;
	int m_iTeam;
	float m_flBotAudibleRange;
	float m_flArmorRatio;
	int m_iCrosshairMinDistance;
	int m_iCrosshairDeltaDistance;
	bool m_bCanEquipWithShield;
	char m_szWrongTeamMsg[32];
	char m_szPlayerAnimationExtension[MAX_WEAPON_PREFIX];
	char m_szShieldViewModel[64];
	char m_szAddonModel[MAX_WEAPON_STRING];
	char m_szAddonAttachment[MAX_WEAPON_STRING];
	float m_flMuzzleFlashScale;
	int m_iPenetration;
	int m_iDamage;
	float m_flRange;
	float m_flRangeModifier;
	int m_iBullets;
	float m_flCycleTime;
	bool m_bAccuracyQuadratic;
	float m_flAccuracyDivisor;
	float m_flAccuracyOffset;
	float m_flMaxInaccuracy;
	float m_flTimeToIdleAfterFire;
	float m_flIdleInterval;
	float m_flNoiseFactor;
	int m_iWeaponPrice;
	int m_iDefaultPrice;
	char m_szResponseRulesName[MAX_WEAPON_STRING];
	char m_szMuzzleFlashEffect1stPerson[MAX_WEAPON_STRING];
	char m_szMuzzleFlashEffect3rdPerson[MAX_WEAPON_STRING];
	char m_szEjectBrassEffect[MAX_WEAPON_STRING];
	char m_szDisplayName[MAX_WEAPON_STRING];
	char m_szDisplayNameAllCaps[MAX_WEAPON_STRING];
	int m_iTier;
	float m_flMusicDynamicSpeed;
	bool m_bNewInL4D2;
	bool m_bCSWeapon;

	// Original FileWeaponInfo_t data absent from the target's native base.
	char m_szAIAddon[MAX_WEAPON_STRING];
};

class CTerrorWeaponInfo : public CCSWeaponInfo
{
public:
	CTerrorWeaponInfo();

	virtual void Parse( KeyValues *pKeyValuesData, const char *szWeaponName ) OVERRIDE;
	virtual void Precache() const OVERRIDE;

	// Character names are the original script keys (Gambler, Coach, etc.).
	// The returned script-owned string survives until the next Parse; NULL
	// means the character has no addon, not a substitute survivor model.
	const char *GetCharacterViewmodelAddon( const char *pszCharacter, bool dual ) const;
	const char *GetCharacterViewModel( const char *character, bool dual ) const; // S3EEF50/C30FE70, NOT arms addon.

	int m_iLoadoutSlots;
	float m_flMaxAutoAimDeflection1;
	float m_flMaxAutoAimRange1;
	float m_flWeaponAutoAimScale;
	float m_flVerticalPunch;
	float m_flHorizontalPunch;
	float m_flHorizontalPunchDirChance;
	float m_flSpreadPerShot;
	float m_flMaxSpread;
	float m_flSpreadDecay;
	float m_flMinDuckingSpread;
	float m_flMinStandingSpread;
	float m_flMinInAirSpread;
	float m_flMaxMovementSpread;
	float m_flPelletScatterPitch;
	float m_flPelletScatterYaw;
	float m_flReloadDuration;
	float m_flDualReloadDuration;
	float m_flDeployDuration;
	float m_flDualDeployDuration;
	int m_iPenetrationNumLayers;
	float m_flPenetrationPower;
	float m_flPenetrationMaxDistance;
	float m_flCharacterPenetrationMaxDistance;
	float m_flGainRange;
	char m_szDualViewModel[MAX_WEAPON_STRING];
	char m_szDualPlayerModel[MAX_WEAPON_STRING];
	Vector m_vecAddonOffset;
	QAngle m_angAddonAngles;
	// Optional worldmodel is distinct from FileWeaponInfo_t::szWorldModel,
	// which is the script's playermodel.
	char m_szWorldModel[MAX_WEAPON_STRING];

private:
	CUtlDict< CUtlString, unsigned short > m_CharacterViewmodelAddons;
};

#endif // TERROR_WEAPON_PARSE_H
