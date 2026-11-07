//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PRECACHE_CLIENT_H
#define TERROR_PRECACHE_CLIENT_H
#pragma once

class IPrecacheHandler;
void PrecacheParticleMaterial( const char *pName, void *pOutput );
void PrecacheEffect( const char *pName );
void PrecacheTerrorImpactResources( IPrecacheHandler *pHandler, bool precache, int resourceList );

extern int g_nInfectedBloodGibIndex;
extern int g_nChainsawBloodFirstPersonIndex;
extern int g_nChainsawBloodThirdPersonIndex;
extern int g_nChainsawSmokeFirstPersonIndex;
extern int g_nChainsawSmokeThirdPersonIndex;
extern int g_nMolotovHeldIndex;
extern int g_nPipeBombFuseIndex;
#endif // TERROR_PRECACHE_CLIENT_H
