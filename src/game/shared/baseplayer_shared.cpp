//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements shared baseplayer class functionality
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "movevars_shared.h"
#include "util_shared.h"
#include "datacache/imdlcache.h"
#if defined ( TF_DLL ) || defined ( TF_CLIENT_DLL )
#include "tf_gamerules.h"
#endif

#if defined( CLIENT_DLL )

	#include "iclientvehicle.h"
	#include "prediction.h"
	#include "c_basedoor.h"
	#include "c_world.h"
	#include "view.h"
	#include "client_virtualreality.h"
	#define CRecipientFilter C_RecipientFilter
	#include "sourcevr/isourcevirtualreality.h"

#else

	#include "iservervehicle.h"
	#include "trains.h"
	#include "world.h"
	#include "doors.h"
	#include "ai_basenpc.h"
	#include "env_zoom.h"

	extern int TrainSpeed(int iSpeed, int iMax);
	
#endif

#if defined( CSTRIKE_DLL )
#include "weapon_c4.h"
#endif // CSTRIKE_DLL

#include "in_buttons.h"
#include "engine/IEngineSound.h"
#include "tier0/vprof.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "decals.h"
#include "obstacle_pushaway.h"
#ifdef SIXENSE
#include "sixense/in_sixense.h"
#endif

// NVNT haptic utils
#if !defined( L4D2 )
#include "haptics/haptic_utils.h"
#endif // !L4D2
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if defined(GAME_DLL) && !defined(_XBOX)
	extern ConVar sv_pushaway_max_force;
	extern ConVar sv_pushaway_force;
	extern ConVar sv_turbophysics;

	class CUsePushFilter : public CTraceFilterEntitiesOnly
	{
	public:
		bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
		{
			CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );

			// Static prop case...
			if ( !pEntity )
				return false;

			// Only impact on physics objects
			if ( !pEntity->VPhysicsGetObject() )
				return false;

#if defined( CSTRIKE_DLL )
			// don't push the bomb!
			if ( dynamic_cast<CC4*>( pEntity ) )
				return false;
#endif // CSTRIKE_DLL

			return g_pGameRules->CanEntityBeUsePushed( pEntity );
		}
	};
#endif

#ifdef CLIENT_DLL
ConVar mp_usehwmmodels( "mp_usehwmmodels", "0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000000A
#else
	NULL
#endif
, "Enable the use of the hw morph models. (-1 = never, 1 = always, 0 = based upon GPU)" ); // -1 = never, 0 = if hasfastvertextextures, 1 = always
#endif

bool UseHWMorphModels()
{
// #ifdef CLIENT_DLL 
// 	if ( mp_usehwmmodels.GetInt() == 0 )
// 		return g_pMaterialSystemHardwareConfig->HasFastVertexTextures();
// 
// 	return mp_usehwmmodels.GetInt() > 0;
// #else
// 	return false;
// #endif
	return false;
}

