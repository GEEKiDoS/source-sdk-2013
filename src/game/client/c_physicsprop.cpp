//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//
#include "cbase.h"
#include "model_types.h"
#include "vcollide.h"
#include "vcollide_parse.h"
#include "solidsetdefaults.h"
#include "bone_setup.h"
#include "engine/ivmodelinfo.h"
#include "physics.h"
#include "view.h"
#include "clienteffectprecachesystem.h"
#include "c_physicsprop.h"
#include "tier0/vprof.h"
#include "ivrenderview.h"
#if defined( L4D2 )
#include "studio.h"
#endif
#include "clientleafsystem.h"
#include "shadowmaps_dx12.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT(C_PhysicsProp, DT_PhysicsProp, CPhysicsProp)
	RecvPropBool( RECVINFO( m_bAwake ) ),
#if defined( L4D2 )
	RecvPropBool( RECVINFO( m_hasTankGlow ) ),
	RecvPropBool( RECVINFO( m_isCarryable ) ),
	RecvPropInt( RECVINFO( m_spawnflags ) ),
#endif
	RecvPropBool( RECVINFO( m_bShadowMapOrdinaryPhysicsProp ) ),
END_RECV_TABLE()

ConVar r_PhysPropStaticLighting( "r_PhysPropStaticLighting", 
#if defined( L4D2 )
	"0"
#else
	"1"
#endif

#if defined( L4D2 )
	, 0x00080008
#endif
 );

#if defined( L4D2 )
// Original engine E1AFFC0 maps studio-header flags in E0E9430. The target
// has no r_staticlight_streams engine control; Main selects its default1
// branch, not a half-functional game-owned renderer setting.
static bool TerrorModelSupportsStaticLighting( const model_t *model )
{
	if ( !model || modelinfo->GetModelType( model ) != mod_studio )
		return false;
	const studiohdr_t *header = modelinfo->GetStudiomodel( model );
	return header && ( header->flags & STUDIOHDR_FLAGS_STATIC_PROP ) &&
		!( header->flags & STUDIOHDR_FLAGS_USES_BUMPMAPPING );
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_PhysicsProp::C_PhysicsProp( void )
{
	m_pPhysicsObject = NULL;
	m_takedamage = DAMAGE_YES;

	// default true so static lighting will get recomputed when we go to sleep
	m_bAwakeLastTime = true;
	m_bShadowMapOrdinaryPhysicsProp = false;
	m_bShadowMapOverrideEligibleLast = false;
}

void C_PhysicsProp::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
#if defined( L4D2 )
	m_bStaticLightingEligible = TerrorModelSupportsStaticLighting( GetModel() );
	if ( m_bStaticLightingEligible )
		CreateModelInstance();
#endif

	bool eligible = CanOverrideShadowMapNoShadow();
	if ( eligible != m_bShadowMapOverrideEligibleLast )
	{
		m_bShadowMapOverrideEligibleLast = eligible;
		// Render registration can predate the first receive, including save restores.
		// Identity/solidity/visibility changes also retire cached rigid silhouettes.
		if ( RenderHandle() != INVALID_CLIENT_RENDER_HANDLE )
		{
			ShadowMapsDX12_InvalidateCasterRegistration( this );
			ClientLeafSystem()->RenderableChanged( RenderHandle() );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_PhysicsProp::~C_PhysicsProp( void )
{
}


// @MULTICORE (toml 9/18/2006): this visualization will need to be implemented elsewhere
ConVar r_visualizeproplightcaching( "r_visualizeproplightcaching", "0"
#if defined( L4D2 )
	, 0x0000000A
#endif
 );

#if defined( L4D2 )
// C86800: ineligible models do not bake, flag, tint, or advance awake history.
bool C_PhysicsProp::OnInternalDrawModel( ClientModelRenderInfo_t *pInfo )
{
	if ( m_bStaticLightingEligible && r_PhysPropStaticLighting.GetBool() )
	{
		if ( m_bAwakeLastTime != m_bAwake )
		{
			if ( m_bAwakeLastTime && !m_bAwake )
			{
				if ( !modelrender->RecomputeStaticLighting( GetModelInstance() ) )
					return false;
				if ( r_visualizeproplightcaching.GetBool() )
				{
					float color[] = { 0.0f, 1.0f, 0.0f, 1.0f };
					render->SetColorModulation( color );
				}
			}
			else if ( r_visualizeproplightcaching.GetBool() )
			{
				float color[] = { 1.0f, 0.0f, 0.0f, 1.0f };
				render->SetColorModulation( color );
			}
		}
		if ( !m_bAwake )
			pInfo->flags |= STUDIO_STATIC_LIGHTING;
		m_bAwakeLastTime = m_bAwake;
	}
	return true;
}
#else
//-----------------------------------------------------------------------------
// Purpose: Draws the object
// Input  : flags - 
//-----------------------------------------------------------------------------
bool C_PhysicsProp::OnInternalDrawModel( ClientModelRenderInfo_t *pInfo )
{
	CreateModelInstance();

	if ( r_PhysPropStaticLighting.GetBool() && m_bAwakeLastTime != m_bAwake )
	{
		if ( m_bAwakeLastTime && !m_bAwake )
		{
			// transition to sleep, bake lighting now, once
			if ( !modelrender->RecomputeStaticLighting( GetModelInstance() ) )
			{
				// not valid for drawing
				return false;
			}

			if ( r_visualizeproplightcaching.GetBool() )
			{
				float color[] = { 0.0f, 1.0f, 0.0f, 1.0f };
				render->SetColorModulation( color );
			}
		}
		else if ( r_visualizeproplightcaching.GetBool() )
		{
			float color[] = { 1.0f, 0.0f, 0.0f, 1.0f };
			render->SetColorModulation( color );
		}
	}

	if ( !m_bAwake && r_PhysPropStaticLighting.GetBool() )
	{
		// going to sleep, have static lighting
		pInfo->flags |= STUDIO_STATIC_LIGHTING;
	}
	
	// track state
	m_bAwakeLastTime = m_bAwake;

	return true;
}
#endif

#if defined( L4D2 )
static ConVar cl_glow_physics_props( "cl_glow_physics_props", "0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );

// Whole C86590. Native2408 wire maps to the ONE existing client root spawnflags
// storage, not a duplicated derived field or invented ObjectCaps provider.
int C_PhysicsProp::ObjectCaps()
{
	int caps = C_BaseEntity::ObjectCaps();
	if ( GetSpawnFlags() & 0x8000 ) caps |= 0x2000;
	if ( m_hasTankGlow ) return caps | 0x1000;
	if ( m_isCarryable ) return caps | 0x0210;
	return caps;
}

// Whole C867C0. Both bools are received from the actual paired server writers;
// this predicate is NOT IsPotentiallyUseable360/registration or awake/static.
bool C_PhysicsProp::IsGlowUseTarget()
{
	if ( m_hasTankGlow ) return true;
	return ( ObjectCaps() & 0x2000 ) != 0 || cl_glow_physics_props.GetBool();
}

#endif
