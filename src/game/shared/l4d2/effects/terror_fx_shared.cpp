//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "cliententitylist.h"
#include "c_playerresource.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "igameevents.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "l4d2/stats/player_reporter.h"
#include "world.h"
#endif
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "ammodef.h"
#include "ispatialpartition.h"
#include "physics_shared.h"
#include <float.h>
#include "precache_register.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "debugoverlay_shared.h"
#include "datacache/imdlcache.h"
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
ConVar perf_fire_bullet_firstpredictedonly( "perf_fire_bullet_firstpredictedonly", "1", 0x0000200a, "Only do effects tracing, etc., first time a CUserCmd is predicted." );
ConVar z_tracer_particles( "z_tracer_particles", "1", 0x0000200a );
#else
ConVar survivor_ff_tolerance( "survivor_ff_tolerance", "26", 0x00004004, "" );
ConVar bullet_ff_through_walls( "bullet_ff_through_walls", "0", 0x00002006, "" );
ConVar upgrade_explosive_slug_force( "upgrade_explosive_slug_force", "4", 0x00006004, "" );
ConVar upgrade_explosive_bullet_force( "upgrade_explosive_bullet_force", "2", 0x00006004, "" );
ConVar upgrade_show_explosive_ammo_radius( "upgrade_show_explosive_ammo_radius", "0", 0x00006004, "" );
ConVar perf_fire_bullet_single( "perf_fire_bullet_single", "0", 0x00002006, "" );
ConVar perf_fire_bullet_firstpredictedonly( "perf_fire_bullet_firstpredictedonly", "1", 0x00002006, "Only do effects tracing, etc., first time a CUserCmd is predicted." );
ConVar z_tracer_particles( "z_tracer_particles", "1", 0x00002006 );
#endif

#ifndef CLIENT_DLL
extern ConVar sv_showdamage;
extern ConVar tongue_bullet_radius;
#endif
#ifdef CLIENT_DLL
extern ConVar bullet_ff_through_walls;
extern ConVar perf_fire_bullet_single;
#endif
extern ConVar sv_showimpacts;
extern ConVar sv_showplayerhitboxes;
extern ConVar sv_showdebugtracers;

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

const CTerrorWeaponInfo *GetTerrorWeaponInfo( int nWeaponID )
{
	if ( nWeaponID <= TERROR_WEAPON_NONE || nWeaponID > TERROR_WEAPON_UPGRADE_ITEM )
		return NULL;

	// S24EBE0/C231530: cache every handle once, including missing entries.
	static bool s_bInitialized = false;
	static WEAPON_FILE_INFO_HANDLE s_hWeaponInfo[TERROR_WEAPON_UPGRADE_ITEM + 1];
	if ( !s_bInitialized )
	{
		s_bInitialized = true;
		for ( int id = 0; id < ARRAYSIZE( s_hWeaponInfo ); ++id )
		{
			const char *pszClassname = id == TERROR_WEAPON_NONE ? "weapon_none" : WeaponIDToClassname( id );
			s_hWeaponInfo[id] = LookupWeaponInfoSlot( pszClassname );
		}
	}

	const WEAPON_FILE_INFO_HANDLE hInfo = s_hWeaponInfo[nWeaponID];
	if ( hInfo == GetInvalidWeaponInfoHandle() )
		return NULL;
	return static_cast< const CTerrorWeaponInfo * >( GetFileWeaponInfoFromHandle( hInfo ) );
}

namespace
{
	const unsigned int TERROR_BULLET_WORLD_MASK = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_MOVEABLE | CONTENTS_MONSTER;
	const unsigned int TERROR_BULLET_EXIT_MASK = TERROR_BULLET_WORLD_MASK | CONTENTS_GRATE;

#ifdef CLIENT_DLL
	void AddTerrorBulletGhostExclusions( CTraceFilterSimpleList &filter )
	{
		for ( int index = 1; index <= gpGlobals->maxClients; ++index )
		{
			CBaseEntity *entity = cl_entitylist->GetBaseEntity( index );
			if ( !entity || !entity->IsPlayer() || entity->IsDormant() )
				continue;
			if ( !g_PR )
				continue; // Skip only the invalid connection-resource-dependent call.
			if ( g_PR->IsConnected( index ) && entity->IsGhost() )
				filter.AddEntityToIgnore( entity );
		}
	}
#else
	class CTerrorBulletGhostExclusion
	{
	public:
		explicit CTerrorBulletGhostExclusion( CTraceFilterSimpleList &filter ) : m_Filter( filter ) {}
		bool operator()( CBasePlayer *player )
		{
			CTerrorPlayer *terrorPlayer = static_cast< CTerrorPlayer * >( player );
			if ( terrorPlayer->IsGhost() )
				m_Filter.AddEntityToIgnore( terrorPlayer );
			return true;
		}
	private:
		CTraceFilterSimpleList &m_Filter;
	};

