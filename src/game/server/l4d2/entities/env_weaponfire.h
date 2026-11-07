#pragma once
#include "cbase.h"
#include "filters.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "ammodef.h"
#include "NextBot/NextBotManager.h"
#include "NextBot/NextBotInterface.h"
#include <math.h>

class CEnvWeaponfire : public CPointEntity
{
public:
    DECLARE_CLASS( CEnvWeaponfire, CPointEntity );
    DECLARE_DATADESC();
    DECLARE_SERVERCLASS();
    CEnvWeaponfire();
    virtual void Spawn() OVERRIDE;
    virtual void Activate() OVERRIDE;
    virtual int UpdateTransmitState() OVERRIDE;
    void InputEnable( inputdata_t &data );
    void InputDisable( inputdata_t &data );
    void WeaponfireThink();
    bool IsValidTarget( CBaseEntity *entity );
private:
    class FindTarget;
    bool FindEnemy();
    void ResetBurst();
    void Fire();
    void FirePellet( const CTerrorWeaponInfo &info, const Vector &direction, Vector &destination );
    bool m_bDisabled;
    bool m_bIgnorePlayers;
    int m_iEnemyTeam;
    float m_flTargetArc;
    float m_flTargetRange;
    CNetworkVar( int, m_iWeaponType );
    float m_flDamageMod;
    string_t m_iFilterName;
    EHANDLE m_hTarget;
    QAngle m_angOriginal;
    Vector m_vecOriginalForward;
    int m_nWeaponID;
    int m_nAmmoType;
    int m_iBurstSize;
    CNetworkVar( int, m_iShotsTaken );
    CNetworkVector( m_vecShotDest );
    CHandle< CBaseFilter > m_hFilter;
};

class CEnvWeaponfire::FindTarget
{
public:
	explicit FindTarget( CEnvWeaponfire *owner );
	bool operator()( INextBot *bot );
    CEnvWeaponfire *m_owner;
    CBaseEntity *m_target;
};
