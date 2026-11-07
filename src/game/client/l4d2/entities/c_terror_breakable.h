//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_TERROR_BREAKABLE_H
#define C_TERROR_BREAKABLE_H
#ifdef _WIN32
#pragma once
#endif
#include "c_baseentity.h"
class C_Breakable : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_Breakable, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // C86230.
	void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
	unsigned int PhysicsSolidMaskForEntity() const OVERRIDE;
};
#endif
