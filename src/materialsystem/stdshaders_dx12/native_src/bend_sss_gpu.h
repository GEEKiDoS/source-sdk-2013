#ifndef DX12_BEND_SSS_GPU_H
#define DX12_BEND_SSS_GPU_H

// Copyright 2023 Sony Interactive Entertainment.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// https://www.bendstudio.com
// https://www.twitter.com/bendstudio

// Bend Studio screen-space shadow projection (GPU).
// Renderer adaptations: checked integer loads, viewport-local output, normalized raw
// depth, runtime sample count, and group synchronization instead of wave intrinsics.
// The projection, edge filter, perspective-correct LDS depths and shadow window
// below follow the supplied Bend implementation.

#if !defined(WAVE_SIZE) || !defined(SAMPLE_COUNT) || !defined(HARD_SHADOW_SAMPLES) || !defined(FADE_OUT_SAMPLES)
#error Bend SSS requires WAVE_SIZE, SAMPLE_COUNT, HARD_SHADOW_SAMPLES and FADE_OUT_SAMPLES
#endif
#if WAVE_SIZE != 64
#error Bend SSS dispatch partitioning requires 64 threads
#endif

struct DispatchParameters
{
	float SurfaceThickness;
	float BilinearThreshold;
	float ShadowContrast;
	int MaxSampleCount;
	bool IgnoreEdgePixels;
	bool UsePrecisionOffset;
	bool BilinearSamplingOffsetMode;
	bool DebugOutputEdgeMask;
	bool DebugOutputThreadIndex;
	bool DebugOutputWaveIndex;
	float2 DepthBounds;
	float2 RawDepthBounds;
	bool UseEarlyOut;
	float4 LightCoordinate;
	int2 WaveOffset;
	float FarDepthValue;
	float NearDepthValue;
	int2 ViewportOrigin;
	int2 ViewportSize;
	int2 DepthTextureSize;
	int2 OutputTextureSize;
	uint DepthSamples;
#if MSAA
	Texture2DMS<float> DepthTexture;
#else
	Texture2D<float> DepthTexture;
#endif
	RWTexture2D<float> OutputTexture;
};

static bool EarlyOutPixel( DispatchParameters p, float depth )
{
	// The minimum is inclusive: raw zero is a valid viewmodel/near-plane depth.
	return depth >= p.DepthBounds.y || depth < p.DepthBounds.x;
}

static float BendLoadDepth( DispatchParameters p, int2 localXY )
{
	// Never sample another view or rely on undefined out-of-bounds Load behavior.
	if ( any( localXY < 0 ) || any( localXY >= p.ViewportSize ) )
		return p.FarDepthValue;
	int2 xy = localXY + p.ViewportOrigin;
	if ( any( xy < 0 ) || any( xy >= p.DepthTextureSize ) )
		return p.FarDepthValue;

#if MSAA
	// Use the closest valid sample, ignoring samples outside this pass's raw
	// depth interval (including the other world/viewmodel range and clear depth).
	float rawDepth = p.RawDepthBounds.y;
	[loop] for ( uint sampleIndex = 0; sampleIndex < p.DepthSamples; ++sampleIndex )
	{
		float d = p.DepthTexture.Load( xy, sampleIndex );
		if ( d >= p.RawDepthBounds.x && d < p.RawDepthBounds.y )
			rawDepth = min( rawDepth, d );
	}
#else
	float rawDepth = p.DepthTexture.Load( int3( xy, 0 ) );
#endif
	// Ordered comparisons also reject NaNs. Dividing by the exclusive far
	// value expands the viewmodel's compressed raw depth interval to [0,1).
	if ( !( rawDepth >= p.RawDepthBounds.x && rawDepth < p.RawDepthBounds.y ) )
		return p.FarDepthValue;
	return rawDepth / p.RawDepthBounds.y;
}

