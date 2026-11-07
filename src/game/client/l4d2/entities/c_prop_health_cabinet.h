//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_PROP_HEALTH_CABINET_H
#define L4D2_C_PROP_HEALTH_CABINET_H
#ifdef _WIN32
#pragma once
#endif

#include "c_props.h"

class C_PropHealthCabinet : public C_DynamicProp
{
public:
	DECLARE_CLASS( C_PropHealthCabinet, C_DynamicProp );
	DECLARE_CLIENTCLASS();

	virtual int ObjectCaps() OVERRIDE;
	virtual int GetUsePriority( C_BaseEntity *user ) OVERRIDE { return m_isUsed ? 0 : 2; } // C268E70.
	virtual ShadowType_t ShadowCastType() OVERRIDE { return SHADOWS_NONE; }
	virtual bool IsUsable( C_BaseEntity *pUser ) { return true; }
	bool IsUsed() const { return m_isUsed; }

private:
	bool m_isUsed;
};

#ifdef CLIENT_DLL
#define CPropHealthCabinet C_PropHealthCabinet
#endif

#endif // L4D2_C_PROP_HEALTH_CABINET_H
