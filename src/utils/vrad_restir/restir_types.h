//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Shared data model for VRAD ReSTIR. Every module (scene builder,
//          Vulkan backend, denoiser, ambient cubes, prop lighting, BSP output)
//          communicates exclusively through these types. GPU-visible structs
//          are std430 compatible: every member is 16-byte aligned, no vec3.
//
//=============================================================================//

#ifndef RESTIR_TYPES_H
#define RESTIR_TYPES_H
#pragma once

#include "mathlib/mathlib.h"
#include "mathlib/vector.h"
#include "mathlib/vector2d.h"
#include "mathlib/bumpvects.h"
#include "tier1/utlvector.h"
#include "tier1/utlstring.h"
#include "bspfile.h"

//-----------------------------------------------------------------------------
// Hit classification (identical bit layout to utils/vrad/vrad.h:300-302 so the
// exported semantics match VRAD; lower bits of TRACE_ID_STATICPROP hold the
// static-prop index).
//-----------------------------------------------------------------------------
#define RESTIR_TRACE_ID_SKY         0x01000000
#define RESTIR_TRACE_ID_OPAQUE      0x02000000
#define RESTIR_TRACE_ID_STATICPROP  0x04000000
#define RESTIR_TRACE_ID_PROP_MASK   0x00FFFFFF

//-----------------------------------------------------------------------------
// Triangle flags (ReSTIRGpuTriangle::flags)
//-----------------------------------------------------------------------------
#define RESTIR_TRI_NONOPAQUE   0x1  // alpha-tested: coverage lookup through material.coverageTexture
#define RESTIR_TRI_SKY         0x2  // sky face: hits terminate as "sky" for sky/sun lights
#define RESTIR_TRI_WORLDFACE   0x4  // lightmap gather surface: model-0 dface (not SURF_NOLIGHT) or displacement,
                                    // `face` set. Only geometry the ambient/indirect lightmap gather may hit, and
                                    // only from the front side (VRAD CLightSurface backface-culls, vraddetailprops.cpp:437).
#define RESTIR_TRI_STATICPROP  0x8
#define RESTIR_TRI_SHADOW      0x10 // occluder for lighting/visibility traversal (VRAD g_RtEnv contents:
                                    // MASK_OPAQUE brush-side windings, vrad_brush_cast_shadows entities, sky faces,
                                    // displacements, static props). World dfaces are NOT occluders (they duplicate
                                    // brush sides); they carry only RESTIR_TRI_WORLDFACE.

// Ray masks: a triangle is considered when (triangle.flags & mask) != 0.
#define RESTIR_RAY_MASK_ALL        0xFFFFFFFFu
#define RESTIR_RAY_MASK_SHADOW     RESTIR_TRI_SHADOW     // lighting, visibility, TestLine-equivalent probes
#define RESTIR_RAY_MASK_WORLDFACE  RESTIR_TRI_WORLDFACE  // lightmap gather (front faces only)

// Point-query flags (ReSTIRGpuPointQuery::flags)
#define RESTIR_POINT_IGNORE_NORMALS  0x1  // ambient gather over the full sphere (STATIC_PROP_IGNORE_NORMALS)
#define RESTIR_POINT_NO_SELF_SHADOW  0x2  // skip triangles whose hitId == skipHitId for direct visibility
#define RESTIR_POINT_DETAIL_GATHER   0x4  // `indirect` uses the detail-prop full-sphere semantics (see ReSTIRGpuPointQuery)

//-----------------------------------------------------------------------------
// Options (parsed by vrad_restir.cpp, read-only everywhere else)
//-----------------------------------------------------------------------------
enum ReSTIRDenoiserMode   { RESTIR_DENOISER_OIDN = 0, RESTIR_DENOISER_NONE };
enum ReSTIRDenoiserQuality{ RESTIR_DENOISER_QUALITY_FAST = 0, RESTIR_DENOISER_QUALITY_BALANCED, RESTIR_DENOISER_QUALITY_HIGH };
enum ReSTIRDenoiserDevice { RESTIR_DENOISER_DEVICE_DEFAULT = 0, RESTIR_DENOISER_DEVICE_CPU, RESTIR_DENOISER_DEVICE_GPU };
enum ReSTIRPreset         { RESTIR_PRESET_DEFAULT = 0, RESTIR_PRESET_FAST, RESTIR_PRESET_FINAL };

