//========= Copyright Valve Corporation, All rights reserved. ============//
//
// An entity that allows level designer control over the fog parameters.
//
//=============================================================================

#include "cbase.h"
#include "fogcontroller.h"
#include "entityinput.h"
#include "entityoutput.h"
#include "eventqueue.h"
#include "player.h"
#include "world.h"
#include "ndebugoverlay.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CFogSystem s_FogSystem( "FogSystem" );

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CFogSystem *FogSystem( void )
{
	return &s_FogSystem;
}

LINK_ENTITY_TO_CLASS( env_fog_controller, CFogController );

BEGIN_DATADESC( CFogController )

	DEFINE_INPUTFUNC( FIELD_FLOAT,		"SetStartDist",	InputSetStartDist ),
	DEFINE_INPUTFUNC( FIELD_FLOAT,		"SetEndDist",	InputSetEndDist ),
	DEFINE_INPUTFUNC( FIELD_FLOAT,		"SetMaxDensity",	InputSetMaxDensity ),
	DEFINE_INPUTFUNC( FIELD_VOID,		"TurnOn",		InputTurnOn ),
	DEFINE_INPUTFUNC( FIELD_VOID,		"TurnOff",		InputTurnOff ),
	DEFINE_INPUTFUNC( FIELD_COLOR32,	"SetColor",		InputSetColor ),
	DEFINE_INPUTFUNC( FIELD_COLOR32,	"SetColorSecondary",	InputSetColorSecondary ),
	DEFINE_INPUTFUNC( FIELD_INTEGER,	"SetFarZ",		InputSetFarZ ),
	DEFINE_INPUTFUNC( FIELD_STRING,		"SetAngles",	InputSetAngles ),
#if !defined( L4D2 )
	DEFINE_INPUTFUNC( FIELD_BOOLEAN,	"SetRadial",	InputSetRadial ),
#endif

	DEFINE_INPUTFUNC( FIELD_COLOR32,	"SetColorLerpTo",		InputSetColorLerpTo ),
	DEFINE_INPUTFUNC( FIELD_COLOR32,	"SetColorSecondaryLerpTo",	InputSetColorSecondaryLerpTo ),
	DEFINE_INPUTFUNC( FIELD_FLOAT,		"SetStartDistLerpTo",	InputSetStartDistLerpTo ),
	DEFINE_INPUTFUNC( FIELD_FLOAT,		"SetEndDistLerpTo",	InputSetEndDistLerpTo ),
#if defined( L4D2 )
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetMaxDensityLerpTo", InputSetMaxDensityLerpTo ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "Set2DSkyboxFogFactorLerpTo", InputSet2DSkyboxFogFactorLerpTo ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "Set2DSkyboxFogFactor", InputSet2DSkyboxFogFactor ),
#endif
	DEFINE_INPUTFUNC( FIELD_VOID,		"StartFogTransition", InputStartFogTransition ),

	// Quiet classcheck
	//DEFINE_EMBEDDED( m_fog ),

	DEFINE_KEYFIELD( m_bUseAngles,			FIELD_BOOLEAN,	"use_angles" ),
	DEFINE_KEYFIELD( m_fog.colorPrimary,	FIELD_COLOR32,	"fogcolor" ),
	DEFINE_KEYFIELD( m_fog.colorSecondary,	FIELD_COLOR32,	"fogcolor2" ),
	DEFINE_KEYFIELD( m_fog.dirPrimary,		FIELD_VECTOR,	"fogdir" ),
	DEFINE_KEYFIELD( m_fog.enable,			FIELD_BOOLEAN,	"fogenable" ),
	DEFINE_KEYFIELD( m_fog.blend,			FIELD_BOOLEAN,	"fogblend" ),
	DEFINE_KEYFIELD( m_fog.start,			FIELD_FLOAT,	"fogstart" ),
	DEFINE_KEYFIELD( m_fog.end,				FIELD_FLOAT,	"fogend" ),
	DEFINE_KEYFIELD( m_fog.maxdensity,		FIELD_FLOAT,	"fogmaxdensity" ),
	DEFINE_KEYFIELD( m_fog.farz,			FIELD_FLOAT,	"farz" ),
	DEFINE_KEYFIELD( m_fog.duration,		FIELD_FLOAT,	"foglerptime" ),
