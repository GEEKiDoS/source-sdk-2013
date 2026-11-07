//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//


#include "cbase.h"
#include "c_physicsprop.h"
#include "c_physbox.h"
#include "c_props.h"
#if defined( L4D2 )
#include "l4d2/player/c_terrorplayer.h"
#include "util_shared.h"
#include "in_buttons.h"
#endif

#define CPhysBox C_PhysBox
#define CPhysicsProp C_PhysicsProp


// memdbgon must be the last include file in a .cpp file!!!
#if defined( L4D2 )
#include "physics.h"
#include "KeyValues.h"
#include "c_recipientfilter.h"
#include "itempents.h"
#include "l4d2/terror_shareddefs.h"
#include "datacache/imdlcache.h"
#endif // L4D2
#if defined( L4D2 )
#include <emmintrin.h>
#endif
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( DynamicProp, DT_DynamicProp )

BEGIN_NETWORK_TABLE( CDynamicProp, DT_DynamicProp )
	RecvPropBool(RECVINFO(m_bUseHitboxesForRenderBox)),
END_NETWORK_TABLE()

C_DynamicProp::C_DynamicProp( void )
{
	m_iCachedFrameCount = -1;
}

C_DynamicProp::~C_DynamicProp( void )
{
}

bool C_DynamicProp::TestBoneFollowers( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	// UNDONE: There is no list of the bone followers that is networked to the client
	// so instead we do a search for solid stuff here.  This is not really great - a list would be
	// preferable.
	CBaseEntity	*pList[128];
	Vector mins, maxs;
	CollisionProp()->WorldSpaceAABB( &mins, &maxs );
	int count = UTIL_EntitiesInBox( pList, ARRAYSIZE(pList), mins, maxs, 0, PARTITION_CLIENT_SOLID_EDICTS );
	for ( int i = 0; i < count; i++ )
	{
		if ( pList[i]->GetOwnerEntity() == this )
		{
			if ( pList[i]->TestCollision(ray, fContentsMask, tr) )
			{
				return true;
			}
		}
	}
	return false;
}

bool C_DynamicProp::TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	if ( IsSolidFlagSet(FSOLID_NOT_SOLID) )
	{
		// if this entity is marked non-solid and custom test it must have bone followers
		if ( IsSolidFlagSet( FSOLID_CUSTOMBOXTEST ) && IsSolidFlagSet( FSOLID_CUSTOMRAYTEST ))
		{
			return TestBoneFollowers( ray, fContentsMask, tr );
		}
	}
	return BaseClass::TestCollision( ray, fContentsMask, tr );
}

//-----------------------------------------------------------------------------
// implements these so ragdolls can handle frustum culling & leaf visibility
//-----------------------------------------------------------------------------
void C_DynamicProp::GetRenderBounds( Vector& theMins, Vector& theMaxs )
{
	if ( m_bUseHitboxesForRenderBox )
	{
		if ( GetModel() )
		{
			studiohdr_t *pStudioHdr = modelinfo->GetStudiomodel( GetModel() );
			if ( !pStudioHdr || GetSequence() == -1 )
			{
				theMins = vec3_origin;
				theMaxs = vec3_origin;
				return;
			}

			// Only recompute if it's a new frame
			if ( gpGlobals->framecount != m_iCachedFrameCount )
			{
				ComputeEntitySpaceHitboxSurroundingBox( &m_vecCachedRenderMins, &m_vecCachedRenderMaxs );
				m_iCachedFrameCount = gpGlobals->framecount;
			}

			theMins = m_vecCachedRenderMins;
			theMaxs = m_vecCachedRenderMaxs;
			return;
		}
	}

	BaseClass::GetRenderBounds( theMins, theMaxs );
}

unsigned int C_DynamicProp::ComputeClientSideAnimationFlags()
{
	if ( GetSequence() != -1 )
	{
		CStudioHdr *pStudioHdr = GetModelPtr();
		if ( GetSequenceCycleRate(pStudioHdr, GetSequence()) != 0.0f )
		{
			return BaseClass::ComputeClientSideAnimationFlags();
		}
	}

	// no sequence or no cycle rate, don't do any per-frame calcs
	return 0;
}

