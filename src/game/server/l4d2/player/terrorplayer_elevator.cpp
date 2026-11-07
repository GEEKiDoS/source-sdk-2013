#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/entities/func_elevator.h"
#include "trains.h"
#include "tier0/memdbgon.h"

// S2FBF70. Real ground membership includes either mover. This is distinct
// from BasePlayer::GetElevator(), the native16DEA0 tracked raw handle.
CBaseEntity *CTerrorPlayer::GetGroundElevator() const
{
	CBaseEntity *ground = GetGroundEntity();
	if ( !ground || ground->IsWorld() ) return NULL;
	if ( dynamic_cast< CFuncElevator * >( ground ) || dynamic_cast< CFuncTrackTrain * >( ground ) ) return ground;
	return NULL;
}

// S2FBFC0 repeats the real membership checks, then reads ACTUAL abs velocity.
// Moving/current-speed curve fields are not the predicate and zero velocity
// must not be repaired by synthesizing delta-position / frame-time.
bool CTerrorPlayer::IsOnElevatorThatIsMoving() const
{
	CBaseEntity *ground = GetGroundEntity();
	if ( !ground || ground->IsWorld() ) return false;
	if ( !dynamic_cast< CFuncElevator * >( ground ) && !dynamic_cast< CFuncTrackTrain * >( ground ) ) return false;
	return ground->GetAbsVelocity().LengthSqr() > 0.0f;
}
