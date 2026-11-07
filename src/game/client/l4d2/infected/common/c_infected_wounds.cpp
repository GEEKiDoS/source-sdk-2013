//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/c_infected.h"
#include "l4d2/effects/c_guts.h"
#include "l4d2/infected/common/infected_damage_cutouts.h"
#include "particle_parse.h"
#include "particles_new.h"
#include "ragdoll_shared.h"
#include "vphysics_interface.h"
#include <xmmintrin.h>

#include "l4d2/content/terror_precache_client.h"
#include "tier0/memdbgon.h"

static ConVar z_wound_client_disabled( "z_wound_client_disabled", "0", 0x01000088 );
static ConVar z_wound_particles( "z_wound_particles", "1", 0x00004008 );
static ConVar z_gibs_per_frame( "z_gibs_per_frame", "2", 0x00004008 );
static ConVar z_gib_despawn_time( "z_gib_despawn_time", "10.0f", 0x00004008 );
static ConVar z_gib_spread_force( "z_gib_spread_force", "10000", 0x0000000A );
static ConVar z_gib_spread_randomize( "z_gib_spread_randomize", "0.1", 0x0000000A );
static ConVar z_gib_spread_torque( "z_gib_spread_torque", "150", 0x0000000A );

static int s_nBudgetFrame = -1;
static int s_nGibBudget = 0;

static int InfectedCPULevel()
{
	static ConVarRef cpu_level( "cpu_level" );
	return cpu_level.GetInt();
}

void C_Infected::PrepareGibBudgetForSimulate()
{
	if ( s_nBudgetFrame != gpGlobals->framecount )
	{
		s_nGibBudget = m_mobRush && InfectedCPULevel() == 0 ? 1 : z_gibs_per_frame.GetInt();
		s_nBudgetFrame = gpGlobals->framecount;
	}
}


static void SetInfectedWoundRegion( InfectedWoundRenderData_t &state, const InfectedDamageCutoutRegion_t &region, bool bSlash )
{
	state.m_bEnabled = true;
	state.m_vecPosition = region.m_vecCenter;
	state.m_vecAxis[0] = region.m_vecLookAt;
	state.m_vecAxis[1] = region.m_vecUp;
	state.m_vecAxis[2] = region.m_vecScale;
	state.m_vecUVMapping.Init( 0.5f, 1.0f, bSlash ? 0.5f : 0.0f, 0.0f );
	state.m_vecUVScale.Init( bSlash ? 0.75f : 0.5f, 0.5f );
}

void C_Infected::ReleaseWoundModels()
{
	for ( int i = 0; i < ARRAYSIZE( m_hWoundModel ); ++i )
	{
		if ( m_hWoundModel[i] )
		{
			m_hWoundModel[i]->Release();
			m_hWoundModel[i] = NULL;
		}
	}
}

