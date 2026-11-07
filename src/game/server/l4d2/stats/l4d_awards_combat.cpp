//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/l4d_awards_combat.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/infected/boss/witch.h"
#include "tier0/memdbgon.h"

namespace
{
CTerrorPlayer *AwardPlayer( IGameEvent *event, const char *key )
{
	return ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( key ) ) );
}

// A nonzero userid takes precedence, even when its player has disconnected.
CBaseEntity *AwardVictim( IGameEvent *event )
{
	int userID = event->GetInt( "userid" );
	if ( userID ) return UTIL_PlayerByUserId( userID );
	int entityID = event->GetInt( "entityid" );
	return entityID ? UTIL_EntityByIndex( entityID ) : NULL;
}

bool OwnerFirstSpawn( IGameEvent *event, CTerrorPlayer *owner )
{
	return !Q_stricmp( event->GetName(), "player_first_spawn" ) && AwardPlayer( event, "userid" ) == owner;
}

bool SpawnedInStartArea( CTerrorPlayer *player )
{
	const TerrorNavArea *area = ToTerrorNavArea( player->GetSpawnArea() );
	return area && area->HasSpawnAttributes( TERROR_NAV_PLAYER_START );
}

bool IsThrowableDamageWeapon( const char *weapon )
{
	return !Q_stricmp( weapon, "pipe_bomb" ) || !Q_stricmp( weapon, "molotov" ) || !Q_stricmp( weapon, "inferno" );
}
}

// S376AD0/376B10/376B90/376D80. "Ambush" is the original melee-kill template.
CAmbushAwardTemplate::CAmbushAwardTemplate( int userID ) : CAwardTemplate( userID ),
	m_kills( 0 ), m_failedMap( false ), m_startedInStartArea( false ), m_died( false )
{
	// S376D80 has no start-snapshot constructor store. The standing undefined-read rule skips that campaign award until real first-spawn writes it.
}

void CAmbushAwardTemplate::Init()
{
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "melee_kill" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "finale_win" );
	ListenForGameEvent( "player_death" );
}

void CAmbushAwardTemplate::FinishMap()
{
	if ( m_kills < 5 ) m_failedMap = true;
	else EarnAward( 23 );
	m_kills = 0;
}

void CAmbushAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "melee_kill" ) )
	{
		if ( AwardPlayer( event, "userid" ) == owner ) { EarnAward( 22 ); ++m_kills; }
	}
	else if ( !Q_stricmp( name, "map_transition" ) ) FinishMap();
	else if ( !Q_stricmp( name, "finale_win" ) )
	{
		FinishMap();
		if ( !m_died && !m_failedMap && m_startedInStartArea ) EarnAward( 24 );
	}
	else if ( OwnerFirstSpawn( event, owner ) )
	{
		m_kills = 0;
		m_failedMap = m_died = false;
		m_startedInStartArea = SpawnedInStartArea( owner );
	}
	else if ( !Q_stricmp( name, "player_death" ) && AwardPlayer( event, "userid" ) == owner ) m_died = true;
}

// S377110/377180/377510: same-curtime blast grouping, not a rolling kill window.
CBoomerComboAwardTemplate::CBoomerComboAwardTemplate( int userID ) : CAwardTemplate( userID ) { Reset(); }
void CBoomerComboAwardTemplate::Reset() { m_killTime = m_awardTime = 0.0f; m_kills = 0; }
void CBoomerComboAwardTemplate::Init()
{
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}
void CBoomerComboAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "map_transition" ) || OwnerFirstSpawn( event, owner ) ) { Reset(); return; }
	bool death = !Q_stricmp( name, "player_death" );
	if ( !death && Q_stricmp( name, "player_incapacitated" ) ) return;
	CBaseEntity *victim = AwardVictim( event );
	if ( !victim || AwardPlayer( event, "attacker" ) != owner || Q_stricmp( event->GetString( "weapon" ), "boomer" ) || m_awardTime == gpGlobals->curtime ) return;
	if ( death )
	{
		if ( victim->GetTeamNumber() == owner->GetTeamNumber() ) return;
		if ( !dynamic_cast< Witch * >( victim ) )
		{
			if ( m_killTime == gpGlobals->curtime ) ++m_kills;
			else { m_kills = 1; m_killTime = gpGlobals->curtime; }
			if ( m_kills < 5 ) return;
			EarnAward( 16 );
			m_kills = 0;
			m_killTime = m_awardTime = gpGlobals->curtime;
			return;
		}
	}
	else
	{
		CTerrorPlayer *player = event->GetInt( "userid" ) ? ToTerrorPlayer( victim ) : NULL;
		if ( !player || player->GetZombieClass() != 8 ) return;
	}
	EarnAward( 16 );
	m_kills = 0;
	m_killTime = 0.0f;
	m_awardTime = gpGlobals->curtime;
}

