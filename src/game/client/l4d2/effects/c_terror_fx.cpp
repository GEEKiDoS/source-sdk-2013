//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_baseviewmodel.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "particle_parse.h"
#include "iefx.h"
#include "dlight.h"
#include "prediction.h"
#include "ammodef.h"
#include "c_impact_effects.h"
#include "c_recipientfilter.h"
#include "fx_impact.h"
#include "gamevars_shared.h"
#include "particles_new.h"
#include "util_shared.h"
#include "tier1/utlvector.h"
#include "engine/IEngineSound.h"

#include "cliententitylist.h"
#include "precache_register.h"
#include "l4d2/content/terror_precache_client.h"
#include "bspfile.h"
#include "filesystem.h"
#include "debugoverlay_shared.h"
#include "particles_localspace.h"
#include <stddef.h>
#include "tier0/memdbgon.h"

extern ConVar perf_fire_bullet_firstpredictedonly;
extern ConVar z_tracer_particles;

extern ConVar sv_showdebugtracers;

#pragma float_control( precise, on, push )
// C230010/C230280. These are the legacy dispatch callbacks, not the gun's
// script-selected PCF muzzle effects or its independently owned entity light.
static unsigned char TerrorMuzzleParticleSize( double value )
{
	return static_cast< unsigned char >( _mm_cvttsd_si32( _mm_set_sd( value ) ) );
}

static void TerrorCSMuzzleFlash( const CEffectData &data )
{
	CSmartPtr<CLocalSpaceEmitter> emitter = CLocalSpaceEmitter::Create(
		"CS_MuzzleFlash", data.m_hEntity, data.m_nAttachmentIndex, 0 );
	if ( !emitter )
		return;
	Vector center( 0, 0, 0 );
	C_BaseEntity *entity = data.GetEntity();
	if ( entity )
		center = entity->WorldSpaceCenter();
	else
	{
		IClientRenderable *renderable = data.GetRenderable();
		if ( renderable )
		{
			Vector mins, maxs;
			renderable->GetRenderBounds( mins, maxs );
			center = ( mins + maxs ) * 0.5f;
		}
	}
	emitter->GetBinding().SetBBox( center - Vector( 3, 3, 3 ), center + Vector( 3, 3, 3 ), true );
	emitter->SetNearClip( 0.0f, 5.0f );
	const PMaterialHandle material = emitter->GetPMaterial( "sprites/muzzleflash4" );
	for ( int i = 0; i < 3; ++i )
	{
		// Native leaves flags byte+55 uninitialized. Main's defined adaptation
		// retains the target AddSimpleParticle allocator's flags0.
		SimpleParticle *particle = emitter->AddSimpleParticle( material, vec3_origin, 0.079999998f );
		if ( !particle )
			continue;
		particle->m_flLifetime = 0.0f;
		particle->m_flDieTime = 0.079999998f;
		particle->m_vecVelocity = vec3_origin;
		particle->m_uchColor[0] = particle->m_uchColor[1] = particle->m_uchColor[2] = 255;
		particle->m_uchStartAlpha = 80;
		particle->m_uchEndAlpha = 30;
		particle->m_uchStartSize = TerrorMuzzleParticleSize( ( static_cast< double >( i ) + 1.0 ) * 3.0 * data.m_flScale );
		particle->m_uchEndSize = TerrorMuzzleParticleSize( static_cast< double >( particle->m_uchStartSize ) * 0.8 );
		particle->m_flRoll = static_cast< float >( random->RandomInt( 0, 3 ) );
		particle->m_flRollDelta = 0.0f;
	}
}
DECLARE_CLIENT_EFFECT( "CS_MuzzleFlash", TerrorCSMuzzleFlash );

