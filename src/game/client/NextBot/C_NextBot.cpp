// C_NextBot.cpp
// Client-side implementation of Next generation bot system
// Author: Michael Booth, April 2005
//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "C_NextBot.h"
#include "debugoverlay_shared.h"
#include <bitbuf.h>
#include "viewrender.h"
#if defined( L4D2 )
#include "view_shared.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#if defined( L4D2 )
#include "l4d2/effects/terror_view_frustum.h"
#endif // L4D2
#include "tier0/memdbgon.h"

#undef NextBot

#if defined( L4D2 )
extern ConVar nb_shadow_blobby_dist;
extern ConVar nb_shadow_cull_dist;
#else
ConVar NextBotShadowDist( "nb_shadow_dist", "400" );
#endif
#ifdef L4D2
ConVar NextBotStop( "nb_stop", "0", 
#if defined( L4D2 )
	0x00006008
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Stop all NextBots" );
#endif

//-----------------------------------------------------------------------------
IMPLEMENT_CLIENTCLASS_DT( C_NextBotCombatCharacter, DT_NextBot, NextBotCombatCharacter )
END_RECV_TABLE()


//-----------------------------------------------------------------------------
C_NextBotCombatCharacter::C_NextBotCombatCharacter()
{
	// Left4Dead have surfaces too steep for IK to work properly
	m_EntClientFlags |= ENTCLIENTFLAG_DONTUSEIK;

	m_shadowType = SHADOWS_SIMPLE;
	m_forcedShadowType = SHADOWS_NONE;
	m_bForceShadowType = false;

	TheClientNextBots().Register( this );
	UseClientSideAnimation();
}


//-----------------------------------------------------------------------------
C_NextBotCombatCharacter::~C_NextBotCombatCharacter()
{
	TheClientNextBots().UnRegister( this );
}


//-----------------------------------------------------------------------------
void C_NextBotCombatCharacter::Spawn( void )
{
	BaseClass::Spawn();
}


//-----------------------------------------------------------------------------
void C_NextBotCombatCharacter::UpdateClientSideAnimation()
{
	if (IsDormant())
	{
		return;
	}

	BaseClass::UpdateClientSideAnimation();
}


#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

//--------------------------------------------------------------------------------------------------------
void C_NextBotCombatCharacter::UpdateShadowLOD( void )
{
	ShadowType_t oldShadowType = m_shadowType;

	if ( m_bForceShadowType )
	{
		m_shadowType = m_forcedShadowType;
	}
	else
	{
#if defined( L4D2 )
		// C2D4F30, projected onto the one real local view. Both boundaries
		// include equality; unordered distances take the dynamic-shadow arm.
		if ( C_BasePlayer::GetLocalPlayer( 0 ) )
		{
			const Vector &localOrigin = C_BasePlayer::GetLocalPlayer( 0 )->GetAbsOrigin();
			const Vector &origin = GetAbsOrigin();
			const float x = origin.x - localOrigin.x;
			const float y = origin.y - localOrigin.y;
			const float z = origin.z - localOrigin.z;
			const float distanceSquared = ( x * x + y * y ) + z * z;
			const float cullDistance = nb_shadow_cull_dist.GetFloat();
			const float blobbyDistance = nb_shadow_blobby_dist.GetFloat();
			if ( distanceSquared > cullDistance * cullDistance )
				m_shadowType = SHADOWS_NONE;
			else if ( distanceSquared > blobbyDistance * blobbyDistance )
				m_shadowType = SHADOWS_SIMPLE;
			else
				m_shadowType = SHADOWS_RENDER_TO_TEXTURE_DYNAMIC;
		}
		else
			m_shadowType = SHADOWS_SIMPLE;
#else
#ifdef NEED_SPLITSCREEN_INTEGRATION
		FOR_EACH_VALID_SPLITSCREEN_PLAYER( hh )
		{
			C_BasePlayer *pl = C_BasePlayer::GetLocalPlayer(hh);
			if ( pl )
			{
				Vector delta = GetAbsOrigin() - C_BasePlayer::GetLocalPlayer(hh)->GetAbsOrigin();
#else
		{
			if ( C_BasePlayer::GetLocalPlayer() )
			{
				Vector delta = GetAbsOrigin() - C_BasePlayer::GetLocalPlayer()->GetAbsOrigin();
#endif
				if ( delta.IsLengthLessThan( NextBotShadowDist.GetFloat() ) )
				{
					m_shadowType = SHADOWS_RENDER_TO_TEXTURE_DYNAMIC;
				}
				else
				{
					m_shadowType = SHADOWS_SIMPLE;
				}
			}
			else
			{
				m_shadowType = SHADOWS_SIMPLE;
			}
		}
#endif
	}

	if ( oldShadowType != m_shadowType )
	{
		DestroyShadow();
	}
}

#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif


//--------------------------------------------------------------------------------------------------------
ShadowType_t C_NextBotCombatCharacter::ShadowCastType( void ) 
{
#if defined( L4D2 )
	// C2D53A0 checks the handle before the per-view visibility bit.
	if ( GetRenderHandle() == INVALID_CLIENT_RENDER_HANDLE || !IsVisible() )
#else
	if ( !IsVisible() )
#endif
		return SHADOWS_NONE;

	if ( m_shadowTimer.IsElapsed() )
	{
		m_shadowTimer.Start( 0.15f );
		UpdateShadowLOD();
	}

	return m_shadowType;
}


//--------------------------------------------------------------------------------------------------------
bool C_NextBotCombatCharacter::GetForcedShadowCastType( ShadowType_t* pForcedShadowType ) const
{
	if ( pForcedShadowType )
	{
		*pForcedShadowType = m_forcedShadowType;
	}
	return m_bForceShadowType;
}

//--------------------------------------------------------------------------------------------------------
/**
 * Singleton accessor.
 * By returning a reference, we guarantee construction of the 
 * instance before its first use.
 */
C_NextBotManager &TheClientNextBots( void )
{
	static C_NextBotManager manager;
	return manager;
}


//--------------------------------------------------------------------------------------------------------
C_NextBotManager::C_NextBotManager( void )
{
	#if defined( L4D2 )
	m_nInFrustumFrame = -1;
	#endif
}


//--------------------------------------------------------------------------------------------------------
C_NextBotManager::~C_NextBotManager()
{
}


//--------------------------------------------------------------------------------------------------------
void C_NextBotManager::Register( C_NextBotCombatCharacter *bot )
{
	m_botList.AddToTail( bot );
}


//--------------------------------------------------------------------------------------------------------
void C_NextBotManager::UnRegister( C_NextBotCombatCharacter *bot )
{
	m_botList.FindAndRemove( bot );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool C_NextBotManager::SetupInFrustumData( void )
{
#if defined( L4D2 )
	if ( IsInFrustumDataValid() ) return true;
	const GameViewFrustum_t &frustum = GetGameViewFrustum();
	if ( frustum.m_nFrame != gpGlobals->framecount ) return false;
	FOR_EACH_VEC( m_botList, iBot )
		if ( m_botList[iBot] ) m_botList[iBot]->InitFrustumData();
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer )
	{
		FOR_EACH_VEC( m_botList, iBot )
		{
			C_NextBotCombatCharacter *pBot = m_botList[iBot];
			if ( !pBot ) continue;
			Vector mins, maxs;
			pBot->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
			if ( !frustum.CullBox( mins, maxs ) )
			{
				const Vector delta = pBot->GetAbsOrigin() - pPlayer->GetAbsOrigin();
				const float distanceSqr = ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z;
				if ( pBot->GetInFrustumDistanceSqr() > distanceSqr ) pBot->SetInFrustumDistanceSqr( distanceSqr );
				pBot->SetInFrustum( true );
			}
		}
	}
	m_nInFrustumFrame = gpGlobals->framecount;
#elif defined( ENABLE_AFTER_INTEGRATION )
	// Done already this frame.
	if ( IsInFrustumDataValid() )
		return true;

	// Can we use the view data yet?
	if ( !FrustumCache()->IsValid() )
		return false;

	// Get the number of active bots.
	int nBotCount = m_botList.Count();

	// Reset.
	for ( int iBot = 0; iBot < nBotCount; ++iBot )
	{
		// Get the current bot.
		C_NextBotCombatCharacter *pBot = m_botList[iBot];
		if ( !pBot )
			continue;

		pBot->InitFrustumData();
	}

	FOR_EACH_VALID_SPLITSCREEN_PLAYER( iSlot )
	{
		ACTIVE_SPLITSCREEN_PLAYER_GUARD( iSlot );
		// Get the active local player.
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		if ( !pPlayer )
			continue;

		for ( int iBot = 0; iBot < nBotCount; ++iBot )
		{
			// Get the current bot.
			C_NextBotCombatCharacter *pBot = m_botList[iBot];
			if ( !pBot )
				continue;

			// Are we in the view frustum?
			Vector vecMin, vecMax;
			pBot->CollisionProp()->WorldSpaceAABB( &vecMin, &vecMax );
			bool bInFrustum = !FrustumCache()->m_Frustums[iSlot].CullBox( vecMin, vecMax );
		
			if ( bInFrustum )
			{
				Vector vecSegment;
				VectorSubtract( pBot->GetAbsOrigin(), pPlayer->GetAbsOrigin(), vecSegment );
				float flDistance = vecSegment.LengthSqr();
				if ( flDistance < pBot->GetInFrustumDistanceSqr() )
				{
					pBot->SetInFrustumDistanceSqr( flDistance );
				}	

				pBot->SetInFrustum( true );
			}
		}
	}

	// Mark as setup this frame.
	m_nInFrustumFrame = gpGlobals->framecount;
#endif

	return true;
}

//--------------------------------------------------------------------------------------------------------