// ------------------------------------------------------------------------------------------ //
// ------------------------------------------------------------------------------------------ //
class C_BasePropDoor : public C_DynamicProp
{
	DECLARE_CLASS( C_BasePropDoor, C_DynamicProp );
public:
	DECLARE_CLIENTCLASS();

	// constructor, destructor
	C_BasePropDoor( void );
	virtual ~C_BasePropDoor( void );

	virtual void OnDataChanged( DataUpdateType_t type );
#if defined( L4D2 )
	int ObjectCaps() OVERRIDE;
	void PostDataUpdate( DataUpdateType_t type ) OVERRIDE;
	bool InSameTeam( const C_BaseEntity *entity ) const OVERRIDE;
	unsigned int PhysicsSolidMaskForEntity() const OVERRIDE;
	virtual bool IsGlowUseTarget() OVERRIDE { return false; } // Native8E940, genuine356.
	virtual int GetDoorUseActionFlags( C_BasePlayer *player ); // Native8E660, genuine219.
#endif
#if defined( L4D2 )
	void SendPlayerDoorCommand();
#endif

	virtual bool TestCollision( const Ray_t &ray, unsigned int mask, trace_t& trace );

private:
	C_BasePropDoor( const C_BasePropDoor & );
#if defined( L4D2 )
protected:
	int m_eDoorState;
	int m_nDoorSpawnFlags;
	bool m_bLocked;
	bool m_bIsRescueDoor;
	CHandle< C_BasePropDoor > m_hMaster;
	CountdownTimer m_doorUseTimer; // Native2448, actual default0/-1; no wire/save/prediction.
	bool m_bRecreateVPhysics; // Native2440; only explicit base ctor local bool store.
#endif
};

IMPLEMENT_CLIENTCLASS_DT(C_BasePropDoor, DT_BasePropDoor, CBasePropDoor)
#if defined( L4D2 )
	RecvPropInt( RECVINFO_NAME( m_nDoorSpawnFlags, m_spawnflags ) ),
	RecvPropInt( RECVINFO( m_eDoorState ) ),
	RecvPropBool( RECVINFO( m_bLocked ) ),
	RecvPropBool( RECVINFO_NAME( m_bIsRescueDoor, m_isRescueDoor ) ),
	RecvPropEHandle( RECVINFO( m_hMaster ) ),
#endif
END_RECV_TABLE()

C_BasePropDoor::C_BasePropDoor( void )
{
#if defined( L4D2 )
	m_bRecreateVPhysics = false;
#endif
}

C_BasePropDoor::~C_BasePropDoor( void )
{
}

#if defined( L4D2 )
int C_BasePropDoor::ObjectCaps()
{
	// C8D660 bypasses the SDK animating save capability.
	enum { DOOR_SPAWNFLAG_IGNORE_USE = 0x8000 };
	const int caps = C_BaseEntity::ObjectCaps();
	return caps | ( ( m_nDoorSpawnFlags & DOOR_SPAWNFLAG_IGNORE_USE ) ? 0 : ( FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS ) );
}

void C_BasePropDoor::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	bool created = type == DATA_UPDATE_CREATED;
	if ( VPhysicsGetObject() && m_bRecreateVPhysics )
	{
		VPhysicsDestroyObject();
		created = true;
		m_bRecreateVPhysics = false;
	}
	PhysOnDataChanged( this, created );
}
#else
void C_BasePropDoor::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );

	if ( type == DATA_UPDATE_CREATED )
	{
		SetSolid(SOLID_VPHYSICS);
		VPhysicsInitShadow( false, false );
	}
	else if ( VPhysicsGetObject() )
	{
		VPhysicsGetObject()->UpdateShadow( GetAbsOrigin(), GetAbsAngles(), false, TICK_INTERVAL );
	}
}
#endif

