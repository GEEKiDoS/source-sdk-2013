// S0EB040..S0EB4C0 / C0FB8F0..C0FC0D0. Shared game-owned timescale state.
#include "cbase.h"
#include "l4d2/entities/game_timescale.h"
#ifdef CLIENT_DLL
#include "usermessages.h"
#else
#include "recipientfilter.h"
#endif
#include "tier0/memdbgon.h"

static CGameTimescale s_GameTimescale;
CGameTimescale *GameTimescale() { return &s_GameTimescale; }
CGameTimescale::CGameTimescale() : CAutoGameSystemPerFrame( "CGameTimescale" ) {}

#ifndef CLIENT_DLL
// Main-approved capability approximation. Native sets an independent engine
// server factor (IVEngineServer+512); the SDK exposes only host_timescale.
// That variable is replicated, so the client must NOT multiply it again.
static void ApplyHostTimescaleFactor( float factor )
{
	static ConVarRef hostTimescale( "host_timescale" );
	static bool capturedBase = false;
	static float base;
	Assert( hostTimescale.IsValid() );
	if ( factor == 1.0f )
	{
		if ( capturedBase )
		{
			hostTimescale.SetValue( base );
			capturedBase = false;
		}
	}
	else
	{
		if ( !capturedBase )
		{
			base = hostTimescale.GetFloat();
			capturedBase = true;
		}
		hostTimescale.SetValue( base * factor );
	}
}
#else
static void ReceiveCurrentTimescale( bf_read &message )
{
	GameTimescale()->SetCurrentTimescale( message.ReadFloat() );
}
static void ReceiveDesiredTimescale( bf_read &message )
{
	// Read order is observable; do not put four reads in a function call.
	const float desired = message.ReadFloat();
	const float acceleration = message.ReadFloat();
	const float minimum = message.ReadFloat();
	const float multiplier = message.ReadFloat();
	GameTimescale()->SetDesiredTimescale( desired, acceleration, minimum, multiplier );
}
#endif

#pragma float_control( precise, on, push )
#if defined( _MSC_VER ) && _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
void CGameTimescale::ResetDefaults()
{
	m_flDesiredTimescale = 1.0f;
	// Native reads the OLD minimum before assigning its default. On initial
	// static zero-fill this yields 0; on later lifecycle resets it yields .1.
	m_flBlendRate = m_flMinBlendRate;
	m_flCurrentTimescale = 1.0f;
	m_flAcceleration = 0.05f;
	m_flMinBlendRate = 0.1f;
	m_flBlendDeltaMultiplier = 3.0f;
	m_flLastRealTime = 0.0f;
#ifndef CLIENT_DLL
	ApplyHostTimescaleFactor( 1.0f );
#endif
}
bool CGameTimescale::Init()
{
	ResetDefaults();
#ifdef CLIENT_DLL
	usermessages->HookMessage( "CurrentTimescale", ReceiveCurrentTimescale );
	usermessages->HookMessage( "DesiredTimescale", ReceiveDesiredTimescale );
#endif
	return true;
}
void CGameTimescale::Shutdown() { ResetDefaults(); }
void CGameTimescale::LevelInitPostEntity() { ResetDefaults(); }
void CGameTimescale::LevelShutdownPostEntity() { ResetDefaults(); }

void CGameTimescale::SetCurrentTimescale( float scale )
{
	if ( m_flCurrentTimescale != scale )
	{
		m_flDesiredTimescale = scale;
		m_flCurrentTimescale = scale;
#ifndef CLIENT_DLL
		ApplyHostTimescaleFactor( scale );
		CReliableBroadcastRecipientFilter filter;
		UserMessageBegin( filter, "CurrentTimescale" );
		WRITE_FLOAT( m_flCurrentTimescale );
		MessageEnd();
#endif
	}
}
void CGameTimescale::SetDesiredTimescale( float scale, float acceleration, float minBlendRate, float blendDeltaMultiplier )
{
	if ( m_flDesiredTimescale != scale || m_flAcceleration != acceleration ||
		m_flMinBlendRate != minBlendRate || m_flBlendDeltaMultiplier != blendDeltaMultiplier )
	{
		m_flDesiredTimescale = scale;
		m_flAcceleration = acceleration;
		m_flMinBlendRate = minBlendRate;
		m_flBlendDeltaMultiplier = blendDeltaMultiplier;
#ifndef CLIENT_DLL
		CReliableBroadcastRecipientFilter filter;
		UserMessageBegin( filter, "DesiredTimescale" );
		WRITE_FLOAT( m_flDesiredTimescale );
		WRITE_FLOAT( m_flAcceleration );
		WRITE_FLOAT( m_flMinBlendRate );
		WRITE_FLOAT( m_flBlendDeltaMultiplier );
		MessageEnd();
#endif
	}
}

