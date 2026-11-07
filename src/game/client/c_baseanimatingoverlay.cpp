//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "c_baseanimatingoverlay.h"
#include "bone_setup.h"
#include "tier0/vprof.h"
#include "engine/ivdebugoverlay.h"
#include "datacache/imdlcache.h"
#include "eventlist.h"

#include "dt_utlvector_recv.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar r_sequence_debug;

C_BaseAnimatingOverlay::C_BaseAnimatingOverlay()
{
	// FIXME: where does this initialization go now?
	//for ( int i=0; i < MAX_OVERLAYS; i++ )
	//{
	//	memset( &m_Layer[i], 0, sizeof(m_Layer[0]) );
	//	m_Layer[i].m_nOrder = MAX_OVERLAYS;
	//}

	// FIXME: where does this initialization go now?
	// AddVar( m_Layer, &m_iv_AnimOverlay, LATCH_ANIMATION_VAR );
}

#undef CBaseAnimatingOverlay



#if defined( L4D2 )
// Native3D4E0/3D510/3D2C0/3D160: pOut is genuinely unused.
static void RecvProxy_AnimationLayerSequence( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	static_cast< C_AnimationLayer * >( pStruct )->SetSequence( pData->m_Value.m_Int );
}
static void RecvProxy_AnimationLayerCycle( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	static_cast< C_AnimationLayer * >( pStruct )->SetCycle( pData->m_Value.m_Float );
}
static void RecvProxy_AnimationLayerWeight( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	static_cast< C_AnimationLayer * >( pStruct )->SetWeight( pData->m_Value.m_Float );
}
static void RecvProxy_AnimationLayerOrder( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	static_cast< C_AnimationLayer * >( pStruct )->SetOrder( pData->m_Value.m_Int );
}
#endif
BEGIN_RECV_TABLE_NOBASE(CAnimationLayer, DT_Animationlayer)
#if defined( L4D2 )
	RecvPropInt( RECVINFO_NAME( m_nSequence, m_nSequence ), 0, RecvProxy_AnimationLayerSequence ),
	RecvPropFloat( RECVINFO_NAME( m_flCycle, m_flCycle ), 0, RecvProxy_AnimationLayerCycle ),
	RecvPropFloat( RECVINFO_NAME( m_flPrevCycle, m_flPrevCycle ) ),
	RecvPropFloat( RECVINFO_NAME( m_flWeight, m_flWeight ), 0, RecvProxy_AnimationLayerWeight ),
	RecvPropInt( RECVINFO_NAME( m_nOrder, m_nOrder ), 0, RecvProxy_AnimationLayerOrder )
#else
	RecvPropInt(	RECVINFO_NAME(m_nSequence, m_nSequence)),
	RecvPropFloat(	RECVINFO_NAME(m_flCycle, m_flCycle)),
	RecvPropFloat(	RECVINFO_NAME(m_flPrevCycle, m_flPrevCycle)),
	RecvPropFloat(	RECVINFO_NAME(m_flWeight, m_flWeight)),
	RecvPropInt(	RECVINFO_NAME(m_nOrder, m_nOrder))
#endif
END_RECV_TABLE()

const char *s_m_iv_AnimOverlayNames[C_BaseAnimatingOverlay::MAX_OVERLAYS] =
{
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay00",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay01",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay02",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay03",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay04",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay05",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay06",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay07",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay08",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay09",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay10",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay11",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay12",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay13",
	"C_BaseAnimatingOverlay::m_iv_AnimOverlay14"
};

