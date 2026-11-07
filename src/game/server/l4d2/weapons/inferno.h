//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_INFERNO_H
#define L4D2_INFERNO_H
#ifdef _WIN32
#pragma once
#endif

#include "baseentity.h"
#include "util_shared.h"

class CNavArea;

class CInferno : public CBaseEntity
{
public:
	DECLARE_CLASS( CInferno, CBaseEntity );
	friend class CTerrorPlayer; // S336010 directly writes the existing fire type.
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CInferno();
	virtual ~CInferno();
	virtual void Spawn();
	virtual void Precache();
	virtual int UpdateTransmitState();

	static CInferno *Create( CBaseEntity *pOwner, const Vector &position, const Vector &normal,
		const Vector &velocity, int nDepth = 0 );
	void StartBurning( const Vector &position, const Vector &normal, const Vector &velocity, int nDepth = 0 );
	bool IsTouching( CBaseEntity *pEntity, float flRadius, bool bCheckLOS ) const;
	bool IsTouching( const Vector &start, const Vector &end, Vector *pClosestPoint = NULL ) const;
	bool IsTouching( CNavArea *pArea ) const;
	bool Extinguish( CBaseEntity *pExtinguisher );
	void InfernoThink();
	void SetMaxFlames( int nMaxFlames ) { m_nMaxFlames = nMaxFlames; }
	void SetDamageCustom( int nDamageCustom ) { m_nDamageCustom = nDamageCustom; }

protected:
	enum { MAX_INFERNO_FIRES = 64 };
	virtual float GetFlameLifetime() const;
	virtual double GetDamagePerSecond() const;
	virtual int GetDamageType() const { return DMG_BURN; }
	virtual int GetDamageCustom() const { return m_nDamageCustom; }
	virtual bool CanHarm( CBaseEntity *pEntity ) const;
	virtual float GetSpreadInterval() const { return 0.0f; }
	virtual const char *GetLoopParticleName() const;
	enum FireType { FIRE = 0, ACID = 1, FIREWORKS = 2 };
	FireType m_fireType;
	virtual const char *GetStartParticleName() const;

private:
	struct FireInfo
	{
		Vector m_pos;
		Vector m_center;
		Vector m_normal;
		int m_nDepth;
		int m_nSpawnCredits;
		FireInfo *m_pParent;
		CountdownTimer m_spawnTimer;
		CountdownTimer m_spawnInterval;
		CountdownTimer m_lifetime;
		float m_flWaterOffset;
	};

	bool AddFire( const Vector &position, const Vector &normal, FireInfo *pParent, int nDepth );
	void Spread();
	void RecomputeBounds();
	void DamageEntities();
	void MarkNavAreas();

	CNetworkArray( int, m_fireXDelta, MAX_INFERNO_FIRES );
	CNetworkArray( int, m_fireYDelta, MAX_INFERNO_FIRES );
	CNetworkArray( int, m_fireZDelta, MAX_INFERNO_FIRES );
	CNetworkVar( int, m_fireCount );
	// Normals are server-only in the pinned DT_Inferno, despite the client reserving space for them.
	FireInfo m_fire[MAX_INFERNO_FIRES];
	float m_flFireHalfWidth;
	float m_flFireHeight;
	Vector m_mins;
	Vector m_maxs;
	CountdownTimer m_damageTimer;
	CountdownTimer m_damageRampTimer;
	Vector m_spreadVelocity;
	Vector m_initialPosition;
	IntervalTimer m_burnTimer;
	int m_nSpreadIndex;
	int m_nMaxFlames;
	CountdownTimer m_spreadTimer;
	int m_nDamageCustom;
	CUtlVector< CNavArea * > m_damagingAreas;
};

class CFireCrackerBlast : public CInferno
{
public:
	DECLARE_CLASS( CFireCrackerBlast, CInferno );
	DECLARE_SERVERCLASS();
	virtual void Spawn();
	static CFireCrackerBlast *Create( CBaseEntity *pOwner, const Vector &position,
		const Vector &normal, const Vector &velocity, int nDepth = 0 );
protected:
	virtual const char *GetLoopParticleName() const;
	virtual const char *GetStartParticleName() const;
};

#endif // L4D2_INFERNO_H