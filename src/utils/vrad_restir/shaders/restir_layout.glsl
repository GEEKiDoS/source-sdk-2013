// restir_layout.glsl - GLSL twin of utils/vrad_restir/restir_gpu_layout.h and the GPU structs of
// restir_types.h. Binding numbers, push constants and struct layouts MUST match those headers exactly.
// Every .comp includes this first. std430 everywhere; every member is 16-byte aligned on the host side.
#ifndef RESTIR_LAYOUT_GLSL
#define RESTIR_LAYOUT_GLSL

#define RESTIR_WORKGROUP_SIZE 64
#define RESTIR_MAX_CHANNELS 4
#define RESTIR_NUM_ANORMS 162
#define RESTIR_MAXLIGHTMAPS 4
#define RESTIR_NUM_BUMP_VECTS 3

// Hit classification (restir_types.h)
#define RESTIR_TRACE_ID_SKY         0x01000000u
#define RESTIR_TRACE_ID_OPAQUE      0x02000000u
#define RESTIR_TRACE_ID_STATICPROP  0x04000000u

// Triangle flags
#define RESTIR_TRI_NONOPAQUE   0x1u
#define RESTIR_TRI_SKY         0x2u
#define RESTIR_TRI_WORLDFACE   0x4u
#define RESTIR_TRI_STATICPROP  0x8u
#define RESTIR_TRI_SHADOW      0x10u
#define RESTIR_RAY_MASK_ALL        0xFFFFFFFFu
#define RESTIR_RAY_MASK_SHADOW     RESTIR_TRI_SHADOW
#define RESTIR_RAY_MASK_WORLDFACE  RESTIR_TRI_WORLDFACE

// Face flags
#define RESTIR_FACE_BUMPED  0x1
#define RESTIR_FACE_DISP    0x2

// Point-query flags
#define RESTIR_POINT_IGNORE_NORMALS  0x1u
#define RESTIR_POINT_NO_SELF_SHADOW  0x2u
#define RESTIR_POINT_DETAIL_GATHER   0x4u
#define RESTIR_POINT_EMITTERS_ONLY   0x8u

// Light flags
#define RESTIR_LIGHT_MATERIAL        0x1

// emittype_t
#define EMIT_SURFACE     0
#define EMIT_POINT       1
#define EMIT_SPOTLIGHT   2
#define EMIT_SKYLIGHT    3
#define EMIT_QUAKELIGHT  4
#define EMIT_SKYAMBIENT  5

// Reservoir flags
#define RESTIR_RES_VALID    0x1u
#define RESTIR_RES_PATH     0x2u
#define RESTIR_RES_VISIBLE  0x4u

// Push-constant flags
#define RESTIR_PC_FINAL_ITERATION 0x1u
#define RESTIR_PC_HARDWARE_RT     0x2u

//-----------------------------------------------------------------------------
// Structs (restir_types.h / restir_gpu_layout.h)
//-----------------------------------------------------------------------------
struct ReSTIRGpuTriangle				// 80 bytes
{
	vec4 v0;							// xyz position, w uv0.x
	vec4 v1;							// xyz position, w uv0.y
	vec4 v2;							// xyz position, w uv1.x
	vec4 uv;							// uv1.y, uv2.x, uv2.y, pad
	uint hitId;
	uint material;
	uint flags;
	int  face;
};

struct ReSTIRGpuMaterial				// 48 bytes
{
	vec4 reflectivity;					// w unused
	vec4 albedoScale;					// reflectivity / mean linear albedo of albedoTexture; 0 when none
	int  coverageTexture;				// -1 opaque
	int  albedoTexture;					// -1: bounce uses reflectivity only
	int  textureWidth;					// brush texture UV = texel coord / size
	int  textureHeight;
};

