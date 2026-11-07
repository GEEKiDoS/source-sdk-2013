//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_te_effect_dispatch.h"
#include "effect_dispatch_data.h"
#include "fx_impact.h"
#include "decals.h"
#include "IEffects.h"
#include "iefx.h"
#include "engine/IStaticPropMgr.h"
#include "l4d2/effects/terror_materials.h"
#include "tier0/memdbgon.h"

// C22FD40. KnifeSlash uses the attacker's right axis for the actual slash
// decal, then the ordinary native impact trace/material effects without a
// second decal or a generic bullet-impact sound.
static void KnifeSlashCallback( const CEffectData &data )
{
	Vector origin, start, shotDirection;
	short surface;
	int material, damageType, hitbox;
	C_BaseEntity *entity = ParseImpactData( data, &origin, &start, &shotDirection, surface, material, damageType, hitbox );
	if ( !entity )
		return;
	const int decal = decalsystem->GetDecalIndexForName( GetImpactDecal( entity, material, damageType ) );
	if ( decal == -1 )
		return;
	Vector right;
	AngleVectors( data.m_vAngles, NULL, &right, NULL );
	static ConVarRef r_decals( "r_decals" );
	if ( r_decals.IsValid() && r_decals.GetInt() != 0 )
	{
		Terror_PrepareDecalMaterial( decal );
		if ( entity->entindex() != 0 || hitbox == 0 )
		{
			effects->DecalShoot( decal, entity->entindex(), entity->GetModel(), entity->GetAbsOrigin(),
				entity->GetAbsAngles(), origin, &right, 0 );
		}
		else
		{
			Vector ray = origin - start;
			const float length = VectorNormalize( ray );
			const Vector end = start + ray * ( length + 8.0f );
			trace_t trace;
			// Main-approved engine-API gap: the target static-prop API has
			// no slash-axis argument; keep its own decal orientation.
			staticpropmgr->AddDecalToStaticProp( start, end, hitbox - 1, decal, true, trace );
		}
	}
	trace_t trace;
	if ( Impact( origin, start, material, damageType, hitbox, entity, trace, data.m_fFlags ) )
		PerformCustomEffects( origin, trace, shotDirection, material, 1, 0 );
}
DECLARE_CLIENT_EFFECT( "KnifeSlash", KnifeSlashCallback );
