#ifndef TERROR_RAGDOLL_RETIREMENT_H
#define TERROR_RAGDOLL_RETIREMENT_H
#ifdef _WIN32
#pragma once
#endif
// Included in ragdoll_shared.h in place of the SDK two-list class for L4D2.
enum RagdollType_t
{
	RAGDOLL_TYPE_GIB = 0,
	RAGDOLL_TYPE_GENERIC,
	RAGDOLL_TYPE_SPECIAL,
	RAGDOLL_TYPE_BOSS,
	RAGDOLL_TYPE_SURVIVOR,
	RAGDOLL_TYPE_COUNT
};
class CRagdollLRURetirement : public CAutoGameSystemPerFrame
{
public:
	CRagdollLRURetirement( const char *name );
	void LevelInitPreEntity() OVERRIDE;
	virtual void Update( float frametime );
	virtual void FrameUpdatePostEntityThink();
	void MoveToTopOfLRU( CBaseAnimating *ragdoll, int category );
	void SetMaxRagdollCount( int count ) { m_iMaxRagdolls = count; }
	int CountRagdolls( bool simulating ) { return simulating ? m_iSimulatedRagdollCount : m_iRagdollCount; }
#ifdef CLIENT_DLL
	void AddRagdollFader( CBaseEntity *fader );
	void RemoveRagdollFader( CBaseEntity *fader );
	bool ShouldSkipRagdoll( CBaseAnimating *source );
#endif
private:
	void RetireRagdolls( int category, bool keepAwake, bool overallLimit );
	typedef CHandle< CBaseAnimating > CRagdollHandle;
	CUtlLinkedList< CRagdollHandle > m_LRU[RAGDOLL_TYPE_COUNT];
#ifdef CLIENT_DLL
	CUtlVector< EHANDLE > m_Faders;
#endif
	int m_iMaxRagdolls;
	int m_iSimulatedRagdollCount;
	int m_iRagdollCount;
	int m_iRemovedThisFrame;
#ifdef CLIENT_DLL
	float m_flLastRealTime;
	float m_flLastFPS;
	float m_flFPSMultiplier;
	Vector m_vecViewForward;
	float m_flViewNearDistance;
#endif
};
#endif
