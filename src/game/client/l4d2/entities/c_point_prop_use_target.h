//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_POINT_PROP_USE_TARGET_H
#define C_POINT_PROP_USE_TARGET_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseentity.h"

class C_BasePlayer;

// The original client RTTI name, unlike the script target, has no C_ prefix.
class CPointPropUseTarget : public C_BaseEntity
{
public:
	DECLARE_CLASS( CPointPropUseTarget, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	CPointPropUseTarget();
	virtual void Spawn() OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;
	bool IsGlowUseTarget() OVERRIDE { return false; } // Genuine2F6530, not IsAvailableForUse.

	enum { USE_GASCAN = 0x01000000, USE_COLA = 0x02000000 };
	static C_BaseEntity *FindUseTarget( C_BasePlayer *pPlayer, float flRange, float flTolerance, int nCapability, C_BaseEntity *pSkip );
	bool IsAvailableForUse() const;
	C_BaseEntity *GetUseActionOwner() const { return m_useActionOwner.Get(); }
	C_BaseEntity *GetGasNozzle() const { return m_hGasNozzle.Get(); }
	void SetUseActionOwner( C_BaseEntity *pOwner );

private:
	EHANDLE m_useActionOwner;
	EHANDLE m_hGasNozzle;
	int m_nUseCapabilities;
	bool m_bActive;
};

#endif // C_POINT_PROP_USE_TARGET_H
