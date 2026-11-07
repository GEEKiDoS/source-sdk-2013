//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_basetempentity.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "usercmd.h"

#include "tier0/memdbgon.h"

class C_TEBullets : public C_BaseTempEntity
{
public:
	DECLARE_CLASS( C_TEBullets, C_BaseTempEntity );
	DECLARE_CLIENTCLASS();

	C_TEBullets()
	{
		m_vecOrigin.Init();
		m_vecAngles.Init();
		m_iWeaponID = m_iMode = m_iPlayer = 0;
		m_iSeed = 0;
		m_flSoundTime = 0.0f;
	}

	virtual void PostDataUpdate( DataUpdateType_t updateType ) OVERRIDE;

	Vector m_vecOrigin;
	QAngle m_vecAngles;
	int m_iWeaponID;
	int m_iMode;
	int m_iSeed;
	int m_iPlayer;
	float m_flSoundTime;
};

IMPLEMENT_CLIENTCLASS_EVENT( C_TEBullets, DT_TEBullets, CTEBullets )
BEGIN_RECV_TABLE_NOBASE( C_TEBullets, DT_TEBullets )
	RecvPropVector( RECVINFO( m_vecOrigin ) ),
	RecvPropFloat( RECVINFO( m_vecAngles[0] ) ),
	RecvPropFloat( RECVINFO( m_vecAngles[1] ) ),
	RecvPropInt( RECVINFO( m_iWeaponID ) ),
	RecvPropInt( RECVINFO( m_iMode ) ),
	RecvPropInt( RECVINFO( m_iSeed ) ),
	RecvPropInt( RECVINFO( m_iPlayer ) ),
	RecvPropFloat( RECVINFO( m_flSoundTime ) ),
END_RECV_TABLE()

void C_TEBullets::PostDataUpdate( DataUpdateType_t updateType )
{
	// C:0x2F63C0 clears unsent roll and installs the transmitted seed through a
	// real user command before entering the shared presentation path.
	m_vecAngles.z = 0.0f;
	CUserCmd command;
	command.random_seed = m_iSeed;
	C_BaseEntity::SetPredictionRandomSeed( &command );
	FireTerrorBullets( ToTerrorPlayer( C_BaseEntity::Instance( m_iPlayer + 1 ) ),
		m_vecOrigin, m_vecAngles, m_iWeaponID, m_iMode, m_iSeed, m_flSoundTime );
}
