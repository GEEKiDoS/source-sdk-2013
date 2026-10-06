//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 shader, vertex, index buffers and meshes backed by CPU byte storage.
//
//=============================================================================//

#ifndef RESOURCES_DX12_H
#define RESOURCES_DX12_H
#pragma once

#include "materialsystem/imesh.h"
#include "materialsystem/shaderapidx12/vertex_layout_dx12.h"
#include "shaderapi/IShaderDevice.h"
#include "tier1/utlmemory.h"
#include "tier1/utlstring.h"
#include <d3d12.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <cstdint>

namespace shaderapidx12
{
bool ValidateHighresShaderResourcesDX12( ID3D12ShaderReflection *reflection, const D3D12_SHADER_DESC &shader, bool pixelStage, bool *highresAbi, CUtlString &error );

template <typename T>
struct ByteSpanDX12
{
	T *bytes;
	size_t count;

	T *data() const { return bytes; }

	size_t size() const { return count; }

	bool empty() const { return count == 0; }
};

class CShaderBufferDX12 final : public IShaderBuffer
{
public:
	CShaderBufferDX12( const void *pData, size_t nSize );

	size_t GetSize() const override { return m_nByteSize; }

	const void *GetBits() const override { return m_nByteSize ? m_Bytes.Base() : nullptr; }

	void Release() override { delete this; }

	ByteSpanDX12<const unsigned char> Bytes() const { return { m_Bytes.Base(), m_nByteSize }; }

private:
	CUtlMemoryConservative<unsigned char> m_Bytes;
	size_t m_nByteSize = 0;
};

class CVertexBufferDX12 : public IVertexBuffer
{
public:
	CVertexBufferDX12( VertexFormat_t format, int nCount, bool bDynamic );

	int VertexCount() const override { return m_nVertexCount; }

	int WrittenCount() const { return m_nWritten; }

	VertexFormat_t GetVertexFormat() const override { return m_Format; }

	bool IsDynamic() const override { return m_bDynamic; }

	void BeginCastBuffer( VertexFormat_t format ) override;
	void EndCastBuffer() override;
	int GetRoomRemaining() const override;
	bool Lock( int nVertexCount, bool bAppend, VertexDesc_t &desc ) override;
	void Unlock( int nVertexCount, VertexDesc_t &desc ) override;
	// CMeshDX8 static-mesh locks (dynamicvb.h CVertexBuffer): every lock starts at vertex 0, the contents beyond it
	// persist, and unlocking defines the whole locked range whatever count is reported. Callers fill the lock
	// pointer directly and unlock with 0 (engine static-prop color meshes copy .vhv lighting with memcpy).
	bool LockStatic( int nVertexCount, VertexDesc_t &desc );
	void UnlockStatic();
	void Spew( int nVertexCount, const VertexDesc_t &desc ) override;
	void ValidateData( int nVertexCount, const VertexDesc_t &desc ) override;

	ByteSpanDX12<const unsigned char> Bytes() const { return { m_Bytes.Base(), m_nByteSize }; }

	ByteSpanDX12<unsigned char> Bytes()
	{
		MarkModified();
		return { m_Bytes.Base(), m_nByteSize };
	}

	ByteSpanDX12<const unsigned char> Data() const { return { m_Bytes.Base(), m_nByteSize }; }

	uint32_t Stride() const { return m_nStride; }

	const VertexLayoutDX12 &Layout() const { return m_Layout; }

	// Byte offsets of colors needing red/blue swap, derived from the fixed layout on first use.
	const uint32_t *SwapOffsets( size_t &nCount ) const
	{
		if ( m_nSwapLayoutStride != m_Layout.stride + 1 )
		{
			m_nSwapCount = 0;
			for ( uint32_t i = 0; i < m_Layout.inputCount; ++i )
				if ( m_Layout.inputs[i].swapRedBlue )
					m_SwapOffsets[m_nSwapCount++] = m_Layout.inputs[i].byteOffset;
			m_nSwapLayoutStride = m_Layout.stride + 1;
		}
		nCount = m_nSwapCount;
		return m_SwapOffsets;
	}

	ID3D12Resource *NativeResource() const { return m_pResource.Get(); }

	bool EnsureCapacity( int nCount );

	Microsoft::WRL::ComPtr<ID3D12Resource> &NativeResourceRef() { return m_pResource; }

	uint64_t ContentVersion() const { return m_nContentVersion; }
	uint64_t Identity() const { return m_nIdentity; }

	uint64_t NativeResourceVersion() const { return m_nNativeResourceVersion; }

