//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/infected/common/infected.h"
#include "EntityFlame.h"
#include "props.h"
#include "recipientfilter.h"
#include "rumble_shared.h"
#include "world.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/weapons/weapon_chainsaw.h"
#include "usermessages.h"
#include "l4d2/sound/terror_music.h"
#include "EventLog.h"
#include "engine/IEngineSound.h"
#include "NextBot/NextBot.h"
#include "triggers.h"
#include "l4d2/entities/script_brushes.h"
#include "IEffects.h"
#include "particle_parse.h"
#include "death_pose.h"
#include "activitylist.h"
#include "tier1/utlmap.h"
#include "datacache/imdlcache.h"
#include "ipredictionsystem.h"
#include <xmmintrin.h>

#include "l4d2/player/terrorplayer_damage_console_server_cvars.inc"
#include "tier0/memdbgon.h"

// S:0x23FEC0 / S:0x23FED0. Both damage-history lists consume this same
// process-lifetime serial. There is no per-weapon or level-reset counter.
static uint32 g_nTerrorBulletSerial = 0;

void IncrementTerrorBulletSerial()
{
	++g_nTerrorBulletSerial;
}

int GetTerrorBulletSerial()
{
	return static_cast< int >( g_nTerrorBulletSerial );
}

// S:0x17FCD0/17E4B0/17E540. This scope is the current player's
// ItemPostFrame, not the server tick. The only original opt-in names are
// immutable literals, so the native map can borrow them without key copies.
static bool LessTerrorMessageName( const char *const &left, const char *const &right )
{
	if ( !left )
		return false;
	return !right || Q_stricmp( left, right ) < 0;
}

static CUtlMap< const char *, uint32, int > g_terrorMessageRecipients( 0, 0, LessTerrorMessageName );

void BeginTerrorWeaponMessageScope()
{
	g_terrorMessageRecipients.RemoveAll();
	g_terrorMessageRecipients.Insert( "Fade", 0u );
	g_terrorMessageRecipients.Insert( "Damage", 0u );
}

void EndTerrorWeaponMessageScope()
{
	g_terrorMessageRecipients.RemoveAll();
}

bool CBasePlayer::IsUserMessageSuppressed( const char *name )
{
	if ( !name )
		return false;
	const int index = g_terrorMessageRecipients.Find( name );
	if ( index == g_terrorMessageRecipients.InvalidIndex() )
		return false;
	const uint32 bit = 1u << ( ( entindex() - 1 ) & 31 );
	uint32 &recipients = g_terrorMessageRecipients[index];
	if ( recipients & bit )
		return true;
	recipients |= bit;
	return false;
}

// S1059D140: this is a ConVar in L4D2, not the SDK's FL_GODMODE command.
ConVar god( "god", "0", 0x00004104, "Survivors don't take damage" );

static ConVar survivor_friendly_fire_factor_easy( "survivor_friendly_fire_factor_easy", "0", 0x00004004 );
static ConVar survivor_friendly_fire_factor_normal( "survivor_friendly_fire_factor_normal", "0.1", 0x00004004 );
static ConVar survivor_friendly_fire_factor_hard( "survivor_friendly_fire_factor_hard", "0.3", 0x00004004 );
static ConVar survivor_friendly_fire_factor_expert( "survivor_friendly_fire_factor_expert", "0.5", 0x00004004 );
static ConVar survivor_burn_factor_easy( "survivor_burn_factor_easy", "0.2", 0x00004004, "Flame damage multiplier" );
static ConVar survivor_burn_factor_normal( "survivor_burn_factor_normal", "0.2", 0x00004004, "Flame damage multiplier" );
static ConVar survivor_burn_factor_hard( "survivor_burn_factor_hard", "0.4", 0x00004004, "Flame damage multiplier" );
static ConVar survivor_burn_factor_expert( "survivor_burn_factor_expert", "1", 0x00004004, "Flame damage multiplier" );
static ConVar grenadelauncher_ff_scale( "grenadelauncher_ff_scale", "0.12", 0x00004004, "ff scale for grenade launcher damage to other players" );
static ConVar grenadelauncher_ff_scale_self( "grenadelauncher_ff_scale_self", "0.12", 0x00004004, "ff scale for grenade launcher damage to other players" );
static ConVar sb_friendlyfire( "sb_friendlyfire", "0", 0x00004004 );
static ConVar z_friendly_fire_forgiveness( "z_friendly_fire_forgiveness", "1", 0x00004004, "Ignore friendly fire that appears to be unintentional." );
static ConVar z_throttle_hit_interval_easy( "z_throttle_hit_interval_easy", "0.5", 0x00004004, "Minimum time between damaging a Survivor from a mob" );
static ConVar z_throttle_hit_interval_normal( "z_throttle_hit_interval_normal", "0.33", 0x00004004, "Minimum time between damaging a Survivor from a mob" );
static ConVar z_throttle_hit_interval_hard( "z_throttle_hit_interval_hard", "0.5", 0x00004004, "Minimum time between damaging a Survivor from a mob" );
static ConVar z_throttle_hit_interval_expert( "z_throttle_hit_interval_expert", "1", 0x00004004, "Minimum time between damaging a Survivor from a mob" );
static ConVar common_dps_window( "common_dps_window", "2.0f", 0x00000006 );
static ConVar common_dps_spew( "common_dps_spew", "0", 0x00000006 );
static ConVar z_hit_from_behind_factor( "z_hit_from_behind_factor", "0.5", 0x00004004, "Damage multiplier if zombie hits Survivor from behind" );
static ConVar z_hit_from_behind_cosine( "z_hit_from_behind_cosine", "0.0", 0x00004004 );
static ConVar z_hit_chainsawer_factor( "z_hit_chainsawer_factor", "0.5", 0x00004004, "Damage multiplier if zombie hits someone using a chainsaw" );
static ConVar z_hit_incap_factor_easy( "z_hit_incap_factor_easy", "0.1", 0x00004004, "Damage multiplier if zombie hits incapacitated Survivor" );
static ConVar z_hit_incap_factor_normal( "z_hit_incap_factor_normal", "1.0", 0x00004004, "Damage multiplier if zombie hits incapacitated Survivor" );
static ConVar z_hit_incap_factor_hard( "z_hit_incap_factor_hard", "1.0", 0x00004004, "Damage multiplier if zombie hits incapacitated Survivor" );
static ConVar z_hit_incap_factor_expert( "z_hit_incap_factor_expert", "1.0", 0x00004004, "Damage multiplier if zombie hits incapacitated Survivor" );
static ConVar survivor_damage_speed_factor( "survivor_damage_speed_factor", "0.25f", 0x00004004, "How much a Survivor is slowed when hit by an Infected" );
ConVar survivor_incap_max_fall_damage( "survivor_incap_max_fall_damage", "200", 0x00000006, "Taking falling damage greater than this will kill survivors outright instead of incapacitating them" );
ConVar z_witch_always_kills( "z_witch_always_kills", "0", 0x00000006 );
static ConVar survivor_calm_damage_delay( "survivor_calm_damage_delay", "5", 0x00004004, "survivors are not calm for this many seconds after taking damage" );
static ConVar z_claw_hit_pitch_min( "z_claw_hit_pitch_min", "-20", 0x00004004 );
static ConVar z_claw_hit_pitch_max( "z_claw_hit_pitch_max", "20", 0x00004004 );
static ConVar z_claw_hit_yaw_min( "z_claw_hit_yaw_min", "-20", 0x00004004 );
static ConVar z_claw_hit_yaw_max( "z_claw_hit_yaw_max", "20", 0x00004004 );

