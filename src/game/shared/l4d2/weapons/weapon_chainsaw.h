//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WEAPON_CHAINSAW_H
#define L4D2_WEAPON_CHAINSAW_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/terror_weapon.h"
#ifdef CLIENT_DLL
#include "particles_new.h"
#define CChainsaw C_Chainsaw
#endif

class CSoundPatch;
class CBaseViewModel;

// This is a continuous-contact weapon, not a CTerrorMeleeWeapon swing.
class CChainsaw : public CTerrorWeapon
{
public:
	DECLARE_CLASS( CChainsaw, CTerrorWeapon );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	CChainsaw();
	virtual ~CChainsaw();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual bool Deploy() OVERRIDE;
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL ) OVERRIDE;
	virtual void Drop( const Vector &velocity ) OVERRIDE;
	virtual void UpdateOnRemove() OVERRIDE;
	virtual void ItemPostFrame() OVERRIDE;
	virtual void PrimaryAttack() OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE;
	virtual void WeaponIdle() OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_CHAINSAW; }
	virtual Activity GetDeployActivity() OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE { return ACT_INVALID; }
	virtual int GetViewModelLayer( Activity activity ) OVERRIDE;
	virtual acttable_t *ActivityList( int &count ) OVERRIDE;
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
	virtual int ObjectCaps() OVERRIDE;
#else
	virtual bool Simulate() OVERRIDE;
#endif

	enum State
	{
		INACTIVE = 0,
		STARTUP = 1,
		IDLE = 2,
		ACTIVE = 3
	};

	CNetworkVar( int, m_iState );
	CNetworkVar( bool, m_bHitting );
	CNetworkArray( float, m_PoseParameterTargets, 2 );

private:
	void CacheOwnerAndViewModel();
	void UpdatePoseParameters();
	void DisplayDebug( bool serverLane );
	void StartSound( CSoundPatch *&sound, const char *name );
	void StopSound( CSoundPatch *&sound, float fadeTime = 0.0f );
	void StopAllSounds();
	void EnterIdle();
#ifndef CLIENT_DLL
	void UpdateActive();
	void TraceContact();
	void NotifyInfected();
#else
	void UpdateParticles();
	void RemoveParticles();
	void UpdateBloodDecals();
	HPARTICLEFFECT m_hBloodFirstPerson;
	HPARTICLEFFECT m_hBloodThirdPerson;
	HPARTICLEFFECT m_hSmokeFirstPerson;
	HPARTICLEFFECT m_hSmokeThirdPerson;
	CountdownTimer m_bloodDecalTimer;
#endif

	bool m_bDeployed;
	float m_flAmmoUseAccum;
	float m_flHitIntervalAccum;
	CSoundPatch *m_pStartSound;
	CSoundPatch *m_pIdleSound;
	CSoundPatch *m_pActiveSound;
	CSoundPatch *m_pStopSound;
	CHandle< CTerrorPlayer > m_hChainsawOwner;
	CHandle< CBaseViewModel > m_hChainsawViewModel;
	int m_nPitch;
	CountdownTimer m_pitchDownTimer;
	float m_flNextGoreSound;
	float m_PoseParameters[2];
	int m_nPoseParameterIndices[2];
#ifndef CLIENT_DLL
	CountdownTimer m_attractTimer;
	CountdownTimer m_usedTimer;
#endif
};

#endif // L4D2_WEAPON_CHAINSAW_H
