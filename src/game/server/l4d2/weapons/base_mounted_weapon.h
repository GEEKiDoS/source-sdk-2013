//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef BASE_MOUNTED_WEAPON_H
#define BASE_MOUNTED_WEAPON_H
#ifdef _WIN32
#pragma once
#endif

#include "props.h"

class CTerrorPlayer;

// Original CBaseMountedWeapon derives from the dynamic prop, not a carried gun.
class CBaseMountedWeapon : public CDynamicProp
{
public:
	DECLARE_CLASS( CBaseMountedWeapon, CDynamicProp );
	DECLARE_SERVERCLASS();

	CBaseMountedWeapon();
	virtual bool ShouldCollide( int collisionGroup, int contentsMask ) const OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;
	virtual bool OnControls( CBaseEntity *pControls ) OVERRIDE;
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	CTerrorPlayer *GetUser() const { return m_owner.Get(); }
	bool IsFiring() const { return m_firing; }
	bool IsOverheated() const { return m_overheated; }
	float GetHeat() const { return m_heat; }
	const QAngle &GetInitialAngles() const { return m_initialAngles; }
	void GetControlAngles( QAngle *pInitial, float *pMinYaw, float *pMaxYaw, float *pMinPitch, float *pMaxPitch ) const;
	virtual void GetUserPosition( Vector *pPosition );

protected:
	void InitializeMountedWeapon();
	void UpdateParentAngles();
	void LookupMountedPoseParameters();
	void UpdateMountedPose( float interval );
	float ApproachPoseParameter( int index, float current, float target, float speed, float interval );
	void RestoreUserWeapon( CTerrorPlayer *pPlayer );

	CNetworkQAngle( m_initialAngles );
	CNetworkHandle( CTerrorPlayer, m_owner );
	CNetworkVar( float, m_maxYaw );
	CNetworkVar( float, m_maxPitch );
	CNetworkVar( float, m_minPitch );
	CNetworkVar( bool, m_firing );
	CNetworkVar( bool, m_overheated );
	CNetworkVar( float, m_heat );

	QAngle m_parentAngles;
	float m_lastFireTime;
	float m_lastAngleThink;
	float m_spinSpeed;
	int m_horizontalPose;
	int m_verticalPose;
	float m_horizontalAngle;
	float m_verticalAngle;
};

#endif // BASE_MOUNTED_WEAPON_H
