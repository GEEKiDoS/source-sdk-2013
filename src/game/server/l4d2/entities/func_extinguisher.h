//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_FUNC_EXTINGUISHER_H
#define L4D2_FUNC_EXTINGUISHER_H
#ifdef _WIN32
#pragma once
#endif

#include "triggers.h"

class CFuncExtinguisher : public CBaseTrigger
{
public:
	DECLARE_CLASS( CFuncExtinguisher, CBaseTrigger );
	DECLARE_DATADESC();

	CFuncExtinguisher();
	virtual void Spawn() OVERRIDE;
	virtual void InputEnable( inputdata_t &data ) OVERRIDE;
	virtual void InputDisable( inputdata_t &data ) OVERRIDE;
	void ExtinguishThink();

private:
	void ExtinguishClass( const char *pClassname );
};

#endif // L4D2_FUNC_EXTINGUISHER_H
