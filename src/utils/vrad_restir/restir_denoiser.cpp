//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: OIDN 2.5.1 lightmap denoiser and deterministic fallback.
//
// The OIDN declarations below are copied from the public OIDN 2.5.1 C API
// (include/OpenImageDenoise/oidn.h). The SDK header is intentionally not
// vendored; all entry points are resolved at runtime.
//
//=============================================================================//
#include "restir_denoiser.h"

#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "tier1/interface.h"
#include "tier1/strtools.h"
#include "mathlib/mathlib.h"
#include <windows.h>
#include <math.h>
#include <string.h>

namespace
{

typedef struct OIDNDeviceImpl *OIDNDevice;
typedef struct OIDNBufferImpl *OIDNBuffer;
typedef struct OIDNFilterImpl *OIDNFilter;
typedef int OIDNError;
typedef int OIDNDeviceType;
typedef int OIDNFormat;

enum
{
	OIDN_DEVICE_TYPE_DEFAULT = 0,
	OIDN_DEVICE_TYPE_CPU = 1,
	OIDN_FORMAT_FLOAT3 = 3,
	OIDN_QUALITY_FAST = 4,
	OIDN_QUALITY_BALANCED = 5,
	OIDN_QUALITY_HIGH = 6,
	OIDN_ERROR_NONE = 0
};

struct OIDNApi
{
	typedef int (__cdecl *GetNumPhysicalDevicesFn)();
	typedef bool (__cdecl *GetPhysicalDeviceBoolFn)( int, const char * );
	typedef int (__cdecl *GetPhysicalDeviceIntFn)( int, const char * );
	typedef const char *(__cdecl *GetPhysicalDeviceStringFn)( int, const char * );
	typedef const void *(__cdecl *GetPhysicalDeviceDataFn)( int, const char *, size_t * );
	typedef OIDNDevice (__cdecl *NewDeviceFn)( OIDNDeviceType );
	typedef OIDNDevice (__cdecl *NewDeviceByLUIDFn)( const void * );
	typedef OIDNDevice (__cdecl *NewDeviceByUUIDFn)( const void * );
	typedef void (__cdecl *CommitDeviceFn)( OIDNDevice );
	typedef OIDNError (__cdecl *GetDeviceErrorFn)( OIDNDevice, const char ** );
	typedef void (__cdecl *ReleaseDeviceFn)( OIDNDevice );
	typedef OIDNBuffer (__cdecl *NewBufferFn)( OIDNDevice, size_t );
	typedef void (__cdecl *WriteBufferFn)( OIDNBuffer, size_t, size_t, const void * );
	typedef void (__cdecl *ReadBufferFn)( OIDNBuffer, size_t, size_t, void * );
	typedef void (__cdecl *ReleaseBufferFn)( OIDNBuffer );
	typedef OIDNFilter (__cdecl *NewFilterFn)( OIDNDevice, const char * );
	typedef void (__cdecl *SetFilterImageFn)( OIDNFilter, const char *, OIDNBuffer, OIDNFormat, size_t, size_t, size_t, size_t, size_t );
	typedef void (__cdecl *SetFilterBoolFn)( OIDNFilter, const char *, bool );
	typedef void (__cdecl *SetFilterIntFn)( OIDNFilter, const char *, int );
	typedef void (__cdecl *CommitFilterFn)( OIDNFilter );
	typedef void (__cdecl *ExecuteFilterFn)( OIDNFilter );
	typedef void (__cdecl *ReleaseFilterFn)( OIDNFilter );

	GetNumPhysicalDevicesFn getNumPhysicalDevices;
	GetPhysicalDeviceBoolFn getPhysicalDeviceBool;
	GetPhysicalDeviceIntFn getPhysicalDeviceInt;
	GetPhysicalDeviceStringFn getPhysicalDeviceString;
	GetPhysicalDeviceDataFn getPhysicalDeviceData;
	NewDeviceFn newDevice;
	NewDeviceByLUIDFn newDeviceByLUID;
	NewDeviceByUUIDFn newDeviceByUUID;
	CommitDeviceFn commitDevice;
	GetDeviceErrorFn getDeviceError;
	ReleaseDeviceFn releaseDevice;
	NewBufferFn newBuffer;
	WriteBufferFn writeBuffer;
	ReadBufferFn readBuffer;
	ReleaseBufferFn releaseBuffer;
	NewFilterFn newFilter;
	SetFilterImageFn setFilterImage;
	SetFilterBoolFn setFilterBool;
	SetFilterIntFn setFilterInt;
	CommitFilterFn commitFilter;
	ExecuteFilterFn executeFilter;
	ReleaseFilterFn releaseFilter;

	OIDNApi() { memset( this, 0, sizeof( *this ) ); }

	template <typename T>
	bool Resolve( HMODULE module, const char *name, T &out )
	{
		out = reinterpret_cast<T>( GetProcAddress( module, name ) );
		return out != nullptr;
	}

