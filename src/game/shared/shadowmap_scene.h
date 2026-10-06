//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Shared (client + server) metadata and projection math for runtime
//          shadow maps: guard-expanded projections, cube faces, cascade splits
//          and conservative light influence volumes. Pure functions over the
//          validated 'rshd' records and the original worldlights; no renderer
//          or entity dependencies, so the server can build the same caster
//          transmission volumes the client uses for caster queries.
//
//          Conventions (plan step 6): light travel is light -> receiver; sun
//          receiverToLight = -travel. worldToShadow = P_render * V, row-major,
//          HLSL mul(matrix, float4(world,1)). D3D depth [0,1], uv = (0.5*x/w+0.5,
//          0.5-0.5*y/w). Matrices already contain the guard expansion.
//
//=============================================================================//
#ifndef SHADOWMAP_SCENE_H
#define SHADOWMAP_SCENE_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include "mathlib/vmatrix.h"
#include "mathlib/vplane.h"
#include "bspfile.h"
#include "shadowmap_bsp.h"
#include "shaderapi/ishaderapidx12lighting.h"

//-----------------------------------------------------------------------------
// Guard policy: fit the nominal receiver coverage into the useful square U = S - 36 and render the full S.
//-----------------------------------------------------------------------------
inline float ShadowMapScene_GuardScale( int S, int U )				{ return (float)S / (float)U; }
inline float ShadowMapScene_RenderedTanHalfFov( float nominalTanHalfFov, int S, int U )	{ return nominalTanHalfFov * ShadowMapScene_GuardScale( S, U ); }
inline float ShadowMapScene_RenderedOrthoHalfExtent( float nominalHalfExtent, int S, int U )	{ return nominalHalfExtent * ShadowMapScene_GuardScale( S, U ); }

// Wide spots (stopdot2 <= cos 89deg) and points use six faces; narrow spots one expanded perspective.
inline bool ShadowMapScene_IsWideSpot( float stopdot2 )				{ return stopdot2 <= 0.017452406f; }
inline float ShadowMapScene_NarrowSpotTanHalfFov( float stopdot2 )	{ float c = clamp( stopdot2, 0.017452406f, 1.0f ); return sqrtf( MAX( 0.0f, 1.0f - c * c ) ) / c; } // tan(acos(stopdot2))

// Cube faces: +X,-X,+Y,-Y,+Z,-Z with up vectors +Z,+Z,+Z,+Z,-Y,+Y, nominal 90deg FOV, near DX12_SHADOW_CUBE_NEAR.
void ShadowMapScene_CubeFaceBasis( int face, Vector &forward, Vector &up, Vector &right );
// Major-axis face selection (ties X, then Y, then Z) of a light-relative direction.
int ShadowMapScene_SelectCubeFace( const Vector &lightToPoint );

// World->view (right-handed, looking down +forward, D3D clip conventions) and projections. All projections are
// normal (non-reversed) depth in [0,1].
void ShadowMapScene_BuildViewMatrix( const Vector &origin, const Vector &forward, const Vector &up, VMatrix &worldToView );
void ShadowMapScene_BuildPerspective( float tanHalfFov, float zNear, float zFar, VMatrix &proj );
void ShadowMapScene_BuildOrtho( float halfWidth, float halfHeight, float zNear, float zFar, VMatrix &proj );

//-----------------------------------------------------------------------------
// Cascades (plan step 6): n > 0, f = min(viewFar, r_csm_distance). s0 = n, s4 = f,
// si = 0.6*n*pow(f/n, i/4) + 0.4*(n + (f-n)*i/4); interior wi = 0.05*min(si-s(i-1), s(i+1)-si).
// Returns false when f <= n (CSM disabled: static sun visibility only).
//-----------------------------------------------------------------------------
bool ShadowMapScene_CascadeSplits( float n, float f, float splits[DX12_SHADOW_CSM_CASCADES + 1], float blend[DX12_SHADOW_CSM_CASCADES - 1] );

// Stable light-space basis for a sun travel direction (basisX/basisY orthonormal, independent of the camera).
void ShadowMapScene_SunBasis( const Vector &travel, Vector &basisX, Vector &basisY );

//-----------------------------------------------------------------------------
// Influence volumes. A local light's volume is the union of its guard-expanded caster frusta (one narrow spot
// frustum or the six cube faces), bounded by its far plane. `far` for an unbounded light is the distance that
// encloses the world bounds from the light (never an invented illumination radius). planeCount is 0 for a
// cube (AABB only: the six faces cover all directions).
//-----------------------------------------------------------------------------
struct ShadowMapInfluenceVolume_t
{
	Vector	mins, maxs;
	VPlane	planes[6];
	int		planeCount;
	float	zNear, zFar;
	bool	cube;				// six faces
	float	tanRenderedHalfFov;	// per face (cube) or the narrow spot's expanded tan
};

float ShadowMapScene_LocalFar( const dworldlight_t &light, const Vector &worldMins, const Vector &worldMaxs );
void ShadowMapScene_LocalInfluence( const dworldlight_t &light, const Vector &worldMins, const Vector &worldMaxs, ShadowMapInfluenceVolume_t &out );
// Map-wide selected-sun domain: the whole world AABB (every public entity inside it is sun-eligible).
void ShadowMapScene_SunDomain( const Vector &worldMins, const Vector &worldMaxs, ShadowMapInfluenceVolume_t &out );
bool ShadowMapScene_VolumeIntersectsBox( const ShadowMapInfluenceVolume_t &volume, const Vector &mins, const Vector &maxs );

// Per-face view/projection for a cube light or the single narrow-spot face. `faceIndex` 0..5 (cube) or 0.
void ShadowMapScene_LocalFaceMatrices( const dworldlight_t &light, int faceIndex, float zNear, float zFar, int S, int U,
	VMatrix &worldToView, VMatrix &proj, VMatrix &worldToClip, float &tanRenderedHalfFov );

// Light record conversion (shared so client packets and server volumes agree on units): `record.light` supplies
// every photometric/cone/attenuation field (radiance = intensity, already /255 linear), `lightIndex` is the
// record's index in the mode's rshd list (= lightId), startFade/endFade/capDist are 0 (dworldlight_t carries
// none; attenuation uses radius + coefficients), shadow radii come from the record. Type mapped to
// DX12_SHADOW_LIGHT_*; false for unsupported types (surface/quake/skyambient are never selected).
bool ShadowMapScene_SelectedLightFromWorldLight( const ShadowMapLightDisk &record, uint32 lightIndex, DX12LightingSelectedLight &out );

#endif // SHADOWMAP_SCENE_H
