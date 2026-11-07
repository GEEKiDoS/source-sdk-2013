//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/escape_route.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

LINK_ENTITY_TO_CLASS( escape_route, CEscapeRoute );
BEGIN_DATADESC( CEscapeRoute )
END_DATADESC()

static CEscapeRoute *s_pCriticalPath;

// S:294100. Entity private data comes from the actual engine allocator; it
// zeroes the arrays. Native Clear subsequently retains those array contents.
CEscapeRoute::CEscapeRoute() : m_nPointCount( 0 ), m_flLength( 0.0f ), m_lastArea( NULL )
{
	s_pCriticalPath = this;
}

CEscapeRoute::~CEscapeRoute()
{
	if ( s_pCriticalPath == this ) s_pCriticalPath = NULL;
}

CEscapeRoute *GetCriticalPath() { return s_pCriticalPath; }
void CEscapeRoute::Spawn() {} // S:2936C0, original empty override.
void CEscapeRoute::Precache() {} // S:293D70, original empty override.
int CEscapeRoute::UpdateTransmitState() { return FL_EDICT_DONTSEND; } // S:293680.

void CEscapeRoute::Clear()
{
	m_lastArea = NULL;
	m_nPointCount = 0;
	m_flLength = 0.0f;
}

namespace
{
float EscapeRouteSegmentLength( const Vector &first, const Vector &second )
{
	float x = first.x - second.x;
	float y = first.y - second.y;
	float z = first.z - second.z;
	return sqrtf( ( y * y + z * z ) + x * x );
}
}

// S:2936F0. Each area entry is written by AddArea BEFORE this spacing test.
// A rejected short sample neither increments count nor rolls that write back.
void CEscapeRoute::AddPoint( const Vector &point, TerrorNavArea *area )
{
	if ( m_nPointCount >= MAX_ROUTE_POINTS )
	{
		Warning( "Survivor main path is too long to transmit" );
		return;
	}
	if ( m_nPointCount > 0 && 10.0f > EscapeRouteSegmentLength( point, m_points[m_nPointCount - 1] ) ) return;
	m_points[m_nPointCount] = point;
	float flow = area->GetFlowDistance( FLOW_TOWARD_GOAL );
	float reverseFlow = area->GetFlowDistance( FLOW_FROM_START );
	if ( !m_lastArea || m_lastArea == area )
	{
		m_flowDistances[m_nPointCount][0] = flow;
		m_flowDistances[m_nPointCount++][1] = reverseFlow;
		return;
	}
	float oldReverseFlow = m_lastArea->GetFlowDistance( FLOW_FROM_START );
	float oldDistance = EscapeRouteSegmentLength( point, m_lastArea->GetCenter() );
	float distance = EscapeRouteSegmentLength( point, area->GetCenter() );
	if ( distance == 0.0f && oldDistance == 0.0f )
	{
		m_flowDistances[m_nPointCount][0] = flow;
		m_flowDistances[m_nPointCount++][1] = reverseFlow;
		return;
	}
	float fraction = distance / ( distance + oldDistance );
	m_flowDistances[m_nPointCount][0] = ( 1.0f - fraction ) * flow + fraction * m_lastArea->GetFlowDistance( FLOW_TOWARD_GOAL );
	m_flowDistances[m_nPointCount++][1] = ( 1.0f - fraction ) * reverseFlow + fraction * oldReverseFlow;
}

void CEscapeRoute::AddArea( TerrorNavArea *area )
{
	if ( m_nPointCount >= MAX_ROUTE_POINTS )
	{
		Warning( "Survivor main path is too long to transmit" );
		return;
	}
	m_areas[m_nPointCount] = area;
	area->AddSpawnAttributes( TERROR_NAV_ESCAPE_ROUTE );
	Vector point = area->GetCenter();
	if ( m_lastArea )
	{
		m_lastArea->GetClosestPointOnArea( area->GetCenter(), &point );
		m_flLength = EscapeRouteSegmentLength( point, m_points[m_nPointCount - 1] ) + m_flLength;
	}
	AddPoint( point, area );
	m_lastArea = area;
}

void CEscapeRoute::Finish()
{
	if ( m_nPointCount >= MAX_ROUTE_POINTS )
	{
		Warning( "Survivor main path is too long to transmit" );
		return;
	}
	if ( m_lastArea )
	{
		m_areas[m_nPointCount] = m_lastArea;
		AddPoint( m_lastArea->GetCenter(), m_lastArea );
	}
	// Original293E90 does NOT add the final center segment to m_flLength.
}

