#ifndef FUNC_ELEVATOR_SHARED_H
#define FUNC_ELEVATOR_SHARED_H
#ifdef _WIN32
#pragma once
#endif

// App550 build23990068: server100E5ED0/client100F0240.  The caller owns
// the different server/client start-time guard and the stationary origin.
inline double ComputeElevatorPosition( float elapsed, float startZ, float startSpeed,
	float destinationZ, float maxSpeed, float acceleration, float *speed )
{
	if ( elapsed <= 0.0f )
		return startZ;

	float velocity = maxSpeed;
	if ( startZ > destinationZ )
		velocity = -velocity;
	const float accelerationTime = maxSpeed / acceleration;
	if ( accelerationTime > elapsed )
	{
		const float currentVelocity = ( elapsed / accelerationTime ) * velocity;
		const float averageVelocity = ( startSpeed + currentVelocity ) * 0.5f;
		*speed = currentVelocity;
		return (double)averageVelocity * (double)elapsed + (double)startZ; // E5FA9..E5FB3: no float result spill
	}

	const float cruiseTime = elapsed - accelerationTime;
	const float cruiseStartZ = ( ( startSpeed + velocity ) * 0.5f ) * accelerationTime + startZ;
	const float cruiseZ = velocity * cruiseTime + cruiseStartZ;
	if ( ( velocity > 0.0f && cruiseZ >= destinationZ ) ||
		 ( velocity < 0.0f && destinationZ >= cruiseZ ) )
	{
		*speed = 0.0f;
		return destinationZ;
	}

	const double stoppingDistance = static_cast<double>( velocity ) * 0.5 * accelerationTime;
	const float roundedStoppingDistance = stoppingDistance;
	if ( fabs( (double)destinationZ - (double)cruiseZ ) >= fabs( stoppingDistance ) )
	{
		*speed = velocity;
		return cruiseZ;
	}

	const float decelerationStartZ = destinationZ - roundedStoppingDistance;
	const float decelerationTime = cruiseTime - ( ( decelerationStartZ - cruiseStartZ ) / velocity );
	// E605B COMISS/JB admits unordered, as well as strict less.
	if ( ( FloatBits( decelerationTime ) & 0x7fffffffu ) > 0x7f800000u ||
		( FloatBits( accelerationTime ) & 0x7fffffffu ) > 0x7f800000u ||
		decelerationTime < accelerationTime )
	{
		const float currentVelocity = ( 1.0f - decelerationTime / accelerationTime ) * velocity;
		*speed = currentVelocity;
		return ( (double)velocity + (double)currentVelocity ) * 0.5 * (double)decelerationTime + (double)decelerationStartZ;
	}

	*speed = 0.0f;
	return destinationZ;
}

#endif
