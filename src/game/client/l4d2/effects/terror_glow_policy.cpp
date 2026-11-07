//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/effects/terror_glow_policy.h"
#include <math.h>
#include <emmintrin.h>

#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

// C:1050FA60 and C:10517760. Native 0x80000 flags below are retained as
// numeric metadata; SDK2013 has no named counterpart for that native bit.
ConVar cl_glow_brightness( "cl_glow_brightness", "1", 0x00004008, "Brightness of player halos" );
extern ConVar cl_colorblind; // Sole native registration is the actual crosshair owner.
ConVar spec_keep_team_glows( "spec_keep_team_glows", "1", 0x00004008, "Use team glows even when spectating in vs mode" );
ConVar cl_glow_it_timer_ratio_reduction( "cl_glow_it_timer_ratio_reduction", "0.08", 0x00004008,
	"Shortens the IT time remaining ratio that's applied to glow visibility by this amount." );
static ConVar cl_glow_range_exp( "cl_glow_range_exp", "1.0", 0x0000000A );
extern ConVar survivor_limp_health;

// C:1050FF40..10510000: pulse/include settings have native bounds.
static ConVar cl_glow_survivor_health_bleed_pulse( "cl_glow_survivor_health_bleed_pulse", "1", 0x00080008,
	"Controls the survivors glow pulse when they are bleeding out a health buffer", true, 0.0f, true, 1.0f );
static ConVar cl_glow_survivor_health_bleed_pulse_speed( "cl_glow_survivor_health_bleed_pulse_speed", "10", 0x00080008,
	"Controls the survivors glow pulse when they are bleeding out a health buffer", true, 0.0f, true, 100.0f );
static ConVar cl_glow_survivor_health_bleed_pulse_amount( "cl_glow_survivor_health_bleed_pulse_amount", "0.3", 0x00080008,
	"Controls the survivors glow pulse when they are bleeding out a health buffer", true, 0.0f, true, 1.0f );
static ConVar cl_glow_survivor_health_include_buffer( "cl_glow_survivor_health_include_buffer", "1", 0x00080008,
	"Include temp health buffer when displaying survivor health", true, 0.0f, true, 1.0f );

