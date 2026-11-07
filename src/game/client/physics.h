//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: insulates client DLL from dependencies on vphysics
//
// $NoKeywords: $
//=============================================================================//

#ifndef PHYSICS_H
#define PHYSICS_H
#ifdef _WIN32
#pragma once
#endif


#include "interface.h"
#include "physics_shared.h"

struct objectparams_t;
struct solid_t;

// HACKHACK: Make this part of IClientSystem somehow???
extern bool PhysicsDLLInit( CreateInterfaceFn physicsFactory );
extern void PhysicsReset();
extern void PhysicsSimulate();
#if defined( L4D2 )
extern double PhysGetSyncCreateTime();
#else
extern float PhysGetSyncCreateTime();
#endif

#if defined( L4D2 )
void L4D2ClientConcatTransforms( const matrix3x4_t &left, const matrix3x4_t &right, matrix3x4_t &out );
void PhysOnDataChanged( C_BaseEntity *entity, bool created );
void PhysUpdateShadow( C_BaseEntity *entity, IPhysicsObject *object, const Vector &localOrigin, const QAngle &localAngles, float timeOffset );
#endif

#endif // PHYSICS_H
