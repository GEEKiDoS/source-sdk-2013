//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//
#include "cbase.h"
#include "model_types.h"
#include "vcollide.h"
#include "vcollide_parse.h"
#include "solidsetdefaults.h"
#include "bone_setup.h"
#include "engine/ivmodelinfo.h"
#include "physics.h"
#include "c_breakableprop.h"
#include "view.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT(C_BreakableProp, DT_BreakableProp, CBreakableProp)
#if defined( L4D2 )
	RecvPropBool( RECVINFO( m_noGhostCollision ) ),
	RecvPropBool( RECVINFO( m_bClientPhysics ) ),
#endif
END_RECV_TABLE()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_BreakableProp::C_BreakableProp( void )
{
	m_takedamage = DAMAGE_YES;
}

#if defined( L4D2 )
void C_BreakableProp::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	if ( m_bClientPhysics ) PhysOnDataChanged( this, type == DATA_UPDATE_CREATED );
}

unsigned int C_BreakableProp::PhysicsSolidMaskForEntity() const
{
	if ( m_noGhostCollision ) return BaseClass::PhysicsSolidMaskForEntity() | 0x0200;
	return BaseClass::PhysicsSolidMaskForEntity();
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BreakableProp::SetFadeMinMax( float fademin, float fademax )
{
	m_fadeMinDist = fademin;
	m_fadeMaxDist = fademax;
}

//-----------------------------------------------------------------------------
// Copy fade from another breakable prop
//-----------------------------------------------------------------------------
void C_BreakableProp::CopyFadeFrom( C_BreakableProp *pSource )
{
	m_flFadeScale = pSource->m_flFadeScale;
}
