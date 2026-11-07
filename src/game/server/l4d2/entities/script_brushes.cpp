//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/script_brushes.h"
#include "l4d2/player/terrorplayer.h"
#include "basecombatweapon.h"
#include "vphysics_interface.h"
#include "tier0/memdbgon.h"

IMPLEMENT_SERVERCLASS_ST( CBaseToggle, DT_BaseToggle )
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( script_clip_vphysics, CScriptVPhysicsClip );

LINK_ENTITY_TO_CLASS( script_func_button, CScriptBaseButton );

LINK_ENTITY_TO_CLASS( script_trigger_hurt, CScriptTriggerHurt );

LINK_ENTITY_TO_CLASS( script_trigger_multiple, CScriptTriggerMultiple );

LINK_ENTITY_TO_CLASS( script_trigger_once, CScriptTriggerOnce );

LINK_ENTITY_TO_CLASS( script_trigger_push, CScriptTriggerPush );

LINK_ENTITY_TO_CLASS( trigger_hurt_ghost, CTriggerHurtGhost );

LINK_ENTITY_TO_CLASS( trigger_auto_crouch, CTriggerAutoCrouch );

LINK_ENTITY_TO_CLASS( trigger_callback, CTriggerCallback );

LINK_ENTITY_TO_CLASS( trigger_active_weapon_detect, CTriggerActiveWeaponDetect );

BEGIN_DATADESC( CScriptVPhysicsClip )
	DEFINE_KEYFIELD( m_vExtent, FIELD_VECTOR, "extent" ),
END_DATADESC()

BEGIN_DATADESC( CScriptBaseButton )
	DEFINE_KEYFIELD( m_vExtent, FIELD_VECTOR, "extent" ),
END_DATADESC()

BEGIN_DATADESC( CScriptTriggerHurt )
	DEFINE_KEYFIELD( m_vExtent, FIELD_VECTOR, "extent" ),
END_DATADESC()

BEGIN_DATADESC( CScriptTriggerMultiple )
	DEFINE_KEYFIELD( m_vExtent, FIELD_VECTOR, "extent" ),
END_DATADESC()

BEGIN_DATADESC( CScriptTriggerOnce )
	DEFINE_KEYFIELD( m_vExtent, FIELD_VECTOR, "extent" ),
END_DATADESC()

BEGIN_DATADESC( CScriptTriggerPush )
	DEFINE_KEYFIELD( m_vExtent, FIELD_VECTOR, "extent" ),
END_DATADESC()

BEGIN_DATADESC( CTriggerAutoCrouch )
END_DATADESC()

BEGIN_DATADESC( CTriggerCallback )
END_DATADESC()

BEGIN_DATADESC( CTriggerActiveWeaponDetect )
	DEFINE_KEYFIELD( m_iszWeaponClassName, FIELD_STRING, "weaponclassname" ),
	DEFINE_THINKFUNC( ActiveWeaponThink ),
	DEFINE_OUTPUT( m_OnTouchedActiveWeapon, "OnTouchedActiveWeapon" ),
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CScriptBaseButton, DT_ScriptBaseButton )
END_SEND_TABLE()


// S:1E7C50: native toggle adds only the base-entity table.


// S:84000. No brush model is required; do not call CFuncVPhysicsClip::Spawn.
void CScriptVPhysicsClip::Spawn()
{
	SetMoveType( MOVETYPE_PUSH );
	SetSolid( SOLID_OBB );
	AddSolidFlags( FSOLID_NOT_SOLID );
	AddEffects( EF_NODRAW );
	CollisionProp()->SetCollisionBounds( -m_vExtent, m_vExtent );
	CreateVPhysics();
	IPhysicsObject *physics = VPhysicsGetObject();
	if ( physics )
		physics->EnableCollisions( !m_bDisabled );
}

// S:8B4B0: the base spawn deliberately runs BEFORE setting script bounds.
void CScriptBaseButton::Spawn()
{
	BaseClass::Spawn();
	SetSolid( SOLID_OBB );
	CollisionProp()->SetCollisionBounds( -m_vExtent, m_vExtent );
	VPhysicsInitShadow( false, false );
	// Exact native flags: NOT_SOLID plus the L4D2-specific 0x800 flag.
	// This is not SDK FSOLID_ROOT_PARENT_ALIGNED (0x100).
	SetSolidFlags( 0x804 );
	DispatchUpdateTransmitState();
}
int CScriptBaseButton::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS ); // S:87470
}

