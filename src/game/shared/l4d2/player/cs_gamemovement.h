//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef CS_GAMEMOVEMENT_H
#define CS_GAMEMOVEMENT_H
#ifdef _WIN32
#pragma once
#endif

#include "gamemovement.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_cs_player.h"
#else
#include "l4d2/player/cs_player.h"
#endif

class CCSGameMovement : public CGameMovement
{
public:
	DECLARE_CLASS( CCSGameMovement, CGameMovement );
	CCSGameMovement();
	virtual void ProcessMovement( CBasePlayer *pPlayer, CMoveData *pMove );

protected:
	virtual void CheckParameters();
	virtual void CategorizePosition( bool bForceMoveToEndPos ) OVERRIDE;
	virtual void ReduceTimers();
	virtual void DecayPunchAngle() OVERRIDE;
	virtual bool CanAccelerate() OVERRIDE;
	virtual float GetSpeedModifier() const;
	virtual bool LadderMove() OVERRIDE;
	virtual bool OnLadder( trace_t &trace ) OVERRIDE;
	virtual void OnFinishWalkMove( bool bOnGround );
	virtual void Duck() OVERRIDE;
	virtual void HandleDuckingSpeedCrop() OVERRIDE;
	virtual void FinishUnDuck() OVERRIDE;
	virtual void FinishDuck() OVERRIDE;
	virtual bool CanUnduck() OVERRIDE;
	void CheckDuckUntilOnGround();
	void CheckSpeedParameters();
	void CheckAngleParameters();
	virtual void CheckGroundObstacle();
	bool SetCrouchForJump( bool duck );
	CCSPlayer *m_pCSPlayer;
};

#endif // CS_GAMEMOVEMENT_H
