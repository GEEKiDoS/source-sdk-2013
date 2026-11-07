//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Mounted L4D2 PCFs use DMX binary encodings newer than the SDK reader.
// The adapter changes the wire encoding only; the particle manager still
// owns definitions, precaching, reloads and simulation.
//
//=============================================================================
#ifndef TERROR_PARTICLE_PARSE_H
#define TERROR_PARTICLE_PARSE_H
#ifdef _WIN32
#pragma once
#endif

bool ReadTerrorParticleConfigFile( const char *pFileName, bool bPrecache, bool bDecommitTempMemory );
class CUtlBuffer;
bool ReadTerrorParticleConfigFile( CUtlBuffer &input, bool bPrecache,
	bool bDecommitTempMemory, const char *pFileName = NULL );

#ifdef CLIENT_DLL
void RegisterTerrorParticleOperators();
void RegisterTerrorParticleRenderers();
#endif

#endif // TERROR_PARTICLE_PARSE_H
