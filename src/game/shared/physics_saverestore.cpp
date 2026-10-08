//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"

#include "utlpriorityqueue.h"
#include "utlmap.h"
#include "utlvector.h"
#include "isaverestore.h"
#include "physics.h"
#include "physics_saverestore.h"
#include "saverestoretypes.h"
#include "gamestringpool.h"
#include "datacache/imdlcache.h"
#if defined( _WIN64 )
#include <windows.h>
#endif

#if !defined( CLIENT_DLL )
#include "entitylist.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------

// Version 5 truncated the world identity; the intermediate Win64 version 6
// still used the provider's broken 32-bit pointer ops. Neither is recoverable.
#ifdef PLATFORM_64BITS
static short PHYS_SAVE_RESTORE_VERSION = 7;
#else
static short PHYS_SAVE_RESTORE_VERSION = 5;
#endif

class CPhysWorldObjectSaveRestoreOps : public CClassPtrSaveRestoreOps
{
public:
	virtual void Save( const SaveRestoreFieldInfo_t &fieldInfo, ISave *pSave )
	{
		pSave->WriteData( (const char *)fieldInfo.pField, sizeof( IPhysicsObject * ) );
	}

	virtual void Restore( const SaveRestoreFieldInfo_t &fieldInfo, IRestore *pRestore )
	{
		pRestore->ReadData( (char *)fieldInfo.pField, sizeof( IPhysicsObject * ), sizeof( IPhysicsObject * ) );
	}
};

static CPhysWorldObjectSaveRestoreOps g_PhysWorldObjectSaveRestoreOps;