static void ComputeWavefrontExtents( DispatchParameters p, int3 groupID, uint lane, out float2 deltaXY, out float2 pixelXY, out float pixelDistance, out bool majorX )
{
	int2 xy = groupID.yz * WAVE_SIZE + p.WaveOffset;
	float2 lightXY = floor( p.LightCoordinate.xy ) + 0.5;
	float2 lightFraction = p.LightCoordinate.xy - lightXY;
	bool reverse = p.LightCoordinate.w > 0.0;
	int2 signXY = sign( xy );
	bool horizontal = abs( xy.x + signXY.y ) < abs( xy.y - signXY.x );
	int2 axis = horizontal ? int2( signXY.y, 0 ) : int2( 0, -signXY.x );
	xy += axis * groupID.x;
	float2 xyF = float2( xy );
	majorX = abs( xyF.x ) > abs( xyF.y );
	float major = majorX ? xyF.x : xyF.y;
	float majorStart = abs( major );
	float majorEnd = abs( major ) - float( WAVE_SIZE );
	float lightFrac = majorX ? lightFraction.x : lightFraction.y;
	lightFrac = major > 0.0 ? -lightFrac : lightFrac;
	float2 startXY = xyF + lightXY;
	// The innermost ring interpolates to pixel centers to avoid skipped pixels.
	float2 endXY = lerp( p.LightCoordinate.xy, startXY, ( majorEnd + lightFrac ) / ( majorStart + lightFrac ) );
	deltaXY = startXY - endXY;
	float threadStep = float( lane ^ ( reverse ? 0 : WAVE_SIZE - 1 ) );
	pixelXY = lerp( startXY, endXY, threadStep / float( WAVE_SIZE ) );
	pixelDistance = majorStart - threadStep + lightFrac;
}

// Two reads per lane cover 60 shadow samples plus the following lane's depth.
#define BEND_READ_COUNT ( SAMPLE_COUNT / WAVE_SIZE + 2 )
groupshared float BendDepthData[BEND_READ_COUNT * WAVE_SIZE];

// The quality budgets and hard/fade lengths are all multiples of four. Reading
// the four independent minima together also avoids dynamically indexing a vector lvalue in FXC.
static float4 BendShadowDeltas( int idx, float startDepth, float depthScale )
{
	return abs( startDepth - depthScale * float4( BendDepthData[idx], BendDepthData[idx + 1],
		BendDepthData[idx + 2], BendDepthData[idx + 3] ) );
}