struct ReSTIROptions
{
	CUtlString	mapPath;				// full path, ".bsp" appended
	bool		hdr;					// this pass bakes HDR lumps (false = LDR)
	bool		staticPropLighting;		// -StaticPropLighting
	bool		textureShadows;			// -TextureShadows
	float		smoothingThreshold;		// cos(-smooth deg); VRAD default cos(45deg); 1.0 disables Phong
	CUtlString	lightsFile;				// -lights <file>, empty = none
	float		lightmapScale;			// -restir_lightmapscale (1.0): multiplier on luxel size for brush faces, (0,1];
										// 0.5 = twice the luxel density per axis; faces are re-split to the 32-luxel limit
	bool		textureAlbedo;			// default on: bounce light picks up the base texture's per-texel colour (normalized
										// to the material's reflectivity); -restir_notexturealbedo restores VRAD's one colour per material

	// Quality knobs. -fast / -final set all of them at once (see ApplyPreset in vrad_restir.cpp);
	// an explicit -restir_* value always wins over the preset, whatever the argument order.
	ReSTIRPreset preset;				// -fast / -final (default)
	int			iterations;				// -restir_iterations (128; fast 32, final 512): ReSTIR iterations; bounce light is
										// fed back from previous iterations' irradiance, so this also bounds bounce depth
	int			candidates;				// -restir_candidates (8; fast 4, final 16) local-light candidates per sample per
										// iteration; the sun, sky ambient and the bounce path are one candidate each on top
	int			spatialRadius;			// -restir_spatial_radius (2), in luxel cells
	int			maxBounces;				// -restir_maxbounces (4; fast 2, final 6): 0 disables indirect light entirely
	int			seed;					// -restir_seed (1)
	int			gpuIndex;				// -restir_gpu, -1 = first discrete GPU
	bool		forceComputeBvh;		// -restir_force_compute_bvh
	bool		probeEnabled;			// -restir_probe x y z nx ny nz (diagnostic: prints LightPoints at that point)
	float		probe[6];

	ReSTIRDenoiserMode		denoiser;			// -restir_denoiser (oidn)
	ReSTIRDenoiserQuality	denoiserQuality;	// -restir_denoiser_quality (balanced; fast fast, final high)
	ReSTIRDenoiserDevice	denoiserDevice;		// -restir_denoiser_device (default)

	ReSTIROptions()
		: hdr( false ), staticPropLighting( false ), textureShadows( false ),
		  smoothingThreshold( 0.7071067f ), lightmapScale( 1.0f ), textureAlbedo( true ), preset( RESTIR_PRESET_DEFAULT ),
		  iterations( 128 ), candidates( 8 ), spatialRadius( 2 ), maxBounces( 4 ),
		  seed( 1 ), gpuIndex( -1 ), forceComputeBvh( false ), probeEnabled( false ),
		  denoiser( RESTIR_DENOISER_OIDN ), denoiserQuality( RESTIR_DENOISER_QUALITY_BALANCED ),
		  denoiserDevice( RESTIR_DENOISER_DEVICE_DEFAULT ) {}
};

//-----------------------------------------------------------------------------
// GPU geometry
//-----------------------------------------------------------------------------
struct ReSTIRGpuTriangle				// 80 bytes
{
	float		v0[4];					// xyz = position, w = uv0.x
	float		v1[4];					// xyz = position, w = uv0.y
	float		v2[4];					// xyz = position, w = uv1.x
	float		uv[4];					// uv1.y, uv2.x, uv2.y, pad.
										// RESTIR_TRI_NONOPAQUE: material texture uv (coverage lookup, repeat).
										// Displacement triangles (face has RESTIR_FACE_DISP): the displacement UV in [0,1]
										// per vertex (VRAD CLightSurface DispUV): luxel coord s = u*(luxelW-1), t = v*(luxelH-1).
										// Displacement triangles are therefore never NONOPAQUE (alpha-tested displacement
										// materials shadow as opaque; documented limitation).
	unsigned int hitId;					// RESTIR_TRACE_ID_* (| prop index)
	unsigned int material;				// index into ReSTIRScene::materials
	unsigned int flags;					// RESTIR_TRI_*
	int			face;					// dface index for world faces / displacement faces, -1 otherwise
};

