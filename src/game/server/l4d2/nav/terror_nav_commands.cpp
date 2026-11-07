//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_commands.h"
#include "l4d2/nav/terror_nav_path.h"
#include "l4d2/player/terrorplayer.h"
#include "fogcontroller.h"
#include "filesystem.h"
#include "ndebugoverlay.h"
#include "tier0/memdbgon.h"

extern ConVar nav_max_view_distance;
extern ConVar nav_potentially_visible_dot_tolerance;
extern ConVar z_debug;
// Real server VGUI provider owned by ui_nav_tools; compose its bridge packet.
void NavUICommand();

#pragma float_control( precise, on, push )

namespace
{
struct NavEditorToken
{
	const char *name;
	unsigned int bits;
};
// S107AD358 is NOT the separate region/script parser at S2E25B0.
const NavEditorToken s_spawnTokens[] =
{
	{ "EMPTY", 2 }, { "STOP_SCAN", 4 }, { "FINALE", 0x40 },
	{ "PLAYER_START", 0x80 }, { "NOT_CLEARABLE", 0x400 },
	{ "CHECKPOINT", 0x800 }, { "OBSCURED", 0x1000 }, { "NO_MOBS", 0x2000 },
	{ "THREAT", 0x4000 }, { "NOTHREAT", 0x80000 }, { "LYINGDOWN", 0x100000 },
	{ "RESCUE_CLOSET", 0x10000 }, { "RESCUE_VEHICLE", 0x8000 },
	{ "BATTLEFIELD", 0x100 }, { "BATTLESTATION", 0x20 },
	{ "ESCAPE_ROUTE", 0x20000 }, { "DOOR", 0x40000 }
};
const NavEditorToken s_moveTokens[] =
{
	{ "CROUCH", 1 }, { "JUMP", 2 }, { "PRECISE", 4 }, { "NO_JUMP", 8 },
	{ "STOP", 0x10 }, { "RUN", 0x20 }, { "WALK", 0x40 }, { "AVOID", 0x80 },
	{ "TRANSIENT", 0x100 }, { "DONT_HIDE", 0x200 }, { "STAND", 0x400 },
	{ "NO_HOSTAGES", 0x800 }, { "STAIRS", 0x1000 }, { "NO_MERGE", 0x2000 },
	{ "OBSTACLE_TOP", 0x4000 }, { "CLIFF", 0x8000 },
	{ "PLAYERCLIP", 0x40000 }, { "BREAKABLEWALL", 0x80000 }
};
const NavEditorToken s_troubleTokens[] =
{
	{ "orphan", 1 }, { "battlefield", 2 }, { "finale", 4 }, { "checkpoint", 8 },
	{ "overlapping_areas", 0x10 }, { "corner_to_corner", 0x20 },
	{ "climb_up_from_stairs", 0x40 }, { "connection_to_overlapping_area", 0x80 },
	{ "long_horizontal_connections", 0x100 }, { "multiple_connections", 0x200 },
	{ "floating_or_interpenetrating_areas", 0x400 }, { "non_walkable_connections", 0x800 },
	{ "ladder_disconnected", 0x1000 }, { "ladder_overlapping", 0x2000 },
	{ "ladder_not_climbable", 0x4000 }, { "too_small", 0x8000 },
	{ "long_vertical_connections", 0x10000 },
	{ "short_vertical_one_way_connections", 0x20000 }, { "all", 0xFFFFFFFFu }
};

unsigned int EditorToken( const char *name, const NavEditorToken *tokens, int count, unsigned int missing = 0 )
{
	for ( int i = 0; i < count; ++i )
		if ( !Q_stricmp( name, tokens[i].name ) ) return tokens[i].bits;
	return missing;
}

// S206B40: retain the native command-index gate, without replay/admin additions.
bool NavCommandAllowed()
{
	int index = UTIL_GetCommandClientIndex();
	return ( !engine->IsDedicatedServer() || index <= 0 ) && index <= 1;
}

void NavPickSound()
{
	CBasePlayer *host = UTIL_GetListenServerHost();
	if ( host ) host->EmitSound( "EDIT_PLACE_PICK" );
}

struct ChangeEditorAttribute
{
	unsigned int bits;
	bool spawn;
	bool clear;
	bool operator()( CNavArea *base )
	{
		bool toggle = !clear && TheNavMesh->IsSelectedSetEmpty();
		if ( spawn )
		{
			TerrorNavArea *area = ToTerrorNavArea( base );
			if ( clear || ( toggle && area->HasSpawnAttributes( bits ) ) ) area->RemoveSpawnAttributes( bits );
			else area->AddSpawnAttributes( bits );
		}
		else
		{
			if ( clear || ( toggle && base->HasAttributes( bits ) ) ) base->RemoveAttributes( bits );
			else base->SetAttributes( base->GetAttributes() | bits );
		}
		return true;
	}
};

struct SelectVisibleArea
{
	bool operator()( CNavArea *area ) { TheNavMesh->AddToSelectedSet( area ); return true; }
};

int CompleteTokenTable( const char *partial, char commands[64][64], const NavEditorToken *tokens, int count, int used )
{
	if ( Q_strlen( partial ) >= 64 ) return used;
	char prefix[68];
	Q_strncpy( prefix, partial, sizeof( prefix ) );
	char *space = Q_strstr( prefix, " " );
	if ( !space ) return used;
	*space++ = 0;
	int length = Q_strlen( space );
	for ( int i = 0; i < count && used < 64; ++i )
	{
		if ( !Q_strnicmp( tokens[i].name, space, length ) )
			Q_snprintf( commands[used++], 64, "%s %s", prefix, tokens[i].name );
	}
	return used;
}
int PopulationNameCompare( const char *const *left, const char *const *right )
{
	return strcmp( *left, *right );
}

void PrintFogController( CFogController *fog, bool master )
{
	if ( master )
		Msg( "master fog controller is %s - start: %.0f end: %.0f density: %.0f fog Z: %.0f\n",
			fog->GetDebugName(), double( fog->m_fog.start ), double( fog->m_fog.end ),
			double( fog->m_fog.maxdensity ), double( fog->m_fog.farz ) );
	else
		Msg( "fog controller %s has start: %.0f end: %.0f density: %.0f fog Z: %.0f\n",
			fog->GetDebugName(), double( fog->m_fog.start ), double( fog->m_fog.end ),
			double( fog->m_fog.maxdensity ), double( fog->m_fog.farz ) );
}
}

