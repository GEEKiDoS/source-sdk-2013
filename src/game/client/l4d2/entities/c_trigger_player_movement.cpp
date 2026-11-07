//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/brush_roots_client.h"
#include "l4d2/entities/c_trigger_player_movement.h"
#include "cliententitylist.h"
#include "collisionutils.h"
#include "engine/IEngineTrace.h"
#include "in_buttons.h"
#include "ispatialpartition.h"
#include "tier0/memdbgon.h"

class C_TriggerPlayerMovement : public C_BaseTrigger
{
public:
	DECLARE_CLASS( C_TriggerPlayerMovement, C_BaseTrigger );
	DECLARE_CLIENTCLASS();
	C_TriggerPlayerMovement();
	virtual ~C_TriggerPlayerMovement();
	virtual datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // C0A2CB0.
	virtual void UpdatePartitionListEntry() OVERRIDE;
	virtual void StartTouch( C_BaseEntity *other ) OVERRIDE;
	virtual void EndTouch( C_BaseEntity *other ) OVERRIDE;
	C_TriggerPlayerMovement *m_pNext;
};
IMPLEMENT_CLIENTCLASS_DT( C_TriggerPlayerMovement, DT_TriggerPlayerMovement, CTriggerPlayerMovement )
END_RECV_TABLE()

// Exact existing native utility: insertion at head, unlink, LevelShutdown NULL.
C_EntityClassList< C_TriggerPlayerMovement > g_TriggerPlayerMovementList;
template<> C_TriggerPlayerMovement *C_EntityClassList< C_TriggerPlayerMovement >::m_pClassList = NULL;

C_TriggerPlayerMovement::C_TriggerPlayerMovement()
{
	g_TriggerPlayerMovementList.Insert( this );
}
C_TriggerPlayerMovement::~C_TriggerPlayerMovement()
{
	g_TriggerPlayerMovementList.Remove( this );
}
void C_TriggerPlayerMovement::UpdatePartitionListEntry()
{
	if ( !m_bClientSidePredicted )
	{
		BaseClass::UpdatePartitionListEntry();
		return;
	}
	// C0A2F90. Main-approved target capability adaptation: original insert256
	// is absent from the target's spatial partition. All touches use the list
	// below, not that bit. Remove the SAME native140 lists, insert into none.
	::partition->RemoveAndInsert( PARTITION_CLIENT_SOLID_EDICTS | PARTITION_CLIENT_RESPONSIVE_EDICTS |
		PARTITION_CLIENT_NON_STATIC_EDICTS, 0, CollisionProp()->GetPartitionHandle() );
}
void C_TriggerPlayerMovement::StartTouch( C_BaseEntity *other )
{
	// C0A2FC0: deliberately no base call, local-player filter, or reference count.
	if ( !other || !other->IsPlayer() ) return;
	C_BasePlayer *player = static_cast< C_BasePlayer * >( other );
	if ( m_spawnflags & 0x0800 ) player->ForceButtons( IN_DUCK );
	if ( m_spawnflags & 0x1000 ) player->ForceButtons( IN_SPEED );
	if ( m_spawnflags & 0x2000 ) player->DisableButtons( IN_JUMP );
	if ( m_spawnflags & 0x0080 ) player->m_Local.m_bAllowAutoMovement = false;
}
void C_TriggerPlayerMovement::EndTouch( C_BaseEntity *other )
{
	// C0A3040: overlapping triggers do NOT count their forced/disabled bits.
	if ( !other || !other->IsPlayer() ) return;
	C_BasePlayer *player = static_cast< C_BasePlayer * >( other );
	if ( m_spawnflags & 0x0800 ) player->UnforceButtons( IN_DUCK );
	if ( m_spawnflags & 0x1000 ) player->UnforceButtons( IN_SPEED );
	if ( m_spawnflags & 0x2000 ) player->EnableButtons( IN_JUMP );
	if ( m_spawnflags & 0x0080 ) player->m_Local.m_bAllowAutoMovement = true;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void TouchTriggers( C_BaseEntity *entity )
{
	// C0A3280/3390: stationary hull in collision space; non-BBOX entities use
	// their world AABB relative to collision origin. Do NOT sweep old->new.
	CCollisionProperty *collision = entity->CollisionProp();
	Vector mins, maxs;
	const Vector &origin = collision->GetCollisionOrigin();
	if ( collision->GetSolid() == SOLID_BBOX )
	{
		mins = collision->OBBMins();
		maxs = collision->OBBMaxs();
	}
	else
	{
		collision->WorldSpaceSurroundingBounds( &mins, &maxs );
		mins -= origin;
		maxs -= origin;
	}
	Ray_t ray;
	ray.Init( origin, origin, mins, maxs );
	for ( C_TriggerPlayerMovement *trigger = g_TriggerPlayerMovementList.m_pClassList;
		trigger; trigger = trigger->m_pNext )
	{
		if ( trigger->IsDormant() ) continue;
		CCollisionProperty *triggerCollision = trigger->CollisionProp();
		const int solidFlags = triggerCollision->GetSolidFlags();
		if ( !collision->ShouldTouchTrigger( solidFlags ) ) continue;
		bool intersects;
		if ( solidFlags & FSOLID_USE_TRIGGER_BOUNDS )
		{
			Vector triggerMins, triggerMaxs;
			triggerCollision->WorldSpaceTriggerBounds( &triggerMins, &triggerMaxs );
			intersects = IsBoxIntersectingRay( triggerMins, triggerMaxs, ray, 0.0f );
		}
		else
		{
			trace_t trace;
			enginetrace->ClipRayToCollideable( ray, 0x0200400B, triggerCollision, &trace );
			intersects = ( trace.contents & 0x0200400B ) != 0;
		}
		if ( !intersects ) continue;
		trace_t touchTrace;
		memset( &touchTrace, 0, sizeof( touchTrace ) ); // Native trace constructor.
		touchTrace.fraction = 1.0f;
		touchTrace.surface.name = "**empty**";
		touchTrace.endpos = ( trigger->GetAbsOrigin() + entity->GetAbsOrigin() ) * 0.5f;
		entity->PhysicsMarkEntitiesAsTouching( trigger, touchTrace );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