	void AddTerrorBulletGhostExclusions( CTraceFilterSimpleList &filter )
	{
		CTerrorBulletGhostExclusion exclude( filter );
		ForEachPlayer( exclude );
	}

	class CTerrorBulletTongueEligibility
	{
	public:
		CTerrorBulletTongueEligibility() : m_bFound( false ) {}
		bool operator()( CBasePlayer *player )
		{
			CTerrorPlayer *terrorPlayer = static_cast< CTerrorPlayer * >( player );
			if ( !terrorPlayer->IsAlive() || terrorPlayer->GetTeamNumber() != TEAM_INFECTED || terrorPlayer->GetZombieClass() != 1 )
				return true;
			CTongue *tongue = dynamic_cast< CTongue * >( terrorPlayer->GetCustomAbility() );
			if ( !tongue || !terrorPlayer->GetTongueVictim() )
				return true;
			m_bFound = true;
			return false;
		}
		bool m_bFound;
	};

	bool CanTerrorBulletCutTongue( CTerrorPlayer *player )
	{
		CTerrorBulletTongueEligibility eligibility;
		ForEachPlayer( eligibility );
		return eligibility.m_bFound && !player->IsTongueVictim();
	}
#endif

	struct TerrorBulletTrace
	{
		TerrorBulletTrace() {}
		TerrorBulletTrace( const TerrorBulletTrace &src ) { *this = src; }
		trace_t trace;
		EHANDLE hEntity;
		ICollideable *pCollideable;
		Vector vecFarEnd;
		float flSortFraction;
		int nInsertionOrder;
	};

	class CTraceBulletEntityEnumerator : public IPartitionEnumerator
	{
	public:
		virtual IterationRetval_t EnumElement( IHandleEntity *pHandleEntity ) OVERRIDE
		{
			if ( m_Entities.Count() >= 128 )
				return ITERATION_STOP;
			m_Entities.AddToTail( pHandleEntity );
			return ITERATION_CONTINUE;
		}
		CUtlVectorFixed< IHandleEntity *, 128 > m_Entities;
	};

	int CompareTerrorBulletTraces( const TerrorBulletTrace *pLeft, const TerrorBulletTrace *pRight )
	{
		if ( pLeft->flSortFraction != pRight->flSortFraction )
			return pLeft->flSortFraction < pRight->flSortFraction ? -1 : 1;
		return pLeft->nInsertionOrder - pRight->nInsertionOrder;
	}

	bool CollectTerrorBulletTraces( ITraceFilter &filter, unsigned int nEntityMask,
		const Vector &vecStart, const Vector &vecEnd, CUtlVectorFixed< TerrorBulletTrace, 129 > &traces )
	{
		CTraceFilterWorldOnly worldFilter;
		Ray_t ray;
		ray.Init( vecStart, vecEnd );
		trace_t worldTrace;
		enginetrace->TraceRay( ray, TERROR_BULLET_WORLD_MASK, &worldFilter, &worldTrace );
		if ( worldTrace.startsolid )
			return false;

		ray.Init( vecStart, worldTrace.endpos );
		CTraceBulletEntityEnumerator enumerator;
#ifdef CLIENT_DLL
		partition->EnumerateElementsAlongRay( PARTITION_CLIENT_SOLID_EDICTS, ray, false, &enumerator );
#else
		partition->EnumerateElementsAlongRay( PARTITION_ENGINE_SOLID_EDICTS, ray, false, &enumerator );
#endif
		FOR_EACH_VEC( enumerator.m_Entities, i )
		{
			IHandleEntity *pHandle = enumerator.m_Entities[i];
			if ( !staticpropmgr->IsStaticProp( pHandle ) && !filter.ShouldHitEntity( pHandle, nEntityMask ) )
				continue;
			ICollideable *pCollideable = enginetrace->GetCollideable( pHandle );
			if ( !pCollideable )
				continue;
			trace_t trace;
			enginetrace->ClipRayToCollideable( ray, nEntityMask, pCollideable, &trace );
			if ( trace.allsolid )
				return false;
			if ( trace.fraction < 1.0f )
			{
				TerrorBulletTrace &hit = traces[traces.AddToTail()];
				hit.trace = trace;
				hit.hEntity = trace.m_pEnt;
				hit.pCollideable = pCollideable;
				hit.vecFarEnd = worldTrace.endpos;
				hit.flSortFraction = trace.fraction;
				hit.nInsertionOrder = traces.Count() - 1;
			}
		}
		if ( worldTrace.DidHit() )
		{
			TerrorBulletTrace &hit = traces[traces.AddToTail()];
			hit.trace = worldTrace;
			hit.hEntity = worldTrace.m_pEnt;
			hit.pCollideable = NULL;
			hit.vecFarEnd = vecEnd;
			hit.flSortFraction = 1.0f;
			hit.nInsertionOrder = traces.Count() - 1;
		}
		traces.Sort( CompareTerrorBulletTraces );
		return true;
	}

