//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Mounted common-infected populations. Original server RVAs 0x3FFAD0,
// 0x401E70, 0x402130 and the model-list portion of 0x402F40.
//
//=============================================================================//

#ifndef INFECTED_POPULATION_H
#define INFECTED_POPULATION_H
#ifdef _WIN32
#pragma once
#endif

class KeyValues;
KeyValues *GetInfectedPopulationDefinition();

// Called by Infected::Precache. Lists retain mounted population/model order.
void PrecacheInfectedModels();
void ResetInfectedPopulation();
// The Director owns population data across levels and releases it on destruction.
void ShutdownInfectedPopulation();

// Called by the actor only when it has no explicit model. Pass the name of
// its underlying TerrorNavArea population override, or NULL to use z_population. The selected
// low-violence list uses the actual DirectorSessionManager aggregate, not a local engine flag.
// Returns NULL when the selected precache list is empty; the actor removes
// itself in that case. The returned string has the native pooled-string lifetime.
const char *SelectInfectedModel( const char *population );

#endif // INFECTED_POPULATION_H
