// Dense ambient probe grid (public/hprobe_bsp.h) for pbr_model_ps51 ENHANCED (PBR-only; no legacy shader includes it).
// Backend-owned pixel-stage space 4: DX12ProbeConstantsV1 plus the ten descriptors of DX12_PROBE_TABLE_COUNT
// (ishaderapidx12lighting.h). Probe_Decode runs once per pixel; Probe_Irradiance / Probe_IrradianceSoft then evaluate any
// direction (N, -N for backlight, the subsurface-softened N) at the same normal-biased sample point.
//
// Coverage means the resident brick provided enough visible, valid interpolation weight. The quad variant
// also distinguishes an absent brick from a covered domain that rejected all usable corners.
// Irradiance is the normalized diffuse E / pi in the engine's linear cube units, exactly what the ambient cube holds:
// multiply by the diffuse color only.
#ifndef PBR_PROBE_HLSLI
#define PBR_PROBE_HLSLI

cbuffer DX12ProbeConstantsV1 : register( b0, space4 )
{
	float4 cProbeOrigin;     // xyz grid origin, w 1 / spacing
	uint4 cProbeBricks;      // xyz indirection brick dims, w 1 = a grid is bound
	float4 cProbeAtlas;      // xyz 1 / atlas texels, w normal bias (0.25 * spacing)
	uint4 cProbeAtlasBricks; // xyz atlas bricks
};
Texture3D<uint> ProbeIndirection : register( t0, space4 );
Texture3D<float3> ProbeDC : register( t1, space4 );
Texture3D<float4> ProbeBands[6] : register( t2, space4 );
Texture3D<float> ProbeValidity : register( t8, space4 );
Texture3D<uint> ProbeFreeDistance : register( t9, space4 );
SamplerState ProbeLinear : register( s0, space4 );

// E(n) = dc + sum_k band[k - 1] * Y_k(n), k = 1..8, with band[k - 1] = 4 * dc * s_k per color channel.
struct ProbeSH
{
	float3 dc;
	float3 band[8];
};

// Real SH basis of hprobe_bsp.h: Y1 y, Y2 z, Y3 x (x 0.488603); Y4 xy, Y5 yz, Y7 xz (x 1.092548); Y6 (3z^2 - 1) x 0.315392; Y8 (x^2 - y^2) x 0.546274.
float3 Probe_L1( ProbeSH sh, float3 n )
{
	return sh.band[0] * ( 0.488603 * n.y ) + sh.band[1] * ( 0.488603 * n.z ) + sh.band[2] * ( 0.488603 * n.x );
}
float3 Probe_L2( ProbeSH sh, float3 n )
{
	return sh.band[3] * ( 1.092548 * n.x * n.y ) + sh.band[4] * ( 1.092548 * n.y * n.z ) +
		sh.band[5] * ( 0.315392 * ( 3.0 * n.z * n.z - 1.0 ) ) + sh.band[6] * ( 1.092548 * n.x * n.z ) +
		sh.band[7] * ( 0.546274 * ( n.x * n.x - n.y * n.y ) );
}
float3 Probe_Irradiance( ProbeSH sh, float3 n )
{
	return max( sh.dc + Probe_L1( sh, n ) + Probe_L2( sh, n ), 0.0 );
}
// Subsurface-softened lobe: the l = 1 band halved, the l = 2 band quartered.
float3 Probe_IrradianceSoft( ProbeSH sh, float3 n )
{
	return max( sh.dc + 0.5 * Probe_L1( sh, n ) + 0.25 * Probe_L2( sh, n ), 0.0 );
}

// DC and bands are stored premultiplied by validity (invalid texels are zero), so the hardware-filtered values
// divide by the filtered validity. The 5x5x5 probes of a brick duplicate their edges, so the clamped local
// coordinate never filters across bricks.
bool Probe_FreeVisible( uint packed, float3 delta )
{
	[unroll] for ( uint axis = 0; axis < 3; ++axis )
	{
		const float distance = abs( delta[axis] );
		const uint side = axis * 2 + ( delta[axis] < 0.0 ? 1 : 0 );
		const float freeDistance = float( ( packed >> ( side * 4 ) ) & 15u ) / 15.0;
		if ( distance > freeDistance )
			return false;
	}
	return true;
}

