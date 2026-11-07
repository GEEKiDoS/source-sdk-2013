//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/l4d_awards_faults.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/nav/terror_nav_path.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "EventLog.h"
#include "NextBot/NextBotManager.h"
#include "tier0/memdbgon.h"

extern ConVar survivor_limp_health;

// Original 252730 factory/vtables and callback bodies are recorded in
// native_award_vtables.txt and native_all_awards_raw.txt. All EarnAward calls
// below use the real CAwardTemplate mission gate and OnAwardEarned transport.
// No callback manufactures a second player_hurt, death, shove or use event.

// Integration request index (existing-source/VPC edits belong to the integrator):
// FI1 Stats: include l4d_awards_faults.h in the 252730 factory and construct the
// twelve exact classes with userID, in native_factory_order.txt order; Init is
// called by the factory. Destruction uses CGameEventListener's unregistration.
// FI2 Core/Life/Damage: CTerrorPlayer::GetHealthBuffer(), IsIncapacitated(),
// GetLastKnownArea(), GetDisplacingEntity() const (S303490, entity not bool),
// IsConnected(), and CBaseCombatCharacter::IsGhost() (native virtual1364).
// FI3 Inventory: HasWeaponOfType(8) is parser TYPE, GetWeaponOfType(12) is
// cached weapon ID; weapon_given.weapon is numeric ID, giver/userid are userIDs.
// GetTerrorWeaponInfo reads the real weapon registry. survivor_limp_health is
// the existing shared cvar (40, FCVAR_REPLICATED|FCVAR_CHEAT), not pill-use cap.
// FI4 Nav/Music: GetCheckpoint(origin), CTerrorCheckpoint::IsOutward(),
// GetNearestNavArea(origin,false,10000,false,true,0), flow direction 0, and
// BuildTerrorShortestPath(...,0,-1,false). The wrapper translates the original
// any-team -1 to SDK TEAM_ANY; it uses actual ShortestPathCost and nav blockers.
// FI5 Director/Population: real TheDirector->IsFinale(), and TheNextBots()
// registration iteration. No empty population/mode stand-ins or new counters.
// FI6 Native events: existing rich player_hurt is the SOLE hurt producer;
// Damage/Life own death/incap/ledge; Combat owns player_shoved(userid,attacker);
// SurvivorSpeech owns player_blocked(userid,blocker) after successful speech;
// Inventory/Consumables own weapon/item/heal/revive/pills events; Witch owns
// witch_harasser_set(userid,witchid,first) at real SetHarasser; world/player
// checkpoint owners supply entry/exit and door(checkpoint,userid,closed) events.
// Awards consume these boundaries and must never synthesize their own copies.
// FI7 Security's native identity call uses the existing CEventLog singleton:
// expose its real public virtual GetEntityIdentity in the L4D2 base interface;
// CTerrorEventLog's existing override supplies the formatter, not a new logger.
//
// Original-first controlled-run case index (not acceptance evidence):
// FB1 86: duplicate incap/ledge records; first-only revive/death removal; 5s
// equality; missing/unreachable nav, flow exactly1000 vs >1000, owner checkpoint.
// Native path calls pass NULL closest-area output. The adjacent byte-address
// argument is the unused cost functor; flow remains intact, including NaN.
// FB2 93: shove then fall/incap/ledge just below/at3s, unrelated hurt attacker,
// and fall type MASK0x20. FB3 92: blocked then any hurt/incap below/at1.5s.
// Both invalidate the victim timer after an award, including failed award gate.
// FB4 84/85: direct FF versus prior FF below/at0.5s with another nonnull current
// attacker; exact2056 exclusion versus combined types; actual displacement
// source rejection. Murder ignores2056 on hurt; Knockdown DOES start its timer.
// Neither consumes the timer on earning. Keep native direct entindex indexing
// of the 32-timer arrays. Only truly out-of-array accesses use Main's standing minimal skip.
// FB5 89: boomer self/team death/incap, hurt19/20, alive/incap split. FB6 90:
// pipe_bomb/inferno self/team death/incap, exact2056 vs other combined flags.
// FB7 91: subject not healer, no heal_cancel reset, undefined constructor
// byte, scans0..31, integer health1/40/41 (default), incap class1, peer kitID12.
// FB8 97: only events sample time; qualifying hurt/fire RESTART the10s interval;
// >=10 flag; gift uses recipient float health even for giver-owned template;
// map transition timestamp0 and flag persistence; pills_used reads subject.
// FB9 98: enemy death cooldown5s, strict500 enemy radius, inclusive300 corpse
// distance, non-NextBot players plus actual NextBots, nonteamkill/selfkill.
// FB10 94: actual harasser userid, team2 and !IsFinale. FB11 95: ASM-only branch:
// same outward checkpoint, >0 living survivors in its actual area vector, live
// team3 entry passing native virtual1364, last-opener flag; other live survivor
// entry clears the flag, owner entry sets it. Identity formatting precedes flag.
// FB12 Every owner-team2 gate precedes reset handling; only declared native
// map_transition/owner player_first_spawn reset listeners exist. Round/team/
// death/cancel events do not gain blanket resets. All earned subjects are NULL.

