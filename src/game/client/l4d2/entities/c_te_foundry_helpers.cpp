//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_basetempentity.h"
#include "tier0/memdbgon.h"

class C_TEFoundryHelpers : public C_BaseTempEntity
{
public:
	DECLARE_CLASS( C_TEFoundryHelpers, C_BaseTempEntity );
	DECLARE_CLIENTCLASS();
	// C0F01E0. The ONLY native sender writes -1 (S0D7000); consequently
	// the client helper list has no append producer and is always empty.
	// Ledger326 elides that list, its empty Purge and its drawing together.
	// Keep this override: the SDK base PostDataUpdate asserts, unlike native.
	virtual void PostDataUpdate( DataUpdateType_t ) OVERRIDE {}
	int m_iEntity;
};
IMPLEMENT_CLIENTCLASS_EVENT_DT( C_TEFoundryHelpers, DT_TEFoundryHelpers, CTEFoundryHelpers )
	RecvPropInt( RECVINFO( m_iEntity ) ),
END_RECV_TABLE()
