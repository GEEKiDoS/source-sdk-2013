//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/infected/common/infected.h"
#include "physics.h"
#include "vphysics_interface.h"
#include "world.h"
#include "KeyValues.h"

#include "l4d2/weapons/weapon_csbase.h"
#include "l4d2/gamerules/terror_gamerules_damage_console_server_cvars.inc"
#include <float.h>
#include "ndebugoverlay.h"
#include "tier0/memdbgon.h"

// S:0x599950/0x599910. The original callbacks mirror these mutable values
// into two floats. Reading the native ConVars directly keeps the same values.
ConVar fall_speed_safe( "fall_speed_safe", "560", 0x00000006 );
static ConVar fall_speed_fatal( "fall_speed_fatal", "720", 0x00000006 );
static ConVar phys_explosion_force( "phys_explosion_force", "7.0", 0x00004004 );
extern ConVar radius_damage_show_hits;

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// S4EA090: wide x87 length, float spill, then scalar float reciprocal.
static void NormalizeBlastDirection( Vector &direction )
{
	const double length = sqrt( ( static_cast< double >( direction.x ) * direction.x +
		static_cast< double >( direction.y ) * direction.y ) +
		static_cast< double >( direction.z ) * direction.z );
	const float roundedLength = static_cast< float >( length );
	const float inverseLength = 1.0f / ( roundedLength + FLT_EPSILON );
	direction *= inverseLength;
}

// S2D9130 returns the original double-precision squared excess-speed ratio.
double CalculateTerrorFallDamage( float excessSpeed )
{
	if ( excessSpeed < 0.0f )
		return 0.0;
	const double ratio = static_cast< double >( excessSpeed ) /
		( static_cast< double >( fall_speed_fatal.GetFloat() ) -
			static_cast< double >( fall_speed_safe.GetFloat() ) );
	return ratio * ratio * 100.0;
}

bool CTerrorGameRules::FPlayerCanTakeDamage( CBasePlayer *victim, CBaseEntity *attacker, const CTakeDamageInfo &info )
{
	static ConVarRef friendlyFire( "mp_friendlyfire" );
	if ( attacker && PlayerRelationship( victim, attacker ) == GR_TEAMMATE &&
		!friendlyFire.GetBool() && attacker != victim )
		return false;
	// S:0x1EC740 delegates to the always-true base, not the newer target's
	// ForceFriendlyFire extension to teamplay policy.
	return CGameRules::FPlayerCanTakeDamage( victim, attacker, info );
}

// S:0x23CEF0. This is material density, not the physics object's mass.
float CTerrorGameRules::GetAmountOfPointVisible( const Vector &source, const Vector &point, CBaseEntity *target )
{
	trace_t trace;
	UTIL_TraceLine( source, point, 0x46004003u, target, COLLISION_GROUP_NONE, &trace );
	if ( trace.fraction == 1.0f )
		return 1.0f;
	CBaseEntity *blocker = trace.m_pEnt;
	if ( trace.DidHitWorld() || !blocker || blocker == target || blocker->GetOwnerEntity() == target )
		return 0.0f;
	if ( FClassnameIs( blocker, "func_door*" ) || FClassnameIs( blocker, "prop_door*" ) ||
		FClassnameIs( blocker, "func_breakable" ) || FClassnameIs( blocker, "func_movelinear" ) )
		return 0.0f;
	UTIL_TraceLine( source, point, CONTENTS_SOLID, NULL, COLLISION_GROUP_NONE, &trace );
	if ( trace.fraction != 1.0f )
		return 0.0f;
	IPhysicsObject *physics = blocker->VPhysicsGetObject();
	if ( !physics )
		return 0.75f;
	float density, thickness, friction, elasticity;
	physprops->GetPhysicsProperties( physics->GetMaterialIndex(), &density, &thickness, &friction, &elasticity );
	const float blocked = density * ( 1.0f / 3000.0f );
	if ( blocked < 0.0f )
		return 1.0f;
	if ( blocked < 1.0f )
		return 1.0f - blocked;
	return 0.0f;
}

