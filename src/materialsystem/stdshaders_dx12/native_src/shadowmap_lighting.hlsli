//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef DX12_SHADOWMAP_LIGHTING_HLSLI
#define DX12_SHADOWMAP_LIGHTING_HLSLI
// Included after dx12_engine_cbuffers.h, only in lighting ABI 4 pixel shaders.
// All projections are normal D3D [0,1]. Matrices already expand the rendered
// guard: UV -> slotOrigin + UV * slotSize, with NO second guard remapping.
// Radiance is linear irradiance, already /255 and lightstyled by the view owner.
// Compile-cost rule: instantiate the kernel exactly once per pixel shader with
// one ShadowMap_GatherDirect call; ShadowMap_Debug consumes its result and never samples.
Texture2D<float> g_ShadowLocalAtlas[DX12_SHADOW_MAX_LOCAL_PAGES] : register(t0, space2);
Texture2D<float> g_ShadowCascadeAtlas : register(t1024, space2);
Texture2D<float> g_ShadowStaticSun : register(t1025, space2);
StructuredBuffer<RuntimeShadowLightGpu> g_ShadowLights : register(t1026, space2);
StructuredBuffer<uint2> g_ShadowTileRanges : register(t1027, space2);
StructuredBuffer<uint> g_ShadowTileIndices : register(t1028, space2);
Texture2D<uint> g_ShadowSunVisibility : register(t1029, space2);
SamplerComparisonState g_ShadowCmpSampler : register(s0, space2);

// Equal weights, rotation zero. Sample zero detects even a one-texel blocker.
// Remaining blocker points: sqrt((k+.5)/15), angle k*2.39996323, k=0..14.
static const float2 g_ShadowBlockerDisk[DX12_SHADOW_PCSS_BLOCKER_SAMPLES] =
{
	float2( 0.0, 0.0 ),
	float2( 0.1825741858, 0.0000000000 ),
	float2( -0.2331765131, 0.2136087867 ),
	float2( 0.0356914029, -0.4066851244 ),
	float2( 0.2939038917, 0.3833455827 ),
	float2( -0.5393497885, -0.0954033839 ),
	float2( 0.5109192039, -0.3250049750 ),
	float2( -0.1708924757, 0.6357114873 ),
	float2( -0.3259104815, -0.6275208029 ),
	float2( 0.7070953834, 0.2582301017 ),
	float2( -0.7356149235, 0.3036511448 ),
	float2( 0.3546150035, -0.7577916595 ),
	float2( 0.2620514635, 0.8354613678 ),
	float2( -0.7898261595, -0.4577204071 ),
	float2( 0.9265559948, -0.2037007328 ),
	float2( -0.5654627029, 0.8043125004 )
};
// Filter points: sqrt((k+.5)/32), angle k*2.39996323, k=0..31.
static const float2 g_ShadowFilterDisk[DX12_SHADOW_PCSS_FILTER_SAMPLES] =
{
	float2( 0.1250000000, 0.0000000000 ),
	float2( -0.1596450451, 0.1462479387 ),
	float2( 0.0244362331, -0.2784382706 ),
	float2( 0.2012222391, 0.2624587787 ),
	float2( -0.3692675569, -0.0653182318 ),
	float2( 0.3498024663, -0.2225156952 ),
	float2( -0.1170020798, 0.4352419021 ),
	float2( -0.2231356530, -0.4296341238 ),
	float2( 0.4841151148, 0.1767980646 ),
	float2( -0.5036411090, 0.2078957270 ),
	float2( 0.2427882958, -0.5188244823 ),
	float2( 0.1794143722, 0.5720012964 ),
	float2( -0.5407570051, -0.3133797400 ),
	float2( 0.6343695239, -0.1394643579 ),
	float2( -0.3871458473, 0.5506751247 ),
	float2( -0.0894396521, -0.6901996440 ),
	float2( 0.5490717551, 0.4627582606 ),
	float2( -0.7388784715, 0.0305549411 ),
	float2( 0.5389551278, -0.5363323318 ),
	float2( -0.0360581900, 0.7797915150 ),
	float2( -0.5128175286, -0.6145267955 ),
	float2( 0.8123595928, 0.1093018390 ),
	float2( -0.6883106434, 0.4789086116 ),
	float2( 0.1880860626, -0.8360613812 ),
	float2( 0.4350332607, 0.7591910577 ),
	float2( -0.8504484085, -0.2713162444 ),
	float2( 0.8261024038, -0.3816802568 ),
	float2( -0.3578882080, 0.8551555593 ),
	float2( -0.3194073275, -0.8880337601 ),
	float2( 0.8499086319, 0.4466881658 ),
	float2( -0.9440346483, 0.2488444954 ),
	float2( 0.5365958153, -0.8345297664 )
};
static const float2 g_ShadowPCFOffsets[4] =
{
	float2( -0.5, -0.5 ), float2( 0.5, -0.5 ),
	float2( -0.5, 0.5 ), float2( 0.5, 0.5 )
};

