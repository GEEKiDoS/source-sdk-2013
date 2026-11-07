#pragma once
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"

class CScavengeProgressDisplay : public CBaseEntity
{
public:
	DECLARE_CLASS( CScavengeProgressDisplay, CBaseEntity );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;
	void InputTurnOn( inputdata_t &data );
	void InputTurnOff( inputdata_t &data );
	void InputSetTotalItems( inputdata_t &data );
private:
	int m_nTotalScavengeItems;
	CNetworkVar( int, m_bActive );
};
