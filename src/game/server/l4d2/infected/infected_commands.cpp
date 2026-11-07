//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Existing-owner replacement: native z_spawn35B3C0 -> SpawnZombie35AF70.
// No entity is replaced by a generic NPC, fakeclient, or Director-only request.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/nav/terror_nav_area.h"
#include "gamerules.h"
#include "nav_mesh.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar z_spawn_height( "z_spawn_height", "12", 0x00004004, "Height above ground z_spawn places new infected" );
ConVar z_spawn_const_distance( "z_spawn_const_distance", "-1", 0x00004004, "If positive, it's the distance from the player where z_spawn spawns a character (as opposed to raycasting to the nearest obstacle)" );
ConVar z_prevent_burrowing( "z_prevent_burrowing", "1", 0x00004004, "Use extra tracelines to prevent burrowing zombies" );
ConVar z_debug_tank_spawn( "z_debug_tank_spawn", "1", 0x00004004, "When console spawning a tank automatically give it to a player." );
ConVar z_spawn_health( "z_spawn_health", "0", 0x00004004, "If non-0, health given to a zombie spawned with z_spawn" );
extern ConVar tank_raffle_debug;

namespace
{
const unsigned int s_SpawnSolidMask = 0x0202420Bu;


TerrorNavArea *FindCommandSpawnArea( const Vector &position )
{
	return ToTerrorNavArea( TheNavMesh->GetNearestNavArea( position, false, 10000.0f, false, true, 0 ) );
}

// S35A700/359CB0, with the native caller's constant mode0 (human-only).
// The existing ForEachPlayer preserves index1..maxClients, valid edict,
// IsPlayer and IsConnected gates. There is no bot-registry or alive-only scan.
class CFindCommandInfectedPlayer
{
public:
	explicit CFindCommandInfectedPlayer( int zombieClass ) : m_player( NULL ), m_zombieClass( zombieClass ) {}
	bool operator()( CBasePlayer *base )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		if ( ( player->IsAlive() && !player->IsGhost() ) ||
			player->GetTeamNumber() != TEAM_INFECTED ||
			( player->IsObserver() && player->State_Get() == STATE_DEATH_ANIM ) ||
			player->IsBot() || !player->IsZombieClass( m_zombieClass ) ||
			player->GetDeathTime() > gpGlobals->curtime + 1.0f )
			return true;
		m_player = player;
		return false;
	}
	CTerrorPlayer *m_player;
private:
	int m_zombieClass;
};
} // namespace
// S350C20. "common" and other unknown strings return9, not a new alias0;
// both still enter the real common branch of the native materializer.
int ZombieClassFromName( const char *name )
{
	if ( !Q_stricmp( "Tank", name ) ) return 8;
	if ( !Q_stricmp( "Spitter", name ) ) return 4;
	if ( !Q_stricmp( "Hunter", name ) ) return 3;
	if ( !Q_stricmp( "Infected", name ) ) return 0;
	if ( !Q_stricmp( "Witch", name ) ) return 7;
	if ( !Q_stricmp( "Boomer", name ) || !Q_stricmp( "Exploding", name ) ) return 2;
	if ( !Q_stricmp( "Smoker", name ) || !Q_stricmp( "Gas", name ) ) return 1;
	if ( !Q_stricmp( "Charger", name ) ) return 6;
	return !Q_stricmp( "Jockey", name ) ? 5 : 9;
}