struct ShadowMapReceiver
{
	float3 positionWS;
	float3 vertexNormalWS;
	float3 planeNormalWS;
	float2 screenPos;
	float bakedSunVisibility;
	uint2 tile;
};
ShadowMapReceiver ShadowMap_BeginReceiver( float3 positionWS, float3 vertexNormalWS, float2 svPositionXY )
{
	ShadowMapReceiver r;
	r.positionWS = positionWS;
	r.vertexNormalWS = normalize( vertexNormalWS );
	// Derivatives occur exactly here, before ANY divergent per-light work.
	float3 dx = ddx( positionWS );
	float3 dy = ddy( positionWS );
	float3 plane = cross( dx, dy );
	float planeLengthSquared = dot( plane, plane );
	float derivativeScale = dot( dx, dx ) * dot( dy, dy );
	r.planeNormalWS = planeLengthSquared > max( derivativeScale * 1e-12, 1e-30 ) ? normalize( plane ) : r.vertexNormalWS;
	if ( dot( r.planeNormalWS, r.vertexNormalWS ) < 0.0 ) r.planeNormalWS = -r.planeNormalWS;
	r.screenPos = svPositionXY;
	r.bakedSunVisibility = 1.0;
	r.tile = uint2( max( ( svPositionXY - cShadowViewport.xy ) / DX12_SHADOW_TILE_PIXELS, 0.0 ) );
	return r;
}
float ShadowMap_BakedSunVisibility( float2 baseLightmapUV )
{
	uint width, height;
	g_ShadowSunVisibility.GetDimensions( width, height );
	// Match CPU float(UV) * float(pageDimension) before the center offset; no MAD contraction.
	precise float2 texel = baseLightmapUV * float2( width, height );
	precise float2 q = texel - 0.5;
	float2 base = floor( q );
	float2 fraction = q - base;
	int2 last = int2( width, height ) - 1;
	int2 p0 = clamp( int2( base ), int2( 0, 0 ), last );
	int2 p1 = clamp( int2( base ) + 1, int2( 0, 0 ), last );
	uint4 taps = uint4(
		g_ShadowSunVisibility.Load( int3( p0, 0 ) ),
		g_ShadowSunVisibility.Load( int3( p1.x, p0.y, 0 ) ),
		g_ShadowSunVisibility.Load( int3( p0.x, p1.y, 0 ) ),
		g_ShadowSunVisibility.Load( int3( p1, 0 ) ) );
	// q indexes texel centers; frac(q) >= .5 selects the containing physical cell.
	// Packed high 24 bits identify a rectangle, low 8 bits retain the disk R8 field.
	uint2 containing = uint2( fraction.x >= 0.5, fraction.y >= 0.5 );
	uint containingTap = taps[containing.x + 2 * containing.y];
	uint allocation = containingTap >> 8;
	uint4 rowTaps = containing.x != 0 ? taps.yyww : taps.xxzz;
	uint4 columnTaps = containing.y != 0 ? taps.zwzw : taps.xyxy;
	// Rectangular ownership makes these replacements exactly a texel-center clamp:
	// same row first, then same column, then the containing tap at a corner.
	uint4 filtered = ( taps >> 8 ) == allocation ? taps :
		( rowTaps >> 8 ) == allocation ? rowTaps :
		( columnTaps >> 8 ) == allocation ? columnTaps : containingTap;
	float4 visibility = float4( filtered & 255u );
	float result = lerp( lerp( visibility.x, visibility.y, fraction.x ),
		lerp( visibility.z, visibility.w, fraction.x ), fraction.y ) * ( 1.0 / 255.0 );
	return allocation != 0 ? result : 0.0; // Unproven cells are blocked, never white.
}
ShadowMapReceiver ShadowMap_BeginLightmappedReceiver( float3 positionWS, float3 vertexNormalWS, float2 svPositionXY, float2 baseLightmapUV )
{
	ShadowMapReceiver r = ShadowMap_BeginReceiver( positionWS, vertexNormalWS, svPositionXY );
	// An independent scalar at the BASE block: never decode RGB/alpha or sample a bumped offset.
	r.bakedSunVisibility = ShadowMap_BakedSunVisibility( baseLightmapUV );
	return r;
}
uint2 ShadowMap_TileRange( ShadowMapReceiver r )
{
	uint2 result = uint2( 0, 0 );
	if ( r.tile.x < cShadowView1.x && r.tile.y < cShadowView1.y ) result = g_ShadowTileRanges[r.tile.y * cShadowView1.x + r.tile.x];
	return result;
}
float2 ShadowMap_ClipUV( float4 clip )
{
	return clip.xy / clip.w * float2( 0.5, -0.5 ) + 0.5;
}
bool ShadowMap_InCore( float4 clip, uint size )
{
	bool result = false;
	if ( !( clip.w <= 0.0 || size <= 2 * DX12_SHADOW_GUARD_TEXELS ) )
	{
		float2 uv = ShadowMap_ClipUV( clip );
		float guard = float( DX12_SHADOW_GUARD_TEXELS ) / float( size );
		result = all( uv >= guard ) && all( uv <= 1.0 - guard ) && clip.z >= 0.0 && clip.z <= clip.w;
	}
	return result;
}
// Saturate BEFORE multiplying by an arbitrarily large finite authored emitter.
// The ratio is factored before this call (no radius * distance intermediate).
float ShadowMap_CappedRadius( float emitter, float ratio )
{
	float result = 1.0;
	if ( !( emitter <= 0.0 || ratio <= 0.0 ) )
	{
		if ( emitter >= float( DX12_SHADOW_PCSS_MAX_TEXELS ) / ratio ) result = float( DX12_SHADOW_PCSS_MAX_TEXELS );
		else result = max( 1.0, emitter * ratio );
	}
	return result;
}
float ShadowMap_PerspectiveDistance( float z, float n, float f )
{
	return n / ( 1.0 - z * ( 1.0 - n / f ) );
}

bool ShadowMap_SunProjectionValid( row_major float4x4 worldToClip, float4 depth, uint size, float3 p, float3 normal )
{
	bool result = false;
	if ( !( depth.y <= depth.x || depth.z <= 0.0 || !ShadowMap_InCore( mul( worldToClip, float4( p, 1.0 ) ), size ) ) )
	{
		float4 centerClip = mul( worldToClip, float4( p + normal * ( 0.5 * depth.z ), 1.0 ) );
		result = centerClip.w > 0.0 && centerClip.z >= 0.0 && centerClip.z <= centerClip.w;
	}
	return result;
}

