//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_commands.h"
#include "l4d2/terror_shareddefs.h"
#include "world.h"
#include "tier1/utlbuffer.h"
#include <float.h>

// Population selection retains ownership of this setting (retail 0x2FA0A0).
extern ConVar z_population;
extern ConVar nav_max_view_distance;
ConVar nav_blockers_can_affect_flow( "nav_blockers_can_affect_flow", "1", 0x00004004 );
ConVar nav_flow_max_survivor_climb_height( "nav_flow_max_survivor_climb_height", "66", 0x00004004, "Maximum Survivor climb-up height used during flow computation" );
ConVar nav_flow_max_survivor_drop_height( "nav_flow_max_survivor_drop_height", "240", 0x00004004, "Maximum Survivor drop-down height used during flow computation" );

#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00851B48; constructor 0x0059A234.
ConVar nav_fog_edit( "nav_fog_edit", "0", 0x00004004, "" );

// Native object 0x00852278; constructor 0x0059A8D9.
ConVar nav_trouble_test_area( "nav_trouble_test_area", "0", 0x00004004, "If nonzero, tests will be run against this areaID only" );

// Native object 0x008522C0; constructor 0x0059A904.
ConVar nav_trouble_test_debug_duration( "nav_trouble_test_debug_duration", "10", 0x00004004, "" );

// Native object 0x00852308; constructor 0x0059A934.
ConVar nav_trouble_walkable_backtrace( "nav_trouble_walkable_backtrace", "0", 0x00004004, "" );

// Native object 0x0085DCA0; constructor 0x0059ED94.
ConVar z_nav_debug( "z_nav_debug", "0", 0x00004004, "" );

// Native object 0x00862770; constructor 0x005A1469.
ConVar nav_gui_rebuild( "nav_gui_rebuild", "0", 0x00004004, "Rebuilds the nav ui windows from scratch every time they're opened" );

#endif

#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native object 0x00851DE0; callback 0x002E5970; legacy_function.
static ConCommand wipe_attributes_command( "wipe_attributes", CC_wipe_attributes, "Clear all attributes of selected area.", 0x00004004 );

// Native object 0x00851E04; callback 0x002E59A0; command_function.
static ConCommand clear_attribute_command( "clear_attribute", CC_clear_attribute, "Remove given attribute from all areas in the selected set.", 0x00004004, Complete_RVA002E2880 );

// Native object 0x00851E28; callback 0x002E5AD0; command_function.
static ConCommand mark_command( "mark", CC_mark, "Set attribute of selected area.", 0x00004004, Complete_RVA002E2880 );

// Native object 0x00851E4C; callback 0x002E5BD0; legacy_function.
static ConCommand select_invalid_finale_nospawn_command( "select_invalid_finale_nospawn", CC_select_invalid_finale_nospawn, "Selects all FINALE_NOSPAWN areas that aren't also FINALE.", 0x00004004 );

// Native object 0x00851E70; callback 0x002E5C30; command_function.
static ConCommand select_with_attribute_command( "select_with_attribute", CC_select_with_attribute, "Selects areas with the given attribute.", 0x00004004, Complete_RVA002E2880 );

// Native object 0x00851E94; callback 0x002E5D70; legacy_function.
static ConCommand select_cliff_command( "select_cliff", CC_select_cliff, "Selects all NAV_MESH_CLIFF areas.", 0x00004004 );

// Native object 0x00851EB8; callback 0x002F0960; legacy_function.
static ConCommand travel_distance_command( "travel_distance", CC_travel_distance, "Build the shortest path from the previously marked area to the currently selected one and print the length of that path.", 0x00004004 );

// Native object 0x00851EDC; callback 0x002E29F0; command_function.
static ConCommand current_flow_distance_command( "current_flow_distance", CC_current_flow_distance, "Reports the flow distance for the local player", 0x00004004 );

// Native object 0x00851F24; callback 0x002E6560; command_function.
static ConCommand nav_fix_playerclips_command( "nav_fix_playerclips", CC_nav_fix_playerclips, "Fixup nodes marked with NAV_PLAYERCLIP.", 0x00004004 );