#if defined( _WIN64 )
namespace
{
typedef CUtlMap<void *, void *> NativePhysicsPointerMap_t;
typedef UtlRBTreeNode_t<NativePhysicsPointerMap_t::Node_t, unsigned short> NativePhysicsPointerNode_t;
COMPILE_TIME_ASSERT( sizeof( CUtlVector<void *> ) == 32 );
COMPILE_TIME_ASSERT( sizeof( NativePhysicsPointerMap_t ) == 40 );
COMPILE_TIME_ASSERT( sizeof( NativePhysicsPointerNode_t ) == 24 );
COMPILE_TIME_ASSERT( offsetof( NativePhysicsPointerNode_t, m_Data ) == 8 );
COMPILE_TIME_ASSERT( offsetof( NativePhysicsPointerMap_t::Node_t, elem ) == 8 );

static const byte *s_pNativePhysicsModule;
static const uint32 NATIVE_PHYSICS_SCALAR_OPS = 0x11CE10;
static const uint32 NATIVE_PHYSICS_VECTOR_OPS = 0x11CE18;
static const uint32 NATIVE_PHYSICS_POINTER_MAP = 0x11CE20;
static const uint32 NATIVE_PHYSICS_ENVIRONMENT_VTABLE = 0xE4BA8;
static const uint32 s_NativePhysicsSaveFunctions[] =
{
	0x67F0, 0x1D1D0, 0x67F0, 0x20E60, 0xF790, 0xF640, 0x1F6B0, 0x67F0, 0x19380, 0x23480, 0
};
static const uint32 s_NativePhysicsRestoreFunctions[] =
{
	0x67F0, 0x1D060, 0x67F0, 0x20DD0, 0xF4C0, 0xF140, 0x67F0, 0x67F0, 0x19270, 0x233D0, 0
};
COMPILE_TIME_ASSERT( ARRAYSIZE( s_NativePhysicsSaveFunctions ) == PIID_NUM_TYPES );
COMPILE_TIME_ASSERT( ARRAYSIZE( s_NativePhysicsRestoreFunctions ) == PIID_NUM_TYPES );

struct NativePhysicsBytePin_t
{
	uint32 rva;
	int size;
	byte bytes[24];
};

// SDK Base 2013 Multiplayer Win64 vphysics.dll, timestamp 67b40ef3.
// Pin the pointer strides, allocation, lookup layout and comparator as well as
// the singleton vtables. No executable memory or installed file is modified.
static const NativePhysicsBytePin_t s_NativePhysicsPins[] =
{
	{ 0x2FF70, 10, { 0x48,0x8B,0x02,0x48,0x39,0x01,0x0F,0x92,0xC0,0xC3 } },
	{ 0x2F8B0, 16, { 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48 } },
	{ 0x2F8FF, 8, { 0x48,0x8D,0x14,0x98,0x41,0xFF,0x51,0x68 } },
	{ 0x2F9DA, 4, { 0x48,0xC1,0xE1,0x02 } },
	{ 0x2FA59, 4, { 0x4C,0x8D,0x34,0xB0 } },
	{ 0x2FA6B, 21, { 0x0F,0xB7,0x1D,0xC6,0xD3,0x0E,0x00,0x49,0x8B,0x06,0x48,0x89,0x44,0x24,0x20,0x66,0x41,0x3B,0xDC,0x74,0x7F } },
	{ 0x30500, 17, { 0x40,0x56,0x41,0x54,0x41,0x56,0x48,0x83,0xEC,0x30,0x48,0x8B,0x42,0x10,0x45,0x33,0xE4 } },
	{ 0x30563, 15, { 0x0F,0xB7,0x1D,0xCE,0xC8,0x0E,0x00,0x48,0x8B,0x06,0x48,0x89,0x44,0x24,0x20 } },
	{ 0x30603, 7, { 0x48,0x8B,0x44,0xCA,0x10,0xEB,0x03 } },
	{ 0x30960, 15, { 0x48,0x89,0x5C,0x24,0x10,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0x42,0x10,0x49 } },
	{ 0x3099F, 7, { 0xFF,0x50,0x68,0x48,0x83,0xC3,0x04 } },
	{ 0x2FA90, 22, { 0x0F,0xB7,0xC3,0x48,0x83,0xC2,0x08,0x48,0x8D,0x0C,0x40,0x48,0x8D,0x3C,0xCD,0x00,0x00,0x00,0x00,0x48,0x03,0xD7 } },
	{ 0x305FC, 12, { 0x0F,0xB7,0xC3,0x48,0x8D,0x0C,0x40,0x48,0x8B,0x44,0xCA,0x10 } },
	{ 0x30900, 24, { 0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x63,0x72,0x10,0x48,0x8B,0xFA,0x83,0xFE,0x0A,0x77,0x3C,0x48,0x8B } },
	{ 0x3092E, 21, { 0x48,0x8B,0x57,0x08,0x4C,0x8D,0x05,0x17,0xC5,0x0E,0x00,0x48,0x8B,0xCF,0x49,0x8B,0x04,0xF0,0x48,0x8B,0x5C } },
	{ 0x30390, 24, { 0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20,0x48,0x63,0x72,0x10,0x48,0x8B,0xDA,0x48,0x8B } },
	{ 0x303C7, 18, { 0x48,0x8B,0x53,0x08,0x4C,0x8D,0x05,0xDE,0xCA,0x0E,0x00,0x48,0x8B,0xCB,0x41,0xFF,0x14,0xF0 } },
	{ 0x303E1, 22, { 0x48,0x8B,0x53,0x08,0x48,0x8B,0x4C,0x24,0x38,0x48,0x8B,0x12,0xE8,0x4E,0xF7,0xFF,0xFF,0x83,0xFE,0x01,0x0F,0x85 } },
	{ 0x303FB, 15, { 0x48,0x8B,0x43,0x08,0x8B,0x77,0x30,0x48,0x89,0x6C,0x24,0x30,0x48,0x8B,0x28 } },
	{ 0x304C4, 8, { 0x48,0x8B,0x47,0x20,0x48,0x89,0x2C,0xF0 } },
	{ 0x30120, 24, { 0x41,0x55,0x41,0x57,0x48,0x83,0xEC,0x48,0x33,0xC0,0x4C,0x8B,0xEA,0x44,0x8B,0xF8,0x39,0x02,0x0F,0x8E,0x78,0x01,0x00,0x00 } },
	{ 0x3015C, 19, { 0x4C,0x8D,0x72,0x10,0x49,0x8B,0x46,0xF8,0x40,0x32,0xF6,0x0F,0xB7,0x1D,0xCA,0xCC,0x0E,0x00,0x41 } },
	{ 0x30100, 24, { 0x48,0x83,0xEC,0x28,0x48,0x8D,0x0D,0x15,0xCD,0x0E,0x00,0xE8,0xB0,0x01,0x00,0x00,0xE8,0xFB,0xC8,0xFE,0xFF,0x48,0x83,0xC4 } },
};

static bool NativePhysicsVtableMatches( const byte *pModule, uint32 ops, uint32 vtable, uint32 save, uint32 restore )
{
	if ( *(const byte *const *)(pModule + ops) != pModule + vtable )
		return false;
	const uint32 functions[] = { save, restore, 0x67F0, 0x6CA0, 0x67F0 };
	for ( int i = 0; i < ARRAYSIZE( functions ); ++i )
	{
		if ( ((const byte *const *)(pModule + vtable))[i] != pModule + functions[i] )
			return false;
	}
	return true;
}

static bool NativePhysicsTableMatches( const byte *pModule, uint32 table, const uint32 *pFunctions, int count )
{
	for ( int i = 0; i < count; ++i )
	{
		const byte *pExpected = pFunctions[i] ? pModule + pFunctions[i] : NULL;
		if ( ((const byte *const *)(pModule + table))[i] != pExpected )
			return false;
	}
	return true;
}

static bool CertifyNativePhysicsPointerOps()
{
	if ( s_pNativePhysicsModule )
		return true;

	const byte *pModule = (const byte *)GetModuleHandleA( "vphysics.dll" );
	if ( !pModule )
		return false;
	const IMAGE_DOS_HEADER *pDOS = (const IMAGE_DOS_HEADER *)pModule;
	if ( pDOS->e_magic != IMAGE_DOS_SIGNATURE || pDOS->e_lfanew < (LONG)sizeof( IMAGE_DOS_HEADER ) ||
		pDOS->e_lfanew > 4096 - (LONG)sizeof( IMAGE_NT_HEADERS64 ) )
		return false;
	const IMAGE_NT_HEADERS64 *pNT = (const IMAGE_NT_HEADERS64 *)(pModule + pDOS->e_lfanew);
	if ( pNT->Signature != IMAGE_NT_SIGNATURE || pNT->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
		pNT->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
		pNT->FileHeader.TimeDateStamp != 0x67B40EF3 || pNT->OptionalHeader.SizeOfImage != 0x15D000 )
		return false;

	for ( int i = 0; i < ARRAYSIZE( s_NativePhysicsPins ); ++i )
	{
		const NativePhysicsBytePin_t &pin = s_NativePhysicsPins[i];
		if ( memcmp( pModule + pin.rva, pin.bytes, pin.size ) != 0 )
			return false;
	}
	if ( !NativePhysicsVtableMatches( pModule, NATIVE_PHYSICS_SCALAR_OPS, 0xE7710, 0x30960, 0x30500 ) ||
		!NativePhysicsVtableMatches( pModule, NATIVE_PHYSICS_VECTOR_OPS, 0xE76E8, 0x2F8B0, 0x2F920 ) ||
		*(const byte *const *)(pModule + NATIVE_PHYSICS_POINTER_MAP) != pModule + 0x2FF70 )
		return false;
	const uint32 environmentFunctions[] = { 0x30900, 0x30120, 0x30390, 0x30100 };
	if ( !NativePhysicsTableMatches( pModule, 0x11CE50, s_NativePhysicsSaveFunctions, PIID_NUM_TYPES ) ||
		!NativePhysicsTableMatches( pModule, 0x11CEB0, s_NativePhysicsRestoreFunctions, PIID_NUM_TYPES ) ||
		!NativePhysicsTableMatches( pModule, NATIVE_PHYSICS_ENVIRONMENT_VTABLE + 51 * sizeof( void * ),
			environmentFunctions, ARRAYSIZE( environmentFunctions ) ) )
		return false;

	s_pNativePhysicsModule = pModule;
	return true;
}

static void *RemapNativePhysicsPointer( void *pOldObject )
{
	// The certified provider's lookup is the same CUtlMap layout and unsigned
	// pointer comparator as this SDK. Find is read-only and uses all 64 bits.
	NativePhysicsPointerMap_t *pMap = (NativePhysicsPointerMap_t *)(s_pNativePhysicsModule + NATIVE_PHYSICS_POINTER_MAP);
	unsigned short index = pMap->Find( pOldObject );
	return index != pMap->InvalidIndex() ? (*pMap)[index] : NULL;
}

class CNativePhysicsPointerSaveRestoreOps : public CDefSaveRestoreOps
{
public:
	virtual void Save( const SaveRestoreFieldInfo_t &fieldInfo, ISave *pSave )
	{
		pSave->WriteData( (const char *)fieldInfo.pField, fieldInfo.pTypeDesc->fieldSize * (int)sizeof( void * ) );
	}