void CScriptTriggerHurt::Spawn() // S:205290
{
	BaseClass::Spawn();
	SetSolid( SOLID_OBB );
	CollisionProp()->SetCollisionBounds( -m_vExtent, m_vExtent );
	VPhysicsInitShadow( false, false );
}
void CScriptTriggerMultiple::Spawn() // S:2048F0
{
	BaseClass::Spawn();
	SetSolid( SOLID_OBB );
	CollisionProp()->SetCollisionBounds( -m_vExtent, m_vExtent );
	VPhysicsInitShadow( false, false );
}
void CScriptTriggerOnce::Spawn() // S:203A60
{
	BaseClass::Spawn();
	SetSolid( SOLID_OBB );
	CollisionProp()->SetCollisionBounds( -m_vExtent, m_vExtent );
	VPhysicsInitShadow( false, false );
}
void CScriptTriggerPush::Spawn() // S:204AF0
{
	BaseClass::Spawn();
	SetSolid( SOLID_OBB );
	CollisionProp()->SetCollisionBounds( -m_vExtent, m_vExtent );
	VPhysicsInitShadow( false, false );
}

// S:202FB0/1FFFF0. This filter intentionally does not consult spawnflags/filtername.
void CTriggerAutoCrouch::Spawn()
{
	BaseClass::Spawn();
	InitTrigger();
}
bool CTriggerAutoCrouch::PassesTriggerFilters( CBaseEntity *other )
{
	return other && other->IsPlayer();
}
void CTriggerAutoCrouch::StartTouch( CBaseEntity *other ) // S:1FFB00
{
	BaseClass::StartTouch( other );
	if ( IsTouching( other ) && other && other->IsPlayer() )
		static_cast< CTerrorPlayer * >( other )->m_isAutoCrouchEnabled = true;
}
void CTriggerAutoCrouch::EndTouch( CBaseEntity *other ) // S:1FFBA0
{
	BaseClass::EndTouch( other );
	// Native clears unconditionally, even if a second auto-crouch volume overlaps.
	if ( other && other->IsPlayer() )
		static_cast< CTerrorPlayer * >( other )->m_isAutoCrouchEnabled = false;
}

void CTriggerCallback::Spawn() // S:202FC0
{
	SetMoveType( MOVETYPE_NONE );
	SetSolid( SOLID_OBB );
	SetSolidFlags( FSOLID_TRIGGER | FSOLID_NOT_SOLID );
	AddSpawnFlags( SF_TRIGGER_ALLOW_CLIENTS | SF_TRIGGER_ALLOW_NPCS | SF_TRIGGER_ALLOW_PHYSICS );
}
void CTriggerCallback::StartTouch( CBaseEntity *other ) // S:1FFC10
{
	IPhysicsObject *physics = other->VPhysicsGetObject();
	if ( physics && ( physics->GetGameFlags() & FVPHYSICS_PLAYER_HELD ) )
		return;
	if ( !PassesTriggerFilters( other ) )
		return;
	if ( GetParent() && m_pfnCallback )
	{
		CBaseEntity *parent = GetParent();
		// Preserve the native second m_pParent resolution.
		if ( parent )
			( parent->*m_pfnCallback )( other );
	}
	BaseClass::StartTouch( other );
}

void CTriggerActiveWeaponDetect::Spawn() // S:2032F0
{
	BaseClass::Spawn();
	InitTrigger();
}
bool CTriggerActiveWeaponDetect::PassesTriggerFilters( CBaseEntity *other )
{
	return other && other->IsPlayer(); // S:200020, no inherited filter test
}
void CTriggerActiveWeaponDetect::StartTouch( CBaseEntity *other ) // S:1FFCA0
{
	BaseClass::StartTouch( other );
	if ( m_hTouchingEntities.Count() > 0 )
		SetContextThink( &CTriggerActiveWeaponDetect::ActiveWeaponThink, gpGlobals->curtime, "ActiveWeaponContext" );
}
void CTriggerActiveWeaponDetect::ActiveWeaponThink() // S:1FBBD0
{
	const int count = m_hTouchingEntities.Count();
	if ( count <= 0 )
		return;
	for ( int i = 0; i < count; ++i )
	{
		// The original snapshots only the count; it reads the live vector each time.
		// Main's minimal-skip rule: a reentrant output may shrink the live vector.
		if ( i >= m_hTouchingEntities.Count() )
			break;
		CBaseEntity *entity = m_hTouchingEntities[i].Get();
		if ( !entity || !entity->IsPlayer() )
			continue;
		CBasePlayer *player = ToBasePlayer( entity );
		CBaseCombatWeapon *weapon = player->GetActiveWeapon();
		if ( weapon && !Q_strcmp( weapon->GetClassname(), STRING( m_iszWeaponClassName ) ) )
			m_OnTouchedActiveWeapon.FireOutput( player, this );
	}
	SetContextThink( &CTriggerActiveWeaponDetect::ActiveWeaponThink, gpGlobals->curtime, "ActiveWeaponContext" );
}