// C:10510040..10510490, consumed by C:10252410.
static ConVar cl_glow_survivor_health_crit_r( "cl_glow_survivor_health_crit_r", "0.63", 0x00080008, "Color the Infected see Survivors when their health is critical" );
static ConVar cl_glow_survivor_health_crit_g( "cl_glow_survivor_health_crit_g", "0.098", 0x00080008, "Color the Infected see Survivors when their health is critical" );
static ConVar cl_glow_survivor_health_crit_b( "cl_glow_survivor_health_crit_b", "0.098", 0x00080008, "Color the Infected see Survivors when their health is critical" );
static ConVar cl_glow_survivor_health_crit_colorblind_r( "cl_glow_survivor_health_crit_colorblind_r", "0.047", 0x00080008, "Color the Infected see Survivors when their health is critical" );
static ConVar cl_glow_survivor_health_crit_colorblind_g( "cl_glow_survivor_health_crit_colorblind_g", "0.807", 0x00080008, "Color the Infected see Survivors when their health is critical" );
static ConVar cl_glow_survivor_health_crit_colorblind_b( "cl_glow_survivor_health_crit_colorblind_b", "0.807", 0x00080008, "Color the Infected see Survivors when their health is critical" );
static ConVar cl_glow_survivor_health_low_r( "cl_glow_survivor_health_low_r", "0.70", 0x00080008, "Color the Infected see Survivors when their health is low" );
static ConVar cl_glow_survivor_health_low_g( "cl_glow_survivor_health_low_g", "0.25", 0x00080008, "Color the Infected see Survivors when their health is low" );
static ConVar cl_glow_survivor_health_low_b( "cl_glow_survivor_health_low_b", "0.0", 0x00080008, "Color the Infected see Survivors when their health is low" );
static ConVar cl_glow_survivor_health_low_colorblind_r( "cl_glow_survivor_health_low_colorblind_r", "0.047", 0x00080008, "Color the Infected see Survivors when their health is low" );
static ConVar cl_glow_survivor_health_low_colorblind_g( "cl_glow_survivor_health_low_colorblind_g", "0.807", 0x00080008, "Color the Infected see Survivors when their health is low" );
static ConVar cl_glow_survivor_health_low_colorblind_b( "cl_glow_survivor_health_low_colorblind_b", "0.807", 0x00080008, "Color the Infected see Survivors when their health is low" );
static ConVar cl_glow_survivor_health_med_r( "cl_glow_survivor_health_med_r", "0.59", 0x00080008, "Color the Infected see Survivors when their health is medium" );
static ConVar cl_glow_survivor_health_med_g( "cl_glow_survivor_health_med_g", "0.48", 0x00080008, "Color the Infected see Survivors when their health is medium" );
static ConVar cl_glow_survivor_health_med_b( "cl_glow_survivor_health_med_b", "0.032", 0x00080008, "Color the Infected see Survivors when their health is medium" );
static ConVar cl_glow_survivor_health_med_colorblind_r( "cl_glow_survivor_health_med_colorblind_r", "0.694", 0x00080008, "Color the Infected see Survivors when their health is medium" );
static ConVar cl_glow_survivor_health_med_colorblind_g( "cl_glow_survivor_health_med_colorblind_g", "0.573", 0x00080008, "Color the Infected see Survivors when their health is medium" );
static ConVar cl_glow_survivor_health_med_colorblind_b( "cl_glow_survivor_health_med_colorblind_b", "0.098", 0x00080008, "Color the Infected see Survivors when their health is medium" );
static ConVar cl_glow_survivor_health_high_r( "cl_glow_survivor_health_high_r", "0.039", 0x00080008, "Color the Infected see Survivors when their health is high" );
static ConVar cl_glow_survivor_health_high_g( "cl_glow_survivor_health_high_g", "0.69", 0x00080008, "Color the Infected see Survivors when their health is high" );
static ConVar cl_glow_survivor_health_high_b( "cl_glow_survivor_health_high_b", "0.196", 0x00080008, "Color the Infected see Survivors when their health is high" );
static ConVar cl_glow_survivor_health_high_colorblind_r( "cl_glow_survivor_health_high_colorblind_r", "0.047", 0x00080008, "Color the Infected see Survivors when their health is high" );
static ConVar cl_glow_survivor_health_high_colorblind_g( "cl_glow_survivor_health_high_colorblind_g", "0.694", 0x00080008, "Color the Infected see Survivors when their health is high" );
static ConVar cl_glow_survivor_health_high_colorblind_b( "cl_glow_survivor_health_high_colorblind_b", "0.392", 0x00080008, "Color the Infected see Survivors when their health is high" );

bool TerrorGlow_IsSurvivorTeam( int nTeam )
{
	return nTeam == TEAM_SURVIVOR || nTeam == TEAM_L4D1_SURVIVOR;
}

// C:10251B50: only IN_EYE substitutes the observer target.
C_BasePlayer *TerrorGlow_ResolveRenderPlayer( C_BasePlayer *pActiveSlotPlayer )
{
	if ( pActiveSlotPlayer && pActiveSlotPlayer->GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_BaseEntity *pTarget = pActiveSlotPlayer->GetObserverTarget();
		if ( pTarget && pTarget->IsPlayer() )
			return ToBasePlayer( pTarget );
	}
	return pActiveSlotPlayer;
}

