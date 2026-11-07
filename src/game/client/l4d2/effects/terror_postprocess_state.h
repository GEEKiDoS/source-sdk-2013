//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_POSTPROCESS_STATE_H
#define L4D2_TERROR_POSTPROCESS_STATE_H
#ifdef _WIN32
#pragma once
#endif

// Original TerrorClientMode: client RVA 0x2727B0 / 0x26FAF0 / 0x271640.
// This is the view-state producer, not the bile PCF owner. The native caller
// supplies the typed original player/controller fields listed in the integration
// artifact. No field is inferred from m_vomitFadeStart or a fabricated duration.
class C_BaseEntity;

struct TerrorPostProcessInputs_t
{
	bool m_bHavePlayer, m_bAlive, m_bIncapacitated, m_bHaveViewEntity;
	bool m_bPounced, m_bPummeled, m_bHangingFromTongue, m_bHangingFromLedge;
	bool m_bDefibrillatorUse, m_bReviving, m_bAdrenaline, m_bThirdStrike, m_bOnFire;
	float m_flHealth, m_flIncapHealth, m_flReviveProgress, m_flIntensity;
	float m_flITDuration, m_flITTimestamp, m_flITRatioReduction;
	const C_BaseEntity *m_pController;
	const float *m_pControllerValues;
	float m_flControllerFadeTime;
};

struct TerrorPostProcessPresets_t
{
	float m_Values[18][8];
	TerrorPostProcessPresets_t()
	{
		static const float s_Initial[18][8] =
		{
			{ .5f, 0, .8f, 1.1f, 0, 0, 0, 0 },
			{ 3, 0, .8f, 1.1f, 0, 0, 0, 0 },
			{ 0, 0, .8f, 1.1f, 0, 0, 0, 0 },
			{ 0, 0, .8f, 1.1f, 0, 0, 0, 0 },
			{ -.7f, -.7f, .8f, 1.1f, 0, 0, 0, 0 },
			{ 1, 0, .7f, 1, 1, 0, 0, 0 },
			{ 1, 0, .7f, 1, 1, 1, 0, 0 },
			{ 1, 0, .7f, 1, 1, 0, 0, 0 },
			{ 1, 0, .7f, 1, 1, 1, 0, 0 },
			{ 1, -1, .6f, 1, 0, 0, 0, 0 },
			{ 1, -1, .8f, 1.1f, 0, 0, 0, 0 },
			{ 0, -1, .2f, .6f, 0, 0, 0, 0 },
			{ 0, -1, 0, .5f, 0, 1, 0, 0 },
			{ 1, .3f, .8f, 1.1f, 0, 0, 0, 0 },
			{ 1.25f, -.65f, .4f, .85f, 0, 0, 0, 0 },
			{ 1.8f, -1, .6f, .95f, 0, 0, 0, 0 },
			{ -.8f, -.8f, .26f, .95f, 1, -3, 0, 0 },
			{ 0, 0, 0, 0, 0, 0, 0, 0 }
		};
		Q_memcpy( m_Values, s_Initial, sizeof( m_Values ) );
	}
	void Load( const char *pFilename = "scripts/postprocess.txt" )
	{
		static const char *s_Names[18] = { "default", "pills_1", "pills_2", "healed", "it",
			"pounced", "pounced_incap", "choked", "choked_incap", "3rd_strike", "incap_1",
			"incap_2", "incap_3", "high_stress", "on_fire", "adrenaline", "defibrillator", "map_control_unused" };
		KeyValues *pRoot = new KeyValues( "post_process" );
		if ( !pRoot->LoadFromFile( filesystem, pFilename ) )
		{
			Warning( "Error loading postprocessing params from file %s\n", pFilename );
			pRoot->deleteThis();
			return;
		}
		for ( KeyValues *p = pRoot->GetFirstSubKey(); p; p = p->GetNextKey() )
		{
			int n = 0;
			while ( n < 18 && Q_stricmp( p->GetName(), s_Names[n] ) ) ++n;
			if ( n == 18 )
			{
				Warning( "Unknown postprocess effect type: %s\n", p->GetName() );
				continue;
			}
			m_Values[n][0] = p->GetFloat( "localcontrast", 0 );
			m_Values[n][1] = p->GetFloat( "edgelocalcontrast", 0 );
			m_Values[n][2] = p->GetFloat( "vignettestart", 1 );
			m_Values[n][3] = p->GetFloat( "vignetteend", 2 );
			m_Values[n][4] = p->GetFloat( "vignetteblur", 0 );
			m_Values[n][5] = p->GetFloat( "fadetoblack", 0 );
		}
		pRoot->deleteThis();
	}
};

