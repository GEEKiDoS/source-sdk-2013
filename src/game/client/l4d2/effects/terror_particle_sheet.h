//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Native particle-sheet declarations omitted from the public SDK headers.
// These types are provided by particles.lib. Their declarations were recovered
// from that library's CodeView type records, not a retail object offset.
// Keep sheet allocation native; the renderer adapts the512-row index schedule.
//
//=============================================================================
#ifndef TERROR_PARTICLE_SHEET_H
#define TERROR_PARTICLE_SHEET_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlobjectreference.h"

class CUtlBuffer;

struct SequenceSampleTextureCoords_t
{
	float m_fLeft_U0;
	float m_fTop_V0;
	float m_fRight_U0;
	float m_fBottom_V0;
	float m_fLeft_U1;
	float m_fTop_V1;
	float m_fRight_U1;
	float m_fBottom_V1;
};

struct SheetSequenceSample_t
{
	SequenceSampleTextureCoords_t m_TextureCoordData[2];
	float m_fBlendFactor;

	void CopyFirstFrameToOthers()
	{
		m_TextureCoordData[1] = m_TextureCoordData[0];
	}
};

class CSheet
{
public:
	CSheet( CUtlBuffer &buffer );
	CSheet();
	~CSheet();

	CUtlReferenceList< CSheet > m_References;
	SheetSequenceSample_t *m_pSamples[64];
	bool m_bClamp[64];
	bool m_bSequenceIsCopyOfAnotherSequence[64];
	int m_nNumFrames[64];
	float m_flFrameSpan[64];
};


#endif // TERROR_PARTICLE_SHEET_H