bool C_BasePropDoor::TestCollision( const Ray_t &ray, unsigned int mask, trace_t& trace )
{
	if ( !VPhysicsGetObject() )
		return false;

	CStudioHdr *pStudioHdr = GetModelPtr( );
	if (!pStudioHdr)
		return false;

	physcollision->TraceBox( ray, VPhysicsGetObject()->GetCollide(), GetAbsOrigin(), GetAbsAngles(), &trace );

	if ( trace.DidHit() )
	{
		trace.surface.surfaceProps = VPhysicsGetObject()->GetMaterialIndex();
		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------ //
// Special version of func_physbox.
// ------------------------------------------------------------------------------------------ //
#ifndef _XBOX
class CPhysBoxMultiplayer : public CPhysBox, public IMultiplayerPhysics
{
public:
	DECLARE_CLASS( CPhysBoxMultiplayer, CPhysBox );

	virtual int GetMultiplayerPhysicsMode()
	{
		return m_iPhysicsMode;
	}

	virtual float GetMass()
	{
		return m_fMass;
	}

	virtual bool IsAsleep()
	{
		Assert ( 0 );
		return true;
	}

	CNetworkVar( int, m_iPhysicsMode );	// One of the PHYSICS_MULTIPLAYER_ defines.	
	CNetworkVar( float, m_fMass );

	DECLARE_CLIENTCLASS();
};

IMPLEMENT_CLIENTCLASS_DT( CPhysBoxMultiplayer, DT_PhysBoxMultiplayer, CPhysBoxMultiplayer )
	RecvPropInt( RECVINFO( m_iPhysicsMode ) ),
	RecvPropFloat( RECVINFO( m_fMass ) ),
END_RECV_TABLE()


#if !defined( L4D2 )
class CPhysicsPropMultiplayer : public CPhysicsProp, public IMultiplayerPhysics
{
	DECLARE_CLASS( CPhysicsPropMultiplayer, CPhysicsProp );

	virtual int GetMultiplayerPhysicsMode()
	{
		Assert( m_iPhysicsMode != PHYSICS_MULTIPLAYER_CLIENTSIDE );
		Assert( m_iPhysicsMode != PHYSICS_MULTIPLAYER_AUTODETECT );
		return m_iPhysicsMode;
	}

	virtual float GetMass()
	{
		return m_fMass;
	}

	virtual bool IsAsleep()
	{
		return !m_bAwake;
	}

	virtual void ComputeWorldSpaceSurroundingBox( Vector *mins, Vector *maxs )
	{
		Assert( mins != NULL && maxs != NULL );
		if ( !mins || !maxs )
			return;

		// Take our saved collision bounds, and transform into world space
		TransformAABB( EntityToWorldTransform(), m_collisionMins, m_collisionMaxs, *mins, *maxs );
	}

	CNetworkVar( int, m_iPhysicsMode );	// One of the PHYSICS_MULTIPLAYER_ defines.	
	CNetworkVar( float, m_fMass );
	CNetworkVector( m_collisionMins );
	CNetworkVector( m_collisionMaxs );

	DECLARE_CLIENTCLASS();
};

IMPLEMENT_CLIENTCLASS_DT( CPhysicsPropMultiplayer, DT_PhysicsPropMultiplayer, CPhysicsPropMultiplayer )
	RecvPropInt( RECVINFO( m_iPhysicsMode ) ),
	RecvPropFloat( RECVINFO( m_fMass ) ),
	RecvPropVector( RECVINFO( m_collisionMins ) ),
	RecvPropVector( RECVINFO( m_collisionMaxs ) ),
END_RECV_TABLE()
#endif
#endif

#if defined( L4D2 )
extern ConVar player_use_radius;
extern ConVar survivor_teammatefocus_enabled;
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

static bool IsTeammateFocusThreatened( int character )
{
	C_TerrorPlayer *player = GetTerrorPlayerBySurvivorCharacter( character );
	return player && ( gpGlobals->curtime < player->GetITTimerTimestamp() ||
		player->IsTongueVictim() || player->IsBeingPounced() || player->IsJockeyVictim() ||
		player->IsIncapacitated() || player->m_isHangingFromLedge ||
		player->m_isHangingFromTongue || player->IsPummelVictim() );
}

int C_TerrorPlayer::GetNextTeammateFocusCharacter( bool requireThreat )
{
	int character = 0;
	if ( m_bTeammateFocusActive )
		character = m_nTeammateFocusCharacter >= 3 ? 0 : m_nTeammateFocusCharacter + 1;
	const int first = character;
	if ( character == m_nTeammateFocusCharacter )
		return 8;
	do
	{
		C_TerrorPlayer *player = GetTerrorPlayerBySurvivorCharacter( character );
		if ( player && player != C_BasePlayer::GetLocalPlayer() && !player->IsDormant() &&
			( !requireThreat || IsTeammateFocusThreatened( character ) ) )
			return character;
		character = character >= 3 ? 0 : character + 1;
	}
	while ( character != first && character != m_nTeammateFocusCharacter );
	return 8;
}

void C_BasePropDoor::SendPlayerDoorCommand()
{
	if ( m_eDoorState == 0 || m_eDoorState == 4 )
	{
		if ( !( gpGlobals->curtime >= m_doorUseTimer.m_timestamp ) )
			return;
		engine->ServerCmd( "choose_opendoor\n" );
	}
	else if ( m_eDoorState == 2 )
	{
		if ( !( gpGlobals->curtime >= m_doorUseTimer.m_timestamp ) )
			return;
		engine->ServerCmd( "choose_closedoor\n" );
	}
	else
	{
		return;
	}
	m_doorUseTimer.Start( 0.05f );
}

void C_TerrorPlayer::PlayerUse( C_BaseEntity *preferred )
{
	if ( m_lifeState != LIFE_ALIVE || IsGhost() ||
		( m_lifeState == LIFE_ALIVE && m_isIncapacitated ) ||
		!( ( m_nButtons | m_afButtonPressed | m_afButtonReleased ) & IN_USE ) )
		return;
	C_BaseEntity *selected = preferred;
	if ( !selected )
		selected = FindUseEntity( player_use_radius.GetFloat(), 0.0f, 0.0f, NULL, false );
	if ( selected )
	{
		C_BasePropDoor *door = dynamic_cast< C_BasePropDoor * >( selected );
		if ( door )
			door->SendPlayerDoorCommand();
	}
	else if ( survivor_teammatefocus_enabled.GetBool() && GetTeamNumber() == TEAM_SURVIVOR && ( m_nButtons & IN_USE ) )
	{
		int character = GetNextTeammateFocusCharacter( true );
		if ( character == 8 )
		{
			if ( m_nTeammateFocusCharacter == 8 || !IsTeammateFocusThreatened( m_nTeammateFocusCharacter ) )
				character = GetNextTeammateFocusCharacter( false );
			else
				character = m_nTeammateFocusCharacter;
		}
		m_nTeammateFocusCharacter = character;
		if ( character < 8 )
			m_bTeammateFocusActive = true;
		else if ( m_bTeammateFocusActive )
			m_bTeammateFocusActive = false;
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif

#if defined( L4D2 )
void C_DynamicProp::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	if ( type == DATA_UPDATE_CREATED )
	{
		const float createTime = GetCreateTime();
		if ( createTime != 0.0f ) m_flAnimTime = createTime;
	}
	DisableCachedRenderBounds( m_bUseHitboxesForRenderBox );
}
#endif

#if defined( L4D2 )
void C_BasePropDoor::PostDataUpdate( DataUpdateType_t type )
{
	if ( type != DATA_UPDATE_CREATED )
	{
		const model_t *model = GetModel();
		BaseClass::PostDataUpdate( type );
		if ( model != GetModel() ) m_bRecreateVPhysics = true;
	}
	else BaseClass::PostDataUpdate( type );
}
#endif

#if defined( L4D2 )
#include "l4d2/entities/c_prop_door_rotating_checkpoint.inc"
#endif
