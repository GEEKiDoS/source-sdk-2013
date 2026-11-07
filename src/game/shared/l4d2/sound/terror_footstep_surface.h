//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_FOOTSTEP_SURFACE_H
#define TERROR_FOOTSTEP_SURFACE_H
#ifdef _WIN32
#pragma once
#endif

// Original walk/run, left/right names from the mounted physics manifest.
// Both game modules use their real physics surface indices; no ABI extension.
const char *TerrorFootstepSurfaceSound( int surfaceIndex, bool left, bool running );

#endif // TERROR_FOOTSTEP_SURFACE_H
