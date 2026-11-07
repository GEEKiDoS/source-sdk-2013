//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ENV_ROCK_LAUNCHER_H
#define L4D2_ENV_ROCK_LAUNCHER_H
#ifdef _WIN32
#pragma once
#endif
#include "baseentity.h"

// Native RTTI: CEnvRockLauncher -> CPointEntity -> CBaseEntity.
class CEnvRockLauncher : public CPointEntity
{
public:
	DECLARE_CLASS( CEnvRockLauncher, CPointEntity );
	DECLARE_DATADESC();
	CEnvRockLauncher();
	virtual void Spawn() OVERRIDE;
	void InputLaunchRock( inputdata_t &input );
	void InputSetTarget( inputdata_t &input );
private:
	void LaunchAtTarget( CBaseEntity *target );
	void LaunchForward();
	string_t m_targetName;
	int m_rockDamageOverride;
};
#endif // L4D2_ENV_ROCK_LAUNCHER_H
