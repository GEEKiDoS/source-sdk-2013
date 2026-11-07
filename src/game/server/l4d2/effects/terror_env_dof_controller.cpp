#include "cbase.h"
#include "tier0/memdbgon.h"

class CEnvDOFController : public CBaseEntity
{
public:
	DECLARE_CLASS( CEnvDOFController, CBaseEntity );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	void Spawn() OVERRIDE { SetSolid( SOLID_NONE ); SetMoveType( MOVETYPE_NONE ); }
	// S0B9640 is the native nullsub itself. No scheduled interpolation exists.
	void UpdateParamBlend() {}
	void InputSetNearBlurDepth( inputdata_t &data ) { m_flNearBlurDepth = data.value.Float(); }
	void InputSetNearFocusDepth( inputdata_t &data ) { m_flNearFocusDepth = data.value.Float(); }
	void InputSetFarFocusDepth( inputdata_t &data ) { m_flFarFocusDepth = data.value.Float(); }
	void InputSetFarBlurDepth( inputdata_t &data ) { m_flFarBlurDepth = data.value.Float(); }
	void InputSetNearBlurRadius( inputdata_t &data ) { m_flNearBlurRadius = data.value.Float(); m_bDOFEnabled = m_flNearBlurRadius > 0.0f || m_flFarBlurRadius > 0.0f; }
	void InputSetFarBlurRadius( inputdata_t &data ) { m_flFarBlurRadius = data.value.Float(); m_bDOFEnabled = m_flNearBlurRadius > 0.0f || m_flFarBlurRadius > 0.0f; }
private:
	// Native entity zero-fill; deliberately no client-constructor defaults here.
	CNetworkVar( bool, m_bDOFEnabled );
	CNetworkVar( float, m_flNearBlurDepth );
	CNetworkVar( float, m_flNearFocusDepth );
	CNetworkVar( float, m_flFarFocusDepth );
	CNetworkVar( float, m_flFarBlurDepth );
	CNetworkVar( float, m_flNearBlurRadius );
	CNetworkVar( float, m_flFarBlurRadius );
	float m_flBlendEndTime, m_flBlendStartTime;
};
LINK_ENTITY_TO_CLASS( env_dof_controller, CEnvDOFController );
IMPLEMENT_SERVERCLASS_ST( CEnvDOFController, DT_EnvDOFController )
	SendPropBool( SENDINFO( m_bDOFEnabled ) ),
	SendPropFloat( SENDINFO( m_flNearBlurDepth ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flNearFocusDepth ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flFarFocusDepth ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flFarBlurDepth ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flNearBlurRadius ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flFarBlurRadius ), 0, SPROP_NOSCALE ),
END_SEND_TABLE()
BEGIN_DATADESC( CEnvDOFController )
	DEFINE_FIELD( m_bDOFEnabled, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flNearBlurDepth, FIELD_FLOAT ),
	DEFINE_FIELD( m_flNearFocusDepth, FIELD_FLOAT ),
	DEFINE_FIELD( m_flFarFocusDepth, FIELD_FLOAT ),
	DEFINE_FIELD( m_flFarBlurDepth, FIELD_FLOAT ),
	DEFINE_FIELD( m_flNearBlurRadius, FIELD_FLOAT ),
	DEFINE_FIELD( m_flFarBlurRadius, FIELD_FLOAT ),
	DEFINE_FIELD( m_flBlendEndTime, FIELD_FLOAT ),
	DEFINE_FIELD( m_flBlendStartTime, FIELD_FLOAT ),
	DEFINE_THINKFUNC( UpdateParamBlend ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetNearBlurDepth", InputSetNearBlurDepth ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetNearFocusDepth", InputSetNearFocusDepth ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetFarFocusDepth", InputSetFarFocusDepth ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetFarBlurDepth", InputSetFarBlurDepth ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetNearBlurRadius", InputSetNearBlurRadius ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetFarBlurRadius", InputSetFarBlurRadius ),
END_DATADESC()
