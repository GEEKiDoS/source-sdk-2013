// L4D2-only replacement of existing CNavArea MarkAsBlocked,
// UpdateBlockedFromNavBlockers, UnblockArea and UpdateBlocked bodies.
// Include in nav_area.cpp with nav_entities.h and world.h already visible.
extern ConVar z_debug_breakables;

void CNavArea::MarkAsBlocked( int teamID, CBaseEntity *blocker, bool generateEvent )
{
	bool affectsFlow = false;
	if ( blocker && ( blocker->ClassMatches( "func_nav_blocker" ) || blocker->ClassMatches( "script_nav_blocker" ) ) )
	{
		m_attributeFlags |= 0x80000000u;
		CFuncNavBlocker *navBlocker = dynamic_cast< CFuncNavBlocker * >( blocker );
		// Ledger306: classname mutation can make the native cast NULL;
		// skip only the field-dependent flow-bit write in that case.
		if ( navBlocker && navBlocker->AffectsFlow() )
		{
			m_attributeFlags |= 0x08000000u;
			affectsFlow = true;
		}
	}
	bool oldBlocked[MAX_NAV_TEAMS];
	bool wasBlocked = false;
	for ( int i = 0; i < MAX_NAV_TEAMS; ++i )
	{
		oldBlocked[i] = m_isBlocked[i];
		wasBlocked |= oldBlocked[i];
	}
	if ( teamID == TEAM_ANY )
		for ( int i = 0; i < MAX_NAV_TEAMS; ++i ) m_isBlocked[i] = true;
	else
	{
		const int team = teamID % MAX_NAV_TEAMS;
		if ( team >= 0 ) m_isBlocked[team] = true; // Ledger306: ignore only the invalid write, then continue.
	}
	if ( !wasBlocked )
	{
		if ( generateEvent )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "nav_blocked" );
			if ( event ) { event->SetInt( "area", m_id ); event->SetInt( "blocked", 1 ); gameeventmanager->FireEvent( event ); }
		}
		if ( nav_debug_blocked.GetBool() )
		{
			if ( blocker ) ConColorMsg( Color( 0, 255, 128, 255 ), "%s %d blocked area %d\n", blocker->GetDebugName(), blocker->entindex(), GetID() );
			else ConColorMsg( Color( 0, 255, 128, 255 ), "non-entity blocked area %d\n", GetID() );
		}
		TheNavMesh->OnAreaBlocked( this, affectsFlow );
	}
	else
	{
		if ( nav_debug_blocked.GetBool() )
		{
			if ( blocker ) ConColorMsg( Color( 0, 255, 128, 255 ), "DUPE: %s %d blocked area %d\n", blocker->GetDebugName(), blocker->entindex(), GetID() );
			else ConColorMsg( Color( 0, 255, 128, 255 ), "DUPE: non-entity blocked area %d\n", GetID() );
		}
		for ( int i = 0; i < MAX_NAV_TEAMS; ++i )
			if ( oldBlocked[i] != m_isBlocked[i] )
			{
				Warning( "MarkAsBlocked: Area %d is already blocked with a different team id! This may cause flow bugs.\n", GetID() );
				break;
			}
	}
}

void CNavArea::UpdateBlockedFromNavBlockers()
{
	// S476380: this helper generates NO event and invokes NO mesh callback.
	Extent bounds;
	GetExtent( &bounds );
	m_attributeFlags &= ~0x88000000u;
	const int result = CFuncNavBlocker::CalculateBlocked( m_isBlocked, bounds.lo, bounds.hi );
	if ( result > 0 )
	{
		m_attributeFlags |= 0x80000000u;
		if ( result == 2 ) m_attributeFlags |= 0x08000000u;
	}
}

void CNavArea::UnblockArea()
{
	// S478280 has no team argument and always clears both native teams.
	const bool wasBlocked = m_isBlocked[0] || m_isBlocked[1];
	const bool affectsFlow = ( m_attributeFlags & 0x88000000u ) == 0x88000000u;
	for ( int i = 0; i < MAX_NAV_TEAMS; ++i ) m_isBlocked[i] = false;
	if ( wasBlocked )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "nav_blocked" );
		if ( event ) { event->SetInt( "area", m_id ); event->SetInt( "blocked", 0 ); gameeventmanager->FireEvent( event ); }
		if ( nav_debug_blocked.GetBool() ) ConColorMsg( Color( 255, 0, 128, 255 ), "area %d is unblocked by UnblockArea\n", GetID() );
		TheNavMesh->OnAreaUnblocked( this, affectsFlow );
	}
}

