//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/client_path.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_path.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( client_path, CClientPath );

IMPLEMENT_SERVERCLASS_ST_NOBASE( CClientPath, DT_ClientPath )
	SendPropArray3( SENDINFO_ARRAY3( m_posX ), SendPropInt( SENDINFO_ARRAY( m_posX ), 15 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_posY ), SendPropInt( SENDINFO_ARRAY( m_posY ), 15 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_posZ ), SendPropInt( SENDINFO_ARRAY( m_posZ ), 15 ) ),
	SendPropInt( SENDINFO( m_posCount ), 10, SPROP_UNSIGNED ),
END_SEND_TABLE()

BEGIN_DATADESC( CClientPath )
	DEFINE_THINKFUNC( VisibilityThink ),
	DEFINE_INPUTFUNC( FIELD_EHANDLE, "Player", InputPlayer ),
END_DATADESC()


CClientPath::CClientPath() : m_pStartArea( NULL ), m_pGoalArea( NULL ), m_bValid( false )
{}

int CClientPath::UpdateTransmitState()
{ return SetTransmitState( FL_EDICT_FULLCHECK ); }

void CClientPath::Spawn()
{
	SetThink( &CClientPath::VisibilityThink );
	SetNextThink( gpGlobals->curtime );
}
void CClientPath::InputPlayer( inputdata_t &inputdata )
{
	m_hPlayer = inputdata.value.FieldType() == FIELD_EHANDLE ? inputdata.value.Entity() : NULL;
}
int CClientPath::ShouldTransmit( const CCheckTransmitInfo *info )
{
	CBaseEntity *entity = m_hPlayer.Get();
	if ( !entity || !m_bValid ) return FL_EDICT_DONTSEND;
	if ( info->m_pClientEnt == entity->edict() ) return FL_EDICT_ALWAYS;
	// Existing constructor-owned single-slot split-screen state; no new slot.
	CBasePlayer *player = entity->IsPlayer() ? dynamic_cast< CBasePlayer * >( m_hPlayer.Get() ) : NULL;
	CBasePlayer *receiver = ToBasePlayer( CBaseEntity::Instance( info->m_pClientEnt ) );
	return player && player->IsSplitScreenPlayer() && player->GetSplitScreenPlayerOwner() == receiver ?
		FL_EDICT_ALWAYS : FL_EDICT_DONTSEND;
}
void CClientPath::AddPoint( const Vector &point )
{
	if ( m_posCount >= 256 ) { Warning( "path is too long to transmit" ); return; }
	if ( m_posCount > 0 )
	{
		const int last = m_posCount - 1;
		const float dx = point.x - static_cast< float >( m_posX[last] );
		const float dy = point.y - static_cast< float >( m_posY[last] );
		const float dz = point.z - static_cast< float >( m_posZ[last] );
		const float distance = sqrtf( ( dy * dy + dz * dz ) + dx * dx );
		if ( 10.0f > distance ) return; // Native JA: unordered distance is NOT skipped.
	}
	const int index = m_posCount;
	// CVTTSS2SI has defined INT_MIN results for NaN/out-of-range operands.
	m_posX.Set( index, _mm_cvtt_ss2si( _mm_set_ss( point.x ) ) );
	m_posY.Set( index, _mm_cvtt_ss2si( _mm_set_ss( point.y ) ) );
	m_posZ.Set( index, _mm_cvtt_ss2si( _mm_set_ss( point.z ) ) );
	m_posCount = index + 1;
}
void CClientPath::AddArea( CNavArea *area )
{
	if ( m_posCount >= 256 ) { Warning( "path is too long to transmit" ); return; }
	Vector point = area->GetCenter();
	if ( m_pLastArea ) m_pLastArea->GetClosestPointOnArea( area->GetCenter(), &point );
	AddPoint( point );
	m_pLastArea = area;
}
void CClientPath::VisibilityThink()
{
	CBaseEntity *entity = m_hPlayer.Get();
	CTerrorPlayer *player = entity && entity->IsPlayer() ? dynamic_cast< CTerrorPlayer * >( entity ) : NULL;
	if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_INFECTED )
	{
		m_bValid = false;
		SetNextThink( gpGlobals->curtime + 1.0f );
		return;
	}
	CTerrorPlayer *nearest = NULL;
	float nearestDistance = 100000000.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( i );
		CTerrorPlayer *candidate = base && base->IsPlayer() ? dynamic_cast< CTerrorPlayer * >( base ) : NULL;
		if ( !candidate || !candidate->edict() || !candidate->entindex() || candidate->State_Get() == STATE_PICKINGTEAM ||
			!candidate->IsAlive() || candidate->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		// This instance permits incapacitated/pinned survivors and checkpoints.
		const Vector delta = candidate->WorldSpaceCenter() - GetAbsOrigin();
		const float distance = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
		if ( nearestDistance > distance ) { nearestDistance = distance; nearest = candidate; }
	}
	if ( !nearest )
	{
		// Native deliberately leaves valid/count/area cache unchanged here.
		SetNextThink( gpGlobals->curtime + 1.0f );
		return;
	}
	SetNextThink( gpGlobals->curtime + 0.25f );
	CNavArea *start = player->GetLastKnownArea();
	CNavArea *goal = nearest->GetLastKnownArea();
	if ( start == m_pStartArea && goal == m_pGoalArea ) return;
	m_pStartArea = start;
	m_pGoalArea = goal;
	const Vector startPosition = player->GetAbsOrigin();
	const Vector goalPosition = nearest->GetAbsOrigin();
	m_bValid = BuildTerrorShortestPath( start, goal, NULL, 0.0f, TEAM_INFECTED, false, &startPosition, &goalPosition );
	if ( !m_bValid ) return; // Retain old positions on failure; do not retry unchanged areas.
	m_pLastArea = NULL;
	m_posCount = 0;
	CUtlVector< CNavArea * > visited;
	for ( CNavArea *area = goal; area; area = area->GetParent() )
	{
		if ( visited.Find( area ) != visited.InvalidIndex() ) break; // Approved cycle-stop rule.
		visited.AddToTail( area );
		AddArea( area );
	}
	if ( m_posCount >= 256 ) Warning( "path is too long to transmit" );
	else if ( m_pLastArea ) AddPoint( m_pLastArea->GetCenter() );
}