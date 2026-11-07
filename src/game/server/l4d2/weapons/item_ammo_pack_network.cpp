//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_backpack_item.h"
#include "tier0/memdbgon.h"

// S3D22C0/3D2340: retained network and sentinel-only SAVE metadata. The
// pinned server has NO weapon_ammo_pack factory, CItemAmmoPack vtable or RTTI.
// Do not fabricate a server item implementation/factory to match its client.
// This metadata type deliberately remains abstract through its real parent.
class CItemAmmoPack : public CBaseBackpackItem
{
public:
	DECLARE_CLASS( CItemAmmoPack, CBaseBackpackItem );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
};
IMPLEMENT_SERVERCLASS_ST( CItemAmmoPack, DT_AmmoPack )
END_SEND_TABLE()
BEGIN_DATADESC( CItemAmmoPack )
END_DATADESC()