// Native object 0x00851F48; callback 0x002EC1C0; command_function.
static ConCommand nav_fog_set_command( "nav_fog_set", CC_nav_fog_set, "If used without arguments, all available Fog values will be listed. If a Fog argument is given, the current Fog is set.", 0x00004004, Complete_RVA002E84C0 );

// Native object 0x00851F6C; callback 0x002E6860; legacy_function.
static ConCommand nav_fog_mark_command( "nav_fog_mark", CC_nav_fog_mark, "Sets fog value for selected areas.", 0x00004004 );

// Native object 0x00851F90; callback 0x002E2B40; legacy_function.
static ConCommand nav_fog_pick_command( "nav_fog_pick", CC_nav_fog_pick, "Sets fog value for selected areas.", 0x00004004 );

// Native object 0x00851FB4; callback 0x002E6A30; legacy_function.
static ConCommand nav_test_visibility_command( "nav_test_visibility", CC_nav_test_visibility, "Tests visibility from the selected area to the one under the cursor.", 0x00004004 );

// Native object 0x00851FD8; callback 0x002EC480; legacy_function.
static ConCommand nav_select_completely_visible_command( "nav_select_completely_visible", CC_nav_select_completely_visible, "Selects all areas completely visible to the selected area.", 0x00004004 );

// Native object 0x00851FFC; callback 0x002EC530; legacy_function.
static ConCommand nav_select_partially_visible_command( "nav_select_partially_visible", CC_nav_select_partially_visible, "Selects all areas at least partially visible to the selected area.", 0x00004004 );

// Native object 0x00852020; callback 0x002EC5E0; command_function.
static ConCommand nav_select_view_distance_command( "nav_select_view_distance", CC_nav_select_view_distance, "Selects all areas within nav_max_view_distance.", 0x00004004 );

// Native object 0x00852044; callback 0x002E6AB0; legacy_function.
static ConCommand nav_select_threat_command( "nav_select_threat", CC_nav_select_threat, "Selects all threat areas", 0x00004004 );

// Native object 0x00852098; callback 0x002E6B20; command_function.
static ConCommand nav_select_visible_set_size_command( "nav_select_visible_set_size", CC_nav_select_visible_set_size, "Selects all areas with visible sets this size or greater.", 0x00004004 );

// Native object 0x008520BC; callback 0x002E6C70; command_function.
static ConCommand nav_print_visible_set_counts_command( "nav_print_visible_set_counts", CC_nav_print_visible_set_counts, "Outputs the sizes of the visible sets", 0x00004004 );

// Native object 0x008520E0; callback 0x002E88C0; command_function.
static ConCommand nav_print_visibility_info_command( "nav_print_visibility_info", CC_nav_print_visibility_info, "Dumps some visibility-related data to the console.", 0x00004004 );

// Native object 0x00852254; callback 0x002F90B0; command_function.
static ConCommand nav_trouble_report_command( "nav_trouble_report", CC_nav_trouble_report, "Selects areas that may be breaking the map checklist for inspection", 0x00004004, Complete_RVA002F5730 );

// Native object 0x00852350; callback 0x002F9390; command_function.
static ConCommand nav_trouble_report_invalid_command( "nav_trouble_report_invalid", CC_nav_trouble_report_invalid, "Selects areas that can break game mechanics", 0x00004004 );

// Native object 0x00852374; callback 0x002F8770; command_function.
static ConCommand nav_trouble_report_corner_to_corner_command( "nav_trouble_report_corner_to_corner", CC_nav_trouble_report_corner_to_corner, "Selects areas that can break game mechanics", 0x00004004 );

// Native object 0x00852398; callback 0x002F8870; command_function.
static ConCommand nav_trouble_report_suggestions_command( "nav_trouble_report_suggestions", CC_nav_trouble_report_suggestions, "Selects areas that do not have a population set, only if any area has a population", 0x00004004 );

// Native object 0x0086266C; callback 0x00387130; command_function.
static ConCommand nav_fog_select_place_command( "nav_fog_select_place", CC_nav_fog_select_place, "Selects all areas with the given fog place.", 0x00004004 );

