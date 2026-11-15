//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Bink 1 movie playback for the L4D2 menu through binkdec.dll
//          (libbinkdec, thirdparty/libbinkdec). The decoded Y/Cb/Cr planes
//          feed the stock "Bik" shader, which does the YUV to RGB conversion.
//
//=============================================================================//
#ifndef TERROR_BINK_MOVIE_H
#define TERROR_BINK_MOVIE_H
#ifdef _WIN32
#pragma once
#endif

#include <stdint.h>
#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/itexture.h"
#include "tier0/threadtools.h"
#include "tier1/utlbuffer.h"
#include "tier1/utlvector.h"

struct bdec_decoder;

class CTerrorBinkMovie
{
public:
	CTerrorBinkMovie();
	~CTerrorBinkMovie();

	// Reads the whole file into memory (the retail menu opens its movie with the preload
	// flag) and starts decoding on a worker thread. Playback loops. False leaves it closed.
	bool Open( const char *pFilename, const char *pPathID );
	void Close();
	bool IsOpen() const { return m_pDecoder != NULL; }

	// Presents the frame due now, once per paint. A frame is drawable once HasFrame().
	void Update();
	bool HasFrame() const { return m_bHasFrame; }
	IMaterial *GetMaterial() { return m_Material; }
	int GetWidth() const { return m_nWidth; }
	int GetHeight() const { return m_nHeight; }
	// Extent of the frame inside its (possibly padded) textures.
	void GetTexCoordRange( float &flMaxU, float &flMaxV ) const { flMaxU = m_flMaxU; flMaxV = m_flMaxV; }

private:
	enum { kPlaneCount = 3, kSlotCount = 3 };
	// Y, Cb and Cr of one decoded frame, rows packed at the visible plane width.
	struct Frame_t
	{
		CUtlVector<unsigned char> m_Planes[kPlaneCount];
	};
	class CPlaneRegenerator : public ITextureRegenerator
	{
	public:
		CPlaneRegenerator() : m_pMovie( NULL ), m_nPlane( 0 ) {}
		virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pSubRect ) OVERRIDE;
		virtual void Release() OVERRIDE {}
		CTerrorBinkMovie *m_pMovie;
		int m_nPlane;
	};

	static uintp DecodeThread( void *pParam );
	static uint32_t ReadMemory( void *pDest, uint32_t nBytes, void *pUser );
	static bool MemoryError( void *pUser );
	static void SeekMemory( int32_t nOffset, uint8_t nDirection, void *pUser );
	void DecodeLoop();
	bool CreateResources();
	int PlaneWidth( int nPlane ) const { return nPlane ? m_nWidth / 2 : m_nWidth; }
	int PlaneHeight( int nPlane ) const { return nPlane ? m_nHeight / 2 : m_nHeight; }

	bdec_decoder *m_pDecoder;
	CUtlBuffer m_File;
	int64 m_nReadPos;
	bool m_bReadError;
	int m_nWidth, m_nHeight;
	uint32 m_nFrameCount;
	double m_flFrameTime;

	// Decoder -> presenter ring, guarded by m_QueueMutex; the worker sleeps on m_Wake while full.
	Frame_t m_Slots[kSlotCount];
	int m_nHead, m_nReady;
	CThreadFastMutex m_QueueMutex;
	CThreadEvent m_Wake;
	ThreadHandle_t m_hThread;
	volatile bool m_bStop;

	// The presented frame; the regenerators copy it into the textures (possibly on the render thread).
	Frame_t m_Display;
	CThreadFastMutex m_DisplayMutex;
	CPlaneRegenerator m_Regenerators[kPlaneCount];
	CTextureReference m_Textures[kPlaneCount];
	CMaterialReference m_Material;
	float m_flMaxU, m_flMaxV;
	double m_flNextPresent;
	bool m_bHasFrame;
};

#endif // TERROR_BINK_MOVIE_H