	float TerrorBulletDistanceSquared( const Vector &delta )
	{
		const float ySquared = delta.y * delta.y;
		const float xSquared = delta.x * delta.x;
		const float xySquared = ySquared + xSquared;
		const float zSquared = delta.z * delta.z;
		return xySquared + zSquared;
	}

	float TerrorBulletRangeFromOrigin( const Vector &delta )
	{
		// S3C53E0/C2F3A7C and S3C5C6D use (y*y + z*z) + x*x.
		// Entry distance and wall thickness above retain their distinct sum.
		const float ySquared = delta.y * delta.y;
		const float zSquared = delta.z * delta.z;
		const float yzSquared = ySquared + zSquared;
		const float xSquared = delta.x * delta.x;
		return sqrtf( yzSquared + xSquared );
	}

	float TerrorBulletDamage( const CTerrorWeaponInfo &info, float flDistance )
	{
		const float flBaseDamage = static_cast< float >( info.m_iDamage );
		if ( flDistance == 0.0f )
			return flBaseDamage;
		// S3C4832: the F32 coefficient is widened BEFORE the unstored exponent.
		const double exponent = static_cast< double >( flDistance ) * static_cast< double >( 0.0020000001f );
		float attenuation = static_cast< float >( pow( static_cast< double >( info.m_flRangeModifier ), exponent ) );
		if ( info.m_flGainRange > 0.0f && flDistance > info.m_flGainRange )
		{
			if ( flDistance > info.m_flRange )
				return 0.0f * flBaseDamage;
			const float gainDistance = flDistance - info.m_flGainRange;
			const float gainRange = info.m_flRange - info.m_flGainRange;
			const float ratio = gainDistance / gainRange;
			const float gainBase = 1.0f - ratio;
			attenuation = static_cast< float >( pow( static_cast< double >( gainBase ), static_cast< double >( 0.80000001f ) ) * static_cast< double >( attenuation ) );
		}
		return attenuation * flBaseDamage;
	}

	float TerrorPenetrationScale( int nMaterial )
	{
		switch ( nMaterial )
		{
		case 'C': return 0.4f;
		case 'D': return 0.5f;
		case 'G': return 1.0f;
		case 'M': return 0.5f;
		case 'P': return 0.4f;
		case 'T': return 0.65f;
		case 'V': return 0.5f;
		case 'W': return 1.0f;
		default: return 1.0f;
		}
	}

	bool FindTerrorWorldExit( const Vector &vecEntry, const Vector &direction, Vector &vecOutside )
	{
		vecOutside = vecEntry;
		float flDistance = 0.0f;
		for ( ;; )
		{
			vecOutside += direction * 12.0f;
			flDistance += 12.0f;
			if ( !( enginetrace->GetPointContents( vecOutside ) & TERROR_BULLET_EXIT_MASK ) )
				return true;
			// The test follows PointContents. Thus an outside sample at 36
			// succeeds despite the original 24-unit limit.
			if ( 24.0f < flDistance )
				return false;
		}
	}

