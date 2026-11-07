//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/brush_roots_client.h"
#include "l4d2/gamerules/terror_client_game_details.h"
#include "l4d2/gamerules/terror_mode.h"
#include "tier1/KeyValues.h"
#include "physics.h"
#include "tier0/memdbgon.h"

class C_FuncMoveLinear : public C_BaseToggle
{
public:
	DECLARE_CLASS( C_FuncMoveLinear, C_BaseToggle );
	DECLARE_CLIENTCLASS();
	C_FuncMoveLinear();
	datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // C7C750.
	void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
private:
	bool m_bNonOriginalCampaign; // Native C1592, constructor C7C850 only.
};
IMPLEMENT_CLIENTCLASS_DT( C_FuncMoveLinear, DT_FuncMoveLinear, CFuncMoveLinear )
	RecvPropVector( RECVINFO( m_vecVelocity ), 0, RecvProxy_LocalVelocity ),
	RecvPropInt( RECVINFO( m_fFlags ) ),
END_RECV_TABLE()

C_FuncMoveLinear::C_FuncMoveLinear()
{
	// Native C27B530 recognizes only L4D2C1..5. Ledger297 maps the absent
	// match-session mission query to the existing mounted dictionary owner.
	KeyValues *details = TerrorBuildConsumedClientGameProjection();
	KeyValues *mission = NULL;
	if ( details )
	{
		g_TerrorModes.GetChapterInfo( details, &mission );
		details->deleteThis();
	}
	const char *suffix = mission ? StringAfterPrefix( mission->GetString( "name", "" ), "L4D2C" ) : NULL;
	m_bNonOriginalCampaign = !suffix || static_cast< unsigned int >( atoi( suffix ) ) - 1u > 4u;
}

void C_FuncMoveLinear::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	if ( type == DATA_UPDATE_CREATED )
	{
		SetSolid( SOLID_VPHYSICS );
		VPhysicsInitShadow( false, false );
		CollisionRulesChanged();
	}
	else if ( m_bNonOriginalCampaign )
	{
		PhysOnDataChanged( this, false );
	}
}