struct ReSTIRGpuMaterial				// 48 bytes
{
	float		reflectivity[4];		// xyz = dtexdata_t::reflectivity (or prop material average), w unused
	float		albedoScale[4];			// xyz = reflectivity / mean linear albedo of albedoTexture, so that
										// albedoScale * linear(albedo(uv)) averages to reflectivity over the texture;
										// w unused. Zero when albedoTexture < 0.
	int			coverageTexture;		// index into ReSTIRScene::textures (R8), -1 = opaque
	int			albedoTexture;			// index into ReSTIRScene::textures (RGBA8, gamma space), -1 = use reflectivity
	int			textureWidth;			// dtexdata_t width/height: brush texture UV = texel coord / size
	int			textureHeight;
};

// Textures sampled on the GPU. channels == 1: R8 coverage for an alpha-tested
// material (-TextureShadows), texel >= 128 is opaque. channels == 4: RGBA8 base
// texture, gamma space, for per-texel bounce albedo (-restir_texturealbedo).
// Row-major, origin top-left, no mips, repeat addressing.
struct ReSTIRSceneTexture
{
	int							width;
	int							height;
	int							channels;
	CUtlVector<unsigned char>	texels;
};

//-----------------------------------------------------------------------------
// GPU lights. One entry per VRAD directlight_t equivalent. Semantics follow
// utils/vrad/lightmap.cpp GatherSampleLight per emittype_t.
//
// emit_surface (texlight faces, I3): the emitter is the triangle range
// [firstTri, firstTri+numTris) of ReSTIRScene::triangles. `intensity` is the
// emission per unit world area: VRAD's patch intensity
// (baselight * lightscale * scale[0]*scale[1] / basearea * DIRECT_SCALE)
// divided by the patch area, so a point sampled uniformly on a triangle of
// area A with pdf 1/A contributes
//     intensity * max(dot(n_light, -w), 0) * max(dot(n_recv, w), 0) / d^2 * A
// which reproduces VRAD's per-patch (dot * dot2 / dist^2 * intensity) sum.
//
// emit_point / emit_spotlight / emit_quakelight: `origin`, `intensity`,
// attenuation and cone fields exactly as dworldlight_t (VRAD scale, before the
// 1/255 export scale). `fade` carries directlight_t::m_flStartFadeDistance,
// m_flEndFadeDistance, m_flCapDist (-1 end = unset).
//
// emit_skylight: `normal` is the light's TRAVEL direction exactly as VRAD/dworldlight_t store it
// (lightmap.cpp:1686 `dot = -(n . normal)`, :1711 ray towards the sky = -normal); `sunSpreadAngle` in
// degrees; visible when the ray along -normal hits a RESTIR_TRI_SKY triangle.
//
// emit_skyambient: `intensity` is the sky ambient radiance seen by rays that
// reach a RESTIR_TRI_SKY triangle; sampled as a hemispherical light.
//-----------------------------------------------------------------------------
struct ReSTIRGpuLight					// 112 bytes
{
	float		origin[4];				// xyz, w = radius (0 = unlimited)
	float		intensity[4];			// rgb, w unused
	float		normal[4];				// xyz, w = stopdot (cos inner cone)
	float		attenuation[4];			// constant, linear, quadratic, exponent
	float		fade[4];				// startFade, endFade, capDist, stopdot2 (cos outer cone)
	int			type;					// emittype_t
	int			style;					// light style (0 = none)
	int			firstTri;				// emit_surface only
	int			numTris;				// emit_surface only
	float		sunSpreadAngle;			// emit_skylight only, degrees
	int			styleSlot;				// index into ReSTIRScene::sceneStyles (0 == style 0)
	int			pad[2];
};