// Native object 0x00862690; callback 0x00387D40; command_function.
static ConCommand nav_fog_print_used_command( "nav_fog_print_used", CC_nav_fog_print_used, "Prints out the list of fog names used in the map.", 0x00004004 );

// Native object 0x008626B4; callback 0x003871B0; command_function.
static ConCommand nav_fog_replace_command( "nav_fog_replace", CC_nav_fog_replace, "Replaces the first fog name with the second in the map.", 0x00004004 );

// Native object 0x0086274C; callback 0x003895B0; legacy_function.
static ConCommand nav_gui_command( "nav_gui", CC_nav_gui, "Opens the nav editing GUI", 0x00004004 );

// Native object 0x00875CC8; callback 0x0049FBE0; legacy_function.
static ConCommand nav_auto_build_area_command( "nav_auto_build_area", CC_nav_auto_build_area, "Build out high-precision areas around the edit cursor and create them.", 0x00004004 );

#endif

#include "tier0/memdbgon.h"

namespace
{
// NAV strings are bytes, not aligned native structures. A malformed string
// must not move the cursor into the following directory or area collection.
bool ReadNavString( CUtlBuffer &buffer, string_t *value, bool hasLength )
{
	if ( !buffer.IsValid() || buffer.GetBytesRemaining() <= 0 )
		return false;
	int length;
	if ( hasLength )
	{
		length = buffer.GetUnsignedShort();
		if ( !buffer.IsValid() || length == 0 || length > buffer.GetBytesRemaining() )
			return false;
	}
	else
	{
		const char *begin = static_cast< const char * >( buffer.PeekGet() );
		const char *end = static_cast< const char * >( memchr( begin, 0, buffer.GetBytesRemaining() ) );
		if ( !end )
			return false;
		length = int( end - begin ) + 1;
	}

	const char *text = static_cast< const char * >( buffer.PeekGet() );
	if ( text[length - 1] != 0 || memchr( text, 0, length - 1 ) != NULL )
		return false;
	*value = AllocPooledString( text );
	buffer.SeekGet( CUtlBuffer::SEEK_CURRENT, length );
	return buffer.IsValid();
}

void WriteNavString( CUtlBuffer &buffer, const char *text )
{
	unsigned short length = static_cast< unsigned short >( Q_strlen( text ) + 1 );
	buffer.PutUnsignedShort( length );
	buffer.Put( text, length );
}
}

TerrorNavMesh::TerrorNavMesh()
{
	m_loadedSubVersion = 0;
	m_loadError = NAV_OK;
	m_visibilityNeedsRebuild = false;
	m_hasUnnamedPlaces = false;
	m_maxFlowDistance = 0.0f;
	m_maxNonCheckpointFlowDistance = 0.0f;
	m_startArea = m_rescueVehicleArea = m_goalArea = NULL;
	m_hasFlowDistances = false;
	m_earliestBattleStationFlow = FLT_MAX;
	m_boundarySpreadIndex = 0;
	m_debugSelectedArea = NULL;
	m_selectedFogPopulation = 0;
}

CNavArea *TerrorNavMesh::CreateArea() const
{
	return new TerrorNavArea;
}

void TerrorNavMesh::PreLoadAreas( int nAreas )
{
	// S:0x2E3640 resets the legacy latch only for a real area load, not
	// GetPlacesFromNavFile's precache-only read of the same header/directories.
	m_visibilityNeedsRebuild = false;
	TheNavAreas.EnsureCapacity( nAreas );
}

void TerrorNavMesh::Reset()
{
	ClearCheckpoints();
	BaseClass::Reset();
	m_loadedSubVersion = 0;
	m_loadError = NAV_OK;
	m_visibilityNeedsRebuild = false;
	m_hasUnnamedPlaces = false;
	m_maxFlowDistance = 0.0f;
	m_maxNonCheckpointFlowDistance = 0.0f;
	m_placeDirectoryNames.RemoveAll();
	m_loadedPlaces.RemoveAll();
	m_populationDirectory.RemoveAll();
}


