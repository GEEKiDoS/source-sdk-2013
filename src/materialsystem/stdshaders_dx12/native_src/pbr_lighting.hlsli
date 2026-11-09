// PBR light loops for the pbr_* native shaders (PBR-only; included after dx12_preamble.h).
// Projected (flashlight / env_projectedtexture) spots are sampled by projected_lighting.hlsli (register space 5);
// PBR_GatherDirect exists only when the entry file defined DX12_SHADOWMAPS (model ENHANCED=1, world highres twin).
#ifndef PBR_LIGHTING_HLSLI
#define PBR_LIGHTING_HLSLI
#include "pbr_brdf.hlsli"
#include "projected_lighting.hlsli"
#include "screenspace_contact.hlsli"

#define PBR_RECEIVER_WORLD 1
#define PBR_RECEIVER_MODEL 2

// Burley diffuse + GGX for every projected light that lights this receiver class. A shadowed light's comparison
// visibility is multiplied by the screen-space contact ray (surface normal, stopping short of the light) before the
// existing shadowAtten blend; cookie, receiver mask and distance attenuation are DX12_ProjectedSample's. Each
// light's GGX share joins specularSum at its distance (SSR occlusion payload).
float3 PBR_ProjectedSpots( PBRSurface s, float3 P, uint receiverFlag, inout PBRSpecularSum specularSum )
{
	uint count, stride;
	g_ProjectedLights.GetDimensions( count, stride );
	float3 result = 0.0;
	[loop] for ( uint i = 0; i < count; ++i )
	{
		// Receiver-class rejection touches only the flags, not the 128-byte spot.
		[branch] if ( ( uint( g_ProjectedLights[i].slotsFlags.z ) & receiverFlag ) == 0 )
			continue;
		PBRSpotGpu light = g_ProjectedLights[i];
		float3 L, radiance;
		float distance, rawVisibility;
		if ( !DX12_ProjectedSample( light, P, L, distance, radiance, rawVisibility ) )
			continue;
		[branch] if ( light.slotsFlags.y >= 0 && rawVisibility > 0.0 )
			rawVisibility *= DX12_ContactVisibility( P, s.N, L, distance - 0.25 );
		float shadow = lerp( rawVisibility, 1.0, light.attnShadow.z );
		float3 specular;
		float3 diffuse = PBR_DiffuseBurley( s, L );
		if ( g_L4D2Model.y != 0.0 )
			diffuse = s.diffuseColor;
		else if ( g_L4D2Model.z > 0.0 )
		{
			float nDotL = saturate( dot( s.N, L ) );
			diffuse *= pow( nDotL, g_L4D2Model.z ) / max( nDotL, 1e-4 );
		}
		float3 ggx = PBR_Specular( s, L );
		specular = ggx * radiance * shadow;
		result += ( diffuse + ggx ) * radiance * shadow;
		PBR_AddSpecular( specularSum, specular, distance );
	}
	return result;
}

// Draw-local lights (engine cLightInfo: up to four, the fourth packed into the .w channels). Directional and
// spot lights arrive point-encoded; their cone/distance attenuation is the VS-computed lightAtten.
float3 PBR_LightColor( int i )
{
	return i == 3 ? float3( cLightInfo[0].color.w, cLightInfo[0].pos.w, cLightInfo[1].color.w ) : cLightInfo[i].color.rgb;
}
float3 PBR_LightPosition( int i )
{
	return i == 3 ? float3( cLightInfo[1].pos.w, cLightInfo[2].color.w, cLightInfo[2].pos.w ) : cLightInfo[i].pos.xyz;
}
float3 PBR_LocalLights( PBRSurface s, float3 P, int count, float4 lightAtten, inout PBRSpecularSum specularSum )
{
	float3 result = 0.0;
	// A loop: unrolled, the four slots carry four copies of the Burley + GGX lobes for draws that mostly have none.
	[loop] for ( int i = 0; i < min( count, 4 ); ++i )
	{
		float3 toLight = PBR_LightPosition( i ) - P;
		float attenuation = i == 0 ? lightAtten.x : i == 1 ? lightAtten.y : i == 2 ? lightAtten.z : lightAtten.w;
		float3 specular;
		result += PBR_DirectSplit( s, normalize( toLight ), PBR_LightColor( i ) * attenuation, 1.0, true, specular );
		PBR_AddSpecular( specularSum, specular, length( toLight ) );
	}
	return result;
}