// S35AF70. Source-level signature established from the caller's nine stack
// words plus classECX; the old inferred usercall's EBX/EDI/ESI are NOT inputs.
bool SpawnZombie( int zombieClass, Vector position, QAngle angles,
	int wound, bool ragdoll, bool inArea )
{
	const float height = z_spawn_height.GetFloat();
	if ( zombieClass == 10 )
	{
		TheZombieManager->SpawnMob( TheDirector->GetCommonLimit() );
		return true;
	}

	// Native performs two separate real GetViewVectors queries, maximum first.
	const Vector &hullMax = g_pGameRules->GetViewVectors()->m_vHullMax;
	const Vector &hullMin = g_pGameRules->GetViewVectors()->m_vHullMin;
	trace_t trace;
	UTIL_TraceHull( position, position, hullMin, hullMax, s_SpawnSolidMask,
		NULL, COLLISION_GROUP_NONE, &trace );
	TerrorNavArea *area = FindCommandSpawnArea( position );
	if ( ( trace.startsolid || trace.fraction < 1.0f ) && area )
	{
		position = area->GetRandomSpawnPoint();
		position.z += height;
	}

	if ( zombieClass == 7 || zombieClass == 11 )
	{
		if ( ragdoll )
			Warning( "ragdoll flag is not implemented\n" ); // Native literal/behavior.
		g_bZombieSpawnBypass = true;
		Witch *witch = zombieClass == 7 ?
			TheZombieManager->SpawnWitch( position, angles ) :
			TheZombieManager->SpawnWitchBride( position, angles );
		g_bZombieSpawnBypass = false;
		return witch != NULL;
	}

	if ( zombieClass != 8 && ( zombieClass < 1 || zombieClass > 6 ) )
	{
		g_bZombieSpawnBypass = true;
		Infected *infected = TheZombieManager->SpawnCommonZombie( area, position, ZombieManager::WANDERER );
		if ( infected )
		{
			g_bZombieSpawnBypass = false;
			if ( wound >= 0 )
			{
				Infected *actor = infected->MyInfectedPointer();
				if ( actor )
				{
					actor->m_gibbedLimbForce = vec3_origin;
					actor->SetRequestedWound( wound );
				}
			}
			if ( ragdoll )
			{
				CTakeDamageInfo info( infected, infected, 10000.0f, DMG_CRUSH );
				infected->BecomeRagdoll( info, Vector( 0.0f, 0.0f, 1.0f ) );
			}
			return true;
		}

		// Exactly one second area candidate. Native does NOT apply wound or
		// ragdoll to success on this second path; do not repair that asymmetry.
		area = FindCommandSpawnArea( position );
		if ( !area )
		{
			g_bZombieSpawnBypass = false;
			return false;
		}
		position = area->GetRandomSpawnPoint();
		position.z += height;
		infected = TheZombieManager->SpawnCommonZombie( area, position, ZombieManager::WANDERER );
		g_bZombieSpawnBypass = false;
		return infected != NULL;
	}

	if ( ragdoll )
		Warning( "ragdoll flag is not implemented" ); // Native has no newline here.
	CFindCommandInfectedPlayer findPlayer( zombieClass );
	ForEachPlayer( findPlayer );
	CTerrorPlayer *player = findPlayer.m_player;
	if ( player )
		player->State_Transition( STATE_OBSERVER_MODE );

	// Even the human rejected by these cvars has ALREADY transitioned6.
	if ( !player || ( !z_debug_tank_spawn.GetBool() && zombieClass == 8 ) || tank_raffle_debug.GetBool() )
	{
		g_bZombieSpawnBypass = true;
		CTerrorPlayer *spawned;
		if ( zombieClass == 8 )
		{
			spawned = area && inArea ? TheZombieManager->SpawnTankInArea( area, angles, false ) :
				TheZombieManager->SpawnTank( position, angles );
			TheDirector->OfferTank( spawned, true ); // Native also calls on NULL.
		}
		else
		{
			spawned = area && inArea ? TheZombieManager->SpawnSpecialInArea( zombieClass, area, angles ) :
				TheZombieManager->SpawnSpecial( zombieClass, position, angles );
		}
		g_bZombieSpawnBypass = false;
		return spawned != NULL;
	}

	player->State_Transition( STATE_ACTIVE );
	player->SetAbsOrigin( position );
	player->SetAbsAngles( angles );
	TheZombieManager->SetSpecialSpawnPosition( player->GetAbsOrigin() );
	g_iNextZombieClass = zombieClass;
	player->m_bReusingInfectedSpawnState = true;     // Native16373.
	player->m_reusedInfectedSpawnHealth = -1;       // Native16376 integer.
	player->m_reusedInfectedRecharge = -1.0f;       // Native16380 float.
	// Native16384 m_reusedInfectedRechargeDuration is retained.
	player->Spawn();
	if ( z_spawn_health.GetInt() > 0 )
		player->SetHealth( z_spawn_health.GetInt() );
	player->Teleport( &position, &angles, &vec3_origin );
	TheZombieManager->ClearSpecialSpawnPosition();
	// Native does not clear g_iNextZombieClass or the bypass flag on this path.
	return true;
}

