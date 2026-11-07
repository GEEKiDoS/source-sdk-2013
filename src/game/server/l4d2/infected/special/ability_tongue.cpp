//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/infected/special_infected_target_scan.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "particle_parse.h"
#include "in_buttons.h"
#include "l4d2/nav/terror_nav_area.h"
#include "BasePropDoor.h"
#include "eventlog.h"
#include "debugoverlay_shared.h"
#include "ipredictionsystem.h"
#include "l4d2/terror_trace_filter.h"
#include "datacache/imdlcache.h"
#include "tier0/memdbgon.h"
extern ConVar tongue_hit_delay, tongue_miss_delay, tongue_cone_start_tolerance, tongue_vs_cone_start_tolerance;
extern ConVar tongue_range, tongue_fly_speed, tongue_los_forgiveness_time, tongue_health;
extern ConVar tongue_bend_point_deflection, tongue_unbend, tongue_force_break, tongue_allow_voluntary_release;
extern ConVar tongue_player_dropping_to_ground_time, tongue_dropping_to_ground_time, tongue_debug;
extern ConVar tongue_no_progress_choke_early_ambush_delay, tongue_no_progress_choke_early_delay;
extern ConVar tongue_no_progress_tolerance, tongue_no_progress_choke_time, tongue_no_progress_damage_interval;
extern ConVar tongue_no_progress_break_interval, tongue_vertical_choke_dot, tongue_vertical_choke_height;
extern ConVar tongue_vertical_choke_time_off_ground, tongue_break_from_damage_amount;
LINK_ENTITY_TO_CLASS( ability_tongue, CTongue );
BEGIN_DATADESC( CTongue )
	DEFINE_ARRAY( m_bendPositions, FIELD_VECTOR, 10 ),
END_DATADESC()


// S36B380 overrides the base team-only policy: survivors and observers must
// receive the real authoritative tongue, including its particle segments.
int CTongue::ShouldTransmit( const CCheckTransmitInfo *info ) { return FL_EDICT_ALWAYS; }

