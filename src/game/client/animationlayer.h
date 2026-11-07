//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef ANIMATIONLAYER_H
#define ANIMATIONLAYER_H
#ifdef _WIN32
#pragma once
#endif


#include "rangecheckedvar.h"
#include "lerp_functions.h"
#include "networkvar.h"
#if defined( L4D2 )
class C_BaseAnimatingOverlay;
#endif

class C_AnimationLayer
{
public:

	// This allows the datatables to access private members.
	ALLOW_DATATABLES_PRIVATE_ACCESS();

	C_AnimationLayer();
	void Reset();

	void SetOrder( int order );
#if defined( L4D2 )
	void SetWeight( float weight );
	void SetCycle( float cycle );
	void SetSequence( int sequence );
#endif

public:

	bool IsActive( void );

	CRangeCheckedVar<int, -1, 65535, 0>	m_nSequence;
	CRangeCheckedVar<float, -2, 2, 0>	m_flPrevCycle;
	CRangeCheckedVar<float, -5, 5, 0>	m_flWeight;
	int		m_nOrder;

	// used for automatic crossfades between sequence changes
	CRangeCheckedVar<float, -50, 50, 1>		m_flPlaybackRate;
	CRangeCheckedVar<float, -2, 2, 0>		m_flCycle;

	float GetFadeout( float flCurTime );

	void BlendWeight();

	float	m_flLayerAnimtime;
	float	m_flLayerFadeOuttime;

	float   m_flBlendIn;
	float   m_flBlendOut;

	bool    m_bClientBlend;
#if defined( L4D2 )
	C_BaseAnimatingOverlay *m_pOwnerEntity;
#endif
};
#ifdef CLIENT_DLL
	#define CAnimationLayer C_AnimationLayer
#endif

inline C_AnimationLayer::C_AnimationLayer()
{
#if defined( L4D2 )
	m_pOwnerEntity = NULL;
	// Main decision:3E9C0 omits heap order+8;3D120/6C01C otherwise read it.
	// New order0 preserves the selected initial dirty16 outcome. Reset keeps it.
	m_nOrder = 0;
#endif
	Reset();
}

#if !defined( L4D2 )
inline void C_AnimationLayer::Reset()
{
	m_nSequence = 0;
	m_flPrevCycle = 0;
	m_flWeight = 0;
	m_flPlaybackRate = 0;
	m_flCycle = 0;
	m_flLayerAnimtime = 0;
	m_flLayerFadeOuttime = 0;
	m_flBlendIn = 0;
	m_flBlendOut = 0;
	m_bClientBlend = false;
}


inline void C_AnimationLayer::SetOrder( int order )
{
	m_nOrder = order;
}
#endif

#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
inline float C_AnimationLayer::GetFadeout( float flCurTime )
{
	float s;

    if (m_flLayerFadeOuttime <= 0.0f)
	{
		s = 0;
	}
	else
	{
		// blend in over 0.2 seconds
#if defined( L4D2 )
		s = 1.0f - (flCurTime - m_flLayerAnimtime) / m_flLayerFadeOuttime;
#else
		s = 1.0 - (flCurTime - m_flLayerAnimtime) / m_flLayerFadeOuttime;
#endif
		if (s > 0 && s <= 1.0)
		{
			// do a nice spline curve
			s = 3 * s * s - 2 * s * s * s;
		}
		else if ( s > 1.0f )
		{
			// Shouldn't happen, but maybe curtime is behind animtime?
			s = 1.0f;
		}
	}
	return s;
}
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif


inline C_AnimationLayer LoopingLerp( float flPercent, C_AnimationLayer& from, C_AnimationLayer& to )
{
	C_AnimationLayer output;

	output.m_nSequence = to.m_nSequence;
	output.m_flCycle = LoopingLerp( flPercent, (float)from.m_flCycle, (float)to.m_flCycle );
	output.m_flPrevCycle = to.m_flPrevCycle;
	output.m_flWeight = Lerp( flPercent, from.m_flWeight, to.m_flWeight );
	output.m_nOrder = to.m_nOrder;

	output.m_flLayerAnimtime = to.m_flLayerAnimtime;
	output.m_flLayerFadeOuttime = to.m_flLayerFadeOuttime;
#if defined( L4D2 )
	output.m_pOwnerEntity = to.m_pOwnerEntity;
#endif
	return output;
}