//-----------------------------------------------------------------------------
// Lit faces, samples and luxels (I11/I12).
//
// Indexing contract (used by backend, denoiser, ambient, output):
//   face f (index into ReSTIRScene::faces, NOT a dface index):
//     samples          [f.firstSample, +f.numSamples)      -> ReSTIRScene::samples
//     luxels           [f.firstLuxel,  +f.luxelW*f.luxelH)  -> ReSTIRScene::luxels, luxel = s + t*luxelW
//     neighbors        [f.firstNeighbor, +f.numNeighbors)   -> ReSTIRScene::faceNeighbors (face indices)
//     output radiance  f.firstOutput + (slot*f.numChannels + channel)*numLuxels + luxel
//                      slot in [0, f.numStyles), channel in [0, f.numChannels)
//   Luxel-space <-> world (VRAD lightinfo_t):
//     world = luxelOrigin + (s + lmMins[0]) * luxelToWorld[0] + (t + lmMins[1]) * luxelToWorld[1]
//     s     = dot(world - luxelOrigin, worldToLuxel[0]) - lmMins[0]   (same for t)
//-----------------------------------------------------------------------------
#define RESTIR_FACE_BUMPED  0x1		// SURF_BUMPLIGHT: numChannels == NUM_BUMP_VECTS+1
#define RESTIR_FACE_DISP    0x2		// displacement face
#define RESTIR_FACE_SKY     0x4		// not lit (kept for completeness; such faces are never in `faces`)

struct ReSTIRGpuFace					// 192 bytes
{
	float		luxelOrigin[4];
	float		luxelToWorld[2][4];
	float		worldToLuxel[2][4];
	float		faceNormal[4];			// plane normal (facing corrected)
	float		reflectivity[4];		// xyz = dtexdata_t::reflectivity; w = RESTIR_FACE_DISP only: VRAD displacement
										// radial radius squared (vraddisps.cpp BuildLuxelRadial: min(2.2*sqrt(2)/luxelsPerUnit,512)^2),
										// the reconstruction gathers disp samples by WORLD distance <= sqrt(w); 0 for brush faces
	float		textureS[4];			// texinfo textureVecsTexelsPerWorldUnits[0]: xyz bump basis (VRAD GetBumpNormals,
										// lightmap.cpp:2473), NOT the lightmap vectors (those are luxelToWorld/worldToLuxel);
										// w = texel offset [3], so texture u = (dot(p, xyz) + w) / material.textureWidth
	float		textureT[4];			// texinfo textureVecsTexelsPerWorldUnits[1], same layout
	int			lmMins[2];				// dface_t::m_LightmapTextureMinsInLuxels
	int			luxelW, luxelH;			// m_LightmapTextureSizeInLuxels + 1
	int			firstSample, numSamples;
	int			firstLuxel, firstOutput;
	int			firstNeighbor, numNeighbors;
	int			numChannels;			// 1 or NUM_BUMP_VECTS+1
	int			numStyles;				// active slots, 1..MAXLIGHTMAPS
	int			styles[MAXLIGHTMAPS];	// style id per slot, 255 = unused; slot 0 is always style 0
	int			dface;					// index into g_pFaces (selected mode's face array)
	int			material;				// index into ReSTIRScene::materials
	int			flags;					// RESTIR_FACE_*
	int			firstReservoir;			// backend-private, scene builder leaves 0
};

struct ReSTIRGpuSample					// 112 bytes. One VRAD sample_t (BuildFacesamples / BuildDispSamples)
{
	float		position[4];			// xyz = world position (balance point), w = world area.
										// Stored exactly as VRAD stores sample_t::pos: brush faces on the plane; displacement
										// samples already pushed 1.0 along the disp vertex normal (CVRADDispColl). VRAD then
										// pushes EVERY sample (brush and disp) by the face plane normal * 1.0 before direct
										// lighting (lightmap.cpp:2445-2451 ComputeIlluminationPointAndNormalsSSE).
										// Shaders: direct-lighting origin = position + faces[face].faceNormal, always.
	float		normal[4];				// xyz = Phong-smoothed normal (I12), w unused
	float		bump[NUM_BUMP_VECTS][4];// bump normals (GetBumpNormals of `normal`), valid when face is RESTIR_FACE_BUMPED
	float		lmCoord[4];				// brush faces: sample_t::coord.xy, sample_t::mins.xy (luxel space).
										// disp faces: xy = displacement UV in [0,1] (zw unused): VRAD disp reconstruction
										// (CVRadDispMgr::BuildLuxelRadial/SampleRadial) is by world distance, not cells.
	float		lmMaxs[4];				// brush faces: sample_t::maxs.xy, pad, pad; disp faces unused
	int			face;					// index into ReSTIRScene::faces
	int			s, t;					// integer cell (sample_t::s/t)
	int			pad;
};

