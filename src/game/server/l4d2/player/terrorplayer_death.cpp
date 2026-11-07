//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/effects/cs_ragdoll.h"
#include "l4d2/effects/survivor_death_model.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "CRagdollMagnet.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "l4d2/stats/player_reporter.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "l4d2/infected/special/ability_vomit.h"
#include "l4d2/weapons/inferno.h"
#include "ammodef.h"
#include "particle_parse.h"
#include "ipredictionsystem.h"
#include "l4d2/entities/visibility_monitor.h"
#include "ndebugoverlay.h"
#include "EntityFlame.h"
#include "hierarchy.h"
#include "items.h"
#include "BasePropDoor.h"
#include "props_shared.h"
#include "nav_mesh.h"
#include "entitylist.h"
#include "world.h"
#include "NextBot/NextBotInterface.h"

#ifdef L4D2
#include "precache_register.h"
#endif
#include "l4d2/player/terrorplayer_death_console_server_cvars.inc"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// S:24B23D. Both signs of zero suppress this one corpse callback. An
// unordered damage value does not; keep that distinction under /fp:fast.
static bool IsSilentPlayerDeath( const CTakeDamageInfo &info )
{
	return info.GetDamageType() == DMG_NEVERGIB &&
		( FloatBits( info.GetDamage() ) & 0x7FFFFFFFu ) == 0;
}

// S:24B020. info is the Terror layer's reattributed copy, not the original
// fatal hit. The lower base's force snapshot precedes pose and magnet work.
void CCSPlayer::Event_Killed( const CTakeDamageInfo &info )
{
	SetArmorValue( 0 );
	DropWeapons( true );
	SetProgressBarTime( 0.0f );
	m_bIsDefusing = false;
	m_bHasNightVision = false;
	m_bNightVisionOn = false;
	m_bHasHelmet = false;
	m_flFlashDuration = 0.0f;
	FlashlightTurnOff( false );
	SetObserverTarget( info.GetAttacker() );

	CTakeDamageInfo baseInfo( info );
	baseInfo.SetDamageForce( m_vecTotalBulletForce );
	SelectDeathPose( info );
	CRagdollMagnet *magnet = CRagdollMagnet::FindBestMagnet( this );
	if ( magnet )
		m_vecTotalBulletForce += magnet->GetForceVector( this );

	if ( !IsSilentPlayerDeath( info ) )
		CreateRagdollEntity( info );

	State_Leave( STATE_DEATH_ANIM );
	State_Enter( STATE_DEATH_ANIM );
	CBasePlayer::Event_Killed( baseInfo );
	TerrorGameRules()->CheckWinConditions();
	PrintDamageGiven();
	PrintDamageTaken();
	m_damageGiven.PurgeAndDeleteElements(); // 245490: delete each record, then purge
	m_damageTaken.PurgeAndDeleteElements();
	m_bDeathCleanup = false;
}

// S2442E0/2441F0: the sole pointer-owning damage lists print before their
// callers delete and purge the records, preserving native lifetime/order.
void CCSPlayer::PrintDamageGiven()
{
	bool first = true;
	FOR_EACH_LL( m_damageGiven, i )
	{
		if ( first )
		{
			ClientPrint( this, HUD_PRINTCONSOLE, "Player: %s1 - Damage Given\n", GetPlayerName() );
			ClientPrint( this, HUD_PRINTCONSOLE, "-------------------------\n" );
			first = false;
		}
		const TerrorDamageRecord_t &record = *m_damageGiven[i];
		char text[64];
		Q_snprintf( text, sizeof( text ), record.hits == 1 ? "%d in %d hit" : "%d in %d hits", record.damage, record.hits );
		ClientPrint( this, HUD_PRINTCONSOLE, "Damage Given to \"%s1\" - %s2\n", record.name, text );
	}
}

void CCSPlayer::PrintDamageTaken()
{
	bool first = true;
	FOR_EACH_LL( m_damageTaken, i )
	{
		if ( first )
		{
			ClientPrint( this, HUD_PRINTCONSOLE, "Player: %s1 - Damage Taken\n", GetPlayerName() );
			ClientPrint( this, HUD_PRINTCONSOLE, "-------------------------\n" );
			first = false;
		}
		const TerrorDamageRecord_t &record = *m_damageTaken[i];
		char text[64];
		Q_snprintf( text, sizeof( text ), record.hits == 1 ? "%d in %d hit" : "%d in %d hits", record.damage, record.hits );
		ClientPrint( this, HUD_PRINTCONSOLE, "Damage Taken from \"%s1\" - %s2\n", record.name, text );
	}
}

// S:245AC0. This overload consumes the damage type; the SDK's unrelated
// no-argument CreateRagdollEntity is not this original virtual interface.
void CCSPlayer::CreateRagdollEntity( const CTakeDamageInfo &info )
{
	CCSRagdoll *ragdoll = dynamic_cast< CCSRagdoll * >( m_hRagdoll.Get() );
	if ( !ragdoll )
		ragdoll = dynamic_cast< CCSRagdoll * >( CreateEntityByName( "cs_ragdoll", -1, true ) );
	if ( ragdoll )
	{
		ragdoll->SetAbsOrigin( GetAbsOrigin() );
		ragdoll->SetThink( &CCSRagdoll::PruneThink );
		// Native schedules an absolute time, including on late-map deaths.
		ragdoll->SetNextThink( 10.0f );
		ragdoll->m_hPlayer = this;
		ragdoll->m_vecRagdollOrigin = GetAbsOrigin();
		ragdoll->m_vecRagdollVelocity = GetAbsVelocity();
		ragdoll->SetModelIndex( GetModelIndex() );
		ragdoll->m_nForceBone = m_nForceBone;
		ragdoll->m_vecForce = m_vecTotalBulletForce;
		CTerrorPlayer *player = ToTerrorPlayer( this );
		if ( player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 8 )
		{
			ragdoll->m_nForceBone = 0;
			ragdoll->m_vecForce = vec3_origin;
		}
		ragdoll->m_iDeathPose = m_iDeathPose;
		ragdoll->m_iDeathFrame = m_iDeathFrame;
		ragdoll->m_bInterpOrigin = !( info.GetDamageType() & DMG_FALL );
		ragdoll->m_ragdollType = GetRagdollType();
		ragdoll->m_zombieClass = player->GetZombieClass();
		ragdoll->m_survivorCharacter = player->GetSurvivorCharacter();
		ragdoll->InitializeFromPlayer();
	}
	// The native handle assignment also runs when creation failed.
	m_hRagdoll = ragdoll;
}

// S:2FB900. The client placeholder's constructor value1 is not this query.
int CTerrorPlayer::GetRagdollType() const
{
	if ( GetTeamNumber() != TEAM_INFECTED )
		return 4;
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
		return 3;
	return 2;
}