unsigned int TerrorNavMesh::FindPopulationIndex( const char *name ) const
{
	FOR_EACH_VEC( m_populationDirectory, i )
		if ( !Q_stricmp( STRING( m_populationDirectory[i] ), name ) ) return i + 1;
	return 0;
}

const char *TerrorNavMesh::GetPopulationNameForIndex( unsigned int index ) const
{
	return index && index <= static_cast< unsigned int >( m_populationDirectory.Count() ) ? STRING( m_populationDirectory[index - 1] ) : NULL;
}

const char *TerrorNavArea::GetPopulationName() const
{
	return TheTerrorNavMesh()->GetPopulationNameForIndex( m_population );
}

// S476B00: the existing flow caller and debug callers share this ONE real area owner.
double TerrorNavArea::ComputeSurfaceHeightChange( const TerrorNavArea *destination ) const
{
	if ( HasFlowFlatGeometry() && destination->HasFlowFlatGeometry() )
		return double( destination->GetCorner( SOUTH_WEST ).z ) - GetCorner( SOUTH_WEST ).z;
	Vector close;
	if ( destination->HasFlowFlatGeometry() ) close = destination->GetCenter();
	else destination->GetClosestPointOnArea( GetCenter(), &close );
	if ( HasFlowFlatGeometry() ) return double( close.z ) - GetCorner( SOUTH_WEST ).z;
	// Native clamps X/Y as floats, then subtracts GetZ's unrounded x87 result.
	const Vector &low = GetCorner( NORTH_WEST );
	const Vector &high = GetCorner( SOUTH_EAST );
	float x = close.x;
	if ( !( x - low.x >= 0.0f ) ) x = low.x;
	if ( x - high.x >= 0.0f ) x = high.x;
	float y = low.y;
	if ( close.y - y >= 0.0f ) y = close.y;
	if ( y - high.y >= 0.0f ) y = high.y;
	return double( close.z ) - GetZ( x, y );
}

