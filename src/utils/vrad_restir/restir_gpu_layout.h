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
                                         //   assumed — host fills from ReSTIRGpuSample::s/t for every face)
#define RESTIR_BIND_RESERVOIRS_PREV  8   // ReSTIRReservoir[numReservoirs]  previous iteration (read only)
#define RESTIR_BIND_RESERVOIRS_CUR   9   // ReSTIRReservoir[numReservoirs]  candidate + temporal result (owner writes)
#define RESTIR_BIND_RESERVOIRS_NEXT  10  // ReSTIRReservoir[numReservoirs]  spatial result (owner writes); becomes PREV next iteration
#define RESTIR_BIND_ACCUMULATION     11  // vec4[numReservoirs * RESTIR_MAX_CHANNELS]: rgb = running sum, w = contributing iterations
#define RESTIR_BIND_OUTPUT           12  // vec4[numOutputValues] reconstructed luxel radiance (ReSTIRLightmapResult::radiance order)
#define RESTIR_BIND_LUXEL_VALID      13  // uint[numLuxels]
#define RESTIR_BIND_FINAL_LIGHTMAP   14  // vec4[numOutputValues] denoised radiance (UploadFinalLightmap); gathers read this
#define RESTIR_BIND_SERVICE_IN       15  // ReSTIRGpuRay[] | ReSTIRGpuAmbientQuery[] | ReSTIRGpuPointQuery[] (per pass)
#define RESTIR_BIND_SERVICE_OUT      16  // ReSTIRGpuHit[] | ReSTIRGpuAmbientResult[] | ReSTIRGpuPointResult[]
#define RESTIR_BIND_BVH_NODES        17  // ReSTIRBvhNode[2*numTriangles-1] (compute-bvh)
#define RESTIR_BIND_BVH_PRIMS        18  // uint[numTriangles] triangle index per sorted leaf (compute-bvh)
#define RESTIR_BIND_BVH_SCRATCH      19  // uint[]: morton codes, sort ping-pong, refit counters (compute-bvh)
#define RESTIR_BIND_ANORMS           20  // vec4[162] g_anorms (mathlib/anorms.h), w = 0
#define RESTIR_BIND_HW_PRIM_MAP      21  // uint[]: hardware instanceCustomIndex + primitiveID -> scene triangle index (hardware-rt)
#define RESTIR_BIND_SCENE_STYLES     22  // int[numStyles]
#define RESTIR_BIND_EMITTER_TRIS     23  // ReSTIRGpuEmitterTriangle[] (ReSTIRScene::emitterTriangles)
#define RESTIR_BIND_STYLE_LIGHTS     24  // int[] (ReSTIRScene::styleLights): numStyles+1 offsets, then local light indices per style
#define RESTIR_BIND_COVERAGE         25  // sampler2D[] unsized, partially bound; index = ReSTIRGpuMaterial::coverageTexture etc.
#define RESTIR_BIND_TLAS             26  // accelerationStructureEXT (hardware-rt only)
#define RESTIR_BIND_COUNT            27

#define RESTIR_MAX_CHANNELS          4   // NUM_BUMP_VECTS + 1; accumulation always reserves 4 per reservoir
#define RESTIR_WORKGROUP_SIZE        64

//-----------------------------------------------------------------------------
// Reservoir indexing: R(face f, sample k of f, slot) = faces[f].firstReservoir + k * faces[f].numStyles + slot
// (scene builder leaves firstReservoir = 0; the host assigns it as a prefix sum of numSamples*numStyles
// in the uploaded copy of the faces buffer). numReservoirs = sum(numSamples*numStyles).
// Accumulation index = R * RESTIR_MAX_CHANNELS + channel.
//-----------------------------------------------------------------------------
struct ReSTIRReservoir						// 64 bytes
{
	float		samplePos[4];			// xyz: direct -> point on the light (sky/sun: origin + dir*1e5); indirect -> path vertex. w: unused
	float		radiance[4];			// rgb: unshadowed radiance arriving from samplePos EXCLUDING the receiver cosine
										//      (direct: VRAD falloff*intensity incl. light-side cosine/cone; indirect: incoming
										//      radiance estimate at the path vertex); w: target pdf p_hat (luminance of rgb)
	float		wSum;					// RIS weight sum
	float		M;						// candidate count (clamped for temporal reuse, e.g. 20 * candidates)
	float		W;						// unbiased contribution weight = wSum / (M * p_hat), 0 when p_hat == 0
	float		sourcePdf;				// pdf of the selected candidate in its generating distribution
	unsigned int light;					// light index for direct samples, 0xFFFFFFFF for path samples
	unsigned int flags;					// RESTIR_RES_*
	unsigned int hitClass;				// hitId class of the path vertex (indirect) or 0
	unsigned int emitterTri;			// direct emit_surface sample: index into ReSTIRScene::emitterTriangles of samplePos; else unused
};
#define RESTIR_RES_VALID        0x1
#define RESTIR_RES_PATH         0x2   // indirect (slot 0 only)
#define RESTIR_RES_VISIBLE      0x4   // visibility of samplePos from the owning sample has been traced and passed

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

//-----------------------------------------------------------------------------
// Push constants (identical for every pipeline; 112 bytes, under the 128-byte guaranteed minimum)
struct ReSTIRPushConstants
{
	unsigned int pass;					// pass-specific sub-id (e.g. sort bit, refit level)
	unsigned int iteration;				// 0-based iteration
	unsigned int count;					// work items in this dispatch
	unsigned int first;					// index of the first work item (batching)
	unsigned int seed;					// options.seed
	unsigned int candidates;			// options.candidates
	unsigned int spatialRadius;			// options.spatialRadius (cells)
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
};
#define RESTIR_PC_FINAL_ITERATION   0x1
#define RESTIR_PC_HARDWARE_RT       0x2   // informative; shaders are compiled per backend anyway

#endif // RESTIR_GPU_LAYOUT_H