namespace
{
bool IsSurvivorDamageTeam( const CBaseEntity *entity )
{
	return entity && ( entity->GetTeamNumber() == TEAM_SURVIVOR || entity->GetTeamNumber() == 4 );
}

float DifficultyValue( int difficulty, const ConVar &easy, const ConVar &normal, const ConVar &hard, const ConVar &expert, float other )
{
	switch ( difficulty )
	{
	case 0: return easy.GetFloat();
	case 1: return normal.GetFloat();
	case 2: return hard.GetFloat();
	case 3: return expert.GetFloat();
	default: return other;
	}
}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S:0x301500. Inferno calls this before TakeDamage; traced attacks call it
// from ModifyDamageForHitgroup. It is not applied again in OnTakeDamage.
float CTerrorPlayer::GetFriendlyFireDamageMultiplier( const CTakeDamageInfo &info )
{
	const unsigned int type = static_cast< unsigned int >( info.GetDamageType() );
	const int difficulty = TerrorGameRules()->GetDifficultyLevel();
	if ( ( type & 8u ) && !( type & 2u ) )
		return DifficultyValue( difficulty, survivor_burn_factor_easy, survivor_burn_factor_normal,
			survivor_burn_factor_hard, survivor_burn_factor_expert, 1.0f );

	CBaseEntity *attacker = info.GetAttacker();
	if ( !attacker || !IsSurvivorDamageTeam( this ) ||
		( !IsSurvivorDamageTeam( attacker ) && attacker->GetTeamNumber() != TEAM_SPECTATOR ) )
		return 1.0f;
	if ( attacker == this && !( type & 0x00800000u ) && info.GetDamageCustom() != 1 )
		return 1.0f;
	if ( !attacker->IsNetClient() && !sb_friendlyfire.GetBool() )
		return 0.0f;

	if ( z_friendly_fire_forgiveness.GetBool() && attacker == info.GetInflictor() )
	{
		Vector forward;
		CBasePlayer *playerAttacker = ToBasePlayer( attacker );
		if ( playerAttacker )
			playerAttacker->EyeVectors( &forward );
		else
			attacker->GetVectors( &forward, NULL, NULL );
		const Vector eye = attacker->EyePosition();
		const Vector center = WorldSpaceCenter();
		Vector closest;
		CalcClosestPointOnLine( center, eye, eye + forward, closest );
		if ( closest.z > GetAbsOrigin().z + CollisionProp()->OBBMaxs().z ||
			closest.z < GetAbsOrigin().z + CollisionProp()->OBBMins().z )
			return 0.0f;
		// S301777..30179D retains x87 precision through both squares.
		const double radius = ( static_cast< double >( CollisionProp()->OBBMaxs().x ) -
			CollisionProp()->OBBMins().x ) * 0.5;
		const double dx = static_cast< double >( center.x ) - closest.x;
		const double dy = static_cast< double >( center.y ) - closest.y;
		if ( dx * dx + dy * dy > radius * radius )
			return 0.0f;
	}

	float multiplier = 1.0f;
	if ( difficulty != 3 && ( !( type & 0x40u ) || ( type & 0x00800000u ) ) )
		multiplier = 0.35f;
	const int factorDifficulty = ( type & 0x00200000u ) && difficulty >= 1 && difficulty <= 3 ? 1 : difficulty;
	multiplier = DifficultyValue( factorDifficulty, survivor_friendly_fire_factor_easy, survivor_friendly_fire_factor_normal,
		survivor_friendly_fire_factor_hard, survivor_friendly_fire_factor_expert, 1.0f ) * multiplier;

	if ( info.GetDamageCustom() == 1 )
	{
		float factor;
		if ( attacker == this )
			factor = TerrorGameRules()->IsPlayerControlledZombies() ? 0.65f : grenadelauncher_ff_scale_self.GetFloat();
		else
			factor = grenadelauncher_ff_scale.GetFloat();
		multiplier = factor * multiplier;
	}
	if ( type & 0x04000000u )
		return multiplier * 0.1f;
	return multiplier;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:0x3117D0 calls the infected-family getter at native v328 (also Witch).
// Players and special infected return null; record pre-leaf-scaling damage.
bool CTerrorPlayer::CheckDamageThrottle( const CTakeDamageInfo &info, bool record )
{
	CBaseEntity *attacker = info.GetAttacker();
	if ( attacker && attacker->MyInfectedPointer() )
	{
		float interval = DifficultyValue( TerrorGameRules()->GetDifficultyLevel(), z_throttle_hit_interval_easy,
			z_throttle_hit_interval_normal, z_throttle_hit_interval_hard, z_throttle_hit_interval_expert, 1.0f );
		if ( m_commonDamageTimer.GetStartTime() > 0.0f && interval > gpGlobals->curtime - m_commonDamageTimer.GetStartTime() )
			return false;
		if ( record )
		{
			m_commonDamageTimer.Start();
			const float window = common_dps_window.GetFloat();
			for ( int i = m_commonDamageHistory.Count() - 1; i >= 0; --i )
			{
				if ( gpGlobals->curtime - window > m_commonDamageHistory[i].time )
					m_commonDamageHistory.Remove( i );
			}
			CommonDamageSample_t sample;
			sample.damage = info.GetDamage();
			sample.time = gpGlobals->curtime;
			m_commonDamageHistory.AddToTail( sample );
		}
	}
	return true;
}

// S:0x30AF40; called by the original scheduled PostThink, not by acid hits.
void CTerrorPlayer::UpdateCommonDamageDPS()
{
	const float window = common_dps_window.GetFloat();
	if ( window <= 0.0f )
	{
		m_commonDamageDPS = 0.0f;
		return;
	}
	float damage = 0.0f;
	for ( int i = m_commonDamageHistory.Count() - 1; i >= 0; --i )
	{
		if ( gpGlobals->curtime - window <= m_commonDamageHistory[i].time )
			damage += m_commonDamageHistory[i].damage;
		else
			m_commonDamageHistory.Remove( i );
	}
	m_commonDamageDPS = damage / window;
	if ( common_dps_spew.GetInt() == entindex() )
		DevMsg( "common dps  = %.2f\n", m_commonDamageDPS );
}

// S:0x242980, used by the original CS damage stage, not the generic SDK armor.
bool CTerrorPlayer::IsArmoredHitgroup( int hitgroup ) const
{
	if ( ArmorValue() <= 0 )
		return false;
	switch ( hitgroup )
	{
	case HITGROUP_GENERIC:
	case HITGROUP_CHEST:
	case HITGROUP_STOMACH:
	case HITGROUP_LEFTARM:
	case HITGROUP_RIGHTARM:
		return true;
	case HITGROUP_HEAD:
		return m_bHasHelmet;
	default:
		return false;
	}
}

// S:0x245080. View punch is a direct setter in the original, not ViewPunch's
// velocity impulse. The trace's hitgroup, hitbox, type and native handles survive.
void CTerrorPlayer::TraceAttack( const CTakeDamageInfo &inputInfo, const Vector &direction, trace_t *trace, CDmgAccumulator *accumulator )
{
	if ( m_takedamage != DAMAGE_YES )
		return;
	SetLastHitGroup( trace->hitgroup );
	m_nForceBone = trace->physicsbone;
	const unsigned int type = static_cast< unsigned int >( inputInfo.GetDamageType() );
	const float rawDamage = inputInfo.GetDamage();
	float damage = rawDamage;
	bool headshot = false;
	bool protectedHead = false;
	bool blood = true;
	bool bodyPunch = false;
	if ( type & 0x40u )
	{
		bodyPunch = ArmorValue() <= 0;
		blood = bodyPunch;
	}
	else
	{
		switch ( trace->hitgroup )
		{
		case HITGROUP_HEAD:
			if ( type & 0x20000000u )
			{
				damage *= 1.25f;
				bodyPunch = true;
			}
			else
			{
				protectedHead = m_bHasHelmet;
				damage *= 4.0f;
				if ( !protectedHead )
				{
					QAngle punch = GetPunchAngle();
					punch.x = MAX( -12.0f, damage * -0.5f );
					punch.z = clamp( random->RandomFloat( -1.0f, 1.0f ) * damage, -9.0f, 9.0f );
					SetPunchAngle( punch );
				}
				headshot = true;
			}
			break;
		case HITGROUP_CHEST:
			bodyPunch = ArmorValue() <= 0;
			break;
		case HITGROUP_STOMACH:
			damage *= 1.25f;
			bodyPunch = ArmorValue() <= 0;
			break;
		case HITGROUP_LEFTLEG:
		case HITGROUP_RIGHTLEG:
			damage *= 0.75f;
			break;
		default:
			break;
		}
	}
	if ( bodyPunch )
	{
		QAngle punch = GetPunchAngle();
		punch.x = MAX( -4.0f, damage * -0.1f );
		SetPunchAngle( punch );
	}

	damage = ModifyDamageForHitgroup( inputInfo, rawDamage, damage, headshot );
	CDisablePredictionFiltering disablePredictionFiltering;
	if ( blood && damage > 0.0f )
		SpawnTraceBlood( damage, inputInfo, direction, trace );
	if ( trace->hitgroup == HITGROUP_HEAD && protectedHead )
		g_pEffects->Sparks( trace->endpos, 1, 1, &trace->plane.normal );

	CTakeDamageInfo info = inputInfo;
	info.SetDamage( damage );
	if ( headshot )
		info.AddDamageType( static_cast< int >( 0x40000000u ) );
	if ( accumulator )
		accumulator->AccumulateMultiDamage( info, this );
	else
		AddMultiDamage( info, this );
}

// S:0x249370 / S:0x2494A0. Keep the original 32-byte, case-insensitive
// name key and the one shared shot serial. Damage is accumulated on every
// call, but a shotgun's pellets count as one hit for this serial.
static void AddDamageRecord( CUtlLinkedList< TerrorDamageRecord_t *, int > &records, const char *name, int damage )
{
	FOR_EACH_LL( records, i )
	{
		TerrorDamageRecord_t *record = records[i];
		if ( !Q_strnicmp( name, record->name, sizeof( record->name ) ) )
		{
			record = records[i];
			const uint32 serial = static_cast< uint32 >( GetTerrorBulletSerial() );
			record->damage = static_cast< int >( static_cast< uint32 >( record->damage ) + static_cast< uint32 >( damage ) );
			if ( record->bulletSerial != serial )
				record->hits = static_cast< int >( static_cast< uint32 >( record->hits ) + 1u );
			record->bulletSerial = serial;
			return;
		}
	}
	TerrorDamageRecord_t *record = new TerrorDamageRecord_t;
	const uint32 serial = static_cast< uint32 >( GetTerrorBulletSerial() );
	Q_strncpy( record->name, name, sizeof( record->name ) );
	record->damage = damage;
	record->hits = 1;
	record->bulletSerial = serial;
	records.AddToTail( record );
}

void CCSPlayer::RecordDamageGiven( const char *name, int damage )
{
	AddDamageRecord( m_damageGiven, name, damage );
}

void CCSPlayer::RecordDamageTaken( const char *name, int damage )
{
	AddDamageRecord( m_damageTaken, name, damage );
}

// S:0x3119D0. This override updates the checkpoint, not the mission total.
void CTerrorPlayer::RecordDamageTaken( const char *name, int damage )
{
	if ( !IsIncapacitated() )
		m_checkpointDamageTaken += damage;
	BaseClass::RecordDamageTaken( name, damage );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S:0x307050. Pin redirection is to the actual attacker entity, not a
// second damage application to the pinned survivor.
float CTerrorPlayer::ModifyDamageForHitgroup( const CTakeDamageInfo &info, float rawDamage, float damage, bool headshot )
{
	CBaseEntity *attacker = info.GetAttacker();
	CTerrorPlayer *playerAttacker = ToTerrorPlayer( attacker );
	CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( info.GetInflictor() );
	bool rifleHeadshot = false;
	if ( playerAttacker )
	{
		if ( weapon && !playerAttacker->GetUseEntity() )
		{
			const TerrorWeaponType_t type = weapon->GetCSWpnData().m_WeaponType;
			rifleHeadshot = type == TERROR_WEAPONTYPE_SNIPER_RIFLE || type == TERROR_WEAPONTYPE_RIFLE;
		}
		if ( GetTeamNumber() == playerAttacker->GetTeamNumber() )
			damage = rawDamage;
	}
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 && playerAttacker )
	{
		damage = rawDamage;
		CWeaponCSBase *active = dynamic_cast< CWeaponCSBase * >( playerAttacker->GetActiveWeapon() );
		if ( active && active->GetCSWpnData().m_WeaponType == TERROR_WEAPONTYPE_SHOTGUN &&
			active->GetCSWpnData().m_iTier == 2 && ( info.GetDamageType() & 0x20000000u ) )
		{
			static ConVarRef scale( "z_tank_autoshotgun_dmg_scale" );
			damage = scale.GetFloat() * rawDamage;
		}
	}
	if ( !MyNextBotPointer() && rifleHeadshot && headshot &&
		GetTeamNumber() == TEAM_INFECTED && ( m_zombieClass == 3 || m_zombieClass == 1 ) &&
		static_cast< float >( GetHealth() ) > damage )
	{
		damage = static_cast< float >( GetHealth() );
	}
	if ( m_jockeyAttacker.Get() && IsSurvivorDamageTeam( attacker ) )
	{
		if ( LastHitGroup() == HITGROUP_CHEST || LastHitGroup() == HITGROUP_HEAD ||
			( LastHitGroup() == HITGROUP_GENERIC && ( info.GetDamageType() & 0x04000000u ) ) )
		{
			m_jockeyAttacker->TakeDamage( info );
		}
		return 0.0f;
	}
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 5 && m_jockeyVictim.Get() &&
		playerAttacker && playerAttacker->GetTeamNumber() == GetTeamNumber() &&
		( info.GetDamageType() & 0x84u ) )
	{
		return 0.0f;
	}
	if ( m_pummelAttacker.Get() )
	{
		if ( attacker != m_pummelAttacker.Get() && !( info.GetDamageType() & 8u ) )
			m_pummelAttacker->TakeDamage( info );
		return 0.0f;
	}
	CTakeDamageInfo adjusted = info;
	adjusted.SetDamage( damage );
	damage = GetFriendlyFireDamageMultiplier( adjusted ) * damage;
	if ( info.GetDamageCustom() == 1 && GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 )
	{
		static ConVarRef scale( "z_tank_grenade_launcher_dmg_scale" );
		damage = scale.GetFloat() * damage;
	}
	if ( damage > 0.0f && damage <= 1.0f )
		return 1.0f;
	return damage;
}

// S:0x332C20, before the lower health mutation. CS has already staged the
// integer health damage; the gate must not use the unrounded trace damage.
bool CTerrorPlayer::ShouldApplySurvivorDamage( const CTakeDamageInfo &info )
{
	if ( !IsAlive() )
		return false;
	const unsigned int type = static_cast< unsigned int >( info.GetDamageType() );
	if ( IsPlayingDeathAnimation() && type != 0x00020000u )
		return false;
	if ( IsIncapacitated() && IsSurvivorDamageTeam( this ) && ( type & 0x40u ) )
	{
		CTerrorPlayer *inflictor = ToTerrorPlayer( info.GetInflictor() );
		if ( inflictor && inflictor->m_zombieClass == 2 )
			return false;
	}
	float health = static_cast< float >( GetHealth() );
	if ( m_isGoingToDie && !IsIncapacitated() )
		health -= 1.0f;
	if ( ( m_iDamageHealth <= 0 || static_cast< float >( static_cast< int >( GetHealthBuffer() ) ) +
		health - static_cast< float >( m_iDamageHealth ) > 0.0f ) && !( type & 0x8000u ) )
	{
		return true;
	}
	bool canIncap = true;
	CBaseEntity *attacker = info.GetAttacker();
	CBaseCombatCharacter *combatAttacker = attacker ? attacker->MyCombatCharacterPointer() : NULL;
	if ( ( TerrorGameRules()->GetDifficultyLevel() == 3 ||
		( z_witch_always_kills.GetBool() && TerrorGameRules()->GetDifficultyLevel() != 0 ) ) &&
		combatAttacker && combatAttacker->GetZombieClass() == 7 )
	{
		canIncap = false;
	}
	if ( m_carryAttacker.Get() && ( type & 0x8000u ) )
		static_cast< CTerrorPlayer * >( m_carryAttacker.Get() )->RecordDamageToSurvivor( static_cast< int >( 100.0f - info.GetDamage() ), false, GetSurvivorCharacter() );
	if ( !canIncap )
		return true;
	if ( !IsSurvivorDamageTeam( this ) )
	{
		if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 && !IsIncapacitated() )
		{
			OnTankIncapacitated( info );
			return false;
		}
		return true;
	}
	if ( IsIncapacitated() || m_isFallingFromLedge )
		return true;
	const int totalHealth = static_cast< int >( GetHealthBuffer() + health );
	CTerrorPlayer *playerAttacker = ToTerrorPlayer( attacker );
	if ( m_currentReviveCount >= TerrorGameRules()->GetSurvivorMaxIncapacitatedCount() )
	{
		if ( !BeginDeathAnimation( info ) )
		{
			if ( GetHealth() - m_iDamageHealth + static_cast< int >( GetHealthBuffer() ) > 0 )
				SetHealth( 0 );
			return true;
		}
	}
	else
	{
		if ( m_iDamageHealth >= survivor_incap_max_fall_damage.GetInt() && ( type & 0x20u ) )
			return true;
		if ( playerAttacker )
			playerAttacker->RecordDamageGiven( GetPlayerName(), totalHealth );
		RecordDamageTaken( playerAttacker ? playerAttacker->GetPlayerName() : "world", totalHealth );
		OnIncapacitated( info );
		return false;
	}
	if ( playerAttacker )
		playerAttacker->RecordDamageGiven( GetPlayerName(), totalHealth );
	RecordDamageTaken( playerAttacker ? playerAttacker->GetPlayerName() : "world", totalHealth );
	return false;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:0x24A020. Native CS staging/armor precedes the Terror lethal gate.
int CCSPlayer::OnTakeDamage( const CTakeDamageInfo &input )
{
	CTakeDamageInfo info = input;
	CBaseEntity *attacker = info.GetAttacker();
	CBaseEntity *inflictor = info.GetInflictor();
	if ( !inflictor || GetMoveType() == MOVETYPE_NOCLIP || GetMoveType() == MOVETYPE_OBSERVER )
		return 0;
	CTerrorPlayer *victim = ToTerrorPlayer( this );
	bool friendly = false;
	bool botAttacker = false;
	if ( inflictor->GetTeamNumber() == GetTeamNumber() && inflictor != this && attacker != this )
	{
		friendly = true;
		CTerrorPlayer *player = ToTerrorPlayer( inflictor );
		if ( !player )
			player = ToTerrorPlayer( attacker );
		if ( player )
		{
			botAttacker = player->IsBot();
			player->m_iDisplayHistoryBits |= 0x200;
			if ( !botAttacker && gpGlobals->curtime > player->m_lastFriendlyAttackAnnouncement + 0.6f )
			{
				player->m_lastFriendlyAttackAnnouncement = gpGlobals->curtime;
				Msg( "%s attacked %s\n", player->GetPlayerName(), GetPlayerName() );
			}
		}
	}
	victim->OnSurvivorDamageResponse( info );
	CTerrorPlayer *playerAttacker = ToTerrorPlayer( attacker );
	float ratio = 0.5f;
	if ( playerAttacker )
	{
		CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( playerAttacker->GetActiveWeapon() );
		if ( weapon )
			ratio = weapon->GetCSWpnData().m_flArmorRatio * 0.5f;
	}
	float damage = info.GetDamage();
	static ConVarRef cheats( "sv_cheats" );
	if ( ( m_debugOverlays & OVERLAY_BUDDHA_MODE ) && cheats.GetBool() && damage > GetHealth() - 1 )
		damage = static_cast< float >( GetHealth() - 1 );
	m_lastDamageAmount = static_cast< int >( damage );
	if ( ArmorValue() != 0 && !( info.GetDamageType() & 0x4020u ) && victim->IsArmoredHitgroup( LastHitGroup() ) )
	{
		const float armorDamage = ( damage - damage * ratio ) * 0.5f;
		m_iDamageArmor = static_cast< int >( armorDamage );
		if ( armorDamage <= ArmorValue() )
		{
			damage = damage * ratio;
			SetArmorValue( static_cast< int >( static_cast< float >( ArmorValue() ) -
				( armorDamage < 0.0f ? 1.0f : armorDamage ) ) );
		}
		else
		{
			damage -= static_cast< float >( ArmorValue() ) * 2.0f;
			SetArmorValue( 0 );
		}
		if ( ArmorValue() <= 0 )
			m_bHasHelmet = false;
		info.SetDamage( damage );
	}
	else
	{
		m_iDamageArmor = 0;
	}
	m_iDamageHealth = static_cast< int >( damage );
	if ( !victim->ShouldApplySurvivorDamage( info ) )
		return 0;
	info.SetDamage( static_cast< float >( m_iDamageHealth ) );
	m_lastDamageTime = gpGlobals->curtime;
	m_lastDamageInfo = info;
	if ( info.GetDamage() <= 0.0f && ( !friendly || botAttacker ) )
		return 0;
	if ( info.GetDamageType() != 0x20000 && !IsUserMessageSuppressed( "Damage" ) )
	{
		CBaseEntity *inflictor = info.GetInflictor();
		if ( !inflictor )
			inflictor = this;
		CSingleUserRecipientFilter filter( this );
		filter.MakeReliable();
		UserMessageBegin( filter, "Damage" );
		WRITE_UBITLONG( static_cast< int >( clamp( info.GetDamage(), 0.0f, 100.0f ) ), 7 );
		WRITE_UBITLONG( inflictor->entindex(), 11 );
		MessageEnd();
	}
	if ( info.GetDamageType() & 0x40u )
		OnDamagedByExplosion( info );
	const int before = static_cast< int >( victim->GetHealthBuffer() + GetHealth() );
	const bool incapacitated = victim->IsIncapacitated();
	m_vecTotalBulletForce += info.GetDamageForce();
	const int result = CBaseCombatCharacter::OnTakeDamage( info );
	if ( result && !incapacitated )
	{
		const int actualDamage = before - static_cast< int >( victim->GetHealthBuffer() + GetHealth() );
		if ( playerAttacker )
		{
			if ( !botAttacker )
			{
				playerAttacker->RecordDamageGiven( GetPlayerName(), actualDamage );
				victim->RecordDamageTaken( playerAttacker->GetPlayerName(), actualDamage );
			}
		}
		else
		{
			CBaseCombatCharacter *combat = attacker ? attacker->MyCombatCharacterPointer() : NULL;
			const char *name = combat && combat->MyNextBotPointer() ? GetZombieClassName( combat->GetZombieClass() ) : "world";
			victim->RecordDamageTaken( name, actualDamage );
		}
	}
	return result;
}

// S:0x329F10. This owns the one lower health application and the original
// late hurt callback; neither the CS stage nor life transitions apply it again.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
int CTerrorPlayer::OnTakeDamage_Alive( const CTakeDamageInfo &input )
{
	CBaseEntity *attacker = input.GetAttacker();
	const int attackerTeam = attacker ? attacker->GetTeamNumber() : 0;
	if ( attacker && attacker != GetWorldEntity() &&
		( ( IsSurvivorDamageTeam( this ) && attackerTeam == TEAM_INFECTED ) ||
		  ( GetTeamNumber() == TEAM_INFECTED && IsSurvivorDamageTeam( attacker ) ) ) )
		m_terrorInjuryInterval.Start();
	if ( IsSurvivorDamageTeam( this ) && attacker && !CheckDamageThrottle( input, true ) )
		return 0;
	CTakeDamageInfo info = input;
	CBaseEntity *inflictor = info.GetInflictor();
	const float originalDamage = info.GetDamage();
	const unsigned int type = static_cast< unsigned int >( info.GetDamageType() );
	if ( type & 0x200000C6u )
	{
		float scale = 1.0f;
		CBaseCombatCharacter *combat = attacker ? attacker->MyCombatCharacterPointer() : NULL;
		if ( combat && combat->GetTeamNumber() == TEAM_INFECTED && combat->GetZombieClass() == 0 && IsSurvivorDamageTeam( this ) )
		{
			if ( IsIncapacitated() )
				scale = DifficultyValue( TerrorGameRules()->GetDifficultyLevel(), z_hit_incap_factor_easy, z_hit_incap_factor_normal,
					z_hit_incap_factor_hard, z_hit_incap_factor_expert, scale );
			else
			{
				Vector forward;
				EyeVectors( &forward );
				forward.z = 0.0f;
				VectorNormalize( forward );
				Vector toAttacker = attacker->GetAbsOrigin() - GetAbsOrigin();
				toAttacker.z = 0.0f;
				VectorNormalize( toAttacker );
				if ( DotProduct( toAttacker, forward ) < z_hit_from_behind_cosine.GetFloat() )
					scale = z_hit_from_behind_factor.GetFloat();
				CChainsaw *chainsaw = dynamic_cast< CChainsaw * >( GetActiveWeapon() );
				if ( chainsaw && chainsaw->m_iState == CChainsaw::ACTIVE )
					scale = z_hit_chainsawer_factor.GetFloat() * scale;
			}
		}
		info.SetDamage( scale * originalDamage );
	}
	if ( attackerTeam != TEAM_INFECTED )
	{
		if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 5 && !m_jockeyVictim.Get() && !GetGroundEntity() && ( type & 0x04000084u ) )
			info.SetDamage( static_cast< float >( GetHealth() ) );
		if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 3 && m_isAttemptingToPounce && ( type & 0x04000084u ) )
			info.SetDamage( static_cast< float >( GetHealth() ) );
	}
	if ( type == 1u && inflictor && inflictor->GetMoveType() == MOVETYPE_VPHYSICS )
	{
		if ( ( m_physicsDamageImmunityTimer.m_timestamp.Get() > 0.0f &&
			gpGlobals->curtime < m_physicsDamageImmunityTimer.m_timestamp.Get() ) ||
			( IsIncapacitated() && info.GetDamage() < 50.0f ) )
			return 0;
		m_physicsDamageImmunityTimer.Start( 2.0f );
	}
	const bool dominated = IsDominated();
	if ( type & 0x400u )
	{
		if ( IsIncapacitated() || dominated )
			info.SetDamage( 1.0f );
		else if ( IsBot() )
			info.ScaleDamage( 0.5f );
		else
		{
			float maximum;
			switch ( TerrorGameRules()->GetDifficultyLevel() )
			{
			case 0: maximum = 2.0f; break;
			case 1: maximum = 6.0f; break;
			case 2: maximum = 10.0f; break;
			case 3: maximum = 20.0f; break;
			default: maximum = -1.0f; break;
			}
			// Pinned PC VEngineServer022 slot126 is engine1319E0:
			// 32 C0 C3 (xor al,al;ret). Its actual path uses .5/.35.
			if ( maximum > 0.0f && m_commonDamageDPS > maximum * 0.5f )
			{
				const float scale = RemapValClamped( m_commonDamageDPS / maximum, 0.5f, 1.0f, 1.0f, 0.35f );
				info.ScaleDamage( scale );
				if ( info.GetDamage() < 1.0f )
					info.SetDamage( 1.0f );
			}
		}
	}
	m_bitsDamageType |= info.GetDamageType();
	float excess = info.GetDamage() - static_cast< float >( GetHealth() );
	if ( !( excess > 0.0f ) )
		excess = 0.0f;
	if ( m_isGoingToDie && !IsIncapacitated() )
		excess += 1.0f;
	if ( m_zombieClass >= 1 && m_zombieClass <= 6 && ( type & 8u ) && ( type & 0x10000000u ) &&
		!TerrorGameRules()->IsPlayerControlledZombies() )
	{
		static ConVarRef burnScale( "z_special_burn_dmg_scale" );
		info.ScaleDamage( burnScale.GetFloat() );
	}
	if ( !CBaseCombatCharacter::OnTakeDamage_Alive( info ) && originalDamage > 0.0f )
		return 0;
	LogTerrorDamage( info, 0 );
	const float buffer = GetHealthBuffer() - excess;
	if ( GetHealth() < 1 && buffer > 0.0f )
	{
		m_isGoingToDie = true;
		SetHealth( 1 );
		SetHealthBuffer( buffer );
	}
	DelayCalm( survivor_calm_damage_delay.GetFloat() );
	CTerrorGun *gun = dynamic_cast< CTerrorGun * >( GetActiveWeapon() );
	if ( gun && GetFOV() != GetDefaultFOV() )
		gun->ToggleZoom();
	OnAbilityOwnerDamaged( info );
	if ( IsSurvivorDamageTeam( this ) && attacker && attackerTeam == TEAM_INFECTED )
	{
		if ( type & 0x80u )
			m_jumpSupressedUntil = gpGlobals->curtime + 1.0f;
		else if ( type & 0x400u )
			OnEnteredSpit();
	}
	if ( IsIncapacitated() && IsSurvivorDamageTeam( this ) && m_reviveOwner.Get() && !m_isHangingFromLedge )
	{
		CTerrorPlayer *reviver = ToTerrorPlayer( m_reviveOwner.Get() );
		if ( reviver && !reviver->IsBot() && !( type & 0x40000u ) && type != 0x20000u &&
			!( type & 8u ) && !( type & 0x4000u ) && attacker && !IsSurvivorDamageTeam( attacker ) )
			StopBeingRevived( true );
	}
	if ( ( TerrorGameRules()->IsScavengeMode() || TerrorGameRules()->IsVersusMode() ) &&
		IsPerformingUseAction( 8 ) && ( ( attacker && !IsSurvivorDamageTeam( attacker ) ) || ( type & 0x408u ) ) )
	{
		ResetUseActionProgress();
		CBasePlayer *playerAttacker = ToBasePlayer( attacker );
		if ( playerAttacker )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "gascan_pour_interrupted" );
			if ( event )
			{
				event->SetInt( "userid", playerAttacker->GetUserID() );
				gameeventmanager->FireEvent( event );
			}
		}
	}
	if ( IsSurvivorDamageTeam( this ) )
		OnSurvivorPain( info );
	else
		OnInfectedPain( info );
	if ( info.GetDamage() > 0.0f || !IsDominated() )
		OnTerrorDamageAlive( info );
	if ( IsSurvivorDamageTeam( this ) && !IsIncapacitated() && attacker && attackerTeam == TEAM_INFECTED &&
		input.GetDamageType() == 128 )
	{
		QAngle punch( random->RandomFloat( z_claw_hit_pitch_min.GetFloat(), z_claw_hit_pitch_max.GetFloat() ),
			random->RandomFloat( z_claw_hit_yaw_min.GetFloat(), z_claw_hit_yaw_max.GetFloat() ), 0 );
		SetPunchAngle( punch );
	}
	if ( ( IsSurvivorDamageTeam( this ) && attacker && attackerTeam == TEAM_INFECTED ) ||
		( GetTeamNumber() == TEAM_INFECTED && attacker && IsSurvivorDamageTeam( attacker ) && m_zombieClass != 8 ) )
		DoTerrorAnimationEvent( GetDamageAnimationEvent( info ) );
	m_previousDamageTime = m_damageTimer.GetStartTime();
	m_damageTimer.Start();
	if ( type & 8u )
		m_burnDamageTimer.Start();
	if ( ( type & 0x8Eu ) || ( type & 0x200400u ) )
		RumbleEffect( info.GetDamage() > 25.0f ? 17 : info.GetDamage() > 5.0f ? 16 : 15, 0, RUMBLE_FLAG_RESTART );
	return 1;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:0x2FB760.