// S:32AB90. The survivor body observes the pre-base LIFE_ALIVE/incap state.
// Do not insert OnSurvivorKilled's unrelated revive/use teardown here.
void CTerrorPlayer::CreateRagdollEntity( const CTakeDamageInfo &info )
{
	if ( ( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == 4 ) && !IsFallingFromLedge() )
	{
		CSurvivorDeathModel::Create( this );
		return;
	}
	if ( m_suppressDeathEffects )
		return;
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 2 )
	{
		SetModel( GetGender() == 2 ? "models/infected/limbs/exploded_boomette.mdl" : "models/infected/limbs/exploded_boomer.mdl" );
		m_nForceBone = 0;
		m_vecTotalBulletForce = vec3_origin;
	}
	CCSPlayer::CreateRagdollEntity( info );
}

static ConVar intensity_enemy_death_near_range( "intensity_enemy_death_near_range", "150", 0x00004004 );
static ConVar intensity_enemy_death_far_range( "intensity_enemy_death_far_range", "500", 0x00004004 );

// S:350A70 IsSurvivorTeam(int): team 2 or 4 only.
static bool IsSurvivorDeathTeam( const CTerrorPlayer *player )
{
	return player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == TEAM_L4D1_SURVIVOR;
}

// S:303E20/304110/304190 use the real connected player/edict roster.
static CTerrorPlayer *DeathParticipant( int index )
{
	CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( index ) );
	return player && player->edict() && player->entindex() != 0 && player->IsConnected() ? player : NULL;
}

static CTerrorWeapon *DeathWeapon( const CTakeDamageInfo &info )
{
	CBaseEntity *weapon = info.GetWeapon();
	return weapon ? dynamic_cast< CTerrorWeapon * >( weapon->MyCombatWeaponPointer() ) : NULL;
}

// S:25F580/25B980, the flags0/direction0 functor used by this death entry.
static float HighestLivingSurvivorDeathFlow()
{
	float flow = -9999.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = DeathParticipant( i );
		if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR )
			continue;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( area && area->GetFlowDistance() > flow )
			flow = area->GetFlowDistance();
	}
	return flow;
}

static void DispatchSurvivorDeathBlood( CTerrorPlayer *player )
{
	const Vector &velocity = player->GetAbsVelocity();
	if ( !( velocity.x * velocity.x + velocity.y * velocity.y < 100.0f && velocity.z < 10.0f ) )
		return;
	const Vector origin = player->GetAbsOrigin();
	// S:20A020 = M3's sole shared UTIL_IsGroundFlat (also used by the 34A290 incap tail
	// and 3FDDF0); the death call's native arguments are radius 48, duck-hull top, mask 400B,
	// and debug false.
	if ( !UTIL_IsGroundFlat( 48.0f, origin, VEC_DUCK_HULL_MAX.z, 0x400Bu, player, false ) )
		return;
	trace_t trace;
	UTIL_TraceLine( origin, origin - Vector( 0.0f, 0.0f, 75.0f ), 0x400Bu,
		player, COLLISION_GROUP_DEBRIS, &trace );
	if ( trace.fraction < 1.0f && !trace.allsolid && trace.m_pEnt && !trace.m_pEnt->ClassMatches( "prop_*" ) )
	{
		QAngle angles;
		VectorAngles( Vector( trace.plane.normal.x, trace.plane.normal.z, -trace.plane.normal.y ), angles );
		CDisablePredictionFiltering filtering;
		DispatchParticleEffect( "blood_bleedout", trace.endpos, angles, player );
	}
}