struct ShadowMapSunChart
{
	row_major float4x4 worldToClip;
	uint4 rect;
	float4 depth;
	float2 atlasSize;
	float3 center;
	float3 normal;
	float centerAxial;
	uint mapIndex;
	bool valid;
};
ShadowMapSunChart ShadowMap_MakeSunChart( uint mapIndex, float3 p, float3 normal )
{
	ShadowMapSunChart c = (ShadowMapSunChart)0;
	c.mapIndex = mapIndex;
	if ( mapIndex < DX12_SHADOW_CSM_CASCADES )
	{
		c.worldToClip = cSunWorldToClip[mapIndex];
		c.rect = cCascadeRects[mapIndex];
		c.valid = ( cShadowView1.w & DX12_SHADOW_VIEW_CSM_VALID ) != 0;
	}
	else
	{
		c.worldToClip = cStaticSunWorldToClip;
		c.rect = cStaticSunRect;
		c.valid = ( cShadowView1.w & DX12_SHADOW_VIEW_STATIC_SUN_VALID ) != 0;
	}
	c.depth = cShadowDepthRecords[mapIndex];
	c.valid = c.valid && ShadowMap_SunProjectionValid( c.worldToClip, c.depth, c.rect.z, p, normal );
	c.normal = normal;
	c.center = p + normal * ( 0.5 * c.depth.z );
	float4 clip = mul( c.worldToClip, float4( c.center, 1.0 ) );
	c.centerAxial = c.depth.x;
	c.atlasSize = 1.0;
	if ( c.valid )
	{
		c.centerAxial += ( clip.z / clip.w ) * ( c.depth.y - c.depth.x );
		uint width = 0, height = 0;
		if ( mapIndex < DX12_SHADOW_CSM_CASCADES ) g_ShadowCascadeAtlas.GetDimensions( width, height );
		else g_ShadowStaticSun.GetDimensions( width, height );
		c.atlasSize = float2( width, height );
	}
	return c;
}
float3 ShadowMap_SunPoint( ShadowMapSunChart c, float2 offset, bool usePlane )
{
	// Texture v points down, whereas the world basis Y / clip Y points up.
	float3 lateral = ( offset.x * cSunBasisX.xyz - offset.y * cSunBasisY.xyz ) * c.depth.z;
	float3 q = c.center + lateral;
	if ( usePlane ) q -= cSunTravel.xyz * ( dot( c.normal, lateral ) / dot( c.normal, cSunTravel.xyz ) );
	return q;
}
bool ShadowMap_SunPreflight( ShadowMapSunChart c, float footprint )
{
	bool result = true;
	float denominator = dot( c.normal, cSunTravel.xyz );
	if ( denominator == 0.0 || abs( denominator ) < 1e-4 * length( cSunTravel.xyz ) ) result = false;
	if ( result )
	{
		[loop] for ( uint i = 0; i < 4; ++i )
		{
			float3 q = ShadowMap_SunPoint( c, g_ShadowPCFOffsets[i] * ( 2.0 * footprint ), true );
			float4 clip = mul( c.worldToClip, float4( q, 1.0 ) );
			if ( clip.w <= 0.0 ) { result = false; break; }
			float axial = c.depth.x + clip.z / clip.w * ( c.depth.y - c.depth.x );
			if ( axial <= 0.0 || axial < c.depth.x || axial > c.depth.y ) { result = false; break; }
		}
	}
	return result;
}
float ShadowMap_SunCompare( ShadowMapSunChart c, float2 offset, bool usePlane )
{
	float4 clip = mul( c.worldToClip, float4( ShadowMap_SunPoint( c, offset, usePlane ), 1.0 ) );
	float2 atlasUV = ( float2( c.rect.xy ) + ShadowMap_ClipUV( clip ) * c.rect.z ) / c.atlasSize;
	float reference = usePlane ? clip.z / clip.w : ( c.centerAxial - c.depth.x ) / ( c.depth.y - c.depth.x );
	float result = 1.0;
	if ( c.mapIndex < DX12_SHADOW_CSM_CASCADES ) result = g_ShadowCascadeAtlas.SampleCmpLevelZero( g_ShadowCmpSampler, atlasUV, reference );
	else result = g_ShadowStaticSun.SampleCmpLevelZero( g_ShadowCmpSampler, atlasUV, reference );
	return result;
}
float ShadowMap_SunPCFChart( ShadowMapSunChart c )
{
	float result = 1.0;
	if ( c.valid )
	{
		bool usePlane = ShadowMap_SunPreflight( c, 0.5 );
		float visibility = 0.0;
		[unroll] for ( uint i = 0; i < 4; ++i ) visibility += ShadowMap_SunCompare( c, g_ShadowPCFOffsets[i], usePlane );
		result = visibility * 0.25;
	}
	return result;
}
float ShadowMap_SunPCF( uint mapIndex, float3 positionWS, float3 planeNormalWS )
{
	return ShadowMap_SunPCFChart( ShadowMap_MakeSunChart( mapIndex, positionWS, planeNormalWS ) );
}
float SampleSunPCSS( uint mapIndex, float3 positionWS, float3 planeNormalWS, out float2 diagnostics )
{
	float result = 1.0;
	diagnostics = 0.0;
	ShadowMapSunChart c = ShadowMap_MakeSunChart( mapIndex, positionWS, planeNormalWS );
	// Fail before reading: never spend a blocker budget and then recurse to PCF.
	bool pcss = c.valid && cShadowView0.z == DX12_SHADOW_FILTER_PCSS && cSunRadiance.w > 0.0;
	if ( pcss ) pcss = ShadowMap_SunPreflight( c, DX12_SHADOW_GUARD_TEXELS );
	if ( pcss )
	{
		float searchRadius = ShadowMap_CappedRadius( cSunRadiance.w, max( 0.0, c.centerAxial - c.depth.x ) / c.depth.z );
		float blockerDistance = 0.0;
		uint blockerCount = 0;
		float2 centerUV = ShadowMap_ClipUV( mul( c.worldToClip, float4( c.center, 1.0 ) ) );
		[loop] for ( uint i = 0; i < DX12_SHADOW_PCSS_BLOCKER_SAMPLES; ++i )
		{
			float2 offset = g_ShadowBlockerDisk[i] * searchRadius;
			float4 clip = mul( c.worldToClip, float4( ShadowMap_SunPoint( c, offset, true ), 1.0 ) );
			int2 texel = clamp( int2( floor( ShadowMap_ClipUV( clip ) * c.rect.z ) ), int2( 0, 0 ), int2( c.rect.z - 1, c.rect.z - 1 ) );
			int3 address = int3( int2( c.rect.xy ) + texel, 0 );
			float stored = 1.0;
			if ( mapIndex < DX12_SHADOW_CSM_CASCADES ) stored = g_ShadowCascadeAtlas.Load( address );
			else stored = g_ShadowStaticSun.Load( address );
			// Reference is on the ACTUAL point-loaded texel center, not the Vogel ray.
			float2 loadedOffset = float2( texel ) + 0.5 - centerUV * c.rect.z;
			float4 loadedClip = mul( c.worldToClip, float4( ShadowMap_SunPoint( c, loadedOffset, true ), 1.0 ) );
			float reference = loadedClip.z / loadedClip.w;
			if ( stored < 1.0 && stored < reference )
			{
				blockerDistance += c.depth.x + stored * ( c.depth.y - c.depth.x );
				++blockerCount;
			}
		}
		diagnostics.x = float( blockerCount ) / DX12_SHADOW_PCSS_BLOCKER_SAMPLES;
		if ( blockerCount > 0 )
		{
			float meanBlocker = blockerDistance / float( blockerCount );
			float filterRadius = ShadowMap_CappedRadius( cSunRadiance.w, max( 0.0, c.centerAxial - meanBlocker ) / c.depth.z );
			diagnostics.y = filterRadius / DX12_SHADOW_PCSS_MAX_TEXELS;
			float visibility = 0.0;
			[loop] for ( uint j = 0; j < DX12_SHADOW_PCSS_FILTER_SAMPLES; ++j ) visibility += ShadowMap_SunCompare( c, g_ShadowFilterDisk[j] * filterRadius, true );
			result = visibility / DX12_SHADOW_PCSS_FILTER_SAMPLES;
		}
	}
	else if ( c.valid ) result = ShadowMap_SunPCFChart( c );
	return result;
}