namespace
{
CTerrorPlayer *FaultPlayer( IGameEvent *event, const char *key )
{
	return dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( event->GetInt( key ) ) );
}

CBaseEntity *FaultSubject( IGameEvent *event )
{
	if ( event->GetInt( "userid" ) )
		return UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	return event->GetInt( "entityid" ) ? UTIL_EntityByIndex( event->GetInt( "entityid" ) ) : NULL;
}

void InvalidateFaultTimers( IntervalTimer ( &timers )[32] )
{
	for ( int i = 0; i < ARRAYSIZE( timers ); ++i )
		timers[i].Invalidate();
}

bool IsFaultReset( IGameEvent *event, CTerrorPlayer *player )
{
	return !Q_stricmp( event->GetName(), "map_transition" ) ||
		( !Q_stricmp( event->GetName(), "player_first_spawn" ) && FaultPlayer( event, "userid" ) == player );
}

// S377E50/S377EB0/S378040/S3781A0: one nearest-entity functor is run over
// non-NextBot players, then the actual NextBot registry. Strict radius and
// strict nearest comparisons are intentional; there is no visibility test.
class CMourningEnemyScan
{
public:
	explicit CMourningEnemyScan( const Vector &position ) : m_position( position ), m_flRangeSquared( 500.0f * 500.0f ), m_pNearest( NULL ) {}
	bool Consider( CBaseEntity *entity )
	{
		if ( entity->IsAlive() && entity->GetTeamNumber() == TEAM_INFECTED )
		{
			float rangeSquared = ( entity->WorldSpaceCenter() - m_position ).LengthSqr();
			if ( rangeSquared < m_flRangeSquared )
			{
				m_flRangeSquared = rangeSquared;
				m_pNearest = entity;
			}
		}
		return true;
	}
	bool operator()( INextBot *bot ) { return Consider( bot->GetEntity() ); }
	bool HasEnemy() const { return m_pNearest != NULL; }
private:
	Vector m_position;
	float m_flRangeSquared;
	CBaseEntity *m_pNearest;
};

bool HasMourningEnemy( const Vector &position )
{
	CMourningEnemyScan scan( position );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() && player->IsPlayer() &&
			player->IsConnected() && !player->MyNextBotPointer() )
			scan.Consider( player );
	}
	TheNextBots().ForEachBot( scan );
	return scan.HasEnemy();
}

// S379FB0 truncates the health+buffer sum before classifying; equality with
// survivor_limp_health is the middle class, not the high class.
int GreedHealthClass( CTerrorPlayer *player )
{
	const int health = static_cast< int >( player->GetHealth() + player->GetHealthBuffer() );
	if ( player->IsIncapacitated() )
		return 1;
	if ( health <= survivor_limp_health.GetFloat() )
		return health > 1 ? 1 : 0;
	return 2;
}

