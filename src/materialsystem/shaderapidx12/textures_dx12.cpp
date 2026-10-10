//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Native D3D12 textures: creation, uploads, readback, render targets, clears and blits.
//
//=============================================================================//

#include "pixelwriter.h"
#include "materialsystem/shaderapidx12/shaderapi_dx12.h"
#include "materialsystem/shaderapidx12/shaderdevice_dx12.h"
#include "lighting_dx12.h"
#include "highres_lightmaps_dx12.h"
#include "shaderapi/ishaderutil.h"
#include "tier0/dbg.h"
#include "tier1/keyvalues.h"
#include "tier1/strtools.h"
#include "vtf/vtf.h"
#include "tracy_dx12.h"
#include <d3dcompiler.h>
#include <climits>

namespace shaderapidx12
{
namespace
{
DXGI_FORMAT SRVFormat( DXGI_FORMAT format, bool bSRGB, bool bDepth );

//-----------------------------------------------------------------------------
// Purpose: Vendor depth formats that are allocated as depth-stencil resources
//-----------------------------------------------------------------------------
bool IsDepthFormat( ImageFormat format )
{
	return format == IMAGE_FORMAT_NV_DST16 || format == IMAGE_FORMAT_ATI_DST16 || format == IMAGE_FORMAT_NV_DST24 || format == IMAGE_FORMAT_ATI_DST24 || format == IMAGE_FORMAT_NV_INTZ || format == IMAGE_FORMAT_NV_RAWZ;
}

bool IsFloatFormat( ImageFormat format )
{
	return format == IMAGE_FORMAT_RGB323232F || format == IMAGE_FORMAT_RGBA32323232F || format == IMAGE_FORMAT_RGBA16161616F || format == IMAGE_FORMAT_R32F;
}

//-----------------------------------------------------------------------------
// Purpose: Resolve targets are single-sampled, unaligned and carry no view flags
//-----------------------------------------------------------------------------
D3D12_RESOURCE_DESC NormalizeResolveDesc( const D3D12_RESOURCE_DESC &source )
{
	D3D12_RESOURCE_DESC desc = source;
	desc.Alignment = 0;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	return desc;
}

bool SameResolveDesc( const D3D12_RESOURCE_DESC &a, const D3D12_RESOURCE_DESC &b )
{
	return a.Dimension == b.Dimension && a.Alignment == b.Alignment && a.Width == b.Width && a.Height == b.Height && a.DepthOrArraySize == b.DepthOrArraySize && a.MipLevels == b.MipLevels && a.Format == b.Format && a.SampleDesc.Count == b.SampleDesc.Count && a.SampleDesc.Quality == b.SampleDesc.Quality && a.Layout == b.Layout && a.Flags == b.Flags;
}

constexpr int kResolveTextureCacheCapacity = 16;
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Maps a requested format to one the device can sample (and filter)
//-----------------------------------------------------------------------------
ImageFormat CShaderAPIDX12::GetNearestSupportedFormat( ImageFormat format, bool filteringRequired ) const
{
	if ( IsDepthFormat( format ) )
	{
		const DXGI_FORMAT native = ImageFormatToDXGI12( format );
		if ( native == DXGI_FORMAT_UNKNOWN )
			return IMAGE_FORMAT_NV_DST24;
		if ( !m_pDevice || !m_pDevice->NativeDevice() )
			return format;
		D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
		support.Format = SRVFormat( native, false, true );
		const UINT nRequired = D3D12_FORMAT_SUPPORT1_TEXTURE2D | ( filteringRequired ? D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE : 0 );
		return SUCCEEDED( m_pDevice->NativeDevice()->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof( support ) ) ) && ( support.Support1 & nRequired ) == nRequired ? format : IMAGE_FORMAT_NV_DST24;
	}
	if ( format == IMAGE_FORMAT_RGB323232F )
		format = IMAGE_FORMAT_RGBA32323232F;
	else if ( format == IMAGE_FORMAT_ABGR8888 || format == IMAGE_FORMAT_ARGB8888 || format == IMAGE_FORMAT_RGB888 || format == IMAGE_FORMAT_BGR888 || format == IMAGE_FORMAT_IA88 || format == IMAGE_FORMAT_BGRA5551 || format == IMAGE_FORMAT_BGR565 || format == IMAGE_FORMAT_BGRX5551 || format == IMAGE_FORMAT_BGRA4444 || format == IMAGE_FORMAT_UV88 || format == IMAGE_FORMAT_UVWQ8888 )
		format = IMAGE_FORMAT_RGBA8888;
	DXGI_FORMAT native = ImageFormatToDXGI12( format );
	if ( native == DXGI_FORMAT_UNKNOWN )
		return IsFloatFormat( format ) ? IMAGE_FORMAT_RGBA32323232F : IMAGE_FORMAT_RGBA8888;
	if ( !m_pDevice || !m_pDevice->NativeDevice() )
		return format;
	D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
	support.Format = SRVFormat( native, false, false );
	const UINT nRequired = D3D12_FORMAT_SUPPORT1_TEXTURE2D | ( filteringRequired ? D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE : 0 );
	if ( FAILED( m_pDevice->NativeDevice()->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof( support ) ) ) || ( support.Support1 & nRequired ) != nRequired )
		return IsFloatFormat( format ) ? IMAGE_FORMAT_RGBA32323232F : IMAGE_FORMAT_RGBA8888;
	return format;
}

//-----------------------------------------------------------------------------
// Purpose: Maps a requested format to one the device can render to
//-----------------------------------------------------------------------------
ImageFormat CShaderAPIDX12::GetNearestRenderTargetFormat( ImageFormat format ) const
{
	if ( IsDepthFormat( format ) )
	{
		const ImageFormat selected = GetNearestSupportedFormat( format, false );
		const DXGI_FORMAT native = ImageFormatToDXGI12( selected );
		if ( !m_pDevice || !m_pDevice->NativeDevice() )
			return selected;
		D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
		support.Format = native == DXGI_FORMAT_R16_TYPELESS ? DXGI_FORMAT_D16_UNORM : DXGI_FORMAT_D24_UNORM_S8_UINT;
		return SUCCEEDED( m_pDevice->NativeDevice()->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof( support ) ) ) && ( support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL ) ? selected : IMAGE_FORMAT_NV_DST24;
	}
	const ImageFormat selected = GetNearestSupportedFormat( format, false );
	const DXGI_FORMAT native = ImageFormatToDXGI12( selected );
	if ( native == DXGI_FORMAT_BC1_TYPELESS || native == DXGI_FORMAT_BC2_TYPELESS || native == DXGI_FORMAT_BC3_TYPELESS || native == DXGI_FORMAT_BC4_UNORM || native == DXGI_FORMAT_BC5_UNORM )
		return IMAGE_FORMAT_RGBA8888;
	if ( !m_pDevice || !m_pDevice->NativeDevice() )
		return selected;
	D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
	support.Format = SRVFormat( native, false, false );
	if ( FAILED( m_pDevice->NativeDevice()->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof( support ) ) ) || !( support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET ) )
		return IMAGE_FORMAT_RGBA8888;
	return selected;
}

//-----------------------------------------------------------------------------
// Purpose: Multisampled scene targets have to be resolved before sampling
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::DoRenderTargetsNeedSeparateDepthBuffer() const
{
	return m_pDevice && m_pDevice->SceneSampleCount() > 1;
}