	virtual void Restore( const SaveRestoreFieldInfo_t &fieldInfo, IRestore *pRestore )
	{
		void **ppObjects = (void **)fieldInfo.pField;
		for ( int i = 0; i < fieldInfo.pTypeDesc->fieldSize; ++i )
		{
			void *pOldObject = NULL;
			pRestore->ReadData( (char *)&pOldObject, sizeof( pOldObject ), sizeof( pOldObject ) );
			ppObjects[i] = RemapNativePhysicsPointer( pOldObject );
		}
	}
};

class CNativePhysicsPointerVectorSaveRestoreOps : public CDefSaveRestoreOps
{
public:
	virtual void Save( const SaveRestoreFieldInfo_t &fieldInfo, ISave *pSave )
	{
		CUtlVector<void *> *pObjects = (CUtlVector<void *> *)fieldInfo.pField;
		int count = pObjects->Count();
		pSave->WriteInt( &count );
		if ( count )
			pSave->WriteData( (const char *)pObjects->Base(), count * (int)sizeof( void * ) );
	}

	virtual void Restore( const SaveRestoreFieldInfo_t &fieldInfo, IRestore *pRestore )
	{
		CUtlVector<void *> *pObjects = (CUtlVector<void *> *)fieldInfo.pField;
		int count = pRestore->ReadInt();
		pObjects->SetCount( count );
		for ( int i = 0; i < count; ++i )
		{
			void *pOldObject = NULL;
			pRestore->ReadData( (char *)&pOldObject, sizeof( pOldObject ), sizeof( pOldObject ) );
			(*pObjects)[i] = RemapNativePhysicsPointer( pOldObject );
		}
	}
};

static CNativePhysicsPointerSaveRestoreOps s_NativePhysicsPointerOps;
static CNativePhysicsPointerVectorSaveRestoreOps s_NativePhysicsPointerVectorOps;

static bool NativePhysicsEnvironmentMatches( IPhysicsEnvironment *pEnvironment )
{
	return pEnvironment &&
		*(const byte *const *)pEnvironment == s_pNativePhysicsModule + NATIVE_PHYSICS_ENVIRONMENT_VTABLE;
}

static bool SaveNativePhysicsObject( IPhysicsEnvironment *pEnvironment, const physsaveparams_t &params )
{
	if ( !NativePhysicsEnvironmentMatches( pEnvironment ) ||
		(unsigned)params.type >= PIID_NUM_TYPES || !s_NativePhysicsSaveFunctions[params.type] )
		return false;
	params.pSave->WriteData( (const char *)&params.pObject, sizeof( params.pObject ) );
	typedef bool (*SaveFunction_t)( const physsaveparams_t &, void * );
	SaveFunction_t save = (SaveFunction_t)(s_pNativePhysicsModule + s_NativePhysicsSaveFunctions[params.type]);
	return save( params, params.pObject );
}

static bool RestoreNativePhysicsObject( IPhysicsEnvironment *pEnvironment, const physrestoreparams_t &params )
{
	if ( !NativePhysicsEnvironmentMatches( pEnvironment ) ||
		(unsigned)params.type >= PIID_NUM_TYPES || !s_NativePhysicsRestoreFunctions[params.type] )
		return false;

	void *pOldObject = NULL;
	params.pRestore->ReadData( (char *)&pOldObject, sizeof( pOldObject ), sizeof( pOldObject ) );
	typedef bool (*RestoreFunction_t)( const physrestoreparams_t &, void ** );
	RestoreFunction_t restore = (RestoreFunction_t)(s_pNativePhysicsModule + s_NativePhysicsRestoreFunctions[params.type]);
	if ( !restore( params, params.ppObject ) )
		return false;

	NativePhysicsPointerMap_t *pMap = (NativePhysicsPointerMap_t *)(s_pNativePhysicsModule + NATIVE_PHYSICS_POINTER_MAP);
	pMap->Insert( pOldObject, *params.ppObject );
	if ( params.type == PIID_IPHYSICSOBJECT )
	{
		// Preserve the native environment's restored-object post-processing list.
		CUtlVector<void *> *pRestoredObjects = (CUtlVector<void *> *)((byte *)pEnvironment + 0x20);
		pRestoredObjects->AddToTail( *params.ppObject );
	}
	return true;
}
} // namespace
#endif

