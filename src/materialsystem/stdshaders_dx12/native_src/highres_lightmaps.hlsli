// Explicit native-atlas translation; never interprets RGB or reconstructed geometry as identity.
#ifndef DX12_HIGHRES_LIGHTMAPS_HLSLI
#define DX12_HIGHRES_LIGHTMAPS_HLSLI
// Fixed-function embedding includes this source before the shadow evaluator.
#define DX12_HIGHRES_LIGHTMAPS 1
struct HlightFaceGpuDX12
{
    uint4 nativeRect;
    uint4 dimensionsFlags;
    uint4 styles;
    uint4 tiles[4];
    float4 bakedModelToWorld[3];
};
struct HlightTileGpuDX12 { uint4 address; uint4 size; };
Texture2D<uint> HlightFaceIds : register(t0, space3);
StructuredBuffer<HlightFaceGpuDX12> HlightFaces : register(t1, space3);
StructuredBuffer<HlightTileGpuDX12> HlightTiles : register(t2, space3);
Texture2D<float4> HlightDynamic : register(t3, space3);
Texture2DArray<float4> HlightPages2048 : register(t4, space3);
Texture2DArray<float4> HlightPages4096 : register(t5, space3);
Texture2DArray<float4> HlightPages8192 : register(t6, space3);
Texture2DArray<float4> HlightPages16384 : register(t7, space3);
RWByteAddressBuffer HlightFailure : register(u0, space3);
SamplerState HlightLinear : register(s0, space3);
cbuffer DX12HighresDrawConstants : register(b0, space3)
{
    uint4 cHlightRoute;
    float4 cHlightModelToWorld[3];
    float4 cHlightStyles[16];
};
struct HlightReceiver
{
    HlightFaceGpuDX12 face;
    float2 q;
    float2 nativeUV;
    uint valid;
    uint nativeOnly;
    uint faceId; // Zero-based ordinal in the active mode's face domain.
    uint localVisibilityEligible;
    uint bakedDirectEligible;
    uint2 unbakedLocalRange; // Raw t1032 byte offset/count, only for baked-direct receivers.
    float sun;
    float3 style0BaseRGB;
};
// Per-invocation state, not a UAV read. Report after material derivatives have run.
static uint hlightRejected = 0;
void HighresLightmap_Reject()
{
    hlightRejected = 1;
}
void HighresLightmap_Report()
{
    if (hlightRejected != 0) HlightFailure.InterlockedOr(0, 1);
}
float4 HighresLightmap_Finish(float4 color)
{
    HighresLightmap_Report();
    return color;
}
float4 HighresLightmap_Tile(uint index, float2 q)
{
    uint count, stride;
    HlightTiles.GetDimensions(count, stride);
    if (index >= count) { HighresLightmap_Reject(); return 0; }
    HlightTileGpuDX12 tile = HlightTiles[index];
    float2 uv = (tile.address.zw + saturate(q) * (tile.size.xy - 1) + 0.5) / tile.size.z;
    float3 location = float3(uv, tile.address.y);
    switch(tile.address.x)
    {
    case 0: return HlightPages2048.SampleLevel(HlightLinear, location, 0);
    case 1: return HlightPages4096.SampleLevel(HlightLinear, location, 0);
    case 2: return HlightPages8192.SampleLevel(HlightLinear, location, 0);
    case 3: return HlightPages16384.SampleLevel(HlightLinear, location, 0);
    }
    HighresLightmap_Reject(); return 0;
}
HlightReceiver HighresLightmap_Begin(float2 baseUV)
{
    HlightReceiver r = (HlightReceiver)0;
    r.sun = 1;
    r.nativeUV = baseUV;
    if (cHlightRoute.x == 0) return r;
    r.sun = 0; // Rejected owners cannot appear unoccluded while GPU rejection is in flight.
    precise float2 pixel = baseUV * cHlightRoute.yz;
    int2 owner = int2(floor(pixel));
    if (any(owner < 0) || any(owner >= int2(cHlightRoute.yz))) { HighresLightmap_Reject(); return r; }
    uint faceId = HlightFaceIds.Load(int3(owner, 0));
    uint count, stride;
    HlightFaces.GetDimensions(count, stride);
    if (faceId == 0 || faceId > count) { HighresLightmap_Reject(); return r; }
    r.face = HlightFaces[faceId - 1];
    r.faceId = faceId - 1;
    // Explicitly unlit BSP faces can still own regular (including bumped)
    // native allocations. Preserve their executed native lighting; they have
    // no baked tiles or selected-direct contribution. Unknown owners still reject.
    if ((r.face.dimensionsFlags.z & 4) == 0) { r.nativeOnly = 1; return r; }
    float2 local = pixel - r.face.nativeRect.xy - 0.5;
    r.q = saturate(float2(r.face.nativeRect.z == 0 ? 0 : local.x / r.face.nativeRect.z,
                         r.face.nativeRect.w == 0 ? 0 : local.y / r.face.nativeRect.w));
    r.valid = 1;
    // This is the sole designated style-0/base alpha fetch. Other planes never contribute sun alpha.
    float4 base = HighresLightmap_Tile(r.face.tiles[0].x, r.q);
    bool bakedPose = (r.face.dimensionsFlags.w & 1) != 0;
    [unroll] for (uint row = 0; row < 3; ++row)
        bakedPose = bakedPose && all(r.face.bakedModelToWorld[row] == cHlightModelToWorld[row]);
    r.localVisibilityEligible = bakedPose ? 1 : 0;
    r.bakedDirectEligible = bakedPose && (r.face.dimensionsFlags.z & 16) != 0 ? 1 : 0;
    r.unbakedLocalRange = r.bakedDirectEligible != 0 ?
        uint2((r.face.dimensionsFlags.w >> 1) * 4u, r.face.dimensionsFlags.z >> 16) : uint2(0, 0);
    r.style0BaseRGB = base.rgb;
    r.sun = bakedPose && (r.face.dimensionsFlags.z & 8) != 0 ? saturate(base.a) : 1;
    return r;
}
float3 HighresLightmap_RGB(HlightReceiver r, uint plane)
{
    if (r.valid == 0) return 0; // rejected owner stays visibly dark; the UAV reports the actual failure.
    float3 rgb = 0;
    [unroll] for (uint styleSlot = 0; styleSlot < 4; ++styleSlot)
    {
        uint tile = r.face.tiles[styleSlot][plane];
        if (tile == 0xffffffff) continue;
        uint style = r.face.styles[styleSlot];
        if (style >= 64) { HighresLightmap_Reject(); continue; }
        rgb += (styleSlot == 0 && plane == 0 ? r.style0BaseRGB : HighresLightmap_Tile(tile, r.q).rgb) * cHlightStyles[style / 4][style % 4];
    }
    // Captured actual native dynamic additions use the native padded bumped stride, not highres coordinates.
    float2 dynamicPixel = r.face.nativeRect.xy + r.q * r.face.nativeRect.zw + 0.5;
    dynamicPixel.x += plane * (r.face.nativeRect.z + 3);
    rgb += HlightDynamic.SampleLevel(HlightLinear, dynamicPixel / cHlightRoute.yz, 0).rgb;
    return rgb;
}
float4 HighresLightmap_Plane(HlightReceiver r, uint plane, float4 original)
{
    if (cHlightRoute.x != 0 && r.nativeOnly == 0) original.rgb = HighresLightmap_RGB(r, plane);
    return original; // Native alpha is never replaced by the independent sun visibility.
}
float4 HighresLightmap_NativeScalePlane(HlightReceiver r, uint plane, float4 original, float nativeScale)
{
    float4 result = HighresLightmap_Plane(r, plane, original);
    if (cHlightRoute.x == 0 || r.nativeOnly != 0) result.rgb *= nativeScale;
    return result;
}
#endif