static Vector TongueEndpoint( CTerrorPlayer *player )
{
	Vector position = player->EyePosition();
	position.x = player->GetAbsOrigin().x;
	position.y = player->GetAbsOrigin().y;
	return position;
}
static bool IsTongueSurvivor( CTerrorPlayer *player )
{
	return player && ( player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == TEAM_L4D1_SURVIVOR );
}
class CTraceFilterTongue : public CTraceFilterSimple
{
public:
	CTraceFilterTongue() : CTraceFilterSimple( NULL, COLLISION_GROUP_NONE ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE
	{
		if ( !CTraceFilterSimple::ShouldHitEntity( handle, mask ) ) return false;
		CBaseEntity *entity = EntityFromEntityHandle( handle );
		if ( !entity ) return false;
		CTerrorPlayer *player = ToTerrorPlayer( entity );
		if ( player && player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 8 ) return true;
		return entity->MyCombatCharacterPointer() == NULL;
	}
};
static void TraceTongueStep( CTongue *tongue, CTerrorPlayer *owner, const Vector &from, const Vector &to, const Vector &direction, trace_t &trace )
{
	CTraceFilterTongue filter;
	UTIL_TraceLine( from, to, 33579011, &filter, &trace );
	if ( trace.fraction < 1.0f && trace.m_pEnt && IsLowHealthBossBreakable( trace.m_pEnt ) )
	{
		CTakeDamageInfo info( tongue, owner, 10.0f, DMG_CLUB );
		CalculateMeleeDamageForce( &info, direction, from, 1.0f );
		ClearMultiDamage();
		trace.m_pEnt->DispatchTraceAttack( info, direction, &trace );
		ApplyMultiDamage();
		if ( trace.m_pEnt->GetHealth() <= 0 ) UTIL_TraceLine( from, to, 33579011, &filter, &trace );
	}
}
static void FireTongueBreak( CTerrorPlayer *owner )
{
	IGameEvent *event = gameeventmanager->CreateEvent( "tongue_broke_bent" );
	if ( event )
	{
		event->SetInt( "userid", owner->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
}

CTongue::CTongue()
{
	m_potentialTarget = NULL;
	m_currentTipTarget = NULL;
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
bool CTongue::CanActivate()
{
	CTerrorPlayer *owner = GetOwner();
	return owner && m_tongueState == 0 && !owner->IsGhost() && ( owner->GetFlags() & FL_ONGROUND ) &&
		!( owner->GetPlayerMaxSpeed() < 10.0f ) && owner->m_lifeState < LIFE_RESPAWNABLE &&
		!owner->IsStaggering() && BaseClass::CanActivate();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
void CTongue::SetOwner( CTerrorPlayer *owner )
{
	SetRechargeTime( 0.0f, 0.0f );
	m_tongueGrabStartingHealth = 0;
	m_tongueVictimPositionTime = 0.0f;
	m_potentialTarget.GetForModify().Init( 0, 0 );
	m_tongueHitRange = 0.0f;
	m_tongueHitTimestamp = 0.0f;
	m_tongueHitWasAmbush = false;
	m_tongueState = 0;
	m_draggedCharacter = m_chokedCharacter = SurvivorCharacter_Unknown;
	BaseClass::SetOwner( owner );
}

// Native inlined replacement blocks, including infected S31A960.
// Owner rebinding remains CBaseAbility::RebindOwner; neither path restarts
// this entity, bends, travel state, cooldown, or the victim's choke clock.
void CTongue::ReplacePlayerReferences( CTerrorPlayer *oldPlayer, CTerrorPlayer *newPlayer )
{
	if ( m_potentialTarget.Get() == oldPlayer ) m_potentialTarget = newPlayer;
	if ( m_currentTipTarget.Get() == oldPlayer ) m_currentTipTarget = newPlayer;
}
float CTongue::GetTongueCooldown() const
{
	CTerrorPlayer *owner = GetOwner();
	CTerrorPlayer *victim = owner ? owner->GetTongueVictim() : NULL;
	if ( !victim || !TerrorGameRules()->IsPlayerControlledZombies() || victim->IsIncapacitated() ||
		victim->IsHangingFromTongue() || victim->IsHangingFromLedge() ) return tongue_hit_delay.GetFloat();
	return tongue_miss_delay.GetFloat();
}
void CTongue::ResetTongueCooldown() { SetRechargeTime( GetTongueCooldown(), 0.0f ); }

void CTongue::ChangeState( int state )
{
	if ( m_tongueState == state ) return;
	if ( m_tongueState == 2 ) LeaveTraveling();
	else if ( m_tongueState == 3 ) LeaveAttached();
	m_tongueState = state;
	m_stateTimer.Start();
	switch ( state )
	{
	case 0: EnterReady(); break;
	case 2: EnterTraveling(); break;
	case 3: EnterAttached(); break;
	case 4: EnterDropping(); break;
	}
}
void CTongue::EnterReady()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) { UTIL_Remove( this ); return; }
	m_bendPointCount = 0;
	const int group = owner->FindBodygroupByName( "tongue" );
	if ( group >= 0 ) owner->SetBodygroup( group, 0 );
}
void CTongue::EnterTraveling()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	m_currentTipTarget = GetPotentialTarget();
	owner->SetBodygroup( owner->FindBodygroupByName( "tongue" ), 1 );
	m_bendPointCount = 0;
	m_tipPosition = m_launchPosition;
	owner->EyeVectors( &m_tipVelocity );
	m_tipVelocity *= tongue_fly_speed.GetFloat();
	SetRechargeTime( tongue_miss_delay.GetFloat(), 0.0f );
	m_lostLineOfSightTimer.Invalidate();
	owner->EmitInfectedSound( "SmokerZombie.TongueAttack", 0.0f, 0.0f );
	owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_TONGUE_ATTACK_START );
	if ( owner->GetActiveWeapon() ) owner->GetActiveWeapon()->SendWeaponAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_TONGUE ) );
	IGameEvent *event = gameeventmanager->CreateEvent( "ability_use" );
	if ( event )
	{
		event->SetInt( "userid", owner->GetUserID() );
		event->SetString( "ability", GetClassname() );
		event->SetInt( "context", m_currentTipTarget.Get() != NULL );
		gameeventmanager->FireEvent( event );
	}
	if ( !m_currentTipTarget.Get() )
	{
		Debug( "Tongue launching as a miss." );
		ChangeState( 1 );
	}
	else DebugVictim( ToTerrorPlayer( m_currentTipTarget.Get() ), "Tongue launching towards victim." );
	SetAbsOrigin( m_tipPosition );
	EmitSound( "SmokerZombie.TongueFly" );
}
void CTongue::LeaveTraveling() { StopSound( "SmokerZombie.TongueFly" ); }
void CTongue::EnterDropping() { m_currentTipTarget = NULL; }
void CTongue::LeaveAttached()
{
	if ( GetOwner() ) GetOwner()->ReleaseTongue( true );
}
bool CTongue::ActivateAbility()
{
	if ( !CanActivate() || !GetOwner() ) return false;
	m_launchPosition = TongueEndpoint( GetOwner() );
	UpdatePotentialTarget();
	ChangeState( 2 );
	BaseClass::ActivateAbility();
	return true;
}
void CTongue::UpdatePotentialTarget()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	MDLCACHE_CRITICAL_SECTION();
	const float tolerance = TerrorGameRules()->IsPlayerControlledZombies() ? tongue_vs_cone_start_tolerance.GetFloat() : tongue_cone_start_tolerance.GetFloat();
	TongueTargetScan scan( owner, tolerance, tongue_range.GetFloat() );
	CBaseEntity *oldTarget = m_potentialTarget.Get();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() != 0 && player->IsConnected() && scan( player ) == false ) break;
	}
	m_potentialTarget = scan.GetTarget();
	if ( tongue_debug.GetInt() >= 2 && m_potentialTarget.Get() )
		NDebugOverlay::EntityBounds( m_potentialTarget.Get(), 255, 0, 0, 50, 0.1f );
	if ( !IsTongueExtended() && oldTarget != m_potentialTarget.Get() )
	{
		if ( !m_potentialTarget.Get() ) Debug( "Tongue not targeting anyone." );
		else DebugVictim( scan.GetTarget(), "Tongue targeting new victim." );
	}
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CTongue::UpdateMiss()
{
	if ( m_stateTimer.GetStartTime() <= 0.0f || m_stateTimer.GetElapsedTime() > 1.0f ) ChangeState( 0 );
	if ( !( m_tipVelocity.x <= -0.01f || m_tipVelocity.x >= 0.01f ||
		m_tipVelocity.y <= -0.01f || m_tipVelocity.y >= 0.01f ||
		m_tipVelocity.z <= -0.01f || m_tipVelocity.z >= 0.01f ) ) return;
	Vector direction = m_tipVelocity;
	VectorNormalize( direction );
	trace_t trace;
	TraceTongueStep( this, GetOwner(), m_tipPosition, m_tipPosition + direction * ( tongue_fly_speed.GetFloat() * gpGlobals->frametime ), direction, trace );
	m_tipPosition = trace.endpos;
	if ( trace.fraction < 1.0f ) m_tipVelocity.Init();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
void CTongue::UpdateDropping()
{
	const float wait = GetOwner()->IsBot() ? tongue_dropping_to_ground_time.GetFloat() : tongue_player_dropping_to_ground_time.GetFloat();
	if ( m_stateTimer.GetStartTime() <= 0.0f || m_stateTimer.GetElapsedTime() > wait ) ChangeState( 0 );
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CTongue::EnterAttached()
{
	CTerrorPlayer *owner = GetOwner();
	CTerrorPlayer *target = ToTerrorPlayer( m_currentTipTarget.Get() );
	if ( !owner ) return;
	if ( !target || !target->IsAlive() || !IsTongueSurvivor( target ) )
	{
		Debug( "TONGUE: not a good target\n" );
		ChangeState( 4 );
		return;
	}
	owner->GrabVictimWithTongue( target );
	CTerrorPlayer *victim = owner->GetTongueVictim();
	if ( !victim )
	{
		Debug( "Tongue dropping because GrabVictimWithTongue somehow failed." );
		ChangeState( 4 );
		return;
	}
	m_tongueGrabStartingHealth = owner->GetHealth();
	m_draggedCharacter = victim->GetSurvivorCharacter();
	const Vector rangeDelta = owner->GetAbsOrigin() - victim->GetAbsOrigin();
	m_tongueHitRange = sqrtf( ( rangeDelta.y * rangeDelta.y + rangeDelta.z * rangeDelta.z ) + rangeDelta.x * rangeDelta.x );
	m_grabPosition = victim->GetAbsOrigin();
	m_tongueHitTimestamp = gpGlobals->curtime;
	m_tongueHitWasAmbush = false;
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
	{
		Vector to = victim->GetAbsOrigin() - owner->GetAbsOrigin();
		Vector forward;
		victim->EyeVectors( &forward );
		VectorNormalize( to );
		VectorNormalize( forward );
		if ( DotProduct( forward, to ) > 0.0f )
		{
			Debug( "Tongue from behind, attack was an ambush.\n" );
			m_tongueHitWasAmbush = true;
		}
	}
	m_tongueVictimDistance = m_tongueHitRange;
	m_tongueVictimPositionTime = gpGlobals->curtime;
	m_noProgressTimer.Invalidate();
	SetRechargeTime( tongue_hit_delay.GetFloat(), 0.0f );
	m_bendPointCount = 0;
	m_targetPosition = TongueEndpoint( victim );
	m_previousTargetPosition = m_targetPosition;
	m_currentTipTarget = NULL;
	m_tongueHealth = tongue_health.GetFloat();
	owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_TONGUE_CHOKE_START );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
void CTongue::OnTouch( CBaseEntity *entity )
{
	CTerrorPlayer *owner = GetOwner();
	CTerrorPlayer *victim = owner ? owner->GetTongueVictim() : NULL;
	if ( entity != victim || !victim ) return;
	if ( !victim->IsHangingFromTongue() ) owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_TONGUE_WALL_HIT );
	victim->SetReachedTongueOwner( true );
	victim->BeginTongueChoke( 2 );
}
void CTongue::OnRemove()
{
	ChangeState( 0 );
	CTerrorPlayer *owner = GetOwner();
	if ( m_tongueState != 3 && owner )
	{
		if ( m_chokedCharacter == SurvivorCharacter_Unknown )
		{
			if ( m_draggedCharacter != SurvivorCharacter_Unknown ) owner->RecordSmokerDrag( m_draggedCharacter );
			m_draggedCharacter = SurvivorCharacter_Unknown;
		}
		else
		{
			owner->RecordSmokerChoke( m_chokedCharacter );
			m_chokedCharacter = m_draggedCharacter = SurvivorCharacter_Unknown;
		}
	}
}
void CTongue::RemoveAbility()
{
	if ( m_tongueState == 0 ) { UTIL_Remove( this ); return; }
	ChangeState( 4 );
	SetThink( &CBaseEntity::SUB_Remove );
	SetNextThink( gpGlobals->curtime + 2.0f );
}
void CTongue::Update()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	m_launchPosition = TongueEndpoint( owner );
	UpdatePotentialTarget();
	switch ( m_tongueState )
	{
	case 1: UpdateMiss(); break;
	case 2: UpdateTraveling(); break;
	case 3: UpdateAttached(); break;
	case 4: UpdateDropping(); break;
	}
	if ( m_tongueState != 3 )
	{
		if ( m_chokedCharacter == SurvivorCharacter_Unknown )
		{
			if ( m_draggedCharacter != SurvivorCharacter_Unknown ) owner->RecordSmokerDrag( m_draggedCharacter );
			m_draggedCharacter = SurvivorCharacter_Unknown;
		}
		else
		{
			owner->RecordSmokerChoke( m_chokedCharacter );
			m_chokedCharacter = m_draggedCharacter = SurvivorCharacter_Unknown;
		}
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CTongue::UpdateTraveling()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	CTerrorPlayer *target = ToTerrorPlayer( m_currentTipTarget.Get() );
	if ( !target )
	{
		Debug( "TONGUE: Lost target\n" );
		ChangeState( 4 );
		return;
	}
	m_targetPosition = TongueEndpoint( target );
	CTraceFilterSimpleListNoGhosts ghostFilter( COLLISION_GROUP_NONE );
	CTraceFilterSimpleListNoInfectedOrGhosts infectedFilter( COLLISION_GROUP_NONE );
	CTraceFilterSimpleListNoGhosts *sightFilter = TerrorGameRules()->IsPlayerControlledZombies() ? &infectedFilter : &ghostFilter;
	sightFilter->AddEntityToIgnore( owner );
	trace_t trace;
	UTIL_TraceLine( m_launchPosition, m_targetPosition, 1107320961, sightFilter, &trace );
	if ( !( trace.fraction < 1.0f ) || trace.m_pEnt == target ) m_lostLineOfSightTimer.Invalidate();
	else if ( m_lostLineOfSightTimer.GetStartTime() > 0.0f )
	{
		if ( m_lostLineOfSightTimer.GetElapsedTime() > tongue_los_forgiveness_time.GetFloat() )
		{
			Debug( "TONGUE: Lost line of sight\n" );
			ChangeState( 4 );
			return;
		}
	}
	else m_lostLineOfSightTimer.Start();
	Vector direction = m_targetPosition - m_tipPosition;
	const float distance = VectorNormalize( direction );
	const float maximumStep = tongue_fly_speed.GetFloat() * gpGlobals->frametime;
	const float step = distance > maximumStep ? maximumStep : distance;
	TraceTongueStep( this, owner, m_tipPosition, m_tipPosition + direction * step, direction, trace );
	m_tipPosition = trace.endpos;
	SetAbsOrigin( m_tipPosition );
	if ( trace.fraction < 1.0f )
	{
		Debug( "hit geometry" );
		ChangeState( 4 );
		return;
	}
	const Vector reachDelta = m_tipPosition - m_targetPosition;
	const float distanceSquared = ( reachDelta.x * reachDelta.x + reachDelta.y * reachDelta.y ) + reachDelta.z * reachDelta.z;
	if ( !( distanceSquared < 25.0f ) ) return;
	if ( m_lostLineOfSightTimer.GetStartTime() > 0.0f )
	{
		Debug( "TONGUE: dropping because failed the last moment LOS check before grabbing." );
		ChangeState( 4 );
		return;
	}
	TerrorNavArea *targetArea = ToTerrorNavArea( target->GetLastKnownArea() );
	TerrorNavArea *ownerArea = ToTerrorNavArea( owner->GetLastKnownArea() );
	if ( targetArea && ownerArea &&
		targetArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) != ownerArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
	{
		CBasePropDoor *door = dynamic_cast<CBasePropDoor *>( gEntList.FindEntityByClassnameNearest(
			"prop_door_rotating_checkpoint", owner->WorldSpaceCenter(), tongue_range.GetFloat() * 2.0f ) );
		if ( door && door->IsCheckpointDoor() && ( door->IsDoorClosed() || door->IsDoorClosing() ) )
		{
			Debug( "Tongue dropping because we tried to grab through a closed checkpoint door." );
			ChangeState( 4 );
			return;
		}
	}
	ChangeState( 3 );
}

bool CTongue::AddBend( const Vector &position )
{
	CTerrorPlayer *owner = GetOwner();
	CTerrorPlayer *victim = owner ? owner->GetTongueVictim() : NULL;
	if ( !victim ) return true;
	const Vector from = m_bendPointCount ? m_bendPositions[m_bendPointCount - 1] : m_launchPosition;
	CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
	trace_t trace;
	UTIL_TraceLine( from, position, 33579139, &filter, &trace );
	if ( !( trace.fraction < 1.0f ) || trace.m_pEnt == victim ||
		( trace.m_pEnt && trace.m_pEnt->MyCombatCharacterPointer() ) ) return false;
	if ( m_bendPointCount >= 10 ) return true;
	Vector along = trace.endpos - from;
	Vector incoming = trace.endpos - m_previousTargetPosition;
	VectorNormalize( along );
	VectorNormalize( incoming );
	Vector normal = CrossProduct( along, incoming );
	VectorNormalize( normal );
	Vector deflection = CrossProduct( along, normal );
	VectorNormalize( deflection );
	m_bendPositions.Set( m_bendPointCount, trace.endpos + deflection * tongue_bend_point_deflection.GetFloat() );
	++m_bendPointCount;
	if ( tongue_debug.GetBool() )
	{
		const Vector &bend = m_bendPositions[m_bendPointCount - 1];
		DebugVictim( victim, "Tongue bending on %s at %.0f,%.0f,%.0f",
			trace.m_pEnt ? trace.m_pEnt->GetClassname() : "world", bend.x, bend.y, bend.z );
		NDebugOverlay::Cross3D( bend, 5.0f, 255, 0, 0, true, 30.0f );
	}
	return false;
}

// S36CC20 samples every TWO world units along the victim's swept endpoint.
// A single endpoint ray is not equivalent near corners or moving victims.
bool CTongue::RemoveBend()
{
	CTerrorPlayer *owner = GetOwner();
	CTerrorPlayer *victim = owner ? owner->GetTongueVictim() : NULL;
	if ( !victim ) return true;
	MDLCACHE_CRITICAL_SECTION();
	const int oldCount = m_bendPointCount;
	Vector direction = m_targetPosition - m_previousTargetPosition;
	const float distance = VectorNormalize( direction );
	for ( float step = 0.0f; step <= distance; step += 2.0f )
		if ( AddBend( m_previousTargetPosition + direction * step ) ) return true;
	if ( AddBend( m_previousTargetPosition ) ) return true;
	if ( oldCount != m_bendPointCount || oldCount <= 0 || !tongue_unbend.GetBool() ) return false;
	CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
	const Vector previous = m_bendPointCount <= 1 ? m_launchPosition : m_bendPositions[m_bendPointCount - 2];
	const Vector bend = m_bendPositions[m_bendPointCount - 1];
	direction = m_targetPosition - bend;
	const float length = VectorNormalize( direction );
	trace_t trace;
	for ( float step = 0.0f; step <= length; step += 2.0f )
	{
		UTIL_TraceLine( previous, bend + direction * step, 33579139, &filter, &trace );
		if ( trace.fraction < 1.0f && trace.m_pEnt != victim ) return false;
	}
	UTIL_TraceLine( previous, bend + direction * length, 33579139, &filter, &trace );
	if ( !( trace.fraction < 1.0f ) || trace.m_pEnt == victim )
	{
		Debug( "Unbending!" );
		--m_bendPointCount;
	}
	return false;
}

void CTongue::UpdateVerticalChoke()
{
	CTerrorPlayer *victim = GetOwner() ? GetOwner()->GetTongueVictim() : NULL;
	if ( !victim ) return;
	if ( victim->GetGroundEntity() ) m_tongueVictimLastOnGroundTime = gpGlobals->curtime;
	Vector direction = victim->GetAbsOrigin() - GetPullPosition();
	if ( direction.Length2DSqr() > 2500.0f && !victim->GetAbsVelocity().IsZero( 0.01f ) ) return;
	VectorNormalize( direction );
	if ( tongue_vertical_choke_dot.GetFloat() > -direction.z ) return;
	bool hanging = false;
	if ( gpGlobals->curtime - m_tongueVictimLastOnGroundTime > tongue_vertical_choke_time_off_ground.GetFloat() )
		hanging = true;
	else
	{
		trace_t trace;
		const Vector origin = victim->GetAbsOrigin();
		UTIL_TraceHull( origin, origin - Vector( 0, 0, tongue_vertical_choke_height.GetFloat() ),
			g_pGameRules->GetViewVectors()->m_vHullMin, g_pGameRules->GetViewVectors()->m_vHullMax,
			16395, victim, COLLISION_GROUP_NONE, &trace );
		if ( trace.fraction >= 1.0f ) hanging = true;
	}
	if ( victim->IsHangingFromLedge() || !hanging ) victim->EndTongueChoke( 1 );
	else
	{
		victim->BeginTongueChoke( 1 );
		m_chokedCharacter = victim->GetSurvivorCharacter();
	}
}

bool CTongue::UpdateGroundChoke()
{
	CTerrorPlayer *owner = GetOwner();
	CTerrorPlayer *victim = owner ? owner->GetTongueVictim() : NULL;
	if ( !victim ) return false;
	if ( victim->HasReachedTongueOwner() ) return true;
	bool shouldChoke = false;
	const Vector rangeDelta = victim->GetAbsOrigin() - owner->GetAbsOrigin();
	const float distance = sqrtf( ( rangeDelta.y * rangeDelta.y + rangeDelta.z * rangeDelta.z ) + rangeDelta.x * rangeDelta.x );
	if ( victim->IsHangingFromLedge() ||
		m_tongueVictimDistance - distance > tongue_no_progress_tolerance.GetFloat() )
	{
		if ( !victim->IsHangingFromLedge() )
		{
			m_tongueVictimDistance = distance;
			m_tongueVictimPositionTime = gpGlobals->curtime;
		}
		m_noProgressTimer.Invalidate();
	}
	else if ( gpGlobals->curtime - m_tongueVictimPositionTime <= tongue_no_progress_choke_time.GetFloat() )
		m_noProgressTimer.Invalidate();
	else
	{
		shouldChoke = true;
		if ( !victim->IsHangingFromTongue() && victim->m_lastDamageTime > gpGlobals->curtime - 2.0f )
		{
			CBaseEntity *attacker = victim->m_lastDamageInfo.GetAttacker();
			if ( attacker && attacker->GetTeamNumber() == TEAM_INFECTED && attacker != owner )
			{
				shouldChoke = false;
				m_noProgressTimer.Invalidate();
			}
		}
		if ( shouldChoke && m_noProgressTimer.GetStartTime() <= 0.0f ) m_noProgressTimer.Start();
	}
	if ( victim->GetGroundEntity() )
	{
		if ( !shouldChoke ) victim->EndTongueChoke( 2 );
		else if ( m_noProgressTimer.GetStartTime() > 0.0f &&
			m_noProgressTimer.GetElapsedTime() > tongue_no_progress_damage_interval.GetFloat() )
			victim->BeginTongueChoke( 2 );
	}
	else if ( !victim->IsHangingFromTongue() && m_noProgressTimer.GetStartTime() > 0.0f &&
		m_noProgressTimer.GetElapsedTime() > tongue_no_progress_break_interval.GetFloat() ) return false;
	return true;
}

void CTongue::UpdateAttached()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	if ( tongue_force_break.GetBool() )
	{
		tongue_force_break.SetValue( 0 );
		ChangeState( 4 );
		return;
	}
	CTerrorPlayer *victim = owner->GetTongueVictim();
	if ( !victim || !victim->IsAlive() || !IsTongueSurvivor( victim ) ) { ChangeState( 4 ); return; }
	if ( !( owner->GetFlags() & FL_ONGROUND ) || ( victim->GetFlags() & FL_ONTRAIN ) || victim->m_positionEntity.Get() )
	{
		FireTongueBreak( owner );
		ChangeState( 4 );
		return;
	}
	m_targetPosition = victim->WorldSpaceCenter();
	UpdateVerticalChoke();
	const float elapsed = gpGlobals->curtime - m_tongueHitTimestamp;
	if ( ( ( elapsed >= tongue_no_progress_choke_early_ambush_delay.GetFloat() && m_tongueHitWasAmbush ) ||
		elapsed >= tongue_no_progress_choke_early_delay.GetFloat() ) && !UpdateGroundChoke() )
	{
		FireTongueBreak( owner );
		ChangeState( 4 );
		return;
	}
	if ( RemoveBend() )
	{
		FireTongueBreak( owner );
		ChangeState( 4 );
		return;
	}
	if ( tongue_allow_voluntary_release.GetBool() && !owner->IsBot() && ( owner->m_afButtonPressed & IN_ATTACK2 ) )
	{
		ChangeState( 4 );
		return;
	}
	m_previousTargetPosition = m_targetPosition;
	if ( !victim->IsHangingFromTongue() ) victim->SetProneTongueDrag( true );
	m_tipPosition = m_targetPosition;
}