void TerrorNavMesh::CommandNavFogSet( const CCommand &args )
{
	if ( !NavCommandAllowed() ) return;
	if ( args.ArgC() == 1 )
	{
		CUtlVector< const char * > names;
		FOR_EACH_VEC( m_populationDirectory, i ) names.AddToTail( STRING( m_populationDirectory[i] ) );
		names.Sort( PopulationNameCompare );
		FOR_EACH_VEC( names, i )
		{
			if ( FindPopulationIndex( names[i] ) == m_selectedFogPopulation ) Msg( "--> %-26s", names[i] );
			else Msg( "%-30s", names[i] );
			if ( ( i + 1 ) % 3 == 0 ) Msg( "\n" );
		}
		Msg( "\n" );
	}
	else
	{
		m_selectedFogPopulation = FindPopulationIndex( args[1] );
		// Native wording/polarity is deliberately backwards.
		Msg( "%s", m_selectedFogPopulation ? "No nav fog.\n" : "Fog unselected\n" );
	}
}

void TerrorNavMesh::CommandNavFogMark()
{
	if ( !NavCommandAllowed() ) return;
	if ( IsSelectedSetEmpty() )
	{
		if ( GetSelectedArea() ) ToTerrorNavArea( GetSelectedArea() )->m_population = m_selectedFogPopulation;
	}
	else
	{
		const NavAreaVector &selected = GetSelectedSet();
		FOR_EACH_VEC( selected, i ) ToTerrorNavArea( selected[i] )->m_population = m_selectedFogPopulation;
	}
	ClearSelectedSet();
}

void TerrorNavMesh::CommandNavFogPick()
{
	if ( !NavCommandAllowed() ) return;
	if ( GetSelectedArea() )
	{
		NavPickSound();
		m_selectedFogPopulation = ToTerrorNavArea( GetSelectedArea() )->m_population;
	}
	SetMarkedArea( NULL ); // S4812A0 also clears marked ladder/corner, via the real setter.
}

void TerrorNavMesh::CommandNavFogSelectPlace( const CCommand &args )
{
	if ( !NavCommandAllowed() ) return;
	ClearSelectedSet();
	if ( args.ArgC() < 2 ) return;
	unsigned int population = FindPopulationIndex( args[1] );
	int count = 0;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		if ( ToTerrorNavArea( TheNavAreas[i] )->m_population == population ) { ++count; AddToSelectedSet( TheNavAreas[i] ); }
	}
	Msg( "Selected %d %s areas\n", count, args[1] );
}

void TerrorNavMesh::CommandNavFogReplace( const CCommand &args )
{
	if ( !NavCommandAllowed() || args.ArgC() < 3 ) return;
	unsigned int from = FindPopulationIndex( args[1] );
	if ( !from ) return;
	unsigned int to = FindPopulationIndex( args[2] );
	if ( !to ) return;
	ClearSelectedSet();
	int count = 0;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( area->m_population == from ) { ++count; area->m_population = to; }
	}
	Msg( "Replaced %s with %s in %d areas\n", args[1], args[2], count );
}

void TerrorNavMesh::CommandNavFogPrintUsed()
{
	if ( !NavCommandAllowed() ) return;
	CUtlVector< unsigned int > used;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		unsigned int index = ToTerrorNavArea( TheNavAreas[i] )->m_population;
		if ( index && used.Find( index ) == used.InvalidIndex() ) used.AddToTail( index );
	}
	FOR_EACH_VEC( used, i )
	{
		const char *name = GetPopulationNameForIndex( used[i] );
		if ( name ) Msg( "%s\n", name ); // Undefined NULL-format operand: skip only this print.
	}
}