inline C_AnimationLayer Lerp( float flPercent, const C_AnimationLayer& from, const C_AnimationLayer& to )
{
	C_AnimationLayer output;

	output.m_nSequence = to.m_nSequence;
	output.m_flCycle = Lerp( flPercent, from.m_flCycle, to.m_flCycle );
	output.m_flPrevCycle = to.m_flPrevCycle;
	output.m_flWeight = Lerp( flPercent, from.m_flWeight, to.m_flWeight );
	output.m_nOrder = to.m_nOrder;

	output.m_flLayerAnimtime = to.m_flLayerAnimtime;
	output.m_flLayerFadeOuttime = to.m_flLayerFadeOuttime;
#if defined( L4D2 )
	output.m_pOwnerEntity = to.m_pOwnerEntity;
#endif
	return output;
}

inline C_AnimationLayer LoopingLerp_Hermite( float flPercent, C_AnimationLayer& prev, C_AnimationLayer& from, C_AnimationLayer& to )
{
	C_AnimationLayer output;

	output.m_nSequence = to.m_nSequence;
	output.m_flCycle = LoopingLerp_Hermite( flPercent, (float)prev.m_flCycle, (float)from.m_flCycle, (float)to.m_flCycle );
	output.m_flPrevCycle = to.m_flPrevCycle;
	output.m_flWeight = Lerp( flPercent, from.m_flWeight, to.m_flWeight );
	output.m_nOrder = to.m_nOrder;

	output.m_flLayerAnimtime = to.m_flLayerAnimtime;
	output.m_flLayerFadeOuttime = to.m_flLayerFadeOuttime;
#if defined( L4D2 )
	output.m_pOwnerEntity = to.m_pOwnerEntity;
#endif
	return output;
}

// YWB:  Specialization for interpolating euler angles via quaternions...
inline C_AnimationLayer Lerp_Hermite( float flPercent, const C_AnimationLayer& prev, const C_AnimationLayer& from, const C_AnimationLayer& to )
{
	C_AnimationLayer output;

	output.m_nSequence = to.m_nSequence;
	output.m_flCycle = Lerp_Hermite( flPercent, prev.m_flCycle, from.m_flCycle, to.m_flCycle );
	output.m_flPrevCycle = to.m_flPrevCycle;
	output.m_flWeight = Lerp( flPercent, from.m_flWeight, to.m_flWeight );
	output.m_nOrder = to.m_nOrder;

	output.m_flLayerAnimtime = to.m_flLayerAnimtime;
	output.m_flLayerFadeOuttime = to.m_flLayerFadeOuttime;
#if defined( L4D2 )
	output.m_pOwnerEntity = to.m_pOwnerEntity;
#endif
	return output;
}

inline void Lerp_Clamp( C_AnimationLayer &val )
{
	Lerp_Clamp( val.m_nSequence );
	Lerp_Clamp( val.m_flCycle );
	Lerp_Clamp( val.m_flPrevCycle );
	Lerp_Clamp( val.m_flWeight );
	Lerp_Clamp( val.m_nOrder );
	Lerp_Clamp( val.m_flLayerAnimtime );
	Lerp_Clamp( val.m_flLayerFadeOuttime );
}

inline void C_AnimationLayer::BlendWeight()
{
	if ( !m_bClientBlend )
		return;

	m_flWeight = 1;

	// blend in?
	if ( m_flBlendIn != 0.0f )
	{
		if (m_flCycle < m_flBlendIn)
		{
			m_flWeight = m_flCycle / m_flBlendIn;
		}
	}

	// blend out?
	if ( m_flBlendOut != 0.0f )
	{
		if (m_flCycle > 1.0 - m_flBlendOut)
		{
			m_flWeight = (1.0 - m_flCycle) / m_flBlendOut;
		}
	}

	m_flWeight = 3.0 * m_flWeight * m_flWeight - 2.0 * m_flWeight * m_flWeight * m_flWeight;
	if (m_nSequence == 0)
		m_flWeight = 0;
}

#endif // ANIMATIONLAYER_H
