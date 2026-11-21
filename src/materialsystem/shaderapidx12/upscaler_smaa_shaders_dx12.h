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
	uint32_t reset, subsample, depthInverted, renderWidth, renderHeight, padding;
};
static_assert( sizeof( SmaaConstantsDX12 ) == 14 * 4, "SMAA and the shared compute root agree" );

static const char kSmaaShaderPrefixDX12[] = R"HLSL(
cbuffer SmaaParams : register(b0)
{
    float4 gMetrics;
    float2 gJitter;
    float gCameraNear, gCameraFar;
    uint gReset, gSubsample, gDepthInverted;
    uint gRenderWidth, gRenderHeight, gPadding2;
};
SamplerState LinearSampler : register(s0);
#define SMAA_RT_METRICS gMetrics
#define SMAA_CUSTOM_SL
#define SMAA_MAX_SEARCH_STEPS 16
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#define SMAA_THRESHOLD 0.1
// The temporal resolve clips history to the neighborhood instead of the reference's velocity-alpha weighting.
#define SMAA_REPROJECTION 0
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
    Output[id.xy] = SMAANeighborhoodBlendingPS(uv, offset, Input0, Input1);
}

// Catmull-Rom history fetch from five bilinear taps (the four corner taps of the 4x4 kernel dropped): reprojection
// under motion stays sharp instead of losing a bilinear blur every frame.
float4 SampleHistoryCatmullRom(Texture2D<float4> history, float2 uv)
{
    float2 position = uv * gMetrics.zw;
    float2 center = floor(position - 0.5) + 0.5;
    float2 f = position - center;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 uv0 = (center - 1.0) * gMetrics.xy;
    float2 uv3 = (center + 2.0) * gMetrics.xy;
    float2 uv12 = (center + w2 / w12) * gMetrics.xy;
    float4 result = history.SampleLevel(LinearSampler, float2(uv12.x, uv0.y), 0) * (w12.x * w0.y)
                  + history.SampleLevel(LinearSampler, float2(uv0.x, uv12.y), 0) * (w0.x * w12.y)
                  + history.SampleLevel(LinearSampler, uv12, 0) * (w12.x * w12.y)
                  + history.SampleLevel(LinearSampler, float2(uv3.x, uv12.y), 0) * (w3.x * w12.y)
                  + history.SampleLevel(LinearSampler, float2(uv12.x, uv3.y), 0) * (w12.x * w3.y);
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return result / weight;
}

groupshared float4 SpatialTile[100];
// Filmic T2x resolve: the output is the equal average of this frame's SMAA 1x result and the previous phase
// reprojected onto it, i.e. two jittered subsamples per pixel. The current phase is used as rendered (its +-0.25 px
// jitter IS the second subsample; resampling it away blurred every frame). History is fetched Catmull-Rom and clipped
// to the current 3x3 neighborhood, which removes ghosting/disocclusion without switching temporal AA off under motion.
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
    float4 current = SpatialTile[p];
    float4 motion = Input2.Load(int3(id.xy, 0));
    float velocityPixels = length(motion.xy * gMetrics.zw);
    float2 previousUV = uv - motion.xy; // unjittered current-minus-previous UV
    float3 result = current.rgb;
    if (!gReset && all(previousUV >= 0.5 * gMetrics.xy) && all(previousUV <= 1.0 - 0.5 * gMetrics.xy))
    {
        // Static pixels read the previous phase exactly.
        float3 previous = velocityPixels < 1e-3 ? Input1.Load(int3(id.xy, 0)).rgb : max(SampleHistoryCatmullRom(Input1, previousUV).rgb, 0);
        float3 low = current.rgb, high = current.rgb;
        [unroll] for (int y = -1; y <= 1; ++y)
        {
            [unroll] for (int x = -1; x <= 1; ++x)
            {
                float3 neighbor = SpatialTile[p + y * 10 + x].rgb;
                low = min(low, neighbor);
                high = max(high, neighbor);
            }
        }
        result = 0.5 * (current.rgb + clamp(previous, low, high));
    }
    // T2x history is the raw previous phase, never a recursive moving average.
    HistoryOutput[id.xy] = current;
    Output[id.xy] = float4(max(result, 0), 1);
}
// One output pixel gathers four jittered input samples. Use those same samples
// for variance clipping; a second input-neighbourhood fetch is not required.
float3 ToYCoCg(float3 c)
{
    return float3(dot(c, float3(0.25, 0.5, 0.25)), (c.r - c.b) * 0.5, (2*c.g - c.r - c.b) * 0.25);
}
float3 FromYCoCg(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}
[numthreads(8, 8, 1)]
void CSTaau(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= uint2(gMetrics.zw))) return;
    float2 uv = (id.xy + 0.5) * gMetrics.xy;
    int2 limit = int2(gRenderWidth, gRenderHeight) - 1;
    float2 pos = clamp(uv * float2(gRenderWidth, gRenderHeight) + gJitter - 0.5, 0, limit);
    int2 p = int2(pos);
    float2 f = frac(pos);
    float3 a = ToYCoCg(Input0.Load(int3(p, 0)).rgb);
    float3 b = ToYCoCg(Input0.Load(int3(min(p + int2(1,0), limit), 0)).rgb);
    float3 c = ToYCoCg(Input0.Load(int3(min(p + int2(0,1), limit), 0)).rgb);
    float3 d = ToYCoCg(Input0.Load(int3(min(p + 1, limit), 0)).rgb);
    // A narrower tent preserves detail; temporal integration supplies missing phases.
    f = smoothstep(0, 1, f);
    float3 result = lerp(lerp(a,b,f.x), lerp(c,d,f.x), f.y);
    int2 nearest = int2(pos + 0.5);
    float z = Input3.Load(int3(nearest,0)).r;
    // Store reciprocal linear depth scaled by near, not near-one device Z (FP16).
    float invDepth = lerp(gCameraNear / gCameraFar, 1, gDepthInverted ? z : 1-z);
    float2 velocity = Input2.Load(int3(nearest,0)).xy;
    float2 prev = uv - velocity;
    if (!gReset && all(prev >= 0) && all(prev <= 1))
    {
        float2 motionPixels = velocity * gMetrics.zw;
        float speed = length(motionPixels);
        // Static history is already on the display pixel grid. Avoid the cubic filter there.
        float4 history;
        [branch] if (speed < 0.001) history = Input1.Load(int3(id.xy, 0));
        else history = SampleHistoryCatmullRom(Input1, prev);
        float3 mean = 0.25 * (a+b+c+d);
        float3 sigma = sqrt(max(0.25 * (a*a+b*b+c*c+d*d) - mean*mean, 0));
        float3 hist = clamp(ToYCoCg(history.rgb), mean - 1.5*sigma, mean + 1.5*sigma);
        float match = saturate(2 - abs(invDepth - history.a) / max(0.04*invDepth, 1e-6));
        result = lerp(result, hist, match * lerp(0.90, 0.64, saturate(speed * 0.08)));
    }
    float3 rgb = max(FromYCoCg(result), 0);
    Output[id.xy] = float4(rgb, 1);
    HistoryOutput[id.xy] = float4(rgb, invDepth);
}
)HLSL";
} // namespace shaderapidx12
