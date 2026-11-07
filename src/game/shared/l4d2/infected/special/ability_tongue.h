//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ABILITY_TONGUE_H
#define L4D2_ABILITY_TONGUE_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/base_ability.h"
#ifdef CLIENT_DLL
#include "particles_new.h"
#define CTongue C_Tongue
#endif

// The tongue is the ability entity itself, not a projectile or a weapon.
class CTongue : public CBaseAbility
{
public:
	DECLARE_CLASS( CTongue, CBaseAbility );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CTongue();
	virtual bool CanActivate() OVERRIDE;
	virtual bool ActivateAbility() OVERRIDE;
	virtual void SetOwner( CTerrorPlayer *owner ) OVERRIDE;
	virtual bool IsReadyToActivate() OVERRIDE;
	virtual int GetActivationButton() const OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void OnRemove() OVERRIDE;
	CTerrorPlayer *GetPotentialTarget() const;
	bool IsTongueExtended() const;
#ifdef CLIENT_DLL
	bool IsPastTongueLaunchPhase() const; // C28E730, distinct from state!=0.
#endif
	Vector GetPullPosition( bool absolute = false ) const;
	const Vector &GetPullGroundPosition();
	const Vector &GetTipPosition() const { return m_tipPosition; }
	int GetSegmentCount() const;
	Vector GetSegmentStart( int segment );
	Vector GetSegmentEnd( int segment );
#ifdef CLIENT_DLL
	virtual ~CTongue();
	virtual void UpdateOnRemove() OVERRIDE;
	virtual void NotifyShouldTransmit( ShouldTransmitState_t state ) OVERRIDE;
	virtual bool Simulate() OVERRIDE;
	virtual void OnParticleEffectDeleted( CNewParticleEffect *effect );
	virtual float GetRechargeProgress() const OVERRIDE;
	virtual const char *GetCooldownBackground() const OVERRIDE;
	virtual const char *GetCooldownForeground() const OVERRIDE;
private:
	void UpdateTongueEffects();
	void StopTongueEffects();
	void DropTongueEffects();
	int m_lastTongueState;
	HPARTICLEFFECT m_tongueEffects[11];
	Vector m_effectStarts[11], m_effectEnds[11];
#else
	DECLARE_DATADESC();
	virtual int ShouldTransmit( const CCheckTransmitInfo *info ) OVERRIDE;
	void ReplacePlayerReferences( CTerrorPlayer *oldPlayer, CTerrorPlayer *newPlayer );
	virtual void RemoveAbility() OVERRIDE;
	virtual void OnTouch( CBaseEntity *entity ) OVERRIDE;
	virtual void OnOwnerDamaged( const CTakeDamageInfo &info ) OVERRIDE;
	const Vector &GetGrabPosition() const { return m_grabPosition; }
	void ResetTongueCooldown();
	float GetTongueCooldown() const;
	void TakeDamage( CBasePlayer *attacker, float damage, int damageType, const Vector &hitPosition, const Vector &direction );
private:
	void ChangeState( int state );
	void UpdatePotentialTarget();
	void EnterReady();
	void EnterTraveling();
	void EnterAttached();
	void EnterDropping();
	void UpdateMiss();
	void UpdateTraveling();
	void UpdateAttached();
	void UpdateDropping();
	void LeaveTraveling();
	void LeaveAttached();
	bool AddBend( const Vector &victimPosition );
	bool RemoveBend();
	void UpdateVerticalChoke();
	bool UpdateGroundChoke();
	void Debug( const char *format, ... );
	virtual void DebugVictim( CTerrorPlayer *victim, const char *format, ... ) OVERRIDE; // S36BB80.
	float m_tongueHealth;
	int m_draggedCharacter, m_chokedCharacter;
	Vector m_launchPosition, m_targetPosition, m_previousTargetPosition;
	Vector m_tipVelocity, m_grabPosition;
	IntervalTimer m_stateTimer;
#endif
	CNetworkVar( int, m_tongueGrabStartingHealth );
	CNetworkVar( float, m_tongueHitRange );
	CNetworkVar( float, m_tongueHitTimestamp );
	CNetworkVar( bool, m_tongueHitWasAmbush );
	CNetworkVar( float, m_tongueVictimDistance );
	CNetworkVar( float, m_tongueVictimPositionTime );
	CNetworkVar( float, m_tongueVictimLastOnGroundTime );
	CNetworkHandle( CBaseEntity, m_potentialTarget );
	CNetworkHandle( CBaseEntity, m_currentTipTarget );
	CountdownTimer m_targetScanTimer;
	IntervalTimer m_noProgressTimer, m_lostLineOfSightTimer;
	CNetworkVar( int, m_tongueState );
	CNetworkArray( Vector, m_bendPositions, 10 );
	CNetworkVar( int, m_bendPointCount );
	CNetworkVector( m_tipPosition );
	Vector m_groundTracePosition, m_groundTraceResult;
};
#endif // L4D2_ABILITY_TONGUE_H