TerrorPlayerAnimEvent CTerrorPlayer::GetDamageAnimationEvent( const CTakeDamageInfo &info ) const
{
	Vector direction = info.GetDamageForce();
	if ( direction.x > -0.01f && direction.x < 0.01f && direction.y > -0.01f && direction.y < 0.01f &&
		direction.z > -0.01f && direction.z < 0.01f )
		return TERROR_ANIMEVENT_FLINCH_FRONT;
	VectorNormalize( direction );
	QAngle angles;
	VectorAngles( direction, angles );
	const float yaw = AngleNormalize( const_cast< CTerrorPlayer * >( this )->EyeAngles().y - angles.y + 180.0f );
	return fabsf( yaw ) < 90.0f ? TERROR_ANIMEVENT_FLINCH_FRONT : TERROR_ANIMEVENT_FLINCH_BACK;
}

void CTerrorPlayer::ClearDelayedDSP()
{
	m_delayedDSP = 0;
	m_delayedDSPTimer.Invalidate();
}

// S:0x3075F0. Equality advances to the next distance band.
void CTerrorPlayer::SetDelayedDSP( float distance, float delay, float sensitivity )
{
	if ( GetObserverMode() != OBS_MODE_NONE && GetObserverMode() != OBS_MODE_IN_EYE )
		return;
	static ConVarRef one( "z_deafen_radius_one" );
	static ConVarRef two( "z_deafen_radius_two" );
	static ConVarRef three( "z_deafen_radius_three" );
	if ( one.GetFloat() * sensitivity > distance )
		m_delayedDSP = 134;
	else if ( two.GetFloat() * sensitivity > distance )
		m_delayedDSP = 135;
	else if ( three.GetFloat() * sensitivity > distance )
		m_delayedDSP = 136;
	else
		return;
	m_delayedDSPTimer.Start( delay );
}

