//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef FUNC_VPHYSICSCLIP_H
#define FUNC_VPHYSICSCLIP_H
#ifdef _WIN32
#pragma once
#endif
#include "baseentity.h"
#include "filters.h"

// Move the existing bmodels.cpp declaration here; retain that TU's methods and
// factory. Protected disabled state is shared by native CScriptVPhysicsClip.
class CFuncVPhysicsClip : public CBaseEntity
{
	DECLARE_CLASS( CFuncVPhysicsClip, CBaseEntity );
	DECLARE_DATADESC();
public:
	void Spawn() OVERRIDE;
	void Activate() OVERRIDE;
	bool CreateVPhysics() OVERRIDE;
	bool EntityPassesFilter( CBaseEntity *other );
	bool ForceVPhysicsCollide( CBaseEntity *other ) OVERRIDE;
	void InputEnable( inputdata_t &inputdata );
	void InputDisable( inputdata_t &inputdata );
protected:
	string_t m_iFilterName;
	CHandle< CBaseFilter > m_hFilter;
	bool m_bDisabled;
};
#endif // FUNC_VPHYSICSCLIP_H