// S37A460/37A510/37AA70. Each projectile's hit is evaluated at the NEXT throw.
CGrenadeExpertAwardTemplate::CGrenadeExpertAwardTemplate( int userID ) : CAwardTemplate( userID ), m_startedInStartArea( false ) { Reset(); }
void CGrenadeExpertAwardTemplate::Reset()
{
	m_pipeBombs = m_molotovs = 0;
	m_pipeHurtFriend = m_pipeMissed = m_molotovHurtFriend = m_molotovMissed = m_died = false;
	m_pipeHitEnemy = m_molotovHitEnemy = true;
}
void CGrenadeExpertAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "infected_hurt" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "weapon_fire" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "finale_win" );
	ListenForGameEvent( "player_death" );
}
void CGrenadeExpertAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "weapon_fire" ) )
	{
		if ( AwardPlayer( event, "userid" ) != owner ) return;
		const char *weapon = event->GetString( "weapon" );
		if ( !Q_stricmp( weapon, "pipe_bomb" ) )
		{
			if ( !m_pipeHitEnemy ) m_pipeMissed = true;
			++m_pipeBombs; m_pipeHitEnemy = false;
		}
		else if ( !Q_stricmp( weapon, "molotov" ) || !Q_stricmp( weapon, "inferno" ) )
		{
			if ( !m_molotovHitEnemy ) m_molotovMissed = true;
			++m_molotovs; m_molotovHitEnemy = false;
		}
	}
	else if ( !Q_stricmp( name, "finale_win" ) )
	{
		if ( !m_died && m_startedInStartArea )
		{
			if ( m_pipeBombs >= 3 && !m_pipeHurtFriend && !m_pipeMissed && m_pipeHitEnemy ) EarnAward( 38 );
			if ( m_molotovs >= 3 && !m_molotovHurtFriend && !m_molotovMissed && m_molotovHitEnemy ) EarnAward( 39 );
		}
	}
	else if ( OwnerFirstSpawn( event, owner ) ) { Reset(); m_startedInStartArea = SpawnedInStartArea( owner ); }
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		if ( AwardPlayer( event, "userid" ) == owner ) m_died = true;
	}
	else if ( !Q_stricmp( name, "player_incapacitated" ) )
	{
		CBaseEntity *victim = AwardVictim( event );
		if ( !victim || AwardPlayer( event, "attacker" ) != owner ) return;
		bool friendly = victim->GetTeamNumber() == owner->GetTeamNumber();
		const char *weapon = event->GetString( "weapon" );
		if ( !Q_stricmp( weapon, "pipe_bomb" ) ) { if ( friendly ) m_pipeHurtFriend = true; else m_pipeHitEnemy = true; }
		else if ( !Q_stricmp( weapon, "molotov" ) || !Q_stricmp( weapon, "inferno" ) ) { if ( friendly ) m_molotovHurtFriend = true; else m_molotovHitEnemy = true; }
	}
	else if ( !Q_stricmp( name, "player_hurt" ) || !Q_stricmp( name, "infected_hurt" ) )
	{
		if ( AwardPlayer( event, "attacker" ) != owner ) return;
		bool infected = !Q_stricmp( name, "infected_hurt" );
		CTerrorPlayer *victim = infected ? NULL : AwardPlayer( event, "userid" );
		if ( !infected && !victim ) return;
		int type = event->GetInt( "type" );
		bool friendly = victim && victim->GetTeamNumber() == owner->GetTeamNumber();
		if ( ( type & 0x40 ) && !( type & 0x08000000 ) )
		{
			if ( friendly ) m_pipeHurtFriend = true;
			else m_pipeHitEnemy = true;
		}
		else if ( type & 8 )
		{
			if ( friendly ) { if ( !( type & 0x800 ) ) m_molotovHurtFriend = true; }
			else if ( !infected || !( type & 0x800 ) ) m_molotovHitEnemy = true;
		}
	}
}