	size_t NativeResourceBytes() const { return m_nNativeResourceBytes; }

	ID3D12Device *NativeDevice() const { return m_pNativeDevice; }

	void SetNativeResourceVersion( uint64_t nVersion, size_t nBytes, ID3D12Device *pDevice )
	{
		m_nNativeResourceVersion = nVersion;
		m_nNativeResourceBytes = nBytes;
		m_pNativeDevice = pDevice;
	}

	// Last transient upload of this buffer's contents: reusable while version, fence and epoch match.
	struct TransientUploadDX12
	{
		uint64_t version = 0, fence = 0, epoch = 0;
		size_t bytes = 0;
		D3D12_GPU_VIRTUAL_ADDRESS address = 0;
	};

	TransientUploadDX12 &TransientUpload() { return m_TransientUpload; }

	// The pipeline cache already holds a fence-owned reference for this resource and recording fence.
	bool IsRetainedFor( uint64_t fence, uint64_t epoch ) const { return m_nRetainedFence == fence && m_nRetainedEpoch == epoch && m_pRetainedResource == m_pResource.Get(); }

	void MarkRetained( uint64_t fence, uint64_t epoch )
	{
		m_nRetainedFence = fence;
		m_nRetainedEpoch = epoch;
		m_pRetainedResource = m_pResource.Get();
		m_nRetainedAddress = m_pResource->GetGPUVirtualAddress();
	}

	// GPU address of the retained resource; valid while IsRetainedFor holds.
	D3D12_GPU_VIRTUAL_ADDRESS RetainedAddress() const { return m_nRetainedAddress; }

	void MarkModified();

private:
	bool LockRange( int nFirst, int nVertexCount, VertexDesc_t &desc );

protected:
	VertexFormat_t m_Format = 0;
	VertexLayoutDX12 m_Layout{};
	int m_nVertexCount = 0;
	int m_nWritten = 0;
	int m_nStaticLockCount = 0;
	bool m_bDynamic = false;
	uint32_t m_nStride = 0;
	CUtlMemoryConservative<unsigned char> m_Bytes;
	size_t m_nByteSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pResource;
	uint64_t m_nContentVersion = 1, m_nNativeResourceVersion = 0;
	uint64_t m_nIdentity = 0;
	size_t m_nNativeResourceBytes = 0;
	ID3D12Device *m_pNativeDevice = nullptr;
	ID3D12Resource *m_pRetainedResource = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS m_nRetainedAddress = 0;
	uint64_t m_nRetainedFence = 0, m_nRetainedEpoch = 0;
	TransientUploadDX12 m_TransientUpload{};
	mutable uint32_t m_SwapOffsets[MAX_VERTEX_INPUTS_DX12] = {};
	mutable size_t m_nSwapCount = 0;
	mutable uint32_t m_nSwapLayoutStride = 0;
};

class CIndexBufferDX12 : public IIndexBuffer
{
public:
	CIndexBufferDX12( MaterialIndexFormat_t format, int nCount, bool bDynamic );

	int IndexCount() const override { return m_nIndexCount; }

	int WrittenCount() const { return m_nWritten; }

	MaterialIndexFormat_t IndexFormat() const override { return m_Format; }

	bool IsDynamic() const override { return m_bDynamic; }

	void BeginCastBuffer( MaterialIndexFormat_t format ) override;
	void EndCastBuffer() override;
	int GetRoomRemaining() const override;
	bool Lock( int nMaxIndexCount, bool bAppend, IndexDesc_t &desc ) override;
	void Unlock( int nWrittenIndexCount, IndexDesc_t &desc ) override;
	void ModifyBegin( bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t &desc ) override;
	void ModifyEnd( IndexDesc_t &desc ) override;
	void Spew( int nIndexCount, const IndexDesc_t &desc ) override;
	void ValidateData( int nIndexCount, const IndexDesc_t &desc ) override;

	ByteSpanDX12<const unsigned char> Bytes() const { return { m_Bytes.Base(), m_nByteSize }; }

	ByteSpanDX12<unsigned char> Bytes()
	{
		MarkModified();
		return { m_Bytes.Base(), m_nByteSize };
	}

	ByteSpanDX12<const unsigned char> Data() const { return { m_Bytes.Base(), m_nByteSize }; }

	uint32_t IndexSize() const { return m_nIndexSize; }

	ID3D12Resource *NativeResource() const { return m_pResource.Get(); }

	bool EnsureCapacity( int nCount );