void CTerrorPlayer::UpdateDelayedDSP()
{
	if ( m_delayedDSP != 0 && gpGlobals->curtime >= m_delayedDSPTimer.m_timestamp.Get() )
	{
		CSingleUserRecipientFilter filter( this );
		enginesound->SetPlayerDSP( filter, m_delayedDSP, false );
		m_delayedDSP = 0;
	}
}

bool CTerrorPlayer::IsInvulnerable() const
{
	return IsGhost() || ( m_invulnerabilityTimer.m_timestamp.Get() > 0.0f &&
		gpGlobals->curtime < m_invulnerabilityTimer.m_timestamp.Get() );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// S:0x333020. Preserve the native input handles and unsigned original masks.
int CTerrorPlayer::OnTakeDamage( const CTakeDamageInfo &input )
{
	CTakeDamageInfo info = input;
	if ( GetTeamNumber() == 4 && IsBot() )
		return 0;
	const unsigned int type = static_cast< unsigned int >( info.GetDamageType() );
	if ( ( type & 0x20u ) && m_fallDamageImmunityUntil > gpGlobals->curtime )
	{
		if ( m_clearFallDamageImmunityOnDamage )
			m_fallDamageImmunityUntil = 0.0f;
		return 0;
	}
	if ( ( type & 0x02000000u ) && GetTeamNumber() == TEAM_INFECTED && m_zombieClass != 8 && m_zombieClass != 6 )
		StaggerPlayer( info.GetInflictor(), NULL );
	if ( ( type & 0x00800000u ) && GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 2 )
		info.AddDamage( static_cast< float >( GetHealth() ) );
	if ( !IsOnFire() || ( type & 8u ) )
	{
		if ( IsOnFire() && ( type & 0x10000000u ) )
		{
			static ConVarRef lifetime( "z_burning_lifetime" );
			float damage = ( static_cast< float >( GetMaxHealth() ) / lifetime.GetFloat() ) * 0.2f;
			if ( !( damage > 1.0f ) ) damage = 1.0f;
			if ( IsSurvivorDamageTeam( this ) )
				damage = 0.0f;
			else if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 )
			{
				static ConVarRef normal( "tank_burn_duration" );
				static ConVarRef hard( "tank_burn_duration_hard" );
				static ConVarRef expert( "tank_burn_duration_expert" );
				const int difficulty = TerrorGameRules()->GetDifficultyLevel();
				const float duration = difficulty == 3 ? expert.GetFloat() : difficulty == 2 ? hard.GetFloat() : normal.GetFloat();
				damage = ( static_cast< float >( GetMaxHealth() ) / duration ) * 0.2f;
				if ( !( damage > 1.0f ) ) damage = 1.0f;
			}
			info.SetDamage( damage );
		}
		if ( type & 8u )
		{
			if ( GetTeamNumber() == TEAM_INFECTED )
			{
				m_burnAttacker = ToBasePlayer( info.GetAttacker() );
				const bool ownFlame = IsOnFire() && GetEffectEntity() && GetEffectEntity() == info.GetInflictor();
				if ( type & 2u )
				{
					static ConVarRef duration( "incendiary_ammo_burn_time" );
					Ignite( duration.GetFloat(), false );
					m_burnDamageType = 2;
				}
				else if ( !IsOnFire() || ( m_burnDamageType == 2 && !ownFlame ) )
				{
					Ignite( 9999.0f, false );
					CEntityFlame *flame = dynamic_cast< CEntityFlame * >( GetEffectEntity() );
					if ( flame )
						flame->SetDamageCustom( info.GetDamageCustom() );
				}
			}
			else if ( IsSurvivorDamageTeam( this ) && info.GetDamage() > 0.0f && !( type & 0x10000000u ) )
			{
				Ignite( 1.0f, false );
				if ( TerrorGameRules()->IsPlayerControlledZombies() && dynamic_cast< CEntityFlame * >( info.GetAttacker() ) )
					info.SetDamage( 0.0f );
			}
		}
	}
	if ( IsSurvivorDamageTeam( this ) )
	{
		if ( TheDirector->IsFinaleWon() || ( m_positionEntity.Get() && m_positionEntity.Get() != this ) )
			return 0;
		CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() );
		if ( attacker && IsSurvivorDamageTeam( attacker ) && attacker != this &&
			( m_pounceAttacker.Get() || m_tongueOwner.Get() ) && !( type & 8u ) )
			return 0;
	}
	if ( IsGhost() && info.GetDamageCustom() == 2 )
		return BaseClass::OnTakeDamage( info );
	if ( IsInvulnerable() && type != 0x20u )
		return 0;
	if ( IsGhost() )
		return TakeGhostTriggerDamage( info );
	if ( GetFlags() & FL_GODMODE )
		return 0;
	if ( god.GetInt() != 0 && IsSurvivorDamageTeam( this ) )
	{
		if ( god.GetInt() < 0 )
		{
			SetHealth( GetMaxHealth() );
			SetHealthBuffer( 0.0f );
			if ( !IsAlive() )
				return 0;
			if ( IsIncapacitated() )
				OnRevived();
			TakeHealth( static_cast< float >( GetMaxHealth() ), 0 );
		}
		return 0;
	}
	static ConVarRef buddha( "buddha" );
	if ( buddha.GetInt() != 0 && IsSurvivorDamageTeam( this ) && !IsIncapacitated() )
	{
		int survivors = 0;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( player && player->IsConnected() && player->GetTeamNumber() == TEAM_SURVIVOR &&
				player->IsAlive() && !player->IsIncapacitated() )
				++survivors;
		}
		if ( survivors <= buddha.GetInt() && static_cast< int >( GetHealth() - info.GetDamage() ) < 2 )
		{
			SetHealth( 10 );
			info.SetDamage( 1.0f );
		}
	}
	if ( OnLedgeBleedOut( info ) )
		return 0;
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 && ( type & 1u ) )
	{
		CPhysicsProp *prop = dynamic_cast< CPhysicsProp * >( info.GetAttacker() );
		if ( !prop )
			prop = dynamic_cast< CPhysicsProp * >( info.GetInflictor() );
		if ( prop && prop->IsTankHittable() && prop->WasRecentlyHitByTank() )
			return 0;
	}
	CTerrorPlayer *playerAttacker = ToTerrorPlayer( info.GetAttacker() );
	if ( TheDirector->GetScenario()->GetFinaleType() == 1 && ( type & 1u ) && info.GetInflictor() &&
		playerAttacker && playerAttacker->GetTeamNumber() == TEAM_INFECTED && playerAttacker->m_zombieClass == 8 )
	{
		info.ScaleDamage( 0.25f );
		// Original333A94 passes the RAW prop velocity. The nearby computed
		// horizontal clamp/upward300 local is unused; do not repair that bug.
		Launch( info.GetInflictor()->GetAbsVelocity(), TERROR_ANIMEVENT_HIT_BY_TANK, playerAttacker, 3.0f );
	}
	// The original weapon v424 is empty in every concrete original weapon,
	// including the independent HunterClaw empty override.
	return BaseClass::OnTakeDamage( info );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CTerrorPlayer::OnEnteredSpit()
{
	if ( gpGlobals->curtime - m_lastSpitTime > 1.0f )
	{
		if ( IsBot() )
			MyNextBotPointer()->OnEnteredSpit();
		IGameEvent *event = gameeventmanager->CreateEvent( "entered_spit" );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			gameeventmanager->FireEvent( event );
		}
		GetMusic()->OnSpitterSpitAttack();
		SpeakIfAllowed( "GooedBySpitter" );
	}
	m_lastSpitTime = gpGlobals->curtime;
}

