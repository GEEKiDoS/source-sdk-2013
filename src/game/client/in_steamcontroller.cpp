//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Mouse input routines
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//


#include "cbase.h"
#include "basehandle.h"
#include "utlvector.h"
#include "cdll_client_int.h"
#include "cdll_util.h"
#include "kbutton.h"
#include "usercmd.h"
#include "input.h"
#include "iviewrender.h"
#include "convar.h"
#include "hud.h"
#include "vgui/ISurface.h"
#include "vgui_controls/Controls.h"
#include "vgui/Cursor.h"
#include "tier0/icommandline.h"
#include "inputsystem/iinputsystem.h"
#include "inputsystem/ButtonCode.h"
#include "math.h"
#include "tier1/convar_serverbounded.h"
#include "cam_thirdperson.h"
#include "ienginevgui.h"
#if defined( L4D2 )
#include "l4d2/ui/terror_radial_menu.h"
#include "l4d2/ui/cs_textwindow.h"
#endif

#if defined( _X360 )
#include "xbox/xbox_win32stubs.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifndef UINT64_MAX
const uint64 UINT64_MAX = 0xffffffffffffffff;
#endif

// up / down
#define	PITCH	0
// left / right
#define	YAW		1

extern const ConVar *sv_cheats;

extern ConVar cam_idealyaw;
extern ConVar cam_idealpitch;
extern ConVar thirdperson_platformer;

extern ConVar cl_forwardspeed;
extern ConVar cl_backspeed;
extern ConVar cl_sidespeed;

static ConVar sc_yaw_sensitivity( "sc_yaw_sensitivity","1.0", 
#if defined( L4D2 )
	0x00008088
#else
	FCVAR_ARCHIVE
#endif
 , "SteamController yaw factor." );
static ConVar sc_yaw_sensitivity_default( "sc_yaw_sensitivity_default","1.0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_NONE
#endif
 );

static ConVar sc_pitch_sensitivity( "sc_pitch_sensitivity",
#if defined( L4D2 )
	"0.9"
#else
	"0.75"
#endif
, 
#if defined( L4D2 )
	0x00008088
#else
	FCVAR_ARCHIVE
#endif
 , "SteamController pitch factor." );
static ConVar sc_pitch_sensitivity_default( "sc_pitch_sensitivity_default",
#if defined( L4D2 )
	"0.9"
#else
	"0.75"
#endif
, 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_NONE
#endif
 );

#if defined( L4D2 )
extern ConVar sc_mouse_sensitivity;
extern ConVar cl_inputmode;
extern ConVar joy_variable_frametime;
extern ConVar joy_lookspin_default;
extern ConVar cl_pitchdown;
extern ConVar cl_pitchup;
extern bool IN_IsLookSpinPressed();
extern bool IN_IsResetCameraHeld();
extern void L4D2ControllerLook( float yaw, float pitch, float frametime, bool platformer,
	float &yawDelta, float &pitchDelta );
#else
ConVar sc_look_sensitivity_scale( "sc_look_sensitivity_scale", "0.125", FCVAR_NONE, "Steam Controller look sensitivity global scale factor." );
#endif

