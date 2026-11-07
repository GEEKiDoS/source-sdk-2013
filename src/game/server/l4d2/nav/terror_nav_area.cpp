//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/terror_shareddefs.h"
#include "team.h"
#include "tier1/utlbuffer.h"
#include "tier1/KeyValues.h"

extern ConVar nav_blockers_can_affect_flow;
ConVar nav_obscure_range( "nav_obscure_range", "400", 0x00004004 );

#include "tier0/memdbgon.h"

TerrorNavArea::TerrorNavArea()
{
	m_usesTerrorAttributes = true;
	m_spawnAttributes = 0;
	m_population = 0;
	m_flowDistance[FLOW_TOWARD_GOAL] = 0.0f; // Native2E9B63/6B; computation, not Load, writes -9999.
	m_flowDistance[FLOW_FROM_START] = 0.0f;
	m_saveOrdinal = 0;
	m_placeDirectoryIndex = 0;
	m_loadedPlace = UNDEFINED_PLACE;
	m_visibilityUsesOrdinals = false;
	m_lowestCorner = m_highestCorner = NORTH_WEST;
	m_aasCollectionMarker = 0;
	m_wanderingPopulation = 0;
	m_playerAASMask = 0;
	m_potentiallyVisibleCharacters = 0;
	m_completelyVisibleCharacters = 0;
	m_clearedCharacters = 0;
	m_hDoor = NULL;
	m_hScriptInstance = NULL; // Native2E9B56, constructor-only handle initialization.
}


void TerrorNavArea::OnLoadPlace( unsigned short index )
{
	m_placeDirectoryIndex = index;
	m_loadedPlace = GetPlace();
	if ( index > placeDirectory.GetPlaces()->Count() )
		TheTerrorNavMesh()->SetLoadError( NAV_CORRUPT_DATA );
}

unsigned short TerrorNavArea::GetPlaceDirectoryIndex( const PlaceDirectory &directory ) const
{
	return GetPlace() == m_loadedPlace ? m_placeDirectoryIndex : directory.GetIndex( GetPlace() );
}

// Retail base serialization ends at the four light values. The L4D2 seam in
// CNavArea::Save leaves visibility to this writer (S:0x2F9D40).
void TerrorNavArea::Save( CUtlBuffer &buffer, unsigned int version ) const
{
	BaseClass::Save( buffer, version );
	buffer.PutUnsignedInt( m_spawnAttributes & PERSISTENT_SPAWN_ATTRIBUTES );
	buffer.PutUnsignedShort( static_cast<unsigned short>( m_population ) );
	buffer.PutUnsignedInt( m_potentiallyVisibleAreas.Count() );
	for ( int i = 0; i < m_potentiallyVisibleAreas.Count(); ++i )
	{
		const TerrorNavArea *area = ToTerrorNavArea( m_potentiallyVisibleAreas[i].area );
		buffer.PutUnsignedInt( area ? area->m_saveOrdinal : ~0U );
		buffer.PutUnsignedChar( m_potentiallyVisibleAreas[i].attributes );
	}
	buffer.PutUnsignedInt( m_inheritVisibilityFrom.area ? m_inheritVisibilityFrom.area->GetID() : 0 );
}

