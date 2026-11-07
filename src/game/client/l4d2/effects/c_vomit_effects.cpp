//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_te_effect_dispatch.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_baseviewmodel.h"
#include "particle_property.h"
#include "input.h"
#include "tier0/memdbgon.h"

static bool IsFirstPersonVomitView( C_TerrorPlayer *owner )
{
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	if ( local == owner && !input->CAM_IsThirdPerson() )
		return true;
	return local && local->GetObserverMode() == OBS_MODE_IN_EYE && local->GetObserverTarget() == owner;
}

static void StartVomitCallback( const CEffectData &data )
{
	C_TerrorPlayer *owner = ToTerrorPlayer( data.GetEntity() );
	if ( !owner )
		return;
	C_BaseEntity *emitter = owner;
	if ( IsFirstPersonVomitView( owner ) )
	{
		C_BaseViewModel *viewModel = owner->GetViewModel( 0 );
		// C290390 explicitly falls back to the world attachment when there
		// is no first-person viewmodel. This is not a substitute spray entity.
		if ( viewModel )
			emitter = viewModel;
	}
	emitter->ParticleProp()->Create( "boomer_vomit", PATTACH_POINT_FOLLOW, "mouth" );
}
DECLARE_CLIENT_EFFECT( "startvomit", StartVomitCallback );

static void StopVomitCallback( const CEffectData &data )
{
	C_TerrorPlayer *owner = ToTerrorPlayer( data.GetEntity() );
	if ( !owner )
		return;
	C_BaseEntity *emitter = IsFirstPersonVomitView( owner ) ? static_cast< C_BaseEntity * >( owner->GetViewModel( 0 ) ) : owner;
	// C290640 has no missing-viewmodel fallback. Main's generalized minimal-
	// skip rule omits only this undefined NULL-dependent particle operation.
	if ( emitter )
		emitter->ParticleProp()->StopParticlesNamed( "boomer_vomit", false );
}
DECLARE_CLIENT_EFFECT( "stopvomit", StopVomitCallback );
