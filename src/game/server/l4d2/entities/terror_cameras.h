//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_CAMERAS_H
#define TERROR_CAMERAS_H
#ifdef _WIN32
#pragma once
#endif

#include "baseentity.h"
#include "utlvector.h"

class CBasePlayer;

// This is the original shared base of the two L4D2 moving cameras.  It is
// deliberately NOT a second implementation or subclass of CTriggerCamera.
class CMoveableCamera : public CBaseEntity
{
public:
	DECLARE_CLASS( CMoveableCamera, CBaseEntity );
	CMoveableCamera();
	virtual void Spawn() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;

	virtual void Enable() = 0;
	virtual void Disable() = 0;

protected:
	virtual bool GetTargetPosition( CBaseEntity *target, Vector &position ) = 0;
	virtual double GetPositionFraction( float fraction ) = 0;
	void StartMovement();
	void FollowTarget();
	void Move();

	bool m_bEnabled;                  // Native1068, initialized in Spawn.
	bool m_bDisableAfterMove;         // Native1069, Spawn captures spawnflag1.
	bool m_bMoving;                   // Native1070, not reset by Disable.
	Vector m_vecStartPosition;        // Native1072..1083, allocation zero.
	QAngle m_angStartAngles;          // Native1084..1095, allocation zero.
	float m_flStartTime;              // Native1096, allocation zero.
	float m_flInterpTime;             // Native1100, constructor1.
	EHANDLE m_hTarget;                // Native1104, constructor invalid.
};

class CTriggerCameraMultiplayer : public CMoveableCamera
{
public:
	DECLARE_CLASS( CTriggerCameraMultiplayer, CMoveableCamera );
	DECLARE_DATADESC();
	void Spawn() OVERRIDE;
	CTriggerCameraMultiplayer();
	void Enable() OVERRIDE;
	void Disable() OVERRIDE;
	void InputEnable( inputdata_t &inputdata );
	void InputDisable( inputdata_t &inputdata );
	void InputAddPlayer( inputdata_t &inputdata );
	void InputRemovePlayer( inputdata_t &inputdata );
	void InputStartMovement( inputdata_t &inputdata );

protected:
	bool GetTargetPosition( CBaseEntity *target, Vector &position ) OVERRIDE;
	double GetPositionFraction( float fraction ) OVERRIDE;

private:
	void AddPlayer( CBasePlayer *player );
	void RemovePlayer( CBasePlayer *player );
	CUtlVector< EHANDLE > m_players;  // Native1108, not saved.
	float m_fov;                      // Native1128, constructor90.
	float m_fovSpeed;                 // Native1132, constructor1.
	// Native1136..1139 are allocation-zero, unreferenced alignment/tail bytes.
	string_t m_targetEntName;         // Native1140, allocation zero.
};

class CSurvivorCamera : public CMoveableCamera
{
public:
	void Spawn() OVERRIDE;
	DECLARE_CLASS( CSurvivorCamera, CMoveableCamera );
	DECLARE_DATADESC();
	CSurvivorCamera();
	void Enable() OVERRIDE;
	void Disable() OVERRIDE;
	void InputEnable( inputdata_t &inputdata );
	void InputDisable( inputdata_t &inputdata );
	void InputStartMovement( inputdata_t &inputdata );

protected:
	bool GetTargetPosition( CBaseEntity *target, Vector &position ) OVERRIDE;
	double GetPositionFraction( float fraction ) OVERRIDE;

private:
	EHANDLE m_hPlayer;                // Native1108, constructor invalid, not saved.
	float m_fov;                      // Native1112, constructor90.
	float m_fovSpeed;                 // Native1116, constructor1.
};

class CDeathFallCamera : public CBaseEntity
{
public:
	DECLARE_CLASS( CDeathFallCamera, CBaseEntity );
	DECLARE_DATADESC();
	CDeathFallCamera();
	void Spawn() OVERRIDE;
	int UpdateTransmitState() OVERRIDE;
	void InputEnable( inputdata_t &inputdata );

private:
	void Enable( CBasePlayer *player );
	float m_fov;                      // Native1068, constructor90.
	float m_fovSpeed;                 // Native1072, constructor1.
};

class CInfoViewParameters : public CBaseEntity
{
public:
	DECLARE_CLASS( CInfoViewParameters, CBaseEntity );
	DECLARE_DATADESC();
private:
	int m_nViewMode;
};

#endif // TERROR_CAMERAS_H
