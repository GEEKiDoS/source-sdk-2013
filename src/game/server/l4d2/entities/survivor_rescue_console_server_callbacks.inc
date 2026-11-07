// Transplant into existing src/game/server/l4d2/survivor_rescue.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

namespace
{
class CollectRescueNavigation
{
public:
 bool operator()( TerrorNavArea *area ) { m_areas.AddToTail( area ); return true; }
 CUtlVector< TerrorNavArea * > m_areas;
};
void SelectRescueNavigation( CBaseEntity *entity )
{
 TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( entity->GetAbsOrigin(), false, 350.0f, true, true, TEAM_SPECTATOR ) );
 if ( !area ) return; // Named native branch NULL use: ledger306.
 CollectRescueNavigation collect;
 CollectSurroundingTerrorAreas( area, area->GetCenter(), collect, 350.0f, 0, -1 );
 FOR_EACH_VEC( collect.m_areas, i )
 {
  collect.m_areas[i]->AddSpawnAttributes( TERROR_NAV_RESCUE_CLOSET );
  TheNavMesh->AddToSelectedSet( collect.m_areas[i] );
 }
}
}

// Native callback 0x002CCAC0 (command_function).
static void CC_rescue_show( const CCommand &args )
{
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "info_survivor_rescue" ) ) != NULL )
	{
		entity->m_debugOverlays |= 0x21;
		NDebugOverlay::Axis( entity->GetAbsOrigin(), entity->GetAbsAngles(), 10.0f, true, 100000.0f );
	}
}

// Native object 0x0084B90C; callback 0x002CCAC0; command_function.
static ConCommand rescue_show_command( "rescue_show", CC_rescue_show, "Shows debugging info for info_survivor_rescue ents", 0x00004004 );

// Native callback 0x002CCB60 (command_function).
static void CC_rescue_show_blocked( const CCommand &args )
{
	int blocked = 0;
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "info_survivor_rescue" ) ) != NULL )
	{
		const Vector position = entity->GetAbsOrigin() + Vector( 0, 0, 1.0f );
		Vector mins, maxs;
		entity->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
		mins -= entity->GetAbsOrigin(); maxs -= entity->GetAbsOrigin();
		trace_t trace;
		UTIL_TraceHull( position, position, mins, maxs, 33636875u, NULL, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
		if ( trace.startsolid )
		{
			++blocked;
			const Vector &origin = entity->GetAbsOrigin();
			Msg( "%d: Found a blocked info_survivor_rescue at setpos %f %f %f\n", blocked, origin.x, origin.y, origin.z );
			if ( entity->m_debugOverlays & 0x29 ) entity->m_debugOverlays &= ~0x29;
			else entity->m_debugOverlays |= 0x29;
		}
	}
}

// Native object 0x0084BA08; callback 0x002CCB60; command_function.
static ConCommand rescue_show_blocked_command( "rescue_show_blocked", CC_rescue_show_blocked, "Shows bboxes for blocked info_survivor_rescue ents", 0x00004004 );

// Native callback 0x002CF980 (command_function).
static void CC_rescue_update_nav( const CCommand &args )
{
	FOR_EACH_VEC( TheNavAreas, i ) ToTerrorNavArea( TheNavAreas[i] )->RemoveSpawnAttributes( TERROR_NAV_RESCUE_CLOSET );
	TheNavMesh->ClearSelectedSet();
	if ( args.ArgC() > 1 )
	{
		const int index = atoi( args[1] );
		if ( !index ) return;
		CBaseEntity *entity = UTIL_EntityByIndex( index );
		if ( entity && FClassnameIs( entity, "info_survivor_rescue" ) ) SelectRescueNavigation( entity );
		return;
	}
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "info_survivor_rescue" ) ) != NULL ) SelectRescueNavigation( entity );
}

// Native object 0x0084BA2C; callback 0x002CF980; command_function.
static ConCommand rescue_update_nav_command( "rescue_update_nav", CC_rescue_update_nav, "Updates RESCUE_CLOSET nav flags", 0x00004004 );

#endif