// S:0x306120. Pain criteria are consumed by the real priority/response owner.
void CTerrorPlayer::OnSurvivorPain( const CTakeDamageInfo &info )
{
	CBaseEntity *attacker = info.GetAttacker();
	const unsigned int type = static_cast< unsigned int >( info.GetDamageType() );
	const bool combatDamage = attacker && attacker->MyCombatCharacterPointer() &&
		!( IsIncapacitated() && attacker == this );
	char criteria[256] = "";
	bool speakPain = false;
	int intensity = 2;
	if ( combatDamage )
	{
		if ( GetObserverTarget() == this || !IsObserver() )
		{
			static ConVarRef alpha( "z_damage_screen_fade_alpha" );
			static ConVarRef duration( "z_damage_screen_fade_duration" );
			const color32 fade = { 255, 0, 0, static_cast< unsigned char >( alpha.GetInt() ) };
			UTIL_ScreenFade( this, fade, duration.GetFloat(), 0.0f, FFADE_IN );
		}
		if ( attacker->GetTeamNumber() == TEAM_INFECTED && type != 0x20000u && type != 0x100000u )
		{
			if ( IsIncapacitated() )
			{
				if ( m_downAndBeatenTimer.IsElapsed() )
					GetMusic()->OnDownAndBeaten();
				m_downAndBeatenTimer.Start( 3.0f );
			}
			CTerrorPlayer *playerAttacker = ToTerrorPlayer( attacker );
			if ( playerAttacker && playerAttacker != m_pounceAttacker.Get() && playerAttacker->m_zombieClass != 6 )
				EmitSound( "PlayerZombie.AttackHit" );
		}
		CTerrorPlayer *hunter = ToTerrorPlayer( attacker );
		if ( hunter && type == 128u && hunter->m_zombieClass == 3 && m_pounceAttacker.Get() == hunter )
		{
			if ( m_vocalizeTimer.IsElapsed() || m_vocalizeTimer.GetRemainingTime() < 0.5f )
			{
				if ( m_pouncePainAlternation || !IsIncapacitated() )
				{
					SpeakIfAllowed( "ScreamWhilePounced" );
					m_vocalizeTimer.Start( 2.0f );
				}
				else
				{
					Q_strncpy( criteria, "PainLevel:Incapacitated", sizeof( criteria ) );
					speakPain = true;
				}
				m_pouncePainAlternation = !m_pouncePainAlternation;
			}
		}
		else if ( IsIncapacitated() )
		{
			Q_strncpy( criteria, "PainLevel:Incapacitated", sizeof( criteria ) );
			intensity = 3;
			speakPain = true;
		}
		else
			speakPain = true;
	}
	else
	{
		if ( type != 256u && !( type & 8u ) )
			return;
		speakPain = true;
	}
	if ( speakPain && !criteria[0] )
	{
		float ratio = info.GetDamage() / static_cast< float >( GetHealth() );
		if ( TerrorGameRules()->IsTempHealthOnly() )
			ratio = info.GetDamage() / GetHealthBuffer();
		if ( ratio < 0.2f )
		{
			Q_strncpy( criteria, "PainLevel:Minor", sizeof( criteria ) );
			intensity = 2;
		}
		else if ( ratio < 0.5f )
		{
			Q_strncpy( criteria, "PainLevel:Major", sizeof( criteria ) );
			intensity = 3;
		}
		else
		{
			Q_strncpy( criteria, "PainLevel:Critical", sizeof( criteria ) );
			intensity = 4;
		}
	}
	if ( !IsSurvivorDamageTeam( attacker ) )
		m_survivorIntensity.Add( intensity );
	if ( speakPain && gpGlobals->curtime >= m_painTimer.m_timestamp.Get() )
	{
		CTerrorPlayer *playerAttacker = ToTerrorPlayer( attacker );
		if ( playerAttacker && playerAttacker->GetTeamNumber() == TEAM_INFECTED )
		{
			Q_strncat( criteria, ",SubjectIsA:", sizeof( criteria ) );
			Q_strncat( criteria, GetZombieClassName( playerAttacker->m_zombieClass ), sizeof( criteria ) );
		}
		const bool wasOnFire = IsOnFire();
		if ( type & 8u )
		{
			AddFlag( FL_ONFIRE );
			Q_strncat( criteria, ",DamageType:DMG_BURN", sizeof( criteria ) );
		}
		if ( SpeakIfAllowed( "Pain", 1, criteria ) )
			m_painTimer.Start( 1.5f );
		if ( !wasOnFire )
			RemoveFlag( FL_ONFIRE );
	}
}