// S37AB00/37ABA0/37B050. Hurt only recognizes pipe_bomb; incap also fire.
CGrenadierAwardTemplate::CGrenadierAwardTemplate( int userID ) : CAwardTemplate( userID ), m_kills( 0 ) {}
void CGrenadierAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "weapon_fire" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}
void CGrenadierAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "map_transition" ) || OwnerFirstSpawn( event, owner ) ) { m_kills = 0; return; }
	if ( !Q_stricmp( name, "weapon_fire" ) )
	{
		if ( AwardPlayer( event, "userid" ) == owner && IsThrowableDamageWeapon( event->GetString( "weapon" ) ) ) m_kills = 0;
		return;
	}
	bool death = !Q_stricmp( name, "player_death" );
	bool hurt = !Q_stricmp( name, "player_hurt" );
	if ( !death && !hurt && Q_stricmp( name, "player_incapacitated" ) ) return;
	CBaseEntity *victim = AwardVictim( event );
	if ( !victim || AwardPlayer( event, "attacker" ) != owner ) return;
	const char *weapon = event->GetString( "weapon" );
	bool earned = false;
	if ( death )
	{
		if ( victim->GetTeamNumber() != owner->GetTeamNumber() && IsThrowableDamageWeapon( weapon ) )
			earned = ++m_kills >= 10 || FClassnameIs( victim, "witch" );
	}
	else if ( ( hurt && !Q_stricmp( weapon, "pipe_bomb" ) ) || ( !hurt && IsThrowableDamageWeapon( weapon ) ) )
	{
		CTerrorPlayer *player = event->GetInt( "userid" ) ? ToTerrorPlayer( victim ) : NULL;
		earned = player && player->GetZombieClass() == 8;
	}
	if ( earned ) { EarnAward( 15 ); m_kills = 0; }
}

// S37B0D0/37B160/37B1D0/37B420.
CHeadhunterAwardTemplate::CHeadhunterAwardTemplate( int userID ) : CAwardTemplate( userID ),
	m_kills( 0 ), m_headshots( 0 ), m_failedMap( false ), m_lowerTierMap( false ), m_startedInStartArea( false ), m_died( false ) {}
void CHeadhunterAwardTemplate::Init()
{
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "finale_win" );
	ListenForGameEvent( "player_death" );
}
void CHeadhunterAwardTemplate::FinishMap()
{
	float ratio = static_cast< float >( m_headshots ) / m_kills;
	if ( m_kills < 30 || ratio < 0.25f ) m_failedMap = m_lowerTierMap = true;
	else if ( ratio >= 0.5f ) EarnAward( 35 );
	else { EarnAward( 34 ); m_lowerTierMap = true; }
	m_kills = m_headshots = 0;
}
void CHeadhunterAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "map_transition" ) ) FinishMap();
	else if ( OwnerFirstSpawn( event, owner ) )
	{
		m_kills = m_headshots = 0; m_failedMap = m_lowerTierMap = m_died = false;
		m_startedInStartArea = SpawnedInStartArea( owner );
	}
	else if ( !Q_stricmp( name, "finale_win" ) )
	{
		FinishMap();
		if ( !m_died && m_startedInStartArea && !m_failedMap ) EarnAward( m_lowerTierMap ? 36 : 37 );
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		CBaseEntity *victim = AwardVictim( event );
		if ( !victim ) return;
		if ( AwardPlayer( event, "attacker" ) == owner && victim->GetTeamNumber() != owner->GetTeamNumber() )
		{
			++m_kills; if ( event->GetBool( "headshot" ) ) ++m_headshots;
		}
		else if ( victim == owner ) m_died = true;
	}
}

