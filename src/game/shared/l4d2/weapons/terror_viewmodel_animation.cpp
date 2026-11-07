//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Reached rifle viewmodel layer producers and consumers. The native
//          L4D2 BaseViewModel seam owns the paired fields declared there.
//
//=============================================================================//

#include "cbase.h"
#include "baseviewmodel_shared.h"
#include "activitylist.h"
#include "l4d2/weapons/terror_weapon.h"
#include "datacache/imdlcache.h"
#ifdef CLIENT_DLL
#include "prediction.h"
#include "c_baseplayer.h"
#include "animationlayer.h"
#include "l4d2/terror_shareddefs.h"
#endif

#include "tier0/memdbgon.h"

// Original CBaseViewModel::SendViewModelMatchingSequence, client 0x022F50;
// CTerrorWeapon layer mapping 0x30EC80, rifle override 0x2F16C0. Activities
// are resolved by registered name, never by the original image's enum IDs.
void CBaseViewModel::SendTerrorLayerSequence( int nSequence )
{
	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( GetOwningWeapon() );
	const Activity literalIdle = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_IDLE );
	const Activity idle = weapon ? weapon->TranslateViewModelActivity( literalIdle ) : literalIdle;
	const Activity sequenceActivity = static_cast< Activity >( GetSequenceActivity( nSequence ) );
	if ( sequenceActivity == idle )
		return;
	const int layer = weapon ? weapon->GetViewModelLayer( sequenceActivity ) : 0;
	MDLCACHE_CRITICAL_SECTION();
	if ( sequenceActivity == TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_DEPLOY ) )
		ResetSequence( SelectWeightedSequence( idle ) );
	else if ( sequenceActivity == literalIdle )
		return;
	if ( weapon )
	{
		const Activity mapped = weapon->TranslateViewModelActivity(
			weapon->TranslateViewModelLayerActivity( sequenceActivity ) );
		if ( mapped != sequenceActivity )
			nSequence = SelectWeightedSequence( mapped );
	}
	m_nLayerSequence = nSequence;
	m_nLayer = layer;
	m_flLayerStartTime = gpGlobals->curtime;
	m_nAnimationParity = ( m_nAnimationParity + 1 ) & 7;
	ApplyTerrorLayerSequence( nSequence, layer, gpGlobals->curtime );
#ifdef CLIENT_DLL
	m_nOldAnimationParity = m_nAnimationParity;
#endif
}

void CBaseViewModel::ApplyTerrorLayerSequence( int nSequence, int nLayer, float flStartTime )
{
	if ( GetNumAnimOverlays() != 4 )
	{
		SetNumAnimOverlays( 4 );
		for ( int i = 0; i < 4; ++i )
		{
			CAnimationLayer *pLayer = GetAnimOverlay( i );
#ifdef CLIENT_DLL
			pLayer->SetWeight( 0.0f );
#else
			pLayer->m_flWeight = 0;
#endif
			pLayer->SetOrder( 15 );
		}
	}

	CAnimationLayer *pLayer = GetAnimOverlay( nLayer );
#ifdef CLIENT_DLL
	pLayer->SetCycle( 0.0f );
	pLayer->m_flPrevCycle = 0.0f;
	pLayer->SetSequence( nSequence );
	pLayer->m_flPlaybackRate = static_cast< float >( GetEffectivePlaybackRate() );
	pLayer->SetWeight( 1.0f );
#else
	pLayer->m_flCycle = 0;
	pLayer->m_flPrevCycle = 0;
	pLayer->m_nSequence = nSequence;
	// S07FE88 overwrites the earlier effective-rate product with unit playback.
	// The main sequence may be stopped; its rate must not freeze weapon layers.
	pLayer->m_flPlaybackRate = 1.0f;
	pLayer->m_flWeight = 1;
#endif
	pLayer->m_flLayerAnimtime = flStartTime;
	pLayer->SetOrder( nLayer );
#ifndef CLIENT_DLL
	pLayer->m_fFlags = ANIM_LAYER_ACTIVE;
	pLayer->m_bSequenceFinished = false;
	pLayer->m_bLooping = IsSequenceLooping( nSequence );
#else
	// This is intentionally index zero, even for a layer-three shove, in
	// the original first-predicted path (0x022F50).
	if ( prediction->IsFirstTimePredicted() )
		m_flOverlayPrevEventCycle[0] = -0.01f;
#endif
}


