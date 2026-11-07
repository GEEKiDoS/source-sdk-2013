//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/prop_minigun.h"
#include "l4d2/player/terrorplayer.h"
#include "basecombatweapon.h"
#include "ammodef.h"
#include "igameevents.h"
#include "in_buttons.h"
#include "ipredictionsystem.h"
#include "ndebugoverlay.h"
#include "networkstringtable_gamedll.h"
#include "NextBot/NextBot.h"
#include "l4d2/weapons/prop_minigun_console_server_cvars.inc"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif


static ConVar z_minigun_rate_of_fire( "z_minigun_rate_of_fire", "1500", 0x00004004,
	"Minigun rate of fire, in bullets/minute", true, 60.0f, false, 0.0f );
static ConVar z_minigun_damage_rate( "z_minigun_damage_rate", "48000", 0x00004004,
	"Minigun damage rate, in health/minute", true, 60.0f, false, 0.0f );
static ConVar z_minigun_fire_think_interval( "z_minigun_fire_think_interval", "0.2", 0x00004004 );
static ConVar z_minigun_overheat_time( "z_minigun_overheat_time", "20", 0x00004004,
	"Minigun overheat time", true, 0.1f, true, 100.0f );
static ConVar z_minigun_cooldown_time( "z_minigun_cooldown_time", "3", 0x00004004,
	"Minigun cooldown time", true, 0.1f, true, 100.0f );
static ConVar z_minigun_spin_up_speed( "z_minigun_spin_up_speed", "15", FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED | FCVAR_GAMEDLL );
static ConVar z_minigun_spin_down_speed( "z_minigun_spin_down_speed", "4", FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED | FCVAR_GAMEDLL );
static ConVar z_minigun_firing_speed( "z_minigun_firing_speed", "15", FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED | FCVAR_GAMEDLL );
static ConVar z_minigun_stand_distance( "z_minigun_stand_distance", "36", FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED | FCVAR_GAMEDLL );
static ConVar z_minigun_spread( "z_minigun_spread", "7", FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED | FCVAR_GAMEDLL,
	"Minigun spray in degrees from forward" );
static ConVar z_minigun_debug( "z_minigun_debug", "0", FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED | FCVAR_GAMEDLL );

LINK_ENTITY_TO_CLASS( prop_minigun_l4d1, CPropMinigun );

IMPLEMENT_SERVERCLASS_ST( CPropMinigun, DT_PropMinigun )
	SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),
	// These deliberately repeat the mounted base's fields in the original DT.
	SendPropBool( SENDINFO( m_overheated ) ),
	SendPropFloat( SENDINFO( m_heat ), 16, 0, 0.0f, 1.0f ),
END_SEND_TABLE()

BEGIN_DATADESC( CPropMinigun )
	DEFINE_KEYFIELD( m_maxYaw, FIELD_FLOAT, "MaxYaw" ),
	DEFINE_KEYFIELD( m_maxPitch, FIELD_FLOAT, "MaxPitch" ),
	DEFINE_KEYFIELD( m_minPitch, FIELD_FLOAT, "MinPitch" ),
	DEFINE_THINKFUNC( AngleThink ),
	DEFINE_THINKFUNC( FireThink ),
	DEFINE_OUTPUT( m_OnFireStart, "OnFireStart" ),
	DEFINE_OUTPUT( m_OnFireStop, "OnFireStop" ),
END_DATADESC()

CPropMinigun::CPropMinigun()
{
	m_lastFireAngles.Init();
	m_targetSpinSpeed = 0.0f;
	m_barrelRotation = 0.0f;
}

void CPropMinigun::Precache()
{
	PrecacheScriptSound( "Minigun.SpinUp" );
	PrecacheScriptSound( "Minigun.Fire" );
	PrecacheScriptSound( "Minigun.SpinDown" );
	PrecacheModel( "models/w_models/weapons/w_minigun.mdl" );
	g_pStringTableEffectDispatch->AddString( IsServer(), "ParticleTracer" );
	PrecacheParticleSystem( "weapon_tracers" );
	BaseClass::Precache();
}