// S37C080/37C0D0/37C2F0. Keep per-Tank damage, including repeated hurt callbacks.
CHulkSlayerAwardTemplate::CHulkSlayerAwardTemplate( int userID ) : CAwardTemplate( userID ) { V_memset( m_damage, 0, sizeof( m_damage ) ); }
void CHulkSlayerAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_spawn" );
}
void CHulkSlayerAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	CTerrorPlayer *victim = AwardPlayer( event, "userid" );
	if ( !victim || victim->GetZombieClass() != 8 || victim->entindex() >= ARRAYSIZE( m_damage ) ) return;
	int index = victim->entindex();
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_spawn" ) ) m_damage[index] = 0;
	else if ( AwardPlayer( event, "attacker" ) == owner )
	{
		if ( !Q_stricmp( name, "player_hurt" ) )
		{
			if ( event->GetInt( "type" ) != 0x20000 ) m_damage[index] += event->GetInt( "dmg_health" );
		}
		else if ( !Q_stricmp( name, "player_incapacitated" ) && static_cast< float >( m_damage[index] ) / victim->GetMaxHealth() >= 0.8f ) EarnAward( 17 );
	}
}

// S37C3D0/37C420/37C6C0: a real downward player hull sweep at jump apex.
CHurdleAwardTemplate::CHurdleAwardTemplate( int userID ) : CAwardTemplate( userID ) { m_cooldown.Invalidate(); }
void CHurdleAwardTemplate::Init()
{
	ListenForGameEvent( "player_jump_apex" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "map_transition" );
}
void CHurdleAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "map_transition" ) || OwnerFirstSpawn( event, owner ) ) { m_cooldown.Invalidate(); return; }
	if ( Q_stricmp( name, "player_jump_apex" ) || AwardPlayer( event, "userid" ) != owner || owner->IsIncapacitated() || ( m_cooldown.HasStarted() && !m_cooldown.IsElapsed() ) ) return;
	trace_t trace;
	const CViewVectors *view = g_pGameRules->GetViewVectors();
	UTIL_TraceHull( owner->GetAbsOrigin(), owner->GetAbsOrigin() - Vector( 0, 0, 50 ), view->m_vHullMin, view->m_vHullMax,
		owner->PhysicsSolidMaskForEntity(), owner, COLLISION_GROUP_NONE, &trace );
	CBaseCombatCharacter *actor = trace.m_pEnt ? trace.m_pEnt->MyCombatCharacterPointer() : NULL;
	if ( actor && ( actor->GetZombieClass() == 8 || actor->GetZombieClass() == 7 ) )
	{
		EarnAward( 49 ); m_cooldown.Start( 1.0f );
	}
}

// S37C770/37C810/37CAB0. no-longer-it intentionally does not inspect userid.
CItSurvivorAwardTemplate::CItSurvivorAwardTemplate( int userID ) : CAwardTemplate( userID ) { Reset(); }
void CItSurvivorAwardTemplate::Reset() { m_isIt = m_hurt = false; m_kills = 0; }
void CItSurvivorAwardTemplate::Init()
{
	ListenForGameEvent( "player_now_it" );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_no_longer_it" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}
void CItSurvivorAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_now_it" ) )
	{
		if ( AwardPlayer( event, "userid" ) == owner ) { m_isIt = true; m_hurt = false; m_kills = 0; }
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		if ( !m_isIt ) return;
		CBaseEntity *victim = AwardVictim( event );
		if ( victim && AwardPlayer( event, "attacker" ) == owner && victim->GetTeamNumber() != owner->GetTeamNumber() ) ++m_kills;
	}
	else if ( !Q_stricmp( name, "player_hurt" ) )
	{
		CTerrorPlayer *attacker = AwardPlayer( event, "attacker" );
		if ( m_isIt && AwardPlayer( event, "userid" ) == owner && ( !attacker || attacker->GetTeamNumber() != owner->GetTeamNumber() ) ) m_hurt = true;
	}
	else if ( !Q_stricmp( name, "player_no_longer_it" ) )
	{
		if ( !m_isIt ) return;
		if ( !m_hurt && m_kills >= 10 ) EarnAward( 14 );
		Reset();
	}
	else if ( !Q_stricmp( name, "map_transition" ) || OwnerFirstSpawn( event, owner ) ) Reset();
}