void CTerrorPlayer::OnInfectedPain( const CTakeDamageInfo &info )
{
	if ( gpGlobals->curtime < m_infectedPainTimer.m_timestamp.Get() )
		return;
	m_infectedPainTimer.Start( 0.7f );
	const bool shortPain = ( info.GetDamageType() & 0x82u ) != 0;
	switch ( m_zombieClass )
	{
	case 1: EmitInfectedSound( shortPain ? "SmokerZombie.PainShort" : "SmokerZombie.Pain" ); break;
	case 2: EmitInfectedSound( shortPain ? "BoomerZombie.PainShort" : "BoomerZombie.Pain" ); break;
	case 3: EmitInfectedSound( shortPain ? "HunterZombie.PainShort" : "HunterZombie.Pain" ); break;
	case 4: EmitInfectedSound( shortPain ? "SpitterZombie.PainShort" : "SpitterZombie.Pain" ); break;
	case 5: EmitInfectedSound( shortPain ? "JockeyZombie.PainShort" : "JockeyZombie.Pain" ); break;
	case 6: EmitInfectedSound( "ChargerZombie.Pain" ); break;
	case 8: if ( !IsIncapacitated() ) EmitInfectedSound( "HulkZombie.Pain", 0.7f ); break;
	}
}

int CTerrorPlayer::TakeGhostTriggerDamage( const CTakeDamageInfo &info )
{
	return dynamic_cast< CTriggerHurtGhost * >( info.GetInflictor() ) ? BaseClass::OnTakeDamage( info ) : 0;
}

void CTerrorPlayer::LogTerrorDamage( const CTakeDamageInfo &info, int reason )
{
	static ConVarRef detail( "mp_logdetail" );
	if ( detail.GetInt() != 3 )
		return;
	char attacker[256], inflictor[256], victim[256], types[256] = "";
	CEventLog *eventLog = static_cast< CEventLog * >( GameLogSystem() );
	eventLog->GetEntityIdentity( info.GetAttacker(), attacker, sizeof( attacker ) );
	eventLog->GetEntityIdentity( info.GetInflictor(), inflictor, sizeof( inflictor ) );
	eventLog->GetEntityIdentity( this, victim, sizeof( victim ) );
	static const char *const names[32] =
	{
		"DMG_CRUSH", "DMG_BULLET", "DMG_SLASH", "DMG_BURN", "DMG_VEHICLE", "DMG_FALL", "DMG_BLAST", "DMG_CLUB",
		"DMG_SHOCK", "DMG_SONIC", "DMG_ENERGYBEAM", "DMG_BLAMELESS_FRIENDLY_FIRE", "DMG_NEVERGIB", "DMG_ALWAYSGIB",
		"DMG_DROWN", "DMG_FORCE_INCAPACITATE", "DMG_NERVEGAS", "DMG_POISON", "DMG_RADIATION", "DMG_DROWNRECOVER",
		"DMG_CHOKE", "DMG_SLOWBURN", "DMG_REMOVENORAGDOLL", "DMG_PHYSGUN", "DMG_PLASMA", "DMG_AIRBOAT",
		"DMG_DISSOLVE", "DMG_BLAST_SURFACE", "DMG_DIRECT", "DMG_BUCKSHOT", "DMG_HEADSHOT", "DMG_DISMEMBER"
	};
	const unsigned int mask = static_cast< unsigned int >( info.GetDamageType() );
	for ( int i = 0; i < 32; ++i )
	{
		if ( mask & ( 1u << i ) )
		{
			if ( types[0] )
				Q_strncat( types, "|", sizeof( types ) );
			Q_strncat( types, names[i], sizeof( types ) );
		}
	}
	if ( reason == 0 )
		UTIL_LogPrintf( "%s was hurt by %s, using %s to inflict %f damage of type %s\n", victim, attacker, inflictor, info.GetDamage(), types );
	else if ( reason == 1 )
		UTIL_LogPrintf( "%s was incapacitated by %s, using %s to inflict %f damage of type %s\n", victim, attacker, inflictor, info.GetDamage(), types );
	else if ( reason == 2 )
		UTIL_LogPrintf( "%s was killed by %s, using %s to inflict %f damage of type %s\n", victim, attacker, inflictor, info.GetDamage(), types );
}

// S:0x3173B0. Native damage credits accept negative amounts and cap only
// the upper bound. Assist and direct character counters remain independent.
void CTerrorPlayer::RecordDamageToSurvivor( int damage, bool assist, int character )
{
	if ( GetTeamNumber() != TEAM_INFECTED )
		return;
	static ConVarRef maximum( "z_max_survivor_damage" );
	if ( damage >= maximum.GetInt() )
		damage = maximum.GetInt();
	switch ( m_zombieClass )
	{
	case 1: m_checkpointPZSmokerDamage += damage; break;
	case 2: m_checkpointPZBoomerDamage += damage; break;
	case 3: m_checkpointPZHunterDamage += damage; break;
	case 4: m_checkpointPZSpitterDamage += damage; break;
	case 5: m_checkpointPZJockeyDamage += damage; break;
	case 6: m_checkpointPZChargerDamage += damage; break;
	case 8: m_checkpointPZTankDamage += damage; break;
	}
	if ( TerrorGameRules() && TerrorGameRules()->IsPlayerControlledZombies() )
		TerrorGameRules()->RecordSurvivorDamage( this, damage, assist );
	m_checkpointSurvivorDamage += damage;
	m_missionSurvivorDamage += damage;
	m_totalSurvivorDamage += damage;
	if ( static_cast< unsigned int >( character ) <= 3u )
	{
		if ( assist )
		{
			m_survivorAssistDamage[character] += damage;
			m_assistSurvivorDamage += damage;
		}
		else
		{
			m_survivorDirectDamage[character] += damage;
			m_directSurvivorDamage += damage;
		}
	}
}

void SendPZDamageMessage( int type, CTerrorPlayer *victim, CTerrorPlayer *attacker, CTerrorPlayer *assist, bool allTeams, int amount )
{
	if ( !victim || !attacker )
		return;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *recipient = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !recipient || !recipient->IsConnected() || !recipient->IsNetClient() || recipient->IsSplitScreenPlayer() )
			continue;
		if ( !allTeams && type > 6 && ( type < 15 ? recipient->GetTeamNumber() != TEAM_INFECTED : recipient->GetTeamNumber() != TEAM_SURVIVOR ) )
			continue;
		CSingleUserRecipientFilter filter( recipient );
		filter.MakeReliable();
		UserMessageBegin( filter, "PZDmgMsg" );
		WRITE_BYTE( type );
		WRITE_SHORT( attacker->GetUserID() );
		WRITE_SHORT( victim->GetUserID() );
		WRITE_SHORT( assist ? assist->GetUserID() : 0 );
		WRITE_SHORT( amount );
		MessageEnd();
	}
}

// The survivor-victim portion of original Director helper266050.
float CTerrorPlayer::GetTankHitDamage() const
{
	if ( IsSurvivorDamageTeam( this ) && TerrorGameRules()->IsPlayerControlledZombies() )
	{
		static ConVarRef versusDamage( "vs_tank_damage" );
		return TheDirector->GetDirectorOptionFloat( "TankHitDamageModifierVersus", 1.0f ) * versusDamage.GetFloat();
	}
	if ( IsSurvivorDamageTeam( this ) && IsIncapacitated() && !m_isHangingFromLedge && !m_isHangingFromTongue )
	{
		const int difficulty = TerrorGameRules()->GetDifficultyLevel();
		if ( difficulty >= 0 && difficulty <= 2 )
			return 75.0f;
		if ( difficulty == 3 )
			return 150.0f;
	}
	else if ( GetTeamNumber() == TEAM_INFECTED )
		return 250.0f;
	const float modifier = TheDirector->GetDirectorOptionFloat( "TankHitDamageModifierCoop", 1.0f );
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 0:
	case 1: return modifier * 24.0f;
	case 2: return modifier * 33.0f;
	case 3: return modifier * 100.0f;
	default: return 999.90002f;
	}
}

// S:0x3317E0. The prop owns its interaction bit, timer and physics attacker.
void CTerrorPlayer::OnSurvivorPhysicsDamage( const CTakeDamageInfo &info )
{
	if ( info.GetDamage() < 1.0f || !( info.GetDamageType() & 1u ) || !IsAlive() || IsInvulnerable() ||
		IsIncapacitated() || m_isHangingFromLedge || m_isFallingFromLedge || IsKnockedDown() ||
		m_reviveOwner.Get() || ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 8 ) )
		return;
	CPhysicsProp *prop = dynamic_cast< CPhysicsProp * >( info.GetAttacker() );
	if ( !prop )
		prop = dynamic_cast< CPhysicsProp * >( info.GetInflictor() );
	if ( !prop || !prop->IsTankHittable() || !prop->WasRecentlyHitByTank() )
		return;
	if ( info.GetDamage() >= GetTankHitDamage() )
		Knockdown( 2 );
	else if ( info.GetDamage() >= 10.0f )
	{
		const Vector position = info.GetDamagePosition();
		StaggerPlayer( prop, &position );
	}
	CTerrorPlayer *tank = ToTerrorPlayer( info.GetAttacker() );
	if ( !tank || tank->GetTeamNumber() != TEAM_INFECTED || tank->m_zombieClass != 8 )
		tank = ToTerrorPlayer( prop->HasPhysicsAttacker( 2.0f ) );
	if ( tank && tank->GetTeamNumber() == TEAM_INFECTED && tank->m_zombieClass == 8 )
	{
		tank->OnTankPhysicsHitVictim( this, false );
		tank->ResetFrustration();
	}
}

// S2FB820. Rock passes its actual newly-incapacitated result; props pass false.
void CTerrorPlayer::OnTankPhysicsHitVictim( CTerrorPlayer *victim, bool newlyIncapacitated )
{
	if ( GetTeamNumber() != TEAM_INFECTED || GetZombieClass() != 8 || !IsAlive() || IsIncapacitated() ||
		IsStaggeringOrPending() || TerrorGameRules()->IsPlayerControlledZombies() )
		return;

	const bool usePrimary = ( newlyIncapacitated || TerrorGameRules()->GetDifficultyLevel() == 0 ) &&
		victim && victim->IsPlayer() &&
		( victim->IsIncapacitated() || TerrorGameRules()->GetDifficultyLevel() == 0 );
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_TANK_SPECIAL, usePrimary ? 0 : 1 );
}

