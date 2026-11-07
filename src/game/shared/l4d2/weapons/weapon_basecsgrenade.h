//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WEAPON_BASECSGRENADE_H
#define L4D2_WEAPON_BASECSGRENADE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/terror_weapon.h"

#ifdef CLIENT_DLL
#define CBaseCSGrenade C_BaseCSGrenade
#endif

class CBaseCSGrenade : public CTerrorWeapon
{
public:
	DECLARE_CLASS( CBaseCSGrenade, CTerrorWeapon );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif

	CBaseCSGrenade();
	virtual bool Deploy() OVERRIDE;
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL ) OVERRIDE;
	virtual bool AllowsAutoSwitchFrom() const OVERRIDE;
	virtual void PrimaryAttack() OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE;
	virtual bool Reload() OVERRIDE;
	virtual void ItemPostFrame() OVERRIDE;
	virtual bool CanDisplayHelpingHand() const OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity TranslatePlayerActivity( Activity activity ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;

	// Original v446/v447: separate pull-back and release animation selectors.
	Activity GetPlayerGrenadeActivity( Activity movement, bool bRelease ) const;
	bool IsPinPulled() const { return m_bPinPulled; }
	float GetThrowTime() const { return m_fThrowTime; }
	virtual void StartGrenadeThrow();
	virtual void ThrowGrenade();
	virtual void DropGrenade();
	void OnPickedUpGrenade();

protected:
#ifndef CLIENT_DLL
	virtual void EmitGrenade( const Vector &position, const QAngle &angles, const Vector &velocity,
		const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower ) = 0;
#endif
	CNetworkVar( bool, m_bRedraw );
	CNetworkVar( bool, m_bPinPulled );
	CNetworkVar( float, m_fThrowTime );
	CNetworkVar( bool, m_isGoingToRoll );
	CNetworkVar( bool, m_bPinPullStarted );
	bool m_bHasBeenPickedUp;
};

#endif // L4D2_WEAPON_BASECSGRENADE_H