bool IsPhysSaveRestoreSupported()
{
#if defined( _WIN64 )
	return CertifyNativePhysicsPointerOps();
#elif defined( PLATFORM_64BITS )
	return false; // No certified provider profile for another 64-bit platform.
#else
	return true;
#endif
}

ISaveRestoreOps *ResolvePhysSaveRestoreOps( ISaveRestoreOps *pOps )
{
#if defined( _WIN64 )
	if ( s_pNativePhysicsModule )
	{
		if ( (const byte *)pOps == s_pNativePhysicsModule + NATIVE_PHYSICS_SCALAR_OPS )
			return &s_NativePhysicsPointerOps;
		if ( (const byte *)pOps == s_pNativePhysicsModule + NATIVE_PHYSICS_VECTOR_OPS )
			return &s_NativePhysicsPointerVectorOps;
	}
#endif
	return pOps;
}

struct PhysBlockHeader_t
{
	int				nSaved;
	IPhysicsObject	*pWorldObject;

	inline void Clear()
	{
		nSaved = 0;
		pWorldObject = 0;
	}

	DECLARE_SIMPLE_DATADESC();
};
BEGIN_SIMPLE_DATADESC( PhysBlockHeader_t )
	DEFINE_FIELD( nSaved,	FIELD_INTEGER ),
	// Opaque old address: an identity key, never an entity or string pointer.
	DEFINE_CUSTOM_FIELD( pWorldObject, &g_PhysWorldObjectSaveRestoreOps ),
END_DATADESC()

#if defined(_STATIC_LINKED) && defined(CLIENT_DLL)
const char *g_ppszPhysTypeNames[PIID_NUM_TYPES] =
{
	"Unknown",
	"IPhysicsObject",
	"IPhysicsFluidController",
	"IPhysicsSpring",
	"IPhysicsConstraintGroup",
	"IPhysicsConstraint",
	"IPhysicsShadowController",
	"IPhysicsPlayerController",
	"IPhysicsMotionController",
	"IPhysicsVehicleController",
};
#endif

//-----------------------------------------------------------------------------

struct BBox_t
{
	Vector mins, maxs;
};

struct Sphere_t
{
	float radius;
};


struct PhysObjectHeader_t
{
	PhysObjectHeader_t()
	{
		memset( this, 0, sizeof(*this) );
	}

	PhysInterfaceId_t 	type;
	EHANDLE				hEntity;
	string_t			fieldName;
	int 				nObjects;
	string_t			modelName;
	BBox_t				bbox;
	Sphere_t			sphere;
	int					iCollide;
	
	DECLARE_SIMPLE_DATADESC();
};

BEGIN_SIMPLE_DATADESC( PhysObjectHeader_t )
  	DEFINE_FIELD( type,			FIELD_INTEGER ),
  	DEFINE_FIELD( hEntity,		FIELD_EHANDLE ),
  	DEFINE_FIELD( fieldName,	FIELD_STRING ),
  	DEFINE_FIELD( nObjects,		FIELD_INTEGER ),
  	DEFINE_FIELD( modelName,	FIELD_STRING ),

	// Silence, Classcheck!
	// DEFINE_FIELD( bbox, BBox_t ),
	// DEFINE_FIELD( sphere, Sphere_t ),

  	DEFINE_FIELD( bbox.mins,	FIELD_VECTOR ),
  	DEFINE_FIELD( bbox.maxs,	FIELD_VECTOR ),
  	DEFINE_FIELD( sphere.radius, FIELD_FLOAT ),
  	DEFINE_FIELD( iCollide,		FIELD_INTEGER ),
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose:	The central manager of physics save/load
//

class CPhysSaveRestoreBlockHandler : public CDefSaveRestoreBlockHandler, 
									 public IPhysSaveRestoreManager
#if !defined( CLIENT_DLL )
									 , public IEntityListener
