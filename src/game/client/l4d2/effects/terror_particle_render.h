//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PARTICLE_RENDER_H
#define TERROR_PARTICLE_RENDER_H
#ifdef _WIN32
#pragma once
#endif

// Replace the native animated-sprite factory in place before PCF definitions load.
void RegisterTerrorParticleRenderers();

#endif // TERROR_PARTICLE_RENDER_H
