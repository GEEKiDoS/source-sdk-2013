#include "cbase.h"
#include "l4d2/effects/terror_depth_of_field.h"
#include "tier0/memdbgon.h"

// Native pre-controller globals differ deliberately from constructor defaults.
TerrorDOFState_t g_TerrorDOFState = { false, 50.0f, 200.0f, 250.0f, 1000.0f, BitsToFloat( 0xffffffffu ), 5.0f };
class C_EnvDOFController;
static CHandle< C_EnvDOFController > s_LastUpdatedDOFController;
class C_EnvDOFController : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_EnvDOFController, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	C_EnvDOFController() : m_bDOFEnabled( true ), m_flNearBlurDepth( 50.0f ), m_flNearFocusDepth( 100.0f ), m_flFarFocusDepth( 250.0f ), m_flFarBlurDepth( 1000.0f ), m_flNearBlurRadius( 0.0f ), m_flFarBlurRadius( 5.0f ) {}
	~C_EnvDOFController()
	{
		if ( s_LastUpdatedDOFController.Get() == this ) g_TerrorDOFState.enabled = false;
	}
	void OnDataChanged( DataUpdateType_t type ) OVERRIDE
	{
		BaseClass::OnDataChanged( type );
		g_TerrorDOFState.enabled = m_bDOFEnabled && ( m_flNearBlurRadius > 0.0f || m_flFarBlurRadius > 0.0f );
		g_TerrorDOFState.nearBlurDepth = m_flNearBlurDepth;
		g_TerrorDOFState.nearFocusDepth = m_flNearFocusDepth;
		g_TerrorDOFState.farFocusDepth = m_flFarFocusDepth;
		g_TerrorDOFState.farBlurDepth = m_flFarBlurDepth;
		g_TerrorDOFState.nearBlurRadius = m_flNearBlurRadius;
		g_TerrorDOFState.farBlurRadius = m_flFarBlurRadius;
		s_LastUpdatedDOFController = this;
	}
private:
	bool m_bDOFEnabled;
	float m_flNearBlurDepth, m_flNearFocusDepth, m_flFarFocusDepth, m_flFarBlurDepth;
	float m_flNearBlurRadius, m_flFarBlurRadius;
};
IMPLEMENT_CLIENTCLASS_DT( C_EnvDOFController, DT_EnvDOFController, CEnvDOFController )
	RecvPropBool( RECVINFO( m_bDOFEnabled ) ),
	RecvPropFloat( RECVINFO( m_flNearBlurDepth ) ),
	RecvPropFloat( RECVINFO( m_flNearFocusDepth ) ),
	RecvPropFloat( RECVINFO( m_flFarFocusDepth ) ),
	RecvPropFloat( RECVINFO( m_flFarBlurDepth ) ),
	RecvPropFloat( RECVINFO( m_flNearBlurRadius ) ),
	RecvPropFloat( RECVINFO( m_flFarBlurRadius ) ),
END_RECV_TABLE()
