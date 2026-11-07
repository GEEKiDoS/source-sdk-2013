//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"
#include "view.h"
#include "iviewrender.h"
#if defined( L4D2 )
#include "physics.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class C_Func_LOD : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_Func_LOD, C_BaseEntity );
	DECLARE_CLIENTCLASS();

					C_Func_LOD();

// C_BaseEntity overrides.
public:

#if defined( L4D2 )
	void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
#else
	unsigned char	GetClientSideFade();
#endif

public:
// Replicated vars from the server.
// These are documented in the server-side entity.
public:
#if defined( L4D2 )
	int m_nDisappearMinDist;
	int m_nDisappearMaxDist;
#else
	float			m_fDisappearDist;
#endif
};


#if !defined( L4D2 )
ConVar lod_TransitionDist("lod_TransitionDist", "800");
#endif


// ------------------------------------------------------------------------- //
// Tables.
// ------------------------------------------------------------------------- //

// Datatable..
IMPLEMENT_CLIENTCLASS_DT(C_Func_LOD, DT_Func_LOD, CFunc_LOD)
#if defined( L4D2 )
	RecvPropInt( RECVINFO( m_nDisappearMinDist ) ),
	RecvPropInt( RECVINFO( m_nDisappearMaxDist ) ),
#else
	RecvPropFloat(RECVINFO(m_fDisappearDist)),
#endif
END_RECV_TABLE()



// ------------------------------------------------------------------------- //
// C_Func_LOD implementation.
// ------------------------------------------------------------------------- //

C_Func_LOD::C_Func_LOD()
{
#if defined( L4D2 )
	m_nDisappearMinDist = 5000;
	m_nDisappearMaxDist = 5800;
#else
	m_fDisappearDist = 5000.0f;
#endif
}

#if defined( L4D2 )
void C_Func_LOD::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	PhysOnDataChanged( this, type == DATA_UPDATE_CREATED );
	SetRenderAlphaFadeDistances( 1.0f, static_cast< float >( m_nDisappearMinDist ), static_cast< float >( m_nDisappearMaxDist ) );
}
#else
//-----------------------------------------------------------------------------
// Purpose: Calculate a fade.
//-----------------------------------------------------------------------------
unsigned char C_Func_LOD::GetClientSideFade()
{
	return UTIL_ComputeEntityFade( this, m_fDisappearDist, m_fDisappearDist + lod_TransitionDist.GetFloat(), 1.0f );
}
#endif