// C:10251BB0: IN_EYE and CHASE substitute; every other mode keeps raw local.
C_BasePlayer *TerrorGlow_ResolveItemColorPlayer( C_BasePlayer *pActiveSlotPlayer )
{
	if ( pActiveSlotPlayer && ( pActiveSlotPlayer->GetObserverMode() == OBS_MODE_IN_EYE ||
		pActiveSlotPlayer->GetObserverMode() == OBS_MODE_CHASE ) )
	{
		C_BaseEntity *pTarget = pActiveSlotPlayer->GetObserverTarget();
		if ( pTarget && pTarget->IsPlayer() )
			return ToBasePlayer( pTarget );
	}
	return pActiveSlotPlayer;
}

// C:10257830/10257CE0 share this decision; do not reuse the renderer selector.
C_BasePlayer *TerrorGlow_ResolveTeamColorPlayer( C_BasePlayer *pActiveSlotPlayer, bool bPlayerControlledZombies )
{
	if ( !bPlayerControlledZombies ||
		( pActiveSlotPlayer && pActiveSlotPlayer->GetTeamNumber() == TEAM_SPECTATOR ) ||
		!spec_keep_team_glows.GetBool() ||
		( pActiveSlotPlayer && pActiveSlotPlayer->GetObserverMode() == OBS_MODE_DEATHCAM ) )
	{
		return TerrorGlow_ResolveItemColorPlayer( pActiveSlotPlayer );
	}
	return pActiveSlotPlayer;
}

// C:1026EC00/1026EC80. The buffer-inclusive form truncates AFTER adding the
// float buffer. Critical uses m_isGoingToDie, not m_bIsOnThirdStrike.
int TerrorGlow_GetSurvivorHealthLevel( const C_TerrorPlayer &player )
{
	int nHealth = player.GetHealth();
	if ( cl_glow_survivor_health_include_buffer.GetBool() )
		nHealth = _mm_cvttss_si32( _mm_set_ss( (float)nHealth + player.GetHealthBuffer() ) );
	float flFraction = (float)nHealth / (float)player.GetMaxHealth();
	if ( flFraction > 1.0f ) flFraction = 1.0f;
	else if ( flFraction < 0.0f ) flFraction = 0.0f;
	if ( player.IsIncapacitated() || ( player.m_isGoingToDie && nHealth <= 1 ) )
		return 0;
	if ( flFraction < 0.25f )
		return 1;
	// Native JBE also takes the unordered path to health level3.
	return survivor_limp_health.GetFloat() * 0.0099999998f > flFraction ? 2 : 3;
}

void TerrorGlow_GetSurvivorHealthColor( const C_TerrorPlayer &player, Vector &color )
{
	const bool bColorblind = cl_colorblind.GetInt() == 2;
	ConVar *pRed;
	ConVar *pGreen;
	ConVar *pBlue;
	switch ( TerrorGlow_GetSurvivorHealthLevel( player ) )
	{
	case 0:
		pRed = bColorblind ? &cl_glow_survivor_health_crit_colorblind_r : &cl_glow_survivor_health_crit_r;
		pGreen = bColorblind ? &cl_glow_survivor_health_crit_colorblind_g : &cl_glow_survivor_health_crit_g;
		pBlue = bColorblind ? &cl_glow_survivor_health_crit_colorblind_b : &cl_glow_survivor_health_crit_b;
		break;
	case 1:
		pRed = bColorblind ? &cl_glow_survivor_health_low_colorblind_r : &cl_glow_survivor_health_low_r;
		pGreen = bColorblind ? &cl_glow_survivor_health_low_colorblind_g : &cl_glow_survivor_health_low_g;
		pBlue = bColorblind ? &cl_glow_survivor_health_low_colorblind_b : &cl_glow_survivor_health_low_b;
		break;
	case 2:
		pRed = bColorblind ? &cl_glow_survivor_health_med_colorblind_r : &cl_glow_survivor_health_med_r;
		pGreen = bColorblind ? &cl_glow_survivor_health_med_colorblind_g : &cl_glow_survivor_health_med_g;
		pBlue = bColorblind ? &cl_glow_survivor_health_med_colorblind_b : &cl_glow_survivor_health_med_b;
		break;
	default:
		pRed = bColorblind ? &cl_glow_survivor_health_high_colorblind_r : &cl_glow_survivor_health_high_r;
		pGreen = bColorblind ? &cl_glow_survivor_health_high_colorblind_g : &cl_glow_survivor_health_high_g;
		pBlue = bColorblind ? &cl_glow_survivor_health_high_colorblind_b : &cl_glow_survivor_health_high_b;
		break;
	}
	color.Init( pRed->GetFloat(), pGreen->GetFloat(), pBlue->GetFloat() );
	if ( cl_glow_survivor_health_bleed_pulse.GetBool() && player.GetHealthBuffer() > 0.0f )
	{
		// C257BD6..C257C04 keeps the phase and pulse wide until each RGB store.
		const double flPulse = sin( (double)cl_glow_survivor_health_bleed_pulse_speed.GetFloat() * (double)gpGlobals->curtime ) *
			(double)cl_glow_survivor_health_bleed_pulse_amount.GetFloat() + 1.0;
		color.x = (float)( (double)color.x * flPulse );
		color.y = (float)( (double)color.y * flPulse );
		color.z = (float)( (double)color.z * flPulse );
	}
	color *= cl_glow_brightness.GetFloat();
}

