//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "player.h"
#include "l4d2/entities/visibility_monitor.h"
#include "tier0/memdbgon.h"

ConVar sv_gameinstructor_disable( "sv_gameinstructor_disable", "0", 0x00002006, "Force all clients to disable their game instructors." );

// S0BCE60/0BD320: all unmentioned fields retain CBaseEntity's zero-on-new.
class CEnvInstructorHint : public CPointEntity
{
public:
	DECLARE_CLASS( CEnvInstructorHint, CPointEntity );
	DECLARE_DATADESC();
	CEnvInstructorHint() : m_iDisplayLimit( 0 ), m_iInstanceType( 2 ) {}
	virtual ~CEnvInstructorHint() { inputdata_t input; InputEndHint( input ); }
	virtual void Spawn() OVERRIDE;
	void InputShowHint( inputdata_t &inputdata );
	void InputEndHint( inputdata_t &inputdata );
private:
	static bool AutoStart( CBaseEntity *entity, CBasePlayer *observer );
	string_t m_iszName, m_iszReplace_Key, m_iszHintTargetEntity;
	int m_iTimeout, m_iDisplayLimit;
	string_t m_iszIcon_Onscreen, m_iszIcon_Offscreen, m_iszCaption;
	color32 m_Color;
	float m_fIconOffset, m_fRange;
	uint8 m_iPulseOption, m_iAlphaOption, m_iShakeOption;
	bool m_bStatic, m_bNoOffscreen, m_bForceCaption;
	int m_iInstanceType;
	bool m_bSuppressRest;
	string_t m_iszBinding;
	bool m_bAllowNoDrawTarget, m_bAutoStart;
};
LINK_ENTITY_TO_CLASS( env_instructor_hint, CEnvInstructorHint );
BEGIN_DATADESC( CEnvInstructorHint )
	DEFINE_KEYFIELD( m_iszReplace_Key, FIELD_STRING, "hint_replace_key" ),
	DEFINE_KEYFIELD( m_iszHintTargetEntity, FIELD_STRING, "hint_target" ),
	DEFINE_KEYFIELD( m_iTimeout, FIELD_INTEGER, "hint_timeout" ),
	DEFINE_KEYFIELD( m_iDisplayLimit, FIELD_INTEGER, "hint_display_limit" ),
	DEFINE_KEYFIELD( m_iszIcon_Onscreen, FIELD_STRING, "hint_icon_onscreen" ),
	DEFINE_KEYFIELD( m_iszIcon_Offscreen, FIELD_STRING, "hint_icon_offscreen" ),
	DEFINE_KEYFIELD( m_iszCaption, FIELD_STRING, "hint_caption" ),
	DEFINE_KEYFIELD( m_Color, FIELD_COLOR32, "hint_color" ),
	DEFINE_KEYFIELD( m_fIconOffset, FIELD_FLOAT, "hint_icon_offset" ),
	DEFINE_KEYFIELD( m_fRange, FIELD_FLOAT, "hint_range" ),
	DEFINE_KEYFIELD( m_iPulseOption, FIELD_CHARACTER, "hint_pulseoption" ),
	DEFINE_KEYFIELD( m_iAlphaOption, FIELD_CHARACTER, "hint_alphaoption" ),
	DEFINE_KEYFIELD( m_iShakeOption, FIELD_CHARACTER, "hint_shakeoption" ),
	DEFINE_KEYFIELD( m_bStatic, FIELD_BOOLEAN, "hint_static" ),
	DEFINE_KEYFIELD( m_bNoOffscreen, FIELD_BOOLEAN, "hint_nooffscreen" ),
	DEFINE_KEYFIELD( m_bForceCaption, FIELD_BOOLEAN, "hint_forcecaption" ),
	DEFINE_KEYFIELD( m_iInstanceType, FIELD_INTEGER, "hint_instance_type" ),
	DEFINE_KEYFIELD( m_bSuppressRest, FIELD_BOOLEAN, "hint_suppress_rest" ),
	DEFINE_KEYFIELD( m_iszBinding, FIELD_STRING, "hint_binding" ),
	DEFINE_KEYFIELD( m_bAllowNoDrawTarget, FIELD_BOOLEAN, "hint_allow_nodraw_target" ),
	DEFINE_KEYFIELD( m_bAutoStart, FIELD_BOOLEAN, "hint_auto_start" ),
	DEFINE_KEYFIELD( m_iszName, FIELD_STRING, "hint_name" ),
	DEFINE_INPUTFUNC( FIELD_STRING, "ShowHint", InputShowHint ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EndHint", InputEndHint ),
END_DATADESC()

void CEnvInstructorHint::Spawn()
{
	BaseClass::Spawn();
	if ( m_bAutoStart )
		VisibilityMonitor_AddEntity( this, m_fRange > 0.0f ? m_fRange : 1000.0f, AutoStart );
}

bool CEnvInstructorHint::AutoStart( CBaseEntity *entity, CBasePlayer *observer )
{
	inputdata_t input;
	input.pActivator = observer;
	input.pCaller = entity;
	input.nOutputID = 0;
	static_cast<CEnvInstructorHint *>( entity )->InputShowHint( input );
	return false; // Native marks seen, but suppresses generic entity_visible.
}

// S0BCEF0: no activator/world fallback for an unresolved named target; the
// ShowHint string value and hint_replace_key are not read by the pinned body.
void CEnvInstructorHint::InputShowHint( inputdata_t &inputdata )
{
	IGameEvent *event = gameeventmanager->CreateEvent( "instructor_server_hint_create" );
	if ( !event ) return;
	CBaseEntity *target = gEntList.FindEntityByName( NULL, m_iszHintTargetEntity );
	char color[128];
	Q_snprintf( color, sizeof( color ), "%.3d,%.3d,%.3d", m_Color.r, m_Color.g, m_Color.b );
	// The PE32 shifts use the hardware-masked count, including malformed map
	// option bytes. Use unsigned shifts so that behavior is defined on x64.
	unsigned int flags = m_iPulseOption ? 1u << ( ( m_iPulseOption - 1 ) & 31 ) : 0u;
	flags |= m_iAlphaOption ? 8u << ( ( m_iAlphaOption - 1 ) & 31 ) : 0u;
	flags |= m_iShakeOption ? 64u << ( ( m_iShakeOption - 1 ) & 31 ) : 0u;
	flags |= m_bStatic ? 0x100u : 0u;
	if ( m_iszName == NULL_STRING ) m_iszName = GetEntityName();
	CBasePlayer *activator = dynamic_cast<CBasePlayer *>( inputdata.pActivator );
	event->SetInt( "userid", activator ? activator->GetUserID() : 0 );
	event->SetInt( "hint_entindex", entindex() );
	event->SetString( "hint_name", STRING( m_iszName ) );
	event->SetInt( "hint_target", target ? target->entindex() : -1 );
	event->SetInt( "hint_timeout", m_iTimeout );
	event->SetInt( "hint_display_limit", m_iDisplayLimit );
	event->SetString( "hint_icon_onscreen", STRING( m_iszIcon_Onscreen ) );
	event->SetString( "hint_icon_offscreen", STRING( m_iszIcon_Offscreen ) );
	event->SetString( "hint_caption", STRING( m_iszCaption ) );
	event->SetString( "hint_color", color );
	event->SetFloat( "hint_icon_offset", m_fIconOffset );
	event->SetFloat( "hint_range", m_fRange );
	event->SetInt( "hint_flags", (int)flags );
	event->SetString( "hint_binding", STRING( m_iszBinding ) );
	event->SetBool( "hint_allow_nodraw_target", m_bAllowNoDrawTarget );
	event->SetBool( "hint_nooffscreen", m_bNoOffscreen );
	event->SetBool( "hint_forcecaption", m_bForceCaption );
	event->SetInt( "hint_instance_type", m_iInstanceType );
	event->SetBool( "hint_suppress_rest", m_bSuppressRest );
	gameeventmanager->FireEvent( event );
}

void CEnvInstructorHint::InputEndHint( inputdata_t &inputdata )
{
	IGameEvent *event = gameeventmanager->CreateEvent( "instructor_server_hint_stop" );
	if ( !event ) return;
	event->SetString( "hint_name", STRING( m_iszName ) );
	event->SetInt( "hint_entindex", entindex() );
	gameeventmanager->FireEvent( event );
}

class CInfoInstructorHintTarget : public CPointEntity
{
public:
	DECLARE_CLASS( CInfoInstructorHintTarget, CPointEntity );
	DECLARE_DATADESC();
	virtual int UpdateTransmitState() OVERRIDE { return SetTransmitState( FL_EDICT_ALWAYS ); }
};
LINK_ENTITY_TO_CLASS( info_target_instructor_hint, CInfoInstructorHintTarget );
BEGIN_DATADESC( CInfoInstructorHintTarget )
END_DATADESC()
