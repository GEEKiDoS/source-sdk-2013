//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Vertex buffer allocation tracker for the DX12 shader API.
//
//=============================================================================//

#ifndef VBALLOCTRACKER_DX12_H
#define VBALLOCTRACKER_DX12_H
#pragma once

#include "materialsystem/ivballoctracker.h"
#include "tier0/threadtools.h"
#include "tier1/utlhashtable.h"

namespace shaderapidx12
{
class CVBAllocTrackerDX12 final : public IVBAllocTracker
{
public:
	void CountVB( void *pBuffer, bool bDynamic, int nBufferSize, int nVertexSize, VertexFormat_t fmt ) override;
	void UnCountVB( void *pBuffer ) override;
	bool TrackMeshAllocations( const char *pszAllocatorName ) override;

private:
	struct Record
	{
		bool dynamic;
		int bytes, vertexSize;
		VertexFormat_t format;
	};

	CThreadFastMutex m_Mutex;
	CUtlHashtable<void *, Record> m_Records;
	bool m_bTracking = false;
};

extern CVBAllocTrackerDX12 *g_pVBAllocTrackerDX12;
} // namespace shaderapidx12

#endif // VBALLOCTRACKER_DX12_H