	bool Load( HMODULE module )
	{
#define OIDN_LOAD( field, exportName ) if ( !Resolve( module, exportName, field ) ) return false
		OIDN_LOAD( getNumPhysicalDevices, "oidnGetNumPhysicalDevices" );
		OIDN_LOAD( getPhysicalDeviceBool, "oidnGetPhysicalDeviceBool" );
		OIDN_LOAD( getPhysicalDeviceInt, "oidnGetPhysicalDeviceInt" );
		OIDN_LOAD( getPhysicalDeviceString, "oidnGetPhysicalDeviceString" );
		OIDN_LOAD( getPhysicalDeviceData, "oidnGetPhysicalDeviceData" );
		OIDN_LOAD( newDevice, "oidnNewDevice" );
		OIDN_LOAD( newDeviceByLUID, "oidnNewDeviceByLUID" );
		OIDN_LOAD( newDeviceByUUID, "oidnNewDeviceByUUID" );
		OIDN_LOAD( commitDevice, "oidnCommitDevice" );
		OIDN_LOAD( getDeviceError, "oidnGetDeviceError" );
		OIDN_LOAD( releaseDevice, "oidnReleaseDevice" );
		OIDN_LOAD( newBuffer, "oidnNewBuffer" );
		OIDN_LOAD( writeBuffer, "oidnWriteBuffer" );
		OIDN_LOAD( readBuffer, "oidnReadBuffer" );
		OIDN_LOAD( releaseBuffer, "oidnReleaseBuffer" );
		OIDN_LOAD( newFilter, "oidnNewFilter" );
		OIDN_LOAD( setFilterImage, "oidnSetFilterImage" );
		OIDN_LOAD( setFilterBool, "oidnSetFilterBool" );
		OIDN_LOAD( setFilterInt, "oidnSetFilterInt" );
		OIDN_LOAD( commitFilter, "oidnCommitFilter" );
		OIDN_LOAD( executeFilter, "oidnExecuteFilter" );
		OIDN_LOAD( releaseFilter, "oidnReleaseFilter" );
#undef OIDN_LOAD
		return true;
	}
};

static HMODULE LoadOIDNModule()
{
	wchar_t path[MAX_PATH];
	HMODULE self = nullptr;
	if ( GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>( &LoadOIDNModule ), &self ) )
	{
		DWORD length = GetModuleFileNameW( self, path, ARRAYSIZE( path ) );
		if ( length && length < ARRAYSIZE( path ) )
		{
			for ( int i = static_cast<int>( length ) - 1; i >= 0; --i )
			{
				if ( path[i] == L'\\' || path[i] == L'/' )
				{
					path[i + 1] = 0;
					break;
				}
			}
			wchar_t beside[MAX_PATH];
			if ( V_swprintf_safe( beside, L"%lsOpenImageDenoise.dll", path ) >= 0 )
			{
				HMODULE module = LoadLibraryExW( beside, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
				if ( module )
					return module;
			}
		}
	}

	DWORD length = GetModuleFileNameW( nullptr, path, ARRAYSIZE( path ) );
	if ( length && length < ARRAYSIZE( path ) )
	{
		for ( int i = static_cast<int>( length ) - 1; i >= 0; --i )
		{
			if ( path[i] == L'\\' || path[i] == L'/' )
			{
				path[i + 1] = 0;
				break;
			}
		}
		wchar_t beside[MAX_PATH];
		if ( V_swprintf_safe( beside, L"%lsOpenImageDenoise.dll", path ) >= 0 )
		{
			HMODULE module = LoadLibraryExW( beside, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
			if ( module )
				return module;
		}
	}

	// This is the optional-provider fallback used by the renderer providers:
	// the normal Windows search order then includes PATH.
	return LoadLibraryW( L"OpenImageDenoise.dll" );
}

static const char *QualityName( ReSTIRDenoiserQuality quality )
{
	switch ( quality )
	{
	case RESTIR_DENOISER_QUALITY_FAST: return "fast";
	case RESTIR_DENOISER_QUALITY_HIGH: return "high";
	default: return "balanced";
	}
}

static int OIDNQualityValue( ReSTIRDenoiserQuality quality )
{
	switch ( quality )
	{
	case RESTIR_DENOISER_QUALITY_FAST: return OIDN_QUALITY_FAST;
	case RESTIR_DENOISER_QUALITY_HIGH: return OIDN_QUALITY_HIGH;
	default: return OIDN_QUALITY_BALANCED;
	}
}

static float SafeFloat( float value )
{
	return ( isfinite( value ) && value > 0.0f ) ? value : 0.0f;
}

static float Dot3( const float *a, const float *b )
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float Luminance( const float *value )
{
	return SafeFloat( value[0] * 0.2126f + value[1] * 0.7152f + value[2] * 0.0722f );
}

// Ported from utils/vrad/imagepacker.cpp:19-140; kept local because vrad_restir
// does not link the legacy utils/vrad target.
class CImagePacker
{
public:
	bool Reset( int width, int height )
	{
		if ( width <= 0 || height <= 0 || width > 2048 )
			return false;
		m_width = width;
		m_height = height;
		m_maxBlockWidth = width + 1;
		m_maxBlockHeight = height + 1;
		m_minimumHeight = -1;
		for ( int i = 0; i < width; ++i )
			m_wavefront[i] = -1;
		return true;
	}

	bool AddBlock( int width, int height, int *x, int *y )
	{
		if ( width <= 0 || height <= 0 || width > m_width || height > m_height ||
			( width >= m_maxBlockWidth && height >= m_maxBlockHeight ) )
			return false;

		int bestX = -1;
		int outerMinY = m_height;
		int lastMaxYVal = -2;
		const int lastX = m_width - width;
		for ( int outerX = 0; outerX <= lastX; )
		{
			if ( m_wavefront[outerX] == lastMaxYVal )
			{
				++outerX;
				continue;
			}
			int maxYIndex = outerX;
			int maxY = -1;
			for ( int testX = outerX; testX < outerX + width; ++testX )
			{
				if ( m_wavefront[testX] >= maxY )
				{
					maxY = m_wavefront[testX];
					maxYIndex = testX;
				}
			}
			lastMaxYVal = m_wavefront[maxYIndex];
			if ( outerMinY > lastMaxYVal )
			{
				outerMinY = lastMaxYVal;
				bestX = outerX;
			}
			outerX = maxYIndex + 1;
		}

		if ( bestX < 0 )
		{
			if ( width <= m_maxBlockWidth && height <= m_maxBlockHeight )
			{
				m_maxBlockWidth = width;
				m_maxBlockHeight = height;
			}
			return false;
		}

		*x = bestX;
		*y = outerMinY + 1;
		if ( *y + height > m_height )
		{
			if ( width <= m_maxBlockWidth && height <= m_maxBlockHeight )
			{
				m_maxBlockWidth = width;
				m_maxBlockHeight = height;
			}
			return false;
		}
		if ( *y + height > m_minimumHeight )
			m_minimumHeight = *y + height;
		for ( int xIndex = bestX; xIndex < bestX + width; ++xIndex )
			m_wavefront[xIndex] = outerMinY + height;
		return true;
	}

private:
	int m_width;
	int m_height;
	int m_wavefront[2048];
	int m_maxBlockWidth;
	int m_maxBlockHeight;
	int m_minimumHeight;
};

struct FacePlacement
{
	int page;
	int x;
	int y;
};

struct PackedPage
{
	CImagePacker packer;
	CUtlVector<int> faces;
	int width;
	int height;

	PackedPage() : width( 0 ), height( 0 ) {}
};

static bool BuildPages( const ReSTIRScene &scene, CUtlVector<PackedPage> &pages, CUtlVector<FacePlacement> &placements )
{
	pages.RemoveAll();
	placements.SetCount( scene.faces.Count() );
	for ( int faceIndex = 0; faceIndex < scene.faces.Count(); ++faceIndex )
	{
		const ReSTIRGpuFace &face = scene.faces[faceIndex];
		const int blockWidth = face.luxelW + 2;
		const int blockHeight = face.luxelH + 2;
		if ( blockWidth > 2048 || blockHeight > 2048 || blockWidth <= 2 || blockHeight <= 2 )
			return false;

		int pageIndex = -1;
		int x = 0, y = 0;
		for ( int p = 0; p < pages.Count(); ++p )
		{
			if ( pages[p].packer.AddBlock( blockWidth, blockHeight, &x, &y ) )
			{
				pageIndex = p;
				break;
			}
		}
		if ( pageIndex < 0 )
		{
			pageIndex = pages.AddToTail();
			if ( !pages[pageIndex].packer.Reset( 2048, 2048 ) ||
				!pages[pageIndex].packer.AddBlock( blockWidth, blockHeight, &x, &y ) )
			{
				pages.Remove( pageIndex );
				return false;
			}
		}
		placements[faceIndex].page = pageIndex;
		placements[faceIndex].x = x + 1;
		placements[faceIndex].y = y + 1;
		pages[pageIndex].faces.AddToTail( faceIndex );
		pages[pageIndex].width = MAX( pages[pageIndex].width, x + blockWidth );
		pages[pageIndex].height = MAX( pages[pageIndex].height, y + blockHeight );
	}
	return true;
}

struct PageImages
{
	int width;
	int height;
	CUtlVector<float> color;
	CUtlVector<float> albedo;
	CUtlVector<float> normal;
	CUtlVector<unsigned char> valid;
	CUtlVector<float> output;

	void Allocate( int inWidth, int inHeight )
	{
		width = inWidth;
		height = inHeight;
		const int pixelCount = width * height;
		color.SetCount( pixelCount * 3 );
		albedo.SetCount( pixelCount * 3 );
		normal.SetCount( pixelCount * 3 );
		valid.SetCount( pixelCount );
		output.SetCount( pixelCount * 3 );
		memset( color.Base(), 0, color.Count() * sizeof( float ) );
		memset( albedo.Base(), 0, albedo.Count() * sizeof( float ) );
		memset( normal.Base(), 0, normal.Count() * sizeof( float ) );
		memset( valid.Base(), 0, valid.Count() * sizeof( unsigned char ) );
		memset( output.Base(), 0, output.Count() * sizeof( float ) );
	}
};

static void SetPagePixel( PageImages &page, int x, int y, const float *color, const float *albedo, const float *normal, bool valid )
{
	const int index = y * page.width + x;
	for ( int c = 0; c < 3; ++c )
	{
		page.color[index * 3 + c] = SafeFloat( color[c] );
		page.albedo[index * 3 + c] = SafeFloat( albedo[c] );
		page.normal[index * 3 + c] = isfinite( normal[c] ) ? normal[c] : 0.0f;
	}
	page.valid[index] = valid ? 1 : 0;
}

static void BuildPageImages( const ReSTIRScene &scene, const ReSTIRLightmapResult &result, const PackedPage &packed,
	const CUtlVector<FacePlacement> &placements, int styleSlot, int channel, PageImages &page )
{
	page.Allocate( packed.width, packed.height );
	CUtlVector<unsigned char> filled;
	filled.SetCount( page.width * page.height );
	memset( filled.Base(), 0, filled.Count() * sizeof( unsigned char ) );
	for ( int listIndex = 0; listIndex < packed.faces.Count(); ++listIndex )
	{
		const int faceIndex = packed.faces[listIndex];
		const ReSTIRGpuFace &face = scene.faces[faceIndex];
		if ( styleSlot < 0 || styleSlot >= face.numStyles || channel < 0 || channel >= face.numChannels )
			continue;
		const FacePlacement &placement = placements[faceIndex];
		const int numLuxels = face.luxelW * face.luxelH;
		for ( int t = 0; t < face.luxelH; ++t )
		{
			for ( int s = 0; s < face.luxelW; ++s )
			{
				const int luxel = s + t * face.luxelW;
				const int sceneLuxel = face.firstLuxel + luxel;
				const int outputIndex = face.firstOutput + ( styleSlot * face.numChannels + channel ) * numLuxels + luxel;
				float pixelColor[3] = { 0, 0, 0 };
				if ( outputIndex >= 0 && outputIndex < result.radiance.Count() )
				{
					pixelColor[0] = result.radiance[outputIndex].x;
					pixelColor[1] = result.radiance[outputIndex].y;
					pixelColor[2] = result.radiance[outputIndex].z;
				}
				float pixelAlbedo[3] = { face.reflectivity[0], face.reflectivity[1], face.reflectivity[2] };
				float pixelNormal[3] = { 0, 0, 1 };
				if ( sceneLuxel >= 0 && sceneLuxel < scene.luxels.Count() )
				{
					const ReSTIRGpuLuxel &luxelData = scene.luxels[sceneLuxel];
					if ( channel == 0 || face.numChannels == 1 )
					{
						pixelNormal[0] = luxelData.normal[0];
						pixelNormal[1] = luxelData.normal[1];
						pixelNormal[2] = luxelData.normal[2];
					}
					else
					{
						Vector bumpNormals[NUM_BUMP_VECTS];
						Vector textureS( face.textureS[0], face.textureS[1], face.textureS[2] );
						Vector textureT( face.textureT[0], face.textureT[1], face.textureT[2] );
						Vector flatNormal( face.faceNormal[0], face.faceNormal[1], face.faceNormal[2] );
						Vector smoothNormal( luxelData.normal[0], luxelData.normal[1], luxelData.normal[2] );
						GetBumpNormals( textureS, textureT, flatNormal, smoothNormal, bumpNormals );
						const Vector &bump = bumpNormals[channel - 1];
						pixelNormal[0] = bump.x;
						pixelNormal[1] = bump.y;
						pixelNormal[2] = bump.z;
					}
				}
				const bool valid = sceneLuxel >= 0 && sceneLuxel < result.luxelValid.Count() && result.luxelValid[sceneLuxel] != 0;
				if ( !valid )
				{
					pixelColor[0] = pixelColor[1] = pixelColor[2] = 0.0f;
					pixelAlbedo[0] = pixelAlbedo[1] = pixelAlbedo[2] = 0.0f;
					pixelNormal[0] = pixelNormal[1] = 0.0f;
					pixelNormal[2] = 0.0f;
				}
				const int pagePixel = ( placement.y + t ) * page.width + placement.x + s;
				SetPagePixel( page, placement.x + s, placement.y + t, pixelColor, pixelAlbedo, pixelNormal, valid );
				filled[pagePixel] = valid ? 1 : 0;
			}
		}

		// Fill invalid in-face texels from deterministic 8-neighbour means.
		// The temporary candidate lists make each pass independent of scan order.
		const int dilationPasses = MAX( face.luxelW, face.luxelH );
		for ( int pass = 0; pass < dilationPasses; ++pass )
		{
			CUtlVector<int> fillPixels;
			CUtlVector<float> fillValues;
			for ( int t = 0; t < face.luxelH; ++t )
			{
				for ( int s = 0; s < face.luxelW; ++s )
				{
					const int pagePixel = ( placement.y + t ) * page.width + placement.x + s;
					if ( filled[pagePixel] )
						continue;
					float colorSum[3] = { 0, 0, 0 };
					float albedoSum[3] = { 0, 0, 0 };
					float normalSum[3] = { 0, 0, 0 };
					int neighbourCount = 0;
					for ( int dy = -1; dy <= 1; ++dy )
					{
						for ( int dx = -1; dx <= 1; ++dx )
						{
							if ( dx == 0 && dy == 0 )
								continue;
							const int neighbourS = s + dx;
							const int neighbourT = t + dy;
							if ( neighbourS < 0 || neighbourS >= face.luxelW || neighbourT < 0 || neighbourT >= face.luxelH )
								continue;
							const int neighbour = ( placement.y + neighbourT ) * page.width + placement.x + neighbourS;
							if ( !filled[neighbour] )
								continue;
							++neighbourCount;
							for ( int c = 0; c < 3; ++c )
							{
								colorSum[c] += page.color[neighbour * 3 + c];
								albedoSum[c] += page.albedo[neighbour * 3 + c];
								normalSum[c] += page.normal[neighbour * 3 + c];
							}
						}
					}
					if ( !neighbourCount )
						continue;
					fillPixels.AddToTail( pagePixel );
					for ( int c = 0; c < 3; ++c )
						fillValues.AddToTail( colorSum[c] / neighbourCount );
					for ( int c = 0; c < 3; ++c )
						fillValues.AddToTail( albedoSum[c] / neighbourCount );
					const float normalLength = sqrtf( normalSum[0] * normalSum[0] + normalSum[1] * normalSum[1] + normalSum[2] * normalSum[2] );
					if ( normalLength > 1.0e-8f )
					{
						fillValues.AddToTail( normalSum[0] / normalLength );
						fillValues.AddToTail( normalSum[1] / normalLength );
						fillValues.AddToTail( normalSum[2] / normalLength );
					}
					else
					{
						fillValues.AddToTail( 0.0f );
						fillValues.AddToTail( 0.0f );
						fillValues.AddToTail( 1.0f );
					}
				}
			}
			if ( !fillPixels.Count() )
				break;
			int valueOffset = 0;
			for ( int fillIndex = 0; fillIndex < fillPixels.Count(); ++fillIndex )
			{
				const int pagePixel = fillPixels[fillIndex];
				for ( int c = 0; c < 3; ++c )
					page.color[pagePixel * 3 + c] = fillValues[valueOffset++];
				for ( int c = 0; c < 3; ++c )
					page.albedo[pagePixel * 3 + c] = fillValues[valueOffset++];
				for ( int c = 0; c < 3; ++c )
					page.normal[pagePixel * 3 + c] = fillValues[valueOffset++];
				filled[pagePixel] = 1;
			}
		}

		// One-luxel gutter, copied from the closest edge texel. The valid mask
		// deliberately remains false for the gutter and is used on scatter/filter.
		const int innerLeft = placement.x;
		const int innerTop = placement.y;
		const int innerRight = placement.x + face.luxelW - 1;
		const int innerBottom = placement.y + face.luxelH - 1;
		for ( int y = innerTop - 1; y <= innerBottom + 1; ++y )
		{
			for ( int x = innerLeft - 1; x <= innerRight + 1; ++x )
			{
				if ( x >= innerLeft && x <= innerRight && y >= innerTop && y <= innerBottom )
					continue;
				const int sourceX = clamp( x, innerLeft, innerRight );
				const int sourceY = clamp( y, innerTop, innerBottom );
				const int source = sourceY * page.width + sourceX;
				const int destination = y * page.width + x;
				for ( int c = 0; c < 3; ++c )
				{
					page.color[destination * 3 + c] = page.color[source * 3 + c];
					page.albedo[destination * 3 + c] = page.albedo[source * 3 + c];
					page.normal[destination * 3 + c] = page.normal[source * 3 + c];
				}
				page.valid[destination] = 0;
			}
		}
	}
}

// A face slot with no light anywhere (a pre-assigned style the light never reached) must
// stay black: faces share a page, and the denoiser inpaints a black region from lit
// neighbours. Checked on the raw page input so filled/gutter pixels do not count.
static bool FaceSlotHasInput( const ReSTIRLightmapResult &source, const ReSTIRGpuFace &face, int styleSlot, int channel )
{
	const int numLuxels = face.luxelW * face.luxelH;
	const int first = face.firstOutput + ( styleSlot * face.numChannels + channel ) * numLuxels;
	for ( int luxel = 0; luxel < numLuxels; ++luxel )
	{
		const int outputIndex = first + luxel;
		if ( outputIndex < 0 || outputIndex >= source.radiance.Count() )
			continue;
		const Vector &v = source.radiance[outputIndex];
		if ( v.x != 0.0f || v.y != 0.0f || v.z != 0.0f )
			return true;
	}
	return false;
}

static void ScatterPage( const ReSTIRScene &scene, const ReSTIRLightmapResult &pageSource, ReSTIRLightmapResult &result,
	const PackedPage &packed, const CUtlVector<FacePlacement> &placements, int styleSlot, int channel, const PageImages &page )
{
	for ( int listIndex = 0; listIndex < packed.faces.Count(); ++listIndex )
	{
		const int faceIndex = packed.faces[listIndex];
		const ReSTIRGpuFace &face = scene.faces[faceIndex];
		if ( styleSlot < 0 || styleSlot >= face.numStyles || channel < 0 || channel >= face.numChannels )
			continue;
		if ( !FaceSlotHasInput( pageSource, face, styleSlot, channel ) )
			continue;
		const FacePlacement &placement = placements[faceIndex];
		const int numLuxels = face.luxelW * face.luxelH;
		for ( int t = 0; t < face.luxelH; ++t )
		{
			for ( int s = 0; s < face.luxelW; ++s )
			{
				const int luxel = s + t * face.luxelW;
				const int sceneLuxel = face.firstLuxel + luxel;
				if ( sceneLuxel < 0 || sceneLuxel >= pageSource.luxelValid.Count() || !pageSource.luxelValid[sceneLuxel] )
					continue;
				const int outputIndex = face.firstOutput + ( styleSlot * face.numChannels + channel ) * numLuxels + luxel;
				const int pageIndex = ( placement.y + t ) * page.width + placement.x + s;
				if ( outputIndex >= 0 && outputIndex < result.radiance.Count() )
					result.radiance[outputIndex].Init( SafeFloat( page.output[pageIndex * 3 + 0] ), SafeFloat( page.output[pageIndex * 3 + 1] ), SafeFloat( page.output[pageIndex * 3 + 2] ) );
			}
		}
	}
}

static void ApplyBilateralPage( PageImages &page )
{
	for ( int y = 0; y < page.height; ++y )
	{
		for ( int x = 0; x < page.width; ++x )
		{
			const int center = y * page.width + x;
			if ( !page.valid[center] )
				continue;
			const float *centerColor = &page.color[center * 3];
			const float *centerAlbedo = &page.albedo[center * 3];
			const float *centerNormal = &page.normal[center * 3];
			float accum[3] = { 0, 0, 0 };
			float weightSum = 0.0f;
			for ( int dy = -1; dy <= 1; ++dy )
			{
				for ( int dx = -1; dx <= 1; ++dx )
				{
					const int sampleX = clamp( x + dx, 0, page.width - 1 );
					const int sampleY = clamp( y + dy, 0, page.height - 1 );
					const int sample = sampleY * page.width + sampleX;
					if ( !page.valid[sample] )
						continue;
					const float *sampleColor = &page.color[sample * 3];
					const float *sampleAlbedo = &page.albedo[sample * 3];
					const float *sampleNormal = &page.normal[sample * 3];
					const float normalWeight = MAX( 0.0f, Dot3( centerNormal, sampleNormal ) );
					const float albedoDelta = fabsf( Luminance( centerAlbedo ) - Luminance( sampleAlbedo ) );
					const float albedoWeight = 1.0f / ( 1.0f + albedoDelta * 8.0f );
					const float centerLuma = Luminance( centerColor );
					const float sampleLuma = Luminance( sampleColor );
					const float ratio = ( centerLuma > 0.0f && sampleLuma > 0.0f ) ? MIN( centerLuma, sampleLuma ) / MAX( centerLuma, sampleLuma ) : ( centerLuma == sampleLuma ? 1.0f : 0.0f );
					const float spatialWeight = ( dx == 0 && dy == 0 ) ? 1.0f : 0.5f;
					const float weight = spatialWeight * normalWeight * normalWeight * albedoWeight * ( 0.25f + 0.75f * ratio );
					accum[0] += sampleColor[0] * weight;
					accum[1] += sampleColor[1] * weight;
					accum[2] += sampleColor[2] * weight;
					weightSum += weight;
				}
			}
			if ( weightSum > 0.0f )
			{
				page.output[center * 3 + 0] = accum[0] / weightSum;
				page.output[center * 3 + 1] = accum[1] / weightSum;
				page.output[center * 3 + 2] = accum[2] / weightSum;
			}
			else
			{
				page.output[center * 3 + 0] = centerColor[0];
				page.output[center * 3 + 1] = centerColor[1];
				page.output[center * 3 + 2] = centerColor[2];
			}
		}
	}
}

static bool ApplyBilateral( const ReSTIRScene &scene, ReSTIRLightmapResult &result, const CUtlVector<PackedPage> &pages,
	const CUtlVector<FacePlacement> &placements )
{
	for ( int pageIndex = 0; pageIndex < pages.Count(); ++pageIndex )
	{
		const PackedPage &page = pages[pageIndex];
		int maxStyles = 0;
		int maxChannels = 0;
		for ( int faceListIndex = 0; faceListIndex < page.faces.Count(); ++faceListIndex )
		{
			const ReSTIRGpuFace &face = scene.faces[page.faces[faceListIndex]];
			maxStyles = MAX( maxStyles, face.numStyles );
			maxChannels = MAX( maxChannels, face.numChannels );
		}
		for ( int slot = 0; slot < maxStyles; ++slot )
		{
			for ( int channel = 0; channel < maxChannels; ++channel )
			{
				PageImages images;
				BuildPageImages( scene, result, page, placements, slot, channel, images );
				ApplyBilateralPage( images );
				ScatterPage( scene, result, result, page, placements, slot, channel, images );
			}
		}
	}
	return true;
}

static bool CheckOIDNError( const OIDNApi &api, OIDNDevice device, const char *operation )
{
	const char *message = nullptr;
	const OIDNError error = api.getDeviceError( device, &message );
	if ( error == OIDN_ERROR_NONE )
		return true;
	Warning( "VRAD ReSTIR: OIDN %s failed (%d)%s%s\n", operation, error, message ? ": " : "", message ? message : "" );
	return false;
}

static bool RunOIDNPage( const OIDNApi &api, OIDNDevice device, ReSTIRDenoiserQuality quality, PageImages &page )
{
	const size_t pixelCount = static_cast<size_t>( page.width ) * static_cast<size_t>( page.height );
	const size_t imageBytes = pixelCount * 3 * sizeof( float );
	OIDNBuffer color = api.newBuffer( device, imageBytes );
	OIDNBuffer albedo = api.newBuffer( device, imageBytes );
	OIDNBuffer normal = api.newBuffer( device, imageBytes );
	OIDNBuffer output = api.newBuffer( device, imageBytes );
	if ( !color || !albedo || !normal || !output )
	{
		if ( color ) api.releaseBuffer( color );
		if ( albedo ) api.releaseBuffer( albedo );
		if ( normal ) api.releaseBuffer( normal );
		if ( output ) api.releaseBuffer( output );
		return false;
	}

	api.writeBuffer( color, 0, imageBytes, page.color.Base() );
	api.writeBuffer( albedo, 0, imageBytes, page.albedo.Base() );
	api.writeBuffer( normal, 0, imageBytes, page.normal.Base() );
	api.writeBuffer( output, 0, imageBytes, page.color.Base() );
	if ( !CheckOIDNError( api, device, "buffer upload" ) )
	{
		api.releaseBuffer( color ); api.releaseBuffer( albedo ); api.releaseBuffer( normal ); api.releaseBuffer( output );
		return false;
	}

	OIDNFilter filter = api.newFilter( device, "RT" );
	if ( !filter )
	{
		api.releaseBuffer( color ); api.releaseBuffer( albedo ); api.releaseBuffer( normal ); api.releaseBuffer( output );
		return false;
	}
	api.setFilterImage( filter, "color", color, OIDN_FORMAT_FLOAT3, page.width, page.height, 0, 0, 0 );
	api.setFilterImage( filter, "albedo", albedo, OIDN_FORMAT_FLOAT3, page.width, page.height, 0, 0, 0 );
	api.setFilterImage( filter, "normal", normal, OIDN_FORMAT_FLOAT3, page.width, page.height, 0, 0, 0 );
	api.setFilterImage( filter, "output", output, OIDN_FORMAT_FLOAT3, page.width, page.height, 0, 0, 0 );
	api.setFilterBool( filter, "hdr", true );
	api.setFilterBool( filter, "cleanAux", true );
	api.setFilterInt( filter, "quality", OIDNQualityValue( quality ) );
	api.commitFilter( filter );
	bool success = CheckOIDNError( api, device, "filter commit" );
	if ( success )
	{
		api.executeFilter( filter );
		success = CheckOIDNError( api, device, "filter execute" );
	}
	if ( success )
	{
		api.readBuffer( output, 0, imageBytes, page.output.Base() );
		success = CheckOIDNError( api, device, "buffer read" );
	}
	api.releaseFilter( filter );
	api.releaseBuffer( color );
	api.releaseBuffer( albedo );
	api.releaseBuffer( normal );
	api.releaseBuffer( output );
	return success;
}

static bool OIDNMatchesLUID( const OIDNApi &api, int id, const ReSTIRDeviceInfo &device )
{
	if ( !device.luidValid || !api.getPhysicalDeviceBool( id, "luidSupported" ) )
		return false;
	size_t size = 0;
	const void *value = api.getPhysicalDeviceData( id, "luid", &size );
	return value && size == 8 && memcmp( value, device.luid, 8 ) == 0;
}

static bool OIDNMatchesUUID( const OIDNApi &api, int id, const ReSTIRDeviceInfo &device )
{
	if ( api.getPhysicalDeviceBool( id, "uuidSupported" ) == false )
		return false;
	size_t size = 0;
	const void *value = api.getPhysicalDeviceData( id, "uuid", &size );
	return value && size == 16 && memcmp( value, device.uuid, 16 ) == 0;
}

} // namespace

CReSTIRDenoiser::CReSTIRDenoiser()
	: m_pModule( nullptr ), m_pApi( nullptr ), m_pDevice( nullptr ), m_requestedMode( RESTIR_DENOISER_OIDN ),
	  m_quality( RESTIR_DENOISER_QUALITY_BALANCED ), m_devicePreference( RESTIR_DENOISER_DEVICE_DEFAULT ),
	  m_pMode( "disabled" ), m_deviceName( "none" )
{
}

CReSTIRDenoiser::~CReSTIRDenoiser()
{
	Shutdown();
}

bool CReSTIRDenoiser::Init( const ReSTIROptions &options, const ReSTIRDeviceInfo &device )
{
	Shutdown();
	m_requestedMode = options.denoiser;
	m_quality = options.denoiserQuality;
	m_devicePreference = options.denoiserDevice;
	m_pMode = options.denoiser == RESTIR_DENOISER_NONE ? "disabled" : "bilateral-fallback";
	m_deviceName = "none";
	if ( options.denoiser == RESTIR_DENOISER_NONE )
		return false;

	HMODULE module = LoadOIDNModule();
	if ( !module )
	{
		Warning( "VRAD ReSTIR: OpenImageDenoise.dll was not found; using bilateral fallback.\n" );
		return true;
	}
	OIDNApi *api = new OIDNApi;
	if ( !api->Load( module ) )
	{
		Warning( "VRAD ReSTIR: OpenImageDenoise.dll is missing required exports; using bilateral fallback.\n" );
		delete api;
		FreeLibrary( module );
		return true;
	}

	OIDNDevice oidnDevice = nullptr;
	int matchedPhysicalDevice = -1;
	if ( options.denoiserDevice == RESTIR_DENOISER_DEVICE_CPU )
	{
		oidnDevice = api->newDevice( OIDN_DEVICE_TYPE_CPU );
		m_deviceName = "CPU";
	}
	else
	{
		const int physicalCount = MAX( 0, api->getNumPhysicalDevices() );
		for ( int id = 0; id < physicalCount; ++id )
		{
			if ( OIDNMatchesLUID( *api, id, device ) || ( !device.luidValid && OIDNMatchesUUID( *api, id, device ) ) )
			{
				matchedPhysicalDevice = id;
				break;
			}
		}
		// OIDN 2.5.1 has no generic GPU enum. By-LUID/UUID is the only
		// unambiguous GPU selection API; DEFAULT is used when no adapter matches.
		if ( matchedPhysicalDevice >= 0 )
		{
			if ( device.luidValid )
				oidnDevice = api->newDeviceByLUID( device.luid );
			else
				oidnDevice = api->newDeviceByUUID( device.uuid );
			const char *name = api->getPhysicalDeviceString( matchedPhysicalDevice, "name" );
			if ( name && name[0] )
				m_deviceName = name;
		}
		if ( !oidnDevice )
		{
			oidnDevice = api->newDevice( OIDN_DEVICE_TYPE_DEFAULT );
			m_deviceName = "default";
		}
	}
	if ( !oidnDevice )
	{
		CheckOIDNError( *api, nullptr, "device creation" );
		Warning( "VRAD ReSTIR: OIDN could not create a device; using bilateral fallback.\n" );
		delete api;
		FreeLibrary( module );
		return true;
	}
	api->commitDevice( oidnDevice );
	if ( !CheckOIDNError( *api, oidnDevice, "device commit" ) )
	{
		api->releaseDevice( oidnDevice );
		delete api;
		FreeLibrary( module );
		return true;
	}

	m_pModule = module;
	m_pApi = api;
	m_pDevice = oidnDevice;
	m_pMode = "oidn";
	return true;
}

void CReSTIRDenoiser::Shutdown()
{
	if ( m_pApi && m_pDevice )
	{
		OIDNApi *api = static_cast<OIDNApi *>( m_pApi );
		api->releaseDevice( static_cast<OIDNDevice>( m_pDevice ) );
	}
	if ( m_pModule )
		FreeLibrary( static_cast<HMODULE>( m_pModule ) );
	delete static_cast<OIDNApi *>( m_pApi );
	m_pModule = nullptr;
	m_pApi = nullptr;
	m_pDevice = nullptr;
}

bool CReSTIRDenoiser::Denoise( const ReSTIRScene &scene, ReSTIRLightmapResult &result )
{
	if ( !DenoiseRadiance( scene, result ) )
		return false;
	if ( result.sourceRadiance.Count() == 0 )
		return true;
	// Swap ownership, not image contents: run the same estimator-independent denoiser on full transport.
	result.radiance.Swap( result.sourceRadiance );
	const bool success = DenoiseRadiance( scene, result );
	result.radiance.Swap( result.sourceRadiance );
	return success;
}

bool CReSTIRDenoiser::DenoiseRadiance( const ReSTIRScene &scene, ReSTIRLightmapResult &result )
{
	const double startTime = Plat_FloatTime();
	CUtlVector<PackedPage> pages;
	CUtlVector<FacePlacement> placements;
	const bool pagesBuilt = BuildPages( scene, pages, placements );
	if ( !pagesBuilt )
	{
		Warning( "VRAD ReSTIR: one or more lightmap faces cannot be packed into a 2048x2048 denoiser page; using bilateral fallback.\n" );
		m_pMode = "bilateral-fallback";
	}

	CUtlVector<Vector> originalRadiance;
	originalRadiance = result.radiance;
	bool success = pagesBuilt && m_pMode[0] == 'o';
	if ( success )
	{
		const OIDNApi &api = *static_cast<OIDNApi *>( m_pApi );
		const OIDNDevice oidnDevice = static_cast<OIDNDevice>( m_pDevice );
		for ( int pageIndex = 0; pageIndex < pages.Count() && success; ++pageIndex )
		{
			const PackedPage &page = pages[pageIndex];
			int maxStyles = 0;
			int maxChannels = 0;
			for ( int faceListIndex = 0; faceListIndex < page.faces.Count(); ++faceListIndex )
			{
				const ReSTIRGpuFace &face = scene.faces[page.faces[faceListIndex]];
				maxStyles = MAX( maxStyles, face.numStyles );
				maxChannels = MAX( maxChannels, face.numChannels );
			}
			for ( int slot = 0; slot < maxStyles && success; ++slot )
			{
				for ( int channel = 0; channel < maxChannels && success; ++channel )
				{
					PageImages images;
					BuildPageImages( scene, result, page, placements, slot, channel, images );
					if ( !RunOIDNPage( api, oidnDevice, m_quality, images ) )
						success = false;
					else
						ScatterPage( scene, result, result, page, placements, slot, channel, images );
				}
			}
		}
	}

	if ( !success && m_pMode[0] != 'd' )
	{
		result.radiance = originalRadiance;
		m_pMode = "bilateral-fallback";
		if ( pagesBuilt )
			ApplyBilateral( scene, result, pages, placements );
	}

	for ( int i = 0; i < result.radiance.Count(); ++i )
	{
		result.radiance[i].x = SafeFloat( result.radiance[i].x );
		result.radiance[i].y = SafeFloat( result.radiance[i].y );
		result.radiance[i].z = SafeFloat( result.radiance[i].z );
	}
	const double elapsedMs = ( Plat_FloatTime() - startTime ) * 1000.0;
	Msg( "denoiser=%s device=%s quality=%s page_count=%d elapsed_ms=%.3f\n", GetModeString(), m_deviceName.Get(), QualityName( m_quality ), pages.Count(), elapsedMs );
	return true;
}

const char *CReSTIRDenoiser::GetModeString() const
{
	return m_pMode;
}
