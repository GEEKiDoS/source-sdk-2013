//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Selection-mode hit records for the DX12 shader API. Record semantics
//          follow shaderapidx8.cpp and meshdx8.cpp.
//
//=============================================================================//

#include "mesh_dx12.h"
#include "mathlib/vector4d.h"
#include "tier1/strtools.h"

namespace shaderapidx12
{
static_assert( sizeof( VertexFormat_t ) == sizeof( uint64_t ), "DX12 vertex formats must remain 64-bit" );

//-----------------------------------------------------------------------------
// Purpose: Sets the caller-owned record buffer; ignored while selection is on
//-----------------------------------------------------------------------------
void CSelectionStateDX12::SetBuffer( unsigned int *pBuffer, int nWords )
{
	if ( m_bEnabled )
		return;
	m_pBuffer = pBuffer;
	m_nCapacity = pBuffer && nWords > 0 ? static_cast<size_t>( nWords ) : 0;
	m_nUsed = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Toggles selection mode; returns the hit count (-1 on overflow)
//-----------------------------------------------------------------------------
int CSelectionStateDX12::SetMode( bool bEnabled )
{
	if ( m_bEnabled )
		Flush();
	const int nResult = m_bOverflow ? -1 : m_nHits;
	m_bEnabled = bEnabled;
	m_nUsed = 0;
	m_nHits = 0;
	m_bOverflow = false;
	m_flMinimum = FLT_MAX;
	m_flMaximum = 0.f;
	return nResult;
}

//-----------------------------------------------------------------------------
// Purpose: Writes the accumulated hit record for the current name stack
//-----------------------------------------------------------------------------
void CSelectionStateDX12::Flush()
{
	if ( m_bEnabled && !m_Names.IsEmpty() && m_flMinimum != FLT_MAX )
	{
		const size_t nNameCount = static_cast<size_t>( m_Names.Count() );
		if ( nNameCount > m_nCapacity || m_nCapacity - nNameCount < 3 || m_nUsed > m_nCapacity - nNameCount - 3 )
			m_bOverflow = true;
		else
		{
			m_pBuffer[m_nUsed++] = static_cast<unsigned int>( nNameCount );
			const double flRange = static_cast<double>( UINT_MAX );
			m_pBuffer[m_nUsed++] = static_cast<unsigned int>( 0.5 + m_flMinimum * flRange );
			m_pBuffer[m_nUsed++] = static_cast<unsigned int>( 0.5 + m_flMaximum * flRange );
			memcpy( m_pBuffer + m_nUsed, m_Names.Base(), nNameCount * sizeof( unsigned int ) );
			m_nUsed += nNameCount;
			++m_nHits;
		}
	}
	m_flMinimum = FLT_MAX;
	m_flMaximum = 0.f;
}

//-----------------------------------------------------------------------------
// Purpose: Name stack operations; each flushes the pending record first
//-----------------------------------------------------------------------------
void CSelectionStateDX12::ClearNames()
{
	Flush();
	m_Names.RemoveAll();
}

void CSelectionStateDX12::LoadName( unsigned int nName )
{
	if ( !m_bEnabled )
		return;
	Flush();
	if ( !m_Names.IsEmpty() )
		m_Names.Tail() = nName;
}

void CSelectionStateDX12::PushName( unsigned int nName )
{
	if ( m_bEnabled )
	{
		Flush();
		m_Names.AddToTail( nName );
	}
}

void CSelectionStateDX12::PopName()
{
	if ( m_bEnabled )
	{
		Flush();
		if ( !m_Names.IsEmpty() )
			m_Names.RemoveMultipleFromTail( 1 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Widens the pending record's depth range
//-----------------------------------------------------------------------------
void CSelectionStateDX12::Record( float flMinimum, float flMaximum )
{
	if ( !m_bEnabled || !IsFinite( flMinimum ) || !IsFinite( flMaximum ) )
		return;
	const float flClampedMinimum = clamp( flMinimum, 0.f, 1.f );
	const float flClampedMaximum = clamp( flMaximum, 0.f, 1.f );
	m_flMinimum = MIN( flClampedMinimum, m_flMinimum );
	m_flMaximum = MAX( flClampedMaximum, m_flMaximum );
}

//-----------------------------------------------------------------------------
// Purpose: Signed distance of a clip-space vertex to one of the six frustum planes
//-----------------------------------------------------------------------------
static float PlaneDistance( const Vector4D &vertex, int nPlane )
{
	switch ( nPlane )
	{
	case 0:
		return vertex[0] + vertex[3];
	case 1:
		return vertex[3] - vertex[0];
	case 2:
		return vertex[1] + vertex[3];
	case 3:
		return vertex[3] - vertex[1];
	case 4:
		return vertex[2];
	default:
		return vertex[3] - vertex[2];
	}
}

//-----------------------------------------------------------------------------
// Purpose: Clips each triangle against the frustum and records its depth range
//-----------------------------------------------------------------------------
void TestSelectionDX12( const CVertexBufferDX12 &vertices, const CIndexBufferDX12 &indices,
    MaterialPrimitiveType_t primitive, int nFirstIndex, int nIndexCount,
    const VMatrix &modelToClip, bool bCull, bool bFrontCounterClockwise,
    CSelectionStateDX12 &selection, size_t nVertexOffset, size_t nIndexOffset )
{
	if ( !selection.Enabled() || ( primitive != MATERIAL_TRIANGLES && primitive != MATERIAL_TRIANGLE_STRIP ) ||
	    nFirstIndex < 0 || nIndexCount < 3 || !vertices.Stride() || nVertexOffset > vertices.Bytes().size() ||
	    nIndexOffset > indices.Bytes().size() )
		return;
	const size_t nIndexSize = indices.IndexSize();
	const size_t nAvailable = ( indices.Bytes().size() - nIndexOffset ) / nIndexSize;
	if ( static_cast<size_t>( nFirstIndex ) > nAvailable || static_cast<size_t>( nIndexCount ) > nAvailable - nFirstIndex )
		return;
	const VertexLayoutDX12 layout = ComputeVertexLayoutDX12( vertices.GetVertexFormat() );
	const VertexInputDX12 *pPosition = nullptr;
	for ( uint32_t i = 0; i < layout.inputCount; ++i )
		if ( V_strcmp( layout.inputs[i].semantic, "POSITION" ) == 0 && layout.inputs[i].semanticIndex == 0 )
			pPosition = &layout.inputs[i];
	if ( !pPosition || pPosition->format != DXGI_FORMAT_R32G32B32_FLOAT )
		return;
	const int nTriangles = primitive == MATERIAL_TRIANGLES ? nIndexCount / 3 : nIndexCount - 2;
	for ( int nTriangle = 0; nTriangle < nTriangles; ++nTriangle )
	{
		Vector4D polygon[16], temporary[16];
		bool bValid = true;
		for ( int nCorner = 0; nCorner < 3; ++nCorner )
		{
			const int nLocal = primitive == MATERIAL_TRIANGLES ? nTriangle * 3 + nCorner : nTriangle + nCorner;
			const unsigned char *pIndex = indices.Bytes().data() + nIndexOffset + static_cast<size_t>( nFirstIndex + nLocal ) * nIndexSize;
			uint32_t nVertexIndex = 0;
			if ( nIndexSize == 4 )
				memcpy( &nVertexIndex, pIndex, 4 );
			else
			{
				uint16_t nShortIndex;
				memcpy( &nShortIndex, pIndex, 2 );
				nVertexIndex = nShortIndex;
			}
			const size_t nOffset = nVertexOffset + static_cast<size_t>( nVertexIndex ) * vertices.Stride() + pPosition->byteOffset;
			if ( nOffset > vertices.Bytes().size() || vertices.Bytes().size() - nOffset < 3 * sizeof( float ) )
			{
				bValid = false;
				break;
			}
			float p[3];
			memcpy( p, vertices.Bytes().data() + nOffset, sizeof( p ) );
			for ( int nRow = 0; nRow < 4; ++nRow )
			{
				polygon[nCorner][nRow] = modelToClip[nRow][0] * p[0] + modelToClip[nRow][1] * p[1] + modelToClip[nRow][2] * p[2] + modelToClip[nRow][3];
				bValid = bValid && IsFinite( polygon[nCorner][nRow] );
			}
		}
		if ( !bValid )
			continue;
		if ( primitive == MATERIAL_TRIANGLE_STRIP && ( nTriangle & 1 ) )
			V_swap( polygon[0], polygon[1] );
		int nCount = 3;
		for ( int nPlane = 0; nPlane < 6 && nCount >= 3; ++nPlane )
		{
			int nOutput = 0;
			Vector4D previous = polygon[nCount - 1];
			float flPreviousDistance = PlaneDistance( previous, nPlane );
			for ( int i = 0; i < nCount; ++i )
			{
				const Vector4D &current = polygon[i];
				const float flDistance = PlaneDistance( current, nPlane );
				if ( ( flDistance >= 0 ) != ( flPreviousDistance >= 0 ) )
				{
					const float t = flPreviousDistance / ( flPreviousDistance - flDistance );
					for ( int nComponent = 0; nComponent < 4; ++nComponent )
						temporary[nOutput][nComponent] = previous[nComponent] + t * ( current[nComponent] - previous[nComponent] );
					++nOutput;
				}
				if ( flDistance >= 0 )
					temporary[nOutput++] = current;
				previous = current;
				flPreviousDistance = flDistance;
			}
			nCount = nOutput;
			for ( int i = 0; i < nCount; ++i )
				polygon[i] = temporary[i];
		}
		if ( nCount < 3 )
			continue;
		float flMinimum = 1.f, flMaximum = 0.f;
		for ( int i = 0; i < nCount; ++i )
		{
			if ( !( polygon[i][3] > 0.f ) )
			{
				bValid = false;
				break;
			}
			const float flInverse = 1.f / polygon[i][3];
			for ( int nComponent = 0; nComponent < 3; ++nComponent )
				polygon[i][nComponent] *= flInverse;
			flMinimum = MIN( polygon[i][2], flMinimum );
			flMaximum = MAX( polygon[i][2], flMaximum );
		}
		if ( !bValid )
			continue;
		float flArea = 0.f;
		for ( int i = 0; i < nCount; ++i )
		{
			const Vector4D &a = polygon[i];
			const Vector4D &b = polygon[( i + 1 ) % nCount];
			flArea += a[0] * b[1] - a[1] * b[0];
		}
		if ( flArea == 0.f || ( bCull && ( bFrontCounterClockwise ? flArea >= 0.f : flArea <= 0.f ) ) )
			continue;
		selection.Record( flMinimum, flMaximum );
	}
}
} // namespace shaderapidx12