#ifdef CLIENT_DLL

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// Full existing M4 AdvanceTerrorLayers replacement, C6B47A..838. Native setters
// are the one canonical C_AnimationLayer family, including physics dirty0x10.
void CBaseViewModel::AdvanceTerrorLayers( float currentTime, CStudioHdr *pHdr, C_BasePlayer *player, float playback )
{
	for ( int i = 0; i < GetNumAnimOverlays(); ++i )
	{
		CAnimationLayer *layer = GetAnimOverlay( i );
		float elapsed = currentTime - layer->m_flLayerAnimtime;
		if ( IsClientCreated() )
		{
			const float now = player ? player->GetFinalPredictedTime() : currentTime;
			elapsed = now - layer->m_flLayerAnimtime;
			if ( !engine->IsPaused() )
				elapsed = ( gpGlobals->interpolation_amount * TICK_INTERVAL ) + elapsed;
			if ( elapsed < 0.0f )
				elapsed = 0.0f;
		}
		const int sequence = layer->m_nSequence;
		const float rate = GetSequenceCycleRate( pHdr, sequence );
		const double product = static_cast< double >( rate ) * static_cast< double >( elapsed ) * static_cast< double >( playback );
		float cycle = static_cast< float >( product ); // Native FST local; ST0 stays.
		if ( product >= 1.0 ) // Compare UNSTORED product, not rounded float cycle.
		{
			if ( IsSequenceLooping( pHdr, sequence ) )
			{
				cycle = static_cast< float >( fmod( static_cast< double >( cycle ), 1.0 ) );
			}
			else
			{
				switch ( LookupTerrorOriginalActivity( static_cast< Activity >( GetSequenceActivity( sequence ) ) ) )
				{
				case TERROR_ORIGINAL_ACT_VM_PULLPIN_PIPEBOMB_LAYER:
				case TERROR_ORIGINAL_ACT_VM_THROW_PIPEBOMB_LAYER:
				case TERROR_ORIGINAL_ACT_VM_PULLPIN_MOLOTOV_LAYER:
				case TERROR_ORIGINAL_ACT_VM_THROW_MOLOTOV_LAYER:
				case TERROR_ORIGINAL_ACT_VM_RELOAD_LOOP_LAYER:
				case TERROR_ORIGINAL_ACT_VM_RELOAD_LAYER:
				case TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_EXTEND_LAYER:
				case TERROR_ORIGINAL_ACT_VM_HELPINGHAND_EXTEND_LAYER:
					cycle = 1.0f;
					break;
				default:
					layer->SetWeight( 0.0f );
					layer->SetCycle( 0.99900001f );
					if ( i != 0 )
						layer->SetOrder( 15 );
					continue;
				}
			}
		}
		layer->SetCycle( cycle );
		if ( sequence < 0 || sequence >= pHdr->GetNumSeq() )
		{
			layer->SetWeight( 0.0f );
			if ( i != 0 )
				layer->SetOrder( 15 );
			continue;
		}

		float scale = 1.0f;
		if ( i == 0 )
		{
			// Native producers establish count0 or4, not a made-up shorter family.
			CAnimationLayer *melee = GetAnimOverlay( 3 );
			if ( melee->m_nSequence == 0 )
				melee->SetWeight( 0.0f );
			scale = 1.0f - melee->m_flWeight;
		}
		else if ( i == 1 )
		{
			scale = 1.0f - GetAnimOverlay( 0 )->m_flWeight;
			float elapsedFade = gpGlobals->curtime - layer->m_flLayerFadeOuttime;
			if ( elapsedFade > 0.2f )
				elapsedFade = 0.2f;
			else if ( elapsedFade < 0.0f )
				elapsedFade = 0.0f; // C6B758/762 JBE retains unordered input.
			if ( scale <= 0.0f || elapsedFade >= 0.2f )
			{
				layer->SetWeight( 0.0f );
				layer->SetOrder( 15 );
				continue;
			}
			const float fade = ( 0.2f - elapsedFade ) * 5.0f;
			if ( !( fade > scale ) ) // Native JA keeps old scale; unordered selects fade.
				scale = fade;
		}

		mstudioseqdesc_t &desc = pHdr->pSeqdesc( sequence );
		if ( ( desc.flags & STUDIO_SNAP ) || IsSequenceLooping( sequence ) )
		{
			layer->SetWeight( 1.0f );
		}
		else
		{
			const float duration = SequenceDuration( pHdr, sequence );
			const float timeInSequence = layer->m_flCycle * duration;
			if ( desc.fadeintime > timeInSequence )
				layer->SetWeight( 1.0f - ( ( desc.fadeintime - timeInSequence ) / desc.fadeintime ) );
			else
			{
				const float remaining = duration - timeInSequence;
				if ( !( desc.fadeouttime > remaining ) ) // C6B7AD JBE includes unordered.
					layer->SetWeight( 1.0f );
				else
					layer->SetWeight( 1.0f - ( ( desc.fadeouttime - remaining ) / desc.fadeouttime ) );
			}
		}
		layer->SetWeight( layer->m_flWeight * scale );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif // CLIENT_DLL
