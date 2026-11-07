//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client's C_BaseCombatCharacter entity
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_basecombatcharacter.h"

// memdbgon must be the last include file in a .cpp file!!!
#if defined( L4D2 )
#include "c_baseplayer.h"
#include "particles_new.h"
#include "particle_parse.h"
#include "precache_register.h"
#endif
#if defined( L4D2 )
#include "c_basecombatweapon.h"
#endif // L4D2
#if defined( L4D2 )
#include "l4d2/weapons/weapon_csbase.h"
#include "l4d2/weapons/terror_weapon.h"
#endif
#include "tier0/memdbgon.h"

#if defined( CBaseCombatCharacter )
#undef CBaseCombatCharacter	
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_BaseCombatCharacter::C_BaseCombatCharacter()
{
	for ( int i=0; i < m_iAmmo.Count(); i++ )
	{
		m_iAmmo.Set( i, 0 );
	}
#if defined( L4D2 )
	m_leftFootAttachment = 0;
	m_rightFootAttachment = 0;
	m_nWaterTrailMode = 0;
	m_flWaterTrailNextProbeTime = -1000.0f;
	m_bWeaponArrayChanged = false;
	Q_memset( m_weaponIndexByID, 0, sizeof( m_weaponIndexByID ) );
#endif

#ifdef GLOWS_ENABLE
	m_pGlowEffect = NULL;
	m_bGlowEnabled = false;
	m_bOldGlowEnabled = false;
	m_bClientSideGlowEnabled = false;
#endif // GLOWS_ENABLE
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_BaseCombatCharacter::~C_BaseCombatCharacter()
{
#ifdef GLOWS_ENABLE
	DestroyGlowEffect();
#endif // GLOWS_ENABLE
}

/*
//-----------------------------------------------------------------------------
// Purpose: Returns the amount of ammunition of the specified type the character's carrying
//-----------------------------------------------------------------------------
int	C_BaseCombatCharacter::GetAmmoCount( char *szName ) const
{
	return GetAmmoCount( g_pGameRules->GetAmmoDef()->Index(szName) );
}
*/

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseCombatCharacter::OnPreDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnPreDataChanged( updateType );

#ifdef GLOWS_ENABLE
	m_bOldGlowEnabled = m_bGlowEnabled;
#endif // GLOWS_ENABLE
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
void C_BaseCombatCharacter::PostDataUpdate( DataUpdateType_t updateType )
{

	// C40240, networkable slot6 PostDataUpdate (NOT deferred OnDataChanged).
	// Its networkable-this+8 offsets4248/4249 are object4256/4257.
	if ( m_bWeaponArrayChanged )
	{
		m_bWeaponArrayChanged = false;
		Q_memset( m_weaponIndexByID, 0, sizeof( m_weaponIndexByID ) );
		for ( int slot = 0; slot < MAX_WEAPONS; ++slot )
		{
			C_BaseCombatWeapon *weapon = m_hMyWeapons[slot].Get();
			if ( weapon )
			{
				// Every registered original inventory weapon is CS-derived; native
				// calls its ID virtual directly. Never add a fake ID to the engine base.
				CWeaponCSBase *csWeapon = static_cast< CWeaponCSBase * >( weapon );
				m_weaponIndexByID[csWeapon->GetWeaponID()] = static_cast< unsigned char >( slot + 1 );
			}
		}
	}
	BaseClass::PostDataUpdate( updateType ); // AFTER cache rebuild, not before.
}
#else
void C_BaseCombatCharacter::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

#ifdef GLOWS_ENABLE
	if ( m_bOldGlowEnabled != m_bGlowEnabled )
	{
		UpdateGlowEffect();
	}
#endif // GLOWS_ENABLE
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Overload our muzzle flash and send it to any actively held weapon
//-----------------------------------------------------------------------------
void C_BaseCombatCharacter::DoMuzzleFlash()
{
	// Our weapon takes our muzzle flash command
	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		pWeapon->DoMuzzleFlash();
		//NOTENOTE: We do not chain to the base here
	}
	else
	{
		BaseClass::DoMuzzleFlash();
	}
}

