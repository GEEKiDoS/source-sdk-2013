//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Color correction entity with simple radial falloff
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"

#include "filesystem.h"
#include "cdll_client_int.h"
#include "colorcorrectionmgr.h"
#include "materialsystem/MaterialSystemUtil.h"
#if defined( L4D2 )
#include "l4d2/clientmode_terror.h"
#include "tier1/utlvector.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if defined( L4D2 )
static ConVar mat_colcorrection_disableentities( "mat_colcorrection_disableentities", "0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
, "Disable map color-correction entities" );
static ConVar mat_colcorrection_forceentitiesclientside( "mat_colcorrection_forceentitiesclientside", "0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
, "Forces color correction entities to be updated on the client" );
class C_ColorCorrection;
static CUtlVector< C_ColorCorrection * > s_TerrorColorCorrections;

class C_ColorCorrection : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_ColorCorrection, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	C_ColorCorrection();
	virtual ~C_ColorCorrection();
	virtual void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
	virtual bool ShouldDraw() OVERRIDE { return false; }
	void SetClientSideWeight( bool enabled, bool reset );
	void UpdateWeight( C_BasePlayer *player, float scale );
private:
	void UpdateClientSideFade();
	Vector m_vecOrigin;
	float m_minFalloff, m_maxFalloff;
	float m_flFadeInDuration, m_flFadeOutDuration, m_flMaxWeight, m_flCurWeight;
	char m_netLookupFilename[MAX_PATH];
	bool m_bEnabled, m_bMaster, m_bClientSide, m_bExclusive;
	bool m_bClientSideEnabled;
	float m_flClientSideWeight;
	bool m_bFadeIn;
	float m_flFadeStartWeight, m_flFadeStartTime, m_flFadeDuration;
	ClientCCHandle_t m_CCHandle;
};
IMPLEMENT_CLIENTCLASS_DT( C_ColorCorrection, DT_ColorCorrection, CColorCorrection )
	RecvPropVector( RECVINFO( m_vecOrigin ) ),
	RecvPropFloat( RECVINFO( m_minFalloff ) ),
	RecvPropFloat( RECVINFO( m_maxFalloff ) ),
	RecvPropFloat( RECVINFO( m_flCurWeight ) ),
	RecvPropFloat( RECVINFO( m_flMaxWeight ) ),
	RecvPropFloat( RECVINFO( m_flFadeInDuration ) ),
	RecvPropFloat( RECVINFO( m_flFadeOutDuration ) ),
	RecvPropString( RECVINFO( m_netLookupFilename ) ),
	RecvPropBool( RECVINFO( m_bEnabled ) ),
	RecvPropBool( RECVINFO( m_bMaster ) ),
	RecvPropBool( RECVINFO( m_bClientSide ) ),
	RecvPropBool( RECVINFO( m_bExclusive ) ),
END_RECV_TABLE()

