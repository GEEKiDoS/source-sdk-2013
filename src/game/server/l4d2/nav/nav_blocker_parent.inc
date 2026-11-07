// L4D2-only replacement of the CFuncNavBlocker section in nav_entities.cpp.
// Keep the existing sole class, registry and non-L4D2 implementation.
BEGIN_DATADESC( CFuncNavBlocker )
	DEFINE_INPUTFUNC( FIELD_VOID, "BlockNav", InputBlockNav ),
	DEFINE_INPUTFUNC( FIELD_VOID, "UnblockNav", InputUnblockNav ),
	DEFINE_KEYFIELD( m_blockedTeamNumber, FIELD_INTEGER, "teamToBlock" ),
	DEFINE_KEYFIELD( m_bAffectsFlow, FIELD_BOOLEAN, "affectsFlow" ),
END_DATADESC()
LINK_ENTITY_TO_CLASS( func_nav_blocker, CFuncNavBlocker );
CUtlLinkedList< CFuncNavBlocker * > CFuncNavBlocker::gm_NavBlockers;

void CFuncNavBlocker::Spawn()
{
	// S48C790: do not initially BlockNav/UnblockNav. StartDisabled is not
	// a native key. The entity allocation supplies zero blocking bytes.
	gm_NavBlockers.AddToTail( this );
	// Preserve the native keyvalue domain, including NetProps reads/writes.
	if ( !m_blockedTeamNumber )
		m_blockedTeamNumber = -1;
	SetMoveType( MOVETYPE_NONE );
	SetModel( STRING( GetModelName() ) );
	AddEffects( EF_NODRAW );
	SetCollisionGroup( COLLISION_GROUP_NONE );
	SetSolid( SOLID_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );
	CollisionProp()->WorldSpaceAABB( &m_CachedMins, &m_CachedMaxs );
}
void CFuncNavBlocker::UpdateOnRemove()
{
	UnblockNav();
	gm_NavBlockers.FindAndRemove( this );
	BaseClass::UpdateOnRemove();
}
void CFuncNavBlocker::InputBlockNav( inputdata_t &input ) { BlockNav(); }
void CFuncNavBlocker::InputUnblockNav( inputdata_t &input ) { UnblockNav(); }
void CFuncNavBlocker::BlockNav()
{
	if ( m_blockedTeamNumber == -1 )
		for ( int i = 0; i < MAX_NAV_TEAMS; ++i ) m_isBlockingNav[i] = true;
	else
	{
		const int team = m_blockedTeamNumber % MAX_NAV_TEAMS;
		if ( team >= 0 ) m_isBlockingNav[team] = true; // Ledger306.
	}
	Extent extent;
	extent.Init( this );
	TheNavMesh->ForAllAreasOverlappingExtent( *this, extent );
}
void CFuncNavBlocker::UnblockNav()
{
	if ( m_blockedTeamNumber == -1 )
		for ( int i = 0; i < MAX_NAV_TEAMS; ++i ) m_isBlockingNav[i] = false;
	else
	{
		const int team = m_blockedTeamNumber % MAX_NAV_TEAMS;
		if ( team >= 0 ) m_isBlockingNav[team] = false; // Ledger306.
	}
	UpdateBlocked();
}
void CFuncNavBlocker::UpdateBlocked()
{
	NavAreaCollector collector( true );
	Extent extent;
	extent.Init( this );
	TheNavMesh->ForAllAreasOverlappingExtent( collector, extent );
	FOR_EACH_VEC( collector.m_area, i ) collector.m_area[i]->UpdateBlocked( true, TEAM_ANY );
}
bool CFuncNavBlocker::operator()( CNavArea *area )
{
	// Only this call crosses from native team IDs to the SDK nav-area API.
	const int teamID = m_blockedTeamNumber == -1 ? TEAM_ANY : m_blockedTeamNumber % MAX_NAV_TEAMS;
	area->MarkAsBlocked( teamID, this, true );
	return true;
}
int CFuncNavBlocker::CalculateBlocked( bool *resultByTeam, const Vector &mins, const Vector &maxs )
{
	// S48BD30 returns 0=none,1=ordinary blocker,2=affectsFlow. Preserve
	// the early exit: later affectsFlow blockers do not upgrade filled teams.
	for ( int i = 0; i < MAX_NAV_TEAMS; ++i ) resultByTeam[i] = false;
	int teamsBlocked = 0, result = 0;
	FOR_EACH_LL( gm_NavBlockers, index )
	{
		CFuncNavBlocker *blocker = gm_NavBlockers[index];
		bool intersects = false;
		for ( int team = 0; team < MAX_NAV_TEAMS; ++team )
		{
			if ( blocker->m_isBlockingNav[team] && !resultByTeam[team] &&
				( intersects || ( intersects = IsBoxIntersectingBox( blocker->m_CachedMins, blocker->m_CachedMaxs, mins, maxs ) ) ) )
			{
				result = MAX( result, blocker->m_bAffectsFlow ? 2 : 1 );
				++teamsBlocked;
				resultByTeam[team] = true;
			}
		}
		if ( teamsBlocked == MAX_NAV_TEAMS ) break;
	}
	return result;
}
int CFuncNavBlocker::DrawDebugTextOverlays()
{
	int offset = BaseClass::DrawDebugTextOverlays();
	if ( m_debugOverlays & OVERLAY_TEXT_BIT )
	{
		CFmtStr text;
		for ( int teamNumber = 2; teamNumber < 4; ++teamNumber )
		{
			if ( IsBlockingNav( teamNumber ) )
			{
				CTeam *team = GetGlobalTeam( teamNumber );
				if ( team ) EntityText( offset++, text.sprintf( "blocking team %s", team->GetName() ), 0 );
				else EntityText( offset++, text.sprintf( "blocking team %d", teamNumber ), 0 );
			}
		}
		Extent extent;
		extent.Init( this );
		NavAreaCollector collector( true );
		TheNavMesh->ForAllAreasOverlappingExtent( collector, extent );
		NDebugOverlay::Box( vec3_origin, extent.lo, extent.hi, 255, 0, 0, 10, 0.01023f );
		FOR_EACH_VEC( collector.m_area, i )
		{
			collector.m_area[i]->GetExtent( &extent );
			NDebugOverlay::Box( vec3_origin, extent.lo, extent.hi, 0, 255, 0, 10, 0.01023f );
		}
	}
	return offset;
}