#pragma float_control( precise, on, push )
void CNavArea::UpdateBlockedFromPhysicalEntities( bool force, int teamID, bool preserveNavBlockers )
{
	// S47BEE0. Out-of-world corners retain previous blocking; no repair.
	Vector worldMins, worldMaxs;
	GetWorldEntity()->GetWorldBounds( worldMins, worldMaxs );
	for ( int i = 0; i < NUM_CORNERS; ++i )
		if ( !IsPointInBox( GetCorner( static_cast< NavCornerType >( i ) ), worldMins, worldMaxs ) ) return;
	Vector origin = GetCenter();
	origin.z += 35.5f;
	const float rawX = GetSizeX() * 0.5f - 5.0f;
	const float rawY = GetSizeY() * 0.5f - 5.0f;
	// S47C004/47C032 COMISS/JBE sends unordered sizes to the upper16.
	const float sizeX = rawX < 16.0f ? ( rawX < 1.0f ? 1.0f : rawX ) : 16.0f;
	const float sizeY = rawY < 16.0f ? ( rawY < 1.0f ? 1.0f : rawY ) : 16.0f;
	Vector mins( -sizeX, -sizeY, 0.0f );
	Vector maxs( sizeX, sizeY, VEC_DUCK_HULL_MAX.z - 35.5f );
	CTraceFilterWalkableEntities filter( NULL, COLLISION_GROUP_PLAYER_MOVEMENT, WALK_THRU_PROP_DOORS | WALK_THRU_BREAKABLES );
	trace_t trace;
	UTIL_TraceHull( origin, origin, mins, maxs, 0x2420B, &filter, &trace );
	if ( !trace.startsolid )
	{
		if ( !preserveNavBlockers )
		{
			if ( z_debug_breakables.GetBool() ) NDebugOverlay::Box( origin, mins, maxs, 0, 255, 0, 10, 5.0f );
			else for ( int i = 0; i < MAX_NAV_TEAMS; ++i ) m_isBlocked[i] = false;
		}
	}
	else if ( force )
	{
		if ( trace.m_pEnt && FClassnameIs( trace.m_pEnt, "func_door" ) )
		{
			const int flags = trace.m_pEnt->GetSpawnFlags();
			teamID = TEAM_ANY;
			if ( flags & 0x60000 )
			{
				if ( !( flags & 0x20000 ) ) teamID = 2;
				else if ( !( flags & 0x40000 ) ) teamID = 3;
			}
		}
		if ( teamID == TEAM_ANY )
			for ( int i = 0; i < MAX_NAV_TEAMS; ++i ) m_isBlocked[i] = true;
		else
		{
			const int team = teamID % MAX_NAV_TEAMS;
			if ( team >= 0 ) m_isBlocked[team] = true; // Ledger306.
		}
	}
	if ( TheNavMesh->GetMarkedArea() == this )
	{
		const bool blocked = teamID == 2 && ( m_attributeFlags & 0x40000 ) ? true :
			teamID == TEAM_ANY ? m_isBlocked[0] || m_isBlocked[1] :
			teamID % MAX_NAV_TEAMS >= 0 ? m_isBlocked[teamID % MAX_NAV_TEAMS] : false;
		NDebugOverlay::Box( origin, mins, maxs, blocked ? 255 : 0, blocked ? 0 : 255, 0, 64, 3.0f );
	}
}

void CNavArea::UpdateBlocked( bool force, int teamID )
{
	// S47C2E0. Keep physical blockers alongside team-specific nav blockers.
	if ( !force && !m_blockedTimer.IsElapsed() ) return;
	const float previous = m_blockedTimer.m_timestamp > 0.0f ? m_blockedTimer.m_duration : 0.0f;
	float interval = previous + 1.0f;
	if ( interval > 5.0f ) interval = 5.0f;
	m_blockedTimer.Start( interval );
	bool oldBlocked[MAX_NAV_TEAMS];
	for ( int i = 0; i < MAX_NAV_TEAMS; ++i ) oldBlocked[i] = m_isBlocked[i];
	const bool wasBlocked = oldBlocked[0] || oldBlocked[1];
	bool affectsFlow = ( m_attributeFlags & 0x88000000u ) == 0x88000000u;
	if ( ( m_attributeFlags & 0x80000000u ) && force ) UpdateBlockedFromNavBlockers();
	if ( !( m_attributeFlags & 0x80000000u ) ||
		( !( m_attributeFlags & 0x40000 ) && !m_isBlocked[0] ) || !m_isBlocked[1] )
		UpdateBlockedFromPhysicalEntities( force, teamID, ( m_attributeFlags & 0x80000000u ) && ( m_isBlocked[0] || m_isBlocked[1] ) );
	const bool isBlocked = m_isBlocked[0] || m_isBlocked[1];
	if ( wasBlocked && isBlocked )
		for ( int i = 0; i < MAX_NAV_TEAMS; ++i )
			if ( oldBlocked[i] != m_isBlocked[i] )
			{
				Warning( "UpdateBlocked: Area %d is already blocked with a different team id! This may cause flow bugs.\n", GetID() );
				break;
			}
	// Real native campaign-specific policy: any c8 prefix forces this hint.
	if ( !Q_strncmp( STRING( gpGlobals->mapname ), "c8", 2 ) ) affectsFlow = true;
	if ( wasBlocked != isBlocked )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "nav_blocked" );
		if ( event ) { event->SetInt( "area", m_id ); event->SetInt( "blocked", isBlocked ); gameeventmanager->FireEvent( event ); }
		if ( isBlocked )
		{
			if ( nav_debug_blocked.GetBool() ) ConColorMsg( Color( 0, 255, 128, 255 ), "area %d is blocked\n", GetID() );
			if ( ( m_attributeFlags & 0x88000000u ) == 0x88000000u ) affectsFlow = true;
			TheNavMesh->OnAreaBlocked( this, affectsFlow );
		}
		else
		{
			if ( nav_debug_blocked.GetBool() ) ConColorMsg( Color( 0, 128, 255, 255 ), "area %d is unblocked\n", GetID() );
			TheNavMesh->OnAreaUnblocked( this, affectsFlow );
		}
	}
}
#pragma float_control( pop )