	Microsoft::WRL::ComPtr<ID3D12Resource> &NativeResourceRef() { return m_pResource; }

	uint64_t ContentVersion() const { return m_nContentVersion; }
	uint64_t Identity() const { return m_nIdentity; }

	uint64_t NativeResourceVersion() const { return m_nNativeResourceVersion; }

	size_t NativeResourceBytes() const { return m_nNativeResourceBytes; }

	ID3D12Device *NativeDevice() const { return m_pNativeDevice; }

	void SetNativeResourceVersion( uint64_t nVersion, size_t nBytes, ID3D12Device *pDevice )
	{
		m_nNativeResourceVersion = nVersion;
		m_nNativeResourceBytes = nBytes;
		m_pNativeDevice = pDevice;
	}

	bool IsRetainedFor( uint64_t fence, uint64_t epoch ) const { return m_nRetainedFence == fence && m_nRetainedEpoch == epoch && m_pRetainedResource == m_pResource.Get(); }

	void MarkRetained( uint64_t fence, uint64_t epoch )
	{
		m_nRetainedFence = fence;
		m_nRetainedEpoch = epoch;
		m_pRetainedResource = m_pResource.Get();
		m_nRetainedAddress = m_pResource->GetGPUVirtualAddress();
	}

	// GPU address of the retained resource; valid while IsRetainedFor holds.
	D3D12_GPU_VIRTUAL_ADDRESS RetainedAddress() const { return m_nRetainedAddress; }

	struct TransientUploadDX12
	{
		uint64_t version = 0, fence = 0, epoch = 0;
		size_t bytes = 0;
		D3D12_GPU_VIRTUAL_ADDRESS address = 0;
	};

	TransientUploadDX12 &TransientUpload() { return m_TransientUpload; }

	void MarkModified();

private:
	MaterialIndexFormat_t m_Format = MATERIAL_INDEX_FORMAT_16BIT;
	int m_nIndexCount = 0;
	int m_nWritten = 0;
	bool m_bDynamic = false;
	uint32_t m_nIndexSize = 2;
	CUtlMemoryConservative<unsigned char> m_Bytes;
	size_t m_nByteSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pResource;
	uint64_t m_nContentVersion = 1, m_nNativeResourceVersion = 0;
	uint64_t m_nIdentity = 0;
	size_t m_nNativeResourceBytes = 0;
	ID3D12Device *m_pNativeDevice = nullptr;
	ID3D12Resource *m_pRetainedResource = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS m_nRetainedAddress = 0;
	uint64_t m_nRetainedFence = 0, m_nRetainedEpoch = 0;
	TransientUploadDX12 m_TransientUpload{};
	bool m_bModifyingWritable = false;
};

class CMeshDX12 final : public IMesh
{
public:
	using DrawCallback = void ( * )( void *, CMeshDX12 *, int, int );
	CMeshDX12( VertexFormat_t format, int nVertexCount, bool bDynamic, DrawCallback pfnDraw, void *pDrawContext );

	int VertexCount() const override { return DrawVertices().VertexCount(); }

	VertexFormat_t GetVertexFormat() const override { return DrawVertices().GetVertexFormat(); }

	bool IsDynamic() const override;

	void BeginCastBuffer( VertexFormat_t format ) override { m_Vertices.BeginCastBuffer( format ); }

	void EndCastBuffer() override;

	int GetRoomRemaining() const override { return m_Vertices.GetRoomRemaining(); }

	bool Lock( int nVertexCount, bool bAppend, VertexDesc_t &desc ) override { return m_Vertices.Lock( nVertexCount, bAppend, desc ); }

	void Unlock( int nVertexCount, VertexDesc_t &desc ) override { m_Vertices.Unlock( nVertexCount, desc ); }

	void Spew( int nVertexCount, const VertexDesc_t &desc ) override { m_Vertices.Spew( nVertexCount, desc ); }

	void ValidateData( int nVertexCount, const VertexDesc_t &desc ) override { m_Vertices.ValidateData( nVertexCount, desc ); }

	int IndexCount() const override { return DrawIndices().IndexCount(); }

	MaterialIndexFormat_t IndexFormat() const override { return DrawIndices().IndexFormat(); }

	bool IsDynamicIndex() const { return m_Indices.IsDynamic(); }

	void BeginCastBuffer( MaterialIndexFormat_t format ) override { m_Indices.BeginCastBuffer( format ); }

	int GetRoomRemainingIndex() const { return m_Indices.GetRoomRemaining(); }