#pragma float_control( precise, on, push )
class CTerrorPostProcessState
{
public:
	CTerrorPostProcessState() : m_nState( 0 ), m_pController( NULL ), m_flDuration( 0 ),
		m_flTimestamp( -1 ), m_flPillsTimestamp( -1 ), m_flHealedTimestamp( -1 ), m_flSmoothedHealth( 0 )
	{
		const float initial[8] = { .5f, 0, .8f, 1.1f, 0, 0, 0, 0 };
		Q_memcpy( m_From, initial, sizeof( m_From ) );
		Q_memcpy( m_Target, initial, sizeof( m_Target ) );
		Q_memcpy( m_Current, initial, sizeof( m_Current ) );
	}
	void LevelInitReset( const TerrorPostProcessPresets_t &presets, float now )
	{
		m_flTimestamp = m_flPillsTimestamp = m_flHealedTimestamp = -1.0f;
		SetState( 0, 0, NULL, presets, now );
		m_Target[6] = m_Target[7] = 1.0f;
	}
	void LevelShutdownReset( const TerrorPostProcessPresets_t &presets, float now )
	{
		m_pController = NULL;
		SetState( 0, 0, NULL, presets, now );
		m_Target[6] = m_Target[7] = 1.0f;
	}
	void PillsUsed( float now ) { m_flPillsTimestamp = now + 2.0f; }
	void HealSucceeded( float now ) { m_flHealedTimestamp = now + 3.0f; }
	void ReloadTarget( const TerrorPostProcessPresets_t &presets )
	{
		if ( m_nState < 18 ) Q_memcpy( m_Target, presets.m_Values[m_nState], sizeof( m_Target ) );
	}
	const float *GetValues() const { return m_Current; }
	int GetState() const { return m_nState; }
	void ClearController() { m_pController = NULL; }
	void Update( const TerrorPostProcessInputs_t &in, const TerrorPostProcessPresets_t &presets, float now )
	{
		float values[8], incap[8];
		if ( !in.m_bHavePlayer )
			SetState( 0, .75f, NULL, presets, now );
		else
		{
			float health = in.m_flHealth;
			if ( in.m_bAlive && in.m_bIncapacitated && health <= m_flSmoothedHealth )
				health = ( health - m_flSmoothedHealth ) * .05f + m_flSmoothedHealth;
			m_flSmoothedHealth = health;
			if ( in.m_bHaveViewEntity )
				SetMapOrDefault( in, presets, now );
			else if ( in.m_bPounced || in.m_bPummeled )
			{
				if ( in.m_bAlive && in.m_bIncapacitated )
				{
					Mix( presets.m_Values[5], presets.m_Values[6], Clamp01( 1.0f - health / in.m_flIncapHealth * 2.0f ), values );
					SetState( 5, .5f, values, presets, now );
				}
				else SetState( 5, .5f, NULL, presets, now );
			}
			else if ( in.m_bHangingFromTongue )
			{
				if ( in.m_bAlive && in.m_bIncapacitated )
				{
					Mix( presets.m_Values[7], presets.m_Values[8], Clamp01( 1.0f - health / in.m_flIncapHealth * 4.0f ), values );
					// Original selects state 5 even for this custom choked profile.
					SetState( 5, .5f, values, presets, now );
				}
				else SetState( 7, .75f, NULL, presets, now );
			}
			else if ( in.m_bDefibrillatorUse ) SetState( 16, .75f, NULL, presets, now );
			else if ( in.m_bAlive && in.m_bIncapacitated && !in.m_bHangingFromLedge )
			{
				float ratio = health / in.m_flIncapHealth;
				if ( ratio <= .25f ) Mix( presets.m_Values[11], presets.m_Values[12], 1.0f - ClampKeepUnordered( ratio * 4.0f ), incap );
				else Mix( presets.m_Values[10], presets.m_Values[11], 1.0f - ClampKeepUnordered( ( ratio - .25f ) * 1.3333334f ), incap );
				if ( in.m_bReviving )
				{
					Mix( incap, presets.m_Values[0], ClampKeepUnordered( in.m_flReviveProgress ), values );
					Q_memcpy( incap, values, sizeof( incap ) );
				}
				SetState( 10, .5f, incap, presets, now );
			}
			else if ( now < in.m_flITTimestamp )
			{
				const float remaining = (float)( (double)in.m_flITTimestamp - (double)now );
				float ratio = in.m_flITTimestamp > 0.0f ? ClampKeepUnordered( remaining / in.m_flITDuration ) : 0.0f;
				if ( ratio < .5f ) ratio = MAX( 0.0f, ratio - in.m_flITRatioReduction );
				Mix( presets.m_Values[0], presets.m_Values[4], ratio, values );
				SetState( 4, .25f, values, presets, now );
			}
			else if ( in.m_bAdrenaline ) SetState( 15, .75f, NULL, presets, now );
			else if ( now < m_flPillsTimestamp )
			{
				const bool early = Clamp01( ( m_flPillsTimestamp - now ) / 2.0f ) > .8f;
				AddBase( in, presets, early ? 1 : 2, 3.0f, values );
				SetState( early ? 1 : 2, early ? .2f : 1.0f, values, presets, now );
			}
			else if ( now < m_flHealedTimestamp )
			{
				AddBase( in, presets, 3, 3.0f, values );
				SetState( 3, .5f, values, presets, now );
			}
			else if ( in.m_bThirdStrike ) SetState( 9, .3f, NULL, presets, now );
			else if ( in.m_flIntensity * .01f >= 1.0f )
			{
				AddBase( in, presets, 13, 1.5f, values );
				SetState( 13, 2.0f, values, presets, now );
			}
			else if ( in.m_bOnFire ) SetState( 14, .75f, NULL, presets, now );
			else SetMapOrDefault( in, presets, now );
		}
		float remaining = m_flTimestamp > 0.0f ? Clamp01( ( m_flTimestamp - now ) / m_flDuration ) : 0.0f;
		Mix( m_From, m_Target, 1.0f - remaining, m_Current );
	}
private:
	static float Clamp01( float x ) { return x > 1.0f ? 1.0f : x >= 0.0f ? x : 0.0f; }
	static float ClampKeepUnordered( float x ) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
	static void Mix( const float *a, const float *b, float t, float *out )
	{
		for ( int i = 0; i < 8; ++i ) out[i] = ( b[i] - a[i] ) * t + a[i];
	}
	static void AddBase( const TerrorPostProcessInputs_t &in, const TerrorPostProcessPresets_t &p, int n, float maxValue, float *out )
	{
		Q_memcpy( out, p.m_Values[n], sizeof( float ) * 8 );
		const float *base = in.m_pController ? in.m_pControllerValues : p.m_Values[0];
		for ( int i = 0; i < 2; ++i )
		{
			const float value = out[i] + base[i];
			out[i] = value > maxValue ? maxValue : value >= -.3f ? value : -.3f;
		}
	}
	void SetState( int n, float duration, const float *values, const TerrorPostProcessPresets_t &p, float now )
	{
		if ( m_nState == n )
		{
			if ( !values ) return;
		}
		else
		{
			m_nState = n; m_pController = NULL;
			m_flTimestamp = (float)( (double)now + (double)duration ); m_flDuration = duration;
			Q_memcpy( m_From, m_Current, sizeof( m_From ) );
		}
		// Preserve the retail six-lane copy. Grain/top-vignette remain from the
		// current map-controller target; normal states do not reset those lanes.
		Q_memcpy( m_Target, values ? values : p.m_Values[n], sizeof( float ) * 6 );
	}
	void SetMapOrDefault( const TerrorPostProcessInputs_t &in, const TerrorPostProcessPresets_t &p, float now )
	{
		if ( !in.m_pController ) { SetState( 0, .75f, NULL, p, now ); return; }
		m_nState = 17;
		if ( m_pController != in.m_pController )
		{
			m_pController = in.m_pController;
			m_flDuration = in.m_flControllerFadeTime > .001f ? in.m_flControllerFadeTime : .001f;
			m_flTimestamp = (float)( (double)now + (double)m_flDuration );
			Q_memcpy( m_From, m_Current, sizeof( m_From ) );
		}
		Q_memcpy( m_Target, in.m_pControllerValues, sizeof( m_Target ) );
	}
	int m_nState;
	const C_BaseEntity *m_pController;
	float m_flDuration, m_flTimestamp, m_flPillsTimestamp, m_flHealedTimestamp, m_flSmoothedHealth;
	float m_From[8], m_Target[8], m_Current[8];
};
#pragma float_control( pop )
#endif // L4D2_TERROR_POSTPROCESS_STATE_H
