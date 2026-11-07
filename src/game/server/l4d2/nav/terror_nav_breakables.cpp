//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "props.h"
#include "func_break.h"
#include "nav_mesh.h"
#include "tier0/memdbgon.h"

// The sole shared props enum/parser owns the native authored Tank/Mob flags.
extern ConVar z_debug_breakables;

void CPhysicsProp::InitializeTerrorNavAttributes()
{
	if ( !z_debug_breakables.GetBool() )
	{
		if ( m_breakableType == 2 ) SetInteraction( PROPINTER_GAME_ONLY_BREAKABLE_BY_TANK );
		else if ( m_breakableType == 1 ) SetInteraction( PROPINTER_GAME_ONLY_BREAKABLE_BY_MOB );
	}
	if ( HasInteraction( PROPINTER_GAME_ONLY_BREAKABLE_BY_TANK ) )
	{
		m_hasTankGlow = true;
		AddSpawnFlags( 0x1000 );
		SetAIWalkable( false );
		TheNavMesh->OnBreakableCreated( this );
	}
	if ( HasInteraction( PROPINTER_GAME_ONLY_BREAKABLE_BY_MOB ) )
	{
		AddSpawnFlags( 0x2000 );
		SetAIWalkable( false );
		TheNavMesh->OnBreakableCreated( this ); // Native duplicates if BOTH interactions are set.
	}
}

// S:198B60, real OnBreak virtual. Exhaustive raw MSVC COL/hierarchy scan found
//19 primary CPhysicsProp/CBreakableProp descendants, ALL v928=163EB0(false):
// PhysicsProp634744/BasePropDoor63599C/PropDoorRotating635E8C/BreakableProp6364FC/
// DynamicProp636994/PhysicsPropRespawnable636F34/CarProp6373E4/CarGlassProp63787C/
// OrnamentProp637D14/PhysSphere6381B4/PropDoorRotatingBreakable638D8C/
// PropBreakableWall63927C/PropDoorRotatingCheckpoint639774/FinaleTrigger67514C/
// FuelBarrelProp688974/FuelBarrelPiece688E34/PropHealthCabinet68965C/
// PropMinigun6C76F4/PropMountedGun6C8D8C. FuelBarrelProp2C1200 and Piece2BFF80
// are the only derived wrappers calling198B60. Four direct OnBreak slots are
// PhysicsProp/Respawnable/CarProp/PhysSphere; every other descendant uses its
// separate196450 no-op. Full COL/base/secondary-view/ref evidence:
// E:/l4d2_work/m6/scenario/prop_onbreak_predicate_rtti.txt.
void CPhysicsProp::OnBreak( const Vector &velocity, const AngularImpulse &angles, CBaseEntity *breaker )
{
	if ( !z_debug_breakables.GetBool() && m_breakableType > 0 )
		TheNavMesh->OnBreakableBroken( this );
}

void CBreakable::InitializeTerrorNavAttributes()
{
	if ( !z_debug_breakables.GetBool() )
	{
		const char *name = STRING( GetEntityName() );
		if ( m_breakableType == 2 || !Q_stricmp( name, "hulk" ) || !Q_stricmp( name, "tank" ) )
		{
			AddSpawnFlags( 0x1000 );
			SetInteraction( PROPINTER_GAME_ONLY_BREAKABLE_BY_TANK );
			SetAIWalkable( false ); // Native6DA50 sets eflag0x04000000, not NO_DAMAGE_FORCES.
		}
		else if ( m_breakableType == 1 || !Q_stricmp( name, "zombie" ) )
		{
			AddSpawnFlags( 0x2000 );
			SetInteraction( PROPINTER_GAME_ONLY_BREAKABLE_BY_MOB );
			SetAIWalkable( false );
		}
	}
	TheNavMesh->OnBreakableCreated( this ); // Native brush Spawn always queues.
}
