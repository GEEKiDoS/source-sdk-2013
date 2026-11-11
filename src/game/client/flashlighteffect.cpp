//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//===========================================================================//

#include "cbase.h"
#include "flashlighteffect.h"
#include "dlight.h"
#include "iefx.h"
#include "iviewrender.h"
#include "view.h"
#include "engine/ivdebugoverlay.h"
#include "tier0/vprof.h"
#include "tier1/KeyValues.h"
#include "toolframework_client.h"

#ifdef HL2_CLIENT_DLL
#include "c_basehlplayer.h"
#endif // HL2_CLIENT_DLL

#if defined( _X360 )
extern ConVar r_flashlightdepthres;
#else
extern ConVar r_flashlightdepthres;
#endif

// memdbgon must be the last include file in a .cpp file!!!
#if defined( L4D2 )
#include "cliententitylist.h"
#include "l4d2/player/c_terrorplayer.h"
#include "debugoverlay_shared.h"
#include <emmintrin.h>
#include "toolframework/itoolframework.h"
#include "materialsystem/itexture.h"
#include "texture_group_names.h"
#include "tier1/strtools.h"
#endif // L4D2
#include "tier0/memdbgon.h"

extern ConVar r_flashlightdepthtexture;

#if !defined( L4D2 )
void r_newflashlightCallback_f( IConVar *pConVar, const char *pOldString, float flOldValue );

