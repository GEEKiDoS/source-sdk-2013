//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/content/terror_precache_membership.h"
#include <ctype.h>
#include "tier0/memdbgon.h"
CModelPrecacheSystem g_ModelPrecacheSystem;
CScenePrecacheSystem g_ScenePrecacheSystem;
CModelPrecacheSystem::CModelPrecacheSystem() : CAutoGameSystem("CModelPrecacheSystem"), m_models(DefLessFunc(int)) {}
CScenePrecacheSystem::CScenePrecacheSystem() : CAutoGameSystem("CScenePrecacheSystem"), m_scenes(DefLessFunc(int)) {}
// Native S06B300: admission precedes model validity/type checks; a miss is retained too.
bool CModelPrecacheSystem::ShouldPrecacheModelComponents( int modelIndex )
{
 int index=m_models.Find(modelIndex);
 if(index!=m_models.InvalidIndex())
 {
  ++m_models[index];
  return false;
 }
 m_models.Insert(modelIndex,0u);
 return true;
}
// Native S1CCA70 / S5011F0: HASH ONLY, deliberately no string collision check.
bool CScenePrecacheSystem::ShouldPrecacheInstancedScene( const char *scene )
{
 unsigned int hash=0xAAAAAAAAu;
 for(const unsigned char *character=(const unsigned char *)scene;*character;++character)
  hash=(unsigned char)tolower(*character)+33u*hash;
 int key=(int)hash;
 int index=m_scenes.Find(key);
 if(index!=m_scenes.InvalidIndex())
 {
  ++m_scenes[index];
  return false;
 }
 m_scenes.Insert(key,0u);
 return true;
}
// Native vtable slot6 is LevelShutdownPreEntity; slot12 is the deleting destructor.
void CModelPrecacheSystem::LevelShutdownPreEntity() { m_models.Purge(); }
void CScenePrecacheSystem::LevelShutdownPreEntity() { m_scenes.Purge(); }