// S37D600/37D650/37D870. Votes are genuine player nominations, not kill scores.
CMVPAwardTemplate::CMVPAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
void CMVPAwardTemplate::Init()
{
	ListenForGameEvent( "finale_win" );
	ListenForGameEvent( "mission_lost" );
	ListenForGameEvent( "final_reportscreen" );
}
void CMVPAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR || Q_stricmp( event->GetName(), "final_reportscreen" ) ) return;
	CTerrorPlayer *nominee = owner->GetMVPNominee();
	if ( !nominee || nominee == owner ) return;
	int votes[32] = { 0 };
	int highest = -1, winningIndex = -1, ownerIndex = -1;
	bool tied = false;
	for ( int i = 0; i < ARRAYSIZE( votes ); ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		CTerrorPlayer *vote = player->GetMVPNominee();
		if ( vote && (unsigned int)vote->entindex() < ARRAYSIZE( votes ) ) // Native out-of-array vote step only is skipped.
		{
			int index = vote->entindex();
			int count = ++votes[index];
			if ( count > highest ) { highest = count; winningIndex = index; tied = false; }
			else if ( count == highest ) tied = true;
		}
		if ( player == owner ) ownerIndex = i;
	}
	if ( ownerIndex != -1 && ownerIndex == winningIndex && highest >= 2 && !tied ) EarnAward( 28 );
}

// S37DEB0/37DFC0/37DEF0. Native Init omits the reset events handled below.
CParryAwardTemplate::CParryAwardTemplate( int userID ) : CAwardTemplate( userID ) { m_lungeShoveTimer.Invalidate(); }
void CParryAwardTemplate::Init()
{
	ListenForGameEvent( "player_shoved" );
	ListenForGameEvent( "lunge_shove" );
}
void CParryAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "lunge_shove" ) )
	{
		if ( AwardPlayer( event, "victim" ) == owner ) m_lungeShoveTimer.Start( 1.0f );
	}
	else if ( !Q_stricmp( name, "player_shoved" ) )
	{
		CTerrorPlayer *victim = AwardPlayer( event, "userid" );
		if ( victim && AwardPlayer( event, "attacker" ) == owner && victim->GetZombieClass() == 3 && victim->IsLunging() && m_lungeShoveTimer.IsElapsed() ) EarnAward( 21 );
	}
	else if ( !Q_stricmp( name, "map_transition" ) || OwnerFirstSpawn( event, owner ) ) m_lungeShoveTimer.Invalidate();
}

// S37E4B0/37E640/37E590. Select the nearest visible survivor before testing need.
CProtectorAwardTemplate::CProtectorAwardTemplate( int userID ) : CAwardTemplate( userID ) { m_cooldown.Invalidate(); }
void CProtectorAwardTemplate::Init()
{
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}
void CProtectorAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	if ( !Q_stricmp( event->GetName(), "map_transition" ) || OwnerFirstSpawn( event, owner ) ) { m_cooldown.Invalidate(); return; }
	if ( Q_stricmp( event->GetName(), "player_death" ) || ( m_cooldown.HasStarted() && !m_cooldown.IsElapsed() ) ) return;
	CBaseEntity *victim = AwardVictim( event );
	if ( !victim || AwardPlayer( event, "attacker" ) != owner || victim->GetTeamNumber() == owner->GetTeamNumber() ) return;
	CTerrorPlayer *victimPlayer = ToTerrorPlayer( victim );
	if ( victimPlayer && victimPlayer->GetZombieClass() == 2 ) return;
	CBaseCombatCharacter *actor = victim->MyCombatCharacterPointer();
	if ( !actor ) return;
	CTerrorPlayer *nearest = NULL;
	float nearestDistance = -1.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() || !actor->IsInFieldOfView( player ) ) continue;
		float distance = actor->GetAbsOrigin().DistToSqr( player->GetAbsOrigin() );
		if ( distance <= 40000.0f && ( !nearest || nearestDistance > distance ) && actor->IsLineOfSightClear( player, CBaseCombatCharacter::IGNORE_NOTHING ) )
		{
			nearest = player; nearestDistance = distance;
		}
	}
	CBaseCombatWeapon *weapon = nearest ? nearest->GetActiveWeapon() : NULL;
	bool progressActive = nearest && nearest->m_flProgressBarDuration > 0.0f &&
		( ( nearest->GetTeamNumber() == TEAM_INFECTED && nearest->GetZombieClass() == 3 ) ||
		gpGlobals->curtime <= nearest->m_flProgressBarStartTime + nearest->m_flProgressBarDuration );
	if ( nearest && nearest != owner && ( nearest->m_isHangingFromTongue || nearest->IsHangingFromLedge() || progressActive || ( weapon && weapon->m_bInReload ) || !nearest->IsInFieldOfView( victim ) ) )
	{
		EarnAward( 67, nearest ); m_cooldown.Start( 2.0f );
	}
}