void CopySoundNameWithModifierToken( char *pchDest, const char *pchSource, int nMaxLenInChars, const char *pchToken )
{
	// Copy the sound name
	int nSource = 0;
	int nDest = 0;
	bool bFoundPeriod = false;

	while ( pchSource[ nSource ] != '\0' && nDest < nMaxLenInChars - 2 )
	{
		pchDest[ nDest ] = pchSource[ nSource ];
		nDest++;
		nSource++;

		if ( !bFoundPeriod && pchSource[ nSource - 1 ] == '.' )
		{
			// Insert special token after the period
			bFoundPeriod = true;

			int nToken = 0;

			while ( pchToken[ nToken ] != '\0' && nDest < nMaxLenInChars - 2 )
			{
				pchDest[ nDest ] = pchToken[ nToken ];
				nDest++;
				nToken++;
			}
		}
	}

	pchDest[ nDest ] = '\0';
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float CBasePlayer::GetTimeBase( void ) const
{
	return m_nTickBase * TICK_INTERVAL;
}

float CBasePlayer::GetPlayerMaxSpeed()
{
	// player max speed is the lower limit of m_flMaxSpeed and sv_maxspeed
	float fMaxSpeed = sv_maxspeed.GetFloat();
	if ( MaxSpeed() > 0.0f && MaxSpeed() < fMaxSpeed )
		fMaxSpeed = MaxSpeed();

	return fMaxSpeed;
}

//-----------------------------------------------------------------------------
// Purpose: Called every usercmd by the player PreThink
//-----------------------------------------------------------------------------
void CBasePlayer::ItemPreFrame()
{
	// Handle use events
	PlayerUse();

	CBaseCombatWeapon *pActive = GetActiveWeapon();

	// Allow all the holstered weapons to update
	for ( int i = 0; i < WeaponCount(); ++i )
	{
		CBaseCombatWeapon *pWeapon = GetWeapon( i );

		if ( pWeapon == NULL )
			continue;

		if ( pActive == pWeapon )
			continue;

		pWeapon->ItemHolsterFrame();
	}

    if ( gpGlobals->curtime < m_flNextAttack )
		return;

	if (!pActive)
		return;

#if defined( CLIENT_DLL )
	// Not predicting this weapon
	if ( !pActive->IsPredicted() )
		return;
#endif

	pActive->ItemPreFrame();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBasePlayer::UsingStandardWeaponsInVehicle( void )
{
	Assert( IsInAVehicle() );
#if !defined( CLIENT_DLL )
	IServerVehicle *pVehicle = GetVehicle();
#else
	IClientVehicle *pVehicle = GetVehicle();
#endif
	Assert( pVehicle );
	if ( !pVehicle )
		return true;

	// NOTE: We *have* to do this before ItemPostFrame because ItemPostFrame
	// may dump us out of the vehicle
	int nRole = pVehicle->GetPassengerRole( this );
	bool bUsingStandardWeapons = pVehicle->IsPassengerUsingStandardWeapons( nRole );

	// Fall through and check weapons, etc. if we're using them 
	if (!bUsingStandardWeapons )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Called every usercmd by the player PostThink
//-----------------------------------------------------------------------------
void CBasePlayer::ItemPostFrame()
{
	VPROF( "CBasePlayer::ItemPostFrame" );

	// Put viewmodels into basically correct place based on new player origin
	CalcViewModelView( EyePosition(), EyeAngles() );

	// Don't process items while in a vehicle.
	if ( GetVehicle() )
	{
#if defined( CLIENT_DLL )
		IClientVehicle *pVehicle = GetVehicle();
#else
		IServerVehicle *pVehicle = GetVehicle();
#endif

		bool bUsingStandardWeapons = UsingStandardWeaponsInVehicle();

#if defined( CLIENT_DLL )
		if ( pVehicle->IsPredicted() )
#endif
		{
			pVehicle->ItemPostFrame( this );
		}

		if (!bUsingStandardWeapons || !GetVehicle())
			return;
	}


	// check if the player is using something
	if ( m_hUseEntity != NULL )
	{
#if !defined( CLIENT_DLL )
		Assert( !IsInAVehicle() );
		ImpulseCommands();// this will call playerUse
#endif
		return;
	}

    if ( gpGlobals->curtime < m_flNextAttack )
	{
		if ( GetActiveWeapon() )
		{
			GetActiveWeapon()->ItemBusyFrame();
		}
	}
	else
	{
		if ( GetActiveWeapon() && (!IsInAVehicle() || UsingStandardWeaponsInVehicle()) )
		{
#if defined( CLIENT_DLL )
			// Not predicting this weapon
			if ( GetActiveWeapon()->IsPredicted() )
#endif

			{
				GetActiveWeapon()->ItemPostFrame( );
			}
		}
	}

#if !defined( CLIENT_DLL )
	ImpulseCommands();
#else
	// NOTE: If we ever support full impulse commands on the client,
	// remove this line and call ImpulseCommands instead.
	m_nImpulse = 0;
#endif
}


//-----------------------------------------------------------------------------
// Eye angles
//-----------------------------------------------------------------------------
const QAngle &CBasePlayer::EyeAngles( )
{
	// NOTE: Viewangles are measured *relative* to the parent's coordinate system
	CBaseEntity *pMoveParent = const_cast<CBasePlayer*>(this)->GetMoveParent();

	if ( !pMoveParent )
	{
		return pl.v_angle;
	}

	// FIXME: Cache off the angles?
	matrix3x4_t eyesToParent, eyesToWorld;
	AngleMatrix( pl.v_angle, eyesToParent );
	ConcatTransforms( pMoveParent->EntityToWorldTransform(), eyesToParent, eyesToWorld );

	static QAngle angEyeWorld;
	MatrixAngles( eyesToWorld, angEyeWorld );
	return angEyeWorld;
}


const QAngle &CBasePlayer::LocalEyeAngles()
{
	return pl.v_angle;
}

//-----------------------------------------------------------------------------
// Actual Eye position + angles
//-----------------------------------------------------------------------------
Vector CBasePlayer::EyePosition( )
{
	if ( GetVehicle() != NULL )
	{
		// Return the cached result
		CacheVehicleView();
		return m_vecVehicleViewOrigin;
	}
	else
	{
#ifdef CLIENT_DLL
		if ( IsObserver() )
		{
			if ( GetObserverMode() == OBS_MODE_CHASE
#if !defined( L4D2 )
				|| GetObserverMode() == OBS_MODE_POI
#endif
				)
			{
				if ( IsLocalPlayer() )
				{
					return MainViewOrigin();
				}
			}
		}
#endif
		return BaseClass::EyePosition();
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : 
// Output : const Vector
//-----------------------------------------------------------------------------
const Vector CBasePlayer::GetPlayerMins( void ) const
{
	if ( IsObserver() )
	{
#ifdef L4D2
		return VEC_OBS_HULL_MIN;
#else
		return VEC_OBS_HULL_MIN_SCALED( this );
#endif
	}
	else
	{
		if ( GetFlags() & FL_DUCKING )
		{
#ifdef L4D2
			return VEC_DUCK_HULL_MIN;
#else
			return VEC_DUCK_HULL_MIN_SCALED( this );
#endif
		}
		else
		{
#ifdef L4D2
			return VEC_HULL_MIN;
#else
			return VEC_HULL_MIN_SCALED( this );
#endif
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : 
// Output : const Vector
//-----------------------------------------------------------------------------
const Vector CBasePlayer::GetPlayerMaxs( void ) const
{	
	if ( IsObserver() )
	{
#ifdef L4D2
		return VEC_OBS_HULL_MAX;
#else
		return VEC_OBS_HULL_MAX_SCALED( this );
#endif
	}
	else
	{
		if ( GetFlags() & FL_DUCKING )
		{
#ifdef L4D2
			return VEC_DUCK_HULL_MAX;
#else
			return VEC_DUCK_HULL_MAX_SCALED( this );
#endif
		}
		else
		{
#ifdef L4D2
			return VEC_HULL_MAX;
#else
			return VEC_HULL_MAX_SCALED( this );
#endif
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Update the vehicle view, or simply return the cached position and angles
//-----------------------------------------------------------------------------
void CBasePlayer::CacheVehicleView( void )
{
	// If we've calculated the view this frame, then there's no need to recalculate it
	if ( m_nVehicleViewSavedFrame == gpGlobals->framecount )
		return;

#ifdef CLIENT_DLL
	IClientVehicle *pVehicle = GetVehicle();
#else
	IServerVehicle *pVehicle = GetVehicle();
#endif

	if ( pVehicle != NULL )
	{		
		int nRole = pVehicle->GetPassengerRole( this );

		// Get our view for this frame
		pVehicle->GetVehicleViewPosition( nRole, &m_vecVehicleViewOrigin, &m_vecVehicleViewAngles, &m_flVehicleViewFOV );
		m_nVehicleViewSavedFrame = gpGlobals->framecount;

#if defined( CLIENT_DLL ) && !defined( L4D2 )
		if( UseVR() )
		{
			C_BaseAnimating *pVehicleAnimating = dynamic_cast<C_BaseAnimating *>( pVehicle );
			if( pVehicleAnimating )
			{
				int eyeAttachmentIndex = pVehicleAnimating->LookupAttachment( "vehicle_driver_eyes" );

				Vector vehicleEyeOrigin;
				QAngle vehicleEyeAngles;
				pVehicleAnimating->GetAttachment( eyeAttachmentIndex, vehicleEyeOrigin, vehicleEyeAngles );

				g_ClientVirtualReality.OverrideTorsoTransform( vehicleEyeOrigin, vehicleEyeAngles );
			}
		}
#endif
	}
}

//-----------------------------------------------------------------------------
// Returns eye vectors
//-----------------------------------------------------------------------------
void CBasePlayer::EyeVectors( Vector *pForward, Vector *pRight, Vector *pUp )
{
	if ( GetVehicle() != NULL )
	{
		// Cache or retrieve our calculated position in the vehicle
		CacheVehicleView();
		AngleVectors( m_vecVehicleViewAngles, pForward, pRight, pUp );
	}
	else
	{
		AngleVectors( EyeAngles(), pForward, pRight, pUp );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns the eye position and angle vectors.
//-----------------------------------------------------------------------------
void CBasePlayer::EyePositionAndVectors( Vector *pPosition, Vector *pForward,
										 Vector *pRight, Vector *pUp )
{
	// Handle the view in the vehicle
	if ( GetVehicle() != NULL )
	{
		CacheVehicleView();
		AngleVectors( m_vecVehicleViewAngles, pForward, pRight, pUp );
		
		if ( pPosition != NULL )
		{
			*pPosition = m_vecVehicleViewOrigin;
		}
	}
	else
	{
		VectorCopy( BaseClass::EyePosition(), *pPosition );
		AngleVectors( EyeAngles(), pForward, pRight, pUp );
	}
}

#ifdef CLIENT_DLL
surfacedata_t * CBasePlayer::GetFootstepSurface( const Vector &origin, const char *surfaceName )
{
	return physprops->GetSurfaceData( physprops->GetSurfaceIndex( surfaceName ) );
}
#endif

#if defined( L4D2 )
// S7DC80/C1F2D0. Client receives one byte, server stores an int; both wrap at4.
void CBasePlayer::IncrementEFNoInterpParity()
{
	if ( gpGlobals->maxClients != 1 )
		m_ubEFNoInterpParity = ( m_ubEFNoInterpParity + 1 ) & 3;
}
#endif

surfacedata_t *CBasePlayer::GetLadderSurface( const Vector &origin )
{
#ifdef CLIENT_DLL
	return GetFootstepSurface( origin, "ladder" );
#else
	return physprops->GetSurfaceData( physprops->GetSurfaceIndex( "ladder" ) );
#endif
}

void CBasePlayer::UpdateStepSound( surfacedata_t *psurface, const Vector &vecOrigin, const Vector &vecVelocity )
{
	bool bWalking;
	float fvol;
	Vector knee;
	Vector feet;
	float height;
	float speed;
	float velrun;
	float velwalk;
	int	fLadder;

	if ( m_flStepSoundTime > 0 )
	{
		m_flStepSoundTime -= 1000.0f * gpGlobals->frametime;
		if ( m_flStepSoundTime < 0 )
		{
			m_flStepSoundTime = 0;
		}
	}

	if ( m_flStepSoundTime > 0 )
		return;

	if ( GetFlags() & (FL_FROZEN|FL_ATCONTROLS))
		return;

	if ( GetMoveType() == MOVETYPE_NOCLIP || GetMoveType() == MOVETYPE_OBSERVER )
		return;

	if ( !sv_footsteps.GetFloat() )
		return;

	speed = VectorLength( vecVelocity );
	float groundspeed = Vector2DLength( vecVelocity.AsVector2D() );

	// determine if we are on a ladder
	fLadder = ( GetMoveType() == MOVETYPE_LADDER );

	GetStepSoundVelocities( &velwalk, &velrun );

	bool onground = ( GetFlags() & FL_ONGROUND );
	bool movingalongground = ( groundspeed > 0.0001f );
	bool moving_fast_enough =  ( speed >= velwalk );

#ifdef PORTAL
	// In Portal we MUST play footstep sounds even when the player is moving very slowly
	// This is used to count the number of footsteps they take in the challenge mode
	// -Jeep
	moving_fast_enough = true;
#endif

	// To hear step sounds you must be either on a ladder or moving along the ground AND
	// You must be moving fast enough

	if ( !moving_fast_enough || !(fLadder || ( onground && movingalongground )) )
			return;

//	MoveHelper()->PlayerSetAnimation( PLAYER_WALK );

	bWalking = speed < velrun;		

	VectorCopy( vecOrigin, knee );
	VectorCopy( vecOrigin, feet );

	height = GetPlayerMaxs()[ 2 ] - GetPlayerMins()[ 2 ];

	knee[2] = vecOrigin[2] + 0.2 * height;

	// find out what we're stepping in or on...
	if ( fLadder )
	{
		psurface = GetLadderSurface(vecOrigin);
		fvol = 0.5;

		SetStepSoundTime( STEPSOUNDTIME_ON_LADDER, bWalking );
	}
#ifdef CSTRIKE_DLL
	else if ( enginetrace->GetPointContents( knee ) & MASK_WATER )  // we want to use the knee for Cstrike, not the waist
#else
	else if ( GetWaterLevel() == WL_Waist )
#endif // CSTRIKE_DLL
	{
		static int iSkipStep = 0;

		if ( iSkipStep == 0 )
		{
			iSkipStep++;
			return;
		}

		if ( iSkipStep++ == 3 )
		{
			iSkipStep = 0;
		}
		psurface = physprops->GetSurfaceData( physprops->GetSurfaceIndex( "wade" ) );
		fvol = 0.65;
		SetStepSoundTime( STEPSOUNDTIME_WATER_KNEE, bWalking );
	}
	else if ( GetWaterLevel() == WL_Feet )
	{
		psurface = physprops->GetSurfaceData( physprops->GetSurfaceIndex( "water" ) );
		fvol = bWalking ? 0.2 : 0.5;

		SetStepSoundTime( STEPSOUNDTIME_WATER_FOOT, bWalking );
	}
	else
	{
		if ( !psurface )
			return;

		SetStepSoundTime( STEPSOUNDTIME_NORMAL, bWalking );

		switch ( psurface->game.material )
		{
		default:
		case CHAR_TEX_CONCRETE:						
			fvol = bWalking ? 0.2 : 0.5;
			break;

		case CHAR_TEX_METAL:	
			fvol = bWalking ? 0.2 : 0.5;
			break;

		case CHAR_TEX_DIRT:
			fvol = bWalking ? 0.25 : 0.55;
			break;

		case CHAR_TEX_VENT:	
			fvol = bWalking ? 0.4 : 0.7;
			break;

		case CHAR_TEX_GRATE:
			fvol = bWalking ? 0.2 : 0.5;
			break;

		case CHAR_TEX_TILE:	
			fvol = bWalking ? 0.2 : 0.5;
			break;

		case CHAR_TEX_SLOSH:
			fvol = bWalking ? 0.2 : 0.5;
			break;
		}
	}
	
	// play the sound
	// 65% volume if ducking
	if ( GetFlags() & FL_DUCKING )
	{
		fvol *= 0.65;
	}

	PlayStepSound( feet, psurface, fvol, false );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : step - 
//			fvol - 
//			force - force sound to play
//-----------------------------------------------------------------------------
void CBasePlayer::PlayStepSound( Vector &vecOrigin, surfacedata_t *psurface, float fvol, bool force )
{
	if ( gpGlobals->maxClients > 1 && !sv_footsteps.GetFloat() )
		return;

#if defined( CLIENT_DLL )
	// during prediction play footstep sounds only once
	if ( prediction->InPrediction() && !prediction->IsFirstTimePredicted() )
		return;
#endif

	if ( !psurface )
		return;

	int nSide = m_Local.m_nStepside;
	unsigned short stepSoundName = nSide ? psurface->sounds.stepleft : psurface->sounds.stepright;
	if ( !stepSoundName )
		return;

	m_Local.m_nStepside = !nSide;

	CSoundParameters params;

	Assert( nSide == 0 || nSide == 1 );

	if ( m_StepSoundCache[ nSide ].m_usSoundNameIndex == stepSoundName )
	{
		params = m_StepSoundCache[ nSide ].m_SoundParameters;
	}
	else
	{
		const char *pSoundName = MoveHelper()->GetSurfaceProps()->GetString( stepSoundName );

		// Give child classes an opportunity to override.
		pSoundName = GetOverrideStepSound( pSoundName );

		if ( !CBaseEntity::GetParametersForSound( pSoundName, params, NULL ) )
			return;

		// Only cache if there's one option.  Otherwise we'd never here any other sounds
		if ( params.count == 1 )
		{
			m_StepSoundCache[ nSide ].m_usSoundNameIndex = stepSoundName;
			m_StepSoundCache[ nSide ].m_SoundParameters = params;
		}
	}

	CRecipientFilter filter;
	filter.AddRecipientsByPAS( vecOrigin );

#ifndef CLIENT_DLL
	// in MP, server removes all players in the vecOrigin's PVS, these players generate the footsteps client side
	if ( gpGlobals->maxClients > 1 )
	{
		filter.RemoveRecipientsByPVS( vecOrigin );
	}
#endif

	EmitSound_t ep;
	ep.m_nChannel = CHAN_BODY;
	ep.m_pSoundName = params.soundname;
#if defined ( TF_DLL ) || defined ( TF_CLIENT_DLL )
	if( TFGameRules()->IsMannVsMachineMode() )
	{
		ep.m_flVolume = params.volume;
	}
	else
	{
		ep.m_flVolume = fvol;
	}
#else
	ep.m_flVolume = fvol;
#endif
	ep.m_SoundLevel = params.soundlevel;
	ep.m_nFlags = 0;
	ep.m_nPitch = params.pitch;
	ep.m_pOrigin = &vecOrigin;

	EmitSound( filter, entindex(), ep );

	// Kyle says: ugggh. This function may as well be called "PerformPileOfDesperateGameSpecificFootstepHacks".
	OnEmitFootstepSound( params, vecOrigin, fvol );
}

void CBasePlayer::UpdateButtonState( int nUserCmdButtonMask )
{
	// Track button info so we can detect 'pressed' and 'released' buttons next frame
	m_afButtonLast = m_nButtons;

	// Get button states
	m_nButtons = nUserCmdButtonMask;
 	int buttonsChanged = m_afButtonLast ^ m_nButtons;
	
	// Debounced button codes for pressed/released
	// UNDONE: Do we need auto-repeat?
	m_afButtonPressed =  buttonsChanged & m_nButtons;		// The changed ones still down are "pressed"
	m_afButtonReleased = buttonsChanged & (~m_nButtons);	// The ones not down are "released"
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBasePlayer::GetStepSoundVelocities( float *velwalk, float *velrun )
{
	// UNDONE: need defined numbers for run, walk, crouch, crouch run velocities!!!!	
	if ( ( GetFlags() & FL_DUCKING) || ( GetMoveType() == MOVETYPE_LADDER ) )
	{
		*velwalk = 60;		// These constants should be based on cl_movespeedkey * cl_forwardspeed somehow
		*velrun = 80;		
	}
	else
	{
		*velwalk = 90;
		*velrun = 220;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBasePlayer::SetStepSoundTime( stepsoundtimes_t iStepSoundTime, bool bWalking )
{
	switch ( iStepSoundTime )
	{
	case STEPSOUNDTIME_NORMAL:
	case STEPSOUNDTIME_WATER_FOOT:
		m_flStepSoundTime = bWalking ? 400 : 300;
		break;

	case STEPSOUNDTIME_ON_LADDER:
		m_flStepSoundTime = 350;
		break;

	case STEPSOUNDTIME_WATER_KNEE:
		m_flStepSoundTime = 600;
		break;

	default:
		Assert(0);
		break;
	}

	// UNDONE: need defined numbers for run, walk, crouch, crouch run velocities!!!!	
	if ( ( GetFlags() & FL_DUCKING) || ( GetMoveType() == MOVETYPE_LADDER ) )
	{
		m_flStepSoundTime += 100;
	}
}

Vector CBasePlayer::Weapon_ShootPosition( )
{
	return EyePosition();
}

void CBasePlayer::SetAnimationExtension( const char *pExtension )
{
	Q_strncpy( m_szAnimExtension, pExtension, sizeof(m_szAnimExtension) );
}


//-----------------------------------------------------------------------------
// Purpose: Set the weapon to switch to when the player uses the 'lastinv' command
//-----------------------------------------------------------------------------
void CBasePlayer::Weapon_SetLast( CBaseCombatWeapon *pWeapon )
{
	m_hLastWeapon = pWeapon;
}

//-----------------------------------------------------------------------------
// Purpose: Override base class so player can reset autoaim
// Input  :
// Output :
//-----------------------------------------------------------------------------
bool CBasePlayer::Weapon_Switch( CBaseCombatWeapon *pWeapon, int viewmodelindex /*=0*/ ) 
{
	CBaseCombatWeapon *pLastWeapon = GetActiveWeapon();

	if ( BaseClass::Weapon_Switch( pWeapon, viewmodelindex ))
	{
		if ( pLastWeapon && Weapon_ShouldSetLast( pLastWeapon, GetActiveWeapon() ) )
		{
			Weapon_SetLast( pLastWeapon->GetLastWeapon() );
		}

		CBaseViewModel *pViewModel = GetViewModel( viewmodelindex );
		Assert( pViewModel );
		if ( pViewModel )
			pViewModel->RemoveEffects( EF_NODRAW );
		ResetAutoaim( );
		return true;
	}
	return false;
}

void CBasePlayer::SelectLastItem(void)
{
	if ( m_hLastWeapon.Get() == NULL )
		return;

	if ( GetActiveWeapon() && !GetActiveWeapon()->CanHolster() )
		return;

	SelectItem( m_hLastWeapon.Get()->GetClassname(), m_hLastWeapon.Get()->GetSubType() );
}


//-----------------------------------------------------------------------------
// Purpose: Abort any reloads we're in
//-----------------------------------------------------------------------------
void CBasePlayer::AbortReload( void )
{
	if ( GetActiveWeapon() )
	{
		GetActiveWeapon()->AbortReload();
	}
}

#if !defined( NO_ENTITY_PREDICTION )
void CBasePlayer::AddToPlayerSimulationList( CBaseEntity *other )
{
	CHandle< CBaseEntity > h;
	h = other;
	// Already in list
	if ( m_SimulatedByThisPlayer.Find( h ) != m_SimulatedByThisPlayer.InvalidIndex() )
		return;

	Assert( other->IsPlayerSimulated() );

	m_SimulatedByThisPlayer.AddToTail( h );
}

//-----------------------------------------------------------------------------
// Purpose: Fixme, this should occur if the player fails to drive simulation
//  often enough!!!
// Input  : *other - 
//-----------------------------------------------------------------------------
void CBasePlayer::RemoveFromPlayerSimulationList( CBaseEntity *other )
{
	if ( !other )
		return;

	Assert( other->IsPlayerSimulated() );
	Assert( other->GetSimulatingPlayer() == this );


	CHandle< CBaseEntity > h;
	h = other;

	m_SimulatedByThisPlayer.FindAndRemove( h );
}

void CBasePlayer::SimulatePlayerSimulatedEntities( void )
{
	int c = m_SimulatedByThisPlayer.Count();
	int i;

	for ( i = c - 1; i >= 0; i-- )
	{
		CHandle< CBaseEntity > h;
		
		h = m_SimulatedByThisPlayer[ i ];
		CBaseEntity *e = h;

		if ( !e || !e->IsPlayerSimulated() )
		{
			m_SimulatedByThisPlayer.Remove( i );
			continue;
		}

#if defined( CLIENT_DLL )
		if ( e->IsClientCreated() && prediction->InPrediction() && !prediction->IsFirstTimePredicted() )
		{
			continue;
		}
#endif
		Assert( e->IsPlayerSimulated() );
		Assert( e->GetSimulatingPlayer() == this );

		e->PhysicsSimulate();
	}

	// Loop through all entities again, checking their untouch if flagged to do so
	c = m_SimulatedByThisPlayer.Count();

	for ( i = c - 1; i >= 0; i-- )
	{
		CHandle< CBaseEntity > h;
		
		h = m_SimulatedByThisPlayer[ i ];

		CBaseEntity *e = h;
		if ( !e || !e->IsPlayerSimulated() )
		{
			m_SimulatedByThisPlayer.Remove( i );
			continue;
		}

#if defined( CLIENT_DLL )
		if ( e->IsClientCreated() && prediction->InPrediction() && !prediction->IsFirstTimePredicted() )
		{
			continue;
		}
#endif

		Assert( e->IsPlayerSimulated() );
		Assert( e->GetSimulatingPlayer() == this );

		if ( !e->GetCheckUntouch() )
			continue;

		e->PhysicsCheckForEntityUntouch();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBasePlayer::ClearPlayerSimulationList( void )
{
	int c = m_SimulatedByThisPlayer.Size();
	int i;

	for ( i = c - 1; i >= 0; i-- )
	{
		CHandle< CBaseEntity > h;
		
		h = m_SimulatedByThisPlayer[ i ];
		CBaseEntity *e = h;
		if ( e )
		{
			e->UnsetPlayerSimulated();
		}
	}

	m_SimulatedByThisPlayer.RemoveAll();
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Return true if we should allow selection of the specified item
//-----------------------------------------------------------------------------
bool CBasePlayer::Weapon_ShouldSelectItem( CBaseCombatWeapon *pWeapon )
{
	return ( pWeapon != GetActiveWeapon() );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBasePlayer::SelectItem( const char *pstr, int iSubType )
{
	if (!pstr)
		return;

	CBaseCombatWeapon *pItem = Weapon_OwnsThisType( pstr, iSubType );

	if (!pItem)
		return;

	if( GetObserverMode() != OBS_MODE_NONE )
		return;// Observers can't select things.

	if ( !Weapon_ShouldSelectItem( pItem ) )
		return;

	// FIX, this needs to queue them up and delay
	// Make sure the current weapon can be holstered
	if ( GetActiveWeapon() )
	{
		if ( !GetActiveWeapon()->CanHolster() && !pItem->ForceWeaponSwitch() )
			return;

		ResetAutoaim( );
	}

	Weapon_Switch( pItem );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
ConVar sv_debug_player_use( "sv_debug_player_use", "0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000200A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
, "Visualizes +use logic. Green cross=trace success, Red cross=trace too far, Green box=radius success" );
float IntervalDistance( float x, float x0, float x1 )
{
	// swap so x0 < x1
	if ( x0 > x1 )
	{
		float tmp = x0;
		x0 = x1;
		x1 = tmp;
	}

	if ( x < x0 )
		return x0-x;
	else if ( x > x1 )
		return x - x1;
	return 0;
}

CBaseEntity *CBasePlayer::FindUseEntity()
{
	Vector forward, up;
	EyeVectors( &forward, NULL, &up );

	trace_t tr;
	// Search for objects in a sphere (tests for entities that are not solid, yet still useable)
	Vector searchCenter = EyePosition();

	// NOTE: Some debris objects are useable too, so hit those as well
	// A button, etc. can be made out of clip brushes, make sure it's +useable via a traceline, too.
	int useableContents = MASK_SOLID | CONTENTS_DEBRIS | CONTENTS_PLAYERCLIP;

#ifdef CSTRIKE_DLL
	useableContents = MASK_NPCSOLID_BRUSHONLY | MASK_OPAQUE_AND_NPCS;
#endif

#ifdef HL1_DLL
	useableContents = MASK_SOLID;
#endif
#ifndef CLIENT_DLL
	CBaseEntity *pFoundByTrace = NULL;
#endif

	// UNDONE: Might be faster to just fold this range into the sphere query
	CBaseEntity *pObject = NULL;

	float nearestDist = FLT_MAX;
	// try the hit entity if there is one, or the ground entity if there isn't.
	CBaseEntity *pNearest = NULL;

	const int NUM_TANGENTS = 8;
	// trace a box at successive angles down
	//							forward, 45 deg, 30 deg, 20 deg, 15 deg, 10 deg, -10, -15
	const float tangents[NUM_TANGENTS] = { 0, 1, 0.57735026919f, 0.3639702342f, 0.267949192431f, 0.1763269807f, -0.1763269807f, -0.267949192431f };
	for ( int i = 0; i < NUM_TANGENTS; i++ )
	{
		if ( i == 0 )
		{
			UTIL_TraceLine( searchCenter, searchCenter + forward * 1024, useableContents, this, COLLISION_GROUP_NONE, &tr );
		}
		else
		{
			Vector down = forward - tangents[i]*up;
			VectorNormalize(down);
			UTIL_TraceHull( searchCenter, searchCenter + down * 72, -Vector(16,16,16), Vector(16,16,16), useableContents, this, COLLISION_GROUP_NONE, &tr );
		}
		pObject = tr.m_pEnt;

#ifndef CLIENT_DLL
		pFoundByTrace = pObject;
#endif
		bool bUsable = IsUseableEntity(pObject, 0);
		while ( pObject && !bUsable && pObject->GetMoveParent() )
		{
			pObject = pObject->GetMoveParent();
			bUsable = IsUseableEntity(pObject, 0);
		}

		if ( bUsable )
		{
			Vector delta = tr.endpos - tr.startpos;
			float centerZ = CollisionProp()->WorldSpaceCenter().z;
			delta.z = IntervalDistance( tr.endpos.z, centerZ + CollisionProp()->OBBMins().z, centerZ + CollisionProp()->OBBMaxs().z );
			float dist = delta.Length();
			if ( dist < PLAYER_USE_RADIUS )
			{
#ifndef CLIENT_DLL

				if ( sv_debug_player_use.GetBool() )
				{
					NDebugOverlay::Line( searchCenter, tr.endpos, 0, 255, 0, true, 30 );
					NDebugOverlay::Cross3D( tr.endpos, 16, 0, 255, 0, true, 30 );
				}

				if ( pObject->MyNPCPointer() && pObject->MyNPCPointer()->IsPlayerAlly( this ) )
				{
					// If about to select an NPC, do a more thorough check to ensure
					// that we're selecting the right one from a group.
					pObject = DoubleCheckUseNPC( pObject, searchCenter, forward );
				}
#endif
				if ( sv_debug_player_use.GetBool() )
				{
					Msg( "Trace using: %s\n", pObject ? pObject->GetDebugName() : "no usable entity found" );
				}

				pNearest = pObject;
				
				// if this is directly under the cursor just return it now
				if ( i == 0 )
					return pObject;
			}
		}
	}

	// check ground entity first
	// if you've got a useable ground entity, then shrink the cone of this search to 45 degrees
	// otherwise, search out in a 90 degree cone (hemisphere)
	if ( GetGroundEntity() && IsUseableEntity(GetGroundEntity(), FCAP_USE_ONGROUND) )
	{
		pNearest = GetGroundEntity();
	}
	if ( pNearest )
	{
		// estimate nearest object by distance from the view vector
		Vector point;
		pNearest->CollisionProp()->CalcNearestPoint( searchCenter, &point );
		nearestDist = CalcDistanceToLine( point, searchCenter, forward );
		if ( sv_debug_player_use.GetBool() )
		{
			Msg("Trace found %s, dist %.2f\n", pNearest->GetClassname(), nearestDist );
		}
	}

	for ( CEntitySphereQuery sphere( searchCenter, PLAYER_USE_RADIUS ); ( pObject = sphere.GetCurrentEntity() ) != NULL; sphere.NextEntity() )
	{
		if ( !pObject )
			continue;

		if ( !IsUseableEntity( pObject, FCAP_USE_IN_RADIUS ) )
			continue;

		// see if it's more roughly in front of the player than previous guess
		Vector point;
		pObject->CollisionProp()->CalcNearestPoint( searchCenter, &point );

		Vector dir = point - searchCenter;
		VectorNormalize(dir);
		float dot = DotProduct( dir, forward );

		// Need to be looking at the object more or less
		if ( dot < 0.8 )
			continue;

		float dist = CalcDistanceToLine( point, searchCenter, forward );

		if ( sv_debug_player_use.GetBool() )
		{
			Msg("Radius found %s, dist %.2f\n", pObject->GetClassname(), dist );
		}

		if ( dist < nearestDist )
		{
			// Since this has purely been a radius search to this point, we now
			// make sure the object isn't behind glass or a grate.
			trace_t trCheckOccluded;
			UTIL_TraceLine( searchCenter, point, useableContents, this, COLLISION_GROUP_NONE, &trCheckOccluded );

			if ( trCheckOccluded.fraction == 1.0 || trCheckOccluded.m_pEnt == pObject )
			{
				pNearest = pObject;
				nearestDist = dist;
			}
		}
	}

#ifndef CLIENT_DLL
	if ( !pNearest )
	{
		// Haven't found anything near the player to use, nor any NPC's at distance.
		// Check to see if the player is trying to select an NPC through a rail, fence, or other 'see-though' volume.
		trace_t trAllies;
		UTIL_TraceLine( searchCenter, searchCenter + forward * PLAYER_USE_RADIUS, MASK_OPAQUE_AND_NPCS, this, COLLISION_GROUP_NONE, &trAllies );

		if ( trAllies.m_pEnt && IsUseableEntity( trAllies.m_pEnt, 0 ) && trAllies.m_pEnt->MyNPCPointer() && trAllies.m_pEnt->MyNPCPointer()->IsPlayerAlly( this ) )
		{
			// This is an NPC, take it!
			pNearest = trAllies.m_pEnt;
		}
	}

	if ( pNearest && pNearest->MyNPCPointer() && pNearest->MyNPCPointer()->IsPlayerAlly( this ) )
	{
		pNearest = DoubleCheckUseNPC( pNearest, searchCenter, forward );
	}

	if ( sv_debug_player_use.GetBool() )
	{
		if ( !pNearest )
		{
			NDebugOverlay::Line( searchCenter, tr.endpos, 255, 0, 0, true, 30 );
			NDebugOverlay::Cross3D( tr.endpos, 16, 255, 0, 0, true, 30 );
		}
		else if ( pNearest == pFoundByTrace )
		{
			NDebugOverlay::Line( searchCenter, tr.endpos, 0, 255, 0, true, 30 );
			NDebugOverlay::Cross3D( tr.endpos, 16, 0, 255, 0, true, 30 );
		}
		else
		{
			NDebugOverlay::Box( pNearest->WorldSpaceCenter(), Vector(-8, -8, -8), Vector(8, 8, 8), 0, 255, 0, true, 30 );
		}
	}
#endif

	if ( sv_debug_player_use.GetBool() )
	{
		Msg( "Radial using: %s\n", pNearest ? pNearest->GetDebugName() : "no usable entity found" );
	}

	return pNearest;
}

//-----------------------------------------------------------------------------
// Purpose: Handles USE keypress
//-----------------------------------------------------------------------------
#if defined( CLIENT_DLL ) && defined( L4D2 )
void CBasePlayer::PlayerUse( CBaseEntity *preferred )
#else
void CBasePlayer::PlayerUse ( void )
#endif
{
#ifdef GAME_DLL
	// Was use pressed or released?
	if ( ! ((m_nButtons | m_afButtonPressed | m_afButtonReleased) & IN_USE) )
		return;

	if ( IsObserver() )
	{
		// do special use operation in oberserver mode
		if ( m_afButtonPressed & IN_USE )
			ObserverUse( true );
		else if ( m_afButtonReleased & IN_USE )
			ObserverUse( false );
		
		return;
	}

#if !defined(_XBOX)
	// push objects in turbo physics mode
	if ( (m_nButtons & IN_USE) && sv_turbophysics.GetBool() )
	{
		Vector forward, up;
		EyeVectors( &forward, NULL, &up );

		trace_t tr;
		// Search for objects in a sphere (tests for entities that are not solid, yet still useable)
		Vector searchCenter = EyePosition();

		CUsePushFilter filter;

		UTIL_TraceLine( searchCenter, searchCenter + forward * 96.0f, MASK_SOLID, &filter, &tr );

		// try the hit entity if there is one, or the ground entity if there isn't.
		CBaseEntity *entity = tr.m_pEnt;

		if ( entity )
		{
			IPhysicsObject *pObj = entity->VPhysicsGetObject();

			if ( pObj )
			{
				Vector vPushAway = (entity->WorldSpaceCenter() - WorldSpaceCenter());
				vPushAway.z = 0;

				float flDist = VectorNormalize( vPushAway );
				flDist = MAX( flDist, 1 );

				float flForce = sv_pushaway_force.GetFloat() / flDist;
				flForce = MIN( flForce, sv_pushaway_max_force.GetFloat() );

				pObj->ApplyForceOffset( vPushAway * flForce, WorldSpaceCenter() );
			}
		}
	}
#endif

	if ( m_afButtonPressed & IN_USE )
	{
		// Controlling some latched entity?
		if ( ClearUseEntity() )
		{
			return;
		}
		else
		{
			if ( m_afPhysicsFlags & PFLAG_DIROVERRIDE )
			{
				m_afPhysicsFlags &= ~PFLAG_DIROVERRIDE;
				m_iTrain = TRAIN_NEW|TRAIN_OFF;
				return;
			}
			else
			{	// Start controlling the train!
				CBaseEntity *pTrain = GetGroundEntity();
				if ( pTrain && !(m_nButtons & IN_JUMP) && (GetFlags() & FL_ONGROUND) && (pTrain->ObjectCaps() & FCAP_DIRECTIONAL_USE) && pTrain->OnControls(this) )
				{
					m_afPhysicsFlags |= PFLAG_DIROVERRIDE;
					m_iTrain = TrainSpeed(pTrain->m_flSpeed, ((CFuncTrackTrain*)pTrain)->GetMaxSpeed());
					m_iTrain |= TRAIN_NEW;
					EmitSound( "Player.UseTrain" );
					return;
				}
			}
		}
	}

	CBaseEntity *pUseEntity = FindUseEntity();

	// Found an object
	if ( pUseEntity )
	{

		//!!!UNDONE: traceline here to prevent +USEing buttons through walls			

		int caps = pUseEntity->ObjectCaps();
		variant_t emptyVariant;
		if ( ( (m_nButtons & IN_USE) && (caps & FCAP_CONTINUOUS_USE) ) || ( (m_afButtonPressed & IN_USE) && (caps & (FCAP_IMPULSE_USE|FCAP_ONOFF_USE)) ) )
		{
			if ( caps & FCAP_CONTINUOUS_USE )
			{
				m_afPhysicsFlags |= PFLAG_USING;
			}

			if ( pUseEntity->ObjectCaps() & FCAP_ONOFF_USE )
			{
				pUseEntity->AcceptInput( "Use", this, this, emptyVariant, USE_ON );
			}
			else
			{
				pUseEntity->AcceptInput( "Use", this, this, emptyVariant, USE_TOGGLE );
			}
		}
		// UNDONE: Send different USE codes for ON/OFF.  Cache last ONOFF_USE object to send 'off' if you turn away
		else if ( (m_afButtonReleased & IN_USE) && (pUseEntity->ObjectCaps() & FCAP_ONOFF_USE) )	// BUGBUG This is an "off" use
		{
			pUseEntity->AcceptInput( "Use", this, this, emptyVariant, USE_OFF );
		}
	}
	else if ( m_afButtonPressed & IN_USE )
	{
		PlayUseDenySound();
	}
#endif
}

ConVar	sv_suppress_viewpunch( "sv_suppress_viewpunch", "0", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000600A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00006006
#else
	FCVAR_REPLICATED | FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY
#endif
 );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBasePlayer::ViewPunch( const QAngle &angleOffset )
{
	//See if we're suppressing the view punching
	if ( sv_suppress_viewpunch.GetBool() )
		return;

	// We don't allow view kicks in the vehicle
	if ( IsInAVehicle() )
		return;

	m_Local.m_vecPunchAngleVel += angleOffset * 20;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBasePlayer::ViewPunchReset( float tolerance )
{
	if ( tolerance != 0 )
	{
		tolerance *= tolerance;	// square
		float check = m_Local.m_vecPunchAngleVel->LengthSqr() + m_Local.m_vecPunchAngle->LengthSqr();
		if ( check > tolerance )
			return;
	}
	m_Local.m_vecPunchAngle = vec3_angle;
	m_Local.m_vecPunchAngleVel = vec3_angle;
}

#if defined( CLIENT_DLL )

#include "iviewrender.h"
#include "ivieweffects.h"

#endif

#if defined( CLIENT_DLL ) && defined( L4D2 )
static ConVar smoothstairs( "smoothstairs", "1", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000200A
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
, "Smooth player eye z coordinate when traversing stairs." );
#else
static ConVar smoothstairs( "smoothstairs", "1", 
#if defined( L4D2 ) && defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
, "Smooth player eye z coordinate when traversing stairs." );
#endif

//-----------------------------------------------------------------------------
// Handle view smoothing when going up or down stairs
//-----------------------------------------------------------------------------
#if defined( CLIENT_DLL ) && defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#pragma fp_contract( off )
#endif
// Whole native C205D0: staircase eye-origin smoothing, not a separate bob backend.
void CBasePlayer::SmoothViewOnStairs( Vector &eyeOrigin )
{
	CBaseEntity *ground = GetGroundEntity();
	const float currentZ = GetLocalOrigin().z;
	const float viewOffsetZ = GetViewOffset().z;
	if ( ground && ground->GetMoveType() == MOVETYPE_NONE &&
		currentZ != m_flOldPlayerZ && smoothstairs.GetInt() != 0 &&
		m_flOldPlayerViewOffsetZ == viewOffsetZ )
	{
		const int direction = currentZ > m_flOldPlayerZ ? 1 : -1;
		float stepTime = gpGlobals->frametime;
		if ( stepTime < 0.0f )
			stepTime = 0.0f;
		m_flOldPlayerZ = ( stepTime * 150.0f ) * static_cast< float >( direction ) + m_flOldPlayerZ;
		if ( direction > 0 )
		{
			if ( m_flOldPlayerZ > currentZ )
				m_flOldPlayerZ = currentZ;
			if ( currentZ - m_flOldPlayerZ > 18.0f )
				m_flOldPlayerZ = currentZ - 18.0f;
		}
		else
		{
			if ( currentZ > m_flOldPlayerZ )
				m_flOldPlayerZ = currentZ;
			if ( currentZ - m_flOldPlayerZ < -18.0f )
				m_flOldPlayerZ = currentZ + 18.0f;
		}
		eyeOrigin.z = ( m_flOldPlayerZ - currentZ ) + eyeOrigin.z;
	}
	else
	{
		m_flOldPlayerZ = currentZ;
		m_flOldPlayerViewOffsetZ = viewOffsetZ;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
void CBasePlayer::SmoothViewOnStairs( Vector& eyeOrigin )
{
	CBaseEntity *pGroundEntity = GetGroundEntity();
	float flCurrentPlayerZ = GetLocalOrigin().z;
	float flCurrentPlayerViewOffsetZ = GetViewOffset().z;

	// Smooth out stair step ups
	// NOTE: Don't want to do this when the ground entity is moving the player
	if ( ( pGroundEntity != NULL && pGroundEntity->GetMoveType() == MOVETYPE_NONE ) && ( flCurrentPlayerZ != m_flOldPlayerZ ) && smoothstairs.GetBool() &&
		 m_flOldPlayerViewOffsetZ == flCurrentPlayerViewOffsetZ )
	{
		int dir = ( flCurrentPlayerZ > m_flOldPlayerZ ) ? 1 : -1;

		float steptime = gpGlobals->frametime;
		if (steptime < 0)
		{
			steptime = 0;
		}

		m_flOldPlayerZ += steptime * 150 * dir;

		const float stepSize = 18.0f;

		if ( dir > 0 )
		{
			if (m_flOldPlayerZ > flCurrentPlayerZ)
			{
				m_flOldPlayerZ = flCurrentPlayerZ;
			}
			if (flCurrentPlayerZ - m_flOldPlayerZ > stepSize)
			{
				m_flOldPlayerZ = flCurrentPlayerZ - stepSize;
			}
		}
		else
		{
			if (m_flOldPlayerZ < flCurrentPlayerZ)
			{
				m_flOldPlayerZ = flCurrentPlayerZ;
			}
			if (flCurrentPlayerZ - m_flOldPlayerZ < -stepSize)
			{
				m_flOldPlayerZ = flCurrentPlayerZ + stepSize;
			}
		}

		eyeOrigin[2] += m_flOldPlayerZ - flCurrentPlayerZ;
	}
	else
	{
		m_flOldPlayerZ = flCurrentPlayerZ;
		m_flOldPlayerViewOffsetZ = flCurrentPlayerViewOffsetZ;
	}
}

#endif

static bool IsWaterContents( int contents )
{
	if ( contents & MASK_WATER )
		return true;

//	if ( contents & CONTENTS_TESTFOGVOLUME )
//		return true;

	return false;
}

void CBasePlayer::ResetObserverMode()
{

	m_hObserverTarget.Set( 0 );
	m_iObserverMode = (int)OBS_MODE_NONE;

#ifndef CLIENT_DLL
	m_iObserverLastMode = OBS_MODE_ROAMING;
	m_bForcedObserverMode = false;
	m_afPhysicsFlags &= ~PFLAG_OBSERVER;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : eyeOrigin - 
//			eyeAngles - 
//			zNear - 
//			zFar - 
//			fov - 
//-----------------------------------------------------------------------------
#if defined( CLIENT_DLL ) && defined( L4D2 )
void CBasePlayer::CalcView( Vector &eyeOrigin, QAngle &eyeAngles, float &zNear, float &zFar, float &fov )
{
	IClientVehicle *vehicle = GetVehicle();
	if ( vehicle )
		CalcVehicleView( vehicle, eyeOrigin, eyeAngles, zNear, zFar, fov );
	else if ( GetObserverMode() != 0 )
		CalcObserverView( eyeOrigin, eyeAngles, fov );
	else
		CalcPlayerView( eyeOrigin, eyeAngles, fov ); // VMRoot owns whole C20750.

	static ConVarRef followBone( "cl_camera_follow_bone_index" );
	CStudioHdr *header = GetCameraFollowStudioHdr();
	if ( header && header->IsValid() && followBone.IsValid() && C_BasePlayer::GetLocalPlayer( 0 ) == this )
	{
		const int bone = followBone.GetInt();
		const studiohdr_t *rawHeader = header->GetRenderHdr();
		if ( bone >= -1 && bone < rawHeader->numbones )
		{
			if ( bone == -1 )
				g_vecCameraFollowBoneOrigin = GetRenderOrigin();
			else if ( rawHeader->pBone( bone )->flags & 0x0007FF00 )
			{
				const matrix3x4_t &transform = GetBone( bone ); // Cached, NOT SetupBones/GetBoneTransform.
				g_vecCameraFollowBoneOrigin.Init( transform[0][3], transform[1][3], transform[2][3] );
			}
		}
	}
}
#else
void CBasePlayer::CalcView( Vector &eyeOrigin, QAngle &eyeAngles, float &zNear, float &zFar, float &fov )
{
#if defined( CLIENT_DLL )
	IClientVehicle *pVehicle; 
#else
	IServerVehicle *pVehicle;
#endif
	pVehicle = GetVehicle();

	if ( !pVehicle )
	{
#if defined( CLIENT_DLL ) && !defined( L4D2 )
		if( UseVR() )
			g_ClientVirtualReality.CancelTorsoTransformOverride();
#endif

		if ( IsObserver() )
		{
			CalcObserverView( eyeOrigin, eyeAngles, fov );
		}
		else
		{
			CalcPlayerView( eyeOrigin, eyeAngles, fov );
		}
	}
	else
	{
		CalcVehicleView( pVehicle, eyeOrigin, eyeAngles, zNear, zFar, fov );
	}
	// NVNT update fov on the haptics dll for input scaling.
#if defined( CLIENT_DLL ) && !defined( L4D2 )
	if(IsLocalPlayer() && haptics)
		haptics->UpdatePlayerFOV(fov);
#endif
}
#endif


void CBasePlayer::CalcViewModelView( const Vector& eyeOrigin, const QAngle& eyeAngles)
{
	for ( int i = 0; i < MAX_VIEWMODELS; i++ )
	{
		CBaseViewModel *vm = GetViewModel( i );
		if ( !vm )
			continue;
	
		vm->CalcViewModelView( this, eyeOrigin, eyeAngles );
	}
}

#if defined( CLIENT_DLL ) && defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#pragma fp_contract( off )
#endif
// Whole native C20750, the ordinary player-view branch selected by C21980.
// C21980 owns vehicle/observer dispatch; this body adds neither branch again.
void CBasePlayer::CalcPlayerView( Vector &eyeOrigin, QAngle &eyeAngles, float &fov )
{
	if ( !prediction->InPrediction() )
		view->DriftPitch();

	// Native PC100A84B0 is proven pure false; the dead autoaim-offset arm folds.
	eyeOrigin = EyePosition();
	eyeAngles = EyeAngles();
	if ( !prediction->InPrediction() )
		SmoothViewOnStairs( eyeOrigin );

	for ( int i = 0; i < MAX_VIEWMODELS; ++i ) // Actual constant2, not local-user count.
	{
		CBaseViewModel *viewModel = GetViewModel( i );
		if ( viewModel )
			viewModel->CalcViewModelCamera( eyeOrigin, eyeAngles, fov );
	}

	if ( GetMoveType() != MOVETYPE_NOCLIP )
	{
		const float rollSpeed = sv_rollspeed.GetFloat();
		const float rollAngle = sv_rollangle.GetFloat();
		const Vector &velocity = GetAbsVelocity();
		const QAngle &angles = GetAbsAngles();
		const float roll = CalcRoll( angles, velocity, rollAngle, rollSpeed );
		eyeAngles.z = static_cast< float >( static_cast< double >( roll ) + static_cast< double >( eyeAngles.z ) );
	}
	eyeAngles.x = eyeAngles.x + m_Local.m_vecPunchAngle.GetX();
	eyeAngles.y = m_Local.m_vecPunchAngle.GetY() + eyeAngles.y;
	eyeAngles.z = m_Local.m_vecPunchAngle.GetZ() + eyeAngles.z;

	if ( !prediction->InPrediction() )
	{
		vieweffects->CalcShake();
		vieweffects->ApplyShake( eyeOrigin, eyeAngles, 1.0f );
	}
	Vector smoothing;
	GetPredictionErrorSmoothingVector( smoothing );
	eyeOrigin.x = eyeOrigin.x + smoothing.x;
	eyeOrigin.y = eyeOrigin.y + smoothing.y;
	eyeOrigin.z = smoothing.z + eyeOrigin.z;
	m_flObserverChaseDistance = 0.0f;
	fov = GetFOV(); // Actual C67600 family returns F32 on every exit; keep API.
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
void CBasePlayer::CalcPlayerView( Vector& eyeOrigin, QAngle& eyeAngles, float& fov )
{
#if defined( CLIENT_DLL )
	if ( !prediction->InPrediction() )
	{
		// FIXME: Move into prediction
		view->DriftPitch();
	}
#endif

	VectorCopy( EyePosition(), eyeOrigin );
#ifdef SIXENSE
	if ( g_pSixenseInput->IsEnabled() )
	{
		VectorCopy( EyeAngles() + GetEyeAngleOffset(), eyeAngles );
	}
	else
	{
		VectorCopy( EyeAngles(), eyeAngles );
	}
#else
	VectorCopy( EyeAngles(), eyeAngles );
#endif

#if defined( CLIENT_DLL )
	if ( !prediction->InPrediction() )
#endif
	{
		SmoothViewOnStairs( eyeOrigin );
	}

	// Snack off the origin before bob + water offset are applied
	Vector vecBaseEyePosition = eyeOrigin;

	CalcViewRoll( eyeAngles );

	// Apply punch angle
	VectorAdd( eyeAngles, m_Local.m_vecPunchAngle, eyeAngles );

#if defined( CLIENT_DLL )
	if ( !prediction->InPrediction() )
	{
		// Shake it up baby!
		vieweffects->CalcShake();
		vieweffects->ApplyShake( eyeOrigin, eyeAngles, 1.0 );
	}
#endif

#if defined( CLIENT_DLL )
	// Apply a smoothing offset to smooth out prediction errors.
	Vector vSmoothOffset;
	GetPredictionErrorSmoothingVector( vSmoothOffset );
	eyeOrigin += vSmoothOffset;
	m_flObserverChaseDistance = 0.0;
#endif

	// calc current FOV
	fov = GetFOV();
}

#endif

//-----------------------------------------------------------------------------
// Purpose: The main view setup function for vehicles
//-----------------------------------------------------------------------------
#if defined( CLIENT_DLL ) && defined( L4D2 )
void CBasePlayer::CalcVehicleView( IClientVehicle *vehicle, Vector &eyeOrigin, QAngle &eyeAngles,
	float &zNear, float &zFar, float &fov )
{
	CacheVehicleView();
	eyeOrigin = m_vecVehicleViewOrigin;
	eyeAngles = m_vecVehicleViewAngles;
	fov = GetFOV();
	vehicle->GetVehicleClipPlanes( zNear, zFar );
	CalcViewRoll( eyeAngles );
	eyeAngles.x = m_Local.m_vecPunchAngle.GetX() + eyeAngles.x;
	eyeAngles.y = m_Local.m_vecPunchAngle.GetY() + eyeAngles.y;
	eyeAngles.z = m_Local.m_vecPunchAngle.GetZ() + eyeAngles.z;
	if ( !prediction->InPrediction() )
	{
		vieweffects->CalcShake();
		vieweffects->ApplyShake( eyeOrigin, eyeAngles, 1.0f );
	}
}
#else
void CBasePlayer::CalcVehicleView( 
#if defined( CLIENT_DLL )
	IClientVehicle *pVehicle, 
#else
	IServerVehicle *pVehicle,
#endif
	Vector& eyeOrigin, QAngle& eyeAngles,
	float& zNear, float& zFar, float& fov )
{
	Assert( pVehicle );

	// Start with our base origin and angles
	CacheVehicleView();
	eyeOrigin = m_vecVehicleViewOrigin;
	eyeAngles = m_vecVehicleViewAngles;

#if defined( CLIENT_DLL )

	fov = GetFOV();

	// Allows the vehicle to change the clip planes
	pVehicle->GetVehicleClipPlanes( zNear, zFar );
#endif

	// Snack off the origin before bob + water offset are applied
	Vector vecBaseEyePosition = eyeOrigin;

	CalcViewRoll( eyeAngles );

	// Apply punch angle
	VectorAdd( eyeAngles, m_Local.m_vecPunchAngle, eyeAngles );

#if defined( CLIENT_DLL )
	if ( !prediction->InPrediction() )
	{
		// Shake it up baby!
		vieweffects->CalcShake();
		vieweffects->ApplyShake( eyeOrigin, eyeAngles, 1.0 );
	}
#endif

}
#endif


#if defined( CLIENT_DLL ) && defined( L4D2 )
void CBasePlayer::CalcObserverView( Vector &eyeOrigin, QAngle &eyeAngles, float &fov )
{
	switch ( GetObserverMode() )
	{
	case OBS_MODE_DEATHCAM: // 1 -> primary1316 / C2565E0 -> qualified C66ED0.
		CalcRoamingView( eyeOrigin, eyeAngles, fov );
		break;
	case OBS_MODE_FREEZECAM: // 2 -> primary1324 / C667A0.
		CalcFreezeCamView( eyeOrigin, eyeAngles, fov );
		break;
	case OBS_MODE_FIXED: // 3 -> primary1320 / C255EB0 (genuine fly/keyframe).
	case OBS_MODE_ROAMING: // 6 -> same primary1320.
		CalcInEyeCamView( eyeOrigin, eyeAngles, fov );
		break;
	case OBS_MODE_IN_EYE: // 4 -> primary1312 / actor-owned C255AA0 -> C66C20.
		CalcDeathCamView( eyeOrigin, eyeAngles, fov );
		break;
	case OBS_MODE_CHASE: // 5 -> primary1308 / C255CB0 -> C66370.
		CalcChaseCamView( eyeOrigin, eyeAngles, fov );
		break;
	}
}
#else
void CBasePlayer::CalcObserverView( Vector& eyeOrigin, QAngle& eyeAngles, float& fov )
{
#if defined( CLIENT_DLL )
	switch ( GetObserverMode() )
	{

		case OBS_MODE_DEATHCAM	:	CalcDeathCamView( eyeOrigin, eyeAngles, fov );
									break;

		case OBS_MODE_ROAMING	:	// just copy current position without view offset
		case OBS_MODE_FIXED		:	CalcRoamingView( eyeOrigin, eyeAngles, fov );
									break;

		case OBS_MODE_IN_EYE	:	CalcInEyeCamView( eyeOrigin, eyeAngles, fov );
									break;

#if !defined( L4D2 )
		case OBS_MODE_POI		: // PASSTIME
#endif
		case OBS_MODE_CHASE		:	CalcChaseCamView( eyeOrigin, eyeAngles, fov  );
									break;

		case OBS_MODE_FREEZECAM	:	CalcFreezeCamView( eyeOrigin, eyeAngles, fov  );
									break;
	}
#else
	// on server just copy target postions, final view positions will be calculated on client
	VectorCopy( EyePosition(), eyeOrigin );
	VectorCopy( EyeAngles(), eyeAngles );
#endif
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Compute roll angle for a particular lateral velocity
// Input  : angles - 
//			velocity - 
//			rollangle - 
//			rollspeed - 
// Output : float CViewRender::CalcRoll
//-----------------------------------------------------------------------------
#if defined( CLIENT_DLL ) && defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#pragma fp_contract( off )
#endif
// Whole native C1F0F0. Native scalar products/adds and the linear arm spill F32.
// The last x87 multiply is by exactly +/-1: its finite result is F32-exact,
// so the existing canonical float API is retained without an alternate helper.
float CBasePlayer::CalcRoll( const QAngle &angles, const Vector &velocity, float rollAngle, float rollSpeed )
{
	Vector forward, right, up;
	AngleVectors( angles, &forward, &right, &up );
	const float lateral = ( velocity.y * right.y + velocity.x * right.x ) + velocity.z * right.z;
	const float sign = lateral < 0.0f ? -1.0f : 1.0f;
	const float magnitude = fabsf( lateral );
	float value = rollAngle;
	if ( rollSpeed > magnitude )
	{
		const float scaled = magnitude * rollAngle;
		value = scaled / rollSpeed;
	}
	return static_cast< float >( static_cast< double >( sign ) * static_cast< double >( value ) );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#else
float CBasePlayer::CalcRoll (const QAngle& angles, const Vector& velocity, float rollangle, float rollspeed)
{
    float   sign;
    float   side;
    float   value;
	
	Vector  forward, right, up;
	
    AngleVectors (angles, &forward, &right, &up);
	
	// Get amount of lateral movement
    side = DotProduct( velocity, right );
	// Right or left side?
    sign = side < 0 ? -1 : 1;
    side = fabs(side);
    
	value = rollangle;
	// Hit 100% of rollangle at rollspeed.  Below that get linear approx.
    if ( side < rollspeed )
	{
		side = side * value / rollspeed;
	}
    else
	{
		side = value;
	}

	// Scale by right/left sign
    return side*sign;
}

#endif

//-----------------------------------------------------------------------------
// Purpose: Determine view roll, including data kick
//-----------------------------------------------------------------------------
void CBasePlayer::CalcViewRoll( QAngle& eyeAngles )
{
	if ( GetMoveType() == MOVETYPE_NOCLIP )
		return;

	float side = CalcRoll( GetAbsAngles(), GetAbsVelocity(), sv_rollangle.GetFloat(), sv_rollspeed.GetFloat() );
	eyeAngles[ROLL] += side;
}


void CBasePlayer::DoMuzzleFlash()
{
	for ( int i = 0; i < MAX_VIEWMODELS; i++ )
	{
		CBaseViewModel *vm = GetViewModel( i );
		if ( !vm )
			continue;

		vm->DoMuzzleFlash();
	}

	BaseClass::DoMuzzleFlash();
}


#if !defined( CLIENT_DLL ) || !defined( L4D2 )
float CBasePlayer::GetFOVDistanceAdjustFactor()
{
	float defaultFOV	= (float)GetDefaultFOV();
	float localFOV		= (float)GetFOV();

	if ( localFOV == defaultFOV || defaultFOV < 0.001f )
	{
		return 1.0f;
	}

	// If FOV is lower, then we're "zoomed" in and this will give a factor < 1 so apparent LOD distances can be
	//  shorted accordingly
	return localFOV / defaultFOV;
}
#endif
//========= Copyright Valve Corporation, All rights reserved. ============//
// Replace the CLIENT_DLL && L4D2 arm of the EXISTING GetFOVDistanceAdjustFactor
// in game/shared/baseplayer_shared.cpp. Stock/server keep their original float
// signature/body. Client c_baseplayer.h declares double on L4D2 only.
#if defined( CLIENT_DLL ) && defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// Whole native C1F1A0. Default FOV is queried before current FOV. Only the
// equality comparison spills the current value to float; division stays wide.
double CBasePlayer::GetFOVDistanceAdjustFactor()
{
	const float defaultFOV = static_cast< float >( GetDefaultFOV() );
	const double currentFOV = GetFOV();
	const float comparisonFOV = static_cast< float >( currentFOV );
	if ( comparisonFOV == defaultFOV || defaultFOV < 0.001f )
		return 1.0;
	return currentFOV / defaultFOV;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &vecTracerSrc - 
//			&tr - 
//			iTracerType - 
//-----------------------------------------------------------------------------
void CBasePlayer::MakeTracer( const Vector &vecTracerSrc, const trace_t &tr, int iTracerType )
{
	if ( GetActiveWeapon() )
	{
		GetActiveWeapon()->MakeTracer( vecTracerSrc, tr, iTracerType );
		return;
	}

	BaseClass::MakeTracer( vecTracerSrc, tr, iTracerType );
}


void CBasePlayer::SharedSpawn()
{
	SetMoveType( MOVETYPE_WALK );
	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_STANDABLE );
	SetFriction( 1.0f );

	pl.deadflag	= false;
	m_lifeState	= LIFE_ALIVE;
	m_iHealth = 100;
	m_takedamage		= DAMAGE_YES;

	m_Local.m_bDrawViewmodel = true;
	m_Local.m_flStepSize = sv_stepsize.GetFloat();
	m_Local.m_bAllowAutoMovement = true;

	m_nRenderFX = kRenderFxNone;
	m_flNextAttack	= gpGlobals->curtime;
	m_flMaxspeed		= 0.0f;

	MDLCACHE_CRITICAL_SECTION();
	SetSequence( SelectWeightedSequence( ACT_IDLE ) );

	if ( GetFlags() & FL_DUCKING ) 
		SetCollisionBounds( VEC_DUCK_HULL_MIN, VEC_DUCK_HULL_MAX );
	else
		SetCollisionBounds( VEC_HULL_MIN, VEC_HULL_MAX );

	// dont let uninitialized value here hurt the player
	m_Local.m_flFallVelocity = 0;

	SetBloodColor( BLOOD_COLOR_RED );
	// NVNT inform haptic dll we have just spawned local player
#if defined( CLIENT_DLL ) && !defined( L4D2 )
	if(IsLocalPlayer() &&haptics)
		haptics->LocalPlayerReset();
#endif
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBasePlayer::IsLerpingFOV( void ) const
{
	// If it's immediate, just do it
	if (m_Local.m_flFOVRate == 0.0f)
		return false;

	float deltaTime = (float)(gpGlobals->curtime - m_flFOVTime) / m_Local.m_flFOVRate;
	return deltaTime < 1.f;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
#if defined( L4D2 )
#if defined( CLIENT_DLL )
int CBasePlayer::GetDefaultFOV()
{
	if ( GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_BaseEntity *entity = GetObserverTarget();
		if ( entity && entity->IsPlayer() )
		{
			C_BasePlayer *target = static_cast< C_BasePlayer * >( entity );
			if ( target->GetObserverMode() == OBS_MODE_NONE ) return target->GetDefaultFOV();
		}
	}
	return m_iDefaultFOV != 0 ? m_iDefaultFOV : g_pGameRules->DefaultFOV();
}
#else
int CBasePlayer::GetDefaultFOV() const
{
	return m_iDefaultFOV != 0 ? m_iDefaultFOV : g_pGameRules->DefaultFOV();
}
#endif
#endif
#if !defined( L4D2 )
int CBasePlayer::GetDefaultFOV( void ) const
{
#if defined( CLIENT_DLL )
	if ( GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_BasePlayer *pTargetPlayer = dynamic_cast<C_BasePlayer*>( GetObserverTarget() );

		if ( pTargetPlayer && !pTargetPlayer->IsObserver() )
		{
			return pTargetPlayer->GetDefaultFOV();
		}
	}
#endif

	int iFOV = ( m_iDefaultFOV == 0 ) ? g_pGameRules->DefaultFOV() : m_iDefaultFOV;
	if ( iFOV > MAX_FOV )
		iFOV = MAX_FOV;

	return iFOV;
}
#endif

void CBasePlayer::AvoidPhysicsProps( CUserCmd *pCmd )
{
#ifndef _XBOX
	// Don't avoid if noclipping or in movetype none
	switch ( GetMoveType() )
	{
	case MOVETYPE_NOCLIP:
	case MOVETYPE_NONE:
	case MOVETYPE_OBSERVER:
		return;
	default:
		break;
	}

	if ( GetObserverMode() != OBS_MODE_NONE || !IsAlive() )
		return;

	AvoidPushawayProps( this, pCmd );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : const char
//-----------------------------------------------------------------------------
const char *CBasePlayer::GetTracerType( void )
{
	if ( GetActiveWeapon() )
	{
		return GetActiveWeapon()->GetTracerType();
	}

	return BaseClass::GetTracerType();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBasePlayer::ClearZoomOwner( void )
{
	m_hZoomOwner = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Sets the FOV of the client, doing interpolation between old and new if requested
// Input  : FOV - New FOV
//			zoomRate - Amount of time (in seconds) to move between old and new FOV
//-----------------------------------------------------------------------------
bool CBasePlayer::SetFOV( CBaseEntity *pRequester, int FOV, float zoomRate, int iZoomStart /* = 0 */ )
{
	//NOTENOTE: You MUST specify who is requesting the zoom change
	assert( pRequester != NULL );
	if ( pRequester == NULL )
		return false;

	// If we already have an owner, we only allow requests from that owner
	if ( ( m_hZoomOwner.Get() != NULL ) && ( m_hZoomOwner.Get() != pRequester ) )
	{
#ifdef GAME_DLL
		if ( CanOverrideEnvZoomOwner( m_hZoomOwner.Get() ) == false )
#endif
			return false;
	}
	else
	{
		//FIXME: Maybe do this is as an accessor instead
		if ( FOV == 0 )
		{
			m_hZoomOwner = NULL;
		}
		else
		{
			m_hZoomOwner = pRequester;
		}
	}

	// Setup our FOV and our scaling time

	if ( iZoomStart > 0 )
	{
		m_iFOVStart = iZoomStart;
	}
	else
	{
		m_iFOVStart = GetFOV();
	}

	m_flFOVTime = gpGlobals->curtime;
	m_iFOV = FOV;

	m_Local.m_flFOVRate	= zoomRate;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBasePlayer::UpdateUnderwaterState( void )
{
	if ( GetWaterLevel() == WL_Eyes )
	{
		if ( IsPlayerUnderwater() == false )
		{
			SetPlayerUnderwater( true );
		}
		return;
	}

	if ( IsPlayerUnderwater() )
	{
		SetPlayerUnderwater( false );
	}

	if ( GetWaterLevel() == 0 )
	{
		if ( GetFlags() & FL_INWATER )
		{
#ifndef CLIENT_DLL
			if ( m_iHealth > 0 && IsAlive() )
			{
				EmitSound( "Player.Wade" );
			}
#endif
			RemoveFlag( FL_INWATER );
		}
	}
	else if ( !(GetFlags() & FL_INWATER) )
	{
#ifndef CLIENT_DLL
		// player enter water sound
		if (GetWaterType() == CONTENTS_WATER)
		{
			EmitSound( "Player.Wade" );
		}
#endif

		AddFlag( FL_INWATER );
	}
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
// ensure that for every emitsound there is a matching stopsound
//-----------------------------------------------------------------------------
void CBasePlayer::SetPlayerUnderwater( bool state )
{
	if ( m_bPlayerUnderwater != state )
	{
		// S07ABB0/C01F290 have no haptic message or input-feedback call here.
#if defined( WIN32 ) && !defined( _X360 ) && !defined( L4D2 )
		// NVNT turn on haptic drag when underwater
		if(state)
			HapticSetDrag(this,1);
		else
			HapticSetDrag(this,0);
#endif
		m_bPlayerUnderwater = state;

#ifdef CLIENT_DLL
		if ( state )
			EmitSound( "Player.AmbientUnderWater" );
		else
			StopSound( "Player.AmbientUnderWater" );		
#endif
	}
}


void CBasePlayer::SetPreviouslyPredictedOrigin( const Vector &vecAbsOrigin )
{
	m_vecPreviouslyPredictedOrigin = vecAbsOrigin;
}

const Vector &CBasePlayer::GetPreviouslyPredictedOrigin() const
{
	return m_vecPreviouslyPredictedOrigin;
}

#if defined( L4D2 )
fogparams_t::fogparams_t()
{
	dirPrimary.Init( 1.0f, 0.0f, 0.0f );
	color32 black = { 0, 0, 0, 0 };
	colorPrimary = black;
	colorSecondary = black;
	colorPrimaryLerpTo = black;
	colorSecondaryLerpTo = black;
	start = 0.0f;
	end = 0.0f;
	farz = 0.0f;
	maxdensity = 0.0f;
	skyboxFogFactor = 0.0f;
	skyboxFogFactorLerpTo = 0.0f;
	startLerpTo = 0.0f;
	endLerpTo = 0.0f;
	maxdensityLerpTo = 0.0f;
	lerptime = 0.0f;
	duration = 0.0f;
	enable = false;
	blend = false;
	radial = false;
	HDRColorScale = 0.0f;
}
#endif

#if defined( L4D2 )
#pragma float_control( precise, on, push )
#endif
bool fogparams_t::operator !=( const fogparams_t& other ) const
{
#if defined( L4D2 )
	if ( enable != other.enable || blend != other.blend ||
		fabs( static_cast< double >( dirPrimary.Get().x ) - static_cast< double >( other.dirPrimary.Get().x ) ) > static_cast< double >( 0.01f ) ||
		fabs( static_cast< double >( dirPrimary.Get().y ) - static_cast< double >( other.dirPrimary.Get().y ) ) > static_cast< double >( 0.01f ) ||
		!( fabs( static_cast< double >( dirPrimary.Get().z ) - static_cast< double >( other.dirPrimary.Get().z ) ) <= static_cast< double >( 0.01f ) ) ||
		colorPrimary.Get() != other.colorPrimary.Get() ||
		colorSecondary.Get() != other.colorSecondary.Get() ||
		skyboxFogFactor != other.skyboxFogFactor ||
		skyboxFogFactorLerpTo != other.skyboxFogFactorLerpTo ||
		start != other.start || end != other.end || farz != other.farz ||
		maxdensity != other.maxdensity ||
		colorPrimaryLerpTo.Get() != other.colorPrimaryLerpTo.Get() ||
		colorSecondaryLerpTo.Get() != other.colorSecondaryLerpTo.Get() ||
		startLerpTo != other.startLerpTo || endLerpTo != other.endLerpTo ||
		maxdensityLerpTo != other.maxdensityLerpTo ||
		lerptime != other.lerptime || duration != other.duration ||
		HDRColorScale != other.HDRColorScale )
		return true;
	return false;
#else
	if ( this->enable != other.enable ||
		this->blend != other.blend ||
		this->radial != other.radial ||
		!VectorsAreEqual(this->dirPrimary, other.dirPrimary, 0.01f ) || 
		this->colorPrimary.Get() != other.colorPrimary.Get() ||
		this->colorSecondary.Get() != other.colorSecondary.Get() ||
		this->start != other.start ||
		this->end != other.end ||
		this->farz != other.farz ||
		this->maxdensity != other.maxdensity ||
		this->colorPrimaryLerpTo.Get() != other.colorPrimaryLerpTo.Get() ||
		this->colorSecondaryLerpTo.Get() != other.colorSecondaryLerpTo.Get() ||
		this->startLerpTo != other.startLerpTo ||
		this->endLerpTo != other.endLerpTo ||
		this->lerptime != other.lerptime ||
		this->duration != other.duration )
		return true;

	return false;
#endif
}
#if defined( L4D2 )
#pragma float_control( pop )
#endif