// Engine ambient cube (PixelShaderAmbientLight): squared-normal weights over the six faces.
float3 PBR_AmbientCube( float3 n )
{
	float3 nSquared = n * n;
	float3 isNegative = n >= 0.0 ? 0.0 : nSquared;
	float3 isPositive = n >= 0.0 ? nSquared : 0.0;
	return isPositive.x * cAmbientCube[0] + isNegative.x * cAmbientCube[1] +
		isPositive.y * cAmbientCube[2] + isNegative.y * cAmbientCube[3] +
		isPositive.z * cAmbientCube[4] + isNegative.z * cAmbientCube[5];
}

#if defined( DX12_SHADOWMAPS )
// A world face's entry range: the face directory entry, no sampling state (ShadowMap_BeginBakedLookup's face path).
uint2 PBR_FaceEntries( ShadowMapReceiver r )
{
	uint4 face = g_ShadowVisibilityFaces[r.bakedFaceId];
	return uint2( face.x, face.y );
}

// Baked visibility of a receiver entry: the stored constant, or the bilinear (world face) / barycentric (prop) R8 payload
// taps. A prop's lookup must have been begun; a world face's taps are derived here from its directory entry and the
// receiver's face coordinate, exactly as ShadowMap_BeginBakedLookup does.
float PBR_EntryVisibility( ShadowMapReceiver r, ShadowMapBakedLookup lookup, uint4 entry )
{
	float visibility = float( entry.y ) * ( 1.0 / 255.0 );
	[branch] if ( entry.y > 255u )
	{
		[branch] if ( r.bakedPropPrimitive != 0xFFFFFFFF )
		{
			float3 taps = float3(
				ShadowMap_VisibilityByte( entry.z + lookup.samples.x ),
				ShadowMap_VisibilityByte( entry.z + lookup.samples.y ),
				ShadowMap_VisibilityByte( entry.z + lookup.samples.z ) );
			visibility = dot( taps, lookup.barycentrics );
		}
		else
		{
			uint4 face = g_ShadowVisibilityFaces[r.bakedFaceId];
			uint2 last = face.zw - 1u;
			float2 pixel = saturate( r.bakedQ ) * float2( last );
			uint2 p0 = uint2( floor( pixel ) );
			uint2 p1 = min( p0 + 1u, last );
			float2 fraction = pixel - float2( p0 );
			uint2 rows = uint2( p0.y, p1.y ) * face.z;
			float4 taps = float4(
				ShadowMap_VisibilityByte( entry.z + rows.x + p0.x ),
				ShadowMap_VisibilityByte( entry.z + rows.x + p1.x ),
				ShadowMap_VisibilityByte( entry.z + rows.y + p0.x ),
				ShadowMap_VisibilityByte( entry.z + rows.y + p1.x ) );
			visibility = lerp( lerp( taps.x, taps.y, fraction.x ), lerp( taps.z, taps.w, fraction.x ), fraction.y );
		}
	}
	return visibility;
}

// Baked visibility of one selected light for the receiver (1 where the receiver has no entry for it): a binary search of the
// receiver's entries, sorted by selected-light index. A prop's lookup is begun on first use; its range is never advanced.
float PBR_BakedVisibility( ShadowMapReceiver r, inout ShadowMapBakedLookup lookup, uint selectedLightIndex )
{
	float visibility = 1.0;
	uint first = 0, end = 0;
	[branch] if ( r.bakedPropPrimitive != 0xFFFFFFFF )
	{
		[branch] if ( ShadowMap_BeginBakedLookup( r, lookup ) )
		{
			first = lookup.cursor;
			end = lookup.end;
		}
	}
	else
	{
		uint2 face = PBR_FaceEntries( r );
		first = face.x;
		end = face.x + face.y;
	}
	uint last = end;
	[loop] while ( first < end )
	{
		uint middle = first + ( end - first ) / 2u;
		if ( g_ShadowVisibilityEntries[middle].x < selectedLightIndex ) first = middle + 1u;
		else end = middle;
	}
	[branch] if ( first < last )
	{
		uint4 entry = g_ShadowVisibilityEntries[first];
		if ( entry.x == selectedLightIndex ) visibility = PBR_EntryVisibility( r, lookup, entry );
	}
	return visibility;
}

#define PBR_LIVE_BATCH 32
// The upload owner stores a live bitmask in .w of the first entry of each receiver-local 32-entry chunk.
// It is derived from the immutable .y visibility (nonzero), including dense payload entries; disk data is unchanged.
uint PBR_LiveEntries( uint first, uint count )
{
	[branch] if ( count == 0 ) return 0;
	return g_ShadowVisibilityEntries[first].w;
}