	bool ProcessTerrorBulletImpact( const TerrorBulletShotContext &shot, const Vector &direction, TerrorBulletTrace &hit,
		bool bLastLayer, bool &bSeenCharacter, Vector &vecNextStart, float &flRangeSpent, unsigned int nEntityMask,
		TerrorTraceImpactCallback pImpactCallback, TerrorTraceImpactCallback pExitCallback, void *pContext )
	{
		CTerrorPlayer *pPlayer = shot.m_pShooter;
		const CTerrorWeaponInfo &info = *shot.m_pInfo;
		trace_t &trace = hit.trace;
		if ( trace.m_pEnt && hit.hEntity.Get() != trace.m_pEnt )
		{
			DevWarning( "Multi-trace error: encountered a recycled entity %p!\n", trace.m_pEnt );
			return false;
		}
		if ( trace.m_pEnt && trace.m_pEnt->IsPlayer() )
		{
			const int team = trace.m_pEnt->GetTeamNumber();
			if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
				( ( !shot.m_bSingleBullet && !bullet_ff_through_walls.GetBool() ) || bSeenCharacter ) )
			{
				flRangeSpent = FLT_MAX;
				return true;
			}
		}
		const surfacedata_t *pEntrySurface = physprops->GetSurfaceData( trace.surface.surfaceProps );
		const int nEntryMaterial = pEntrySurface->game.material;
		const bool bEntryGlass = ( trace.contents & CONTENTS_WINDOW ) && nEntryMaterial == 'Y';
		const int showImpacts = sv_showimpacts.GetInt();
#ifdef CLIENT_DLL
		if ( showImpacts == 1 || showImpacts == 2 )
		{
			NDebugOverlay::Box( trace.endpos, Vector( -2.0f, -2.0f, -2.0f ), Vector( 2.0f, 2.0f, 2.0f ), 255, 0, 0, 127, 4.0f );
			if ( trace.m_pEnt && trace.m_pEnt->IsPlayer() )
			{
				trace.m_pEnt->IsPlayer();
				static_cast< CBasePlayer * >( trace.m_pEnt )->DrawClientHitboxes( 4.0f, true );
			}
		}
#else
		if ( showImpacts == 1 || showImpacts == 3 )
		{
			NDebugOverlay::Box( trace.endpos, Vector( -2.0f, -2.0f, -2.0f ), Vector( 2.0f, 2.0f, 2.0f ), 0, 0, 255, 127, 4.0f );
			if ( trace.m_pEnt && trace.m_pEnt->IsPlayer() )
			{
				trace.m_pEnt->IsPlayer();
				static_cast< CBasePlayer * >( trace.m_pEnt )->DrawServerHitboxes( 4.0f, true );
			}
		}
#endif
		const bool bCharacter = trace.m_pEnt && trace.m_pEnt->MyCombatCharacterPointer();
		if ( bCharacter )
			bSeenCharacter = true;
		const float flDistanceSqr = TerrorBulletDistanceSquared( trace.endpos - shot.m_vecOrigin );
		const float flDistance = sqrtf( flDistanceSqr );
		const bool bFreeCharacter = bCharacter && shot.m_flCharacterPenetrationMaxDistanceSquared != 0.0f &&
			shot.m_flCharacterPenetrationMaxDistanceSquared >= flDistanceSqr;

#ifndef CLIENT_DLL
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "bullet_impact" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", pPlayer->GetUserID() );
			pEvent->SetFloat( "x", trace.endpos.x );
			pEvent->SetFloat( "y", trace.endpos.y );
			pEvent->SetFloat( "z", trace.endpos.z );
			gameeventmanager->FireEvent( pEvent );
		}
#endif
		if ( pImpactCallback )
			pImpactCallback( trace, direction, flDistance, shot, bCharacter, pContext );