// S:336010's DeadCharacter criteria for the shared S:2D1B80 speech functor.
static void SpeakSurvivorDied( CTerrorPlayer *dead, const CTakeDamageInfo &info )
{
	char criteria[256] = {};
	Q_strncat( criteria, "DeadCharacter:", sizeof( criteria ) );
	Q_strncat( criteria, dead->GetResponseCharacterName(), sizeof( criteria ) );
	CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
	if ( attacker )
	{
		if ( attacker->GetTeamNumber() == TEAM_INFECTED )
		{
			Q_strncat( criteria, ",SubjectIsA:", sizeof( criteria ) );
			Q_strncat( criteria, GetZombieClassName( attacker->GetZombieClass() ), sizeof( criteria ) );
		}
		else
		{
			Q_strncat( criteria, ",SubjectTeam:Survivor", sizeof( criteria ) );
		}
	}
	SpeakConceptToSurvivors( dead, "SurvivorDied", criteria );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S:304190/2D1910. Perception owns the intensity mutator and its hold/lock
// semantics; this caller owns only the native distance/class severity choice.
void NotifyZombieDeath( CBaseCombatCharacter *dead, int zombieClass )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = DeathParticipant( i );
		if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR )
			continue;
		Vector delta = player->GetAbsOrigin() - dead->GetAbsOrigin();
		const float range = VectorNormalize( delta );
		const bool boss = zombieClass == 8 || zombieClass == 7;
		if ( intensity_enemy_death_near_range.GetFloat() > range )
		{
			if ( boss )
				player->m_survivorIntensity.Add( 4 );
			else if ( zombieClass != 0 )
				player->m_survivorIntensity.Add( 2 );
			else
				player->m_survivorIntensity.Add( !( dead->GetAbsVelocity().LengthSqr() > 22500.0f ) ? 1 : 2 );
		}
		else if ( intensity_enemy_death_far_range.GetFloat() > range )
		{
			player->m_survivorIntensity.Add( boss ? 3 : 1 );
		}
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:360C80. Free cdecl helper taking the player, not the ability. Native callers:
// 313A50, 3146F0, 31EAC0, 3290F0, 333020, 336010, 36D310, 36D560. It formats into
// 4096 bytes, then calls ability virtual slot 227 (+908): CBaseAbility nullsub 360630,
// CTongue 36BB80. The native passes the formatted text as the format argument; a '%'
// from a player name would then be reinterpreted, which is undefined. Main decision
// (2026-10-22): pass it through "%s"; identical for every text without '%'.
void DebugAbilityVictim( CTerrorPlayer *owner, CTerrorPlayer *victim, const char *format, ... )
{
	CBaseAbility *ability = owner->GetCustomAbility();
	if ( !ability )
		return;
	char text[4096];
	va_list args;
	va_start( args, format );
	V_vsnprintf( text, sizeof( text ), format, args );
	va_end( args );
	ability->DebugVictim( victim, "%s", text );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// Both death branches read CTongue's initial grab position (native +1400).
// reportRelease identifies own-victim (dx2+dy2)+dz2 versus owner-victim
// (dy2+dz2)+dx2. The ability callback precedes the fresh victim-handle read.
void CTerrorPlayer::ReleaseTongueOnDeath( bool reportRelease )
{
	if ( !m_tongueVictim.Get() )
		return;
	if ( reportRelease )
		DebugAbilityVictim( this, ToTerrorPlayer( m_tongueVictim.Get() ), "Tongue letting go on death." );
	CTerrorPlayer *victim = ToTerrorPlayer( m_tongueVictim.Get() );
	if ( !victim )
		return;
	CTongue *tongue = dynamic_cast< CTongue * >( m_customAbility.Get() );
	if ( tongue )
		tongue->ResetTongueCooldown();
	IGameEvent *event = gameeventmanager->CreateEvent( "tongue_release" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetInt( "victim", victim->GetUserID() );
		if ( tongue )
		{
			const Vector &origin = victim->GetAbsOrigin();
			const Vector &grabPosition = tongue->GetGrabPosition();
			const float dx = grabPosition.x - origin.x;
			const float dy = grabPosition.y - origin.y;
			const float dz = grabPosition.z - origin.z;
			const float distanceSquared = reportRelease ?
				( dx * dx + dy * dy ) + dz * dz :
				( dy * dy + dz * dz ) + dx * dx;
			event->SetInt( "distance", _mm_cvttss_si32( _mm_sqrt_ss( _mm_set_ss( distanceSquared ) ) ) );
		}
		else
		{
			event->SetInt( "distance", 0 );
		}
		gameeventmanager->FireEvent( event );
	}
	// The native re-resolves here and calls through a possibly NULL result if a synchronous
	// tongue_release listener cleared the handle. Same Main/user decision as 32F6B6: skip the
	// victim release, keep the owner release.
	if ( CTerrorPlayer *releasedVictim = ToTerrorPlayer( m_tongueVictim.Get() ) )
		releasedVictim->ReleaseTongueVictim();
	ReleaseTongueOwner();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:300840. The bool is part of the virtual contract but is genuinely unused.
void CTerrorPlayer::DropWeapons( bool dead )
{
	if ( GetTeamNumber() == TEAM_INFECTED )
		return;
	for ( int i = 0; i < MAX_WEAPONS; ++i )
	{
		CBaseCombatWeapon *weapon = GetWeapon( i );
		if ( !weapon )
			continue;
		if ( weapon->GetSlot() == 1 )
			UTIL_Remove( weapon );
		else
			DropWeapon( weapon, true, NULL );
	}
}

// S:336010. Keep original info A alive across the whole call. Stats, log and
// CCS receive B; CCS makes its own earlier-force snapshot C for CBasePlayer.
void CTerrorPlayer::Event_Killed( const CTakeDamageInfo &info )
{
	const bool wasGhost = IsGhost();
	EyePosition();
	m_deathTimestamp = gpGlobals->curtime; // +11612, M4's existing owner
	m_deathPosition = GetAbsOrigin();

	if ( TerrorGameRules()->IsPlayerControlledZombies() && info.GetAttacker() &&
		( info.GetAttacker()->ClassMatches( "trigger_hurt" ) || info.GetAttacker()->ClassMatches( "trigger_hurt_ghost" ) ) )
	{
		if ( IsBeingCarried() )
		{
			ToTerrorPlayer( m_carryAttacker.Get() )->RecordDamageToSurvivor( 300, false, GetSurvivorCharacter() );
			IGameEvent *event = gameeventmanager->CreateEvent( "charger_carry_kill" );
			if ( event )
			{
				event->SetInt( "userid", ToTerrorPlayer( m_carryAttacker.Get() )->GetUserID() );
				event->SetInt( "victim", GetUserID() );
				gameeventmanager->FireEvent( event );
			}
		}
		else if ( GetTerrorAnimState()->IsChargerImpactAnimation() )
		{
			if ( CTerrorPlayer *displacer = ToTerrorPlayer( GetDisplacingEntity() ) )
				displacer->RecordDamageToSurvivor( 300, false, GetSurvivorCharacter() );
		}
		else if ( m_carryVictim.Get() )
		{
			RecordDamageToSurvivor( 300, false, ToTerrorPlayer( m_carryVictim.Get() )->GetSurvivorCharacter() );
			IGameEvent *event = gameeventmanager->CreateEvent( "charger_carry_kill" );
			if ( event )
			{
				event->SetInt( "userid", GetUserID() );
				event->SetInt( "victim", ToTerrorPlayer( m_carryVictim.Get() )->GetUserID() );
				gameeventmanager->FireEvent( event );
			}
		}
	}

	if ( IsSurvivorDeathTeam( this ) )
	{
		++m_checkpointDeaths;
		CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
		const unsigned int character = GetSurvivorCharacter();
		if ( attacker && attacker->GetTeamNumber() == TEAM_INFECTED && character <= 3u )
		{
			++attacker->m_survivorKills[character];
			++attacker->m_totalSurvivorKills;
			++attacker->m_checkpointPZKills;
		}
		if ( ( info.GetDamageType() & 0x00100000 ) || m_isHangingFromTongue )
			EmitSound( "Player.NeckSnap" );
		m_deathFlowDistance = HighestLivingSurvivorDeathFlow();
		TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
		if ( area && area->GetFlowDistance() >= 0.0f )
			m_deathFlowDistance = MAX( m_deathFlowDistance, area->GetFlowDistance() );
		UpdatePotentiallyVisibleAreas( NULL, NULL );
		GetMusic()->OnKilled( m_isBleedingOut );
		DispatchSurvivorDeathBlood( this );
	}
	else
	{
		m_deathFlowDistance = 0.0f;
	}

	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
	{
		m_totalSurvivorDamage = 0;
	}
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 4 )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "spitter_killed" );
		if ( event )
		{
			CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
			event->SetInt( "userid", GetUserID() );
			event->SetInt( "attacker", attacker ? attacker->GetUserID() : 0 );
			gameeventmanager->FireEvent( event );
		}
	}
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 2 )
	{
		CVomit *vomit = dynamic_cast< CVomit * >( m_customAbility.Get() );
		if ( vomit && vomit->IsActive() )
			vomit->StopVomitEffect();
		VisibilityMonitor_RemoveEntity( this );
	}
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 6 && m_pummelTarget.Get() )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( m_pummelTarget.Get() );
		victim->UnstickAfterPin();
		victim->SetMoveType( MOVETYPE_WALK );
		victim->SetParent( NULL );
		victim->m_pummelObject = NULL;
		m_pummelTarget.Init( 0, 0 );
	}

	CTakeDamageInfo creditedInfo( info );
	if ( ( info.GetDamageType() & DMG_FALL ) && info.GetAttacker() && !info.GetAttacker()->IsPlayer() && m_hStaggerSource.Get() )
		creditedInfo.SetAttacker( m_hStaggerSource.Get() );
	if ( ( info.GetDamageType() & DMG_FALL ) && info.GetAttacker() && !info.GetAttacker()->IsPlayer() && m_hVomitDamageSource.Get() )
		creditedInfo.SetAttacker( m_hVomitDamageSource.Get() );
	L4DGameStats().Event_Kill( this, creditedInfo );
	LogTerrorDamage( creditedInfo, 2 );

	ReleaseTongueOnDeath( true );
	if ( CTerrorPlayer *owner = ToTerrorPlayer( m_tongueOwner.Get() ) )
		owner->ReleaseTongueOnDeath( false );
	if ( CTerrorPlayer *jockey = ToTerrorPlayer( m_jockeyAttacker.Get() ) )
		jockey->ReleaseJockey( NULL );
	if ( m_jockeyVictim.Get() )
		ReleaseJockey( ToTerrorPlayer( info.GetAttacker() ) );
	if ( CTerrorPlayer *carrier = ToTerrorPlayer( m_carryAttacker.Get() ) )
		carrier->ReleaseCarry( true, false, false );
	if ( m_carryVictim.Get() )
		ReleaseCarry( true, false, false );

	CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
	if ( GetTeamNumber() == TEAM_INFECTED && attacker )
	{
		char modifiers[256] = {};
		Q_snprintf( modifiers, sizeof( modifiers ), "SubjectIsA:%s,Distance:%f",
			GetZombieClassName( GetZombieClass() ), ( attacker->GetAbsOrigin() - GetAbsOrigin() ).Length() );
		attacker->SpeakIfAllowed( "KilledZombie", 1, modifiers );
	}
	else if ( IsSurvivorDeathTeam( this ) )
	{
		if ( !IsSilentPlayerDeath( info ) )
			SpeakSurvivorDied( this, info );

		m_lastPrimaryWeaponId = 0;
		CTerrorWeapon *primary = dynamic_cast< CTerrorWeapon * >( Weapon_GetSlot( 0 ) );
		if ( primary )
		{
			m_lastPrimaryWeaponId = primary->GetWeaponID();
			const int ammoType = primary->GetPrimaryAmmoType();
			const int maximum = GetAmmoDef()->MaxCarry( ammoType, this );
			m_lastPrimaryWeaponAmmo = primary->UsesClipsForAmmo1() ? MIN( primary->Clip1() + GetAmmoCount( ammoType ), maximum ) : 0;
		}
		m_lastSecondaryWeaponId = 0;
		CTerrorWeapon *secondary = dynamic_cast< CTerrorWeapon * >( Weapon_GetSlot( 1 ) );
		if ( secondary )
		{
			m_lastSecondaryWeaponId = secondary->GetWeaponID();
			if ( secondary->IsDualWielding() )
				m_lastHadElites = true;
			if ( m_lastSecondaryWeaponId != 1 )
			{
				secondary->Holster( NULL );
				Weapon_Detach( secondary );
				secondary->AddEffects( EF_NODRAW );
				m_hHiddenWeapon = secondary;
			}
		}
	}

	attacker = ToTerrorPlayer( info.GetAttacker() );
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 && attacker )
	{
		if ( m_pounceVictim.Get() )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "pounce_stopped" );
			if ( event )
			{
				event->SetInt( "userid", attacker->GetUserID() );
				event->SetInt( "victim", ToTerrorPlayer( m_pounceVictim.Get() )->GetUserID() );
				gameeventmanager->FireEvent( event );
			}
		}
		else if ( m_isAttemptingToPounce )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "pounce_attempt_stopped" );
			if ( event )
			{
				event->SetInt( "userid", attacker->GetUserID() );
				if ( CTerrorWeapon *weapon = DeathWeapon( info ) )
					event->SetInt( "weaponid", weapon->GetWeaponID() );
				gameeventmanager->FireEvent( event );
			}
		}
	}

	if ( GetTeamNumber() == TEAM_INFECTED )
	{
		NotifyZombieDeath( this, GetZombieClass() );
		TheDirector->OnInfectedKilled( this );
		SetZombieState( 0 );
		if ( TerrorGameRules()->IsPlayerControlledZombies() )
		{
			const Vector center = info.GetAttacker() ? info.GetAttacker()->WorldSpaceCenter() : WorldSpaceCenter();
			float closest = 99999.0f;
			CTerrorPlayer *target = NULL;
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *other = DeathParticipant( i );
				if ( !other || other == this || other->GetTeamNumber() != TEAM_INFECTED )
					continue;
				const float range = ( other->WorldSpaceCenter() - center ).Length();
				if ( range < closest )
				{
					closest = range;
					target = other;
				}
			}
			if ( target )
				SetObserverTarget( target );
		}
		attacker = ToTerrorPlayer( info.GetAttacker() );
		if ( info.GetDamageCustom() == 3 && attacker )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "infected_explosive_barrel_kill" );
			if ( event )
			{
				event->SetInt( "userid", attacker->GetUserID() );
				gameeventmanager->FireEvent( event );
			}
		}
	}
	else if ( IsSurvivorDeathTeam( this ) )
	{
		attacker = ToTerrorPlayer( info.GetAttacker() );
		if ( attacker && attacker->GetTeamNumber() == TEAM_INFECTED )
			++attacker->m_survivorKillScore;
	}

	const bool wasAbilityActive = IsCustomAbilityActive();
	CCSPlayer::Event_Killed( creditedInfo );
	if ( GetTeamNumber() == TEAM_INFECTED && !m_suppressDeathEffects && !wasGhost &&
		DispatchSpecialDeathEffects( info, wasAbilityActive ) )
	{
		TheDirector->OnZombieDeath( this, info );
		DropHolidayGift();
	}
	Extinguish();
	m_isIncapacitated = false;
	m_incapacitatedByAttacker = NULL;
	m_incapacitatedTimestamp.Invalidate();
}