#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CInput::ApplySteamControllerCameraMove( QAngle& viewangles, CUserCmd *cmd, Vector2D vecPosition )
{
#if defined( L4D2 )
	// Whole C135AA0: mouse-like Steam input uses the native sensitivity owner.
	const bool thirdPerson = CAM_IsThirdPerson() != 0;
	Vector offset;
	if ( thirdPerson )
	{
		offset = g_ThirdPersonManager.GetCameraOffsetAngles();
		if ( vecPosition.x != 0.0f )
		{
			offset[YAW] -= vecPosition.x;
			cam_idealyaw.SetValue( offset[YAW] - viewangles[YAW] );
		}
	}
	else
		viewangles[YAW] -= sc_yaw_sensitivity.GetFloat() * vecPosition.x;
	if ( thirdPerson && thirdperson_platformer.GetInt() )
	{
		if ( vecPosition.y != 0.0f )
		{
			offset[PITCH] += vecPosition.y;
			cam_idealpitch.SetValue( offset[PITCH] - viewangles[PITCH] );
		}
	}
	else
	{
		viewangles[PITCH] -= sc_pitch_sensitivity.GetFloat() * vecPosition.y;
		viewangles[PITCH] = clamp( viewangles[PITCH], -cl_pitchup.GetFloat(), cl_pitchdown.GetFloat() );
	}
	if ( thirdPerson )
		g_ThirdPersonManager.SetCameraOffsetAngles( offset );
	cmd->mousedx = (int)vecPosition.x;
	cmd->mousedy = (int)vecPosition.y;
#else
	//roll the view angles so roll is 0 (the HL2 assumed state) and mouse adjustments are relative to the screen.
	//Assuming roll is unchanging, we want mouse left to translate to screen left at all times (same for right, up, and down)	

	ConVarRef cl_pitchdown ( "cl_pitchdown" );
	ConVarRef cl_pitchup ( "cl_pitchup" );

	// Scale yaw and pitch inputs by sensitivity, and make sure they are within acceptable limits (important to avoid exploits, e.g. during Demoman charge we must restrict allowed yaw).
	float yaw = CAM_CapYaw( sc_yaw_sensitivity.GetFloat() * vecPosition.x );
	float pitch = CAM_CapPitch( sc_pitch_sensitivity.GetFloat() * vecPosition.y );

	if ( CAM_IsThirdPerson() )
	{
		if ( vecPosition.x )
		{
			auto vecCameraOffset = g_ThirdPersonManager.GetCameraOffsetAngles();

			// use the mouse to orbit the camera around the player, and update the idealAngle
			vecCameraOffset[YAW] -= yaw;
			cam_idealyaw.SetValue( vecCameraOffset[ YAW ] - viewangles[ YAW ] );
			viewangles[YAW] -= yaw;
		}
	}
	else
	{
		// Otherwize, use mouse to spin around vertical axis
		viewangles[YAW] -= yaw;
	}

	if ( CAM_IsThirdPerson() && thirdperson_platformer.GetInt() )
	{
		if ( vecPosition.y )
		{
			// use the mouse to orbit the camera around the player, and update the idealAngle
			auto vecCameraOffset = g_ThirdPersonManager.GetCameraOffsetAngles();
			vecCameraOffset[PITCH] += pitch;
			cam_idealpitch.SetValue( vecCameraOffset[ PITCH ] - viewangles[ PITCH ] );
		}
	}
	else
	{
		viewangles[PITCH] -= pitch;

		// Check pitch bounds
		viewangles[PITCH] = clamp ( viewangles[PITCH], -cl_pitchdown.GetFloat(), cl_pitchup.GetFloat() );
	}		

	// Finally, add mouse state to usercmd.
	// NOTE:  Does rounding to int cause any issues?  ywb 1/17/04
	cmd->mousedx = (int)vecPosition.x;
	cmd->mousedy = (int)vecPosition.y;
#endif
}
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif

#define CONTROLLER_ACTION_FLAGS_NONE 0
#define CONTROLLER_ACTION_FLAGS_STOPS_TAUNT ( 1 << 0 )
#define CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE ( 1 << 1 )
		
#if defined( L4D2 )
// The L4D2 table's third field selects a native radial callback, not TF flags.
enum TerrorSteamRadialCallback
{
	TERROR_STEAM_RADIAL_QA = 1,
	TERROR_STEAM_RADIAL_ORDERS,
	TERROR_STEAM_RADIAL_ACCEPT,
	TERROR_STEAM_RADIAL_CANCEL
};
#endif
struct ControllerDigitalActionToCommand
{
	const char* action;
	const char* cmd;
	int flags;
};