// S37EE80 traverses connected player slots; its S2582B0 functor ALWAYS returns
// true and counts living survivors in this exact checkpoint's area vector.
// It is NOT an all-survivors predicate and does NOT use S2FC540's mission gate.
int CountFaultSurvivorsInCheckpoint( const CTerrorCheckpoint *checkpoint )
{
	int count = 0;
	const CUtlVector< TerrorNavArea * > &areas = checkpoint->GetAreas();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() ||
			!player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR )
			continue;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) && areas.Find( area ) != areas.InvalidIndex() )
			++count;
	}
	return count;
}

}


CAbandonmentAwardTemplate::CAbandonmentAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
void CAbandonmentAwardTemplate::Init()
{
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_ledge_grab" );
	ListenForGameEvent( "revive_success" );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}

// S376160: remember each eligible incap/ledge event separately. Duplicate
// subjects are not coalesced; revive/death removes only the first match.
void CAbandonmentAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_incapacitated" ) || !Q_stricmp( name, "player_ledge_grab" ) )
	{
		if ( !event->GetInt( "userid" ) )
			return;
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		if ( !victim || victim == player || victim->GetTeamNumber() != player->GetTeamNumber() )
			return;
		TerrorNavArea *victimArea = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( victim->GetAbsOrigin(), false, 10000.0f, false, true, 0 ) );
		TerrorNavArea *playerArea = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( !victimArea || !playerArea || victimArea->GetFlowDistance() == -1.0f || playerArea->GetFlowDistance() == -1.0f )
			return;
		if ( BuildTerrorShortestPath( playerArea, victimArea, NULL, 0.0f, -1, false ) )
		{
			AbandonedPlayer_t abandoned = { victim->GetUserID(), gpGlobals->curtime };
			m_AbandonedPlayers.AddToTail( abandoned );
		}
		return;
	}
	if ( !Q_stricmp( name, "revive_success" ) )
	{
		if ( !event->GetInt( "subject" ) )
			return;
		CTerrorPlayer *victim = FaultPlayer( event, "subject" );
		if ( victim && victim != player && victim->GetTeamNumber() == player->GetTeamNumber() )
		{
			FOR_EACH_LL( m_AbandonedPlayers, i )
			{
				if ( m_AbandonedPlayers[i].userID == victim->GetUserID() )
				{
					m_AbandonedPlayers.Remove( i );
					return;
				}
			}
		}
		return;
	}
	if ( !Q_stricmp( name, "player_death" ) )
	{
		if ( !event->GetInt( "userid" ) )
			return;
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		if ( !victim || victim == player || victim->GetTeamNumber() != player->GetTeamNumber() || victim->GetTeamNumber() != TEAM_SURVIVOR )
			return;
		bool found = false;
		float incapTime = 0.0f;
		FOR_EACH_LL( m_AbandonedPlayers, i )
		{
			if ( m_AbandonedPlayers[i].userID == victim->GetUserID() )
			{
				incapTime = m_AbandonedPlayers[i].time;
				found = true;
				m_AbandonedPlayers.Remove( i );
				break;
			}
		}
		// Removal precedes the owner's life/elapsed checks in the original.
		if ( player->IsIncapacitated() || !player->IsAlive() || !found || gpGlobals->curtime - incapTime < 5.0f )
			return;
		TerrorNavArea *victimArea = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( victim->GetAbsOrigin(), false, 10000.0f, false, true, 0 ) );
		TerrorNavArea *playerArea = ToTerrorNavArea( player->GetLastKnownArea() );
		const float flowDifference = victimArea && playerArea ? fabsf( victimArea->GetFlowDistance() - playerArea->GetFlowDistance() ) : 0.0f;
		if ( !victimArea || !playerArea )
		{
			EarnAward( 86, NULL );
			return;
		}
		const bool reached = BuildTerrorShortestPath( playerArea, victimArea, NULL, 0.0f, -1, false );
		// fabsf gives nonnegative bits; integer order preserves COMISS/JB's NaN branch under /fp:fast.
		if ( !reached || FloatBits( flowDifference ) > FloatBits( 1000.0f ) || playerArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
			EarnAward( 86, NULL );
		return;
	}
	if ( IsFaultReset( event, player ) )
		m_AbandonedPlayers.RemoveAll();
}

CAbusiveBehaviorAwardTemplate::CAbusiveBehaviorAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
void CAbusiveBehaviorAwardTemplate::Init()
{
	ListenForGameEvent( "player_shoved" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_ledge_grab" );
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}
void CAbusiveBehaviorAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_shoved" ) )
	{
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		CTerrorPlayer *attacker = FaultPlayer( event, "attacker" );
		if ( victim && attacker && victim != player && attacker == player && victim->GetTeamNumber() == attacker->GetTeamNumber() && (unsigned int)victim->entindex() < ARRAYSIZE( m_ShoveTimers ) )
			m_ShoveTimers[victim->entindex()].Start();
	}
	else if ( !Q_stricmp( name, "player_incapacitated" ) || !Q_stricmp( name, "player_ledge_grab" ) ||
		( !Q_stricmp( name, "player_hurt" ) && ( event->GetInt( "type" ) & DMG_FALL ) ) )
	{
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		if ( victim && victim != player && victim->GetTeamNumber() == player->GetTeamNumber() && (unsigned int)victim->entindex() < ARRAYSIZE( m_ShoveTimers ) )
		{
			IntervalTimer &timer = m_ShoveTimers[victim->entindex()];
			if ( timer.HasStarted() && timer.GetElapsedTime() < 3.0f )
			{
				EarnAward( 93, NULL );
				timer.Invalidate();
			}
		}
	}
	else if ( IsFaultReset( event, player ) )
		InvalidateFaultTimers( m_ShoveTimers );
}

CEarlyMourningAwardTemplate::CEarlyMourningAwardTemplate( int userID ) : CAwardTemplate( userID ), m_flLastEnemyDeath( 0.0f ) {}
void CEarlyMourningAwardTemplate::Init()
{
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "map_transition" );
}
void CEarlyMourningAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	if ( !Q_stricmp( event->GetName(), "map_transition" ) )
	{
		m_flLastEnemyDeath = 0.0f;
		return;
	}
	if ( Q_stricmp( event->GetName(), "player_death" ) )
		return;
	CBaseEntity *victim = FaultSubject( event );
	if ( !victim )
		return;
	if ( victim->GetTeamNumber() != player->GetTeamNumber() )
	{
		m_flLastEnemyDeath = gpGlobals->curtime;
		return;
	}
	if ( player->IsIncapacitated() || !player->IsAlive() )
		return;
	CTerrorPlayer *attacker = FaultPlayer( event, "attacker" );
	if ( victim == attacker || gpGlobals->curtime - m_flLastEnemyDeath < 5.0f )
		return;
	if ( !HasMourningEnemy( victim->GetAbsOrigin() ) &&
		( player->GetAbsOrigin() - victim->GetAbsOrigin() ).Length() <= 300.0f &&
		( !attacker || attacker->GetTeamNumber() != victim->GetTeamNumber() ) )
		EarnAward( 98, NULL );
}

