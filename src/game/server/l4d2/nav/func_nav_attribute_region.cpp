//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/director/zombiemanager.h"
#include "engine/ivmodelinfo.h"
#include "tier0/memdbgon.h"

// S:2E6FE0/2E8B20. Both map names use the SAME native class. Its six bools
// have no constructor stores: CBaseEntity's entity allocator supplies zero.
class CFuncNavAttributeRegion : public CBaseEntity
{
public:
	DECLARE_CLASS( CFuncNavAttributeRegion, CBaseEntity );
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
	virtual int DrawDebugTextOverlays() OVERRIDE;
	void InputApplyNavAttributes( inputdata_t &inputdata );

private:
	bool m_bPrecise;
	bool m_bCrouch;
	bool m_bStairs;
	bool m_bTankOnly;
	bool m_bMobOnly;
	bool m_bRemoveAttributes;
};

LINK_ENTITY_TO_CLASS( func_nav_attribute_region, CFuncNavAttributeRegion );
LINK_ENTITY_TO_CLASS( point_nav_attribute_region, CFuncNavAttributeRegion );
BEGIN_DATADESC( CFuncNavAttributeRegion )
	DEFINE_INPUTFUNC( FIELD_VOID, "ApplyNavAttributes", InputApplyNavAttributes ),
	DEFINE_KEYFIELD( m_bPrecise, FIELD_BOOLEAN, "precise" ),
	DEFINE_KEYFIELD( m_bCrouch, FIELD_BOOLEAN, "crouch" ),
	DEFINE_KEYFIELD( m_bStairs, FIELD_BOOLEAN, "stairs" ),
	DEFINE_KEYFIELD( m_bTankOnly, FIELD_BOOLEAN, "tank_only" ),
	DEFINE_KEYFIELD( m_bMobOnly, FIELD_BOOLEAN, "mob_only" ),
	DEFINE_KEYFIELD( m_bRemoveAttributes, FIELD_BOOLEAN, "remove_attributes" ),
END_DATADESC()

void CFuncNavAttributeRegion::Spawn()
{
	const char *modelName = STRING( GetModelName() );
	const model_t *model = modelinfo->GetModel( modelinfo->GetModelIndex( modelName ) );
	if ( model )
	{
		SetSolid( SOLID_BSP );
		SetModel( modelName );
	}
	else
	{
		SetSolid( SOLID_BBOX );
	}
	AddSolidFlags( FSOLID_NOT_SOLID );
	SetMoveType( MOVETYPE_NONE );
	AddEffects( EF_NODRAW );
	if ( TheNavMesh->IsLoaded() )
	{
		inputdata_t inputdata;
		InputApplyNavAttributes( inputdata );
		TheZombieManager->OnSpawnAreasChanged(); // Native manager byte649, not a flow recompute.
	}
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
// S:2E6D40. Surrounding-bounds admission precedes the actual volume clip;
// collision-only bounds or centers/corners alone are not the native prefilter.
void CFuncNavAttributeRegion::InputApplyNavAttributes( inputdata_t &inputdata )
{
	Extent region;
	CollisionProp()->WorldSpaceSurroundingBounds( &region.lo, &region.hi );
	RemoveSolidFlags( FSOLID_NOT_SOLID );
	unsigned int movement = 0;
	if ( m_bPrecise ) movement |= NAV_MESH_PRECISE;
	if ( m_bCrouch ) movement |= NAV_MESH_CROUCH;
	if ( m_bStairs ) movement |= NAV_MESH_STAIRS;
	if ( m_bTankOnly ) movement |= 0x10000;
	if ( m_bMobOnly ) movement |= 0x20000;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		Extent bounds;
		area->GetExtent( &bounds );
		bounds.hi.z += 1.0f;
		// COMISS/JB rejects unordered values at every original comparison.
		if ( !( bounds.hi.x >= region.lo.x ) || !( region.hi.x >= bounds.lo.x ) ||
			!( bounds.hi.y >= region.lo.y ) || !( region.hi.y >= bounds.lo.y ) ||
			!( bounds.hi.z >= region.lo.z ) || !( region.hi.z >= bounds.lo.z ) ) continue;

		Ray_t ray;
		ray.Init( vec3_origin, vec3_origin, bounds.lo, bounds.hi );
		trace_t trace;
		enginetrace->ClipRayToEntity( ray, MASK_ALL, this, &trace );
		if ( !trace.startsolid ) continue;

		if ( m_bRemoveAttributes )
		{
			area->RemoveSpawnAttributes( GetSpawnFlags() );
			area->RemoveAttributes( movement );
		}
		else
		{
			area->AddSpawnAttributes( GetSpawnFlags() );
			area->SetAttributes( area->GetAttributes() | movement );
		}
	}
	// Native always sets NOT_SOLID on exit; it does not restore the old flag.
	AddSolidFlags( FSOLID_NOT_SOLID );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

int CFuncNavAttributeRegion::DrawDebugTextOverlays()
{
	int line = BaseClass::DrawDebugTextOverlays();
	if ( !( m_debugOverlays & OVERLAY_TEXT_BIT ) ) return line;
	// Native107AD358 is a debug-label table, distinct from the command-token
	// parser. In particular IGNORE_VISIBILITY deliberately has no label here.
	static const struct { unsigned int bit; const char *name; } attributes[] =
	{
		{ 0x2, "EMPTY" }, { 0x4, "STOP_SCAN" }, { 0x40, "FINALE" },
		{ 0x80, "PLAYER_START" }, { 0x400, "NOT_CLEARABLE" }, { 0x800, "CHECKPOINT" },
		{ 0x1000, "OBSCURED" }, { 0x2000, "NO_MOBS" }, { 0x4000, "THREAT" },
		{ 0x80000, "NOTHREAT" }, { 0x100000, "LYINGDOWN" }, { 0x10000, "RESCUE_CLOSET" },
		{ 0x8000, "RESCUE_VEHICLE" }, { 0x100, "BATTLEFIELD" }, { 0x20, "BATTLESTATION" },
		{ 0x20000, "ESCAPE_ROUTE" }, { 0x40000, "DOOR" }
	};
	for ( unsigned int bit = 1; bit < 0x80000000U; bit *= 2 )
	{
		if ( !( bit & GetSpawnFlags() ) ) continue;
		for ( int i = 0; i < ARRAYSIZE( attributes ); ++i )
			if ( attributes[i].bit == bit ) { EntityText( line++, attributes[i].name, 0.01023f ); break; }
	}
	return line;
}

class CScriptNavAttributeRegion : public CFuncNavAttributeRegion
{
public:
	DECLARE_CLASS( CScriptNavAttributeRegion, CFuncNavAttributeRegion );
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
private:
	Vector m_vExtent;
};

LINK_ENTITY_TO_CLASS( script_nav_attribute_region, CScriptNavAttributeRegion );
BEGIN_DATADESC( CScriptNavAttributeRegion )
	DEFINE_KEYFIELD( m_vExtent, FIELD_VECTOR, "extent" ),
END_DATADESC()

void CScriptNavAttributeRegion::Spawn()
{
	// S:2E8BE0: keep this order, including loaded-NAV application BEFORE the
	// derived OBB/extents/static-physics setup. No second Apply is performed.
	BaseClass::Spawn();
	SetSolid( SOLID_OBB );
	CollisionProp()->SetCollisionBounds( -m_vExtent, m_vExtent );
	VPhysicsInitStatic();
}