static void TerrorCSMuzzleFlashX( const CEffectData &data )
{
	CSmartPtr<CLocalSpaceEmitter> emitter = CLocalSpaceEmitter::Create(
		"CS_MuzzleFlashX", data.m_hEntity, data.m_nAttachmentIndex, 0 );
	if ( !emitter )
		return; // Only the native undefined NULL-emitter dereference is skipped.
	C_BaseEntity *entity = data.GetEntity();
	if ( entity )
	{
		const Vector center = entity->WorldSpaceCenter();
		emitter->GetBinding().SetBBox( center - Vector( 3, 3, 3 ), center + Vector( 3, 3, 3 ), true );
	}
	emitter->SetNearClip( 0.0f, 5.0f );
	const PMaterialHandle material = emitter->GetPMaterial( "effects/muzzleflashX" );
	SimpleParticle *particle = emitter->AddSimpleParticle( material, vec3_origin, 0.079999998f );
	if ( !particle )
		return;
	particle->m_flLifetime = 0.0f;
	particle->m_flDieTime = 0.079999998f;
	particle->m_vecVelocity = vec3_origin;
	particle->m_uchColor[0] = particle->m_uchColor[1] = particle->m_uchColor[2] = 255;
	particle->m_uchStartAlpha = 130;
	particle->m_uchEndAlpha = 80;
	const float randomScale = random->RandomFloat( 0.89999998f, 1.1f );
	particle->m_uchStartSize = TerrorMuzzleParticleSize(
		static_cast< double >( randomScale ) * ( static_cast< double >( data.m_flScale ) * 6.0 ) );
	particle->m_uchEndSize = TerrorMuzzleParticleSize( static_cast< double >( particle->m_uchStartSize ) * 0.8 );
	particle->m_flRoll = random->RandomFloat( -0.25f, 0.25f );
	particle->m_flRollDelta = 0.0f;
}
DECLARE_CLIENT_EFFECT( "CS_MuzzleFlash_X", TerrorCSMuzzleFlashX );
#pragma float_control( pop )

// Main's native-capability reconstruction: engine1005E320 reads the loaded
// world's numleafwaterdata, not a cvar or an always-enabled water-effects flag.
static bool s_bTerrorLevelContainsWater;
void TerrorLevelWaterInit( const char *mapName )
{
	s_bTerrorLevelContainsWater = false;
	if ( !mapName || !mapName[0] )
		return;
	char path[MAX_PATH];
	V_snprintf( path, sizeof( path ), "maps/%s.bsp", mapName );
	FileHandle_t file = filesystem->Open( path, "rb", "GAME" );
	if ( file == FILESYSTEM_INVALID_HANDLE )
		return;
	lump_t waterLump;
	filesystem->Seek( file, offsetof( dheader_t, lumps ) + LUMP_LEAFWATERDATA * sizeof( lump_t ), FILESYSTEM_SEEK_HEAD );
	if ( filesystem->Read( &waterLump, sizeof( waterLump ), file ) == sizeof( waterLump ) )
	{
		const int count = waterLump.filelen / static_cast< int >( sizeof( dleafwaterdata_t ) );
		s_bTerrorLevelContainsWater = count != 0;
	}
	filesystem->Close( file );
}

void TerrorLevelWaterShutdown()
{
	s_bTerrorLevelContainsWater = false;
}

bool TerrorLevelContainsWater()
{
	return s_bTerrorLevelContainsWater;
}

static void TerrorTraceEffects( CTerrorPlayer *pPlayer, const Vector &origin,
	const QAngle &angles, const CTerrorWeaponInfo &info, int seed );

// C:0x2F6090. Ammo, recoil and damage never run from a received shot.
void FireTerrorBullets( CTerrorPlayer *pPlayer, const Vector &origin, const QAngle &angles,
	int weaponID, int mode, int seed, float soundTime )
{
	if ( !pPlayer )
		return;
	const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( weaponID );
	if ( !pInfo )
	{
		if ( weaponID == TERROR_WEAPON_RIFLE )
			DevMsg( "FireTerrorBullets: LookupWeaponInfoSlot failed for weapon %s\n", "weapon_rifle" );
		else
			DevMsg( "FireTerrorBullets: weapon alias for ID %d not found\n", weaponID );
		return;
	}

	// C2F6090 forwards native40/41 through the actual owner/animstate router.
	// Its intro gate and muzzle-flash dispatch remain in that single receiver.
	if ( !pPlayer->IsDormant() )
		pPlayer->DoTerrorAnimationEvent( mode != 0 ? TERROR_ANIMEVENT_FIRE_PRIMARY_ALT : TERROR_ANIMEVENT_FIRE_PRIMARY, 0 );

	// The original sound helper (C:0x2F30F0) returns for a rifle: its additional
	// sound branch requires WeaponType shotgun (3). Rifle SINGLE is emitted by
	// the real gun's WeaponSound after this call, with PAS/prediction routing.
	SetTerrorImpactParticleBudget( 3 );
	TerrorTraceEffects( pPlayer, origin, angles, *pInfo, seed );
	SetTerrorImpactParticleBudget( -1 );
}