CForgetfulAwardTemplate::CForgetfulAwardTemplate( int userID ) : CAwardTemplate( userID ), m_flHealthItemStartTime( -1.0f ), m_bForgotHealthItem( false ) {}
void CForgetfulAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "weapon_fire" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "weapon_given" );
	ListenForGameEvent( "item_pickup" );
	ListenForGameEvent( "heal_success" );
	ListenForGameEvent( "revive_success" );
	ListenForGameEvent( "pills_used" );
	ListenForGameEvent( "map_transition" );
}
void CForgetfulAwardTemplate::UpdateHealthItemTimer()
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player )
		return;
	int health = static_cast< int >( player->GetHealth() + player->GetHealthBuffer() );
	if ( player->IsIncapacitated() )
		health = 0;
	const bool lowHealth = survivor_limp_health.GetFloat() > health;
	if ( m_flHealthItemStartTime > 0.0f && gpGlobals->curtime - m_flHealthItemStartTime >= 10.0f && lowHealth && player->HasWeaponOfType( 8 ) )
		m_bForgotHealthItem = true;
	// Native starts again on EVERY relevant hurt/fire event, not only on the
	// transition into low health. A busy player can therefore defer the award.
	if ( player->HasWeaponOfType( 8 ) && lowHealth )
		m_flHealthItemStartTime = gpGlobals->curtime;
	else
		m_flHealthItemStartTime = -1.0f;
}
void CForgetfulAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_hurt" ) || !Q_stricmp( name, "weapon_fire" ) )
	{
		if ( FaultPlayer( event, "userid" ) == player )
			UpdateHealthItemTimer();
		return;
	}
	if ( !Q_stricmp( name, "revive_success" ) )
	{
		if ( FaultPlayer( event, "subject" ) != player )
			return;
		if ( player->HasWeaponOfType( 8 ) )
		{
			m_flHealthItemStartTime = gpGlobals->curtime;
			return;
		}
		m_flHealthItemStartTime = -1.0f;
		m_bForgotHealthItem = false;
		return;
	}
	if ( !Q_stricmp( name, "player_incapacitated" ) || !Q_stricmp( name, "player_death" ) )
	{
		if ( FaultPlayer( event, "userid" ) != player )
			return;
		UpdateHealthItemTimer();
		if ( m_bForgotHealthItem )
			EarnAward( 97, NULL );
		m_flHealthItemStartTime = -1.0f;
		m_bForgotHealthItem = false;
		return;
	}
	if ( !Q_stricmp( name, "weapon_given" ) )
	{
		CTerrorPlayer *recipient = FaultPlayer( event, "userid" );
		CTerrorPlayer *giver = FaultPlayer( event, "giver" );
		const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( event->GetInt( "weapon" ) );
		if ( !info || info->m_WeaponType != 8 || ( recipient != player && giver != player ) )
			return;
		// Original compares the recipient's untruncated, non-incap-zeroed health
		// even when the template belongs to the giver.
		if ( player->HasWeaponOfType( 8 ) && survivor_limp_health.GetFloat() > recipient->GetHealth() + recipient->GetHealthBuffer() )
		{
			m_flHealthItemStartTime = gpGlobals->curtime;
			return;
		}
		m_flHealthItemStartTime = -1.0f;
		m_bForgotHealthItem = false;
		return;
	}
	if ( !Q_stricmp( name, "item_pickup" ) )
	{
		const char *item = event->GetString( "item" );
		if ( Q_stricmp( item, "first_aid_kit" ) && Q_stricmp( item, "pain_pills" ) )
			return;
		CTerrorPlayer *recipient = FaultPlayer( event, "userid" );
		if ( recipient != player )
			return;
		if ( player->HasWeaponOfType( 8 ) && survivor_limp_health.GetFloat() > recipient->GetHealth() + recipient->GetHealthBuffer() )
			m_flHealthItemStartTime = gpGlobals->curtime;
		else
			m_flHealthItemStartTime = -1.0f;
		return;
	}
	if ( !Q_stricmp( name, "heal_success" ) || !Q_stricmp( name, "pills_used" ) )
	{
		if ( FaultPlayer( event, "subject" ) != player )
			return;
		m_flHealthItemStartTime = -1.0f;
		m_bForgotHealthItem = false;
		return;
	}
	if ( !Q_stricmp( name, "map_transition" ) )
	{
		if ( player->HasWeaponOfType( 8 ) && survivor_limp_health.GetFloat() > player->GetHealth() + player->GetHealthBuffer() )
			m_flHealthItemStartTime = 0.0f;
		else
			m_flHealthItemStartTime = -1.0f;
		// No flag reset here in S378750.
	}
}