#ifndef CLIENT_DLL
		if ( trace.m_pEnt )
		{
			ClearMultiDamage();
			const float baseDamage = TerrorBulletDamage( info, flDistance );
			const int hitTeam = trace.m_pEnt->GetTeamNumber();
			if ( hitTeam == TEAM_SURVIVOR || hitTeam == TEAM_L4D1_SURVIVOR )
			{
				const int shooterTeam = pPlayer->GetTeamNumber();
				if ( ( shooterTeam == TEAM_SURVIVOR || shooterTeam == TEAM_L4D1_SURVIVOR ) && pPlayer->IsIncapacitated() )
					return false;
			}
			CTakeDamageInfo damage( pPlayer, pPlayer, baseDamage, shot.m_nDamageType );
			damage.SetWeapon( pPlayer->GetActiveWeapon() );
			damage.SetAmmoType( shot.m_nAmmoType );
			const bool explosive = ( shot.m_nDamageType & 0x00800000 ) != 0;
			const float forceScale = explosive ? ( ( shot.m_nDamageType & 0x20000000 ) ? upgrade_explosive_slug_force.GetFloat() : upgrade_explosive_bullet_force.GetFloat() ) : 0.40000001f;
			CalculateBulletDamageForce( &damage, shot.m_nAmmoType, direction, trace.endpos, forceScale );
			if ( !shot.m_bSingleBullet )
				damage.SetDamageVictimIndex( -1 );
			if ( trace.m_pEnt->GetNonShadowMass() > 400.0f ||
				( !TerrorGameRules()->IsRoundInProgress() && TerrorGameRules()->IsSurvivalMode() ) )
			{
				damage.AddDamageType( 0x800 );
				Vector force = damage.GetDamageForce();
				force *= 0.0f;
				damage.SetDamageForce( force );
			}
			const int showDamage = sv_showdamage.GetInt();
			if ( showDamage == 2 || ( showDamage == 1 && trace.m_pEnt->IsPlayer() ) )
			{
				char text[20];
				V_snprintf( text, sizeof( text ), "%f", damage.GetDamage() );
				NDebugOverlay::Text( trace.endpos - Vector( 0.0f, 0.0f, 3.0f ), text, false, 4.0f );
			}
			if ( explosive )
			{
				const float pelletScale = ( shot.m_nDamageType & 0x20000000 ) ? static_cast< float >( info.m_iBullets ) : 1.0f;
				damage.SetDamage( damage.GetDamage() * pelletScale );
				float radius = damage.GetDamage() * 0.5f + 32.0f;
				if ( trace.m_pEnt == GetWorldEntity() )
					radius += 32.0f;
				RadiusDamage( damage, trace.endpos, radius, CLASS_NONE, trace.m_pEnt );
				if ( upgrade_show_explosive_ammo_radius.GetBool() )
					NDebugOverlay::Sphere( trace.endpos, vec3_angle, radius, 255, 0, 0, 0, false, 1.0f );
			}
			CBaseEntity *pHit = hit.hEntity.Get();
			if ( pHit == trace.m_pEnt && pHit )
				pHit->DispatchTraceAttack( damage, direction, &trace );
			ApplyMultiDamage();
			pHit = hit.hEntity.Get();
			if ( shot.m_bSingleBullet && pHit == trace.m_pEnt && pHit && pHit->MyCombatCharacterPointer() && pHit->GetTeamNumber() != TEAM_L4D1_SURVIVOR )
				pPlayer->AddTerrorSplatter( 1, flDistance, 0 );
		}