ConVar z_exploding_inner_radius( "z_exploding_inner_radius", "130", 0x00000006 );
ConVar z_exploding_outer_radius( "z_exploding_outer_radius", "200", 0x00000006 );
ConVar z_exploding_force( "z_exploding_force", "5000", 0x00000006 );
static ConVar z_exploding_splat_radius( "z_exploding_splat_radius", "200", 0x00004004,
	"Boomers splattering on the ground make survivors inside this radius it" );


// S:303EC0 is used for Tank; S:304330 for the other class notifications.
void NotifySpecialDeathMusic( int zombieClass )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = DeathParticipant( i );
		if ( player && ( ( zombieClass == 8 && TerrorGameRules()->IsPlayerControlledZombies() ) ||
			player->GetTeamNumber() == TEAM_SURVIVOR ) )
			player->GetMusic()->OnBossDefeated( zombieClass );
	}
}

bool CTerrorPlayer::IsCustomAbilityActive() const
{
	return m_customAbility.Get() && m_customAbility->IsActive();
}

void CTerrorPlayer::ExplodeOnDeath( const CTakeDamageInfo &info )
{
	NotifySpecialDeathMusic( 2 );
	if ( m_nVariantType == 1 )
	{
		Vector forward;
		EyeVectors( &forward );
		CInferno *inferno = static_cast< CInferno * >( CBaseEntity::Create( "inferno", GetAbsOrigin(), vec3_angle, this ) );
		inferno->StartBurning( GetAbsOrigin(), Vector( 0.0f, 0.0f, 1.0f ), forward * 128.0f, 0 );
		// S336010 writes type 2 after the Boomer variant ignition.
		inferno->m_fireType = CInferno::FIREWORKS;
	}

	CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() );
	float damage = 100.0f;
	float innerRadius = z_exploding_inner_radius.GetFloat();
	float outerRadius = z_exploding_outer_radius.GetFloat();
	const int difficulty = TerrorGameRules()->GetDifficultyLevel();
	if ( difficulty >= 0 && difficulty <= 1 )
	{
		damage = 30.0f;
		innerRadius = z_exploding_outer_radius.GetFloat() * 0.89999998f;
	}
	else if ( difficulty == 2 )
	{
		damage = 49.0f;
		innerRadius = z_exploding_outer_radius.GetFloat() * 0.89999998f;
	}
	if ( info.GetDamageType() == DMG_FALL )
	{
		innerRadius = z_exploding_splat_radius.GetFloat();
		outerRadius = innerRadius;
	}

	if ( TerrorGameRules()->IsPlayerControlledZombies() && info.GetAttacker() == this )
	{
		CDisablePredictionFiltering filtering;
		DispatchParticleEffect( "boomer_explode", GetAbsOrigin(), QAngle( 0.0f, 0.0f, 1.0f ) );
		EmitSound( "BoomerZombie.Detonate" );
	}
	else if ( m_nVariantType == 1 )
	{
		CDisablePredictionFiltering filtering;
		DispatchParticleEffect( "boomer_explode", GetAbsOrigin(), QAngle( 0.0f, 0.0f, 1.0f ) );
		EmitSound( "BoomerZombie.Detonate" );
		const CTakeDamageInfo explosion( this, this, Vector( 0.0f, 0.0f, z_exploding_force.GetFloat() ),
			WorldSpaceCenter(), damage, DMG_BLAST );
		TerrorRadiusDamage( explosion, innerRadius, outerRadius, 1 );
	}
	else
	{
		const CTakeDamageInfo explosion( this, attacker ? attacker : this,
			Vector( 0.0f, 0.0f, z_exploding_force.GetFloat() ), WorldSpaceCenter(), damage, 0x08000040 );
		TerrorRadiusDamage( explosion, innerRadius, outerRadius, 2 );
	}

	int biled = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = DeathParticipant( i );
		if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && player->IsAlive() && player->IsIT() )
			++biled;
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "boomer_exploded" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetInt( "attacker", attacker ? attacker->GetUserID() : 0 );
		event->SetBool( "splashedbile", biled > 0 );
		gameeventmanager->FireEvent( event );
	}
}