uint ShadowMap_CubeFace( float3 v )
{
	uint result = v.z >= 0.0 ? 4 : 5;
	float3 a = abs( v );
	if ( a.x >= a.y && a.x >= a.z ) result = v.x >= 0.0 ? 0 : 1;
	else if ( a.y >= a.z ) result = v.y >= 0.0 ? 2 : 3;
	return result;
}
// Recover the physical basis from the exact CPU matrix, including narrow spots.
// Cube matrices encode +/-X,+/-Y,+/-Z and up +Z,+Z,+Z,+Z,-Y,+Y.
float3 ShadowMap_LocalRay( RuntimeShadowLightGpu light, uint face, float2 plane )
{
	float3 forward = normalize( light.worldToClip[face][3].xyz );
	float3 right = normalize( light.worldToClip[face][0].xyz );
	float3 up = normalize( light.worldToClip[face][1].xyz );
	return forward + right * plane.x + up * plane.y;
}
struct ShadowMapLocalChart
{
	float3 center;
	float3 normal;
	float2 homePlane;
	float radial;
	float planeNumerator;
	uint homeFace;
	bool valid;
};
ShadowMapLocalChart ShadowMap_MakeLocalChart( RuntimeShadowLightGpu light, float3 p, float3 normal )
{
	ShadowMapLocalChart c = (ShadowMapLocalChart)0;
	float3 relative = p - light.origin;
	float radial = length( relative );
	c.center = p;
	c.radial = radial;
	c.normal = normal;
	c.planeNumerator = dot( normal, relative );
	c.homeFace = 0;
	c.homePlane = 0.0;
	c.valid = light.faceCount > 0 && light.shadowFar > light.shadowNear && light.shadowNear > 0.0 && light.planeToTexel > 0.0 && radial > 0.0;
	if ( c.valid )
	{
		c.center = p + normal * ( 0.5 * radial / light.planeToTexel );
		relative = c.center - light.origin;
		c.radial = length( relative );
		c.normal = normal;
		c.planeNumerator = dot( normal, relative );
		c.homeFace = light.faceCount == DX12_SHADOW_MAX_FACES ? ShadowMap_CubeFace( relative ) : 0;
		float4 clip = mul( light.worldToClip[c.homeFace], float4( c.center, 1.0 ) );
		c.valid = clip.w > 0.0 && clip.z >= 0.0 && clip.z <= clip.w && c.radial > 0.0;
		if ( c.valid )
		{
			c.homePlane = clip.xy / clip.w * light.tanRenderedHalfFov;
			uint originalFace = light.faceCount == DX12_SHADOW_MAX_FACES ? ShadowMap_CubeFace( p - light.origin ) : 0;
			c.valid = c.valid && ShadowMap_InCore( mul( light.worldToClip[originalFace], float4( p, 1.0 ) ), light.faces[originalFace].w );
		}
	}
	return c;
}
float3 ShadowMap_LocalOffsetRay( RuntimeShadowLightGpu light, ShadowMapLocalChart c, float2 offset )
{
	return ShadowMap_LocalRay( light, c.homeFace, c.homePlane + offset * float2( 1.0, -1.0 ) / light.planeToTexel );
}
float3 ShadowMap_LocalPoint( RuntimeShadowLightGpu light, ShadowMapLocalChart c, float3 ray, bool usePlane )
{
	float distanceAlongRay = usePlane ? c.planeNumerator / dot( c.normal, ray ) : c.radial / length( ray );
	return light.origin + ray * distanceAlongRay;
}
bool ShadowMap_LocalPreflight( RuntimeShadowLightGpu light, ShadowMapLocalChart c, float footprint )
{
	bool result = true;
	float minDenominator = 3.402823466e+38;
	float maxRayLength = 0.0;
	float signDenominator = 0.0;
	[loop] for ( uint i = 0; i < 4; ++i )
	{
		float3 ray = ShadowMap_LocalOffsetRay( light, c, g_ShadowPCFOffsets[i] * ( 2.0 * footprint ) );
		float denominator = dot( c.normal, ray );
		if ( denominator == 0.0 ) { result = false; break; }
		if ( i == 0 ) signDenominator = denominator;
		if ( ( denominator > 0.0 ) != ( signDenominator > 0.0 ) ) { result = false; break; }
		minDenominator = min( minDenominator, abs( denominator ) );
		maxRayLength = max( maxRayLength, length( ray ) );
		float intersection = c.planeNumerator / denominator;
		if ( intersection <= 0.0 ) { result = false; break; }
		float3 relative = ray * intersection;
		// In home physical coordinates the forward component of ray is one.
		// Every neighboring cube face axial depth is an absolute XYZ component.
		if ( light.faceCount == DX12_SHADOW_MAX_FACES )
		{
			if ( intersection < light.shadowNear || any( abs( relative ) > light.shadowFar ) ) { result = false; break; }
		}
		else if ( intersection < light.shadowNear || intersection > light.shadowFar ) { result = false; break; }
	}
	if ( result ) result = minDenominator / maxRayLength >= 1e-4;
	return result;
}
float ShadowMap_LocalCompare( RuntimeShadowLightGpu light, ShadowMapLocalChart c, float2 offset, bool usePlane )
{
	float3 ray = ShadowMap_LocalOffsetRay( light, c, offset );
	uint face = light.faceCount == DX12_SHADOW_MAX_FACES ? ShadowMap_CubeFace( ray ) : 0;
	float3 q = ShadowMap_LocalPoint( light, c, ray, usePlane );
	float4 clip = mul( light.worldToClip[face], float4( q, 1.0 ) );
	uint4 rect = light.faces[face];
	float2 atlasUV = ( float2( rect.yz ) + ShadowMap_ClipUV( clip ) * rect.w ) / 4096.0;
	return g_ShadowLocalAtlas[NonUniformResourceIndex( rect.x )].SampleCmpLevelZero( g_ShadowCmpSampler, atlasUV, clip.z / clip.w );
}
float ShadowMap_LocalPCFChart( RuntimeShadowLightGpu light, ShadowMapLocalChart c )
{
	float result = 1.0;
	if ( c.valid )
	{
		bool usePlane = ShadowMap_LocalPreflight( light, c, 0.5 );
		float visibility = 0.0;
		[unroll] for ( uint i = 0; i < 4; ++i ) visibility += ShadowMap_LocalCompare( light, c, g_ShadowPCFOffsets[i], usePlane );
		result = visibility * 0.25;
	}
	return result;
}
float ShadowMap_LocalPCF( uint lightIndex, float3 positionWS, float3 planeNormalWS )
{
	RuntimeShadowLightGpu light = g_ShadowLights[lightIndex];
	return ShadowMap_LocalPCFChart( light, ShadowMap_MakeLocalChart( light, positionWS, planeNormalWS ) );
}
float ShadowMap_LocalPCSSChart( RuntimeShadowLightGpu light, ShadowMapLocalChart c, out float2 diagnostics )
{
	float result = 1.0;
	diagnostics = 0.0;
	bool pcss = c.valid && cShadowView0.z == DX12_SHADOW_FILTER_PCSS && light.shadowSourceRadius > 0.0;
	if ( pcss ) pcss = ShadowMap_LocalPreflight( light, c, DX12_SHADOW_GUARD_TEXELS );
	if ( pcss )
	{
		// R * (rhoR-n)/(rhoR*n*tau), factored into bounded separation ratio.
		float searchRatio = ( max( 0.0, c.radial - light.shadowNear ) / c.radial ) * ( light.planeToTexel / light.shadowNear );
		float searchRadius = ShadowMap_CappedRadius( light.shadowSourceRadius, searchRatio );
		float blockerDistance = 0.0;
		uint blockerCount = 0;
		[loop] for ( uint i = 0; i < DX12_SHADOW_PCSS_BLOCKER_SAMPLES; ++i )
		{
			float3 ray = ShadowMap_LocalOffsetRay( light, c, g_ShadowBlockerDisk[i] * searchRadius );
			uint face = light.faceCount == DX12_SHADOW_MAX_FACES ? ShadowMap_CubeFace( ray ) : 0;
			float3 q = ShadowMap_LocalPoint( light, c, ray, true );
			float4 clip = mul( light.worldToClip[face], float4( q, 1.0 ) );
			uint4 rect = light.faces[face];
			int2 texel = clamp( int2( floor( ShadowMap_ClipUV( clip ) * rect.w ) ), int2( 0, 0 ), int2( rect.w - 1, rect.w - 1 ) );
			float stored = g_ShadowLocalAtlas[NonUniformResourceIndex( rect.x )].Load( int3( int2( rect.yz ) + texel, 0 ) );
			float2 loadedPlane = ( ( float2( texel ) + 0.5 ) / rect.w * float2( 2.0, -2.0 ) + float2( -1.0, 1.0 ) ) * light.tanRenderedHalfFov;
			float3 loadedRay = ShadowMap_LocalRay( light, face, loadedPlane );
			float3 loadedPoint = ShadowMap_LocalPoint( light, c, loadedRay, true );
			float4 loadedClip = mul( light.worldToClip[face], float4( loadedPoint, 1.0 ) );
			if ( stored < 1.0 && stored < loadedClip.z / loadedClip.w )
			{
				// Face-local axial z must be linearized then made radial BEFORE summing.
				float axial = ShadowMap_PerspectiveDistance( stored, light.shadowNear, light.shadowFar );
				blockerDistance += axial * sqrt( 1.0 + dot( loadedPlane, loadedPlane ) );
				++blockerCount;
			}
		}
		diagnostics.x = float( blockerCount ) / DX12_SHADOW_PCSS_BLOCKER_SAMPLES;
		if ( blockerCount > 0 )
		{
			float meanBlocker = blockerDistance / float( blockerCount );
			float filterRatio = ( max( 0.0, c.radial - meanBlocker ) / c.radial ) * ( light.planeToTexel / max( meanBlocker, light.shadowNear ) );
			float filterRadius = ShadowMap_CappedRadius( light.shadowSourceRadius, filterRatio );
			diagnostics.y = filterRadius / DX12_SHADOW_PCSS_MAX_TEXELS;
			float visibility = 0.0;
			[loop] for ( uint j = 0; j < DX12_SHADOW_PCSS_FILTER_SAMPLES; ++j ) visibility += ShadowMap_LocalCompare( light, c, g_ShadowFilterDisk[j] * filterRadius, true );
			result = visibility / DX12_SHADOW_PCSS_FILTER_SAMPLES;
		}
	}
	else if ( c.valid ) result = ShadowMap_LocalPCFChart( light, c );
	return result;
}
float SampleLocalPCSS( uint lightIndex, float3 positionWS, float3 planeNormalWS, out float2 diagnostics )
{
	RuntimeShadowLightGpu light = g_ShadowLights[lightIndex];
	return ShadowMap_LocalPCSSChart( light, ShadowMap_MakeLocalChart( light, positionWS, planeNormalWS ), diagnostics );
}