// Both corner walks are loops, not unrolled: unrolled, the fallback's 8 x 9 loads were hoisted together and set the
// model shader's register peak (GTX 1050, c1m2 foliage: about 2 ms of the opaque renderables, same result).
bool Probe_Decode( float3 positionWS, float3 normalWS, out ProbeSH sh )
{
	sh = (ProbeSH)0;
	bool covered = false;
	float3 grid = ( positionWS + normalWS * cProbeAtlas.w - cProbeOrigin.xyz ) * cProbeOrigin.w;
	float3 brick = floor( grid * 0.25 );
	[branch] if ( cProbeBricks.w != 0 && all( brick >= 0.0 ) && all( brick < float3( cProbeBricks.xyz ) ) )
	{
		uint slot = ProbeIndirection.Load( int4( int3( brick ), 0 ) );
		[branch] if ( slot != 0xffffffffu )
		{
			uint3 atlasBricks = cProbeAtlasBricks.xyz;
			uint3 atlasBrick = uint3( slot % atlasBricks.x, ( slot / atlasBricks.x ) % atlasBricks.y, slot / ( atlasBricks.x * atlasBricks.y ) );
			float3 local = clamp( grid - brick * 4.0, 0.0, 4.0 );
			uint3 base = uint3( min( floor( local ), 3.0 ) );
			float3 fraction = local - float3( base );
			uint3 atlasBase = atlasBrick * 5u;
			// One free-distance read per corner; the fallback reuses the mask instead of reloading.
			uint visibleMask = 0;
			[loop] for ( uint corner = 0; corner < 8; ++corner )
			{
				const uint3 offset = uint3( corner & 1u, ( corner >> 1u ) & 1u, corner >> 2u );
				const float3 cornerGrid = brick * 4.0 + float3( base + offset );
				const uint freeDistance = ProbeFreeDistance.Load( int4( atlasBase + base + offset, 0 ) );
				if ( Probe_FreeVisible( freeDistance, grid - cornerGrid ) )
					visibleMask |= 1u << corner;
			}
			const bool allVisible = visibleMask == 0xFFu;
			float3 uvw = ( float3( atlasBase ) + local + 0.5 ) * cProbeAtlas.xyz;
			[branch] if ( allVisible )
			{
				const float validity = ProbeValidity.SampleLevel( ProbeLinear, uvw, 0 );
				[branch] if ( validity >= 0.25 )
				{
					const float scale = 1.0 / validity;
					sh.dc = ProbeDC.SampleLevel( ProbeLinear, uvw, 0 ) * scale;
					[unroll] for ( uint c = 0; c < 3; ++c )
					{
						float4 low = ProbeBands[2 * c].SampleLevel( ProbeLinear, uvw, 0 ) * scale;
						float4 high = ProbeBands[2 * c + 1].SampleLevel( ProbeLinear, uvw, 0 ) * scale;
						[unroll] for ( uint k = 0; k < 4; ++k )
						{
							sh.band[k][c] = 4.0 * sh.dc[c] * low[k];
							sh.band[4 + k][c] = 4.0 * sh.dc[c] * high[k];
						}
					}
					covered = true;
				}
			}
			[branch] if ( !allVisible )
			{
				float weightSum = 0.0;
				float3 dc = 0.0;
				float4 low[3];
				float4 high[3];
				[unroll] for ( uint c = 0; c < 3; ++c )
				{
					low[c] = 0.0;
					high[c] = 0.0;
				}
				uint remaining = visibleMask;
				[loop] while ( remaining != 0 )
				{
					const uint corner = firstbitlow( remaining );
					remaining &= remaining - 1u;
					const uint3 offset = uint3( corner & 1u, ( corner >> 1u ) & 1u, corner >> 2u );
					const float3 weights = float3( offset ) * fraction + ( 1.0 - float3( offset ) ) * ( 1.0 - fraction );
					const float trilinear = weights.x * weights.y * weights.z;
					const uint3 coordinate = atlasBase + base + offset;
					if ( trilinear == 0.0 )
						continue;
					const float weight = trilinear * ProbeValidity.Load( int4( coordinate, 0 ) );
					if ( weight <= 0.0 )
						continue;
					weightSum += weight;
					dc += ProbeDC.Load( int4( coordinate, 0 ) ) * weight;
					[unroll] for ( uint c = 0; c < 3; ++c )
					{
						low[c] += ProbeBands[2 * c].Load( int4( coordinate, 0 ) ) * weight;
						high[c] += ProbeBands[2 * c + 1].Load( int4( coordinate, 0 ) ) * weight;
					}
				}
				[branch] if ( weightSum > 1e-4 )
				{
					sh.dc = dc / weightSum;
					[unroll] for ( uint c = 0; c < 3; ++c )
					{
						low[c] /= weightSum;
						high[c] /= weightSum;
						[unroll] for ( uint k = 0; k < 4; ++k )
						{
							sh.band[k][c] = 4.0 * sh.dc[c] * low[c][k];
							sh.band[4 + k][c] = 4.0 * sh.dc[c] * high[c][k];
						}
					}
					covered = true;
				}
			}
		}
	}
	return covered;
}

