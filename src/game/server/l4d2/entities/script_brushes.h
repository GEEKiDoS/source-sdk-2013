//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SCRIPT_BRUSHES_H
#define L4D2_SCRIPT_BRUSHES_H
#ifdef _WIN32
#pragma once
#endif
#include "triggers.h"
#include "buttons.h"
#include "l4d2/entities/func_vphysicsclip.h"

class CScriptVPhysicsClip : public CFuncVPhysicsClip
{
	DECLARE_CLASS( CScriptVPhysicsClip, CFuncVPhysicsClip );
public:
	DECLARE_DATADESC();
	void Spawn() OVERRIDE;
private:
	Vector m_vExtent;
};

class CScriptBaseButton : public CBaseButton
{
	DECLARE_CLASS( CScriptBaseButton, CBaseButton );
public:
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();
	void Spawn() OVERRIDE;
	int UpdateTransmitState() OVERRIDE;
private:
	Vector m_vExtent;
};

class CScriptTriggerHurt : public CTriggerHurt
{
	DECLARE_CLASS( CScriptTriggerHurt, CTriggerHurt );
public:
	DECLARE_DATADESC();
	void Spawn() OVERRIDE;
private:
	Vector m_vExtent;
};

class CScriptTriggerMultiple : public CTriggerMultiple
{
	DECLARE_CLASS( CScriptTriggerMultiple, CTriggerMultiple );
public:
	DECLARE_DATADESC();
	void Spawn() OVERRIDE;
private:
	Vector m_vExtent;
};

class CScriptTriggerOnce : public CTriggerOnce
{
	DECLARE_CLASS( CScriptTriggerOnce, CTriggerOnce );
public:
	DECLARE_DATADESC();
	void Spawn() OVERRIDE;
private:
	Vector m_vExtent;
};

class CScriptTriggerPush : public CTriggerPush
{
	DECLARE_CLASS( CScriptTriggerPush, CTriggerPush );
public:
	DECLARE_DATADESC();
	void Spawn() OVERRIDE;
private:
	Vector m_vExtent;
};

class CTriggerHurtGhost : public CTriggerHurt
{
	DECLARE_CLASS( CTriggerHurtGhost, CTriggerHurt );
};

class CTriggerAutoCrouch : public CBaseTrigger
{
	DECLARE_CLASS( CTriggerAutoCrouch, CBaseTrigger );
public:
	DECLARE_DATADESC();
	void Spawn() OVERRIDE;
	bool PassesTriggerFilters( CBaseEntity *other ) OVERRIDE;
	void StartTouch( CBaseEntity *other ) OVERRIDE;
	void EndTouch( CBaseEntity *other ) OVERRIDE;
};

class CTriggerCallback : public CBaseTrigger
{
	DECLARE_CLASS( CTriggerCallback, CBaseTrigger );
public:
	DECLARE_DATADESC();
	void Spawn() OVERRIDE;
	void StartTouch( CBaseEntity *other ) OVERRIDE;
private:
	// Native +1368: movement-parent member callback, NOT a script function handle.
	// No datamap entry or factory-time setter; zero-filled entity storage is NULL.
	void ( CBaseEntity::*m_pfnCallback )( CBaseEntity *other );
};

class CTriggerActiveWeaponDetect : public CBaseTrigger
{
	DECLARE_CLASS( CTriggerActiveWeaponDetect, CBaseTrigger );
public:
	DECLARE_DATADESC();
	void Spawn() OVERRIDE;
	bool PassesTriggerFilters( CBaseEntity *other ) OVERRIDE;
	void StartTouch( CBaseEntity *other ) OVERRIDE;
	void ActiveWeaponThink();
private:
	COutputEvent m_OnTouchedActiveWeapon;
	string_t m_iszWeaponClassName;
};
#endif // L4D2_SCRIPT_BRUSHES_H
