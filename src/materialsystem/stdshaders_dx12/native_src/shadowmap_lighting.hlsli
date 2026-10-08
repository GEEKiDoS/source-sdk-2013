//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef DX12_SHADOWMAP_LIGHTING_HLSLI
#define DX12_SHADOWMAP_LIGHTING_HLSLI
// Included after dx12_engine_cbuffers.h, only in lighting ABI 6 pixel shaders.
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
// Immutable map-level visibility, independent of RGB pages and model lightmaps.
StructuredBuffer<uint4> g_ShadowVisibilityFaces : register(t1030, space2);
StructuredBuffer<uint4> g_ShadowVisibilityEntries : register(t1031, space2);
ByteAddressBuffer g_ShadowVisibilityPayload : register(t1032, space2);
StructuredBuffer<uint4> g_ShadowVisibilityPropMeshes : register(t1033, space2);
StructuredBuffer<DX12StaticPropTriangleGpu> g_ShadowPropTriangles : register(t1034, space2);
SamplerComparisonState g_ShadowCmpSampler : register(s0, space2);
// Packet-only policy, deliberately independent of material/snapshot variants.
static const uint DX12_SHADOW_VIEW_UNSHADOWED = 0x8;

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
	uint bakedFaceId; // 0xFFFFFFFF = no baked domain; directory lookup is lazy.
	float2 bakedQ;
	uint bakedLocalDirect; // Valid world highres owner with baked local-direct RGB.
	uint2 unbakedLocalRange; // Sorted canonical selected-local indices in raw t1032.
	uint bakedPropPrimitive; // 0xFFFFFFFF = dynamic/moved/unknown; topology lookup is lazy.
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
	r.bakedFaceId = 0xFFFFFFFF;
	r.bakedQ = 0.0;
	r.bakedLocalDirect = 0;
	r.unbakedLocalRange = uint2( 0, 0 );
	r.bakedPropPrimitive = 0xFFFFFFFF;
	return r;
}
void ShadowMap_SetBakedFace( inout ShadowMapReceiver r, uint faceId, float2 q, uint eligible, uint bakedDirectEligible, uint2 unbakedLocalRange )
{
	// Only carry the native owner/pose join. No visibility SRV is touched until
	// a light survives the radiance bound and actually needs the baked endpoint.
	r.bakedFaceId = eligible != 0 ? faceId : 0xFFFFFFFF;
	r.bakedQ = q;
	r.bakedLocalDirect = eligible != 0 && bakedDirectEligible != 0 ? 1 : 0;
	r.unbakedLocalRange = r.bakedLocalDirect != 0 ? unbakedLocalRange : uint2( 0, 0 );
}
void ShadowMap_SetBakedProp( inout ShadowMapReceiver r, uint primitiveId )
{
	// Exact identity/pose is backend-validated. A zero count disables the domain
	// for every dynamic, moved or unmatched draw without any resource reads.
	r.bakedPropPrimitive = primitiveId < cPropDraw.w ? primitiveId : 0xFFFFFFFF;
}
float3 ShadowMap_PropPositionWS( float4 localPosition )
{
	float4 p = float4( localPosition.xyz, 1.0 );
	return float3( dot( cPropModelToWorld[0], p ), dot( cPropModelToWorld[1], p ), dot( cPropModelToWorld[2], p ) );
}
float ShadowMap_VisibilityByte( uint byteOffset )
{
	uint packed = g_ShadowVisibilityPayload.Load( byteOffset & ~3u );
	return float( ( packed >> ( ( byteOffset & 3u ) * 8u ) ) & 255u ) * ( 1.0 / 255.0 );
}
struct ShadowMapBakedLookup
{
	uint cursor, end;
	uint4 entry; // Keep the current entry resident across ascending light queries.
	uint4 samples;
	float2 fraction;
	float3 barycentrics;
	uint valid, entryLoaded, propFlags;
};
bool ShadowMap_BeginBakedLookup( ShadowMapReceiver r, inout ShadowMapBakedLookup lookup )
{
	[branch] if ( lookup.end == 0xFFFFFFFF )
	{
		// Initialize every cached field on every domain path, including rejection.
		// Direct RGB and R8 lookup share interpolation without advancing the cursor.
		lookup = (ShadowMapBakedLookup)0;
		[branch] if ( r.bakedPropPrimitive != 0xFFFFFFFF )
		{
			DX12StaticPropTriangleGpu propTriangle = g_ShadowPropTriangles[cPropDraw.z + r.bakedPropPrimitive];
			uint4 mesh = g_ShadowVisibilityPropMeshes[propTriangle.vertexIndices.w];
			// Preserve the validated directory join and bound every dense R8 tap.
			[branch] if ( all( mesh.xy == cPropDraw.xy ) && all( propTriangle.vertexIndices.xyz < mesh.zzz ) )
			{
				float3 p0 = ShadowMap_PropPositionWS( propTriangle.positions[0] );
				float3 e0 = ShadowMap_PropPositionWS( propTriangle.positions[1] ) - p0;
				float3 e1 = ShadowMap_PropPositionWS( propTriangle.positions[2] ) - p0;
				float3 normal = cross( e0, e1 );
				float denominator = dot( normal, normal );
				// Degenerate primitives never borrow another primitive's vertex IDs.
				[branch] if ( denominator > max( dot( e0, e0 ) * dot( e1, e1 ) * 1e-12, 1e-30 ) )
				{
					float3 offset = r.positionWS - p0;
					float v = dot( cross( offset, e1 ), normal ) / denominator;
					float w = dot( cross( e0, offset ), normal ) / denominator;
					lookup.barycentrics = saturate( float3( 1.0 - v - w, v, w ) );
					lookup.barycentrics /= dot( lookup.barycentrics, float3( 1.0, 1.0, 1.0 ) );
					lookup.cursor = cPropDraw.x;
					lookup.end = cPropDraw.x + cPropDraw.y;
					lookup.samples = propTriangle.vertexIndices;
					lookup.propFlags = mesh.w;
					lookup.valid = 1;
				}
			}
		}
		else if ( r.bakedFaceId != 0xFFFFFFFF )
		{
			uint4 face = g_ShadowVisibilityFaces[r.bakedFaceId];
			lookup.cursor = face.x;
			lookup.end = face.x + face.y;
			uint2 last = face.zw - 1u;
			float2 pixel = saturate( r.bakedQ ) * float2( last );
			uint2 p0 = uint2( floor( pixel ) );
			uint2 p1 = min( p0 + 1u, last );
			lookup.fraction = pixel - float2( p0 );
			uint2 rows = uint2( p0.y, p1.y ) * face.z;
			lookup.samples = uint4( rows.x + p0.x, rows.x + p1.x, rows.y + p0.x, rows.y + p1.x );
			lookup.valid = 1;
		}
	}
	return lookup.valid != 0;
}
float ShadowMap_BakedLocalVisibility( ShadowMapReceiver r, uint selectedLightIndex, inout ShadowMapBakedLookup lookup )
{
	float visibility = 1.0;
	bool valid = ShadowMap_BeginBakedLookup( r, lookup );
	[branch] if ( valid )
	{
		[branch] if ( lookup.entryLoaded == 0 && lookup.cursor < lookup.end )
		{
			lookup.entry = g_ShadowVisibilityEntries[lookup.cursor];
			lookup.entryLoaded = 1;
		}
		// Client packets preserve manifest order; tile CSR and the resident tail are ascending.
		// Skipped lights need not query: each receiver entry is loaded at
		// most once as the cursor merges the two ordered streams.
		[loop] while ( lookup.cursor < lookup.end && lookup.entry.x < selectedLightIndex )
		{
			++lookup.cursor;
			if ( lookup.cursor < lookup.end ) lookup.entry = g_ShadowVisibilityEntries[lookup.cursor];
		}
		[branch] if ( lookup.cursor < lookup.end && lookup.entry.x == selectedLightIndex )
		{
			[branch] if ( lookup.entry.y <= 255u ) visibility = float( lookup.entry.y ) * ( 1.0 / 255.0 );
			else if ( r.bakedPropPrimitive != 0xFFFFFFFF )
			{
				float3 taps = float3(
					ShadowMap_VisibilityByte( lookup.entry.z + lookup.samples.x ),
					ShadowMap_VisibilityByte( lookup.entry.z + lookup.samples.y ),
					ShadowMap_VisibilityByte( lookup.entry.z + lookup.samples.z ) );
				visibility = dot( taps, lookup.barycentrics );
			}
			else
			{
				float4 taps = float4(
					ShadowMap_VisibilityByte( lookup.entry.z + lookup.samples.x ),
					ShadowMap_VisibilityByte( lookup.entry.z + lookup.samples.y ),
					ShadowMap_VisibilityByte( lookup.entry.z + lookup.samples.z ),
					ShadowMap_VisibilityByte( lookup.entry.z + lookup.samples.w ) );
				visibility = lerp( lerp( taps.x, taps.y, lookup.fraction.x ), lerp( taps.z, taps.w, lookup.fraction.x ), lookup.fraction.y );
			}
		}
	}
	return visibility;
}
float3 ShadowMap_PropDirectVertex( uint byteOffset )
{
	uint2 packed = g_ShadowVisibilityPayload.Load2( byteOffset );
	// Baker stores linear irradiance already /255 in RGBA16F. Alpha is padding.
	return float3( f16tof32( packed.x & 65535u ), f16tof32( packed.x >> 16u ), f16tof32( packed.y & 65535u ) );
}
float3 ShadowMap_BakedPropDirect( uint angularMode, ShadowMapBakedLookup lookup )
{
	float3 result = 0.0;
	uint planeOffset = angularMode == 1 ? cPropDirect.y : 0;
	[unroll] for ( uint style = 0; style < 4; ++style )
	{
		[branch] if ( cPropStyles[style] != 0.0 )
		{
			uint base = cPropDirect.x + style * ( 2u * cPropDirect.y ) + planeOffset;
			float3 rgb = ShadowMap_PropDirectVertex( base + lookup.samples.x * 8u ) * lookup.barycentrics.x +
				ShadowMap_PropDirectVertex( base + lookup.samples.y * 8u ) * lookup.barycentrics.y +
				ShadowMap_PropDirectVertex( base + lookup.samples.z * 8u ) * lookup.barycentrics.z;
			result += rgb * cPropStyles[style];
		}
	}
	return result;
}
uint ShadowMap_UnbakedViewIndex( uint selectedLightIndex )
{
	// Views are backend-validated in canonical bakedLightIndex order.
	// This search is reached only on a receiver with a nonempty overflow list.
	uint first = 0, end = cShadowView1.z;
	[loop] while ( first < end )
	{
		uint middle = first + ( end - first ) / 2u;
		if ( g_ShadowLights[middle].bakedLightIndex < selectedLightIndex ) first = middle + 1u;
		else end = middle;
	}
	uint result = 0xFFFFFFFF; // Absent from the conservative per-view selection: irrelevant.
	[branch] if ( first < cShadowView1.z )
	{
		if ( g_ShadowLights[first].bakedLightIndex == selectedLightIndex ) result = first;
	}
	return result;
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
bool ShadowMap_SunSlotInitialized( uint mapIndex )
{
	bool result = false;
	if ( ( cShadowView1.w & DX12_SHADOW_VIEW_UNSHADOWED ) == 0 )
	{
		if ( mapIndex < DX12_SHADOW_CSM_CASCADES )
			result = ( cShadowView1.w & DX12_SHADOW_VIEW_CSM_VALID ) != 0 && cCascadeRects[mapIndex].z == 2048;
		else if ( mapIndex == 4 )
			result = ( cShadowView1.w & DX12_SHADOW_VIEW_STATIC_SUN_VALID ) != 0 && cStaticSunRect.z == 4096;
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
	float planeDenominator;
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
		c.valid = ShadowMap_SunSlotInitialized( mapIndex );
	}
	else
	{
		c.worldToClip = cStaticSunWorldToClip;
		c.rect = cStaticSunRect;
		c.valid = ShadowMap_SunSlotInitialized( mapIndex );
	}
	c.depth = cShadowDepthRecords[mapIndex];
	c.valid = c.valid && ShadowMap_SunProjectionValid( c.worldToClip, c.depth, c.rect.z, p, normal );
	c.normal = normal;
	c.planeDenominator = dot( c.normal, cSunTravel.xyz );
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
	if ( usePlane ) q -= cSunTravel.xyz * ( dot( c.normal, lateral ) / c.planeDenominator );
	return q;
}
bool ShadowMap_SunPreflight( ShadowMapSunChart c, float footprint )
{
	bool result = true;
	float denominator = c.planeDenominator;
	if ( denominator == 0.0 || abs( denominator ) < 1e-4 * length( cSunTravel.xyz ) ) result = false;
	if ( result )
	{
		[unroll] for ( uint i = 0; i < 4; ++i )
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
	float result = 1.0;
	if ( c.valid )
	{
		float4 clip = mul( c.worldToClip, float4( ShadowMap_SunPoint( c, offset, usePlane ), 1.0 ) );
		float2 atlasUV = ( float2( c.rect.xy ) + ShadowMap_ClipUV( clip ) * c.rect.z ) / c.atlasSize;
		float reference = usePlane ? clip.z / clip.w : ( c.centerAxial - c.depth.x ) / ( c.depth.y - c.depth.x );
		if ( c.mapIndex < DX12_SHADOW_CSM_CASCADES ) result = g_ShadowCascadeAtlas.SampleCmpLevelZero( g_ShadowCmpSampler, atlasUV, reference );
		else result = g_ShadowStaticSun.SampleCmpLevelZero( g_ShadowCmpSampler, atlasUV, reference );
	}
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
		float depthRange = c.depth.y - c.depth.x;
		// Four blocker taps / two filter taps expose independent fetches without
		// full-unroll register pressure or oversized Source VCS blocks. Keep order.
		[loop] for ( uint base = 0; base < DX12_SHADOW_PCSS_BLOCKER_SAMPLES; base += 4 )
		{
			[unroll] for ( uint tap = 0; tap < 4; ++tap )
			{
				uint i = base + tap;
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
					blockerDistance += c.depth.x + stored * depthRange;
					++blockerCount;
				}
			}
		}
		diagnostics.x = float( blockerCount ) / DX12_SHADOW_PCSS_BLOCKER_SAMPLES;
		if ( blockerCount > 0 )
		{
			float meanBlocker = blockerDistance / float( blockerCount );
			float filterRadius = ShadowMap_CappedRadius( cSunRadiance.w, max( 0.0, c.centerAxial - meanBlocker ) / c.depth.z );
			diagnostics.y = filterRadius / DX12_SHADOW_PCSS_MAX_TEXELS;
			float visibility = 0.0;
			[loop] for ( uint base = 0; base < DX12_SHADOW_PCSS_FILTER_SAMPLES; base += 2 )
			{
				[unroll] for ( uint tap = 0; tap < 2; ++tap )
					visibility += ShadowMap_SunCompare( c, g_ShadowFilterDisk[base + tap] * filterRadius, true );
			}
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
bool ShadowMap_LocalFaceInitialized( RuntimeShadowLightGpu light, uint face, uint4 rect )
{
	return ( cShadowView1.w & DX12_SHADOW_VIEW_UNSHADOWED ) == 0 && face < light.faceCount &&
		rect.w == 512 && rect.x < DX12_SHADOW_MAX_LOCAL_PAGES;
}
// Index the structured buffer before loading the matrix. Indexing an array in a
// copied light makes SM5.1 select between all six matrices inside every PCSS tap.
float4x4 ShadowMap_LocalMatrix( uint lightIndex, uint face )
{
	return g_ShadowLights[lightIndex].worldToClip[face];
}
// Recover the physical basis from the exact CPU matrix, including narrow spots.
// Cube matrices encode +/-X,+/-Y,+/-Z and up +Z,+Z,+Z,+Z,-Y,+Y.
float3 ShadowMap_LocalRay( float4x4 worldToClip, float2 plane )
{
	float3 forward = normalize( worldToClip[3].xyz );
	float3 right = normalize( worldToClip[0].xyz );
	float3 up = normalize( worldToClip[1].xyz );
	return forward + right * plane.x + up * plane.y;
}
struct ShadowMapLocalChart
{
	float3 center;
	float3 normal;
	float3 forward;
	float3 right;
	float3 up;
	float2 homePlane;
	float radial;
	float planeNumerator;
	uint homeFace;
	uint lightIndex;
	float4x4 homeMatrix;
	uint4 homeRect;
	bool valid;
};
ShadowMapLocalChart ShadowMap_MakeLocalChart( uint lightIndex, RuntimeShadowLightGpu light, float3 p, float3 normal )
{
	ShadowMapLocalChart c = (ShadowMapLocalChart)0;
	c.lightIndex = lightIndex;
	float3 relative = p - light.origin;
	float radial = length( relative );
	c.center = p;
	c.radial = radial;
	c.normal = normal;
	c.planeNumerator = dot( normal, relative );
	c.homeFace = 0;
	c.homePlane = 0.0;
	// Admission publishes cubes atomically (all six initialized or all zero).
	c.valid = ShadowMap_LocalFaceInitialized( light, 0, g_ShadowLights[lightIndex].faces[0] ) && light.shadowFar > light.shadowNear && light.shadowNear > 0.0 && light.planeToTexel > 0.0 && radial > 0.0;
	if ( c.valid )
	{
		c.center = p + normal * ( 0.5 * radial / light.planeToTexel );
		relative = c.center - light.origin;
		c.radial = length( relative );
		c.normal = normal;
		c.planeNumerator = dot( normal, relative );
		c.homeFace = light.faceCount == DX12_SHADOW_MAX_FACES ? ShadowMap_CubeFace( relative ) : 0;
		float4x4 homeMatrix = ShadowMap_LocalMatrix( lightIndex, c.homeFace );
		c.homeMatrix = homeMatrix;
		c.homeRect = g_ShadowLights[lightIndex].faces[c.homeFace];
		float4 clip = mul( homeMatrix, float4( c.center, 1.0 ) );
		c.valid = clip.w > 0.0 && clip.z >= 0.0 && clip.z <= clip.w && c.radial > 0.0;
		if ( c.valid )
		{
			c.homePlane = clip.xy / clip.w * light.tanRenderedHalfFov;
			c.forward = normalize( homeMatrix[3].xyz );
			c.right = normalize( homeMatrix[0].xyz );
			c.up = normalize( homeMatrix[1].xyz );
			uint originalFace = light.faceCount == DX12_SHADOW_MAX_FACES ? ShadowMap_CubeFace( p - light.origin ) : 0;
			c.valid = c.valid && ShadowMap_InCore( mul( ShadowMap_LocalMatrix( lightIndex, originalFace ), float4( p, 1.0 ) ), g_ShadowLights[lightIndex].faces[originalFace].w );
		}
	}
	return c;
}
// Almost every tap stays on the home face. Retain its exact matrix/rectangle
// across the loops instead of reloading five structured-buffer vectors per tap.
// Seam crossings still select the neighbouring face's own camera and rectangle.
bool ShadowMap_LocalTapFace( RuntimeShadowLightGpu light, ShadowMapLocalChart c, uint face, out float4x4 faceMatrix, out uint4 rect )
{
	faceMatrix = c.homeMatrix;
	rect = c.homeRect;
	[branch] if ( face != c.homeFace )
	{
		faceMatrix = ShadowMap_LocalMatrix( c.lightIndex, face );
		rect = g_ShadowLights[c.lightIndex].faces[face];
	}
	return ShadowMap_LocalFaceInitialized( light, face, rect );
}
float3 ShadowMap_LocalOffsetRay( RuntimeShadowLightGpu light, ShadowMapLocalChart c, float2 offset )
{
	float2 plane = c.homePlane + offset * float2( 1.0, -1.0 ) / light.planeToTexel;
	return c.forward + c.right * plane.x + c.up * plane.y;
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
	[unroll] for ( uint i = 0; i < 4; ++i )
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
	float result = 1.0;
	uint4 rect;
	float4x4 faceMatrix;
	if ( c.valid && ShadowMap_LocalTapFace( light, c, face, faceMatrix, rect ) )
	{
		float3 q = ShadowMap_LocalPoint( light, c, ray, usePlane );
		float4 clip = mul( faceMatrix, float4( q, 1.0 ) );
		float2 atlasUV = ( float2( rect.yz ) + ShadowMap_ClipUV( clip ) * rect.w ) / 4096.0;
		result = g_ShadowLocalAtlas[NonUniformResourceIndex( rect.x )].SampleCmpLevelZero( g_ShadowCmpSampler, atlasUV, clip.z / clip.w );
	}
	return result;
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
	return ShadowMap_LocalPCFChart( light, ShadowMap_MakeLocalChart( lightIndex, light, positionWS, planeNormalWS ) );
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
		// Preserve exact tap/accumulation order, batching only loop control.
		[loop] for ( uint base = 0; base < DX12_SHADOW_PCSS_BLOCKER_SAMPLES; base += 4 )
		{
			[unroll] for ( uint tap = 0; tap < 4; ++tap )
			{
				uint i = base + tap;
				float3 ray = ShadowMap_LocalOffsetRay( light, c, g_ShadowBlockerDisk[i] * searchRadius );
				uint face = light.faceCount == DX12_SHADOW_MAX_FACES ? ShadowMap_CubeFace( ray ) : 0;
				uint4 rect;
				float4x4 faceMatrix;
				if ( !ShadowMap_LocalTapFace( light, c, face, faceMatrix, rect ) ) continue;
				float3 q = ShadowMap_LocalPoint( light, c, ray, true );
				float4 clip = mul( faceMatrix, float4( q, 1.0 ) );
				int2 texel = clamp( int2( floor( ShadowMap_ClipUV( clip ) * rect.w ) ), int2( 0, 0 ), int2( rect.w - 1, rect.w - 1 ) );
				float stored = g_ShadowLocalAtlas[NonUniformResourceIndex( rect.x )].Load( int3( int2( rect.yz ) + texel, 0 ) );
				float2 loadedPlane = ( ( float2( texel ) + 0.5 ) / rect.w * float2( 2.0, -2.0 ) + float2( -1.0, 1.0 ) ) * light.tanRenderedHalfFov;
				float3 loadedRay;
				[branch] if ( face == c.homeFace ) loadedRay = c.forward + c.right * loadedPlane.x + c.up * loadedPlane.y;
				else loadedRay = ShadowMap_LocalRay( faceMatrix, loadedPlane );
				float3 loadedPoint = ShadowMap_LocalPoint( light, c, loadedRay, true );
				float4 loadedClip = mul( faceMatrix, float4( loadedPoint, 1.0 ) );
				if ( stored < 1.0 && stored < loadedClip.z / loadedClip.w )
				{
					// Face-local axial z must be linearized then made radial BEFORE summing.
					float axial = ShadowMap_PerspectiveDistance( stored, light.shadowNear, light.shadowFar );
					blockerDistance += axial * sqrt( 1.0 + dot( loadedPlane, loadedPlane ) );
					++blockerCount;
				}
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
			[loop] for ( uint base = 0; base < DX12_SHADOW_PCSS_FILTER_SAMPLES; base += 2 )
			{
				[unroll] for ( uint tap = 0; tap < 2; ++tap )
					visibility += ShadowMap_LocalCompare( light, c, g_ShadowFilterDisk[base + tap] * filterRadius, true );
			}
			result = visibility / DX12_SHADOW_PCSS_FILTER_SAMPLES;
		}
	}
	else if ( c.valid ) result = ShadowMap_LocalPCFChart( light, c );
	return result;
}
float SampleLocalPCSS( uint lightIndex, float3 positionWS, float3 planeNormalWS, out float2 diagnostics )
{
	RuntimeShadowLightGpu light = g_ShadowLights[lightIndex];
	return ShadowMap_LocalPCSSChart( light, ShadowMap_MakeLocalChart( lightIndex, light, positionWS, planeNormalWS ), diagnostics );
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
	if ( ShadowMap_SunSlotInitialized( mapIndex ) )
	{
		if ( mapIndex < DX12_SHADOW_CSM_CASCADES ) result = ShadowMap_SunProjectionValid( cSunWorldToClip[mapIndex], cShadowDepthRecords[mapIndex], cCascadeRects[mapIndex].z, p, normal );
		else result = ShadowMap_SunProjectionValid( cStaticSunWorldToClip, cShadowDepthRecords[4], cStaticSunRect.z, p, normal );
	}
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
	uint bakedLocalDirect; // Actual eligible world/prop direct route, including angular-mode validation.
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
	d.bakedLocalDirect = 0;
	return d;
}
ShadowMapDirect ShadowMap_GatherDirect( ShadowMapReceiver r, ShadowMapShading s )
{
	ShadowMapDirect d = ShadowMap_NoDirect();
	if ( ( cShadowView1.w & DX12_SHADOW_VIEW_HAS_SUN ) != 0 )
	{
		// Geometric surface backfaces skip the map (silhouette taps can read "lit").
		// Detail gathers are full-sphere point receivers: their orientation is NOT a sun cosine gate.
		float3 sunResult = float3( 0.0, 0.0, 0.0 );
		// A zero baked cap makes visibility exactly zero. Keep real map evaluation
		// for blocker/radius debug views, which observe diagnostics even when capped.
		bool sunDiagnostics = cShadowView0.w == 5 || cShadowView0.w == 6;
		[branch] if ( ( r.bakedSunVisibility != 0.0 || sunDiagnostics ) &&
			( s.mode == 4 || dot( r.planeNormalWS, -cSunTravel.xyz ) > 0.0 ) )
		{
			sunResult = float3( 1.0, 0.0, 0.0 );
			[branch] if ( ( cShadowView1.w & DX12_SHADOW_VIEW_UNSHADOWED ) == 0 ) sunResult = ShadowMap_SunResult( r );
		}
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
	ShadowMapBakedLookup bakedLookup = (ShadowMapBakedLookup)0;
	bakedLookup.end = 0xFFFFFFFF; // Validated entry ranges cannot reach this sentinel.
	bool bakedLocalDirect = r.bakedLocalDirect != 0;
	uint2 unbakedLocalRange = r.unbakedLocalRange;
	// Modes 0/1 have exact per-vertex Lambert/half-Lambert planes. Bumped-basis,
	// detail and other angular modes keep full runtime lighting with baked R8.
	[branch] if ( r.bakedPropPrimitive != 0xFFFFFFFF && s.mode <= 1 && cPropDirect.x != 0xFFFFFFFF )
	{
		bakedLocalDirect = ShadowMap_BeginBakedLookup( r, bakedLookup ) && ( bakedLookup.propFlags & 1u ) != 0;
		[branch] if ( bakedLocalDirect )
		{
			d.diffuse += ShadowMap_BakedPropDirect( s.mode, bakedLookup );
			unbakedLocalRange = cPropDirect.zw;
		}
	}
	d.bakedLocalDirect = bakedLocalDirect ? 1 : 0;
	// Select the short resident tail BEFORE touching the full tile CSR.
	uint2 range;
	[branch] if ( bakedLocalDirect ) range = cSunIdentity.zw;
	else range = ShadowMap_TileRange( r );
	uint lowestId = 0xFFFFFFFF;
	uint unbakedCursor = 0;
	uint nextUnbaked = 0xFFFFFFFF;
	bool hasUnbaked = bakedLocalDirect && unbakedLocalRange.y != 0;
	// Empty lists retain the resident-only path: no raw-list loads or searches.
	[loop] for ( uint i = 0; i < range.y
		|| ( hasUnbaked && ( unbakedCursor < unbakedLocalRange.y || nextUnbaked != 0xFFFFFFFF ) )
		; )
	{
		uint index;
		bool unbaked = false;
		[branch] if ( hasUnbaked )
		{
			// Resolve the next present fallback once, retaining it while resident
			// indices precede it. Gaps in view selection never query visibility.
			[loop] while ( nextUnbaked == 0xFFFFFFFF && unbakedCursor < unbakedLocalRange.y )
			{
				uint canonical = g_ShadowVisibilityPayload.Load( unbakedLocalRange.x + 4u * unbakedCursor );
				++unbakedCursor;
				nextUnbaked = ShadowMap_UnbakedViewIndex( canonical );
			}
			uint resident = 0xFFFFFFFF;
			[branch] if ( i < range.y ) resident = g_ShadowTileIndices[range.x + i];
			index = min( resident, nextUnbaked );
			if ( index == 0xFFFFFFFF ) break;
			unbaked = index == nextUnbaked;
			if ( unbaked ) nextUnbaked = 0xFFFFFFFF;
			if ( index == resident ) ++i; // Equal members are evaluated once, as full terms.
		}
		else
		{
			index = g_ShadowTileIndices[range.x + i];
			++i;
		}
		bool bakedDelta = bakedLocalDirect && !unbaked;
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
			float2 diagnostics = 0.0;
			float visibility = 1.0;
			float diffuseVisibility = bakedDelta ? 0.0 : 1.0;
			// Bound diffuse + specular visibility error, scaled by w for baked deltas.
			// A skipped delta is zero (both endpoints use V=1); specular keeps that
			// same unshadowed stand-in times w. Diagnostics never skip.
			float3 contribution = radiance * ( max( angular, 0.0 ) + max( specularAngular, 0.0 ) );
			if ( bakedDelta ) contribution *= light.realtimeWeight;
			bool sampleShadow = cSunBasisY.w <= 0.0 ||
				max( contribution.r, max( contribution.g, contribution.b ) ) > cSunBasisY.w ||
				cShadowView0.w == 5 || cShadowView0.w == 6;
			[branch] if ( sampleShadow )
			{
				float bakedVisibility = 1.0;
				// Full hybrid skips baked loads at w=1; baked direct must subtract Vbaked even then.
				[branch] if ( ( bakedDelta || light.realtimeWeight < 1.0 ) &&
					( r.bakedFaceId != 0xFFFFFFFF || r.bakedPropPrimitive != 0xFFFFFFFF ) &&
					( light.visibilityFlags & DX12_SHADOW_VISIBILITY_BAKED_AVAILABLE ) != 0 )
					bakedVisibility = ShadowMap_BakedLocalVisibility( r, light.bakedLightIndex, bakedLookup );
				float realtimeVisibility = bakedVisibility;
				[branch] if ( light.realtimeWeight > 0.0 )
					realtimeVisibility = ShadowMap_LocalPCSSChart( light, ShadowMap_MakeLocalChart( index, light, r.positionWS, r.planeNormalWS ), diagnostics );
				if ( bakedDelta )
				{
					diffuseVisibility = ( realtimeVisibility - bakedVisibility ) * light.realtimeWeight;
					visibility = realtimeVisibility * light.realtimeWeight;
				}
				else
				{
					visibility = lerp( bakedVisibility, realtimeVisibility, light.realtimeWeight );
					diffuseVisibility = visibility;
				}
			}
			else if ( bakedDelta ) visibility = light.realtimeWeight;
			if ( angular > 0.0 ) d.diffuse += radiance * angular * diffuseVisibility;
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
	// No evaluated union member: retain the empty-resident baked-only convention
	// without walking CSR. A full-term fallback diagnostic must not be overridden.
	bool bakedOnly = d.bakedLocalDirect != 0 && cSunIdentity.w == 0 && cShadowView1.z != 0 && d.lowestLocal == 0xFFFFFFFF;
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
		color = bakedOnly ? float3( 0.0, 1.0, 1.0 ) : 0.0;
		if ( index != 0xFFFFFFFF )
		{
			RuntimeShadowLightGpu light = g_ShadowLights[index];
			[branch] if ( light.realtimeWeight == 0.0 )
				color = float3( 0.0, 1.0, 1.0 ); // Baked-only: never interpret zero rect as atlas page 0.
			else
			{
				uint face = light.faceCount == DX12_SHADOW_MAX_FACES ? ShadowMap_CubeFace( r.positionWS - light.origin ) : 0;
				uint page = light.faces[face].x;
				float3 realtimeColor = float3( float( page & 31 ) / 31.0, float( ( page >> 5 ) & 31 ) / 31.0, float( face + 1 ) / 6.0 );
				color = lerp( float3( 0.0, 1.0, 1.0 ), realtimeColor, light.realtimeWeight );
			}
		}
	}
	else if ( mode == 5 || mode == 6 )
	{
		// Diagnostics are cached by GatherDirect. Baked direct observes the
		// lowest-id union member; other receivers observe the lowest-id tile local.
		// Without sun, a dark/backfacing receiver shows zero (lighting gate skips).
		float2 diagnostics = hasSun ? d.sunDiagnostics : d.localDiagnostics;
		float value = mode == 5 ? diagnostics.x : diagnostics.y;
		color = value.xxx;
		if ( !hasSun && ( bakedOnly || ( d.lowestLocal != 0xFFFFFFFF && g_ShadowLights[d.lowestLocal].realtimeWeight == 0.0 ) ) )
			color = float3( 0.0, 1.0, 1.0 ); // No blocker/radius chart exists in baked-only mode.
	}
	return float4( color, lit.a );
}
#endif