CGreedAwardTemplate::CGreedAwardTemplate( int userID ) : CAwardTemplate( userID ), m_bGreedyHeal( false ) {} // S37A020 leaves it undefined: skip only pre-heal_begin award91.
void CGreedAwardTemplate::Init()
{
	ListenForGameEvent( "heal_success" );
	ListenForGameEvent( "heal_begin" );
}
void CGreedAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	if ( !Q_stricmp( event->GetName(), "heal_success" ) )
	{
		if ( FaultPlayer( event, "subject" ) == player && m_bGreedyHeal )
		{
			EarnAward( 91, NULL );
			m_bGreedyHeal = false;
		}
		return;
	}
	if ( Q_stricmp( event->GetName(), "heal_begin" ) )
		return;
	m_bGreedyHeal = false;
	if ( FaultPlayer( event, "subject" ) != player )
		return;
	TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
	if ( area && area->HasSpawnAttributes( TERROR_NAV_PLAYER_START | TERROR_NAV_CHECKPOINT ) )
		return;
	const int healthClass = GreedHealthClass( player );
	// Native deliberately visits indices 0..31, not 1..maxClients.
	for ( int i = 0; i < 32; ++i )
	{
		CTerrorPlayer *other = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( other && other->IsPlayer() && other != player && other->GetTeamNumber() == TEAM_SURVIVOR && other->IsAlive() &&
			GreedHealthClass( other ) < healthClass && !other->GetWeaponOfType( 12 ) )
		{
			m_bGreedyHeal = true;
			return;
		}
	}
}

CHinderingEscapeAwardTemplate::CHinderingEscapeAwardTemplate( int userID ) : CAwardTemplate( userID )
{
	InvalidateFaultTimers( m_BlockedTimers );
}
void CHinderingEscapeAwardTemplate::Init()
{
	ListenForGameEvent( "player_blocked" );
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}
void CHinderingEscapeAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_blocked" ) )
	{
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		CTerrorPlayer *blocker = FaultPlayer( event, "blocker" );
		if ( victim && blocker && blocker == player && victim->GetTeamNumber() == blocker->GetTeamNumber() && (unsigned int)victim->entindex() < ARRAYSIZE( m_BlockedTimers ) )
			m_BlockedTimers[victim->entindex()].Start();
	}
	else if ( !Q_stricmp( name, "player_hurt" ) || !Q_stricmp( name, "player_incapacitated" ) )
	{
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		if ( victim && victim != player && victim->GetTeamNumber() == player->GetTeamNumber() && (unsigned int)victim->entindex() < ARRAYSIZE( m_BlockedTimers ) )
		{
			IntervalTimer &timer = m_BlockedTimers[victim->entindex()];
			if ( timer.HasStarted() && timer.GetElapsedTime() < 1.5f )
			{
				EarnAward( 92, NULL );
				timer.Invalidate();
			}
		}
	}
	else if ( IsFaultReset( event, player ) )
		InvalidateFaultTimers( m_BlockedTimers );
}

CMurderAwardTemplate::CMurderAwardTemplate( int userID ) : CAwardTemplate( userID )
{
	InvalidateFaultTimers( m_HurtTimers );
}
void CMurderAwardTemplate::Init()
{
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}
void CMurderAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_hurt" ) )
	{
		if ( event->GetInt( "type" ) == 2056 )
			return;
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		CTerrorPlayer *attacker = FaultPlayer( event, "attacker" );
		if ( victim && attacker && victim != player && attacker == player && victim->GetTeamNumber() == attacker->GetTeamNumber() && (unsigned int)victim->entindex() < ARRAYSIZE( m_HurtTimers ) )
			m_HurtTimers[victim->entindex()].Start();
	}
	else if ( !Q_stricmp( name, "player_death" ) )
	{
		if ( !event->GetInt( "userid" ) )
			return;
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		CTerrorPlayer *attacker = FaultPlayer( event, "attacker" );
		if ( victim && attacker && victim != player && victim->GetTeamNumber() == player->GetTeamNumber() &&
			event->GetInt( "type" ) != 2056 && !victim->GetDisplacingEntity() )
		{
			const unsigned int slot = (unsigned int)victim->entindex();
			if ( attacker == player || ( slot < ARRAYSIZE( m_HurtTimers ) && m_HurtTimers[slot].HasStarted() && m_HurtTimers[slot].GetElapsedTime() < 0.5f ) )
				EarnAward( 84, NULL );
		}
	}
	else if ( IsFaultReset( event, player ) )
		InvalidateFaultTimers( m_HurtTimers );
}

