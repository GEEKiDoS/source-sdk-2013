//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Vertex buffer allocation tracker for the DX12 shader API.
//
//=============================================================================//

#include "vballoctracker_dx12.h"

namespace shaderapidx12
{
CVBAllocTrackerDX12 *g_pVBAllocTrackerDX12 = nullptr;

//-----------------------------------------------------------------------------
// Purpose: Records (or replaces) the allocation for pBuffer
//-----------------------------------------------------------------------------
void CVBAllocTrackerDX12::CountVB( void *pBuffer, bool bDynamic, int nBufferSize, int nVertexSize, VertexFormat_t fmt )
{
	if ( !pBuffer )
		return;
	AUTO_LOCK( m_Mutex );
	m_Records[m_Records.Insert( pBuffer )] = { bDynamic, nBufferSize, nVertexSize, fmt };
}

//-----------------------------------------------------------------------------
// Purpose: Forgets the allocation for pBuffer
//-----------------------------------------------------------------------------
void CVBAllocTrackerDX12::UnCountVB( void *pBuffer )
{
	if ( !pBuffer )
		return;
	AUTO_LOCK( m_Mutex );
	m_Records.Remove( pBuffer );
}

//-----------------------------------------------------------------------------
// Purpose: Enables tracking; returns the previous state
//-----------------------------------------------------------------------------
bool CVBAllocTrackerDX12::TrackMeshAllocations( const char * )
{
	AUTO_LOCK( m_Mutex );
	const bool bOld = m_bTracking;
	m_bTracking = true;
	return bOld;
}
} // namespace shaderapidx12
