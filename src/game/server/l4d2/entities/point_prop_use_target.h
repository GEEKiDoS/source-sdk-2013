//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef POINT_PROP_USE_TARGET_H
#define POINT_PROP_USE_TARGET_H
#ifdef _WIN32
#pragma once
#endif

#include "baseentity.h"
#include "entityoutput.h"

class CBasePlayer;

class CPointPropUseTarget : public CBaseEntity
{
public:
	DECLARE_CLASS( CPointPropUseTarget, CBaseEntity );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CPointPropUseTarget();
	virtual void Spawn() OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;

	// These bits are the original gascan / cola backpack-use capabilities.
	enum { USE_GASCAN = 0x01000000, USE_COLA = 0x02000000 };
	static CPointPropUseTarget *FindUseTarget( CBasePlayer *pPlayer, float flRange, float flTolerance, int nCapability );

	bool IsAvailableForUse() const;
	CBaseEntity *GetUseActionOwner() const { return m_useActionOwner.Get(); }
	CBaseEntity *GetGasNozzle() const { return m_hGasNozzle.Get(); }
	void SetUseActionOwner( CBaseEntity *pOwner );
	void OnUseStarted( CBaseEntity *pOwner );
	void OnUseCancelled();
	void OnUseFinished();

	void InputActivate( inputdata_t &inputdata );
	void InputDeactivate( inputdata_t &inputdata );

private:
	CNetworkHandle( CBaseEntity, m_useActionOwner );
	CNetworkHandle( CBaseEntity, m_hGasNozzle );
	int m_nUseCapabilities;
	bool m_bActive;
	COutputEvent m_onUseStarted;
	COutputEvent m_onUseCancelled;
	COutputEvent m_onUseFinished;
	string_t m_sGasNozzleName;
};

#endif // POINT_PROP_USE_TARGET_H
