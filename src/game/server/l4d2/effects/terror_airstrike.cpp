#include "cbase.h"
#include "shake.h"
#include "tier0/memdbgon.h"

class CEnvAirStrikeIndoorEffect : public CBaseAnimating
{
public:
	DECLARE_CLASS( CEnvAirStrikeIndoorEffect, CBaseAnimating );
	DECLARE_DATADESC();
	CEnvAirStrikeIndoorEffect() : m_nUserSpecifiedHeight( -1 ) {}
	void Precache() OVERRIDE
	{
		PrecacheModel( "models/props_destruction/general_dest_plaster_set.mdl" );
		PrecacheScriptSound( "bombing.close_jetsflyby" );
		PrecacheScriptSound( "bombing.close_explos" );
	}
	void Spawn() OVERRIDE;
	void SelectFallSequence();
	void CrumbleThink();
	void InputDoEffect( inputdata_t & )
	{
		m_flNextStateTime = gpGlobals->curtime;
		m_nState = 0;
		SetThink( &CEnvAirStrikeIndoorEffect::CrumbleThink );
		SetNextThink( gpGlobals->curtime );
	}
private:
	int m_nState;
	float m_flHeight;
	float m_flNextStateTime;
	int m_nUserSpecifiedHeight;
};
LINK_ENTITY_TO_CLASS( env_airstrike_indoors, CEnvAirStrikeIndoorEffect );
BEGIN_DATADESC( CEnvAirStrikeIndoorEffect )
	DEFINE_THINKFUNC( CrumbleThink ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DoEffect", InputDoEffect ),
	DEFINE_KEYFIELD( m_nUserSpecifiedHeight, FIELD_INTEGER, "height" ),
END_DATADESC()

#pragma float_control( precise, on, push )
#if defined( _MSC_VER ) && _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
void CEnvAirStrikeIndoorEffect::SelectFallSequence()
{
	static const float heights[] = { 128.0f, 144.0f, 152.0f, 160.0f, 190.0f, 300.0f, 320.0f };
	const char variant = RandomInt( 1, 100 ) <= 50 ? 'b' : 'a';
	int selected = m_nUserSpecifiedHeight;
	if ( selected == -1 )
	{
		selected = 0;
		while ( !( fabs( static_cast< double >( m_flHeight ) - heights[selected] ) <= 6.0 ) )
		{
			if ( ++selected >= ARRAYSIZE( heights ) ) break;
		}
	}
	// Main decision: only the native undefined explicit index takes this
	// existing unsupported-height diagnostic/removal branch; no clamping.
	if ( selected < 0 || selected >= ARRAYSIZE( heights ) )
	{
		Msg( "ERROR - Air strike effect named '%s' has an unsupported height of %f\n", GetDebugName(), m_flHeight );
		UTIL_Remove( this );
		return;
	}
	char sequence[256];
	Q_snprintf( sequence, sizeof( sequence ), "fall_%d%c", static_cast< int >( heights[selected] ), variant );
	SetSequence( LookupSequence( sequence ) );
}
void CEnvAirStrikeIndoorEffect::Spawn()
{
	m_flNextStateTime = gpGlobals->curtime;
	Precache();
	AddEffects( EF_NODRAW );
	SetSolid( SOLID_NONE );
	SetModel( "models/props_destruction/general_dest_plaster_set.mdl" );
	SetPlaybackRate( 0.0f );
	m_nState = 0;
	trace_t trace;
	Vector end = GetAbsOrigin();
	end.z -= 16384.0f;
	UTIL_TraceLine( GetAbsOrigin(), end, 0x400B, this, COLLISION_GROUP_NONE, &trace );
	SetAbsOrigin( trace.endpos );
	end = GetAbsOrigin();
	end.z += 16384.0f;
	UTIL_TraceLine( GetAbsOrigin(), end, 0x400B, this, COLLISION_GROUP_NONE, &trace );
	const Vector delta = trace.endpos - trace.startpos; // Native snapshots from the second trace.
	const float xx = delta.x * delta.x;
	const float yy = delta.y * delta.y;
	const float zz = delta.z * delta.z;
	const float yz = yy + zz;
	m_flHeight = sqrtf( yz + xx );
	if ( 126.0f > m_flHeight || m_flHeight > 386.0f )
	{
		Msg( "ERROR - Air strike effect named %s has a bad height of %f\n", GetDebugName(), m_flHeight );
		UTIL_Remove( this );
		return;
	}
	SelectFallSequence();
}
void CEnvAirStrikeIndoorEffect::CrumbleThink()
{
	if ( !IsEffectActive( EF_NODRAW ) ) StudioFrameAdvance();
	if ( gpGlobals->curtime >= m_flNextStateTime )
	{
		switch ( m_nState )
		{
		case 0:
		{
			EmitSound( "bombing.close_jetsflyby" );
			UTIL_ScreenShake( GetAbsOrigin(), 8.0f, 200.0f, 1.0f, 600.0f, SHAKE_START );
			++m_nState;
			const double randomDelay = RandomFloat( 0.0f, 0.2f );
			m_flNextStateTime = static_cast< float >( randomDelay + ( static_cast< double >( gpGlobals->curtime ) + 2.0 ) );
			break;
		}
		case 1:
			UTIL_ScreenShake( GetAbsOrigin(), 25.0f, 130.0f, 1.0f, 600.0f, SHAKE_START );
			EmitSound( "bombing.close_explos" );
			++m_nState;
			SetPlaybackRate( 1.0f );
			RemoveEffects( EF_NODRAW );
			++m_nState; // native goes directly from 1 to 3; state 2 is not used
			m_flNextStateTime = gpGlobals->curtime + 2.0f;
			break;
		case 3:
			EmitSound( "BaseExplosionEffect.Sound" );
			++m_nState;
			m_flNextStateTime = gpGlobals->curtime + 10.0f;
			break;
		case 4:
			AddEffects( EF_NODRAW );
			SetCycle( 0.0f );
			return;
		}
	}
	SetNextThink( gpGlobals->curtime + 0.05f );
}
#pragma float_control( pop )

class CEnvAirStrikeOutdoorEffect : public CBaseAnimating
{
public:
	DECLARE_CLASS( CEnvAirStrikeOutdoorEffect, CBaseAnimating );
	DECLARE_DATADESC();
	CEnvAirStrikeOutdoorEffect() : m_flNextStateTime( 0.0f ), m_nState( 1 ) {}
	void Precache() OVERRIDE { PrecacheModel( STRING( GetModelName() ) ); PrecacheScriptSound( "bombing.close_explos" ); }
	void Spawn() OVERRIDE
	{
		m_flNextStateTime = gpGlobals->curtime;
		Precache();
		AddEffects( EF_NODRAW | EF_NOSHADOW );
		SetSolid( SOLID_NONE );
		SetModel( STRING( GetModelName() ) );
	}
	void Activate() OVERRIDE
	{
		BaseClass::Activate();
		CBaseEntity *entity = NULL;
		while ( ( entity = gEntList.FindEntityByName( entity, m_strModelGroup ) ) != NULL )
		{
			CBaseAnimating *animating = entity->GetBaseAnimating();
			if ( animating ) { animating->AddEffects( EF_NODRAW | EF_NOSHADOW ); animating->SetSolid( SOLID_NONE ); }
		}
	}
	void InputDoEffect( inputdata_t & )
	{
		m_flNextStateTime = gpGlobals->curtime;
		m_nState = 1;
		SetThink( &CEnvAirStrikeOutdoorEffect::CrumbleThink );
		SetNextThink( gpGlobals->curtime + 0.1f );
	}
	void CrumbleThink();
private:
	float m_flNextStateTime;
	string_t m_strModelGroup, m_strSequenceName1, m_strSequenceName2;
	int m_nState;
};
LINK_ENTITY_TO_CLASS( env_airstrike_outdoors, CEnvAirStrikeOutdoorEffect );
BEGIN_DATADESC( CEnvAirStrikeOutdoorEffect )
	DEFINE_INPUTFUNC( FIELD_VOID, "DoEffect", InputDoEffect ),
	DEFINE_KEYFIELD( m_strModelGroup, FIELD_STRING, "modelgroup" ),
	DEFINE_KEYFIELD( m_strSequenceName1, FIELD_STRING, "sequence1" ),
	DEFINE_KEYFIELD( m_strSequenceName2, FIELD_STRING, "sequence2" ),
END_DATADESC()
#pragma float_control( precise, on, push )
#if defined( _MSC_VER ) && _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
void CEnvAirStrikeOutdoorEffect::CrumbleThink()
{
	if ( gpGlobals->curtime >= m_flNextStateTime )
	{
		switch ( m_nState )
		{
		case 1:
		{
			UTIL_ScreenShake( GetAbsOrigin(), 25.0f, 130.0f, 1.0f, 1800.0f, SHAKE_START );
			EmitSound( "bombing.close_explos" );
			m_nState += 2;
			m_flNextStateTime = gpGlobals->curtime + 2.0f;
			RemoveEffects( EF_NODRAW );
			const char *sequence = STRING( RandomInt( 0, 1 ) ? m_strSequenceName1 : m_strSequenceName2 );
			int index = LookupSequence( sequence );
			if ( index > -1 ) ResetSequence( index );
			CBaseEntity *entity = NULL;
			while ( ( entity = gEntList.FindEntityByName( entity, m_strModelGroup ) ) != NULL )
			{
				CBaseAnimating *animating = entity->GetBaseAnimating();
				if ( !animating ) continue;
				animating->RemoveEffects( EF_NODRAW );
				edict_t *edict = animating->edict();
				if ( edict ) edict->m_fStateFlags |= FL_EDICT_DIRTY_PVS_INFORMATION;
				animating->DispatchUpdateTransmitState();
				index = animating->LookupSequence( sequence );
				if ( index > -1 ) animating->ResetSequence( index );
			}
			break;
		}
		case 3:
			EmitSound( "BaseExplosionEffect.Sound" );
			++m_nState;
			m_flNextStateTime = gpGlobals->curtime + 10.0f;
			break;
		case 4:
		{
			SUB_FadeOut();
			CBaseEntity *entity = NULL;
			while ( ( entity = gEntList.FindEntityByName( entity, m_strModelGroup ) ) != NULL )
			{
				// S0AEC8E: native bug calls SUB_FadeOut on THIS repeatedly,
				// not on each animating group member. Keep the receiver.
				if ( entity->GetBaseAnimating() ) SUB_FadeOut();
			}
			return;
		}
		}
	}
	if ( !IsEffectActive( EF_NODRAW ) ) { StudioFrameAdvance(); DispatchAnimEvents( this ); }
	SetNextThink( gpGlobals->curtime + 0.05f );
}
#pragma float_control( pop )
