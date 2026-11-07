//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PARTICLE_ATTRIBUTES_H
#define TERROR_PARTICLE_ATTRIBUTES_H
#ifdef _WIN32
#pragma once
#endif

#include "particles/particles.h"

// The target's native allocator handles all 32 attribute bits. Keep new vector
// components in separate, previously unnamed scalar fields: retail NORMAL21
// conflicts with the SDK's integer CONTROL_POINT_INDEX, and retail GLOW_RGB22
// is not a native SDK vector field. No existing SDK semantic field is aliased.
enum
{
	TERROR_PARTICLE_GLOW_RED = 22,
	TERROR_PARTICLE_GLOW_GREEN,
	TERROR_PARTICLE_GLOW_BLUE,
	TERROR_PARTICLE_GLOW_ALPHA,
	TERROR_PARTICLE_NORMAL_X,
	TERROR_PARTICLE_NORMAL_Y,
	TERROR_PARTICLE_NORMAL_Z,
};

static const uint32 TERROR_PARTICLE_GLOW_RGB_MASK =
	( 1u << TERROR_PARTICLE_GLOW_RED ) |
	( 1u << TERROR_PARTICLE_GLOW_GREEN ) |
	( 1u << TERROR_PARTICLE_GLOW_BLUE );
static const uint32 TERROR_PARTICLE_GLOW_ALPHA_MASK = 1u << TERROR_PARTICLE_GLOW_ALPHA;
static const uint32 TERROR_PARTICLE_NORMAL_MASK =
	( 1u << TERROR_PARTICLE_NORMAL_X ) |
	( 1u << TERROR_PARTICLE_NORMAL_Y ) |
	( 1u << TERROR_PARTICLE_NORMAL_Z );

inline Vector TerrorReadParticleGlow( CParticleCollection *pParticles, int nParticle )
{
	return Vector(
		*pParticles->GetFloatAttributePtr( TERROR_PARTICLE_GLOW_RED, nParticle ),
		*pParticles->GetFloatAttributePtr( TERROR_PARTICLE_GLOW_GREEN, nParticle ),
		*pParticles->GetFloatAttributePtr( TERROR_PARTICLE_GLOW_BLUE, nParticle ) );
}

inline void TerrorWriteParticleGlow( CParticleCollection *pParticles, int nParticle, const Vector &glow )
{
	*pParticles->GetFloatAttributePtrForWrite( TERROR_PARTICLE_GLOW_RED, nParticle ) = glow.x;
	*pParticles->GetFloatAttributePtrForWrite( TERROR_PARTICLE_GLOW_GREEN, nParticle ) = glow.y;
	*pParticles->GetFloatAttributePtrForWrite( TERROR_PARTICLE_GLOW_BLUE, nParticle ) = glow.z;
}

inline Vector TerrorReadParticleNormal( CParticleCollection *pParticles, int nParticle )
{
	return Vector(
		*pParticles->GetFloatAttributePtr( TERROR_PARTICLE_NORMAL_X, nParticle ),
		*pParticles->GetFloatAttributePtr( TERROR_PARTICLE_NORMAL_Y, nParticle ),
		*pParticles->GetFloatAttributePtr( TERROR_PARTICLE_NORMAL_Z, nParticle ) );
}

inline void TerrorWriteParticleNormal( CParticleCollection *pParticles, int nParticle, const Vector &normal )
{
	*pParticles->GetFloatAttributePtrForWrite( TERROR_PARTICLE_NORMAL_X, nParticle ) = normal.x;
	*pParticles->GetFloatAttributePtrForWrite( TERROR_PARTICLE_NORMAL_Y, nParticle ) = normal.y;
	*pParticles->GetFloatAttributePtrForWrite( TERROR_PARTICLE_NORMAL_Z, nParticle ) = normal.z;
}

#endif // TERROR_PARTICLE_ATTRIBUTES_H