#ifdef GLOWS_ENABLE
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseCombatCharacter::GetGlowEffectColor( float *r, float *g, float *b )
{
	*r = 0.76f;
	*g = 0.76f;
	*b = 0.76f;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
/*
void C_BaseCombatCharacter::EnableGlowEffect( float r, float g, float b )
{
	// destroy the existing effect
	if ( m_pGlowEffect )
	{
		DestroyGlowEffect();
	}

	m_pGlowEffect = new CGlowObject( this, Vector( r, g, b ), 1.0, true );
}
*/

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseCombatCharacter::UpdateGlowEffect( void )
{
	// destroy the existing effect
	if ( m_pGlowEffect )
	{
		DestroyGlowEffect();
	}

	// create a new effect
	if ( m_bGlowEnabled || m_bClientSideGlowEnabled )
	{
		float r, g, b;
		GetGlowEffectColor( &r, &g, &b );

		m_pGlowEffect = new CGlowObject( this, Vector( r, g, b ), 1.0, true );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseCombatCharacter::DestroyGlowEffect( void )
{
	if ( m_pGlowEffect )
	{
		delete m_pGlowEffect;
		m_pGlowEffect = NULL;
	}
}
#endif // GLOWS_ENABLE

#if defined( L4D2 )
void C_BaseCombatCharacter::RecvProxy_WeaponArray( const RecvProp *prop, void **out, void *data, int objectID )
{
	C_BaseEntity *entity = cl_entitylist->GetBaseEntity( objectID );
	if ( entity )
		static_cast< C_BaseCombatCharacter * >( entity )->m_bWeaponArrayChanged = true;
	DataTableRecvProxy_StaticDataTable( prop, out, data, objectID );
}
#endif
IMPLEMENT_CLIENTCLASS(C_BaseCombatCharacter, DT_BaseCombatCharacter, CBaseCombatCharacter);

// Only send active weapon index to local player
BEGIN_RECV_TABLE_NOBASE( C_BaseCombatCharacter, DT_BCCLocalPlayerExclusive )
	RecvPropTime( RECVINFO( m_flNextAttack ) ),
END_RECV_TABLE();


BEGIN_RECV_TABLE(C_BaseCombatCharacter, DT_BaseCombatCharacter)
	RecvPropDataTable( "bcc_localdata", 0, 0, &REFERENCE_RECV_TABLE(DT_BCCLocalPlayerExclusive) ),
	RecvPropEHandle( RECVINFO( m_hActiveWeapon ) ),
#if defined( L4D2 )
	RecvPropArray3( RECVINFO_ARRAY(m_hMyWeapons), RecvPropEHandle( RECVINFO( m_hMyWeapons[0] ) ), C_BaseCombatCharacter::RecvProxy_WeaponArray ),
#else
	RecvPropArray3( RECVINFO_ARRAY(m_hMyWeapons), RecvPropEHandle( RECVINFO( m_hMyWeapons[0] ) ) ),
#endif
#ifdef GLOWS_ENABLE
	RecvPropBool( RECVINFO( m_bGlowEnabled ) ),
#endif // GLOWS_ENABLE

#ifdef INVASION_CLIENT_DLL
	RecvPropInt( RECVINFO( m_iPowerups ) ),
#endif

END_RECV_TABLE()


BEGIN_PREDICTION_DATA( C_BaseCombatCharacter )

	DEFINE_PRED_ARRAY( m_iAmmo, FIELD_INTEGER,  MAX_AMMO_TYPES, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_flNextAttack, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_hActiveWeapon, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_ARRAY( m_hMyWeapons, FIELD_EHANDLE, MAX_WEAPONS, FTYPEDESC_INSENDTABLE ),

END_PREDICTION_DATA()

#if defined( L4D2 )
bool C_BaseCombatCharacter::Simulate()
{
	UpdateClientSideParticleEffects();
	BaseClass::Simulate();
	return true;
}
#endif

#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// Actual107245B4/B0/AC cached resource indices, native3FF80 registered global
// precacher. The explicit native PrecacheRegisterSystem runs it per map.
static int g_nWaterTrailDirectionalIndex;
static int g_nWaterTrailWalkIndex;
static int g_nWaterTrailIdleIndex;
namespace PrecacheWaterWakePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( PRECACHE_SYSTEM_CLIENT, "PrecacheWaterWake", 0x529550 ) {}
	virtual void Precache( IPrecacheHandler *pHandler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "water_trail_directional", precache, resourceList, &g_nWaterTrailDirectionalIndex );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "water_trail_directional_walk", precache, resourceList, &g_nWaterTrailWalkIndex );
		pHandler->Precache( RESOURCETYPE_PARTICLE_SYSTEM, "water_trail_idle", precache, resourceList, &g_nWaterTrailIdleIndex );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}

static inline void StopAndClearWaterTrail( HPARTICLEFFECT &effect )
{
	if ( effect )
	{
		effect->StopEmission( false, false, false );
		// CUtlReference assignment deliberately re-reads its current node after
		// StopEmission callbacks, matching the native unlink/clear behavior.
		effect = NULL;
	}
}

void C_BaseCombatCharacter::OnNewParticleEffect( const char *name, CNewParticleEffect *effect )
{
	if ( !Q_stricmp( name, "water_trail_directional" ) ) m_hWaterTrailDirectional = effect;
	else if ( !Q_stricmp( name, "water_trail_directional_walk" ) ) m_hWaterTrailWalk = effect;
	else if ( !Q_stricmp( name, "water_trail_idle" ) ) m_hWaterTrailIdle = effect;
	else BaseClass::OnNewParticleEffect( name, effect );
}

void C_BaseCombatCharacter::OnParticleEffectDeleted( CNewParticleEffect *effect )
{
	if ( m_hWaterTrailDirectional() == effect ) m_hWaterTrailDirectional = NULL;
	else if ( m_hWaterTrailWalk() == effect ) m_hWaterTrailWalk = NULL;
	else if ( m_hWaterTrailIdle() == effect ) m_hWaterTrailIdle = NULL;
	// Whole406A0 has NO base callback and deliberately stops at FIRST match.
}

void C_BaseCombatCharacter::SetWaterTrailMode( int mode, int onlyDrawForLocalUser )
{
	if ( mode == m_nWaterTrailMode ) return;
	m_nWaterTrailMode = mode;
	if ( mode != 3 ) StopAndClearWaterTrail( m_hWaterTrailDirectional );
	if ( mode != 2 ) StopAndClearWaterTrail( m_hWaterTrailWalk );
	if ( mode != 1 ) StopAndClearWaterTrail( m_hWaterTrailIdle );
	const Vector offset( 0.0f, 0.0f, m_flWaterTrailSurfaceZ );
	if ( mode == 1 )
	{
		if ( !m_hWaterTrailIdle )
		{
			const Vector &origin = GetAbsOrigin();
			const Vector position( origin.x, origin.y, m_flWaterTrailSurfaceZ );
			// The native idle effect is WORLD-OWNED (NULL entity), not parented
			// merely to force an OnNewParticleEffect/HPARTICLE assignment.
			DispatchParticleEffect( g_nWaterTrailIdleIndex, position, vec3_angle,
				PATTACH_CUSTOMORIGIN, NULL, onlyDrawForLocalUser );
		}
	}
	else if ( mode == 2 )
	{
		if ( !m_hWaterTrailWalk )
			DispatchParticleEffect( g_nWaterTrailWalkIndex, offset, vec3_angle,
				PATTACH_WATERWAKE, this, onlyDrawForLocalUser );
	}
	else if ( mode == 3 && !m_hWaterTrailDirectional )
	{
		DispatchParticleEffect( g_nWaterTrailDirectionalIndex, offset, vec3_angle,
			PATTACH_WATERWAKE, this, onlyDrawForLocalUser );
	}
}

void C_BaseCombatCharacter::UpdateClientSideParticleEffects()
{
	int onlyDrawForLocalUser = -1;
	C_BasePlayer *player = IsPlayer() ? ToBasePlayer( this ) : NULL;
	if ( player && player->IsLocalPlayer() )
	{
		// Main's actual sole user0 policy: native1-GetSplitScreenPlayerSlot
		// is user1 for the sole local player. Preserve creation/simulation and
		// callbacks; the genuine effect/leaf draw selector hides ONLY drawing.
		onlyDrawForLocalUser = 1;
	}
	if ( m_lifeState != LIFE_ALIVE || IsGhost() || GetWaterLevel() == 0 )
	{
		if ( m_nWaterTrailMode != 0 )
		{
			m_nWaterTrailMode = 0;
			StopAndClearWaterTrail( m_hWaterTrailDirectional );
			StopAndClearWaterTrail( m_hWaterTrailWalk );
			StopAndClearWaterTrail( m_hWaterTrailIdle );
		}
		return;
	}
	// Native40953 COMISS next/current+JA: UNORDERED still enters this phase.
	if ( m_flWaterTrailNextProbeTime > gpGlobals->curtime ) return;
	Vector velocity;
	EstimateAbsVelocity( velocity );
	const float speedSquared = ( velocity.y * velocity.y + velocity.x * velocity.x ) + velocity.z * velocity.z;
	int mode = 3;
	// Native4099A JBE: UNORDERED takes the slow/open-cube selection as well.
	if ( !( speedSquared > 12100.0f ) )
	{
		const bool still = velocity.x > -1.0f && 1.0f > velocity.x &&
			velocity.y > -1.0f && 1.0f > velocity.y && velocity.z > -1.0f && 1.0f > velocity.z;
		mode = still && IsPlayer() ? 1 : 2;
	}
	const Vector &origin = GetAbsOrigin();
	if ( m_nWaterTrailMode == 0 )
	{
		const Vector end( origin.x, origin.y, origin.z + 128.0f );
		CTraceFilterWorldOnly filter;
		trace_t trace;
		UTIL_TraceLine( origin, end, MASK_WATER, &filter, &trace );
		// Native var_20 is trace+56/fractionleftsolid, NOT fraction. The
		// surface is trace+8/STARTPOS.z, NOT endpos.z. Preserve these fields.
		if ( trace.fractionleftsolid == 0.0f )
		{
			SetWaterTrailMode( 0, onlyDrawForLocalUser );
			m_flWaterTrailNextProbeTime = gpGlobals->curtime + 0.1f;
			return;
		}
		m_flWaterTrailSurfaceZ = trace.startpos.z;
	}
	const float top = WorldAlignMaxs().z + origin.z;
	// Native40AAC COMISS surface/top+JA: NaN does NOT select submerged4.
	SetWaterTrailMode( m_flWaterTrailSurfaceZ > top ? 4 : mode, onlyDrawForLocalUser );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif

#if defined( L4D2 )
CStudioHdr *C_BaseCombatCharacter::OnNewModel()
{
	CStudioHdr *model = BaseClass::OnNewModel();
	if ( model )
	{
		m_leftFootAttachment = LookupAttachment( "lfoot" );
		m_rightFootAttachment = LookupAttachment( "rfoot" );
	}
	return model;
}
#endif

#if defined( L4D2 )
C_BaseCombatWeapon *C_BaseCombatCharacter::Weapon_GetSlot( int slot ) const
{
	for ( int i = 0; i < MAX_WEAPONS; ++i )
	{
		C_BaseCombatWeapon *weapon = m_hMyWeapons[i].Get();
		if ( weapon && weapon->GetSlot() == slot )
			return m_hMyWeapons[i].Get();
	}
	return NULL;
}
#endif
