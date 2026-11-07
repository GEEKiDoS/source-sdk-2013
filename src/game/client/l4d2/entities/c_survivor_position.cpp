//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "tier0/memdbgon.h"

class C_SurvivorPosition : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_SurvivorPosition, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	C_SurvivorPosition() : m_order( 0 ) {}
private:
	int m_order;
};

IMPLEMENT_CLIENTCLASS_DT( C_SurvivorPosition, DT_SurvivorPosition, CSurvivorPosition )
	RecvPropInt( RECVINFO( m_order ) ),
END_RECV_TABLE()