#if defined( PBR_PROBE_QUAD )
// Pixel stage: one decode per 2x2 quad. The probe field is far lower frequency than a pixel and the four lanes of a quad
// lie on one triangle, so the quad samples it once at its centre; each lane still evaluates the SH for its own normal.
// Every per-corner step is split across the lanes (lane q takes corners 2q and 2q + 1) and recombined with quad
// operations: the free-distance test reads 2 corners per lane instead of 8, and the !allVisible fallback accumulates 2
// corners per lane (9 loads each) instead of 8. The filtered allVisible samples come from seven different textures and
// stay per lane (splitting them would only diverge). All branches below are quad-uniform (same brick, cell and mask).
float3 Probe_QuadSum( float3 v ) { v += QuadReadAcrossX( v ); return v + QuadReadAcrossY( v ); }
float4 Probe_QuadSum( float4 v ) { v += QuadReadAcrossX( v ); return v + QuadReadAcrossY( v ); }
float Probe_QuadSum( float v ) { v += QuadReadAcrossX( v ); return v + QuadReadAcrossY( v ); }
uint Probe_QuadOr( uint v ) { v |= QuadReadAcrossX( v ); return v | QuadReadAcrossY( v ); }
// quadLane: ( x & 1 ) | ( ( y & 1 ) << 1 ) of the pixel's SV_Position, the D3D quad lane order.
bool Probe_DecodeQuad( float3 positionWS, float3 normalWS, uint quadLane, out ProbeSH sh, out bool brickResident )
{
	sh = (ProbeSH)0;
	brickResident = false;
	bool covered = false;
	const float3 position = Probe_QuadSum( positionWS ) * 0.25;
	const float3 normal = Probe_QuadSum( normalWS ) * 0.25;
	float3 grid = ( position + normal * cProbeAtlas.w - cProbeOrigin.xyz ) * cProbeOrigin.w;
	float3 brick = floor( grid * 0.25 );
	[branch] if ( cProbeBricks.w != 0 && all( brick >= 0.0 ) && all( brick < float3( cProbeBricks.xyz ) ) )
	{
		uint slot = ProbeIndirection.Load( int4( int3( brick ), 0 ) );
		[branch] if ( slot != 0xffffffffu )
		{
			brickResident = true;
			uint3 atlasBricks = cProbeAtlasBricks.xyz;
			uint3 atlasBrick = uint3( slot % atlasBricks.x, ( slot / atlasBricks.x ) % atlasBricks.y, slot / ( atlasBricks.x * atlasBricks.y ) );
			float3 local = clamp( grid - brick * 4.0, 0.0, 4.0 );
			uint3 base = uint3( min( floor( local ), 3.0 ) );
			float3 fraction = local - float3( base );
			uint3 atlasBase = atlasBrick * 5u;
			uint laneMask = 0;
			[unroll] for ( uint k = 0; k < 2; ++k )
			{
				const uint corner = 2u * quadLane + k;
				const uint3 offset = uint3( corner & 1u, ( corner >> 1u ) & 1u, corner >> 2u );
				const float3 cornerGrid = brick * 4.0 + float3( base + offset );
				const uint freeDistance = ProbeFreeDistance.Load( int4( atlasBase + base + offset, 0 ) );
				if ( Probe_FreeVisible( freeDistance, grid - cornerGrid ) )
					laneMask |= 1u << k;
			}
			const uint visibleMask = Probe_QuadOr( laneMask << ( 2u * quadLane ) );
			float3 uvw = ( float3( atlasBase ) + local + 0.5 ) * cProbeAtlas.xyz;
			[branch] if ( visibleMask == 0xFFu )
			{
				const float validity = ProbeValidity.SampleLevel( ProbeLinear, uvw, 0 );
				[branch] if ( validity >= 0.25 )
				{
					const float scale = 1.0 / validity;
					sh.dc = ProbeDC.SampleLevel( ProbeLinear, uvw, 0 ) * scale;
					[unroll] for ( uint c = 0; c < 3; ++c )
					{
						float4 low = ProbeBands[2 * c].SampleLevel( ProbeLinear, uvw, 0 ) * scale;
						float4 high = ProbeBands[2 * c + 1].SampleLevel( ProbeLinear, uvw, 0 ) * scale;
						[unroll] for ( uint k = 0; k < 4; ++k )
						{
							sh.band[k][c] = 4.0 * sh.dc[c] * low[k];
							sh.band[4 + k][c] = 4.0 * sh.dc[c] * high[k];
						}
					}
					covered = true;
				}
			}
			else
			{
				float weightSum = 0.0;
				float3 dc = 0.0;
				float4 low[3];
				float4 high[3];
				[unroll] for ( uint c = 0; c < 3; ++c )
				{
					low[c] = 0.0;
					high[c] = 0.0;
				}
				[loop] for ( uint k = 0; k < 2; ++k )
				{
					const uint corner = 2u * quadLane + k;
					const uint3 offset = uint3( corner & 1u, ( corner >> 1u ) & 1u, corner >> 2u );
					const float3 weights = float3( offset ) * fraction + ( 1.0 - float3( offset ) ) * ( 1.0 - fraction );
					const float trilinear = weights.x * weights.y * weights.z;
					const uint3 coordinate = atlasBase + base + offset;
					if ( ( laneMask & ( 1u << k ) ) == 0 || trilinear == 0.0 )
						continue;
					const float weight = trilinear * ProbeValidity.Load( int4( coordinate, 0 ) );
					if ( weight <= 0.0 )
						continue;
					weightSum += weight;
					dc += ProbeDC.Load( int4( coordinate, 0 ) ) * weight;
					[unroll] for ( uint c = 0; c < 3; ++c )
					{
						low[c] += ProbeBands[2 * c].Load( int4( coordinate, 0 ) ) * weight;
						high[c] += ProbeBands[2 * c + 1].Load( int4( coordinate, 0 ) ) * weight;
					}
				}
				weightSum = Probe_QuadSum( weightSum );
				[branch] if ( weightSum > 1e-4 )
				{
					sh.dc = Probe_QuadSum( dc ) / weightSum;
					[unroll] for ( uint c = 0; c < 3; ++c )
					{
						low[c] = Probe_QuadSum( low[c] ) / weightSum;
						high[c] = Probe_QuadSum( high[c] ) / weightSum;
						[unroll] for ( uint k = 0; k < 4; ++k )
						{
							sh.band[k][c] = 4.0 * sh.dc[c] * low[c][k];
							sh.band[4 + k][c] = 4.0 * sh.dc[c] * high[c][k];
						}
					}
					covered = true;
				}
			}
		}
	}
	return covered;
}
#endif
#endif
