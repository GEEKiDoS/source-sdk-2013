//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terrorplayer.h"
#include <math.h>
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "props.h"
#include "ndebugoverlay.h"
#include "util_shared.h"
#endif
#include "tier0/memdbgon.h"

static ConVar sv_tankpropfade( "sv_tankpropfade", "1", 0x00004004 );
static ConVar fade_immediately( "fade_immediately", "0", 0x00004004 );

namespace
{
// S:25DDD0/4EDA80. The projection is on an infinite LINE, not a segment.
// Keep native dot-product grouping and the unordered zero-divisor branch.
bool IsSphereOutsidePlayerView( CTerrorPlayer *player, const Vector &center, float radiusSquared )
{
	if ( player->IsBot() ) return true;
	Vector eye = player->EyePosition();
	Vector forward;
	player->EyeVectors( &forward, NULL, NULL );
	Vector end = eye + forward;
	Vector direction = end - eye;
	float divisor = ( direction.x * direction.x + direction.y * direction.y ) + direction.z * direction.z;
	float t = 0.0f;
	if ( divisor >= 0.00001f )
	{
		float toCenter = ( center.y * direction.y + center.x * direction.x ) + center.z * direction.z;
		float toEye = ( eye.y * direction.y + eye.x * direction.x ) + eye.z * direction.z;
		t = ( toCenter - toEye ) / divisor;
	}
	Vector closest = eye + direction * t;
	Vector offset = closest - eye;
	float projectedDistanceSquared = ( offset.y * offset.y + offset.x * offset.x ) + offset.z * offset.z;
	if ( radiusSquared > projectedDistanceSquared ) return false;
	double cosine = cos( ( (double)player->GetFOV() * 0.5 * 3.141592653589793 ) * 0.005555555555555556 );
	float projection = ( offset.x * forward.x + forward.y * offset.y ) + forward.z * offset.z;
	float cosineFloat = (float)cosine;
	float cone = ( ( ( offset.x * offset.x + offset.y * offset.y ) + offset.z * offset.z ) * cosineFloat ) * cosineFloat;
	if ( cosine > 0.0 )
	{
		if ( projection < 0.0f ) return true;
		return projection * projection < cone;
	}
	if ( projection > 0.0f ) return false;
	return cone < projection * projection;
}

// S:25F140/25F1C0 use the real ForEachPlayer admission, all teams, and no
// alive filter. Bots never prevent either of these human-view decisions.
class OutsidePropView
{
public:
	OutsidePropView( const Vector &center, float radiusSquared ) : m_center( center ), m_radiusSquared( radiusSquared ) {}
	bool operator()( CBasePlayer *base ) const
	{
		return IsSphereOutsidePlayerView( static_cast< CTerrorPlayer * >( base ), m_center, m_radiusSquared );
	}
private:
	const Vector &m_center;
	float m_radiusSquared;
};

class OutsidePropRange
{
public:
	OutsidePropRange( const Vector &center, float radiusSquared ) : m_center( center ), m_radiusSquared( radiusSquared ) {}
	bool operator()( CBasePlayer *base ) const
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		if ( player->IsBot() ) return true;
		Vector delta = m_center - player->WorldSpaceCenter();
		return !( ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z < m_radiusSquared );
	}
private:
	const Vector &m_center;
	float m_radiusSquared;
};
}

// S:26E1E0. Registration is append-only: duplicate and null handles are native.
void CDirector::RegisterTankProp( CBaseEntity *prop )
{
	if ( sv_tankpropfade.GetBool() ) m_TankProps.AddToTail( prop );
}

// S:26E230. Compare resolved pointers, including null. Updating an existing
// entry replaces BOTH deadlines; it does not retain a prior earlier deadline.
void CDirector::QueueTankPropRemoval( CBaseEntity *prop, float delay, float forcedDelay )
{
	if ( !sv_tankpropfade.GetBool() ) return;
	int index = 0;
	for ( ; index < m_TankPropRemovalQueue.Count(); ++index )
		if ( m_TankPropRemovalQueue[index].m_hProp.Get() == prop ) break;
	if ( index == m_TankPropRemovalQueue.Count() ) index = m_TankPropRemovalQueue.AddToTail();
	TankPropRemoval &entry = m_TankPropRemovalQueue[index];
	entry.m_hProp = prop;
	entry.m_flEarliestRemovalTime = gpGlobals->curtime + delay;
	entry.m_flForcedRemovalTime = gpGlobals->curtime + forcedDelay;
}

// S:270670. Already queued props do not consume RNG and are not refreshed.
void CDirector::OnLastTankKilled()
{
	int count = m_TankProps.Count();
	for ( int i = 0; i < count; ++i )
	{
		CBaseEntity *prop = m_TankProps[i].Get();
		bool queued = false;
		FOR_EACH_VEC( m_TankPropRemovalQueue, j )
		{
			if ( m_TankPropRemovalQueue[j].m_hProp.Get() == prop )
			{
				queued = true;
				break;
			}
		}
		if ( !queued ) QueueTankPropRemoval( prop, RandomFloat( 10.0f, 15.0f ), 40.0f );
	}
	m_TankProps.RemoveAll();
}

// S:265E00, reached after panic update and BEFORE the native infected scan.
void CDirector::UpdateSpawnRemovalQueue()
{
	bool immediately = fade_immediately.GetBool();
	for ( int i = m_TankPropRemovalQueue.Count() - 1; i >= 0; --i )
	{
		TankPropRemoval &entry = m_TankPropRemovalQueue[i];
		CBaseEntity *prop = entry.m_hProp.Get();
		if ( !prop )
		{
			m_TankPropRemovalQueue.FastRemove( i );
			continue;
		}
		if ( immediately || gpGlobals->curtime > entry.m_flForcedRemovalTime )
		{
			prop->SUB_StartFadeOut( 0.0f, true );
			m_TankPropRemovalQueue.FastRemove( i );
			continue;
		}
		if ( entry.m_flEarliestRemovalTime <= gpGlobals->curtime )
		{
			float radius = prop->BoundingRadius();
			Vector center = prop->WorldSpaceCenter();
			OutsidePropView view( center, radius * radius );
			if ( ForEachPlayer( view ) )
			{
				UTIL_Remove( prop );
				m_TankPropRemovalQueue.FastRemove( i );
				continue;
			}
			radius = prop->BoundingRadius() + 384.0f;
			center = prop->WorldSpaceCenter();
			OutsidePropRange range( center, radius * radius );
			if ( ForEachPlayer( range ) )
			{
				prop->SUB_StartFadeOut( 0.0f, true );
				m_TankPropRemovalQueue.FastRemove( i );
			}
		}
	}
	fade_immediately.SetValue( 0 );
}

#include "l4d2/director/director_tank_props_console_server_callbacks.inc"
