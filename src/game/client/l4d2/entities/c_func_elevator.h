#ifndef C_FUNC_ELEVATOR_H
#define C_FUNC_ELEVATOR_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseentity.h"

class C_FuncElevator : public C_BaseEntity
{
public:
bool IsMoving() const;

	DECLARE_CLASS( C_FuncElevator, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
	C_FuncElevator();
	virtual ~C_FuncElevator();
	virtual bool IsElevator() const OVERRIDE { return true; }
	virtual const Vector &GetRenderOrigin() OVERRIDE;
	virtual bool Interpolate( float currentTime ) OVERRIDE;
	virtual void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
	double CalcPosition( const char *context, float time, float *speed ) const;
	float GetCurrentSpeed() const { return m_currentSpeed; }
	float GetCachedInterpolationAmount() const { return m_interpolationAmount; } // C63A20 reads native1636.

private:
	float GetRenderTime() const;
	Vector m_renderOrigin;
	float m_destinationFloorPosition;
	float m_maxSpeed;
	float m_acceleration;
	float m_currentSpeed;
	float m_movementStartTime;
	float m_movementStartSpeed;
	float m_movementStartZ;
	bool m_isMoving;
	float m_interpolationAmount;
};

int GetClientFuncElevatorCount();
C_FuncElevator *GetClientFuncElevator( int index );

#endif
