//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Flame entity to be attached to target entity. Serves two purposes:
//
//			1) An entity that can be placed by a level designer and triggered
//			   to ignite a target entity.
//
//			2) An entity that can be created at runtime to ignite a target entity.
//
//=============================================================================//

#include "cbase.h"
#include "EntityFlame.h"
#include "ai_basenpc.h"
#include "fire.h"
#include "shareddefs.h"
#if defined( L4D2 )
#include "l4d2/player/terrorplayer.h"
#include "soundent.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

#if defined( L4D2 )
#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
static string_t s_iszFlameBurningFlesh;
static string_t s_iszFlameBurningObject;
static HSOUNDSCRIPTHANDLE s_hFlameBurningFlesh = SOUNDEMITTER_INVALID_HANDLE;
static HSOUNDSCRIPTHANDLE s_hFlameBurningObject = SOUNDEMITTER_INVALID_HANDLE;
#endif

#if defined( L4D2 )
BEGIN_DATADESC( CEntityFlame )
	DEFINE_FIELD( m_flLifetime, FIELD_TIME ),
	DEFINE_FIELD( m_flSize, FIELD_FLOAT ),
	DEFINE_FIELD( m_hEntAttached, FIELD_EHANDLE ),
	DEFINE_FIELD( m_iDangerSound, FIELD_INTEGER ),
	DEFINE_FIELD( m_bCheapEffect, FIELD_BOOLEAN ),
	DEFINE_FUNCTION( FlameThink ),
END_DATADESC()
#else
BEGIN_DATADESC( CEntityFlame )

	DEFINE_KEYFIELD( m_flLifetime, FIELD_FLOAT, "lifetime" ),

	DEFINE_FIELD( m_flSize, FIELD_FLOAT ),
	DEFINE_FIELD( m_hEntAttached, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bUseHitboxes, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_iNumHitboxFires, FIELD_INTEGER ),
	DEFINE_FIELD( m_flHitboxFireScale, FIELD_FLOAT ),
	// DEFINE_FIELD( m_bPlayingSound, FIELD_BOOLEAN ),
	
	DEFINE_FUNCTION( FlameThink ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Ignite", InputIgnite ),

END_DATADESC()
#endif


IMPLEMENT_SERVERCLASS_ST( CEntityFlame, DT_EntityFlame )
	SendPropEHandle( SENDINFO( m_hEntAttached ) ),
#if defined( L4D2 )
	SendPropBool( SENDINFO( m_bCheapEffect ) ),
#endif
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( entityflame, CEntityFlame );
#if !defined( L4D2 )
LINK_ENTITY_TO_CLASS( env_entity_igniter, CEntityFlame );
#endif
#ifndef L4D2
PRECACHE_REGISTER(entityflame);
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CEntityFlame::CEntityFlame( void )
{
#if defined( L4D2 )
	m_hEntAttached = NULL;
	m_hAttacker = NULL;
	m_flSize = 0.0f;
	m_flLifetime = gpGlobals->curtime;
	m_iszLoopingSound = NULL_STRING;
	m_iDangerSound = SOUNDLIST_EMPTY;
	m_bCheapEffect = false;
	m_nDamageCustom = 0;
	m_flDirectDamagePerSecond = 5.0f;
#else
	m_flSize			= 0.0f;
	m_iNumHitboxFires	= 10;
	m_flHitboxFireScale	= 1.0f;
	m_flLifetime		= 0.0f;
	m_bPlayingSound		= false;
#endif
}

#if defined( L4D2 )
void CEntityFlame::UpdateOnRemove()
{
	if ( m_iszLoopingSound != NULL_STRING )
	{
		StopSound( STRING( m_iszLoopingSound ) );
		m_iszLoopingSound = NULL_STRING;
	}
	if ( m_iDangerSound != SOUNDLIST_EMPTY )
	{
		CSoundEnt::FreeSound( m_iDangerSound );
		m_iDangerSound = SOUNDLIST_EMPTY;
	}
	BaseClass::UpdateOnRemove();
}
#else
void CEntityFlame::UpdateOnRemove()
{
	// Sometimes the entity I'm burning gets destroyed by other means,
	// which kills me. Make sure to stop the burning sound.
	if ( m_bPlayingSound )
	{
		EmitSound( "General.StopBurning" );
		m_bPlayingSound = false;
	}

	BaseClass::UpdateOnRemove();
}
#endif

#if defined( L4D2 )
void CEntityFlame::Precache()
{
	s_iszFlameBurningFlesh = AllocPooledString( "General.BurningFlesh" );
	s_iszFlameBurningObject = AllocPooledString( "General.BurningObject" );
	PrecacheScriptSound( "General.StopBurning" );
	s_hFlameBurningFlesh = PrecacheScriptSound( STRING( s_iszFlameBurningFlesh ) );
	s_hFlameBurningObject = PrecacheScriptSound( STRING( s_iszFlameBurningObject ) );
	PrecacheParticleSystem( "burning_character" );
	PrecacheParticleSystem( "burning_gib_01" );
}
#else
void CEntityFlame::Precache()
{
	BaseClass::Precache();

	PrecacheScriptSound( "General.StopBurning" );
	PrecacheScriptSound( "General.BurningFlesh" );
	PrecacheScriptSound( "General.BurningObject" );
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : inputdata - 
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void CEntityFlame::InputIgnite( inputdata_t &inputdata )
{
	if (m_target != NULL_STRING)
	{
		CBaseEntity *pTarget = NULL;
		while ((pTarget = gEntList.FindEntityGeneric(pTarget, STRING(m_target), this, inputdata.pActivator)) != NULL)
		{
			// Combat characters know how to catch themselves on fire.
			CBaseCombatCharacter *pBCC = pTarget->MyCombatCharacterPointer();
			if (pBCC)
			{
				// DVS TODO: consider promoting Ignite to CBaseEntity and doing everything here
				pBCC->Ignite(m_flLifetime);
			}
			// Everything else, we handle here.
			else
			{
				CEntityFlame *pFlame = CEntityFlame::Create(pTarget);
				if (pFlame)
				{
					pFlame->SetLifetime(m_flLifetime);
				}
			}
		}
	}
}
#endif


//-----------------------------------------------------------------------------
// Purpose: Creates a flame and attaches it to a target entity.
// Input  : pTarget - 
//-----------------------------------------------------------------------------
CEntityFlame *CEntityFlame::Create( CBaseEntity *pTarget, bool useHitboxes )
{
	CEntityFlame *pFlame = (CEntityFlame *) CreateEntityByName( "entityflame" );

	if ( pFlame == NULL )
		return NULL;

	float xSize = pTarget->CollisionProp()->OBBMaxs().x - pTarget->CollisionProp()->OBBMins().x;
	float ySize = pTarget->CollisionProp()->OBBMaxs().y - pTarget->CollisionProp()->OBBMins().y;

	float size = ( xSize + ySize ) * 0.5f;
	
	if ( size < 16.0f )
	{
		size = 16.0f;
	}

	UTIL_SetOrigin( pFlame, pTarget->GetAbsOrigin() );

	pFlame->m_flSize = size;
	pFlame->SetThink( &CEntityFlame::FlameThink );
	pFlame->SetNextThink( gpGlobals->curtime + 0.1f );

	pFlame->AttachToEntity( pTarget );
	pFlame->SetLifetime( 2.0f );

	//Send to the client even though we don't have a model
	pFlame->AddEFlags( EFL_FORCE_CHECK_TRANSMIT );

	pFlame->SetUseHitboxes( useHitboxes );

	return pFlame;
}


//-----------------------------------------------------------------------------
// Purpose: Attaches the flame to an entity and moves with it
// Input  : pTarget - target entity to attach to
//-----------------------------------------------------------------------------
#if defined( L4D2 )
void CEntityFlame::AttachToEntity( CBaseEntity *target )
{
	m_hEntAttached = target;
	const int team = target->GetTeamNumber();
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
	{
		m_iszLoopingSound = NULL_STRING;
	}
	else if ( target->IsNPC() || target->IsPlayer() || target->MyCombatCharacterPointer() )
	{
		m_iszLoopingSound = s_iszFlameBurningFlesh;
		EmitSound( STRING( m_iszLoopingSound ), s_hFlameBurningFlesh, 0.0f, NULL );
	}
	else
	{
		m_iszLoopingSound = s_iszFlameBurningObject;
		EmitSound( STRING( m_iszLoopingSound ), s_hFlameBurningObject, 0.0f, NULL );
	}
	SetParent( target, -1 );
}
#else
void CEntityFlame::AttachToEntity( CBaseEntity *pTarget )
{
	// For networking to the client.
	m_hEntAttached = pTarget;

	if( pTarget->IsNPC() )
	{
		EmitSound( "General.BurningFlesh" );
	}
	else
	{
		EmitSound( "General.BurningObject" );
	}

	m_bPlayingSound = true;

	// So our heat emitter follows the entity around on the server.
	SetParent( pTarget );
}
#endif


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : lifetime - 
//-----------------------------------------------------------------------------
void CEntityFlame::SetLifetime( float lifetime )
{
	m_flLifetime = gpGlobals->curtime + lifetime;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : use - 
//-----------------------------------------------------------------------------
void CEntityFlame::SetUseHitboxes( bool use )
{
	m_bUseHitboxes = use;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : iNumHitBoxFires - 
//-----------------------------------------------------------------------------
void CEntityFlame::SetNumHitboxFires( int iNumHitboxFires )
{
	m_iNumHitboxFires = iNumHitboxFires;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flHitboxFireScale - 
//-----------------------------------------------------------------------------
void CEntityFlame::SetHitboxFireScale( float flHitboxFireScale )
{
	m_flHitboxFireScale = flHitboxFireScale;
}

#if defined( L4D2 )
// S0B31E0: no narrowing spill before the native caller compares the remainder.
double CEntityFlame::GetRemainingLife( void )
{
	return static_cast< double >( m_flLifetime.Get() ) - static_cast< double >( gpGlobals->curtime );
}
#else
float CEntityFlame::GetRemainingLife( void )
{
	return m_flLifetime - gpGlobals->curtime;
}
#endif

int CEntityFlame::GetNumHitboxFires( void )
{
	return m_iNumHitboxFires;
}

float CEntityFlame::GetHitboxFireScale( void )
{
	return m_flHitboxFireScale;
}

//-----------------------------------------------------------------------------
// Purpose: Burn targets around us
//-----------------------------------------------------------------------------
#if defined( L4D2 )
void CEntityFlame::FlameThink()
{
	SetNextThink( gpGlobals->curtime + 0.2f );
	if ( !m_hEntAttached.Get() )
	{
		UTIL_Remove( this );
		return;
	}
	if ( m_hEntAttached.Get()->GetFlags() & FL_DISSOLVING )
	{
		SetRenderColorA( 0 );
		return;
	}
	CAI_BaseNPC *npc = m_hEntAttached.Get()->MyNPCPointer();
	if ( npc && !npc->IsAlive() )
	{
		UTIL_Remove( this );
		npc->Extinguish();
		return;
	}
	if ( m_hEntAttached.Get()->GetWaterLevel() != 0 )
	{
		Vector minimum = m_hEntAttached.Get()->WorldSpaceCenter();
		Vector maximum = minimum;
		maximum.z = m_hEntAttached.Get()->WorldSpaceCenter().z;
		maximum.x += 32.0f;
		maximum.y += 32.0f;
		minimum.z -= 32.0f;
		minimum.x -= 32.0f;
		minimum.y -= 32.0f;
		UTIL_Bubbles( minimum, maximum, 12 );
	}
	// COMISS(now,deadline)/JA expires only strict greater. The second native
	// helper0B3200 compares the attached EHANDLE to NULL; it is NOT water.
	if ( !( gpGlobals->curtime > m_flLifetime.Get() ) && m_hEntAttached.Get() != NULL )
	{
		CBaseEntity *attacker = m_hAttacker.Get();
		if ( !attacker )
			attacker = this;
		if ( m_hEntAttached.Get() )
		{
			CBaseEntity *ignore = NULL;
			CTerrorPlayer *player = ToTerrorPlayer( m_hEntAttached.Get() );
			if ( player )
			{
				if ( player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 3 )
					ignore = player->m_pounceVictim.Get();
				else if ( player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 5 )
					ignore = player->m_jockeyVictim.Get();
				else if ( player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 6 )
					ignore = player->m_pummelVictim.Get();
			}
			const float radius = m_flSize.Get() * 0.5f;
			const Vector &origin = GetAbsOrigin();
			CTakeDamageInfo radiusDamage( this, attacker, 4.0f, DMG_BURN, 0 );
			// Native excludes the PIN VICTIM, not the entity carrying the flame.
			RadiusDamage( radiusDamage, origin, radius, CLASS_NONE, ignore );
			const float directDamage = m_flDirectDamagePerSecond * 0.2f;
			CTakeDamageInfo direct( this, attacker, directDamage, DMG_BURN | DMG_DIRECT, m_nDamageCustom );
			m_hEntAttached.Get()->TakeDamage( direct );
			if ( !m_hEntAttached.Get()->IsNPC() && hl2_episodic.GetBool() )
			{
				CSoundEnt::InsertSound( SOUND_MOVE_AWAY, GetAbsOrigin(), 24, 0.1f, this, 2 );
				Vector above = GetAbsOrigin();
				above.z += 48.0f;
				CSoundEnt::InsertSound( SOUND_MOVE_AWAY, above, 24, 0.1f, this, 1 );
			}
		}
		else
		{
			const float radius = m_flSize.Get() * 0.5f;
			const Vector &origin = GetAbsOrigin();
			CTakeDamageInfo radiusDamage( this, attacker, 0.80000001192092896f, DMG_BURN, m_nDamageCustom );
			RadiusDamage( radiusDamage, origin, radius, CLASS_NONE, NULL );
		}
		const float heatRadius = m_flSize.Get() * 0.5f;
		FireSystem_AddHeatInRadius( GetAbsOrigin(), heatRadius, 2.0f );
		return;
	}
	if ( m_iszLoopingSound != NULL_STRING )
	{
		StopSound( STRING( m_iszLoopingSound ) );
		m_iszLoopingSound = NULL_STRING;
	}
	SetThink( &CBaseEntity::SUB_Remove );
	SetNextThink( gpGlobals->curtime + 0.5f );
	if ( m_hEntAttached.Get() )
	{
		CBaseCombatCharacter *combat = m_hEntAttached.Get()->MyCombatCharacterPointer();
		if ( combat )
			combat->Extinguish();
	}
}
#else
void CEntityFlame::FlameThink( void )
{
	// Assure that this function will be ticked again even if we early-out in the if below.
	SetNextThink( gpGlobals->curtime + FLAME_DAMAGE_INTERVAL );

	if ( m_hEntAttached )
	{
		if ( m_hEntAttached->GetFlags() & FL_TRANSRAGDOLL )
		{
			SetRenderColorA( 0 );
			return;
		}
	
		CAI_BaseNPC *pNPC = m_hEntAttached->MyNPCPointer();
		if ( pNPC && !pNPC->IsAlive() )
		{
			UTIL_Remove( this );
			// Notify the NPC that it's no longer burning!
			pNPC->Extinguish();
			return;
		}

		if( m_hEntAttached->GetWaterLevel() > 0 )
		{
			Vector mins, maxs;

			mins = m_hEntAttached->WorldSpaceCenter();
			maxs = mins;

			maxs.z = m_hEntAttached->WorldSpaceCenter().z;
			maxs.x += 32;
			maxs.y += 32;
			
			mins.z -= 32;
			mins.x -= 32;
			mins.y -= 32;

			UTIL_Bubbles( mins, maxs, 12 );
		}
	}
	else
	{
		UTIL_Remove( this );
		return;
	}

	// See if we're done burning, or our attached ent has vanished
	if ( m_flLifetime < gpGlobals->curtime || m_hEntAttached == NULL )
	{
		EmitSound( "General.StopBurning" );
		m_bPlayingSound = false;
		SetThink( &CEntityFlame::SUB_Remove );
		SetNextThink( gpGlobals->curtime + 0.5f );

		// Notify anything we're attached to
		if ( m_hEntAttached )
		{
			CBaseCombatCharacter *pAttachedCC = m_hEntAttached->MyCombatCharacterPointer();

			if( pAttachedCC )
			{
				// Notify the NPC that it's no longer burning!
				pAttachedCC->Extinguish();
			}
		}

		return;
	}

	if ( m_hEntAttached )
	{
		// Do radius damage ignoring the entity I'm attached to. This will harm things around me.
		RadiusDamage( CTakeDamageInfo( this, this, 4.0f, DMG_BURN ), GetAbsOrigin(), m_flSize/2, CLASS_NONE, m_hEntAttached );

		// Directly harm the entity I'm attached to. This is so we can precisely control how much damage the entity
		// that is on fire takes without worrying about the flame's position relative to the bodytarget (which is the
		// distance that the radius damage code uses to determine how much damage to inflict)
		m_hEntAttached->TakeDamage( CTakeDamageInfo( this, this, FLAME_DIRECT_DAMAGE, DMG_BURN | DMG_DIRECT ) );

		if( !m_hEntAttached->IsNPC() && hl2_episodic.GetBool() )
		{
			const float ENTITYFLAME_MOVE_AWAY_DIST = 24.0f;
			// Make a sound near my origin, and up a little higher (in case I'm on the ground, so NPC's still hear it)
			CSoundEnt::InsertSound( SOUND_MOVE_AWAY, GetAbsOrigin(), ENTITYFLAME_MOVE_AWAY_DIST, 0.1f, this, SOUNDENT_CHANNEL_REPEATED_DANGER );
			CSoundEnt::InsertSound( SOUND_MOVE_AWAY, GetAbsOrigin() + Vector( 0, 0, 48.0f ), ENTITYFLAME_MOVE_AWAY_DIST, 0.1f, this, SOUNDENT_CHANNEL_REPEATING );
		}
	}
	else
	{
		RadiusDamage( CTakeDamageInfo( this, this, FLAME_RADIUS_DAMAGE, DMG_BURN ), GetAbsOrigin(), m_flSize/2, CLASS_NONE, NULL );
	}

	FireSystem_AddHeatInRadius( GetAbsOrigin(), m_flSize/2, 2.0f );

}
#endif


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : pEnt -	
//-----------------------------------------------------------------------------
void CreateEntityFlame(CBaseEntity *pEnt)
{
	CEntityFlame::Create( pEnt );
}

#if defined( L4D2 )
// S0B3E20. Factory S0B3EC0 calls this virtual directly, not DispatchSpawn.
void CEntityFlame::Spawn()
{
	BaseClass::Spawn();
	m_flLifetime = gpGlobals->curtime;
	SetThink( &CEntityFlame::FlameThink );
	SetNextThink( gpGlobals->curtime + 0.1f );
	AddEFlags( EFL_FORCE_CHECK_TRANSMIT );
}

// S0B3EC0. Original calls Spawn after size, and Activate after lifetime.
CEntityFlame *CEntityFlame::Create( CBaseEntity *pTarget, float flLifetime, float flSize, bool useHitboxes )
{
	CEntityFlame *flame = static_cast< CEntityFlame * >( CreateEntityByName( "entityflame" ) );
	if ( !flame )
		return NULL;
	// COMISS(0,size)/JB skips positive AND unordered values.
	if ( flSize <= 0.0f )
	{
		const float maximumX = pTarget->CollisionProp()->OBBMaxs().x;
		const float xSize = maximumX - pTarget->CollisionProp()->OBBMins().x;
		const float maximumY = pTarget->CollisionProp()->OBBMaxs().y;
		const float ySize = maximumY - pTarget->CollisionProp()->OBBMins().y;
		flSize = ( ySize + xSize ) * 0.5f;
		if ( 16.0f > flSize )
			flSize = 16.0f;
	}
	if ( flLifetime <= 0.0f )
		flLifetime = 2.0f;
	flame->m_flSize = flSize;
	flame->Spawn();
	UTIL_SetOrigin( flame, pTarget->GetAbsOrigin() );
	flame->SetThink( &CEntityFlame::FlameThink );
	flame->SetNextThink( gpGlobals->curtime + 0.1f );
	flame->AttachToEntity( pTarget );
	// AddEFlags performs the native bit-or followed by DispatchUpdateTransmitState.
	flame->AddEFlags( EFL_FORCE_CHECK_TRANSMIT );
	flame->SetUseHitboxes( useHitboxes );
	flame->SetLifetime( flLifetime );
	flame->Activate();
	return flame;
}
#endif

#if defined( L4D2 )
class CEnvEntityIgniter : public CBaseEntity
{
public:
	DECLARE_CLASS( CEnvEntityIgniter, CBaseEntity );
	DECLARE_DATADESC();
	void Precache() OVERRIDE { UTIL_PrecacheOther( "entityflame" ); }
	void InputIgnite( inputdata_t &inputdata );

private:
	// Native factory B32E0 only calls CBaseEntity's constructor. The entity
	// allocation is zero-filled; no derived ctor/default-lifetime write.
	float m_flLifetime;
};

LINK_ENTITY_TO_CLASS( env_entity_igniter, CEnvEntityIgniter );

// Native map75E5CC has EXACTLY these two descriptors, inherited CBaseEntity.
BEGIN_DATADESC( CEnvEntityIgniter )
	DEFINE_KEYFIELD( m_flLifetime, FIELD_FLOAT, "lifetime" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Ignite", InputIgnite ),
END_DATADESC()

// S0B4110. Lifetime is a relative duration, not CEntityFlame's deadline.
void CEnvEntityIgniter::InputIgnite( inputdata_t &inputdata )
{
	if ( m_target != NULL_STRING )
	{
		CBaseEntity *target = NULL;
		while ( ( target = gEntList.FindEntityGeneric( target, STRING( m_target ), this, inputdata.pActivator ) ) != NULL )
		{
			CBaseCombatCharacter *combat = target->MyCombatCharacterPointer();
			if ( combat )
				combat->Ignite( m_flLifetime, true, 0.0f, false );
			else
				CEntityFlame::Create( target, m_flLifetime, 0.0f, true );
		}
	}
}
#endif

#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif

// Native server 0xb2f20; 0x5c1980 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace entityflamePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "entityflame", 0x5c1980 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "entityflame", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
