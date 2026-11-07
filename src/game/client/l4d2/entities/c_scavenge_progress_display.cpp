//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/c_scavenge_progress_display.h"
#include "hud.h"
#include "iclientmode.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "vgui/IVGui.h"
#include "tier1/fmtstr.h"
#include "c_recipientfilter.h"
#include "engine/IEngineSound.h"
#include "tier0/memdbgon.h"

DECLARE_HUDELEMENT( CHudScavengeProgress );

// Native C269120 allocates1600: active byte1596 followed by three padding bytes.
// C0E94C0 replaces only the low byte; save/restore retains the complete word.
// Evidence: native_scavenge_client_allocation.txt and native_scavenge_client_wire.txt.
static void RecvProxy_ScavengeActive( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	int *active = static_cast< int * >( pOut );
	*active = ( *active & ~0xff ) | ( pData->m_Value.m_Int & 0xff );
}
IMPLEMENT_CLIENTCLASS_DT( CScavengeProgressDisplay, DT_ScavengeProgressDisplay, CScavengeProgressDisplay )
	RecvPropInt( RECVINFO( m_bActive ), 0, RecvProxy_ScavengeActive ),
END_RECV_TABLE()
BEGIN_DATADESC( CScavengeProgressDisplay )
	DEFINE_FIELD( m_bActive, FIELD_INTEGER ),
END_DATADESC()

CHudScavengeProgress::CHudScavengeProgress( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudScavengeProgress" )
{
	SetHiddenBits( 0 );
	vgui::ivgui()->AddTickSignal( GetVPanel(), 250 );
	m_bShow = false;
	SetActive( true );
	ListenForGameEvent( "server_spawn" );
}
void CHudScavengeProgress::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/HUD/HudScavengeProgress.res" );
	BaseClass::ApplySchemeSettings( scheme );
	m_nItemsCollected = 0;
	m_nItemsGoal = 0;
}
void CHudScavengeProgress::LevelInit()
{
	m_nItemsCollected = 0;
	m_nItemsGoal = 0;
	m_bNativeUnused = false;
	m_bShow = false;
}
void CHudScavengeProgress::FireGameEvent( IGameEvent *event )
{
	const char *name = event->GetName();
	if ( name && name[0] && !Q_strcmp( name, "server_spawn" ) ) LevelInit();
}
bool CHudScavengeProgress::ShouldDraw() { return m_bShow && CHudElement::ShouldDraw(); }
void CHudScavengeProgress::SetDisplayActive( bool active ) { m_bShow = active; }
void CHudScavengeProgress::OnThink()
{
	CTerrorGameRules *rules = TerrorGameRules();
	const int collected = rules ? rules->GetScavengeTeamScore( TEAM_SURVIVOR, -1 ) : 0;
	if ( collected != m_nItemsCollected )
	{
		m_nItemsCollected = collected;
		SetControlString( "ItemsCollectedDigits", CFmtStr( "%d", collected ).Access() );
		// Native plays on ANY changed positive value, even a decrease.
		if ( ShouldDraw() && collected > 0 )
		{
			CLocalPlayerFilter filter;
			C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "Scavenge.point_scored" );
		}
	}
	rules = TerrorGameRules(); // Native re-reads rules after the score/sound calls.
	const int goal = rules ? rules->GetScavengeItemsGoal() : 0;
	if ( goal != m_nItemsGoal )
	{
		m_nItemsGoal = goal;
		SetControlString( "ItemsGoalDigits", CFmtStr( "%d", goal ).Access() );
	}
}
datamap_t *CScavengeProgressDisplay::GetPredDescMap() { return NULL; }
void CScavengeProgressDisplay::OnDataChanged( DataUpdateType_t type )
{
	CHudScavengeProgress *hud = GET_HUDELEMENT( CHudScavengeProgress );
	if ( hud ) hud->SetDisplayActive( ( m_bActive & 0xff ) != 0 );
	// Native2690B0 has no base callback, list/aggregation, or removal reset.
	// Multiple entities deliberately have last-updated-wins semantics.
}
