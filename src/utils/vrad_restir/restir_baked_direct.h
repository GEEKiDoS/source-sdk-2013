//========= Copyright Valve Corporation, All rights reserved. ============//
// Selected local direct for enhanced world lightmaps only. Native BSP/VHV,
// ambient and detail transport must not use these receiver-only helpers.
#ifndef RESTIR_BAKED_DIRECT_H
#define RESTIR_BAKED_DIRECT_H
#pragma once

#include "restir_types.h"
#include <math.h>

// ShadowMap_StandardLightRadiance in native_src/shadowmap_lighting.hlsli.
// Manifest worldlight intensity is runtime linear radiance (the scene exporter
// divides VRAD intensity by 255). Return unstyled VRAD units for hlight's /255
// encode; neither visibility nor a receiver cosine is part of this function.
inline Vector ReSTIR_BakedLocalRadiance( const ShadowMapLightDisk &disk,
	const Vector &position, Vector &L )
{
	const dworldlight_t &light = disk.light;
	const Vector offset = light.origin - position;
	const float distanceSquared = DotProduct( offset, offset );
	L.Init();
	const float distance = sqrtf( MAX( distanceSquared, 0.0f ) );
	const bool hardFade = disk.endFade > disk.startFade;
	if ( !( distanceSquared > 0.0f ) || ( light.radius > 0.0f && distance > light.radius ) ||
		( hardFade && distance > disk.endFade ) )
		return Vector( 0.0f, 0.0f, 0.0f );

	L = offset / distance;
	const float clampedDistance = MAX( distance, 1.0f );
	const float evaluationDistance = disk.capDist > 0.0f ? MIN( clampedDistance, disk.capDist ) : clampedDistance;
	const float denominator = light.constant_attn + evaluationDistance * light.linear_attn +
		evaluationDistance * evaluationDistance * light.quadratic_attn;
	float falloff = denominator > 0.0f ? 1.0f / denominator : 0.0f;
	if ( light.type == emit_spotlight )
	{
		const float coneDot = -DotProduct( L, light.normal );
		float cone = 1.0f;
		if ( coneDot <= light.stopdot )
		{
			cone = clamp( ( coneDot - light.stopdot2 ) / ( light.stopdot - light.stopdot2 ), 0.0f, 1.0f );
			if ( light.exponent != 0.0f && light.exponent != 1.0f )
				cone = powf( cone, light.exponent );
		}
		falloff = coneDot <= light.stopdot2 ? 0.0f : falloff * coneDot * cone;
	}
	if ( hardFade )
	{
		const float t = 1.0f - clamp( ( clampedDistance - disk.startFade ) /
			( disk.endFade - disk.startFade ), 0.0f, 1.0f );
		falloff *= t * t * t * ( t * ( t * 6.0f - 15.0f ) + 10.0f );
	}
	return light.intensity * ( 255.0f * falloff );
}

// ShadowMap_Angular mode 0, or one component of mode 2. Do not normalize N:
// generated highres pixel shaders use the interpolated normal/bases as supplied.
inline float ReSTIR_BakedLocalAngular( const Vector &L, const Vector &N )
{
	return clamp( DotProduct( N, L ), 0.0f, 1.0f );
}

// HLSL normalize, rather than VectorNormalize's nonzero epsilon. Generated VS
// explicitly normalizes the renderer's incoming tangent axes before export.
inline void ReSTIR_BakedNormalizeTangent( Vector &v )
{
	const float lengthSquared = DotProduct( v, v );
	if ( lengthSquared > 0.0f ) v /= sqrtf( lengthSquared );
}

// Reference renderer: E:/SourceEngine/engine/matsys_interface.cpp:1655-1685
// (TangentSpaceSurfaceSetup / TangentSpaceComputeBasis). textureT anchors the
// frame; mirrored mapping negates S ONLY after constructing T. VRAD's
// GetBumpNormals anchors textureS instead and is not equivalent for skewed axes.
// This constructs a VERTEX frame. Where normals vary across a triangle, callers
// must interpolate the three vertex frames like the generated VS/PS join, not
// reconstruct a frame from an interpolated normal.
inline void ReSTIR_BakedTangentFrame( const ReSTIRGpuFace &face, const Vector &N,
	Vector &S, Vector &T )
{
	Vector textureS( face.textureS[0], face.textureS[1], face.textureS[2] );
	Vector textureT( face.textureT[0], face.textureT[1], face.textureT[2] );
	ReSTIR_BakedNormalizeTangent( textureS );
	ReSTIR_BakedNormalizeTangent( textureT );
	const Vector flatNormal( face.faceNormal[0], face.faceNormal[1], face.faceNormal[2] );
	const bool negateS = DotProduct( flatNormal, CrossProduct( textureS, textureT ) ) > 0.0f;
	S = CrossProduct( N, textureT );
	ReSTIR_BakedNormalizeTangent( S );
	T = CrossProduct( S, N );
	ReSTIR_BakedNormalizeTangent( T );
	if ( negateS ) S = -S;
}

// Exact common_fxc.h bumpBasis constants, NOT g_localBumpBasis: shader basis 1
// X and basis 2 Y differ slightly. Preserve their order and do not normalize the
// transformed bases. Lightmappedgeneric highres mode 2 weights these three
// clamped angular responses (normalized squared dots for normal maps, raw
// texture weights for SSBUMP); there is no additional bumped normalization.
inline void ReSTIR_BakedBumpFrameNormals( const Vector &N, const Vector &S, const Vector &T, Vector out[3] )
{
	out[0] = S * 0.81649661064147949f + N * 0.57735025882720947f;
	out[1] = S * -0.40824833512306213f + T * 0.70710676908493042f + N * 0.57735025882720947f;
	out[2] = S * -0.40824821591377258f + T * -0.7071068286895752f + N * 0.57735025882720947f;
}

inline void ReSTIR_BakedBumpNormals( const ReSTIRGpuFace &face, const Vector &N, Vector out[3] )
{
	Vector S, T;
	ReSTIR_BakedTangentFrame( face, N, S, T );
	ReSTIR_BakedBumpFrameNormals( N, S, T, out );
}

#endif // RESTIR_BAKED_DIRECT_H