void CPropMinigun::Spawn()
{
	// S:0x3D8C10 leaves model selection to the map/CDynamicProp. Unlike the
	// .50 caliber, it does not replace the model with a hard-coded one.
	Precache();
	BaseClass::Spawn();
	SetClassname( "prop_minigun_l4d1" );
	InitializeMountedWeapon();
	m_lastFireTime = 0.0f;
	m_lastFireAngles.Init();
	SetThink( &CPropMinigun::AngleThink );
	SetNextThink( gpGlobals->curtime + 0.1f );
}

void CPropMinigun::GetUserPosition( Vector *pPosition )
{
	if ( pPosition )
	{
		Vector forward;
		AngleVectors( QAngle( 0.0f, GetAbsAngles().y, 0.0f ), &forward );
		*pPosition = GetAbsOrigin() - forward * z_minigun_stand_distance.GetFloat();
	}
}

void CPropMinigun::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pActivator );
	if ( ( GetUser() && GetUser() != pActivator ) || !pPlayer )
		return;

	if ( pPlayer->GetUseEntity() == this )
	{
		if ( useType != USE_SET )
		{
			pPlayer->SetUseEntity( NULL );
			pPlayer->OnUseEntityChanged();
			m_owner = NULL;
			RestoreUserWeapon( pPlayer );
		}
	}
	else if ( OnControls( pPlayer ) )
	{
		pPlayer->SetUseEntity( this );
		pPlayer->OnUseEntityChanged();
		m_owner = pPlayer;
		CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
		if ( pWeapon )
			pWeapon->Holster( NULL );
		SetContextThink( &CPropMinigun::FireThink,
			gpGlobals->curtime + z_minigun_fire_think_interval.GetFloat(), "FireContext" );
		m_lastFireTime = gpGlobals->curtime;
	}
}

void CPropMinigun::AngleThink()
{
	SetNextThink( gpGlobals->curtime + 0.1f );
	const float interval = gpGlobals->curtime - m_lastAngleThink;
	m_lastAngleThink = gpGlobals->curtime;
	UpdateSpin( interval );
	UpdateHeat( interval );
	UpdateParentAngles();
}

void CPropMinigun::UpdateSpin( float interval )
{
	// S:0x3D9240: audio/muzzle/barrel rendering is client-owned; the server
	// emits map outputs only on these two exact spin threshold transitions.
	const float rate = m_firing ? z_minigun_spin_up_speed.GetFloat() : z_minigun_spin_down_speed.GetFloat();
	m_targetSpinSpeed = m_firing ? z_minigun_firing_speed.GetFloat() : 0.0f;
	const float previous = m_spinSpeed;
	if ( m_spinSpeed != m_targetSpinSpeed )
		m_spinSpeed = Approach( m_targetSpinSpeed, m_spinSpeed, rate * interval );
	if ( m_spinSpeed != previous )
	{
		if ( m_spinSpeed == m_targetSpinSpeed && m_targetSpinSpeed > 0.0f )
			m_OnFireStart.FireOutput( this, this );
		else if ( previous == z_minigun_firing_speed.GetFloat() && m_targetSpinSpeed == 0.0f )
			m_OnFireStop.FireOutput( this, this );
	}
	m_barrelRotation += interval * m_spinSpeed;
}

void CPropMinigun::UpdateHeat( float interval )
{
	float heat;
	if ( m_firing && m_spinSpeed >= z_minigun_firing_speed.GetFloat() )
	{
		heat = m_heat + interval / z_minigun_overheat_time.GetFloat();
		if ( heat > 1.0f )
			heat = 1.0f;
	}
	else
	{
		heat = m_heat - interval / z_minigun_cooldown_time.GetFloat();
		if ( heat < 0.0f )
			heat = 0.0f;
	}
	m_heat = heat;

	if ( m_heat == 0.0f )
		m_overheated = false;
	else if ( m_heat == 1.0f )
	{
		m_overheated = true;
		m_firing = false;
	}
}

