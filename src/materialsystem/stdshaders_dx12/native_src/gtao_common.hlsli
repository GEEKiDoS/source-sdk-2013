// XeGTAO port for Source DX12 native shaders.
// Origin: https://github.com/GameTechDev/XeGTAO, Intel Corporation.
// MIT License
// Copyright (C) 2016-2021, Intel Corporation
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE.

#define XE_GTAO_PI 3.14159265358979323846
#define XE_GTAO_PI_HALF 1.57079632679489661923
#define XE_GTAO_DEPTH_MIP_LEVELS 5

cbuffer GtaoParams : register( b0, space1 )
{
	int2 gViewportSize;
	float2 gViewportPixelSize;
	float2 gDepthUnpackConsts;
	float2 gCameraTanHalfFOV;
	float2 gNdcToViewMul;
	float2 gNdcToViewAdd;
	float2 gNdcToViewMulXPixelSize;
	float gEffectRadius;
	float gEffectFalloffRange;
	float gRadiusMultiplier;
	float gFinalValuePower;
	float gDenoiseBlurBeta;
	float gSampleDistributionPower;
	float gThinOccluderCompensation;
	float gDepthMIPSamplingOffset;
	int gNoiseIndex;
	int gSliceCount;
	int gStepsPerSlice;
	int gDenoiseFinal;
	int2 gSceneOrigin;
	float2 gDepthScale;
};

float ViewDepth( float z )
{
	float denominator = gDepthUnpackConsts.y - z;
	return abs( denominator ) < 1e-6 ? 65504.0 : gDepthUnpackConsts.x / denominator;
}

float3 ViewPosition( float2 uv, float depth )
{
	return float3( ( uv * gNdcToViewMul + gNdcToViewAdd ) * depth, depth );
}

float4 CalculateEdges( float centerZ, float leftZ, float rightZ, float topZ, float bottomZ )
{
	float4 e = float4( leftZ, rightZ, topZ, bottomZ ) - centerZ;
	float slopeLR = ( e.y - e.x ) * 0.5;
	float slopeTB = ( e.w - e.z ) * 0.5;
	float4 adjusted = e + float4( slopeLR, -slopeLR, slopeTB, -slopeTB );
	e = min( abs( e ), abs( adjusted ) );
	return saturate( 1.25 - e / max( centerZ * 0.011, 1e-5 ) );
}

float PackEdges( float4 edges )
{
	edges = round( saturate( edges ) * 2.9 );
	return dot( edges, float4( 64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0 ) );
}

float4 UnpackEdges( float packed )
{
	uint p = (uint)( saturate( packed ) * 255.5 );
	return saturate( float4( ( p >> 6 ) & 3, ( p >> 4 ) & 3, ( p >> 2 ) & 3, p & 3 ) / 3.0 );
}

float3 CalculateNormal( float4 edges, float3 center, float3 left, float3 right, float3 top, float3 bottom )
{
	float4 accepted = saturate( float4( edges.x * edges.z, edges.z * edges.y, edges.y * edges.w, edges.w * edges.x ) + 0.01 );
	left = normalize( left - center );
	right = normalize( right - center );
	top = normalize( top - center );
	bottom = normalize( bottom - center );
	return normalize( accepted.x * cross( left, top ) + accepted.y * cross( top, right ) + accepted.z * cross( right, bottom ) + accepted.w * cross( bottom, left ) );
}

float FastSqrt( float x )
{
	return asfloat( 0x1fbd1df5 + ( asint( x ) >> 1 ) );
}

float FastACos( float inX )
{
	float x = abs( inX );
	float res = ( -0.156583 * x + XE_GTAO_PI_HALF ) * FastSqrt( 1.0 - x );
	return inX >= 0 ? res : XE_GTAO_PI - res;
}

uint HilbertIndex( uint x, uint y )
{
	uint index = 0;
	for ( uint level = 32; level > 0; level >>= 1 )
	{
		uint rx = ( x & level ) != 0;
		uint ry = ( y & level ) != 0;
		index += level * level * ( ( 3 * rx ) ^ ry );
		if ( ry == 0 )
		{
			if ( rx == 1 ) { x = 63 - x; y = 63 - y; }
			uint t = x; x = y; y = t;
		}
	}
	return index;
}

float2 SpatioTemporalNoise( uint2 pix, int frame )
{
	uint h = HilbertIndex( pix.x & 63, pix.y & 63 );
	float2 r2 = float2( 0.754877666, 0.569840296 );
	return frac( float2( h * r2.x, h * r2.y ) + float2( frame * 0.381966011, frame * 0.618033989 ) );
}

float DepthMipFilter( float a, float b, float c, float d )
{
	float maxDepth = max( max( a, b ), max( c, d ) );
	float effectRadius = 0.75 * gEffectRadius * gRadiusMultiplier;
	float falloffRange = max( gEffectFalloffRange * effectRadius, 1e-4 );
	float falloffFrom = effectRadius * ( 1.0 - gEffectFalloffRange );
	float falloffMul = -1.0 / falloffRange;
	float falloffAdd = falloffFrom / falloffRange + 1.0;
	float4 w = saturate( ( maxDepth - float4( a, b, c, d ) ) * falloffMul + falloffAdd );
	return dot( w, float4( a, b, c, d ) ) / max( dot( w, 1.0.xxxx ), 1e-4 );
}
