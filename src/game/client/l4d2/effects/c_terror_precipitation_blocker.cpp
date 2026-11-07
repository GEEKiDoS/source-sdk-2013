//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/c_terror_precipitation_blocker.h"
#include "collisionproperty.h"
#include "tier0/memdbgon.h"

CUtlVector< C_BaseEntity * > g_PrecipitationBlockers;

class C_PrecipitationBlocker : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_PrecipitationBlocker, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	C_PrecipitationBlocker() { g_PrecipitationBlockers.AddToTail( this ); }
	virtual ~C_PrecipitationBlocker() { g_PrecipitationBlockers.FindAndRemove( this ); }
};
IMPLEMENT_CLIENTCLASS_DT( C_PrecipitationBlocker, DT_PrecipitationBlocker, CPrecipitationBlocker )
END_RECV_TABLE()

// C1EC900 -> C0D67A0. Ordered inclusive bounds in collision space;
// do not replace with an unrotated world AABB or a world trace.
bool IsInPrecipitationBlocker( const Vector &point )
{
	FOR_EACH_VEC( g_PrecipitationBlockers, index )
	{
		if ( g_PrecipitationBlockers[index]->CollisionProp()->IsPointInBounds( point ) )
			return true;
	}
	return false;
}
