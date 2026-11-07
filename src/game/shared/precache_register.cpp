//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "precache_register.h"
#ifdef L4D2
#include "filesystem.h"
#include "tier1/KeyValues.h"
#include "vphysics_interface.h"
#include "igamesystem.h"
#ifdef CLIENT_DLL
#include "l4d2/content/terror_precache_client.h"
#include "particlemgr.h"
#include "materialsystem/imaterialsystem.h"
#endif
#endif
#include "tier0/memdbgon.h"

#ifdef L4D2
static IResourcePrecacher *s_Precachers[PRECACHE_SYSTEM_COUNT];

CBaseResourcePrecacher::CBaseResourcePrecacher( int system, const char *name, unsigned int nativeOrder )
	: m_System( system ), m_Name( name ), m_Next( NULL ), m_NativeOrder( nativeOrder )
{
	Assert( system >= 0 && system < PRECACHE_SYSTEM_COUNT );
	IResourcePrecacher *previous = NULL;
	IResourcePrecacher *next = s_Precachers[system];
	while ( next && static_cast<CBaseResourcePrecacher *>( next )->m_NativeOrder > nativeOrder )
	{
		previous = next;
		next = next->GetNext();
	}
	m_Next = next;
	if ( previous ) previous->SetNext( this );
	else s_Precachers[system] = this;
}

// Linked stack records require no allocation, and keep only the active path.
// The standing cyclic-map-data rule skips only a re-entered resource owner.
struct ActiveResourcePrecacher_t
{
	IResourcePrecacher *precacher;
	ActiveResourcePrecacher_t *previous;
};
static ActiveResourcePrecacher_t *s_ActivePrecacher;

void CPrecacheRegister::PrecacheSystem( IPrecacheHandler *handler, int system,
	const char *name, bool precache, int resourceList, bool /*buildResourceList*/ )
{
	Assert( system >= 0 && system < PRECACHE_SYSTEM_COUNT );
	for ( IResourcePrecacher *current = s_Precachers[system]; current; current = current->GetNext() )
	{
		if ( name && Q_stricmp( name, current->GetName() ) ) continue;
		ActiveResourcePrecacher_t *active = s_ActivePrecacher;
		while ( active && active->precacher != current ) active = active->previous;
		if ( active ) continue;
		ActiveResourcePrecacher_t record = { current, s_ActivePrecacher };
		s_ActivePrecacher = &record;
		// Datacache 10990 always passes FALSE here. buildResourceList only
		// controls the absent access-control module, never these conditionals.
		current->Precache( handler, precache, resourceList, false );
		s_ActivePrecacher = record.previous;
	}
}

static CPrecacheHandler s_PrecacheHandler;
void CPrecacheRegister::Precache()
{
#ifdef CLIENT_DLL
	PrecacheSystem( &s_PrecacheHandler, PRECACHE_SYSTEM_CLIENT, NULL, true, -1, false );
#else
	PrecacheSystem( &s_PrecacheHandler, PRECACHE_SYSTEM_SERVER, NULL, true, -1, false );
#endif
}