bool TerrorNavMesh::IsSupportedVersion( unsigned int version, unsigned int subVersion )
{
	return ( version == 16 && ( subVersion == 0 || subVersion == 14 ) ) ||
		( version == 15 && subVersion == 13 );
}

bool TerrorNavMesh::BeginLoad( unsigned int version, unsigned int subVersion )
{
	m_loadedSubVersion = subVersion;
	m_loadError = IsSupportedVersion( version, subVersion ) ? NAV_OK : NAV_BAD_FILE_VERSION;
	return m_loadError == NAV_OK;
}

void TerrorNavMesh::LoadPlaceDirectory( PlaceDirectory &directory, CUtlBuffer &buffer, int version )
{
	directory.Reset();
	m_placeDirectoryNames.RemoveAll();
	m_loadedPlaces.RemoveAll();
	unsigned short count = buffer.GetUnsignedShort();
	if ( !buffer.IsValid() || count > buffer.GetBytesRemaining() / 3 )
	{
		m_loadError = NAV_CORRUPT_DATA;
		return;
	}
	m_placeDirectoryNames.EnsureCapacity( count );
	m_loadedPlaces.EnsureCapacity( count );
	directory.m_directory.EnsureCapacity( count );

	// Grow the native registry only for new names, once per directory. Repeated
	// precache reads of an already-known directory need no registry allocation.
	CUtlVectorFixedGrowable< string_t, 16 > unknownNames;
	for ( int i = 0; i < count; ++i )
	{
		string_t text;
		if ( !ReadNavString( buffer, &text, true ) )
		{
			m_loadError = NAV_CORRUPT_DATA;
			return;
		}
		Place place = NameToPlace( STRING( text ) );
		if ( place == UNDEFINED_PLACE && !unknownNames.HasElement( text ) )
			unknownNames.AddToTail( text );
		m_placeDirectoryNames.AddToTail( text );
		m_loadedPlaces.AddToTail( place );
	}
	if ( unknownNames.Count() )
	{
		char **names = new char *[m_placeCount + unknownNames.Count()];
		if ( m_placeCount )
			Q_memcpy( names, m_placeName, m_placeCount * sizeof( char * ) );
		delete [] m_placeName;
		m_placeName = names;
		FOR_EACH_VEC( unknownNames, i )
		{
			int length = Q_strlen( STRING( unknownNames[i] ) ) + 1;
			m_placeName[m_placeCount] = new char[length];
			Q_memcpy( m_placeName[m_placeCount], STRING( unknownNames[i] ), length );
			++m_placeCount;
		}
	}
	for ( int i = 0; i < count; ++i )
	{
		if ( m_loadedPlaces[i] == UNDEFINED_PLACE )
			m_loadedPlaces[i] = NameToPlace( STRING( m_placeDirectoryNames[i] ) );
		directory.m_directory.AddToTail( m_loadedPlaces[i] );
	}
	directory.m_hasUnnamedAreas = version > 11 && buffer.GetUnsignedChar() != 0;
	m_hasUnnamedPlaces = directory.m_hasUnnamedAreas;
	if ( !buffer.IsValid() )
		m_loadError = NAV_CORRUPT_DATA;
}

void TerrorNavMesh::PreparePlaceDirectory( PlaceDirectory &directory ) const
{
	directory.Reset();
	directory.m_directory = m_loadedPlaces;
	directory.m_hasUnnamedAreas = m_hasUnnamedPlaces;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		directory.AddPlace( TheNavAreas[i]->GetPlace() );
	}
}

void TerrorNavMesh::SavePlaceDirectory( const PlaceDirectory &directory, CUtlBuffer &buffer ) const
{
	buffer.PutUnsignedShort( static_cast< unsigned short >( directory.m_directory.Count() ) );
	FOR_EACH_VEC( directory.m_directory, i )
	{
		const char *name = i < m_placeDirectoryNames.Count() ? STRING( m_placeDirectoryNames[i] ) : PlaceToName( directory.m_directory[i] );
		WriteNavString( buffer, name );
	}
	buffer.PutUnsignedChar( directory.m_hasUnnamedAreas );
}

