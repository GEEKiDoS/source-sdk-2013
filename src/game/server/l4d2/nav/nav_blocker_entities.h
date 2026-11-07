//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_NAV_BLOCKER_ENTITIES_H
#define L4D2_NAV_BLOCKER_ENTITIES_H
#ifdef _WIN32
#pragma once
#endif
#include "nav_entities.h"

class CFuncNavConnectionBlocker : public CBaseEntity
{
public:
	DECLARE_CLASS( CFuncNavConnectionBlocker, CBaseEntity );
	virtual void Spawn() OVERRIDE;
	bool IsBlocking( const Ray_t &ray );
};
class CScriptNavBlocker : public CFuncNavBlocker
{
public:
	DECLARE_CLASS( CScriptNavBlocker, CFuncNavBlocker );
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
private:
	Vector m_vExtent;
};
#endif // L4D2_NAV_BLOCKER_ENTITIES_H