void CPrecacheHandler::Precache( ResourceType_t type, const char *name, bool precache,
	int resourceList, void *output )
{
#ifdef CLIENT_DLL
	if ( type == RESOURCETYPE_PMATERIAL )
	{
		if ( output ) *static_cast<PMaterialHandle *>( output ) = NULL;
		if ( precache ) PrecacheParticleMaterial( name, output );
		return;
	}
#endif
	int *index = static_cast<int *>( output );
	if ( index ) *index = 0;
	if ( !precache ) return;
	// S195750 / C17A1C0. VResourceAccessControl001 AddResource is the only
	// omitted step in kinds 1..4; that engine module does not exist on target.
	switch ( type )
	{
	case RESOURCETYPE_MATERIAL:
		PrecacheMaterial( name );
		if ( index ) *index = GetMaterialIndex( name );
		break;
	case RESOURCETYPE_MODEL:
		{
			int model = CBaseEntity::PrecacheModel( name );
			if ( index ) *index = model;
		}
		break;
	case RESOURCETYPE_SCRIPTSOUND:
		{
			// Native handler sign-extends the original signed 16-bit handle.
			short sound = static_cast<short>( CBaseEntity::PrecacheScriptSound( name ) );
			if ( index ) *index = sound;
		}
		break;
	case RESOURCETYPE_PARTICLE_SYSTEM:
		PrecacheParticleSystem( name );
		if ( index ) *index = GetParticleSystemIndex( name );
		break;
	case RESOURCETYPE_ENTITY:
		UTIL_PrecacheOther( name );
		break;
	case RESOURCETYPE_DECAL:
		{
			int decal = UTIL_PrecacheDecal( name, true );
			if ( index ) *index = decal;
		}
		break;
	case RESOURCETYPE_DEPENDENCY_FILE:
		PrecacheDependencyFile( name, precache, resourceList );
		break;
	case RESOURCETYPE_PHYSICS_GAMESOUNDS:
		PrecachePhysicsGameSounds( name, precache, resourceList );
		break;
	case RESOURCETYPE_SHARED:
		CPrecacheRegister::PrecacheSystem( this, PRECACHE_SYSTEM_SHARED, name,
			precache, resourceList, false );
		break;
	default:
		// Native types 0 and 9 do nothing; server type 7 also does nothing.
		break;
	}
}

struct ActiveDependencyFile_t
{
	const char *name;
	ActiveDependencyFile_t *previous;
};
static ActiveDependencyFile_t *s_ActiveDependencyFile;

void CPrecacheHandler::PrecacheDependencyFile( const char *name, bool precache, int resourceList )
{
	for ( ActiveDependencyFile_t *active = s_ActiveDependencyFile; active; active = active->previous )
		if ( !Q_stricmp( active->name, name ) ) return;
	ActiveDependencyFile_t record = { name, s_ActiveDependencyFile };
	s_ActiveDependencyFile = &record;
	KeyValues *resources = new KeyValues( "ResourceFile" );
	if ( resources->LoadFromFile( filesystem, name, "GAME" ) )
	{
		static const char *const resourceTypes[] =
		{
			"vgui_resource", "material", "model", "scriptsound", "particle_system",
			"entity", "decal", "pmaterial", "dependency_file", "game_material_decals",
			"physics_gamesounds", "shared"
		};
		for ( KeyValues *resource = resources->GetFirstSubKey(); resource; resource = resource->GetNextKey() )
		{
			const char *type = resource->GetName();
			const char *value = resource->GetString();
			if ( !type[0] || !value[0] ) continue;
			int kind;
			for ( kind = 0; kind < ARRAYSIZE( resourceTypes ); ++kind )
				if ( !Q_stricmp( type, resourceTypes[kind] ) ) break;
			if ( kind == ARRAYSIZE( resourceTypes ) )
			{
				Warning( "Error in precache file \"%s\":\n", name );
				Warning( "\tUnknown resource type specified \"%s\", value \"%s\"\n", type, value );
				continue;
			}
			Precache( static_cast<ResourceType_t>( kind ), value, precache, resourceList, NULL );
		}
	}
	else Warning( "Can't open %s for client precache info.", name );
	resources->deleteThis();
	s_ActiveDependencyFile = record.previous;
}

