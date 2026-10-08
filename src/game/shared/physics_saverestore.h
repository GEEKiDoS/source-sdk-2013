//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#ifndef PHYSICS_SAVERESTORE_H
#define PHYSICS_SAVERESTORE_H

#if defined( _WIN32 )
#pragma once
#endif

#include "vphysics_interface.h"

class ISaveRestoreBlockHandler;
class IPhysicsObject;
class CPhysCollide;

//-----------------------------------------------------------------------------

ISaveRestoreBlockHandler *GetPhysSaveRestoreBlockHandler();
// Valid after ReadRestoreHeaders; callers must reject incompatible saves before
// restoring entities, since skipping only the physics block leaves null objects.
bool IsPhysSaveRestoreCompatible();
// Module-only preflight: restore headers precede creation of the new environment.
// Unsupported providers cannot write the repaired format or restore its payload.
bool IsPhysSaveRestoreSupported();
// Redirects only the certified provider's two broken native pointer serializers.
ISaveRestoreOps *ResolvePhysSaveRestoreOps( ISaveRestoreOps *pOps );
ISaveRestoreOps *GetPhysObjSaveRestoreOps( PhysInterfaceId_t );

//-------------------------------------

#define DEFINE_PHYSPTR(name) \
	{ FIELD_CUSTOM, #name, { (int)offsetof(classNameTypedef,name), 0 }, 1, FTYPEDESC_SAVE, NULL, GetPhysObjSaveRestoreOps( GetPhysIID( &(((classNameTypedef *)0)->name) ) ), NULL }

#define DEFINE_PHYSPTR_ARRAY(name) \
	{ FIELD_CUSTOM, #name, { (int)offsetof(classNameTypedef,name), 0 }, ARRAYSIZE(((classNameTypedef *)0)->name), FTYPEDESC_SAVE, NULL, GetPhysObjSaveRestoreOps( GetPhysIID( &(((classNameTypedef *)0)->name[0]) ) ), NULL }

//-----------------------------------------------------------------------------

abstract_class IPhysSaveRestoreManager
{
public:
	virtual void NoteBBox( const Vector &mins, const Vector &maxs, CPhysCollide * ) = 0;

	virtual void AssociateModel( IPhysicsObject *, int modelIndex ) = 0;
	virtual void AssociateModel( IPhysicsObject *, const CPhysCollide *pModel ) = 0;
	virtual void ForgetModel( IPhysicsObject * ) = 0;

	virtual void ForgetAllModels() = 0;
};

extern IPhysSaveRestoreManager *g_pPhysSaveRestoreManager;

//=============================================================================

#endif // PHYSICS_SAVERESTORE_H
