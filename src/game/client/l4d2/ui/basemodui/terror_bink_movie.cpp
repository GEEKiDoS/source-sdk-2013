//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Bink 1 movie playback for the L4D2 menu through binkdec.dll.
//
//=============================================================================//

#include <windows.h>
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_bink_movie.h"
#include "binkdec_interface.h"
#include "filesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "vtf/vtf.h"
#include <type_traits>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
// The C interface of binkdec.dll, resolved once per process. The module stays loaded.
struct BinkDecApi_t
{
	decltype( &bdec_decoder_new ) pfnNew;
	decltype( &bdec_decoder_free ) pfnFree;
	decltype( &bdec_open_file ) pfnOpen;
	decltype( &bdec_get_info ) pfnGetInfo;
	decltype( &bdec_get_frame_count ) pfnGetFrameCount;
	decltype( &bdec_get_frame_time ) pfnGetFrameTime;
	decltype( &bdec_goto_frame ) pfnGotoFrame;
	decltype( &bdec_get_next_frame ) pfnGetNextFrame;
};

void BinkDecLog( const char *pMessage )
{
	Warning( "binkdec: %s\n", pMessage );
}

const BinkDecApi_t *BinkDecApi()
{
	static bool s_bTried = false;
	static BinkDecApi_t s_Api;
	static const BinkDecApi_t *s_pApi = NULL;
	if ( s_bTried )
		return s_pApi;
	s_bTried = true;
	// Sys_LoadModule finds the EXE-local bin\x64 copy (the CSysModule is its HMODULE).
	CSysModule *pModule = Sys_LoadModule( "binkdec" );
	if ( !pModule )
	{
		Warning( "Unable to load binkdec.dll; Bink movies are unavailable\n" );
		return NULL;
	}
	const HMODULE hModule = reinterpret_cast<HMODULE>( pModule );
	bool bComplete = true;
	const auto Resolve = [&]( auto &pfn, const char *pName )
	{
		pfn = reinterpret_cast<std::remove_reference_t<decltype( pfn )>>( GetProcAddress( hModule, pName ) );
		bComplete = bComplete && pfn != NULL;
	};
	Resolve( s_Api.pfnNew, "bdec_decoder_new" );
	Resolve( s_Api.pfnFree, "bdec_decoder_free" );
	Resolve( s_Api.pfnOpen, "bdec_open_file" );
	Resolve( s_Api.pfnGetInfo, "bdec_get_info" );
	Resolve( s_Api.pfnGetFrameCount, "bdec_get_frame_count" );
	Resolve( s_Api.pfnGetFrameTime, "bdec_get_frame_time" );
	Resolve( s_Api.pfnGotoFrame, "bdec_goto_frame" );
	Resolve( s_Api.pfnGetNextFrame, "bdec_get_next_frame" );
	decltype( &bdec_set_log_func ) pfnSetLog = NULL;
	Resolve( pfnSetLog, "bdec_set_log_func" );
	if ( !bComplete )
	{
		Warning( "binkdec.dll lacks its C interface; Bink movies are unavailable\n" );
		return NULL;
	}
	pfnSetLog( BinkDecLog );
	s_pApi = &s_Api;
	return s_pApi;
}

int s_nMovieSerial = 0;
}

CTerrorBinkMovie::CTerrorBinkMovie() : m_pDecoder( NULL ), m_nReadPos( 0 ), m_bReadError( false ), m_nWidth( 0 ), m_nHeight( 0 ),
	m_nFrameCount( 0 ), m_flFrameTime( 0.0 ), m_nHead( 0 ), m_nReady( 0 ), m_hThread( NULL ), m_bStop( false ),
	m_flMaxU( 1.0f ), m_flMaxV( 1.0f ), m_flNextPresent( 0.0 ), m_bHasFrame( false )
{
	for ( int i = 0; i < kPlaneCount; ++i )
	{
		m_Regenerators[i].m_pMovie = this;
		m_Regenerators[i].m_nPlane = i;
	}
}

CTerrorBinkMovie::~CTerrorBinkMovie()
{
	Close();
}

uint32_t CTerrorBinkMovie::ReadMemory( void *pDest, uint32_t nBytes, void *pUser )
{
	CTerrorBinkMovie *pMovie = static_cast<CTerrorBinkMovie *>( pUser );
	const int64 nAvailable = pMovie->m_File.TellPut() - pMovie->m_nReadPos;
	const uint32_t nRead = nAvailable > 0 ? uint32_t( MIN( int64( nBytes ), nAvailable ) ) : 0;
	if ( nRead < nBytes )
		pMovie->m_bReadError = true;
	if ( nRead )
		memcpy( pDest, static_cast<const unsigned char *>( pMovie->m_File.Base() ) + pMovie->m_nReadPos, nRead );
	pMovie->m_nReadPos += nRead;
	return nRead;
}

bool CTerrorBinkMovie::MemoryError( void *pUser )
{
	return static_cast<CTerrorBinkMovie *>( pUser )->m_bReadError;
}