// S:0x3D69D0 and 0x3D6FA0. The cone applies independent 26/101 random
// trials to targets, not a conventional one-ray rifle/pellet damage model.
class CMinigunSpray
{
public:
	CMinigunSpray( CPropMinigun *pGun, CTerrorPlayer *pPlayer, const Vector &origin,
		const Vector &direction, int ammoType, float coneCosine )
		: m_pGun( pGun ), m_pPlayer( pPlayer ), m_origin( origin ), m_direction( direction ),
		m_ammoType( ammoType ), m_coneCosine( coneCosine )
	{
	}

	bool operator()( INextBot *pBot )
	{
		return ApplyTo( pBot->GetEntity() );
	}

	bool ApplyTo( CBaseCombatCharacter *pTarget )
	{
		if ( pTarget == m_pPlayer || !pTarget->IsAlive() )
			return true;
		CTerrorPlayer *pTargetPlayer = ToTerrorPlayer( pTarget );
		if ( ( pTargetPlayer && pTargetPlayer->IsGhost() ) || RandomInt( 0, 100 ) > 25 )
			return true;

		const Vector delta = m_direction * 10000.0f;
		Vector closest;
		float t;
		CalcClosestPointOnLineSegment( pTarget->WorldSpaceCenter(), m_origin, m_origin + delta, closest, &t );
		Vector direction = delta * t;
		if ( t >= 0.0f && t <= 1.0f )
		{
			const Vector mins = pTarget->GetAbsOrigin() + pTarget->CollisionProp()->OBBMins();
			const Vector maxs = pTarget->GetAbsOrigin() + pTarget->CollisionProp()->OBBMaxs();
			if ( CalcSqrDistanceToAABB( mins, maxs, m_origin + direction ) > 0.0f )
				direction = pTarget->WorldSpaceCenter() - m_origin;
			const float distance = VectorNormalize( direction );
			if ( DotProduct( m_direction, direction ) > m_coneCosine )
			{
				CTraceFilterNoNPCsOrPlayer filter( m_pGun, COLLISION_GROUP_NONE );
				trace_t trace;
				UTIL_TraceLine( m_origin, m_origin + direction * distance, MASK_SOLID, &filter, &trace );
				if ( trace.fraction == 1.0f )
				{
					ClearMultiDamage();
					CTakeDamageInfo damage( m_pGun, m_pPlayer,
						z_minigun_damage_rate.GetFloat() / z_minigun_rate_of_fire.GetFloat(),
						static_cast< int >( 0x80000002u ) );
					CalculateBulletDamageForce( &damage, m_ammoType, m_direction, trace.endpos );
					damage.SetAmmoType( m_ammoType );
					// Intentionally dispatch to the candidate with the unobstructed
					// scenery trace, as the original does; do not replace with TakeDamage.
					pTarget->DispatchTraceAttack( damage, m_direction, &trace );
					ApplyMultiDamage();
				}
			}
		}
		return true;
	}

private:
	CPropMinigun *m_pGun;
	CTerrorPlayer *m_pPlayer;
	const Vector &m_origin;
	const Vector &m_direction;
	int m_ammoType;
	float m_coneCosine;
};