#endif
		flRangeSpent = FLT_MAX;
		if ( ( shot.m_nDamageType & 0x00800000 ) || ( bLastLayer && !bEntryGlass && !bFreeCharacter ) )
			return true;
		if ( bCharacter )
		{
			CBaseEntity *entity = hit.hEntity.Get();
			if ( entity && entity == trace.m_pEnt )
			{
				CBaseCombatCharacter *character = entity->MyCombatCharacterPointer();
				if ( character && character->GetZombieClass() == 8 )
					return true;
			}
		}
		if ( shot.m_flPenetrationMaxDistanceSquared > 0.0f && flDistanceSqr > shot.m_flPenetrationMaxDistanceSquared )
			return true;

		trace_t exitTrace;
		Ray_t ray;
		if ( !hit.pCollideable )
		{
			Vector vecOutside;
			if ( !FindTerrorWorldExit( trace.endpos, direction, vecOutside ) )
				return true;
			CTraceFilterWorldOnly filter;
			ray.Init( vecOutside, trace.endpos );
			enginetrace->TraceRay( ray, TERROR_BULLET_EXIT_MASK, &filter, &exitTrace );
			if ( exitTrace.startsolid )
				return true;
		}
		else
		{
			ray.Init( hit.vecFarEnd, trace.endpos );
			enginetrace->ClipRayToCollideable( ray, nEntityMask, hit.pCollideable, &exitTrace );
			if ( !bEntryGlass && !bCharacter && ( exitTrace.startsolid || TerrorBulletDistanceSquared( exitTrace.endpos - trace.endpos ) >= 576.0f ) )
				return true;
		}
		vecNextStart = exitTrace.endpos;
		flRangeSpent = TerrorBulletRangeFromOrigin( exitTrace.endpos - shot.m_vecOrigin );
		if ( bFreeCharacter )
			return false;
		const int nExitMaterial = physprops->GetSurfaceData( exitTrace.surface.surfaceProps )->game.material;
		const bool bExitGlass = ( exitTrace.contents & CONTENTS_WINDOW ) && nExitMaterial == 'Y';
		if ( bEntryGlass && bExitGlass )
		{
			return false;
		}
		float flScale = bEntryGlass ? 1.0f : TerrorPenetrationScale( nEntryMaterial );
		if ( nEntryMaterial == nExitMaterial && ( nExitMaterial == 'W' || nExitMaterial == 'M' ) )
			flScale *= 2.0f;
		const float scaledPower = shot.m_flPenetrationPowerSquared * flScale;
		const float scaledPowerSquared = scaledPower * flScale;
		if ( !bLastLayer && !( TerrorBulletDistanceSquared( exitTrace.endpos - trace.endpos ) > scaledPowerSquared ) )
		{
			if ( pExitCallback )
				pExitCallback( exitTrace, direction, flRangeSpent, shot, bCharacter, pContext );
			return true;
		}
		flRangeSpent = FLT_MAX;
		return true;
	}
}

#ifndef CLIENT_DLL
	void FinishTerrorBulletPellet( const TerrorBulletShotContext &shot, const TerrorBulletPelletResult &pellet )
	{
		if ( shot.m_bCanCutTongue )
		{
			Vector hitPoint;
			float distance;
			CTongue *tongue = CollectTerrorTongueSegments( shot.m_vecOrigin, pellet.m_LastConsumedTrace.endpos, hitPoint, distance );
			if ( tongue && tongue_bullet_radius.GetFloat() > distance )
			{
				const float range = TerrorBulletRangeFromOrigin( hitPoint - shot.m_vecOrigin );
				const float damage = TerrorBulletDamage( *shot.m_pInfo, range );
				tongue->TakeDamage( shot.m_pShooter, damage, DMG_BULLET, hitPoint, pellet.m_vecDirection );
			}
		}
		ClearMultiDamage();
		CTakeDamageInfo damage( shot.m_pShooter, shot.m_pShooter, TerrorBulletDamage( *shot.m_pInfo, 0.0f ), shot.m_nDamageType );
		damage.SetAmmoType( shot.m_nAmmoType );
		CalculateBulletDamageForce( &damage, shot.m_nAmmoType, pellet.m_vecDirection, pellet.m_LastConsumedTrace.endpos, 1.0f );
		shot.m_pShooter->TraceAttackToTriggers( damage, shot.m_vecOrigin, pellet.m_LastConsumedTrace.endpos, pellet.m_vecDirection );
		ApplyMultiDamage();
	}
#endif

void TerrorTraceBullet( CTerrorPlayer *pPlayer, const Vector &vecOrigin, const QAngle &angles, const CTerrorWeaponInfo &info, int nSeed, bool bTraceWater,
	TerrorTraceImpactCallback pImpactCallback, void *pContext, TerrorTraceImpactCallback pExitCallback, TerrorTraceFinishCallback pFinishCallback )
{
	const int showPlayer = sv_showplayerhitboxes.GetInt();
	if ( showPlayer > 0 )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( showPlayer );
		if ( player )
		{
#ifdef CLIENT_DLL
			player->DrawClientHitboxes( 4.0f, true );
#else
			player->DrawServerHitboxes( 4.0f, true );
#endif
		}
	}
	CBaseEntity *pUseEntity = pPlayer->GetUseEntity();
	CBaseEntity *pReviveOwner = pPlayer->m_reviveOwner;
	const unsigned int nEntityMask = pUseEntity ? MASK_SHOT & ~CONTENTS_HITBOX : MASK_SHOT;
	CTraceFilterSimpleList outerFilter( COLLISION_GROUP_NONE );
	outerFilter.AddEntityToIgnore( pPlayer );
	if ( pReviveOwner )
		outerFilter.AddEntityToIgnore( pReviveOwner );
	if ( pUseEntity )
		outerFilter.AddEntityToIgnore( pUseEntity );
	AddTerrorBulletGhostExclusions( outerFilter );

	TerrorBulletShotContext shot;
	shot.m_pShooter = pPlayer;
	shot.m_pInfo = &info;
	shot.m_vecOrigin = vecOrigin;
