//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "player_command.h"
#include "igamemovement.h"
#include "ipredictionsystem.h"
#include "l4d2/player/cs_player.h"
#include "iservervehicle.h"
#include "tier0/memdbgon.h"

// Native CCSPlayerMove: S24BCD0 / S24BD40 / S24BDA0.
class CCSPlayerMove : public CPlayerMove
{
public:
	DECLARE_CLASS( CCSPlayerMove, CPlayerMove );
protected:
	virtual void SetupMove( CBasePlayer *player, CUserCmd *cmd, IMoveHelper *helper, CMoveData *move ) OVERRIDE;
	virtual void FinishMove( CBasePlayer *player, CUserCmd *cmd, CMoveData *move ) OVERRIDE;
	virtual void StartCommand( CBasePlayer *player, CUserCmd *cmd ) OVERRIDE;
};

#pragma float_control( precise, on, push )
void CCSPlayerMove::SetupMove( CBasePlayer *player, CUserCmd *cmd, IMoveHelper *helper, CMoveData *move )
{
	player->AvoidPhysicsProps( cmd );
	BaseClass::SetupMove( player, cmd, helper, move );
	IServerVehicle *vehicle = player->GetVehicle();
	// Native UCOMISS/LAHF accepts unordered as well as nonzero frame times.
	if ( vehicle && gpGlobals->frametime != 0.0f )
		vehicle->SetupMove( player, cmd, helper, move );
}

void CCSPlayerMove::FinishMove( CBasePlayer *player, CUserCmd *cmd, CMoveData *move )
{
	BaseClass::FinishMove( player, cmd, move );
	IServerVehicle *vehicle = player->GetVehicle();
	if ( vehicle && gpGlobals->frametime != 0.0f )
		vehicle->FinishMove( player, cmd, move );
}
#pragma float_control( pop )

void CCSPlayerMove::StartCommand( CBasePlayer *player, CUserCmd *cmd )
{
	CCSPlayer *csPlayer = dynamic_cast< CCSPlayer * >( player );
	// Ledger306: only a wrong-type dependent reset is skipped; base still runs.
	if ( csPlayer )
	{
		csPlayer->m_bInBombZone = false;
		csPlayer->m_bInBuyZone = false;
		csPlayer->m_bInHostageRescueZone = false;
	}
	BaseClass::StartCommand( player, cmd );
}

static CMoveData g_MoveData;
CMoveData *g_pMoveData = &g_MoveData;
IPredictionSystem *IPredictionSystem::g_pPredictionSystems = NULL;
static CCSPlayerMove g_PlayerMove;
CPlayerMove *PlayerMove()
{
	return &g_PlayerMove;
}
