//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_INFECTED_H
#define L4D2_INFECTED_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/NextBot.h"
#include "l4d2/infected/common/zombiebot_locomotion.h"
#include "l4d2/infected/common/zombiebot_body.h"
#include "l4d2/infected/common/zombiebot_vision.h"
#include "l4d2/infected/common/infected_intention.h"
#include "utlvector.h"
#include "recipientfilter.h"

class Path;
class CTerrorPlayer;
template < typename Actor > class Action;

// The server keeps native animation layers for event dispatch and hitboxes.  This
// smaller presentation record is the original common-infected wire contract.
struct InfectedAnimationLayer
{
	DECLARE_CLASS_NOBASE( InfectedAnimationLayer );
	InfectedAnimationLayer() { Reset(); }
	void Reset()
	{
		m_nSequence = 0;
		m_flStartTime = 0.0f;
		m_nOrder = 6;
		m_bLooping = false;
		m_flPreviousCycle = 0.0f;
	}
	int m_nSequence;
	float m_flStartTime;
	int m_nOrder;
	bool m_bLooping;
	float m_flPreviousCycle;
};

class Infected : public NextBotCombatCharacter
{
public:
	DECLARE_CLASS( Infected, NextBotCombatCharacter );
	DECLARE_SERVERCLASS();

	Infected();
	virtual ~Infected();
	virtual void Precache() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	void OnLowViolence();
	virtual void Update() OVERRIDE;
	virtual void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void Event_Dying( const CTakeDamageInfo &info ) OVERRIDE;
	virtual int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void TraceAttack( const CTakeDamageInfo &info, const Vector &direction, trace_t *trace, CDmgAccumulator *accumulator = NULL ) OVERRIDE;
	virtual int BloodColor() OVERRIDE;
	virtual bool ShouldCollide( int collisionGroup, int contentsMask ) const OVERRIDE;
	virtual void OnSequenceChanged( int oldSequence ) OVERRIDE; // S401E10, real existing network clock.
	virtual Infected *MyInfectedPointer( void ) OVERRIDE { return this; }
	virtual int GetZombieClass() const OVERRIDE { return 0; }
	virtual const char *GetTerrorFootstepSoundName( const char *sound, bool right, float speed, bool force ) OVERRIDE;
	void SetActive( bool active );
	void SetAmbientMob() { m_bAmbientMob = true; } // Native3FDCF0, one-way.
	virtual bool IsCullable( int flags ) const { return true; } // Native3FDD00, distinct from Cull().
	virtual void OnHitByVomitJar( CBaseEntity *attacker ) OVERRIDE;
	virtual bool IsIT() const OVERRIDE;
	virtual void OnIgnite() OVERRIDE;
	virtual void Ignite( float lifetime, CBaseEntity *attacker ) OVERRIDE;
	virtual void UpdateOnRemove() OVERRIDE;
	void ApplyUncommonSpawnRules();
	void ApplyUncommonDifficultyHealth();
	void DropUncommonItems( const CTakeDamageInfo &info );
	bool IsRiotArmorFacingDamage( const CTakeDamageInfo &info );
	bool BlocksRiotBlood( const CTakeDamageInfo &info );
	void CreateUncommonChaseGoal();
	void RemoveUncommonChaseGoals();
	bool IsUncommon() const { return (unsigned int)( GetGender() - 11 ) <= 6; }
	bool IsShoved() const;
	bool HasStartedDying() const { return m_bHasStartedDying; }
	virtual Action< Infected > *CreateDyingAction( const CTakeDamageInfo &info );
	void UpdateCommonApproachSteering( CBaseEntity *victim, const Path *path );
	float GetFlowDistance() const;
	float GetWitchAttentionRemainingRatio() const { return m_witchAttentionRemainingRatio; }
	void SetWitchAttentionRemainingRatio( float value ) { m_witchAttentionRemainingRatio = value; }
	CRecipientFilter &GetVocalizationRecipients() { return m_vocalizationRecipients; }

	virtual ZombieBotLocomotion *GetLocomotionInterface() const OVERRIDE;
	virtual ZombieBotBody *GetBodyInterface() const OVERRIDE;
	virtual ZombieBotVision *GetVisionInterface() const OVERRIDE;
	virtual InfectedIntention *GetIntentionInterface() const OVERRIDE;
	virtual bool IsEnemy( const CBaseEntity *subject ) const OVERRIDE;
	virtual bool IsFriend( const CBaseEntity *subject ) const OVERRIDE;

