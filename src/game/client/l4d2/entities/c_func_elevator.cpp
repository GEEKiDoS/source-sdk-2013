#include "cbase.h"
#include "l4d2/entities/c_func_elevator.h"
#include "l4d2/entities/func_elevator_shared.h"
#include "c_baseplayer.h"
#include "physics_shared.h"
#include "vphysics_interface.h"
#include "physics.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_FuncElevator, DT_FuncElevator, CFuncElevator )
	RecvPropVector( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ) ),
	RecvPropFloat( RECVINFO( m_acceleration ) ),
	RecvPropFloat( RECVINFO( m_currentSpeed ) ),
	RecvPropFloat( RECVINFO( m_movementStartTime ) ),
	RecvPropFloat( RECVINFO( m_movementStartSpeed ) ),
	RecvPropFloat( RECVINFO( m_movementStartZ ) ),
	RecvPropFloat( RECVINFO( m_destinationFloorPosition ) ),
	RecvPropFloat( RECVINFO( m_maxSpeed ) ),
	RecvPropBool( RECVINFO( m_isMoving ) ),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA_NO_BASE( C_FuncElevator )
	DEFINE_PRED_FIELD_TOL( m_vecNetworkOrigin.x, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.125f ),
	DEFINE_PRED_FIELD_TOL( m_vecNetworkOrigin.y, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.125f ),
	DEFINE_PRED_FIELD_TOL( m_vecNetworkOrigin.z, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.125f ),
	DEFINE_PRED_FIELD_TOL( m_acceleration, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.125f ),
	DEFINE_PRED_FIELD_TOL( m_currentSpeed, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.125f ),
END_PREDICTION_DATA()

static ConVar cl_elevator_physics( "cl_elevator_physics", "1", 0x0000000A );
static CUtlVector< C_FuncElevator * > s_elevators;

int GetClientFuncElevatorCount()
{
	return s_elevators.Count();
}

C_FuncElevator *GetClientFuncElevator( int index )
{
	return s_elevators.IsValidIndex( index ) ? s_elevators[index] : NULL;
}

C_FuncElevator::C_FuncElevator()
{
	s_elevators.AddToTail( this );
}

C_FuncElevator::~C_FuncElevator()
{
	s_elevators.FindAndRemove( this );
}

double C_FuncElevator::CalcPosition( const char *context, float time, float *speed ) const
{
	*speed = 0.0f;
	// Unlike the server's >= admission, the client's original test is <.
	if ( !m_isMoving || gpGlobals->curtime < m_movementStartTime ) return GetAbsOrigin().z;
	return ComputeElevatorPosition( time - m_movementStartTime, m_movementStartZ,
		m_movementStartSpeed, m_destinationFloorPosition, m_maxSpeed, m_acceleration, speed );
}

float C_FuncElevator::GetRenderTime() const
{
	float time = gpGlobals->curtime - m_interpolationAmount;
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player )
	{
		time = player->GetFinalPredictedTime() - gpGlobals->interval_per_tick;
		if ( !engine->IsPaused() )
			time = gpGlobals->interpolation_amount * gpGlobals->interval_per_tick + time;
	}
	return time;
}

const Vector &C_FuncElevator::GetRenderOrigin()
{
	m_renderOrigin = BaseClass::GetRenderOrigin();
	float speed;
	m_renderOrigin.z = CalcPosition( "GetRenderOrigin", GetRenderTime(), &speed );
	return m_renderOrigin;
}

bool C_FuncElevator::Interpolate( float currentTime )
{
	const bool result = BaseClass::Interpolate( currentTime );
	SetAbsOrigin( GetRenderOrigin() );
	return result;
}


void C_FuncElevator::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	if ( type == DATA_UPDATE_CREATED )
		m_interpolationAmount = GetInterpolationAmount( LATCH_SIMULATION_VAR );
	if ( !cl_elevator_physics.GetBool() ) return;
	if ( type == DATA_UPDATE_CREATED )
	{
		PhysOnDataChanged( this, true );
		return;
	}
	if ( !VPhysicsGetObject() ) return;
	Vector origin = GetLocalOrigin();
	const QAngle angles = GetLocalAngles();
	const float simulationTime = physenv->GetSimulationTime();
	const float nextFrameTime = physenv->GetNextFrameTime();
	const float timestep = physenv->GetSimulationTimestep();
	float targetTime = nextFrameTime;
	float remaining = ( 0.06f - ( targetTime - simulationTime ) ) + gpGlobals->frametime;
	while ( remaining > 0.0f )
	{
		remaining -= timestep;
		targetTime += timestep;
	}
	float speed;
	origin.z = CalcPosition( "PhysicsUpdate", targetTime - simulationTime + GetRenderTime(), &speed );
	PhysUpdateShadow( this, VPhysicsGetObject(), origin, angles, targetTime - nextFrameTime );
}

#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
bool C_FuncElevator::IsMoving() const
{
	return m_isMoving && gpGlobals->curtime >= m_movementStartTime;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif
