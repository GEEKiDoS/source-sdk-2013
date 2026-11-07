//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_GAMEMOVEMENT_H
#define TERROR_GAMEMOVEMENT_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/cs_gamemovement.h"
#include "tier1/utlvector.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif

class ITraceFilter;

class CTerrorGameMovement : public CCSGameMovement
{
public:
	DECLARE_CLASS( CTerrorGameMovement, CCSGameMovement );
	CTerrorGameMovement();
#ifndef CLIENT_DLL
	void UnblockPusher( CBasePlayer *subject, CBaseEntity *pusher );
#endif
	virtual void ProcessMovement( CBasePlayer *pPlayer, CMoveData *pMove );
	virtual Vector GetPlayerMins( bool bDucked ) const;
	virtual Vector GetPlayerMaxs( bool bDucked ) const;
	virtual Vector GetPlayerViewOffset( bool bDucked ) const;

protected:
	virtual void PlayerMove();
	virtual void CheckParameters();
	virtual bool CanCheckStuck() OVERRIDE;
	virtual ITraceFilter *GetMovementTraceFilter( int collisionGroup ) OVERRIDE;
	virtual void ReleaseMovementTraceFilter( ITraceFilter **filter ) OVERRIDE;
	virtual void DecayPunchAngle() OVERRIDE;
	virtual float GetSpeedModifier() const OVERRIDE;
	virtual void CheckGroundObstacle() OVERRIDE;
	virtual void HandleAbilityJump() OVERRIDE;
	virtual void OnStartDuck() OVERRIDE;
	virtual void PlayerRoughLandingEffects( float volume ) OVERRIDE;
	virtual bool CanSwim() const OVERRIDE;
	virtual void PlaySwimSound() OVERRIDE;
	void PerformJump( const Vector *queuedMovement );
	void SetMoveDirection( const Vector &direction );
	virtual void CategorizePosition( bool bForceMoveToEndPos ) OVERRIDE;
	virtual void SetGroundEntity( trace_t *trace ) OVERRIDE;
	virtual void OnPlayerMoveCollision( const Vector &start, const Vector &end, trace_t *trace ) OVERRIDE;
	virtual void FullCustomMove() OVERRIDE;
	virtual bool ShouldStayOnGround() OVERRIDE;
	void FullLedgeMove();
	virtual void WalkMove();
	virtual void AirMove() OVERRIDE;
	virtual void FullLadderMove() OVERRIDE;
	bool FullTongueMove();
	virtual float GetStandableNormal() const OVERRIDE { return m_flStandableNormal; }
	virtual bool CheckJumpButton();
	virtual void HandleDuckingSpeedCrop();
	virtual bool LadderMove() OVERRIDE;
	virtual bool OnLadder( trace_t &trace ) OVERRIDE;
	virtual unsigned int PlayerSolidMask( bool brushOnly = false, CBasePlayer *pPlayerOverride = NULL ) OVERRIDE;
	virtual unsigned int LadderMask() const OVERRIDE { return 0x0200420Bu; }
	virtual float ClimbSpeed() const OVERRIDE;
	virtual void OnFinishWalkMove( bool bOnGround ) OVERRIDE;
#ifndef CLIENT_DLL
	void UpdateLedgeGrab();
	int TryLedgeGrab( const Vector &direction, float verticalStep, float probeDistance );
	int TryLungePushPlayerMove( CTerrorPlayer *other, Vector &position, Vector &velocity,
		const Vector *firstDestination, const trace_t *firstTrace );
	CUtlVector< CTerrorPlayer * > m_lungePushedPlayers;
#endif
	void MoveGround( const Vector &vecDestination, float flStandableNormal );
	void TryGroundMove( CTerrorPlayer *movingPlayer, const Vector &vecStart, const Vector &vecDestination, const Vector *pStep,
		float flStandableNormal, Vector &vecVelocity, trace_t &trace, ITraceFilter *filter, unsigned int mask );
	void TraceGroundMoveHull( const Vector &start, const Vector &end, const Vector &mins, const Vector &maxs,
		unsigned int mask, ITraceFilter *filter, trace_t &trace );
	void CheckAutoCrouch( const Vector &vecDestination, const trace_t &trace, bool bStepped, ITraceFilter *filter );
	void UpdateStandableNormal();
	CUtlVector< CBaseEntity * > m_groundMoveContacts; // Native paired raw contact list, distinct from lunge tracking.
	CTerrorPlayer *m_pTerrorPlayer;
	float m_flStandableNormal;
};

#endif // TERROR_GAMEMOVEMENT_H