void CPropMinigun::FireBullet( CTerrorPlayer *pPlayer, const Vector &vecEyePosition,
	const QAngle &angles, float coneSpread, bool bLastBullet )
{
	IncrementTerrorBulletSerial();
	const int ammoType = GetAmmoDef()->Index( "AMMO_TYPE_MINIGUN" );
	Vector direction;
	AngleVectors( angles, &direction );
	CMinigunSpray spray( this, pPlayer, vecEyePosition, direction, ammoType, 1.0f - coneSpread );

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *pTarget = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( pTarget && pTarget->edict() && pTarget->entindex() != 0 && pTarget->IsPlayer() &&
			pTarget->State_Get() != STATE_PICKINGTEAM && !pTarget->MyNextBotPointer() )
		{
			if ( !spray.ApplyTo( pTarget ) )
				break;
		}
	}
	TheNextBots().ForEachBot( spray );
	CBaseEntity *pRock = gEntList.FindEntityByClassname( NULL, "tank_rock" );
	if ( pRock && pRock->MyCombatCharacterPointer() )
		spray.ApplyTo( pRock->MyCombatCharacterPointer() );

	// The cone worker never sets the original hit flag. Only the last bullet
	// in a think gets the independent, jittered scenery/physics trace.
	if ( !bLastBullet )
		return;
	QAngle jitteredAngles = angles;
	const float theta = RandomFloat( 0.0f, 2.0f * M_PI );
	const float radius = RandomFloat( 0.0f, z_minigun_spread.GetFloat() * 0.5f );
	jitteredAngles.y = static_cast< float >( cos( static_cast< double >( theta ) ) * radius + jitteredAngles.y );
	jitteredAngles.x = static_cast< float >( sin( static_cast< double >( theta ) ) * radius + jitteredAngles.x );
	AngleVectors( jitteredAngles, &direction );
	CTraceFilterSkipClassname filter( this, "player", COLLISION_GROUP_NONE );
	trace_t trace;
	UTIL_TraceLine( vecEyePosition, vecEyePosition + direction * 10000.0f, MASK_SOLID, &filter, &trace );
	if ( z_minigun_debug.GetBool() )
		NDebugOverlay::Line( trace.startpos - Vector( 0.0f, 0.0f, 10.0f ), trace.endpos, 255, 0, 0, false, 0.1f );
	IGameEvent *pImpact = gameeventmanager->CreateEvent( "bullet_impact" );
	if ( pImpact )
	{
		pImpact->SetInt( "userid", pPlayer->GetUserID() );
		pImpact->SetFloat( "x", trace.endpos.x );
		pImpact->SetFloat( "y", trace.endpos.y );
		pImpact->SetFloat( "z", trace.endpos.z );
		gameeventmanager->FireEvent( pImpact );
	}
	if ( !( trace.surface.flags & ( SURF_SKY | SURF_NODRAW | SURF_HINT | SURF_SKIP ) ) )
	{
		if ( !trace.m_pEnt )
			return;
		if ( !trace.m_pEnt->MyCombatCharacterPointer() )
			UTIL_ImpactTrace( &trace, static_cast< int >( 0x80000002u ) );
	}
	if ( trace.m_pEnt )
	{
		ClearMultiDamage();
		float mass = 1.0f;
		float push = 50.0f;
		IPhysicsObject *pPhysics = trace.m_pEnt->VPhysicsGetObject();
		if ( pPhysics )
			mass = pPhysics->GetMass();
		if ( !pPhysics || mass <= 1000.0f )
			push = 20.0f;
		CTakeDamageInfo damage( this, pPlayer,
			( z_minigun_damage_rate.GetFloat() / z_minigun_rate_of_fire.GetFloat() ) * 10.0f,
			static_cast< int >( 0x80000002u ) );
		CalculateMeleeDamageForce( &damage, direction, trace.endpos, ( mass * 0.00066666666f * push ) * 0.1f );
		damage.SetAmmoType( ammoType );
		trace.m_pEnt->DispatchTraceAttack( damage, direction, &trace );
		pPlayer->TraceAttackToTriggers( damage, trace.startpos, trace.endpos, direction );
		ApplyMultiDamage();
	}
}