int TerrorNavArea::GetRawVisibilityCount( bool includeInherited ) const
{
	int count = 0;
	FOR_EACH_VEC( m_potentiallyVisibleAreas, i )
		if ( m_potentiallyVisibleAreas[i].attributes & 3 ) ++count;
	if ( includeInherited && m_inheritVisibilityFrom.area )
	{
		const CAreaBindInfoArray &inherited = m_inheritVisibilityFrom.area->m_potentiallyVisibleAreas;
		FOR_EACH_VEC( inherited, i ) if ( inherited[i].attributes & 3 ) ++count;
	}
	return count; // Raw counts intentionally do NOT deduplicate or apply local NOT_VISIBLE overrides.
}

#pragma float_control( precise, on, push )
CNavArea::VisibilityType TerrorNavArea::ComputeDebugVisibility( const TerrorNavArea *area ) const
{
	float distanceSq = area->GetCenter().DistToSqr( GetCenter() );
	float range = nav_max_view_distance.GetFloat();
	if ( range > 0.00001f && distanceSq > range * range ) return NOT_VISIBLE;
	SetupPVS();
	const Vector eye( 0, 0, 53.25f );
	if ( !area->IsInPVS() ) return NOT_VISIBLE; // Real shared PVS buffer remains owned by nav_area.cpp.
	Vector nw = GetCorner( NORTH_WEST ) + eye;
	Vector ne = GetCorner( NORTH_EAST ) + eye;
	Vector sw = GetCorner( SOUTH_WEST ) + eye;
	Vector se = GetCorner( SOUTH_EAST ) + eye;
	Vector center = GetCenter() + eye;
	float lowZ = MIN( MIN( MIN( nw.z, ne.z ), se.z ), sw.z );
	float highZ = MAX( MAX( MAX( nw.z, ne.z ), se.z ), sw.z );
	Vector mins( nw.x - center.x, nw.y - center.y, lowZ - center.z );
	Vector maxs( se.x - center.x, se.y - center.y,
		float( double( highZ ) + 0.1 - double( center.z ) ) );
	Vector target;
	Vector otherMin = area->GetCorner( NORTH_WEST ), otherMax = area->GetCorner( SOUTH_EAST );
	CalcClosestPointOnAABB( otherMin, otherMax, center, target );
	target.z = area->GetZ( target ) + eye.z;
	trace_t trace;
	CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
	UTIL_TraceHull( center, target, mins, maxs, 33579073, &filter, &trace );
	if ( trace.fraction == 1.0f || ( trace.endpos.x > otherMin.x && otherMax.x > trace.endpos.x && trace.endpos.y > otherMin.y && otherMax.y > trace.endpos.y ) ) return COMPLETELY_VISIBLE;
	unsigned char vis = area->IsVisibleFrom( center ) ? 3 : 0;
	Vector toCenter = GetCenter() - area->GetCenter();
	VectorNormalize( toCenter );
	float tolerance = nav_potentially_visible_dot_tolerance.GetFloat();
	for ( float y = 12.5f; y <= GetSizeY() - 12.5f; y += 25.0f )
	{
		for ( float x = 12.5f; x <= GetSizeX() - 12.5f; x += 25.0f )
		{
			if ( vis == POTENTIALLY_VISIBLE ) return POTENTIALLY_VISIBLE;
			Vector point = GetCorner( NORTH_WEST ) + Vector( x, y, 0 );
			point.z = GetZ( point ) + eye.z;
			Vector direction = point - center;
			VectorNormalize( direction );
			if ( distanceSq <= 1000000.0f || direction.Dot( toCenter ) < tolerance )
			{
				if ( area->IsVisibleFrom( point ) ) vis |= 1;
				else vis &= ~2;
			}
		}
	}
	return static_cast< VisibilityType >( vis );
}
#pragma float_control( pop )

void TerrorNavMesh::CommandNavSelectVisible( bool completely )
{
	if ( !NavCommandAllowed() ) return;
	CNavArea *area = GetMarkedArea();
	if ( !area ) return;
	ClearSelectedSet();
	NavPickSound();
	SelectVisibleArea select;
	if ( completely ) area->ForAllCompletelyVisibleAreas( select );
	else area->ForAllPotentiallyVisibleAreas( select );
}