// S:2939B0. Unlike295230, THIS original query has an explicit empty guard.
// Keep its zero result here only. It is not an empty-route fallback for295230.
double CEscapeRoute::GetPositionAtFlowDistance( float distance, bool interpolate, Vector *position, TerrorFlowDirection direction ) const
{
	if ( m_nPointCount == 0 )
	{
		position->Init();
		return 0.0;
	}
	if ( distance <= 0.0f )
	{
		*position = m_points[m_nPointCount - 1];
		return 0.0;
	}
	if ( distance >= m_flowDistances[0][direction] )
	{
		*position = m_points[0];
		return (double)m_flowDistances[0][direction];
	}
	for ( int i = m_nPointCount - 1; i > 0; --i )
	{
		if ( !( m_flowDistances[i - 1][direction] >= distance ) ) continue;
		float current = m_flowDistances[i][direction];
		if ( !interpolate )
		{
			*position = m_points[i];
			return (double)current;
		}
		float span = m_flowDistances[i - 1][direction] - current;
		if ( span == 0.0f )
		{
			*position = m_points[i];
			return (double)m_flowDistances[i - 1][direction];
		}
		float fraction = ( distance - current ) / span;
		*position = m_points[i] + ( m_points[i - 1] - m_points[i] ) * fraction;
		// Native293BB2 keeps the multiply/add in x87, unlike the vector.
		return (double)fraction * (double)span + (double)current;
	}
	*position = vec3_origin;
	return 0.0;
}

TerrorNavArea *CEscapeRoute::GetAreaAtFlowDistance( float distance, TerrorFlowDirection direction ) const
{
	if ( m_nPointCount == 0 ) return NULL;
	if ( distance <= 0.0f ) return m_areas[m_nPointCount - 1];
	if ( distance >= m_flowDistances[0][direction] ) return m_areas[0];
	for ( int i = m_nPointCount - 1; i > 0; --i )
		if ( m_flowDistances[i - 1][direction] >= distance ) return m_areas[i];
	return NULL;
}

int CEscapeRoute::GetPointIndexAtFlowDistance( float distance, TerrorFlowDirection direction ) const
{
	if ( m_nPointCount == 0 ) return -1;
	if ( distance <= 0.0f ) return m_nPointCount - 1;
	if ( distance >= m_flowDistances[0][direction] ) return 0;
	for ( int i = m_nPointCount - 1; i > 0; --i )
		if ( m_flowDistances[i][direction] >= distance ) return i;
	return -1;
}

double CEscapeRoute::GetPointFlowDistance( int index, TerrorFlowDirection direction ) const
{
	if ( index < 0 || index >= m_nPointCount ) return -1.0;
	return (double)m_flowDistances[index][direction];
}

void CEscapeRoute::OnNavAreaDestroyed( TerrorNavArea *area )
{
	for ( int i = m_nPointCount - 1; i >= 0; --i )
		if ( m_areas[i] == area ) m_areas[i] = NULL;
	// Original retains points/flow/count/length AND m_lastArea here.
}

// S:295230. Reverse goal-to-start point order and the native empty-route
// alias are independent of the guarded scalar distance queries above.
Vector CEscapeRoute::GetPositionAtFlow( float fraction ) const
{
	if ( fraction <= 0.0f )
	{
		if ( m_nPointCount ) return m_points[m_nPointCount - 1];
		// Native points[-1] is Base demo-cache angles.y/z + parent-index
		// bits, NOT current entity angles, origin, or a fixed NaN fallback.
		const ServerDemoBaseEntityState_t &state = GetDemoRecordingStateCache();
		Vector position( state.m_angAngles.y, state.m_angAngles.z, 0.0f );
		Q_memcpy( &position.z, &state.m_nFollowParentIndex, sizeof( position.z ) );
		return position;
	}
	if ( fraction >= 1.0f ) return m_points[0];
	float remaining = m_flLength * fraction;
	for ( int i = m_nPointCount - 1; i > 0; --i )
	{
		const Vector &first = m_points[i - 1];
		const Vector &second = m_points[i];
		float length = EscapeRouteSegmentLength( first, second );
		if ( length > remaining )
		{
			float along = remaining / length;
			return Vector( ( first.x - second.x ) * along + second.x,
				( first.y - second.y ) * along + second.y,
				( first.z - second.z ) * along + second.z );
		}
		remaining -= length;
	}
	return vec3_origin; // Actual native exhausted/interior-count<=1 branch.
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x293650; 0x5c368c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace escape_routePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "escape_route", 0x5c368c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "escape_route", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
