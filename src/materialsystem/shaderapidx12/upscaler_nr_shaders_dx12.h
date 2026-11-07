//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Compute shaders for the DLSS-NR layer chain and the typed depth clone, compiled at runtime with
//          D3DCompile (cs_5_0).
//
//=============================================================================//
#ifndef UPSCALER_NR_SHADERS_DX12_H
#define UPSCALER_NR_SHADERS_DX12_H
#pragma once

// CSMain mode 0 encodes a layer's linear scRGB input into the sRGB display-referred proxy the model is trained on;
// mode 1 composes the model's answer back onto that layer's input. The proxy curves (soft knee, Neutwo, hybrid) and
// the white-point normalisation follow OptiScaler_DLSSNR (https://github.com/Dagherbou/OptiScaler_DLSSNR,
// OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl, GPL-3.0). The composition -- the two-branch luminance ratio, the
// OkLab hue correction, the luminance-only/full-colour blend and the neutral-axis gamut compression -- is RenoDX's
// DLSS 5 addon design:
//
//   RenoDX, https://github.com/clshortfuse/renodx
//   MIT License, Copyright (c) 2025 Carlos Lopez Jr.
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
//   documentation files (the "Software"), to deal in the Software without restriction, including without limitation
//   the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
//   to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//   The above copyright notice and this permission notice shall be included in all copies or substantial portions of
//   the Software.
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
//   THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
//   CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
//   IN THE SOFTWARE.
//
// The OkLab matrices are Bjorn Ottosson's published constants; the sRGB transfer is standard.
//
// Root signature: b0 = NrConstantsDX12 (14 dwords), t0..t3 SRV table, u0..u1 UAV table, s0 static linear clamp.