CON_COMMAND_F( z_spawn, "<tank|boomer|smoker|witch|hunter|spitter|jockey|charger|mob|common> <auto> <ragdoll> <area>.  Spawns the specified zombie(s) under your cursor, or out in the world/in the targetted nav area if auto or area is specified.", 0x00004004 )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player )
		return;
	const float height = z_spawn_height.GetFloat();
	Vector forward;
	player->EyeVectors( &forward );
	const Vector end = player->EyePosition() + forward * 10000.0f;
	trace_t trace;
	UTIL_TraceLine( player->EyePosition(), end, s_SpawnSolidMask, player, COLLISION_GROUP_NONE, &trace );
	const float distance = z_spawn_const_distance.GetFloat();
	if ( distance >= 0.0f ) // Native COMISS/JB rejects unordered values here.
	{
		trace.endpos = player->EyePosition() + forward * distance;
		trace.fraction = z_spawn_const_distance.GetFloat() * 0.0001f;
	}
	if ( trace.fraction == 1.0f )
		return;

	QAngle angles( 0.0f, RandomFloat( 0.0f, 360.0f ), 0.0f );
	Vector position = trace.endpos;
	position.z += height;
	const char *name = "Common";
	int zombieClass = 0;
	int wound = -1;
	if ( args.ArgC() >= 2 )
	{
		name = args[1];
		if ( !Q_stricmp( name, "mob" ) )
		{
			// The native mob shortcut precedes option parsing and overrides.
			SpawnZombie( 10, position, angles, -1, false, false );
			return;
		}
		zombieClass = ZombieClassFromName( name );
		if ( args.ArgC() >= 4 && !Q_stricmp( args[2], "wound" ) )
			wound = atoi( args[3] );
	}

	bool ragdoll = false;
	bool inArea = false;
	for ( int i = 2; i < args.ArgC(); ++i )
	{
		// Native's accumulating auto local has NO post-loop reader. "auto"
		// remains admitted but does not pick an automatic position at35B3C0.
		// Its write-only accumulator/comparisons are elided, not implemented
		// as a new request to Director or GetRandomPZSpawnPosition.
		ragdoll = ragdoll || !Q_stricmp( args[i], "ragdoll" );
		inArea = inArea || !Q_stricmp( args[i], "area" );
	}
	if ( g_bZombieSpawnPositionOverride )
		position = g_vecZombieSpawnPosition;
	if ( g_bZombieSpawnAngleOverride )
		angles.Init( 0.0f, g_flZombieSpawnAngle, 0.0f );
	if ( !Q_stricmp( name, "witch_bride" ) )
		zombieClass = 11;
	SpawnZombie( zombieClass, position, angles, wound, ragdoll, inArea );
}

CON_COMMAND_F( z_spawn_const_pos, "z_spawn will spawn new zombies at the x y z coordinates specified. Pass no arguments to disable.", 0x00004004 )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player || !player->IsPlayer() )
		return;
	if ( args.ArgC() > 1 )
	{
		g_vecZombieSpawnPosition.Init();
		g_bZombieSpawnPositionOverride = true;
		g_vecZombieSpawnPosition.x = static_cast< float >( atof( args[1] ) );
		if ( args.ArgC() > 2 )
			g_vecZombieSpawnPosition.y = static_cast< float >( atof( args[2] ) );
		if ( args.ArgC() > 3 )
			g_vecZombieSpawnPosition.z = static_cast< float >( atof( args[3] ) );
		ClientPrint( player, HUD_PRINTCONSOLE, UTIL_VarArgs( "Constant z_spawn position set to %f, %f, %f.\n",
			g_vecZombieSpawnPosition.x, g_vecZombieSpawnPosition.y, g_vecZombieSpawnPosition.z ) );
	}
	else
	{
		ClientPrint( player, HUD_PRINTCONSOLE, "Constant z_spawn position disabled.\n" );
		g_bZombieSpawnPositionOverride = false;
	}
}

CON_COMMAND_F( z_spawn_const_ang, "z_spawn will spawn new zombies at the yaw angle specified. Pass no arguments to disable.", 0x00004004 )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player || !player->IsPlayer() )
		return;
	if ( args.ArgC() > 1 )
	{
		g_bZombieSpawnAngleOverride = true;
		const double angle = atof( args[1] );
		g_flZombieSpawnAngle = static_cast< float >( angle );
		ClientPrint( player, HUD_PRINTCONSOLE, UTIL_VarArgs( "Constant z_spawn angle set to %f.\n", angle ) );
	}
	else
	{
		ClientPrint( player, HUD_PRINTCONSOLE, "Constant z_spawn angle disabled.\n" );
		g_bZombieSpawnAngleOverride = false;
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#include "l4d2/infected/infected_commands_console_server_callbacks.inc"
