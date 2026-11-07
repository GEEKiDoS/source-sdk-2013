//========= Copyright Valve Corporation, All rights reserved. ============//
// Native HDR SMAA T2x compute wrappers. Morphological searches/area integration
// use the MIT-licensed iryoku/smaa reference in thirdparty/smaa unchanged.
#pragma once

namespace shaderapidx12
{
struct SmaaConstantsDX12
{
	float metrics[4]; // inverse width/height, width/height
	float jitter[2], cameraNear, cameraFar;
	uint32_t reset, subsample, depthInverted, padding[3];
};
static_assert( sizeof( SmaaConstantsDX12 ) == 14 * 4, "SMAA and the shared compute root agree" );

static const char kSmaaShaderPrefixDX12[] = R"HLSL(
cbuffer SmaaParams : register(b0)
{
    float4 gMetrics;
    float2 gJitter;
    float gCameraNear, gCameraFar;
    uint gReset, gSubsample, gDepthInverted;
    uint gPadding0, gPadding1, gPadding2;
};
SamplerState LinearSampler : register(s0);
#define SMAA_RT_METRICS gMetrics
#define SMAA_CUSTOM_SL
#define SMAA_MAX_SEARCH_STEPS 16
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#define SMAA_THRESHOLD 0.1
#define SMAA_REPROJECTION 1
#define SMAA_REPROJECTION_WEIGHT_SCALE 30.0
#define SMAATexture2D(tex) Texture2D<float4> tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASamplePoint(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define discard return float2(0, 0)
)HLSL";

static const char kSmaaShaderEntriesDX12[] = R"HLSL(
#undef discard
Texture2D<float4> Input0 : register(t0);
Texture2D<float4> Input1 : register(t1);
Texture2D<float4> Input2 : register(t2);
Texture2D<float4> Input3 : register(t3);
RWTexture2D<float4> Output : register(u0);
RWTexture2D<float4> HistoryOutput : register(u1);

groupshared float LumaTile[121];
float LinearLuma(float3 c) { return max(dot(c, float3(0.2126, 0.7152, 0.0722)), 0); }
float PerceptualLuma(float3 c)
{
    // A display-like luma without clipping HDR highlights or depending on exposure.
    float l = LinearLuma(c);
    return sqrt(l / (1.0 + l));
}
int2 ClampedPixel(int2 p) { return clamp(p, int2(0, 0), int2(gMetrics.zw) - 1); }

[numthreads(8, 8, 1)]
void CSEdges(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID,
             uint3 local : SV_GroupThreadID, uint lane : SV_GroupIndex)
{
    // Share perceptual-luma conversions including the two-pixel left/top halo.
    for (uint i = lane; i < 121; i += 64)
    {
        int2 p = int2(group.xy * 8) + int2(i % 11, i / 11) - 2;
        LumaTile[i] = PerceptualLuma(Input0.Load(int3(ClampedPixel(p), 0)).rgb);
    }
    GroupMemoryBarrierWithGroupSync();
    if (any(id.xy >= uint2(gMetrics.zw))) return;
    uint p = (local.y + 2) * 11 + local.x + 2;
    float l = LumaTile[p], left = LumaTile[p - 1], top = LumaTile[p - 11];
    float2 delta = abs(l - float2(left, top));
    float2 edges = step(SMAA_THRESHOLD, delta);
    if (any(edges))
    {
        float2 opposite = abs(l - float2(LumaTile[p + 1], LumaTile[p + 11]));
        float2 second = abs(float2(left, top) - float2(LumaTile[p - 2], LumaTile[p - 22]));
        float2 maximum = max(delta, max(opposite, second));
        edges *= step(max(maximum.x, maximum.y), SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta);
    }
    // Unlike a stencil-masked pixel shader, compute writes zero on every non-edge.
    Output[id.xy] = float4(edges, 0, 0);
}

[numthreads(8, 8, 1)]
void CSWeights(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= uint2(gMetrics.zw))) return;
    float2 uv = (id.xy + 0.5) * gMetrics.xy;
    float2 pixel;
    float4 offsets[3];
    SMAABlendingWeightCalculationVS(uv, pixel, offsets);
    float4 indices = float4(gSubsample.xxx, 0);
    Output[id.xy] = SMAABlendingWeightCalculationPS(uv, pixel, offsets, Input0, Input1, Input2, indices);
}

[numthreads(8, 8, 1)]
void CSNeighborhood(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= uint2(gMetrics.zw))) return;
    float2 uv = (id.xy + 0.5) * gMetrics.xy;
    float4 offset;
    SMAANeighborhoodBlendingVS(uv, offset);
    Output[id.xy] = SMAANeighborhoodBlendingPS(uv, offset, Input0, Input1, Input2);
}

groupshared float4 SpatialTile[100];
[numthreads(8, 8, 1)]
void CSTemporal(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID,
                uint3 local : SV_GroupThreadID, uint lane : SV_GroupIndex)
{
    for (uint tile = lane; tile < 100; tile += 64)
    {
        int2 tilePixel = int2(group.xy * 8) + int2(tile % 10, tile / 10) - 1;
        SpatialTile[tile] = Input0.Load(int3(ClampedPixel(tilePixel), 0));
    }
    GroupMemoryBarrierWithGroupSync();
    if (any(id.xy >= uint2(gMetrics.zw))) return;
    uint p = (local.y + 1) * 10 + local.x + 1;
    float2 uv = (id.xy + 0.5) * gMetrics.xy;
    // Remove exactly the raster-space camera jitter before resolving the two phases.
    int2 base = int2(local.xy) + 1 + int2(floor(gJitter));
    uint tileIndex = uint(base.y * 10 + base.x);
    float2 fraction = frac(gJitter);
    float4 current = lerp(lerp(SpatialTile[tileIndex], SpatialTile[tileIndex + 1], fraction.x),
                          lerp(SpatialTile[tileIndex + 10], SpatialTile[tileIndex + 11], fraction.x), fraction.y);
    float3 neighbors = SpatialTile[p - 1].rgb + SpatialTile[p + 1].rgb
                     + SpatialTile[p - 10].rgb + SpatialTile[p + 10].rgb;
    float3 limit = 0.08 * max(current.rgb, 0.02);
    current.rgb = max(current.rgb + clamp(0.1 * (current.rgb - 0.25 * neighbors), -limit, limit), 0);
    float4 motion = Input2.Load(int3(id.xy, 0));
    float velocityPixels = length(motion.xy * gMetrics.zw);
    float2 previousUV = uv - motion.xy; // unjittered current-minus-previous UV
    float4 result = current;
    if (!gReset && all(previousUV >= 0.5 * gMetrics.xy) && all(previousUV <= 1.0 - 0.5 * gMetrics.xy))
    {
        // Static pixels use an exact texel fetch and equal two-phase weight. The
        // velocity alpha can differ at a jittered silhouette even when the camera
        // and object are static; using it would make A/B order visible.
        float4 previous = velocityPixels < 1e-3 ? Input1.Load(int3(id.xy, 0)) : Input1.SampleLevel(LinearSampler, previousUV, 0);
        float delta = abs(current.a * current.a - previous.a * previous.a) / 5.0;
        float weight = velocityPixels < 1e-3 ? 0.5 : 0.5 * saturate(1.0 - sqrt(delta) * SMAA_REPROJECTION_WEIGHT_SCALE);
        result = lerp(current, previous, weight);
    }
    // T2x history is the raw previous phase, never a recursive moving average.
    HistoryOutput[id.xy] = current;
    Output[id.xy] = float4(max(result.rgb, 0), 1);
}
)HLSL";
} // namespace shaderapidx12