struct ReSTIRGpuLight					// 112 bytes
{
	vec4 origin;						// xyz, w radius (0 = unlimited)
	vec4 intensity;						// rgb; EMIT_SURFACE: per-area emission, w = power bound
	vec4 normal;						// xyz, w stopdot
	vec4 attenuation;					// constant, linear, quadratic, exponent; EMIT_SURFACE: x = bounding radius
	vec4 fade;							// startFade, endFade, capDist, stopdot2
	int  type;							// EMIT_*
	int  style;
	int  firstTri;						// EMIT_SURFACE: emitterTriangles range
	int  numTris;
	float sunSpreadAngle;				// EMIT_SKYLIGHT, degrees
	int  styleSlot;
	int  emissionTexture;				// EMIT_SURFACE: sceneTextures index (RGBA8 gamma), -1 uniform
	int  lightFlags;					// RESTIR_LIGHT_*
};

struct ReSTIRGpuEmitterTriangle			// 64 bytes, front-facing
{
	vec4 v0;							// xyz position, w uv0.x
	vec4 v1;							// xyz position, w uv0.y
	vec4 v2;							// xyz position, w uv1.x
	vec4 uv;							// uv1.y, uv2.x, uv2.y, w inclusive CDF in the light's range
};

struct ReSTIRGpuFace					// 192 bytes
{
	vec4 luxelOrigin;
	vec4 luxelToWorld0;
	vec4 luxelToWorld1;
	vec4 worldToLuxel0;
	vec4 worldToLuxel1;
	vec4 faceNormal;
	vec4 reflectivity;					// w = disp radial radius^2 (RESTIR_FACE_DISP)
	vec4 textureS;
	vec4 textureT;
	ivec2 lmMins;
	int  luxelW;
	int  luxelH;
	int  firstSample;
	int  numSamples;
	int  firstLuxel;
	int  firstOutput;
	int  firstNeighbor;
	int  numNeighbors;
	int  numChannels;
	int  numStyles;
	ivec4 styles;						// 255 = unused
	int  dface;
	int  material;
	int  flags;
	int  firstReservoir;				// assigned by the host (prefix sum of numSamples*numStyles)
};

struct ReSTIRGpuSample					// 112 bytes
{
	vec4 position;						// xyz, w area (brush faces unpushed: origin = position + faceNormal; disp: already pushed)
	vec4 normal;
	vec4 bump0;
	vec4 bump1;
	vec4 bump2;
	vec4 lmCoord;						// brush: coord.xy, mins.xy; disp: uv.xy
	vec4 lmMaxs;						// brush: maxs.xy
	int  face;
	int  s;
	int  t;
	int  pad;
};

struct ReSTIRGpuLuxel					// 32 bytes
{
	vec4 position;
	vec4 normal;
};

struct ReSTIRGpuRay						// 32 bytes
{
	vec4 origin;						// w tMin
	vec4 direction;						// w tMax
};

struct ReSTIRGpuHit						// 32 bytes
{
	float t;							// < 0 miss
	uint  hitId;
	int   triangle;
	int   face;
	vec4  normal;						// unflipped winding normal
};

struct ReSTIRGpuAmbientQuery { vec4 position; };		// 16 bytes
struct ReSTIRGpuAmbientResult { vec4 box[6]; };			// 96 bytes

struct ReSTIRGpuPointQuery				// 32 bytes
{
	vec4 position;
	vec4 normal;
	uint flags;
	uint skipHitId;
	ivec2 pad;
};

struct ReSTIRGpuPointResult				// 32 bytes
{
	vec4 direct;
	vec4 indirect;
};

struct ReSTIRReservoir					// 64 bytes
{
	vec4 samplePos;
	vec4 radiance;						// w = p_hat
	float wSum;
	float M;
	float W;
	float sourcePdf;
	uint light;
	uint flags;
	uint hitClass;
	uint emitterTri;					// direct EMIT_SURFACE: emitterTriangles index of samplePos
};

struct ReSTIRBvhNode					// 48 bytes
{
	vec4 boundsMin;						// w = floatBitsToInt -> left child (-1 leaf)
	vec4 boundsMax;						// w = floatBitsToInt -> right child
	uint parent;
	uint primitive;
	uint refitCounter;
	uint pad;
};

