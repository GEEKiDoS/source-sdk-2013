//========= Copyright Valve Corporation, All rights reserved. ============//
// Compose into the existing clientshadowmgr.cpp owner, L4D2 only.
// Native C0D03D0 / C0C70E0. Registrations/state scheduling remain console-owned.

extern ConVar r_shadow_lightpos_lerptime;
extern ConVar r_shadow_shortenfactor;
extern ConVar r_shadowfromworldlights_debug;

#pragma float_control( precise, on, push )

// C35F4C0 uses the scalar x87 length, rounded to float before epsilon/divide.
// The SDK's inline VectorNormalize instead uses an approximate reciprocal root.
static void NormalizeWorldLightShadowVector( Vector &vec )
{
	const double x = vec.x;
	const double y = vec.y;
	const double z = vec.z;
	const float length = static_cast< float >( sqrt( x * x + y * y + z * z ) );
	const float inverseLength = 1.0f / ( length + FLT_EPSILON );
	vec.x *= inverseLength;
	vec.y *= inverseLength;
	vec.z *= inverseLength;
}

void CClientShadowMgr::GetShadowDirection( ClientShadowHandle_t handle, ShadowType_t shadowType, Vector &direction ) const
{
	const ClientShadow_t &shadow = m_Shadows[handle];
	IClientRenderable *pRenderable = ClientEntityList().GetClientRenderableFromHandle( shadow.m_Entity );
	if ( m_bShadowFromWorldLightsEnabled && !m_bShadowFromWorldLightsDisabled )
	{
		direction = shadow.m_vecShadowDirection;
	}
	else
	{
		direction = GetShadowDirection();
	}

	// Standing minimal-undefined-step rule: keep the chosen direction if the
	// renderable vanished; skip only its otherwise-unchecked virtual call.
	if ( pRenderable )
		pRenderable->GetShadowCastDirection( &direction, shadowType );
}

void CClientShadowMgr::UpdateShadowDirectionFromLocalLightSource( ClientShadowHandle_t handle )
{
	ClientShadow_t &shadow = m_Shadows[handle];
	IClientRenderable *pRenderable = ClientEntityList().GetClientRenderableFromHandle( shadow.m_Entity );
	if ( !pRenderable )
	{
		DevWarning( "%s(): Skipping shadow with invalid client renderable (shadow handle %d)\n",
			"CClientShadowMgr::UpdateShadowDirectionFromLocalLightSource", handle );
		return;
	}

	Vector mins, maxs;
	pRenderable->GetRenderBoundsWorldspace( mins, maxs );
	const Vector origin( ( maxs.x + mins.x ) * 0.5f, ( maxs.y + mins.y ) * 0.5f, mins.z );
	Vector lightPosition;
	if ( shadow.m_flLightPosLerp >= 1.0f )
	{
		// Retain this native caller-side read: it may recompute the render origin.
		pRenderable->GetRenderOrigin();
		// Main capability decision, 2026-10-23: original VEngineModel016 +100
		// queries a shadowing world-light position, with r_shadowfromanyworldlight
		// as its last argument. The target has no such service. Omit only that
		// query and take the original false-result branch; never guess a light.
		lightPosition.Init( FLT_MAX, FLT_MAX, FLT_MAX );
	}

	if ( shadow.m_flLightPosLerp == FLT_MAX )
	{
		shadow.m_vecCurrentLightPosition = lightPosition;
		shadow.m_vecTargetLightPosition = lightPosition;
		shadow.m_flLightPosLerp = 1.0f;
	}
	else if ( shadow.m_flLightPosLerp < 1.0f )
	{
		shadow.m_flLightPosLerp += gpGlobals->frametime / r_shadow_lightpos_lerptime.GetFloat();
		if ( shadow.m_flLightPosLerp > 1.0f )
			shadow.m_flLightPosLerp = 1.0f;
		else if ( shadow.m_flLightPosLerp < 0.0f )
			shadow.m_flLightPosLerp = 0.0f;

		if ( shadow.m_flLightPosLerp >= 1.0f )
		{
			shadow.m_vecCurrentLightPosition = shadow.m_vecTargetLightPosition;
			lightPosition = shadow.m_vecCurrentLightPosition;
		}
		else
		{
			Vector current = shadow.m_vecCurrentLightPosition;
			Vector target = shadow.m_vecTargetLightPosition;
			if ( current.x == FLT_MAX )
				current = origin - GetShadowDirection() * 200.0f;
			if ( target.x == FLT_MAX )
				target = origin - GetShadowDirection() * 200.0f;

			// Deliberate native quirk: test the stored positions, not the
			// sentinel-replaced temporaries used for interpolation below.
			const Vector currentDelta = origin - shadow.m_vecCurrentLightPosition;
			const Vector targetDelta = origin - shadow.m_vecTargetLightPosition;
			const float dot = DotProduct( targetDelta, currentDelta );
			if ( dot < 0.0f )
			{
				const Vector overhead( origin.x, origin.y, origin.z + 200.0f );
				if ( shadow.m_flLightPosLerp < 0.5f )
					lightPosition = current + ( overhead - current ) * ( shadow.m_flLightPosLerp * 2.0f );
				else
					lightPosition = overhead + ( target - overhead ) * ( shadow.m_flLightPosLerp * 2.0f - 1.0f );
			}
			else
			{
				lightPosition = current + ( target - current ) * shadow.m_flLightPosLerp;
			}

			// This is the native shared transparent/interpolating queue. Do not
			// force a transform change here: the native stationary-lerp quirk stays.
			m_TransparentShadows.AddToTail( handle );
		}
	}
	else if ( shadow.m_flLightPosLerp >= 1.0f )
	{
		if ( ( lightPosition - shadow.m_vecCurrentLightPosition ).LengthSqr() > 1.0f )
		{
			shadow.m_vecTargetLightPosition = lightPosition;
			shadow.m_flLightPosLerp = 0.0f;
			m_TransparentShadows.AddToTail( handle );
		}
		lightPosition = shadow.m_vecCurrentLightPosition;
	}
	else
	{
		// A NaN fraction reaches native C0D09B8 with an unwritten stack
		// light position. Standing rule306: skip only its dependent work.
		return;
	}

	if ( lightPosition.x == FLT_MAX )
		lightPosition = origin - GetShadowDirection() * 200.0f;

	Vector direction = origin - lightPosition;
	direction.z *= r_shadow_shortenfactor.GetFloat();
	NormalizeWorldLightShadowVector( direction );
	shadow.m_vecShadowDirection = direction;

	if ( r_shadowfromworldlights_debug.GetBool() )
	{
		// C0DCF40's line admission, including pause and unordered polarity.
		// Use the real debug-overlay endpoint, not the SDK helper's different
		// paused/distance policy.
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		if ( pPlayer )
		{
			bool draw = engine->IsPaused();
			if ( !draw && ( ( pPlayer->GetAbsOrigin() - lightPosition ).LengthSqr() <= 90000000.0f ||
				( pPlayer->GetAbsOrigin() - origin ).LengthSqr() <= 90000000.0f ) )
			{
				Vector forward;
				pPlayer->EyeVectors( &forward );
				draw = DotProduct( lightPosition - pPlayer->GetAbsOrigin(), forward ) >= 0.0f ||
					DotProduct( origin - pPlayer->GetAbsOrigin(), forward ) >= 0.0f;
			}
			if ( draw && debugoverlay )
				debugoverlay->AddLineOverlay( lightPosition, origin, 255, 255, 0, false, 0.0f );
		}
	}
}

#pragma float_control( pop )
