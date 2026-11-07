//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_HUD_NATIVE_NUMERIC_H
#define TERROR_HUD_NATIVE_NUMERIC_H
#ifdef _WIN32
#pragma once
#endif
#include <emmintrin.h>
// C native CVTTSS2SI has a defined INT_MIN result for unordered/overflow. A C++
// float-to-int cast does not. Keep the float spill at each recovered SSE site.
inline int TerrorHudCvttFloat( float value )
{
	return _mm_cvtt_ss2si( _mm_set_ss( value ) );
}
// Native C47F370 SSE2 path spills ST0 to double, then CVTTSD2SI EAX. This
// preserves INT_MIN for unordered/overflow without undefined C++ casts.
inline int TerrorHudFtol( double value )
{
	return _mm_cvttsd_si32( _mm_set_sd( value ) );
}
// Defined x86 ADD/SUB wrap, including coordinates made from integer-indefinite.
inline int TerrorHudAdd32( int a, int b ) { return static_cast<int>( static_cast<unsigned int>( a ) + static_cast<unsigned int>( b ) ); }
inline int TerrorHudSub32( int a, int b ) { return static_cast<int>( static_cast<unsigned int>( a ) - static_cast<unsigned int>( b ) ); }
#endif
