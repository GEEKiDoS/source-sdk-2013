//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_PAIRED_SCENE_H
#define RESTIR_PAIRED_SCENE_H
#pragma once
#include "restir_types.h"
#include <string.h>
#include <stddef.h>

// Only compare fully built, pre-ResolveFaceStyles CPU scenes. Both builds use
// the same options and BSP snapshot, changing only hdr. Exact bytes deliberately
// reject even numerically close inputs; padding differences merely deny reuse.
// Never compare the CUtlVector/CUtlString containers themselves or mode headers.
template <typename T>
inline bool ReSTIR_PairedArrayEqual( const CUtlVector<T> &a, const CUtlVector<T> &b )
{
	return a.Count() == b.Count() && ( !a.Count() ||
		memcmp( a.Base(), b.Base(), size_t( a.Count() ) * sizeof( T ) ) == 0 );
}
// GPU records deliberately place their float blocks before integer indexing.
// Reject identical NaN/Inf bit patterns as unproven inputs, without interpreting
// index bits as floats or allocating a second canonicalized scene.
template <typename T>
inline bool ReSTIR_PairedFinitePrefix( const CUtlVector<T> &values, size_t bytes )
{
	for ( int i = 0; i < values.Count(); ++i )
	{
		const unsigned char *p = reinterpret_cast<const unsigned char *>( &values[i] );
		for ( size_t offset = 0; offset < bytes; offset += sizeof( uint32 ) )
		{
			uint32 bits;
			memcpy( &bits, p + offset, sizeof( bits ) );
			if ( ( bits & 0x7f800000u ) == 0x7f800000u )
				return false;
		}
	}
	return true;
}
inline bool ReSTIR_PairedWorldLightFinite( const dworldlight_t &light )
{
	return light.origin.IsValid() && light.intensity.IsValid() && light.normal.IsValid() &&
		ShadowMap_IsFiniteFloat( light.stopdot ) && ShadowMap_IsFiniteFloat( light.stopdot2 ) &&
		ShadowMap_IsFiniteFloat( light.exponent ) && ShadowMap_IsFiniteFloat( light.radius ) &&
		ShadowMap_IsFiniteFloat( light.constant_attn ) && ShadowMap_IsFiniteFloat( light.linear_attn ) &&
		ShadowMap_IsFiniteFloat( light.quadratic_attn );
}