void ResizeAnimationLayerCallback( void *pStruct, int offsetToUtlVector, int len )
{
	C_BaseAnimatingOverlay *pEnt = (C_BaseAnimatingOverlay*)pStruct;
	CUtlVector < C_AnimationLayer > *pVec = &pEnt->m_AnimOverlay;
	CUtlVector< CInterpolatedVar< C_AnimationLayer > > *pVecIV = &pEnt->m_iv_AnimOverlay;
	
	Assert( (char*)pVec - (char*)pEnt == offsetToUtlVector );
	Assert( pVec->Count() == pVecIV->Count() );
	Assert( pVec->Count() <= C_BaseAnimatingOverlay::MAX_OVERLAYS );
	
	int diff = len - pVec->Count();

	

	if ( diff == 0 )
		return;

	// remove all entries
	for ( int i=0; i < pVec->Count(); i++ )
	{
		pEnt->RemoveVar( &pVec->Element( i ) );
	}

	// adjust vector sizes
	if ( diff > 0 )
	{
		pVec->AddMultipleToTail( diff );
		pVecIV->AddMultipleToTail( diff );
	}
	else
	{
		pVec->RemoveMultiple( len, -diff );
		pVecIV->RemoveMultiple( len, -diff );
	}

	// Rebind all the variables in the ent's list.
	for ( int i=0; i < len; i++ )
	{
#if defined( L4D2 )
		// Bind before AddVar snapshots current state; buffered copies retain owner.
		pVec->Element( i ).m_pOwnerEntity = pEnt;
#endif
		IInterpolatedVar *pWatcher = &pVecIV->Element( i );
		pWatcher->SetDebugName( s_m_iv_AnimOverlayNames[i] );
		pEnt->AddVar( &pVec->Element( i ), pWatcher, LATCH_ANIMATION_VAR, true );
	}
	// FIXME: need to set historical values of nOrder in pVecIV to MAX_OVERLAY
	
}


BEGIN_RECV_TABLE_NOBASE( C_BaseAnimatingOverlay, DT_OverlayVars )
	 RecvPropUtlVector( 
		RECVINFO_UTLVECTOR_SIZEFN( m_AnimOverlay, ResizeAnimationLayerCallback ), 
		C_BaseAnimatingOverlay::MAX_OVERLAYS,
		RecvPropDataTable(NULL, 0, 0, &REFERENCE_RECV_TABLE( DT_Animationlayer ) ) )
END_RECV_TABLE()


IMPLEMENT_CLIENTCLASS_DT( C_BaseAnimatingOverlay, DT_BaseAnimatingOverlay, CBaseAnimatingOverlay )
	RecvPropDataTable( "overlay_vars", 0, 0, &REFERENCE_RECV_TABLE( DT_OverlayVars ) )
END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_BaseAnimatingOverlay )

/*
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[0][2].m_nSequence, FIELD_INTEGER ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[0][2].m_flCycle, FIELD_FLOAT ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[0][2].m_flPlaybackRate, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[0][2].m_flWeight, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[1][2].m_nSequence, FIELD_INTEGER ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[1][2].m_flCycle, FIELD_FLOAT ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[1][2].m_flPlaybackRate, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[1][2].m_flWeight, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[2][2].m_nSequence, FIELD_INTEGER ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[2][2].m_flCycle, FIELD_FLOAT ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[2][2].m_flPlaybackRate, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[2][2].m_flWeight, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[3][2].m_nSequence, FIELD_INTEGER ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[3][2].m_flCycle, FIELD_FLOAT ),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[3][2].m_flPlaybackRate, FIELD_FLOAT),
	DEFINE_FIELD( C_BaseAnimatingOverlay, m_Layer[3][2].m_flWeight, FIELD_FLOAT),
*/

END_PREDICTION_DATA()

C_AnimationLayer* C_BaseAnimatingOverlay::GetAnimOverlay( int i )
{
	Assert( i >= 0 && i < MAX_OVERLAYS );
	return &m_AnimOverlay[i];
}


#if defined( L4D2 )
void C_BaseAnimatingOverlay::SetNumAnimOverlays( int num )
{
	while ( m_AnimOverlay.Count() < num )
	{
		const int index = m_AnimOverlay.AddToTail();
		m_AnimOverlay[index].m_pOwnerEntity = this;
	}
	if ( m_AnimOverlay.Count() > num )
	{
		m_AnimOverlay.RemoveMultiple( num, m_AnimOverlay.Count() - num );
		InvalidatePhysicsRecursive( 0x10 );
	}
}
#else
void C_BaseAnimatingOverlay::SetNumAnimOverlays( int num )
{
	if ( m_AnimOverlay.Count() < num )
	{
		m_AnimOverlay.AddMultipleToTail( num - m_AnimOverlay.Count() );
	}
	else if ( m_AnimOverlay.Count() > num )
	{
		m_AnimOverlay.RemoveMultiple( num, m_AnimOverlay.Count() - num );
	}
}
#endif


