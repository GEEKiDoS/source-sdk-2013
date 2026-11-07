//========= Copyright Valve Corporation, All rights reserved. ============//
//
// An entity that allows level designer control over the fog parameters.
//
//=============================================================================

#include "cbase.h"
#include "c_env_fog_controller.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( FogController, DT_FogController )

//-----------------------------------------------------------------------------
// Datatable
//-----------------------------------------------------------------------------
BEGIN_NETWORK_TABLE_NOBASE( CFogController, DT_FogController )
	// fog data
	RecvPropInt( RECVINFO( m_fog.enable ) ),
	RecvPropInt( RECVINFO( m_fog.blend ) ),
#if !defined( L4D2 )
	RecvPropInt( RECVINFO( m_fog.radial ) ),
#endif
	RecvPropVector( RECVINFO( m_fog.dirPrimary ) ),
	RecvPropInt( RECVINFO( m_fog.colorPrimary ) ),
	RecvPropInt( RECVINFO( m_fog.colorSecondary ) ),
	RecvPropFloat( RECVINFO( m_fog.start ) ),
	RecvPropFloat( RECVINFO( m_fog.end ) ),
	RecvPropFloat( RECVINFO( m_fog.farz ) ),
	RecvPropFloat( RECVINFO( m_fog.maxdensity ) ),

	RecvPropInt( RECVINFO( m_fog.colorPrimaryLerpTo ) ),
	RecvPropInt( RECVINFO( m_fog.colorSecondaryLerpTo ) ),
	RecvPropFloat( RECVINFO( m_fog.startLerpTo ) ),
	RecvPropFloat( RECVINFO( m_fog.endLerpTo ) ),
#if defined( L4D2 )
	RecvPropFloat( RECVINFO( m_fog.maxdensityLerpTo ) ),
#endif
	RecvPropFloat( RECVINFO( m_fog.lerptime ) ),
	RecvPropFloat( RECVINFO( m_fog.duration ) ),
#if defined( L4D2 )
	RecvPropFloat( RECVINFO( m_fog.HDRColorScale ) ),
	RecvPropFloat( RECVINFO( m_fog.skyboxFogFactor ) ),
	RecvPropFloat( RECVINFO( m_fog.skyboxFogFactorLerpTo ) ),
#endif
END_NETWORK_TABLE()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_FogController::C_FogController()
{
	// Make sure that old maps without fog fields don't get wacked out fog values.
	m_fog.enable = false;
	m_fog.maxdensity = 1.0f;
#if defined( L4D2 )
	m_fog.HDRColorScale = 1.0f;
#endif
	m_fog.radial = false;
}
