//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/vomitjar_projectile.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "particle_parse.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h"
#include "eventqueue.h"
#include "NextBot/NextBotManager.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "l4d2/weapons/vomitjar_projectile_console_server_cvars.inc"
#include "tier0/memdbgon.h"

static ConVar vomitjar_radius( "vomitjar_radius", "110", 0x00006004 );
static ConVar vomitjar_radius_survivors( "vomitjar_radius_survivors", "0", 0x00006004 );
static ConVar vomitjar_duration_survivor( "vomitjar_duration_survivor", "5", 0x00006004 );
static ConVar vomitjar_duration_infected_bot( "vomitjar_duration_infected_bot", "20", 0x00006004 );
static ConVar vomitjar_duration_infected_pz( "vomitjar_duration_infected_pz", "20", 0x00006004 );
ConVar z_vomit_hit_pitch_min( "z_vomit_hit_pitch_min", "-15", 0x00004004 );
ConVar z_vomit_hit_pitch_max( "z_vomit_hit_pitch_max", "15", 0x00004004 );
ConVar z_vomit_hit_yaw_min( "z_vomit_hit_yaw_min", "-10", 0x00004004 );
ConVar z_vomit_hit_yaw_max( "z_vomit_hit_yaw_max", "10", 0x00004004 );
// This is the same original cvar used by Boomer vomit, not a jar-only timer.
ConVar z_vomit_fade_start( "z_vomit_fade_start", "5", 0x00006004, "When the vomit starts to fade away" );
extern bool IsInCommentaryMode();

LINK_ENTITY_TO_CLASS( vomitjar_projectile, CVomitJarProjectile );
IMPLEMENT_SERVERCLASS_ST( CVomitJarProjectile, DT_VomitJarProjectile )
END_SEND_TABLE()