// S37F6B0/37F730/37F7E0/37FB60. One bullet hit per curtime, not per target/seed.
CSharpshooterAwardTemplate::CSharpshooterAwardTemplate( int userID ) : CAwardTemplate( userID ),
	m_shots( 0 ), m_hits( 0 ), m_hitTime( -1.0f ), m_failedMap( false ), m_lowerTierMap( false ), m_startedInStartArea( false ), m_died( false )
{
	// S37FB60 has no start-snapshot constructor store. Skip only dependent campaign award until real first-spawn writes it.
}
void CSharpshooterAwardTemplate::Init()
{
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "weapon_fire" );
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "infected_hurt" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "finale_win" );
	ListenForGameEvent( "player_death" );
}
void CSharpshooterAwardTemplate::FinishMap()
{
	float ratio = static_cast< float >( m_hits ) / m_shots;
	if ( m_shots >= 50 && ratio >= 0.75f )
	{
		if ( ratio >= 0.9f ) EarnAward( 11 );
		else { EarnAward( 10 ); m_lowerTierMap = true; }
	}
	else m_failedMap = m_lowerTierMap = true;
	m_shots = m_hits = 0; m_hitTime = -1.0f;
}
void CSharpshooterAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "map_transition" ) ) FinishMap();
	else if ( !Q_stricmp( name, "finale_win" ) )
	{
		FinishMap();
		if ( !m_died && m_startedInStartArea && !m_failedMap ) EarnAward( m_lowerTierMap ? 12 : 13 );
	}
	else if ( OwnerFirstSpawn( event, owner ) )
	{
		m_shots = m_hits = 0; m_hitTime = -1.0f; m_failedMap = m_lowerTierMap = m_died = false;
		m_startedInStartArea = SpawnedInStartArea( owner );
	}
	else if ( !Q_stricmp( name, "weapon_fire" ) )
	{
		const char *weapon = event->GetString( "weapon" );
		if ( AwardPlayer( event, "userid" ) == owner && Q_stricmp( weapon, "molotov" ) && Q_stricmp( weapon, "pipe_bomb" ) ) ++m_shots;
	}
	else if ( !Q_stricmp( name, "player_hurt" ) || !Q_stricmp( name, "infected_hurt" ) )
	{
		if ( !( event->GetInt( "type" ) & DMG_BULLET ) || AwardPlayer( event, "attacker" ) != owner ) return;
		if ( !Q_stricmp( name, "player_hurt" ) )
		{
			CTerrorPlayer *victim = AwardPlayer( event, "userid" );
			if ( !victim || victim->GetTeamNumber() == owner->GetTeamNumber() ) return;
		}
		if ( m_hitTime != gpGlobals->curtime ) { ++m_hits; m_hitTime = gpGlobals->curtime; }
	}
	else if ( !Q_stricmp( name, "player_death" ) && AwardPlayer( event, "userid" ) == owner ) m_died = true;
}

// S37FBF0/37FC10/37FE00. Air clearance, prior-damage interval, and range all apply.
CSkeetShootAwardTemplate::CSkeetShootAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
void CSkeetShootAwardTemplate::Init() { ListenForGameEvent( "player_death" ); }
void CSkeetShootAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR || Q_stricmp( event->GetName(), "player_death" ) ) return;
	CTerrorPlayer *victim = AwardPlayer( event, "userid" );
	if ( !victim || AwardPlayer( event, "attacker" ) != owner || victim->GetZombieClass() != 3 ) return;
	trace_t trace;
	const CViewVectors *view = g_pGameRules->GetViewVectors();
	UTIL_TraceHull( victim->GetAbsOrigin(), victim->GetAbsOrigin() - Vector( 0, 0, 30 ), view->m_vHullMin, view->m_vHullMax,
		0x0200400B, victim, COLLISION_GROUP_NONE, &trace );
	if ( trace.fraction >= 1.0f && victim->GetTimeSinceLastDamage() >= 2.0f && owner->GetAbsOrigin().DistTo( victim->GetAbsOrigin() ) >= 300.0f ) EarnAward( 20 );
}