namespace shaderapidx12
{
struct NrConstantsDX12
{
	uint32_t mode; // 0 encode, 1 resolve
	float whitePoint;
	uint32_t width, height;
	float transferStrength, colourStrength;
	uint32_t debugView;
	float maxRatio;
	uint32_t reversibleMode, applyModel, compareMode;
	float compareSplit, compareZoom;
	uint32_t compareSwap;
};

static_assert( sizeof( NrConstantsDX12 ) == 14 * 4, "root constants must match the HLSL cbuffer" );

static const char kNrShaderSourceDX12[] = R"HLSL(
cbuffer Params : register(b0)
{
    uint  gMode;
    float gWhitePoint;
    uint  gWidth;
    uint  gHeight;
    float gTransferStrength;
    float gColourStrength;
    uint  gDebugView;      // 0 off, 1 proxy, 2 model answer, 3 amplified edit
    float gMaxRatio;
    uint  gReversibleMode; // 0 soft knee, 1 Neutwo composed, 2 Neutwo replace, 3 hybrid composed, 4 hybrid replace
    uint  gApplyModel;
    uint  gCompareMode;    // 0 off, 1 side by side, 2 wipe
    float gCompareSplit;
    float gCompareZoom;
    uint  gCompareSwap;
};

Texture2D<float4>   gSource   : register(t0); // encode: the layer input. resolve: the proxy. depth: scene depth.
Texture2D<float4>   gModel    : register(t1); // resolve: the model's answer.
Texture2D<float4>   gOriginal : register(t2); // resolve: the layer input.
RWTexture2D<float4> gTarget   : register(u0); // encode: the proxy. resolve: the layer output.
RWTexture2D<float>  gDepthOut : register(u1); // depth: the typed clone.
SamplerState        gLinear   : register(s0);

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

float WhitePoint() { return max(gWhitePoint, 1e-4); }

float SanitizeFinite(float v, float fallback) { return isfinite(v) ? v : fallback; }
float3 SanitizeFinite3(float3 v, float3 fallback) { return float3(SanitizeFinite(v.x, fallback.x), SanitizeFinite(v.y, fallback.y), SanitizeFinite(v.z, fallback.z)); }
float SafeDivide(float n, float d, float fallback) { return abs(d) > 1e-8 ? n / d : fallback; }

// Hunt-Pointer-Estevez LMS over linear BT.709 with a fixed D65 adaptation state.
float3 LMSToBT709(float3 c)
{
    const float3x3 m = { 5.62059812, -4.57145756, 0.15577924, -1.15555585, 2.25800438, -0.15415806, 0.03059913, -0.19018011, 1.06820532 };
    return mul(m, c);
}
float3 BT709ToLMS(float3 c)
{
    const float3x3 m = { 0.30569589, 0.62271286, 0.04528636, 0.15776262, 0.76968599, 0.08807030, 0.01933082, 0.11919478, 0.95053215 };
    return mul(m, c);
}
float3 D65NeutralBT709(float3 stateLms, float luminance)
{
    float3 d65 = LMSToBT709(max(stateLms, 1e-8));
    return d65 * (luminance / max(dot(d65, kLuma), 1e-8));
}
// The largest scale toward the neutral axis that leaves no channel negative; 1 for a representable colour.
float GamutCompressionScale(float3 c, float3 stateLms)
{
    c = SanitizeFinite3(c, float3(0, 0, 0));
    const float y = dot(c, kLuma);
    if (!(y > 1e-8)) return 1.0;
    const float3 n = D65NeutralBT709(stateLms, y);
    float s = 1.0;
    if (c.r < 0.0 && n.r > c.r) s = min(s, SafeDivide(n.r, n.r - c.r, 1.0));
    if (c.g < 0.0 && n.g > c.g) s = min(s, SafeDivide(n.g, n.g - c.g, 1.0));
    if (c.b < 0.0 && n.b > c.b) s = min(s, SafeDivide(n.b, n.b - c.b, 1.0));
    return saturate(SanitizeFinite(s, 1.0));
}
float3 ClampGamut(float3 c)
{
    const float3 stateLms = BT709ToLMS(float3(0.18, 0.18, 0.18));
    const float s = GamutCompressionScale(c, stateLms);
    if (s >= 1.0) return c;
    const float3 n = D65NeutralBT709(stateLms, dot(c, kLuma));
    return SanitizeFinite3(n + (c - n) * s, max(n, 0.0));
}

float3 CbrtSigned(float3 v) { return sign(v) * pow(abs(v), 1.0 / 3.0); }
float3 ToOkLab(float3 c)
{
    const float3x3 rgbToLms = { 0.4122214708, 0.5363325363, 0.0514459929, 0.2119034982, 0.6806995451, 0.1073969566, 0.0883024619, 0.2817188376, 0.6299787005 };
    const float3x3 lmsToLab = { 0.2104542553, 0.7936177850, -0.0040720468, 1.9779984951, -2.4285922050, 0.4505937099, 0.0259040371, 0.7827717662, -0.8086757660 };
    return mul(lmsToLab, CbrtSigned(mul(rgbToLms, c)));
}
float3 FromOkLab(float3 lab)
{
    const float3x3 labToLms = { 1.0, 0.3963377774, 0.2158037573, 1.0, -0.1055613458, -0.0638541728, 1.0, -0.0894841775, -1.2914855480 };
    const float3x3 lmsToRgb = { 4.0767416621, -3.3077115913, 0.2309699292, -1.2684380046, 2.6097574011, -0.3413193965, -0.0041960863, -0.7034186147, 1.7076147010 };
    const float3 lms = mul(labToLms, lab);
    return mul(lmsToRgb, lms * lms * lms);
}
// Hue and chroma direction from `correct`, chroma magnitude from `incorrect`; the unit direction keeps near-grey
// model pixels from amplifying noise.
float3 HueOkLab(float3 incorrect, float3 correct)
{
    float3 incorrectLab = ToOkLab(incorrect);
    const float3 correctLab = ToOkLab(correct);
    const float incorrectChroma = length(incorrectLab.yz), correctChroma = length(correctLab.yz);
    const float2 direction = correctChroma > 1e-5 ? correctLab.yz / correctChroma : float2(0.0, 0.0);
    incorrectLab.yz = direction * incorrectChroma;
    return ClampGamut(FromOkLab(incorrectLab));
}

float3 LinearToSrgb(float3 v) { v = saturate(v); return lerp(v * 12.92, 1.055 * pow(max(v, 1e-8), 1.0 / 2.4) - 0.055, step(0.0031308, v)); }
float3 SrgbToLinear(float3 v) { v = saturate(v); return lerp(v / 12.92, pow((v + 0.055) / 1.055, 2.4), step(0.04045, v)); }

// Mode 0 proxy: luminance soft knee above 0.75, then one scalar on the peak channel so nothing clips per channel.
float3 SoftKnee(float3 display)
{
    const float luma = dot(display, kLuma);
    if (luma > 0.75) display *= (0.75 + 0.25 * (1.0 - exp(-(luma - 0.75) / 0.25))) / luma;
    const float peak = max(display.r, max(display.g, display.b));
    return peak > 1.0 ? display / peak : display;
}
float Neutwo(float x) { return x * rsqrt(x * x + 1.0); }
float3 NeutwoEncode(float3 v)
{
    v = max(v, 0.0);
    const float m = max(v.r, max(v.g, v.b));
    return m <= 1e-6 ? v : v * (Neutwo(m) / m);
}
float3 NeutwoDecode(float3 y)
{
    y = max(y, 0.0);
    const float m = min(max(y.r, max(y.g, y.b)), 0.999999);
    return m <= 1e-6 ? y : y * ((m * rsqrt(max(1.0 - m * m, 1e-8))) / m);
}
float HybridCurve(float m)
{
    const float k = 0.75;
    if (m <= k) return m;
    const float e = (m - k) / (1.0 - k);
    return k + (1.0 - k) * (e * rsqrt(e * e + 1.0));
}
float3 HybridEncode(float3 v)
{
    v = max(v, 0.0);
    const float m = max(v.r, max(v.g, v.b));
    return m <= 1e-6 ? v : v * (HybridCurve(m) / m);
}
float HybridCurveInv(float y)
{
    const float k = 0.75;
    if (y <= k) return y;
    const float u = min((y - k) / (1.0 - k), 0.999999);
    return k + (1.0 - k) * (u * rsqrt(max(1.0 - u * u, 1e-8)));
}
float3 HybridDecode(float3 y)
{
    y = max(y, 0.0);
    const float m = max(y.r, max(y.g, y.b));
    return m <= 1e-6 ? y : y * (HybridCurveInv(m) / m);
}

[numthreads(8, 8, 1)]
void CSDepth(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight) return;
    gDepthOut[id.xy] = gSource.Load(int3(id.xy, 0)).r;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight) return;

    if (gMode == 0)
    {
        const float4 source = gSource.Load(int3(id.xy, 0));
        const float3 normalized = max(source.rgb, 0.0) / WhitePoint();
        const float3 display = gReversibleMode == 0 ? SoftKnee(normalized) : gReversibleMode >= 3 ? HybridEncode(normalized) : NeutwoEncode(normalized);
        // The reversible proxies are fed opaque; the knee keeps the layer's alpha.
        gTarget[id.xy] = float4(LinearToSrgb(display), gReversibleMode != 0 ? 1.0 : source.a);
        return;
    }

    const float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);
    float2 cmpUv = uv;
    bool showOriginal = false, onDivider = false, outsideFrame = false;
    if (gCompareMode == 1)
    {
        // Side by side: each half shows the whole frame at its own aspect, letterboxed (zoom 1) or cropped (zoom 2).
        showOriginal = (uv.x < 0.5) != (gCompareSwap != 0);
        const float2 half2 = float2(uv.x < 0.5 ? uv.x * 2.0 : (uv.x - 0.5) * 2.0, uv.y) - 0.5;
        const float zoom = max(gCompareZoom, 1e-3);
        cmpUv = float2(0.5 + half2.x / zoom, 0.5 + half2.y * 2.0 / zoom);
        outsideFrame = any(cmpUv < 0.0) || any(cmpUv > 1.0);
        onDivider = abs(uv.x - 0.5) < (1.0 / max(gWidth, 1u));
    }
    else if (gCompareMode == 2)
    {
        showOriginal = (uv.x < gCompareSplit) != (gCompareSwap != 0);
        onDivider = abs(uv.x - gCompareSplit) < (1.0 / max(gWidth, 1u));
    }

    const float3 proxy = SrgbToLinear(gSource.SampleLevel(gLinear, cmpUv, 0).rgb);
    const float3 model = SrgbToLinear(gModel.SampleLevel(gLinear, cmpUv, 0).rgb);
    const float4 originalSample = gCompareMode == 1 ? gOriginal.SampleLevel(gLinear, cmpUv, 0) : gOriginal.Load(int3(id.xy, 0));
    // Proxy and model sit in 0..1 with 1 at the white point; the layer input is open-ended linear light.
    const float normScale = WhitePoint();
    const float3 original = originalSample.rgb / normScale;
    const float originalLuma = dot(original, kLuma), proxyLuma = dot(proxy, kLuma);

    if (gApplyModel == 0) { gTarget[id.xy] = float4(max(originalSample.rgb, 0.0), originalSample.a); return; }
    if (gDebugView == 1) { gTarget[id.xy] = float4(proxy * normScale, originalSample.a); return; }
    if (gDebugView == 2) { gTarget[id.xy] = float4(model * normScale, originalSample.a); return; }
    if (gDebugView == 3) { gTarget[id.xy] = float4(SrgbToLinear(saturate(0.5 + (model - proxy) * 20.0)) * normScale, originalSample.a); return; }

    // The model's answer is a complete picture, rescaled to where the input's luminance says it should sit: below the
    // proxy the input's own luminance is the target; above it the excess is headroom the proxy could not show.
    const float modelLuma = dot(model, kLuma);
    float3 upgraded;
    if (modelLuma <= 1e-5) upgraded = original;
    else
    {
        const float ratio = originalLuma < proxyLuma ? originalLuma / max(proxyLuma, 1e-6) : (modelLuma + max(0.0, originalLuma - proxyLuma)) / modelLuma;
        upgraded = lerp(original, HueOkLab(model * ratio, model), saturate(gTransferStrength));
    }

    // A floored, two-sided multiplicative guard on the composed luminance; strength above 1 amplifies the ratio.
    const float kRatioFloor = 1.0 / 512.0;
    const float lumaRatio = (dot(upgraded, kLuma) + kRatioFloor) / (originalLuma + kRatioFloor);
    const float amplified = pow(max(lumaRatio, 1e-6), 1.0 + max(gTransferStrength - 1.0, 0.0));
    const float guard = max(gMaxRatio, 1.0);
    const float boundedRatio = clamp(amplified, 1.0 / guard, guard);
    upgraded *= boundedRatio / max(lumaRatio, 1e-6);

    // Colour strength 0 keeps the input's hue with the model's light; 1 takes the model's colour; above 1 scales
    // OkLab chroma and rolls off at the gamut boundary.
    float3 result = lerp(original * boundedRatio, upgraded, min(gColourStrength, 1.0));
    if (gColourStrength > 1.0) result = ClampGamut(FromOkLab(float3(1.0, gColourStrength, gColourStrength) * ToOkLab(max(result, 0.0))));

    // Replace modes take the model's answer through the exact inverse of the proxy curve, without composition.
    if (gReversibleMode == 2) result = NeutwoDecode(model);
    else if (gReversibleMode == 4) result = HybridDecode(model);

    result *= normScale;
    if (showOriginal) result = originalSample.rgb;
    if (outsideFrame) result = float3(0.0, 0.0, 0.0);
    if (onDivider) result = float3(normScale, normScale, normScale);
    gTarget[id.xy] = float4(max(result, 0.0), originalSample.a);
}
)HLSL";
} // namespace shaderapidx12

#endif // UPSCALER_NR_SHADERS_DX12_H
