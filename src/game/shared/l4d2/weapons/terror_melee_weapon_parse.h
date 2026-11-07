//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Original melee-script catalog. This is not weapon_melee's gun script.
//
//=============================================================================//

#ifndef TERROR_MELEE_WEAPON_PARSE_H
#define TERROR_MELEE_WEAPON_PARSE_H
#ifdef _WIN32
#pragma once
#endif

#include "ai_activity.h"
#include "igamesystem.h"
#include "mathlib/vector.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"

class KeyValues;
class CHudTexture;

typedef int MeleeWeaponInfoHandle_t;
const MeleeWeaponInfoHandle_t INVALID_MELEE_WEAPON_INFO_HANDLE = -1;
const int MAX_MELEE_WEAPON_STRING = 80;
const int MAX_MELEE_WEAPON_INFOS = 16;
const int MELEE_WEAPON_SOUND_COUNT = 20;

enum MeleeAttackDirection_t
{
	MELEE_DIRECTION_INVALID = -1,
	MELEE_DIRECTION_N = 0,
	MELEE_DIRECTION_S,
	MELEE_DIRECTION_E,
	MELEE_DIRECTION_W,
	MELEE_DIRECTION_NE,
	MELEE_DIRECTION_SE,
	MELEE_DIRECTION_SW,
	MELEE_DIRECTION_NW
};

enum MeleeWeaponActivity_t
{
	MELEE_ACTIVITY_IDLE = 0,
	MELEE_ACTIVITY_WALK,
	MELEE_ACTIVITY_RUN,
	MELEE_ACTIVITY_CROUCHIDLE,
	MELEE_ACTIVITY_CROUCHWALK,
	MELEE_ACTIVITY_CROUCHRUN,
	MELEE_ACTIVITY_IDLEINJURED,
	MELEE_ACTIVITY_WALKINJURED,
	MELEE_ACTIVITY_RUNINJURED,
	MELEE_ACTIVITY_IDLECALM,
	MELEE_ACTIVITY_WALKCALM,
	MELEE_ACTIVITY_RUNCALM,
	MELEE_ACTIVITY_JUMP,
	MELEE_ACTIVITY_DEPLOY,
	MELEE_ACTIVITY_SHOVE,
	MELEE_ACTIVITY_COUNT
};

struct MeleeAttackInfo_t
{
	Activity m_Activity;
	Activity m_PlayerActivity;
	Activity m_PlayerIdleActivity;
	MeleeAttackDirection_t m_StartDirection;
	MeleeAttackDirection_t m_EndDirection;
	float m_flStartTime;
	float m_flEndTime;
	float m_flDuration;
	bool m_bPrimaryAttack;
	Vector m_vecForceDirection;

	void Parse( KeyValues *pData, bool bPrimary );
};

class CMeleeWeaponInfo
{
public:
	CMeleeWeaponInfo( const char *pszName, KeyValues *pData );
	void Precache() const;

	char m_szName[MAX_MELEE_WEAPON_STRING];
	float m_flRefireDelay;
	char m_szViewModel[MAX_MELEE_WEAPON_STRING];
	char m_szPlayerModel[MAX_MELEE_WEAPON_STRING];
	char m_szAddonAttachment[MAX_MELEE_WEAPON_STRING];
	Vector m_vecAddonOffset;
	QAngle m_angAddonAngles;
	// Original Parse copies 80 bytes over a 16-byte member. Keep the resulting
	// bytes, including the subsequent damage/activity writes, without an overrun.
	char m_szAnimPrefix[MAX_MELEE_WEAPON_STRING];
	float m_flDamage;
	int m_iDamageFlags;
	int m_iRumbleEffect;
	bool m_bDecapitates;
	char m_szActivity[MELEE_ACTIVITY_COUNT][MAX_MELEE_WEAPON_STRING];
	char m_szShootSounds[MELEE_WEAPON_SOUND_COUNT][MAX_MELEE_WEAPON_STRING];
	CUtlVector<MeleeAttackInfo_t> m_PrimaryAttacks;
	CUtlVector<MeleeAttackInfo_t> m_SecondaryAttacks;
	int m_iPlayerAnimEvent;
	float m_flWeaponIdleTime;
#ifdef CLIENT_DLL
	char m_szSpriteActive[MAX_MELEE_WEAPON_STRING];
	mutable CHudTexture *m_pSpriteActive;
#endif

private:
	CMeleeWeaponInfo( const CMeleeWeaponInfo & );
	CMeleeWeaponInfo &operator=( const CMeleeWeaponInfo & );
};

class CMeleeWeaponInfoStore : public CAutoGameSystem
{
public:
	CMeleeWeaponInfoStore();
	virtual ~CMeleeWeaponInfoStore();
	virtual void LevelInitPreEntity() OVERRIDE;
	virtual void LevelInitPostEntity() OVERRIDE;
	virtual void Shutdown() OVERRIDE;

	const CMeleeWeaponInfo *GetMeleeWeaponInfo( MeleeWeaponInfoHandle_t hInfo ) const;
	MeleeWeaponInfoHandle_t GetMeleeWeaponInfoIndex( const char *pszName ) const;
	MeleeWeaponInfoHandle_t GetRandomMeleeWeaponInfo() const;
	int GetMeleeWeaponCount() const;
	bool IsValidAttackInfo( const MeleeAttackInfo_t *pAttack ) const;
	void Dump() const;

	// Explicit reload command and level initialization are the only loaders.
	// Lookups never add missing campaign weapons or reparse mounted scripts.
	void LoadScripts();

private:
	void Clear();
	void AddScript( const char *pszName, KeyValues *pData );
#ifndef CLIENT_DLL
	bool IsScriptAllowed( const char *pszName ) const;
	void LoadManifest( const CUtlVector<CUtlString> &names );
	void LoadNamedScripts( const CUtlVector<CUtlString> &names );
#endif
	CUtlVector<CUtlString> m_Names;
	CUtlVector<CMeleeWeaponInfo *> m_Infos;
};

CMeleeWeaponInfoStore *MeleeWeaponInfoStore();

#endif // TERROR_MELEE_WEAPON_PARSE_H