void C_Infected::UpdateWounds()
{
	if ( UTIL_IsLowViolence() || !m_allowWounds || z_wound_client_disabled.GetBool() )
		return;

	const InfectedDamageCutoutSet *pSet = GetInfectedDamageCutouts( m_Gender );
	int nWound = m_iRequestedWound1;

	if ( nWound != m_RenderState.m_iWound[0] && nWound >= 0 && nWound < pSet->m_Cutout.Count() )
	{
		if ( m_RenderState.m_iWound[0] != -1 )
		{
			int nOldWound = m_RenderState.m_iWound[0];
			for ( int i = 0; i < pSet->m_Cutout[nOldWound].m_nRegionCount; ++i )
				m_RenderState.m_Wound[i].m_bEnabled = false;
			m_RenderState.m_iWound[0] = -1;
			m_RenderState.m_iWound[1] = -1;
			m_RenderState.m_Wound[1].m_bEnabled = false;
			ReleaseWoundModels();
			--m_nBody;
		}

		m_RenderState.m_iWound[0] = nWound;
		m_bWoundDecalsDirty = true;
		const InfectedDamageCutout_t &cutout = pSet->m_Cutout[nWound];
		for ( int i = 0; i < cutout.m_nRegionCount; ++i )
			SetInfectedWoundRegion( m_RenderState.m_Wound[i], cutout.m_Region[i], ( cutout.m_nDamageType & INFECTED_CUTOUT_SLASH ) != 0 );
		++m_nBody;

		C_BaseAnimating *pModel = new C_Guts;
		pModel->AddEffects( EF_NOSHADOW | EF_NORECEIVESHADOW );
		if ( pModel->InitializeAsClientEntity( cutout.m_szModel, false ) )
		{
			pModel->FollowEntity( this );
			m_hWoundModel[0] = pModel;
		}
		else
		{
			pModel->Release();
		}
		if ( TheClientNextBots().IsInFrustumDataValid() && IsInFrustum() )
			m_GibQueue.AddToTail( nWound );
		UpdateVisibility();
	}

	if ( m_mobRush && InfectedCPULevel() == 0 )
		return;

	// The retail split-screen suppression is not reached by this single-view target.
	{
		nWound = m_iRequestedWound2;
		if ( nWound != m_RenderState.m_iWound[1] && nWound >= 0 && nWound < pSet->m_Cutout.Count() )
		{
			if ( m_RenderState.m_iWound[1] != -1 )
			{
				m_RenderState.m_Wound[1].m_bEnabled = false;
				m_RenderState.m_iWound[1] = -1;
				if ( m_hWoundModel[1] )
				{
					m_hWoundModel[1]->Release();
					m_hWoundModel[1] = NULL;
				}
			}
			m_RenderState.m_iWound[1] = nWound;
			m_bWoundDecalsDirty = true;
			const InfectedDamageCutout_t &cutout = pSet->m_Cutout[nWound];
			SetInfectedWoundRegion( m_RenderState.m_Wound[1], cutout.m_Region[0], ( cutout.m_nDamageType & INFECTED_CUTOUT_SLASH ) != 0 );
			C_BaseAnimating *pModel = new C_Guts;
			pModel->AddEffects( EF_NOSHADOW | EF_NORECEIVESHADOW );
			if ( pModel->InitializeAsClientEntity( cutout.m_szModel, false ) )
			{
				pModel->FollowEntity( this );
				m_hWoundModel[1] = pModel;
			}
			else
			{
				pModel->Release();
			}
			if ( TheClientNextBots().IsInFrustumDataValid() && IsInFrustum() )
				m_GibQueue.AddToTail( nWound );
			UpdateVisibility();
		}
	}
}

void EmitInfectedWoundParticle( C_BaseAnimating *pEntity, int nModelType, int nWound, const Vector &vecForce )
{
	if ( !z_wound_particles.GetBool() )
		return;
	const InfectedDamageCutout_t &cutout = GetInfectedDamageCutouts( nModelType )->m_Cutout[nWound];
	if ( cutout.m_nParticleEffectCount == 0 )
		return;

	// Inclusive upper bound and a possible no-effect result are original behavior.
	int nWeight = RandomInt( 0, cutout.m_nTotalParticleWeight );
	int nEffect;
	for ( nEffect = 0; nEffect < cutout.m_nParticleEffectCount; ++nEffect )
	{
		nWeight -= cutout.m_nParticleWeight[nEffect];
		if ( nWeight < 0 )
			break;
	}
	if ( nEffect >= cutout.m_nParticleEffectCount )
		return;

	const char *pszEffect = cutout.m_szParticleEffect[nEffect];
	if ( cutout.m_szParticleAttachment[0] )
	{
		int nAttachment = pEntity->LookupAttachment( cutout.m_szParticleAttachment );
		if ( cutout.m_nDamageType & INFECTED_CUTOUT_HEADSHOT )
		{
			matrix3x4_t matrix;
			if ( !pEntity->GetAttachment( nAttachment, matrix ) )
				return; // Skip only the native uninitialized-output-dependent effect.
			Vector vecOrigin;
			MatrixGetColumn( matrix, 3, vecOrigin );
			QAngle angles;
			VectorAngles( -vecForce, angles );
			DispatchParticleEffect( GetParticleSystemIndex( pszEffect ), vecOrigin, angles, PATTACH_POINT, NULL, -1 );
		}
		else if ( nAttachment != -1 )
		{
			DispatchParticleEffect( pszEffect, PATTACH_POINT_FOLLOW, pEntity, nAttachment );
		}
	}
	else if ( cutout.m_nParticleAttachmentHitbox == -1 )
	{
		DispatchParticleEffect( pszEffect, PATTACH_ABSORIGIN_FOLLOW, pEntity );
	}
	else
	{
		DispatchParticleEffect( pszEffect, vec3_origin, Vector( (float)cutout.m_nParticleAttachmentHitbox, 0.0f, 0.0f ), vec3_angle, pEntity );
	}
}

