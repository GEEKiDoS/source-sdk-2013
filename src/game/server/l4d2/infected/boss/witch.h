//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WITCH_H
#define L4D2_WITCH_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/infected/common/infected.h"

class WitchLocomotion;
class WitchBody;
class WitchIntention;
class WitchVision;

// The original Witch is an Infected, not an infected player or a common model
// variant. Its Action hierarchy deliberately remains Action<Infected>.
class Witch : public Infected
{
public:
	DECLARE_CLASS( Witch, Infected );
	DECLARE_SERVERCLASS();
	friend class CInfoZombieSpawn; // S29C100's sole SAME7600 pending-harasser writer.

	Witch();
	virtual ~Witch();
	virtual void Precache() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual int GetZombieClass() const OVERRIDE;
	virtual bool ShouldCollide( int collisionGroup, int contentsMask ) const OVERRIDE;
	virtual bool IsCullable( int flags ) const OVERRIDE { return false; } // Native41AF30.
	virtual bool Cull() OVERRIDE { return false; } // Native41AF40: separate culling dispatch.
	virtual int OnTakeDamage_Alive( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void OnModelChanged() OVERRIDE;

	float GetRage() const { return m_rage; }
	float GetWanderRage() const { return m_wanderrage; }
	void ChangeRage( float amount );
	void SetWanderRage( float rage );
	void SetHarasser( CBaseEntity *harasser );
	CBaseEntity *GetHarasser() const { return m_harasser.Get(); }
	void StartRageRamp( float duration );
	float GetTimeSinceIgnited() const { return m_burnTimer.GetElapsedTime(); }
	bool IsHostileTo( CBaseEntity *subject ) const;
	bool Slash( CBaseEntity *victim );

protected:
	virtual void CreateComponents() const OVERRIDE;
	virtual Action< Infected > *CreateDyingAction( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void EmitTraceAttackBlood( float amount, const CTakeDamageInfo &info, const Vector &direction, trace_t *trace ) OVERRIDE;

private:
	CNetworkVar( float, m_rage );
	CNetworkVar( float, m_wanderrage );
	int m_ragePoseParameter;
	float m_rageRampStart;
	float m_rageRampDuration;
	EHANDLE m_harasser;
	EHANDLE m_pendingHarasser;
	float m_firstDamageTime;
	CUtlVector< int > m_damageUserIDs;
	int m_survivorDamageTypeMask;
};

const CUtlVector< Witch * > &GetWitchList();
void ClearWitchList();

#endif // L4D2_WITCH_H