void CPrecacheHandler::PrecachePhysicsGameSounds( const char *name, bool precache, int resourceList )
{
	if ( !precache ) return;
	bool bulletSounds = !Q_stricmp( name, "BulletSounds" );
	bool stepSounds = !Q_stricmp( name, "StepSounds" );
	bool impactSounds = !Q_stricmp( name, "PhysicsImpactSounds" );
	for ( int i = 0; i < physprops->SurfacePropCount(); ++i )
	{
		const surfacesoundnames_t &sounds = physprops->GetSurfaceData( i )->sounds;
		if ( bulletSounds )
			Precache( RESOURCETYPE_SCRIPTSOUND, physprops->GetString( sounds.bulletImpact ), precache, resourceList );
		if ( stepSounds )
		{
			Precache( RESOURCETYPE_SCRIPTSOUND, physprops->GetString( sounds.stepleft ), precache, resourceList );
			Precache( RESOURCETYPE_SCRIPTSOUND, physprops->GetString( sounds.stepright ), precache, resourceList );
		}
		if ( impactSounds )
		{
			Precache( RESOURCETYPE_SCRIPTSOUND, physprops->GetString( sounds.impactSoft ), precache, resourceList );
			Precache( RESOURCETYPE_SCRIPTSOUND, physprops->GetString( sounds.impactHard ), precache, resourceList );
			Precache( RESOURCETYPE_SCRIPTSOUND, physprops->GetString( sounds.scrapeSmooth ), precache, resourceList );
			Precache( RESOURCETYPE_SCRIPTSOUND, physprops->GetString( sounds.scrapeRough ), precache, resourceList );
			Precache( RESOURCETYPE_SCRIPTSOUND, physprops->GetString( sounds.rolling ), precache, resourceList );
			Precache( RESOURCETYPE_SCRIPTSOUND, physprops->GetString( sounds.breakSound ), precache, resourceList );
			Precache( RESOURCETYPE_SCRIPTSOUND, physprops->GetString( sounds.strainSound ), precache, resourceList );
		}
	}
}

// Keep the ONE explicit M5 driver, added immediately after GameString/Physics.
class CPrecacheRegisterGameSystem : public CBaseGameSystem
{
public:
	virtual const char *Name() OVERRIDE { return "PrecacheRegister"; }
	virtual void LevelInitPreEntity() OVERRIDE
	{
		// Host_Error can abandon these stack records without running their pops.
		// A new level must start with no active resource/dependency path.
		s_ActivePrecacher = NULL;
		s_ActiveDependencyFile = NULL;
		COM_TimestampedLog( "LevelInitPreEntity - PreCache - Start" );
		CPrecacheRegister::Precache();
		COM_TimestampedLog( "LevelInitPreEntity - PreCache - Finish" );
#ifdef CLIENT_DLL
		COM_TimestampedLog( "LevelInitPreEntity - CacheUsedMaterials - Start" );
		materials->CacheUsedMaterials();
		COM_TimestampedLog( "LevelInitPreEntity - CacheUsedMaterials - Finish" );
#endif
	}
	virtual void LevelShutdownPostEntity() OVERRIDE
	{
		// Release callbacks run after the engine has unwound an aborted precache.
		// Never traverse the stack-backed paths left by that nonlocal exit.
		s_ActivePrecacher = NULL;
		s_ActiveDependencyFile = NULL;
		// S1953A0 / C179E10 release handler-bound registrations after deletion.
		// The target has no VPrecacheSystem module; release each game-owned
		// list through the native precache=false contract (no resource loads).
		for ( int system = 0; system < PRECACHE_SYSTEM_COUNT; ++system )
			CPrecacheRegister::PrecacheSystem( &s_PrecacheHandler, system,
				NULL, false, -1, false );
		// Native VResourceAccessControl001 level release: absent target module.
	}
};
static CPrecacheRegisterGameSystem s_PrecacheRegisterGameSystem;
IGameSystem *PrecacheRegisterSystem() { return &s_PrecacheRegisterGameSystem; }
#else
static CPrecacheRegister	*g_pPrecacheRegisters = 0;

CPrecacheRegister::CPrecacheRegister(PrecacheFn fn, const void *pUser)
{
	m_Fn = fn;
	m_pUser = const_cast<void *>(pUser);

	m_pNext = g_pPrecacheRegisters;
	g_pPrecacheRegisters = this;
}


void CPrecacheRegister::Precache()
{
	for(CPrecacheRegister *pCur=g_pPrecacheRegisters; pCur; pCur=pCur->m_pNext)
	{
		pCur->m_Fn(pCur->m_pUser);
	}
}


void CPrecacheRegister::PrecacheFn_Other(void *pUser)
{
	UTIL_PrecacheOther((const char*)pUser);
}




#endif