static ConVar cl_impacteffects( "cl_impacteffects", "1", 0x0000000a );
static ConVar cl_tracers( "cl_tracers", "1", 0x0000000a );
static ConVar cl_impacteffects_limit_general( "cl_impacteffects_limit_general", "10", 0x0000000a, "Limits the number of impact effects per frame" );
static ConVar cl_impacteffects_limit_exit( "cl_impacteffects_limit_exit", "3", 0x0000000a, "Limits the number of exit impact effects per frame" );
static ConVar cl_ricochet_percent( "cl_ricochet_percent", "3", 0x0000000a, "Percent chance a bullet will create a ricochet tracer (0..10)" );
static ConVar cl_ricochet_percent_shotgun( "cl_ricochet_percent_shotgun", "0", 0x0000000a, "Percent chance a shotgun pellet will create a ricochet tracer (0..10)" );

struct TerrorImpactSound
{
	const char *m_pName;
	Vector m_Position;
};

static CUtlVectorFixedGrowable< TerrorImpactSound, 16 > *s_pTerrorImpactSounds;

static void TerrorImpactSoundRoute( const char *pName, const Vector &position )
{
	if ( !cl_impacteffects.GetBool() )
		return;
	FOR_EACH_VEC( *s_pTerrorImpactSounds, i )
	{
		const TerrorImpactSound &previous = ( *s_pTerrorImpactSounds )[i];
		if ( ( position - previous.m_Position ).LengthSqr() < 90000.0f && !Q_strcmp( previous.m_pName, pName ) )
			return;
	}
	CLocalPlayerFilter filter;
	C_BaseEntity::EmitSound( filter, SOUND_FROM_WORLD, pName, &position );
	TerrorImpactSound &record = ( *s_pTerrorImpactSounds )[s_pTerrorImpactSounds->AddToTail()];
	record.m_pName = pName;
	record.m_Position = position;
}

struct TerrorImpactParticleNames
{
	const char *m_pFull;
	const char *m_pCheap;
	int m_nFullIndex;
	int m_nCheapIndex;
};

// C:0x6D5D78/0x6D5E48. These are the mounted original graphs, not the SDK's
// different _noflecks roots or a replacement sprite/fleck implementation.
static TerrorImpactParticleNames s_TerrorImpactParticles[26] =
{
	{ NULL, NULL }, { NULL, NULL },
	{ "impact_concrete", "impact_concrete_cheap" },
	{ "impact_dirt", "impact_dirt_cheap" },
	{ NULL, NULL }, { NULL, NULL },
	{ "impact_metal", "impact_metal_cheap" },
	{ NULL, NULL }, { NULL, NULL },
	{ "impact_grass", "impact_grass_cheap" },
	{ "impact_mud", "impact_mud_cheap" },
	{ "impact_plastic", "impact_plastic_cheap" },
	{ "impact_metal", "impact_metal_cheap" },
	{ "impact_sand", "impact_sand_cheap" },
	{ "impact_leaves", "impact_leaves_cheap" },
	{ "impact_computer", "impact_computer_cheap" },
	{ "impact_asphalt", "impact_asphalt_cheap" },
	{ "impact_brick", "impact_brick_cheap" },
	{ "impact_wet", "impact_wet_cheap" },
	{ "impact_tile", "impact_tile_cheap" },
	{ "impact_cardboard", "impact_cardboard_cheap" },
	{ "impact_metal", "impact_metal_cheap" },
	{ "impact_wood", "impact_wood_cheap" },
	{ NULL, NULL },
	{ "impact_glass", "impact_glass_cheap" },
	{ NULL, NULL }
};

static TerrorImpactParticleNames s_TerrorSpecialImpactParticles[10] =
{
	{ "impact_clay", "impact_clay_cheap" },
	{ "impact_plaster", "impact_plaster_cheap" },
	{ "impact_rock", "impact_rock_cheap" },
	{ "impact_rubber", "impact_rubber_cheap" },
	{ "impact_sheetrock", "impact_sheetrock_cheap" },
	{ "impact_cloth", "impact_cloth_cheap" },
	{ "impact_carpet", "impact_carpet_cheap" },
	{ "impact_paper", "impact_paper_cheap" },
	{ "impact_upholstery", "impact_upholstery_cheap" },
	{ "impact_puddle", "impact_puddle_cheap" }
};