void WriteScreenSpaceShadow( DispatchParameters p, int3 groupID, int groupThreadID )
{
	float2 deltaXY, pixelXY;
	float pixelDistance;
	bool majorX;
	ComputeWavefrontExtents( p, groupID, uint( groupThreadID ), deltaXY, pixelXY, pixelDistance, majorX );

	float samplingDepth[BEND_READ_COUNT], shadowingDepth[BEND_READ_COUNT], thicknessScale[BEND_READ_COUNT], sampleDistance[BEND_READ_COUNT];
	const float direction = -p.LightCoordinate.w;
	const float zSign = p.NearDepthValue > p.FarDepthValue ? -1.0 : 1.0;
	bool isEdge = false;
	int2 writeXY = int2( floor( pixelXY ) );
	int i;
	[unroll] for ( i = 0; i < BEND_READ_COUNT; ++i )
	{
		int2 readXY = int2( floor( pixelXY ) );
		float minorAxis = majorX ? pixelXY.y : pixelXY.x;
		float bilinear = frac( minorAxis ) - 0.5;
		const int bias = bilinear > 0.0 ? 1 : -1;
		float2 depths;
		depths.x = BendLoadDepth( p, readXY );
		depths.y = BendLoadDepth( p, readXY + ( majorX ? int2( 0, bias ) : int2( bias, 0 ) ) );
		thicknessScale[i] = abs( p.FarDepthValue - depths.x );
		bool pointFilter = abs( depths.x - depths.y ) > thicknessScale[i] * p.BilinearThreshold;
		if ( i == 0 ) isEdge = pointFilter;
		if ( p.BilinearSamplingOffsetMode )
		{
			bilinear = pointFilter ? 0.0 : bilinear;
			samplingDepth[i] = lerp( depths.x, depths.y, abs( bilinear ) );
			shadowingDepth[i] = ( p.IgnoreEdgePixels && pointFilter ) ? 1e20 : samplingDepth[i];
		}
		else
		{
			samplingDepth[i] = depths.x;
			float edgeDepth = p.IgnoreEdgePixels ? 1e20 : depths.x;
			float shadowDepth = depths.x + abs( depths.x - depths.y ) * zSign;
			shadowingDepth[i] = pointFilter ? edgeDepth : shadowDepth;
		}
		sampleDistance[i] = pixelDistance + float( WAVE_SIZE * i ) * direction;
		pixelXY += deltaXY * direction;
	}

	[unroll] for ( i = 0; i < BEND_READ_COUNT; ++i )
	{
		// Perspective correction makes all light rays parallel in the LDS array.
		// Ignore extended reads that overshoot the light, including its singular center.
		float stored = 1e10;
		if ( sampleDistance[i] > 0.0 )
			stored = ( shadowingDepth[i] - p.LightCoordinate.z ) / sampleDistance[i];
		BendDepthData[i * WAVE_SIZE + groupThreadID] = stored;
	}
	// Every lane contributes before any lane can return. This is independent of
	// the hardware wave width, and works in both FXC SM5.1 and DXC SM6.
	GroupMemoryBarrierWithGroupSync();

	if ( any( writeXY < 0 ) || any( writeXY >= p.ViewportSize ) || any( writeXY >= p.OutputTextureSize ) )
		return;
	bool skipPixel = p.UseEarlyOut && EarlyOutPixel( p, samplingDepth[0] );
	if ( skipPixel || !( sampleDistance[0] > 0.0 ) )
	{
		// Sky, clear depth, invalid reads and the other depth range must overwrite
		// stale visibility, not merely return and leave an old shadow behind.
		p.OutputTexture[writeXY] = 1.0;
		return;
	}

	float startDepth = samplingDepth[0];
	if ( p.UsePrecisionOffset ) startDepth = lerp( startDepth, p.FarDepthValue, -1.0 / 65535.0 );
	startDepth = ( startDepth - p.LightCoordinate.z ) / sampleDistance[0];
	int sampleIndex = groupThreadID + 1;
	float4 shadowValue = 1.0;
	float hardShadow = 1.0;
	// SurfaceThickness is a fraction of the remaining depth to the far plane,
	// not a fraction of the whole depth range.
	float depthScale = min( sampleDistance[0] + direction, 1.0 / p.SurfaceThickness ) * sampleDistance[0] / thicknessScale[0];
	startDepth = startDepth * depthScale - zSign;
	[unroll] for ( i = 0; i < HARD_SHADOW_SAMPLES; ++i )
		hardShadow = min( hardShadow, abs( startDepth - BendDepthData[sampleIndex + i] * depthScale ) );
	int fadeStart = p.MaxSampleCount - FADE_OUT_SAMPLES;
	[loop] for ( i = HARD_SHADOW_SAMPLES; i < fadeStart; i += 4 )
		shadowValue = min( shadowValue, BendShadowDeltas( sampleIndex + i, startDepth, depthScale ) );
	[unroll] for ( i = 0; i < FADE_OUT_SAMPLES; i += 4 )
	{
		float4 fade = ( i + float4( 1, 2, 3, 4 ) ) * ( 0.75 / ( FADE_OUT_SAMPLES + 1 ) );
		shadowValue = min( shadowValue, BendShadowDeltas( sampleIndex + fadeStart + i, startDepth, depthScale ) + fade );
	}
	shadowValue = saturate( shadowValue * p.ShadowContrast + ( 1.0 - p.ShadowContrast ) );
	hardShadow = saturate( hardShadow * p.ShadowContrast + ( 1.0 - p.ShadowContrast ) );
	float result = min( hardShadow, dot( shadowValue, 0.25 ) );
	if ( p.DebugOutputEdgeMask ) result = isEdge ? 1.0 : 0.0;
	if ( p.DebugOutputThreadIndex ) result = groupThreadID / float( WAVE_SIZE );
	if ( p.DebugOutputWaveIndex ) result = frac( groupID.x / float( WAVE_SIZE ) );
	p.OutputTexture[writeXY] = result;
}

#endif // DX12_BEND_SSS_GPU_H