#endif
{
	struct QueuedItem_t;
public:
	CPhysSaveRestoreBlockHandler()
	{
		m_QueuedSaves.SetLessFunc( SaveQueueFunc );
		SetDefLessFunc( m_QueuedRestores );
		SetDefLessFunc( m_PhysObjectModels );
		SetDefLessFunc( m_PhysObjectCustomModels );
		SetDefLessFunc( m_PhysCollideBBoxModels );
	}

	const char *GetBlockName()
	{
		return "Physics";
	}

	bool IsSaveRestoreCompatible() const
	{
		return m_fDoLoad;
	}

	//---------------------------------

	virtual void PreSave( CSaveRestoreData * ) 
	{
		m_blockHeader.Clear();
		m_fSaveSupported = IsPhysSaveRestoreSupported();
		if ( !m_fSaveSupported )
		{
			Warning( "Cannot save game physics: this provider is not certified for full-width save/restore. Saving is unavailable with this provider.\n" );
		}
	}
	
	//---------------------------------

	virtual void Save( ISave *pSave ) 
	{
		if ( !m_fSaveSupported )
			return;

		m_blockHeader.pWorldObject = g_PhysWorldObject;
		m_blockHeader.nSaved = m_QueuedSaves.Count();

		while ( m_QueuedSaves.Count() )
		{
			const QueuedItem_t &item = m_QueuedSaves.ElementAtHead();
			
			CBaseEntity *pOwner = item.header.hEntity.Get();
			
			if ( pOwner )
			{
				pSave->WriteAll( &item.header );
				pSave->StartBlock(); //  Need block here in case entity is NULL on load
				if ( item.header.nObjects )
				{
					for ( int i = 0; i < item.header.nObjects; i++ )
					{
						// Starting a block here allows the implementation of any individual physics
						// class save/load to change non-trivially while retaining the overall
						// integrity of the savefile.
						pSave->StartBlock();
						SavePhysicsObject( pSave, pOwner, item.ppPhysObj[i], item.header.type );
						pSave->EndBlock();
					}
				}
				// else, it will simply be recreated on restore
				pSave->EndBlock();
			}
			m_QueuedSaves.RemoveAtHead();
		}
	}
	
	//---------------------------------

	virtual void WriteSaveHeaders( ISave *pSave )
	{
		short version = m_fSaveSupported ? PHYS_SAVE_RESTORE_VERSION : 0;
		pSave->WriteShort( &version );
		pSave->WriteAll( &m_blockHeader );
	}
	
	//---------------------------------

	virtual void PostSave() 
	{
		m_QueuedSaves.Purge();
	}
	
	//---------------------------------

	virtual void PreRestore() 
	{
		m_fDoLoad = false;
		m_blockHeader.Clear();

#if !defined( CLIENT_DLL )
		gEntList.AddListenerEntity( this );
#endif

		// UNDONE: This never runs!!!!
		if ( physenv )
		{
			physprerestoreparams_t params;
			params.recreatedObjectCount = 0;
			physenv->PreRestore( params );
		}
	}
	
	//---------------------------------

	virtual void ReadRestoreHeaders( IRestore *pRestore )
	{
		short version = pRestore->ReadShort();
		if ( !IsPhysSaveRestoreSupported() )
		{
			m_fDoLoad = false;
			Warning( "Cannot load save: this physics provider is not certified for full-width save/restore. Saving and loading are unavailable with this provider.\n" );
			return;
		}
		m_fDoLoad = ( version == PHYS_SAVE_RESTORE_VERSION );
		if ( !m_fDoLoad )
		{
			Warning( "Cannot load save: physics format %d is incompatible with format %d. Create a new save with this build.\n",
				version, PHYS_SAVE_RESTORE_VERSION );
			return;
		}

		pRestore->ReadAll( &m_blockHeader );
	}

	//---------------------------------
	
	virtual void Restore( IRestore *pRestore, bool ) 
	{
		if ( m_fDoLoad )
		{
			if ( physenv )
			{
				physprerestoreparams_t params;
				params.recreatedObjectCount = 1;
				params.recreatedObjectList[0].pNewObject = g_PhysWorldObject;
				params.recreatedObjectList[0].pOldObject = m_blockHeader.pWorldObject;
				physenv->PreRestore( params );
			}

			PhysObjectHeader_t header;

			while ( m_blockHeader.nSaved-- )
			{
				pRestore->ReadAll( &header );
				pRestore->StartBlock();
				
				if ( header.hEntity != NULL )
				{
					RestoreBlock( pRestore, header );
				}

				pRestore->EndBlock();
			}
		}
	}
	
	//---------------------------------
	
	void RestoreBlock( IRestore *pRestore, const PhysObjectHeader_t &header ) 
	{
		CBaseEntity *  pOwner  = header.hEntity.Get();
		unsigned short iQueued = m_QueuedRestores.Find( pOwner );
		
		if ( iQueued != m_QueuedRestores.InvalidIndex() )
		{
			MDLCACHE_CRITICAL_SECTION();
			if ( pOwner->ShouldSavePhysics() && header.nObjects > 0 )
			{
				QueuedItem_t *pItem = m_QueuedRestores[iQueued]->FindItem( header.fieldName );
				
				if ( pItem )
				{
					int nObjects = MIN( header.nObjects, pItem->header.nObjects );
					if ( pItem->header.type == PIID_IPHYSICSOBJECT && nObjects == 1 )
					{
						RestorePhysicsObjectAndModel( pRestore, header, pItem, nObjects );
					}
					else
					{
						void **ppPhysObj = pItem->ppPhysObj;
						
						for ( int i = 0; i < nObjects; i++ )
						{
							pRestore->StartBlock();
							RestorePhysicsObject( pRestore, header, ppPhysObj + i );
							pRestore->EndBlock();
							if ( header.type == PIID_IPHYSICSMOTIONCONTROLLER )
							{
								void *pObj = ppPhysObj[i];
								IPhysicsMotionController *pController = (IPhysicsMotionController *)pObj;
								if ( pController )
								{
									// If the entity is the motion callback handler, then automatically set it
									// NOTE: This is usually the case
									IMotionEvent *pEvent = dynamic_cast<IMotionEvent *>(pOwner);
									if ( pEvent )
									{
										pController->SetEventHandler( pEvent );
									}
								}
							}
						}
					}
				}
			}
			else
				pOwner->CreateVPhysics();
		}
	}
	
	
	//---------------------------------

	void RestorePhysicsObjectAndModel( IRestore *pRestore, const PhysObjectHeader_t &header, CPhysSaveRestoreBlockHandler::QueuedItem_t *pItem, int nObjects )
	{
		if ( nObjects == 1 )
		{
			pRestore->StartBlock();
			
			CPhysCollide *pPhysCollide   = NULL;
			int 		  modelIndex 	 = -1;
			bool 		  fCustomCollide = false;
			
			if ( header.modelName != NULL_STRING )
			{
				CBaseEntity *pGlobalEntity = header.hEntity;
#if !defined( CLIENT_DLL )
				if ( NULL_STRING != pGlobalEntity->m_iGlobalname )
				{
					modelIndex = pGlobalEntity->GetModelIndex();
				}
				else
#endif
				{
					modelIndex = modelinfo->GetModelIndex( STRING( header.modelName ) );
					pGlobalEntity = NULL;
				}

				if ( modelIndex != -1 )
				{
					vcollide_t *pCollide = modelinfo->GetVCollide( modelIndex );
					if ( pCollide )
					{
						if ( pCollide->solidCount > 0 && pCollide->solids && header.iCollide < pCollide->solidCount )
							pPhysCollide = pCollide->solids[header.iCollide];
					}
				}
			}
			else if ( header.bbox.mins != vec3_origin || header.bbox.maxs != vec3_origin )
			{
				pPhysCollide = PhysCreateBbox( header.bbox.mins, header.bbox.maxs );
				fCustomCollide = true;
			}
			else if ( header.sphere.radius != 0 )
			{
				// HACKHACK: Handle spheres here!!!
				if ( !(*pItem->ppPhysObj) )
				{
					RestorePhysicsObject( pRestore, header, pItem->ppPhysObj, NULL );
				}
				return;
			}
			
			if ( pPhysCollide )
			{
				if ( !(*pItem->ppPhysObj) )
				{
					RestorePhysicsObject( pRestore, header, pItem->ppPhysObj, pPhysCollide );
					if ( (*pItem->ppPhysObj) )
					{
						IPhysicsObject *pObject = (IPhysicsObject *)(*pItem->ppPhysObj);
						if ( !fCustomCollide )
						{
							AssociateModel( pObject, modelIndex );
						}
						else
						{
							AssociateModel( pObject, pPhysCollide );
						}
					}
					else
						DevMsg( "Failed to restore physics object\n" );
				}
				else
					DevMsg( "Physics object pointer unexpectedly non-null before restore. Should be creating physics object in CreatePhysics()?\n" );
			}
			else
				DevMsg( "Failed to reestablish collision model for object\n" );
				
			pRestore->EndBlock();
		}
		else
			DevMsg( "Don't know how to reconsitite models for physobj array \n" );
	}
	
	//---------------------------------
	
	virtual void PostRestore() 
	{
		if ( physenv )
			physenv->PostRestore();

		unsigned short i = m_QueuedRestores.FirstInorder();
		while ( i != m_QueuedRestores.InvalidIndex() )
		{
			delete m_QueuedRestores[i];
			i = m_QueuedRestores.NextInorder( i );
		}
		
		m_QueuedRestores.RemoveAll();
#if !defined( CLIENT_DLL )
		gEntList.RemoveListenerEntity( this );
#endif
	}
	
	//---------------------------------
	
	void QueueSave( CBaseEntity *pOwner, typedescription_t *pTypeDesc, void **ppPhysObj, PhysInterfaceId_t type )
	{
		if ( !pOwner )
			return;

		bool fOnlyNotingExistence = !pOwner->ShouldSavePhysics();
		
		QueuedItem_t item;
		
		item.ppPhysObj		= ppPhysObj;
		item.header.hEntity = pOwner;
		item.header.type	= type;
		item.header.nObjects = ( !fOnlyNotingExistence ) ? pTypeDesc->fieldSize : 0;
		item.header.fieldName = AllocPooledString( pTypeDesc->fieldName ); 	
																	// A pooled string is used here because there is no way
																	// right now to save a non-string_t string and have it 
																	// compressed in the save symbol tables. Furthermore,
																	// the field name would normally be in the string
																	// pool anyway. (toml 12-10-02)
		item.header.modelName = NULL_STRING;
		memset( &item.header.bbox, 0, sizeof( item.header.bbox ) );
		item.header.sphere.radius = 0;
		
		if ( !fOnlyNotingExistence && type == PIID_IPHYSICSOBJECT )
		{
			// Don't doing the box thing for things like wheels on cars
			IPhysicsObject *pPhysObj = (IPhysicsObject *)(*ppPhysObj);

			if ( pPhysObj )
			{
				item.header.modelName = GetModelName( pPhysObj );
				item.header.iCollide = physcollision->CollideIndex( pPhysObj->GetCollide() );
				if ( item.header.modelName == NULL_STRING )
				{
					BBox_t *pBBox = GetBBox( pPhysObj );
					if ( pBBox != NULL )
					{
						item.header.bbox = *pBBox;
					}
					else 
					{
						if ( pPhysObj && pPhysObj->GetSphereRadius() != 0 )
						{
							item.header.sphere.radius = pPhysObj->GetSphereRadius();
						}
						else
						{
							DevMsg( "Don't know how to save model for physics object (class \"%s\")\n", pOwner->GetClassname() );
						}
					}
				}
			}
		}

		m_QueuedSaves.Insert( item );
	}

	//---------------------------------
	
	void QueueRestore( CBaseEntity *pOwner, typedescription_t *pTypeDesc, void **ppPhysObj, PhysInterfaceId_t type )
	{
		CEntityRestoreSet *pEntitySet = NULL;
		unsigned short 	   iEntitySet = m_QueuedRestores.Find( pOwner );
		
		if ( iEntitySet != m_QueuedRestores.InvalidIndex() )
		{
			pEntitySet = m_QueuedRestores[iEntitySet];
		}
		else
		{
			pEntitySet = new CEntityRestoreSet;
			m_QueuedRestores.Insert( pOwner, pEntitySet );
		}
		
		pEntitySet->Add( pOwner, pTypeDesc, ppPhysObj, type );

		memset( ppPhysObj, 0, pTypeDesc->fieldSize * sizeof( void * ) );
	}

	//---------------------------------
	
	void SavePhysicsObject( ISave *pSave, CBaseEntity *pOwner, void *pObject, PhysInterfaceId_t type )
	{
		if ( physenv )
		{
			if ( !pObject )
				return;
			physsaveparams_t params = { pSave, pObject, type };
#if defined( _WIN64 )
			SaveNativePhysicsObject( physenv, params );
#else
			physenv->Save( params );
#endif
		}
	}
	
	//---------------------------------
	
	void RestorePhysicsObject( IRestore *pRestore, const PhysObjectHeader_t &header, void **ppObject, const CPhysCollide *pCollide = NULL )
	{
		if ( physenv )
		{
			physrestoreparams_t params = { pRestore, ppObject, header.type, header.hEntity.Get(), STRING(header.modelName), pCollide, physenv, physgametrace };
#if defined( _WIN64 )
			RestoreNativePhysicsObject( physenv, params );
#else
			physenv->Restore( params );
#endif
		}
	}
#if !defined( CLIENT_DLL )	
	//-----------------------------------------------------
	// IEntityListener methods
	// This object is only a listener during restore	
	virtual void OnEntityCreated( CBaseEntity *pEntity )
	{
	}

	//---------------------------------
	
	virtual void OnEntityDeleted( CBaseEntity *pEntity )
	{
		unsigned short iEntitySet = m_QueuedRestores.Find( pEntity );
		
		if ( iEntitySet != m_QueuedRestores.InvalidIndex() )
		{
			delete m_QueuedRestores[iEntitySet];
			m_QueuedRestores.RemoveAt( iEntitySet );
		}
	}
#endif

	//-----------------------------------------------------
	// IPhysSaveRestoreManager methods
	
	virtual void NoteBBox( const Vector &mins, const Vector &maxs, CPhysCollide *pCollide )
	{
		if ( pCollide && m_PhysCollideBBoxModels.Find( pCollide ) == m_PhysCollideBBoxModels.InvalidIndex() )
		{
			BBox_t box;
			box.mins = mins;
			box.maxs = maxs;
			m_PhysCollideBBoxModels.Insert( pCollide, box );
		}
	}

	//---------------------------------
	
	virtual void AssociateModel( IPhysicsObject *pObject, int modelIndex )
	{
		Assert( m_PhysObjectModels.Find( pObject ) == m_PhysObjectModels.InvalidIndex() );
		m_PhysObjectModels.Insert( pObject, modelIndex );
	}

	//---------------------------------
	
	virtual void AssociateModel( IPhysicsObject *pObject, const CPhysCollide *pModel )
	{
		Assert( m_PhysObjectCustomModels.Find( pObject ) == m_PhysObjectCustomModels.InvalidIndex() );
		m_PhysObjectCustomModels.Insert( pObject, pModel );
	}

	//---------------------------------
	
	virtual void ForgetModel( IPhysicsObject *pObject )
	{
		if ( !m_PhysObjectModels.Remove( pObject ) )
			m_PhysObjectCustomModels.Remove( pObject );
	}

	//---------------------------------

	virtual void ForgetAllModels()
	{
		m_PhysObjectModels.RemoveAll();
		m_PhysObjectCustomModels.RemoveAll();
		m_PhysCollideBBoxModels.RemoveAll();
	}

	//---------------------------------
	
	string_t GetModelName( IPhysicsObject *pObject )
	{
		int i = m_PhysObjectModels.Find( pObject );
		if ( i == m_PhysObjectModels.InvalidIndex() )
			return NULL_STRING;
		return AllocPooledString( modelinfo->GetModelName( modelinfo->GetModel( m_PhysObjectModels[i] ) ) );
	}
	
	//---------------------------------
	
	BBox_t * GetBBox( IPhysicsObject *pObject )
	{
		int i = m_PhysObjectCustomModels.Find( pObject );
		if ( i == m_PhysObjectCustomModels.InvalidIndex() )
			return NULL;
		i = m_PhysCollideBBoxModels.Find( m_PhysObjectCustomModels[i] );
		if ( i == m_PhysCollideBBoxModels.InvalidIndex() )
			return NULL;
		return &(m_PhysCollideBBoxModels[i]);
	}

	//---------------------------------
	
private:
	struct QueuedItem_t
	{
		PhysObjectHeader_t	  header;
	 	void **				  ppPhysObj;
	};
	
	class CEntityRestoreSet : public CUtlVector<QueuedItem_t>
	{
	public:
		int Add( CBaseEntity *pOwner, typedescription_t *pTypeDesc, void **ppPhysObj, PhysInterfaceId_t type )
		{
			int i = AddToTail();
			
			Assert( ppPhysObj );
			Assert( *ppPhysObj == NULL ); // expected field to have been cleared
			Assert( pOwner );

			QueuedItem_t &item = Element( i );

			item.ppPhysObj			= ppPhysObj;
			item.header.hEntity 	= pOwner;
			item.header.type		= type;
			item.header.nObjects 	= pTypeDesc->fieldSize;
			item.header.fieldName 	= AllocPooledString( pTypeDesc->fieldName ); 	// See comment in CPhysSaveRestoreBlockHandler::QueueSave()
			
			return i;
		}
		
		QueuedItem_t *FindItem( string_t itemFieldName )
		{
			// generally, the set is very small, usually one, so linear search is not too gruesome;
			for ( int i = 0; i < Count(); i++ )
			{
				string_t testName = Element(i).header.fieldName;
				Assert( ( testName == itemFieldName && strcmp( STRING( testName ), STRING( itemFieldName ) ) == 0 ) ||
						( testName != itemFieldName && strcmp( STRING( testName ), STRING( itemFieldName ) ) != 0 ) );
				
				if ( testName == itemFieldName )
					return &(Element(i));
			}
			return NULL;
		}
	};
	
	//---------------------------------
	
	static bool SaveQueueFunc( const QueuedItem_t &left, const QueuedItem_t &right )
	{
		if ( left.header.type == right.header.type )
			return ( left.header.hEntity->entindex() > right.header.hEntity->entindex() );

		return ( left.header.type > right.header.type );
	}
	
	//---------------------------------

	CUtlPriorityQueue<QueuedItem_t> 			m_QueuedSaves;
	CUtlMap<CBaseEntity *, CEntityRestoreSet *>	m_QueuedRestores;
	bool 										m_fDoLoad;
	bool 										m_fSaveSupported;

	//---------------------------------
	
	CUtlMap<IPhysicsObject *, int>					m_PhysObjectModels;
	CUtlMap<IPhysicsObject *, const CPhysCollide *>	m_PhysObjectCustomModels;
	CUtlMap<const CPhysCollide *, BBox_t>			m_PhysCollideBBoxModels;

	//---------------------------------
	
	PhysBlockHeader_t							m_blockHeader;
};

