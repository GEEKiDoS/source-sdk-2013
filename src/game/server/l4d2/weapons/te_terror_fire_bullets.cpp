//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "basetempentity.h"
#include "recipientfilter.h"
#include "l4d2/effects/terror_fx_shared.h"

#include "tier0/memdbgon.h"

// S:0x3C7AB0/0x3C7C40. The angles have already had the gun's spread applied.
class CTEBullets : public CBaseTempEntity
{
public:
	DECLARE_CLASS( CTEBullets, CBaseTempEntity );
	DECLARE_SERVERCLASS();

	CTEBullets( const char *pName ) : BaseClass( pName )
	{
		m_vecOrigin.Init();
		m_vecAngles.Init();
		m_iWeaponID = m_iMode = m_iSeed = m_iPlayer = 0;
		m_flSoundTime = 0.0f;
	}

	CNetworkVector( m_vecOrigin );
	CNetworkQAngle( m_vecAngles );
	CNetworkVar( int, m_iWeaponID );
	CNetworkVar( int, m_iMode );
	CNetworkVar( int, m_iSeed );
	CNetworkVar( int, m_iPlayer );
	CNetworkVar( float, m_flSoundTime );
};

IMPLEMENT_SERVERCLASS_ST_NOBASE( CTEBullets, DT_TEBullets )
	SendPropVector( SENDINFO( m_vecOrigin ), -1, SPROP_COORD_MP_LOWPRECISION ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecAngles, 0 ), 10, SPROP_ROUNDDOWN, 0.0f, 360.0f, SendProxy_AngleToFloat ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecAngles, 1 ), 10, SPROP_ROUNDDOWN, 0.0f, 360.0f, SendProxy_AngleToFloat ),
	SendPropInt( SENDINFO( m_iWeaponID ), 6, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iMode ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iSeed ), 32, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iPlayer ), 5, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_flSoundTime ), 0, SPROP_NOSCALE ),
END_SEND_TABLE()

static CTEBullets g_TEBullets( "Bullets" );

void TE_Bullets( int playerIndex, const Vector &origin, const QAngle &angles,
	int weaponID, int mode, int seed, float soundTime )
{
	CPASFilter filter( origin );
	// Do not unconditionally remove the shooter: bot/non-predicted shots have no
	// predicted counterpart. The command's native suppress-host owns this rule.
	filter.UsePredictionRules();

	g_TEBullets.m_iPlayer = playerIndex - 1;
	g_TEBullets.m_vecOrigin = origin;
	g_TEBullets.m_vecAngles = angles;
	g_TEBullets.m_iWeaponID = weaponID;
	g_TEBullets.m_iMode = mode;
	g_TEBullets.m_iSeed = seed;
	g_TEBullets.m_flSoundTime = soundTime;
	g_TEBullets.Create( filter, 0.0f );
}