struct ReSTIRGpuLuxel					// 32 bytes. One grid point of the (w)x(h) luxel grid
{
	float		position[4];			// xyz = world position (VRAD facelight_t::luxel), w unused
	float		normal[4];				// xyz = smoothed normal (facelight_t::luxelNormals), w unused
};

//-----------------------------------------------------------------------------
// Host-side scene (built once by CReSTIRSceneBuilder, uploaded once by the backend)
//-----------------------------------------------------------------------------
struct ReSTIRScene
{
	// Geometry
	CUtlVector<ReSTIRGpuTriangle>		triangles;
	CUtlVector<ReSTIRGpuMaterial>		materials;
	CUtlVector<ReSTIRSceneTexture>		textures;
	Vector								worldMins, worldMaxs;

	// Lights
	CUtlVector<ReSTIRGpuLight>			lights;			// GPU evaluation set
	CUtlVector<dworldlight_t>			exportLights;	// LUMP_WORLDLIGHTS entries (I2/I3/I13), flags = 0; same order as `lights` for
														// point/spot/sky lights; surface emitters expand to one entry per fixed-chop patch.
	CUtlVector<int>						exportLightToGpuLight; // parallel to exportLights: index into `lights` (ambient-cube flagging)
	CUtlVector<int>						sceneStyles;	// distinct light styles present, sceneStyles[0] == 0 always
	int									skyAmbientLight;// index into `lights` of the emit_skyambient entry, -1 if none
	int									skyLight;		// index into `lights` of the emit_skylight entry, -1 if none

	// Lightmapped faces
	CUtlVector<ReSTIRGpuFace>			faces;
	CUtlVector<int>						dfaceToFace;	// numfaces entries, -1 for faces without lightmaps
	CUtlVector<ReSTIRGpuSample>			samples;
	CUtlVector<ReSTIRGpuLuxel>			luxels;
	CUtlVector<int>						faceNeighbors;
	int									numOutputValues;// total radiance entries (sum over faces of numStyles*numChannels*numLuxels)

	// Per-face encode-time data (host only)
	CUtlVector<Vector>					faceMinLight;	// per `faces` entry: _minlight (radial.cpp:676)

	ReSTIRScene() : worldMins( 0, 0, 0 ), worldMaxs( 0, 0, 0 ), skyAmbientLight( -1 ), skyLight( -1 ), numOutputValues( 0 ) {}
};

//-----------------------------------------------------------------------------
// Lightmap bake result (backend -> denoiser -> ambient/output)
//-----------------------------------------------------------------------------
struct ReSTIRLightmapResult
{
	CUtlVector<Vector>			radiance;		// ReSTIRScene::numOutputValues entries, linear RGB (VRAD light scale), indexed per ReSTIRGpuFace
	CUtlVector<unsigned char>	luxelValid;		// ReSTIRScene::luxels.Count() entries; 0 = no sample reaches the luxel (SampleRadial false,
												// VRAD bRed2Black black), 1 = direct cell coverage, 2 = brush luxel covered only by the
												// AddBouncedToRadial kernel fallback (excluded from the face median like baseSampleOk == false)
};

//-----------------------------------------------------------------------------
// Generic GPU services used by ambient cubes and prop lighting
//-----------------------------------------------------------------------------
struct ReSTIRGpuRay						// 32 bytes
{
	float		origin[4];				// xyz, w = tMin
	float		direction[4];			// xyz (need not be normalized), w = tMax (fraction space: hit.t in [0,tMax])
};

struct ReSTIRGpuHit						// 32 bytes
{
	float		t;						// parametric hit distance along direction, < 0 = miss
	unsigned int hitId;					// triangle hitId
	int			triangle;				// index into ReSTIRScene::triangles, -1 = miss
	int			face;					// triangle.face
	float		normal[4];				// UNFLIPPED geometric normal from triangle winding (v1-v0)x(v2-v0), normalized; w unused.
										// Callers test orientation themselves (dot(direction, normal) > 0 == back face hit).
};