// S:0x23D060. Source first is the actual original helper ABI.
double CTerrorGameRules::GetAmountOfEntityVisible( const Vector &source, CBaseEntity *entity )
{
	CBaseCombatCharacter *combat = entity->MyCombatCharacterPointer();
	if ( combat && combat->IsGhost() )
		return 0.0f;
	if ( !entity->IsPlayer() )
		return GetAmountOfPointVisible( source, entity->BodyTarget( source ), entity );
	const Vector origin = entity->GetAbsOrigin();
	const Vector body = entity->BodyTarget( source, false );
	Vector right;
	AngleVectors( entity->GetAbsAngles(), NULL, &right, NULL );
	NormalizeBlastDirection( right );
	right *= 16.0f;
	Vector leftPoint = origin - right;
	Vector rightPoint = origin + right;
	const float bodyHeight = body.z - origin.z;
	leftPoint.z = rightPoint.z = origin.z + bodyHeight;
	// S23D2C2..23D32F spills the first four sums, but returns the fifth wide.
	float visible = static_cast< float >( static_cast< double >( GetAmountOfPointVisible( source, body, entity ) ) * 0.4f );
	visible = static_cast< float >( static_cast< double >( GetAmountOfPointVisible( source, origin + Vector( 0, 0, 71 ), entity ) ) * 0.2f + visible );
	visible = static_cast< float >( static_cast< double >( GetAmountOfPointVisible( source, origin, entity ) ) * 0.2f + visible );
	visible = static_cast< float >( static_cast< double >( GetAmountOfPointVisible( source, leftPoint, entity ) ) * 0.1f + visible );
	return static_cast< double >( GetAmountOfPointVisible( source, rightPoint, entity ) ) * 0.1f + visible;
}

// S:0x23E700 -> S:0x23D340 with exposure enabled. Every accepted target
// goes through DispatchTraceAttack, including a clear final trace.
void CTerrorGameRules::RadiusDamage( const CTakeDamageInfo &info, const Vector &inputSource, float radius, int classIgnore, CBaseEntity *entityIgnore )
{
	Vector source = inputSource;
	source.z += 1.0f;
	const float falloff = radius == 0.0f ? 1.0f : info.GetDamage() / radius;
	int victimIndex = 0;
	for ( CEntitySphereQuery sphere( source, radius ); sphere.GetCurrentEntity(); sphere.NextEntity() )
	{
		CBaseEntity *entity = sphere.GetCurrentEntity();
		if ( entity == entityIgnore || entity->m_takedamage == DAMAGE_NO ||
			( classIgnore != CLASS_NONE && entity->Classify() == classIgnore ) )
			continue;
		const Vector spot = entity->BodyTarget( source, true );
		const double visibility = GetAmountOfEntityVisible( source, entity );
		const float visible = static_cast< float >( visibility );
		if ( !( visibility > 0.0 ) )
			continue;
		Vector direction = spot - source;
		const float distance = sqrtf( ( direction.y * direction.y + direction.z * direction.z ) + direction.x * direction.x );
		const float damage = ( info.GetDamage() - distance * falloff ) * visible;
		if ( !( damage > 0.0f ) )
			continue;
		CTakeDamageInfo adjusted = info;
		adjusted.SetDamage( damage );
		adjusted.SetDamageRadius( radius );
		NormalizeBlastDirection( direction );
		if ( adjusted.GetDamageForce() == vec3_origin || adjusted.GetDamagePosition() == vec3_origin )
			CalculateExplosiveDamageForce( &adjusted, direction, source, phys_explosion_force.GetFloat() );
		else
		{
			const Vector &force = adjusted.GetDamageForce();
			const float forceLength = sqrtf( ( force.x * force.x + force.z * force.z ) + force.y * force.y );
			adjusted.SetDamageForce( direction * forceLength );
			adjusted.SetDamagePosition( source );
		}
		trace_t trace;
		UTIL_TraceLine( source, entity->BodyTarget( source, false ), 0x46004003u, NULL, COLLISION_GROUP_NONE, &trace );
		adjusted.SetDamageVictimIndex( victimIndex++ );
		if ( !IsRoundInProgress() && IsSurvivalMode() )
		{
			adjusted.AddDamageType( 0x800 );
			adjusted.ScaleDamageForce( 0.0f );
		}
		ClearMultiDamage();
		entity->DispatchTraceAttack( adjusted, direction, &trace );
		ApplyMultiDamage();
		if ( radius_damage_show_hits.GetBool() )
			NDebugOverlay::Cross3D( trace.endpos, 16.0f, 0, 255, 0, true, 1.0f );
		entity->TraceAttackToTriggers( adjusted, source, spot, direction );
	}
}