inline bool ReSTIR_PairedScenesEqual( const ReSTIRScene &a, const ReSTIRScene &b )
{
	if ( a.worldMins != b.worldMins || a.worldMaxs != b.worldMaxs ||
		a.skyAmbientLight != b.skyAmbientLight || a.skyLight != b.skyLight ||
		a.receiverStyleMask != b.receiverStyleMask || a.numOutputValues != b.numOutputValues ||
		a.shadowSunAngularRadius != b.shadowSunAngularRadius )
		return false;
	if ( !a.worldMins.IsValid() || !a.worldMaxs.IsValid() ||
		!ShadowMap_IsFiniteFloat( a.shadowSunAngularRadius ) )
		return false;
	// The generalized receiver domain is required even for local-only scenes.
	// Selected source uploads derive only from shadowLights (compared below),
	// preserving canonical indices/radii without a second independently mutable map.
	if ( ( a.shadowLights.Count() && a.sunVisibilityOrigins.Count() != a.luxels.Count() ) ||
		( b.shadowLights.Count() && b.sunVisibilityOrigins.Count() != b.luxels.Count() ) )
		return false;
#define RESTIR_COMPARE_ARRAY( field ) if ( !ReSTIR_PairedArrayEqual( a.field, b.field ) ) return false
	RESTIR_COMPARE_ARRAY( triangles );
	RESTIR_COMPARE_ARRAY( materials );
	RESTIR_COMPARE_ARRAY( lights );
	RESTIR_COMPARE_ARRAY( emitterTriangles );
	RESTIR_COMPARE_ARRAY( styleLights );
	RESTIR_COMPARE_ARRAY( exportLights );
	RESTIR_COMPARE_ARRAY( exportLightToGpuLight );
	RESTIR_COMPARE_ARRAY( sceneStyles );
	RESTIR_COMPARE_ARRAY( faces );
	RESTIR_COMPARE_ARRAY( dfaceToFace );
	RESTIR_COMPARE_ARRAY( samples );
	RESTIR_COMPARE_ARRAY( luxels );
	RESTIR_COMPARE_ARRAY( sunVisibilityOrigins );
	RESTIR_COMPARE_ARRAY( localDirectPositions );
	RESTIR_COMPARE_ARRAY( localDirectNormals );
	RESTIR_COMPARE_ARRAY( localDirectBumpNormals );
	RESTIR_COMPARE_ARRAY( faceNeighbors );
	RESTIR_COMPARE_ARRAY( styleOverflowFaces );
	RESTIR_COMPARE_ARRAY( styleCandidateFirst );
	RESTIR_COMPARE_ARRAY( styleCandidateCount );
	RESTIR_COMPARE_ARRAY( styleCandidateLights );
	RESTIR_COMPARE_ARRAY( faceMinLight );
	RESTIR_COMPARE_ARRAY( shadowLights );
#undef RESTIR_COMPARE_ARRAY
	if ( a.textures.Count() != b.textures.Count() )
		return false;
	for ( int i = 0; i < a.textures.Count(); ++i )
	{
		const ReSTIRSceneTexture &x = a.textures[i], &y = b.textures[i];
		if ( x.width != y.width || x.height != y.height || x.channels != y.channels ||
			!ReSTIR_PairedArrayEqual( x.texels, y.texels ) )
			return false;
	}
	if ( !ReSTIR_PairedFinitePrefix( a.triangles, offsetof( ReSTIRGpuTriangle, hitId ) ) ||
		!ReSTIR_PairedFinitePrefix( a.materials, offsetof( ReSTIRGpuMaterial, coverageTexture ) ) ||
		!ReSTIR_PairedFinitePrefix( a.lights, offsetof( ReSTIRGpuLight, type ) ) ||
		!ReSTIR_PairedFinitePrefix( a.emitterTriangles, sizeof( ReSTIRGpuEmitterTriangle ) ) ||
		!ReSTIR_PairedFinitePrefix( a.faces, offsetof( ReSTIRGpuFace, lmMins ) ) ||
		!ReSTIR_PairedFinitePrefix( a.samples, offsetof( ReSTIRGpuSample, face ) ) ||
		!ReSTIR_PairedFinitePrefix( a.luxels, sizeof( ReSTIRGpuLuxel ) ) ||
		!ReSTIR_PairedFinitePrefix( a.sunVisibilityOrigins, sizeof( Vector4D ) ) ||
		!ReSTIR_PairedFinitePrefix( a.localDirectPositions, sizeof( Vector ) ) ||
		!ReSTIR_PairedFinitePrefix( a.localDirectNormals, sizeof( Vector ) ) ||
		!ReSTIR_PairedFinitePrefix( a.localDirectBumpNormals, sizeof( Vector ) ) )
		return false;
	for ( int i = 0; i < a.lights.Count(); ++i )
		if ( !ShadowMap_IsFiniteFloat( a.lights[i].sunSpreadAngle ) )
			return false;
	for ( int i = 0; i < a.faceMinLight.Count(); ++i )
		if ( !a.faceMinLight[i].IsValid() )
			return false;
	for ( int i = 0; i < a.exportLights.Count(); ++i )
		if ( !ReSTIR_PairedWorldLightFinite( a.exportLights[i] ) )
			return false;
	for ( int i = 0; i < a.shadowLights.Count(); ++i )
	{
		const ShadowMapLightDisk &light = a.shadowLights[i];
		if ( !ReSTIR_PairedWorldLightFinite( light.light ) ||
			!ShadowMap_IsFiniteFloat( light.shadowSunAngularRadius ) ||
			!ShadowMap_IsFiniteFloat( light.shadowSourceRadius ) ||
			!ShadowMap_IsFiniteFloat( light.startFade ) || !ShadowMap_IsFiniteFloat( light.endFade ) ||
			!ShadowMap_IsFiniteFloat( light.capDist ) )
			return false;
	}
	return true;
}
#endif // RESTIR_PAIRED_SCENE_H