// S:0x3319E0: shared preapplication response, then the native survivor
// attribution chain. This runs before armor and before the lethal gate.
void CTerrorPlayer::OnSurvivorDamageResponse( const CTakeDamageInfo &info )
{
	if ( info.GetDamage() == 0.0f )
		return;
	CBaseEntity *attackerEntity = info.GetAttacker();
	const unsigned int type = static_cast< unsigned int >( info.GetDamageType() );
	if ( IsSurvivorDamageTeam( attackerEntity ) && GetHealthBuffer() + GetHealth() > info.GetDamage() &&
		( type & 0x40u ) && !IsIncapacitated() &&
		( !( type & 0x00800000u ) || GetTeamNumber() != TEAM_INFECTED || m_zombieClass != 8 ) )
	{
		float ratio = info.GetDamage() / ( info.GetMaxDamage() + 0.001f );
		if ( info.GetDamageCustom() == 1 )
		{
			if ( GetTeamNumber() == TEAM_INFECTED && ( m_zombieClass == 8 || m_zombieClass == 6 ) )
				ratio = 0.0f;
			if ( IsSurvivorDamageTeam( this ) && attackerEntity != this )
				ratio = 0.0f;
		}
		if ( ratio > 0.5f )
		{
			const Vector position = info.GetDamagePosition();
			StaggerPlayer( attackerEntity, &position );
		}
	}
	Infected *common = dynamic_cast< Infected * >( info.GetInflictor() );
	if ( !common || ( common->GetTeamNumber() == TEAM_INFECTED && common->GetZombieClass() != 0 ) )
		SetVelocityModifier( 1.0f );
	else if ( IsSurvivorDamageTeam( this ) )
	{
		if ( !IsAdrenalineActive() && !m_jockeyAttacker.Get() &&
			( !IsBot() || TheDirector->GetScenario()->GetFinaleType() != 1 ) )
			SetVelocityModifier( survivor_damage_speed_factor.GetFloat() );
	}
	else if ( GetTeamNumber() != TEAM_INFECTED || IsIT() )
		SetVelocityModifier( survivor_damage_speed_factor.GetFloat() );
	OnSurvivorPhysicsDamage( info );
	if ( GetTeamNumber() == TEAM_INFECTED )
	{
		OnInfectedDamageResponse( info );
		return;
	}
	CTerrorPlayer *attacker = ToTerrorPlayer( attackerEntity );
	CTerrorPlayer *inflictor = ToTerrorPlayer( info.GetInflictor() );
	if ( inflictor && inflictor->GetTeamNumber() == TEAM_INFECTED && inflictor->m_zombieClass == 2 )
		attacker = inflictor;
	bool credit = true;
	bool displacedDamage = ( type & 0x28u ) != 0;
	if ( m_damageStaggerTimer.GetRemainingTime() > 0.0f || ( type & 0x20000u ) )
	{
		credit = false;
		displacedDamage = false;
	}
	CTerrorPlayer *displacer = ToTerrorPlayer( m_hStaggerSource.Get() );
	CTerrorPlayer *creditedDisplacer = NULL;
	if ( displacer && displacedDamage && m_staggerSourceTimer.GetRemainingTime() > 0.0f && !displacer->IsBot() )
	{
		creditedDisplacer = displacer;
		displacer->RecordDamageToSurvivor( static_cast< int >( info.GetDamage() ), true, GetSurvivorCharacter() );
		if ( type & 8u )
			m_damageStaggerTimer.Start( m_staggerSourceTimer.GetRemainingTime() );
	}
	const bool throttled = !CheckDamageThrottle( info, false );
	CTerrorPlayer *assist = NULL;
	CTerrorPlayer *itSourceCredit = NULL;
	const bool nonPlayerAttacker = !attackerEntity || !attackerEntity->IsPlayer();
	if ( !throttled && credit && nonPlayerAttacker )
	{
		CTerrorPlayer *pinOwners[] =
		{
			ToTerrorPlayer( m_tongueOwner.Get() ), ToTerrorPlayer( m_pounceAttacker.Get() ), ToTerrorPlayer( m_jockeyAttacker.Get() )
		};
		for ( int i = 0; i < ARRAYSIZE( pinOwners ); ++i )
		{
			CTerrorPlayer *owner = pinOwners[i];
			if ( owner && !owner->IsBot() && owner != attacker && owner != creditedDisplacer )
			{
				assist = owner;
				owner->RecordDamageToSurvivor( static_cast< int >( info.GetDamage() ), true, GetSurvivorCharacter() );
			}
		}
	}
	if ( credit && nonPlayerAttacker )
	{
		CTerrorPlayer *source = ToTerrorPlayer( m_coughSource.Get() );
		if ( m_coughSourceTimer.GetRemainingTime() > 0.0f && source && !source->IsBot() &&
			source != attacker && source != creditedDisplacer )
		{
			assist = source;
			if ( !throttled )
				source->RecordDamageToSurvivor( static_cast< int >( info.GetDamage() ), true, GetSurvivorCharacter() );
			if ( type & 8u )
				m_damageStaggerTimer.Start( m_coughSourceTimer.GetRemainingTime() );
		}
		source = ToTerrorPlayer( m_hVomitDamageSource.Get() );
		if ( m_vomitDamageSourceTimer.GetRemainingTime() > 0.0f && source && !source->IsBot() &&
			source != attacker && source != creditedDisplacer )
		{
			assist = source;
			if ( !throttled )
				source->RecordDamageToSurvivor( static_cast< int >( info.GetDamage() ), true, GetSurvivorCharacter() );
			if ( info.GetDamage() >= GetHealthBuffer() + GetHealth() )
			{
				IGameEvent *event = gameeventmanager->CreateEvent( "fatal_vomit" );
				if ( event )
				{
					event->SetInt( "userid", source->GetUserID() );
					event->SetInt( "victim", GetUserID() );
					gameeventmanager->FireEvent( event );
				}
			}
			if ( type & 8u )
				m_damageStaggerTimer.Start( m_vomitDamageSourceTimer.GetRemainingTime() );
			goto damage_message;
		}
	}
	if ( attackerEntity && m_itTimer.GetRemainingTime() > 0.0f && m_hITSource.Get() )
	{
		CTerrorPlayer *source = ToTerrorPlayer( m_hITSource.Get() );
		if ( source && !source->IsBot() && nonPlayerAttacker )
		{
			if ( info.GetDamage() >= GetHealthBuffer() + GetHealth() )
			{
				IGameEvent *event = gameeventmanager->CreateEvent( "fatal_vomit" );
				if ( event )
				{
					event->SetInt( "userid", source->GetUserID() );
					event->SetInt( "victim", GetUserID() );
					gameeventmanager->FireEvent( event );
				}
			}
			if ( throttled )
				return;
			source->RecordDamageToSurvivor( static_cast< int >( info.GetDamage() ), true, GetSurvivorCharacter() );
			itSourceCredit = source;
		}
	}
damage_message:
	if ( throttled )
		return;
	if ( attacker && attacker->GetTeamNumber() == TEAM_INFECTED && !attacker->IsBot() )
	{
		attacker->RecordDamageToSurvivor( static_cast< int >( info.GetDamage() ), false, GetSurvivorCharacter() );
		CTerrorPlayer *secondary = assist ? assist : itSourceCredit;
		if ( secondary && secondary != attacker )
			SendPZDamageMessage( 8, this, attacker, secondary );
		else
			SendPZDamageMessage( 9, this, attacker );
	}
	else if ( attackerEntity && attackerEntity->MyCombatCharacterPointer() )
	{
		CTerrorPlayer *source = assist ? assist : itSourceCredit;
		if ( !source )
			source = creditedDisplacer;
		if ( source )
			SendPZDamageMessage( 7, this, source );
	}
	else if ( type & 0x20u )
	{
		if ( creditedDisplacer )
			SendPZDamageMessage( assist ? 10 : 11, this, creditedDisplacer, assist );
		else if ( assist )
			SendPZDamageMessage( 7, this, assist );
	}
}

// The infected-player continuation of S:0x3319E0.
void CTerrorPlayer::OnInfectedDamageResponse( const CTakeDamageInfo &info )
{
	const unsigned int type = static_cast< unsigned int >( info.GetDamageType() );
	if ( type & 8u )
	{
		const float modifier = TerrorGameRules()->IsPlayerControlledZombies() ? 1.0f : 0.8f;
		if ( GetVelocityModifier() > modifier )
			SetVelocityModifier( modifier );
	}
	else if ( m_zombieClass == 8 && info.GetInflictor() && !( GetFlags() & FL_DUCKING ) )
	{
		if ( info.GetAmmoType() == g_iMinigunAmmoIndex )
		{
			if ( GetVelocityModifier() > 0.5f )
			{
				SetVelocityModifier( 0.5f );
				DoTerrorAnimationEvent( GetDamageAnimationEvent( info ) );
			}
			if ( MyNextBotPointer() )
				MyNextBotPointer()->OnCommandAttack( info.GetAttacker() );
		}
		else
		{
			static ConVarRef minimum( "z_tank_damage_slow_min_range" );
			static ConVarRef maximum( "z_tank_damage_slow_max_range" );
			const float range = ( info.GetInflictor()->GetAbsOrigin() - GetAbsOrigin() ).Length();
			const float scale = range > maximum.GetFloat() ? 0.0f : range <= minimum.GetFloat() ? 1.0f :
				1.0f - ( range - minimum.GetFloat() ) / ( maximum.GetFloat() - minimum.GetFloat() );
			if ( scale > 0.0f )
			{
				const float modifier = scale >= 1.0f ? 0.7f : ( 1.0f - scale ) * 0.3f + 0.7f;
				if ( GetVelocityModifier() > modifier )
					SetVelocityModifier( modifier );
			}
		}
	}
	else if ( m_zombieClass != 6 )
	{
		bool gunHit = false;
		CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() );
		if ( ( GetFlags() & FL_ONGROUND ) && LastHitGroup() != HITGROUP_LEFTLEG && LastHitGroup() != HITGROUP_RIGHTLEG && attacker )
		{
			CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( attacker->GetActiveWeapon() );
			if ( weapon )
			{
				const TerrorWeaponType_t weaponType = weapon->GetCSWpnData().m_WeaponType;
				gunHit = weaponType == TERROR_WEAPONTYPE_RIFLE || weaponType == TERROR_WEAPONTYPE_SHOTGUN ||
					weaponType == TERROR_WEAPONTYPE_SNIPER_RIFLE;
			}
		}
		if ( gunHit )
		{
			if ( GetAbsVelocity().Length() < 300.0f )
				SetVelocityModifier( 0.65f );
		}
		else
			SetVelocityModifier( 0.5f );
	}
	if ( m_zombieClass != 3 || MyNextBotPointer() )
		return;
	if ( !m_isAttemptingToPounce && !m_pounceVictim.Get() )
	{
		if ( IsLunging() )
		{
			m_pounceDamageRemaining = static_cast< int >( m_pounceDamageRemaining - info.GetDamage() );
			if ( m_pounceDamageRemaining <= 0 )
				Bash( 0.5f );
		}
		return;
	}
	m_pounceDamageRemaining = static_cast< int >( m_pounceDamageRemaining - info.GetDamage() );
	if ( m_pounceDamageRemaining <= 0 )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "pounce_stopped" );
		if ( event )
		{
			if ( CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() ) )
				event->SetInt( "userid", attacker->GetUserID() );
			CBasePlayer *victim = ToBasePlayer( m_pounceVictim.Get() );
			event->SetInt( "victim", victim ? victim->GetUserID() : 0 );
			gameeventmanager->FireEvent( event );
		}
		EmitSound( "HunterZombie.Pounce.Cancel" );
		ReleasePounce();
		CTakeDamageInfo lethal = info;
		lethal.AddDamage( static_cast< float >( GetHealth() ) );
		TakeDamage( lethal );
	}
}

