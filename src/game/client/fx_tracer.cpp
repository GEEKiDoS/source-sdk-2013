//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//
#include "cbase.h"
#include "fx.h"
#include "c_te_effect_dispatch.h"
#include "basecombatweapon_shared.h"
#include "baseviewmodel_shared.h"
#include "particles_new.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_drawtracers( "r_drawtracers", "1", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
ConVar r_drawtracers_firstperson( "r_drawtracers_firstperson", "1", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_ARCHIVE
#endif
, 
#if defined( L4D2 )
	""
#else
	"Toggle visibility of first person weapon tracers"
#endif
 );

#define	TRACER_SPEED			5000 

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
Vector GetTracerOrigin( const CEffectData &data )
{
	Vector vecStart = data.m_vStart;
	QAngle vecAngles;

	int iAttachment = data.m_nAttachmentIndex;

	// Attachment?
	if ( data.m_fFlags & TRACER_FLAG_USEATTACHMENT )
	{
		C_BaseViewModel *pViewModel = NULL;

		// If the entity specified is a weapon being carried by this player, use the viewmodel instead
		IClientRenderable *pRenderable = data.GetRenderable();
		if ( !pRenderable )
			return vecStart;

		C_BaseEntity *pEnt = data.GetEntity();

// This check should probably be for all multiplayer games, investigate later
#if defined( HL2MP ) || defined( TF_CLIENT_DLL )
		if ( pEnt && pEnt->IsDormant() )
			return vecStart;
#endif

		C_BaseCombatWeapon *pWpn = dynamic_cast<C_BaseCombatWeapon *>( pEnt );
#if defined( L4D2 )
		C_BasePlayer *viewPlayer = C_BasePlayer::GetLocalPlayer();
		if ( viewPlayer && viewPlayer->GetObserverMode() == OBS_MODE_IN_EYE )
		{
			C_BasePlayer *target = ToBasePlayer( viewPlayer->GetObserverTarget() );
			if ( target )
				viewPlayer = target;
		}
		if ( pWpn )
		{
			C_BasePlayer *owner = NULL;
			if ( pWpn->ShouldDrawUsingViewModel() )
				owner = ToBasePlayer( pWpn->GetOwner() );
			else if ( pWpn->GetOwner() == viewPlayer )
				owner = viewPlayer;
			pViewModel = owner ? owner->GetViewModel( 0 ) : NULL;
			if ( pViewModel )
				pRenderable = pViewModel;
		}
#else
		if ( pWpn && pWpn->ShouldDrawUsingViewModel() )
		{
			C_BasePlayer *player = ToBasePlayer( pWpn->GetOwner() );

			// Use GetRenderedWeaponModel() instead?
			pViewModel = player ? player->GetViewModel( 0 ) : NULL;
			if ( pViewModel )
			{
				// Get the viewmodel and use it instead
				pRenderable = pViewModel;
			}
		}
#endif

		// Get the attachment origin
		if ( !pRenderable->GetAttachment( iAttachment, vecStart, vecAngles ) )
		{
			DevMsg( "GetTracerOrigin: Couldn't find attachment %d on model %s\n", iAttachment, 
				modelinfo->GetModelName( pRenderable->GetModel() ) );
		}
	}

	return vecStart;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void TracerCallback( const CEffectData &data )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player )
		return;

	if ( !r_drawtracers.GetBool() )
		return;

	if ( !r_drawtracers_firstperson.GetBool() )
	{
#if defined( L4D2 )
		C_BaseEntity *entity = data.GetEntity();
		C_BaseAnimating *animating = entity ? entity->GetBaseAnimating() : NULL;
		if ( animating && animating->IsViewModel() )
#else
		C_BasePlayer *pPlayer = dynamic_cast<C_BasePlayer*>( data.GetEntity() );
		if ( pPlayer && !pPlayer->ShouldDrawThisPlayer() )
#endif
			return;
	}

	// Grab the data
	Vector vecStart = GetTracerOrigin( data );
	float flVelocity = data.m_flScale;
	bool bWhiz = (data.m_fFlags & TRACER_FLAG_WHIZ);
	int iEntIndex = data.entindex();

	if ( iEntIndex && iEntIndex == player->index )
	{
		Vector	foo = data.m_vStart;
		QAngle	vangles;
		Vector	vforward, vright, vup;

		engine->GetViewAngles( vangles );
		AngleVectors( vangles, &vforward, &vright, &vup );

		VectorMA( data.m_vStart, 4, vright, foo );
		foo[2] -= 0.5f;

		FX_PlayerTracer( foo, (Vector&)data.m_vOrigin );
		return;
	}
	
	// Use default velocity if none specified
	if ( !flVelocity )
	{
		flVelocity = TRACER_SPEED;
	}

	// Do tracer effect
	FX_Tracer( (Vector&)vecStart, (Vector&)data.m_vOrigin, flVelocity, bWhiz );
}

