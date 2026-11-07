//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/info_remarkable.h"
#include "igamesystem.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_remarkable, CInfoRemarkable );
BEGIN_DATADESC( CInfoRemarkable )
	DEFINE_KEYFIELD( m_szRemarkContext, FIELD_STRING, "contextsubject" ),
END_DATADESC()

namespace
{
	// Actual CRemarkableEntityList107A8... owns native10840198 list. It clears
	// list storage at level shutdown; it NEVER deletes registered map entities.
	class CRemarkableEntityList : public CAutoGameSystem
	{
	public:
		CRemarkableEntityList() : CAutoGameSystem( "CRemarkableEntityList" ) {}
		virtual void LevelShutdownPostEntity() OVERRIDE { m_entities.Purge(); } // Native29B830, original game-system slot28 (no target PreClearSteam slot).
		CUtlLinkedList< CInfoRemarkable *, unsigned short > m_entities;
	};
	CRemarkableEntityList s_remarkableEntities;
}
const CUtlLinkedList< CInfoRemarkable *, unsigned short > &GetTerrorRemarkableEntities()
{
	return s_remarkableEntities.m_entities; // Native29B3E0 borrowed registry.
}
CInfoRemarkable::CInfoRemarkable() : m_nRemarkCount( 0 ), m_szRemarkContext( NULL_STRING )
{
	// Native factory29B670 uses actual CBaseEntity zeroing allocator1076 bytes.
}
CInfoRemarkable::~CInfoRemarkable()
{
	// Native29B960 removes FIRST matching raw pointer, then real base destructor.
	// Repeated Spawn can register duplicates; do not add an invented latch.
	s_remarkableEntities.m_entities.FindAndRemove( this );
}
void CInfoRemarkable::Spawn()
{
	// Native29B9A0 does not call BaseClass::Spawn; actual AddBefore(INVALID)
	// appends to the real list, retaining the entity's native creation order.
	s_remarkableEntities.m_entities.AddToTail( this );
	m_nRemarkCount = 0;
}
