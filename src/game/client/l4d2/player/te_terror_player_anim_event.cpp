//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_basetempentity.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/terror_shareddefs.h"
#include "cdll_bounded_cvars.h"
#include "tier0/memdbgon.h"


class C_TEPlayerAnimEvent : public C_BaseTempEntity
{
public:
	DECLARE_CLASS( C_TEPlayerAnimEvent, C_BaseTempEntity );
	DECLARE_CLIENTCLASS();
	virtual void PostDataUpdate( DataUpdateType_t updateType ) OVERRIDE;

	CHandle< C_BasePlayer > m_hPlayer;
	int m_iEvent;
	int m_nData;
};

IMPLEMENT_CLIENTCLASS_EVENT( C_TEPlayerAnimEvent, DT_TEPlayerAnimEvent, CTEPlayerAnimEvent );

BEGIN_RECV_TABLE_NOBASE( C_TEPlayerAnimEvent, DT_TEPlayerAnimEvent )
	RecvPropEHandle( RECVINFO( m_hPlayer ) ),
	RecvPropInt( RECVINFO( m_iEvent ) ),
	RecvPropInt( RECVINFO( m_nData ) ),
END_RECV_TABLE()

void C_TEPlayerAnimEvent::PostDataUpdate( DataUpdateType_t updateType )
{
	// C223920 resolves the real player, rejects dormant, then applies the
	// original local-prediction whitelist. It does not discard all local TEs.
	C_TerrorPlayer *pPlayer = ToTerrorPlayer( m_hPlayer.Get() );
	if ( !pPlayer || pPlayer->IsDormant() )
		return;
	if ( pPlayer->IsLocalPlayer() && cl_predict->GetInt() != 0 )
	{
		switch ( m_iEvent )
		{
		case 42: case 43: case 44: case 46: case 47:
		case 48: case 49: case 51: case 64:
			break;
		default:
			return;
		}
	}

	// S2FBB30's genuine Terror path broadcasts only7/66. Translate at this
	// wire boundary; never reinterpret a native ordinal as the SDK enum.
	switch ( m_iEvent )
	{
	case 7:
		pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_JUMP, m_nData );
		break;
	case 66:
		pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CUSTOM_ATTACK_GESTURE_ALT,
			static_cast< int >( TerrorActivityFromOriginal( m_nData ) ) );
		break;
	}
}