static ConVar r_newflashlight( "r_newflashlight", "1", FCVAR_CHEAT, "", r_newflashlightCallback_f );
static ConVar r_swingflashlight( "r_swingflashlight", "1", FCVAR_CHEAT );
#endif
static ConVar r_flashlightlockposition( "r_flashlightlockposition", "0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
#if defined( L4D2 )
static ConVar r_flashlightfov( "r_flashlightfov", "53.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
#else
static ConVar r_flashlightfov( "r_flashlightfov", "45.0", FCVAR_CHEAT );
#endif
#if !defined( L4D2 )
static ConVar r_flashlightoffsetx( "r_flashlightoffsetx", "10.0", FCVAR_CHEAT );
static ConVar r_flashlightoffsety( "r_flashlightoffsety", "-20.0", FCVAR_CHEAT );
static ConVar r_flashlightoffsetz( "r_flashlightoffsetz", "24.0", FCVAR_CHEAT );
#endif
static ConVar r_flashlightnear( "r_flashlightnear", "4.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar r_flashlightfar( "r_flashlightfar", "750.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar r_flashlightconstant( "r_flashlightconstant", "0.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar r_flashlightlinear( "r_flashlightlinear", "100.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar r_flashlightquadratic( "r_flashlightquadratic", "0.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar r_flashlightvisualizetrace( "r_flashlightvisualizetrace", "0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar r_flashlightambient( "r_flashlightambient", "0.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar r_flashlightshadowatten( "r_flashlightshadowatten", "0.35", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar r_flashlightladderdist( "r_flashlightladderdist", "40.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar mat_slopescaledepthbias_shadowmap( "mat_slopescaledepthbias_shadowmap", "16", FCVAR_CHEAT );
static ConVar mat_depthbias_shadowmap(	"mat_depthbias_shadowmap", "0.0005", FCVAR_CHEAT  );


#if !defined( L4D2 )
void r_newflashlightCallback_f( IConVar *pConVar, const char *pOldString, float flOldValue )
{
	if( engine->GetDXSupportLevel() < 70 )
	{
		r_newflashlight.SetValue( 0 );
	}	
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : nEntIndex - The m_nEntIndex of the client entity that is creating us.
//			vecPos - The position of the light emitter.
//			vecDir - The direction of the light emission.
//-----------------------------------------------------------------------------
CFlashlightEffect::CFlashlightEffect(int nEntIndex)
{
	m_FlashlightHandle = CLIENTSHADOW_INVALID_HANDLE;
	m_nEntIndex = nEntIndex;

	m_bIsOn = false;
	m_pPointLight = NULL;
	if( engine->GetDXSupportLevel() < 70 )
	{
		r_newflashlight.SetValue( 0 );
	}	

	if ( g_pMaterialSystemHardwareConfig->SupportsBorderColor() )
	{
		m_FlashlightTexture.Init( "effects/flashlight_border", TEXTURE_GROUP_OTHER, true );
	}
	else
	{
		m_FlashlightTexture.Init( "effects/flashlight001", TEXTURE_GROUP_OTHER, true );
	}
}
#endif


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CFlashlightEffect::~CFlashlightEffect()
{
	LightOff();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFlashlightEffect::TurnOn()
{
	m_bIsOn = true;
	m_flDistMod = 1.0f;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFlashlightEffect::TurnOff()
{
	if (m_bIsOn)
	{
		m_bIsOn = false;
		LightOff();
	}
}

#if !defined( L4D2 )
// Custom trace filter that skips the player and the view model.
// If we don't do this, we'll end up having the light right in front of us all
// the time.
class CTraceFilterSkipPlayerAndViewModel : public CTraceFilter
{
public:
	virtual bool ShouldHitEntity( IHandleEntity *pServerEntity, int contentsMask )
	{
		// Test against the vehicle too?
		// FLASHLIGHTFIXME: how do you know that you are actually inside of the vehicle?
		C_BaseEntity *pEntity = EntityFromEntityHandle( pServerEntity );
		if ( !pEntity )
			return true;

		if ( ( dynamic_cast<C_BaseViewModel *>( pEntity ) != NULL ) ||
			 ( dynamic_cast<C_BasePlayer *>( pEntity ) != NULL ) ||
			 pEntity->GetCollisionGroup() == COLLISION_GROUP_DEBRIS ||
			 pEntity->GetCollisionGroup() == COLLISION_GROUP_INTERACTIVE_DEBRIS )
		{
			return false;
		}

		return true;
	}
};

//-----------------------------------------------------------------------------
// Purpose: Do the headlight
//-----------------------------------------------------------------------------
void CFlashlightEffect::UpdateLightNew(const Vector &vecPos, const Vector &vecForward, const Vector &vecRight, const Vector &vecUp )
{
	VPROF_BUDGET( "CFlashlightEffect::UpdateLightNew", VPROF_BUDGETGROUP_SHADOW_DEPTH_TEXTURING );

	FlashlightState_t state;

	// We will lock some of the flashlight params if player is on a ladder, to prevent oscillations due to the trace-rays
	bool bPlayerOnLadder = ( C_BasePlayer::GetLocalPlayer()->GetMoveType() == MOVETYPE_LADDER );

	const float flEpsilon = 0.1f;			// Offset flashlight position along vecUp
	const float flDistCutoff = 128.0f;
	const float flDistDrag = 0.2;

	CTraceFilterSkipPlayerAndViewModel traceFilter;
	float flOffsetY = r_flashlightoffsety.GetFloat();

	if( r_swingflashlight.GetBool() )
	{
		// This projects the view direction backwards, attempting to raise the vertical
		// offset of the flashlight, but only when the player is looking down.
		Vector vecSwingLight = vecPos + vecForward * -12.0f;
		if( vecSwingLight.z > vecPos.z )
		{
			flOffsetY += (vecSwingLight.z - vecPos.z);
		}
	}

	Vector vOrigin = vecPos + flOffsetY * vecUp;

	// Not on ladder...trace a hull
	if ( !bPlayerOnLadder ) 
	{
		trace_t pmOriginTrace;
		UTIL_TraceHull( vecPos, vOrigin, Vector(-4, -4, -4), Vector(4, 4, 4), MASK_SOLID & ~(CONTENTS_HITBOX), &traceFilter, &pmOriginTrace );

		if ( pmOriginTrace.DidHit() )
		{
			vOrigin = vecPos;
		}
	}
	else // on ladder...skip the above hull trace
	{
		vOrigin = vecPos;
	}

	// Now do a trace along the flashlight direction to ensure there is nothing within range to pull back from
	int iMask = MASK_OPAQUE_AND_NPCS;
	iMask &= ~CONTENTS_HITBOX;
	iMask |= CONTENTS_WINDOW;

	Vector vTarget = vecPos + vecForward * r_flashlightfar.GetFloat();

	// Work with these local copies of the basis for the rest of the function
	Vector vDir   = vTarget - vOrigin;
	Vector vRight = vecRight;
	Vector vUp    = vecUp;
	VectorNormalize( vDir   );
	VectorNormalize( vRight );
	VectorNormalize( vUp    );

	// Orthonormalize the basis, since the flashlight texture projection will require this later...
	vUp -= DotProduct( vDir, vUp ) * vDir;
	VectorNormalize( vUp );
	vRight -= DotProduct( vDir, vRight ) * vDir;
	VectorNormalize( vRight );
	vRight -= DotProduct( vUp, vRight ) * vUp;
	VectorNormalize( vRight );

	AssertFloatEquals( DotProduct( vDir, vRight ), 0.0f, 1e-3 );
	AssertFloatEquals( DotProduct( vDir, vUp    ), 0.0f, 1e-3 );
	AssertFloatEquals( DotProduct( vRight, vUp  ), 0.0f, 1e-3 );

	trace_t pmDirectionTrace;
	UTIL_TraceHull( vOrigin, vTarget, Vector( -4, -4, -4 ), Vector( 4, 4, 4 ), iMask, &traceFilter, &pmDirectionTrace );

	if ( r_flashlightvisualizetrace.GetBool() == true )
	{
		if ( debugoverlay )
		{
			debugoverlay->AddBoxOverlay( pmDirectionTrace.endpos, Vector( -4, -4, -4 ), Vector( 4, 4, 4 ), QAngle( 0, 0, 0 ), 0, 0, 255, 16, 0 );
			debugoverlay->AddLineOverlay( vOrigin, pmDirectionTrace.endpos, 255, 0, 0, false, 0 );
		}
	}

	float flDist = (pmDirectionTrace.endpos - vOrigin).Length();
	if ( flDist < flDistCutoff )
	{
		// We have an intersection with our cutoff range
		// Determine how far to pull back, then trace to see if we are clear
		float flPullBackDist = bPlayerOnLadder ? r_flashlightladderdist.GetFloat() : flDistCutoff - flDist;	// Fixed pull-back distance if on ladder
		m_flDistMod = Lerp( flDistDrag, m_flDistMod, flPullBackDist );
		
		if ( !bPlayerOnLadder )
		{
			trace_t pmBackTrace;
			UTIL_TraceHull( vOrigin, vOrigin - vDir*(flPullBackDist-flEpsilon), Vector( -4, -4, -4 ), Vector( 4, 4, 4 ), iMask, &traceFilter, &pmBackTrace );
			if( pmBackTrace.DidHit() )
			{
				// We have an intersection behind us as well, so limit our m_flDistMod
				float flMaxDist = (pmBackTrace.endpos - vOrigin).Length() - flEpsilon;
				if( m_flDistMod > flMaxDist )
					m_flDistMod = flMaxDist;
			}
		}
	}
	else
	{
		m_flDistMod = Lerp( flDistDrag, m_flDistMod, 0.0f );
	}
	vOrigin = vOrigin - vDir * m_flDistMod;

	state.m_vecLightOrigin = vOrigin;

	BasisToQuaternion( vDir, vRight, vUp, state.m_quatOrientation );

	state.m_fQuadraticAtten = r_flashlightquadratic.GetFloat();

	bool bFlicker = false;

#ifdef HL2_EPISODIC
	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	if ( pPlayer )
	{
		float flBatteryPower = ( pPlayer->m_HL2Local.m_flFlashBattery >= 0.0f ) ? ( pPlayer->m_HL2Local.m_flFlashBattery ) : pPlayer->m_HL2Local.m_flSuitPower;
		if ( flBatteryPower <= 10.0f )
		{
			float flScale;
			if ( flBatteryPower >= 0.0f )
			{	
				flScale = ( flBatteryPower <= 4.5f ) ? SimpleSplineRemapVal( flBatteryPower, 4.5f, 0.0f, 1.0f, 0.0f ) : 1.0f;
			}
			else
			{
				flScale = SimpleSplineRemapVal( flBatteryPower, 10.0f, 4.8f, 1.0f, 0.0f );
			}
			
			flScale = clamp( flScale, 0.0f, 1.0f );

			if ( flScale < 0.35f )
			{
				float flFlicker = cosf( gpGlobals->curtime * 6.0f ) * sinf( gpGlobals->curtime * 15.0f );
				
				if ( flFlicker > 0.25f && flFlicker < 0.75f )
				{
					// On
					state.m_fLinearAtten = r_flashlightlinear.GetFloat() * flScale;
				}
				else
				{
					// Off
					state.m_fLinearAtten = 0.0f;
				}
			}
			else
			{
				float flNoise = cosf( gpGlobals->curtime * 7.0f ) * sinf( gpGlobals->curtime * 25.0f );
				state.m_fLinearAtten = r_flashlightlinear.GetFloat() * flScale + 1.5f * flNoise;
			}

			state.m_fHorizontalFOVDegrees = r_flashlightfov.GetFloat() - ( 16.0f * (1.0f-flScale) );
			state.m_fVerticalFOVDegrees = r_flashlightfov.GetFloat() - ( 16.0f * (1.0f-flScale) );
			
			bFlicker = true;
		}
	}
#endif // HL2_EPISODIC

	if ( bFlicker == false )
	{
		state.m_fLinearAtten = r_flashlightlinear.GetFloat();
		state.m_fHorizontalFOVDegrees = r_flashlightfov.GetFloat();
		state.m_fVerticalFOVDegrees = r_flashlightfov.GetFloat();
	}

	state.m_fConstantAtten = r_flashlightconstant.GetFloat();
	state.m_Color[0] = 1.0f;
	state.m_Color[1] = 1.0f;
	state.m_Color[2] = 1.0f;
	state.m_Color[3] = r_flashlightambient.GetFloat();
	state.m_NearZ = r_flashlightnear.GetFloat() + m_flDistMod;	// Push near plane out so that we don't clip the world when the flashlight pulls back 
	state.m_FarZ = r_flashlightfar.GetFloat();
	state.m_bEnableShadows = r_flashlightdepthtexture.GetBool();
	state.m_flShadowMapResolution = r_flashlightdepthres.GetInt();

	state.m_pSpotlightTexture = m_FlashlightTexture;
	state.m_nSpotlightTextureFrame = 0;

	state.m_flShadowAtten = r_flashlightshadowatten.GetFloat();
	state.m_flShadowSlopeScaleDepthBias = mat_slopescaledepthbias_shadowmap.GetFloat();
	state.m_flShadowDepthBias = mat_depthbias_shadowmap.GetFloat();

	if( m_FlashlightHandle == CLIENTSHADOW_INVALID_HANDLE )
	{
		m_FlashlightHandle = g_pClientShadowMgr->CreateFlashlight( state );
	}
	else
	{
		if( !r_flashlightlockposition.GetBool() )
		{
			g_pClientShadowMgr->UpdateFlashlightState( m_FlashlightHandle, state );
		}
	}
	
	g_pClientShadowMgr->UpdateProjectedTexture( m_FlashlightHandle, true );
	
	// Kill the old flashlight method if we have one.
	LightOffOld();

#ifndef NO_TOOLFRAMEWORK
	if ( clienttools->IsInRecordingMode() )
	{
		KeyValues *msg = new KeyValues( "FlashlightState" );
		msg->SetFloat( "time", gpGlobals->curtime );
		msg->SetInt( "entindex", m_nEntIndex );
		msg->SetInt( "flashlightHandle", m_FlashlightHandle );
		msg->SetPtr( "flashlightState", &state );
		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, msg );
		msg->deleteThis();
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Do the headlight
//-----------------------------------------------------------------------------
void CFlashlightEffect::UpdateLightOld(const Vector &vecPos, const Vector &vecDir, int nDistance)
{
	if ( !m_pPointLight || ( m_pPointLight->key != m_nEntIndex ))
	{
		// Set up the environment light
		m_pPointLight = effects->CL_AllocDlight(m_nEntIndex);
		m_pPointLight->flags = 0.0f;
		m_pPointLight->radius = 80;
	}
	
	// For bumped lighting
	VectorCopy(vecDir, m_pPointLight->m_Direction);
	
	Vector end;
	end = vecPos + nDistance * vecDir;
	
	// Trace a line outward, skipping the player model and the view model.
	trace_t pm;
	CTraceFilterSkipPlayerAndViewModel traceFilter;
	UTIL_TraceLine( vecPos, end, MASK_ALL, &traceFilter, &pm );
	VectorCopy( pm.endpos, m_pPointLight->origin );
	
	float falloff = pm.fraction * nDistance;
	
	if ( falloff < 500 )
		falloff = 1.0;
	else
		falloff = 500.0 / falloff;
	
	falloff *= falloff;
	
	m_pPointLight->radius = 80;
	m_pPointLight->color.r = m_pPointLight->color.g = m_pPointLight->color.b = 255 * falloff;
	m_pPointLight->color.exponent = 0;
	
	// Make it live for a bit
	m_pPointLight->die = gpGlobals->curtime + 0.2f;
	
	// Update list of surfaces we influence
	render->TouchLight( m_pPointLight );
	
	// kill the new flashlight if we have one
	LightOffNew();
}

//-----------------------------------------------------------------------------
// Purpose: Do the headlight
//-----------------------------------------------------------------------------
void CFlashlightEffect::UpdateLight(const Vector &vecPos, const Vector &vecDir, const Vector &vecRight, const Vector &vecUp, int nDistance)
{
	if ( !m_bIsOn )
	{
		return;
	}
	if( r_newflashlight.GetBool() )
	{
		UpdateLightNew( vecPos, vecDir, vecRight, vecUp );
	}
	else
	{
		UpdateLightOld( vecPos, vecDir, nDistance );
	}
}
#endif


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFlashlightEffect::LightOffNew()
{
#ifndef NO_TOOLFRAMEWORK
	if ( clienttools->IsInRecordingMode() )
	{
		KeyValues *msg = new KeyValues( "FlashlightState" );
		msg->SetFloat( "time", gpGlobals->curtime );
		msg->SetInt( "entindex", m_nEntIndex );
		msg->SetInt( "flashlightHandle", m_FlashlightHandle );
		msg->SetPtr( "flashlightState", NULL );
		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, msg );
		msg->deleteThis();
	}
#endif

	// Clear out the light
	if( m_FlashlightHandle != CLIENTSHADOW_INVALID_HANDLE )
	{
		g_pClientShadowMgr->DestroyFlashlight( m_FlashlightHandle );
		m_FlashlightHandle = CLIENTSHADOW_INVALID_HANDLE;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void CFlashlightEffect::LightOffOld()
{	
	if ( m_pPointLight && ( m_pPointLight->key == m_nEntIndex ) )
	{
		m_pPointLight->die = gpGlobals->curtime;
		m_pPointLight = NULL;
	}
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
void CFlashlightEffect::LightOff()
{
	LightOffNew();
}
#else
void CFlashlightEffect::LightOff()
{	
#if !defined( L4D2 )
	LightOffOld();
#endif
	LightOffNew();
}
#endif

CHeadlightEffect::CHeadlightEffect() 
{

}

CHeadlightEffect::~CHeadlightEffect()
{
	
}

void CHeadlightEffect::UpdateLight( const Vector &vecPos, const Vector &vecDir, const Vector &vecRight, const Vector &vecUp, int nDistance )
{
	if ( IsOn() == false )
		 return;

	FlashlightState_t state;
	Vector basisX, basisY, basisZ;
	basisX = vecDir;
	basisY = vecRight;
	basisZ = vecUp;
	VectorNormalize(basisX);
	VectorNormalize(basisY);
	VectorNormalize(basisZ);

	BasisToQuaternion( basisX, basisY, basisZ, state.m_quatOrientation );
		
	state.m_vecLightOrigin = vecPos;

	state.m_fHorizontalFOVDegrees = 45.0f;
	state.m_fVerticalFOVDegrees = 30.0f;
	state.m_fQuadraticAtten = r_flashlightquadratic.GetFloat();
	state.m_fLinearAtten = r_flashlightlinear.GetFloat();
	state.m_fConstantAtten = r_flashlightconstant.GetFloat();
	state.m_Color[0] = 1.0f;
	state.m_Color[1] = 1.0f;
	state.m_Color[2] = 1.0f;
	state.m_Color[3] = r_flashlightambient.GetFloat();
	state.m_NearZ = r_flashlightnear.GetFloat();
	state.m_FarZ = r_flashlightfar.GetFloat();
	state.m_bEnableShadows = true;
	state.m_pSpotlightTexture = m_FlashlightTexture;
	state.m_nSpotlightTextureFrame = 0;
	
	if( GetFlashlightHandle() == CLIENTSHADOW_INVALID_HANDLE )
	{
		SetFlashlightHandle( g_pClientShadowMgr->CreateFlashlight( state ) );
	}
	else
	{
		g_pClientShadowMgr->UpdateFlashlightState( GetFlashlightHandle(), state );
	}
	
	g_pClientShadowMgr->UpdateProjectedTexture( GetFlashlightHandle(), true );
}

// Compose into EXISTING client/flashlighteffect.cpp only on L4D2. No new TU,
// engine class/header/vtable extension, second physical manager or fake backend.
// Includes c_baseplayer.h,l4d2/c_terrorplayer.h,cliententitylist.h,
// debugoverlay_shared.h,<emmintrin.h> before existing memdbgon.
// Main-approved engine-ABI gaps ONLY: native extra state float+56=farZ, and
// opaque muzzle FindTexture creation bit0x80000. Target state/texture offsets
// stay native to TARGET; use real texture, shadow backend and live mat_* bias.
#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

extern "C" double L4D2_GlowNormalize( Vector *value ); // Sole actual1035F4C0, no SDK approximation.
extern ConVar r_visualizetraces;
extern ConVar r_flashlightmuzzleflashfov;
extern ConVar r_flashlightnearoffsetscale;
extern ConVar r_flashlightbacktraceoffset;
extern ConVar r_flashlighttracedistcutoff;
extern ConVar r_flashlighttracedistwatercutoff;
static ConVar r_flashlight_topdown( "r_flashlight_topdown", "0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 ); // Native4FEA60 RAW0 ->2.

static FORCEINLINE float FlashlightSqrt( float squared )
{
	return _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( squared ) ) );
}
static float FlashlightDistance( const Vector &a, const Vector &b )
{
	const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
	return FlashlightSqrt( ( x * x + y * y ) + z * z );
}

// Whole nativeECDF0. Regular player update traces other players; mode override
// passes tracePlayers=false and skips ALL players. Always skip actual owner,
// actual GetBaseAnimating()->IsViewModel, debris1 and interactive-debris3.
class CTraceFilterSkipPlayerAndViewModel : public CTraceFilter
{
public:
	CTraceFilterSkipPlayerAndViewModel( C_BasePlayer *player, bool skipPlayers ) :
		m_pPlayer( player ), m_bSkipPlayers( skipPlayers ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int contentsMask ) OVERRIDE
	{
		C_BaseEntity *entity = EntityFromEntityHandle( handle );
		if ( !entity ) return true;
		C_BaseAnimating *animating = entity->GetBaseAnimating();
		if ( animating && animating->IsViewModel() ) return false;
		if ( entity == m_pPlayer || ( m_bSkipPlayers && entity->IsPlayer() ) ) return false;
		const int group = entity->GetCollisionGroup();
		return group != COLLISION_GROUP_DEBRIS && group != COLLISION_GROUP_INTERACTIVE_DEBRIS;
	}
private:
	C_BasePlayer *m_pPlayer;
	bool m_bSkipPlayers;
};

static void TraceFlashlightHull( const Vector &start, const Vector &end,
	const Vector &mins, const Vector &maxs, unsigned int mask,
	CTraceFilterSkipPlayerAndViewModel &filter, trace_t &trace )
{
	Ray_t ray;
	ray.Init( start, end, mins, maxs );
	enginetrace->TraceRay( ray, mask, &filter, &trace );
	if ( r_visualizetraces.GetInt() != 0 )
		DebugDrawLine( trace.startpos, trace.endpos, 255, 255, 0, true, -1.0f );
}

// Same genuine native Gram-Schmidt body in ED470 and ECE70. Preserve
// (origin+direction*distance)-origin cancellation BEFORE common normalization.
static void BuildFlashlightBasis( const Vector &origin, const Vector &direction,
	const Vector &right, const Vector &up, float distance, Vector &end,
	Vector &forwardOut, Vector &rightOut, Vector &upOut )
{
	end.x = direction.x * distance + origin.x;
	end.y = direction.y * distance + origin.y;
	end.z = direction.z * distance + origin.z;
	forwardOut.x = end.x - origin.x;
	forwardOut.y = end.y - origin.y;
	forwardOut.z = end.z - origin.z;
	rightOut = right;
	upOut = up;
	L4D2_GlowNormalize( &forwardOut );
	L4D2_GlowNormalize( &rightOut );
	L4D2_GlowNormalize( &upOut );
	float dot = ( forwardOut.x * upOut.x + upOut.y * forwardOut.y ) + upOut.z * forwardOut.z;
	upOut.x -= forwardOut.x * dot;
	upOut.y -= forwardOut.y * dot;
	upOut.z -= forwardOut.z * dot;
	L4D2_GlowNormalize( &upOut );
	dot = ( forwardOut.x * rightOut.x + rightOut.y * forwardOut.y ) + rightOut.z * forwardOut.z;
	rightOut.x -= forwardOut.x * dot;
	rightOut.y -= forwardOut.y * dot;
	rightOut.z -= forwardOut.z * dot;
	L4D2_GlowNormalize( &rightOut );
	dot = ( rightOut.x * upOut.x + rightOut.y * upOut.y ) + rightOut.z * upOut.z;
	rightOut.x -= dot * upOut.x;
	rightOut.y -= upOut.y * dot;
	rightOut.z -= upOut.z * dot;
	L4D2_GlowNormalize( &rightOut );
}

// Whole nativeECCD0. Both genuine texture references construct before body.
CFlashlightEffect::CFlashlightEffect( int entIndex, const char *textureName, float fov, float farZ, float linear )
{
	m_flDistMod = 1.0f;
	m_flMuzzleFlashBrightness = 1.0f;
	m_flFlashlightFOV = fov;
	m_flFlashlightFarZ = farZ;
	m_FlashlightHandle = CLIENTSHADOW_INVALID_HANDLE;
	m_nEntIndex = entIndex;
	m_bMuzzleFlash = false;
	m_flFlashlightLinear = linear;
	m_bFlashlightShadows = true;
	m_bIsOn = false;
	UpdateTextureName( textureName );
	// Main-authorized opaque creation-bit gap: real target texture load, no
	// wrong TEXTUREFLAGS_STAGING_MEMORY/SRGB guess or invented creation flag.
	m_MuzzleFlashTexture.Init( "effects/muzzleflash_light", TEXTURE_GROUP_OTHER, true );
}

void CFlashlightEffect::UpdateTextureName( const char *textureName ) // Whole ECA00.
{
	const char *name = textureName ? textureName : "";
	if ( !m_FlashlightTexture.IsValid() || Q_stricmp( m_szFlashlightTextureName, name ) != 0 )
	{
		m_FlashlightTexture.Init( textureName ? textureName : "effects/flashlight001", TEXTURE_GROUP_OTHER, true );
		V_strncpy( m_szFlashlightTextureName, name, sizeof( m_szFlashlightTextureName ) );
	}
}
void CFlashlightEffect::SetMuzzleFlashState( bool active, float brightness ) // Whole EC860.
{
	m_bMuzzleFlash = active;
	m_flMuzzleFlashBrightness = brightness;
}

// Whole nativeED470. Actual actor geometry is supplied by four cached vectors
// and virtual1104/7136. Do not redo25FE90 viewmodel/high-low geometry here.
bool CFlashlightEffect::ComputeLightPosAndOrientation( const Vector &sourceOrigin,
	const Vector &direction, const Vector &right, const Vector &up,
	Vector &lightOrigin, Quaternion &orientation, bool tracePlayers )
{
	const bool visualize = r_flashlightvisualizetrace.GetBool();
	float cutoff = r_flashlighttracedistcutoff.GetFloat();
	C_BasePlayer *player = UTIL_PlayerByIndex( m_nEntIndex );
	if ( !player ) player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player ) return false;
	const bool ladder = player->GetMoveType() == MOVETYPE_LADDER;
	CTraceFilterSkipPlayerAndViewModel filter( player, !tracePlayers );
	Vector offset;
	player->ComputeFlashlightOffset( direction, right, up, offset ); // Native even on ladder.
	Vector origin;
	origin.x = sourceOrigin.x + offset.x;
	origin.y = offset.y + sourceOrigin.y;
	origin.z = offset.z + sourceOrigin.z;
	if ( ladder ) origin = sourceOrigin;
	else
	{
		const Vector &viewOffset = player->GetViewOffset();
		const Vector &renderOrigin = player->GetRenderOrigin();
		Vector eye;
		eye.x = viewOffset.x + renderOrigin.x;
		eye.y = renderOrigin.y + viewOffset.y;
		eye.z = renderOrigin.z + viewOffset.z;
		trace_t trace;
		TraceFlashlightHull( eye, origin, Vector( -2, -2, -2 ), Vector( 2, 2, 2 ), 0x0200400B, filter, trace );
		const bool hit = trace.fraction < 1.0f || trace.allsolid || trace.startsolid;
		if ( visualize )
		{
			NDebugOverlay::Box( trace.endpos, Vector( -2, -2, -2 ), Vector( 2, 2, 2 ), 0, 255, 0, 16, 0.0f );
			NDebugOverlay::Line( trace.startpos, trace.endpos, 255, hit ? 128 : 0, hit ? 128 : 0, true, 0.0f );
		}
		if ( hit ) origin = trace.endpos;
		else if ( eye != player->m_vecFlashlightOrigin ) origin = sourceOrigin;
	}

	Vector end, forwardBasis, rightBasis, upBasis;
	BuildFlashlightBasis( origin, direction, right, up, r_flashlightfar.GetFloat(),
		end, forwardBasis, rightBasis, upBasis );
	unsigned int mask = 0x020060AB;
	trace_t trace;
	TraceFlashlightHull( origin, end, Vector( -1.5f, -1.5f, -1.5f ),
		Vector( 1.5f, 1.5f, 1.5f ), mask, filter, trace );
	if ( trace.startsolid ) trace.endpos = origin;
	// Native startsolid ALSO takes the second water-ignore trace regardless of contents.
	if ( trace.startsolid || ( trace.contents & CONTENTS_WATER ) )
	{
		mask = 0x0200608B;
		trace_t waterTrace;
		TraceFlashlightHull( origin, end, Vector( -1.5f, -1.5f, -1.5f ),
			Vector( 1.5f, 1.5f, 1.5f ), mask, filter, waterTrace );
		cutoff = FlashlightDistance( waterTrace.endpos, trace.endpos ) + cutoff;
		const float waterCutoff = r_flashlighttracedistwatercutoff.GetFloat();
		if ( cutoff > waterCutoff ) cutoff = waterCutoff;
		if ( visualize )
			NDebugOverlay::Box( waterTrace.endpos, Vector( -3, -3, -3 ), Vector( 3, 3, 3 ), 255, 0, 0, 16, 0.0f );
	}
	if ( visualize )
	{
		NDebugOverlay::Box( trace.endpos, Vector( -4, -4, -4 ), Vector( 4, 4, 4 ), 0, 0, 255, 16, 0.0f );
		NDebugOverlay::Line( origin, trace.endpos, 255, 0, 0, false, 0.0f );
	}
	// Keep FIRST water-contact endpoint; do not replace it by waterTrace.endpos.
	const float distance = FlashlightDistance( trace.endpos, origin );
	float pullback = 0.0f;
	if ( cutoff > distance )
	{
		pullback = ladder ? r_flashlightladderdist.GetFloat() : cutoff - distance;
		if ( !ladder )
		{
			const float adjusted = pullback - 0.1f;
			Vector backEnd, backStart;
			backEnd.x = origin.x - adjusted * forwardBasis.x;
			backEnd.y = origin.y - forwardBasis.y * adjusted;
			backEnd.z = origin.z - forwardBasis.z * adjusted;
			const float backScale = r_flashlightbacktraceoffset.GetFloat() * cutoff;
			backStart.x = origin.x - backScale * forwardBasis.x;
			backStart.y = origin.y - forwardBasis.y * backScale;
			backStart.z = origin.z - forwardBasis.z * backScale;
			trace_t backTrace;
			TraceFlashlightHull( backStart, backEnd, Vector( -1.5f, -1.5f, -1.5f ),
				Vector( 1.5f, 1.5f, 1.5f ), mask & ~CONTENTS_WATER, filter, backTrace );
			if ( visualize ) NDebugOverlay::Line( backTrace.startpos, backTrace.endpos, 255, 0, 255, true, 0.0f );
			if ( backTrace.fraction < 1.0f || backTrace.allsolid || backTrace.startsolid )
			{
				const float candidate = FlashlightDistance( backTrace.endpos, origin ) - 0.1f;
				if ( pullback > candidate ) pullback = candidate;
			}
		}
	}
	if ( visualize )
	{
		NDebugOverlay::Box( origin - forwardBasis * m_flDistMod, Vector( -2, -2, -2 ),
			Vector( 2, 2, 2 ), 255, 255, 0, 16, 0.0f );
		NDebugOverlay::Box( origin - forwardBasis * pullback, Vector( -1, -1, -1 ),
			Vector( 1, 1, 1 ), 128, 128, 0, 16, 0.0f );
	}
	m_flDistMod = ( pullback - m_flDistMod ) * 0.2f + m_flDistMod;
	if ( !( cutoff > m_flDistMod ) ) m_flDistMod = cutoff; // Native COMISS/JA unordered selects cutoff.
	lightOrigin.x = origin.x - m_flDistMod * forwardBasis.x;
	lightOrigin.y = origin.y - m_flDistMod * forwardBasis.y;
	lightOrigin.z = origin.z - m_flDistMod * forwardBasis.z;
	BasisToQuaternion( forwardBasis, rightBasis, upBasis, orientation );
	return true;
}

// Whole nativeEE5F0. No renderer emulation or layout/offset alias.
bool CFlashlightEffect::UpdateDefaultFlashlightState( FlashlightState_t &state,
	const Vector &origin, const Vector &forward, const Vector &right, const Vector &up,
	bool shadows, bool tracePlayers )
{
	VPROF_BUDGET( "CFlashlightEffect::UpdateDefaultFlashlightState", VPROF_BUDGETGROUP_SHADOW_DEPTH_TEXTURING );
	if ( !ComputeLightPosAndOrientation( origin, forward, right, up,
		state.m_vecLightOrigin, state.m_quatOrientation, tracePlayers ) ) return false;
	state.m_fQuadraticAtten = r_flashlightquadratic.GetFloat();
	state.m_fLinearAtten = m_flFlashlightLinear > 0.0f ? m_flFlashlightLinear : r_flashlightlinear.GetFloat();
	state.m_fHorizontalFOVDegrees = state.m_fVerticalFOVDegrees =
		m_flFlashlightFOV > 0.0f ? m_flFlashlightFOV : r_flashlightfov.GetFloat();
	if ( m_bMuzzleFlash )
		state.m_fHorizontalFOVDegrees = state.m_fVerticalFOVDegrees = r_flashlightmuzzleflashfov.GetFloat();
	state.m_fConstantAtten = r_flashlightconstant.GetFloat();
	state.m_Color[0] = state.m_Color[1] = state.m_Color[2] = 1.0f;
	state.m_Color[3] = r_flashlightambient.GetFloat();
	state.m_NearZ = r_flashlightnearoffsetscale.GetFloat() * m_flDistMod + r_flashlightnear.GetFloat();
	state.m_FarZ = m_flFlashlightFarZ > 0.0f ? m_flFlashlightFarZ : r_flashlightfar.GetFloat();
	// Main-approved engine-ABI gap: native also writes extra FLOAT+56=farZ.
	// TARGET has no member; do not overwrite its Color[0] or invent FarZAtten.
	state.m_bEnableShadows = shadows && r_flashlightdepthtexture.GetBool();
	state.m_flShadowMapResolution = static_cast< float >( r_flashlightdepthres.GetInt() );
	if ( m_bMuzzleFlash )
	{
		state.m_pSpotlightTexture = m_MuzzleFlashTexture;
		state.m_Color[0] = state.m_Color[1] = state.m_Color[2] = m_flMuzzleFlashBrightness;
	}
	else state.m_pSpotlightTexture = m_FlashlightTexture;
	state.m_nSpotlightTextureFrame = 0;
	state.m_flShadowAtten = r_flashlightshadowatten.GetFloat();
	state.m_flShadowSlopeScaleDepthBias = mat_slopescaledepthbias_shadowmap.GetFloat();
	state.m_flShadowDepthBias = mat_depthbias_shadowmap.GetFloat();
	return true;
}

// Shared native Create/Update/UpdateProjected/tool-record sequence in three
// physical methods; one real target backend, one handle, no state copy/cache.
void CFlashlightEffect::SubmitFlashlightState( FlashlightState_t &state )
{
	if ( m_FlashlightHandle == CLIENTSHADOW_INVALID_HANDLE )
		m_FlashlightHandle = g_pClientShadowMgr->CreateFlashlight( state );
	else if ( !r_flashlightlockposition.GetBool() )
		g_pClientShadowMgr->UpdateFlashlightState( m_FlashlightHandle, state );
	if ( m_FlashlightHandle != CLIENTSHADOW_INVALID_HANDLE )
		g_pClientShadowMgr->SetFlashlightEffectOwned( m_FlashlightHandle );
	g_pClientShadowMgr->UpdateProjectedTexture( m_FlashlightHandle, true );
#ifndef NO_TOOLFRAMEWORK
	if ( clienttools->IsInRecordingMode() )
	{
		KeyValues *message = new KeyValues( "FlashlightState" );
		message->SetFloat( "time", gpGlobals->curtime );
		message->SetInt( "entindex", m_nEntIndex );
		message->SetInt( "flashlightHandle", m_FlashlightHandle );
		message->SetPtr( "flashlightState", &state );
		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, message );
		message->deleteThis();
	}
#endif
}

// Whole nativeEE860. Top-down branch PRECEDES ent/param/texture/shadow writes.
void CFlashlightEffect::UpdateLight( int entIndex, const Vector &origin,
	const Vector &forward, const Vector &right, const Vector &up,
	float fov, float farZ, float linear, bool shadows, const char *textureName )
{
	VPROF_BUDGET( "CFlashlightEffect::UpdateLight", VPROF_BUDGETGROUP_SHADOW_DEPTH_TEXTURING );
	if ( r_flashlight_topdown.GetBool() )
	{
		UpdateLightTopDown( origin, forward, right, up );
		return;
	}
	m_flFlashlightFOV = fov;
	m_flFlashlightFarZ = farZ;
	m_nEntIndex = entIndex;
	m_flFlashlightLinear = linear;
	if ( m_bFlashlightShadows != shadows ) LightOffNew();
	m_bFlashlightShadows = shadows;
	UpdateTextureName( textureName );
	FlashlightState_t state;
	if ( UpdateDefaultFlashlightState( state, origin, forward, right, up, shadows, true ) )
		SubmitFlashlightState( state );
}

// Whole nativeEEB50. Exact mode update fixes shadows=true/tracePlayers=false
// at manager callsite; the physical method itself preserves both parameters.
void CFlashlightEffect::UpdateLightOverride( int entIndex, const Vector &origin,
	const Vector &forward, const Vector &right, const Vector &up, float fov,
	bool shadows, ITexture *texture, const Vector &color, bool tracePlayers )
{
	VPROF_BUDGET( "CFlashlightEffect::UpdateLight", VPROF_BUDGETGROUP_SHADOW_DEPTH_TEXTURING );
	m_nEntIndex = entIndex;
	if ( m_bFlashlightShadows != shadows ) LightOffNew();
	m_bFlashlightShadows = shadows;
	FlashlightState_t state;
	if ( !UpdateDefaultFlashlightState( state, origin, forward, right, up, shadows, tracePlayers ) ) return;
	state.m_fHorizontalFOVDegrees = state.m_fVerticalFOVDegrees = fov;
	state.m_Color[0] = color.x;
	state.m_Color[1] = color.y;
	state.m_Color[2] = color.z;
	if ( texture ) state.m_pSpotlightTexture = texture;
	SubmitFlashlightState( state );
}

// Whole nativeECE70. No actor offset, corrective/long trace or custom/muzzle
// param update in this distinct real top-down mode.
void CFlashlightEffect::UpdateLightTopDown( const Vector &origin, const Vector &direction,
	const Vector &right, const Vector &up )
{
	VPROF_BUDGET( "CFlashlightEffect::UpdateLightTopDown", VPROF_BUDGETGROUP_SHADOW_DEPTH_TEXTURING );
	FlashlightState_t state;
	state.m_vecLightOrigin = origin;
	Vector end, forwardBasis, rightBasis, upBasis;
	BuildFlashlightBasis( origin, direction, right, up, r_flashlightfar.GetFloat(),
		end, forwardBasis, rightBasis, upBasis );
	BasisToQuaternion( forwardBasis, rightBasis, upBasis, state.m_quatOrientation );
	state.m_fConstantAtten = r_flashlightconstant.GetFloat();
	state.m_fQuadraticAtten = r_flashlightquadratic.GetFloat();
	state.m_fLinearAtten = r_flashlightlinear.GetFloat();
	state.m_fHorizontalFOVDegrees = state.m_fVerticalFOVDegrees = r_flashlightfov.GetFloat();
	state.m_Color[0] = state.m_Color[1] = state.m_Color[2] = 1.0f;
	state.m_Color[3] = r_flashlightambient.GetFloat();
	state.m_NearZ = r_flashlightnear.GetFloat() + m_flDistMod;
	state.m_FarZ = r_flashlightfar.GetFloat(); // Same Main-approved extra native float gap.
	state.m_bEnableShadows = r_flashlightdepthtexture.GetBool();
	state.m_flShadowMapResolution = static_cast< float >( r_flashlightdepthres.GetInt() );
	state.m_pSpotlightTexture = m_FlashlightTexture;
	state.m_nSpotlightTextureFrame = 0;
	state.m_flShadowAtten = r_flashlightshadowatten.GetFloat();
	state.m_flShadowSlopeScaleDepthBias = mat_slopescaledepthbias_shadowmap.GetFloat();
	state.m_flShadowDepthBias = mat_depthbias_shadowmap.GetFloat();
	SubmitFlashlightState( state );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif
