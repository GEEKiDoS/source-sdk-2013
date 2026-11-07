//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef PRECACHE_REGISTER_H
#define PRECACHE_REGISTER_H
#pragma once

#ifdef L4D2
// Numeric identities are the native dependency_file type table (S786E74).
enum ResourceType_t
{
	RESOURCETYPE_VGUI_RESOURCE = 0,
	RESOURCETYPE_MATERIAL,
	RESOURCETYPE_MODEL,
	RESOURCETYPE_SCRIPTSOUND,
	RESOURCETYPE_PARTICLE_SYSTEM,
	RESOURCETYPE_ENTITY,
	RESOURCETYPE_DECAL,
	RESOURCETYPE_PMATERIAL,
	RESOURCETYPE_DEPENDENCY_FILE,
	RESOURCETYPE_GAME_MATERIAL_DECALS,
	RESOURCETYPE_PHYSICS_GAMESOUNDS,
	RESOURCETYPE_SHARED
};
enum PrecacheSystem_t
{
	PRECACHE_SYSTEM_CLIENT = 0,
	PRECACHE_SYSTEM_SERVER = 1,
	PRECACHE_SYSTEM_UI = 2,
	PRECACHE_SYSTEM_EFFECTS = 3,
	PRECACHE_SYSTEM_SHARED = 4,
	PRECACHE_SYSTEM_COUNT = 5
};

abstract_class IPrecacheHandler
{
public:
	// The x86 output is a DWORD: an index except PMATERIAL, which is a pointer.
	// void* preserves both native meanings without narrowing an x64 pointer.
	virtual void Precache( ResourceType_t type, const char *name, bool precache,
		int resourceList, void *output = NULL ) = 0;
};
abstract_class IResourcePrecacher
{
public:
	virtual void Precache( IPrecacheHandler *handler, bool precache,
		int resourceList, bool ignoreConditionals ) = 0;
	virtual int GetSystem() const = 0;
	virtual const char *GetName() const = 0;
	virtual IResourcePrecacher *GetNext() const = 0;
	virtual void SetNext( IResourcePrecacher *next ) = 0;
};
class CBaseResourcePrecacher : public IResourcePrecacher
{
public:
	CBaseResourcePrecacher( int system, const char *name, unsigned int nativeOrder );
	virtual int GetSystem() const OVERRIDE { return m_System; }
	virtual const char *GetName() const OVERRIDE { return m_Name; }
	virtual IResourcePrecacher *GetNext() const OVERRIDE { return m_Next; }
	virtual void SetNext( IResourcePrecacher *next ) OVERRIDE { m_Next = next; }
private:
	int m_System;
	const char *m_Name;
	IResourcePrecacher *m_Next;
	// Original .CRT initializer-pointer RVA: prepend order, independent of the
	// target linker's translation-unit ordering. No resource list allocation.
	unsigned int m_NativeOrder;
};
class CPrecacheHandler : public IPrecacheHandler
{
public:
	virtual void Precache( ResourceType_t type, const char *name, bool precache,
		int resourceList, void *output = NULL ) OVERRIDE;
private:
	void PrecacheDependencyFile( const char *name, bool precache, int resourceList );
	void PrecachePhysicsGameSounds( const char *name, bool precache, int resourceList );
};
class CPrecacheRegister
{
public:
	static void Precache();
	static void PrecacheSystem( IPrecacheHandler *handler, int system,
		const char *name, bool precache, int resourceList, bool buildResourceList );
};

void PrecacheEffect( const char *name );
#else
// Unrelated SDK games retain their original ABI, macros and prepend traversal.
#define PRECACHE_REGISTER_FN(functionName) static CPrecacheRegister precache_function_##functionName(functionName, 0);
#define PRECACHE_WEAPON_REGISTER(className) static CPrecacheRegister precache_weapon_##className(&CPrecacheRegister::PrecacheFn_Other, #className)
#define PRECACHE_REGISTER(className) static CPrecacheRegister precache_other_##className(&CPrecacheRegister::PrecacheFn_Other, #className)
class CPrecacheRegister
{
public:
	typedef void (*PrecacheFn)(void *pUser);
	CPrecacheRegister(PrecacheFn fn, const void *pUser);
	PrecacheFn m_Fn;
	void *m_pUser;
	CPrecacheRegister *m_pNext;
	static void Precache();
	static void PrecacheFn_Other(void *pUser);
};
#endif
#ifdef L4D2
class IGameSystem;
IGameSystem *PrecacheRegisterSystem();
#endif
#endif // PRECACHE_REGISTER_H
