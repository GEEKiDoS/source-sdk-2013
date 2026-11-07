//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "vphysics_interface.h"
#include "tier0/memdbgon.h"

// Native CBaseEntity066480/051370/066370/066400. Datamap names are
// m_PreStasisMoveType (+4, FIELD_INTEGER) and m_bIsInStasis (+8, BOOLEAN).
// This is independent of EFL_DORMANT/MakeDormant and has no SendProp.
void CBaseEntity::AdjustSuspendedEntityTimes( bool suspend )
{
	const int ticks = TIME_TO_TICKS( gpGlobals->curtime );
	for ( int i = 0; i < m_aThinkFunctions.Count(); ++i )
	{
		thinkfunc_t &think = m_aThinkFunctions[i];
		if ( think.m_nNextThinkTick > 0 )
		{
			think.m_nNextThinkTick += suspend ? -ticks : ticks;
			think.m_nLastThinkTick += suspend ? -ticks : ticks;
		}
	}

	for ( datamap_t *map = GetDataDescMap(); map; map = map->baseMap )
	{
		for ( int i = 0; i < map->dataNumFields; ++i )
		{
			typedescription_t &field = map->dataDesc[i];
			byte *address = reinterpret_cast< byte * >( this ) + field.fieldOffset[TD_OFFSET_NORMAL];
			if ( field.fieldType == FIELD_TIME )
			{
				float &time = *reinterpret_cast< float * >( address );
				if ( !suspend || time > 0.0f )
					time += suspend ? -gpGlobals->curtime : gpGlobals->curtime;
			}
			else if ( field.fieldType == FIELD_TICK )
			{
				int &tick = *reinterpret_cast< int * >( address );
				if ( !suspend || tick != TICK_NEVER_THINK )
					tick += suspend ? -ticks : ticks;
			}
		}
	}
}

void CBaseEntity::SetInStasis( bool suspend )
{
	// Repeated calls deliberately still adjust the clocks. The original tests
	// the old flag only inside the suspend body, after this adjustment.
	AdjustSuspendedEntityTimes( suspend );
	if ( suspend )
	{
		if ( !m_bIsInStasis )
		{
			AddEFlags( EFL_NO_THINK_FUNCTION );
			m_PreStasisMoveType = GetMoveType();
			CheckHasThinkFunction( false );
			if ( edict() )
			{
				AddSolidFlags( FSOLID_NOT_SOLID );
				SetMoveType( MOVETYPE_NONE );
				IPhysicsObject *physics = VPhysicsGetObject();
				if ( physics )
				{
					physics->EnableCollisions( false );
					physics->Sleep();
				}
				AddEffects( EF_NODRAW );
				SetNextThink( TICK_NEVER_THINK );
			}
		}
	}
	else if ( edict() )
	{
		CheckHasThinkFunction( GetNextThink() != TICK_NEVER_THINK );
		RemoveSolidFlags( FSOLID_NOT_SOLID );
		SetMoveType( static_cast< MoveType_t >( m_PreStasisMoveType ) );
		RemoveEffects( EF_NODRAW );
		IPhysicsObject *physics = VPhysicsGetObject();
		if ( physics )
		{
			physics->EnableCollisions( true );
			physics->Wake();
		}
	}
	m_bIsInStasis = suspend;
}
