//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Teleports a named entity to a given position and restores
//			it's physics state
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"


#include "in_buttons.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define	SF_TELEPORT_TO_SPAWN_POS	0x00000001
#define	SF_TELEPORT_INTO_DUCK		0x00000002 ///< episodic only: player should be ducked after this teleport

class CPointTeleport : public CBaseEntity
{
	DECLARE_CLASS( CPointTeleport, CBaseEntity );
public:
	void	Activate( void );

	void InputTeleport( inputdata_t &inputdata );

#if defined( L4D2 )
	void InputTeleportEntity( inputdata_t &inputdata );
	void InputTeleportToCurrentPos( inputdata_t &inputdata );
	void TeleportEntity( inputdata_t &inputdata, const Vector &origin, const QAngle &angles, bool useInputName );
#endif

private:
	
	bool	EntityMayTeleport( CBaseEntity *pTarget );

	Vector m_vSaveOrigin;
	QAngle m_vSaveAngles;

	DECLARE_DATADESC();
};


LINK_ENTITY_TO_CLASS( point_teleport, CPointTeleport );


BEGIN_DATADESC( CPointTeleport )

	DEFINE_FIELD( m_vSaveOrigin, FIELD_VECTOR ),
	DEFINE_FIELD( m_vSaveAngles, FIELD_VECTOR ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Teleport", InputTeleport ),

#if defined( L4D2 )
	DEFINE_INPUTFUNC( FIELD_STRING, "TeleportEntity", InputTeleportEntity ),
	DEFINE_INPUTFUNC( FIELD_VOID, "TeleportToCurrentPos", InputTeleportToCurrentPos ),
#endif

END_DATADESC()


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pTarget - 
// Output : Returns true if the entity may be teleported
//-----------------------------------------------------------------------------
bool CPointTeleport::EntityMayTeleport( CBaseEntity *pTarget )
{
	if ( pTarget->GetMoveParent() != NULL )
	{
		// Passengers in a vehicle are allowed to teleport; their behavior handles it
		CBaseCombatCharacter *pBCC = pTarget->MyCombatCharacterPointer();
		if ( pBCC == NULL || ( pBCC != NULL && pBCC->IsInAVehicle() == false ) )
			return false;
	}

	return true;
}

//------------------------------------------------------------------------------
// Purpose:
//------------------------------------------------------------------------------
void CPointTeleport::Activate( void )
{
	// Start with our origin point
	m_vSaveOrigin = GetAbsOrigin();
	m_vSaveAngles = GetAbsAngles();

	// Save off the spawn position of the target if instructed to do so
	if ( m_spawnflags & SF_TELEPORT_TO_SPAWN_POS )
	{
		CBaseEntity *pTarget = gEntList.FindEntityByName( NULL, m_target );
		if ( pTarget )
		{
			// If teleport object is in a movement hierarchy, remove it first
			if ( EntityMayTeleport( pTarget ) )
			{
				// Save the points
				m_vSaveOrigin = pTarget->GetAbsOrigin();
				m_vSaveAngles = pTarget->GetAbsAngles();
			}
			else
			{
				Warning("ERROR: (%s) can't teleport object (%s) as it has a parent (%s)!\n",GetDebugName(),pTarget->GetDebugName(),pTarget->GetMoveParent()->GetDebugName());
				BaseClass::Activate();
				return;
			}
		}
		else
		{
			Warning("ERROR: (%s) target '%s' not found. Deleting.\n", GetDebugName(), STRING(m_target));
			UTIL_Remove( this );
			return;
		}
	}

	BaseClass::Activate();
}

#if defined( L4D2 )
void CPointTeleport::TeleportEntity( inputdata_t &inputdata, const Vector &origin,
	const QAngle &angles, bool useInputName )
{
	const char *name = useInputName ? inputdata.value.String() : STRING( m_target );
	CBaseEntity *pTarget = gEntList.FindEntityByName( NULL, name, this,
		inputdata.pActivator, inputdata.pCaller );
	if ( !pTarget )
		return;
	if ( !EntityMayTeleport( pTarget ) )
	{
		Warning( "ERROR: (%s) can't teleport object (%s) as it has a parent (%s)!\n",
			GetDebugName(), pTarget->GetDebugName(), pTarget->GetMoveParent()->GetDebugName() );
		return;
	}
	pTarget->Teleport( &origin, &angles, NULL );
}

void CPointTeleport::InputTeleportEntity( inputdata_t &inputdata )
{
	TeleportEntity( inputdata, m_vSaveOrigin, m_vSaveAngles, true );
}

void CPointTeleport::InputTeleportToCurrentPos( inputdata_t &inputdata )
{
	if ( HasSpawnFlags( SF_TELEPORT_TO_SPAWN_POS ) )
		Warning( "%s: TeleportToCurrentPos input received; ignoring 'Teleport Home' spawnflag.\n", GetDebugName() );
	TeleportEntity( inputdata, GetAbsOrigin(), GetAbsAngles(), false );
}
#endif

//------------------------------------------------------------------------------
// Purpose:
//------------------------------------------------------------------------------
#if defined( L4D2 )
void CPointTeleport::InputTeleport( inputdata_t &inputdata )
{
	TeleportEntity( inputdata, m_vSaveOrigin, m_vSaveAngles, false );
}
#else
void CPointTeleport::InputTeleport( inputdata_t &inputdata )
{
	// Attempt to find the entity in question
	CBaseEntity *pTarget = gEntList.FindEntityByName( NULL, m_target, this, inputdata.pActivator, inputdata.pCaller );
	if ( pTarget == NULL )
		return;

	// If teleport object is in a movement hierarchy, remove it first
	if ( EntityMayTeleport( pTarget ) == false )
	{
		Warning("ERROR: (%s) can't teleport object (%s) as it has a parent (%s)!\n",GetDebugName(),pTarget->GetDebugName(),pTarget->GetMoveParent()->GetDebugName());
		return;
	}

	// in episodic, we have a special spawn flag that forces Gordon into a duck
#ifdef HL2_EPISODIC
	if ( (m_spawnflags & SF_TELEPORT_INTO_DUCK) && pTarget->IsPlayer() ) 
	{
		CBasePlayer *pPlayer = ToBasePlayer( pTarget );
		if ( pPlayer != NULL )
		{
			pPlayer->m_nButtons |= IN_DUCK;
			pPlayer->AddFlag( FL_DUCKING );
			pPlayer->m_Local.m_bDucked = true;
			pPlayer->m_Local.m_bDucking = true;
#if defined( L4D2 )
			pPlayer->m_Local.m_nDuckTimeMsecs = 0;
#else
			pPlayer->m_Local.m_flDucktime = 0.0f;
#endif
			pPlayer->SetViewOffset( VEC_DUCK_VIEW_SCALED( pPlayer ) );
			pPlayer->SetCollisionBounds( VEC_DUCK_HULL_MIN, VEC_DUCK_HULL_MAX );
		}
	}		
#endif

	pTarget->Teleport( &m_vSaveOrigin, &m_vSaveAngles, NULL );
}
#endif

