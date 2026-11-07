//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PARTICLE_DEFINITION_METADATA_H
#define TERROR_PARTICLE_DEFINITION_METADATA_H
#ifdef _WIN32
#pragma once
#endif

// Game-owned PCF data missing from target PREBUILT particle-definition type.
// One CLIENT table owns records; no target library layout/ABI is extended.
struct TerrorParticleDefinitionMetadata_t
{
	TerrorParticleDefinitionMetadata_t() : m_bScreenSpace( false ), m_flAggregationRadius( 0.0f ),
		m_nMinimumFreeToAggregate( 0 ), m_nFallbackMaxCount( -1 )
	{
		m_FallbackDefinition[0] = '\0';
	}

	bool m_bScreenSpace;               // Native703, BOOL/default"0".
	float m_flAggregationRadius;       // Native752, FLOAT/default"0".
	int m_nMinimumFreeToAggregate;      // Native748, INT/default"0".
	char m_FallbackDefinition[128];     // Native496, STRING128/defaultempty.
	int m_nFallbackMaxCount;            // Native624, INT/default"-1".
};

// Descriptor source10369090: screen10369874, aggregation10369900,
// minimum-free10369978, fallback-name10369322, max-count1036936E.
#endif
