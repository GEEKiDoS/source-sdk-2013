//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/c_guts.h"
#include "cliententitylist.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( guts, C_Guts ); // C4FBB90.

void C_Guts::Release()
{
	// C82910: remove/invalidate BEFORE root Release; bypass animated Release.
	if ( GetRefEHandle().IsValid() )
	{
		ClientEntityList().RemoveEntity( GetRefEHandle() );
		SetRefEHandle( CBaseHandle( INVALID_EHANDLE ) );
	}
	C_BaseEntity::Release();
}