int C_BaseAnimatingOverlay::GetNumAnimOverlays() const
{
	return m_AnimOverlay.Count();
}


void C_BaseAnimatingOverlay::GetRenderBounds( Vector& theMins, Vector& theMaxs )
{
	BaseClass::GetRenderBounds( theMins, theMaxs );

	if ( !IsRagdoll() )
	{
		CStudioHdr *pStudioHdr = GetModelPtr();
		if ( !pStudioHdr || !pStudioHdr->SequencesAvailable() )
			return;

		int nSequences = pStudioHdr->GetNumSeq();

		int i;
		for (i = 0; i < m_AnimOverlay.Count(); i++)
		{
			if (m_AnimOverlay[i].m_flWeight > 0.0)
			{
				if ( m_AnimOverlay[i].m_nSequence >= nSequences )
				{
					continue;
				}

				mstudioseqdesc_t &seqdesc = pStudioHdr->pSeqdesc( m_AnimOverlay[i].m_nSequence );
				VectorMin( seqdesc.bbmin, theMins, theMins );
				VectorMax( seqdesc.bbmax, theMaxs, theMaxs );
			}
		}
	}
}



void C_BaseAnimatingOverlay::CheckForLayerChanges( CStudioHdr *hdr, float currentTime )
{
	bool bLayersChanged = false;
	
	// FIXME: there has to be a better way than this.
	int i;
	for (i = 0; i < m_iv_AnimOverlay.Count(); i++)
	{
		CDisableRangeChecks disableRangeChecks; 

		int iHead, iPrev1, iPrev2;
		m_iv_AnimOverlay[i].GetInterpolationInfo( currentTime, &iHead, &iPrev1, &iPrev2 );

		// fake up previous cycle values.
		float t0;
		C_AnimationLayer *pHead = m_iv_AnimOverlay[i].GetHistoryValue( iHead, t0 );
		// reset previous
		float t1;
		C_AnimationLayer *pPrev1 = m_iv_AnimOverlay[i].GetHistoryValue( iPrev1, t1 );
		// reset previous previous
		float t2;
		C_AnimationLayer *pPrev2 = m_iv_AnimOverlay[i].GetHistoryValue( iPrev2, t2 );

		if ( pHead && pPrev1 && pHead->m_nSequence != pPrev1->m_nSequence )
		{
			bLayersChanged = true;
	#if 1 // _DEBUG
			if (/* Q_stristr( hdr->pszName(), r_sequence_debug.GetString()) != NULL || */ r_sequence_debug.GetInt() == entindex())
			{
#if defined( L4D2 )
				RecordSequenceDebug( t0, hdr->pSeqdesc( pHead->m_nSequence ).pszLabel(), pHead->m_flCycle, pHead->m_flWeight, VarArgs( "%d", i ) );
				RecordSequenceDebug( t1, hdr->pSeqdesc( pPrev1->m_nSequence ).pszLabel(), pPrev1->m_flCycle, pPrev1->m_flWeight, VarArgs( "%d", i ) );
				if ( pPrev2 )
					RecordSequenceDebug( t2, hdr->pSeqdesc( pPrev2->m_nSequence ).pszLabel(), pPrev2->m_flCycle, pPrev2->m_flWeight, VarArgs( "%d", i ) );
#else
				DevMsgRT( "(%7.4f : %30s : %5.3f : %4.2f : %1d)\n", t0, hdr->pSeqdesc( pHead->m_nSequence ).pszLabel(),  (float)pHead->m_flCycle,  (float)pHead->m_flWeight, i );
				DevMsgRT( "(%7.4f : %30s : %5.3f : %4.2f : %1d)\n", t1, hdr->pSeqdesc( pPrev1->m_nSequence ).pszLabel(),  (float)pPrev1->m_flCycle, (float)pPrev1->m_flWeight, i );
				if (pPrev2)
					DevMsgRT( "(%7.4f : %30s : %5.3f : %4.2f : %1d)\n", t2, hdr->pSeqdesc( pPrev2->m_nSequence ).pszLabel(),  (float)pPrev2->m_flCycle,  (float)pPrev2->m_flWeight, i );

#endif
			}
	#endif

			if (pPrev1)
			{
				pPrev1->m_nSequence = pHead->m_nSequence;
				pPrev1->m_flCycle = pHead->m_flPrevCycle;
				pPrev1->m_flWeight = pHead->m_flWeight;
			}

			if (pPrev2)
			{
				float num = 0;
				if ( fabs( t0 - t1 ) > 0.001f )
					num = (t2 - t1) / (t0 - t1);

				pPrev2->m_nSequence = pHead->m_nSequence;
				float flTemp;
				if (IsSequenceLooping( hdr, pHead->m_nSequence ))
				{
					flTemp = LoopingLerp( num, (float)pHead->m_flPrevCycle, (float)pHead->m_flCycle );
				}
				else
				{
					flTemp = Lerp( num, (float)pHead->m_flPrevCycle, (float)pHead->m_flCycle );
				}
				pPrev2->m_flCycle = flTemp;
				pPrev2->m_flWeight = pHead->m_flWeight;
			}

			/*
			if (stricmp( r_seq_overlay_debug.GetString(), hdr->name ) == 0)
			{
				DevMsgRT( "(%30s %6.2f : %6.2f : %6.2f)\n", hdr->pSeqdesc( pHead->nSequence ).pszLabel(), (float)pPrev2->m_flCycle, (float)pPrev1->m_flCycle, (float)pHead->m_flCycle );
			}
			*/

			m_iv_AnimOverlay[i].SetLooping( IsSequenceLooping( hdr, pHead->m_nSequence ) );
			m_iv_AnimOverlay[i].Interpolate( currentTime );

			// reset event indexes
			m_flOverlayPrevEventCycle[i] = pHead->m_flPrevCycle - 0.01;
		}
	}

	if (bLayersChanged)
	{
		// render bounds may have changed
		UpdateVisibility();
	}
}



