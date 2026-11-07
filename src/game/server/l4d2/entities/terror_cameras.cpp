//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/terror_cameras.h"
#include "l4d2/player/terrorplayer.h"
#include "basecombatweapon.h"
#include "mathlib/mathlib.h"
#include <math.h>
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_view_parameters, CInfoViewParameters );

BEGIN_DATADESC( CInfoViewParameters )
	DEFINE_KEYFIELD( m_nViewMode, FIELD_INTEGER, "ViewMode" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( point_deathfall_camera, CDeathFallCamera );

BEGIN_DATADESC( CDeathFallCamera )
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_KEYFIELD( m_fov, FIELD_FLOAT, "fov" ),
	DEFINE_KEYFIELD( m_fovSpeed, FIELD_FLOAT, "fov_rate" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( point_viewcontrol_multiplayer, CTriggerCameraMultiplayer );

BEGIN_DATADESC( CTriggerCameraMultiplayer )
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "AddPlayer", InputAddPlayer ),
	DEFINE_INPUTFUNC( FIELD_VOID, "RemovePlayer", InputRemovePlayer ),
	DEFINE_INPUTFUNC( FIELD_VOID, "StartMovement", InputStartMovement ),
	DEFINE_KEYFIELD( m_fov, FIELD_FLOAT, "fov" ),
	DEFINE_KEYFIELD( m_fovSpeed, FIELD_FLOAT, "fov_rate" ),
	DEFINE_KEYFIELD( m_targetEntName, FIELD_STRING, "target_entity" ),
	DEFINE_KEYFIELD( m_flInterpTime, FIELD_FLOAT, "interp_time" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( point_viewcontrol_survivor, CSurvivorCamera );

BEGIN_DATADESC( CSurvivorCamera )
	DEFINE_KEYFIELD( m_fov, FIELD_FLOAT, "fov" ),
	DEFINE_KEYFIELD( m_fovSpeed, FIELD_FLOAT, "fov_rate" ),
	DEFINE_INPUTFUNC( FIELD_STRING, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "StartMovement", InputStartMovement ),
	DEFINE_FUNCTION( FollowTarget ),
END_DATADESC()


#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

CDeathFallCamera::CDeathFallCamera() : m_fov( 90.0f ), m_fovSpeed( 1.0f )
{
}

void CDeathFallCamera::Spawn()
{
	BaseClass::Spawn();
	SetMoveType( MOVETYPE_NOCLIP );
	SetSolid( SOLID_NONE );
	SetRenderColorA( 0 );
	m_nRenderMode = kRenderTransTexture;
	DispatchUpdateTransmitState();
}

int CDeathFallCamera::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

void CDeathFallCamera::InputEnable( inputdata_t &inputdata )
{
	if ( inputdata.pActivator && inputdata.pActivator->IsPlayer() )
		Enable( static_cast< CBasePlayer * >( inputdata.pActivator ) );
}

// S25B5D0. There is intentionally no Disable input and no alive/team gate.
void CDeathFallCamera::Enable( CBasePlayer *player )
{
	if ( player->GetActiveWeapon() )
		player->GetActiveWeapon()->AddEffects( EF_NODRAW );
	player->EnableControl( false );
	player->ShowViewModel( false );
	player->SetViewEntity( this );
	player->m_Local.m_iHideHUD |= 0x77b;
	if ( player->GetFOVOwner() && player->GetFOVOwner() != player )
		player->ClearZoomOwner();
	player->SetFOV( this, _mm_cvttss_si32( _mm_set_ss( m_fov ) ), m_fovSpeed, 0 );
}

CMoveableCamera::CMoveableCamera() : m_flInterpTime( 1.0f )
{
}

// S355E10, shared verbatim by both concrete moving camera Spawn endpoints.
void CMoveableCamera::Spawn()
{
	BaseClass::Spawn();
	SetMoveType( MOVETYPE_NOCLIP );
	SetSolid( SOLID_NONE );
	SetRenderColorA( 0 );
	m_nRenderMode = kRenderTransTexture;
	m_bEnabled = false;
	m_bMoving = false;
	m_bDisableAfterMove = HasSpawnFlags( 1 );
	DispatchUpdateTransmitState();
}

int CMoveableCamera::UpdateTransmitState()
{
	return SetTransmitState( m_bEnabled ? FL_EDICT_ALWAYS : FL_EDICT_DONTSEND );
}

// S3555A0. StartMovement does not test m_bEnabled and follows immediately.
void CMoveableCamera::StartMovement()
{
	if ( !m_hTarget )
	{
		Disable();
		return;
	}
	SetParent( NULL );
	SetMoveType( MOVETYPE_NOCLIP );
	m_vecStartPosition = GetAbsOrigin();
	m_angStartAngles = GetLocalAngles();
	m_bMoving = true;
	SetThink( &CMoveableCamera::FollowTarget );
	m_flStartTime = gpGlobals->curtime;
	FollowTarget();
}

// S355410. Orientation uses RAW time fraction, not the position easing.
void CMoveableCamera::FollowTarget()
{
	CBaseEntity *target = m_hTarget.Get();
	if ( !target )
	{
		Disable();
		return;
	}
	const QAngle targetAngles = target->GetAbsAngles();
	const float elapsed = gpGlobals->curtime - m_flStartTime;
	const float fraction = elapsed / m_flInterpTime;
	QAngle angles;
	if ( targetAngles.x == m_angStartAngles.x && targetAngles.y == m_angStartAngles.y && targetAngles.z == m_angStartAngles.z )
	{
		angles = m_angStartAngles;
	}
	else
	{
		Quaternion start, end, result;
		AngleQuaternion( m_angStartAngles, start );
		AngleQuaternion( targetAngles, end );
		QuaternionSlerp( start, end, fraction, result );
		QuaternionAngles( result, angles );
	}
	SetAbsAngles( angles );
	SetNextThink( gpGlobals->curtime );
	Move();
}

// S355060. COMISS/JB selects interpolation for unordered fraction as well.
void CMoveableCamera::Move()
{
	Vector targetPosition;
	const bool hasTargetPosition = GetTargetPosition( m_hTarget.Get(), targetPosition );
	const float elapsed = gpGlobals->curtime - m_flStartTime;
	const float fraction = elapsed / m_flInterpTime;
	if ( !( fraction >= 1.0f ) )
	{
		// Main-approved minimal skip: no fabricated position when the native
		// unchecked target dereference has lost its receiver.
		if ( !hasTargetPosition )
			return;
		const Vector delta = targetPosition - m_vecStartPosition;
		const float positionFraction = static_cast< float >( GetPositionFraction( fraction ) );
		Vector position;
		position.x = delta.x * positionFraction + m_vecStartPosition.x;
		position.y = m_vecStartPosition.y + delta.y * positionFraction;
		position.z = m_vecStartPosition.z + delta.z * positionFraction;
		const Vector &origin = GetAbsOrigin();
		const float inverseFrameTime = 1.0f / gpGlobals->frametime;
		Vector velocity;
		velocity.x = ( position.x - origin.x ) * inverseFrameTime;
		velocity.y = ( position.y - origin.y ) * inverseFrameTime;
		velocity.z = ( position.z - origin.z ) * inverseFrameTime;
		SetAbsVelocity( velocity );
	}
	else
	{
		if ( hasTargetPosition )
			SetLocalOrigin( targetPosition ); // S206EB0(false) -> S63A90, not Teleport.
		// S3550B8 rereads the handle AFTER the origin setter. Do not retain
		// the getter's target across that call or skip the completion tail.
		CBaseEntity *target = m_hTarget.Get();
		if ( target )
			SetAbsAngles( target->GetLocalAngles() );
		SetAbsVelocity( vec3_origin );
		if ( m_bDisableAfterMove )
			Disable();
		m_bMoving = false;
	}
}

CTriggerCameraMultiplayer::CTriggerCameraMultiplayer() : m_fov( 90.0f ), m_fovSpeed( 1.0f )
{
}

void CTriggerCameraMultiplayer::Spawn()
{
	BaseClass::Spawn();
	DispatchUpdateTransmitState();
}

void CTriggerCameraMultiplayer::InputEnable( inputdata_t &inputdata )
{
	m_hTarget = gEntList.FindEntityByName( NULL, m_targetEntName );
	Enable();
}

void CTriggerCameraMultiplayer::InputDisable( inputdata_t &inputdata )
{
	Disable();
}

void CTriggerCameraMultiplayer::InputAddPlayer( inputdata_t &inputdata )
{
	if ( inputdata.pActivator && inputdata.pActivator->IsPlayer() )
		AddPlayer( static_cast< CBasePlayer * >( inputdata.pActivator ) );
}

void CTriggerCameraMultiplayer::InputRemovePlayer( inputdata_t &inputdata )
{
	if ( inputdata.pActivator && inputdata.pActivator->IsPlayer() )
	{
		CBasePlayer *player = static_cast< CBasePlayer * >( inputdata.pActivator );
		// Deliberately restore even when this player is not in the vector.
		RemovePlayer( player );
		const int index = m_players.Find( EHANDLE( player ) );
		if ( index != m_players.InvalidIndex() )
			m_players.FastRemove( index );
	}
}

void CTriggerCameraMultiplayer::InputStartMovement( inputdata_t &inputdata )
{
	StartMovement();
}

// S355CF0 collects ALL teams/control/life states, not just survivors.
void CTriggerCameraMultiplayer::Enable()
{
	m_bEnabled = true;
	SetAbsVelocity( vec3_origin );
	if ( m_bMoving )
		SetThink( &CTriggerCameraMultiplayer::FollowTarget );
	CUtlVector< CTerrorPlayer * > players;
	CollectTerrorPlayers( players, -1, 7, 3, 9 );
	FOR_EACH_VEC( players, i )
		AddPlayer( players[i] );
	DispatchUpdateTransmitState();
}

void CTriggerCameraMultiplayer::Disable()
{
	m_bEnabled = false;
	SetThink( NULL );
	SetLocalAngularVelocity( vec3_angle );
	FOR_EACH_VEC( m_players, i )
	{
		CBaseEntity *entity = m_players[i].Get();
		if ( entity && entity->IsPlayer() )
			RemovePlayer( static_cast< CBasePlayer * >( entity ) );
	}
	m_players.RemoveAll();
	DispatchUpdateTransmitState();
}

void CTriggerCameraMultiplayer::AddPlayer( CBasePlayer *player )
{
	const EHANDLE handle( player );
	if ( m_players.Find( handle ) != m_players.InvalidIndex() )
		return;
	m_players.AddToTail( handle );
	player->EnableControl( false );
	player->ShowViewModel( false );
	player->SetViewEntity( this );
	player->m_Local.m_iHideHUD |= 0x77b;
	CBaseEntity *owner = player->GetFOVOwner();
	if ( owner && ( owner == player || FClassnameIs( owner, "point_viewcontrol_multiplayer" ) || FClassnameIs( owner, "point_viewcontrol_survivor" ) ) )
		player->ClearZoomOwner();
	player->SetFOV( this, _mm_cvttss_si32( _mm_set_ss( m_fov ) ), m_fovSpeed, 0 );
}

void CTriggerCameraMultiplayer::RemovePlayer( CBasePlayer *player )
{
	player->EnableControl( true );
	player->ShowViewModel( true );
	player->SetViewEntity( NULL );
	player->m_Local.m_iHideHUD &= ~0x77b;
	player->SetFOV( this, 0, m_fovSpeed, 0 );
	player->ClearZoomOwner();
}

bool CTriggerCameraMultiplayer::GetTargetPosition( CBaseEntity *target, Vector &position )
{
	// S354FF0 ignores its argument and resolves its own target again.
	target = m_hTarget.Get();
	if ( !target )
		return false;
	position = target->GetAbsOrigin();
	return true;
}

double CTriggerCameraMultiplayer::GetPositionFraction( float fraction )
{
	return ( 1.0 - cos( static_cast< double >( fraction ) * 3.14159265358979323846 ) ) * 0.5;
}

CSurvivorCamera::CSurvivorCamera() : m_fov( 90.0f ), m_fovSpeed( 1.0f )
{
}

void CSurvivorCamera::Spawn()
{
	BaseClass::Spawn();
	DispatchUpdateTransmitState();
}

void CSurvivorCamera::InputEnable( inputdata_t &inputdata )
{
	m_hPlayer = NULL;
	const char *name = inputdata.value.String();
	if ( name && name[0] )
		m_hPlayer = gEntList.FindEntityByName( NULL, name );
	if ( !m_hPlayer )
		m_hPlayer = inputdata.pActivator;
	m_hTarget = m_hPlayer;
	Enable();
}

void CSurvivorCamera::InputDisable( inputdata_t &inputdata )
{
	m_hTarget = NULL;
	Disable();
}

void CSurvivorCamera::InputStartMovement( inputdata_t &inputdata )
{
	StartMovement();
}

void CSurvivorCamera::Enable()
{
	m_bEnabled = true;
	SetAbsVelocity( vec3_origin );
	if ( m_bMoving )
		SetThink( &CSurvivorCamera::FollowTarget );
	CBaseEntity *entity = m_hPlayer.Get();
	CBasePlayer *player = entity && entity->IsPlayer() ? static_cast< CBasePlayer * >( entity ) : NULL;
	if ( entity && player )
	{
		if ( HasSpawnFlags( 2 ) )
		{
			CBaseEntity *owner = player->GetFOVOwner();
			if ( owner && ( FClassnameIs( owner, "point_viewcontrol_multiplayer" ) || FClassnameIs( owner, "point_viewcontrol_survivor" ) ) )
				player->ClearZoomOwner();
			player->SetFOV( this, _mm_cvttss_si32( _mm_set_ss( m_fov ) ), m_fovSpeed, 0 );
		}
		player->SetViewEntity( this );
		player->ShowViewModel( false );
	}
	DispatchUpdateTransmitState();
}

void CSurvivorCamera::Disable()
{
	m_bEnabled = false;
	SetThink( NULL );
	SetLocalAngularVelocity( vec3_angle );
	CBaseEntity *entity = m_hPlayer.Get();
	if ( entity )
	{
		// Native calls IsAlive even for a nonplayer stored by name. Keep that
		// query, but skip only the invalid player-specific operations.
		const bool alive = entity->IsAlive();
		CBasePlayer *player = entity->IsPlayer() ? static_cast< CBasePlayer * >( entity ) : NULL;
		if ( alive && player )
		{
			player->SetViewEntity( NULL );
			player->EnableControl( true );
			player->ShowViewModel( true );
		}
		if ( HasSpawnFlags( 2 ) && player )
			player->SetFOV( this, 0, m_fovSpeed, 0 );
	}
	DispatchUpdateTransmitState();
}

bool CSurvivorCamera::GetTargetPosition( CBaseEntity *target, Vector &position )
{
	if ( !target )
		return false;
	position = target->EyePosition();
	return true;
}

double CSurvivorCamera::GetPositionFraction( float fraction )
{
	// S354EA0 -> S4EB590. Widen the f32 constants before the log/product,
	// then spill the exponent to f32 exactly once before double pow.
	const float exponent = static_cast< float >( log( static_cast< double >( 0.15f ) ) * static_cast< double >( -1.4427f ) );
	return pow( static_cast< double >( fraction ), static_cast< double >( exponent ) );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