NavErrorType TerrorNavArea::Load( CUtlBuffer &buffer, unsigned int version, unsigned int subVersion )
{
	m_usesTerrorAttributes = subVersion != 0;
	NavErrorType result = BaseClass::Load( buffer, version, subVersion );
	if ( result != NAV_OK || !buffer.IsValid() )
		return NAV_CORRUPT_DATA;
	if ( subVersion == 0 )
	{
		UpdateFlowFlatGeometry();
		return NAV_OK;
	}
	if ( !TerrorNavMesh::IsSupportedVersion( version, subVersion ) )
		return NAV_BAD_FILE_VERSION;

	m_spawnAttributes = buffer.GetUnsignedInt();
	// Base S:0x48EB80's persistent mask, then Terror S:0x2FA110's mask.
	SetAttributes( GetAttributes() & 0x67FCFFFF );
	UpdateFlowFlatGeometry();
	unsigned short population = buffer.GetUnsignedShort();
	if ( population > TheTerrorNavMesh()->GetPopulationCount() )
		return NAV_CORRUPT_DATA;
	m_population = population;

	unsigned int count = buffer.GetUnsignedInt();
	if ( !buffer.IsValid() || buffer.GetBytesRemaining() < 4 || count > static_cast< unsigned int >( ( buffer.GetBytesRemaining() - 4 ) / 5 ) )
		return NAV_CORRUPT_DATA;
	m_potentiallyVisibleAreas.RemoveAll();
	m_visibilityUsesOrdinals = subVersion == 14;
	if ( m_visibilityUsesOrdinals )
		m_potentiallyVisibleAreas.EnsureCapacity( count );
	if ( subVersion == 13 && count != 0 )
		TheTerrorNavMesh()->m_visibilityNeedsRebuild = true;
	for ( unsigned int i = 0; i < count; ++i )
	{
		AreaBindInfo info = {};
		info.id = buffer.GetUnsignedInt();
		info.attributes = buffer.GetUnsignedChar();
		if ( m_visibilityUsesOrdinals )
			m_potentiallyVisibleAreas.AddToTail( info );
		// S:0x2FA110 consumes pre14 lists without using them. It marks
		// visibility dirty; it does not prove a PostLoad regeneration call.
	}
	m_inheritVisibilityFrom.id = buffer.GetUnsignedInt();
	return buffer.IsValid() ? NAV_OK : NAV_CORRUPT_DATA;
}

NavErrorType TerrorNavArea::PostLoad()
{
	NavErrorType error = NAV_OK;
	if ( m_visibilityUsesOrdinals )
	{
		for ( int i = 0; i < m_potentiallyVisibleAreas.Count(); ++i )
		{
			AreaBindInfo &info = m_potentiallyVisibleAreas[i];
			if ( info.id >= static_cast< unsigned int >( TheNavAreas.Count() ) )
			{
				info.id = 0;
				error = NAV_CORRUPT_DATA;
			}
			else
			{
				info.id = TheNavAreas[info.id]->GetID();
			}
		}
		m_visibilityUsesOrdinals = false;
	}
	NavErrorType result = BaseClass::PostLoad();
	if ( result != NAV_OK )
		error = result;
	if ( error != NAV_OK )
		TheTerrorNavMesh()->SetLoadError( error );
	return error;
}

bool TerrorNavArea::IsAbleToMergeWith( CNavArea *other ) const
{
	if ( ( ( m_spawnAttributes ^ ToTerrorNavArea( other )->m_spawnAttributes ) & PERSISTENT_SPAWN_ATTRIBUTES ) != 0 ||
		( ( GetAttributes() ^ other->GetAttributes() ) & NAV_PLAYERCLIP ) != 0 )
		return false;
	return BaseClass::IsAbleToMergeWith( other );
}

void TerrorNavArea::InheritAttributes( CNavArea *first, CNavArea *second )
{
	BaseClass::InheritAttributes( first, second );
	if ( !first )
		return;
	TerrorNavArea *a = ToTerrorNavArea( first );
	if ( second )
	{
		TerrorNavArea *b = ToTerrorNavArea( second );
		m_spawnAttributes = a->m_spawnAttributes | b->m_spawnAttributes;
		if ( a->m_population == b->m_population || b->m_population == 0 )
			m_population = a->m_population;
		else if ( a->m_population == 0 )
			m_population = b->m_population;
		else
			m_population = RandomInt( 0, 100 ) < 50 ? a->m_population : b->m_population;
	}
	else
	{
		m_spawnAttributes |= a->m_spawnAttributes;
		if ( m_population == 0 )
			m_population = a->m_population;
	}
}

void TerrorNavArea::SaveToSelectedSet( KeyValues *areaKey ) const
{
	BaseClass::SaveToSelectedSet( areaKey );
	areaKey->SetInt( "SpawnAttributes", m_spawnAttributes );
}

