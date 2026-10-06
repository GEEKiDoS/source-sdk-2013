//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 shader, vertex, index buffers and meshes backed by CPU byte storage.
//
//=============================================================================//

#include "resources_dx12.h"
#include "shaderdevice_dx12.h"
#include "shaderapi/ishaderutil.h"
#include "tier0/dbg.h"
#include <atomic>

namespace shaderapidx12
{
static std::atomic<uint64_t> s_NextGeometryIdentity{ 1 };

//-----------------------------------------------------------------------------
// Purpose: Grows the byte storage geometrically and zero-fills the new tail
//-----------------------------------------------------------------------------
static bool ResizeBytes( CUtlMemoryConservative<unsigned char> &bytes, size_t &nCurrentSize, size_t nRequestedSize )
{
	if ( nRequestedSize > nCurrentSize )
	{
		const size_t nCapacity = bytes.AllocSize();
		if ( nRequestedSize > nCapacity )
		{
			size_t nNewCapacity = nCapacity ? nCapacity : 64;
			const size_t nMaxSize = static_cast<size_t>( -1 );
			while ( nNewCapacity < nRequestedSize )
			{
				if ( nNewCapacity > nMaxSize / 2 )
				{
					nNewCapacity = nRequestedSize;
					break;
				}
				nNewCapacity *= 2;
			}
			bytes.ReAlloc( nNewCapacity );
			if ( !bytes.Base() )
				return false;
		}
		memset( bytes.Base() + nCurrentSize, 0, nRequestedSize - nCurrentSize );
	}
	nCurrentSize = nRequestedSize;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Copies the shader blob into owned storage
//-----------------------------------------------------------------------------
CShaderBufferDX12::CShaderBufferDX12( const void *pData, size_t nSize )
{
	if ( pData && nSize )
	{
		m_Bytes.ReAlloc( nSize );
		if ( !m_Bytes.Base() )
			return;
		memcpy( m_Bytes.Base(), pData, nSize );
		m_nByteSize = nSize;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Static buffers allocate their whole storage up front
//-----------------------------------------------------------------------------
CVertexBufferDX12::CVertexBufferDX12( VertexFormat_t format, int nCount, bool bDynamic )
    : m_Format( format ), m_nVertexCount( MAX( 0, nCount ) ), m_bDynamic( bDynamic )
{
	m_nIdentity = s_NextGeometryIdentity.fetch_add( 1 );
	m_Layout = ComputeVertexLayoutDX12( format );
	m_nStride = m_Layout.valid ? m_Layout.stride : 0;
	if ( !m_bDynamic && m_nStride && m_nVertexCount > 0 &&
	    !ResizeBytes( m_Bytes, m_nByteSize, static_cast<size_t>( m_nStride ) * static_cast<size_t>( m_nVertexCount ) ) )
		m_nVertexCount = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Bumps the content version; zero is reserved for "never uploaded"
//-----------------------------------------------------------------------------
void CVertexBufferDX12::MarkModified()
{
	++m_nContentVersion;
	if ( !m_nContentVersion )
		m_nContentVersion = 1;
}

//-----------------------------------------------------------------------------
// Purpose: Grows the buffer to hold at least nCount vertices
//-----------------------------------------------------------------------------
bool CVertexBufferDX12::EnsureCapacity( int nCount )
{
	if ( nCount < 0 || !m_nStride || static_cast<size_t>( nCount ) > static_cast<size_t>( -1 ) / m_nStride )
		return false;
	if ( nCount <= m_nVertexCount )
		return true;
	if ( !ResizeBytes( m_Bytes, m_nByteSize, static_cast<size_t>( nCount ) * m_nStride ) )
		return false;
	m_nVertexCount = nCount;
	MarkModified();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Reinterprets the buffer with a new vertex format
//-----------------------------------------------------------------------------
void CVertexBufferDX12::BeginCastBuffer( VertexFormat_t format )
{
	m_Format = format;
	m_Layout = ComputeVertexLayoutDX12( format );
	m_nStride = m_Layout.valid ? m_Layout.stride : 0;
	m_nWritten = 0;
	m_nByteSize = 0;
	if ( !m_bDynamic && m_nStride && m_nVertexCount > 0 &&
	    !ResizeBytes( m_Bytes, m_nByteSize, static_cast<size_t>( m_nStride ) * static_cast<size_t>( m_nVertexCount ) ) )
		m_nVertexCount = 0;
	MarkModified();
}

void CVertexBufferDX12::EndCastBuffer()
{
}

int CVertexBufferDX12::GetRoomRemaining() const
{
	return MAX( 0, m_nVertexCount - m_nWritten );
}

//-----------------------------------------------------------------------------
// Purpose: Dynamic-style lock: append after the written range or restart at 0
//-----------------------------------------------------------------------------
bool CVertexBufferDX12::Lock( int nVertexCount, bool bAppend, VertexDesc_t &desc )
{
	if ( nVertexCount < 0 || nVertexCount > ( bAppend ? GetRoomRemaining() : m_nVertexCount ) )
		return false;
	const int nFirst = bAppend ? m_nWritten : 0;
	if ( !bAppend )
		m_nWritten = 0;
	return LockRange( nFirst, nVertexCount, desc );
}

//-----------------------------------------------------------------------------
// Purpose: Static-mesh lock; always starts at vertex 0
//-----------------------------------------------------------------------------
bool CVertexBufferDX12::LockStatic( int nVertexCount, VertexDesc_t &desc )
{
	m_nStaticLockCount = 0;
	if ( m_bDynamic || nVertexCount < 0 || nVertexCount > m_nVertexCount || !LockRange( 0, nVertexCount, desc ) )
		return false;
	m_nStaticLockCount = nVertexCount;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Fills desc for vertices [nFirst, nFirst + nVertexCount)
//-----------------------------------------------------------------------------
bool CVertexBufferDX12::LockRange( int nFirst, int nVertexCount, VertexDesc_t &desc )
{
	const size_t nFirstSize = static_cast<size_t>( nFirst );
	const size_t nCountSize = static_cast<size_t>( nVertexCount );
	if ( !m_nStride || nFirstSize > static_cast<size_t>( -1 ) / m_nStride || nCountSize > static_cast<size_t>( -1 ) / m_nStride - nFirstSize )
		return false;
	const size_t nRequired = ( nFirstSize + nCountSize ) * m_nStride;
	if ( ( m_bDynamic && m_nByteSize < nRequired && !ResizeBytes( m_Bytes, m_nByteSize, nRequired ) ) ||
	    ( !m_bDynamic && m_nByteSize < nRequired ) )
		return false;
	unsigned char *pData = m_nByteSize ? m_Bytes.Base() + nFirstSize * m_nStride : nullptr;
	VertexLayoutDX12 layout = ComputeVertexLayoutDX12( m_Format, pData, &desc );
	if ( !layout.valid )
		return false;
	desc.m_nFirstVertex = nFirst;
	desc.m_nOffset = static_cast<unsigned int>( nFirstSize * m_nStride );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Extends the written range past the locked vertices
//-----------------------------------------------------------------------------
void CVertexBufferDX12::Unlock( int nVertexCount, VertexDesc_t &desc )
{
	if ( nVertexCount > 0 )
	{
		m_nWritten = MIN( m_nVertexCount, MAX( m_nWritten, static_cast<int>( desc.m_nFirstVertex ) ) + nVertexCount );
		MarkModified();
	}
}

//-----------------------------------------------------------------------------
// Purpose: A static unlock defines the whole locked range
//-----------------------------------------------------------------------------
void CVertexBufferDX12::UnlockStatic()
{
	if ( m_nStaticLockCount > 0 )
	{
		m_nWritten = MAX( m_nWritten, MIN( m_nVertexCount, m_nStaticLockCount ) );
		MarkModified();
	}
	m_nStaticLockCount = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Debug output and validation
//-----------------------------------------------------------------------------
void CVertexBufferDX12::Spew( int nVertexCount, const VertexDesc_t &desc )
{
	Msg( "ShaderAPIDX12: vertex buffer %d vertices, stride %u, offset %u\n", nVertexCount, m_nStride, desc.m_nOffset );
}

void CVertexBufferDX12::ValidateData( int nVertexCount, const VertexDesc_t &desc )
{
	if ( nVertexCount < 0 || nVertexCount > m_nVertexCount || desc.m_ActualVertexSize != static_cast<int>( m_nStride ) )
		Warning( "ShaderAPIDX12: invalid vertex buffer range or stride\n" );
}

//-----------------------------------------------------------------------------
// Purpose: Static buffers allocate their whole storage up front
//-----------------------------------------------------------------------------
CIndexBufferDX12::CIndexBufferDX12( MaterialIndexFormat_t format, int nCount, bool bDynamic )
    : m_Format( format ), m_nIndexCount( MAX( 0, nCount ) ), m_bDynamic( bDynamic )
{
	m_nIdentity = s_NextGeometryIdentity.fetch_add( 1 );
	m_nIndexSize = format == MATERIAL_INDEX_FORMAT_32BIT ? 4u : 2u;
	if ( !m_bDynamic && !ResizeBytes( m_Bytes, m_nByteSize, static_cast<size_t>( m_nIndexSize ) * static_cast<size_t>( m_nIndexCount ) ) )
		m_nIndexCount = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Grows the buffer to hold at least nCount indices
//-----------------------------------------------------------------------------
bool CIndexBufferDX12::EnsureCapacity( int nCount )
{
	if ( nCount < 0 || static_cast<size_t>( nCount ) > static_cast<size_t>( -1 ) / m_nIndexSize )
		return false;
	if ( nCount <= m_nIndexCount )
		return true;
	if ( !ResizeBytes( m_Bytes, m_nByteSize, static_cast<size_t>( nCount ) * m_nIndexSize ) )
		return false;
	m_nIndexCount = nCount;
	MarkModified();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Bumps the content version; zero is reserved for "never uploaded"
//-----------------------------------------------------------------------------
void CIndexBufferDX12::MarkModified()
{
	++m_nContentVersion;
	if ( !m_nContentVersion )
		m_nContentVersion = 1;
}

//-----------------------------------------------------------------------------
// Purpose: Reinterprets the buffer with a new index format
//-----------------------------------------------------------------------------
void CIndexBufferDX12::BeginCastBuffer( MaterialIndexFormat_t format )
{
	m_Format = format;
	m_nIndexSize = format == MATERIAL_INDEX_FORMAT_32BIT ? 4u : 2u;
	m_nWritten = 0;
	m_nByteSize = 0;
	if ( !m_bDynamic && !ResizeBytes( m_Bytes, m_nByteSize, static_cast<size_t>( m_nIndexSize ) * static_cast<size_t>( m_nIndexCount ) ) )
		m_nIndexCount = 0;
	MarkModified();
}

void CIndexBufferDX12::EndCastBuffer()
{
}

int CIndexBufferDX12::GetRoomRemaining() const
{
	return MAX( 0, m_nIndexCount - m_nWritten );
}

//-----------------------------------------------------------------------------
// Purpose: Dynamic-style lock: append after the written range or restart at 0
//-----------------------------------------------------------------------------
bool CIndexBufferDX12::Lock( int nMaxIndexCount, bool bAppend, IndexDesc_t &desc )
{
	if ( nMaxIndexCount < 0 || nMaxIndexCount > ( bAppend ? GetRoomRemaining() : m_nIndexCount ) )
		return false;
	const int nFirst = bAppend ? m_nWritten : 0;
	if ( !bAppend )
		m_nWritten = 0;
	const size_t nFirstSize = static_cast<size_t>( nFirst );
	const size_t nCountSize = static_cast<size_t>( nMaxIndexCount );
	if ( nFirstSize > static_cast<size_t>( -1 ) / m_nIndexSize || nCountSize > static_cast<size_t>( -1 ) / m_nIndexSize - nFirstSize )
		return false;
	const size_t nRequired = ( nFirstSize + nCountSize ) * m_nIndexSize;
	if ( ( m_bDynamic && m_nByteSize < nRequired && !ResizeBytes( m_Bytes, m_nByteSize, nRequired ) ) ||
	    ( !m_bDynamic && m_nByteSize < nRequired ) )
		return false;
	desc.m_pIndices = m_nByteSize ? reinterpret_cast<unsigned short *>( m_Bytes.Base() + nFirstSize * m_nIndexSize ) : nullptr;
	desc.m_nOffset = static_cast<unsigned int>( nFirstSize * m_nIndexSize );
	desc.m_nFirstIndex = static_cast<unsigned int>( nFirst );
	desc.m_nIndexSize = static_cast<unsigned char>( m_nIndexSize / sizeof( unsigned short ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Extends the written range past the locked indices
//-----------------------------------------------------------------------------
void CIndexBufferDX12::Unlock( int nWrittenIndexCount, IndexDesc_t &desc )
{
	if ( nWrittenIndexCount > 0 )
	{
		m_nWritten = MIN( m_nIndexCount, MAX( m_nWritten, static_cast<int>( desc.m_nFirstIndex ) ) + nWrittenIndexCount );
		MarkModified();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Exposes an existing index range; desc is zeroed on failure
//-----------------------------------------------------------------------------
void CIndexBufferDX12::ModifyBegin( bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t &desc )
{
	m_bModifyingWritable = false;
	if ( nFirstIndex < 0 || nIndexCount < 0 || nFirstIndex > m_nIndexCount || nIndexCount > m_nIndexCount - nFirstIndex )
	{
		memset( &desc, 0, sizeof( desc ) );
		return;
	}
	const size_t nFirstSize = static_cast<size_t>( nFirstIndex );
	const size_t nCountSize = static_cast<size_t>( nIndexCount );
	if ( nFirstSize > static_cast<size_t>( -1 ) / m_nIndexSize || nCountSize > static_cast<size_t>( -1 ) / m_nIndexSize - nFirstSize )
	{
		memset( &desc, 0, sizeof( desc ) );
		return;
	}
	const size_t nRequired = ( nFirstSize + nCountSize ) * m_nIndexSize;
	if ( nRequired > m_nByteSize )
	{
		if ( bReadOnly || !m_bDynamic || !ResizeBytes( m_Bytes, m_nByteSize, nRequired ) )
		{
			memset( &desc, 0, sizeof( desc ) );
			return;
		}
	}
	desc.m_pIndices = m_nByteSize ? reinterpret_cast<unsigned short *>( m_Bytes.Base() + nFirstSize * m_nIndexSize ) : nullptr;
	desc.m_nOffset = static_cast<unsigned int>( nFirstSize * m_nIndexSize );
	desc.m_nFirstIndex = static_cast<unsigned int>( nFirstIndex );
	desc.m_nIndexSize = static_cast<unsigned char>( m_nIndexSize / sizeof( unsigned short ) );
	if ( !bReadOnly && nIndexCount > 0 )
	{
		m_nWritten = MAX( m_nWritten, nFirstIndex + nIndexCount );
		m_bModifyingWritable = true;
	}
}

void CIndexBufferDX12::ModifyEnd( IndexDesc_t & )
{
	if ( m_bModifyingWritable )
		MarkModified();
	m_bModifyingWritable = false;
}

//-----------------------------------------------------------------------------
// Purpose: Debug output and validation
//-----------------------------------------------------------------------------
void CIndexBufferDX12::Spew( int nIndexCount, const IndexDesc_t &desc )
{
	Msg( "ShaderAPIDX12: index buffer %d indices, %u-byte elements, offset %u\n", nIndexCount, m_nIndexSize, desc.m_nOffset );
}

void CIndexBufferDX12::ValidateData( int nIndexCount, const IndexDesc_t &desc )
{
	if ( nIndexCount < 0 || nIndexCount > m_nIndexCount || desc.m_nIndexSize != m_nIndexSize / sizeof( unsigned short ) )
		Warning( "ShaderAPIDX12: invalid index buffer range or format\n" );
}

//-----------------------------------------------------------------------------
// Purpose: Constructor; draws are forwarded to the owning shader API
//-----------------------------------------------------------------------------
CMeshDX12::CMeshDX12( VertexFormat_t format, int nVertexCount, bool bDynamic, DrawCallback pfnDraw, void *pDrawContext )
    : m_Vertices( format, nVertexCount, bDynamic ), m_Indices( MATERIAL_INDEX_FORMAT_16BIT, INDEX_BUFFER_SIZE, bDynamic ), m_pfnDraw( pfnDraw ), m_pDrawContext( pDrawContext )
{
}

//-----------------------------------------------------------------------------
// Purpose: Whether pMesh is this mesh or one of its buffer overrides
//-----------------------------------------------------------------------------
bool CMeshDX12::DependsOn( const CMeshDX12 *pMesh ) const
{
	return this == pMesh || ( m_pVertexOverride && m_pVertexOverride->DependsOn( pMesh ) ) || ( m_pIndexOverride && m_pIndexOverride->DependsOn( pMesh ) );
}

//-----------------------------------------------------------------------------
// Purpose: Draws from another mesh's buffers; rejects override cycles
//-----------------------------------------------------------------------------
bool CMeshDX12::OverrideBuffers( IMesh *pVertexMesh, IMesh *pIndexMesh )
{
	CMeshDX12 *pVertex = static_cast<CMeshDX12 *>( pVertexMesh );
	CMeshDX12 *pIndex = static_cast<CMeshDX12 *>( pIndexMesh );
	if ( ( pVertex && pVertex->DependsOn( this ) ) || ( pIndex && pIndex->DependsOn( this ) ) )
		return false;
	m_pVertexOverride = pVertex;
	m_pIndexOverride = pIndex;
	return true;
}

void CMeshDX12::EndCastBuffer()
{
	m_Vertices.EndCastBuffer();
	m_Indices.EndCastBuffer();
}

bool CMeshDX12::IsDynamic() const
{
	return m_Vertices.IsDynamic() || m_Indices.IsDynamic();
}

//-----------------------------------------------------------------------------
// Purpose: Mesh state setters; the host shader util may intercept them
//-----------------------------------------------------------------------------
void CMeshDX12::SetPrimitiveType( MaterialPrimitiveType_t type )
{
	if ( g_pShaderDeviceMgrDX12 && g_pShaderDeviceMgrDX12->HostShaderUtil() && !g_pShaderDeviceMgrDX12->HostShaderUtil()->OnSetPrimitiveType( this, type ) )
		return;
	m_Primitive = type;
}

void CMeshDX12::SetColorMesh( IMesh *pMesh, int nOffset )
{
	if ( g_pShaderDeviceMgrDX12 && g_pShaderDeviceMgrDX12->HostShaderUtil() && !g_pShaderDeviceMgrDX12->HostShaderUtil()->OnSetColorMesh( this, pMesh, nOffset ) )
		return;
	m_pColorMesh = pMesh;
	m_nColorOffset = nOffset;
}

void CMeshDX12::SetFlexMesh( IMesh *pMesh, int nOffset )
{
	if ( g_pShaderDeviceMgrDX12 && g_pShaderDeviceMgrDX12->HostShaderUtil() && !g_pShaderDeviceMgrDX12->HostShaderUtil()->OnSetFlexMesh( this, pMesh, nOffset ) )
		return;
	m_pFlexMesh = pMesh;
	m_nFlexOffset = nOffset;
}

//-----------------------------------------------------------------------------
// Purpose: Draws a range of the written indices through the draw callback
//-----------------------------------------------------------------------------
void CMeshDX12::Draw( int nFirstIndex, int nIndexCount )
{
	if ( g_pShaderDeviceMgrDX12 && g_pShaderDeviceMgrDX12->HostShaderUtil() && !g_pShaderDeviceMgrDX12->HostShaderUtil()->OnDrawMesh( this, nFirstIndex, nIndexCount ) )
	{
		m_bDrawn = true;
		return;
	}
	if ( nFirstIndex < 0 )
		nFirstIndex = 0;
	if ( nIndexCount <= 0 )
		nIndexCount = DrawIndices().WrittenCount();
	if ( nFirstIndex > DrawIndices().WrittenCount() || nIndexCount > DrawIndices().WrittenCount() - nFirstIndex )
		return;
	if ( m_pfnDraw )
		m_pfnDraw( m_pDrawContext, this, nFirstIndex, nIndexCount );
	m_bDrawn = true;
}

//-----------------------------------------------------------------------------
// Purpose: Draws each valid primitive list through the draw callback
//-----------------------------------------------------------------------------
void CMeshDX12::Draw( CPrimList *pLists, int nLists )
{
	if ( g_pShaderDeviceMgrDX12 && g_pShaderDeviceMgrDX12->HostShaderUtil() && !g_pShaderDeviceMgrDX12->HostShaderUtil()->OnDrawMesh( this, pLists, nLists ) )
	{
		m_bDrawn = true;
		return;
	}
	if ( !pLists || nLists <= 0 )
		return;
	for ( int i = 0; i < nLists; ++i )
	{
		int nFirst = pLists[i].m_FirstIndex, nCount = pLists[i].m_NumIndices;
		if ( nFirst >= 0 && nCount > 0 && nFirst <= DrawIndices().WrittenCount() && nCount <= DrawIndices().WrittenCount() - nFirst && m_pfnDraw )
			m_pfnDraw( m_pDrawContext, this, nFirst, nCount );
	}
	m_bDrawn = true;
}

//-----------------------------------------------------------------------------
// Purpose: Copies vertices and rebased 16-bit indices into a mesh builder
//-----------------------------------------------------------------------------
void CMeshDX12::CopyToMeshBuilder( int nFirstVertex, int nVertexCount, int nFirstIndex, int nIndexCount, int nIndexOffset, CMeshBuilder &builder )
{
	const CVertexBufferDX12 &sourceVertices = DrawVertices();
	const CIndexBufferDX12 &sourceIndices = DrawIndices();
	if ( nFirstVertex < 0 || nVertexCount < 0 || nFirstIndex < 0 || nIndexCount < 0 || nFirstVertex > sourceVertices.WrittenCount() || nVertexCount > sourceVertices.WrittenCount() - nFirstVertex || nFirstIndex > sourceIndices.WrittenCount() || nIndexCount > sourceIndices.WrittenCount() - nFirstIndex ||
	    ( nVertexCount > 0 && ( builder.VertexSize() != static_cast<int>( sourceVertices.Stride() ) || !builder.Position() ) ) )
	{
		Warning( "ShaderAPIDX12: CopyToMeshBuilder received incompatible vertex layout or range\n" );
		return;
	}
	const unsigned char *pSource = sourceIndices.Bytes().data() + static_cast<size_t>( nFirstIndex ) * sourceIndices.IndexSize();
	for ( int i = 0; i < nIndexCount; ++i )
	{
		uint32_t nIndex = 0;
		if ( sourceIndices.IndexSize() == 2 )
		{
			uint16_t nValue;
			memcpy( &nValue, pSource + static_cast<size_t>( i ) * 2, 2 );
			nIndex = nValue;
		}
		else
			memcpy( &nIndex, pSource + static_cast<size_t>( i ) * 4, 4 );
		const int64_t nAdjusted = static_cast<int64_t>( nIndex ) + nIndexOffset;
		if ( nAdjusted < 0 || nAdjusted > UINT16_MAX )
		{
			Warning( "ShaderAPIDX12: CopyToMeshBuilder index exceeds 16-bit destination\n" );
			return;
		}
	}
	if ( nVertexCount )
	{
		memcpy( const_cast<float *>( builder.Position() ), sourceVertices.Bytes().data() + static_cast<size_t>( nFirstVertex ) * sourceVertices.Stride(), static_cast<size_t>( nVertexCount ) * sourceVertices.Stride() );
		builder.AdvanceVertices( nVertexCount );
	}
	for ( int i = 0; i < nIndexCount; ++i )
	{
		uint32_t nIndex = 0;
		if ( sourceIndices.IndexSize() == 2 )
		{
			uint16_t nValue;
			memcpy( &nValue, pSource + static_cast<size_t>( i ) * 2, 2 );
			nIndex = nValue;
		}
		else
			memcpy( &nIndex, pSource + static_cast<size_t>( i ) * 4, 4 );
		builder.Index( static_cast<unsigned short>( static_cast<int64_t>( nIndex ) + nIndexOffset ) );
		builder.AdvanceIndex();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Debug output and validation forwarded to both buffers
//-----------------------------------------------------------------------------
void CMeshDX12::Spew( int nVertexCount, int nIndexCount, const MeshDesc_t &desc )
{
	m_Vertices.Spew( nVertexCount, desc );
	m_Indices.Spew( nIndexCount, desc );
}

void CMeshDX12::ValidateData( int nVertexCount, int nIndexCount, const MeshDesc_t &desc )
{
	m_Vertices.ValidateData( nVertexCount, desc );
	m_Indices.ValidateData( nIndexCount, desc );
}

//-----------------------------------------------------------------------------
// Purpose: Locks writable vertices and indices; overridden buffers lock 0
//-----------------------------------------------------------------------------
void CMeshDX12::LockMesh( int nVertexCount, int nIndexCount, MeshDesc_t &desc )
{
	if ( nVertexCount < 0 || nIndexCount < -1 )
	{
		memset( &desc, 0, sizeof( desc ) );
		return;
	}
	const int nWritableVertices = m_pVertexOverride ? 0 : nVertexCount;
	const int nWritableIndices = m_pIndexOverride && nIndexCount >= 0 ? 0 : nIndexCount;
	if ( !m_Vertices.EnsureCapacity( nWritableVertices ) || ( nWritableIndices >= 0 && !m_Indices.EnsureCapacity( nWritableIndices ) ) )
	{
		memset( &desc, 0, sizeof( desc ) );
		return;
	}
	const bool bVertices = m_Vertices.IsDynamic() ? m_Vertices.Lock( nWritableVertices, false, desc ) : m_Vertices.LockStatic( nWritableVertices, desc );
	const bool bIndices = nWritableIndices < 0 || m_Indices.Lock( nWritableIndices, false, desc );
	if ( !bVertices || !bIndices )
		memset( &desc, 0, sizeof( desc ) );
}

//-----------------------------------------------------------------------------
// Purpose: Exposes existing vertex and index ranges for writing
//-----------------------------------------------------------------------------
void CMeshDX12::ModifyBegin( int nFirstVertex, int nVertexCount, int nFirstIndex, int nIndexCount, MeshDesc_t &desc )
{
	if ( nFirstVertex < 0 || nVertexCount < 0 || nFirstIndex < 0 || nIndexCount < 0 ||
	    nFirstVertex > m_Vertices.VertexCount() || nVertexCount > m_Vertices.VertexCount() - nFirstVertex || nFirstIndex > m_Indices.IndexCount() || nIndexCount > m_Indices.IndexCount() - nFirstIndex )
	{
		memset( &desc, 0, sizeof( desc ) );
		return;
	}
	if ( nVertexCount > 0 )
		m_Vertices.MarkModified();
	VertexLayoutDX12 layout = ComputeVertexLayoutDX12( m_Vertices.GetVertexFormat(),
	    m_Vertices.Data().empty() ? nullptr : const_cast<unsigned char *>( m_Vertices.Data().data() ) + static_cast<size_t>( nFirstVertex ) * m_Vertices.Stride(), &desc );
	m_Indices.ModifyBegin( false, nFirstIndex, nIndexCount, desc );
	desc.m_nFirstVertex = nFirstVertex;
	desc.VertexDesc_t::m_nOffset = static_cast<unsigned int>( static_cast<size_t>( nFirstVertex ) * m_Vertices.Stride() );
	if ( !layout.valid )
		memset( &desc, 0, sizeof( desc ) );
}

void CMeshDX12::ModifyEnd( MeshDesc_t &desc )
{
	m_Indices.ModifyEnd( desc );
}

//-----------------------------------------------------------------------------
// Purpose: Ends a LockMesh; overridden buffers do not advance
//-----------------------------------------------------------------------------
void CMeshDX12::UnlockMesh( int nVertexCount, int nIndexCount, MeshDesc_t &desc )
{
	if ( m_Vertices.IsDynamic() )
		m_Vertices.Unlock( m_pVertexOverride ? 0 : nVertexCount, desc );
	else
		m_Vertices.UnlockStatic();
	if ( nIndexCount >= 0 )
		m_Indices.Unlock( m_pIndexOverride ? 0 : nIndexCount, desc );
}

//-----------------------------------------------------------------------------
// Purpose: Read-only modify exposes data without bumping content versions
//-----------------------------------------------------------------------------
void CMeshDX12::ModifyBeginEx( bool bReadOnly, int nFirstVertex, int nVertexCount, int nFirstIndex, int nIndexCount, MeshDesc_t &desc )
{
	if ( bReadOnly )
	{
		if ( nFirstVertex < 0 || nVertexCount < 0 || nFirstVertex > m_Vertices.VertexCount() || nVertexCount > m_Vertices.VertexCount() - nFirstVertex )
		{
			memset( &desc, 0, sizeof( desc ) );
			return;
		}
		ComputeVertexLayoutDX12( m_Vertices.GetVertexFormat(), m_Vertices.Data().empty() ? nullptr : const_cast<unsigned char *>( m_Vertices.Data().data() ) + static_cast<size_t>( nFirstVertex ) * m_Vertices.Stride(), &desc );
		desc.m_nFirstVertex = nFirstVertex;
		desc.VertexDesc_t::m_nOffset = static_cast<unsigned int>( static_cast<size_t>( nFirstVertex ) * m_Vertices.Stride() );
		if ( nIndexCount >= 0 )
			m_Indices.ModifyBegin( true, nFirstIndex, nIndexCount, desc );
	}
	else
		ModifyBegin( nFirstVertex, nVertexCount, nFirstIndex, nIndexCount, desc );
}

//-----------------------------------------------------------------------------
// Purpose: CPU bytes held by the mesh's own buffers
//-----------------------------------------------------------------------------
unsigned CMeshDX12::ComputeMemoryUsed()
{
	return static_cast<unsigned>( m_Vertices.Data().size() + m_Indices.Data().size() );
}

} // namespace shaderapidx12
