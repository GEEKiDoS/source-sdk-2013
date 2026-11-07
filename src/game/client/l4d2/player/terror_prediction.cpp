//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "prediction.h"
#include "igamemovement.h"
#include "tier0/memdbgon.h"

// C22DFC0/C22E000. All other prediction slots are inherited from CPrediction.
class CCSPrediction : public CPrediction
{
public:
	DECLARE_CLASS( CCSPrediction, CPrediction );
protected:
	virtual void SetupMove( C_BasePlayer *player, CUserCmd *cmd, IMoveHelper *helper, CMoveData *move ) OVERRIDE
	{
		player->AvoidPhysicsProps( cmd );
		BaseClass::SetupMove( player, cmd, helper, move );
	}
	virtual void FinishMove( C_BasePlayer *player, CUserCmd *cmd, CMoveData *move ) OVERRIDE
	{
		BaseClass::FinishMove( player, cmd, move );
	}
};

static CMoveData g_MoveData;
CMoveData *g_pMoveData = &g_MoveData;
static CCSPrediction g_Prediction;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CCSPrediction, IPrediction, VCLIENT_PREDICTION_INTERFACE_VERSION, g_Prediction );
CPrediction *prediction = &g_Prediction;