bool C_TerrorClientRagdoll::Simulate()
{
	if ( !m_bHasInfectedState || !( m_flWoundParticleTime > 0.0f ) || !( gpGlobals->curtime >= m_flWoundParticleTime ) )
		return true;

	const InfectedDamageCutoutSet *pSet = GetInfectedDamageCutouts( m_RenderState.m_nModelType );
	for ( int i = 0; i < ARRAYSIZE( m_RenderState.m_iWound ); ++i )
	{
		int nWound = m_RenderState.m_iWound[i];
		if ( nWound >= 0 && nWound < pSet->m_Cutout.Count() && !( pSet->m_Cutout[nWound].m_nDamageType & INFECTED_CUTOUT_HEADSHOT ) )
			EmitInfectedWoundParticle( this, m_RenderState.m_nModelType, nWound, Vector( 1.0f, 0.0f, 0.0f ) );
	}
	m_flWoundParticleTime = -1.0f;
	return true;
}

void C_TerrorClientRagdoll::SUB_Remove()
{
	for ( int i = 0; i < ARRAYSIZE( m_hWoundModel ); ++i )
	{
		if ( m_hWoundModel[i] )
			m_hWoundModel[i]->AddEffects( EF_NODRAW );
	}
	if ( m_hFaceplate )
		m_hFaceplate->AddEffects( EF_NODRAW );
	BaseClass::SUB_Remove();
}


#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
static void NormalizeInfectedGibDirection( Vector &direction )
{
	// C315EAF..315F5C: epsilon follows x*x; preserve the RSQRTSS refinement order.
	__m128 lengthSquared = _mm_add_ss( _mm_mul_ss( _mm_set_ss( direction.x ), _mm_set_ss( direction.x ) ), _mm_set_ss( 1.1920928955078125e-7f ) );
	lengthSquared = _mm_add_ss( lengthSquared, _mm_mul_ss( _mm_set_ss( direction.y ), _mm_set_ss( direction.y ) ) );
	lengthSquared = _mm_add_ss( lengthSquared, _mm_mul_ss( _mm_set_ss( direction.z ), _mm_set_ss( direction.z ) ) );
	const __m128 estimate = _mm_rsqrt_ss( lengthSquared );
	const __m128 error = _mm_sub_ss( _mm_set_ss( 3.0f ), _mm_mul_ss( _mm_mul_ss( estimate, lengthSquared ), estimate ) );
	const float inverseLength = _mm_cvtss_f32( _mm_mul_ss( error, _mm_mul_ss( estimate, _mm_set_ss( 0.5f ) ) ) );
	direction.x *= inverseLength;
	direction.y *= inverseLength;
	direction.z *= inverseLength;
}

static float InfectedGibRandom( float flMin, float flMax )
{
	return (float)RandomInt( 0, 32767 ) * 0.000030518509f * ( flMax - flMin ) + flMin;
}