void TerrorNavMesh::CommandNavSelectViewDistance()
{
	if ( !NavCommandAllowed() ) return;
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return;
	ClearSelectedSet();
	player->EmitSound( "EDIT_PLACE_PICK" );
	float range = nav_max_view_distance.GetFloat();
	// Reuse the actual NavAreaCollector specialization so the native search marker
	// and first-use RNG draw are shared with visibility generation, not duplicated.
	NavAreaCollector collector;
	collector.m_area.EnsureCapacity( 1000 );
	ForAllAreasInRadius( collector, player->GetAbsOrigin(), range );
	FOR_EACH_VEC( collector.m_area, i ) AddToSelectedSet( collector.m_area[i] );
	Msg( "%d areas selected\n", GetSelecteSetSize() );
}

void TerrorNavMesh::CommandNavSelectVisibleSetSize( const CCommand &args )
{
	if ( !NavCommandAllowed() ) return;
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return;
	ClearSelectedSet();
	player->EmitSound( "EDIT_PLACE_PICK" );
	if ( args.ArgC() == 1 ) return;
	int threshold = atoi( args[1] ), total = 0, low = 999999999, high = 0;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		int count = ToTerrorNavArea( TheNavAreas[i] )->GetRawVisibilityCount( true );
		if ( count >= threshold ) AddToSelectedSet( TheNavAreas[i] );
		total += count;
		if ( low >= count ) low = count;
		if ( high <= count ) high = count;
	}
	Msg( "%d areas selected\n", GetSelecteSetSize() );
	// Standing integer-divide-by-zero rule: omit this one quotient-dependent print.
	if ( TheNavAreas.Count() ) Msg( "NavMesh Visibility List Lengths:  min = %d, avg = %d, max = %d\n", low, total / TheNavAreas.Count(), high );
}

void TerrorNavMesh::CommandNavPrintVisibilityInfo()
{
	if ( !NavCommandAllowed() || !UTIL_GetCommandClient() ) return;
	int total = 0, low = 999999999, high = 0;
	CNavArea *longFrom = NULL, *longTo = NULL;
	float longestSquared = 0.0f;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		int count = area->GetRawVisibilityCount( true );
		total += count;
		if ( low >= count ) low = count;
		if ( high <= count ) high = count;
		// S2E7630: visit local entries first, including NOT_VISIBLE override marks.
		struct FindLongestVisible
		{
			const CNavArea *source;
			CNavArea *target;
			float squared;
			bool operator()( CNavArea *candidate )
			{
				float distance = candidate->GetCenter().DistToSqr( source->GetCenter() );
				if ( distance > squared ) { target = candidate; squared = distance; }
				return true;
			}
		} find = { area, NULL, 0.0f };
		area->ForAllPotentiallyVisibleAreas( find );
		if ( find.target && find.squared > longestSquared ) { longFrom = area; longTo = find.target; longestSquared = find.squared; }
	}
	if ( TheNavAreas.Count() ) Msg( "NavMesh Visibility List Lengths:  min = %d, avg = %d, max = %d\n", low, total / TheNavAreas.Count(), high );
	if ( longFrom && longTo ) Msg( "Longest distance between at-least-partially visible areas: %d sees %d at range %.0f\n", longFrom->GetID(), longTo->GetID(), sqrt( double( longestSquared ) ) );
	for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "env_fog_controller" ); entity; entity = gEntList.FindEntityByClassname( entity, "env_fog_controller" ) )
	{
		CFogController *fog = dynamic_cast< CFogController * >( entity );
		if ( !fog ) continue; // Standing wrong-type minimal-skip rule.
		PrintFogController( fog, false );
	}
	if ( FogSystem() && FogSystem()->GetMasterFogController() ) PrintFogController( FogSystem()->GetMasterFogController(), true );
}