// Replace the EXISTING TerrorGlow_GetITCompositeScale body in
// client/l4d2/terror_glow_policy.cpp. No new function, state or ConVar owner.
// C2C5E1D..C2C5F19 direct assembly controls unordered behavior. Do not use the
// SDK release float clamp (MINSS/MAXSS) or Hex-Rays' >=/OR reconstruction.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
float TerrorGlow_GetITCompositeScale( const C_TerrorPlayer &player )
{
	const float timestamp = player.GetITTimerTimestamp();
	if ( gpGlobals->curtime >= timestamp )
		return 1.0f;
	float ratio = 0.0f;
	if ( timestamp > 0.0f )
	{
		const float remaining = timestamp - gpGlobals->curtime;
		ratio = remaining / player.GetITTimerDuration();
		if ( ratio < 0.0f ) ratio = 0.0f;
		else if ( ratio > 1.0f ) ratio = 1.0f;
	}
	if ( ratio < 0.5f )
	{
		ratio -= cl_glow_it_timer_ratio_reduction.GetFloat();
		if ( ratio < 0.0f ) ratio = 0.0f;
	}
	float scale;
	if ( ratio > 0.99000001f ) scale = ( ratio - 0.99000001f ) * 100.0f;
	else scale = ( ( 1.0f - ratio ) - 0.69999999f ) * 3.3333333f * 8.0f;
	if ( scale > 1.0f ) scale = 1.0f;
	else if ( scale < 0.0f ) scale = 0.0f;
	return scale;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// Final C:10257CE0 branch. The exposed cl_glow_range_start/end do NOT replace
// these native literals. This does not supply native per-view visibility.
float TerrorGlow_GetDistanceAlpha( float flDistance, double flFOVRatio, float flCurrentAlpha )
{
	// C258403 retains this product for the 500 comparison and pow input;
	// only the separate 1500 comparison reads its float-width snapshot.
	const double flScaledDistance = flFOVRatio * (double)flDistance;
	const float flScaledDistanceFloat = (float)flScaledDistance;
	float flAlpha;
	if ( flScaledDistance <= 500.0 )
		flAlpha = 0.0f;
	else if ( flScaledDistanceFloat >= 1500.0f )
		flAlpha = 1.0f;
	else
		flAlpha = (float)pow( 1.0 - ( 1500.0 - flScaledDistance ) * 0.001f, cl_glow_range_exp.GetFloat() );
	const float flMaxAlpha = cl_glow_brightness.GetFloat() * 1.5f;
	if ( flAlpha > flMaxAlpha )
		flAlpha = flMaxAlpha;
	return flCurrentAlpha > flAlpha ? flCurrentAlpha : flAlpha;
}

#pragma float_control( pop )