BEGIN_DATADESC( CVomitJarProjectile )
	DEFINE_ENTITYFUNC( GrenadeTouch ),
	DEFINE_THINKFUNC( StillThink ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( info_goal_infected_chase, CInfoGoalInfectedChase );
BEGIN_DATADESC( CInfoGoalInfectedChase )
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	DEFINE_THINKFUNC( AttractThink ),
END_DATADESC()

void CInfoGoalInfectedChase::Precache()
{
	PrecacheScriptSound( "InfectedChaseGoal.Attract" );
}

void CInfoGoalInfectedChase::Spawn()
{
	Precache();
	BaseClass::Spawn();
}

void CInfoGoalInfectedChase::InputEnable( inputdata_t &inputdata )
{
	SetThink( &CInfoGoalInfectedChase::AttractThink );
	SetNextThink( gpGlobals->curtime );
}

void CInfoGoalInfectedChase::InputDisable( inputdata_t &inputdata )
{
	SetThink( NULL );
}

void CInfoGoalInfectedChase::AttractThink()
{
	// S29ABD0: the original sound GameData carries the chase stimulus.
	// M5/M6 consume it through real actor hearing; no global target rewrite.
	EmitSound( "InfectedChaseGoal.Attract" );
	SetNextThink( gpGlobals->curtime + 0.5f );
}

void CVomitJarProjectile::Precache()
{
	PrecacheModel( "models/w_models/weapons/w_eq_bile_flask.mdl" );
	PrecacheScriptSound( "CedaJar.Explode" );
	PrecacheParticleSystem( "vomit_jar" );
	BaseClass::Precache();
}

void CVomitJarProjectile::Spawn()
{
	m_stillTimer.Invalidate();
	SetModel( "models/w_models/weapons/w_eq_bile_flask.mdl" );
	BaseClass::Spawn();
}

CVomitJarProjectile *CVomitJarProjectile::Create( const Vector &position, const QAngle &angles,
	const Vector &velocity, const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower )
{
	CVomitJarProjectile *pGrenade = static_cast< CVomitJarProjectile * >(
		CBaseEntity::Create( "vomitjar_projectile", position, angles, pThrower ) );
	if ( !pGrenade )
		return NULL;
	pGrenade->SetTimer( 1.5f );
	pGrenade->SetAbsVelocity( velocity );
	pGrenade->SetInitialVelocity( velocity );
	pGrenade->SetThrower( pThrower );
	pGrenade->SetGravity( 0.4f );
	pGrenade->SetFriction( 0.2f );
	pGrenade->SetElasticity( 0.45f );
	pGrenade->SetTouch( &CVomitJarProjectile::GrenadeTouch );
	pGrenade->SetThink( &CVomitJarProjectile::StillThink );
	pGrenade->SetNextThink( gpGlobals->curtime + 2.0f );
	pGrenade->SetDamage( 200.0f );
	pGrenade->SetDamageRadius( 300.0f );
	if ( pThrower )
		pGrenade->ChangeTeam( pThrower->GetTeamNumber() );
	pGrenade->SetLocalAngularVelocity( QAngle( angularVelocity.x, angularVelocity.y, angularVelocity.z ) );
	return pGrenade;
}

void CVomitJarProjectile::GrenadeTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet( FSOLID_TRIGGER | FSOLID_VOLUME_CONTENTS ) || pOther == GetThrower() ||
		FClassnameIs( pOther, "func_breakable" ) || FClassnameIs( pOther, "func_breakable_surf" ) )
		return;
	if ( FClassnameIs( pOther, "prop_car_alarm" ) || FClassnameIs( pOther, "prop_car_glass" ) )
	{
		CTakeDamageInfo info( this, GetThrower(), 10.0f, DMG_GENERIC );
		pOther->TakeDamage( info );
	}

	const trace_t &trace = GetTouchTrace();
	if ( ( trace.m_pEnt && trace.m_pEnt->MyCombatCharacterPointer() ) || trace.plane.normal.z > 0.0f )
	{
		Detonate();
	}
	else
	{
		const Vector &velocity = GetAbsVelocity();
		SetAbsVelocity( Vector( velocity.x * 0.5f, velocity.y * 0.5f, -10.0f ) );
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CVomitJarProjectile::StillThink()
{
	if ( !( GetAbsVelocity().LengthSqr() > 25.0f ) )
	{
		if ( !( m_stillTimer.GetStartTime() > 0.0f ) )
			m_stillTimer.Start();
	}
	else
	{
		m_stillTimer.Invalidate();
	}
	if ( m_stillTimer.GetStartTime() > 0.0f && m_stillTimer.GetElapsedTime() > 0.5f )
		Detonate();
	else
		SetNextThink( gpGlobals->curtime + 0.1f );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CVomitJarProjectile::Detonate()
{
	const trace_t &touch = GetTouchTrace();
	if ( touch.surface.flags & SURF_SKY )
		return;
	Vector position = touch.endpos;
	Vector normal = touch.plane.normal;
	if ( !touch.DidHit() )
	{
		trace_t ground;
		UTIL_TraceLine( GetAbsOrigin() + Vector( 0, 0, 10 ),
			GetAbsOrigin() - Vector( 0, 0, 999999.875f ), 0x0200400B,
			this, COLLISION_GROUP_NONE, &ground );
		if ( ground.surface.flags & SURF_SKY )
			return;
		// S3F3EF0 preserves the touch end position in this fallback, but
		// takes the new ground normal. Do not silently repair that quirk.
		normal = ground.plane.normal;
	}
	EmitSound( "CedaJar.Explode" );
	SplashBile( position, normal );
	UTIL_Remove( this );
}

void CVomitJarProjectile::SplashBile( const Vector &position, const Vector &normal )
{
	// S3F39D0: impact presentation happens even when survival suppresses bile.
	trace_t trace;
	UTIL_TraceLine( position + normal * 2.0f, position, 0x0200400B,
		NULL, COLLISION_GROUP_NONE, &trace );
	UTIL_BloodDecalTrace( &trace, BLOOD_COLOR_RED );
	CEffectData data;
	data.m_vOrigin = trace.endpos;
	data.m_flScale = 1.0f;
	data.m_flMagnitude = 20.0f;
	data.m_nEntIndex = trace.m_pEnt ? trace.m_pEnt->entindex() : 0;
	DispatchEffect( "csblood", data );
	DispatchParticleEffect( "vomit_jar", trace.endpos, trace.endpos, vec3_angle );

	if ( !TerrorGameRules()->ShouldApplyVomitJarBile() )
		return;

	const Vector source = position + Vector( 0, 0, 1 );
	const float survivorRadiusSqr = Square( vomitjar_radius_survivors.GetFloat() );
	int hitCount = 0;
	CEntitySphereQuery query( source, vomitjar_radius.GetFloat() );
	for ( CBaseEntity *pEntity = query.GetCurrentEntity(); pEntity; query.NextEntity(), pEntity = query.GetCurrentEntity() )
	{
		CBaseCombatCharacter *pVictim = pEntity->MyCombatCharacterPointer();
		if ( !pVictim )
			continue;
		const int team = pVictim->GetTeamNumber();
		if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
			( pVictim->WorldSpaceCenter() - source ).LengthSqr() > survivorRadiusSqr )
			continue;
		if ( pVictim->m_takedamage != DAMAGE_NO && TerrorGameRules()->GetAmountOfEntityVisible( source, pVictim ) > 0.0f )
		{
			++hitCount;
			pVictim->OnHitByVomitJar( GetThrower() );
		}
		CTerrorPlayer *pPlayer = ToTerrorPlayer( pVictim );
		if ( team == TEAM_INFECTED && pPlayer && pPlayer->m_zombieClass == 8 && !( pPlayer->IsAlive() && pPlayer->IsIncapacitated() ) )
		{
			CBasePlayer *pThrower = ToBasePlayer( GetThrower() );
			if ( pThrower )
			{
				IGameEvent *pEvent = gameeventmanager->CreateEvent( "vomit_bomb_tank" );
				if ( pEvent )
				{
					pEvent->SetInt( "userid", pThrower->GetUserID() );
					gameeventmanager->FireEvent( pEvent );
				}
			}
		}
	}

	if ( hitCount == 0 )
		TheNextBots().OnInfectedIT( NULL );
	// The real world chase/sound producer retains its own entity lifetime.
	CBaseEntity *pGoal = CreateEntityByName( "info_goal_infected_chase" );
	if ( pGoal )
	{
		DispatchSpawn( pGoal );
		pGoal->Teleport( &source, &vec3_angle, &vec3_origin );
		variant_t value;
		pGoal->AcceptInput( "Enable", this, this, value, 0 );
		g_EventQueue.AddEvent( pGoal, "Kill", 20.0f, NULL, NULL );
	}
}

void CVomitJarProjectile::BounceSound()
{
	EmitSound( "GlassBottle.ImpactHard" );
}

// The real player timer/presentation owner. S315330 does not inflict blast
// damage and does not use Boomer's separate vomit-in-face cooldown/timer.
void CTerrorPlayer::OnHitByVomitJar( CBaseEntity *pAttacker )
{
	if ( IsInCommentaryMode() && !IsFakeClient() )
		return;
	if ( m_vomitStart == 0.0f || gpGlobals->curtime > m_vomitFadeStart )
		m_vomitStart = gpGlobals->curtime;
	m_vomitFadeStart = gpGlobals->curtime + z_vomit_fade_start.GetFloat();
	QAngle punch;
	punch.x = RandomFloat( z_vomit_hit_pitch_min.GetFloat(), z_vomit_hit_pitch_max.GetFloat() );
	punch.y = RandomFloat( z_vomit_hit_yaw_min.GetFloat(), z_vomit_hit_yaw_max.GetFloat() );
	punch.z = 0.0f;
	SetPunchAngle( punch );
	// Both callbacks observe the original pre-restart timer state.
	if ( !IsIT() )
	{
		TheNextBots().OnInfectedIT( this );
		if ( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR )
			GetMusic()->OnVomitedUpon();
	}
	m_hITSource = pAttacker && pAttacker->IsPlayer() ? pAttacker : NULL;
	m_bITFromBoomer = false;
	if ( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR )
		m_itTimer.Start( vomitjar_duration_survivor.GetFloat() );
	else
		m_itTimer.Start( IsFakeClient() ? vomitjar_duration_infected_bot.GetFloat() : vomitjar_duration_infected_pz.GetFloat() );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
bool CTerrorPlayer::IsIT() const
{
	return gpGlobals->curtime < m_itTimer.m_timestamp || m_bIsITUntilNextAttack;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void Infected::OnHitByVomitJar( CBaseEntity *pAttacker )
{
	// S400710 ignores dead commons, records the real thrower, then sends its
	// native responder event. No forced target or replacement victim member.
	if ( !IsAlive() )
		return;
	if ( !IsIT() )
		TheNextBots().OnInfectedIT( this );
	m_hITSource = pAttacker && pAttacker->IsPlayer() ? pAttacker : NULL;
	m_itTimer.Start( vomitjar_duration_infected_bot.GetFloat() );
	L4DGameStats().Event_VomitJarHit( ToTerrorPlayer( pAttacker ) );
	GetIntentionInterface()->OnHitByVomitJar( pAttacker );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
bool Infected::IsIT() const
{
	return gpGlobals->curtime < m_itTimer.m_timestamp;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CTerrorPlayer::UpdateITEvents()
{
	// S3497A0 is driven by the player's world think, not by a projectile
	// timer or a locally predicted fade. Retain the second IsIT query below.
	if ( IsIT() )
	{
		if ( !m_bWasIT )
		{
			IGameEvent *pEvent = gameeventmanager->CreateEvent( "player_now_it" );
			if ( pEvent )
			{
				pEvent->SetInt( "userid", GetUserID() );
				pEvent->SetBool( "exploded", m_bITFromBoomer );
				CBasePlayer *pSource = ToBasePlayer( m_hITSource.Get() );
				if ( pSource )
					pEvent->SetInt( "attacker", pSource->GetUserID() );
				pEvent->SetBool( "by_boomer", pSource && pSource->GetTeamNumber() == TEAM_INFECTED );
				gameeventmanager->FireEvent( pEvent );
			}
		}
	}
	else if ( m_bWasIT )
	{
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "player_no_longer_it" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", GetUserID() );
			gameeventmanager->FireEvent( pEvent );
		}
		if ( IsAlive() )
			GetMusic()->OnITExpired();
		m_itTimer.Invalidate();
		SpeakIfAllowed( "PlayerVomitExpired" );
	}
	m_bWasIT = IsIT();
}

// Native server 0x3f3560; 0x5c5270 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace vomitjar_projectilePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "vomitjar_projectile", 0x5c5270 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "vomitjar_projectile", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
