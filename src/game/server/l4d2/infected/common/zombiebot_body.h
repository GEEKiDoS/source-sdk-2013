//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ZOMBIEBOT_BODY_H
#define L4D2_ZOMBIEBOT_BODY_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/NextBotInterface.h"
#include "NextBot/NextBotBodyInterface.h"

class Infected;

// Sequence/event ownership remains on the server. The actor publishes the
// resulting animation state; received client events never apply melee damage.
class ZombieBotBody : public IBody
{
public:
	DECLARE_CLASS( ZombieBotBody, IBody );

	explicit ZombieBotBody( Infected *infected );
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void OnModelChanged() OVERRIDE;
	virtual void OnInjured( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void OnAnimationEvent( animevent_t *event ) OVERRIDE;

	virtual const Vector &GetEyePosition() const OVERRIDE;
	virtual const Vector &GetViewVector() const OVERRIDE;
	virtual CBaseEntity *GetEntity() OVERRIDE { return GetBot()->GetEntity(); }
	virtual void AimHeadTowards( const Vector &lookAtPos, LookAtPriorityType priority = BORING,
		float duration = 0.0f, INextBotReply *replyWhenAimed = NULL, const char *reason = NULL );
	virtual void AimHeadTowards( CBaseEntity *subject, LookAtPriorityType priority = BORING,
		float duration = 0.0f, INextBotReply *replyWhenAimed = NULL, const char *reason = NULL );
	virtual float GetMaxHeadAngularVelocity() const OVERRIDE;
	virtual bool IsHeadSteady() const OVERRIDE;

	virtual bool StartActivity( Activity activity, unsigned int flags = 0 ) OVERRIDE;
	virtual int SelectAnimationSequence( Activity activity ) const OVERRIDE;
	virtual Activity GetActivity() const OVERRIDE;
	virtual bool IsActivity( Activity activity ) const OVERRIDE;
	virtual bool HasActivityType( unsigned int flags ) const OVERRIDE;

	virtual void SetDesiredPosture( PostureType posture ) OVERRIDE;
	virtual PostureType GetDesiredPosture() const OVERRIDE;
	virtual bool IsDesiredPosture( PostureType posture ) const OVERRIDE;
	virtual bool IsInDesiredPosture() const OVERRIDE;
	virtual PostureType GetActualPosture() const OVERRIDE;
	virtual bool IsActualPosture( PostureType posture ) const OVERRIDE;
	virtual bool IsPostureMobile() const OVERRIDE;
	virtual bool IsPostureChanging() const OVERRIDE;
	virtual void SetArousal( ArousalType arousal ) OVERRIDE;
	virtual ArousalType GetArousal() const OVERRIDE;
	virtual bool IsArousal( ArousalType arousal ) const OVERRIDE;

private:
	void UpdateCollisionBounds();
	void UpdateMotionPoseParameters();
	Activity GetPostureTransition( PostureType posture ) const;
	bool IsPostureTransition( Activity activity ) const;
	bool IsLandingActivity( Activity activity ) const;
	bool IsTransitoryActivity( Activity activity ) const;
	Activity GetPostureIdle() const;

	Infected *m_infected;
	Activity m_activity;
	unsigned int m_activityFlags;
	Activity m_lastActivity;
	unsigned int m_lastActivityFlags;
	PostureType m_desiredPosture;
	PostureType m_actualPosture;
	ArousalType m_arousal;
	bool m_isPostureChanging;
	int m_forwardAttachment;
	int m_moveXPose;
	int m_moveYPose;
	int m_bodyYawPose;
	int m_bodyPitchPose;
	int m_leanYawPose;
	int m_leanPitchPose;
	Vector m_eyeOffset;
	Vector m_viewVector;
	mutable Vector m_eyePosition;
	CountdownTimer m_eyeUpdateTimer;
protected:
	CountdownTimer m_shotVocalizeTimer;
};

#endif // L4D2_ZOMBIEBOT_BODY_H