// Returning false is the original unknown-class jump past scripted death
// and gift processing, not an effect failure or a fallback class.
bool CTerrorPlayer::DispatchSpecialDeathEffects( const CTakeDamageInfo &info, bool wasAbilityActive )
{
	switch ( GetZombieClass() )
	{
	case 2:
		ExplodeOnDeath( info );
		break;
	case 8:
		NotifySpecialDeathMusic( 8 );
		break;
	case 1:
	{
		EmitSound( "SmokerZombie.Death" );
		EmitSound( "SmokerZombie.Explode" );
		NotifySpecialDeathMusic( 1 );
		CDisablePredictionFiltering filtering;
		DispatchParticleEffect( "smoker_smokecloud", GetAbsOrigin(), GetAbsAngles(), this );
		const Vector cameraOrigin = GetAbsOrigin() + Vector( 0.0f, 0.0f, 24.0f );
		DispatchParticleEffect( "smoker_smokecloud_camera", GetAbsOrigin(), cameraOrigin, vec3_angle );
		SetBodygroup( FindBodygroupByName( "tongue" ), 1 );
		SetBodygroup( FindBodygroupByName( "mulletTongue" ), 1 );
		break;
	}
	case 3:
		NotifySpecialDeathMusic( 3 );
		EmitSound( "HunterZombie.Death" );
		break;
	case 6:
	{
		ReleasePummel( true, ToTerrorPlayer( info.GetAttacker() ) );
		NotifySpecialDeathMusic( 6 );
		IGameEvent *event = gameeventmanager->CreateEvent( "charger_killed" );
		if ( event )
		{
			CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
			event->SetInt( "userid", GetUserID() );
			event->SetInt( "attacker", attacker ? attacker->GetUserID() : 0 );
			CTerrorWeapon *weapon = DeathWeapon( info );
			event->SetBool( "melee", weapon && weapon->GetWeaponID() == 19 );
			event->SetBool( "charging", wasAbilityActive );
			gameeventmanager->FireEvent( event );
		}
		EmitSound( "ChargerZombie.Death" );
		SetPlaybackRate( 0.0f );
		break;
	}
	case 4:
	{
		Vector end = WorldSpaceCenter();
		end.z -= 240.0f;
		trace_t trace;
		UTIL_TraceLine( WorldSpaceCenter(), end, 0x46004003u, this, COLLISION_GROUP_NONE, &trace );
		CInferno *acid = static_cast< CInferno * >( CBaseEntity::Create( "insect_swarm", trace.endpos, vec3_angle, this ) );
		if ( GetGroundEntity() && GetGroundEntity() != GetWorldEntity() )
			acid->SetParent( GetGroundEntity(), -1 );
		acid->m_fireType = CInferno::FIREWORKS;
		acid->StartBurning( trace.endpos, Vector( 0.0f, 0.0f, 1.0f ), vec3_origin, 0 );
		EmitSound( "SpitterZombie.Death" );
		break;
	}
	case 5:
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "jockey_killed" );
		if ( event )
		{
			CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
			event->SetInt( "userid", GetUserID() );
			event->SetInt( "attacker", attacker ? attacker->GetUserID() : 0 );
			if ( CTerrorWeapon *weapon = DeathWeapon( info ) )
				event->SetString( "weapon", weapon->GetResponseRulesName() );
			gameeventmanager->FireEvent( event );
		}
		EmitSound( "JockeyZombie.Death" );
		break;
	}
	default:
		EmitSound( "Zombie.Die" );
		return false;
	}
	return true;
}

// S:30CC00. Collect before queued removal and visit in reverse native order.
// The effect handle is not forcibly cleared before UTIL_Remove processes it.
void CTerrorPlayer::Extinguish()
{
	const bool wasOnFire = ( GetFlags() & FL_ONFIRE ) != 0;
	RemoveFlag( FL_ONFIRE );
	if ( wasOnFire )
	{
		CUtlVector< CBaseEntity * > children;
		GetAllChildren( this, children );
		for ( int i = children.Count() - 1; i >= 0; --i )
		{
			if ( CEntityFlame *flame = dynamic_cast< CEntityFlame * >( children[i] ) )
				UTIL_Remove( flame );
		}
	}
	if ( GetEffectEntity() )
		UTIL_Remove( GetEffectEntity() );
	m_burnAttacker = NULL; // 12996, D's existing owner
}