void CTerrorBinkMovie::SeekMemory( int32_t nOffset, uint8_t nDirection, void *pUser )
{
	// FileStream::SeekDirection: 0 start, 1 current, 2 end.
	CTerrorBinkMovie *pMovie = static_cast<CTerrorBinkMovie *>( pUser );
	const int64 nBase = nDirection == 1 ? pMovie->m_nReadPos : nDirection == 2 ? pMovie->m_File.TellPut() : 0;
	const int64 nTarget = nBase + nOffset;
	pMovie->m_bReadError = nTarget < 0 || nTarget > pMovie->m_File.TellPut();
	if ( !pMovie->m_bReadError )
		pMovie->m_nReadPos = nTarget;
}

bool CTerrorBinkMovie::Open( const char *pFilename, const char *pPathID )
{
	Close();
	const BinkDecApi_t *pApi = BinkDecApi();
	if ( !pApi || !g_pFullFileSystem->ReadFile( pFilename, pPathID, m_File ) )
		return false;
	m_nReadPos = 0;
	m_bReadError = false;
	m_pDecoder = reinterpret_cast<bdec_decoder *>( pApi->pfnNew() );
	bdec_file_io_t io;
	io.error = MemoryError;
	io.seek = SeekMemory;
	io.read = ReadMemory;
	uint32_t nWidth = 0, nHeight = 0;
	if ( !pApi->pfnOpen( reinterpret_cast<bdec_decoder_t *>( m_pDecoder ), io, this ) || m_bReadError )
	{
		Warning( "Unable to open Bink movie %s\n", pFilename );
		Close();
		return false;
	}
	pApi->pfnGetInfo( reinterpret_cast<bdec_decoder_t *>( m_pDecoder ), &nWidth, &nHeight, NULL );
	m_nWidth = int( nWidth );
	m_nHeight = int( nHeight );
	m_nFrameCount = pApi->pfnGetFrameCount( reinterpret_cast<bdec_decoder_t *>( m_pDecoder ) );
	m_flFrameTime = pApi->pfnGetFrameTime( reinterpret_cast<bdec_decoder_t *>( m_pDecoder ) );
	if ( m_nWidth < 2 || m_nHeight < 2 || !m_nFrameCount || !( m_flFrameTime > 0.0 ) || !CreateResources() )
	{
		Warning( "Unable to play Bink movie %s\n", pFilename );
		Close();
		return false;
	}
	for ( int i = 0; i < kSlotCount; ++i )
		for ( int nPlane = 0; nPlane < kPlaneCount; ++nPlane )
			m_Slots[i].m_Planes[nPlane].SetCount( PlaneWidth( nPlane ) * PlaneHeight( nPlane ) );
	m_nHead = m_nReady = 0;
	m_bStop = false;
	m_flNextPresent = 0.0;
	m_hThread = CreateSimpleThread( DecodeThread, this );
	if ( !m_hThread )
	{
		Close();
		return false;
	}
	return true;
}

bool CTerrorBinkMovie::CreateResources()
{
	const int nSerial = ++s_nMovieSerial;
	static const char *s_PlaneNames[kPlaneCount] = { "y", "cb", "cr" };
	char szNames[kPlaneCount][64];
	for ( int i = 0; i < kPlaneCount; ++i )
	{
		Q_snprintf( szNames[i], sizeof( szNames[i] ), "_bink_movie%d_%s", nSerial, s_PlaneNames[i] );
		m_Display.m_Planes[i].SetCount( PlaneWidth( i ) * PlaneHeight( i ) );
		m_Textures[i].InitProceduralTexture( szNames[i], TEXTURE_GROUP_VGUI, PlaneWidth( i ), PlaneHeight( i ), IMAGE_FORMAT_I8,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_NODEBUGOVERRIDE );
		if ( !m_Textures[i].IsValid() )
			return false;
		m_Textures[i]->SetTextureRegenerator( &m_Regenerators[i] );
	}
	// The luma texture sets the extent; the chroma planes are exactly half of it.
	m_flMaxU = float( m_nWidth ) / float( MAX( m_Textures[0]->GetActualWidth(), 1 ) );
	m_flMaxV = float( m_nHeight ) / float( MAX( m_Textures[0]->GetActualHeight(), 1 ) );

	KeyValues *pVMT = new KeyValues( "Bik" );
	pVMT->SetString( "$ytexture", szNames[0] );
	pVMT->SetString( "$cbtexture", szNames[1] );
	pVMT->SetString( "$crtexture", szNames[2] );
	char szMaterial[64];
	Q_snprintf( szMaterial, sizeof( szMaterial ), "_bink_movie%d", nSerial );
	m_Material.Init( szMaterial, pVMT );
	return m_Material.IsValid();
}