namespace
{
//-----------------------------------------------------------------------------
// Purpose: Bytes per texel of an uncompressed CPU staging format
//-----------------------------------------------------------------------------
size_t BytesPerPixel( ImageFormat format )
{
	switch ( format )
	{
	case IMAGE_FORMAT_A8:
	case IMAGE_FORMAT_I8:
	case IMAGE_FORMAT_P8:
		return 1;
	case IMAGE_FORMAT_NV_DST16:
	case IMAGE_FORMAT_ATI_DST16:
		return 2;
	case IMAGE_FORMAT_IA88:
	case IMAGE_FORMAT_RGB565:
	case IMAGE_FORMAT_BGR565:
	case IMAGE_FORMAT_BGRA5551:
	case IMAGE_FORMAT_BGRX5551:
	case IMAGE_FORMAT_BGRA4444:
		return 2;
	case IMAGE_FORMAT_RGB888:
	case IMAGE_FORMAT_BGR888:
		return 3;
	case IMAGE_FORMAT_RGBA16161616:
	case IMAGE_FORMAT_RGBA16161616F:
		return 8;
	case IMAGE_FORMAT_RGB323232F:
		return 12;
	case IMAGE_FORMAT_RGBA32323232F:
		return 16;
	case IMAGE_FORMAT_R32F:
		return 4;
	default:
		return 4;
	}
}

bool IsBlockCompressed( ImageFormat format )
{
	return format == IMAGE_FORMAT_DXT1 || format == IMAGE_FORMAT_DXT1_ONEBITALPHA || format == IMAGE_FORMAT_DXT3 || format == IMAGE_FORMAT_DXT5 || format == IMAGE_FORMAT_ATI1N || format == IMAGE_FORMAT_ATI2N;
}

//-----------------------------------------------------------------------------
// Purpose: Tightly packed staging size of one mip level
//-----------------------------------------------------------------------------
size_t MipBytes( int nWidth, int nHeight, int nDepth, ImageFormat format )
{
	if ( IsBlockCompressed( format ) )
	{
		const int nBlocksWide = MAX( 1, ( nWidth + 3 ) / 4 ), nBlocksHigh = MAX( 1, ( nHeight + 3 ) / 4 );
		const size_t nBlockBytes = ( format == IMAGE_FORMAT_DXT1 || format == IMAGE_FORMAT_DXT1_ONEBITALPHA || format == IMAGE_FORMAT_ATI1N ) ? 8u : 16u;
		return static_cast<size_t>( nBlocksWide ) * nBlocksHigh * nBlockBytes * MAX( 1, nDepth );
	}
	return static_cast<size_t>( MAX( 1, nWidth ) ) * MAX( 1, nHeight ) * MAX( 1, nDepth ) * BytesPerPixel( format );
}

int Faces( const CShaderAPIDX12::TextureRecord &texture )
{
	return ( texture.flags & TEXTURE_CREATE_CUBEMAP ) ? 6 : 1;
}

//-----------------------------------------------------------------------------
// Purpose: True while any subresource holds CPU data that has not been uploaded
//-----------------------------------------------------------------------------
bool HasDirtySubresource( const CShaderAPIDX12::TextureRecord &texture )
{
	for ( int i = 0; i < texture.dirtySubresources.Count(); ++i )
	{
		if ( texture.dirtySubresources[i] )
			return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: True when the handle is bound to one of the color render-target slots
//-----------------------------------------------------------------------------
bool IsBoundColorTarget( const ShaderAPITextureHandle_t ( &renderTargets )[RenderTargetBindingDX12::kMaxColorTargets], ShaderAPITextureHandle_t hTexture )
{
	for ( int i = 0; i < RenderTargetBindingDX12::kMaxColorTargets; ++i )
	{
		if ( renderTargets[i] == hTexture )
			return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Byte offset of a face/mip inside the staging copy (current copy by default)
//-----------------------------------------------------------------------------
size_t SubresourceOffset( const CShaderAPIDX12::TextureRecord &texture, int nFace, int nMip, int nCopy = -1 )
{
	size_t nOffset = static_cast<size_t>( nCopy < 0 ? texture.currentCopy : nCopy ) * texture.bytesPerCopy;
	for ( int i = 0; i < nFace; ++i )
	{
		for ( int j = 0; j < texture.mipLevels; ++j )
		{
			const int nMipWidth = MAX( 1, texture.width >> j ), nMipHeight = MAX( 1, texture.height >> j );
			nOffset += MipBytes( nMipWidth, nMipHeight, ( texture.flags & TEXTURE_CREATE_CUBEMAP ) ? 1 : MAX( 1, texture.depth >> j ), texture.format );
		}
	}
	for ( int j = 0; j < nMip; ++j )
	{
		const int nMipWidth = MAX( 1, texture.width >> j ), nMipHeight = MAX( 1, texture.height >> j );
		nOffset += MipBytes( nMipWidth, nMipHeight, ( texture.flags & TEXTURE_CREATE_CUBEMAP ) ? 1 : MAX( 1, texture.depth >> j ), texture.format );
	}
	return nOffset;
}

//-----------------------------------------------------------------------------
// Purpose: Typed view format for a typeless resource format
//-----------------------------------------------------------------------------
DXGI_FORMAT SRVFormat( DXGI_FORMAT format, bool bSRGB, bool bDepth )
{
	if ( bDepth )
	{
		if ( format == DXGI_FORMAT_R24G8_TYPELESS )
			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		if ( format == DXGI_FORMAT_R16_TYPELESS )
			return DXGI_FORMAT_R16_UNORM;
		if ( format == DXGI_FORMAT_R32_TYPELESS )
			return DXGI_FORMAT_R32_FLOAT;
	}
	switch ( format )
	{
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		return bSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		return bSRGB ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
	// Non-typeless 8-bit formats only reach here for the SDR swap chain and the frame generator's hudless copy.
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return bSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : format;
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		return bSRGB ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : format;
	case DXGI_FORMAT_BC1_TYPELESS:
		return bSRGB ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
	case DXGI_FORMAT_BC2_TYPELESS:
		return bSRGB ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM;
	case DXGI_FORMAT_BC3_TYPELESS:
		return bSRGB ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
	default:
		return format;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Records a staging-buffer copy of one subresource and leaves it shader-readable
//-----------------------------------------------------------------------------
bool CreateUpload( ID3D12Device *pDevice, CCommandRecorderDX12 *pList, ID3D12Resource *pTarget, const void *pSource, size_t nSourceBytes, UINT nSubresource, D3D12_RESOURCE_STATES &state, CShaderDeviceDX12 *pOwner )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 CreateUpload", DX12_ZONES_ACTIVE );
	if ( !pDevice || !pList || !pTarget || !pSource )
		return false;
	const D3D12_RESOURCE_DESC targetDesc = pTarget->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT nRows = 0;
	UINT64 nRequired = 0;
	pDevice->GetCopyableFootprints( &targetDesc, nSubresource, 1, 0, &footprint, &nRows, nullptr, &nRequired );
	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC bufferDesc{};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = nRequired;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> pUpload;
	if ( FAILED( pDevice->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &pUpload ) ) ) )
		return false;
	void *pMapped = nullptr;
	D3D12_RANGE range{};
	if ( FAILED( pUpload->Map( 0, &range, &pMapped ) ) )
		return false;
	const UINT nSlices = MAX( 1u, footprint.Footprint.Depth );
	const UINT nSourceRow = nRows ? static_cast<UINT>( nSourceBytes / ( static_cast<size_t>( nRows ) * nSlices ) ) : 0;
	const UINT nCopyRow = MIN( nSourceRow, footprint.Footprint.RowPitch );
	const unsigned char *pSourceBytes = static_cast<const unsigned char *>( pSource );
	unsigned char *pDestBytes = static_cast<unsigned char *>( pMapped ) + footprint.Offset;
	for ( UINT nSlice = 0; nSlice < nSlices; ++nSlice )
	{
		for ( UINT nRow = 0; nRow < nRows; ++nRow )
			memcpy( pDestBytes + ( static_cast<size_t>( nSlice ) * nRows + nRow ) * footprint.Footprint.RowPitch, pSourceBytes + ( static_cast<size_t>( nSlice ) * nRows + nRow ) * nSourceRow, nCopyRow );
	}
	pUpload->Unmap( 0, nullptr );
	if ( state != D3D12_RESOURCE_STATE_COPY_DEST )
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = pTarget;
		barrier.Transition.StateBefore = state;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.Subresource = nSubresource;
		pList->ResourceBarrier( 1, &barrier );
		state = D3D12_RESOURCE_STATE_COPY_DEST;
	}
	D3D12_TEXTURE_COPY_LOCATION dst{};
	dst.pResource = pTarget;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = nSubresource;
	D3D12_TEXTURE_COPY_LOCATION src{};
	src.pResource = pUpload.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = footprint;
	pList->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr );
	pOwner->RetainResource( pUpload.Get() );
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = pTarget;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	barrier.Transition.Subresource = nSubresource;
	pList->ResourceBarrier( 1, &barrier );
	state = barrier.Transition.StateAfter;
	return true;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Resource format for an engine image format (typeless where views differ)
//-----------------------------------------------------------------------------
DXGI_FORMAT ImageFormatToDXGI12( ImageFormat format, int nFlags, bool bDepth )
{
	if ( bDepth || IsDepthFormat( format ) )
	{
		if ( format == IMAGE_FORMAT_R32F )
			return DXGI_FORMAT_R32_TYPELESS;
		if ( format == IMAGE_FORMAT_NV_DST16 || format == IMAGE_FORMAT_ATI_DST16 )
			return DXGI_FORMAT_R16_TYPELESS;
		if ( IsDepthFormat( format ) )
			return DXGI_FORMAT_R24G8_TYPELESS;
		return DXGI_FORMAT_UNKNOWN;
	}
	switch ( format )
	{
	case IMAGE_FORMAT_RGBA8888:
		return DXGI_FORMAT_R8G8B8A8_TYPELESS;
	case IMAGE_FORMAT_BGRA8888:
	case IMAGE_FORMAT_BGRX8888:
		return DXGI_FORMAT_B8G8R8A8_TYPELESS;
	case IMAGE_FORMAT_DXT1:
	case IMAGE_FORMAT_DXT1_ONEBITALPHA:
		return DXGI_FORMAT_BC1_TYPELESS;
	case IMAGE_FORMAT_DXT3:
		return DXGI_FORMAT_BC2_TYPELESS;
	case IMAGE_FORMAT_DXT5:
		return DXGI_FORMAT_BC3_TYPELESS;
	case IMAGE_FORMAT_ATI1N:
		return DXGI_FORMAT_BC4_UNORM;
	case IMAGE_FORMAT_ATI2N:
		return DXGI_FORMAT_BC5_UNORM;
	case IMAGE_FORMAT_RGBA16161616F:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case IMAGE_FORMAT_RGBA16161616:
		return DXGI_FORMAT_R16G16B16A16_UNORM;
	case IMAGE_FORMAT_R32F:
		return DXGI_FORMAT_R32_FLOAT;
	case IMAGE_FORMAT_RGBA32323232F:
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case IMAGE_FORMAT_A8:
		return DXGI_FORMAT_R8_UNORM;
	case IMAGE_FORMAT_I8:
		return DXGI_FORMAT_R8_UNORM;
	case IMAGE_FORMAT_RGB565:
		return DXGI_FORMAT_B5G6R5_UNORM;
	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Engine image format matching a native resource or view format
//-----------------------------------------------------------------------------
ImageFormat DXGI12ToImageFormat( DXGI_FORMAT format )
{
	switch ( format )
	{
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return IMAGE_FORMAT_RGBA8888;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return IMAGE_FORMAT_BGRA8888;
	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
		return IMAGE_FORMAT_DXT1;
	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
		return IMAGE_FORMAT_DXT3;
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
		return IMAGE_FORMAT_DXT5;
	case DXGI_FORMAT_BC4_UNORM:
		return IMAGE_FORMAT_ATI1N;
	case DXGI_FORMAT_BC5_UNORM:
		return IMAGE_FORMAT_ATI2N;
	case DXGI_FORMAT_R16G16B16A16_UNORM:
		return IMAGE_FORMAT_RGBA16161616;
	case DXGI_FORMAT_R8_UNORM:
		return IMAGE_FORMAT_I8;
	case DXGI_FORMAT_B5G6R5_UNORM:
		return IMAGE_FORMAT_RGB565;
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_UNORM:
		return IMAGE_FORMAT_NV_DST16;
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		return IMAGE_FORMAT_NV_DST24;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		return IMAGE_FORMAT_RGBA16161616F;
	case DXGI_FORMAT_R32_FLOAT:
		return IMAGE_FORMAT_R32F;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		return IMAGE_FORMAT_RGBA32323232F;
	default:
		return IMAGE_FORMAT_UNKNOWN;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Allocates the native resources on first use, or makes evicted ones resident again
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::EnsureTextureResident( TextureRecord &texture )
{
	if ( texture.resourceResident )
		return true;
	if ( !m_pDevice || !m_pDevice->NativeDevice() )
		return false;
	if ( texture.resources.IsEmpty() )
		return AllocateNativeTexture( texture );
	CUtlVector<ID3D12Pageable *> pages;
	pages.EnsureCapacity( texture.resources.Count() );
	for ( const TextureRecord::ResourceCopy &copy : texture.resources )
	{
		if ( copy.resource )
			pages.AddToTail( copy.resource.Get() );
	}
	if ( pages.IsEmpty() || FAILED( m_pDevice->NativeDevice()->MakeResident( static_cast<UINT>( pages.Count() ), pages.Base() ) ) )
		return false;
	texture.resourceResident = true;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Rotates a multi-copy texture to its next copy before it is modified
//-----------------------------------------------------------------------------
void CShaderAPIDX12::AdvanceTextureCopy( TextureRecord &texture )
{
	if ( !texture.switchNeeded || texture.copies <= 1 )
		return;
	if ( m_pDevice ) m_pDevice->Highres().ForgetTexture( texture.id, texture.allocationSerial );
	texture.highresPage = 0xffffffffu;
	texture.highresLayoutGeneration = 0;
	texture.currentCopy = ( texture.currentCopy + 1 ) % texture.copies;
	texture.sampledStateValid = false;
	++m_nTextureStateEpoch;
	++m_nTextureIdentityEpoch;
	texture.switchNeeded = false;
	if ( !texture.resources.IsEmpty() )
		texture.resource = texture.resources[texture.currentCopy].resource;
	if ( m_pDevice && m_pDevice->NativeDevice() )
	{
		if ( texture.rtvHeap )
		{
			const SIZE_T nStart = texture.rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
			const SIZE_T nStride = m_pDevice->NativeDevice()->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
			texture.rtv.ptr = nStart + ( static_cast<SIZE_T>( texture.currentCopy ) * 2 ) * nStride;
			texture.rtvSRGB.ptr = texture.rtv.ptr + nStride;
		}
		if ( texture.dsvHeap )
			texture.dsv.ptr = texture.dsvHeap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>( texture.currentCopy ) * m_pDevice->NativeDevice()->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
	}
	for ( int i = 0; i < ARRAYSIZE( m_BoundTextures ); ++i )
	{
		if ( m_BoundTextures[i] == texture.id )
			m_BoundTextures[i] = 0;
	}
	for ( int i = 0; i < ARRAYSIZE( m_VertexTextures ); ++i )
	{
		if ( m_VertexTextures[i] == texture.id )
			m_VertexTextures[i] = 0;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Evicts resident managed textures after the GPU has drained
//-----------------------------------------------------------------------------
void CShaderAPIDX12::EvictManagedResources()
{
	if ( !m_pDevice || !m_pDevice->CommandList() || !m_pDevice->Submit( true ) )
		return;
	FOR_EACH_HASHTABLE( m_Textures, entry )
	{
		TextureRecord &texture = *m_Textures[entry];
		if ( !( texture.flags & TEXTURE_CREATE_MANAGED ) || !texture.resourceResident )
			continue;
		CUtlVector<ID3D12Pageable *> pages;
		pages.EnsureCapacity( texture.resources.Count() );
		for ( const TextureRecord::ResourceCopy &copy : texture.resources )
		{
			if ( copy.resource )
				pages.AddToTail( copy.resource.Get() );
		}
		// Prepared-slot and render-target caches skip residency checks; invalidate them with the eviction.
		if ( !pages.IsEmpty() && SUCCEEDED( m_pDevice->NativeDevice()->Evict( static_cast<UINT>( pages.Count() ), pages.Base() ) ) )
		{
			texture.resourceResident = false;
			texture.sampledStateValid = false;
			++m_nTextureStateEpoch;
			++m_nTextureIdentityEpoch;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns a pooled single-sample resolve target matching desc, creating one if needed.
//          The pool keeps at most kResolveTextureCacheCapacity entries that the GPU has finished with.
//-----------------------------------------------------------------------------
CShaderAPIDX12::ResolveTextureRecord *CShaderAPIDX12::AcquireResolveTexture( const D3D12_RESOURCE_DESC &desc, HRESULT &hrCreation )
{
	hrCreation = S_OK;
	const uint64_t nCompletedFence = m_pDevice->CompletedFenceValue();
	// Deletes the oldest idle entries beyond the capacity, keeping the order of the rest.
	auto trim = [&]( ResolveTextureRecord *pBorrowed )
	{
		int nRemaining = m_ResolveTextures.Count() - kResolveTextureCacheCapacity;
		for ( int i = 0; i < m_ResolveTextures.Count() && nRemaining > 0; )
		{
			ResolveTextureRecord *pRecord = m_ResolveTextures[i];
			if ( pRecord == pBorrowed || pRecord->lastUseFence > nCompletedFence )
			{
				++i;
				continue;
			}
			delete pRecord;
			m_ResolveTextures.Remove( i );
			--nRemaining;
		}
	};
	ResolveTextureRecord *pMatch = nullptr;
	for ( int i = 0; i < m_ResolveTextures.Count(); ++i )
	{
		if ( SameResolveDesc( m_ResolveTextures[i]->desc, desc ) )
		{
			pMatch = m_ResolveTextures[i];
			break;
		}
	}
	trim( pMatch );
	if ( pMatch )
	{
		pMatch->lastUseFence = m_pDevice->NextFenceValue();
		return pMatch;
	}
	Microsoft::WRL::ComPtr<ID3D12Resource> pResource;
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 BlitResolveAllocation", DX12_ZONES_ACTIVE );
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		hrCreation = m_pDevice->NativeDevice()->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr, IID_PPV_ARGS( &pResource ) );
	}
	if ( FAILED( hrCreation ) )
		return nullptr;
	ResolveTextureRecord *pRecord = new ResolveTextureRecord;
	pRecord->resource = std::move( pResource );
	pRecord->desc = desc;
	pRecord->state = D3D12_RESOURCE_STATE_RESOLVE_DEST;
	pRecord->lastUseFence = m_pDevice->NextFenceValue();
	m_ResolveTextures.AddToTail( pRecord );
	trim( pRecord );
	return pRecord;
}

//-----------------------------------------------------------------------------
// Purpose: Copies (or resolves) the motion-vector target into the engine's motion texture
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ResolveMotionTarget()
{
	if ( !m_hMotionResolveTarget || !m_pMotionTarget || !m_pDevice || !m_pDevice->CommandList() )
		return;
	TextureRecord *pRecord = FindTexture( m_hMotionResolveTarget );
	if ( !pRecord || !( pRecord->flags & TEXTURE_CREATE_RENDERTARGET ) || !EnsureTextureResident( *pRecord ) )
		return;
	const int nSub = pRecord->currentCopy * Faces( *pRecord ) * pRecord->mipLevels;
	D3D12_RESOURCE_STATES &destinationState = pRecord->subresourceStates[nSub];
	CCommandRecorderDX12 *pList = m_pDevice->CommandList();
	if ( m_nMotionTargetSamples > 1 )
	{
		if ( destinationState != D3D12_RESOURCE_STATE_RESOLVE_DEST )
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = pRecord->resource.Get();
			barrier.Transition.Subresource = nSub;
			barrier.Transition.StateBefore = destinationState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
			pList->ResourceBarrier( 1, &barrier );
			destinationState = barrier.Transition.StateAfter;
		}
		TransitionMotionTarget( D3D12_RESOURCE_STATE_RESOLVE_SOURCE );
		pList->ResolveSubresource( pRecord->resource.Get(), 0, m_pMotionTarget.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT );
	}
	else
	{
		if ( destinationState != D3D12_RESOURCE_STATE_COPY_DEST )
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = pRecord->resource.Get();
			barrier.Transition.Subresource = nSub;
			barrier.Transition.StateBefore = destinationState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			pList->ResourceBarrier( 1, &barrier );
			destinationState = barrier.Transition.StateAfter;
		}
		TransitionMotionTarget( D3D12_RESOURCE_STATE_COPY_SOURCE );
		pList->CopyResource( pRecord->resource.Get(), m_pMotionTarget.Get() );
	}
	if ( destinationState != D3D12_RESOURCE_STATE_RENDER_TARGET )
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = pRecord->resource.Get();
		barrier.Transition.Subresource = nSub;
		barrier.Transition.StateBefore = destinationState;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		pList->ResourceBarrier( 1, &barrier );
		destinationState = barrier.Transition.StateAfter;
	}
	pRecord->dirtySubresources[nSub] = 0;
	pRecord->initializedSubresources[nSub] = 0;
	pRecord->gpuAuthoritativeSubresources[nSub] = 1;
	pRecord->gpuDirty = HasDirtySubresource( *pRecord );
	TransitionMotionTarget( D3D12_RESOURCE_STATE_RENDER_TARGET );
	m_pDevice->TransitionSceneDepth( D3D12_RESOURCE_STATE_DEPTH_WRITE );
	m_hMotionResolvedHandle = m_hMotionResolveTarget;
	m_nMotionResolvedFrame = m_nFrameCounter;
}

//-----------------------------------------------------------------------------
// Purpose: Clears the primary target when the motion pass was suppressed this frame
//-----------------------------------------------------------------------------
void CShaderAPIDX12::MarkMotionTargetStale()
{
	TextureRecord *pRecord = FindTexture( m_RenderTargets[0] );
	if ( !pRecord || !( pRecord->flags & TEXTURE_CREATE_RENDERTARGET ) )
		return;
	RenderTargetBindingDX12 binding{};
	if ( !PrepareRenderTargets( binding, false ) || binding.colorCount < 1 )
		return;
	const float flZero[4]{};
	m_pDevice->CommandList()->ClearRenderTargetView( binding.rtvs[0], flZero, 0, nullptr );
}

//-----------------------------------------------------------------------------
// Purpose: Drops every native texture resource and cache; CPU staging data is kept
//          so the textures can be re-created on the next device
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseTextureDeviceResources()
{
	// Drain presentation and release feature contexts before destroying their tagged inputs.
	// Provider/queue support remains device-owned until ShutdownDevice.
	ReleaseFrameGenResources();
	ReleaseUpscalerResources();
	ReleaseMotionResources();
	ReleaseGBufferResources();
	for ( int i = 0; i < ARRAYSIZE( m_PreparedTextureSlots ); ++i )
		m_PreparedTextureSlots[i].valid = false;
	m_bTextureSetValid = false;
	for ( int i = 0; i < ARRAYSIZE( m_InputLayoutCaches ); ++i )
		m_InputLayoutCaches[i].valid = false;
	memset( m_TextureTypeHandles, 0, sizeof( m_TextureTypeHandles ) );
	m_DrawBindingNull = {};
	++m_nPipelineMemoEpoch;
	m_PreparedSamplerTable = {};
	m_nPreparedSamplerFence = 0;
	m_ResolveTextures.PurgeAndDeleteElements();
	m_ClearPasses.RemoveAll();
	m_pClearRoot.Reset();
	m_BlitPasses.RemoveAll();
	m_pBlitRoot.Reset();
	m_pBlitRtvHeap.Reset();
	for ( int i = 0; i < ARRAYSIZE( m_TargetDescs ); ++i )
		m_TargetDescs[i] = CachedResourceDescDX12{};
	m_RetiredTextureViews.RemoveAll();
	FOR_EACH_HASHTABLE( m_Textures, entry )
	{
		TextureRecord &texture = *m_Textures[entry];
		if ( m_pDevice ) m_pDevice->Highres().ForgetTexture( texture.id, texture.allocationSerial );
		texture.highresPage = 0xffffffffu;
		texture.highresLayoutGeneration = 0;
		texture.allocationSerial = 0;
		for ( int sub = 0; sub < texture.dirtySubresources.Count(); ++sub )
		{
			if ( texture.gpuAuthoritativeSubresources[sub] )
				texture.initializedSubresources[sub] = 0;
			texture.dirtySubresources[sub] = texture.initializedSubresources[sub];
			texture.gpuAuthoritativeSubresources[sub] = 0;
		}
		texture.gpuDirty = HasDirtySubresource( texture );
		for ( int slot = 0; slot < 2; ++slot )
		{
			m_Pipeline.ReleaseResourceDescriptor( texture.m_SrvSources[slot], 0 );
			texture.m_SrvSources[slot] = {};
			texture.m_pSrvResources[slot] = nullptr;
			texture.m_SrvDescriptors[slot] = {};
		}
		texture.resource.Reset();
		texture.resources.RemoveAll();
		texture.rtvHeap.Reset();
		texture.dsvHeap.Reset();
		texture.rtv = {};
		texture.rtvSRGB = {};
		texture.dsv = {};
		texture.subresourceStates.RemoveAll();
		texture.resourceResident = false;
		texture.sampledStateValid = false;
		++m_nTextureStateEpoch;
		++m_nTextureIdentityEpoch;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Reads a GPU-authoritative subresource back into the CPU staging copy
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::RefreshTextureStaging( TextureRecord &texture, int nFace, int nMip )
{
	if ( !m_pDevice || !m_pDevice->CommandList() || !EnsureTextureResident( texture ) || nFace < 0 || nFace >= Faces( texture ) || nMip < 0 || nMip >= texture.mipLevels )
		return false;
	const int nSub = nFace * texture.mipLevels + nMip;
	const int nIndex = texture.currentCopy * Faces( texture ) * texture.mipLevels + nSub;
	if ( !texture.gpuAuthoritativeSubresources[nIndex] )
		return true;
	const D3D12_RESOURCE_DESC desc = texture.resource->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT nRows = 0;
	UINT64 nRequired = 0;
	m_pDevice->NativeDevice()->GetCopyableFootprints( &desc, nSub, 1, 0, &footprint, &nRows, nullptr, &nRequired );
	if ( !nRequired )
		return false;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC buffer{};
	buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	buffer.Width = nRequired;
	buffer.Height = 1;
	buffer.DepthOrArraySize = 1;
	buffer.MipLevels = 1;
	buffer.SampleDesc.Count = 1;
	buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> pReadback;
	if ( FAILED( m_pDevice->NativeDevice()->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &pReadback ) ) ) )
		return false;
	D3D12_RESOURCE_STATES &state = texture.subresourceStates[nIndex];
	texture.sampledStateValid = false;
	++m_nTextureStateEpoch;
	if ( state != D3D12_RESOURCE_STATE_COPY_SOURCE )
	{
		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = texture.resource.Get();
		b.Transition.Subresource = nSub;
		b.Transition.StateBefore = state;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		m_pDevice->CommandList()->ResourceBarrier( 1, &b );
		state = D3D12_RESOURCE_STATE_COPY_SOURCE;
	}
	D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
	source.pResource = texture.resource.Get();
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	source.SubresourceIndex = nSub;
	destination.pResource = pReadback.Get();
	destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	destination.PlacedFootprint = footprint;
	m_pDevice->CommandList()->CopyTextureRegion( &destination, 0, 0, 0, &source, nullptr );
	m_pDevice->RetainResource( pReadback.Get() );
	if ( !m_pDevice->Submit( true ) )
		return false;
	void *pMapped = nullptr;
	D3D12_RANGE range{ 0, static_cast<SIZE_T>( nRequired ) };
	if ( FAILED( pReadback->Map( 0, &range, &pMapped ) ) )
		return false;
	const int nWidth = MAX( 1, texture.width >> nMip ), nHeight = MAX( 1, texture.height >> nMip ), nDepth = Faces( texture ) == 1 ? MAX( 1, texture.depth >> nMip ) : 1;
	const size_t nRowBytes = MipBytes( nWidth, 1, 1, texture.format ), nRowCount = IsBlockCompressed( texture.format ) ? static_cast<size_t>( MAX( 1, ( nHeight + 3 ) / 4 ) ) : static_cast<size_t>( nHeight );
	unsigned char *pDestBytes = texture.pixels.Base() + SubresourceOffset( texture, nFace, nMip );
	const unsigned char *pSourceBytes = static_cast<const unsigned char *>( pMapped ) + footprint.Offset;
	for ( int nSlice = 0; nSlice < nDepth; ++nSlice )
	{
		for ( size_t nRow = 0; nRow < nRowCount; ++nRow )
			memcpy( pDestBytes + ( static_cast<size_t>( nSlice ) * nRowCount + nRow ) * nRowBytes, pSourceBytes + ( static_cast<size_t>( nSlice ) * nRows + nRow ) * footprint.Footprint.RowPitch, nRowBytes );
	}
	pReadback->Unmap( 0, nullptr );
	texture.gpuAuthoritativeSubresources[nIndex] = 0;
	texture.initializedSubresources[nIndex] = 1;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: (Re)creates every copy of a texture's native resource and its RTV/DSV heaps
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::AllocateNativeTexture( TextureRecord &texture )
{
	const bool bDepth = ( texture.flags & TEXTURE_CREATE_DEPTHBUFFER ) != 0;
	const DXGI_FORMAT format = ImageFormatToDXGI12( texture.format, texture.flags, bDepth );
	if ( format == DXGI_FORMAT_UNKNOWN || ( bDepth && ( ( texture.flags & TEXTURE_CREATE_CUBEMAP ) || texture.depth > 1 ) ) )
		return false;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = texture.depth > 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = texture.width;
	desc.Height = texture.height;
	desc.DepthOrArraySize = static_cast<UINT16>( texture.depth > 1 ? texture.depth : Faces( texture ) );
	desc.MipLevels = static_cast<UINT16>( texture.mipLevels );
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	if ( texture.flags & TEXTURE_CREATE_RENDERTARGET )
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if ( texture.uavCapable )
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if ( bDepth )
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE clear{};
	clear.Format = bDepth ? ( format == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_D32_FLOAT : format == DXGI_FORMAT_R16_TYPELESS ? DXGI_FORMAT_D16_UNORM :
	                                                                                                                            DXGI_FORMAT_D24_UNORM_S8_UINT ) :
	                        SRVFormat( format, false, false );
	if ( bDepth )
	{
		clear.DepthStencil.Depth = 1.0f;
		clear.DepthStencil.Stencil = 0;
	}
	else
		clear.Color[3] = 1.0f;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	const D3D12_RESOURCE_STATES initial = bDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : ( texture.flags & TEXTURE_CREATE_RENDERTARGET ) ? D3D12_RESOURCE_STATE_RENDER_TARGET :
	                                                                                                                                    D3D12_RESOURCE_STATE_COPY_DEST;
	// Invalidate before releasing any member of the owned allocation set. Restore and
	// render-target promotion both come through this allocation boundary.
	m_pDevice->Highres().ForgetTexture( texture.id, texture.allocationSerial );
	texture.highresPage = 0xffffffffu;
	texture.highresLayoutGeneration = 0;
	texture.allocationSerial = m_nNextTextureAllocationSerial++;
	// Recorded OMSetRenderTargets/Clear*View calls read these CPU descriptors at replay; retire them by fence.
	if ( texture.rtvHeap || texture.dsvHeap )
	{
		RetiredTextureViewsDX12 &views = m_RetiredTextureViews[m_RetiredTextureViews.AddToTail()];
		views.rtv = texture.rtvHeap;
		views.dsv = texture.dsvHeap;
		views.fence = m_pDevice->NextFenceValue();
	}
	++m_nTextureStateEpoch;
	++m_nTextureIdentityEpoch;
	texture.resource.Reset();
	texture.resources.RemoveAll();
	texture.resources.EnsureCapacity( texture.copies );
	texture.rtvHeap.Reset();
	texture.dsvHeap.Reset();
	texture.rtv = {};
	texture.rtvSRGB = {};
	texture.dsv = {};
	texture.resourceResident = false;
	for ( int nCopy = 0; nCopy < texture.copies; ++nCopy )
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> pResource;
		if ( FAILED( m_pDevice->NativeDevice()->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, initial, ( texture.flags & ( TEXTURE_CREATE_RENDERTARGET | TEXTURE_CREATE_DEPTHBUFFER ) ) ? &clear : nullptr, IID_PPV_ARGS( &pResource ) ) ) )
			return false;
		wchar_t wszDebugName[512];
		if ( !texture.name.IsEmpty() && MultiByteToWideChar( CP_UTF8, 0, texture.name.Get(), -1, wszDebugName, static_cast<int>( ARRAYSIZE( wszDebugName ) ) ) )
			pResource->SetName( wszDebugName );
		texture.resources[texture.resources.AddToTail()].resource = std::move( pResource );
	}
	texture.resource = texture.resources[texture.currentCopy].resource;
	if ( ( texture.flags & TEXTURE_CREATE_RENDERTARGET ) && !bDepth )
	{
		D3D12_DESCRIPTOR_HEAP_DESC hd{};
		hd.NumDescriptors = texture.copies * 2;
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		if ( FAILED( m_pDevice->NativeDevice()->CreateDescriptorHeap( &hd, IID_PPV_ARGS( &texture.rtvHeap ) ) ) )
			return false;
		const D3D12_CPU_DESCRIPTOR_HANDLE start = texture.rtvHeap->GetCPUDescriptorHandleForHeapStart();
		const UINT nIncrement = m_pDevice->NativeDevice()->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
		D3D12_RENDER_TARGET_VIEW_DESC view{};
		view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		for ( int nCopy = 0; nCopy < texture.copies; ++nCopy )
		{
			for ( int nEncoding = 0; nEncoding < 2; ++nEncoding )
			{
				D3D12_CPU_DESCRIPTOR_HANDLE handle{ start.ptr + ( static_cast<SIZE_T>( nCopy ) * 2 + nEncoding ) * nIncrement };
				view.Format = SRVFormat( format, nEncoding != 0, false );
				m_pDevice->NativeDevice()->CreateRenderTargetView( texture.resources[nCopy].resource.Get(), &view, handle );
			}
		}
		texture.rtv.ptr = start.ptr + static_cast<SIZE_T>( texture.currentCopy ) * 2 * nIncrement;
		texture.rtvSRGB.ptr = texture.rtv.ptr + nIncrement;
	}
	if ( bDepth )
	{
		D3D12_DESCRIPTOR_HEAP_DESC hd{};
		hd.NumDescriptors = texture.copies;
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		if ( FAILED( m_pDevice->NativeDevice()->CreateDescriptorHeap( &hd, IID_PPV_ARGS( &texture.dsvHeap ) ) ) )
			return false;
		const D3D12_CPU_DESCRIPTOR_HANDLE start = texture.dsvHeap->GetCPUDescriptorHandleForHeapStart();
		const UINT nIncrement = m_pDevice->NativeDevice()->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
		D3D12_DEPTH_STENCIL_VIEW_DESC view{};
		view.Format = clear.Format;
		view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		for ( int nCopy = 0; nCopy < texture.copies; ++nCopy )
		{
			D3D12_CPU_DESCRIPTOR_HANDLE handle{ start.ptr + static_cast<SIZE_T>( nCopy ) * nIncrement };
			m_pDevice->NativeDevice()->CreateDepthStencilView( texture.resources[nCopy].resource.Get(), &view, handle );
		}
		texture.dsv.ptr = start.ptr + static_cast<SIZE_T>( texture.currentCopy ) * nIncrement;
	}
	texture.subresourceStates.SetCount( texture.copies * Faces( texture ) * texture.mipLevels );
	texture.subresourceStates.FillWithValue( initial );
	texture.resourceResident = true;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Creates a texture: CPU staging copies plus the native resources
//-----------------------------------------------------------------------------
ShaderAPITextureHandle_t CShaderAPIDX12::CreateTexture( int width, int height, int depth, ImageFormat format, int mipLevels, int copies, int flags, const char *debugName, const char * )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 CreateTexture", DX12_ZONES_ACTIVE );
	// Source's shadow-depth render targets are allocated through InitRenderTarget with
	// MATERIAL_RT_DEPTH_NONE, while their vendor format still identifies them as depth.
	// Normalize that legacy format contract before validation and native allocation.
	if ( IsDepthFormat( format ) )
		flags |= TEXTURE_CREATE_DEPTHBUFFER;
	if ( !m_pDevice || !m_pDevice->NativeDevice() || width <= 0 || height <= 0 || depth <= 0 || format == IMAGE_FORMAT_UNKNOWN || width > 16384 || height > 16384 || depth > 2048 || mipLevels > 15 || copies > 64 || ( ( flags & TEXTURE_CREATE_CUBEMAP ) && ( width != height || depth != 1 ) ) || ( depth > 1 && ( flags & TEXTURE_CREATE_RENDERTARGET ) ) )
		return 0;
	const ImageFormat nativeFormat = ( flags & TEXTURE_CREATE_DEPTHBUFFER ) ? format : ( flags & TEXTURE_CREATE_RENDERTARGET ) ? GetNearestRenderTargetFormat( format ) :
	                                                                                                                             GetNearestSupportedFormat( format );
	if ( nativeFormat == IMAGE_FORMAT_UNKNOWN || ( nativeFormat != format && IsBlockCompressed( nativeFormat ) ) )
		return 0;
	TextureRecord *pRecord = new TextureRecord;
	pRecord->id = m_hNextTexture++;
	pRecord->width = width;
	pRecord->height = height;
	pRecord->depth = depth;
	pRecord->mipLevels = MAX( 1, mipLevels );
	pRecord->copies = MAX( 1, copies );
	pRecord->format = nativeFormat;
	pRecord->requestedFormat = format;
	pRecord->flags = flags;
	pRecord->name = debugName ? debugName : "";
	const int nFaces = Faces( *pRecord ), nSubresources = nFaces * pRecord->mipLevels;
	size_t nTotal = 0;
	for ( int nFace = 0; nFace < nFaces; ++nFace )
	{
		for ( int nMip = 0; nMip < pRecord->mipLevels; ++nMip )
			nTotal += MipBytes( MAX( 1, width >> nMip ), MAX( 1, height >> nMip ), nFaces == 1 ? MAX( 1, depth >> nMip ) : 1, nativeFormat );
	}
	if ( nTotal > static_cast<size_t>( INT_MAX ) / pRecord->copies )
	{
		delete pRecord;
		return 0;
	}
	pRecord->bytesPerCopy = nTotal;
	pRecord->pixels.SetCount( static_cast<int>( nTotal * pRecord->copies ) );
	memset( pRecord->pixels.Base(), 0, nTotal * pRecord->copies );
	pRecord->dirtySubresources.SetCount( nSubresources * pRecord->copies );
	pRecord->dirtySubresources.FillWithValue( 0 );
	pRecord->initializedSubresources.SetCount( nSubresources * pRecord->copies );
	pRecord->initializedSubresources.FillWithValue( 0 );
	pRecord->gpuAuthoritativeSubresources.SetCount( nSubresources * pRecord->copies );
	pRecord->gpuAuthoritativeSubresources.FillWithValue( 0 );
	if ( !AllocateNativeTexture( *pRecord ) )
	{
		delete pRecord;
		return 0;
	}
	const ShaderAPITextureHandle_t hTexture = pRecord->id;
	m_Textures.Insert( hTexture, pRecord );
	return hTexture;
}

//-----------------------------------------------------------------------------
// Purpose: Gives a single-copy 2D texture a new mip count with clean staging; native resources are not touched
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ResizeTextureStaging( TextureRecord &texture, int nMipLevels )
{
	texture.mipLevels = nMipLevels;
	texture.bytesPerCopy = 0;
	for ( int nMip = 0; nMip < nMipLevels; ++nMip )
		texture.bytesPerCopy += MipBytes( MAX( 1, texture.width >> nMip ), MAX( 1, texture.height >> nMip ), 1, texture.format );
	texture.pixels.SetCount( static_cast<int>( texture.bytesPerCopy ) );
	texture.dirtySubresources.SetCount( nMipLevels );
	texture.dirtySubresources.FillWithValue( 0 );
	texture.initializedSubresources.SetCount( nMipLevels );
	texture.initializedSubresources.FillWithValue( 0 );
	texture.gpuAuthoritativeSubresources.SetCount( nMipLevels );
	texture.gpuAuthoritativeSubresources.FillWithValue( 0 );
}

//-----------------------------------------------------------------------------
// Purpose: Deletes textures that other threads queued for the recording thread
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ProcessPendingTextureDeletes()
{
	if ( !m_pDevice || !m_pDevice->IsRecordingOwner() )
		return;
	// Draws call this per draw; skip the lock and vector swap while nothing is queued.
	if ( !m_pDevice->HasTextureDeletionRequests() )
		return;
	CUtlVector<uintptr_t> handles;
	m_pDevice->TakeTextureDeletionRequests( handles );
	for ( int i = 0; i < handles.Count(); ++i )
		DeleteTexture( static_cast<ShaderAPITextureHandle_t>( handles[i] ) );
}

//-----------------------------------------------------------------------------
// Purpose: Destroys a texture once the GPU no longer references its resources and views
//-----------------------------------------------------------------------------
void CShaderAPIDX12::DeleteTexture( ShaderAPITextureHandle_t handle )
{
	if ( handle <= 0 )
		return;
	// All texture state, including draw bindings, belongs to the recording thread.
	if ( m_pDevice && !m_pDevice->IsRecordingOwner() )
	{
		m_pDevice->QueueTextureDeletion( static_cast<uintptr_t>( handle ) );
		return;
	}
	TextureRecord *pRecord = FindTexture( handle );
	if ( pRecord == nullptr )
		return;
	if ( m_pDevice ) m_pDevice->Highres().ForgetTexture( handle, pRecord->allocationSerial );
	pRecord->highresPage = 0xffffffffu;
	pRecord->highresLayoutGeneration = 0;
	if ( m_pDevice ) m_pDevice->Lighting().ForgetSunReceiverTexture( handle );
	for ( int i = 0; i < ARRAYSIZE( m_TextureTypeHandles ); ++i )
	{
		if ( m_TextureTypeHandles[i] == handle )
			m_TextureTypeHandles[i] = 0;
	}
	++m_nTextureStateEpoch;
	if ( m_pDevice )
	{
		for ( int i = m_RetiredTextureViews.Count() - 1; i >= 0; --i )
		{
			if ( m_RetiredTextureViews[i].fence <= m_pDevice->CompletedFenceValue() )
				m_RetiredTextureViews.Remove( i );
		}
		if ( pRecord->rtvHeap || pRecord->dsvHeap )
		{
			RetiredTextureViewsDX12 &views = m_RetiredTextureViews[m_RetiredTextureViews.AddToTail()];
			views.rtv = pRecord->rtvHeap;
			views.dsv = pRecord->dsvHeap;
			views.fence = m_pDevice->NextFenceValue();
		}
		// A recording frame completes after every already submitted frame. Retaining
		// until its fence therefore also protects draws submitted before deletion.
		for ( int i = 0; i < pRecord->resources.Count(); ++i )
		{
			if ( pRecord->resources[i].resource )
				m_pDevice->RetainResource( pRecord->resources[i].resource.Get() );
		}
	}
	for ( int i = 0; i < ARRAYSIZE( pRecord->m_SrvSources ); ++i )
		m_Pipeline.ReleaseResourceDescriptor( pRecord->m_SrvSources[i], m_pDevice ? m_pDevice->NextFenceValue() : 0 );
	for ( int i = 0; i < ARRAYSIZE( m_PreparedTextureSlots ); ++i )
	{
		PreparedTextureSlot &slot = m_PreparedTextureSlots[i];
		if ( slot.handle == handle )
		{
			slot.valid = false;
			slot.record = nullptr;
		}
	}
	{
		TextureLookupDX12 &cached = m_TextureLookup[static_cast<size_t>( handle ) & ( ARRAYSIZE( m_TextureLookup ) - 1 )];
		if ( cached.handle == handle )
			cached = {};
	}
	m_Textures.Remove( handle );
	delete pRecord;
	++m_nTextureIdentityEpoch;
	if ( m_hModifiedTexture == handle )
		m_hModifiedTexture = 0;
	if ( m_hRenderTarget == handle )
		m_hRenderTarget = SHADER_RENDERTARGET_BACKBUFFER;
	for ( int i = 0; i < ARRAYSIZE( m_RenderTargets ); ++i )
	{
		if ( m_RenderTargets[i] == handle )
			m_RenderTargets[i] = i ? SHADER_RENDERTARGET_NONE : SHADER_RENDERTARGET_BACKBUFFER;
	}
	if ( m_hDepthTarget == handle )
		m_hDepthTarget = SHADER_RENDERTARGET_DEPTHBUFFER;
	for ( int i = 0; i < ARRAYSIZE( m_BoundTextures ); ++i )
	{
		if ( m_BoundTextures[i] == handle )
			m_BoundTextures[i] = 0;
	}
	for ( int i = 0; i < ARRAYSIZE( m_VertexTextures ); ++i )
	{
		if ( m_VertexTextures[i] == handle )
			m_VertexTextures[i] = 0;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Texture handle queries and the "modified texture" selection used by Tex*
//-----------------------------------------------------------------------------
ShaderAPITextureHandle_t CShaderAPIDX12::CreateDepthTexture( ImageFormat format, int width, int height, const char *name, bool )
{
	return CreateTexture( width, height, 1, format, 1, 1, TEXTURE_CREATE_DEPTHBUFFER | TEXTURE_CREATE_RENDERTARGET, name, "Depth" );
}

bool CShaderAPIDX12::IsTexture( ShaderAPITextureHandle_t h )
{
	return h > 0 && FindTexture( h ) != nullptr;
}

bool CShaderAPIDX12::IsTextureResident( ShaderAPITextureHandle_t h )
{
	const TextureRecord *pRecord = FindTexture( h );
	return pRecord != nullptr && pRecord->resourceResident;
}

void CShaderAPIDX12::ModifyTexture( ShaderAPITextureHandle_t h )
{
	TextureRecord *pRecord = FindTexture( h );
	if ( pRecord == nullptr )
		return;
	m_hModifiedTexture = h;
	if ( pRecord->copies > 1 )
		pRecord->switchNeeded = true;
}

//-----------------------------------------------------------------------------
// Purpose: Uploads a full mip level of the modified texture
//-----------------------------------------------------------------------------
void CShaderAPIDX12::TexImage2D( int level, int face, ImageFormat dstFormat, int z, int width, int height, ImageFormat srcFormat, bool tiled, void *data )
{
	const TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord != nullptr && ( pRecord->format == dstFormat || pRecord->requestedFormat == dstFormat ) )
		TexSubImage2D( level, face, 0, 0, z, width, height, srcFormat, 0, tiled, data );
}

//-----------------------------------------------------------------------------
// Purpose: Writes a region into the staging copy of the modified texture and uploads the mip
//-----------------------------------------------------------------------------
void CShaderAPIDX12::TexSubImage2D( int level, int face, int x, int y, int z, int width, int height, ImageFormat srcFormat, int srcStride, bool tiled, void *data )
{
	(void)tiled;
	TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord == nullptr || !data )
		return;
	TextureRecord &t = *pRecord;
	if ( level < 0 || level >= t.mipLevels || face < 0 || face >= Faces( t ) || x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > MAX( 1, t.width >> level ) || y + height > MAX( 1, t.height >> level ) || z < 0 || z >= MAX( 1, t.depth >> level ) )
		return;
	AdvanceTextureCopy( t );
	t.sampledStateValid = false;
	++m_nTextureStateEpoch;
	const bool bCompressed = IsBlockCompressed( t.format );
	const size_t nDstBpp = BytesPerPixel( t.format ), nSrcBpp = BytesPerPixel( srcFormat );
	const int nMipWidth = MAX( 1, t.width >> level ), nMipHeight = MAX( 1, t.height >> level );
	const size_t nBase = SubresourceOffset( t, face, level );
	const int nSubresource = t.currentCopy * Faces( t ) * t.mipLevels + face * t.mipLevels + level;
	if ( t.gpuAuthoritativeSubresources[nSubresource] && ( x || y || z || width != nMipWidth || height != nMipHeight || ( Faces( t ) == 1 && t.depth > 1 ) ) )
	{
		if ( !RefreshTextureStaging( t, face, level ) )
			return;
	}
	if ( bCompressed )
	{
		if ( srcFormat != t.format || ( x & 3 ) || ( y & 3 ) || ( ( width & 3 ) && x + width != nMipWidth ) || ( ( height & 3 ) && y + height != nMipHeight ) )
			return;
		const size_t nBlockBytes = ( t.format == IMAGE_FORMAT_DXT1 || t.format == IMAGE_FORMAT_DXT1_ONEBITALPHA || t.format == IMAGE_FORMAT_ATI1N ) ? 8 : 16;
		const size_t nDstPitch = static_cast<size_t>( MAX( 1, ( nMipWidth + 3 ) / 4 ) ) * nBlockBytes, nRowBytes = static_cast<size_t>( MAX( 1, ( width + 3 ) / 4 ) ) * nBlockBytes;
		if ( srcStride <= 0 )
			srcStride = static_cast<int>( nRowBytes );
		const size_t nSliceBytes = nDstPitch * MAX( 1, ( nMipHeight + 3 ) / 4 );
		const int nBlockRows = MAX( 1, ( height + 3 ) / 4 );
		for ( int nRow = 0; nRow < nBlockRows; ++nRow )
			memcpy( t.pixels.Base() + nBase + static_cast<size_t>( z ) * nSliceBytes + ( static_cast<size_t>( y / 4 + nRow ) * nDstPitch + static_cast<size_t>( x / 4 ) * nBlockBytes ), static_cast<const unsigned char *>( data ) + static_cast<size_t>( nRow ) * srcStride, nRowBytes );
	}
	else
	{
		if ( srcStride <= 0 )
			srcStride = IsBlockCompressed( srcFormat ) ? static_cast<int>( MipBytes( width, 1, 1, srcFormat ) ) : static_cast<int>( width * nSrcBpp );
		unsigned char *pDst = t.pixels.Base() + nBase + ( static_cast<size_t>( z ) * nMipWidth * nMipHeight + static_cast<size_t>( y ) * nMipWidth + x ) * nDstBpp;
		if ( srcFormat == t.format )
		{
			for ( int nRow = 0; nRow < height; ++nRow )
				memcpy( pDst + static_cast<size_t>( nRow ) * nMipWidth * nDstBpp, static_cast<const unsigned char *>( data ) + static_cast<size_t>( nRow ) * srcStride, static_cast<size_t>( width ) * nDstBpp );
		}
		else if ( !m_pShaderUtil || !m_pShaderUtil->ConvertImageFormat( static_cast<unsigned char *>( data ), srcFormat, pDst, t.format, width, height, srcStride, static_cast<int>( nMipWidth * nDstBpp ) ) )
			return;
	}
	t.gpuAuthoritativeSubresources[nSubresource] = 0;
	const UINT nSub = static_cast<UINT>( face * t.mipLevels + level );
	t.dirtySubresources[nSubresource] = 1;
	t.initializedSubresources[nSubresource] = 1;
	t.gpuDirty = true;
	if ( !EnsureTextureResident( t ) || !m_pDevice || !m_pDevice->CommandList() )
		return;
	const size_t nFullBytes = MipBytes( nMipWidth, nMipHeight, Faces( t ) == 1 ? MAX( 1, t.depth >> level ) : 1, t.format );
	if ( CreateUpload( m_pDevice->NativeDevice(), m_pDevice->CommandList(), t.resource.Get(), t.pixels.Base() + nBase, nFullBytes, nSub, t.subresourceStates[nSubresource], m_pDevice ) )
		t.dirtySubresources[nSubresource] = 0;
	t.gpuDirty = HasDirtySubresource( t );
}

//-----------------------------------------------------------------------------
// Purpose: Uploads every face and mip of one VTF frame into the modified texture
//-----------------------------------------------------------------------------
void CShaderAPIDX12::TexImageFromVTF( IVTFTexture *vtf, int frame )
{
	TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord == nullptr || !vtf || frame < 0 || frame >= vtf->FrameCount() )
		return;
	TextureRecord &texture = *pRecord;
	// Legacy VTF cubemaps include a seventh fallback spheremap; native cubes upload only the six directional faces.
	const int nFaces = Faces( texture );
	if ( ( vtf->FaceCount() != nFaces && !( nFaces == 6 && vtf->FaceCount() == CUBEMAP_FACE_COUNT ) ) || vtf->MipCount() < texture.mipLevels || vtf->Format() == IMAGE_FORMAT_UNKNOWN )
		return;
	AdvanceTextureCopy( texture );
	texture.sampledStateValid = false;
	++m_nTextureStateEpoch;
	const int nCount = Faces( texture ) * texture.mipLevels;
	for ( int nFace = 0; nFace < Faces( texture ); ++nFace )
	{
		for ( int nMip = 0; nMip < texture.mipLevels; ++nMip )
		{
			int nWidth = 0, nHeight = 0, nDepth = 0;
			vtf->ComputeMipLevelDimensions( nMip, &nWidth, &nHeight, &nDepth );
			if ( nWidth != MAX( 1, texture.width >> nMip ) || nHeight != MAX( 1, texture.height >> nMip ) || nDepth != ( Faces( texture ) == 1 ? MAX( 1, texture.depth >> nMip ) : 1 ) )
				return;
			const int nSubresource = nFace * texture.mipLevels + nMip, nIndex = texture.currentCopy * nCount + nSubresource;
			const size_t nDestBytes = MipBytes( nWidth, nHeight, nDepth, texture.format );
			const int nSourceBytes = m_pShaderUtil ? m_pShaderUtil->GetMemRequired( nWidth, nHeight, nDepth, vtf->Format(), false ) : vtf->ComputeMipSize( nMip );
			if ( nSourceBytes <= 0 || vtf->ComputeMipSize( nMip ) < nSourceBytes || ( m_pShaderUtil && !m_pShaderUtil->ImageFormatInfo( vtf->Format() ).m_pName ) )
				return;
			unsigned char *pDest = texture.pixels.Base() + SubresourceOffset( texture, nFace, nMip );
			if ( vtf->Format() == texture.format )
			{
				if ( static_cast<size_t>( nSourceBytes ) < nDestBytes )
					return;
				unsigned char *pSource = vtf->ImageData( frame, nFace, nMip );
				if ( !pSource )
					return;
				memcpy( pDest, pSource, nDestBytes );
			}
			else
			{
				if ( !m_pShaderUtil || IsBlockCompressed( texture.format ) )
					return;
				const int nDestStride = nWidth * static_cast<int>( BytesPerPixel( texture.format ) );
				for ( int nSlice = 0; nSlice < nDepth; ++nSlice )
				{
					unsigned char *pSource = vtf->ImageData( frame, nFace, nMip, 0, 0, nSlice );
					if ( !pSource || !m_pShaderUtil->ConvertImageFormat( pSource, vtf->Format(), pDest + static_cast<size_t>( nSlice ) * nHeight * nDestStride, texture.format, nWidth, nHeight, vtf->RowSizeInBytes( nMip ), nDestStride ) )
						return;
				}
			}
			texture.initializedSubresources[nIndex] = 1;
			texture.dirtySubresources[nIndex] = 1;
			texture.gpuAuthoritativeSubresources[nIndex] = 0;
			if ( !EnsureTextureResident( texture ) || !m_pDevice->CommandList() )
				return;
			if ( !CreateUpload( m_pDevice->NativeDevice(), m_pDevice->CommandList(), texture.resource.Get(), pDest, nDestBytes, nSubresource, texture.subresourceStates[nIndex], m_pDevice ) )
				return;
			texture.dirtySubresources[nIndex] = 0;
		}
	}
	texture.gpuDirty = HasDirtySubresource( texture );
}

//-----------------------------------------------------------------------------
// Purpose: Locks a region of the modified texture for CPU writes through a pixel writer
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::TexLock( int level, int face, int x, int y, int width, int height, CPixelWriter &writer )
{
	TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord == nullptr || level < 0 || level >= pRecord->mipLevels || face < 0 || face >= Faces( *pRecord ) || IsBlockCompressed( pRecord->format ) || pRecord->lockLevel >= 0 )
		return false;
	TextureRecord &t = *pRecord;
	const int nMipWidth = MAX( 1, t.width >> level ), nMipHeight = MAX( 1, t.height >> level );
	if ( x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > nMipWidth || y + height > nMipHeight )
		return false;
	AdvanceTextureCopy( t );
	t.sampledStateValid = false;
	++m_nTextureStateEpoch;
	const size_t nPixelBytes = BytesPerPixel( t.format ), nRowBytes = static_cast<size_t>( width ) * nPixelBytes;
	if ( !nPixelBytes || nRowBytes > INT_MAX || static_cast<size_t>( height ) > INT_MAX / nRowBytes )
		return false;
	const int nSubresource = t.currentCopy * Faces( t ) * t.mipLevels + face * t.mipLevels + level;
	if ( t.gpuAuthoritativeSubresources[nSubresource] && !RefreshTextureStaging( t, face, level ) )
		return false;
	t.lockData.SetCountNonDestructively( static_cast<int>( nRowBytes * height ) );
	const unsigned char *pSource = t.pixels.Base() + SubresourceOffset( t, face, level ) + ( static_cast<size_t>( y ) * nMipWidth + x ) * nPixelBytes;
	for ( int nRow = 0; nRow < height; ++nRow )
		memcpy( t.lockData.Base() + static_cast<size_t>( nRow ) * nRowBytes, pSource + static_cast<size_t>( nRow ) * nMipWidth * nPixelBytes, nRowBytes );
	t.lockLevel = level;
	t.lockFace = face;
	t.lockX = x;
	t.lockY = y;
	t.lockWidth = width;
	t.lockHeight = height;
	t.lockPitch = static_cast<int>( nRowBytes );
	t.lockWrite = true;
	t.lockRead = false;
	writer.SetPixelMemory( t.format, t.lockData.Base(), t.lockPitch );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Uploads the region written since TexLock
//-----------------------------------------------------------------------------
void CShaderAPIDX12::TexUnlock()
{
	TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord == nullptr || pRecord->lockLevel < 0 )
		return;
	TextureRecord &t = *pRecord;
	TexSubImage2D( t.lockLevel, t.lockFace, t.lockX, t.lockY, 0, t.lockWidth, t.lockHeight, t.format, t.lockPitch, false, t.lockData.Base() );
	t.lockLevel = -1;
	t.lockData.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Sampler state of the modified texture; changes invalidate its cached sampler descriptors
//-----------------------------------------------------------------------------
void CShaderAPIDX12::TexSetPriority( int priority )
{
	TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord != nullptr )
		pRecord->priority = priority;
}

void CShaderAPIDX12::TexMinFilter( ShaderTexFilterMode_t mode )
{
	TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord != nullptr )
	{
		pRecord->minFilter = static_cast<int>( mode );
		pRecord->m_SamplerDescriptorValid[0] = pRecord->m_SamplerDescriptorValid[1] = false;
		++m_nTextureStateEpoch;
	}
}

void CShaderAPIDX12::TexMagFilter( ShaderTexFilterMode_t mode )
{
	TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord != nullptr )
	{
		pRecord->magFilter = static_cast<int>( mode );
		pRecord->m_SamplerDescriptorValid[0] = pRecord->m_SamplerDescriptorValid[1] = false;
		++m_nTextureStateEpoch;
	}
}

void CShaderAPIDX12::TexWrap( ShaderTexCoordComponent_t coord, ShaderTexWrapMode_t mode )
{
	TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord == nullptr )
		return;
	if ( coord == SHADER_TEXCOORD_S )
		pRecord->wrapU = static_cast<int>( mode );
	else if ( coord == SHADER_TEXCOORD_T )
		pRecord->wrapV = static_cast<int>( mode );
	else
		pRecord->wrapW = static_cast<int>( mode );
	pRecord->m_SamplerDescriptorValid[0] = pRecord->m_SamplerDescriptorValid[1] = false;
	++m_nTextureStateEpoch;
}

void CShaderAPIDX12::TexLodClamp( int finest )
{
	TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord != nullptr )
	{
		pRecord->lodClamp = finest;
		pRecord->m_SamplerDescriptorValid[0] = pRecord->m_SamplerDescriptorValid[1] = false;
		++m_nTextureStateEpoch;
	}
}

void CShaderAPIDX12::TexLodBias( float bias )
{
	TextureRecord *pRecord = FindTexture( m_hModifiedTexture );
	if ( pRecord != nullptr )
	{
		pRecord->lodBias = bias;
		pRecord->m_SamplerDescriptorValid[0] = pRecord->m_SamplerDescriptorValid[1] = false;
		++m_nTextureStateEpoch;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Like DX9 SetTextureState, count a bind only when the sampler binding actually changes
//-----------------------------------------------------------------------------
void CShaderAPIDX12::BindTexture( Sampler_t sampler, ShaderAPITextureHandle_t h )
{
	if ( sampler >= 0 && sampler < static_cast<int>( ARRAYSIZE( m_BoundTextures ) ) && m_BoundTextures[sampler] != h )
	{
		m_BoundTextures[sampler] = h;
		const PreparedTextureSlot &slot = m_PreparedTextureSlots[sampler];
		TextureRecord *pRecord = slot.valid && slot.record && slot.handle == h ? slot.record : FindTexture( h );
		if ( pRecord )
			++pRecord->binds;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Binds the primary color and depth targets; bound textures aliasing them are unbound
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetRenderTarget( ShaderAPITextureHandle_t color, ShaderAPITextureHandle_t depth )
{
	if ( color > 0 && color == depth )
		return;
	m_hRenderTarget = color;
	m_hDepthTarget = depth;
	m_RenderTargets[0] = color;
	for ( int i = 0; i < ARRAYSIZE( m_BoundTextures ); ++i )
	{
		if ( m_BoundTextures[i] > 0 && ( m_BoundTextures[i] == color || m_BoundTextures[i] == depth ) )
			m_BoundTextures[i] = 0;
	}
	for ( int i = 0; i < ARRAYSIZE( m_VertexTextures ); ++i )
	{
		if ( m_VertexTextures[i] > 0 && ( m_VertexTextures[i] == color || m_VertexTextures[i] == depth ) )
			m_VertexTextures[i] = 0;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Binds an MRT slot; slot 0 also binds depth
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetRenderTargetEx( int index, ShaderAPITextureHandle_t color, ShaderAPITextureHandle_t depth )
{
	if ( index < 0 || index >= static_cast<int>( ARRAYSIZE( m_RenderTargets ) ) )
		return;
	if ( index == 0 )
	{
		SetRenderTarget( color, depth );
		return;
	}
	m_RenderTargets[index] = color == SHADER_RENDERTARGET_BACKBUFFER ? SHADER_RENDERTARGET_NONE : color;
	if ( color > 0 )
	{
		for ( int i = 0; i < ARRAYSIZE( m_BoundTextures ); ++i )
		{
			if ( m_BoundTextures[i] == color )
				m_BoundTextures[i] = 0;
		}
		for ( int i = 0; i < ARRAYSIZE( m_VertexTextures ); ++i )
		{
			if ( m_VertexTextures[i] == color )
				m_VertexTextures[i] = 0;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Forwarders into the native texture entry points
//-----------------------------------------------------------------------------
void CShaderAPIDX12::CopyRenderTargetToTexture( ShaderAPITextureHandle_t textureHandle )
{
	CopyRenderTargetToTextureEx( textureHandle, 0, nullptr, nullptr );
}

ITexture *CShaderAPIDX12::GetRenderTargetEx( int index )
{
	return m_pShaderUtil ? m_pShaderUtil->GetRenderTargetEx( index ) : nullptr;
}

bool CShaderAPIDX12::PrepareSampledTexture( ShaderAPITextureHandle_t hTexture, bool bSRGB, ID3D12Resource **ppResource, D3D12_SHADER_RESOURCE_VIEW_DESC &srv, D3D12_SAMPLER_DESC &sampler, D3D12_CPU_DESCRIPTOR_HANDLE *pSource, bool bComparison, int nFirstMip, int nMipCount )
{
	return PrepareSampledTextureDX12( *this, hTexture, bSRGB, ppResource, srv, sampler, pSource, bComparison, nFirstMip, nMipCount );
}

bool CShaderAPIDX12::PrepareRenderTargets( RenderTargetBindingDX12 &binding, bool bEncodeSRGB )
{
	return PrepareRenderTargetsDX12( *this, binding, bEncodeSRGB );
}

//-----------------------------------------------------------------------------
// Purpose: Makes a texture shader-readable (pending uploads, state transitions) and returns
//          its resource, SRV description/descriptor and sampler. Handles <= 0 sample the null view.
//-----------------------------------------------------------------------------
bool PrepareSampledTextureDX12( CShaderAPIDX12 &api, ShaderAPITextureHandle_t hTexture, bool bSRGB, ID3D12Resource **ppResource, D3D12_SHADER_RESOURCE_VIEW_DESC &srv, D3D12_SAMPLER_DESC &sampler, D3D12_CPU_DESCRIPTOR_HANDLE *pSource, bool bComparison, int nFirstMip, int nMipCount )
{
	if ( hTexture <= 0 )
	{
		*ppResource = nullptr;
		if ( pSource )
			*pSource = bComparison ? D3D12_CPU_DESCRIPTOR_HANDLE{} : api.m_Pipeline.NullShaderResourceView();
		srv = {};
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Format = bComparison ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
		srv.Texture2D.MipLevels = 1;
		sampler = {};
		sampler.Filter = bComparison ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.ComparisonFunc = bComparison ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		return true;
	}
	if ( hTexture == api.m_hDepthTarget || IsBoundColorTarget( api.m_RenderTargets, hTexture ) )
	{
		const CShaderAPIDX12::TextureRecord *pFound = api.FindTexture( hTexture );
		Warning( "ShaderAPIDX12: rejecting sampling bound target %lld (%s), primary=%lld depth=%lld shader=%s\n", static_cast<long long>( hTexture ), pFound == nullptr ? "unknown" : pFound->name.Get(), static_cast<long long>( api.m_hRenderTarget ), static_cast<long long>( api.m_hDepthTarget ), api.m_ActiveSnapshot.pixelShaderName.c_str() );
		*ppResource = nullptr;
		return false;
	}
	CShaderAPIDX12::TextureRecord *pRecord = api.FindTexture( hTexture );
	if ( pRecord == nullptr || !api.m_pDevice || !api.m_pDevice->CommandList() )
	{
		*ppResource = nullptr;
		return false;
	}
	if ( bComparison && !( pRecord->flags & TEXTURE_CREATE_DEPTHBUFFER ) )
	{
		// Warn once per texture: this runs per draw and some materials legitimately bind plain textures to
		// comparison-mask slots (the typed SRV is used, as in DX9).
		static ShaderAPITextureHandle_t s_hWarned[16]{};
		bool bWarned = false;
		for ( int i = 0; i < ARRAYSIZE( s_hWarned ); ++i )
		{
			if ( s_hWarned[i] == hTexture )
			{
				bWarned = true;
				break;
			}
		}
		if ( !bWarned )
		{
			for ( int i = 0; i < ARRAYSIZE( s_hWarned ); ++i )
			{
				if ( s_hWarned[i] == 0 )
				{
					s_hWarned[i] = hTexture;
					break;
				}
			}
			Warning( "ShaderAPIDX12: comparison sampling non-depth texture handle=%lld name=%s format=%d requested=%d flags=0x%x shader=%s comparisonMask=0x%04x; using its typed SRV\n",
			    static_cast<long long>( hTexture ), pRecord->name.Get(), static_cast<int>( pRecord->format ), static_cast<int>( pRecord->requestedFormat ), pRecord->flags,
			    api.m_ActiveSnapshot.pixelShaderName.c_str(), api.m_ActiveSnapshot.comparisonSamplerMask );
		}
	}
	CShaderAPIDX12::TextureRecord &t = *pRecord;
	if ( !api.EnsureTextureResident( t ) )
	{
		*ppResource = nullptr;
		return false;
	}
	*ppResource = t.resource.Get();
	const int nFaces = Faces( t ), nCount = nFaces * t.mipLevels;
	const bool bLimitedRange = nFirstMip >= 0;
	const int nFirst = bLimitedRange ? nFirstMip : 0;
	const int nRequested = bLimitedRange && nMipCount > 0 ? nMipCount : t.mipLevels - nFirst;
	if ( nFirst < 0 || nFirst >= t.mipLevels || nRequested <= 0 || nFirst + nRequested > t.mipLevels )
	{
		*ppResource = nullptr;
		return false;
	}
	if ( bLimitedRange || !t.sampledStateValid )
	{
		for ( int nFace = 0; nFace < nFaces; ++nFace )
		{
			for ( int nMip = nFirst; nMip < nFirst + nRequested; ++nMip )
			{
				const int nSub = nFace * t.mipLevels + nMip;
				const int nStateIndex = t.currentCopy * nCount + nSub;
				if ( t.dirtySubresources[nStateIndex] )
				{
					const size_t nBytes = MipBytes( MAX( 1, t.width >> nMip ), MAX( 1, t.height >> nMip ), nFaces == 1 ? MAX( 1, t.depth >> nMip ) : 1, t.format );
					if ( !CreateUpload( api.m_pDevice->NativeDevice(), api.m_pDevice->CommandList(), t.resource.Get(), t.pixels.Base() + SubresourceOffset( t, nFace, nMip ), nBytes, nSub, t.subresourceStates[nStateIndex], api.m_pDevice ) )
						return false;
					t.dirtySubresources[nStateIndex] = 0;
				}
				const D3D12_RESOURCE_STATES sampledState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				if ( t.subresourceStates[nStateIndex] != sampledState )
				{
					D3D12_RESOURCE_BARRIER barrier{};
					barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barrier.Transition.pResource = t.resource.Get();
					barrier.Transition.StateBefore = t.subresourceStates[nStateIndex];
					barrier.Transition.StateAfter = sampledState;
					barrier.Transition.Subresource = nSub;
					api.m_pDevice->CommandList()->ResourceBarrier( 1, &barrier );
					t.subresourceStates[nStateIndex] = sampledState;
				}
			}
		}
		t.sampledStateValid = !bLimitedRange;
		// Uploads above are the only dirty-state change on this path.
		t.gpuDirty = HasDirtySubresource( t );
	}
	const int nSlot = bSRGB ? 1 : 0;
	// Resource shape and component mapping are immutable; sampler state is rebuilt below.
	if ( t.m_SrvSources[nSlot].ptr && t.m_pSrvResources[nSlot] == t.resource.Get() )
	{
		srv = t.m_SrvDescriptors[nSlot];
		if ( pSource )
			*pSource = t.m_SrvSources[nSlot];
	}
	else
	{
		srv = {};
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		const bool bDepth = ( t.flags & TEXTURE_CREATE_DEPTHBUFFER ) != 0;
		srv.Format = SRVFormat( t.resource->GetDesc().Format, bSRGB, bDepth );
		if ( t.flags & TEXTURE_CREATE_CUBEMAP )
		{
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srv.TextureCube.MipLevels = t.mipLevels;
		}
		else if ( t.depth > 1 )
		{
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			srv.Texture3D.MipLevels = t.mipLevels;
		}
		else
		{
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = t.mipLevels;
		}
		if ( t.format == IMAGE_FORMAT_BGRX8888 || t.format == IMAGE_FORMAT_I8 || t.format == IMAGE_FORMAT_A8 )
		{
			const D3D12_SHADER_COMPONENT_MAPPING r = D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0, one = D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1;
			if ( t.format == IMAGE_FORMAT_BGRX8888 )
				srv.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING( D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2, one );
			else if ( t.format == IMAGE_FORMAT_A8 )
				srv.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING( one, one, one, r );
			else
				srv.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING( r, r, r, one );
		}
		if ( pSource )
		{
			// A recorded table copy may still read the old view; never rewrite a published source slot in place.
			if ( t.m_SrvSources[nSlot].ptr )
			{
				api.m_Pipeline.ReleaseResourceDescriptor( t.m_SrvSources[nSlot], api.m_pDevice->NextFenceValue() );
				t.m_SrvSources[nSlot] = {};
			}
			t.m_SrvSources[nSlot] = api.m_Pipeline.AcquireResourceDescriptor( api.m_pDevice->NextFenceValue() );
			if ( !t.m_SrvSources[nSlot].ptr )
				return false;
			api.m_pDevice->NativeDevice()->CreateShaderResourceView( t.resource.Get(), &srv, t.m_SrvSources[nSlot] );
			t.m_pSrvResources[nSlot] = t.resource.Get();
			t.m_SrvDescriptors[nSlot] = srv;
			*pSource = t.m_SrvSources[nSlot];
		}
	}
	const int nSamplerSlot = bComparison ? 1 : 0;
	const int nSamplerAnisotropy = Clamp( api.m_nAnisotropy, 1, 16 );
	const bool bAnisotropic = t.minFilter == SHADER_TEXFILTERMODE_ANISOTROPIC || t.magFilter == SHADER_TEXFILTERMODE_ANISOTROPIC;
	if ( t.m_SamplerDescriptorValid[nSamplerSlot] && ( !bAnisotropic || t.m_nSamplerDescriptorAnisotropy == nSamplerAnisotropy ) )
		sampler = t.m_SamplerDescriptors[nSamplerSlot];
	else
	{
		sampler = {};
		const bool bMinLinear = t.minFilter == SHADER_TEXFILTERMODE_LINEAR || t.minFilter == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_NEAREST || t.minFilter == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_LINEAR;
		const bool bMagLinear = t.magFilter == SHADER_TEXFILTERMODE_LINEAR || t.magFilter == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_NEAREST || t.magFilter == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_LINEAR;
		const bool bMipLinear = t.minFilter == SHADER_TEXFILTERMODE_NEAREST_MIPMAP_LINEAR || t.minFilter == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_LINEAR;
		sampler.Filter = bAnisotropic ? ( bComparison ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC ) : static_cast<D3D12_FILTER>( ( bMinLinear ? 0x10 : 0 ) | ( bMagLinear ? 0x4 : 0 ) | ( bMipLinear ? 0x1 : 0 ) | ( bComparison ? 0x80 : 0 ) );
		sampler.AddressU = t.wrapU == SHADER_TEXWRAPMODE_CLAMP ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : t.wrapU == SHADER_TEXWRAPMODE_BORDER ? D3D12_TEXTURE_ADDRESS_MODE_BORDER :
		                                                                                                                                   D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = t.wrapV == SHADER_TEXWRAPMODE_CLAMP ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : t.wrapV == SHADER_TEXWRAPMODE_BORDER ? D3D12_TEXTURE_ADDRESS_MODE_BORDER :
		                                                                                                                                   D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = t.wrapW == SHADER_TEXWRAPMODE_CLAMP ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : t.wrapW == SHADER_TEXWRAPMODE_BORDER ? D3D12_TEXTURE_ADDRESS_MODE_BORDER :
		                                                                                                                                   D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = t.lodBias;
		sampler.MaxAnisotropy = bAnisotropic ? nSamplerAnisotropy : 1;
		sampler.ComparisonFunc = bComparison ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS;
		sampler.MaxLOD = t.minFilter <= SHADER_TEXFILTERMODE_LINEAR ? 0.f : D3D12_FLOAT32_MAX;
		sampler.MinLOD = MIN( static_cast<float>( MAX( 0, t.lodClamp ) ), sampler.MaxLOD );
		t.m_SamplerDescriptors[nSamplerSlot] = sampler;
		t.m_SamplerDescriptorValid[nSamplerSlot] = true;
		t.m_nSamplerDescriptorAnisotropy = nSamplerAnisotropy;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Resolves the bound color/depth handles into views and formats (cached by texture
//          identity) and transitions the targets for rendering
//-----------------------------------------------------------------------------
bool PrepareRenderTargetsDX12( CShaderAPIDX12 &api, RenderTargetBindingDX12 &binding, bool bEncodeSRGB )
{
	const bool bSRGB = bEncodeSRGB && api.EffectiveSRGBWrite();
	if ( !api.m_pDevice || !api.m_pDevice->CommandList() )
		return false;
	// Resolution (records, views, formats, validation) depends only on the bound handles, the sRGB view choice,
	// the view's scene targets and texture identity; state transitions and uploads below still run every call.
	CShaderAPIDX12::RenderTargetCacheDX12 &cache = api.m_RenderTargetCache;
	const bool bCacheHit = cache.valid && cache.identityEpoch == api.m_nTextureIdentityEpoch && cache.srgb == bSRGB && cache.depthTarget == api.m_hDepthTarget &&
	    !memcmp( cache.handles, api.m_RenderTargets, sizeof( cache.handles ) ) && cache.sceneColor == api.m_pDevice->SceneColor() && cache.sceneDepth == api.m_pDevice->SceneDepth() && cache.sceneRtv == api.m_pDevice->SceneRTV( bSRGB ).ptr;
	CShaderAPIDX12::TextureRecord **records = cache.records;
	CShaderAPIDX12::TextureRecord *&depth = cache.depth;
	if ( bCacheHit )
		binding = cache.binding;
	else
	{
		cache.valid = false;
		binding = {};
		memset( cache.records, 0, sizeof( cache.records ) );
		depth = nullptr;
		for ( int index = 0; index < RenderTargetBindingDX12::kMaxColorTargets; ++index )
		{
			const ShaderAPITextureHandle_t hTarget = api.m_RenderTargets[index];
			if ( hTarget == SHADER_RENDERTARGET_NONE )
			{
				for ( int later = index + 1; later < RenderTargetBindingDX12::kMaxColorTargets; ++later )
				{
					if ( api.m_RenderTargets[later] != SHADER_RENDERTARGET_NONE )
					{
						Warning( "ShaderAPIDX12: MRT hole slot %d before occupied slot %d=%lld\n", index, later, static_cast<long long>( api.m_RenderTargets[later] ) );
						return false;
					}
				}
				break;
			}
			if ( hTarget == SHADER_RENDERTARGET_BACKBUFFER )
			{
				if ( index != 0 )
				{
					Warning( "ShaderAPIDX12: backbuffer cannot occupy secondary MRT slot %d\n", index );
					return false;
				}
				binding.colors[index] = api.m_pDevice->SceneColor();
				binding.rtvs[index] = api.m_pDevice->SceneRTV( bSRGB );
				binding.colorFormats[index] = api.m_pDevice->SceneColorFormat( bSRGB );
			}
			else
			{
				CShaderAPIDX12::TextureRecord *pRecord = api.FindTexture( hTarget );
				if ( pRecord == nullptr || !( pRecord->flags & TEXTURE_CREATE_RENDERTARGET ) || ( pRecord->flags & TEXTURE_CREATE_DEPTHBUFFER ) || !pRecord->rtv.ptr || !api.EnsureTextureResident( *pRecord ) )
				{
					Warning( "ShaderAPIDX12: invalid MRT slot %d handle %lld existing=%d\n", index, static_cast<long long>( hTarget ), pRecord != nullptr );
					return false;
				}
				records[index] = pRecord;
				binding.colors[index] = records[index]->resource.Get();
				binding.rtvs[index] = bSRGB ? records[index]->rtvSRGB : records[index]->rtv;
			}
			if ( !binding.colors[index] )
				return false;
			const D3D12_RESOURCE_DESC &desc = api.m_TargetDescs[index].Get( binding.colors[index] );
			if ( records[index] )
				binding.colorFormats[index] = SRVFormat( desc.Format, bSRGB, false );
			if ( !index )
			{
				binding.width = static_cast<int>( desc.Width );
				binding.height = static_cast<int>( desc.Height );
				binding.sampleCount = desc.SampleDesc.Count;
				binding.sampleQuality = desc.SampleDesc.Quality;
			}
			else if ( desc.Width != static_cast<UINT64>( binding.width ) || desc.Height != static_cast<UINT>( binding.height ) || desc.SampleDesc.Count != static_cast<UINT>( binding.sampleCount ) || desc.SampleDesc.Quality != static_cast<UINT>( binding.sampleQuality ) )
			{
				Warning( "ShaderAPIDX12: MRT slot %d size/sample mismatch primary=%dx%d sample=%d/%d slot=%llux%u sample=%u/%u handle=%lld\n", index, binding.width, binding.height, binding.sampleCount, binding.sampleQuality, desc.Width, desc.Height, desc.SampleDesc.Count, desc.SampleDesc.Quality, static_cast<long long>( hTarget ) );
				return false;
			}
			for ( int previous = 0; previous < index; ++previous )
			{
				if ( binding.colors[previous] == binding.colors[index] )
				{
					Warning( "ShaderAPIDX12: duplicate MRT resource in slots %d and %d\n", previous, index );
					return false;
				}
			}
			++binding.colorCount;
		}
		binding.color = binding.colors[0];
		binding.rtv = binding.rtvs[0];
		binding.colorFormat = binding.colorFormats[0];
		if ( api.m_hDepthTarget == SHADER_RENDERTARGET_DEPTHBUFFER )
		{
			binding.depth = api.m_pDevice->SceneDepth();
			binding.dsv = api.m_pDevice->SceneDSV();
			binding.depthFormat = api.m_pDevice->SceneDepthFormat();
		}
		else if ( api.m_hDepthTarget > 0 )
		{
			CShaderAPIDX12::TextureRecord *pRecord = api.FindTexture( api.m_hDepthTarget );
			if ( pRecord == nullptr || !( pRecord->flags & TEXTURE_CREATE_DEPTHBUFFER ) || !pRecord->dsv.ptr || !api.EnsureTextureResident( *pRecord ) )
				return false;
			depth = pRecord;
			binding.depth = depth->resource.Get();
			binding.dsv = depth->dsv;
		}
		if ( binding.depth )
		{
			const D3D12_RESOURCE_DESC &desc = api.m_TargetDescs[RenderTargetBindingDX12::kMaxColorTargets].Get( binding.depth );
			if ( depth )
				binding.depthFormat = desc.Format == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_D32_FLOAT : desc.Format == DXGI_FORMAT_R16_TYPELESS ? DXGI_FORMAT_D16_UNORM :
				                                                                                                                                  DXGI_FORMAT_D24_UNORM_S8_UINT;
			if ( !binding.colorCount )
			{
				binding.width = static_cast<int>( desc.Width );
				binding.height = static_cast<int>( desc.Height );
				binding.sampleCount = desc.SampleDesc.Count;
				binding.sampleQuality = desc.SampleDesc.Quality;
			}
			// Source shares the full-size depth surface with smaller water and postprocess targets.
			else if ( desc.Width < static_cast<UINT64>( binding.width ) || desc.Height < static_cast<UINT>( binding.height ) || desc.SampleDesc.Count != static_cast<UINT>( binding.sampleCount ) || desc.SampleDesc.Quality != static_cast<UINT>( binding.sampleQuality ) )
			{
				static unsigned s_nMismatches = 0;
				if ( s_nMismatches++ < 8 )
					Warning( "ShaderAPIDX12: depth/color mismatch color=%dx%d samples=%d/%d depth=%llux%u samples=%u/%u colorHandle=%lld depthHandle=%lld\n", binding.width, binding.height, binding.sampleCount, binding.sampleQuality, desc.Width, desc.Height, desc.SampleDesc.Count, desc.SampleDesc.Quality, static_cast<long long>( api.m_RenderTargets[0] ), static_cast<long long>( api.m_hDepthTarget ) );
				return false;
			}
			for ( int index = 0; index < binding.colorCount; ++index )
			{
				if ( binding.depth == binding.colors[index] )
					return false;
			}
		}
		if ( !binding.colorCount && !binding.depth )
			return false;
		cache.binding = binding;
		cache.identityEpoch = api.m_nTextureIdentityEpoch;
		cache.srgb = bSRGB;
		cache.depthTarget = api.m_hDepthTarget;
		memcpy( cache.handles, api.m_RenderTargets, sizeof( cache.handles ) );
		cache.sceneColor = api.m_pDevice->SceneColor();
		cache.sceneDepth = api.m_pDevice->SceneDepth();
		cache.sceneRtv = api.m_pDevice->SceneRTV( bSRGB ).ptr;
		cache.valid = true;
	}
	auto flush = [&]( CShaderAPIDX12::TextureRecord &texture )
	{
		const int nIndex = texture.currentCopy * Faces( texture ) * texture.mipLevels;
		if ( !texture.dirtySubresources[nIndex] )
			return true;
		const size_t nBytes = MipBytes( texture.width, texture.height, 1, texture.format );
		if ( !CreateUpload( api.m_pDevice->NativeDevice(), api.m_pDevice->CommandList(), texture.resource.Get(), texture.pixels.Base() + SubresourceOffset( texture, 0, 0 ), nBytes, 0, texture.subresourceStates[nIndex], api.m_pDevice ) )
			return false;
		texture.dirtySubresources[nIndex] = 0;
		return true;
	};
	auto transition = [&]( CShaderAPIDX12::TextureRecord &texture, D3D12_RESOURCE_STATES desired )
	{
		D3D12_RESOURCE_STATES &state = texture.subresourceStates[texture.currentCopy * Faces( texture ) * texture.mipLevels];
		if ( state == desired )
			return;
		texture.sampledStateValid = false;
		++api.m_nTextureStateEpoch;
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture.resource.Get();
		barrier.Transition.Subresource = 0;
		barrier.Transition.StateBefore = state;
		barrier.Transition.StateAfter = desired;
		api.m_pDevice->CommandList()->ResourceBarrier( 1, &barrier );
		state = desired;
	};
	for ( int index = 0; index < binding.colorCount; ++index )
	{
		if ( records[index] )
		{
			if ( !flush( *records[index] ) )
				return false;
			transition( *records[index], D3D12_RESOURCE_STATE_RENDER_TARGET );
			records[index]->gpuAuthoritativeSubresources[records[index]->currentCopy * Faces( *records[index] ) * records[index]->mipLevels] = 1;
		}
		else
			api.m_pDevice->TransitionSceneColor( D3D12_RESOURCE_STATE_RENDER_TARGET );
	}
	if ( depth )
	{
		if ( !flush( *depth ) )
			return false;
		transition( *depth, D3D12_RESOURCE_STATE_DEPTH_WRITE );
		depth->gpuAuthoritativeSubresources[depth->currentCopy * Faces( *depth ) * depth->mipLevels] = 1;
	}
	else if ( binding.depth )
		api.m_pDevice->TransitionSceneDepth( D3D12_RESOURCE_STATE_DEPTH_WRITE );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Engine stencil operation as a D3D12 op (out-of-range values keep)
//-----------------------------------------------------------------------------
static D3D12_STENCIL_OP StencilOperationDX12( StencilOperation_t operation )
{
	return operation >= STENCILOPERATION_KEEP && operation <= STENCILOPERATION_DECR ? static_cast<D3D12_STENCIL_OP>( operation ) : D3D12_STENCIL_OP_KEEP;
}

//-----------------------------------------------------------------------------
// Purpose: Clears through a full-screen draw so color write masks, the stencil test and
//          stencil writes apply like the DX9 "obey stencil" clears
//-----------------------------------------------------------------------------
void CShaderAPIDX12::DrawMaskedClear( bool bRGB, bool bAlpha, bool bDepth, const D3D12_RECT *pRect, bool bStencilOnly )
{
	if ( !m_pDevice || !m_pDevice->CommandList() || ( !bRGB && !bAlpha && !bDepth && !bStencilOnly ) )
		return;
	RenderTargetBindingDX12 target{};
	if ( !PrepareRenderTargets( target, false ) || ( bDepth && !target.depth ) )
		return;
	if ( bStencilOnly && ( !m_bStencilEnabled || !target.depth || target.depthFormat != DXGI_FORMAT_D24_UNORM_S8_UINT ) )
		return;
	const UINT8 nStencilWriteMask = bStencilOnly ? m_nStencilWriteMask : 0;
	const D3D12_STENCIL_OP stencilPass = bStencilOnly ? StencilOperationDX12( m_StencilPassOp ) : D3D12_STENCIL_OP_KEEP;
	const D3D12_STENCIL_OP stencilFail = bStencilOnly ? StencilOperationDX12( m_StencilFailOp ) : D3D12_STENCIL_OP_KEEP;
	ID3D12Device *pNative = m_pDevice->NativeDevice();
	CCommandRecorderDX12 *pList = m_pDevice->CommandList();
	const UINT8 nMask = ( bRGB ? 7 : 0 ) | ( bAlpha ? 8 : 0 );
	const D3D12_COMPARISON_FUNC compare = m_bStencilEnabled && target.depth && target.depthFormat == DXGI_FORMAT_D24_UNORM_S8_UINT ? static_cast<D3D12_COMPARISON_FUNC>( StencilCompare() ) : D3D12_COMPARISON_FUNC_ALWAYS;
	if ( !m_pClearRoot )
	{
		D3D12_ROOT_PARAMETER parameter{};
		parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameter.Constants.Num32BitValues = 4;
		parameter.Constants.ShaderRegister = 0;
		parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC desc{};
		desc.NumParameters = 1;
		desc.pParameters = &parameter;
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		Microsoft::WRL::ComPtr<ID3DBlob> pBlob, pError;
		if ( FAILED( D3D12SerializeRootSignature( &desc, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pError ) ) || FAILED( pNative->CreateRootSignature( 0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS( &m_pClearRoot ) ) ) )
			return;
	}
	ID3D12PipelineState *pPipeline = nullptr;
	for ( int i = 0; i < m_ClearPasses.Count(); ++i )
	{
		const ClearPassDX12 &pass = m_ClearPasses[i];
		if ( !memcmp( pass.colors, target.colorFormats, sizeof( pass.colors ) ) && pass.colorCount == target.colorCount && pass.depth == target.depthFormat && pass.samples == static_cast<UINT>( target.sampleCount ) && pass.quality == static_cast<UINT>( target.sampleQuality ) && pass.mask == nMask && pass.stencilCompare == compare && pass.stencilReadMask == StencilReadMask() && pass.depthWrite == bDepth && pass.stencilOnly == bStencilOnly && pass.stencilWriteMask == nStencilWriteMask && pass.stencilPass == stencilPass && pass.stencilFail == stencilFail )
		{
			pPipeline = pass.pipeline.Get();
			break;
		}
	}
	if ( !pPipeline )
	{
		static const char vsSource[] = "float4 main(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),1,1); }";
		static const char psSource[] = "cbuffer Color:register(b0){float4 color;}\n#if TARGET_COUNT == 0\nvoid main(){}\n#else\nstruct Output{float4 a:SV_Target0;\n#if TARGET_COUNT > 1\nfloat4 b:SV_Target1;\n#endif\n#if TARGET_COUNT > 2\nfloat4 c:SV_Target2;\n#endif\n#if TARGET_COUNT > 3\nfloat4 d:SV_Target3;\n#endif\n}; Output main(){Output o;o.a=color;\n#if TARGET_COUNT > 1\no.b=color;\n#endif\n#if TARGET_COUNT > 2\no.c=color;\n#endif\n#if TARGET_COUNT > 3\no.d=color;\n#endif\nreturn o;}\n#endif\n";
		Microsoft::WRL::ComPtr<ID3DBlob> pVS, pPS, pError;
		char szColorCount[16];
		V_snprintf( szColorCount, sizeof( szColorCount ), "%d", target.colorCount );
		const D3D_SHADER_MACRO defines[] = { { "TARGET_COUNT", szColorCount }, { nullptr, nullptr } };
		if ( FAILED( D3DCompile( vsSource, sizeof( vsSource ) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pVS, &pError ) ) || ( target.colorCount && FAILED( D3DCompile( psSource, sizeof( psSource ) - 1, nullptr, defines, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pPS, &pError ) ) ) )
			return;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = m_pClearRoot.Get();
		desc.VS = { pVS->GetBufferPointer(), pVS->GetBufferSize() };
		if ( pPS )
			desc.PS = { pPS->GetBufferPointer(), pPS->GetBufferSize() };
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = target.colorCount;
		for ( int i = 0; i < target.colorCount; ++i )
		{
			desc.RTVFormats[i] = target.colorFormats[i];
			desc.BlendState.RenderTarget[i].RenderTargetWriteMask = nMask;
		}
		desc.DSVFormat = target.depth ? target.depthFormat : DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = target.sampleCount;
		desc.SampleDesc.Quality = target.sampleQuality;
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.RasterizerState.DepthClipEnable = TRUE;
		desc.RasterizerState.MultisampleEnable = target.sampleCount > 1;
		desc.DepthStencilState.DepthEnable = bDepth;
		desc.DepthStencilState.DepthWriteMask = bDepth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		desc.DepthStencilState.StencilEnable = target.depth && target.depthFormat == DXGI_FORMAT_D24_UNORM_S8_UINT && m_bStencilEnabled;
		desc.DepthStencilState.StencilReadMask = StencilReadMask();
		desc.DepthStencilState.StencilWriteMask = nStencilWriteMask;
		desc.DepthStencilState.FrontFace.StencilFunc = compare;
		desc.DepthStencilState.FrontFace.StencilFailOp = stencilFail;
		desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		desc.DepthStencilState.FrontFace.StencilPassOp = stencilPass;
		desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
		ClearPassDX12 pass{};
		pass.color = target.colorFormat;
		memcpy( pass.colors, target.colorFormats, sizeof( pass.colors ) );
		pass.colorCount = target.colorCount;
		pass.depth = target.depthFormat;
		pass.samples = target.sampleCount;
		pass.quality = target.sampleQuality;
		pass.mask = nMask;
		pass.stencilCompare = compare;
		pass.stencilReadMask = StencilReadMask();
		pass.depthWrite = bDepth;
		pass.stencilOnly = bStencilOnly;
		pass.stencilWriteMask = nStencilWriteMask;
		pass.stencilPass = stencilPass;
		pass.stencilFail = stencilFail;
		if ( FAILED( pNative->CreateGraphicsPipelineState( &desc, IID_PPV_ARGS( &pass.pipeline ) ) ) )
			return;
		pPipeline = pass.pipeline.Get();
		m_ClearPasses.AddToTail( pass );
	}
	D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>( target.width ), static_cast<float>( target.height ), 0, 1 };
	D3D12_RECT scissor = pRect ? *pRect : D3D12_RECT{ 0, 0, target.width, target.height };
	pList->RSSetViewports( 1, &viewport );
	pList->RSSetScissorRects( 1, &scissor );
	pList->OMSetRenderTargets( target.colorCount, target.colorCount ? target.rtvs : nullptr, FALSE, target.depth ? &target.dsv : nullptr );
	pList->OMSetStencilRef( StencilReference() );
	pList->SetGraphicsRootSignature( m_pClearRoot.Get() );
	pList->SetGraphicsRoot32BitConstants( 0, 4, m_ClearColor, 0 );
	pList->SetPipelineState( pPipeline );
	pList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	m_pDevice->GpuReceiverDraw( false );
	pList->DrawInstanced( 3, 1, 0, 0 );
	m_Pipeline.InvalidateGraphicsBindings();
}

//-----------------------------------------------------------------------------
// Purpose: Clears the bound targets, optionally limited to the top-left width x height
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ClearBuffers( bool color, bool depth, bool stencil, int width, int height )
{
	if ( !m_pDevice || !m_pDevice->CommandList() )
		return;
	RenderTargetBindingDX12 binding{};
	if ( !PrepareRenderTargets( binding, false ) )
		return;
	D3D12_RECT rect{ 0, 0, width > 0 ? MIN( width, binding.width ) : binding.width, height > 0 ? MIN( height, binding.height ) : binding.height };
	if ( color )
	{
		for ( int index = 0; index < binding.colorCount; ++index )
			m_pDevice->CommandList()->ClearRenderTargetView( binding.rtvs[index], m_ClearColor, 1, &rect );
	}
	if ( binding.depth )
	{
		const UINT nFlags = ( depth ? D3D12_CLEAR_FLAG_DEPTH : 0 ) | ( stencil && binding.depthFormat == DXGI_FORMAT_D24_UNORM_S8_UINT ? D3D12_CLEAR_FLAG_STENCIL : 0 );
		if ( nFlags )
			m_pDevice->CommandList()->ClearDepthStencilView( binding.dsv, static_cast<D3D12_CLEAR_FLAGS>( nFlags ), 1.0f, 0, 1, &rect );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Stencil-respecting clears
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ClearBuffersObeyStencil( bool color, bool depth )
{
	DrawMaskedClear( color, color, depth );
}

void CShaderAPIDX12::ClearBuffersObeyStencilEx( bool color, bool alpha, bool depth )
{
	DrawMaskedClear( color, alpha, depth );
}

//-----------------------------------------------------------------------------
// Purpose: Sets the stencil of a depth-target rectangle to value
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ClearStencilBufferRectangle( int x0, int y0, int x1, int y1, int value )
{
	if ( !m_pDevice || !m_pDevice->CommandList() )
		return;
	RenderTargetBindingDX12 target{};
	if ( !PrepareRenderTargets( target ) || !target.depth || target.depthFormat != DXGI_FORMAT_D24_UNORM_S8_UINT )
		return;
	D3D12_RECT rect{ MAX( 0, x0 ), MAX( 0, y0 ), MIN( target.width, x1 ), MIN( target.height, y1 ) };
	if ( rect.left < rect.right && rect.top < rect.bottom )
		m_pDevice->CommandList()->ClearDepthStencilView( target.dsv, D3D12_CLEAR_FLAG_STENCIL, 1.0f, static_cast<UINT8>( value ), 1, &rect );
}

//-----------------------------------------------------------------------------
// Purpose: Reads a tightly packed rectangle of the primary render target
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReadPixels( int x, int y, int width, int height, unsigned char *data, ImageFormat dst )
{
	Rect_t src{ x, y, width, height }, out{ 0, 0, width, height };
	ReadPixels( &src, &out, data, dst, width * static_cast<int>( BytesPerPixel( dst ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Reads srcRect of the primary render target into dstRect of data (stride bytes per row),
//          resolving, scaling or sRGB-encoding through a blit when needed
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReadPixels( Rect_t *srcRect, Rect_t *dstRect, unsigned char *data, ImageFormat format, int stride )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 ReadPixels", DX12_ZONES_ACTIVE );
	if ( !srcRect || !dstRect || !data || !m_pDevice || !m_pDevice->CommandList() || srcRect->width <= 0 || srcRect->height <= 0 || dstRect->width <= 0 || dstRect->height <= 0 )
		return;
	RenderTargetBindingDX12 binding{};
	if ( !PrepareRenderTargets( binding ) || !binding.color )
		return;
	if ( srcRect->x < 0 || srcRect->y < 0 || srcRect->x + srcRect->width > binding.width || srcRect->y + srcRect->height > binding.height || dstRect->x < 0 || dstRect->y < 0 || stride < ( dstRect->x + dstRect->width ) * static_cast<int>( BytesPerPixel( format ) ) )
		return;
	const bool bScene = m_hRenderTarget == SHADER_RENDERTARGET_BACKBUFFER;
	TextureRecord *pRecord = nullptr;
	if ( !bScene )
	{
		pRecord = FindTexture( m_hRenderTarget );
		if ( pRecord == nullptr )
			return;
	}
	const ImageFormat sourceFormat = bScene ? CShaderDeviceDX12::kSceneImageFormat : pRecord->format;
	ID3D12Resource *pSource = binding.color;
	D3D12_RESOURCE_STATES sceneState = D3D12_RESOURCE_STATE_RENDER_TARGET, temporaryState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	D3D12_RESOURCE_STATES *pState = pRecord ? &pRecord->subresourceStates[pRecord->currentCopy * Faces( *pRecord ) * pRecord->mipLevels] : &sceneState;
	Microsoft::WRL::ComPtr<ID3D12Resource> pTemporary;
	Rect_t readRect = *srcRect;
	// FP16 targets hold linear scRGB; 8-bit readers get sRGB bytes from a hardware-encoded blit, not a raw conversion.
	const bool bEncodeSRGB = pSource->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT && format != IMAGE_FORMAT_RGBA16161616F;
	if ( bEncodeSRGB || pSource->GetDesc().SampleDesc.Count > 1 || srcRect->width != dstRect->width || srcRect->height != dstRect->height )
	{
		D3D12_RESOURCE_DESC desc = pSource->GetDesc();
		desc.Width = dstRect->width;
		desc.Height = dstRect->height;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Alignment = 0;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		if ( bEncodeSRGB )
			desc.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		const HRESULT hr = m_pDevice->NativeDevice()->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, temporaryState, nullptr, IID_PPV_ARGS( &pTemporary ) );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12: ReadPixels temporary render texture creation failed (0x%08x), alignment %llu format %u\n", static_cast<unsigned>( hr ), static_cast<unsigned long long>( desc.Alignment ), static_cast<unsigned>( desc.Format ) );
			return;
		}
		const Rect_t region{ 0, 0, dstRect->width, dstRect->height };
		const bool bBlitted = BlitTexture( pSource, *pState, bScene, sourceFormat, pTemporary.Get(), temporaryState, false, bEncodeSRGB ? IMAGE_FORMAT_BGRA8888 : sourceFormat, *srcRect, region, false, bEncodeSRGB, nullptr );
		if ( bScene )
			m_pDevice->TransitionSceneColor( D3D12_RESOURCE_STATE_RENDER_TARGET );
		if ( !bBlitted )
		{
			Warning( "ShaderAPIDX12: ReadPixels resolve, scaling or sRGB encode blit failed\n" );
			return;
		}
		m_pDevice->RetainResource( pTemporary.Get() );
		pSource = pTemporary.Get();
		pState = &temporaryState;
		readRect = region;
	}
	const D3D12_RESOURCE_DESC td = pSource->GetDesc();
	UINT nRows = 0;
	UINT64 nBytes = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
	m_pDevice->NativeDevice()->GetCopyableFootprints( &td, 0, 1, 0, &fp, &nRows, nullptr, &nBytes );
	D3D12_HEAP_PROPERTIES hp{};
	hp.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC bd{};
	bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bd.Width = nBytes;
	bd.Height = 1;
	bd.DepthOrArraySize = 1;
	bd.MipLevels = 1;
	bd.SampleDesc.Count = 1;
	bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> pReadback;
	if ( FAILED( m_pDevice->NativeDevice()->CreateCommittedResource( &hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &pReadback ) ) ) )
		return;
	if ( bScene && !pTemporary )
		m_pDevice->TransitionSceneColor( D3D12_RESOURCE_STATE_COPY_SOURCE );
	else if ( *pState != D3D12_RESOURCE_STATE_COPY_SOURCE )
	{
		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = pSource;
		b.Transition.Subresource = 0;
		b.Transition.StateBefore = *pState;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		m_pDevice->CommandList()->ResourceBarrier( 1, &b );
		*pState = D3D12_RESOURCE_STATE_COPY_SOURCE;
	}
	D3D12_TEXTURE_COPY_LOCATION from{}, to{};
	from.pResource = pSource;
	from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	to.pResource = pReadback.Get();
	to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	to.PlacedFootprint = fp;
	m_pDevice->CommandList()->CopyTextureRegion( &to, 0, 0, 0, &from, nullptr );
	if ( bScene )
		m_pDevice->TransitionSceneColor( D3D12_RESOURCE_STATE_RENDER_TARGET );
	m_pDevice->RetainResource( pReadback.Get() );
	if ( !m_pDevice->Submit( true ) )
		return;
	void *pMapped = nullptr;
	D3D12_RANGE range{ 0, nBytes };
	if ( FAILED( pReadback->Map( 0, &range, &pMapped ) ) )
		return;
	const ImageFormat nativeFormat = DXGI12ToImageFormat( td.Format );
	if ( nativeFormat == IMAGE_FORMAT_UNKNOWN )
	{
		Warning( "ShaderAPIDX12: ReadPixels unsupported native source format %u\n", static_cast<unsigned>( td.Format ) );
		pReadback->Unmap( 0, nullptr );
		return;
	}
	const size_t nSrcPixelBytes = BytesPerPixel( nativeFormat ), nDstPixelBytes = BytesPerPixel( format );
	const unsigned char *pBase = static_cast<const unsigned char *>( pMapped ) + fp.Offset;
	for ( int nRow = 0; nRow < dstRect->height; ++nRow )
	{
		const unsigned char *pSourceRow = pBase + static_cast<size_t>( readRect.y + nRow ) * fp.Footprint.RowPitch + static_cast<size_t>( readRect.x ) * nSrcPixelBytes;
		unsigned char *pDestRow = data + static_cast<size_t>( dstRect->y + nRow ) * stride + static_cast<size_t>( dstRect->x ) * nDstPixelBytes;
		if ( format == nativeFormat )
			memcpy( pDestRow, pSourceRow, static_cast<size_t>( dstRect->width ) * nDstPixelBytes );
		else if ( !m_pShaderUtil || !m_pShaderUtil->ConvertImageFormat( const_cast<unsigned char *>( pSourceRow ), nativeFormat, pDestRow, format, dstRect->width, 1, static_cast<int>( fp.Footprint.RowPitch ), stride ) )
		{
			pReadback->Unmap( 0, nullptr );
			return;
		}
	}
	pReadback->Unmap( 0, nullptr );
}

//-----------------------------------------------------------------------------
// Purpose: Maps a rectangle of a texture's staging copy for CPU access
//-----------------------------------------------------------------------------
void CShaderAPIDX12::LockRect( void **outBits, int *pitch, ShaderAPITextureHandle_t handle, int mip, int x, int y, int width, int height, bool write, bool read )
{
	if ( outBits )
		*outBits = nullptr;
	if ( pitch )
		*pitch = 0;
	TextureRecord *pRecord = FindTexture( handle );
	if ( pRecord == nullptr )
		return;
	TextureRecord &texture = *pRecord;
	if ( texture.lockLevel >= 0 || mip < 0 || mip >= texture.mipLevels || x < 0 || y < 0 || width <= 0 || height <= 0 || IsBlockCompressed( texture.format ) )
		return;
	const int nFullWidth = MAX( 1, texture.width >> mip ), nFullHeight = MAX( 1, texture.height >> mip );
	if ( x + width > nFullWidth || y + height > nFullHeight )
		return;
	const int nIndex = texture.currentCopy * Faces( texture ) * texture.mipLevels + mip;
	if ( read )
	{
		texture.gpuAuthoritativeSubresources[nIndex] = 1;
		if ( !RefreshTextureStaging( texture, 0, mip ) )
			return;
	}
	else if ( texture.gpuAuthoritativeSubresources[nIndex] && !RefreshTextureStaging( texture, 0, mip ) )
		return;
	const size_t nBytesPerPixel = BytesPerPixel( texture.format ), nRowBytes = static_cast<size_t>( width ) * nBytesPerPixel;
	texture.lockData.SetCountNonDestructively( static_cast<int>( nRowBytes * height ) );
	const unsigned char *pSource = texture.pixels.Base() + SubresourceOffset( texture, 0, mip ) + ( static_cast<size_t>( y ) * nFullWidth + x ) * nBytesPerPixel;
	for ( int nRow = 0; nRow < height; ++nRow )
		memcpy( texture.lockData.Base() + static_cast<size_t>( nRow ) * nRowBytes, pSource + static_cast<size_t>( nRow ) * nFullWidth * nBytesPerPixel, nRowBytes );
	texture.lockLevel = mip;
	texture.lockFace = 0;
	texture.lockX = x;
	texture.lockY = y;
	texture.lockWidth = width;
	texture.lockHeight = height;
	texture.lockPitch = static_cast<int>( nRowBytes );
	texture.lockWrite = write;
	texture.lockRead = read;
	if ( outBits )
		*outBits = texture.lockData.Base();
	if ( pitch )
		*pitch = texture.lockPitch;
}

//-----------------------------------------------------------------------------
// Purpose: Uploads a write-locked rectangle and releases the lock
//-----------------------------------------------------------------------------
void CShaderAPIDX12::UnlockRect( ShaderAPITextureHandle_t h, int )
{
	TextureRecord *pRecord = FindTexture( h );
	if ( pRecord == nullptr || pRecord->lockLevel < 0 )
		return;
	m_hModifiedTexture = h;
	if ( pRecord->lockWrite )
		TexSubImage2D( pRecord->lockLevel, pRecord->lockFace, pRecord->lockX, pRecord->lockY, 0, pRecord->lockWidth, pRecord->lockHeight, pRecord->format, pRecord->lockPitch, false, pRecord->lockData.Base() );
	pRecord->lockLevel = -1;
	pRecord->lockData.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Copies a source rectangle into a destination rectangle: a plain copy when formats
//          and sizes match, otherwise a filtered draw (optionally resolving MSAA first and
//          applying the present gamma ramp)
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::BlitTexture( ID3D12Resource *pSource, D3D12_RESOURCE_STATES &sourceState, bool bSourceIsScene, ImageFormat sourceFormat, ID3D12Resource *pDestination, D3D12_RESOURCE_STATES &destinationState, bool bDestinationIsScene, ImageFormat destinationFormat, Rect_t sourceRect, Rect_t destinationRect, bool bSourceSRGB, bool bDestinationSRGB, const float *pGammaCoefficients, BlitEncodeDX12 encode )
{
	if ( !m_pDevice || !m_pDevice->CommandList() || !pSource || !pDestination || pSource == pDestination || sourceRect.width <= 0 || sourceRect.height <= 0 || destinationRect.width <= 0 || destinationRect.height <= 0 )
		return false;
	ID3D12Device *pNative = m_pDevice->NativeDevice();
	CCommandRecorderDX12 *pList = m_pDevice->CommandList();
	const D3D12_RESOURCE_DESC sourceDesc = pSource->GetDesc(), destinationDesc = pDestination->GetDesc();
	if ( sourceRect.x < 0 || sourceRect.y < 0 || sourceRect.x + sourceRect.width > static_cast<int>( sourceDesc.Width ) || sourceRect.y + sourceRect.height > static_cast<int>( sourceDesc.Height ) || destinationRect.x < 0 || destinationRect.y < 0 || destinationRect.x + destinationRect.width > static_cast<int>( destinationDesc.Width ) || destinationRect.y + destinationRect.height > static_cast<int>( destinationDesc.Height ) )
		return false;
	auto transition = [&]( ID3D12Resource *pResource, D3D12_RESOURCE_STATES &state, bool bScene, D3D12_RESOURCE_STATES desired, bool bIsSource )
	{
		if ( bScene )
		{
			m_pDevice->TransitionSceneColor( desired );
			return;
		}
		if ( state == desired )
			return;
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = pResource;
		barrier.Transition.Subresource = 0;
		barrier.Transition.StateBefore = state;
		barrier.Transition.StateAfter = desired;
		pList->ResourceBarrier( 1, &barrier );
		state = desired;
	};
	ResolveTextureRecord *pResolvedRecord = nullptr;
	D3D12_RESOURCE_STATES *pActiveSourceState = &sourceState;
	if ( sourceDesc.SampleDesc.Count > 1 )
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 BlitResolve", DX12_ZONES_ACTIVE );
		const D3D12_RESOURCE_DESC desc = NormalizeResolveDesc( sourceDesc );
		HRESULT hr = S_OK;
		pResolvedRecord = AcquireResolveTexture( desc, hr );
		if ( !pResolvedRecord )
		{
			Warning( "ShaderAPIDX12: MSAA resolve texture creation failed (0x%08x), alignment %llu format %u\n", static_cast<unsigned>( hr ), static_cast<unsigned long long>( desc.Alignment ), static_cast<unsigned>( desc.Format ) );
			return false;
		}
		transition( pSource, sourceState, bSourceIsScene, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, true );
		transition( pResolvedRecord->resource.Get(), pResolvedRecord->state, false, D3D12_RESOURCE_STATE_RESOLVE_DEST, false );
		pList->ResolveSubresource( pResolvedRecord->resource.Get(), 0, pSource, 0, SRVFormat( sourceDesc.Format, false, false ) );
		pSource = pResolvedRecord->resource.Get();
		pActiveSourceState = &pResolvedRecord->state;
		bSourceIsScene = false;
	}
	const bool bSameSize = sourceRect.width == destinationRect.width && sourceRect.height == destinationRect.height;
	if ( encode == BlitEncodeDX12::None && bSameSize && pSource->GetDesc().Format == destinationDesc.Format && destinationDesc.SampleDesc.Count == 1 && bSourceSRGB == bDestinationSRGB )
	{
		transition( pSource, *pActiveSourceState, bSourceIsScene, D3D12_RESOURCE_STATE_COPY_SOURCE, true );
		transition( pDestination, destinationState, bDestinationIsScene, D3D12_RESOURCE_STATE_COPY_DEST, false );
		D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
		src.pResource = pSource;
		src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.pResource = pDestination;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		D3D12_BOX box{ static_cast<UINT>( sourceRect.x ), static_cast<UINT>( sourceRect.y ), 0, static_cast<UINT>( sourceRect.x + sourceRect.width ), static_cast<UINT>( sourceRect.y + sourceRect.height ), 1 };
		pList->CopyTextureRegion( &dst, destinationRect.x, destinationRect.y, 0, &src, &box );
		transition( pDestination, destinationState, bDestinationIsScene, bDestinationIsScene ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false );
		return true;
	}
	const DXGI_FORMAT renderFormat = SRVFormat( destinationDesc.Format, bDestinationSRGB, false );
	if ( renderFormat == DXGI_FORMAT_UNKNOWN || IsBlockCompressed( destinationFormat ) || IsBlockCompressed( sourceFormat ) )
		return false;
	Microsoft::WRL::ComPtr<ID3D12Resource> pScratch;
	const bool bDirect = ( destinationDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET ) != 0;
	ID3D12Resource *pRenderResource = pDestination;
	D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
	if ( !bDirect )
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 BlitScratchAllocation", DX12_ZONES_ACTIVE );
		D3D12_RESOURCE_DESC desc = destinationDesc;
		desc.Width = destinationRect.width;
		desc.Height = destinationRect.height;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Alignment = 0;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		if ( FAILED( pNative->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS( &pScratch ) ) ) )
			return false;
		pRenderResource = pScratch.Get();
		m_pDevice->RetainResource( pRenderResource );
	}
	else
		transition( pDestination, destinationState, bDestinationIsScene, D3D12_RESOURCE_STATE_RENDER_TARGET, false );
	if ( !m_pBlitRtvHeap )
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
		rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvDesc.NumDescriptors = kBlitRtvSlots;
		if ( FAILED( pNative->CreateDescriptorHeap( &rtvDesc, IID_PPV_ARGS( &m_pBlitRtvHeap ) ) ) )
			return false;
		m_nBlitRtvSlot = 0;
	}
	// Replay reads the RTV later; wrapping the ring first replays every recorded blit that names its slots.
	if ( m_nBlitRtvSlot == kBlitRtvSlots )
	{
		m_pDevice->DrainRecording();
		m_nBlitRtvSlot = 0;
	}
	rtv = m_pBlitRtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtv.ptr += static_cast<SIZE_T>( m_nBlitRtvSlot++ ) * pNative->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
	D3D12_RENDER_TARGET_VIEW_DESC view{};
	view.Format = renderFormat;
	view.ViewDimension = destinationDesc.SampleDesc.Count > 1 && bDirect ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
	pNative->CreateRenderTargetView( pRenderResource, &view, rtv );
	if ( !m_pBlitRoot )
	{
		D3D12_DESCRIPTOR_RANGE ranges[2]{};
		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[0].NumDescriptors = 1;
		ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		ranges[1].NumDescriptors = 1;
		D3D12_ROOT_PARAMETER params[3]{};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[0].DescriptorTable.NumDescriptorRanges = 1;
		params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[1].DescriptorTable.NumDescriptorRanges = 1;
		params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		params[2].Constants.Num32BitValues = 8;
		params[2].Constants.ShaderRegister = 0;
		params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC desc{};
		desc.NumParameters = 3;
		desc.pParameters = params;
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		Microsoft::WRL::ComPtr<ID3DBlob> pBlob, pError;
		HRESULT hr = D3D12SerializeRootSignature( &desc, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pError );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12: native blit root signature serialization failed (0x%08x): %s\n", static_cast<unsigned>( hr ), pError ? static_cast<const char *>( pError->GetBufferPointer() ) : "unknown" );
			return false;
		}
		hr = pNative->CreateRootSignature( 0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS( &m_pBlitRoot ) );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12: native blit root signature creation failed (0x%08x)\n", static_cast<unsigned>( hr ) );
			return false;
		}
	}
	const UINT nSamples = bDirect ? destinationDesc.SampleDesc.Count : 1, nQuality = bDirect ? destinationDesc.SampleDesc.Quality : 0;
	ID3D12PipelineState *pPSO = nullptr;
	for ( int i = 0; i < m_BlitPasses.Count(); ++i )
	{
		const BlitPassDX12 &pass = m_BlitPasses[i];
		if ( pass.color == renderFormat && pass.samples == nSamples && pass.quality == nQuality && pass.encode == encode )
		{
			pPSO = pass.pipeline.Get();
			break;
		}
	}
	if ( !pPSO )
	{
		static const char vsSource[] = "float4 main(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1); }";
		static const char psSource[] = "Texture2D image:register(t0); SamplerState sampleState:register(s0); cbuffer Region:register(b0){float4 uv;\n#if defined(PRESENT_GAMMA)||defined(PRESENT_HDR_SCALE)\nfloat4 gamma;\n#endif\n} float4 main(float4 pos:SV_Position):SV_Target {float4 color=image.SampleLevel(sampleState, pos.xy*uv.xy+uv.zw,0);\n#ifdef PRESENT_GAMMA\ncolor.rgb=pow(saturate(color.rgb),gamma.xxx); color.rgb=pow(saturate(color.rgb),gamma.yyy); color.rgb=saturate(color.rgb*gamma.z+gamma.w);\n#endif\n#ifdef PRESENT_HDR_SCALE\ncolor.rgb*=gamma.x;\n#endif\nreturn color;}";
		Microsoft::WRL::ComPtr<ID3DBlob> pVS, pPS, pError;
		const D3D_SHADER_MACRO gammaDefines[] = { { "PRESENT_GAMMA", "1" }, { nullptr, nullptr } };
		const D3D_SHADER_MACRO hdrDefines[] = { { "PRESENT_HDR_SCALE", "1" }, { nullptr, nullptr } };
		HRESULT hr = D3DCompile( vsSource, sizeof( vsSource ) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pVS, &pError );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12: native blit VS compile failed (0x%08x): %s\n", static_cast<unsigned>( hr ), pError ? static_cast<const char *>( pError->GetBufferPointer() ) : "unknown" );
			return false;
		}
		pError.Reset();
		hr = D3DCompile( psSource, sizeof( psSource ) - 1, nullptr, encode == BlitEncodeDX12::Gamma ? gammaDefines : encode == BlitEncodeDX12::HdrScale ? hdrDefines : nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pPS, &pError );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12: native blit PS compile failed (0x%08x): %s\n", static_cast<unsigned>( hr ), pError ? static_cast<const char *>( pError->GetBufferPointer() ) : "unknown" );
			return false;
		}
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = m_pBlitRoot.Get();
		desc.VS = { pVS->GetBufferPointer(), pVS->GetBufferSize() };
		desc.PS = { pPS->GetBufferPointer(), pPS->GetBufferSize() };
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = renderFormat;
		desc.SampleDesc.Count = nSamples;
		desc.SampleDesc.Quality = nQuality;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.RasterizerState.DepthClipEnable = TRUE;
		desc.RasterizerState.MultisampleEnable = nSamples > 1;
		desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.DepthStencilState.DepthEnable = FALSE;
		desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
		D3D12_RENDER_TARGET_BLEND_DESC &blend = desc.BlendState.RenderTarget[0];
		blend.SrcBlend = D3D12_BLEND_ONE;
		blend.DestBlend = D3D12_BLEND_ZERO;
		blend.BlendOp = D3D12_BLEND_OP_ADD;
		blend.SrcBlendAlpha = D3D12_BLEND_ONE;
		blend.DestBlendAlpha = D3D12_BLEND_ZERO;
		blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blend.LogicOp = D3D12_LOGIC_OP_NOOP;
		BlitPassDX12 pass{};
		pass.color = renderFormat;
		pass.samples = nSamples;
		pass.quality = nQuality;
		pass.encode = encode;
		hr = pNative->CreateGraphicsPipelineState( &desc, IID_PPV_ARGS( &pass.pipeline ) );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12: native blit PSO failed (0x%08x), format %u samples %u encode %d\n", static_cast<unsigned>( hr ), static_cast<unsigned>( renderFormat ), nSamples, static_cast<int>( pass.encode ) );
			return false;
		}
		pPSO = pass.pipeline.Get();
		m_BlitPasses.AddToTail( pass );
	}
	DescriptorRangeDX12 srvRange;
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 BlitDescriptorAllocation", DX12_ZONES_ACTIVE );
		srvRange = m_Pipeline.AllocateTransientResources( 1, m_pDevice->NextFenceValue() );
		if ( srvRange.count != 1 || !m_Pipeline.LinearClampSampler().ptr )
			return false;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = SRVFormat( pSource->GetDesc().Format, bSourceSRGB, false );
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	pNative->CreateShaderResourceView( pSource, &srv, srvRange.cpu );
	transition( pSource, *pActiveSourceState, bSourceIsScene, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true );
	D3D12_VIEWPORT viewport{ static_cast<float>( bDirect ? destinationRect.x : 0 ), static_cast<float>( bDirect ? destinationRect.y : 0 ), static_cast<float>( destinationRect.width ), static_cast<float>( destinationRect.height ), 0, 1 };
	D3D12_RECT scissor{ static_cast<LONG>( viewport.TopLeftX ), static_cast<LONG>( viewport.TopLeftY ), static_cast<LONG>( viewport.TopLeftX + viewport.Width ), static_cast<LONG>( viewport.TopLeftY + viewport.Height ) };
	const float flUV[] = { static_cast<float>( sourceRect.width ) / ( sourceDesc.Width * destinationRect.width ), static_cast<float>( sourceRect.height ) / ( sourceDesc.Height * destinationRect.height ), static_cast<float>( sourceRect.x ) / sourceDesc.Width - viewport.TopLeftX * static_cast<float>( sourceRect.width ) / ( sourceDesc.Width * destinationRect.width ), static_cast<float>( sourceRect.y ) / sourceDesc.Height - viewport.TopLeftY * static_cast<float>( sourceRect.height ) / ( sourceDesc.Height * destinationRect.height ) };
	ID3D12DescriptorHeap *pHeaps[] = { m_Pipeline.ResourceDescriptorHeap(), m_Pipeline.SamplerDescriptorHeap() };
	pList->SetDescriptorHeaps( 2, pHeaps );
	pList->RSSetViewports( 1, &viewport );
	pList->RSSetScissorRects( 1, &scissor );
	pList->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );
	pList->SetGraphicsRootSignature( m_pBlitRoot.Get() );
	pList->SetGraphicsRootDescriptorTable( 0, srvRange.gpu );
	pList->SetGraphicsRootDescriptorTable( 1, m_Pipeline.LinearClampSampler() );
	pList->SetGraphicsRoot32BitConstants( 2, 4, flUV, 0 );
	if ( encode != BlitEncodeDX12::None && pGammaCoefficients )
		pList->SetGraphicsRoot32BitConstants( 2, 4, pGammaCoefficients, 4 );
	pList->SetPipelineState( pPSO );
	pList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	m_pDevice->GpuReceiverDraw( false );
	pList->DrawInstanced( 3, 1, 0, 0 );
	m_Pipeline.InvalidateGraphicsBindings();
	if ( pScratch )
	{
		D3D12_RESOURCE_STATES scratchState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		transition( pScratch.Get(), scratchState, false, D3D12_RESOURCE_STATE_COPY_SOURCE, true );
		transition( pDestination, destinationState, bDestinationIsScene, D3D12_RESOURCE_STATE_COPY_DEST, false );
		D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
		src.pResource = pScratch.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.pResource = pDestination;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		pList->CopyTextureRegion( &dst, destinationRect.x, destinationRect.y, 0, &src, nullptr );
		transition( pDestination, destinationState, bDestinationIsScene, bDestinationIsScene ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Presentation encode: draws (or copies) the scene into `pDestination`, a scene-sized single-sample
//          FP16 or R8G8B8A8_UNORM texture. 8-bit destinations get the hardware sRGB encode through an
//          _SRGB render-target view; the windowed gamma/TV-range ramp is folded in when the device has one.
//          `destinationState` is updated to the state the destination is left in.
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::EncodeSceneTo( ID3D12Resource *pDestination, D3D12_RESOURCE_STATES &destinationState )
{
	if ( !m_pDevice || !m_pDevice->CommandList() || !m_pDevice->SceneColor() || !pDestination )
		return false;
	const D3D12_RESOURCE_DESC sceneDesc = m_pDevice->SceneColor()->GetDesc(), destinationDesc = pDestination->GetDesc();
	const bool bEightBit = destinationDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM;
	if ( sceneDesc.Width != destinationDesc.Width || sceneDesc.Height != destinationDesc.Height || destinationDesc.SampleDesc.Count != 1 || ( !bEightBit && destinationDesc.Format != m_pDevice->SceneColorFormat() ) )
		return false;
	float flGamma[4];
	const float *pGamma = m_pDevice->PresentGammaCoefficients( flGamma ) ? flGamma : nullptr;
	const float flScale = PresentOutputScale();
	float flScaleCoefficients[4] = { flScale, 0.f, 0.f, 0.f };
	const BlitEncodeDX12 encode = flScale != 1.f ? BlitEncodeDX12::HdrScale : pGamma ? BlitEncodeDX12::Gamma : BlitEncodeDX12::None;
	const float *pEncodeCoefficients = flScale != 1.f ? flScaleCoefficients : pGamma;
	const ImageFormat destinationFormat = bEightBit ? IMAGE_FORMAT_RGBA8888 : CShaderDeviceDX12::kSceneImageFormat;
	D3D12_RESOURCE_STATES sceneState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	const Rect_t area{ 0, 0, static_cast<int>( destinationDesc.Width ), static_cast<int>( destinationDesc.Height ) };
	const bool bResult = BlitTexture( m_pDevice->SceneColor(), sceneState, true, CShaderDeviceDX12::kSceneImageFormat, pDestination, destinationState, false, destinationFormat, area, area, false, bEightBit, pEncodeCoefficients, encode );
	m_pDevice->TransitionSceneColor( D3D12_RESOURCE_STATE_RENDER_TARGET );
	return bResult;
}

//-----------------------------------------------------------------------------
// Purpose: Copies between textures and/or the scene color target, uploading pending CPU data
//          of the source first
//-----------------------------------------------------------------------------
void CShaderAPIDX12::CopyTextureRegionDX12( ShaderAPITextureHandle_t hSource, ShaderAPITextureHandle_t hDestination, Rect_t *pSourceRect, Rect_t *pDestinationRect )
{
	if ( !m_pDevice || !m_pDevice->CommandList() || hSource == hDestination )
		return;
	ID3D12Resource *pSrcResource = nullptr, *pDstResource = nullptr;
	TextureRecord *pSrcRecord = nullptr, *pDstRecord = nullptr;
	const bool bSceneSource = hSource == SHADER_RENDERTARGET_BACKBUFFER, bSceneDestination = hDestination == SHADER_RENDERTARGET_BACKBUFFER;
	if ( bSceneSource )
		pSrcResource = m_pDevice->SceneColor();
	else
	{
		pSrcRecord = FindTexture( hSource );
		if ( pSrcRecord == nullptr )
			return;
		if ( !EnsureTextureResident( *pSrcRecord ) )
			return;
		pSrcResource = pSrcRecord->resource.Get();
	}
	if ( bSceneDestination )
		pDstResource = m_pDevice->SceneColor();
	else
	{
		pDstRecord = FindTexture( hDestination );
		if ( pDstRecord == nullptr )
			return;
		if ( !EnsureTextureResident( *pDstRecord ) )
			return;
		pDstResource = pDstRecord->resource.Get();
	}
	if ( !pSrcResource || !pDstResource || pSrcResource == pDstResource )
		return;
	const D3D12_RESOURCE_DESC srcDesc = pSrcResource->GetDesc(), dstDesc = pDstResource->GetDesc();
	if ( srcDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || dstDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || srcDesc.DepthOrArraySize != 1 || dstDesc.DepthOrArraySize != 1 || ( pSrcRecord && ( pSrcRecord->flags & TEXTURE_CREATE_DEPTHBUFFER ) ) || ( pDstRecord && ( pDstRecord->flags & TEXTURE_CREATE_DEPTHBUFFER ) ) )
		return;
	if ( pSrcRecord && pSrcRecord->gpuDirty && !IsBoundColorTarget( m_RenderTargets, hSource ) )
	{
		const int nSubCount = Faces( *pSrcRecord ) * pSrcRecord->mipLevels;
		const int nBase = pSrcRecord->currentCopy * nSubCount;
		for ( int nSub = 0; nSub < nSubCount; ++nSub )
		{
			if ( pSrcRecord->dirtySubresources[nBase + nSub] )
			{
				const int nMip = nSub % pSrcRecord->mipLevels, nFace = nSub / pSrcRecord->mipLevels;
				const size_t nBytes = MipBytes( MAX( 1, pSrcRecord->width >> nMip ), MAX( 1, pSrcRecord->height >> nMip ), 1, pSrcRecord->format );
				if ( !CreateUpload( m_pDevice->NativeDevice(), m_pDevice->CommandList(), pSrcResource, pSrcRecord->pixels.Base() + SubresourceOffset( *pSrcRecord, nFace, nMip ), nBytes, nSub, pSrcRecord->subresourceStates[nBase + nSub], m_pDevice ) )
					return;
				pSrcRecord->dirtySubresources[nBase + nSub] = 0;
			}
		}
		pSrcRecord->gpuDirty = HasDirtySubresource( *pSrcRecord );
	}
	Rect_t sourceRect = pSourceRect ? *pSourceRect : Rect_t{ 0, 0, static_cast<int>( srcDesc.Width ), static_cast<int>( srcDesc.Height ) };
	Rect_t destinationRect = pDestinationRect ? *pDestinationRect : Rect_t{ 0, 0, static_cast<int>( dstDesc.Width ), static_cast<int>( dstDesc.Height ) };
	D3D12_RESOURCE_STATES sceneSourceState = D3D12_RESOURCE_STATE_RENDER_TARGET, sceneDestinationState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	D3D12_RESOURCE_STATES &fromState = pSrcRecord ? pSrcRecord->subresourceStates[pSrcRecord->currentCopy * Faces( *pSrcRecord ) * pSrcRecord->mipLevels] : sceneSourceState;
	D3D12_RESOURCE_STATES &toState = pDstRecord ? pDstRecord->subresourceStates[pDstRecord->currentCopy * Faces( *pDstRecord ) * pDstRecord->mipLevels] : sceneDestinationState;
	const ImageFormat fromFormat = pSrcRecord ? pSrcRecord->format : CShaderDeviceDX12::kSceneImageFormat, toFormat = pDstRecord ? pDstRecord->format : CShaderDeviceDX12::kSceneImageFormat;
	if ( BlitTexture( pSrcResource, fromState, bSceneSource, fromFormat, pDstResource, toState, bSceneDestination, toFormat, sourceRect, destinationRect, ( pSrcRecord && ( pSrcRecord->flags & TEXTURE_CREATE_SRGB ) != 0 ), ( pDstRecord && ( pDstRecord->flags & TEXTURE_CREATE_SRGB ) != 0 ) ) )
	{
		if ( pDstRecord )
		{
			const int nSub = pDstRecord->currentCopy * Faces( *pDstRecord ) * pDstRecord->mipLevels;
			pDstRecord->dirtySubresources[nSub] = 0;
			pDstRecord->initializedSubresources[nSub] = 0;
			pDstRecord->gpuAuthoritativeSubresources[nSub] = 1;
			pDstRecord->gpuDirty = HasDirtySubresource( *pDstRecord );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Render-target copy entry points; all go through CopyTextureRegionDX12
//-----------------------------------------------------------------------------
void CShaderAPIDX12::CopyRenderTargetToTextureEx( ShaderAPITextureHandle_t destination, int targetID, Rect_t *sourceRect, Rect_t *destinationRect )
{
	if ( targetID >= 0 && targetID < static_cast<int>( ARRAYSIZE( m_RenderTargets ) ) )
		CopyTextureRegionDX12( m_RenderTargets[targetID], destination, sourceRect, destinationRect );
}

void CShaderAPIDX12::CopyTextureToRenderTargetEx( int targetID, ShaderAPITextureHandle_t source, Rect_t *sourceRect, Rect_t *destinationRect )
{
	if ( targetID >= 0 && targetID < static_cast<int>( ARRAYSIZE( m_RenderTargets ) ) )
		CopyTextureRegionDX12( source, m_RenderTargets[targetID], sourceRect, destinationRect );
}

void CShaderAPIDX12::CopyRenderTargetToScratchTexture( ShaderAPITextureHandle_t source, ShaderAPITextureHandle_t destination, Rect_t *sourceRect, Rect_t *destinationRect )
{
	CopyTextureRegionDX12( source, destination, sourceRect, destinationRect );
}

//-----------------------------------------------------------------------------
// Purpose: Whole-resource copy between identically shaped textures (every copy); anything
//          else falls back to a region blit
//-----------------------------------------------------------------------------
void CShaderAPIDX12::CopyTextureToTexture( ShaderAPITextureHandle_t source, ShaderAPITextureHandle_t destination )
{
	if ( source == destination || !m_pDevice || !m_pDevice->CommandList() )
		return;
	TextureRecord *pFrom = FindTexture( source ), *pTo = FindTexture( destination );
	if ( pFrom == nullptr || pTo == nullptr )
	{
		CopyTextureRegionDX12( source, destination, nullptr, nullptr );
		return;
	}
	TextureRecord &src = *pFrom, &dst = *pTo;
	if ( !EnsureTextureResident( src ) || !EnsureTextureResident( dst ) )
		return;
	const D3D12_RESOURCE_DESC a = src.resource->GetDesc(), b = dst.resource->GetDesc();
	if ( a.Dimension != b.Dimension || a.Format != b.Format || a.Width != b.Width || a.Height != b.Height || a.DepthOrArraySize != b.DepthOrArraySize || a.MipLevels != b.MipLevels || a.SampleDesc.Count != b.SampleDesc.Count || src.copies != dst.copies )
	{
		CopyTextureRegionDX12( source, destination, nullptr, nullptr );
		return;
	}
	const int nCount = Faces( src ) * src.mipLevels;
	if ( source != m_hDepthTarget && !IsBoundColorTarget( m_RenderTargets, source ) )
	{
		for ( int nCopy = 0; nCopy < src.copies; ++nCopy )
		{
			for ( int nSubresource = 0; nSubresource < nCount; ++nSubresource )
			{
				const int nStateIndex = nCopy * nCount + nSubresource;
				if ( !src.dirtySubresources[nStateIndex] )
					continue;
				const int nMip = nSubresource % src.mipLevels, nFace = nSubresource / src.mipLevels;
				const size_t nBytes = MipBytes( MAX( 1, src.width >> nMip ), MAX( 1, src.height >> nMip ), Faces( src ) == 1 ? MAX( 1, src.depth >> nMip ) : 1, src.format );
				if ( !CreateUpload( m_pDevice->NativeDevice(), m_pDevice->CommandList(), src.resources[nCopy].resource.Get(), src.pixels.Base() + SubresourceOffset( src, nFace, nMip, nCopy ), nBytes, nSubresource, src.subresourceStates[nStateIndex], m_pDevice ) )
					return;
				src.dirtySubresources[nStateIndex] = 0;
			}
		}
	}
	auto transition = [&]( TextureRecord &texture, int nCopy, int nSubresource, D3D12_RESOURCE_STATES next )
	{
		D3D12_RESOURCE_STATES &state = texture.subresourceStates[nCopy * nCount + nSubresource];
		if ( state == next )
			return;
		texture.sampledStateValid = false;
		++m_nTextureStateEpoch;
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture.resources[nCopy].resource.Get();
		barrier.Transition.Subresource = nSubresource;
		barrier.Transition.StateBefore = state;
		barrier.Transition.StateAfter = next;
		m_pDevice->CommandList()->ResourceBarrier( 1, &barrier );
		state = next;
	};
	for ( int nCopy = 0; nCopy < src.copies; ++nCopy )
	{
		for ( int nSubresource = 0; nSubresource < nCount; ++nSubresource )
		{
			transition( src, nCopy, nSubresource, D3D12_RESOURCE_STATE_COPY_SOURCE );
			transition( dst, nCopy, nSubresource, D3D12_RESOURCE_STATE_COPY_DEST );
		}
		m_pDevice->CommandList()->CopyResource( dst.resources[nCopy].resource.Get(), src.resources[nCopy].resource.Get() );
		for ( int nSubresource = 0; nSubresource < nCount; ++nSubresource )
			transition( dst, nCopy, nSubresource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
	}
	dst.gpuDirty = false;
	dst.dirtySubresources.FillWithValue( 0 );
	dst.initializedSubresources.FillWithValue( 0 );
	dst.gpuAuthoritativeSubresources.FillWithValue( 1 );
}

//-----------------------------------------------------------------------------
// Purpose: IDebugTextureInfo: texture list and memory statistics
//-----------------------------------------------------------------------------
void CShaderAPIDX12::EnableDebugTextureList( bool enable )
{
	m_bDebugList = enable;
	if ( !enable && m_pDebugTextureEntries )
	{
		m_pDebugTextureEntries->deleteThis();
		m_pDebugTextureEntries = nullptr;
	}
}

void CShaderAPIDX12::EnableGetAllTextures( bool enable )
{
	m_bDebugAll = enable;
}

//-----------------------------------------------------------------------------
// Purpose: Rebuilds the KeyValues list of (bound or all) textures for the debug overlay
//-----------------------------------------------------------------------------
KeyValues *CShaderAPIDX12::GetDebugTextureList()
{
	if ( !m_bDebugList )
		return nullptr;
	if ( m_pDebugTextureEntries )
		m_pDebugTextureEntries->deleteThis();
	m_pDebugTextureEntries = new KeyValues( "Textures" );
	FOR_EACH_HASHTABLE( m_Textures, entry )
	{
		const TextureRecord &t = *m_Textures[entry];
		if ( !m_bDebugAll && !t.binds )
			continue;
		KeyValues *kv = m_pDebugTextureEntries->CreateNewKey();
		kv->SetString( "Name", t.name.Get() );
		kv->SetInt( "Binds", t.binds );
		kv->SetInt( "Format", t.format );
		kv->SetInt( "Width", t.width );
		kv->SetInt( "Height", t.height );
	}
	m_nDebugListFrame = m_nFrameCounter;
	return m_pDebugTextureEntries;
}

//-----------------------------------------------------------------------------
// Purpose: Native allocation size of all (or last-frame bound) textures, or a picmip estimate
//-----------------------------------------------------------------------------
int CShaderAPIDX12::GetTextureMemoryUsed( TextureMemoryType kind )
{
	size_t nTotal = 0;
	FOR_EACH_HASHTABLE( m_Textures, entry )
	{
		const TextureRecord &t = *m_Textures[entry];
		if ( kind == MEMORY_BOUND_LAST_FRAME && !t.binds )
			continue;
		if ( kind == MEMORY_ESTIMATE_PICMIP_1 || kind == MEMORY_ESTIMATE_PICMIP_2 )
		{
			const int nMip = kind == MEMORY_ESTIMATE_PICMIP_1 ? 1 : 2;
			for ( int i = MIN( nMip, t.mipLevels - 1 ); i < t.mipLevels; ++i )
				nTotal += MipBytes( MAX( 1, t.width >> i ), MAX( 1, t.height >> i ), t.depth, t.format ) * Faces( t ) * t.copies;
		}
		else
		{
			for ( int i = 0; i < t.resources.Count(); ++i )
			{
				if ( t.resources[i].resource )
				{
					const D3D12_RESOURCE_DESC desc = t.resources[i].resource->GetDesc();
					nTotal += static_cast<size_t>( m_pDevice->NativeDevice()->GetResourceAllocationInfo( 0, 1, &desc ).SizeInBytes );
				}
			}
		}
	}
	return static_cast<int>( MIN( nTotal, static_cast<size_t>( INT_MAX ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Debug list freshness and debug texture rendering toggle
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::IsDebugTextureListFresh( int frames )
{
	return m_bDebugList && m_pDebugTextureEntries && frames >= 0 && m_nFrameCounter - m_nDebugListFrame <= static_cast<uint64_t>( frames );
}

bool CShaderAPIDX12::SetDebugTextureRendering( bool enable )
{
	const bool old = m_bDebugRender;
	m_bDebugRender = enable;
	return old;
}
} // namespace shaderapidx12
