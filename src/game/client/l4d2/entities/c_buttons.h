//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_BUTTONS_H
#define L4D2_C_BUTTONS_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseentity.h"

class C_BaseToggle : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_BaseToggle, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // Native primary17 C06A2C0.

	virtual void GetGroundVelocityToApply( Vector &vecGroundVel ) OVERRIDE;
};

class C_BaseButton : public C_BaseToggle
{
public:
	DECLARE_CLASS( C_BaseButton, C_BaseToggle );
	DECLARE_CLIENTCLASS();
	datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // Native primary17 C06A3B0.

	C_BaseButton();
	virtual int ObjectCaps() OVERRIDE;
	virtual C_BaseEntity *GetUseRedirectEntity() OVERRIDE;
	virtual bool IsUsable( C_BaseEntity *pUser ) OVERRIDE;
	virtual unsigned int PhysicsSolidMaskForEntity() const OVERRIDE;

protected:
	CHandle< C_BaseEntity > m_glowEntity;
	bool m_usable;
};

class C_ButtonTimed : public C_BaseButton
{
public:
	DECLARE_CLASS( C_ButtonTimed, C_BaseButton );
	DECLARE_CLIENTCLASS();

	C_ButtonTimed();
	const char *GetUseString() const { return m_sUseString; }
	const char *GetUseSubString() const { return m_sUseSubString; }

private:
	char m_sUseString[64];
	char m_sUseSubString[64];
};

#endif // L4D2_C_BUTTONS_H