#if defined( L4D2 )
	DEFINE_KEYFIELD( m_fog.HDRColorScale, FIELD_FLOAT, "HDRColorScale" ),
#endif
#if !defined( L4D2 )
	DEFINE_KEYFIELD( m_fog.radial,			FIELD_BOOLEAN,  "fogradial" ),
#endif

	DEFINE_THINKFUNC( SetLerpValues ),

	DEFINE_FIELD( m_iChangedVariables, FIELD_INTEGER ),

	DEFINE_FIELD( m_fog.lerptime, FIELD_TIME ),
	DEFINE_FIELD( m_fog.colorPrimaryLerpTo, FIELD_COLOR32 ),
	DEFINE_FIELD( m_fog.colorSecondaryLerpTo, FIELD_COLOR32 ),
	DEFINE_FIELD( m_fog.startLerpTo, FIELD_FLOAT ),
	DEFINE_FIELD( m_fog.endLerpTo, FIELD_FLOAT ),
#if defined( L4D2 )
	DEFINE_FIELD( m_fog.maxdensityLerpTo, FIELD_FLOAT ),
	DEFINE_FIELD( m_fog.skyboxFogFactor, FIELD_FLOAT ),
	DEFINE_FIELD( m_fog.skyboxFogFactorLerpTo, FIELD_FLOAT ),
#endif

END_DATADESC()

IMPLEMENT_SERVERCLASS_ST_NOBASE( CFogController, DT_FogController )
// fog data
	SendPropInt( SENDINFO_STRUCTELEM( m_fog.enable ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO_STRUCTELEM( m_fog.blend ), 1, SPROP_UNSIGNED ),
#if !defined( L4D2 )
	SendPropInt( SENDINFO_STRUCTELEM( m_fog.radial ), 1, SPROP_UNSIGNED ),
#endif
	SendPropVector( SENDINFO_STRUCTELEM(m_fog.dirPrimary), -1, SPROP_COORD),
	SendPropInt( SENDINFO_STRUCTELEM( m_fog.colorPrimary ), 32, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO_STRUCTELEM( m_fog.colorSecondary ), 32, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.start ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.end ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.maxdensity ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.farz ), 0, SPROP_NOSCALE ),
#if defined( L4D2 )
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.skyboxFogFactor ), 0, SPROP_NOSCALE ),
#endif

	SendPropInt( SENDINFO_STRUCTELEM( m_fog.colorPrimaryLerpTo ), 32, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO_STRUCTELEM( m_fog.colorSecondaryLerpTo ), 32, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.startLerpTo ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.endLerpTo ), 0, SPROP_NOSCALE ),
#if defined( L4D2 )
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.maxdensityLerpTo ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.skyboxFogFactorLerpTo ), 0, SPROP_NOSCALE ),
#endif
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.lerptime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.duration ), 0, SPROP_NOSCALE ),
#if defined( L4D2 )
	SendPropFloat( SENDINFO_STRUCTELEM( m_fog.HDRColorScale ), 0, SPROP_NOSCALE ),
#endif
END_SEND_TABLE()

CFogController::CFogController()
{
	// Make sure that old maps without fog fields don't get wacked out fog values.
	m_fog.enable = false;
	m_fog.maxdensity = 1.0f;
#if defined( L4D2 )
	m_fog.HDRColorScale = 1.0f;
#endif
}


CFogController::~CFogController()
{
}

void CFogController::Spawn( void )
{
	BaseClass::Spawn();

	m_fog.colorPrimaryLerpTo = m_fog.colorPrimary;
	m_fog.colorSecondaryLerpTo = m_fog.colorSecondary;
#if defined( L4D2 )
	m_fog.skyboxFogFactorLerpTo = m_fog.skyboxFogFactor;
#endif
}