// S:0x314BB0: the fall rule calls this even when infected HP fall damage
// is suppressed. This is not a second fall-health application.
void CTerrorPlayer::OnLand( float damage )
{
	if ( GetTeamNumber() != TEAM_INFECTED )
		return;
	static ConVarRef splat( "z_exploding_splat" );
	if ( m_zombieClass == 2 && splat.GetFloat() > 0.0f && damage > splat.GetFloat() &&
		!IsGhost() && !TerrorGameRules()->IsPlayerControlledZombies() )
	{
		CTakeDamageInfo info( this, this, 100.0f, 0x20 );
		Vector direction = GetAbsVelocity();
		if ( direction.IsZero( 0.01f ) )
			direction.z = -1.0f;
		VectorNormalize( direction );
		CalculateMeleeDamageForce( &info, direction, WorldSpaceCenter(), 0.2f );
		trace_t trace;
		memset( &trace, 0, sizeof( trace ) );
		trace.startpos = trace.endpos = GetAbsOrigin();
		trace.plane.normal = -direction;
		trace.plane.dist = DotProduct( trace.plane.normal, GetAbsOrigin() );
		trace.plane.type = 5;
		trace.plane.signbits = SignbitsForPlane( &trace.plane );
		trace.fraction = 1.0f;
		trace.m_pEnt = GetWorldEntity();
		ClearMultiDamage();
		DispatchTraceAttack( info, direction, &trace );
		ApplyMultiDamage();
		return;
	}
	if ( !IsGhost() )
		EmitSound( m_zombieClass == 8 ? "HulkZombie.Thud" : "PlayerZombie.Thud" );
	m_vocalizeTimer.Start( 1.0f );
	static ConVarRef delay( "z_player_zombie_land_delay" );
	SetStamina( delay.GetFloat() * 1000.0f );
	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( GetActiveWeapon() );
	if ( weapon )
	{
		weapon->m_flNextPrimaryAttack = gpGlobals->curtime + delay.GetFloat();
		weapon->OnOwnerStunned( delay.GetFloat() );
	}
}

// S:0x300BE0: native per-class impact particles and trace bleeding.
void CTerrorPlayer::SpawnTraceBlood( float damage, const CTakeDamageInfo &info, const Vector &direction, trace_t *trace )
{
	if ( GetTeamNumber() == 4 )
		return;
	const char *particle = NULL;
	if ( GetTeamNumber() == TEAM_SURVIVOR )
		particle = "blood_impact_survivor_01";
	else if ( GetTeamNumber() == TEAM_INFECTED )
	{
		if ( m_zombieClass == 1 )
			particle = "blood_impact_smoker_01";
		else
		{
			TraceBleed( damage, direction, trace, info.GetDamageType() );
			switch ( m_zombieClass )
			{
			case 2: particle = "blood_impact_boomer_01"; break;
			case 3: particle = "blood_impact_hunter_01"; break;
			case 8:
			{
				static ConVarRef specialBlood( "z_tank_has_special_blood" );
				particle = specialBlood.GetBool() ? "blood_impact_tank_01" : "blood_impact_infected_01";
				break;
			}
			default: particle = "blood_impact_infected_01"; break;
			}
		}
	}
	if ( particle )
	{
		QAngle angles;
		VectorAngles( -direction, angles );
		DispatchParticleEffect( particle, trace->endpos, GetAbsVelocity(), angles );
	}
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// S2413D0. Keep the existing damage.cpp owner and its independent explosion DSP fields.
void CCSPlayer::OnDamagedByExplosion( const CTakeDamageInfo &info )
{
	const float damage = info.GetDamage();
	CBaseEntity *inflictor = info.GetInflictor();
	if ( inflictor )
	{
		// S241411..241433 force dirty absolute transforms even below the damage gate.
		inflictor->GetAbsOrigin();
		GetAbsOrigin();
	}
	if ( damage >= 30.0f )
	{
		m_explosionDSP = 0;
		// S241459 uses x87 FADD of the DOUBLE 0.3, then stores one float.
		m_explosionDSPTime = static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.3 );
	}
}

// S241470. The sole native caller S246030 owns nonzero/deadline admission above.
void CCSPlayer::UpdateExplosionDSP()
{
	const float elapsed = gpGlobals->curtime - m_explosionDSPTime;
	if ( elapsed >= 4.5f )
	{
		m_explosionDSPTime = 0.0f;
		m_explosionDSP = 0;
		return;
	}
	int preset;
	if ( elapsed < 0.1f )
		preset = static_cast< int >( 137u - static_cast< uint32 >( _mm_cvttss_si32( _mm_set_ss( elapsed * -20.0f ) ) ) );
	else if ( elapsed < 3.0f )
		preset = 139;
	else
		preset = static_cast< int >( 138u - static_cast< uint32 >( _mm_cvttss_si32( _mm_set_ss( ( elapsed - 3.0f ) * 1.3333334f ) ) ) );
	if ( preset != m_explosionDSP )
	{
		m_explosionDSP = preset;
		CSingleUserRecipientFilter filter( this );
		enginesound->SetPlayerDSP( filter, m_explosionDSP, false );
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// S:0x303160 and the actual 329F10 injury producer.
float CTerrorPlayer::GetTimeSinceEnemyInjury() const
{
	return m_terrorInjuryInterval.GetStartTime() > 0.0f ? m_terrorInjuryInterval.GetElapsedTime() : 99999.9f;
}

float CTerrorPlayer::GetTimeSinceBurnDamage() const
{
	return m_burnDamageTimer.GetStartTime() > 0.0f ? m_burnDamageTimer.GetElapsedTime() : 99999.9f;
}

// S:0x241AC0 uses the shared native death-pose direction/frame algorithm.
// The retail player additionally selects the four genuine crouch activities.
void CCSPlayer::SelectDeathPose( const CTakeDamageInfo &info )
{
	CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
	CStudioHdr *studio = GetModelPtr();
	if ( !studio || !studio->IsValid() )
		return;
	if ( info.GetDamageType() & 0x20020u )
	{
		m_iDeathPose = ACT_INVALID;
		m_iDeathFrame = 0;
		return;
	}

	Activity activity = ACT_INVALID;
	int frame = 0;
	SelectDeathPoseActivityAndFrame( this, info, LastHitGroup(), activity, frame );
	if ( m_Local.m_bDucked )
	{
		static const Activity crouchFront = (Activity)ActivityList_IndexForName( "ACT_DIE_CROUCH_FRONTSIDE" );
		static const Activity crouchRight = (Activity)ActivityList_IndexForName( "ACT_DIE_CROUCH_RIGHTSIDE" );
		static const Activity crouchBack = (Activity)ActivityList_IndexForName( "ACT_DIE_CROUCH_BACKSIDE" );
		static const Activity crouchLeft = (Activity)ActivityList_IndexForName( "ACT_DIE_CROUCH_LEFTSIDE" );
		switch ( activity )
		{
		case ACT_DIE_FRONTSIDE: activity = crouchFront; break;
		case ACT_DIE_RIGHTSIDE: activity = crouchRight; break;
		case ACT_DIE_BACKSIDE: activity = crouchBack; break;
		case ACT_DIE_LEFTSIDE: activity = crouchLeft; break;
		default: break;
		}
	}
	m_iDeathPose = activity == ACT_INVALID ? ACT_INVALID : SelectWeightedSequence( activity );
	m_iDeathFrame = activity == ACT_INVALID ? 0 : frame;
}

// S:0x300A90. Charger kills suppress the ordinary pose irrespective of
// damage bits. Incapacitation is still LIFE_ALIVE at this pre-base point.
void CTerrorPlayer::SelectDeathPose( const CTakeDamageInfo &info )
{
	if ( ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 2 ) || IsPlayingDeathAnimation() )
	{
		m_iDeathPose = ACT_INVALID;
		m_iDeathFrame = 0;
		return;
	}
	CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
	if ( IsSurvivorDamageTeam( this ) && attacker &&
		attacker->GetTeamNumber() == TEAM_INFECTED && attacker->GetZombieClass() == 6 )
	{
		m_vecTotalBulletForce *= 100.0f;
		m_iDeathPose = ACT_INVALID;
		m_iDeathFrame = 0;
	}
	else if ( IsIncapacitated() )
	{
		CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
		static const Activity incap = (Activity)ActivityList_IndexForName( "ACT_DIE_INCAP" );
		m_iDeathPose = SelectWeightedSequence( incap );
		m_iDeathFrame = 1;
	}
	else
	{
		CCSPlayer::SelectDeathPose( info );
	}
}

// S:0x3030B0. Damage listeners in the same frame observe the prior damage
// timestamp, not the timer that this injury has just restarted.
float CTerrorPlayer::GetTimeSinceLastDamage() const
{
	if ( m_damageTimer.GetStartTime() <= 0.0f )
		return gpGlobals->curtime;
	const float elapsed = m_damageTimer.GetElapsedTime();
	return elapsed > 0.0f ? elapsed : gpGlobals->curtime - m_previousDamageTime;
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

void CTerrorPlayer::InputIgnoreFallDamage( inputdata_t &inputdata )
{
	// S2FD2D0: positive and unordered float values survive the native COMISS.
	float duration;
	if ( inputdata.value.FieldType() != FIELD_FLOAT || ( duration = inputdata.value.Float() ) <= 0.0f )
		duration = 10.0f;
	m_fallDamageImmunityUntil = gpGlobals->curtime + duration;
	m_clearFallDamageImmunityOnDamage = true;
}

void CTerrorPlayer::InputIgnoreFallDamageWithoutReset( inputdata_t &inputdata )
{
	// S2FD320 differs only in the independent reset-on-damage byte.
	float duration;
	if ( inputdata.value.FieldType() != FIELD_FLOAT || ( duration = inputdata.value.Float() ) <= 0.0f )
		duration = 10.0f;
	m_fallDamageImmunityUntil = gpGlobals->curtime + duration;
	m_clearFallDamageImmunityOnDamage = false;
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