C_ColorCorrection::C_ColorCorrection()
	: m_minFalloff( -1 ), m_maxFalloff( -1 ), m_flFadeInDuration( 0 ), m_flFadeOutDuration( 0 ),
	m_flMaxWeight( 1 ), m_flCurWeight( 0 ), m_bEnabled( false ), m_bMaster( false ), m_bExclusive( false ),
	m_bClientSideEnabled( false ), m_flClientSideWeight( 0 ), m_bFadeIn( false ),
	m_flFadeStartWeight( 0 ), m_flFadeStartTime( 0 ), m_flFadeDuration( 0 ), m_CCHandle( INVALID_CLIENT_CCHANDLE )
{
	m_netLookupFilename[0] = '\0';
	// m_vecOrigin/m_bClientSide are native constructor omissions: genuine
	// zero-filled client entity allocation supplies their initial value.
}
C_ColorCorrection::~C_ColorCorrection()
{
	g_pColorCorrectionMgr->RemoveColorCorrection( m_CCHandle );
	const int index = s_TerrorColorCorrections.Find( this );
	if ( index != s_TerrorColorCorrections.InvalidIndex() ) s_TerrorColorCorrections.FastRemove( index );
	static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->OnColorCorrectionEntityDeleted( this );
}
void C_ColorCorrection::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	if ( type == DATA_UPDATE_CREATED && m_CCHandle == INVALID_CLIENT_CCHANDLE )
	{
		char stripped[MAX_PATH], name[MAX_PATH];
		Q_StripExtension( m_netLookupFilename, stripped, sizeof( stripped ) ); //103D23C0, retains path
		Q_snprintf( name, sizeof( name ), "%s_%d", stripped, entindex() );
		m_CCHandle = g_pColorCorrectionMgr->AddColorCorrection( name, m_netLookupFilename );
		if ( m_CCHandle != INVALID_CLIENT_CCHANDLE ) s_TerrorColorCorrections.AddToTail( this );
	}
}
#pragma float_control( precise, on, push )
void C_ColorCorrection::UpdateClientSideFade()
{
	float fraction = 1.0f;
	if ( m_flFadeDuration != 0.0f )
	{
		fraction = ( gpGlobals->curtime - m_flFadeStartTime ) / m_flFadeDuration;
		if ( fraction > 1.0f ) fraction = 1.0f;
		else if ( 0.0f > fraction ) fraction = 0.0f;
	}
	const float target = m_bFadeIn ? m_flMaxWeight : 0.0f;
	m_flClientSideWeight = ( target - m_flFadeStartWeight ) * fraction + m_flFadeStartWeight;
}
void C_ColorCorrection::SetClientSideWeight( bool enabled, bool reset )
{
	if ( !m_bClientSide && !mat_colcorrection_forceentitiesclientside.GetBool() ) return;
	if ( m_bClientSideEnabled == enabled ) return;
	m_bFadeIn = m_bClientSideEnabled = enabled;
	m_flFadeStartWeight = m_flClientSideWeight;
	float fraction = 1.0f;
	if ( m_flMaxWeight != 0.0f ) fraction = m_flClientSideWeight / m_flMaxWeight;
	if ( m_bFadeIn ) fraction = 1.0f - fraction;
	if ( reset ) fraction = 0.0f;
	const float duration = ( m_bFadeIn ? m_flFadeInDuration : m_flFadeOutDuration ) * fraction;
	m_flFadeStartTime = gpGlobals->curtime;
	// COMISS/JBE makes unordered duration zero, not NaN MAX propagation.
	m_flFadeDuration = duration > 0.0f ? duration : 0.0f;
	UpdateClientSideFade(); // native immediate same-frame calculation
}
void C_ColorCorrection::UpdateWeight( C_BasePlayer *player, float scale )
{
	if ( mat_colcorrection_disableentities.GetBool() )
	{
		g_pColorCorrectionMgr->SetColorCorrectionWeight( m_CCHandle, 0.0f, m_bExclusive );
		return;
	}
	const bool client = m_bClientSide || mat_colcorrection_forceentitiesclientside.GetBool();
	const bool enabled = client ? m_bClientSideEnabled : m_bEnabled;
	if ( client ) UpdateClientSideFade();
	const float weight = client ? m_flClientSideWeight : m_flCurWeight;
	if ( !enabled && weight == 0.0f )
	{
		g_pColorCorrectionMgr->SetColorCorrectionWeight( m_CCHandle, 0.0f, m_bExclusive );
		return;
	}
	const Vector &position = player->GetAbsOrigin(); //native virtual+44
	float falloff = 0.0f;
	if ( m_minFalloff != -1.0f && m_maxFalloff != -1.0f && m_minFalloff != m_maxFalloff )
	{
		const float dx = position.x - m_vecOrigin.x;
		const float dy = position.y - m_vecOrigin.y;
		const float dz = position.z - m_vecOrigin.z;
		const float distance = sqrtf( ( dy * dy + dz * dz ) + dx * dx );
		falloff = ( distance - m_minFalloff ) / ( m_maxFalloff - m_minFalloff );
		if ( 0.0f > falloff ) falloff = 0.0f;
		else if ( falloff > 1.0f ) falloff = 1.0f;
	}
	g_pColorCorrectionMgr->SetColorCorrectionWeight( m_CCHandle, ( ( 1.0f - falloff ) * weight ) * scale, m_bExclusive );
}
#pragma float_control( pop )

void SetTerrorColorCorrectionClientSideWeight( C_BaseEntity *entity, bool enabled, bool reset )
{
	// Native handle6148 is already typed by the real map/receive owner.
	static_cast< C_ColorCorrection * >( entity )->SetClientSideWeight( enabled, reset );
}
void UpdateTerrorColorCorrectionEntities( C_BasePlayer *player, float scale )
{
	FOR_EACH_VEC( s_TerrorColorCorrections, i ) s_TerrorColorCorrections[i]->UpdateWeight( player, scale );
}
#else