// Map action names to commands. Mostly these are identity mappings, but we need to handle the case of mapping to a command
// string that contains spaces (e.g. for "vote option1"), which are not allowed in action names.
static ControllerDigitalActionToCommand g_ControllerDigitalGameActions[] =
{
#if defined( L4D2 )
	// C135D00/off_106D7500: action names strip '+', commands retain it.
	{ "attack", "+attack", 0 },
	{ "attack2", "+attack2", 0 },
	{ "zoom", "+zoom", 0 },
	{ "reload", "+reload", 0 },
	{ "use", "+use", 0 },
	{ "jump", "+jump", 0 },
	{ "duck", "+duck", 0 },
	{ "toggle_duck", "toggle_duck", 0 },
	{ "speed", "+speed", 0 },
	{ "changeclass", "changeclass", 0 },
	{ "togglescores", "togglescores", 0 },
	{ "flashlight", "impulse 100", 0 },
	{ "slot1", "slot1", 0 },
	{ "slot2", "slot2", 0 },
	{ "slot3", "slot3", 0 },
	{ "slot4", "slot4", 0 },
	{ "slot5", "slot5", 0 },
	{ "lookspin", "+lookspin", 0 },
	{ "invnext", "invnext", 0 },
	{ "invprev", "invprev", 0 },
	{ "lastinv", "lastinv", 0 },
	{ "voicerecord", "+voicerecord", 0 },
	{ "radial_orders", "radial_orders", TERROR_STEAM_RADIAL_ORDERS },
	{ "radial_qa", "radial_qa", TERROR_STEAM_RADIAL_QA },
	{ "resetcamera", "+resetcamera", 0 },
	{ "spray", "impulse 201", 0 },
	{ "radial_accept", "radial_accept", TERROR_STEAM_RADIAL_ACCEPT },
	{ "radial_cancel", "radial_cancel", TERROR_STEAM_RADIAL_CANCEL },
	{ "sc_say", "sc_say", 0 },
#else
	{ "duck", "+duck", CONTROLLER_ACTION_FLAGS_NONE },
	{ "attack", "+attack", CONTROLLER_ACTION_FLAGS_NONE },
	{ "attack2", "+attack2", CONTROLLER_ACTION_FLAGS_NONE },
	{ "attack3", "+attack3", CONTROLLER_ACTION_FLAGS_NONE },
	{ "jump", "+jump", CONTROLLER_ACTION_FLAGS_STOPS_TAUNT },
	{ "use_action_slot_item", "+use_action_slot_item", CONTROLLER_ACTION_FLAGS_NONE },
	{ "invprev", "invprev", CONTROLLER_ACTION_FLAGS_STOPS_TAUNT|CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "invnext", "invnext", CONTROLLER_ACTION_FLAGS_STOPS_TAUNT|CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "reload", "+reload", CONTROLLER_ACTION_FLAGS_NONE },
	{ "dropitem", "dropitem", CONTROLLER_ACTION_FLAGS_STOPS_TAUNT|CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "changeclass", "changeclass", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "changeteam", "changeteam", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "open_charinfo_direct", "open_charinfo_direct", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "open_charinfo_backpack", "open_charinfo_backpack", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "inspect", "+inspect", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "taunt", "+taunt", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "voicerecord", "+voicerecord", CONTROLLER_ACTION_FLAGS_NONE },
	{ "show_quest_log", "show_quest_log", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "showscores", "+showscores", CONTROLLER_ACTION_FLAGS_NONE },
	{ "callvote", "callvote", CONTROLLER_ACTION_FLAGS_NONE },
	{ "cl_trigger_first_notification", "cl_trigger_first_notification", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "cl_decline_first_notification", "cl_decline_first_notification", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "cl_trigger_first_notification", "vote option1", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },						// In here twice, because we overload this action to issue both commands
	{ "cl_decline_first_notification", "vote option2", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },						// In here twice, because we overload this action to issue both commands
	{ "vote_option3", "vote option3", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "vote_option4", "vote option4", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "vote_option5", "vote option5", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "next_target", "spec_next", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },			// In the spectator action set only
	{ "prev_target", "spec_prev", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },			// In the spectator action set only
	{ "voice_medic", "voicemenu 0 0", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "voice_thanks", "voicemenu 0 1", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "voice_gogogo", "voicemenu 0 2", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "voice_moveup", "voicemenu 0 3", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "voice_spy", "voicemenu 1 1", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "voice_uberready", "voicemenu 1 7", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
	{ "voice_help", "voicemenu 2 0", CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE },
#endif
};

struct ControllerDigitalActionState {
	const char* cmd;
	ControllerDigitalActionHandle_t handle;
	bool bState;
	bool bAwaitingDebounce;
};

static ControllerDigitalActionState g_ControllerDigitalActionState[ARRAYSIZE(g_ControllerDigitalGameActions)];

static ControllerAnalogActionHandle_t g_ControllerMoveHandle;
static ControllerAnalogActionHandle_t g_ControllerCameraHandle;
#if defined( L4D2 )
static ControllerAnalogActionHandle_t g_ControllerJoystickCameraHandle;
static int g_nL4D2SteamControllerCount;
static ControllerDigitalActionState g_AdditionalControllerDigitalActionState
	[STEAM_CONTROLLER_MAX_COUNT - 1][ARRAYSIZE( g_ControllerDigitalGameActions )];

static ControllerDigitalActionState &L4D2SteamActionState( int controller, int action )
{
	return controller == 0 ? g_ControllerDigitalActionState[action] :
		g_AdditionalControllerDigitalActionState[controller - 1][action];
}

static void L4D2SteamRadialAction( int callback, bool pressed )
{
	if ( callback == TERROR_STEAM_RADIAL_QA || callback == TERROR_STEAM_RADIAL_ORDERS )
	{
		const char *name = callback == TERROR_STEAM_RADIAL_QA ? "QA" : "Orders";
		if ( pressed )
			OpenRadialMenu( name );
		else
			CloseRadialMenu( name, true );
	}
	else if ( pressed )
	{
		CloseRadialMenu( "QA", callback == TERROR_STEAM_RADIAL_ACCEPT );
		CloseRadialMenu( "Orders", callback == TERROR_STEAM_RADIAL_ACCEPT );
	}
}
#endif

bool CInput::InitializeSteamControllerGameActionSets()
{
	auto steamcontroller = g_pInputSystem->SteamControllerInterface();
	if ( !steamcontroller )
	{
		return false;
	}

#if defined( L4D2 )
	uint64 handles[STEAM_CONTROLLER_MAX_COUNT];
	const int count = steamcontroller->GetConnectedControllers( handles );
	g_nL4D2SteamControllerCount = 0;
	for ( int action = 0; action < ARRAYSIZE( g_ControllerDigitalGameActions ); ++action )
	{
		for ( int controller = 0; controller < count; ++controller )
		{
			ControllerDigitalActionState &state = L4D2SteamActionState( controller, action );
			state.handle = steamcontroller->GetDigitalActionHandle( g_ControllerDigitalGameActions[action].action );
			if ( action == 0 && state.handle == 0 )
				return false;
			state.cmd = g_ControllerDigitalGameActions[action].cmd;
			state.bState = false;
			state.bAwaitingDebounce = false;
		}
	}
	g_ControllerMoveHandle = steamcontroller->GetAnalogActionHandle( "Move" );
	g_ControllerCameraHandle = steamcontroller->GetAnalogActionHandle( "Camera" );
	g_ControllerJoystickCameraHandle = steamcontroller->GetAnalogActionHandle( "JoystickCamera" );
	g_nL4D2SteamControllerCount = count;
	return count != 0;
#else
	bool bGotHandle = true;

	for ( int i = 0; i < ARRAYSIZE( g_ControllerDigitalGameActions ); ++i )
	{
		const char* action = g_ControllerDigitalGameActions[i].action;
		const char* cmd = g_ControllerDigitalGameActions[i].cmd;

		ControllerDigitalActionState& state = g_ControllerDigitalActionState[i];
		state.handle = steamcontroller->GetDigitalActionHandle( action );
		bGotHandle = bGotHandle && ( state.handle != 0 );
		//if (!state.handle)
		//	Warning("Failed to initialize action %s\n", action);
		state.cmd = cmd;
		state.bState = false;
		state.bAwaitingDebounce = false;
	}

	g_ControllerMoveHandle = steamcontroller->GetAnalogActionHandle( "Move" );
	g_ControllerCameraHandle = steamcontroller->GetAnalogActionHandle( "Camera" );

	m_PreferredGameActionSet = GAME_ACTION_SET_MENUCONTROLS;

	return true;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: SteamControllerMove -- main entry point for applying Steam Controller Movements
// Input  : *cmd - 
//-----------------------------------------------------------------------------
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CInput::SteamControllerMove( float flFrametime, CUserCmd *cmd )
{
#if defined( L4D2 )
	// C135F50: digital actions, movement, reset/spin, mouse camera, then stick camera.
	if ( ( enginevgui->IsGameUIVisible() || IsTerrorTextWindowInputActive() ) &&
		!HasTerrorRadialInputOverride() )
		return;
	ISteamController *steamcontroller = g_pInputSystem->SteamControllerInterface();
	if ( !steamcontroller )
		return;
	uint64 handles[STEAM_CONTROLLER_MAX_COUNT];
	const int count = steamcontroller->GetConnectedControllers( handles );
	if ( count <= 0 )
		return;
	if ( count != g_nL4D2SteamControllerCount || !m_bSteamControllerGameActionsInitialized )
	{
		m_bSteamControllerGameActionsInitialized = InitializeSteamControllerGameActionSets();
		if ( !m_bSteamControllerGameActionsInitialized )
			return;
	}
	g_pInputSystem->ActivateSteamControllerActionSet( m_PreferredGameActionSet );
	QAngle viewangles;
	engine->GetViewAngles( viewangles );
	view->StopPitchDrift();
	float yawDelta = 0.0f;
	for ( int controller = 0; controller < count; ++controller )
	{
		for ( int action = 0; action < ARRAYSIZE( g_ControllerDigitalGameActions ); ++action )
		{
			ControllerDigitalActionState &state = L4D2SteamActionState( controller, action );
			const ControllerDigitalActionData_t data = steamcontroller->GetDigitalActionData( handles[controller], state.handle );
			if ( data.bState == state.bState )
				continue;
			ControllerDigitalActionToCommand &mapping = g_ControllerDigitalGameActions[action];
			if ( mapping.flags != 0 )
				L4D2SteamRadialAction( mapping.flags, data.bState );
			else if ( data.bState || state.cmd[0] == '+' )
			{
				char command[128];
				Q_strncpy( command, state.cmd, sizeof( command ) );
				if ( !data.bState )
					command[0] = '-';
				engine->ClientCmd_Unrestricted( command );
			}
			state.bState = data.bState;
			m_bSteamControllerSeenInput = true;
		}
		const ControllerAnalogActionData_t movement = steamcontroller->GetAnalogActionData( handles[controller], g_ControllerMoveHandle );
		cmd->forwardmove += ( movement.y > 0.0f ? cl_forwardspeed.GetFloat() : cl_backspeed.GetFloat() ) * movement.y;
		cmd->sidemove += cl_sidespeed.GetFloat() * movement.x;
		// Native107C97A8 retains digital input but suppresses camera sampling.
		if ( HasTerrorRadialInputOverride() )
			continue;

		const bool hasSample = !( m_flRemainingSteamControllerSampleTime <= 0.0f );
		const float sampleTime = MIN( m_flRemainingSteamControllerSampleTime, flFrametime );
		m_flRemainingSteamControllerSampleTime -= sampleTime;
		const bool variableFrameTime = joy_variable_frametime.GetBool();
		const float frameTime = variableFrameTime ? sampleTime : gpGlobals->frametime;
		const bool processLookInput = variableFrameTime || sampleTime != gpGlobals->frametime;
		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
		if ( m_flRemainingLookSpinTime == 0.0f && IN_IsResetCameraHeld() &&
			player && player->GetObserverMode() == OBS_MODE_NONE )
		{
			m_flRemainingResetCameraTime = joy_lookspin_default.GetFloat() * 0.5f;
			m_flResetCameraPitchRate = -( viewangles[PITCH] / m_flRemainingResetCameraTime );
		}
		if ( m_flRemainingLookSpinTime == 0.0f && IN_IsLookSpinPressed() &&
			player && player->GetObserverMode() == OBS_MODE_NONE )
		{
			m_flRemainingLookSpinTime = joy_lookspin_default.GetFloat();
			m_flLookSpinYawRate = ( m_flLastJoystickYawDelta < 0.0f ? -180.0f : 180.0f ) / m_flRemainingLookSpinTime;
		}
		if ( m_flRemainingResetCameraTime != 0.0f )
		{
			float step = frameTime;
			const float remaining = m_flRemainingResetCameraTime - step;
			if ( !( remaining <= 0.0f ) )
				m_flRemainingResetCameraTime = remaining;
			else
			{
				step = m_flRemainingResetCameraTime;
				m_flRemainingResetCameraTime = 0.0f;
			}
			const float delta = m_flResetCameraPitchRate * step;
			viewangles[PITCH] += delta;
			cmd->mousedy = (int)delta;
		}
		else if ( m_flRemainingLookSpinTime != 0.0f )
		{
			float step = frameTime;
			const float remaining = m_flRemainingLookSpinTime - step;
			if ( !( remaining <= 0.0f ) )
				m_flRemainingLookSpinTime = remaining;
			else
			{
				step = m_flRemainingLookSpinTime;
				m_flRemainingLookSpinTime = 0.0f;
			}
			yawDelta = m_flLookSpinYawRate * step;
			viewangles[YAW] += yawDelta;
			cmd->mousedx = (int)yawDelta;
		}
		else
		{
			const ControllerAnalogActionData_t mouse = steamcontroller->GetAnalogActionData( handles[controller], g_ControllerCameraHandle );
			const Vector2D mouseDelta( mouse.x * sc_mouse_sensitivity.GetFloat(), -mouse.y * sc_mouse_sensitivity.GetFloat() );
			if ( mouseDelta.LengthSqr() > 0.0f && !m_fCameraInterceptingMouse )
			{
				cl_inputmode.SetValue( 2 );
				const float previousYaw = viewangles[YAW];
				ApplySteamControllerCameraMove( viewangles, cmd, mouseDelta );
				yawDelta += viewangles[YAW] - previousYaw;
			}
			const ControllerAnalogActionData_t stick = steamcontroller->GetAnalogActionData( handles[controller], g_ControllerJoystickCameraHandle );
			if ( stick.bActive )
			{
				yawDelta = 0.0f;
				if ( hasSample )
				{
					float pitchDelta = 0.0f;
					if ( CAM_IsThirdPerson() && thirdperson_platformer.GetInt() )
					{
						L4D2ControllerLook( stick.x, stick.y, frameTime, true, yawDelta, pitchDelta );
						Vector offset = g_ThirdPersonManager.GetCameraOffsetAngles();
						offset[PITCH] += pitchDelta;
						offset[YAW] += yawDelta;
						g_ThirdPersonManager.SetCameraOffsetAngles( offset );
						if ( stick.x != 0.0f || stick.y != 0.0f )
						{
							cam_idealpitch.SetValue( offset[PITCH] - viewangles[PITCH] );
							cam_idealyaw.SetValue( offset[YAW] - viewangles[YAW] );
						}
						yawDelta = 0.0f;
					}
					else
					{
						if ( processLookInput )
							L4D2ControllerLook( stick.x, stick.y, frameTime, false, yawDelta, pitchDelta );
						viewangles[YAW] += yawDelta;
						viewangles[PITCH] += pitchDelta;
						cmd->mousedx = (int)( cmd->mousedx + yawDelta );
						cmd->mousedy = (int)( cmd->mousedy + pitchDelta );
						view->StopPitchDrift();
						if ( fabs( stick.x ) > 0.5f || fabs( stick.y ) > 0.5f )
							cl_inputmode.SetValue( 1 );
						viewangles[PITCH] = clamp( viewangles[PITCH], -cl_pitchup.GetFloat(), cl_pitchdown.GetFloat() );
					}
				}
			}
		}
	}
	if ( yawDelta != 0.0f )
		m_flLastJoystickYawDelta = yawDelta;
	engine->SetViewAngles( viewangles );
#else
	// Make sure we have an interface
	auto steamcontroller = g_pInputSystem->SteamControllerInterface();
	if ( !steamcontroller )
	{
		return;
	}

	// Check there is a controller connected. Do this check before we ask about handles to avoid thrashing the
	// handle query interface in the case where no controller is connected.
	uint64 nControllerHandles[STEAM_CONTROLLER_MAX_COUNT];
	int nControllerCount = steamcontroller->GetConnectedControllers( nControllerHandles );
	if ( nControllerCount <= 0 )
	{
		return;
	}

	// Initialize action handles if we haven't successfully done so already.
	if ( !m_bSteamControllerGameActionsInitialized )
	{
		m_bSteamControllerGameActionsInitialized = InitializeSteamControllerGameActionSets();
		if ( !m_bSteamControllerGameActionsInitialized )
		{
			return;
		}
	}

	g_pInputSystem->ActivateSteamControllerActionSet( m_PreferredGameActionSet );														  

	QAngle	viewangles;
	engine->GetViewAngles( viewangles );

	view->StopPitchDrift();

	bool bTaunting = m_GameActionSetFlags & GAME_ACTION_SET_FLAGS_TAUNTING;

	uint64 controller = nControllerHandles[0];
	bool bReceivedInput = false;
	for ( int i = 0; i < ARRAYSIZE( g_ControllerDigitalActionState ); ++i )
	{
		ControllerDigitalActionToCommand& cmdmap = g_ControllerDigitalGameActions[ i ];
		ControllerDigitalActionState& state = g_ControllerDigitalActionState[ i ];
		ControllerDigitalActionData_t data = steamcontroller->GetDigitalActionData( controller, state.handle );

		if ( !m_bSteamControllerSeenInput && data.bState )
		{
			Msg( "Seen controller input\n" );
			m_bSteamControllerSeenInput = true;
		}

		if ( data.bActive )
		{
			state.bAwaitingDebounce = state.bAwaitingDebounce && data.bState;

			if ( data.bState != state.bState )
			{
				bReceivedInput = true;
				if ( ( data.bState && !state.bAwaitingDebounce ) || state.cmd[0] == '+' )
				{
					char cmdbuf[128];
					Q_snprintf( cmdbuf, sizeof( cmdbuf ), "%s", state.cmd );
					if ( !data.bState )
					{
						cmdbuf[0] = '-';
					}

					engine->ClientCmd_Unrestricted( cmdbuf );

					// Hack - if we're taunting, we manufacture a stop taunt command for certain inputs (keyboard equivalent of this goes through another codepath).
					if ( bTaunting && data.bState && ( cmdmap.flags & CONTROLLER_ACTION_FLAGS_STOPS_TAUNT ) )
					{
						engine->ClientCmd_Unrestricted( "stop_taunt" );
					}
				}

				state.bState = data.bState;
			}
		}
	}

	ControllerAnalogActionData_t moveData = steamcontroller->GetAnalogActionData( controller, g_ControllerMoveHandle );

	// Clamp input to a vector no longer than 1 unit. This shouldn't happen with the physical circular constraint on the joystick, but if somehow somebody did hack their input
	// to produce longer vectors in the corners (for example), we catch that here.
	Vector2D moveDir( moveData.x, moveData.y );

	if ( moveDir.LengthSqr() > 1.0 )
	{
		moveDir.NormalizeInPlace();
	}

	// Apply forward/back movement
	if ( moveDir.y > 0.0 )
	{
		cmd->forwardmove += cl_forwardspeed.GetFloat() * moveDir.y;
	}
	else
	{
		cmd->forwardmove += cl_backspeed.GetFloat() * moveDir.y;
	}

	// Apply sidestep movement
	cmd->sidemove += cl_sidespeed.GetFloat() * moveDir.x;

	ControllerAnalogActionData_t action = steamcontroller->GetAnalogActionData( controller, g_ControllerCameraHandle );

	const float fSensitivityFactor = sc_look_sensitivity_scale.GetFloat();

	Vector2D vecMouseDelta = Vector2D( action.x, -action.y ) * fSensitivityFactor;

	if ( vecMouseDelta.Length() > 0 )
	{
		if ( !m_fCameraInterceptingMouse )
		{
			ApplySteamControllerCameraMove( viewangles, cmd, vecMouseDelta );
		}
	}

	engine->SetViewAngles( viewangles );
#endif
}
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif

//-----------------------------------------------------------------------------
// Purpose: Sets the preferred game action set, and "debounces" controls where
// appropriate if the set has changed.
//-----------------------------------------------------------------------------
void CInput::SetPreferredGameActionSet( GameActionSet_t action_set )
{
	if ( m_PreferredGameActionSet != action_set )
	{
#if !defined( L4D2 )
		// Debounce. Flag some actions as needing debounce (i.e. must see a "released" state before we'll register another "pressed" input).
		for ( int i = 0; i < ARRAYSIZE( g_ControllerDigitalActionState ); i++ )
		{
			if ( g_ControllerDigitalGameActions[i].flags & CONTROLLER_ACTION_FLAGS_NEEDS_DEBOUNCE )
			{
				g_ControllerDigitalActionState[i].bAwaitingDebounce = true;
			}
		}
#endif

		m_PreferredGameActionSet = action_set;
		g_pInputSystem->ActivateSteamControllerActionSet( m_PreferredGameActionSet );														  
	}
}

GameActionSet_t CInput::GetPreferredGameActionSet()
{
	return m_PreferredGameActionSet;
}

//-----------------------------------------------------------------------------
// Purpose: Sets flags for special-case action handling
//-----------------------------------------------------------------------------
void CInput::SetGameActionSetFlags( GameActionSetFlags_t action_set_flags )
{
	m_GameActionSetFlags = action_set_flags;
}

//-----------------------------------------------------------------------------
// Purpose: Client should call this to determine if Steam Controllers are "active"
//-----------------------------------------------------------------------------
bool CInput::IsSteamControllerActive()
{
	// Not active if we're not initialized, or input system thinks we're inactive
	bool bActive = m_bSteamControllerGameActionsInitialized && g_pInputSystem->IsSteamControllerActive();
	if ( !bActive )
		return false;

	// Otherwise, we're definitely active if we've latched seeing some controller input
	if ( m_bSteamControllerSeenInput )
		return true;

	// Haven't seen input yet, so see if input system thinks any controller buttons are pressed
	for ( int button = (int)ButtonCode_t::STEAMCONTROLLER_FIRST; button <= (int)ButtonCode_t::STEAMCONTROLLER_LAST; button++ )
	{
		if ( g_pInputSystem->IsButtonDown( (ButtonCode_t)button ) )
		{
			m_bSteamControllerSeenInput = true;
			break;
		}
	}

	return m_bSteamControllerSeenInput;
}

#if !defined( L4D2 )
//-----------------------------------------------------------------------------
// Purpose: Console command for launching the Steam Controller binding panel
//-----------------------------------------------------------------------------
CON_COMMAND( sc_show_binding_panel, "Launches the Steam Controller binding panel UI" )
{
	if ( g_pInputSystem )
	{
		auto steamcontroller = g_pInputSystem->SteamControllerInterface();
		if ( steamcontroller )
		{
			ControllerHandle_t controllers[STEAM_CONTROLLER_MAX_COUNT];
			int nConnected = steamcontroller->GetConnectedControllers( controllers );
			if ( nConnected > 0 )
			{
				Msg( "%d controller(s) connected. Launching binding panel for first connected controller.\n", nConnected );
				if ( !steamcontroller->ShowBindingPanel( controllers[0] ) )
				{
					Warning( "Unable to show binding panel. Steam overlay disabled, or Steam not in Big Picture mode.\n" );
				}
			}
			else
			{
				Warning( "No Steam Controllers connected.\n" );
			}
		}																									
		else
		{
			Warning( "Steam Controller interface not initialized.\n" );
		}
	}
	else
	{
		Warning( "Input system not initialized.\n" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Console command for dumping out some Steam Controller status information
//-----------------------------------------------------------------------------
CON_COMMAND( sc_status, "Show Steam Controller status information" )
{
	if ( g_pInputSystem )
	{
		auto steamcontroller = g_pInputSystem->SteamControllerInterface();
		if ( steamcontroller )
		{
			ControllerHandle_t controllers[STEAM_CONTROLLER_MAX_COUNT];
			int nConnected = steamcontroller->GetConnectedControllers( controllers );
			if ( nConnected )
			{
				Msg( "%d Steam Controller(s) connected.\n", nConnected );
			}
			else
			{
				Warning( "No Steam Controllers(s) connected.\n" );
			}

			if ( ::input->IsSteamControllerActive() )
			{
				Msg( "Steam Controller considered active (controller connected and all action handles initialized).\n" );
			}
			else
			{
				Warning( "Steam Controller considered inactive (no controller connected, no input observed, not all action handles initialized, or sc_disable set).\n" );
			}

			auto action_set = ::input->GetPreferredGameActionSet();
			Msg( "Current action set = %d\n", action_set );

			for ( int i = 0; i < ARRAYSIZE( g_ControllerDigitalActionState ); ++i )
			{
				ControllerDigitalActionToCommand& cmdmap = g_ControllerDigitalGameActions[i];
				ControllerDigitalActionState& state = g_ControllerDigitalActionState[i];

				Msg( "Action: '%s' handle = %d\n", cmdmap.action, (int)state.handle );
			}
		}
		else
		{
			Warning( "Steam Controller interface not initialized.\n" );
		}
	}
	else
	{
		Warning( "Input system not initialized.\n" );
	}
}
#endif