void C_Infected::EmitGibs( C_BaseAnimating *pSource, bool bEmitParticles )
{

	if ( m_GibQueue.Count() == 0 )
		return;

	const InfectedDamageCutoutSet *pSet = GetInfectedDamageCutouts( m_Gender );
	while ( m_GibQueue.Count() > 0 && s_nGibBudget > 0 )
	{
		int nWound = m_GibQueue[0];
		const InfectedDamageCutout_t &cutout = pSet->m_Cutout[nWound];
		if ( bEmitParticles && m_nNextGib == 0 )
			EmitInfectedWoundParticle( pSource, pSource->GetGender(), nWound, m_gibbedLimbForce );

		while ( m_nNextGib < cutout.m_nGibCount && s_nGibBudget > 0 )
		{
			C_ClientRagdoll *pGib = static_cast< C_ClientRagdoll * >( pSource->CreateRagdollCopy( RAGDOLL_TYPE_GIB, false ) );
			if ( !pGib )
				return;
			pGib->AddEffects( EF_NOSHADOW | EF_NORECEIVESHADOW );
			matrix3x4_t sourceDelta0[128], sourceCurrent[128];
			matrix3x4_t gibDelta0[128], gibCurrent[128];
			GetRagdollBoneDeltaArrays( sourceDelta0, sourceCurrent, 0.1f );
			pGib->SetModel( cutout.m_szGibModel[m_nNextGib] );

			CStudioHdr *pSourceHdr = GetModelPtr();
			CStudioHdr *pGibHdr = pGib->GetModelPtr();
			if ( pSourceHdr && pGibHdr )
			{
				Q_memset( gibDelta0, 0, sizeof( gibDelta0 ) );
				Q_memset( gibCurrent, 0, sizeof( gibCurrent ) );
				for ( int nBone = 0; nBone < pGibHdr->numbones(); ++nBone )
				{
					const char *pszBone = pGibHdr->pBone( nBone )->pszName();
					int nSourceBone = -1;
					for ( int i = 0; i < pSourceHdr->numbones(); ++i )
					{
						if ( !Q_stricmp( pSourceHdr->pBone( i )->pszName(), pszBone ) )
						{
							nSourceBone = i;
							break;
						}
					}
					if ( nSourceBone == -1 )
					{
						Warning( "Attempting to align ragdoll bone '%s' in model '%s' with nonexistent bone in parent model!\n", pszBone, cutout.m_szGibModel[m_nNextGib] );
						continue;
					}
					MatrixCopy( sourceDelta0[nSourceBone], gibDelta0[nBone] );
					MatrixCopy( sourceCurrent[nSourceBone], gibCurrent[nBone] );
				}

				pGib->InitAsClientRagdoll( gibDelta0, gibCurrent, gibCurrent, 0.1f );
				pGib->SetModelScale( GetModelScale() );
				pGib->SetMoveType( MOVETYPE_VPHYSICS );
				// C3159B0 carries the same forced policy to each real gib.
				ShadowType_t shadowType;
				if ( GetForcedShadowCastType( &shadowType ) )
					pGib->ForceShadowCastType( true, shadowType );
				IPhysicsObject *pPhysics[MAXSTUDIOBONES];
				int nPhysicsCount = pGib->VPhysicsGetObjectList( pPhysics, ARRAYSIZE( pPhysics ) );
				Vector vecForce = m_gibbedLimbForce;
				if ( cutout.m_nGibCount >= 5 && pGibHdr->numbones() > 0 && nPhysicsCount > 0 )
				{
					const Vector vecMassCenter = pPhysics[0]->GetMassCenterLocalSpace();
					Vector vecDirection;
					VectorRotate( vecMassCenter, gibCurrent[pGibHdr->numbones() - 1], vecDirection );
					NormalizeInfectedGibDirection( vecDirection );
					const float flSpread = z_gib_spread_force.GetFloat();
					const float flRandomize = z_gib_spread_randomize.GetFloat();
					const float flRandomX = InfectedGibRandom( -flRandomize, flRandomize );
					const float flRandomY = InfectedGibRandom( -flRandomize, flRandomize );
					const float flRandomZ = InfectedGibRandom( -flRandomize, flRandomize );
					vecForce.x += ( flRandomX + vecDirection.x ) * flSpread;
					vecForce.y += ( vecDirection.y + flRandomY ) * flSpread;
					vecForce.z += ( flRandomZ + vecDirection.z ) * flSpread;
					for ( int i = 0; i < nPhysicsCount; ++i )
					{
						const float flTorque = z_gib_spread_torque.GetFloat();
						AngularImpulse angular;
						angular.x = InfectedGibRandom( -1.0f, 1.0f ) * flTorque;
						angular.y = InfectedGibRandom( -1.0f, 1.0f ) * flTorque;
						angular.z = InfectedGibRandom( -1.0f, 1.0f ) * flTorque;
						pPhysics[i]->ApplyTorqueCenter( angular );
					}
				}
				for ( int i = 0; i < nPhysicsCount; ++i )
					pPhysics[i]->ApplyForceCenter( vecForce );
				if ( cutout.m_nDamageType & INFECTED_CUTOUT_DECAPITATE )
					pGib->ParticleProp()->Create( GetParticleSystemNameFromIndex( g_nInfectedBloodGibIndex ), PATTACH_ABSORIGIN_FOLLOW );
				pGib->SetRenderColor( 255, 255, 255 );
			}

			++m_nNextGib;
			--s_nGibBudget;
		}
		if ( m_nNextGib >= cutout.m_nGibCount )
		{
			m_nNextGib = 0;
			m_GibQueue.Remove( 0 );
		}
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void C_Infected::TransferWoundModels( C_BaseAnimating *pRagdoll )
{
	C_TerrorClientRagdoll *pTerrorRagdoll = dynamic_cast<C_TerrorClientRagdoll *>( pRagdoll );
	if ( !pTerrorRagdoll )
		return;

	pTerrorRagdoll->CopySourceState( m_RenderState );
	for ( int i = 0; i < ARRAYSIZE( m_hWoundModel ); ++i )
	{
		if ( m_hWoundModel[i] )
		{
			pTerrorRagdoll->AcceptWoundModel( i, m_hWoundModel[i] );
			m_hWoundModel[i] = NULL;
		}
	}
	if ( m_pFaceplate )
	{
		pTerrorRagdoll->AcceptFaceplate( m_pFaceplate );
		m_pFaceplate = NULL;
	}
	ReleaseUncommonItemModels();
	m_bRagdollTransferred = true;
}
