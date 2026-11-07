//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//



// Client-side CBasePlayer

#ifndef C_PHYSBOX_H
#define C_PHYSBOX_H
#pragma once


#include "c_baseentity.h"
#if defined( L4D2 )
#include "l4d2/entities/c_terror_breakable.h"
#endif


#if defined( L4D2 )
class C_PhysBox : public C_Breakable
#else
class C_PhysBox : public C_BaseEntity
#endif
{
public:
#if defined( L4D2 )
	DECLARE_CLASS( C_PhysBox, C_Breakable );
#else
	DECLARE_CLASS( C_PhysBox, C_BaseEntity );
#endif
	DECLARE_CLIENTCLASS();

					C_PhysBox();
	virtual			~C_PhysBox();
	virtual ShadowType_t ShadowCastType();
	
public:
	float			m_mass;	// TEST..
};


#endif