// S:2983F0/298440/2984B0/298590. This is a real CItem, not a generic prop.
// It inherits CItem's data map; the pinned actor declares no extra save fields.
class CHolidayGift : public CItem
{
public:
	DECLARE_CLASS( CHolidayGift, CItem );
	virtual void Precache() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual void ItemTouch( CBaseEntity *other ) OVERRIDE;
	virtual bool MyTouch( CBasePlayer *player ) OVERRIDE;
	static CHolidayGift *CreateGift( const Vector &origin, const QAngle &angles,
		const QAngle &eyeAngles, const Vector &velocity, CBaseEntity *owner );
private:
	void SoundThink();
};

LINK_ENTITY_TO_CLASS( holiday_gift, CHolidayGift );

void CHolidayGift::Precache()
{
	BaseClass::Precache();
	PrecacheModel( "models/items/l4d_gift.mdl" );
	PrecacheScriptSound( "Christmas.GiftDrop" );
	PrecacheScriptSound( "Christmas.GiftPickup" );
}

void CHolidayGift::Spawn()
{
	BaseClass::Spawn();
	SetModel( "models/items/l4d_gift.mdl" );
	SetContextThink( &CHolidayGift::SUB_Remove, gpGlobals->curtime + 30.0f, "DIE_THINK" );
	SetContextThink( &CHolidayGift::SoundThink, gpGlobals->curtime + 0.2f, "SOUND_THINK" );
}

void CHolidayGift::SoundThink()
{
	EmitSound( "Christmas.GiftDrop" );
}

void CHolidayGift::ItemTouch( CBaseEntity *other )
{
	if ( other == GetWorldEntity() )
		SetAbsVelocity( Vector( 0.0f, 0.0f, GetAbsVelocity().z ) );
	else
		BaseClass::ItemTouch( other );
}

