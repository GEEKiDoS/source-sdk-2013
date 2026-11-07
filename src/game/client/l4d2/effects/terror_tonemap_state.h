//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_TONEMAP_STATE_H
#define L4D2_TERROR_TONEMAP_STATE_H
#ifdef _WIN32
#pragma once
#endif

// Native game-owned scalar history/chase, original client RVA 0x1C53C0.
// The SDK engine chase is NOT run on top of this state. Submission is at the
// original pre-scene phase; histogram adjustment is at postprocessing phase.
class CTerrorTonemapState
{
public:
	CTerrorTonemapState() : m_flGoal( 1.0f ), m_flCurrent( 1.0f ), m_nSamples( 0 ) {}
	float GetCurrent() const { return m_flCurrent; }
	void Reset( float value, float minimum, float maximum )
	{
		if ( value <= 0.0f ) value = clamp( ( maximum + minimum ) * .5f, 1.0f, 10.0f );
		m_flGoal = m_flCurrent = value;
		m_nSamples = 0;
	}
	void Update( float target, float minimum, float maximum, float frameTime,
		int algorithm, float manualRate, float accelerateDown, float forcedScale )
	{
		if ( !IsFinite( target ) ) return;
		if ( m_nSamples < 10 ) m_History[m_nSamples++] = target;
		else
		{
			for ( int i = 0; i < 9; ++i ) m_History[i] = m_History[i+1];
			m_History[9] = target;
		}
		if ( m_nSamples == 10 )
		{
			float sum = 0.0f, weights = 0.0f;
			for ( int i = 0; i < 10; ++i )
			{
				float weight = (float)abs( i - 5 ) * .2f;
				weights += weight;
				sum += m_History[i] * weight;
			}
			target = clamp( sum * ( 1.0f / weights ), minimum, maximum );
		}
		if ( IsFinite( target ) ) m_flGoal = target;
		float dt = frameTime > 0.0f ? frameTime : 0.0f;
		float rate = algorithm == 1 ? manualRate * 2.0f : manualRate;
		if ( rate == 0.0f ) m_flCurrent = m_flGoal;
		else
		{
			if ( m_flCurrent > m_flGoal )
			{
				float downRate = accelerateDown * rate;
				rate = MIN( ( m_flCurrent - m_flGoal ) * ( downRate - rate ) * .66666669f + rate, downRate );
			}
			float blend = rate * dt;
			if ( algorithm == 1 && blend >= .015625f ) blend = .015625f;
			else blend = clamp( blend, 0.0f, 1.0f );
			m_flCurrent = ( 1.0f - blend ) * m_flCurrent + m_flGoal * blend;
			if ( !IsFinite( m_flCurrent ) ) m_flCurrent = m_flGoal;
		}
		if ( forcedScale > 0.0f ) Reset( forcedScale, minimum, maximum );
	}
private:
	float m_flGoal, m_flCurrent;
	int m_nSamples;
	float m_History[10];
};
#endif // L4D2_TERROR_TONEMAP_STATE_H