// Sole cache producer: native C F7BC0, supplied by Precache composition.
static short s_nImpactWaterSound;
static int s_nImpactWaterIndex;
void PrecacheTerrorImpactResources( IPrecacheHandler *pHandler, bool precache, int resourceList )
{
	for ( int i = 0; i < ARRAYSIZE( s_TerrorImpactParticles ); ++i )
	{
		TerrorImpactParticleNames &entry = s_TerrorImpactParticles[i];
		if ( entry.m_pFull )
			pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, entry.m_pFull, precache, resourceList, &entry.m_nFullIndex );
		if ( entry.m_pCheap )
			pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, entry.m_pCheap, precache, resourceList, &entry.m_nCheapIndex );
	}
	for ( int i = 0; i < ARRAYSIZE( s_TerrorSpecialImpactParticles ); ++i )
	{
		TerrorImpactParticleNames &entry = s_TerrorSpecialImpactParticles[i];
		if ( entry.m_pFull )
			pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, entry.m_pFull, precache, resourceList, &entry.m_nFullIndex );
		if ( entry.m_pCheap )
			pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, entry.m_pCheap, precache, resourceList, &entry.m_nCheapIndex );
	}
	int soundIndex = 0;
	pHandler->Precache( RESOURCETYPE_SCRIPTSOUND, "Physics.WaterSplash", precache, resourceList, &soundIndex );
	s_nImpactWaterSound = static_cast< short >( soundIndex );
	pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "impact_water", precache, resourceList, &s_nImpactWaterIndex );
}


static void SetTerrorImpactControlPoint( CNewParticleEffect *pEffect, int point,
	const Vector &position, const Vector &forward, C_BaseEntity *pEntity )
{
	Vector right, up;
	VectorVectors( forward, right, up );
	right *= -1.0f;
	pEffect->SetControlPoint( point, position );
	pEffect->SetControlPointOrientation( point, forward, right, up );
	pEffect->SetControlPointEntity( point, pEntity );
}

void PerformTerrorImpactParticles( const Vector &origin, trace_t &trace,
	const Vector &shotDirection, int material, int scale, int flags )
{
	const TerrorImpactParticleNames *pNames;
	if ( material >= 1 && material <= 10 )
		pNames = &s_TerrorSpecialImpactParticles[material - 1];
	else if ( material >= 'A' && material <= 'Z' )
		pNames = &s_TerrorImpactParticles[material - 'A'];
	else
	{
		DevMsg( "Invalid surface property.  Double-check surfaceproperties_manifest.txt\n" );
		return;
	}
	static ConVarRef drawFlecks( "r_drawflecks" );
	const bool cheap = ( !drawFlecks.GetBool() || ( flags & FLAGS_CUSTIOM_EFFECTS_NOFLECKS ) ) && pNames->m_pCheap;
	if ( !( cheap ? pNames->m_pCheap : pNames->m_pFull ) )
		return;
	const char *pName = GetParticleSystemNameFromIndex( cheap ? pNames->m_nCheapIndex : pNames->m_nFullIndex );
	CSmartPtr<CNewParticleEffect> pEffect = CNewParticleEffect::Create( NULL, pName );
	if ( !pEffect->IsValid() )
		return;
	Vector reflected;
	VectorMA( shotDirection, -2.0f * DotProduct( shotDirection, trace.plane.normal ), trace.plane.normal, reflected );
	const Vector &position = trace.fraction != 1.0f ? trace.endpos : origin;
	SetTerrorImpactControlPoint( pEffect.GetObject(), 0, position, trace.plane.normal, trace.m_pEnt );
	SetTerrorImpactControlPoint( pEffect.GetObject(), 1, position, reflected, trace.m_pEnt );
	SetTerrorImpactControlPoint( pEffect.GetObject(), 2, position, -shotDirection, trace.m_pEnt );
	pEffect->SetControlPoint( 3, Vector( scale, scale, scale ) );
	if ( pEffect->m_pDef->ReadsControlPoint( 4 ) )
	{
		Vector color;
		GetColorForSurface( &trace, &color );
		pEffect->SetControlPoint( 4, color );
	}
}

