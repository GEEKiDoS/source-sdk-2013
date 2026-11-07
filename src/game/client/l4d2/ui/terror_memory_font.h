//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Process-private original fonts on the pinned target surface.
//
//=============================================================================//

#ifndef TERROR_MEMORY_FONT_H
#define TERROR_MEMORY_FONT_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlstring.h"
#include "tier1/utlvector.h"

struct TerrorFontHandle_t
{
	CUtlString filename;
	void *handle;
};

struct TerrorFontFace_t
{
	CUtlString name;
	unsigned char head[54];
	bool bVerified;
};

// Keep the registries in the HUD owner, not independently destroyed TU globals.
// Its shutdown body runs before these vector members are destroyed.
struct TerrorMemoryFonts_t
{
	CUtlVector<TerrorFontHandle_t> handles;
	CUtlVector<TerrorFontFace_t> faces;
};

bool InstallTerrorMemoryFontBridge( TerrorMemoryFonts_t &fonts );
bool RegisterTerrorMemoryFont( TerrorMemoryFonts_t &fonts, const char *pFilename,
	void *pFontData, int nFontBytes );
void ShutdownTerrorMemoryFonts( TerrorMemoryFonts_t &fonts );

#endif // TERROR_MEMORY_FONT_H