void CPropMinigun::FireThink()
{
	CTerrorPlayer *pPlayer = GetUser();
	if ( !pPlayer )
	{
		m_firing = false;
		return;
	}
	if ( pPlayer->GetUseEntity() != this )
	{
		m_firing = false;
		m_owner = NULL;
		return;
	}
	if ( !pPlayer->IsPlayer() )
	{
		m_firing = false;
		return;
	}
	const float interval = z_minigun_fire_think_interval.GetFloat();
	SetContextThink( &CPropMinigun::FireThink, gpGlobals->curtime + interval, "FireContext" );
	QAngle angles = pPlayer->EyeAngles() - m_initialAngles.Get();
	angles.x = AngleNormalize( angles.x );
	angles.y = AngleNormalize( angles.y );
	angles.x = clamp( angles.x, m_minPitch.Get(), m_maxPitch.Get() );
	angles.y = clamp( angles.y, -m_maxYaw.Get(), m_maxYaw.Get() );
	angles.x += m_initialAngles.Get().x;
	angles.y += m_initialAngles.Get().y;
	angles.z = m_initialAngles.Get().z;
	UpdateMountedPose( interval );

	if ( !( pPlayer->m_nButtons & IN_ATTACK ) )
	{
		m_lastFireTime = gpGlobals->curtime;
		m_firing = false;
	}
	if ( ( pPlayer->m_nButtons & IN_ATTACK ) && pPlayer->CanAttack() && !m_overheated )
	{
		m_firing = true;
		if ( m_spinSpeed >= z_minigun_firing_speed.GetFloat() )
		{
			const float fireInterval = 60.0f / z_minigun_rate_of_fire.GetFloat();
			if ( gpGlobals->curtime > m_lastFireTime + fireInterval )
			{
				pPlayer->NoteTerrorWeaponFired();
				Vector muzzle = GetAbsOrigin();
				QAngle muzzleAngles;
				const int attachment = LookupAttachment( "muzzle_flash" );
				if ( attachment >= 0 )
					GetAttachment( attachment, muzzle, muzzleAngles );
				const Vector eye = pPlayer->EyePosition();
				IGameEvent *pEvent = gameeventmanager->CreateEvent( "weapon_fire" );
				if ( pEvent )
				{
					pEvent->SetInt( "userid", pPlayer->GetUserID() );
					pEvent->SetString( "weapon", GetClassname() );
					pEvent->SetInt( "weaponid", 54 );
					gameeventmanager->FireEvent( pEvent );
				}
				pPlayer->SetNoiseLevel( 300.0f );
				const float bullets = ( gpGlobals->curtime - m_lastFireTime ) / fireInterval;
				const int count = _mm_cvttss_si32( _mm_set_ss( floorf( bullets ) ) );
				m_lastFireTime += static_cast< float >( count ) * fireInterval;
				QAngle step;
				// S3D813D spills 1/fractional-count before multiplying each
				// normalized difference; direct division rounds differently.
				const float inverseBullets = 1.0f / bullets;
				step.x = static_cast< float >( static_cast< double >( AngleNormalize( angles.x - m_lastFireAngles.x ) ) * inverseBullets );
				step.y = static_cast< float >( static_cast< double >( AngleNormalize( angles.y - m_lastFireAngles.y ) ) * inverseBullets );
				step.z = static_cast< float >( static_cast< double >( AngleNormalize( angles.z - m_lastFireAngles.z ) ) * inverseBullets );
				CDisablePredictionFiltering disablePredictionFiltering;
				const float coneSpread = static_cast< float >( 1.0 - cos( static_cast< double >( z_minigun_spread.GetFloat() ) * 0.017453292f ) );
				for ( int i = 0; i < count; ++i )
				{
					m_lastFireAngles += step;
					FireBullet( pPlayer, eye, m_lastFireAngles, coneSpread, i == count - 1 );
				}
			}
		}
	}
	else
	{
		if ( pPlayer->m_nButtons & ( IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_ATTACK2 ) )
		{
			pPlayer->SetUseEntity( NULL );
			pPlayer->OnUseEntityChanged();
			m_owner = NULL;
			RestoreUserWeapon( pPlayer );
		}
		m_firing = false;
	}
	// S:0x3D7CD0 divides by the fractional bullet count, then snaps the saved
	// angles to the requested aim even when no bullet was emitted. Preserve it.
	m_lastFireAngles = angles;
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