#ifdef CLIENT_DLL
	shot.m_bCanCutTongue = false; // Native client context+34 is zero.
#else
	shot.m_bCanCutTongue = CanTerrorBulletCutTongue( pPlayer );
#endif
	shot.m_vecTracerSource = pPlayer->Weapon_ShootPosition();
	shot.m_nAmmoType = info.iAmmoType;
	shot.m_nDamageType = GetAmmoDef()->DamageType( shot.m_nAmmoType );
	shot.m_flPenetrationPowerSquared = info.m_flPenetrationPower * info.m_flPenetrationPower;
	shot.m_flPenetrationMaxDistanceSquared = info.m_flPenetrationMaxDistance * info.m_flPenetrationMaxDistance;
	shot.m_flCharacterPenetrationMaxDistanceSquared = info.m_flCharacterPenetrationMaxDistance * info.m_flCharacterPenetrationMaxDistance;
	if ( info.iSlot == 0 )
	{
		CTerrorWeapon *pActiveWeapon = dynamic_cast< CTerrorWeapon * >( pPlayer->GetActiveWeapon() );
		if ( pActiveWeapon )
		{
			if ( pActiveWeapon->HasUpgrade( 0 ) )
			{
				shot.m_nDamageType |= 8;
				shot.m_flPenetrationPowerSquared = 0.0f;
			}
			if ( pActiveWeapon->HasUpgrade( 1 ) )
			{
				shot.m_nDamageType |= static_cast< int >( 0x80800040u );
				shot.m_flPenetrationPowerSquared = 0.0f;
			}
		}
	}
	shot.m_bInitiallyInWater = bTraceWater && ( enginetrace->GetPointContents( shot.m_vecTracerSource ) & ( CONTENTS_WATER | CONTENTS_SLIME ) ) != 0;
	CMDLCacheCriticalSection modelCacheLock( mdlcache );
	int nPellets = info.m_iBullets;
	const int pelletMultiplier = perf_fire_bullet_single.GetInt();
	if ( pelletMultiplier == 1 )
		nPellets = 1;
	else if ( pelletMultiplier > 1 )
		nPellets = static_cast< int >( static_cast< unsigned int >( nPellets ) * static_cast< unsigned int >( pelletMultiplier ) );
	int nominalPellets = info.m_iBullets;
	CTerrorWeapon *pCountWeapon = dynamic_cast< CTerrorWeapon * >( pPlayer->GetActiveWeapon() );
	if ( pCountWeapon && pCountWeapon->HasUpgrade( 1 ) )
	{
		nominalPellets = 1;
		nPellets = 1;
	}
	shot.m_bSingleBullet = nominalPellets == 1;
	float scatterRatio = 1.0f;
	if ( info.m_flPelletScatterYaw != 0.0f && info.m_flPelletScatterPitch != 0.0f )
		scatterRatio = info.m_flPelletScatterPitch / info.m_flPelletScatterYaw;
	for ( int nPellet = 0; nPellet < nPellets; ++nPellet )
	{
		QAngle pelletAngles = angles;
		if ( nPellet != 0 && info.m_flPelletScatterYaw != 0.0f )
		{
			const int nAdditionalSeed = static_cast< int >( static_cast< unsigned int >( nSeed ) + static_cast< unsigned int >( nPellet ) + 1u );
			const float flSpread = SharedRandomFloat( "CTerrorPlayer::FireBullet Spread", -info.m_flPelletScatterYaw, info.m_flPelletScatterYaw, nAdditionalSeed );
			const float flDirection = SharedRandomFloat( "CTerrorPlayer::FireBullet SpreadDir", 0.0f, 180.0f, nAdditionalSeed );
			float flSin, flCos;
			const float radians = static_cast< float >( ( static_cast< double >( flDirection ) + static_cast< double >( flDirection ) ) * 3.141592653589793 * 0.005555555555555556 );
			SinCos( radians, &flSin, &flCos );
			pelletAngles.x += flSin * flSpread * scatterRatio;
			pelletAngles.y += flCos * flSpread;
		}
		TerrorBulletPelletResult pellet;
		pellet.m_bHitCharacter = false;
		AngleVectors( pelletAngles, &pellet.m_vecDirection );
		const Vector vecEnd = vecOrigin + pellet.m_vecDirection * info.m_flRange;
		pellet.m_FirstConsumedTrace.startpos = vecOrigin;
		pellet.m_FirstConsumedTrace.endpos = vecEnd;
		pellet.m_FirstConsumedTrace.fraction = 1.0f;
		pellet.m_FirstConsumedTrace.allsolid = false;
		pellet.m_FirstConsumedTrace.startsolid = false;
		pellet.m_FirstConsumedTrace.m_pEnt = NULL;
		pellet.m_LastConsumedTrace = pellet.m_FirstConsumedTrace;
		pellet.m_bNoConsumedImpacts = true;
		pellet.m_bHitWater = false;
		if ( bTraceWater )
		{
			CTraceFilterWorldOnly waterFilter;
			trace_t waterTrace;
			Ray_t waterRay;
			waterRay.Init( vecOrigin, vecEnd );
			enginetrace->TraceRay( waterRay, MASK_WATER, &waterFilter, &waterTrace );
			pellet.m_bHitWater = waterTrace.fraction != 1.0f;
			pellet.m_vecWaterPoint = waterTrace.endpos;
		}
#ifdef CLIENT_DLL
		ClientEntityList().DeferEntityReleases( true );
#endif
		CTraceFilterSimpleList pelletFilter( COLLISION_GROUP_NONE );
		CTraceFilterChain filter( &outerFilter, &pelletFilter );
		Vector vecStart = vecOrigin;
		int nLayers = ( shot.m_nDamageType & 0x00800000 ) ? 1 : info.m_iPenetrationNumLayers;
		for ( int nPass = 0; nPass < 15 && nLayers > 0; ++nPass )
		{
			CUtlVectorFixed< TerrorBulletTrace, 129 > traces;
			if ( !CollectTerrorBulletTraces( filter, nEntityMask, vecStart, vecEnd, traces ) || traces.Count() == 0 )
				break;
			float flRangeSpent = 0.0f;
			FOR_EACH_VEC( traces, i )
			{
				TerrorBulletTrace &hit = traces[i];
				if ( hit.trace.m_pEnt && hit.hEntity.Get() == hit.trace.m_pEnt )
				{
					CBaseCombatCharacter *pCharacter = hit.trace.m_pEnt->MyCombatCharacterPointer();
					if ( pCharacter )
						pelletFilter.AddEntityToIgnore( pCharacter );
				}
				if ( ProcessTerrorBulletImpact( shot, pellet.m_vecDirection, hit, nLayers == 1, pellet.m_bHitCharacter, vecStart, flRangeSpent, nEntityMask, pImpactCallback, pExitCallback, pContext ) )
				{
					if ( pellet.m_bNoConsumedImpacts )
					{
						pellet.m_FirstConsumedTrace = hit.trace;
						pellet.m_bNoConsumedImpacts = false;
					}
					pellet.m_LastConsumedTrace = hit.trace;
					--nLayers;
				}
				if ( nLayers <= 0 || flRangeSpent >= info.m_flRange )
					break;
			}
			if ( flRangeSpent >= info.m_flRange )
				break;
		}
#ifdef CLIENT_DLL
		ClientEntityList().DeferEntityReleases( false );
#else
		FinishTerrorBulletPellet( shot, pellet );
		if ( sv_showdebugtracers.GetBool() )
			NDebugOverlay::VertArrow( shot.m_vecTracerSource, pellet.m_FirstConsumedTrace.endpos, 0.2f, 255, 0, 0, 255, true, 5.0f );
#endif
		if ( pFinishCallback )
			pFinishCallback( shot, pellet, pContext );
	}
#ifndef CLIENT_DLL
	GetPlayerReporter().OnBulletGroupFinished( pPlayer );
#endif
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// Sole server BulletEffects precacher, composed from Precache's retained atom.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace BulletEffectsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "BulletEffects", 0x5c4b5c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "impact_ricochet", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