//-----------------------------------------------------------------------------
// Push constants (112 bytes)
//-----------------------------------------------------------------------------
layout( push_constant ) uniform ReSTIRPush
{
	uint pass;
	uint iteration;
	uint count;
	uint first;
	uint seed;
	uint candidates;
	uint spatialRadius;
	uint maxBounces;
	uint numLights;
	uint numStyles;
	uint numTriangles;
	uint numSamples;
	uint numFaces;
	uint numLuxels;
	uint rayMask;
	uint flags;
	int  skyLight;
	int  skyAmbientLight;
	uint totalIterations;
	uint numReservoirs;
	vec4 worldMins;
	vec4 worldMaxs;
} pc;

//-----------------------------------------------------------------------------
// Bindings (set 0)
//-----------------------------------------------------------------------------
layout( std430, set = 0, binding = 0 )  readonly buffer TrianglesBuf     { ReSTIRGpuTriangle triangles[]; };
layout( std430, set = 0, binding = 1 )  readonly buffer MaterialsBuf     { ReSTIRGpuMaterial materials[]; };
layout( std430, set = 0, binding = 2 )  readonly buffer LightsBuf        { ReSTIRGpuLight lights[]; };
layout( std430, set = 0, binding = 3 )  readonly buffer FacesBuf         { ReSTIRGpuFace faces[]; };
layout( std430, set = 0, binding = 4 )  readonly buffer SamplesBuf       { ReSTIRGpuSample samples[]; };
layout( std430, set = 0, binding = 5 )  readonly buffer LuxelsBuf        { ReSTIRGpuLuxel luxels[]; };
layout( std430, set = 0, binding = 6 )  readonly buffer FaceNeighborsBuf { int faceNeighbors[]; };
layout( std430, set = 0, binding = 7 )  readonly buffer CellSamplesBuf   { int cellSamples[]; };
layout( std430, set = 0, binding = 8 )  buffer ReservoirsPrevBuf         { ReSTIRReservoir reservoirsPrev[]; };  // written only by restir_init
layout( std430, set = 0, binding = 9 )  buffer ReservoirsCurBuf          { ReSTIRReservoir reservoirsCur[]; };
layout( std430, set = 0, binding = 10 ) buffer ReservoirsNextBuf         { ReSTIRReservoir reservoirsNext[]; };
layout( std430, set = 0, binding = 11 ) buffer AccumulationBuf           { vec4 accumulation[]; };
layout( std430, set = 0, binding = 12 ) buffer OutputBuf                 { vec4 outputRadiance[]; };
layout( std430, set = 0, binding = 13 ) buffer LuxelValidBuf             { uint luxelValid[]; };
layout( std430, set = 0, binding = 14 ) readonly buffer FinalLightmapBuf { vec4 finalLightmap[]; };
// Bindings 15/16 (service in/out) are declared by each service pass with its typed element
// (ReSTIRGpuRay/ReSTIRGpuHit, ReSTIRGpuAmbientQuery/Result, ReSTIRGpuPointQuery/Result), std430, same set.
layout( std430, set = 0, binding = 17 ) coherent buffer BvhNodesBuf      { ReSTIRBvhNode bvhNodes[]; };
layout( std430, set = 0, binding = 18 ) buffer BvhPrimsBuf               { uint bvhPrims[]; };
layout( std430, set = 0, binding = 19 ) buffer BvhScratchBuf             { uint bvhScratch[]; };
layout( std430, set = 0, binding = 20 ) readonly buffer AnormsBuf        { vec4 anorms[]; };
layout( std430, set = 0, binding = 21 ) readonly buffer HwPrimMapBuf     { uint hwPrimMap[]; };
layout( std430, set = 0, binding = 22 ) readonly buffer SceneStylesBuf   { int sceneStyles[]; };
layout( std430, set = 0, binding = 23 ) readonly buffer EmitterTrisBuf   { ReSTIRGpuEmitterTriangle emitterTriangles[]; };
layout( std430, set = 0, binding = 24 ) readonly buffer StyleLightsBuf   { int styleLights[]; };	// numStyles+1 offsets, then light indices
layout( set = 0, binding = 25 ) uniform sampler2D sceneTextures[];	// ReSTIRScene::textures: R8 coverage, RGBA8 albedo/emission
#if RESTIR_HW_RAYQUERY
layout( set = 0, binding = 26 ) uniform accelerationStructureEXT tlas;
#endif

#endif // RESTIR_LAYOUT_GLSL