void TerrorNavArea::RestoreFromSelectedSet( KeyValues *areaKey )
{
	BaseClass::RestoreFromSelectedSet( areaKey );
	m_spawnAttributes = areaKey->GetInt( "SpawnAttributes", 0 );
}

bool TerrorNavArea::IsBlocked( int teamID, bool ignoreNavBlockers ) const
{
	if ( !m_usesTerrorAttributes )
		return BaseClass::IsBlocked( teamID, ignoreNavBlockers );

	// S:0x477F00. Do not alias the unnamed 0x08000000 producer to an SDK
	// attribute: only this recovered numeric test is established here.
	unsigned int attributes = GetAttributes();
	if ( ignoreNavBlockers && ( attributes & 0x80000000 ) != 0 &&
		( !nav_blockers_can_affect_flow.GetBool() || ( attributes & 0x08000000 ) == 0 ) )
		return false;
	if ( teamID == TEAM_SURVIVOR && ( attributes & NAV_PLAYERCLIP ) != 0 )
		return true;
	if ( teamID == TEAM_ANY )
	{
		for ( int i = 0; i < MAX_NAV_TEAMS; ++i )
			if ( m_isBlocked[i] )
				return true;
		return false;
	}
	// Native S:0x477F5D selects bit 1 for every nonzero signed remainder.
	return m_isBlocked[( teamID % MAX_NAV_TEAMS ) != 0];
}

unsigned int TerrorNavArea::ParseSpawnAttribute( const char *name )
{
	// Original command tokens, S:0x2E25B0. Runtime-only bits have no token.
	static const struct { const char *name; unsigned int attribute; } attributes[] =
	{
		{ "empty", TERROR_NAV_EMPTY }, { "stop", TERROR_NAV_STOP_SCAN },
		{ "finale", TERROR_NAV_FINALE }, { "battlefield", TERROR_NAV_BATTLEFIELD },
		{ "player_start", TERROR_NAV_PLAYER_START }, { "ignore_visibility", TERROR_NAV_IGNORE_VISIBILITY },
		{ "not_clearable", TERROR_NAV_NOT_CLEARABLE }, { "checkpoint", TERROR_NAV_CHECKPOINT },
		{ "obscured", TERROR_NAV_OBSCURED }, { "no_mobs", TERROR_NAV_NO_MOBS },
		{ "threat", TERROR_NAV_THREAT }, { "nothreat", TERROR_NAV_NOTHREAT },
		{ "lyingdown", TERROR_NAV_LYINGDOWN }, { "rescue_closet", TERROR_NAV_RESCUE_CLOSET },
		{ "rescue_vehicle", TERROR_NAV_RESCUE_VEHICLE }
	};
	for ( int i = 0; i < ARRAYSIZE( attributes ); ++i )
		if ( !Q_stricmp( name, attributes[i].name ) )
			return attributes[i].attribute;
	return 0;
}

bool TerrorNavArea::IsPotentiallyVisibleToTeam( int teamIndex ) const
{
	// S:0x2E7E30: mesh visibility is not an actor sight trace. OBSCURED is
	// a distance gate on the viewed area, with same-area acceptance first.
	CTeam *team = GetGlobalTeam( teamIndex );
	for ( int i = 0; i < team->GetNumPlayers(); ++i )
	{
		CBasePlayer *player = team->GetPlayer( i );
		if ( !player->IsAlive() )
			continue;
		CNavArea *area = player->GetLastKnownArea();
		if ( !area )
			continue;
		if ( area == this )
			return true;
		if ( HasSpawnAttributes( TERROR_NAV_OBSCURED ) &&
			!( Sqr( nav_obscure_range.GetFloat() ) > GetCenter().DistToSqr( area->GetCenter() ) ) )
			continue;
		if ( area->IsPotentiallyVisible( this ) )
			return true;
	}
	return false;
}

