//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================

#include "cbase.h"
#include "particle_property.h"
#include "utlvector.h"

#ifdef CLIENT_DLL

#include "c_baseentity.h"
#include "c_baseanimating.h"
#include "recvproxy.h"
#include "particles_new.h"
#include "engine/ivdebugoverlay.h"
#include "bone_setup.h"
#if defined( L4D2 )
#include "physics.h"
#include "c_baseplayer.h"
#include "particlemgr.h"
#include "tier1/checksum_crc.h"
#include "tier1/utlrbtree.h"
#endif
#else

#include "baseentity.h"
#include "baseanimating.h"
#include "sendproxy.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Save/load
//-----------------------------------------------------------------------------
BEGIN_DATADESC_NO_BASE( CParticleProperty )
	//		DEFINE_FIELD( m_pOuter, FIELD_CLASSPTR ),
END_DATADESC()

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// Prediction
//-----------------------------------------------------------------------------
BEGIN_PREDICTION_DATA_NO_BASE( CParticleProperty )
	//DEFINE_PRED_FIELD( m_vecMins, FIELD_VECTOR, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()
#endif

//-----------------------------------------------------------------------------
// Networking
//-----------------------------------------------------------------------------
BEGIN_NETWORK_TABLE_NOBASE( CParticleProperty, DT_ParticleProperty )
#ifdef CLIENT_DLL
//RecvPropVector( RECVINFO(m_vecMins), 0, RecvProxy_OBBMins ),
#else
//SendPropVector( SENDINFO(m_vecMins), 0, SPROP_NOSCALE),
#endif
END_NETWORK_TABLE()


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
CParticleProperty::CParticleProperty()
{
	Init( NULL );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CParticleProperty::~CParticleProperty()
{
	// We're being removed. Call StopEmission() on any particle system
	// that has an unlimited number of particles to emit.
	StopEmission( NULL, false, true );
}

//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------
void CParticleProperty::Init( CBaseEntity *pEntity )
{
	m_pOuter = pEntity;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
int CParticleProperty::GetParticleAttachment( C_BaseEntity *pEntity, const char *pszAttachmentName, const char *pszParticleName )
{
	Assert( pEntity && pEntity->GetBaseAnimating() );
	if ( !pEntity || !pEntity->GetBaseAnimating() )
		return INVALID_PARTICLE_ATTACHMENT;

	// Find the attachment point index
	int iAttachment = pEntity->GetBaseAnimating()->LookupAttachment( pszAttachmentName );

	return iAttachment;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Create a new particle system and attach it to our owner
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
CNewParticleEffect *CParticleProperty::Create( const char *pszParticleName, ParticleAttachment_t iAttachType, const char *pszAttachmentName )
{
	int iAttachment = GetParticleAttachment( GetOuter(), pszAttachmentName, pszParticleName );
	if ( iAttachment == INVALID_PARTICLE_ATTACHMENT )
		return NULL;

	// Create the system
	return Create( pszParticleName, iAttachType, iAttachment );
}
#endif
	  
//-----------------------------------------------------------------------------
// Purpose: Create a new particle system and attach it to our owner
//-----------------------------------------------------------------------------
#if defined( L4D2 )
static ConVar cl_particle_batch_mode( "cl_particle_batch_mode", "1", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
#else
static ConVar cl_particle_batch_mode( "cl_particle_batch_mode", "1" );
#endif
#if !defined( L4D2 )
CNewParticleEffect *CParticleProperty::Create( const char *pszParticleName, ParticleAttachment_t iAttachType, int iAttachmentPoint, Vector vecOriginOffset )
{
	if ( GameRules() )
	{
		pszParticleName = GameRules()->TranslateEffectForVisionFilter( "particles", pszParticleName );
	}

	int nBatchMode = cl_particle_batch_mode.GetInt();
	CParticleSystemDefinition *pDef = g_pParticleSystemMgr->FindParticleSystem( pszParticleName );
	bool bRequestedBatch = ( nBatchMode == 2 ) || ( ( nBatchMode == 1 ) && pDef && pDef->ShouldBatch() ); 
	if ( ( iAttachType == PATTACH_CUSTOMORIGIN ) && bRequestedBatch )
	{
		int iIndex = FindEffect( pszParticleName );
		if ( iIndex >= 0 )
		{
			CNewParticleEffect *pEffect = m_ParticleEffects[iIndex].pParticleEffect.GetObject();
			pEffect->Restart();
			return pEffect;
		}
	}

	if ( !pDef )
	{
		AssertMsg( 0, "Attempting to create unknown particle system" );
		Warning( "Attempting to create unknown particle system '%s' \n", pszParticleName );
		return NULL;
	}

	int iIndex = m_ParticleEffects.AddToTail();
	ParticleEffectList_t *newEffect = &m_ParticleEffects[iIndex];
	newEffect->pParticleEffect = CNewParticleEffect::Create( m_pOuter, pDef );

	if ( !newEffect->pParticleEffect->IsValid() )
	{
		// Caused by trying to spawn an unregistered particle effect. Remove it.
		ParticleMgr()->RemoveEffect( newEffect->pParticleEffect.GetObject() );
		return NULL;
	}

	AddControlPoint( iIndex, 0, GetOuter(), iAttachType, iAttachmentPoint, vecOriginOffset );

	if ( m_pOuter )
	{
		m_pOuter->OnNewParticleEffect( pszParticleName, newEffect->pParticleEffect.GetObject() );
	}
	
	return newEffect->pParticleEffect.GetObject();
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void CParticleProperty::AddControlPoint( CNewParticleEffect *pEffect, int iPoint, C_BaseEntity *pEntity, ParticleAttachment_t iAttachType, const char *pszAttachmentName, Vector vecOriginOffset )
{
	int iAttachment = INVALID_PARTICLE_ATTACHMENT;
	if ( pszAttachmentName )
	{
		iAttachment = GetParticleAttachment( pEntity, pszAttachmentName, pEffect->GetEffectName() );
	}

	for ( int i = 0; i < m_ParticleEffects.Count(); i++ )
	{
		if ( m_ParticleEffects[i].pParticleEffect == pEffect )
		{
			AddControlPoint( i, iPoint, pEntity, iAttachType, iAttachment, vecOriginOffset );
		}
	}
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void CParticleProperty::AddControlPoint( int iEffectIndex, int iPoint, C_BaseEntity *pEntity, ParticleAttachment_t iAttachType, int iAttachmentPoint, Vector vecOriginOffset )
{
	Assert( iEffectIndex >= 0 && iEffectIndex < m_ParticleEffects.Count() );
	ParticleEffectList_t *pEffect = &m_ParticleEffects[iEffectIndex];
	Assert( pEffect->pControlPoints.Count() < MAX_PARTICLE_CONTROL_POINTS );

	// If the control point is already used, override it
	ParticleControlPoint_t *pNewPoint = NULL;
	int iIndex = iPoint;
	FOR_EACH_VEC( pEffect->pControlPoints, i )
	{
		if ( pEffect->pControlPoints[i].iControlPoint == iPoint )
		{
			pNewPoint = &pEffect->pControlPoints[i];
		}
	}

	if ( !pNewPoint )
	{
		iIndex = pEffect->pControlPoints.AddToTail();
		pNewPoint = &pEffect->pControlPoints[iIndex];
	}
	
	pNewPoint->iControlPoint = iPoint;
	pNewPoint->hEntity = pEntity;
	pNewPoint->iAttachType = iAttachType;
	pNewPoint->iAttachmentPoint = iAttachmentPoint;
	pNewPoint->vecOriginOffset = vecOriginOffset;

	UpdateParticleEffect( pEffect, true, iIndex );
}
#endif


//-----------------------------------------------------------------------------
// Used to replace a particle effect with a different one; attaches the control point updating to the new one
//-----------------------------------------------------------------------------
void CParticleProperty::ReplaceParticleEffect( CNewParticleEffect *pOldEffect, CNewParticleEffect *pNewEffect )
{
	int nCount = m_ParticleEffects.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		if ( pOldEffect != m_ParticleEffects[i].pParticleEffect.GetObject() )
			continue;

		m_ParticleEffects[i].pParticleEffect = pNewEffect;
		UpdateParticleEffect( &m_ParticleEffects[i], true );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Set the parent of a given control point to the index of some other
//			control point.
//-----------------------------------------------------------------------------
void CParticleProperty::SetControlPointParent( int iEffectIndex, int whichControlPoint, int parentIdx )
{

}

//-----------------------------------------------------------------------------
// Purpose: Stop effects from emitting more particles. If no effect is 
//			specified, all effects attached to this entity are stopped.
//-----------------------------------------------------------------------------
void CParticleProperty::StopEmission( CNewParticleEffect *pEffect, bool bWakeOnStop, bool bDestroyAsleepSystems )
{
	// If we return from dormancy and are then told to stop emitting,
	// we should have died while dormant. Remove ourselves immediately.
	bool bRemoveInstantly = (m_iDormancyChangedAtFrame == gpGlobals->framecount);

	if ( pEffect )
	{
		if ( FindEffect( pEffect ) != -1 )
		{
			pEffect->StopEmission( false, bRemoveInstantly, bWakeOnStop );
		}
	}
	else
	{
		// Stop all effects
		float flNow = g_pParticleSystemMgr->GetLastSimulationTime();
		int nCount = m_ParticleEffects.Count();
		for ( int i = nCount-1; i >= 0; i-- )
		{
			CNewParticleEffect *pTmp = m_ParticleEffects[i].pParticleEffect.GetObject();
			bool bRemoveSystem = bRemoveInstantly || ( bDestroyAsleepSystems && ( flNow >= pTmp->m_flNextSleepTime ) );
			if ( bRemoveSystem )
			{
				m_ParticleEffects.Remove( i );
				pTmp->SetOwner( NULL );
			}
			pTmp->StopEmission( false, bRemoveSystem, !bRemoveSystem && bWakeOnStop );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Remove effects immediately, including all current particles. If no
// effect is specified, all effects attached to this entity are removed.
//-----------------------------------------------------------------------------
void CParticleProperty::StopEmissionAndDestroyImmediately( CNewParticleEffect *pEffect )
{
	if ( pEffect )
	{
		int iIndex = FindEffect( pEffect );
		//Assert( iIndex != -1 );
		if ( iIndex != -1 )
		{
#if defined( L4D2 )
			pEffect->SetOwner( NULL );
			m_ParticleEffects.Remove( iIndex );
			pEffect->StopEmission( false, true, false );
#else
			m_ParticleEffects.Remove( iIndex );
			pEffect->SetOwner( NULL );
			pEffect->StopEmission( false, true );
#endif
		}
	}
	else
	{
		// Immediately destroy all effects
		int nCount = m_ParticleEffects.Count();
		for ( int i = nCount-1; i >= 0; i-- )
		{
			CNewParticleEffect *pTmp = m_ParticleEffects[i].pParticleEffect.GetObject();
#if defined( L4D2 )
			pTmp->SetOwner( NULL );
			m_ParticleEffects.Remove( i );
			pTmp->StopEmission( false, true, false );
#else
			m_ParticleEffects.Remove( i );
			pTmp->SetOwner( NULL );
			pTmp->StopEmission( false, true );
#endif
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Stop all effects that have  a control point associated with the given
//          entity.
//-----------------------------------------------------------------------------
void CParticleProperty::StopParticlesInvolving( CBaseEntity *pEntity )
{
	Assert( pEntity );

	EHANDLE entHandle(pEntity);

	// If we return from dormancy and are then told to stop emitting,
	// we should have died while dormant. Remove ourselves immediately.
	bool bRemoveInstantly = (m_iDormancyChangedAtFrame == gpGlobals->framecount);
	
	int nCount = m_ParticleEffects.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		// for each effect...
		ParticleEffectList_t &part = m_ParticleEffects[i];
		// look through all the control points to see if any mention the given object
		int cpCount = part.pControlPoints.Count();
		for (int j = 0; j < cpCount ; ++j )
		{
			// if any control points respond to the given handle...
			if (part.pControlPoints[j].hEntity == entHandle)
			{
				part.pParticleEffect->StopEmission( false, bRemoveInstantly );
				part.pControlPoints.Remove( j );
				break; // break out of the inner loop (to where it says BREAK TO HERE)
			}
		}
		// BREAK TO HERE
	}
}

//g_pParticleSystemMgr->FindParticleSystem( pParticleSystemName );

//-----------------------------------------------------------------------------
// Purpose: Stop all effects that were created using the given definition
//			name.
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void CParticleProperty::StopParticlesNamed( const char *pszEffectName, bool bForceRemoveInstantly /* =false */,  bool bInverse /*= false*/ )
{
	if ( !pszEffectName || !pszEffectName[0] )
		return;

	CParticleSystemDefinition *pDef = g_pParticleSystemMgr->FindParticleSystem( pszEffectName );
	AssertMsg1(pDef, "Could not find particle definition %s", pszEffectName );
	if (!pDef)
		return;

	// If we return from dormancy and are then told to stop emitting,
	// we should have died while dormant. Remove ourselves immediately.
	bool bRemoveInstantly = (m_iDormancyChangedAtFrame == gpGlobals->framecount);
	// force remove particles instantly if caller specified
	bRemoveInstantly |= bForceRemoveInstantly;

	int nCount = m_ParticleEffects.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		// for each effect...
		CNewParticleEffect *pParticleEffect = m_ParticleEffects[i].pParticleEffect.GetObject();
		bool bMatches = pParticleEffect->m_pDef() == pDef;
		if ( bMatches == !bInverse )
		{
			pParticleEffect->StopEmission( false, bRemoveInstantly );
		}
	}
}
#endif


void CParticleProperty::StopParticlesWithNameAndAttachment( const char *pszEffectName, int iAttachmentPoint, bool bForceRemoveInstantly /* =false */ )
{
	CParticleSystemDefinition *pDef = g_pParticleSystemMgr->FindParticleSystem( pszEffectName );
	AssertMsg1(pDef, "Could not find particle definition %s", pszEffectName );
	if (!pDef)
		return;


	// If we return from dormancy and are then told to stop emitting,
	// we should have died while dormant. Remove ourselves immediately.
	bool bRemoveInstantly = (m_iDormancyChangedAtFrame == gpGlobals->framecount);
	// force remove particles instantly if caller specified
	bRemoveInstantly |= bForceRemoveInstantly;

	int nCount = m_ParticleEffects.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		// for each effect...
		ParticleEffectList_t *pParticleEffectList = &m_ParticleEffects[i];
		CNewParticleEffect *pParticleEffect = pParticleEffectList->pParticleEffect.GetObject();
		if (pParticleEffect->m_pDef() == pDef)
		{
			int nControlPointCount = pParticleEffectList->pControlPoints.Count();
			for ( int j = 0; j < nControlPointCount; ++j )
			{
				if ( pParticleEffectList->pControlPoints[j].iAttachmentPoint == iAttachmentPoint )
				{
					pParticleEffect->StopEmission( false, bRemoveInstantly );
					break;
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CParticleProperty::OnParticleSystemUpdated( CNewParticleEffect *pEffect, float flTimeDelta )
{
	int iIndex = FindEffect( pEffect );
	Assert( iIndex != -1 );
	if ( iIndex == -1 )
		return;

	// Enable FP exceptions here when FP_EXCEPTIONS_ENABLED is defined,
	// to help track down bad math.
	FPExceptionEnabler enableExceptions;

	UpdateParticleEffect( &m_ParticleEffects[iIndex] );

	/*
	// Display the bounding box of the particle effect
	Vector vecMins, vecMaxs;
	pEffect->GetRenderBounds( vecMins, vecMaxs );
	debugoverlay->AddBoxOverlay( pEffect->GetRenderOrigin(), vecMins, vecMaxs, QAngle( 0, 0, 0 ), 0, 255, 255, 0, 0 );
	*/
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
void CParticleProperty::OnParticleSystemDeleted( CNewParticleEffect *pEffect )
{
	int iIndex = FindEffect( pEffect );
	if ( iIndex == -1 )
		return;

#if defined( L4D2 ) && defined( CLIENT_DLL )
	pEffect->SetOwner( NULL ); // Native152587, BEFORE the actual owner virtual.
	if ( m_pOuter )
		m_pOuter->OnParticleEffectDeleted( pEffect );
#endif
	m_ParticleEffects[iIndex].pParticleEffect.MarkDeleted();
	m_ParticleEffects.Remove( iIndex );
}
#else
void CParticleProperty::OnParticleSystemDeleted( CNewParticleEffect *pEffect )
{
	int iIndex = FindEffect( pEffect );
	if ( iIndex == -1 )
		return;

	m_ParticleEffects[iIndex].pParticleEffect.MarkDeleted();
	m_ParticleEffects.Remove( iIndex );
}
#endif

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// Purpose: The entity we're attached to has change dormancy state on our client
//-----------------------------------------------------------------------------
void CParticleProperty::OwnerSetDormantTo( bool bDormant )
{
	m_iDormancyChangedAtFrame = gpGlobals->framecount;

	int nCount = m_ParticleEffects.Count();
	for ( int i = 0; i < nCount; i++ )
	{
		//m_ParticleEffects[i].pParticleEffect->SetShouldSimulate( !bDormant );
		m_ParticleEffects[i].pParticleEffect->SetDormant( bDormant );
	}
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int	CParticleProperty::FindEffect( CNewParticleEffect *pEffect )
{
	for ( int i = 0; i < m_ParticleEffects.Count(); i++ )
	{
		if ( m_ParticleEffects[i].pParticleEffect == pEffect )
			return i;
	}

	return -1;
}

int CParticleProperty::FindEffect( const char *pEffectName, int nStart /*= 0*/ )
{
	for ( int i = nStart; i < m_ParticleEffects.Count(); i++ )
	{
		if ( !Q_stricmp( m_ParticleEffects[i].pParticleEffect->GetName(), pEffectName ) )
			return i;
	}

	return -1;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CParticleProperty::UpdateParticleEffect( ParticleEffectList_t *pEffect, bool bInitializing, int iOnlyThisControlPoint )
{
	if ( iOnlyThisControlPoint != -1 )
	{
		UpdateControlPoint( pEffect, iOnlyThisControlPoint, bInitializing );
		return;
	}

	// Loop through our control points and update them all
	for ( int i = 0; i < pEffect->pControlPoints.Count(); i++ )
	{
		UpdateControlPoint( pEffect, i, bInitializing );
	}
}

extern void FormatViewModelAttachment( Vector &vOrigin, bool bInverse );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void CParticleProperty::UpdateControlPoint( ParticleEffectList_t *pEffect, int iPoint, bool bInitializing )
{
	ParticleControlPoint_t *pPoint = &pEffect->pControlPoints[iPoint];

	if ( !pPoint->hEntity.Get() )
	{
		if ( pPoint->iAttachType == PATTACH_WORLDORIGIN && bInitializing )
		{
			pEffect->pParticleEffect->SetControlPointOrientation( pPoint->iControlPoint, Vector(1,0,0), Vector(0,1,0), Vector(0,0,1) );
			pEffect->pParticleEffect->SetControlPoint( pPoint->iControlPoint, pPoint->vecOriginOffset );
			pEffect->pParticleEffect->SetSortOrigin( pPoint->vecOriginOffset );
		}

		pEffect->pParticleEffect->SetControlPointEntity( pPoint->iControlPoint, NULL );
		return;
	}

	// Only update non-follow particles when we're initializing, 
	// unless we're parented to something, in which case we should always update
	if ( !bInitializing && !pPoint->hEntity->GetMoveParent() && (pPoint->iAttachType == PATTACH_ABSORIGIN || pPoint->iAttachType == PATTACH_POINT ) )
		return;

	if ( pPoint->iAttachType == PATTACH_CUSTOMORIGIN )
		return;

	Vector vecOrigin, vecForward, vecRight, vecUp;

	float flOffset = 0.0f;
	bool bUsingHeadOrigin = false;

#ifdef TF_CLIENT_DLL

	CBaseEntity *pWearable = (CBaseEntity*) pPoint->hEntity.Get();
	if ( pWearable && GetAttribInterface( pWearable ) && !pWearable->IsPlayer() )
	{
		C_BaseAnimating *pAnimating = pPoint->hEntity->GetBaseAnimating();
		if ( pAnimating )
		{
			int bUseHeadOrigin = 0;
			CALL_ATTRIB_HOOK_INT_ON_OTHER( pAnimating, bUseHeadOrigin, particle_effect_use_head_origin );
			if ( bUseHeadOrigin > 0 )
			{
				int iBone = Studio_BoneIndexByName( pAnimating->GetModelPtr(), "bip_head" );
				if ( iBone < 0 )
				{
					iBone = Studio_BoneIndexByName( pAnimating->GetModelPtr(), "prp_helmet" );
					if ( iBone < 0 )
					{
						iBone = Studio_BoneIndexByName( pAnimating->GetModelPtr(), "prp_hat" );
					}
				}
				if ( iBone < 0 )
				{
					iBone = 0;
				}

				bUsingHeadOrigin = true;
				const matrix3x4_t headBone = pAnimating->GetBone( iBone );
				MatrixVectors( headBone, &vecForward, &vecRight, &vecUp );
				MatrixPosition( headBone, vecOrigin );

				CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( pAnimating, flOffset, particle_effect_vertical_offset );	
			}
		}
	}
#endif

	if ( !bUsingHeadOrigin )
	{
		switch ( pPoint->iAttachType )
		{
		case PATTACH_POINT:
		case PATTACH_POINT_FOLLOW:
			{
				C_BaseAnimating *pAnimating = pPoint->hEntity->GetBaseAnimating();

				Assert( pAnimating );
				if ( pAnimating )
				{
					matrix3x4_t attachmentToWorld;

					if ( !pAnimating->GetAttachment( pPoint->iAttachmentPoint, attachmentToWorld ) )
					{
						// try C_BaseAnimating if attach point is not on the weapon
						if ( !pAnimating->C_BaseAnimating::GetAttachment( pPoint->iAttachmentPoint, attachmentToWorld ) )
						{
							Warning( "Cannot update control point %d for effect '%s'.\n", pPoint->iAttachmentPoint, pEffect->pParticleEffect->GetEffectName() );
							// Remove the effect cause this warning means something is orphaned
							StopParticlesNamed( pEffect->pParticleEffect->GetEffectName() );
							return;
						}
					}

					VMatrix vMat(attachmentToWorld);
					MatrixTranslate( vMat, pPoint->vecOriginOffset );
					MatrixVectors( vMat.As3x4(), &vecForward, &vecRight, &vecUp );
					MatrixPosition( vMat.As3x4(), vecOrigin );

					if ( pEffect->pParticleEffect->GetIsViewModelEffect() )
					{
						FormatViewModelAttachment( vecOrigin, true );
					}
				}
			}
			break;

		case PATTACH_ABSORIGIN:
		case PATTACH_ABSORIGIN_FOLLOW:
		default:
			{
				vecOrigin = pPoint->hEntity->GetAbsOrigin() + pPoint->vecOriginOffset;
				pPoint->hEntity->GetVectors( &vecForward, &vecRight, &vecUp );
			}
			break;

		case PATTACH_ROOTBONE_FOLLOW:
			{
				C_BaseAnimating *pAnimating = pPoint->hEntity->GetBaseAnimating();

				Assert( pAnimating );
				if ( pAnimating )
				{
					matrix3x4_t rootBone;
					if ( pAnimating->GetRootBone( rootBone ) )
					{
						MatrixVectors( rootBone, &vecForward, &vecRight, &vecUp );
						MatrixPosition( rootBone, vecOrigin );
					}
				}
			}
			break;
		}
	}

	Vector vecForcedOriginOffset( 0, 0, flOffset );
	pEffect->pParticleEffect->SetControlPointOrientation( pPoint->iControlPoint, vecForward, vecRight, vecUp );
	pEffect->pParticleEffect->SetControlPointEntity( pPoint->iControlPoint, pPoint->hEntity );
	pEffect->pParticleEffect->SetControlPoint( pPoint->iControlPoint, vecOrigin + vecForcedOriginOffset );
	pEffect->pParticleEffect->SetSortOrigin( vecOrigin + vecForcedOriginOffset);
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Output all active effects
//-----------------------------------------------------------------------------
void CParticleProperty::DebugPrintEffects( void )
{
	int nCount = m_ParticleEffects.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		// for each effect...
		CNewParticleEffect *pParticleEffect = m_ParticleEffects[i].pParticleEffect.GetObject();

		if ( !pParticleEffect )
			continue;
	
		Msg( "(%d)  EffectName \"%s\"  Dormant? %s  Emission Stopped? %s \n",
			i,
			pParticleEffect->GetEffectName(),
			( pParticleEffect->m_bDormant ) ? "yes" : "no",
			( pParticleEffect->m_bEmissionStopped ) ? "yes" : "no" );
	}
}

#if defined( CLIENT_DLL ) && defined( L4D2 )
// Whole native151A70. The null guard is native, not a fallback policy.
bool CParticleProperty::IsValidEffect( CNewParticleEffect *effect )
{
	if ( !effect )
		return false;
	const int count = m_ParticleEffects.Count();
	for ( int i = 0; i < count; ++i )
	{
		if ( m_ParticleEffects[i].pParticleEffect.GetObject() == effect )
			return true;
	}
	return false;
}
#endif

//========= Copyright Valve Corporation, All rights reserved. ============//
// Compose into EXISTING shared/particle_property.cpp CLIENT_DLL+L4D2 branch.
// Real includes before memdbgon: c_baseplayer.h, particlemgr.h, physics.h.
// Native1512A0 whole control-point family, plus151C90 append/matrix writer.
// Other games retain their SDK bodies. No engine/particlelib ABI extension.
#if defined( CLIENT_DLL ) && defined( L4D2 )
extern "C" double L4D2_GlowNormalize( Vector *direction );
extern "C" bool L4D2_WaterWakeNormalizeAndIsShort( Vector *direction );

// Native106D7D38: module-lifetime U32 CRC keys/U16 RB-tree indices, NOT a
// second particle-definition map. Preserve CRC-collision suppression.
static CUtlRBTree< CRC32_t, unsigned short > s_ParticleAttachmentWarningCRCs( 0, 0, DefLessFunc( CRC32_t ) );

static void WarnMissingParticleAttachment( C_BaseAnimating *animating, const char *attachmentName, const char *particleName )
{
	if ( !animating ) return; // Genuine1510970 guard.
	const char *modelName = modelinfo->GetModelName( animating->GetModel() );
	const char *debugName = animating->GetDebugName();
	char message[256];
	// Native CFmtStrN<256> uses BufferCount255, then explicitly stores255=0.
	V_snprintf( message, 255, "%s Model '%s' doesn't have attachment '%s' to attach particle system '%s' to.\n",
		debugName, modelName, attachmentName, particleName );
	message[255] = '\0';
	CRC32_t crc;
	CRC32_Init( &crc );
	CRC32_ProcessBuffer( &crc, message, V_strlen( message ) );
	CRC32_Final( &crc );
	if ( s_ParticleAttachmentWarningCRCs.Find( crc ) == s_ParticleAttachmentWarningCRCs.InvalidIndex() )
	{
		s_ParticleAttachmentWarningCRCs.Insert( crc );
		Warning( "%s", message );
	}
}

// Whole native C1509F0: named attachments share the existing C150970 warning.
void DispatchParticleEffect( const char *pszParticleName, ParticleAttachment_t iAttachType,
	C_BaseEntity *pEntity, const char *pszAttachmentName, bool bResetAllParticlesOnEntity, int onlyDrawForLocalUser )
{
	int iAttachment = -1;
	if ( pEntity && pEntity->GetBaseAnimating() )
	{
		C_BaseAnimating *animating = pEntity->GetBaseAnimating();
		iAttachment = animating->LookupAttachment( pszAttachmentName );
		if ( iAttachment == -1 )
		{
			WarnMissingParticleAttachment( pEntity->GetBaseAnimating(), pszAttachmentName, pszParticleName );
			return;
		}
	}
	DispatchParticleEffect( pszParticleName, iAttachType, pEntity, iAttachment,
		bResetAllParticlesOnEntity, onlyDrawForLocalUser );
}

int CParticleProperty::GetParticleAttachment( C_BaseEntity *entity, const char *attachmentName, const char *particleName )
{
	if ( !entity || !entity->GetBaseAnimating() ) return -1;
	C_BaseAnimating *animating = entity->GetBaseAnimating(); // Native FRESH second query.
	const int attachment = animating->LookupAttachment( attachmentName );
	if ( attachment == -1 )
		WarnMissingParticleAttachment( entity->GetBaseAnimating(), attachmentName, particleName );
	return attachment; // Native accepts0; do NOT compare SDK INVALID_PARTICLE_ATTACHMENT0.
}

// Whole152600 named-attachment wrapper: attachment query precedes name lookup.
CNewParticleEffect *CParticleProperty::Create( const char *name, ParticleAttachment_t attachType, const char *attachmentName )
{
	const int attachment = GetParticleAttachment( GetOuter(), attachmentName, name );
	if ( attachment == -1 ) return NULL;
	return Create( name, attachType, attachment, vec3_origin, NULL );
}

// Whole151DB0: all matching records, FRESH count/base per iteration, -1 when
// there is no attachment name; carry the genuine optional local matrix.
void CParticleProperty::AddControlPoint( CNewParticleEffect *effect, int controlPoint,
	C_BaseEntity *entity, ParticleAttachment_t attachType, const char *attachmentName,
	Vector originOffset, const matrix3x4_t *localMatrix )
{
	int attachment = -1;
	if ( attachmentName ) attachment = GetParticleAttachment( entity, attachmentName, effect->GetName() );
	for ( int i = 0; i < m_ParticleEffects.Count(); ++i )
	{
		if ( m_ParticleEffects[i].pParticleEffect.GetObject() == effect )
			AddControlPoint( i, controlPoint, entity, attachType, attachment, originOffset, localMatrix );
	}
}

// Whole151180. The third integer selects LOCAL USER via effect-renderable
// v+164/15B2D0, NOT an attachment ID and NOT SDK's inverse-name bool.
void CParticleProperty::StopParticlesNamed( const char *name, bool forceRemoveInstantly, int onlyDrawForLocalUser )
{
	CParticleSystemDefinition *definition = g_pParticleSystemMgr->FindParticleSystem( name );
	if ( !definition ) return;
	const int count = m_ParticleEffects.Count();
	const bool remove = forceRemoveInstantly | ( m_iDormancyChangedAtFrame == gpGlobals->framecount );
	for ( int i = 0; i < count; ++i )
	{
		CNewParticleEffect *effect = m_ParticleEffects[i].pParticleEffect.GetObject();
		if ( effect->m_pDef() == definition &&
			( onlyDrawForLocalUser == -1 || effect->ShouldDrawForLocalUser( onlyDrawForLocalUser ) ) )
			effect->StopEmission( false, remove, false );
	}
}

// Existing sole cl_particle_batch_mode lives earlier in particle_property.cpp.
// Native152220 named wrapper resolves a real definition and warns on failure.
CNewParticleEffect *CParticleProperty::Create( const char *name, ParticleAttachment_t attachType,
	int attachment, Vector originOffset, const matrix3x4_t *localMatrix )
{
	CParticleSystemDefinition *definition = g_pParticleSystemMgr->FindParticleSystem( name );
	if ( definition ) return Create( definition, attachType, attachment, originOffset, localMatrix );
	Warning( "Attempting to create unknown particle system '%s' \n", name );
	return NULL;
}

// Whole native152060 actual definition-based creation/backend/callback phase.
CNewParticleEffect *CParticleProperty::Create( CParticleSystemDefinition *definition,
	ParticleAttachment_t attachType, int attachment, Vector originOffset, const matrix3x4_t *localMatrix )
{
	const int batchMode = cl_particle_batch_mode.GetInt();
	const bool batch = batchMode == 2 || ( batchMode == 1 && definition && definition->ShouldBatch() );
	if ( attachType == PATTACH_CUSTOMORIGIN && batch )
	{
		const int index = FindEffect( definition->GetName() );
		if ( index >= 0 )
		{
			CNewParticleEffect *effect = m_ParticleEffects[index].pParticleEffect.GetObject();
			effect->CParticleCollection::Restart(); // Main-authorized real target backend; native bool0 site.
			return effect;
		}
	}
	const int index = m_ParticleEffects.AddToTail();
	ParticleEffectList_t *record = &m_ParticleEffects[index];
	record->pParticleEffect = CNewParticleEffect::Create( m_pOuter, definition );
	if ( !record->pParticleEffect->IsValid() )
	{
		ParticleMgr()->RemoveEffect( record->pParticleEffect.GetObject() );
		return NULL;
	}
	AddControlPoint( index, 0, GetOuter(), attachType, attachment, originOffset, localMatrix );
	if ( m_pOuter )
		m_pOuter->OnNewParticleEffect( definition->GetName(), record->pParticleEffect.GetObject() );
	return record->pParticleEffect.GetObject();
}

void CParticleProperty::AddControlPoint( int effectIndex, int controlPoint,
	C_BaseEntity *entity, ParticleAttachment_t attachType, int attachment,
	Vector originOffset, const matrix3x4_t *localMatrix )
{
	ParticleEffectList_t *record = &m_ParticleEffects[effectIndex];
	// Native151AB0 InsertBefore(size) ALWAYS APPENDS, including repeated CP IDs.
	// Do not keep SDK's replace-first-ID behavior or use CP ID as vector index.
	const int index = record->pControlPoints.AddToTail();
	ParticleControlPoint_t &point = record->pControlPoints[index];
	point.iControlPoint = controlPoint;
	point.hEntity = entity;
	point.iAttachType = attachType;
	point.iAttachmentPoint = attachment;
	point.vecOriginOffset = originOffset;
	if ( localMatrix ) MatrixCopy( *localMatrix, point.m_LocalMatrix );
	else SetIdentityMatrix( point.m_LocalMatrix );
	UpdateParticleEffect( record, true, index );
}

void CParticleProperty::UpdateControlPoint( ParticleEffectList_t *record, int pointIndex, bool initializing )
{
	ParticleControlPoint_t *point = &record->pControlPoints[pointIndex];
	// Original101512C8 tests REAL definition byte703/2BFh and ARRAY INDEX0.
	// Main's game-owned DMX table preserves that data because target prebuilt
	// particles has no such member. Its separate screen-space RENDER gap stays
	// explicit: this is native control-point behavior, NOT renderer emulation.
	if ( ParticleMgr()->IsTerrorParticleDefinitionScreenSpace( record->pParticleEffect->GetName() ) && pointIndex == 0 )
	{
		record->pParticleEffect->SetControlPointOrientation( point->iControlPoint,
			Vector( 1, 0, 0 ), Vector( 0, 1, 0 ), Vector( 0, 0, 1 ) );
		record->pParticleEffect->SetControlPoint( point->iControlPoint, vec3_origin );
		return;
	}
	if ( !point->hEntity.Get() )
	{
		if ( point->iAttachType == PATTACH_WORLDORIGIN && initializing )
		{
			record->pParticleEffect->SetControlPointOrientation( point->iControlPoint,
				Vector( 1, 0, 0 ), Vector( 0, 1, 0 ), Vector( 0, 0, 1 ) );
			record->pParticleEffect->SetControlPoint( point->iControlPoint, point->vecOriginOffset );
			record->pParticleEffect->SetSortOrigin( point->vecOriginOffset );
		}
		record->pParticleEffect->SetControlPointEntity( point->iControlPoint, NULL );
		return;
	}
	if ( !initializing && !point->hEntity->GetMoveParent() &&
		( point->iAttachType == PATTACH_ABSORIGIN || point->iAttachType == PATTACH_POINT ) ) return;
	if ( point->iAttachType == PATTACH_CUSTOMORIGIN ) return;

	Vector origin, forward, right, up;
	switch ( point->iAttachType )
	{
	case PATTACH_CUSTOMORIGIN_FOLLOW:
		{
			matrix3x4_t world;
			L4D2ClientConcatTransforms( point->hEntity->RenderableToWorldTransform(), point->m_LocalMatrix, world );
			MatrixVectors( world, &forward, &right, &up );
			MatrixPosition( world, origin );
			record->pParticleEffect->SetControlPointEntity( point->iControlPoint, point->hEntity.Get() );
			record->pParticleEffect->SetControlPoint( point->iControlPoint, origin );
			record->pParticleEffect->SetSortOrigin( origin );
			record->pParticleEffect->SetControlPointOrientation( point->iControlPoint, forward, right, up );
			return;
		}
	case PATTACH_POINT:
	case PATTACH_POINT_FOLLOW:
		{
			C_BaseAnimating *animating = point->hEntity->GetBaseAnimating();
			matrix3x4_t attachment;
			// Native VM/non-VM branches perform this SAME query. Only their
			// TLS owner-slot guard differs; Main's real sole user0 omits it.
			if ( !animating || !animating->GetAttachment( point->iAttachmentPoint, attachment ) )
			{
				static bool warned = false;
				if ( !warned )
				{
					warned = true;
					// Original42FA0 has no NULL-this guard. Do not invent one or
					// SDK's retry/fallback/StopParticlesNamed removal path.
					DevWarning( "Attempted to attach particle effect %s to an unknown attachment on entity %s\n",
						record->pParticleEffect->GetName(), animating->GetDebugName() );
				}
				return;
			}
			MatrixVectors( attachment, &forward, &right, &up );
			MatrixPosition( attachment, origin );
			if ( record->pParticleEffect->GetIsViewModelEffect() ) FormatViewModelAttachment( origin, true );
			break;
		}
	case PATTACH_EYES_FOLLOW:
		{
			C_BaseEntity *entity = point->hEntity.Get();
			if ( !entity->IsPlayer() ) return;
			C_BasePlayer *player = static_cast< C_BasePlayer * >( entity );
			const Vector eye = player->EyePosition();
			origin.x = point->vecOriginOffset.x + eye.x;
			origin.y = point->vecOriginOffset.y + eye.y;
			origin.z = point->vecOriginOffset.z + eye.z;
			player->EyeVectors( &forward, &right, &up );
			break;
		}
	case PATTACH_WATERWAKE:
		{
			const Vector &entityOrigin = point->hEntity->GetAbsOrigin();
			origin.x = entityOrigin.x;
			origin.y = entityOrigin.y;
			origin.z = point->vecOriginOffset.z; // Absolute water plane; never add ownerZ.
			point->hEntity->EstimateAbsVelocity( forward );
			forward.z = 0.0f;
			if ( L4D2_WaterWakeNormalizeAndIsShort( &forward ) )
			{
				AngleVectors( point->hEntity->GetAbsAngles(), &forward );
				forward.z = 0.0f;
				L4D2_GlowNormalize( &forward );
			}
			up.Init( 0.0f, 0.0f, 1.0f );
			right.Init( forward.y, -forward.x, 0.0f );
			record->pParticleEffect->SetControlPointEntity( point->iControlPoint, point->hEntity.Get() );
			record->pParticleEffect->SetControlPoint( point->iControlPoint, origin );
			record->pParticleEffect->SetSortOrigin( origin );
			record->pParticleEffect->SetControlPointOrientation( point->iControlPoint, forward, right, up );
			return;
		}
	default:
		{
			const Vector &entityOrigin = point->hEntity->GetAbsOrigin();
			origin.x = point->vecOriginOffset.x + entityOrigin.x;
			origin.y = point->vecOriginOffset.y + entityOrigin.y;
			origin.z = point->vecOriginOffset.z + entityOrigin.z;
			point->hEntity->GetVectors( &forward, &right, &up );
			break;
		}
	}
	record->pParticleEffect->SetControlPointOrientation( point->iControlPoint, forward, right, up );
	record->pParticleEffect->SetControlPointEntity( point->iControlPoint, point->hEntity.Get() );
	record->pParticleEffect->SetControlPoint( point->iControlPoint, origin );
	record->pParticleEffect->SetSortOrigin( origin );
}
#endif