static ConVar mat_colcorrection_disableentities( "mat_colcorrection_disableentities", "0", FCVAR_NONE, "Disable map color-correction entities" );


//------------------------------------------------------------------------------
// Purpose : Color correction entity with radial falloff
//------------------------------------------------------------------------------
class C_ColorCorrection : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_ColorCorrection, C_BaseEntity );

	DECLARE_CLIENTCLASS();

	C_ColorCorrection();
	virtual ~C_ColorCorrection();

	void OnDataChanged(DataUpdateType_t updateType);
	bool ShouldDraw();

	void ClientThink();

private:
	Vector	m_vecOrigin;

	float	m_minFalloff;
	float	m_maxFalloff;
	float	m_flCurWeight;
	char	m_netLookupFilename[MAX_PATH];

	bool	m_bEnabled;

	ClientCCHandle_t m_CCHandle;
};

IMPLEMENT_CLIENTCLASS_DT(C_ColorCorrection, DT_ColorCorrection, CColorCorrection)
	RecvPropVector( RECVINFO(m_vecOrigin) ),
	RecvPropFloat(  RECVINFO(m_minFalloff) ),
	RecvPropFloat(  RECVINFO(m_maxFalloff) ),
	RecvPropFloat(  RECVINFO(m_flCurWeight) ),
	RecvPropString( RECVINFO(m_netLookupFilename) ),
	RecvPropBool(   RECVINFO(m_bEnabled) ),

END_RECV_TABLE()


//------------------------------------------------------------------------------
// Constructor, destructor
//------------------------------------------------------------------------------
C_ColorCorrection::C_ColorCorrection()
{
	m_CCHandle = INVALID_CLIENT_CCHANDLE;
}

C_ColorCorrection::~C_ColorCorrection()
{
	g_pColorCorrectionMgr->RemoveColorCorrection( m_CCHandle );
}


//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
void C_ColorCorrection::OnDataChanged(DataUpdateType_t updateType)
{
	BaseClass::OnDataChanged( updateType );

	if ( updateType == DATA_UPDATE_CREATED )
	{
		if ( m_CCHandle == INVALID_CLIENT_CCHANDLE )
		{
			char filename[MAX_PATH];
			Q_strncpy( filename, m_netLookupFilename, MAX_PATH );

			m_CCHandle = g_pColorCorrectionMgr->AddColorCorrection( filename );
			SetNextClientThink( ( m_CCHandle != INVALID_CLIENT_CCHANDLE ) ? CLIENT_THINK_ALWAYS : CLIENT_THINK_NEVER );
		}
	}
}

//------------------------------------------------------------------------------
// We don't draw...
//------------------------------------------------------------------------------
bool C_ColorCorrection::ShouldDraw()
{
	return false;
}

void C_ColorCorrection::ClientThink()
{
	if ( m_CCHandle == INVALID_CLIENT_CCHANDLE )
		return;

	if ( mat_colcorrection_disableentities.GetInt() )
	{
		// Allow the colorcorrectionui panel (or user) to turn off color-correction entities
		g_pColorCorrectionMgr->SetColorCorrectionWeight( m_CCHandle, 0.0f );
		return;
	}

	if( !m_bEnabled && m_flCurWeight == 0.0f )
	{
		g_pColorCorrectionMgr->SetColorCorrectionWeight( m_CCHandle, 0.0f );
		return;
	}

	C_BaseEntity *pPlayer = C_BasePlayer::GetLocalPlayer();
	if( !pPlayer )
		return;

	Vector playerOrigin = pPlayer->GetAbsOrigin();

	float weight = 0;
	if ( ( m_minFalloff != -1 ) && ( m_maxFalloff != -1 ) && m_minFalloff != m_maxFalloff )
	{
		float dist = (playerOrigin - m_vecOrigin).Length();
		weight = (dist-m_minFalloff) / (m_maxFalloff-m_minFalloff);
		if ( weight<0.0f ) weight = 0.0f;	
		if ( weight>1.0f ) weight = 1.0f;	
	}
	
	g_pColorCorrectionMgr->SetColorCorrectionWeight( m_CCHandle, m_flCurWeight * ( 1.0 - weight ) );

	BaseClass::ClientThink();
}













#endif // L4D2
