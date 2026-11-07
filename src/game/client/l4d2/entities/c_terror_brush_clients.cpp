//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/c_terror_breakable.h"
#include "physics.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_Breakable, DT_Breakable, CBreakable )
END_RECV_TABLE()
void C_Breakable::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type ); // C76B70.
	PhysOnDataChanged( this, type == DATA_UPDATE_CREATED );
}
unsigned int C_Breakable::PhysicsSolidMaskForEntity() const
{
	return BaseClass::PhysicsSolidMaskForEntity() | 0x0200; // C76B60.
}
