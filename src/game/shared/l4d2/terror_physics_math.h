//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_PHYSICS_MATH_H
#define L4D2_TERROR_PHYSICS_MATH_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include <float.h>
#include <math.h>
#include <xmmintrin.h>

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// S4EA090/C35F4C0: return the wide length, but normalize with the native
// float length spill, FLT_EPSILON addition, DIVSS and three MULSS operations.
// The SDK's reciprocal-square-root refinement is a different algorithm.
inline double TerrorPhysicsNormalize( Vector &vector )
{
	const double length = sqrt( ( (double)vector.x * vector.x + (double)vector.y * vector.y ) + (double)vector.z * vector.z );
	const float roundedLength = (float)length;
	const __m128 inverse = _mm_div_ss( _mm_set_ss( 1.0f ), _mm_add_ss( _mm_set_ss( roundedLength ), _mm_set_ss( FLT_EPSILON ) ) );
	vector.x = _mm_cvtss_f32( _mm_mul_ss( _mm_set_ss( vector.x ), inverse ) );
	vector.y = _mm_cvtss_f32( _mm_mul_ss( _mm_set_ss( vector.y ), inverse ) );
	vector.z = _mm_cvtss_f32( _mm_mul_ss( _mm_set_ss( vector.z ), inverse ) );
	return length;
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#endif // L4D2_TERROR_PHYSICS_MATH_H