float CTerrorGameRules::FlPlayerFallDamage( CBasePlayer *basePlayer )
{
	CTerrorPlayer *player = ToTerrorPlayer( basePlayer );
	if ( !player )
		return BaseClass::FlPlayerFallDamage( basePlayer );
	CTerrorPlayer *groundPlayer = ToTerrorPlayer( player->GetGroundEntity() );
	if ( groundPlayer && player->GetTeamNumber() == TEAM_INFECTED &&
		( groundPlayer->GetTeamNumber() == TEAM_SURVIVOR || groundPlayer->GetTeamNumber() == 4 ) )
	{
		static ConVarRef pitchMin( "z_splat_survivor_pitch_min" );
		static ConVarRef pitchMax( "z_splat_survivor_pitch_max" );
		static ConVarRef yawMin( "z_splat_survivor_yaw_min" );
		static ConVarRef shakeAmplitude( "z_splat_survivor_shake_amplitude" );
		static ConVarRef shakeFrequency( "z_splat_survivor_shake_frequency" );
		static ConVarRef shakeDuration( "z_splat_survivor_shake_duration" );
		static ConVarRef shakeRadius( "z_splat_survivor_shake_radius" );
		const float pitch = random->RandomFloat( pitchMin.GetFloat(), pitchMax.GetFloat() );
		const float yaw = random->RandomFloat( yawMin.GetFloat(), yawMin.GetFloat() );
		QAngle punch( pitch, yaw, 0 );
		groundPlayer->SetPunchAngle( punch );
		UTIL_ScreenShakeObject( groundPlayer, groundPlayer->GetAbsOrigin(), shakeAmplitude.GetFloat(), shakeFrequency.GetFloat(),
			shakeDuration.GetFloat(), shakeRadius.GetFloat(), SHAKE_START, false );
		groundPlayer->SpeakIfAllowed( "PlayerShoved" );
	}
	if ( !player->IsGhost() )
		player->EmitSound( "Player.StopVoice" );
	player->m_Local.m_flFallVelocity -= fall_speed_safe.GetFloat();
	const float speed = player->m_Local.m_flFallVelocity;
	// S2DECD7..2DECF7 is SSE float arithmetic, unlike prediction's S2D9130.
	float damage = 0.0f;
	if ( !( speed < 0.0f ) )
	{
		const float ratio = speed / ( fall_speed_fatal.GetFloat() - fall_speed_safe.GetFloat() );
		damage = ( ratio * ratio ) * 100.0f;
	}
	if ( player->GetTeamNumber() == TEAM_INFECTED )
	{
		player->OnLand( damage );
		return 0.0f;
	}
	if ( !( damage > 0.0f ) )
		return 0.0f;
	player->OnLand( damage );
	IGameEvent *event = gameeventmanager->CreateEvent( "player_falldamage" );
	if ( event )
	{
		event->SetInt( "userid", player->GetUserID() );
		event->SetFloat( "damage", damage );
		if ( player->GetLedgeCauser() && player->GetLedgeCauser()->IsPlayer() )
			event->SetInt( "causer", ToBasePlayer( player->GetLedgeCauser() )->GetUserID() );
		else if ( player->GetTongueReleaseSource() )
			event->SetInt( "causer", ToBasePlayer( player->GetTongueReleaseSource() )->GetUserID() );
		else if ( player->IsTongueVictim() )
			event->SetInt( "causer", player->GetTongueOwner()->GetUserID() );
		else if ( player->GetDisplacingEntity() && player->GetDisplacingEntity()->IsPlayer() )
			event->SetInt( "causer", ToBasePlayer( player->GetDisplacingEntity() )->GetUserID() );
		event->SetInt( "priority", 4 );
		gameeventmanager->FireEvent( event );
	}
	return damage;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:0x2DC670: saved death-event population, not event firing or a kill.
void CTerrorGameRules::PopulateDeathEvent( KeyValues *event, CBaseEntity *attacker, CBaseCombatCharacter *victim, const CTakeDamageInfo &info )
{
	CBasePlayer *playerAttacker = ToBasePlayer( attacker );
	CTerrorPlayer *playerVictim = ToTerrorPlayer( victim );
	CBaseEntity *inflictor = info.GetInflictor();
	CBaseEntity *damageAttacker = info.GetAttacker();
	CBasePlayer *killer = damageAttacker && damageAttacker->Classify() == CLASS_PLAYER ? ToBasePlayer( damageAttacker ) : NULL;
	const char *weaponName = "world";
	bool headshot = false;
	if ( killer )
	{
		event->SetInt( "attacker", killer->GetUserID() );
		headshot = ( info.GetDamageType() & 0x40000000u ) != 0;
		if ( inflictor == killer )
		{
			if ( CBaseCombatWeapon *weapon = killer->GetActiveWeapon() )
				weaponName = weapon->GetClassname();
		}
		else if ( inflictor )
		{
			CTerrorPlayer *playerInflictor = ToTerrorPlayer( inflictor );
			weaponName = playerInflictor && playerInflictor->m_zombieClass == 2 ? "boomer" : inflictor->GetClassname();
		}
	}
	else
	{
		if ( !inflictor || !damageAttacker )
			return;
		weaponName = inflictor->GetClassname();
		event->SetInt( "attacker", 0 );
		event->SetInt( "attackerentid", damageAttacker->entindex() );
	}
	if ( !Q_strncmp( weaponName, "weapon_", 7 ) )
		weaponName += 7;
	else if ( !Q_strncmp( weaponName, "func_", 5 ) )
		weaponName += 5;
	else if ( !Q_strncmp( weaponName, "pipe_bomb", 9 ) )
		weaponName = "pipe_bomb";
	if ( !Q_strcmp( weaponName, "pistol" ) && killer )
	{
		CTerrorGun *gun = dynamic_cast< CTerrorGun * >( killer->GetActiveWeapon() );
		if ( gun && gun->m_isDualWielding )
			weaponName = "dual_pistols";
	}
	event->SetString( "weapon", weaponName );
	event->SetInt( "headshot", headshot );
	event->SetInt( "type", info.GetDamageType() );
	event->SetInt( "priority", headshot + 7 );
	if ( playerVictim )
		event->SetInt( "abort", playerVictim->m_deathEventAborted );
	const Vector center = victim->WorldSpaceCenter();
	event->SetFloat( "victim_x", center.x );
	event->SetFloat( "victim_y", center.y );
	event->SetFloat( "victim_z", center.z );
	event->SetString( "victimname", "" );
	event->SetInt( "attackerisbot", !playerAttacker || playerAttacker->IsBot() );
	static const char *const names[] = { "Infected", "Smoker", "Boomer", "Hunter", "Spitter", "Jockey", "Charger", "Witch", "Tank" };
	if ( attacker && attacker->GetTeamNumber() == TEAM_INFECTED )
	{
		CBaseCombatCharacter *combat = attacker->MyCombatCharacterPointer();
		int zombieClass = combat ? combat->GetZombieClass() : 0;
		if ( zombieClass < 0 || zombieClass >= ARRAYSIZE( names ) )
			zombieClass = 0;
		event->SetString( "attackername", names[zombieClass] );
	}
	if ( victim->GetTeamNumber() == TEAM_INFECTED )
	{
		int zombieClass = victim->GetZombieClass();
		if ( zombieClass < 0 || zombieClass >= ARRAYSIZE( names ) )
			zombieClass = 0;
		event->SetInt( "victimisbot", !playerVictim || playerVictim->IsBot() );
		event->SetString( "victimname", names[zombieClass] );
	}
}

// Whole native S2DF540 / S2D9760 / S2D9790; names reuse the existing damage owner.
// Original offsets/RVAs below are provenance only, never target layout.

void CTerrorGameRules::RecordSurvivorDamage( CTerrorPlayer *attacker, int damage, bool assist )
{
	if ( !attacker || attacker->GetZombieClass() < 0 || attacker->GetZombieClass() >= 9 )
		return;
	const int zombieClass = attacker->GetZombieClass();
	if ( assist )
		m_survivorAssistDamageByClass[zombieClass] = static_cast< int >( static_cast< unsigned int >( m_survivorAssistDamageByClass[zombieClass] ) + static_cast< unsigned int >( damage ) );
	else
		m_survivorDirectDamageByClass[zombieClass] = static_cast< int >( static_cast< unsigned int >( m_survivorDirectDamageByClass[zombieClass] ) + static_cast< unsigned int >( damage ) );
	// Genuine reached S2DE5D0: preserve the subsequent independent chapter wire.
	// GetDamageScoreTeamIndex is the sole M6-owned provider246B:121-124, not a new mode policy.
	const int teamIndex = GetDamageScoreTeamIndex();
	m_iChapterDamage.Set( teamIndex, static_cast< int >( static_cast< unsigned int >( m_iChapterDamage[teamIndex] ) + static_cast< unsigned int >( damage ) ) );
}

int CTerrorGameRules::GetSurvivorDamageByClass( int zombieClass, bool assist ) const
{
	if ( static_cast< unsigned int >( zombieClass ) > 8u )
		return 0;
	if ( assist )
		return m_survivorAssistDamageByClass[zombieClass];
	return m_survivorDirectDamageByClass[zombieClass];
}

void CTerrorGameRules::ResetSurvivorDamageByClass()
{
	// S2D9790: the native body alternates the two real nine-int arrays.
	m_survivorDirectDamageByClass[0] = 0;
	m_survivorAssistDamageByClass[0] = 0;
	m_survivorDirectDamageByClass[1] = 0;
	m_survivorAssistDamageByClass[1] = 0;
	m_survivorDirectDamageByClass[2] = 0;
	m_survivorAssistDamageByClass[2] = 0;
	m_survivorDirectDamageByClass[3] = 0;
	m_survivorAssistDamageByClass[3] = 0;
	m_survivorDirectDamageByClass[4] = 0;
	m_survivorAssistDamageByClass[4] = 0;
	m_survivorDirectDamageByClass[5] = 0;
	m_survivorAssistDamageByClass[5] = 0;
	m_survivorDirectDamageByClass[6] = 0;
	m_survivorAssistDamageByClass[6] = 0;
	m_survivorDirectDamageByClass[7] = 0;
	m_survivorAssistDamageByClass[7] = 0;
	m_survivorDirectDamageByClass[8] = 0;
	m_survivorAssistDamageByClass[8] = 0;
}

// S2D9800 / static registration S599C10. No script/save/wire exposure is added.
CON_COMMAND_F( pz_damages, "List player zombie damages", 0x00004004 )
{
	CTerrorGameRules *rules = TerrorGameRules();
	if ( !rules )
		return;
	int assist = rules->GetSurvivorDamageByClass( 1, true );
	int direct = rules->GetSurvivorDamageByClass( 1, false );
	Msg( " Smoker direct damage = %d  assist damage = %d\n", direct, assist );
	rules = TerrorGameRules();
	assist = rules->GetSurvivorDamageByClass( 3, true );
	direct = rules->GetSurvivorDamageByClass( 3, false );
	Msg( " Hunter direct damage = %d  assist damage = %d\n", direct, assist );
	rules = TerrorGameRules();
	assist = rules->GetSurvivorDamageByClass( 2, true );
	direct = rules->GetSurvivorDamageByClass( 2, false );
	Msg( " Boomer direct damage = %d  assist damage = %d\n", direct, assist );
	rules = TerrorGameRules();
	assist = rules->GetSurvivorDamageByClass( 8, true );
	direct = rules->GetSurvivorDamageByClass( 8, false );
	Msg( " Tank direct damage = %d  assist damage = %d\n", direct, assist );
}

// S:0x2D91E0 copies only the native flat string/int/float death payload.
static void CopyTerrorDeathEventValues( IGameEvent *event, KeyValues *values )
{
	for ( KeyValues *value = values->GetFirstValue(); value; value = value->GetNextValue() )
	{
		switch ( value->GetDataType() )
		{
		case KeyValues::TYPE_STRING:
			event->SetString( value->GetName(), value->GetString() );
			break;
		case KeyValues::TYPE_INT:
			event->SetInt( value->GetName(), value->GetInt() );
			break;
		case KeyValues::TYPE_FLOAT:
			event->SetFloat( value->GetName(), value->GetFloat() );
			break;
		default:
			break;
		}
	}
}

// Native2D91E0, shared by player23D850 and generic-entity23C330 wrappers.
void CTerrorGameRules::PopulateDeathNotice( IGameEvent *event, CBaseEntity *scorer,
	CBaseCombatCharacter *victim, const CTakeDamageInfo &info )
{
	CTerrorPlayer *player = ToTerrorPlayer( victim );
	if ( player && player->m_deathEvent )
	{
		CopyTerrorDeathEventValues( event, player->m_deathEvent );
	}
	else
	{
		KeyValues *values = new KeyValues( "death" );
		PopulateDeathEvent( values, scorer, victim, info );
		CopyTerrorDeathEventValues( event, values );
		values->deleteThis();
	}
}

// Existing native player wrapper23D850. Keep its userid and broadcast
// behavior; do not turn every player death into the NPC entityid wrapper.
void CTerrorGameRules::DeathNotice( CBaseCombatCharacter *victim, const CTakeDamageInfo &info )
{
	CBasePlayer *scorer = GetDeathScorer( info.GetAttacker(), info.GetInflictor(), victim );
	IGameEvent *event = gameeventmanager->CreateEvent( "player_death" );
	if ( !event )
		return;
	event->SetInt( "userid", engine->GetPlayerUserId( victim->edict() ) );
	PopulateDeathNotice( event, scorer, victim, info );
	gameeventmanager->FireEvent( event );
}

// Native23C330, called FIRST from4AA230. This is server-only and uses
// entityid, not a player userid or an invented common-specific event name.
void CTerrorGameRules::DeathNoticeForEntity( CBaseCombatCharacter *victim, const CTakeDamageInfo &info )
{
	CBasePlayer *scorer = GetDeathScorer( info.GetAttacker(), info.GetInflictor(), victim );
	IGameEvent *event = gameeventmanager->CreateEvent( "player_death" );
	if ( !event )
		return;
	event->SetInt( "entityid", victim->entindex() );
	PopulateDeathNotice( event, scorer, victim, info );
	gameeventmanager->FireEvent( event, true );
}

// Native2DC380, called SECOND from4AA230. This replaces the obsolete
// Infected::FireDeathEvent producer, not a second side-channel notification.
void CTerrorGameRules::DeathNoticeForInfected( CBaseCombatCharacter *victim, const CTakeDamageInfo &info )
{
	IGameEvent *event = gameeventmanager->CreateEvent( "infected_death" );
	if ( !event )
		return;
	CBaseEntity *inflictor = info.GetInflictor();
	CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() );
	CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( info.GetWeapon() );
	bool minigun = false;
	if ( inflictor )
	{
		if ( FClassnameIs( inflictor, "prop_minigun" ) )
			minigun = true;
		if ( FClassnameIs( inflictor, "prop_minigun_l4d1" ) )
			minigun = true;
		if ( FClassnameIs( inflictor, "grenade_launcher_projectile" ) )
			event->SetInt( "weapon_id", 21 );
	}
	event->SetInt( "attacker", attacker ? attacker->GetUserID() : 0 );
	// Main-approved undefined-native policy: a non-CS generic combat weapon
	// would call beyond its native vtable at1588. Skip ONLY that overwrite;
	// retain any inflictor-set21. Never fabricate a zero ID for that case.
	if ( weapon )
		event->SetInt( "weapon_id", weapon->GetWeaponID() );

	// Actual canonical base gender owner is available for every entity class.
	if ( victim )
	{
		event->SetInt( "gender", victim->GetGender() );
		event->SetBool( "submerged", victim->GetWaterLevel() >= 1 );
	}
	event->SetBool( "headshot", ( (unsigned int)info.GetDamageType() & 0x40000000u ) != 0 );
	event->SetBool( "minigun", minigun );
	event->SetBool( "blast", ( info.GetDamageType() & DMG_BLAST ) != 0 );
	gameeventmanager->FireEvent( event );
}

// S:0x2D9340 deliberately delegates directly to the teamplay layer.
void CTerrorGameRules::PlayerKilled( CBasePlayer *victim, const CTakeDamageInfo &info )
{
	CTeamplayRules::PlayerKilled( victim, info );
}

// S:0x2E0480 is the genuine xor al,al;ret callback in Terror's vtable.
bool CTerrorGameRules::CheckWinConditions()
{
	return false;
}
