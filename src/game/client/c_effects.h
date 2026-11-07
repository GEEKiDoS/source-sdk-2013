//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef C_EFFECTS_H
#define C_EFFECTS_H
#ifdef _WIN32
#pragma once
#endif


// Draw rain effects.
void DrawPrecipitation();

#if defined( L4D2 )
#include "mathlib/ssemath.h"
class FourRays;
struct RayTracingResult;

// Native particle queries use the same brush trees as the rain emitter.
int GetPrecipitationRayEnvironmentIndex( const char *name );
bool TracePrecipitationRayEnvironment( int index, const FourRays &rays,
	const fltx4 &minimum, const fltx4 &maximum, RayTracingResult *result );
#endif


#endif // C_EFFECTS_H