unsigned int TerrorNavArea::GetSerializedBaseAttributes() const
{
	unsigned int attributes = GetAttributes() & 0x67FFFFFF;
	// Native sub0's bit29 is SDK cost bookkeeping, not retail geometry.
	if ( !m_usesTerrorAttributes )
		attributes &= ~0x20000000U;
	return attributes;
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
void TerrorNavArea::UpdateFlowFlatGeometry()
{
	// S:0x474D10 starts at NE, then visits NW, SE, SW. Keep the ordered
	// high-first comparisons: unordered heights and ties retain the selector.
	m_lowestCorner = m_highestCorner = NORTH_EAST;
	float low = GetCorner( NORTH_EAST ).z, high = low;
	static const NavCornerType corners[] = { NORTH_WEST, SOUTH_EAST, SOUTH_WEST };
	for ( int i = 0; i < ARRAYSIZE( corners ); ++i )
	{
		NavCornerType corner = corners[i];
		float z = GetCorner( corner ).z;
		if ( z > high )
		{
			high = z;
			m_highestCorner = static_cast< unsigned char >( corner );
		}
		else if ( low > z )
		{
			low = z;
			m_lowestCorner = static_cast< unsigned char >( corner );
		}
	}
	if ( !m_usesTerrorAttributes )
		return;
	// S:0x47F674/0x48ED24 keeps both differences unspilled in x87, with
	// a float32 first tolerance and a float64 second tolerance. NW is omitted.
	const double se = GetCorner( SOUTH_EAST ).z;
	if ( static_cast< double >( 0.05f ) > fabs( static_cast< double >( GetCorner( NORTH_EAST ).z ) - se ) &&
		0.05 > fabs( static_cast< double >( GetCorner( SOUTH_WEST ).z ) - se ) )
		SetAttributes( GetAttributes() | 0x20000000 );
	else
		RemoveAttributes( 0x20000000 );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

void TerrorNavArea::OnEnter( CBaseCombatCharacter *who, CNavArea *areaJustLeft )
{
	// S2E4A90. Non-player actors are native producers too (entityid, not userid).
	if ( !who ) return;
	CBasePlayer *player = ToBasePlayer( who );
	if ( !who->IsAlive() || who->IsGhost() ||
		( areaJustLeft && ToTerrorNavArea( areaJustLeft )->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) ||
		!HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) return;
	CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetCheckpoint( GetCenter() );
	if ( checkpoint && !checkpoint->IsOutward() && TheDirector->GetChapterNumber() == 0 )
	{
		if ( player )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "player_entered_start_area" );
			if ( event )
			{
				event->SetInt( "userid", player->GetUserID() );
				gameeventmanager->FireEvent( event );
			}
		}
		return;
	}
	CBaseEntity *door = gEntList.FindEntityByClassnameNearest( "prop_door_rotating_checkpoint", who->GetAbsOrigin(), 300.0f );
	if ( !door ) return;
	IGameEvent *event = gameeventmanager->CreateEvent( "player_entered_checkpoint" );
	if ( !event ) return;
	if ( player ) event->SetInt( "userid", player->GetUserID() );
	else event->SetInt( "entityid", who->entindex() );
	event->SetInt( "door", door->entindex() );
	event->SetInt( "area", GetID() );
	event->SetString( "doorname", STRING( door->GetEntityName() ) );
	gameeventmanager->FireEvent( event );
}
void TerrorNavArea::OnExit( CBaseCombatCharacter *who, CNavArea *areaJustEntered )
{
	// S2E4CA0 deliberately has no alive/ghost gate and requires no nearby door.
	if ( !who ) return;
	CBasePlayer *player = ToBasePlayer( who );
	if ( !HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ||
		( areaJustEntered && ToTerrorNavArea( areaJustEntered )->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) ) return;
	IGameEvent *event = gameeventmanager->CreateEvent( "player_left_checkpoint" );
	if ( !event ) return;
	if ( player ) event->SetInt( "userid", player->GetUserID() );
	else event->SetInt( "entityid", who->entindex() );
	event->SetInt( "area", GetID() );
	gameeventmanager->FireEvent( event );
}
