//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "iviewrender.h"
#include "clienteffectprecachesystem.h"
#include "studio.h"
#include "bone_setup.h"
#include "engine/ivmodelinfo.h"
#include "c_fire_smoke.h"
#include "engine/IEngineSound.h"
#include "iefx.h"
#include "dlight.h"
#include "tier0/icommandline.h"

// memdbgon must be the last include file in a .cpp file!!!
#if defined( L4D2 )
#include "cliententitylist.h"
#include <emmintrin.h>
#endif // L4D2
#include "tier0/memdbgon.h"


#ifndef L4D2
CLIENTEFFECT_REGISTER_BEGIN( SmokeStackMaterials )
	CLIENTEFFECT_MATERIAL( "particle/SmokeStack" )
CLIENTEFFECT_REGISTER_END()
#endif // !L4D2: native resource registry owns this registration.


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pRecvProp - 
//			*pStruct - 
//			*pVarData - 
//			*pIn - 
//			objectID - 
//-----------------------------------------------------------------------------
void RecvProxy_Scale( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	C_FireSmoke	*pFireSmoke	= (C_FireSmoke	*) pStruct;
	float scale				= pData->m_Value.m_Float;

	//If changed, update our internal information
	if ( ( pFireSmoke->m_flScale != scale ) && ( pFireSmoke->m_flScaleEnd != scale ) )
	{
		pFireSmoke->m_flScaleStart		= pFireSmoke->m_flScaleRegister;
		pFireSmoke->m_flScaleEnd		= scale;			
	}

	pFireSmoke->m_flScale = scale;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pRecvProp - 
//			*pStruct - 
//			*pVarData - 
//			*pIn - 
//			objectID - 
//-----------------------------------------------------------------------------
void RecvProxy_ScaleTime( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	C_FireSmoke	*pFireSmoke	= (C_FireSmoke	*) pStruct;
	float time				= pData->m_Value.m_Float;

	//If changed, update our internal information
	//if ( pFireSmoke->m_flScaleTime != time )
	{
		if ( time == -1.0f )
		{
			pFireSmoke->m_flScaleTimeStart	= Helper_GetTime()-1.0f;
			pFireSmoke->m_flScaleTimeEnd	= pFireSmoke->m_flScaleTimeStart;
		}
		else
		{
			pFireSmoke->m_flScaleTimeStart	= Helper_GetTime();
			pFireSmoke->m_flScaleTimeEnd	= Helper_GetTime() + time;
		}
	}

	pFireSmoke->m_flScaleTime = time;
}

//Receive datatable
IMPLEMENT_CLIENTCLASS_DT( C_FireSmoke, DT_FireSmoke, CFireSmoke )
	RecvPropFloat( RECVINFO( m_flStartScale )),
	RecvPropFloat( RECVINFO( m_flScale ), 0, RecvProxy_Scale ),
	RecvPropFloat( RECVINFO( m_flScaleTime ), 0, RecvProxy_ScaleTime ),
	RecvPropInt( RECVINFO( m_nFlags ) ),
	RecvPropInt( RECVINFO( m_nFlameModelIndex ) ),
	RecvPropInt( RECVINFO( m_nFlameFromAboveModelIndex ) ),
END_RECV_TABLE()

//==================================================
// C_FireSmoke
//==================================================

C_FireSmoke::C_FireSmoke()
{
}

C_FireSmoke::~C_FireSmoke()
{

	// Shut down our effect if we have it
	if ( m_hEffect )
	{
		m_hEffect->StopEmission(false, false , true);
		m_hEffect = NULL;
	}

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void C_FireSmoke::Simulate( void )
{
}
#endif

#define	FLAME_ALPHA_START	0.9f
#define FLAME_ALPHA_END		1.0f

#define	FLAME_TRANS_START	0.75f

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_FireSmoke::AddFlames( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : bnewentity - 
//-----------------------------------------------------------------------------
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void C_FireSmoke::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );


	if ( updateType == DATA_UPDATE_CREATED )
	{
#if defined( L4D2 )
		SetNextClientThink( static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.1 ) ); // C755E0.
#else
		Start();
#endif
	}
}
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif

