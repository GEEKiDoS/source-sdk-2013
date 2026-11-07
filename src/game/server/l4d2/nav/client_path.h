#pragma once
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_path.h"
#include <xmmintrin.h>

class CClientPath : public CBaseEntity
{
public:
	DECLARE_CLASS( CClientPath, CBaseEntity );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	CClientPath();
	virtual void Spawn() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;
	virtual int ShouldTransmit( const CCheckTransmitInfo *info ) OVERRIDE;
	void InputPlayer( inputdata_t &inputdata );
	void VisibilityThink();
private:
	void AddPoint( const Vector &point );
	void AddArea( CNavArea *area );
	CNetworkArray( int, m_posX, 256 );
	CNetworkArray( int, m_posY, 256 );
	CNetworkArray( int, m_posZ, 256 );
	CNetworkVar( int, m_posCount );
	CNavArea *m_pLastArea;
	CNavArea *m_pStartArea;
	CNavArea *m_pGoalArea;
	EHANDLE m_hPlayer;
	bool m_bValid;
};