// S37FE70/37FF00/37FF80/380260. All enemy deaths count, irrespective of weapon.
CSniperAwardTemplate::CSniperAwardTemplate( int userID ) : CAwardTemplate( userID ),
	m_kills( 0 ), m_longRangeKills( 0 ), m_failedMap( false ), m_lowerTierMap( false ), m_startedInStartArea( false ), m_died( false ) {}
void CSniperAwardTemplate::Init()
{
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "finale_win" );
	ListenForGameEvent( "player_death" );
}
void CSniperAwardTemplate::FinishMap()
{
	float ratio = static_cast< float >( m_longRangeKills ) / m_kills;
	if ( m_kills < 30 || ratio < 0.25f ) m_failedMap = m_lowerTierMap = true;
	else if ( ratio >= 0.5f ) EarnAward( 31 );
	else { EarnAward( 30 ); m_lowerTierMap = true; }
	m_kills = m_longRangeKills = 0;
}
void CSniperAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "map_transition" ) ) FinishMap();
	else if ( OwnerFirstSpawn( event, owner ) )
	{
		m_kills = m_longRangeKills = 0; m_failedMap = m_lowerTierMap = m_died = false;
		m_startedInStartArea = SpawnedInStartArea( owner );
	}
	else if ( !Q_stricmp( name, "finale_win" ) )
	{
		FinishMap();
		if ( !m_died && m_startedInStartArea && !m_failedMap ) EarnAward( m_lowerTierMap ? 32 : 33 );
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		CBaseEntity *victim = AwardVictim( event );
		if ( !victim ) return;
		if ( AwardPlayer( event, "attacker" ) == owner && victim->GetTeamNumber() != owner->GetTeamNumber() )
		{
			++m_kills;
			if ( owner->GetAbsOrigin().DistTo( victim->GetAbsOrigin() ) >= 600.0f ) ++m_longRangeKills;
		}
		else if ( victim == owner ) m_died = true;
	}
}

// S3807D0/380820/380C30. Native seven weapon strings, not weapon-type buckets.
CWeaponExpertAwardTemplate::CWeaponExpertAwardTemplate( int userID ) : CAwardTemplate( userID ), m_kills( 0 ) { V_memset( m_weaponKills, 0, sizeof( m_weaponKills ) ); }
void CWeaponExpertAwardTemplate::Init()
{
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "finale_win" );
	ListenForGameEvent( "player_first_spawn" );
}
void CWeaponExpertAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( OwnerFirstSpawn( event, owner ) ) { V_memset( m_weaponKills, 0, sizeof( m_weaponKills ) ); m_kills = 0; }
	else if ( !Q_stricmp( name, "finale_win" ) )
	{
		static const int awards[] = { 42, 43, 45, 44, 46, 47, 48 };
		for ( int i = 0; i < ARRAYSIZE( m_weaponKills ); ++i )
		{
			if ( m_weaponKills[i] >= 100 && static_cast< float >( m_weaponKills[i] ) / m_kills >= 0.8f ) { EarnAward( awards[i] ); return; }
		}
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		if ( AwardPlayer( event, "attacker" ) != owner ) return;
		CBaseEntity *victim = AwardVictim( event );
		if ( !victim || victim->GetTeamNumber() == owner->GetTeamNumber() ) return;
		static const char *weapons[] = { "pistol", "dual_pistols", "smg", "pumpshotgun", "rifle", "hunting_rifle", "autoshotgun" };
		for ( int i = 0; i < ARRAYSIZE( weapons ); ++i )
		{
			if ( !Q_stricmp( event->GetString( "weapon" ), weapons[i] ) ) { ++m_weaponKills[i]; ++m_kills; return; }
		}
	}
}

