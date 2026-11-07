//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ABILITY_VOMIT_H
#define L4D2_ABILITY_VOMIT_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/base_ability.h"
#ifdef CLIENT_DLL
#define CVomit C_Vomit
#endif

extern ConVar z_vomit_interval;
extern ConVar z_vomit_duration;
extern ConVar z_vomit_fatigue;
extern ConVar z_vomit_range;
extern ConVar z_vomit_slide_rate;
extern ConVar z_vomit_slide_mult;
extern ConVar z_vomit_debug;

class CVomit : public CBaseAbility
{
public:
	DECLARE_CLASS( CVomit, CBaseAbility );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CVomit();
	virtual void SetOwner( CTerrorPlayer *owner ) OVERRIDE;
	virtual bool CanActivate() OVERRIDE;
	virtual bool ActivateAbility() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual int GetActivationButton() const OVERRIDE;
	virtual bool IsActive() const OVERRIDE;
#ifdef CLIENT_DLL
	virtual float GetRechargeProgress() const OVERRIDE;
	virtual const char *GetCooldownBackground() const OVERRIDE;
	virtual const char *GetCooldownForeground() const OVERRIDE;
	bool CollectPotentialTargets( CUtlVector< CTerrorPlayer * > *targets );
#else
	virtual void Precache() OVERRIDE;
	virtual void StopActivation( float duration ) OVERRIDE;
	virtual unsigned int PhysicsSolidMaskForEntity() const OVERRIDE;
	void StartVomitEffect();
	void StopVomitEffect();
#endif
private:
	CNetworkVar( bool, m_isSpraying );
	CNetworkVarEmbedded( CountdownTimer, m_attackDuration );
	CNetworkVarEmbedded( CountdownTimer, m_nextSpray );
};
#endif // L4D2_ABILITY_VOMIT_H