//-----------------------------------------------------------------------------
// Activate!
//-----------------------------------------------------------------------------
void CFogController::Activate( ) 
{
	BaseClass::Activate();

	if ( m_bUseAngles )
	{
		AngleVectors( GetAbsAngles(), &m_fog.dirPrimary.GetForModify() );
		m_fog.dirPrimary.GetForModify() *= -1.0f; 
	}	    
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CFogController::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

//------------------------------------------------------------------------------
// Purpose: Input handler for setting the fog start distance.
//------------------------------------------------------------------------------
void CFogController::InputSetStartDist(inputdata_t &inputdata)
{
	// Get the world entity.
	m_fog.start = inputdata.value.Float();
}

//------------------------------------------------------------------------------
// Purpose: Input handler for setting the fog end distance.
//------------------------------------------------------------------------------
void CFogController::InputSetEndDist(inputdata_t &inputdata)
{
	// Get the world entity.
	m_fog.end = inputdata.value.Float();
}

//------------------------------------------------------------------------------
// Input handler for setting the maximum density of the fog. This lets us bring
// the start distance in without the scene fogging too much.
//------------------------------------------------------------------------------
void CFogController::InputSetMaxDensity( inputdata_t &inputdata )
{
	m_fog.maxdensity = inputdata.value.Float();
}

//------------------------------------------------------------------------------
// Purpose: Input handler for turning on the fog.
//------------------------------------------------------------------------------
void CFogController::InputTurnOn(inputdata_t &inputdata)
{
	// Get the world entity.
	m_fog.enable = true;
}

//------------------------------------------------------------------------------
// Purpose: Input handler for turning off the fog.
//------------------------------------------------------------------------------
void CFogController::InputTurnOff(inputdata_t &inputdata)
{
	// Get the world entity.
	m_fog.enable = false;
}

//------------------------------------------------------------------------------
// Purpose: Input handler for setting the primary fog color.
//------------------------------------------------------------------------------
void CFogController::InputSetColor(inputdata_t &inputdata)
{
	// Get the world entity.
	m_fog.colorPrimary = inputdata.value.Color32();
}


//------------------------------------------------------------------------------
// Purpose: Input handler for setting the secondary fog color.
//------------------------------------------------------------------------------
void CFogController::InputSetColorSecondary(inputdata_t &inputdata)
{
	// Get the world entity.
	m_fog.colorSecondary = inputdata.value.Color32();
}

void CFogController::InputSetFarZ(inputdata_t &inputdata)
{
	m_fog.farz = inputdata.value.Int();
}


//------------------------------------------------------------------------------
// Purpose: Sets the angles to use for the secondary fog direction.
//------------------------------------------------------------------------------
void CFogController::InputSetAngles( inputdata_t &inputdata )
{
	const char *pAngles = inputdata.value.String();

	QAngle angles;
	UTIL_StringToVector( angles.Base(), pAngles );

	Vector vTemp;
	AngleVectors( angles, &vTemp );
	SetAbsAngles( angles );

	AngleVectors( GetAbsAngles(), &m_fog.dirPrimary.GetForModify() );
	m_fog.dirPrimary.GetForModify() *= -1.0f;
}

#if !defined( L4D2 )
void CFogController::InputSetRadial( inputdata_t &inputdata )
{
	bool bRadial = inputdata.value.Bool();

	m_fog.radial = bRadial;
}
#endif


//-----------------------------------------------------------------------------
// Purpose: Draw any debug text overlays
// Output : Current text offset from the top
//-----------------------------------------------------------------------------
int CFogController::DrawDebugTextOverlays(void) 
{
	int text_offset = BaseClass::DrawDebugTextOverlays();

	if (m_debugOverlays & OVERLAY_TEXT_BIT) 
	{
		char tempstr[512];

		Q_snprintf(tempstr,sizeof(tempstr),"State: %s",(m_fog.enable)?"On":"Off");
		EntityText(text_offset,tempstr,0);
		text_offset++;

		Q_snprintf(tempstr,sizeof(tempstr),"Start: %3.0f",m_fog.start.Get());
		EntityText(text_offset,tempstr,0);
		text_offset++;

		Q_snprintf(tempstr,sizeof(tempstr),"End  : %3.0f",m_fog.end.Get());
		EntityText(text_offset,tempstr,0);
		text_offset++;

		color32 color = m_fog.colorPrimary;
		Q_snprintf(tempstr,sizeof(tempstr),"1) Red  : %i",color.r);
		EntityText(text_offset,tempstr,0);
		text_offset++;

		Q_snprintf(tempstr,sizeof(tempstr),"1) Green: %i",color.g);
		EntityText(text_offset,tempstr,0);
		text_offset++;

		Q_snprintf(tempstr,sizeof(tempstr),"1) Blue : %i",color.b);
		EntityText(text_offset,tempstr,0);
		text_offset++;

		color = m_fog.colorSecondary;
		Q_snprintf(tempstr,sizeof(tempstr),"2) Red  : %i",color.r);
		EntityText(text_offset,tempstr,0);
		text_offset++;

		Q_snprintf(tempstr,sizeof(tempstr),"2) Green: %i",color.g);
		EntityText(text_offset,tempstr,0);
		text_offset++;

		Q_snprintf(tempstr,sizeof(tempstr),"2) Blue : %i",color.b);
		EntityText(text_offset,tempstr,0);
		text_offset++;
	}
	return text_offset;
}

#define FOG_CONTROLLER_COLORPRIMARY_LERP 1
#define FOG_CONTROLLER_COLORSECONDARY_LERP 2
#define FOG_CONTROLLER_START_LERP 4
#define FOG_CONTROLLER_END_LERP 8

#if defined( L4D2 )
#define FOG_CONTROLLER_MAXDENSITY_LERP 16
#define FOG_CONTROLLER_SKYBOXFOGFACTOR_LERP 32

void CFogController::InputSetMaxDensityLerpTo( inputdata_t &data )
{
	m_iChangedVariables |= FOG_CONTROLLER_MAXDENSITY_LERP;
	m_fog.maxdensityLerpTo = data.value.Float();
}

void CFogController::InputSet2DSkyboxFogFactor( inputdata_t &data )
{
	m_fog.skyboxFogFactor = data.value.Float();
}

void CFogController::InputSet2DSkyboxFogFactorLerpTo( inputdata_t &data )
{
	m_iChangedVariables |= FOG_CONTROLLER_SKYBOXFOGFACTOR_LERP;
	m_fog.skyboxFogFactorLerpTo = data.value.Float();
}
#endif

void CFogController::InputSetColorLerpTo(inputdata_t &data)
{
	m_iChangedVariables |= FOG_CONTROLLER_COLORPRIMARY_LERP;
	m_fog.colorPrimaryLerpTo = data.value.Color32();
}

void CFogController::InputSetColorSecondaryLerpTo(inputdata_t &data)
{
	m_iChangedVariables |= FOG_CONTROLLER_COLORSECONDARY_LERP;
	m_fog.colorSecondaryLerpTo = data.value.Color32();
}

void CFogController::InputSetStartDistLerpTo(inputdata_t &data)
{
	m_iChangedVariables |= FOG_CONTROLLER_START_LERP;
	m_fog.startLerpTo = data.value.Float();
}

void CFogController::InputSetEndDistLerpTo(inputdata_t &data)
{
	m_iChangedVariables |= FOG_CONTROLLER_END_LERP;
	m_fog.endLerpTo = data.value.Float();
}

#if defined( L4D2 )
#pragma float_control( precise, on, push )
#endif
void CFogController::InputStartFogTransition(inputdata_t &data)
{
	SetThink( &CFogController::SetLerpValues );

#if defined( L4D2 )
	// Native S0D611F..0D6139 compares the unrounded x87 deadline before storing it.
	const double flLerpTime = static_cast< double >( gpGlobals->curtime ) + static_cast< double >( m_fog.duration.Get() ) + 0.1;
	if ( static_cast< double >( m_fog.lerptime.Get() ) != flLerpTime )
		m_fog.lerptime.SetDirect( static_cast< float >( flLerpTime ) );
#else
	m_fog.lerptime = gpGlobals->curtime + m_fog.duration + 0.1;
#endif
    SetNextThink( gpGlobals->curtime + m_fog.duration );
}
#if defined( L4D2 )
#pragma float_control( pop )
#endif

void CFogController::SetLerpValues( void )
{
	if ( m_iChangedVariables & FOG_CONTROLLER_COLORPRIMARY_LERP )
	{
		m_fog.colorPrimary = m_fog.colorPrimaryLerpTo;
	}

	if ( m_iChangedVariables & FOG_CONTROLLER_COLORSECONDARY_LERP )
	{
		m_fog.colorSecondary = m_fog.colorSecondaryLerpTo;
	} 

	if ( m_iChangedVariables & FOG_CONTROLLER_START_LERP )
	{
		m_fog.start = m_fog.startLerpTo;
	}

	if ( m_iChangedVariables & FOG_CONTROLLER_END_LERP )
	{
		m_fog.end = m_fog.endLerpTo;
	}

#if defined( L4D2 )
	if ( m_iChangedVariables & FOG_CONTROLLER_MAXDENSITY_LERP )
		m_fog.maxdensity = m_fog.maxdensityLerpTo;
	if ( m_iChangedVariables & FOG_CONTROLLER_SKYBOXFOGFACTOR_LERP )
		m_fog.skyboxFogFactor = m_fog.skyboxFogFactorLerpTo;
#endif
	m_iChangedVariables = 0;
	m_fog.lerptime = gpGlobals->curtime;
}


//-----------------------------------------------------------------------------
// Purpose: Clear out the fog controller.
//-----------------------------------------------------------------------------
void CFogSystem::LevelInitPreEntity( void )
{
#if defined( L4D2 )
	m_hMasterController = NULL;
	ListenForGameEvent( "round_start" );
#else
	m_pMasterController = NULL;
#endif
}

#if defined( L4D2 )
void CFogSystem::FindMasterController()
{
	CFogController *controller = NULL;
	while ( ( controller = static_cast< CFogController * >(
		gEntList.FindEntityByClassname( controller, "env_fog_controller" ) ) ) != NULL )
	{
		if ( !m_hMasterController.Get() || controller->IsMaster() )
			m_hMasterController = controller;
	}
}

void CFogSystem::FireGameEvent( IGameEvent *event )
{
	FindMasterController();
}
#endif

//-----------------------------------------------------------------------------
// Purpose: On level load find the master fog controller.  If no controller is 
//			set as Master, use the first fog controller found.
//-----------------------------------------------------------------------------
void CFogSystem::LevelInitPostEntity( void )
{
#if defined( L4D2 )
	FindMasterController();
#else
	CFogController *pFogController = NULL;
	do
	{
		pFogController = static_cast<CFogController*>( gEntList.FindEntityByClassname( pFogController, "env_fog_controller" ) );
		if ( pFogController )
		{
#if !defined( L4D2 )
			// HACK(misyl): If this is an official map then force on radial fog.
			if ( GameRules()->IsOfficialMap() )
			{
				pFogController->m_fog.radial = true;
			}
#endif

			if ( m_pMasterController == NULL )
			{
				m_pMasterController = pFogController;
			}
			else
			{
				if ( pFogController->IsMaster() )
				{
					m_pMasterController = pFogController;
				}
			}
		}
	} while ( pFogController );
#endif

	// HACK: Singleplayer games don't get a call to CBasePlayer::Spawn on level transitions.
	// CBasePlayer::Activate is called before this is called so that's too soon to set up the fog controller.
	// We don't have a hook similar to Activate that happens after LevelInitPostEntity
	// is called, or we could just do this in the player itself.
	if ( gpGlobals->maxClients == 1 )
	{
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		if ( pPlayer && ( pPlayer->m_Local.m_PlayerFog.m_hCtrl.Get() == NULL ) )
		{
			pPlayer->InitFogController();
		}
	}
}