bool CHolidayGift::MyTouch( CBasePlayer *player )
{
	if ( !player || !player->IsAlive() || player->IsFakeClient() || player->GetTeamNumber() != TEAM_SURVIVOR )
		return false;
	IGameEvent *event = gameeventmanager->CreateEvent( "christmas_gift_grab" );
	if ( event )
	{
		event->SetInt( "userid", player->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	player->EmitSound( "Christmas.GiftPickup" );
	return true;
}

// S:298690 is cdecl with FIVE inputs. The saved EBP is not an argument.
CHolidayGift *CHolidayGift::CreateGift( const Vector &origin, const QAngle &angles,
	const QAngle &eyeAngles, const Vector &velocity, CBaseEntity *owner )
{
	CHolidayGift *gift = static_cast< CHolidayGift * >( CBaseEntity::Create( "holiday_gift", origin, angles, owner ) );
	if ( !gift )
		return NULL;
	gift->AddSpawnFlags( SF_NORESPAWN );
	Vector right, up;
	AngleVectors( eyeAngles, NULL, &right, &up );
	Vector impulse = vec3_origin;
	const float upAmount = RandomFloat( 0.0f, 0.25f );
	impulse += up * upAmount;
	const float rightAmount = RandomFloat( -0.25f, 0.25f );
	impulse += right * rightAmount;
	VectorNormalize( impulse );
	const float speed = RandomFloat( 100.0f, 150.0f );
	impulse = velocity + impulse * speed;
	const float length = sqrtf( ( impulse.y * impulse.y + impulse.z * impulse.z ) + impulse.x * impulse.x );
	if ( length > 300.0f )
	{
		const double scale = 300.0 / static_cast< double >( length );
		impulse.x = static_cast< float >( static_cast< double >( impulse.x ) * scale );
		impulse.y = static_cast< float >( static_cast< double >( impulse.y ) * scale );
		impulse.z = static_cast< float >( static_cast< double >( impulse.z ) * scale );
	}
	gift->SetMoveType( MOVETYPE_FLYGRAVITY );
	gift->SetAbsVelocity( Vector( impulse.x * 2.0f, impulse.y * 2.0f, impulse.z * 2.0f + 200.0f ) );
	gift->SetAbsAngles( vec3_angle );
	gift->UseClientSideAnimation();
	gift->ResetSequence( gift->LookupSequence( "idle" ) );
	gift->EmitSound( "Christmas.GiftDrop" );
	gift->m_Glow.SetColor( Color( 0, 192, 0, 255 ) );
	gift->m_Glow.SetType( 3 );
	gift->ActivateWhenAtRest();
	return gift;
}

static ConVar z_holiday_gift_drop_chance( "z_holiday_gift_drop_chance", "0.3",
	FCVAR_CHEAT | FCVAR_GAMEDLL, "", true, 0.0f, true, 1.0f );
static CountdownTimer s_holidayGiftDropTimer;
static int s_holidayGiftServerCount = -1;

void CTerrorPlayer::DropHolidayGift()
{
	// The pinned PC engine v126 is literally xor al,al;ret. The real season
	// bit is constructor-produced, not a dynamic calendar query here.
	if ( !TerrorGameRules()->IsHolidayGiftSeason() || !TerrorGameRules()->IsVersusMode() )
		return;
	if ( gpGlobals->serverCount != s_holidayGiftServerCount )
	{
		s_holidayGiftDropTimer.Invalidate();
		s_holidayGiftServerCount = gpGlobals->serverCount;
	}
	if ( !s_holidayGiftDropTimer.IsElapsed() )
		return;
	const float chance = z_holiday_gift_drop_chance.GetFloat();
	if ( chance > RandomFloat( 0.0f, 1.0f ) )
	{
		Vector forward, right, up;
		AngleVectors( EyeAngles(), &forward, &right, &up );
		const Vector velocity = GetAbsVelocity();
		const QAngle eyeAngles = EyeAngles();
		const QAngle angles = GetAbsAngles();
		const Vector origin = WorldSpaceCenter();
		CHolidayGift::CreateGift( origin, angles, eyeAngles, velocity, this );
		// This is the process-global DROP cooldown, even if creation failed.
		s_holidayGiftDropTimer.Start( RandomFloat( 45.0f, 75.0f ) );
	}
}

// S:3824E0. Boomer exposure/force/bile is not Rules::RadiusDamage. In
// particular type2 shoves combat characters instead of damaging them.
void TerrorRadiusDamage( const CTakeDamageInfo &info, float innerRadius, float outerRadius, int type )
{
	const Vector source = info.GetDamagePosition();
	for ( CEntitySphereQuery sphere( source, outerRadius, 0 ); sphere.GetCurrentEntity(); sphere.NextEntity() )
	{
		CBaseEntity *entity = sphere.GetCurrentEntity();
		if ( entity->m_takedamage == DAMAGE_NO )
			continue;
		if ( entity->ClassMatches( "prop_physics" ) )
		{
			IBreakableWithPropData *prop = dynamic_cast< IBreakableWithPropData * >( entity );
			if ( prop && ( prop->GetExplosiveDamage() > 0.0f || prop->GetPhysicsDamageTable() != NULL_STRING ) )
				continue;
		}
		const Vector spot = entity->BodyTarget( source, false );
		float visible = TerrorGameRules()->GetAmountOfEntityVisible( source, entity );
		if ( visible <= 0.0f )
			continue;
		Vector direction = spot - source;
		const float range = direction.Length();
		if ( range > outerRadius )
			continue;
		if ( range > innerRadius )
			visible *= 1.0f - ( range - innerRadius ) / ( outerRadius - innerRadius );
		if ( visible <= 0.0f )
			continue;
		CTakeDamageInfo adjusted( info );
		adjusted.SetDamage( info.GetDamage() * visible );
		VectorNormalize( direction );

		if ( type == 2 )
		{
			if ( !TerrorGameRules()->IsPlayerControlledZombies() && static_cast< double >( direction.z ) < -0.7 )
				continue;
			CTerrorPlayer *boomer = ToTerrorPlayer( info.GetInflictor() );
			CTerrorPlayer *victim = ToTerrorPlayer( entity );
			if ( boomer && boomer->GetTeamNumber() == TEAM_INFECTED && boomer->GetZombieClass() == 2 &&
				victim && ( victim->GetTeamNumber() == TEAM_SURVIVOR || victim->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
			{
				TerrorNavArea *victimArea = ToTerrorNavArea( victim->GetLastKnownArea() );
				TerrorNavArea *sourceArea = ToTerrorNavArea(
					TheNavMesh->GetNearestNavArea( source, false, 10000.0f, false, true, TEAM_ANY ) );
				if ( victimArea && sourceArea &&
					victimArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) != sourceArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
				{
					CBaseEntity *nearest = gEntList.FindEntityByClassnameNearest(
						"prop_door_rotating_checkpoint", boomer->WorldSpaceCenter(), outerRadius * 2.0f );
					CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( nearest );
					// Native v952 is class identity; v956's separate
					// m_hasUnlockSequence predicate is NOT used here.
					if ( door && door->IsCheckpointDoor() && ( door->IsDoorClosed() || door->IsDoorClosing() ) )
						continue;
				}
			}
		}

		if ( adjusted.GetDamagePosition() != vec3_origin && adjusted.GetDamageForce() != vec3_origin )
		{
			const Vector &inputForce = adjusted.GetDamageForce();
			const float magnitude = sqrtf( ( inputForce.z * inputForce.z + inputForce.y * inputForce.y ) +
				inputForce.x * inputForce.x );
			Vector force = direction * magnitude;
			force *= visible;
			adjusted.SetDamageForce( force );
			adjusted.SetDamagePosition( source );
		}
		else
		{
			CalculateExplosiveDamageForce( &adjusted, direction, source, 1.5f );
		}
		if ( adjusted.GetDamageForce().z < 0.0f && entity->GetAbsVelocity().LengthSqr() < 1.0f )
		{
			Vector force = adjusted.GetDamageForce();
			force.z *= -0.5f;
			adjusted.SetDamageForce( force );
		}

		trace_t trace;
		UTIL_TraceLine( source, spot, 0x46004003u, NULL, COLLISION_GROUP_NONE, &trace );
		// The intervening S51F00 call is a proven zero-returning pinned
		// game-code helper, not an additional target/entity predicate.
		if ( entity->MyCombatCharacterPointer() && type == 2 )
		{
			CTerrorPlayer *boomer = ToTerrorPlayer( info.GetInflictor() );
			CTerrorPlayer *victim = ToTerrorPlayer( entity );
			if ( boomer )
			{
				if ( victim )
				{
					if ( victim->GetTeamNumber() == TEAM_SURVIVOR || victim->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
					{
						if ( !victim->IsIT() )
							boomer->RecordVomitExplosion( victim->GetSurvivorCharacter() );
						victim->OnVomitedUpon( boomer, true );
						boomer->SetObserverTarget( victim );
					}
					victim->StaggerPlayer( boomer, NULL );
				}
				else if ( INextBot *bot = dynamic_cast< INextBot * >( entity ) )
				{
					// S393EA0/S40C1B0 name this exact v112 event OnShoved.
					bot->OnShoved( boomer );
				}
			}
		}
		else if ( trace.fraction != 1.0f )
		{
			ClearMultiDamage();
			entity->DispatchTraceAttack( adjusted, direction, &trace );
			ApplyMultiDamage();
		}
		else
		{
			entity->TakeDamage( adjusted );
		}
		entity->TraceAttackToTriggers( adjusted, source, spot, direction );
	}

	if ( type == 2 )
	{
		CDisablePredictionFiltering filtering;
		DispatchParticleEffect( "boomer_explode", source, QAngle( 0.0f, 0.0f, 1.0f ) );
		trace_t trace;
		UTIL_TraceLine( source, source - Vector( 0.0f, 0.0f, 100.0f ), 0x0600400Bu,
			info.GetInflictor(), COLLISION_GROUP_NONE, &trace );
		UTIL_DecalTrace( &trace, "Blood" );
		if ( info.GetInflictor() )
			info.GetInflictor()->EmitSound( "BoomerZombie.Detonate" );
		UTIL_ScreenShake( source, 50.0f, 150.0f, 1.0f, 750.0f, SHAKE_START, false );
	}
}

// S:3290F0. This is the active-state cleanup owner, not a Spawn reset.
// It deliberately leaves timer durations, damage history and corpse state alone.
void CTerrorPlayer::CleanupPlayerState()
{
	m_staggerHistory.RemoveAll();
	m_pendingStaggerEventData = -1;
	m_vomitStart = 0.0f;
	m_vomitFadeStart = 0.0f;
	m_bashedStart = 0.0f;
	m_salivaStart = 0.0f;
	ClearUseEntity();
	SetPushEntity( NULL );
	Extinguish();
	ReleasePounce();
	ReleaseCarry( true, false, true );
	ReleasePummel( true, NULL );
	ReleaseJockey( NULL );
	if ( m_pummelTarget.Get() )
	{
		m_pummelTarget->SetMoveType( MOVETYPE_WALK );
		m_pummelTarget->SetParent( NULL, -1 );
	}
	ReleaseTongueOnDeath( true );
	if ( CTerrorPlayer *owner = ToTerrorPlayer( m_tongueOwner.Get() ) )
		owner->ReleaseTongueOnDeath( false );

	RemoveFlag( FL_FROZEN );
	m_itTimer.Invalidate();
	if ( m_bAdrenalineActive )
		ExpireAdrenaline();
	m_isGoingToDie = false;
	m_isIncapacitated = false;
	m_incapacitatedByAttacker = NULL;
	m_incapacitatedTimestamp.Invalidate();
	m_isCalm = false;
	m_calmWeaponTimer.Invalidate();
	m_stunTimer.Invalidate();
	m_airMovementRestricted = false;
	m_shoveForce = vec3_origin;
	m_survivorDistressResponseTimer.Invalidate();
	m_isHangingFromLedge = false;
	RemoveHangLighting();
	m_isFallingFromLedge = false;
	m_isHangingFromTongue = false;
	m_reachedTongueOwner = false;
	m_isProneTongueDrag = false;
	m_hangTimer.Invalidate();
	m_isReleasingLedge = false;
	m_isFirmlyAttached = false;
	m_jockeyRotatedDesire = false;
	m_downAndBeatenTimer.Invalidate();
	m_isBleedingOut = false;
	m_bNativeGhostClockFlag = false;

	if ( m_reviveTarget.Get() )
	{
		ToTerrorPlayer( m_reviveTarget.Get() )->DoTerrorAnimationEvent( TERROR_ANIMEVENT_REVIVE_TARGET_END );
		ToTerrorPlayer( m_reviveTarget.Get() )->StopBeingRevived( true );
	}
	EHANDLE world;
	world.Init( 0, 0 );
	if ( m_reviveTarget.Get() != world.Get() )
		m_reviveTarget.GetForModify().Init( 0, 0 );
	if ( m_reviveOwner.Get() )
		ToTerrorPlayer( m_reviveOwner.Get() )->EndReviving( true );
	if ( m_reviveOwner.Get() != world.Get() )
		m_reviveOwner.GetForModify().Init( 0, 0 );
	m_ledgeHangingEnabled = true;
	StopUseAction();
	EmitSound( "Player.StopVoice" );
	EmitSound( "Player.StopItem" );
	EmitSound( "Player.StopBody" );
	// S:30AD60, not plain StopSound: untracked names are not stopped at all.
	StopTrackedSound( "BoomerZombie.Gurgle" );
	StopTrackedSound( "Player.Fall" );
	StopTrackedSound( "Player.Heartbeat" );
}

// S:8523D0. Only OnTeamChanged's native bracket writes this flag; it is not
// the independent CommitSuicide / special-respawn-interval suppression flag.
bool g_bNotifyDirectorOnInfectedTeamLeave = false;

// S:329DB0, called by the old ACTIVE state's leave callback before publishing
// the new state. The immediate Event_Killed Director notification is separate.
void CTerrorPlayer::OnLeaveActive()
{
	CleanupPlayerState();
	if ( State_Get() == STATE_ACTIVE )
		UpdateLineOfScrimmage( -1.0f, -1.0f );
	const float choice = RandomFloat( 0.0f, 100.0f );
	m_nativeZombieVariantChoice = choice < 50.0f ? 3 : ( choice < 75.0f ? 1 : 2 );
	CBaseAbility *ability = static_cast< CBaseAbility * >( m_customAbility.Get() );
	if ( ability )
	{
		ability->RemoveAbility();
		m_customAbility = NULL;
	}
	if ( ( FloatBits( m_flPendingGhostSpawnDeadline ) & 0x7FFFFFFFu ) == 0 )
	{
		const double deadline = TheDirector->GetInfectedSpawnDelay( 0.0f ) +
			static_cast< double >( gpGlobals->curtime );
		m_flPendingGhostSpawnDeadline = static_cast< float >( deadline );
		m_flGhostSpawnDeadline = static_cast< float >( deadline );
	}
	if ( g_bNotifyDirectorOnInfectedTeamLeave && GetTeamNumber() == TEAM_INFECTED )
		TheDirector->OnInfectedKilled( this );
	CCSPlayer::OnLeaveActive();
}

// S:246D90.
void CCSPlayer::OnLeaveActive()
{
	RemoveAllItems( false );
	m_bIsDefusing = false;
	m_bHasNightVision = false;
	m_bNightVisionOn = false;
	m_bHasHelmet = false;
	FlashlightTurnOff( false );
	SetProgressBarTime( 0.0f );
}

// S:308690. Camera relocation observes the real mission-end timer's deadline,
// not its elapsed state and not the Director's finale enum.
void CTerrorPlayer::OnLeaveDeathAnim()
{
	CBaseEntity *camera = GetViewEntity();
	if ( camera && camera != this && FClassnameIs( camera, "point_deathfall_camera" ) &&
		TheDirector->GetMissionEndDeadline() > 0.0f )
	{
		Teleport( &camera->GetAbsOrigin(), &camera->GetAbsAngles(), NULL );
	}
}

// S:308750. Retain the camera pointer across the actual FOV/view-entity reset.
void CTerrorPlayer::OnLeaveDeathWaitForKey()
{
	CBaseEntity *camera = GetViewEntity();
	if ( !camera || camera == this || !FClassnameIs( camera, "point_deathfall_camera" ) )
		return;
	ClearZoomOwner();
	SetFOV( this, 0, 0.0f, 0 );
	SetViewEntity( NULL );
	if ( TheDirector->GetMissionEndDeadline() > 0.0f )
		Teleport( &camera->GetAbsOrigin(), &camera->GetAbsAngles(), NULL );
}

// S:3056A0. This is the sole native D0 writer. Keep the bracket around the
// real reporter, state callbacks, Music and Director notification; D1 is separate.
void CTerrorPlayer::OnTeamChanged( int oldTeam )
{
	g_bNotifyDirectorOnInfectedTeamLeave =
		oldTeam == TEAM_INFECTED && GetTeamNumber() != TEAM_INFECTED;
	GetPlayerReporter().OnPlayerTeamChanged( this );
	if ( oldTeam == TEAM_INFECTED && GetZombieClass() == 8 &&
		( !TerrorGameRules()->IsPlayerControlledZombies() || !TheDirector->IsFinale() ) )
	{
		NotifySpecialDeathMusic( 8 );
	}
	m_wantsToPlay = false;
	if ( IsSurvivorDeathTeam( this ) && TerrorGameRules()->FPlayerCanRespawn( this ) )
		Spawn();
	else
		State_Transition( STATE_OBSERVER_MODE );
	if ( TerrorGameRules()->IsPlayerControlledZombies() && GetTeamNumber() == TEAM_INFECTED )
	{
		// S:2AF510 is local state reset2AEB10 followed by the real Reset
		// command2AAD20. Ghost-entry's DSP-only2AAE30 is not this operation.
		Music *music = GetMusic();
		music->Reset();
		music->GeneralCommand( "Reset", NULL, NULL, -10 );
	}
	TheDirector->OnPlayerTeamChanged( this, oldTeam );
	g_bNotifyDirectorOnInfectedTeamLeave = false;
}

// S3082A0. These are the two canonical actual flags, not a synthetic silent
// kill argument. Their other real writers/consumers already exist in the
// native JoinTeam, Spawn, Event_Killed and Rules PopulateDeathEvent owners.
void CTerrorPlayer::KillSilently()
{
	TheDirector->OnInfectedKilled( this );
	m_suppressDeathEffects = true;
	m_deathEventAborted = true;
	SetHealth( 0 );
	CTakeDamageInfo info( this, this, 0.0f, DMG_NEVERGIB );
	Event_Killed( info );
	// Native virtual1236 is the real no-argument dying callback. The
	// existing base overload constructs its real info and dispatches the
	// player dying implementation; do not call CommitSuicide/Tank death.
	static_cast< CBaseCombatCharacter * >( this )->Event_Dying();
	State_Transition( STATE_OBSERVER_MODE );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x2983c0; 0x5c36d4 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace holiday_giftPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "holiday_gift", 0x5c36d4 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "holiday_gift", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