// Select by camera-forward distance, NEVER Euclidean camera distance.
uint ShadowMap_Cascade( float distance )
{
	uint result = 3;
	if ( distance < cCascadeSplits.x ) result = 0;
	else if ( distance < cCascadeSplits.y ) result = 1;
	else if ( distance < cCascadeSplits.z ) result = 2;
	return result;
}
bool ShadowMap_SunMapValid( uint mapIndex, float3 p, float3 normal )
{
	bool result = false;
	if ( mapIndex < DX12_SHADOW_CSM_CASCADES ) result = ( cShadowView1.w & DX12_SHADOW_VIEW_CSM_VALID ) != 0 && ShadowMap_SunProjectionValid( cSunWorldToClip[mapIndex], cShadowDepthRecords[mapIndex], cCascadeRects[mapIndex].z, p, normal );
	else result = ( cShadowView1.w & DX12_SHADOW_VIEW_STATIC_SUN_VALID ) != 0 && ShadowMap_SunProjectionValid( cStaticSunWorldToClip, cShadowDepthRecords[4], cStaticSunRect.z, p, normal );
	return result;
}
// Complete results (visibility, blocker fraction, filter radius) share selection
// and blending. Invalid CSM never dilutes the static visibility with white.
float3 ShadowMap_SunResult( ShadowMapReceiver r )
{
	// Validity case table: F/S are first/second CSM validity, C is coverage,
	// B is blend, T is static (or float3(1,0,0) when static is invalid).
	// Outside CSM range / disabled: T.
	// F S | result
	// 0 0 | T
	// 1 0 | C * first + (1-C) * T (also second == first)
	// 0 1 | C * second + (1-C) * T
	// 1 1 | C * ( (1-B) * first + B * second ) + (1-C) * T
	// At C >= 1 static is not read. Entry weights always sum to one.
	uint mapIndices[3] = { 4, 4, 4 };
	float weights[3] = { 1.0, 0.0, 0.0 };
	uint entryCount = 1;
	float distance = dot( r.positionWS - cEyePosition.xyz, cViewForward.xyz );
	float farDistance = cSunTravel.w;
	if ( ( cShadowView1.w & DX12_SHADOW_VIEW_CSM_VALID ) != 0 && farDistance > cEyePosition.w && distance >= cEyePosition.w && distance < farDistance )
	{
		uint first = ShadowMap_Cascade( distance );
		uint second = first;
		float blend = 0.0;
		[loop] for ( uint i = 0; i < 3; ++i )
		{
			float split = cCascadeSplits[i];
			float width = cCascadeBlend[i];
			if ( width > 0.0 && distance >= split - width && distance <= split + width )
			{
				first = i;
				second = i + 1;
				blend = saturate( ( distance - ( split - width ) ) / ( 2.0 * width ) );
			}
		}
		bool firstValid = ShadowMap_SunMapValid( first, r.positionWS, r.planeNormalWS );
		bool secondValid = second != first && ShadowMap_SunMapValid( second, r.positionWS, r.planeNormalWS );
		if ( firstValid || secondValid )
		{
			float coverage = 1.0 - smoothstep( 0.9 * farDistance, farDistance, distance );
			entryCount = 0;
			if ( firstValid )
			{
				mapIndices[entryCount] = first;
				weights[entryCount++] = coverage * ( secondValid ? 1.0 - blend : 1.0 );
			}
			if ( secondValid )
			{
				mapIndices[entryCount] = second;
				weights[entryCount++] = coverage * ( firstValid ? blend : 1.0 );
			}
			if ( coverage < 1.0 )
			{
				mapIndices[entryCount] = 4;
				weights[entryCount++] = 1.0 - coverage;
			}
		}
	}
	float3 result = 0.0;
	// Runtime map selection keeps the heavy PCSS kernel instantiated just once.
	[loop] for ( uint entry = 0; entry < entryCount; ++entry )
	{
		float weight = weights[entry];
		if ( weight <= 0.0 ) continue;
		uint mapIndex = mapIndices[entry];
		float3 mapResult = float3( 1.0, 0.0, 0.0 );
		if ( ShadowMap_SunMapValid( mapIndex, r.positionWS, r.planeNormalWS ) )
		{
			float2 diagnostics;
			float visibility = SampleSunPCSS( mapIndex, r.positionWS, r.planeNormalWS, diagnostics );
			mapResult = float3( visibility, diagnostics );
		}
		result += weight * mapResult;
	}
	return result;
}

