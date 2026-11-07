//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_MATERIAL_PROXY_STATE_H
#define TERROR_MATERIAL_PROXY_STATE_H
#ifdef _WIN32
#pragma once
#endif

class C_BaseEntity;

// Client corpses retain the blend at handoff; they do not keep a live player's timer.
struct TerrorMaterialProxyState_t
{
	TerrorMaterialProxyState_t() : m_flIT( 0.0f ), m_flBloodyHands( 0.0f ) {}

	float m_flIT;
	float m_flBloodyHands;
};

// Returns false and clears the state for entities with no corresponding state.
bool GetTerrorEntityMaterialState( C_BaseEntity *pEntity, TerrorMaterialProxyState_t &state );

// Implemented with the native L4D2 client-ragdoll subclass in c_infected.cpp.
bool GetTerrorRagdollMaterialState( C_BaseEntity *pEntity, TerrorMaterialProxyState_t &state );

#endif // TERROR_MATERIAL_PROXY_STATE_H
