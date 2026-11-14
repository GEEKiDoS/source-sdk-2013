//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GPU-side layout contract shared by restir_vulkan*.cpp and
//          shaders/restir_layout.glsl. The two files MUST stay identical in
//          binding numbers, push-constant layout and struct layouts (std430,
//          16-byte members). Host code includes this; GLSL includes the .glsl.
//
//=============================================================================//

#ifndef RESTIR_GPU_LAYOUT_H
#define RESTIR_GPU_LAYOUT_H
#pragma once

#include "restir_types.h"

//-----------------------------------------------------------------------------
// Descriptor set 0 bindings (one set, one layout, every pass). Storage buffers
// unless noted. Buffers that a pass does not use are still bound (never
// VK_NULL_HANDLE; use a 16-byte dummy).
//-----------------------------------------------------------------------------
#define RESTIR_BIND_TRIANGLES        0   // ReSTIRGpuTriangle[numTriangles]
#define RESTIR_BIND_MATERIALS        1   // ReSTIRGpuMaterial[]
#define RESTIR_BIND_LIGHTS           2   // ReSTIRGpuLight[numLights]
#define RESTIR_BIND_FACES            3   // ReSTIRGpuFace[numFaces]
#define RESTIR_BIND_SAMPLES          4   // ReSTIRGpuSample[numSamples]
#define RESTIR_BIND_LUXELS           5   // ReSTIRGpuLuxel[numLuxels]
#define RESTIR_BIND_FACE_NEIGHBORS   6   // int[]  (ReSTIRScene::faceNeighbors)
#define RESTIR_BIND_CELL_SAMPLES     7   // int[numLuxels]: for face f and cell (s,t): cellSamples[f.firstLuxel + s + t*f.luxelW]
                                         //   = sample index whose (s,t) is that cell, or -1 (host-built; brush faces only,
                                         //   disp faces: sample k of face f lives at cell (k % (luxelW-1), k / (luxelW-1)) is NOT
                                         //   assumed - host fills from ReSTIRGpuSample::s/t for every face)
#define RESTIR_BIND_DIRECT_IRRADIANCE 8  // vec4[numSamples]: exact point/spot direct irradiance, style-0 base channel (resident)
#define RESTIR_BIND_RESERVED_9       9   // 16-byte dummy
#define RESTIR_BIND_RESERVED_10      10  // 16-byte dummy
#define RESTIR_BIND_ACCUMULATION     11  // vec4[numReservoirs * RESTIR_MAX_CHANNELS]: rgb = running sum of the sampled terms, w = contributing iterations
#define RESTIR_BIND_OUTPUT           12  // vec4[numOutputValues] reconstructed luxel radiance (ReSTIRLightmapResult::radiance order)
#define RESTIR_BIND_LUXEL_VALID      13  // uint[numLuxels]
#define RESTIR_BIND_FINAL_LIGHTMAP   14  // vec4[numFaces * RESTIR_MAX_FACE_STYLES + numOutputValues]:
                                         // cached valid-base face/style means, then full-source denoised radiance; gathers read this
#define RESTIR_BIND_SERVICE_IN       15  // ReSTIRGpuRay[] | ReSTIRGpuAmbientQuery[] | ReSTIRGpuPointQuery[] | ReSTIRGpuVisibilityQuery[]
#define RESTIR_BIND_SERVICE_OUT      16  // ReSTIRGpuHit[] | ReSTIRGpuAmbientResult[] | ReSTIRGpuPointResult[] | float visibility[]
#define RESTIR_BIND_BVH_NODES        17  // ReSTIRBvhNode[2*numTriangles-1] (compute-bvh)
#define RESTIR_BIND_BVH_PRIMS        18  // uint[numTriangles] triangle index per sorted leaf (compute-bvh)
#define RESTIR_BIND_BVH_SCRATCH      19  // uint[]: morton codes, sort ping-pong, refit counters (compute-bvh)
#define RESTIR_BIND_ANORMS           20  // vec4[162] g_anorms (mathlib/anorms.h), w = 0
#define RESTIR_BIND_HW_PRIM_MAP      21  // uint[]: hardware instanceCustomIndex + primitiveID -> scene triangle index (hardware-rt)
#define RESTIR_BIND_SCENE_STYLES     22  // int[numStyles]
#define RESTIR_BIND_EMITTER_TRIS     23  // ReSTIRGpuEmitterTriangle[] (ReSTIRScene::emitterTriangles)
#define RESTIR_BIND_STYLE_LIGHTS     24  // int[] (ReSTIRScene::styleLights): per style first/surfaceFirst, the end, then light indices
#define RESTIR_BIND_RECEIVER_ACCUMULATION 25 // vec4[numReservoirs * RESTIR_MAX_CHANNELS]: as ACCUMULATION minus the direct light of
                                         //   runtime-direct lights (shadowMaps bakes with selected lights only; else the 16-byte dummy)