bool TerrorNavMesh::CheckNavFileWritable()
{
	if ( m_navFileWritable ) return true;
	const char *filename = GetFilename();
	if ( !filename ) { Warning( "Couldn't test if nav file was writable, file name pointer is null!" ); return false; }
	int length = Q_strlen( filename );
	if ( length < 5 ) { Warning( "Couldn't test if nav file was writable, file name too short!" ); return false; }
	// Native replaces ".nav" with "_nav.txt". Keep its shared filename slash mutation.
	for ( char *p = const_cast< char * >( filename ); *p; ++p ) if ( *p == '/' ) *p = '\\';
	char textName[MAX_PATH];
	Q_strncpy( textName, filename, sizeof( textName ) );
	Q_StripExtension( textName, textName, sizeof( textName ) );
	Q_strncat( textName, "_nav.txt", sizeof( textName ) );
	bool navLocked = filesystem->FileExists( filename ) && !filesystem->IsFileWritable( filename );
	bool textLocked = !navLocked && filesystem->FileExists( textName ) && !filesystem->IsFileWritable( textName );
	if ( navLocked || textLocked )
	{
		Warning( "*******************************************\n" );
		Warning( "*******************************************\n" );
		Warning( "%s", navLocked ? "** ERROR: .NAV FILE NOT WRITABLE!!!      **\n" : "** ERROR: _NAV.TXT FILE NOT WRITABLE!!!  **\n" );
		Warning( "** YOU MUST CHECK IT OUT FROM PERFORCE!  **\n" );
		Warning( "*******************************************\n" );
		Warning( "*******************************************\n" );
		return false;
	}
	m_navFileWritable = true;
	return true;
}

void TerrorNavMesh::CommandNavEditorAttribute( const CCommand &args, bool clear )
{
	if ( !clear && !CheckNavFileWritable() ) return;
	if ( args.ArgC() < 2 )
	{
		if ( clear ) Msg( "Usage: %s <attribute1> [attribute2...]\n", args[0] );
		else Msg( "Usage: %s <attribute> [attribute2...]\n", args[0] );
		return;
	}
	for ( int i = 1; i < args.ArgC(); ++i )
	{
		unsigned int spawn = EditorToken( args[i], s_spawnTokens, ARRAYSIZE( s_spawnTokens ) );
		unsigned int move = EditorToken( args[i], s_moveTokens, ARRAYSIZE( s_moveTokens ) );
		if ( spawn || move )
		{
			ChangeEditorAttribute change = { spawn ? spawn : move, spawn != 0, clear };
			ForAllSelectedAreas( change );
		}
		else Msg( "Unknown attribute '%s'", args[i] );
	}
}

void CC_clear_attribute( const CCommand &args ) { TheTerrorNavMesh()->CommandNavEditorAttribute( args, true ); }
void CC_mark( const CCommand &args ) { TheTerrorNavMesh()->CommandNavEditorAttribute( args, false ); }
void CC_wipe_attributes()
{
	ChangeEditorAttribute change = { 0xFFFF, true, true };
	TheNavMesh->ForAllSelectedAreas( change );
	TheNavMesh->ClearSelectedSet();
}
void CC_current_flow_distance( const CCommand &args )
{
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_GetCommandClient() );
	if ( !player ) return;
	float flow = player->GetFlowDistance( 0 );
	float maximum = TheTerrorNavMesh()->GetMaxFlowDistance();
	double percent = maximum > 0.0f ? player->GetFlowDistance( 0 ) * 100.0 / maximum : 0.0;
	Msg( "Current flow distance: %f / %f %% \n", double( flow ), percent );
}
void CC_nav_auto_build_area() { if ( NavCommandAllowed() ) TheTerrorNavMesh()->CommandNavAutoBuild(); }
void CC_nav_fix_playerclips( const CCommand &args )
{
	FOR_EACH_VEC( TheNavAreas, i )
	{
		CNavArea *area = TheNavAreas[i];
		trace_t trace;
		// Native NULL-filter engine trace, CONTENTS_PLAYERCLIP only, no admin gate.
		Ray_t ray;
		ray.Init( area->GetCenter() + Vector( 0, 0, 16 ), area->GetCenter() + Vector( 0, 0, 32 ) );
		enginetrace->TraceRay( ray, 0x10000, NULL, &trace );
		if ( r_visualizetraces.GetBool() ) DebugDrawLine( trace.startpos, trace.endpos, 255, 255, 0, true, -1.0f );
		if ( trace.fraction >= 1.0f || trace.DidHitNonWorldEntity() ) area->RemoveAttributes( TerrorNavArea::NAV_PLAYERCLIP );
		else area->SetAttributes( area->GetAttributes() | TerrorNavArea::NAV_PLAYERCLIP );
	}
}
void CC_nav_fog_mark() { TheTerrorNavMesh()->CommandNavFogMark(); }
void CC_nav_fog_pick() { TheTerrorNavMesh()->CommandNavFogPick(); }
void CC_nav_fog_print_used( const CCommand &args ) { TheTerrorNavMesh()->CommandNavFogPrintUsed(); }
void CC_nav_fog_replace( const CCommand &args ) { TheTerrorNavMesh()->CommandNavFogReplace( args ); }
void CC_nav_fog_select_place( const CCommand &args ) { TheTerrorNavMesh()->CommandNavFogSelectPlace( args ); }
void CC_nav_fog_set( const CCommand &args ) { TheTerrorNavMesh()->CommandNavFogSet( args ); }
void CC_nav_gui()
{
	NavUICommand();
}
void CC_nav_print_visibility_info( const CCommand &args ) { TheTerrorNavMesh()->CommandNavPrintVisibilityInfo(); }
void CC_nav_print_visible_set_counts( const CCommand &args )
{
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		Msg( "%8d %8d %8d %8d\n", i, area->GetID(), area->GetRawVisibilityCount( false ), area->GetRawVisibilityCount( true ) );
	}
}
void CC_nav_select_completely_visible() { TheTerrorNavMesh()->CommandNavSelectVisible( true ); }
void CC_nav_select_partially_visible() { TheTerrorNavMesh()->CommandNavSelectVisible( false ); }
void CC_nav_select_threat()
{
	if ( !NavCommandAllowed() ) return;
	TheNavMesh->ClearSelectedSet();
	FOR_EACH_VEC( TheNavAreas, i ) if ( ToTerrorNavArea( TheNavAreas[i] )->HasSpawnAttributes( 0x4000 ) ) TheNavMesh->AddToSelectedSet( TheNavAreas[i] );
	Msg( "Selected %d areas.\n", TheNavMesh->GetSelecteSetSize() );
}
void CC_nav_select_view_distance( const CCommand &args ) { TheTerrorNavMesh()->CommandNavSelectViewDistance(); }
void CC_nav_select_visible_set_size( const CCommand &args ) { TheTerrorNavMesh()->CommandNavSelectVisibleSetSize( args ); }
void CC_nav_test_visibility()
{
	if ( !NavCommandAllowed() ) return;
	TerrorNavArea *from = ToTerrorNavArea( TheNavMesh->GetMarkedArea() );
	TerrorNavArea *to = ToTerrorNavArea( TheNavMesh->GetSelectedArea() );
	if ( !from || !to ) return;
	NavPickSound();
	DevMsg( "Visibility from %d to %d is %d\n", from->GetID(), to->GetID(), from->ComputeDebugVisibility( to ) );
}
void CC_nav_trouble_report( const CCommand &args )
{
	unsigned int mask = args.ArgC() > 1 ? EditorToken( args[1], s_troubleTokens, ARRAYSIZE( s_troubleTokens ), 0xFFFFFFFFu ) : 0xFFFFFFFFu;
	TheTerrorNavMesh()->CommandNavTroubleReport( mask, false );
}
void CC_nav_trouble_report_corner_to_corner( const CCommand &args ) { TheTerrorNavMesh()->CommandNavTroubleReport( 0x20, false ); }
void CC_nav_trouble_report_invalid( const CCommand &args ) { TheTerrorNavMesh()->CommandNavTroubleReport( 1 | 2 | 4 | 8 | 0x40, false ); }
void CC_nav_trouble_report_suggestions( const CCommand &args ) { TheTerrorNavMesh()->CommandNavTroubleReport( 0, true ); }
void CC_select_cliff()
{
	TheNavMesh->ClearSelectedSet();
	FOR_EACH_VEC( TheNavAreas, i ) if ( TheNavAreas[i]->HasAttributes( 0x8000 ) ) TheNavMesh->AddToSelectedSet( TheNavAreas[i] );
}
void CC_select_invalid_finale_nospawn()
{
	TheNavMesh->ClearSelectedSet();
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( area->HasSpawnAttributes( 0x2000 ) && !area->HasSpawnAttributes( 0x40 ) ) TheNavMesh->AddToSelectedSet( area );
	}
}
void CC_select_with_attribute( const CCommand &args )
{
	TheNavMesh->ClearSelectedSet();
	if ( args.ArgC() != 2 ) { Msg( "Usage: %s <attribute>\n", args[0] ); return; }
	unsigned int spawn = EditorToken( args[1], s_spawnTokens, ARRAYSIZE( s_spawnTokens ) );
	unsigned int move = spawn ? 0 : EditorToken( args[1], s_moveTokens, ARRAYSIZE( s_moveTokens ) );
	if ( !spawn && !move )
	{
		Msg( "Unknown attribute '%s'", args[1] );
		Msg( "%d areas added to selection\n", 0 );
		return;
	}
	int count = 0;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		if ( ( spawn && ToTerrorNavArea( TheNavAreas[i] )->HasSpawnAttributes( spawn ) ) || ( move && TheNavAreas[i]->HasAttributes( move ) ) )
		{ TheNavMesh->AddToSelectedSet( TheNavAreas[i] ); ++count; }
	}
	Msg( "%d areas added to selection\n", count );
}
void TerrorNavMesh::CommandNavTravelDistance()
{
	CNavArea *from = GetMarkedArea();
	if ( !from ) { Warning( "ERROR: First mark the 'from' area.\n" ); return; }
	// S2F0960 uses Terror mesh1612, not cursor-selected base1144.
	CNavArea *to = m_debugSelectedArea;
	if ( !to ) { Warning( "ERROR: First point at the 'to' area.\n" ); return; }
	// S2F50D0 retains1612 when z_debug is disabled; a deleted retained target is undefined.
	if ( TheNavAreas.Find( to ) == TheNavAreas.InvalidIndex() ) { CommandNavUnmark(); return; }
	float distance = GetTerrorNavTravelDistance( from, to );
	static char message[128];
	CCSPlayer *host = dynamic_cast< CCSPlayer * >( UTIL_GetListenServerHost() );
	if ( host )
	{
		Q_snprintf( message, sizeof( message ), "Travel distance is %3.2f\n", double( distance ) );
		host->HintMessage( message, true, true, false );
	}
	Msg( "Travel distance is %3.2f\n", double( distance ) );
	CommandNavUnmark();
}
void CC_travel_distance() { TheTerrorNavMesh()->CommandNavTravelDistance(); }