void CTerrorBinkMovie::Close()
{
	if ( m_hThread )
	{
		m_bStop = true;
		m_Wake.Set();
		ThreadJoin( m_hThread );
		ReleaseThreadHandle( m_hThread );
		m_hThread = NULL;
	}
	m_Material.Shutdown();
	for ( int i = 0; i < kPlaneCount; ++i )
	{
		if ( m_Textures[i].IsValid() )
		{
			m_Textures[i]->SetTextureRegenerator( NULL );
			m_Textures[i].Shutdown( true );
		}
	}
	if ( m_pDecoder )
	{
		BinkDecApi()->pfnFree( reinterpret_cast<bdec_decoder_t *>( m_pDecoder ) );
		m_pDecoder = NULL;
	}
	m_File.Purge();
	m_bHasFrame = false;
	m_nWidth = m_nHeight = 0;
	m_nFrameCount = 0;
}

uintp CTerrorBinkMovie::DecodeThread( void *pParam )
{
	static_cast<CTerrorBinkMovie *>( pParam )->DecodeLoop();
	return 0;
}

void CTerrorBinkMovie::DecodeLoop()
{
	const BinkDecApi_t *pApi = BinkDecApi();
	bdec_decoder_t *pDecoder = reinterpret_cast<bdec_decoder_t *>( m_pDecoder );
	uint32 nNextFrame = 0;
	while ( !m_bStop && !m_bReadError )
	{
		int nSlot = -1;
		{
			AUTO_LOCK( m_QueueMutex );
			if ( m_nReady < kSlotCount )
				nSlot = ( m_nHead + m_nReady ) % kSlotCount;
		}
		if ( nSlot < 0 )
		{
			m_Wake.Wait();
			continue;
		}
		// bdec_get_next_frame reads past the last frame; loop back to the first (key) frame.
		if ( nNextFrame >= m_nFrameCount )
		{
			pApi->pfnGotoFrame( pDecoder, 0 );
			nNextFrame = 0;
		}
		bdec_video_plane_t planes[4] = {};
		pApi->pfnGetNextFrame( pDecoder, planes );
		++nNextFrame;
		Frame_t &frame = m_Slots[nSlot];
		for ( int nPlane = 0; nPlane < kPlaneCount; ++nPlane )
		{
			const int nWidth = PlaneWidth( nPlane ), nHeight = PlaneHeight( nPlane );
			const bdec_video_plane_t &plane = planes[nPlane];
			unsigned char *pDest = frame.m_Planes[nPlane].Base();
			for ( int y = 0; y < nHeight; ++y )
				memcpy( pDest + y * nWidth, plane.data + size_t( y ) * plane.pitch, nWidth );
		}
		{
			AUTO_LOCK( m_QueueMutex );
			++m_nReady;
		}
	}
}

void CTerrorBinkMovie::Update()
{
	if ( !m_pDecoder )
		return;
	const double flNow = Plat_FloatTime();
	if ( m_flNextPresent != 0.0 && flNow < m_flNextPresent )
		return;
	int nSlot = -1;
	{
		AUTO_LOCK( m_QueueMutex );
		if ( m_nReady )
			nSlot = m_nHead;
	}
	if ( nSlot < 0 )
		return; // The decoder is behind: keep showing the previous frame.
	{
		AUTO_LOCK( m_DisplayMutex );
		for ( int nPlane = 0; nPlane < kPlaneCount; ++nPlane )
			memcpy( m_Display.m_Planes[nPlane].Base(), m_Slots[nSlot].m_Planes[nPlane].Base(), m_Display.m_Planes[nPlane].Count() );
	}
	{
		AUTO_LOCK( m_QueueMutex );
		m_nHead = ( m_nHead + 1 ) % kSlotCount;
		--m_nReady;
	}
	m_Wake.Set();
	for ( int nPlane = 0; nPlane < kPlaneCount; ++nPlane )
		m_Textures[nPlane]->Download();
	m_bHasFrame = true;
	// Frames are presented on the movie clock; a late frame re-anchors it instead of rushing to catch up.
	m_flNextPresent = m_flNextPresent == 0.0 ? flNow + m_flFrameTime : m_flNextPresent + m_flFrameTime;
	if ( m_flNextPresent < flNow )
		m_flNextPresent = flNow + m_flFrameTime;
}

void CTerrorBinkMovie::CPlaneRegenerator::RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pSubRect )
{
	unsigned char *pDest = pVTFTexture->ImageData( 0, 0, 0 );
	if ( !pDest )
		return;
	const int nRowBytes = pVTFTexture->RowSizeInBytes( 0 );
	const int nRows = MIN( pVTFTexture->Height(), m_pMovie->PlaneHeight( m_nPlane ) );
	const int nWidth = MIN( nRowBytes, m_pMovie->PlaneWidth( m_nPlane ) );
	AUTO_LOCK( m_pMovie->m_DisplayMutex );
	const unsigned char *pSource = m_pMovie->m_Display.m_Planes[m_nPlane].Base();
	for ( int y = 0; y < nRows; ++y )
		memcpy( pDest + y * nRowBytes, pSource + y * m_pMovie->PlaneWidth( m_nPlane ), nWidth );
}