// C:0x22FC10. Reuse the native decal/ragdoll intersection primitive and surface
// sound lookup, but retain the original particle table and control points.
static void TerrorImpactCallback( const CEffectData &data )
{
	Vector origin, start, direction;
	short surface;
	int material, damageType, hitbox;
	C_BaseEntity *pEntity = ParseImpactData( data, &origin, &start, &direction, surface, material, damageType, hitbox );
	if ( !pEntity )
		return;
	trace_t trace;
	if ( Impact( origin, start, material, damageType, hitbox, pEntity, trace, ( damageType & 0x00200000 ) ? IMPACT_NODECAL : 0 ) )
	{
		PerformCustomEffects( origin, trace, direction, material, 1, 0 );
		// The RNG draw occurs even for rifle's 0x80000002 damage mask; its
		// subsequent exact DMG_BULLET comparison then rejects this sound.
		if ( random->RandomInt( 1, 10 ) <= 3 && damageType == DMG_BULLET )
		{
			CLocalPlayerFilter filter;
			C_BaseEntity::EmitSound( filter, SOUND_FROM_WORLD, "Bounce.Shrapnel", &origin );
		}
	}
	PlayImpactSound( pEntity, trace, origin, surface );
}
DECLARE_CLIENT_EFFECT( "Impact", TerrorImpactCallback );


static bool AllowTerrorImpact( bool exitImpact = false )
{
	// C:0x2F2FD0 resets against curtime with a .07-second window, despite
	// the original cvar help text saying "per frame".
	static float lastTime[2];
	static int count[2];
	int slot = exitImpact ? 1 : 0;
	if ( fabs( gpGlobals->curtime - lastTime[slot] ) > 0.07000000000000001 )
	{
		lastTime[slot] = gpGlobals->curtime;
		count[slot] = 0;
	}
	int limit = exitImpact ? cl_impacteffects_limit_exit.GetInt() : cl_impacteffects_limit_general.GetInt();
	return ++count[slot] <= limit;
}

static void TerrorAcceptedImpact( trace_t &trace, const Vector &shotDirection,
	float rangeFromStart, const TerrorBulletShotContext &shot, bool bEntryCombatCharacter, void *pContext )
{
	// C2F34F0 performs upgraded effects before the ordinary impact throttle.
	if ( shot.m_pInfo->iSlot == 0 && !( trace.surface.flags & SURF_SKY ) )
	{
		CTerrorWeapon *pActiveWeapon = dynamic_cast< CTerrorWeapon * >( shot.m_pShooter->GetActiveWeapon() );
		QAngle normalAngles;
		VectorAngles( trace.plane.normal, normalAngles );
		if ( pActiveWeapon && pActiveWeapon->HasUpgrade( 0 ) )
		{
			DispatchParticleEffect( "impact_incendiary_generic", trace.endpos, normalAngles );
		}
		else if ( shot.m_nDamageType & 0x00800000 )
		{
			const char *effect = !bEntryCombatCharacter ? "impact_explosive_ammo_world" :
				( shot.m_nDamageType & 0x20000000 ) ? "impact_explosive_ammo_large" : "impact_explosive_ammo_small";
			DispatchParticleEffect( effect, trace.endpos, normalAngles );
		}
	}
	if ( !cl_impacteffects.GetBool() || !AllowTerrorImpact() )
		return;
	if ( ( trace.surface.flags & ( SURF_SKY | SURF_HINT | SURF_SKIP ) ) ||
		( ( trace.surface.flags & SURF_NODRAW ) && !( trace.contents & CONTENTS_WINDOW ) ) )
		return;
	C_BaseEntity *pHit = trace.m_pEnt;
	if ( !pHit || ( !friendlyfire.GetBool() && pHit->GetTeamNumber() == shot.m_pShooter->GetTeamNumber() ) )
		return;
	// Native UTIL_ImpactTrace has an extra NODRAW rejection. The original's
	// WINDOW exception above must reach the entity's real ImpactTrace instead.
	pHit->ImpactTrace( &trace, shot.m_nDamageType, NULL );
}