bool PBR_IsUnbakedLight( uint2 range, uint selectedLightIndex )
{
	bool found = false;
	[loop] for ( uint j = 0; j < range.y && !found; ++j )
		found = g_ShadowVisibilityPayload.Load( range.x + 4u * j ) == selectedLightIndex;
	return found;
}

// Copy of ShadowMap_GatherDirect (shadowmap_lighting.hlsli) with the Phong lobe replaced by the PBR lobes.
// d.diffuse and d.specular are outgoing radiance (albedo included), not irradiance.
//  - Terms that cancel baked data keep the stored basis (Lambert, squared half-Lambert or the bumped-lightmap
//    planes): the signed (Vrt - Vbaked) * weight delta of baked-direct receivers (world highres faces, props).
//  - Terms with no baked counterpart (the sun, non-baked receivers' tile CSR, style-overflow full terms)
//    use the Burley lobe.
//  - Specular has no baked counterpart for any light, so every light adds it with the visibility lerp( Vbaked, Vrt, weight ):
//    realtime lights fade between the stored and the realtime shadow instead of losing their highlight.
//    Subsurface/backlight extras are added with Vrt * weight and are never subtracted from baked data.
//  - Baked-only lights (weight 0) on baked-direct receivers have their diffuse entirely in the stored carriers, so they add
//    specular alone, shadowed by the stored visibility. They are found from the receiver's own entry list rather than by
//    walking every light of the tile: the bake leaves almost every entry fully blocked (constant 0), such a light adds
//    exactly nothing, and a light without an entry lies beyond its radius of the receiver (the bake keeps an entry for
//    every light that reaches the receiver). Their stored visibility is exact and cheaper than the radiance bound, so they
//    never take the r_shadowmap_skip_radiance shortcut. Realtime-weighted and style-overflow lights stay in the loop below.
// Every specular term also joins specularSum at its light distance (the sun at PBR_SUN_DISTANCE) for the SSR
// occlusion payload; d.specular is unchanged by that bookkeeping.
ShadowMapDirect PBR_GatherDirect( ShadowMapReceiver r, PBRSurface s, inout PBRSpecularSum specularSum )
{
	ShadowMapDirect d = ShadowMap_NoDirect();
	ShadowMapBakedLookup bakedLookup = (ShadowMapBakedLookup)0;
	bakedLookup.end = 0xFFFFFFFF; // Validated entry ranges cannot reach this sentinel.
	bool bakedLocalDirect = r.bakedLocalDirect != 0;
	uint2 unbakedLocalRange = r.unbakedLocalRange;
	// Both PBR angular bases (Lambert, squared half-Lambert) have exact per-vertex prop planes.
	bool bakedProp = false;
	[branch] if ( r.bakedPropPrimitive != 0xFFFFFFFF && cPropDirect.x != 0xFFFFFFFF )
	{
		bakedLocalDirect = ShadowMap_BeginBakedLookup( r, bakedLookup ) && ( bakedLookup.propFlags & 1u ) != 0;
		bakedProp = bakedLocalDirect;
		[branch] if ( bakedProp )
			unbakedLocalRange = cPropDirect.zw;
	}
	// Baked-direct receivers start their entry reads here, ahead of the sun's shadow work that hides their latency.
	uint entryFirst = 0, entryCount = 0, firstLive = 0;
	[branch] if ( bakedLocalDirect )
	{
		[branch] if ( bakedProp )
		{
			entryFirst = bakedLookup.cursor;
			entryCount = bakedLookup.end - bakedLookup.cursor;
		}
		else
		{
			uint2 face = PBR_FaceEntries( r );
			entryFirst = face.x;
			entryCount = face.y;
		}
		firstLive = PBR_LiveEntries( entryFirst, entryCount );
	}
	if ( ( cShadowView1.w & DX12_SHADOW_VIEW_HAS_SUN ) != 0 )
	{
		// Receivers facing away from the sun skip the map (silhouette taps can read "lit"). The gate must not cut a lobe
		// that is still lit: squared half-Lambert is 0.25 at the terminator and subsurface/backlight reach past it, so
		// those receivers always take the map (it self-shadows them) and only a Lambert receiver is gated, on the smooth
		// vertex normal. A per-triangle plane normal would split a curved mesh's terminator into lit/unlit triangles;
		// it still drives the map bias.
		float3 sunL = -cSunTravel.xyz;
		bool sunWraps = s.angular != 0 || s.sss != 0.0 || s.back != 0.0;
		float3 sunResult = float3( 0.0, 0.0, 0.0 );
		float csmWeight = 0.0;
		// A zero dilated baked bound makes visibility exactly zero under either cap. Keep real map evaluation
		// for blocker/radius debug views, which observe diagnostics even when capped.
		bool sunDiagnostics = cShadowView0.w == 5 || cShadowView0.w == 6;
		[branch] if ( ( r.bakedSunDilated != 0.0 || sunDiagnostics ) && ( sunWraps || dot( r.vertexNormalWS, sunL ) > 0.0 ) )
		{
			sunResult = float3( 1.0, 0.0, 0.0 );
			[branch] if ( ( cShadowView1.w & DX12_SHADOW_VIEW_UNSHADOWED ) == 0 )
			{
				// Runtime visibility only: the contact ray needs a valid runtime chart and precedes the baked cap.
				bool charted;
				sunResult = ShadowMap_SunResultCharted( r, 1.0, charted, csmWeight );
				[branch] if ( DX12_SUN_CONTACT_SHADOWS && charted && sunResult.x > 0.0 )
					sunResult.x *= DX12_ContactVisibility( r.positionWS, r.planeNormalWS, -cSunTravel.xyz, cScreenEyeLength.w );
			}
		}
		d.sunVisibility = min( saturate( sunResult.x ), ShadowMap_SunCap( r, csmWeight ) );
		d.sunDiagnostics = sunResult.yz;
		// A fully shadowed sun adds exactly nothing: its lobes are not evaluated.
		[branch] if ( d.sunVisibility > 0.0 )
		{
			float3 L = -cSunTravel.xyz;
			float3 radiance = cSunRadiance.rgb * d.sunVisibility;
			d.diffuse += radiance * PBR_DiffuseBurley( s, L );
			float3 specular = radiance * PBR_Specular( s, L );
			d.specular += specular;
			PBR_AddSpecular( specularSum, specular, PBR_SUN_DISTANCE );
		}
	}
	[branch] if ( bakedProp )
		d.diffuse += s.diffuseColor * s.ao * ShadowMap_BakedPropDirect( s.angular, bakedLookup );
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
		uint lightId = g_ShadowLights[index].lightId;
		bool lowest = lightId < lowestId;
		if ( lowest )
		{
			lowestId = lightId;
			d.lowestLocal = index;
		}
		// The radius/fade test needs only the hot position/range rows; rejected lights never fetch the full header.
		ShadowMapLightRange lightRange;
		[branch] if ( !ShadowMap_LightInRange( index, r.positionWS, lightRange ) ) continue;
		RuntimeShadowLightGpu light = g_ShadowLights[index];
		light.origin = lightRange.origin;
		light.attenuationRadius = lightRange.attenuationRadius;
		light.fadeStart = lightRange.fade.x;
		light.fadeEnd = lightRange.fade.y;
		bool bakedDelta = bakedLocalDirect && !unbaked && ( light.visibilityFlags & DX12_SHADOW_VISIBILITY_BAKED_AVAILABLE ) != 0;
		float3 L;
		float3 radiance = ShadowMap_StandardLightRadianceInRange( light, lightRange.offset, lightRange.distance, L );
		[branch] if ( !any( radiance != 0.0 ) )
			continue;
		// Light-only lobes: the stored basis and subsurface extras for deltas, the Burley term otherwise. A branch, not a
		// select: a select evaluates both lobes for every light.
		float3 basis, extra = float3( 0.0, 0.0, 0.0 );
		[branch] if ( bakedDelta )
		{
			basis = PBR_DiffuseBasis( s, L );
			[branch] if ( s.sss != 0.0 || s.back != 0.0 )
				extra = PBR_DiffuseLambert( s, L ) - basis; // what subsurface/backlight add beyond the stored basis
		}
		else basis = PBR_DiffuseBurley( s, L );
		float3 specular = PBR_Specular( s, L );
		if ( PBR_Max3( abs( basis ) ) > 0.0 || PBR_Max3( extra ) > 0.0 || PBR_Max3( specular ) > 0.0 )
		{
			float visibility = 1.0;
			float diffuseVisibility = bakedDelta ? 0.0 : 1.0;
			float specularVisibility = 1.0;
			// Bound diffuse + specular visibility error: diffuse deltas scale with w, specular does not. A skipped delta
			// is zero (both endpoints use V=1); skipped specular keeps the unshadowed stand-in.
			float3 contribution = radiance * ( ( PBR_Max3( basis ) + PBR_Max3( extra ) ) * ( bakedDelta ? light.realtimeWeight : 1.0 ) + PBR_Max3( specular ) );
			bool sampleShadow = cSunBasisY.w <= 0.0 ||
				max( contribution.r, max( contribution.g, contribution.b ) ) > cSunBasisY.w;
			[branch] if ( sampleShadow )
			{
				float bakedVisibility = 1.0;
				// Full hybrid skips baked loads at w=1; baked direct must subtract Vbaked even then.
				[branch] if ( ( bakedDelta || light.realtimeWeight < 1.0 ) &&
					( r.bakedFaceId != 0xFFFFFFFF || r.bakedPropPrimitive != 0xFFFFFFFF ) &&
					( light.visibilityFlags & DX12_SHADOW_VISIBILITY_BAKED_AVAILABLE ) != 0 )
					bakedVisibility = PBR_BakedVisibility( r, bakedLookup, light.bakedLightIndex );
				float realtimeVisibility = bakedVisibility;
				// Local lights always use the 4-tap receiver-plane PCF; contact-hardening PCSS is sun-only. A valid chart's
				// visibility is then multiplied by the contact ray toward the light (stopping 0.25 short of it).
				[branch] if ( light.realtimeWeight > 0.0 && ( light.visibilityFlags & DX12_SHADOW_VISIBILITY_UNSHADOWED ) == 0 )
				{
					ShadowMapLocalChart chart = ShadowMap_MakeLocalChart( index, light, r.positionWS, r.planeNormalWS );
					realtimeVisibility = ShadowMap_LocalPCFChart( light, chart );
					[branch] if ( chart.valid && realtimeVisibility > 0.0 )
					{
						realtimeVisibility *= DX12_ContactVisibility( r.positionWS, r.planeNormalWS, lightRange.offset, lightRange.distance - 0.25 );
					}
				}
				specularVisibility = lerp( bakedVisibility, realtimeVisibility, light.realtimeWeight );
				if ( bakedDelta )
				{
					diffuseVisibility = ( realtimeVisibility - bakedVisibility ) * light.realtimeWeight;
					visibility = realtimeVisibility * light.realtimeWeight;
				}
				else
				{
					visibility = specularVisibility;
					diffuseVisibility = visibility;
				}
			}
			else if ( bakedDelta ) visibility = light.realtimeWeight;
			d.diffuse += radiance * ( basis * diffuseVisibility + extra * visibility );
			float3 lightSpecular = radiance * specular * specularVisibility;
			d.specular += lightSpecular;
			PBR_AddSpecular( specularSum, lightSpecular, lightRange.distance );
		}
	}
	// Baked-only specular (see above): only the receiver's live entries are visited.
	[branch] if ( bakedLocalDirect )
	{
		[loop] for ( uint chunk = 0; chunk < entryCount; chunk += PBR_LIVE_BATCH )
		{
			uint live = chunk == 0 ? firstLive : PBR_LiveEntries( entryFirst + chunk, entryCount - chunk );
			[loop] while ( live != 0 )
			{
				uint4 entry = g_ShadowVisibilityEntries[entryFirst + chunk + firstbitlow( live )];
				live &= live - 1u;
				uint index = ShadowMap_UnbakedViewIndex( entry.x );
				[branch] if ( index == 0xFFFFFFFF ) continue; // Absent from the conservative per-view selection: irrelevant.
				// Reject from the hot fields before loading attenuation/cone/radiance rows.
				float realtimeWeight = g_ShadowLights[index].realtimeWeight;
				float3 origin = g_ShadowLights[index].origin;
				// The resident tail evaluated the realtime-weighted lights, the unbaked stream the style-overflow ones.
				[branch] if ( realtimeWeight != 0.0 || dot( s.N, origin - r.positionWS ) <= 0.0 ) continue;
				[branch] if ( hasUnbaked && PBR_IsUnbakedLight( unbakedLocalRange, entry.x ) ) continue;
				RuntimeShadowLightGpu light = g_ShadowLights[index];
				light.origin = origin;
				float3 L;
				float3 radiance = ShadowMap_StandardLightRadiance( light, r.positionWS, L );
				[branch] if ( any( radiance != 0.0 ) )
				{
					float3 lightSpecular = radiance * PBR_Specular( s, L ) * PBR_EntryVisibility( r, bakedLookup, entry );
					d.specular += lightSpecular;
					PBR_AddSpecular( specularSum, lightSpecular, length( light.origin - r.positionWS ) );
				}
			}
		}
	}
	return d;
}
#endif

#endif
