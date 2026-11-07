#include "cbase.h"
#include "recipientfilter.h"
#include "precache_register.h"
#include "tier0/memdbgon.h"

// S207680. There is intentionally NO Tilt message registration or client
// handler in the pinned game. Main decision preserves its native Error path.
static void SendNativeTilt( CBasePlayer *player, const QAngle &angles, float duration, float tiltTime, int command, bool ease )
{
	CSingleUserRecipientFilter filter( player );
	filter.MakeReliable();
	UserMessageBegin( filter, "Tilt" );
	WRITE_BYTE( command );
	WRITE_BYTE( ease );
	WRITE_FLOAT( angles.x );
	WRITE_FLOAT( angles.y );
	WRITE_FLOAT( angles.z );
	WRITE_FLOAT( duration );
	WRITE_FLOAT( tiltTime );
	MessageEnd();
}
class CEnvTilt : public CBaseEntity
{
public:
	DECLARE_CLASS( CEnvTilt, CBaseEntity );
	DECLARE_DATADESC();
	void Precache() OVERRIDE { PrecacheEffect( "ShakeRopes" ); }
	void Spawn() OVERRIDE
	{
		Precache();
		SetSolid( SOLID_NONE );
		SetMoveType( MOVETYPE_NONE );
		if ( HasSpawnFlags( 1 ) ) m_Radius = 0.0f;
		if ( HasSpawnFlags( 32 ) && !HasSpawnFlags( 8 ) && !HasSpawnFlags( 16 ) )
			DevWarning( "env_shake %s with \"Don't shake view\" spawnflag set without \"Shake physics\" or \"Shake ropes\" spawnflags set.", GetDebugName() );
	}
	void InputStartTilt( inputdata_t & ) { ApplyTilt( HasSpawnFlags( 64 ) ? 5 : HasSpawnFlags( 32 ) ? 4 : 0 ); }
	void InputStopTilt( inputdata_t & ) { ApplyTilt( 1 ); }
	void ApplyTilt( int command );
	int DrawDebugTextOverlays() OVERRIDE
	{
		int offset = BaseClass::DrawDebugTextOverlays();
		if ( m_debugOverlays & OVERLAY_TEXT_BIT )
		{
			EntityText( offset++, CFmtStr( "    duration: %f", m_Duration ), 0 );
			EntityText( offset++, CFmtStr( "    radius: %f", m_Radius ), 0 );
		}
		return offset;
	}
private:
	float m_Duration, m_Radius, m_TiltTime, m_stopTime;
};
LINK_ENTITY_TO_CLASS( env_tilt, CEnvTilt );
BEGIN_DATADESC( CEnvTilt )
	DEFINE_KEYFIELD( m_Duration, FIELD_FLOAT, "duration" ),
	DEFINE_KEYFIELD( m_Radius, FIELD_FLOAT, "radius" ),
	DEFINE_KEYFIELD( m_TiltTime, FIELD_TIME, "tilttime" ),
	DEFINE_FIELD( m_stopTime, FIELD_TIME ),
	DEFINE_INPUTFUNC( FIELD_VOID, "StartTilt", InputStartTilt ),
	DEFINE_INPUTFUNC( FIELD_VOID, "StopTilt", InputStopTilt ),
END_DATADESC()
#pragma float_control( precise, on, push )
#if defined( _MSC_VER ) && _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
void CEnvTilt::ApplyTilt( int command )
{
	if ( HasSpawnFlags( 32 ) && HasSpawnFlags( 64 ) ) return;
	const QAngle angles( GetAbsAngles().x, 0.0f, GetAbsAngles().z );
	const float radius = HasSpawnFlags( 1 ) ? 0.0f : m_Radius;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player ) continue;
		if ( radius != 0.0f )
		{
			const Vector delta = player->WorldSpaceCenter() - GetAbsOrigin();
			const float xx = delta.x * delta.x;
			const float yy = delta.y * delta.y;
			const float zz = delta.z * delta.z;
			const float xy = yy + xx;
			const float distance = sqrtf( xy + zz );
			// Native JA skips strictly greater, not unordered, distances.
			if ( distance > radius ) continue;
		}
		SendNativeTilt( player, angles, m_Duration, m_TiltTime, command, HasSpawnFlags( 128 ) );
	}
}
#pragma float_control( pop )
