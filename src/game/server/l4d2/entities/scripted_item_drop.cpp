//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/scripted_item_drop.h"
#include "items.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( scripted_item_drop, CScriptItem );

BEGIN_DATADESC( CScriptItem )
	DEFINE_OUTPUT( m_OnPlayerPickup, "OnPlayerPickup" ),
END_DATADESC()


CScriptItem::CScriptItem() : m_iDesiredMoveType( MOVETYPE_VPHYSICS )
{}

void CScriptItem::Precache()
{
		CBaseAnimating::Precache(); // Native10DD10 bypasses CItem sound precache.
		PrecacheModel( STRING( GetModelName() ) );
	}

void CScriptItem::Spawn()
{
		PrecacheModel( STRING( GetModelName() ) );
		SetModel( STRING( GetModelName() ) );
		BaseClass::Spawn();
		if ( m_iDesiredMoveType != MOVETYPE_VPHYSICS && m_iDesiredMoveType != MOVETYPE_FLYGRAVITY )
		{
			SetMoveType( MOVETYPE_NONE );
			if ( m_iDesiredMoveType != MOVETYPE_NONE ) SetMoveType( static_cast< MoveType_t >( m_iDesiredMoveType ) );
		}
	}

bool CScriptItem::KeyValue( const char *key, const char *value )
{
		if ( FStrEq( key, "activateWhenAtRest" ) && atoi( value ) ) ActivateWhenAtRest();
		if ( FStrEq( key, "movetype" ) ) m_iDesiredMoveType = atoi( value );
		return CBaseEntity::KeyValue( key, value ); // Both native side effects still forward.
	}

bool CScriptItem::MyTouch( CBasePlayer *player )
{ return true; }

void CScriptItem::OnPlayerPickup( CBaseEntity *other, CBasePlayer *player )
{
		// Native10DB30 does NOT consume, disable, respawn or run CItem's continuation.
		m_OnPlayerPickup.FireOutput( player, this );
	}

bool CScriptItem::CreateItemVPhysicsObject()
{
		// Native10E120. Preserve raw/native network bit; shared solid-flag
		// census determines its named game consumer versus engine-only gap.
		AddSolidFlags( 0x1000 );
		return BaseClass::CreateItemVPhysicsObject();
	}

// Uses the existing real CItem substrate. The native pickup-continuation and
// virtual physics hook deltas are in scripted_item_drop_integration.txt.