	void FireHurtEvent( const CTakeDamageInfo &info );
	void PublishAnimationState();
	void PrintAnimationLayers( CTerrorPlayer *recipient ); // Native3FF600; real presentation layer owner.
	void Retire( const CTakeDamageInfo &info );
	bool HasRetired() const { return m_bRetired; }
	bool WasRecentlyShoved( float interval ) const { return m_lastShoveTimer.HasStarted() && m_lastShoveTimer.GetElapsedTime() < interval; }
	void RecordShove() { m_lastShoveTimer.Start(); }
	float GetActorRandomValue() const { return m_flActorRandomValue; }
	bool IsShoveImmune() const { return m_bShoveImmune; }
	int GetInfectedFlags() const { return m_nInfectedFlags; }
	bool CanBecomeActive() const { return ( m_nInfectedFlags & 0x8000 ) == 0; }
	virtual bool IsAbleToSee() const OVERRIDE { return ( GetInfectedFlags() & 0x2000 ) == 0; }
	void ClearInfectedFlags( int flags ) { m_nInfectedFlags &= ~flags; }
	void SetInfectedFlags( int flags ) { m_nInfectedFlags |= flags; }
	void ShoveKill( CBaseEntity *pusher );
	bool IsAmbush() const;
	bool WasShoveKilled() const { return m_bShoveKilled; }
	const char *GetWoundDeathActivity( int slot ) const;
	bool SetRequestedWound( int wound );
	void EmitVocalization( const char *sound, bool force = false );
	void SetShoveImmune( bool value ) { m_bShoveImmune = value; }
	const CUtlVector< EHANDLE > &GetNeighbors() const { return m_neighbors; }
	virtual bool Cull();
	void UpdateCullingVisibility();
	double GetTimeSincePotentiallyVisible() const;
	double GetTimeSinceInPlayerAAS() const;

	IMPLEMENT_NETWORK_VAR_FOR_DERIVED( m_fFlags );
	IMPLEMENT_NETWORK_VAR_FOR_DERIVED( m_nWaterLevel );

protected:
	virtual void CreateComponents() const;
	virtual void EmitTraceAttackBlood( float amount, const CTakeDamageInfo &info, const Vector &direction, trace_t *trace );
private:
	void UpdateNeighbors();
	void UpdatePopulation();
	void RecordHitGroup( const trace_t &trace );
	void SelectBulletWound( int hitbox, int hitgroup, bool fatal );
	bool SelectWoundByDamageType( int damageType, int hitbox );
	void SelectMeleeWound( int hitbox, int hitgroup, unsigned char damageType, bool front, int startDirection, int endDirection );
	void SelectExplosionWound();
	void TraceBleed( float damage, const Vector &direction, trace_t *trace, int damageType );

private:
	int m_nInfectedFlags;                                      // 7288, explicit ctor0
public:
	CNetworkVar( int, m_nFallenFlags );                         // 7292, explicit ctor0
private:
	CRecipientFilter m_vocalizationRecipients;                  // 7296, real constructor
protected:
	mutable ZombieBotLocomotion *m_locomotion;                  // 7328
	mutable ZombieBotBody *m_body;                             // 7332
	mutable InfectedIntention *m_intention;                     // 7336
	mutable ZombieBotVision *m_vision;                         // 7340
private:
	IntervalTimer m_potentiallyVisibleTimer;                    // 7344
	IntervalTimer m_inPlayerAASTimer;                          // 7352
	bool m_bIsInPlayerAAS;                                     // 7360, allocation0
public:
	CNetworkVar( bool, m_mobRush );                             // 7361, allocation0
private:
	bool m_bAmbientMob;                                       // 7362, allocation0
protected:
	IntervalTimer m_burnTimer;                                 // 7364, sole inherited timer
private:
	CountdownTimer m_populationCullTimer;                      // 7372, no Spawn reset
public:
	CNetworkVar( bool, m_bIsBurning );                          // 7384, allocation0
	CNetworkVar( float, m_sequenceStartTime );                  // 7388, allocation0
	CNetworkVector( m_gibbedLimbForce );                        // 7392, allocation0
	CNetworkVar( int, m_originalBody );                         // 7404, allocation0
private:
	float m_flActorRandomValue;                                // 7408, one ctor RNG draw
	int m_nLastHitGroupFrame;                                  // 7412, ctor0/Spawn0
public:
	CNetworkVar( int, m_iRequestedWound1 );                     // 7416, ctor-1 only
	CNetworkVar( int, m_iRequestedWound2 );                     // 7420, ctor-1 only
private:
	CountdownTimer m_vocalizationTimer;                        // 7424
	bool m_bShoveKilled;                                       // 7436, ctorfalse; ShoveKill terminal latch
	bool m_bHasStartedDying;                                   // 7437, ctorfalse only
public:
	CNetworkHandle( CBaseEntity, m_clientLookatTarget );         // 7440, invalid member then conditional raw0
	CNetworkVar( float, m_clientLeanYaw );                      // 7444, ctor0 only
	CUtlVector< InfectedAnimationLayer > m_clientAnimatedOverlay;// 7448, six records
private:
	CUtlVector< EHANDLE > m_neighbors;                         // 7468, real vector lifetime
	CountdownTimer m_neighborScanTimer;                        // 7488, no Spawn reset
	// Native four countdowns7500..7547 are write-only; see recorded full census.
	CNetworkVarEmbedded( CountdownTimer, m_itTimer );            // 7548, no own send/save property
	EHANDLE m_hITSource;                                       // 7560, default-invalid member; Spawn invalid
	bool m_bDecapitated;                                       // 7564, ctorfalse only
protected:
	float m_witchAttentionRemainingRatio;                      // 7568, allocation0; NO ctor/Spawn write
private:
	bool m_bShoveImmune;                                       // 7572, ctorfalse; temporary AllowBash gate

	// Retain these preexisting target-adaptation declarations, not new native fields.
	// Neither receives an explicit write in this Infected ctor or Spawn.
	IntervalTimer m_lastShoveTimer;
	bool m_bRetired;
};

void SetCalmExpression( Infected *me );
const unsigned int *GetInfectedColorPalette( int *count );

#endif // L4D2_INFECTED_H