void TerrorNavMesh::LoadCustomDataPreArea( CUtlBuffer &buffer, unsigned int subVersion )
{
	m_populationDirectory.RemoveAll();
	if ( subVersion == 0 )
		return;
	if ( subVersion != 13 && subVersion != 14 )
	{
		m_loadError = NAV_BAD_FILE_VERSION;
		return;
	}

	string_t population;
	if ( !ReadNavString( buffer, &population, false ) )
	{
		m_loadError = NAV_CORRUPT_DATA;
		return;
	}
	z_population.SetValue( STRING( population ) );
	unsigned short count = buffer.GetUnsignedShort();
	if ( !buffer.IsValid() || count > buffer.GetBytesRemaining() / 3 )
	{
		m_loadError = NAV_CORRUPT_DATA;
		return;
	}
	m_populationDirectory.EnsureCapacity( count );
	for ( int i = 0; i < count; ++i )
	{
		if ( !ReadNavString( buffer, &population, true ) )
		{
			m_loadError = NAV_CORRUPT_DATA;
			return;
		}
		m_populationDirectory.AddToTail( population );
	}
}


const char *TerrorNavMesh::GetDefaultPopulationName() const
{
	return z_population.GetString();
}

const char *TerrorNavMesh::GetPopulationName( int index ) const
{
	return STRING( m_populationDirectory[index] );
}

bool TerrorNavMesh::PrepareSave()
{
	if ( m_loadError != NAV_OK )
		return false;
	PreparePlaceDirectory( placeDirectory );
	if ( placeDirectory.GetPlaces()->Count() > 65535 )
		return false;
	FOR_EACH_VEC( ( *placeDirectory.GetPlaces() ), i )
	{
		const char *name = i < m_placeDirectoryNames.Count() ? STRING( m_placeDirectoryNames[i] ) : PlaceToName( ( *placeDirectory.GetPlaces() )[i] );
		if ( !name || Q_strlen( name ) >= 65535 )
			return false;
	}
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		area->m_saveOrdinal = i;
	}
	if ( m_populationDirectory.Count() > 65535 )
		return false;
	FOR_EACH_VEC( m_populationDirectory, i )
	{
		if ( Q_strlen( STRING( m_populationDirectory[i] ) ) >= 65535 )
			return false;
	}
	return true;
}

bool TerrorNavMesh::Save() const
{
	if ( !const_cast< TerrorNavMesh * >( this )->PrepareSave() )
		return false;
	return BaseClass::Save();
}

void TerrorNavMesh::SaveCustomDataPreArea( CUtlBuffer &buffer ) const
{
	buffer.PutString( z_population.GetString() );
	buffer.PutUnsignedShort( static_cast< unsigned short >( m_populationDirectory.Count() ) );
	FOR_EACH_VEC( m_populationDirectory, i )
	{
		WriteNavString( buffer, STRING( m_populationDirectory[i] ) );
	}
}

void TerrorNavMesh::SaveCustomData( CUtlBuffer &buffer ) const
{
	buffer.PutFloat( nav_max_view_distance.GetFloat() );
}

void TerrorNavMesh::LoadCustomData( CUtlBuffer &buffer, unsigned int subVersion )
{
	if ( subVersion != 0 )
		nav_max_view_distance.SetValue( buffer.GetFloat() );
	if ( !buffer.IsValid() || buffer.GetBytesRemaining() != 0 )
		m_loadError = NAV_CORRUPT_DATA;
}

NavErrorType TerrorNavMesh::PostLoad( unsigned int version )
{
	if ( m_loadError != NAV_OK )
		return m_loadError;
	NavErrorType result = BaseClass::PostLoad( version );
	if ( result != NAV_OK )
		return result;
	if ( m_loadError != NAV_OK )
		return m_loadError;
	BindWorldEntities();
	return NAV_OK;
}

void TerrorNavMesh::RebuildVisibility()
{
	BeginVisibilityComputations();
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TheNavAreas[i]->ComputeVisibilityToMesh();
	}
	EndVisibilityComputations();
	// Preserve the explicit visibility-only API after the native phase split.
	BaseClass::PostCustomAnalysis();
	m_visibilityNeedsRebuild = false;
}