// Port of restir_lights.glsl StandardLightRadiance / GatherSampleStandardLightSSE.
float3 ShadowMap_StandardLightRadiance( RuntimeShadowLightGpu light, float3 positionWS, out float3 receiverToLight )
{
	float3 offset = light.origin - positionWS;
	float distanceSquared = dot( offset, offset );
	receiverToLight = 0.0;
	float3 result = 0.0;
	// Single exit: fxc /WX flags early returns ahead of the out-parameter write as X4000.
	float distance = sqrt( max( distanceSquared, 0.0 ) );
	bool hardFade = light.fadeEnd > light.fadeStart;
	bool lit = distanceSquared > 0.0 && !( light.attenuationRadius > 0.0 && distance > light.attenuationRadius ) && !( hardFade && distance > light.fadeEnd );
	if ( lit )
	{
		receiverToLight = offset / distance;
		float clampedDistance = max( distance, 1.0 );
		// Nonpositive capDist is the baker's uncapped sentinel.
		float evaluationDistance = light.capDist > 0.0 ? min( clampedDistance, light.capDist ) : clampedDistance;
		float denominator = light.constantAttn + evaluationDistance * light.linearAttn + evaluationDistance * evaluationDistance * light.quadraticAttn;
		float falloff = denominator > 0.0 ? 1.0 / denominator : 0.0;
		if ( light.type == DX12_SHADOW_LIGHT_SPOT )
		{
			float coneDot = -dot( receiverToLight, light.travelDirection );
			float cone = 1.0;
			if ( coneDot <= light.innerConeCos )
			{
				cone = saturate( ( coneDot - light.outerConeCos ) / ( light.innerConeCos - light.outerConeCos ) );
				if ( light.exponent != 0.0 && light.exponent != 1.0 ) cone = pow( cone, light.exponent );
			}
			falloff = coneDot <= light.outerConeCos ? 0.0 : falloff * coneDot * cone;
		}
		if ( hardFade )
		{
			float t = 1.0 - saturate( ( clampedDistance - light.fadeStart ) / ( light.fadeEnd - light.fadeStart ) );
			falloff *= t * t * t * ( t * ( t * 6.0 - 15.0 ) + 10.0 );
		}
		result = light.radiance * falloff;
	}
	return result;
}
// Modes: 0 Lambert, 1 squared half-Lambert, 2 three world-space bump bases,
// 3 Phong, 4 full-sphere detail gather (no receiver cosine).
float ShadowMap_Angular( uint mode, float3 L, float3 N, float3 basis1, float3 basis2, float3 weights, float3 E, float exponent )
{
	float result = 0.0;
	if ( mode == 4 ) result = 1.0;
	// Match PixelShaderDoSpecularLight: back-facing lights contribute no Phong.
	else if ( mode == 3 ) result = saturate( dot( N, L ) ) * pow( saturate( dot( reflect( -L, N ), E ) ), exponent );
	else if ( mode == 2 ) result = dot( weights, saturate( float3( dot( N, L ), dot( basis1, L ), dot( basis2, L ) ) ) );
	else
	{
		float ndotl = dot( N, L );
		if ( mode == 1 )
		{
			float halfLambert = saturate( ndotl * 0.5 + 0.5 );
			result = halfLambert * halfLambert;
		}
		else result = saturate( ndotl );
	}
	return result;
}
struct ShadowMapShading
{
	uint mode;
	float3 N;
	float3 basis1;
	float3 basis2;
	float3 weights;
	float3 E;
	float exponent;
	bool specular;
};
ShadowMapShading ShadowMap_ShadeLambert( float3 N )
{
	ShadowMapShading s;
	s.mode = 0;
	s.N = N;
	s.basis1 = 0.0;
	s.basis2 = 0.0;
	s.weights = 0.0;
	s.E = 0.0;
	s.exponent = 1.0;
	s.specular = false;
	return s;
}
ShadowMapShading ShadowMap_ShadeHalfLambert( float3 N, bool halfLambert )
{
	ShadowMapShading s = ShadowMap_ShadeLambert( N );
	s.mode = halfLambert ? 1 : 0;
	return s;
}
ShadowMapShading ShadowMap_ShadeBumped( float3 basisWS0, float3 basisWS1, float3 basisWS2, float3 weights )
{
	ShadowMapShading s = ShadowMap_ShadeLambert( basisWS0 );
	s.mode = 2;
	s.basis1 = basisWS1;
	s.basis2 = basisWS2;
	s.weights = weights;
	return s;
}
ShadowMapShading ShadowMap_WithSpecular( ShadowMapShading s, float3 eyeDirWS, float specExponent )
{
	s.specular = true;
	s.E = eyeDirWS;
	s.exponent = specExponent;
	return s;
}
struct ShadowMapDirect
{
	float3 diffuse;
	float3 specular;
	float sunVisibility;
	uint lowestLocal;
	float2 sunDiagnostics;
	float2 localDiagnostics;
};
ShadowMapDirect ShadowMap_NoDirect()
{
	ShadowMapDirect d;
	d.diffuse = 0.0;
	d.specular = 0.0;
	d.sunVisibility = 1.0;
	d.lowestLocal = 0xFFFFFFFF;
	d.sunDiagnostics = 0.0;
	d.localDiagnostics = 0.0;
	return d;
}
ShadowMapDirect ShadowMap_GatherDirect( ShadowMapReceiver r, ShadowMapShading s )
{
	ShadowMapDirect d = ShadowMap_NoDirect();
	if ( ( cShadowView1.w & DX12_SHADOW_VIEW_HAS_SUN ) != 0 )
	{
		// Geometric surface backfaces skip the map (silhouette taps can read "lit").
		// Detail gathers are full-sphere point receivers: their orientation is NOT a sun cosine gate.
		float3 sunResult = ( s.mode == 4 || dot( r.planeNormalWS, -cSunTravel.xyz ) > 0.0 ) ? ShadowMap_SunResult( r ) : float3( 0.0, 0.0, 0.0 );
		d.sunVisibility = min( saturate( sunResult.x ), r.bakedSunVisibility );
		d.sunDiagnostics = sunResult.yz;
		float3 L = -cSunTravel.xyz;
		float angular = ShadowMap_Angular( s.mode, L, s.N, s.basis1, s.basis2, s.weights, s.E, s.exponent );
		if ( angular > 0.0 ) d.diffuse += cSunRadiance.rgb * angular * d.sunVisibility;
		if ( s.specular )
		{
			float specularAngular = ShadowMap_Angular( 3, L, s.N, 0.0, 0.0, 0.0, s.E, s.exponent );
			if ( specularAngular > 0.0 ) d.specular += cSunRadiance.rgb * specularAngular * d.sunVisibility;
		}
	}
	uint2 range = ShadowMap_TileRange( r );
	uint lowestId = 0xFFFFFFFF;
	[loop] for ( uint i = 0; i < range.y; ++i )
	{
		uint index = g_ShadowTileIndices[range.x + i];
		RuntimeShadowLightGpu light = g_ShadowLights[index];
		bool lowest = light.lightId < lowestId;
		if ( lowest )
		{
			lowestId = light.lightId;
			d.lowestLocal = index;
			d.localDiagnostics = 0.0;
		}
		float3 L;
		float3 radiance = ShadowMap_StandardLightRadiance( light, r.positionWS, L );
		float angular = ShadowMap_Angular( s.mode, L, s.N, s.basis1, s.basis2, s.weights, s.E, s.exponent );
		float specularAngular = 0.0;
		if ( s.specular ) specularAngular = ShadowMap_Angular( 3, L, s.N, 0.0, 0.0, 0.0, s.E, s.exponent );
		if ( ( angular > 0.0 || specularAngular > 0.0 ) && any( radiance != 0.0 ) )
		{
			float2 diagnostics;
			float visibility = ShadowMap_LocalPCSSChart( light, ShadowMap_MakeLocalChart( light, r.positionWS, r.planeNormalWS ), diagnostics );
			if ( angular > 0.0 ) d.diffuse += radiance * angular * visibility;
			if ( specularAngular > 0.0 ) d.specular += radiance * specularAngular * visibility;
			if ( lowest ) d.localDiagnostics = diagnostics;
		}
	}
	return d;
}
float3 ShadowMap_DetailDirect( float3 lightingOriginWS, float3 orientationNormalWS, float2 svPositionXY )
{
	ShadowMapReceiver r = ShadowMap_BeginReceiver( lightingOriginWS, orientationNormalWS, svPositionXY );
	ShadowMapShading s = ShadowMap_ShadeLambert( orientationNormalWS );
	s.mode = 4;
	ShadowMapDirect d = ShadowMap_GatherDirect( r, s );
	return d.diffuse;
}
float3 ShadowMap_DebugCascadeColor( uint cascade )
{
	float3 result = float3( 0.5, 0.5, 0.5 );
	if ( cascade == 0 ) result = float3( 1.0, 0.2, 0.2 );
	else if ( cascade == 1 ) result = float3( 0.2, 1.0, 0.2 );
	else if ( cascade == 2 ) result = float3( 0.2, 0.4, 1.0 );
	else if ( cascade == 3 ) result = float3( 1.0, 1.0, 0.2 );
	return result;
}
float4 ShadowMap_Debug( ShadowMapReceiver r, ShadowMapDirect d, float4 lit )
{
	uint mode = cShadowView0.w;
	float3 color = lit.rgb;
	bool hasSun = ( cShadowView1.w & DX12_SHADOW_VIEW_HAS_SUN ) != 0;
	if ( mode == 1 )
	{
		float distance = dot( r.positionWS - cEyePosition.xyz, cViewForward.xyz );
		uint cascade = ShadowMap_Cascade( distance );
		if ( !hasSun || distance < cEyePosition.w || distance >= cSunTravel.w || !ShadowMap_SunMapValid( cascade, r.positionWS, r.planeNormalWS ) ) cascade = 4;
		color = ShadowMap_DebugCascadeColor( cascade );
	}
	else if ( mode == 2 ) color = d.sunVisibility.xxx;
	else if ( mode == 3 )
	{
		uint index = d.lowestLocal;
		color = 0.0;
		if ( index != 0xFFFFFFFF )
		{
			RuntimeShadowLightGpu light = g_ShadowLights[index];
			uint face = light.faceCount == DX12_SHADOW_MAX_FACES ? ShadowMap_CubeFace( r.positionWS - light.origin ) : 0;
			uint page = light.faces[face].x;
			color = float3( float( page & 31 ) / 31.0, float( ( page >> 5 ) & 31 ) / 31.0, float( face + 1 ) / 6.0 );
		}
	}
	else if ( mode == 5 || mode == 6 )
	{
		// Diagnostics are cached by GatherDirect. Without sun, a dark/backfacing
		// lowest-id local receiver shows zero: the lighting gate skips its samples.
		float2 diagnostics = hasSun ? d.sunDiagnostics : d.localDiagnostics;
		float value = mode == 5 ? diagnostics.x : diagnostics.y;
		color = value.xxx;
	}
	return float4( color, lit.a );
}
#endif