static void FireTongueStopped( CTerrorPlayer *owner, CTerrorPlayer *victim, CBasePlayer *attacker, int releaseType, int damageType )
{
	const bool choking = victim->IsHangingFromTongue();
	IGameEvent *event = gameeventmanager->CreateEvent( choking ? "choke_stopped" : "tongue_pull_stopped" );
	if ( !event ) return;
	if ( attacker ) event->SetInt( "userid", attacker->GetUserID() );
	event->SetInt( "victim", victim->GetUserID() );
	event->SetInt( "smoker", owner->GetUserID() );
	event->SetInt( "release_type", releaseType );
	if ( !choking && releaseType == 4 ) event->SetInt( "damage_type", damageType );
	gameeventmanager->FireEvent( event );
}
void CTongue::TakeDamage( CBasePlayer *attacker, float damage, int damageType, const Vector &position, const Vector &direction )
{
	CTerrorPlayer *owner = GetOwner();
	CTerrorPlayer *victim = owner ? owner->GetTongueVictim() : NULL;
	if ( !victim ) return;
	DebugVictim( victim, "Tongue took %f damage (was %f/%f).", damage, m_tongueHealth, tongue_health.GetFloat() );
	QAngle angles;
	VectorAngles( direction, angles );
	{
		CDisablePredictionFiltering disablePredictionFiltering;
		DispatchParticleEffect( "blood_impact_smoker_01", position, angles );
	}
	m_tongueHealth -= damage;
	if ( m_tongueHealth <= 0.0f )
	{
		FireTongueStopped( owner, victim, attacker, 4, damageType );
		DebugVictim( owner->GetTongueVictim(), "Tongue letting go because of damage." );
		const float cooldown = GetTongueCooldown();
		owner->ReleaseTongue( false );
		SetRechargeTime( cooldown, 0.0f );
	}
}
void CTongue::OnOwnerDamaged( const CTakeDamageInfo &info )
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || TerrorGameRules()->IsSurvivalMode() ) return;
	CTerrorPlayer *victim = owner->GetTongueVictim();
	if ( !owner->IsAlive() || !victim || ( info.GetDamageType() & DMG_BURN ) ||
		owner->GetHealth() >= m_tongueGrabStartingHealth - tongue_break_from_damage_amount.GetInt() ) return;
	FireTongueStopped( owner, victim, ToBasePlayer( info.GetAttacker() ), 3, info.GetDamageType() );
	DebugVictim( owner->GetTongueVictim(), "Tongue letting go for taking too much damage." );
	const float cooldown = GetTongueCooldown();
	owner->ReleaseTongue( false );
	SetRechargeTime( cooldown, 0.0f );
	if ( !TerrorGameRules()->IsPlayerControlledZombies() )
	{
		CTakeDamageInfo lethal( info );
		lethal.AddDamage( (float)owner->GetHealth() );
		owner->TakeDamage( lethal );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CTongue::Debug( const char *format, ... )
{
	if ( !tongue_debug.GetBool() || !GetOwner() ) return;
	char text[4096];
	va_list args;
	va_start( args, format );
	V_vsnprintf( text, sizeof( text ), format, args );
	va_end( args );
	DevMsg( "(TONGUE) %s  Smoker: %s\n", text, GetOwner()->GetPlayerName() );
	char ownerIdentity[256] = {};
	static_cast< CEventLog * >( GameLogSystem() )->GetEntityIdentity( GetOwner(), ownerIdentity, sizeof( ownerIdentity ) );
	UTIL_LogPrintf( "(TONGUE) %s  Smoker:%s.\n", text, ownerIdentity );
}
void CTongue::DebugVictim( CTerrorPlayer *victim, const char *format, ... )
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner )
		return;
	char text[4096];
	va_list args;
	va_start( args, format );
	V_vsnprintf( text, sizeof( text ), format, args );
	va_end( args );
	char ownerIdentity[256] = {};
	static_cast< CEventLog * >( GameLogSystem() )->GetEntityIdentity( owner, ownerIdentity, sizeof( ownerIdentity ) );
	char victimIdentity[256] = {};
	if ( victim )
		static_cast< CEventLog * >( GameLogSystem() )->GetEntityIdentity( victim, victimIdentity, sizeof( victimIdentity ) );
	if ( tongue_debug.GetBool() )
	{
		const char *victimName = victim ? victim->GetPlayerName() : "<NONE>";
		const char *ownerName = owner->GetPlayerName();
		DevMsg( "(TONGUE) %s  Smoker: %s. Victim: %s.\n", text, ownerName, victimName );
	}
	UTIL_LogPrintf( "(TONGUE) %s  Smoker:%s. Victim:%s.\n", text, ownerIdentity, victimIdentity );
}
