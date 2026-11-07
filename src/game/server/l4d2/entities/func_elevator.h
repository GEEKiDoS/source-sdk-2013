#ifndef FUNC_ELEVATOR_H
#define FUNC_ELEVATOR_H
#ifdef _WIN32
#pragma once
#endif

#include "basetoggle.h"
#include "entityoutput.h"

class CInfoElevatorFloor : public CBaseEntity
{
public:
	DECLARE_CLASS( CInfoElevatorFloor, CBaseEntity );
	DECLARE_DATADESC();
	COutputEvent m_OnReachedFloor;
};

struct FloorInfo
{
	float height;
	string_t name;
	EHANDLE button;
};

class CFuncElevator : public CBaseToggle
{
public:
	DECLARE_CLASS( CFuncElevator, CBaseToggle );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	// Native enables these existing derived-variable notifications. No new
	// anim/simulation/origin virtuals or movement-conditioned suppression.
	virtual void NetworkStateChanged_m_fFlags() OVERRIDE { NetworkStateChanged(); }
	virtual void NetworkStateChanged_m_fFlags( void * ) OVERRIDE { NetworkStateChanged(); }
	virtual void NetworkStateChanged_m_vecVelocity() OVERRIDE { NetworkStateChanged(); }
	virtual void NetworkStateChanged_m_vecVelocity( void * ) OVERRIDE { NetworkStateChanged(); }

	CFuncElevator();
	virtual ~CFuncElevator();
	virtual bool IsElevator() const OVERRIDE { return true; }
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual bool CreateVPhysics() OVERRIDE;
	virtual void PhysicsSimulate() OVERRIDE;
	virtual void MoveDone() OVERRIDE;
	virtual void Blocked( CBaseEntity *other ) OVERRIDE;
	virtual int DrawDebugTextOverlays() OVERRIDE;

	const FloorInfo *GetFloor( int index ) const;
	int GetNumFloors() const { return m_floors.Count(); }
	int GetCurrentFloor() const;
	int GetDestinationFloor() const;
	int GetNearestFloor( float z ) const;
	EHANDLE GetNearbyControlButton() const;
	bool HasEntity( CBaseEntity *entity ) const;
	void CollectOccupants( CUtlVector< CBasePlayer * > *occupants, int team = TEAM_ANY ) const;
	double CalcPosition( const char *context, float time, float *speed ) const;
	double UpdateCurrentPosition( const char *context );
	void MoveTo( float z );
	void InputMoveToFloor( inputdata_t &inputdata );
	void InputDisable( inputdata_t &inputdata );
	void InputSetMaxSpeed( inputdata_t &inputdata );
	void StopMoveSoundThink();

private:
	static int FloorCompare( const FloorInfo *a, const FloorInfo *b );
	void BuildFloorList();
	void EmitMoveSound( string_t sound );

	string_t m_soundStart;
	string_t m_soundStop;
	string_t m_soundDisable;
	string_t m_currentSound;
	float m_flBlockDamage;
	Vector m_topFloorPosition;
	Vector m_bottomFloorPosition;
	CNetworkVar( float, m_maxSpeed );
	CNetworkVar( float, m_acceleration );
	CNetworkVar( float, m_movementStartTime );
	CNetworkVar( float, m_movementStartSpeed );
	CNetworkVar( float, m_movementStartZ );
	CNetworkVar( float, m_destinationFloorPosition );
	CNetworkVar( bool, m_isMoving );
	float m_currentSpeed;
	EHANDLE m_destinationFloor;
	bool m_enabled;
	float m_stopTime;
	COutputEvent m_OnReachedTop;
	COutputEvent m_OnReachedBottom;
	CUtlVector< FloorInfo > m_floors;
};

int GetFuncElevatorCount();
CFuncElevator *GetFuncElevator( int index );

#endif