static void TerrorAcceptedExit( trace_t &trace, const Vector &shotDirection,
	float rangeFromStart, const TerrorBulletShotContext &shot, bool bEntryCombatCharacter, void *pContext )
{
	if ( !cl_impacteffects.GetBool() || !AllowTerrorImpact( true ) || !trace.m_pEnt ||
		( trace.surface.flags & SURF_SKY ) || trace.fraction == 1.0f )
		return;
	trace.m_pEnt->ImpactTrace( &trace, shot.m_nDamageType, NULL );
}

void TerrorRicochet( const trace_t &trace, int damageType )
{
	if ( !cl_impacteffects.GetBool() || !cl_tracers.GetBool() || !( damageType & DMG_BULLET ) ||
		( trace.surface.flags & ( SURF_SKY | SURF_NODRAW | SURF_HINT | SURF_SKIP ) ) )
		return;
	int material = physprops->GetSurfaceData( trace.surface.surfaceProps )->game.material;
	switch ( material )
	{
	case 'A': case 'B': case 'F': case 'G': case 'H': case 'I': case 'S': case 'X': case 'Z':
		return;
	}
	float chance = ( damageType & 0x20000000 ) ? cl_ricochet_percent_shotgun.GetFloat() : cl_ricochet_percent.GetFloat();
	// Native CVTTSS2SI truncates the cvar, including integer-indefinite for
	// unordered/out-of-range inputs; don't compare the draw against a float.
	if ( random->RandomInt( 1, 10 ) <= _mm_cvttss_si32( _mm_set_ss( chance ) ) )
	{
		QAngle angles;
		VectorAngles( trace.plane.normal, angles );
		DispatchParticleEffect( "impact_ricochet", trace.endpos, angles );
	}
}


static void TerrorFinishShotEffects( const TerrorBulletShotContext &shot, const TerrorBulletPelletResult &pellet, void *pContext )
{
	if ( sv_showdebugtracers.GetBool() )
		NDebugOverlay::HorzArrow( shot.m_vecTracerSource, pellet.m_FirstConsumedTrace.endpos, 0.2f, 0, 0, 255, 255, true, 5.0f );
	if ( cl_tracers.GetBool() )
		shot.m_pShooter->MakeTracer( shot.m_vecTracerSource, pellet.m_FirstConsumedTrace, GetAmmoDef()->TracerType( shot.m_nAmmoType ) );
	if ( !pellet.m_bNoConsumedImpacts && !shot.m_bInitiallyInWater )
		TerrorRicochet( pellet.m_LastConsumedTrace, shot.m_nDamageType );
	if ( pellet.m_bHitWater )
	{
		QAngle angles;
		VectorAngles( Vector( 0.0f, 0.0f, 1.0f ), angles );
		DispatchParticleEffect( s_nImpactWaterIndex, pellet.m_vecWaterPoint, pellet.m_vecWaterPoint, angles, NULL, true, -1 );
		CLocalPlayerFilter filter;
		EmitSound_t sound;
		sound.m_nChannel = CHAN_VOICE;
		sound.m_pSoundName = "Physics.WaterSplash";
		sound.m_flVolume = 1.0f;
		sound.m_SoundLevel = SNDLVL_75dB;
		sound.m_nPitch = PITCH_NORM;
		sound.m_pOrigin = &pellet.m_vecWaterPoint;
		sound.m_hSoundScriptHandle = s_nImpactWaterSound;
		C_BaseEntity::EmitSound( filter, SOUND_FROM_WORLD, sound );
	}
}

static void TerrorTraceEffects( CTerrorPlayer *pPlayer, const Vector &origin,
	const QAngle &angles, const CTerrorWeaponInfo &info, int seed )
{
	if ( perf_fire_bullet_firstpredictedonly.GetBool() && prediction->InPrediction() && !prediction->IsFirstTimePredicted() )
		return;
	CUtlVectorFixedGrowable< TerrorImpactSound, 16 > sounds;
	s_pTerrorImpactSounds = &sounds;
	SetImpactSoundRoute( TerrorImpactSoundRoute );
	TerrorTraceBullet( pPlayer, origin, angles, info, seed, TerrorLevelContainsWater(),
		TerrorAcceptedImpact, NULL, TerrorAcceptedExit, TerrorFinishShotEffects );
	SetImpactSoundRoute( NULL );
	s_pTerrorImpactSounds = NULL;
}