#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_BaseAnimatingOverlay::AccumulateLayers( IBoneSetup &boneSetup, Vector pos[], Quaternion q[], float currentTime )
{
	BaseClass::AccumulateLayers( boneSetup, pos, q, currentTime );
	int layer[MAX_OVERLAYS];
	for ( int i = 0; i < MAX_OVERLAYS; ++i )
		layer[i] = MAX_OVERLAYS;
	for ( int i = 0; i < m_AnimOverlay.Count(); ++i )
	{
		const int order = m_AnimOverlay[i].m_nOrder;
		if ( order < MAX_OVERLAYS )
		{
			if ( layer[order] == MAX_OVERLAYS )
				layer[order] = i;
			else
				m_AnimOverlay[i].SetOrder( MAX_OVERLAYS );
		}
	}
	CheckForLayerChanges( boneSetup.GetStudioHdr(), currentTime );
	const int sequences = boneSetup.GetStudioHdr()->GetNumSeq();
	for ( int order = 0; order < MAX_OVERLAYS; ++order )
	{
		const int i = layer[order];
		if ( i >= m_AnimOverlay.Count() )
			continue;
		if ( m_AnimOverlay[i].m_nSequence >= sequences )
			continue;
		float weight = m_AnimOverlay[i].m_flWeight;
		if ( !( weight > 0.0f ) )
			continue;
		const float cycle = ClampCycle( m_AnimOverlay[i].m_flCycle,
			IsSequenceLooping( m_AnimOverlay[i].m_nSequence ) );
		if ( weight > 1.0f )
			weight = 1.0f;
		boneSetup.AccumulatePose( pos, q, m_AnimOverlay[i].m_nSequence, cycle, weight, currentTime, m_pIk );
		if ( r_sequence_debug.GetInt() == entindex() )
			RecordSequenceDebug( currentTime, boneSetup.GetStudioHdr()->pSeqdesc( m_AnimOverlay[i].m_nSequence ).pszLabel(),
				cycle, weight, VarArgs( "%d", i ) );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
void C_BaseAnimatingOverlay::AccumulateLayers( IBoneSetup &boneSetup, Vector pos[], Quaternion q[], float currentTime )
{
	BaseClass::AccumulateLayers( boneSetup, pos, q, currentTime );
	int i;

	// resort the layers
	int layer[MAX_OVERLAYS];
	for (i = 0; i < MAX_OVERLAYS; i++)
	{
		layer[i] = MAX_OVERLAYS;
	}
	for (i = 0; i < m_AnimOverlay.Count(); i++)
	{
		if (m_AnimOverlay[i].m_nOrder < MAX_OVERLAYS)
		{
			/*
			Assert( layer[m_AnimOverlay[i].m_nOrder] == MAX_OVERLAYS );
			layer[m_AnimOverlay[i].m_nOrder] = i;
			*/
			// hacky code until initialization of new layers is finished
			if (layer[m_AnimOverlay[i].m_nOrder] != MAX_OVERLAYS)
			{
				m_AnimOverlay[i].m_nOrder = MAX_OVERLAYS;
			}
			else
			{
				layer[m_AnimOverlay[i].m_nOrder] = i;
			}
		}
	}

	CheckForLayerChanges( boneSetup.GetStudioHdr(), currentTime );

	int nSequences = boneSetup.GetStudioHdr()->GetNumSeq();

	// add in the overlay layers
	int j;
	for (j = 0; j < MAX_OVERLAYS; j++)
	{
		i = layer[ j ];
		if (i < m_AnimOverlay.Count())
		{
			if ( m_AnimOverlay[i].m_nSequence >= nSequences )
			{
				continue;
			}

			/*
			DevMsgRT( 1 , "%.3f  %.3f  %.3f\n", currentTime, fWeight, dadt );
			debugoverlay->AddTextOverlay( GetAbsOrigin() + Vector( 0, 0, 64 ), -j - 1, 0, 
				"%2d(%s) : %6.2f : %6.2f", 
					m_AnimOverlay[i].m_nSequence,
					hdr->pSeqdesc( m_AnimOverlay[i].m_nSequence )->pszLabel(),
					m_AnimOverlay[i].m_flCycle, 
					m_AnimOverlay[i].m_flWeight
					);
			*/

			m_AnimOverlay[i].BlendWeight();

			float fWeight = m_AnimOverlay[i].m_flWeight;

			if (fWeight > 0)
			{
				// check to see if the sequence changed
				// FIXME: move this to somewhere more reasonable
				// do a nice spline interpolation of the values
				// if ( m_AnimOverlay[i].m_nSequence != m_iv_AnimOverlay.GetPrev( i )->nSequence )
				float fCycle = m_AnimOverlay[ i ].m_flCycle;

				fCycle = ClampCycle( fCycle, IsSequenceLooping( m_AnimOverlay[i].m_nSequence ) );

				if (fWeight > 1)
					fWeight = 1;

				boneSetup.AccumulatePose( pos, q, m_AnimOverlay[i].m_nSequence, fCycle, fWeight, currentTime, m_pIk );

#if 1 // _DEBUG
				if (/* Q_stristr( hdr->pszName(), r_sequence_debug.GetString()) != NULL || */ r_sequence_debug.GetInt() == entindex())
				{
					if (1)
					{
						DevMsgRT( "%8.4f : %30s : %5.3f : %4.2f : %1d\n", currentTime, boneSetup.GetStudioHdr()->pSeqdesc( m_AnimOverlay[i].m_nSequence ).pszLabel(), fCycle, fWeight, i );
					}
					else
					{
						int iHead, iPrev1, iPrev2;
						m_iv_AnimOverlay[i].GetInterpolationInfo( currentTime, &iHead, &iPrev1, &iPrev2 );

						// fake up previous cycle values.
						float t0;
						C_AnimationLayer *pHead = m_iv_AnimOverlay[i].GetHistoryValue( iHead, t0 );
						// reset previous
						float t1;
						C_AnimationLayer *pPrev1 = m_iv_AnimOverlay[i].GetHistoryValue( iPrev1, t1 );
						// reset previous previous
						float t2;
						C_AnimationLayer *pPrev2 = m_iv_AnimOverlay[i].GetHistoryValue( iPrev2, t2 );

						if ( pHead && pPrev1 && pPrev2 )
						{
							DevMsgRT( "%6.2f : %30s %6.2f (%6.2f:%6.2f:%6.2f) : %6.2f (%6.2f:%6.2f:%6.2f) : %1d\n", currentTime, boneSetup.GetStudioHdr()->pSeqdesc( m_AnimOverlay[i].m_nSequence ).pszLabel(), 
								fCycle, (float)pPrev2->m_flCycle, (float)pPrev1->m_flCycle, (float)pHead->m_flCycle,
								fWeight, (float)pPrev2->m_flWeight, (float)pPrev1->m_flWeight, (float)pHead->m_flWeight,
								i );
						}
						else
						{
							DevMsgRT( "%6.2f : %30s %6.2f : %6.2f : %1d\n", currentTime, boneSetup.GetStudioHdr()->pSeqdesc( m_AnimOverlay[i].m_nSequence ).pszLabel(), fCycle, fWeight, i );
						}

					}
				}
#endif

//#define DEBUG_TF2_OVERLAYS
#if defined( DEBUG_TF2_OVERLAYS )
				engine->Con_NPrintf( 10 + j, "%30s %6.2f : %6.2f : %1d", boneSetup.GetStudioHdr()->pSeqdesc( m_AnimOverlay[i].m_nSequence ).pszLabel(), fCycle, fWeight, i );
			}
			else
			{
				engine->Con_NPrintf( 10 + j, "%30s %6.2f : %6.2f : %1d", "            ", 0.f, 0.f, i );
#endif
			}
		}
#if defined( DEBUG_TF2_OVERLAYS )
		else
		{
			engine->Con_NPrintf( 10 + j, "%30s %6.2f : %6.2f : %1d", "            ", 0.f, 0.f, i );
		}
#endif
	}
}

#endif

#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_BaseAnimatingOverlay::DoAnimationEvents( CStudioHdr *pStudioHdr )
{
	if ( !pStudioHdr || !pStudioHdr->SequencesAvailable() )
		return;

	MDLCACHE_CRITICAL_SECTION();

	int nSequences = pStudioHdr->GetNumSeq();

	BaseClass::DoAnimationEvents( pStudioHdr );

	bool watch = false; // Q_strstr( hdr->name, "rifle" ) ? true : false;

	CheckForLayerChanges( pStudioHdr, gpGlobals->curtime ); // !!!

	int j;
	for (j = 0; j < m_AnimOverlay.Count(); j++)
	{
		if ( m_AnimOverlay[j].m_nSequence >= nSequences )
		{
			continue;
		}
#if defined( L4D2 )
		// Native 1003E610 excludes inactive layers before reading their events.
		// A shove resets event cursor zero even when it starts on layer three.
		if ( m_AnimOverlay[j].m_flWeight == 0.0f || m_AnimOverlay[j].m_nOrder == MAX_OVERLAYS )
		{
			continue;
		}
#endif

		mstudioseqdesc_t &seqdesc = pStudioHdr->pSeqdesc( m_AnimOverlay[j].m_nSequence );
		if ( seqdesc.numevents == 0 )
			continue;

		// stalled?
		if (m_AnimOverlay[j].m_flCycle == m_flOverlayPrevEventCycle[j])
			continue;

		bool bLoopingSequence = IsSequenceLooping( m_AnimOverlay[j].m_nSequence );

		bool bLooped = false;

		//in client code, m_flOverlayPrevEventCycle is set to -1 when we first start an overlay, looping or not
		if ( bLoopingSequence &&
			m_flOverlayPrevEventCycle[j] > 0.0f &&
			m_AnimOverlay[j].m_flCycle <= m_flOverlayPrevEventCycle[j] )
		{
			if (m_flOverlayPrevEventCycle[j] - m_AnimOverlay[j].m_flCycle > 0.5)
			{
				bLooped = true;
			}
			else
			{
				// things have backed up, which is bad since it'll probably result in a hitch in the animation playback
				// but, don't play events again for the same time slice
				return;
			}
		}

		mstudioevent_t *pevent = seqdesc.pEvent( 0 );

		// This makes sure events that occur at the end of a sequence occur are
		// sent before events that occur at the beginning of a sequence.
		if (bLooped)
		{
			for (int i = 0; i < (int)seqdesc.numevents; i++)
			{
				// ignore all non-client-side events
				if ( pevent[i].type & AE_TYPE_NEWEVENTSYSTEM )
				{
					if ( !( pevent[i].type & AE_TYPE_CLIENT ) )
						 continue;
				}
				else if ( pevent[i].event < 5000 ) //Adrian - Support the old event system
					continue;
			
				if ( pevent[i].cycle <= m_flOverlayPrevEventCycle[j] )
					continue;
				
				if ( watch )
				{
					Msg( "%i FE %i Looped cycle %f, prev %f ev %f (time %.3f)\n",
						gpGlobals->tickcount,
						pevent[i].event,
						pevent[i].cycle,
						m_flOverlayPrevEventCycle[j],
						(float)m_AnimOverlay[j].m_flCycle,
						gpGlobals->curtime );
				}
					
					
				FireEvent( GetAbsOrigin(), GetAbsAngles(), pevent[ i ].event, pevent[ i ].pszOptions() );
			}

			// Necessary to get the next loop working
			m_flOverlayPrevEventCycle[j] = -0.01;
		}

		for (int i = 0; i < (int)seqdesc.numevents; i++)
		{
			if ( pevent[i].type & AE_TYPE_NEWEVENTSYSTEM )
			{
				if ( !( pevent[i].type & AE_TYPE_CLIENT ) )
					 continue;
			}
			else if ( pevent[i].event < 5000 ) //Adrian - Support the old event system
				continue;

			if ( (pevent[i].cycle > m_flOverlayPrevEventCycle[j] && pevent[i].cycle <= m_AnimOverlay[j].m_flCycle) )
			{
				if ( watch )
				{
					Msg( "%i (seq: %d) FE %i Normal cycle %f, prev %f ev %f (time %.3f)\n",
						gpGlobals->tickcount,
						m_AnimOverlay[j].m_nSequence.GetRaw(),
						pevent[i].event,
						pevent[i].cycle,
						m_flOverlayPrevEventCycle[j],
						(float)m_AnimOverlay[j].m_flCycle,
						gpGlobals->curtime );
				}

				FireEvent( GetAbsOrigin(), GetAbsAngles(), pevent[ i ].event, pevent[ i ].pszOptions() );
			}
		}

		m_flOverlayPrevEventCycle[j] = m_AnimOverlay[j].m_flCycle;
	}
}
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CStudioHdr *C_BaseAnimatingOverlay::OnNewModel()
{
	CStudioHdr *hdr = BaseClass::OnNewModel();

	// Clear out animation layers
	for ( int i=0; i < m_AnimOverlay.Count(); i++ )
	{
		m_AnimOverlay[i].Reset();
		m_AnimOverlay[i].m_nOrder = MAX_OVERLAYS;
	}

	return hdr;
}

#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_AnimationLayer::Reset()
{
	if ( m_pOwnerEntity )
	{
		int flags = 0;
		if ( m_nSequence != 0 || m_flWeight != 0.0f )
			flags = 0x10;
		if ( m_flCycle != 0.0f )
			flags |= 0x08;
		if ( flags )
			m_pOwnerEntity->InvalidatePhysicsRecursive( flags );
	}
	m_nSequence = 0;
	m_flPrevCycle = 0.0f;
	m_flWeight = 0.0f;
	m_flPlaybackRate = 0.0f;
	m_flCycle = 0.0f;
	m_flLayerAnimtime = 0.0f;
	m_flLayerFadeOuttime = 0.0f;
	// These are existing SDK adapter fields, not new native network state.
	m_flBlendIn = 0.0f;
	m_flBlendOut = 0.0f;
	m_bClientBlend = false;
}

void C_AnimationLayer::SetOrder( int order )
{
	if ( m_pOwnerEntity && m_nOrder != order && ( m_nOrder == 15 || order == 15 ) )
		m_pOwnerEntity->InvalidatePhysicsRecursive( 0x10 );
	m_nOrder = order;
}

void C_AnimationLayer::SetWeight( float weight )
{
	if ( m_pOwnerEntity && m_flWeight != weight && ( m_flWeight == 0.0f || weight == 0.0f ) )
		m_pOwnerEntity->InvalidatePhysicsRecursive( 0x10 );
	m_flWeight = weight;
}

void C_AnimationLayer::SetCycle( float cycle )
{
	if ( m_pOwnerEntity && m_flCycle != cycle )
		m_pOwnerEntity->InvalidatePhysicsRecursive( 0x08 );
	m_flCycle = cycle;
}

void C_AnimationLayer::SetSequence( int sequence )
{
	if ( m_pOwnerEntity && m_nSequence != sequence )
		m_pOwnerEntity->InvalidatePhysicsRecursive( 0x10 );
	m_nSequence = sequence;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif
