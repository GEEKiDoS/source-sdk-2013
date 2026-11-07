//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_CAR_ALARM_H
#define L4D2_CAR_ALARM_H
#ifdef _WIN32
#pragma once
#endif

#include "props.h"
#include "GameEventListener.h"
#include "util_shared.h"

// Native prop_car_alarm: CPhysicsProp plus a weapon_fire listener. It inherits
// Spawn/Precache/Activate and the PhysicsProp server class; there is no Car DT.
class CCarProp : public CPhysicsProp, private CGameEventListener
{
	DECLARE_CLASS( CCarProp, CPhysicsProp );
	DECLARE_DATADESC();
public:
	CCarProp();
	~CCarProp() OVERRIDE = default;
	void UpdateOnRemove() OVERRIDE;
	int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;
	void FireGameEvent( IGameEvent *event ) OVERRIDE;

	void InputEnable( inputdata_t &inputdata );
	void InputDisable( inputdata_t &inputdata );
	void InputSurvivorStandingOnCar( inputdata_t &inputdata );
	void TriggerAlarm( CBaseEntity *activator ); // native19E4E0

private:
	void CCarPropAlarmEndThink();
	void CCarPropChirpStartThink();
	void CCarPropChirpEndThink();
	void CCarPropAlarmTouch( CBaseEntity *other );

	// Actual member-construction order after the two native bases.
	COutputEvent m_OnCarAlarmStart; // native5772
	COutputEvent m_OnCarAlarmEnd; // native5796
	COutputEvent m_OnCarAlarmChirpStart; // native5820
	COutputEvent m_OnCarAlarmChirpEnd; // native5844
	bool m_bChirpPending; // native5868; allocation0, not a ctor store
	bool m_bAlarmTriggered; // native5869; allocation0, one-shot true producer
	CountdownTimer m_AlarmTimer; // native5872
	CountdownTimer m_ChirpTimer; // native5884
	bool m_bAlarmSuppressed; // native5896; Tank damage disables an untriggered alarm
	bool m_bDisabled; // native5897; genuine startdisabled key/input state
};

// Native prop_car_glass has exactly DynamicProp's state/layout. Its genuine
// damage override notifies a parent car BEFORE normal breakable-prop damage.
class CCarGlassProp : public CDynamicProp
{
	DECLARE_CLASS( CCarGlassProp, CDynamicProp );
public:
	int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;
};

#endif // L4D2_CAR_ALARM_H
