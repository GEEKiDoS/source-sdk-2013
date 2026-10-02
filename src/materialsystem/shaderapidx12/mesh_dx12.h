//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Selection-mode hit records for the DX12 shader API.
//
//=============================================================================//

#ifndef MESH_DX12_H
#define MESH_DX12_H
#pragma once

#include "resources_dx12.h"
#include "mathlib/vmatrix.h"
#include "tier1/utlvector.h"

namespace shaderapidx12
{
// Source selection records are count, minimum depth, maximum depth, then names.
// Accumulate geometry under one name stack until a name/state transition.
class CSelectionStateDX12
{
public:
	void SetBuffer( unsigned int *pBuffer, int nWords );
	int SetMode( bool bEnabled );

	bool Enabled() const { return m_bEnabled; }

	void ClearNames();
	void LoadName( unsigned int nName );
	void PushName( unsigned int nName );
	void PopName();
	void Record( float flMinimum, float flMaximum );
	void Flush();

private:
	unsigned int *m_pBuffer = nullptr;
	size_t m_nCapacity = 0, m_nUsed = 0;
	CUtlVector<unsigned int> m_Names;
	int m_nHits = 0;
	bool m_bEnabled = false, m_bOverflow = false;
	float m_flMinimum = FLT_MAX, m_flMaximum = 0.f;
};

// Like the reference temp-mesh path, selection tests CPU positions rather than
// vertex-shader deformation. Homogeneous clipping also handles negative w.
void TestSelectionDX12( const CVertexBufferDX12 &vertices, const CIndexBufferDX12 &indices,
    MaterialPrimitiveType_t primitive, int nFirstIndex, int nIndexCount,
    const VMatrix &modelToClip, bool bCull, bool bFrontCounterClockwise,
    CSelectionStateDX12 &selection, size_t nVertexOffset = 0, size_t nIndexOffset = 0 );
} // namespace shaderapidx12

#endif // MESH_DX12_H