// S380CC0/380D00/380E30. tongue_pull_stopped compares both fields to the owner.
CWhiplashAwardTemplate::CWhiplashAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
void CWhiplashAwardTemplate::Init()
{
	ListenForGameEvent( "player_shoved" );
	ListenForGameEvent( "tongue_pull_stopped" );
}
void CWhiplashAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_shoved" ) )
	{
		CTerrorPlayer *victim = AwardPlayer( event, "userid" );
		if ( victim && AwardPlayer( event, "attacker" ) == owner && victim->GetZombieClass() == 1 && victim->GetTongueVictim() == owner ) EarnAward( 27 );
	}
	else if ( !Q_stricmp( name, "tongue_pull_stopped" ) && AwardPlayer( event, "userid" ) == owner && AwardPlayer( event, "victim" ) == owner ) EarnAward( 27 );
}

// S381000/381390/381410. Damage is shared across live Witches, as in the original.
CWitchSlayerAwardTemplate::CWitchSlayerAwardTemplate( int userID ) : CAwardTemplate( userID ) { Reset(); }
void CWitchSlayerAwardTemplate::Reset()
{
	m_damage = 0; m_firstDamageTime = 0.0f; m_pendingWitch = false; m_noMissedWitches = true;
	m_killedWitch = m_startedInStartArea = m_died = false;
}
void CWitchSlayerAwardTemplate::Init()
{
	ListenForGameEvent( "infected_hurt" );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "witch_spawn" );
	ListenForGameEvent( "player_first_spawn" );
	ListenForGameEvent( "finale_win" );
}
void CWitchSlayerAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR ) return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "infected_hurt" ) )
	{
		CBaseEntity *victim = UTIL_EntityByIndex( event->GetInt( "entityid" ) );
		CBaseCombatCharacter *actor = victim ? victim->MyCombatCharacterPointer() : NULL;
		if ( AwardPlayer( event, "attacker" ) == owner && actor && actor->GetZombieClass() == 7 )
		{
			m_damage += event->GetInt( "amount" );
			if ( m_firstDamageTime == 0.0f ) m_firstDamageTime = gpGlobals->curtime;
		}
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		CBaseEntity *victim = AwardVictim( event );
		if ( !victim ) return;
		CBaseCombatCharacter *actor = victim->MyCombatCharacterPointer();
		if ( actor && actor->GetZombieClass() == 7 )
		{
			float ratio = static_cast< float >( m_damage ) / victim->GetMaxHealth();
			if ( ratio >= 0.99f && gpGlobals->curtime == m_firstDamageTime ) { EarnAward( 19 ); m_killedWitch = true; }
			else if ( ratio >= 0.8f ) { EarnAward( 18 ); m_killedWitch = true; }
			m_damage = 0; m_firstDamageTime = 0.0f; m_pendingWitch = false;
		}
		else if ( victim == owner ) m_died = true;
	}
	else if ( !Q_stricmp( name, "witch_spawn" ) )
	{
		if ( m_pendingWitch ) m_noMissedWitches = false;
		m_damage = 0; m_firstDamageTime = 0.0f; m_pendingWitch = true;
	}
	else if ( !Q_stricmp( name, "finale_win" ) )
	{
		if ( !m_died && m_startedInStartArea && m_noMissedWitches && !m_pendingWitch && m_killedWitch ) EarnAward( 40 );
		Reset();
	}
	else if ( OwnerFirstSpawn( event, owner ) ) { Reset(); m_startedInStartArea = SpawnedInStartArea( owner ); }
}

// S3805A0/3806D0/380660. Shove a spraying Boomer while nobody is already IT.
CVomitStopAwardTemplate::CVomitStopAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
void CVomitStopAwardTemplate::Init() { ListenForGameEvent( "player_shoved" ); }
void CVomitStopAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *owner = GetPlayer();
	if ( !owner || owner->GetTeamNumber() != TEAM_SURVIVOR || Q_stricmp( event->GetName(), "player_shoved" ) ) return;
	CTerrorPlayer *victim = AwardPlayer( event, "userid" );
	if ( !victim || AwardPlayer( event, "attacker" ) != owner || victim->GetZombieClass() != 2 ) return;
	CBaseAbility *ability = victim->GetCustomAbility();
	if ( !ability || !ability->IsActive() ) return;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->IsConnected() && player->GetTeamNumber() == TEAM_SURVIVOR && player->IsIT() ) return;
	}
	EarnAward( 26 );
}