// S4EE0A0 / C363430: SSE float operations and ordered '>' predicates.
// In particular, unordered comparisons return target, not value-speed.
static float NativeTimescaleApproach( float target, float value, float speed )
{
	const float delta = target - value;
	if ( delta > speed ) return value + speed;
	if ( -speed > delta ) return value - speed;
	return target;
}
void CGameTimescale::UpdateTimescale()
{
	if ( engine->IsPaused() ) return;
	if ( m_flCurrentTimescale == m_flDesiredTimescale || m_flLastRealTime == 0.0f )
	{
		m_flBlendRate = m_flMinBlendRate;
		m_flLastRealTime = gpGlobals->realtime;
		return;
	}
	const float dt = gpGlobals->realtime - m_flLastRealTime;
	m_flLastRealTime = gpGlobals->realtime;
	// x87 subtraction/absolute value, spilled to f32 before SSE max/product.
	const float delta = static_cast< float >( fabs( static_cast< double >( m_flDesiredTimescale ) - m_flCurrentTimescale ) );
	const float limitedDelta = m_flMinBlendRate > delta ? m_flMinBlendRate : delta;
	const float targetRate = m_flBlendDeltaMultiplier * limitedDelta;
	if ( m_flBlendRate > targetRate ) m_flBlendRate = targetRate;
	else m_flBlendRate = NativeTimescaleApproach( targetRate, m_flBlendRate, m_flAcceleration );
	const float step = m_flBlendRate * dt;
	m_flCurrentTimescale = NativeTimescaleApproach( m_flDesiredTimescale, m_flCurrentTimescale, step );
#ifndef CLIENT_DLL
	ApplyHostTimescaleFactor( m_flCurrentTimescale );
#endif
}
#pragma float_control( pop )
#ifdef CLIENT_DLL
void CGameTimescale::Update( float ) { UpdateTimescale(); }
#else
void CGameTimescale::FrameUpdatePostEntityThink() { UpdateTimescale(); }

class CFuncTimescale : public CBaseEntity
{
public:
	DECLARE_CLASS( CFuncTimescale, CBaseEntity );
	DECLARE_DATADESC();
	CFuncTimescale() : m_flDesiredTimescale( 1.0f ), m_flAcceleration( 0.05f ), m_flMinBlendRate( 0.1f ), m_flBlendDeltaMultiplier( 3.0f ), m_bActive( false ) {}
	void InputStart( inputdata_t & )
	{
		if ( !m_bActive )
		{
			GameTimescale()->SetDesiredTimescale( m_flDesiredTimescale, m_flAcceleration, m_flMinBlendRate, m_flBlendDeltaMultiplier );
			m_bActive = true;
		}
	}
	void InputStop( inputdata_t & )
	{
		if ( m_bActive )
		{
			GameTimescale()->SetDesiredTimescale( 1.0f, m_flAcceleration, m_flMinBlendRate, m_flBlendDeltaMultiplier );
			m_bActive = false;
		}
	}
	void InputReset( inputdata_t & )
	{
		if ( m_bActive )
		{
			GameTimescale()->SetCurrentTimescale( 1.0f );
			m_bActive = false;
		}
	}
private:
	float m_flDesiredTimescale, m_flAcceleration, m_flMinBlendRate, m_flBlendDeltaMultiplier;
	bool m_bActive; // native deliberately omits this latch from its datamap
};
LINK_ENTITY_TO_CLASS( func_timescale, CFuncTimescale );
BEGIN_DATADESC( CFuncTimescale )
	DEFINE_INPUTFUNC( FIELD_VOID, "Start", InputStart ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Stop", InputStop ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Reset", InputReset ),
	DEFINE_KEYFIELD( m_flDesiredTimescale, FIELD_FLOAT, "desiredTimescale" ),
	DEFINE_KEYFIELD( m_flAcceleration, FIELD_FLOAT, "acceleration" ),
	DEFINE_KEYFIELD( m_flMinBlendRate, FIELD_FLOAT, "minBlendRate" ),
	DEFINE_KEYFIELD( m_flBlendDeltaMultiplier, FIELD_FLOAT, "blendDeltaMultiplier" ),
END_DATADESC()
#endif