//-----------------------------------------------------------------------------

CPhysSaveRestoreBlockHandler g_PhysSaveRestoreBlockHandler;

IPhysSaveRestoreManager *g_pPhysSaveRestoreManager = &g_PhysSaveRestoreBlockHandler;

//-------------------------------------

ISaveRestoreBlockHandler *GetPhysSaveRestoreBlockHandler()
{
	return &g_PhysSaveRestoreBlockHandler;
}

bool IsPhysSaveRestoreCompatible()
{
	return g_PhysSaveRestoreBlockHandler.IsSaveRestoreCompatible();
}

static bool IsValidEntityPointer( void *ptr )
{
#if !defined( CLIENT_DLL )
	return gEntList.IsEntityPtr( ptr );
#else
	// Walk entities looking for pointer
	int c = ClientEntityList().GetHighestEntityIndex();
	for ( int i = 0; i <= c; i++ )
	{
		CBaseEntity *e = ClientEntityList().GetBaseEntity( i );
		if ( !e )
			continue;

		if ( e == ptr )
			return true;
	}
	return false;
#endif
}

//-----------------------------------------------------------------------------
// Purpose:	Classifies field and queues it up for physics save/restore.
//

class CPhysObjSaveRestoreOps : public CDefSaveRestoreOps
{
public:
	virtual void Save( const SaveRestoreFieldInfo_t &fieldInfo, ISave *pSave )
	{
		CBaseEntity *pOwnerEntity = pSave->GetGameSaveRestoreInfo()->GetCurrentEntityContext();

		bool bFoundEntity = true;
		
		if ( IsValidEntityPointer(pOwnerEntity) == false )
		{
			bFoundEntity = false;

#if defined( CLIENT_DLL )
			pOwnerEntity = ClientEntityList().GetBaseEntityFromHandle( pOwnerEntity->GetRefEHandle() );

			if ( pOwnerEntity  )
			{
				bFoundEntity = true;
			}
#endif
		}

		AssertMsg( pOwnerEntity && bFoundEntity == true, "Physics save/load is only suitable for entities" );

		if ( m_type == PIID_UNKNOWN )
		{
			AssertMsg( 0, "Unknown physics save/load type");
			return;
		}
		g_PhysSaveRestoreBlockHandler.QueueSave( pOwnerEntity, fieldInfo.pTypeDesc, (void **)fieldInfo.pField, m_type );
	}
	