DECLARE_CLIENT_EFFECT( "Tracer", TracerCallback );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ParticleTracerCallback( const CEffectData &data )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player )
		return;

	if ( !r_drawtracers.GetBool() )
		return;

	if ( !r_drawtracers_firstperson.GetBool() )
	{
#if defined( L4D2 )
		C_BaseEntity *entity = data.GetEntity();
		C_BaseAnimating *animating = entity ? entity->GetBaseAnimating() : NULL;
		if ( animating && animating->IsViewModel() )
#else
		C_BasePlayer *pPlayer = dynamic_cast<C_BasePlayer*>( data.GetEntity() );
		if ( pPlayer && !pPlayer->ShouldDrawThisPlayer() )
#endif
			return;
	}

	// Grab the data
	Vector vecStart = GetTracerOrigin( data );
	Vector vecEnd = data.m_vOrigin;

	// Adjust view model tracers
	C_BaseEntity *pEntity = data.GetEntity();
#if defined( L4D2 )
	C_BaseAnimating *animating = pEntity ? pEntity->GetBaseAnimating() : NULL;
	C_BaseViewModel *viewModel = animating && animating->IsViewModel() ? static_cast< C_BaseViewModel * >( animating ) : NULL;
	const bool weaponOrViewModel = dynamic_cast< C_BaseCombatWeapon * >( pEntity ) != NULL || viewModel != NULL;
	if ( viewModel )
		viewModel->UncorrectViewModelAttachment( vecStart );
	if ( !weaponOrViewModel && data.entindex() && pEntity && pEntity->IsPlayer() && static_cast< C_BasePlayer * >( pEntity )->IsLocalPlayer() )
#else
	if ( data.entindex() && data.entindex() == player->index )
#endif
	{
		QAngle	vangles;
		Vector	vforward, vright, vup;

		engine->GetViewAngles( vangles );
		AngleVectors( vangles, &vforward, &vright, &vup );

		VectorMA( data.m_vStart, 4, vright, vecStart );
		vecStart[2] -= 0.5f;
	}

	// Create the particle effect
	QAngle vecAngles;
	Vector vecToEnd = vecEnd - vecStart;
	VectorNormalize(vecToEnd);
	VectorAngles( vecToEnd, vecAngles );
#if defined( L4D2 )
	// Native weapon arm leaves angles uninitialized; Main retains this existing
	// computed direction rather than introducing an unreproducible stack value.
	DispatchParticleEffect( data.m_nParticleSystemIndex, vecStart, vecEnd, vecAngles, NULL );
#else
	DispatchParticleEffect( data.m_nHitBox, vecStart, vecEnd, vecAngles, pEntity );
#endif

	if ( data.m_fFlags & TRACER_FLAG_WHIZ )
	{
#if defined( L4D2 )
		FX_TracerSound( data.m_vStart, vecEnd, TRACER_TYPE_DEFAULT );
#else
		FX_TracerSound( vecStart, vecEnd, TRACER_TYPE_DEFAULT );
#endif
	}
}

DECLARE_CLIENT_EFFECT( "ParticleTracer", ParticleTracerCallback );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void TracerSoundCallback( const CEffectData &data )
{
	// Grab the data
	Vector vecStart = GetTracerOrigin( data );
	
	// Do tracer effect
	FX_TracerSound( vecStart, (Vector&)data.m_vOrigin, data.m_fFlags );
}

DECLARE_CLIENT_EFFECT( "TracerSound", TracerSoundCallback );