void TerrorNavMesh::ApplySurvivorRescuePolicy( bool allowed )
{
	if ( !allowed )
	{
		FOR_EACH_VEC( TheNavAreas, i )
		{
			ToTerrorNavArea( TheNavAreas[i] )->RemoveSpawnAttributes( TERROR_NAV_RESCUE_CLOSET );
		}
	}
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
namespace
{

bool IsReverseFlowConnectionUsable( const TerrorNavArea *current, const TerrorNavArea *candidate, float currentLow, float currentHigh, float climb, float drop )
{
	float candidateLow = candidate->GetFlowMinZ();
	float candidateHigh = candidate->GetFlowMaxZ();
	if ( currentHigh - candidateLow > climb || candidateHigh - currentLow > drop )
	{
		if ( currentLow - candidateHigh > climb || candidateLow - currentHigh > drop )
			return false;
		double originalChange = candidate->ComputeSurfaceHeightChange( current );
		float storedChange = (float)originalChange;
		if ( originalChange > (double)climb ) return false;
		if ( -storedChange > drop )
			return false;
	}
	return true;
}
}

void TerrorNavMesh::RelaxFlowDistance( TerrorNavArea *from, TerrorNavArea *to, float length, TerrorFlowDirection direction )
{
	float distance = from->m_flowDistance[direction] + length;
	if ( to->m_flowDistance[direction] < 0.0f || to->m_flowDistance[direction] > distance )
	{
		to->m_flowDistance[direction] = distance;
		to->Mark();
		if ( !to->IsOpen() )
			to->AddToOpenListTail();
	}
}

bool TerrorNavMesh::ComputeFlowDistance( TerrorNavArea *startArea, TerrorNavArea *goalArea, TerrorFlowDirection direction )
{
	m_startArea = startArea; // native2EE024 precedes marker reset and every reach test
	// S:0x2EE000 uses the native FIFO open list, not a sorted Dijkstra queue.
	// Reopened nodes go to the tail; strictly equal alternatives do not update.
	CNavArea::ClearSearchLists();
	TerrorNavArea *seed = direction == FLOW_TOWARD_GOAL ? goalArea : startArea;
	TerrorNavArea *target = direction == FLOW_TOWARD_GOAL ? startArea : goalArea;
	seed->m_flowDistance[direction] = 0.0f;
	seed->AddToOpenList();
	seed->Mark();
	bool reached = false;
	float climb = nav_flow_max_survivor_climb_height.GetFloat();
	float drop = nav_flow_max_survivor_drop_height.GetFloat();

	while ( !CNavArea::IsOpenListEmpty() )
	{
		TerrorNavArea *area = ToTerrorNavArea( CNavArea::PopOpenList() );
		if ( area->HasAttributes( 0x70000 ) || area->IsBlocked( TEAM_SURVIVOR, true ) )
			continue;
		if ( area == target )
			reached = true;

		float low = 0.0f, high = 0.0f;
		if ( direction == FLOW_TOWARD_GOAL )
		{
			low = area->GetFlowMinZ();
			high = area->GetFlowMaxZ();
		}
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			// Preserve direction order, incoming first, then ordinary outgoing.
			const NavConnectVector *incoming = area->GetIncomingConnections( static_cast< NavDirType >( d ) );
			FOR_EACH_VEC( ( *incoming ), i )
			{
				const NavConnect &connection = ( *incoming )[i];
				TerrorNavArea *candidate = ToTerrorNavArea( connection.area );
				if ( candidate->HasAttributes( TerrorNavArea::NAV_PLAYERCLIP ) )
					continue;
				if ( direction == FLOW_TOWARD_GOAL &&
					!IsReverseFlowConnectionUsable( area, candidate, low, high, climb, drop ) )
					continue;
				RelaxFlowDistance( area, candidate, connection.length, direction );
			}

			const NavConnectVector *outgoing = area->GetAdjacentAreas( static_cast< NavDirType >( d ) );
			FOR_EACH_VEC( ( *outgoing ), i )
			{
				const NavConnect &connection = ( *outgoing )[i];
				TerrorNavArea *candidate = ToTerrorNavArea( connection.area );
				if ( candidate->HasAttributes( TerrorNavArea::NAV_PLAYERCLIP ) )
					continue;
				if ( direction == FLOW_TOWARD_GOAL )
				{
					if ( !candidate->IsConnected( area, NUM_DIRECTIONS ) ||
						!IsReverseFlowConnectionUsable( area, candidate, low, high, climb, drop ) )
						continue;
				}
				RelaxFlowDistance( area, candidate, connection.length, direction );
			}
		}

		// The original caps this shared ladder/elevator collection at sixteen.
		// In particular, an up ladder does NOT contribute its top-behind area.
		CNavArea *special[16];
		int count = 0;
		const NavLadderConnectVector *ladders = area->GetLadders( CNavLadder::LADDER_UP );
		FOR_EACH_VEC( ( *ladders ), i )
		{
			CNavLadder *ladder = ( *ladders )[i].ladder;
			if ( !ladder->IsUsableByTeam( TEAM_SURVIVOR ) )
				continue;
			if ( ladder->m_topForwardArea && count < ARRAYSIZE( special ) )
				special[count++] = ladder->m_topForwardArea;
			if ( ladder->m_topLeftArea && count < ARRAYSIZE( special ) )
				special[count++] = ladder->m_topLeftArea;
			if ( ladder->m_topRightArea && count < ARRAYSIZE( special ) )
				special[count++] = ladder->m_topRightArea;
		}
		ladders = area->GetLadders( CNavLadder::LADDER_DOWN );
		FOR_EACH_VEC( ( *ladders ), i )
		{
			CNavLadder *ladder = ( *ladders )[i].ladder;
			if ( ladder->IsUsableByTeam( TEAM_SURVIVOR ) && ladder->m_bottomArea && count < ARRAYSIZE( special ) )
				special[count++] = ladder->m_bottomArea;
		}
		if ( area->HasAttributes( 0x40000000 ) )
		{
			const NavConnectVector &elevatorAreas = area->GetElevatorAreas();
			FOR_EACH_VEC( elevatorAreas, i )
			{
				if ( elevatorAreas[i].area && count < ARRAYSIZE( special ) )
					special[count++] = elevatorAreas[i].area;
			}
		}
		for ( int i = 0; i < count; ++i )
		{
			const Vector &from = area->GetCenter();
			const Vector &to = special[i]->GetCenter();
			float x = to.x - from.x, y = to.y - from.y, z = to.z - from.z;
			float length = sqrtf( ( y * y + z * z ) + x * x );
			RelaxFlowDistance( area, ToTerrorNavArea( special[i] ), length, direction );
		}
	}

	if ( !reached )
	{
		FlowMessage( 1, "Flow Error: Goal not reached...\n" );
		FlowMessage( 2, direction == FLOW_FROM_START ? "Started at start..." : "Started at goal..." );
		FlowMessage( 2, "Start %d(%f %f %f), End %d(%f %f %f)\n",
			startArea->GetID(), (double)startArea->GetCenter().x, (double)startArea->GetCenter().y, (double)startArea->GetCenter().z,
			goalArea->GetID(), (double)goalArea->GetCenter().x, (double)goalArea->GetCenter().y, (double)goalArea->GetCenter().z );
		return false;
	}
	if ( direction == FLOW_TOWARD_GOAL )
	{
		float distanceAtStart = startArea->m_flowDistance[FLOW_TOWARD_GOAL];
		FOR_EACH_VEC( TheNavAreas, i )
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
			if ( area->IsMarked() )
			{
				float distance = distanceAtStart - area->m_flowDistance[FLOW_TOWARD_GOAL];
				area->m_flowDistance[FLOW_TOWARD_GOAL] = distance;
				if ( distance > m_maxFlowDistance )
					m_maxFlowDistance = distance;
				if ( distance > m_maxNonCheckpointFlowDistance && !area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
					m_maxNonCheckpointFlowDistance = distance;
			}
			else
			{
				area->m_flowDistance[FLOW_TOWARD_GOAL] = -9999.0f;
			}
		}
	}
	return true;
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#include "l4d2/nav/terror_nav_console_server_callbacks.inc"
