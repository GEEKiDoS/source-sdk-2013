//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PRECACHE_MEMBERSHIP_H
#define TERROR_PRECACHE_MEMBERSHIP_H
#ifdef _WIN32
#pragma once
#endif
#include "igamesystem.h"
#include "tier1/utlmap.h"
class CModelPrecacheSystem : public CAutoGameSystem
{
public:
 CModelPrecacheSystem();
 bool ShouldPrecacheModelComponents( int modelIndex );
 virtual void LevelShutdownPreEntity() OVERRIDE;
private:
 CUtlMap<int, unsigned int> m_models;
};
class CScenePrecacheSystem : public CAutoGameSystem
{
public:
 CScenePrecacheSystem();
 bool ShouldPrecacheInstancedScene( const char *scene );
 virtual void LevelShutdownPreEntity() OVERRIDE;
private:
 CUtlMap<int, unsigned int> m_scenes;
};
extern CModelPrecacheSystem g_ModelPrecacheSystem;
extern CScenePrecacheSystem g_ScenePrecacheSystem;
#endif