#if defined( L4D2 )
// C75BF0 starts the effect once, then removes this thinkable from the list.
void C_FireSmoke::ClientThink()
{
	Start();
	ClientThinkList()->RemoveThinkable( GetClientHandle() );
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_FireSmoke::UpdateEffects( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_FireSmoke::ShouldDraw()
{

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void C_FireSmoke::Start( void )
{
	const char *lpszEffectName;
#if defined( L4D2 )
	// C75AB0 widens the original float multiplier before the x87 product.
	int nSize = _mm_cvttsd_si32( _mm_set_sd( floor( static_cast< double >( m_flStartScale ) * static_cast< double >( 0.027777778f ) ) ) );
#else
	int nSize = (int) floor( m_flStartScale / 36.0f );
#endif
	switch ( nSize )
	{
	case 0:
		lpszEffectName = ( m_nFlags & bitsFIRESMOKE_SMOKE ) ? "env_fire_tiny_smoke" : "env_fire_tiny";
		break;

	case 1:
		lpszEffectName = ( m_nFlags & bitsFIRESMOKE_SMOKE ) ? "env_fire_small_smoke" : "env_fire_small";
		break;

	case 2:
		lpszEffectName = ( m_nFlags & bitsFIRESMOKE_SMOKE ) ? "env_fire_medium_smoke" : "env_fire_medium";
		break;

	case 3:
	default:
		lpszEffectName = ( m_nFlags & bitsFIRESMOKE_SMOKE ) ? "env_fire_large_smoke" : "env_fire_large";
		break;
	}

	// Create the effect of the correct size
	m_hEffect = ParticleProp()->Create( lpszEffectName, PATTACH_ABSORIGIN );

}
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif


//-----------------------------------------------------------------------------
// Purpose: FIXME: what's the right way to do this?
//-----------------------------------------------------------------------------
void C_FireSmoke::StartClientOnly( void )
{
	Start();

	ClientEntityList().AddNonNetworkableEntity(	this );
	CollisionProp()->CreatePartitionHandle();
	AddEffects( EF_NORECEIVESHADOW | EF_NOSHADOW );
	AddToLeafSystem();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_FireSmoke::RemoveClientOnly(void)
{
	ClientThinkList()->RemoveThinkable( GetClientHandle() );

	// Remove from the client entity list.
	ClientEntityList().RemoveEntity( GetClientHandle() );

	::partition->Remove( PARTITION_CLIENT_SOLID_EDICTS | PARTITION_CLIENT_RESPONSIVE_EDICTS | PARTITION_CLIENT_NON_STATIC_EDICTS, CollisionProp()->GetPartitionHandle() );

	RemoveFromLeafSystem();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_FireSmoke::UpdateAnimation( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_FireSmoke::UpdateFlames( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_FireSmoke::UpdateScale( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_FireSmoke::Update( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_FireSmoke::FindClipPlane( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Spawn smoke (...duh)
//-----------------------------------------------------------------------------

void C_FireSmoke::SpawnSmoke( void )
{
}


IMPLEMENT_CLIENTCLASS_DT( C_EntityFlame, DT_EntityFlame, CEntityFlame )
	RecvPropEHandle(RECVINFO(m_hEntAttached)),
#if defined( L4D2 )
	RecvPropBool( RECVINFO( m_bCheapEffect ) ),
#endif
END_RECV_TABLE()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
C_EntityFlame::C_EntityFlame() : m_hEffect( NULL )
{
	m_hOldAttached = NULL;
	AddToEntityList( ENTITY_LIST_SIMULATE );
}
#else
C_EntityFlame::C_EntityFlame( void ) :
m_hEffect( NULL )
{
	m_hOldAttached = NULL;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
C_EntityFlame::~C_EntityFlame()
{
	StopEffect();
}
#else
C_EntityFlame::~C_EntityFlame( void )
{
	StopEffect();
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
void C_EntityFlame::StopEffect()
{
	if ( m_hEffect )
	{
		ParticleProp()->StopEmission( m_hEffect, true, false );
		m_hEffect = NULL;
	}
	if ( m_hEntAttached.Get() )
	{
		// Keep the native fresh resolves/callback order, no cached attachment.
		m_hEntAttached->RemoveFlag( FL_ONFIRE );
		m_hEntAttached->SetEffectEntity( NULL );
		m_hEntAttached->StopSound( "General.BurningFlesh" );
		m_hEntAttached->StopSound( "General.BurningObject" );
		m_hEntAttached = NULL;
	}
}
#else
void C_EntityFlame::StopEffect( void )
{
	if ( m_hEffect )
	{
		ParticleProp()->StopEmission( m_hEffect, true );
		m_hEffect->SetControlPointEntity( 0, NULL );
		m_hEffect->SetControlPointEntity( 1, NULL );
		m_hEffect = NULL;
	}

	if ( m_hEntAttached )
	{
		m_hEntAttached->RemoveFlag( FL_ONFIRE );
		m_hEntAttached->SetEffectEntity( NULL );
		m_hEntAttached->StopSound( "General.BurningFlesh" );
		m_hEntAttached->StopSound( "General.BurningObject" );
		
		
		m_hEntAttached = NULL;
	}
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
void C_EntityFlame::UpdateOnRemove()
{
	StopEffect();
	BaseClass::UpdateOnRemove();
}
#else
void C_EntityFlame::UpdateOnRemove( void )
{
	StopEffect();
	BaseClass::UpdateOnRemove();
}
#endif

#if defined( L4D2 )
void C_EntityFlame::CreateEffect()
{
	if ( m_hEffect )
	{
		m_hOldAttached = m_hEntAttached;
		ParticleProp()->StopEmission( m_hEffect, true, false );
		m_hEffect = NULL;
	}
	C_BaseEntity *attached = m_hEntAttached.Get();
	if ( attached && !attached->ShouldDrawEntityFlameLight() )
		return;
	m_hEffect = ParticleProp()->Create( m_bCheapEffect ? "burning_gib_01" : "burning_character",
		PATTACH_ABSORIGIN_FOLLOW );
	if ( m_hEffect )
	{
		m_hOldAttached = m_hEntAttached;
		ParticleProp()->AddControlPoint( m_hEffect, 1, attached, PATTACH_ABSORIGIN_FOLLOW );
		m_hEffect->SetControlPoint( 0, GetAbsOrigin() );
		m_hEffect->SetControlPoint( 1, GetAbsOrigin() );
		m_hEffect->SetControlPointEntity( 0, attached );
		m_hEffect->SetControlPointEntity( 1, attached );
	}
}
#else
void C_EntityFlame::CreateEffect( void )
{
	if ( m_hEffect )
	{
		ParticleProp()->StopEmission( m_hEffect, true );
		m_hEffect->SetControlPointEntity( 0, NULL );
		m_hEffect->SetControlPointEntity( 1, NULL );
		m_hEffect = NULL;
	}

#if defined( L4D2 )
	m_hEffect = ParticleProp()->Create( m_bCheapEffect ? "burning_gib_01" : "burning_character", PATTACH_ABSORIGIN_FOLLOW );
#elif defined( TF_CLIENT_DLL )
	m_hEffect = ParticleProp()->Create( "burningplayer_red", PATTACH_ABSORIGIN_FOLLOW );
#else
	m_hEffect = ParticleProp()->Create( "burning_character", PATTACH_ABSORIGIN_FOLLOW );
#endif

	if ( m_hEffect )
	{
		C_BaseEntity *pEntity = m_hEntAttached;
		m_hOldAttached = m_hEntAttached;

		ParticleProp()->AddControlPoint( m_hEffect, 1, pEntity, PATTACH_ABSORIGIN_FOLLOW );
		m_hEffect->SetControlPoint( 0, GetAbsOrigin() );
		m_hEffect->SetControlPoint( 1, GetAbsOrigin() );
		m_hEffect->SetControlPointEntity( 0, pEntity );
		m_hEffect->SetControlPointEntity( 1, pEntity );
	}
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
void C_EntityFlame::OnDataChanged( DataUpdateType_t updateType )
{
	if ( updateType == DATA_UPDATE_CREATED )
		CreateEffect();
	else if ( updateType == DATA_UPDATE_DATATABLE_CHANGED )
	{
		C_BaseEntity *oldAttached = m_hOldAttached.Get();
		C_BaseEntity *attached = m_hEntAttached.Get();
		if ( attached != oldAttached )
			CreateEffect();
	}
	BaseClass::OnDataChanged( updateType );
}
#else
void C_EntityFlame::OnDataChanged( DataUpdateType_t updateType )
{
	if ( updateType == DATA_UPDATE_CREATED )
	{
		CreateEffect();
	}

	// FIXME: This is a bit of a shady path
	if ( updateType == DATA_UPDATE_DATATABLE_CHANGED )
	{
		// If our owner changed, then recreate the effect
		if ( m_hEntAttached != m_hOldAttached )
		{
			CreateEffect();
		}
	}

	BaseClass::OnDataChanged( updateType );
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
bool C_EntityFlame::Simulate( void )
{
	if ( !( gpGlobals->frametime <= 0.0f ) )
	{
		C_BaseEntity *pAttached = m_hEntAttached.Get();
		if ( !pAttached || pAttached->ShouldDrawEntityFlameLight() )
		{
			dlight_t *pLight = effects->CL_AllocElight( entindex() );
			pLight->origin = GetAbsOrigin();
			pLight->origin.z += 64.0f;
			pLight->color.r = 254;
			pLight->color.g = 100;
			pLight->color.b = 10;
			pLight->radius = random->RandomFloat( 48.0f, 96.0f );
			pLight->die = gpGlobals->curtime + 0.001f;
			pLight->color.exponent = 5;
		}
	}
	return true;
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif
#else
void C_EntityFlame::Simulate( void )
{
	if ( gpGlobals->frametime <= 0.0f )
		return;

#ifdef HL2_EPISODIC 

	if ( IsEffectActive(EF_BRIGHTLIGHT) || IsEffectActive(EF_DIMLIGHT) )
	{
		dlight_t *dl = effects->CL_AllocDlight ( index );
		dl->origin = GetAbsOrigin();
 		dl->origin[2] += 16;
		dl->color.r = 254;
		dl->color.g = 174;
		dl->color.b = 10;
		dl->radius = random->RandomFloat(400,431);
		dl->die = gpGlobals->curtime + 0.001;
	}

#endif // HL2_EPISODIC 
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
void C_EntityFlame::ClientThink()
{
	StopEffect();
	ClientEntityList().AddToDeleteList( this );
}
#else
void C_EntityFlame::ClientThink( void )
{
	StopEffect();
	Release();
}
#endif