// Ambient gather at a point: 162 g_anorms rays, lightmap × reflectivity or sky at each hit,
// cosine-weighted into the six g_BoxDirections (I6, leaf_ambient_lighting.cpp:139-180 and
// vraddetailprops.cpp:579-617). Results are per scene style: result index = query*numStyles + styleSlot.
struct ReSTIRGpuAmbientQuery			// 16 bytes
{
	float		position[4];
};

struct ReSTIRGpuAmbientResult			// 96 bytes
{
	float		box[6][4];				// g_BoxDirections order (+x,-x,+y,-y,+z,-z)
};

// Point lighting for prop vertices/texels. Results per scene style: index = query*numStyles + styleSlot.
//
// `direct` = sum over every GPU light of VRAD GatherSampleLight's falloff*dot*intensity with visibility
// (exhaustive for point/spot/sky lights; fixed stratified sampling for surface emitters; deterministic;
// visibility rays use RESTIR_RAY_MASK_SHADOW and honor RESTIR_POINT_NO_SELF_SHADOW/skipHitId).
//
// `indirect` has two semantics selected by RESTIR_POINT_DETAIL_GATHER:
//  - default (static props, vradstaticprops.cpp ComputeIndirectLightingAtPoint with force_fast=true,
//    vraddetailprops.cpp:658-750): NUMVERTEXNORMALS/4 = 40 directions from mathlib `DirectionalSampler_t`
//    (Halton bases 2 and 3, fresh sampler per query, mathlib/halton.h), dot = normal.dir (or 0.7071/2 with
//    RESTIR_POINT_IGNORE_NORMALS), skip dot <= EQUAL_EPSILON, totalDot += dot; hit a RESTIR_TRI_WORLDFACE
//    front face: skip sky faces and faces with lightofs < 0; color = style-0 lightmap luxel at the hit (or the
//    face average when VRAD has no luxel) * reflectivity * 1/(1 + (hitDistance/128)^2); sum, then / totalDot.
//    Only the style-0 result is meaningful (other styles 0).
//  - RESTIR_POINT_DETAIL_GATHER (detail props, vraddetailprops.cpp:622-652 ComputeAmbientLightingAtPoint):
//    all 162 g_anorms directions over the full sphere, no cosine weight, each via CalcRayAmbientLighting
//    (:579-617: lightmap point sample blended with the face average by distance 20..40, * reflectivity,
//    per style; sky faces add the sky ambient light to style 0); result = sum * 255/162 per style.
//    The normal is ignored.
struct ReSTIRGpuPointQuery				// 32 bytes
{
	float		position[4];			// xyz, w unused
	float		normal[4];				// xyz, w unused
	unsigned int flags;					// RESTIR_POINT_*
	unsigned int skipHitId;				// with RESTIR_POINT_NO_SELF_SHADOW
	int			pad[2];
};

struct ReSTIRGpuPointResult				// 32 bytes
{
	float		direct[4];				// rgb direct light for this style
	float		indirect[4];			// rgb gathered lightmap/sky light for this style
};

//-----------------------------------------------------------------------------
// Backend identification / timings (reported by vrad_restir.cpp)
//-----------------------------------------------------------------------------
enum ReSTIRBackendKind { RESTIR_BACKEND_HARDWARE_RT = 0, RESTIR_BACKEND_COMPUTE_BVH };

struct ReSTIRDeviceInfo
{
	CUtlString			deviceName;
	unsigned int		vendorId;
	unsigned int		driverVersion;
	unsigned int		apiVersion;
	ReSTIRBackendKind	backend;
	bool				luidValid;
	unsigned char		luid[8];
	unsigned char		uuid[16];
};

struct ReSTIRGpuTimings
{
	double		sceneBuildMs;
	double		candidateMs;
	double		reuseMs;
	double		reconstructionMs;
	double		compactionMs;
	double		ambientMs;
	double		propMs;
};

#endif // RESTIR_TYPES_H
