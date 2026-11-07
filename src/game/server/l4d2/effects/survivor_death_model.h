//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_DEATH_MODEL_H
#define SURVIVOR_DEATH_MODEL_H
#ifdef _WIN32
#pragma once
#endif

#include "baseflex.h"
#include "l4d2/terror_shareddefs.h"

class CTerrorPlayer;

// An animated survivor body, not a cs_ragdoll or an infected retirement timer.
class CSurvivorDeathModel : public CBaseFlex
{
public:
	DECLARE_CLASS( CSurvivorDeathModel, CBaseFlex );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CSurvivorDeathModel();
	virtual ~CSurvivorDeathModel();
	virtual int ObjectCaps() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;

	static CSurvivorDeathModel *Create( CTerrorPlayer *pPlayer );
	static CSurvivorDeathModel *FindForPlayer( const CTerrorPlayer *pPlayer );
	static CSurvivorDeathModel *FindNearest( CTerrorPlayer *pRescuer, float flRange );
	static void RemoveForPlayer( CTerrorPlayer *pPlayer );

	int GetCharacterType() const { return m_nCharacterType.Get(); }
	CTerrorPlayer *GetSurvivor() const;
	bool Defibrillate( CTerrorPlayer *pRescuer );
	void InputShock( inputdata_t &inputdata );

private:
	void Initialize( CTerrorPlayer *pPlayer );
	static bool OnVisible( CBaseEntity *pEntity, CBasePlayer *pObserver );

	CNetworkVar( int, m_nCharacterType );
};

#endif // SURVIVOR_DEATH_MODEL_H