	bool Lock( int nMaxIndexCount, bool bAppend, IndexDesc_t &desc ) override { return m_Indices.Lock( nMaxIndexCount, bAppend, desc ); }

	void Unlock( int nWrittenIndexCount, IndexDesc_t &desc ) override { m_Indices.Unlock( nWrittenIndexCount, desc ); }

	void ModifyBegin( bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t &desc ) override { m_Indices.ModifyBegin( bReadOnly, nFirstIndex, nIndexCount, desc ); }

	void ModifyEnd( IndexDesc_t &desc ) override { m_Indices.ModifyEnd( desc ); }

	void Spew( int nIndexCount, const IndexDesc_t &desc ) override { m_Indices.Spew( nIndexCount, desc ); }

	void ValidateData( int nIndexCount, const IndexDesc_t &desc ) override { m_Indices.ValidateData( nIndexCount, desc ); }

	void SetPrimitiveType( MaterialPrimitiveType_t type ) override;
	void Draw( int nFirstIndex = -1, int nIndexCount = 0 ) override;
	void SetColorMesh( IMesh *pMesh, int nOffset ) override;
	void Draw( CPrimList *pLists, int nLists ) override;
	void CopyToMeshBuilder( int, int, int, int, int, CMeshBuilder & ) override;
	void Spew( int, int, const MeshDesc_t & ) override;
	void ValidateData( int, int, const MeshDesc_t & ) override;
	void LockMesh( int nVertexCount, int nIndexCount, MeshDesc_t &desc ) override;
	void ModifyBegin( int nFirstVertex, int nVertexCount, int nFirstIndex, int nIndexCount, MeshDesc_t &desc ) override;
	void ModifyEnd( MeshDesc_t &desc ) override;
	void UnlockMesh( int nVertexCount, int nIndexCount, MeshDesc_t &desc ) override;
	void ModifyBeginEx( bool bReadOnly, int nFirstVertex, int nVertexCount, int nFirstIndex, int nIndexCount, MeshDesc_t &desc ) override;
	void SetFlexMesh( IMesh *pMesh, int nOffset ) override;

	void DisableFlexMesh() override
	{
		m_pFlexMesh = nullptr;
		m_nFlexOffset = 0;
	}

	void MarkAsDrawn() override { m_bDrawn = true; }

	unsigned ComputeMemoryUsed() override;

	CVertexBufferDX12 &Vertices() { return m_Vertices; }

	CIndexBufferDX12 &Indices() { return m_Indices; }

	CVertexBufferDX12 &DrawVertices() { return m_pVertexOverride ? m_pVertexOverride->DrawVertices() : m_Vertices; }

	const CVertexBufferDX12 &DrawVertices() const { return m_pVertexOverride ? m_pVertexOverride->DrawVertices() : m_Vertices; }

	CIndexBufferDX12 &DrawIndices() { return m_pIndexOverride ? m_pIndexOverride->DrawIndices() : m_Indices; }

	const CIndexBufferDX12 &DrawIndices() const { return m_pIndexOverride ? m_pIndexOverride->DrawIndices() : m_Indices; }

	CMeshDX12 *VertexSourceMesh() { return m_pVertexOverride ? m_pVertexOverride->VertexSourceMesh() : this; }

	CMeshDX12 *IndexSourceMesh() { return m_pIndexOverride ? m_pIndexOverride->IndexSourceMesh() : this; }

	bool DependsOn( const CMeshDX12 *pMesh ) const;
	bool OverrideBuffers( IMesh *pVertexMesh, IMesh *pIndexMesh );

	MaterialPrimitiveType_t PrimitiveType() const { return m_Primitive; }

	IMesh *ColorMesh() const { return m_pColorMesh; }

	IMesh *FlexMesh() const { return m_pFlexMesh; }

	int ColorOffset() const { return m_nColorOffset; }

	int FlexOffset() const { return m_nFlexOffset; }

private:
	CVertexBufferDX12 m_Vertices;
	CIndexBufferDX12 m_Indices;
	MaterialPrimitiveType_t m_Primitive = MATERIAL_TRIANGLES;
	DrawCallback m_pfnDraw = nullptr;
	void *m_pDrawContext = nullptr;
	IMesh *m_pColorMesh = nullptr;
	IMesh *m_pFlexMesh = nullptr;
	CMeshDX12 *m_pVertexOverride = nullptr, *m_pIndexOverride = nullptr;
	int m_nColorOffset = 0, m_nFlexOffset = 0;
	bool m_bDrawn = false;
};

} // namespace shaderapidx12

#endif // RESOURCES_DX12_H