#define RESTIR_BIND_RECEIVER_OUTPUT  26  // vec4[numOutputValues] reconstructed receiver radiance (ReSTIRLightmapResult::radiance when split)
#define RESTIR_BIND_SUN_ORIGINS      27  // vec4[numLuxels]: final origin xyz, w=1 valid / 0 blocked; any selected local/sun
#define RESTIR_BIND_SUN_VISIBILITY   28  // float[numLuxels]: independent closest-sky cone fraction; selected sun only
#define RESTIR_BIND_SELECTED_SOURCES 29 // vec4[shadowLights.Count()]: canonical origin/radius; sun w=-1
#define RESTIR_BIND_PREVIOUS_IRRADIANCE 30 // vec4[numSamples]: style-0 base sampled sums of earlier iterations (rgb, w = count) per sample
#define RESTIR_BIND_SAMPLE_POSITIONS 31 // vec4[numSamples]: resident positions for exact cross-page nearest-sample lookup
#define RESTIR_BIND_COVERAGE         32  // sampler2D[] unsized, partially bound; index = ReSTIRGpuMaterial::coverageTexture etc.
#define RESTIR_BIND_TLAS             33  // accelerationStructureEXT (hardware-rt only); MUST stay the last binding
#define RESTIR_BIND_COUNT            34

#define RESTIR_MAX_CHANNELS          4   // NUM_BUMP_VECTS + 1; accumulation always reserves 4 per reservoir
#define RESTIR_WORKGROUP_SIZE        64

//-----------------------------------------------------------------------------
// Transport records keep the name "reservoir": one per (sample, style slot).
// R(face f, sample k of f, slot) = faces[f].firstReservoir + k * faces[f].numStyles + slot
// (scene builder leaves firstReservoir = 0; the host assigns it as a prefix sum of numSamples*numStyles
// in the uploaded copy of the faces buffer). numReservoirs = sum(numSamples*numStyles).
// Accumulation index = R * RESTIR_MAX_CHANNELS + channel.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Compute-bvh node (Karras LBVH): numTriangles leaves, numTriangles-1 internal nodes.
// Node array: [0, numTriangles-1) internal, [numTriangles-1, 2*numTriangles-1) leaves; root = 0.
//-----------------------------------------------------------------------------
struct ReSTIRBvhNode						// 48 bytes
{
	float		boundsMin[4];			// w = float bits of left child node index (leaf nodes: -1)
	float		boundsMax[4];			// w = float bits of right child node index (leaf nodes: -1)
	unsigned int parent;				// 0xFFFFFFFF for root
	unsigned int primitive;				// leaf: index into BVH_PRIMS (sorted order); internal: 0xFFFFFFFF
	unsigned int refitCounter;			// atomic uint for bottom-up refit (integer atomics only)
	unsigned int pad;
};

// Push constants (identical for every pipeline; 128 bytes, the guaranteed minimum)
struct ReSTIRPushConstants
{
	unsigned int pass;					// pass-specific sub-id (e.g. sort bit, refit level)
	unsigned int iteration;				// 0-based iteration
	unsigned int count;					// work items in this dispatch
	unsigned int first;					// index of the first work item (batching)
	unsigned int seed;					// options.seed
	unsigned int candidates;			// options.candidates: hemisphere rays and emit_surface samples per sample per iteration
	unsigned int reserved0;
	unsigned int maxBounces;			// options.maxBounces
	unsigned int numLights;
	unsigned int numStyles;
	unsigned int numTriangles;
	unsigned int numSamples;
	unsigned int numFaces;
	unsigned int numLuxels;
	unsigned int rayMask;				// RESTIR_RAY_MASK_* for service passes
	unsigned int flags;					// RESTIR_PC_*
	int			skyLight;				// scene.skyLight or -1
	int			skyAmbientLight;		// scene.skyAmbientLight or -1
	unsigned int totalIterations;		// options.iterations
	unsigned int numReservoirs;
	float		worldMins[4];			// scene bounds (morton codes)
	float		worldMaxs[4];
	float		shadowSun[4];			// x: resolved shadowSunAngularRadius
	unsigned int pageBase[4];			// reservoir, sample, luxel and output page bases
};
COMPILE_TIME_ASSERT( sizeof( ReSTIRPushConstants ) == 144 );
#define RESTIR_PC_FINAL_ITERATION   0x1
#define RESTIR_PC_HARDWARE_RT       0x2   // informative; shaders are compiled per backend anyway
#define RESTIR_PC_RECEIVER_PAGE     0x4   // receiverAccumulation is addressed relative to pageBase.x
#define RESTIR_PC_RESERVOIR_PAGE    0x8   // transport arrays are addressed relative to pageBase.x
#define RESTIR_PC_PUBLISH_IRRADIANCE 0x10 // restir_transport: publish previousIrradiance instead of sampling

#endif // RESTIR_GPU_LAYOUT_H
