//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef PRECIPITATION_SHARED_H
#define PRECIPITATION_SHARED_H
#ifdef _WIN32
#pragma once
#endif


// Types of precipitation
enum PrecipitationType_t
{
	PRECIPITATION_TYPE_RAIN = 0,
	PRECIPITATION_TYPE_SNOW,
	PRECIPITATION_TYPE_ASH,
	PRECIPITATION_TYPE_SNOWFALL,
#if defined( L4D2 )
	PRECIPITATION_TYPE_PARTICLE_RAIN,
	PRECIPITATION_TYPE_PARTICLE_ASH,
	PRECIPITATION_TYPE_PARTICLE_RAINSTORM,
	PRECIPITATION_TYPE_PARTICLE_BUGS,
	PRECIPITATION_TYPE_PARTICLE_SMOKE,
#endif
	NUM_PRECIPITATION_TYPES
};


#endif // PRECIPITATION_SHARED_H
