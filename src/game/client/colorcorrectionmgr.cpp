//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose : Singleton manager for color correction on the client
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "tier0/vprof.h"
#include "colorcorrectionmgr.h"
#if defined( L4D2 )
#include "l4d2/clientmode_terror.h"
#include "c_baseplayer.h"
#endif


//------------------------------------------------------------------------------
// Singleton access
//------------------------------------------------------------------------------
static CColorCorrectionMgr s_ColorCorrectionMgr;
CColorCorrectionMgr *g_pColorCorrectionMgr = &s_ColorCorrectionMgr;


//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------
CColorCorrectionMgr::CColorCorrectionMgr()
{
	m_nActiveWeightCount = 0;
#if defined( L4D2 )
	m_bHaveExclusiveWeight = false;
	m_flExclusiveWeight = 0.0f;
#endif
}


//------------------------------------------------------------------------------
// Creates, destroys color corrections
//------------------------------------------------------------------------------
ClientCCHandle_t CColorCorrectionMgr::AddColorCorrection( const char *pName, const char *pFileName )
{
	if ( !pFileName )
	{
		pFileName = pName;
	}

	CMatRenderContextPtr pRenderContext( g_pMaterialSystem );
	ColorCorrectionHandle_t ccHandle = pRenderContext->AddLookup( pName );
	if ( ccHandle )
	{
		pRenderContext->LockLookup( ccHandle );
		pRenderContext->LoadLookup( ccHandle, pFileName );
		pRenderContext->UnlockLookup( ccHandle );
	}
	else
	{
		Warning("Cannot find color correction lookup file: '%s'\n", pFileName );
	}

	return (ClientCCHandle_t)ccHandle;
}

void CColorCorrectionMgr::RemoveColorCorrection( ClientCCHandle_t h )
{
	if ( h != INVALID_CLIENT_CCHANDLE )
	{
		CMatRenderContextPtr pRenderContext( g_pMaterialSystem );
		ColorCorrectionHandle_t ccHandle = (ColorCorrectionHandle_t)h;
		pRenderContext->RemoveLookup( ccHandle );
	}
}


//------------------------------------------------------------------------------
// Modify color correction weights
//------------------------------------------------------------------------------
#if defined( L4D2 )
void UpdateTerrorColorCorrectionEntities( C_BasePlayer *player, float scale );
void UpdateTerrorColorCorrectionVolumes( C_BasePlayer *player, float scale );

// NativeDBA50 queues EVERY publication, including repeated handles and zero
// exclusive weights. Last exclusive wins even when zero; do not repair that bug.
void CColorCorrectionMgr::SetColorCorrectionWeight( ClientCCHandle_t handle, float weight, bool exclusive )
{
	if ( handle == INVALID_CLIENT_CCHANDLE ) return;
	Weight_t value = { handle, weight, exclusive };
	m_Weights.AddToTail( value );
	if ( exclusive )
	{
		if ( m_bHaveExclusiveWeight && weight != 0.0f )
			DevWarning( "Found multiple active color_correction entities with exclusive setting enabled. This is invalid.\n" );
		m_bHaveExclusiveWeight = true;
		m_flExclusiveWeight = weight;
	}
}

void CColorCorrectionMgr::FlushColorCorrectionWeights()
{
	//DB3C0: native count counts nonzero PUBLICATIONS, not unique handles.
	CMatRenderContextPtr context( g_pMaterialSystem );
	FOR_EACH_VEC( m_Weights, i )
	{
		const Weight_t &entry = m_Weights[i];
		const float weight = entry.exclusive ? entry.weight : ( 1.0f - m_flExclusiveWeight ) * entry.weight;
		context->SetLookupWeight( (ColorCorrectionHandle_t)entry.handle, weight );
		if ( weight != 0.0f ) ++m_nActiveWeightCount;
	}
	m_Weights.RemoveAll();
}

void CColorCorrectionMgr::UpdateColorCorrection()
{
	//DB4F0 exact phase. Called INSTEAD OF Reset at cdll_client_int.cpp2240,
	//not after SimulateEntities and not in ClientMode::Update/HUD paint.
	ResetColorCorrectionWeights();
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	ClientModeTerrorNormal *mode = static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() );
	if ( player && mode )
	{
		mode->OnColorCorrectionWeightsReset();
		const float scale = (float)mode->GetModeSaturation();
		UpdateTerrorColorCorrectionEntities( player, scale );
		UpdateTerrorColorCorrectionVolumes( player, scale );
		FlushColorCorrectionWeights();
	}
}
#else
void CColorCorrectionMgr::SetColorCorrectionWeight( ClientCCHandle_t h, float flWeight )
{
	if ( h != INVALID_CLIENT_CCHANDLE )
	{
		CMatRenderContextPtr pRenderContext( g_pMaterialSystem );
		ColorCorrectionHandle_t ccHandle = (ColorCorrectionHandle_t)h;
		pRenderContext->SetLookupWeight( ccHandle, flWeight );

		// FIXME: NOTE! This doesn't work if the same handle has
		// its weight set twice with no intervening calls to ResetColorCorrectionWeights
		// which, at the moment, is true
		if ( flWeight != 0.0f )
		{
			++m_nActiveWeightCount;
		}
	}
}
#endif

void CColorCorrectionMgr::ResetColorCorrectionWeights()
{
	VPROF_("ResetColorCorrectionWeights", 2, VPROF_BUDGETGROUP_OTHER_UNACCOUNTED, false, 0);
#if !defined( L4D2 )
	// FIXME: Where should I put this? It needs to happen prior to SimulateEntities()
	// which is where the client thinks for c_colorcorrection + c_colorcorrectionvolumes
	// update the color correction weights.
#endif
	CMatRenderContextPtr pRenderContext( g_pMaterialSystem );
	pRenderContext->ResetLookupWeights();
	m_nActiveWeightCount = 0;
#if defined( L4D2 )
	m_bHaveExclusiveWeight = false;
	m_flExclusiveWeight = 0.0f;
	m_Weights.RemoveAll();
#endif
}

void CColorCorrectionMgr::SetResetable( ClientCCHandle_t h, bool bResetable )
{
	// NOTE: Setting stuff to be not resettable doesn't work when in queued mode
	// because the logic that sets m_nActiveWeightCount to 0 in ResetColorCorrectionWeights
	// is no longer valid when stuff is not resettable.
	Assert( bResetable || !g_pMaterialSystem->GetThreadMode() == MATERIAL_SINGLE_THREADED );
	if ( h != INVALID_CLIENT_CCHANDLE )
	{
		CMatRenderContextPtr pRenderContext( g_pMaterialSystem );
		ColorCorrectionHandle_t ccHandle = (ColorCorrectionHandle_t)h;
		pRenderContext->SetResetable( ccHandle, bResetable );
	}
}


//------------------------------------------------------------------------------
// Is color correction active?
//------------------------------------------------------------------------------
bool CColorCorrectionMgr::HasNonZeroColorCorrectionWeights() const
{
	return ( m_nActiveWeightCount != 0 );
}