int Complete_RVA002E2880( const char *partial, char commands[64][64] )
{
	int count = CompleteTokenTable( partial, commands, s_spawnTokens, ARRAYSIZE( s_spawnTokens ), 0 );
	return CompleteTokenTable( partial, commands, s_moveTokens, ARRAYSIZE( s_moveTokens ), count );
}
int Complete_RVA002E84C0( const char *partial, char commands[64][64] )
{
	// S2E67A0 skips the literal prefix, retaining directory order, no sorting.
	const int prefixLength = sizeof( "nav_fog_set " ) - 1;
	// A shorter raw input would produce an out-of-bounds string read natively.
	if ( Q_strlen( partial ) < prefixLength ) return 0;
	const char *suffix = partial + prefixLength;
	int length = Q_strlen( suffix ), count = 0;
	TerrorNavMesh *mesh = TheTerrorNavMesh();
	for ( int i = 0; i < mesh->GetPopulationCount() && count < 64; ++i )
		if ( !Q_strnicmp( mesh->GetPopulationName( i ), suffix, length ) )
			Q_snprintf( commands[count++], 64, "nav_fog_set %s", mesh->GetPopulationName( i ) );
	return count;
}
int Complete_RVA002F5730( const char *partial, char commands[64][64] )
{
	return CompleteTokenTable( partial, commands, s_troubleTokens, ARRAYSIZE( s_troubleTokens ), 0 );
}

void TerrorNavMesh::UpdateDebugSelectedArea()
{
	if ( z_debug.GetInt() )
		m_debugSelectedArea = FindActiveNavArea() ? GetSelectedArea() : NULL;
	// Deliberately retain the previous target when z_debug is turned off.
}
#pragma float_control( pop )