	virtual void Restore( const SaveRestoreFieldInfo_t &fieldInfo, IRestore *pRestore )
	{
		CBaseEntity *pOwnerEntity = pRestore->GetGameSaveRestoreInfo()->GetCurrentEntityContext();

		bool bFoundEntity = true;
		
		if ( IsValidEntityPointer(pOwnerEntity) == false )
		{
			bFoundEntity = false;

#if defined( CLIENT_DLL )
			pOwnerEntity = ClientEntityList().GetBaseEntityFromHandle( pOwnerEntity->GetRefEHandle() );

			if ( pOwnerEntity  )
			{
				bFoundEntity = true;
			}
#endif
		}

		AssertMsg( pOwnerEntity && bFoundEntity == true, "Physics save/load is only suitable for entities" );

		if ( m_type == PIID_UNKNOWN )
		{
			AssertMsg( 0, "Unknown physics save/load type");
			return;
		}
		
		g_PhysSaveRestoreBlockHandler.QueueRestore( pOwnerEntity, fieldInfo.pTypeDesc, (void **)fieldInfo.pField, m_type );
	}
	
	virtual void MakeEmpty( const SaveRestoreFieldInfo_t &fieldInfo )
	{
		memset( fieldInfo.pField, 0, fieldInfo.pTypeDesc->fieldSize * sizeof( void * ) );
	}
	
	virtual bool IsEmpty( const SaveRestoreFieldInfo_t &fieldInfo )
	{
		void **ppPhysObj = (void **)fieldInfo.pField;
		int nObjects = fieldInfo.pTypeDesc->fieldSize;
		for ( int i = 0; i < nObjects; i++ )
		{
			if ( ppPhysObj[i] != NULL )
				return false;
		}
		return true;
	}
	
	PhysInterfaceId_t m_type;
};

//-----------------------------------------------------------------------------

CPhysObjSaveRestoreOps g_PhysObjSaveRestoreOps[PIID_NUM_TYPES];

//-------------------------------------

ISaveRestoreOps *GetPhysObjSaveRestoreOps( PhysInterfaceId_t type )
{
	static bool inited;
	if ( !inited )
	{
		inited = true;
		for ( int i = 0; i < PIID_NUM_TYPES; i++ )
		{
			g_PhysObjSaveRestoreOps[i].m_type = (PhysInterfaceId_t)i;
		}
	}
	return &g_PhysObjSaveRestoreOps[type];
}

//=============================================================================