CKnockdownAwardTemplate::CKnockdownAwardTemplate( int userID ) : CAwardTemplate( userID )
{
	InvalidateFaultTimers( m_HurtTimers );
}
void CKnockdownAwardTemplate::Init()
{
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}
void CKnockdownAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_hurt" ) )
	{
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		CTerrorPlayer *attacker = FaultPlayer( event, "attacker" );
		if ( victim && attacker && victim != player && attacker == player && victim->GetTeamNumber() == attacker->GetTeamNumber() && (unsigned int)victim->entindex() < ARRAYSIZE( m_HurtTimers ) )
			m_HurtTimers[victim->entindex()].Start();
	}
	else if ( !Q_stricmp( name, "player_incapacitated" ) )
	{
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		CTerrorPlayer *attacker = FaultPlayer( event, "attacker" );
		if ( victim && attacker && victim != player && victim->GetTeamNumber() == player->GetTeamNumber() &&
			event->GetInt( "type" ) != 2056 && !victim->GetDisplacingEntity() )
		{
			const unsigned int slot = (unsigned int)victim->entindex();
			if ( attacker == player || ( slot < ARRAYSIZE( m_HurtTimers ) && m_HurtTimers[slot].HasStarted() && m_HurtTimers[slot].GetElapsedTime() < 0.5f ) )
				EarnAward( 85, NULL );
		}
	}
	else if ( IsFaultReset( event, player ) )
		InvalidateFaultTimers( m_HurtTimers );
}

CBoomerBlunderAwardTemplate::CBoomerBlunderAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
void CBoomerBlunderAwardTemplate::Init()
{
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_hurt" );
}
void CBoomerBlunderAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_death" ) || !Q_stricmp( name, "player_incapacitated" ) )
	{
		CBaseEntity *victim = FaultSubject( event );
		CTerrorPlayer *attacker = FaultPlayer( event, "attacker" );
		if ( victim && attacker && attacker == player && victim->GetTeamNumber() == attacker->GetTeamNumber() && !Q_stricmp( event->GetString( "weapon" ), "boomer" ) )
			EarnAward( 89, NULL );
	}
	else if ( !Q_stricmp( name, "player_hurt" ) )
	{
		CTerrorPlayer *victim = FaultPlayer( event, "userid" );
		CTerrorPlayer *attacker = FaultPlayer( event, "attacker" );
		if ( victim && attacker && attacker == player && victim->GetTeamNumber() == attacker->GetTeamNumber() &&
			!Q_stricmp( event->GetString( "weapon" ), "boomer" ) && victim->IsAlive() && !victim->IsIncapacitated() && event->GetInt( "dmg_health" ) >= 20 )
			EarnAward( 89, NULL );
	}
}

CGrenadeBlunderAwardTemplate::CGrenadeBlunderAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
void CGrenadeBlunderAwardTemplate::Init()
{
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "player_incapacitated" );
}
void CGrenadeBlunderAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR ||
		( Q_stricmp( event->GetName(), "player_death" ) && Q_stricmp( event->GetName(), "player_incapacitated" ) ) || event->GetInt( "type" ) == 2056 )
		return;
	CBaseEntity *victim = FaultSubject( event );
	CTerrorPlayer *attacker = FaultPlayer( event, "attacker" );
	if ( victim && attacker && attacker == player && victim->GetTeamNumber() == attacker->GetTeamNumber() &&
		( !Q_stricmp( event->GetString( "weapon" ), "pipe_bomb" ) || !Q_stricmp( event->GetString( "weapon" ), "inferno" ) ) )
		EarnAward( 90, NULL );
}

