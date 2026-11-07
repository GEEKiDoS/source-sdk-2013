//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_PROP_HEALTH_CABINET_H
#define L4D2_PROP_HEALTH_CABINET_H
#ifdef _WIN32
#pragma once
#endif

#include "props.h"
#include "GameEventListener.h"

class CTerrorPlayer;

class CPropHealthCabinet : public CDynamicProp, public CGameEventListener
{
public:
	DECLARE_CLASS( CPropHealthCabinet, CDynamicProp );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CPropHealthCabinet();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value ) OVERRIDE;
	virtual void FireGameEvent( IGameEvent *pEvent ) OVERRIDE;
	virtual int GetUsePriority( CBaseEntity *user ) OVERRIDE { return m_isUsed ? 0 : 2; } // S2C12F0.
	bool IsUsed() const { return m_isUsed; }
	void Populate();

private:
	void SetOpen();
	CNetworkVar( bool, m_isUsed );
	int m_pillCount;
	bool m_isPopulated;
};

#endif // L4D2_PROP_HEALTH_CABINET_H