CSecurityFailureAwardTemplate::CSecurityFailureAwardTemplate( int userID ) : CAwardTemplate( userID ), m_bOpenedCheckpoint( false ) {}
void CSecurityFailureAwardTemplate::Init()
{
	ListenForGameEvent( "door_open" );
	ListenForGameEvent( "door_unlocked" );
	ListenForGameEvent( "player_entered_checkpoint" );
	ListenForGameEvent( "player_left_checkpoint" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}
void CSecurityFailureAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "door_open" ) )
	{
		if ( !event->GetBool( "checkpoint" ) )
			return;
		CTerrorPlayer *opener = FaultPlayer( event, "userid" );
		if ( !opener || !event->GetBool( "closed" ) )
			return;
		if ( opener == player )
			m_bOpenedCheckpoint = true;
		else if ( opener->GetTeamNumber() == TEAM_SURVIVOR )
			m_bOpenedCheckpoint = false;
	}
	if ( !Q_stricmp( name, "door_unlocked" ) )
	{
		if ( !event->GetBool( "checkpoint" ) )
			return;
		CTerrorPlayer *opener = FaultPlayer( event, "userid" );
		if ( opener == player )
			m_bOpenedCheckpoint = true;
		else if ( opener && opener->GetTeamNumber() == TEAM_SURVIVOR )
			m_bOpenedCheckpoint = false;
		return;
	}
	if ( !Q_stricmp( name, "player_left_checkpoint" ) )
	{
		CBaseEntity *subject = FaultSubject( event );
		if ( !subject )
			return;
		CTerrorPlayer *subjectPlayer = dynamic_cast< CTerrorPlayer * >( subject );
		if ( subjectPlayer && subjectPlayer->IsIncapacitated() )
			return;
		if ( subject == player && subject->IsAlive() )
			m_bOpenedCheckpoint = true;
		else if ( subject->GetTeamNumber() == TEAM_SURVIVOR && subject->IsAlive() )
			m_bOpenedCheckpoint = false;
		return;
	}
	if ( !Q_stricmp( name, "player_entered_checkpoint" ) )
	{
		if ( !event->GetInt( "userid" ) && !event->GetInt( "entityid" ) )
			return;
		CBaseEntity *subject = FaultSubject( event );
		// S37F25E dereferences a nonzero-ID subject before a null check.
		CBaseCombatCharacter *actor = subject->MyCombatCharacterPointer();
		if ( !actor )
			return;
		CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetCheckpoint( subject->GetAbsOrigin() );
		if ( !checkpoint || checkpoint != TheTerrorNavMesh()->GetCheckpoint( player->GetAbsOrigin() ) ||
			!checkpoint->IsOutward() || CountFaultSurvivorsInCheckpoint( checkpoint ) == 0 )
			return;
		CTerrorPlayer *subjectPlayer = dynamic_cast< CTerrorPlayer * >( actor );
		if ( subjectPlayer && subjectPlayer->IsIncapacitated() )
			return;
		if ( actor == player && actor->IsAlive() )
		{
			m_bOpenedCheckpoint = true;
			return;
		}
		if ( actor->GetTeamNumber() == TEAM_SURVIVOR && actor->IsAlive() )
		{
			m_bOpenedCheckpoint = false;
			return;
		}
		if ( actor->GetTeamNumber() == TEAM_INFECTED && actor->IsAlive() && !actor->IsGhost() )
		{
			// This real identity call is present even though its buffer is not
			// subsequently printed. It precedes the last-opener flag test.
			char identity[256] = { 0 };
			static_cast< CEventLog * >( GameLogSystem() )->GetEntityIdentity( actor, identity, sizeof( identity ) );
			if ( m_bOpenedCheckpoint )
				EarnAward( 95, NULL );
		}
		return;
	}
	if ( IsFaultReset( event, player ) )
		m_bOpenedCheckpoint = false;
}

CWitchHarassmentAwardTemplate::CWitchHarassmentAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
void CWitchHarassmentAwardTemplate::Init() { ListenForGameEvent( "witch_harasser_set" ); }
void CWitchHarassmentAwardTemplate::FireGameEvent( IGameEvent *event )
{
	if ( !event->GetName() )
		return;
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR || Q_stricmp( event->GetName(), "witch_harasser_set" ) || !event->GetInt( "userid" ) )
		return;
	CTerrorPlayer *harasser = FaultPlayer( event, "userid" );
	if ( harasser && harasser->GetTeamNumber() == TEAM_SURVIVOR && !TheDirector->IsFinale() && harasser == player )
		EarnAward( 94, NULL );
}
