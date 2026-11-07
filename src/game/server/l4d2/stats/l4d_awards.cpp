//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/l4d_awards.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "team.h"
#include "l4d2/stats/l4d_awards_console_server_cvars.inc"
#include "tier0/memdbgon.h"

CAwardTemplate::CAwardTemplate( int userID ) : m_userID( userID ) {}

CTerrorPlayer *CAwardTemplate::GetPlayer() const
{
	return dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( m_userID ) );
}

// S253460: the mission-ID gate applies to the template, not OnAwardEarned.
bool CAwardTemplate::EarnAward( int award, CBaseEntity *subject )
{
	CTerrorPlayer *player = GetPlayer();
	return player && player->m_missionIDSpawnedOn == TheDirector->GetMissionID() && player->OnAwardEarned( award, subject );
}

CFriendlyFireAwardTemplate::CFriendlyFireAwardTemplate( int userID ) : CAwardTemplate( userID ), m_hasLastAttackSequence( false )
{
	// S379070 leaves the serial uninitialized; skip only its comparison until a real native writer.
	// CUtlLinkedList/IntervalTimer constructors supply the actual empty/-1 state.
}

void CFriendlyFireAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_first_spawn" );
}

void CFriendlyFireAwardTemplate::Reset()
{
	m_nLastAttackSequence = static_cast< unsigned int >( GetTerrorBulletSerial() ) - 1;
	m_hasLastAttackSequence = true;
	for ( int i = 0; i < ARRAYSIZE( m_Hits ); ++i )
		m_Hits[i].RemoveAll();
	m_RecklessTimer.Invalidate();
	m_FriendlyFireTimer.Invalidate();
}

void CFriendlyFireAwardTemplate::EmitFriendlyFire( CTerrorPlayer *victim, CTerrorPlayer *guilty, int damageType )
{
	IGameEvent *event = gameeventmanager->CreateEvent( "friendly_fire" );
	if ( event )
	{
		event->SetInt( "attacker", GetPlayer()->GetUserID() );
		event->SetInt( "victim", victim->GetUserID() );
		event->SetInt( "guilty", guilty->GetUserID() );
		event->SetInt( "type", damageType );
		gameeventmanager->FireEvent( event );
	}
}

void CFriendlyFireAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *attacker = GetPlayer();
	if ( !attacker || attacker->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "map_transition" ) ||
		( !Q_stricmp( name, "player_first_spawn" ) && event->GetInt( "userid" ) == attacker->GetUserID() ) )
	{
		Reset();
		return;
	}
	bool hurt = !Q_stricmp( name, "player_hurt" );
	if ( !hurt && Q_stricmp( name, "player_incapacitated" ) )
		return;
	int type = event->GetInt( "type" );
	unsigned int serial = static_cast< unsigned int >( GetTerrorBulletSerial() );
	if ( ( hurt && type == 0x808 ) || ( ( type & 0x20000002 ) && m_hasLastAttackSequence && m_nLastAttackSequence == serial ) )
		return;
	CTerrorPlayer *victim = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
	CTerrorPlayer *eventAttacker = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( event->GetInt( "attacker" ) ) );
	if ( !victim || !eventAttacker || victim == attacker || eventAttacker != attacker || victim->GetTeamNumber() != attacker->GetTeamNumber() )
		return;

	float victimStill = victim->GetStillnessDuration( true );
	float attackerStill = attacker->GetStillnessDuration( false );
	float fired = attacker->GetTimeSinceWeaponFired();
	if ( !hurt )
	{
		if ( victimStill >= 1.0f || attackerStill <= 1.0f || fired >= 2.0f )
		{
			if ( m_FriendlyFireTimer.GetElapsedTime() >= 2.0f )
			{
				EarnAward( 87, NULL );
				m_FriendlyFireTimer.Start();
			}
			m_nLastAttackSequence = serial;
			m_hasLastAttackSequence = true;
		}
		else if ( m_RecklessTimer.GetElapsedTime() >= 2.0f )
		{
			victim->OnAwardEarned( 88, attacker );
			m_RecklessTimer.Start();
		}
		return;
	}

	CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
	if ( !team || victim->GetDisplacingEntity() )
		return;
	const char *weapon = event->GetString( "weapon" );
	bool direct = Q_stricmp( weapon, "boomer" ) && Q_stricmp( weapon, "env_explosion" );
	float visibleTime = victim->GetTimeSinceFriendSeen( attacker );
	if ( direct && attackerStill > 1.0f && visibleTime < 1.0f && visibleTime >= 0.0f )
		return;
	if ( direct && victimStill < 1.0f && attackerStill > 1.0f && fired < 2.0f )
	{
		if ( m_RecklessTimer.GetElapsedTime() >= 2.0f )
		{
			EmitFriendlyFire( victim, victim, type );
			victim->OnAwardEarned( 88, attacker );
			m_RecklessTimer.Start();
		}
		return;
	}

	for ( int slot = 0; slot < team->GetNumPlayers() && slot < ARRAYSIZE( m_Hits ); ++slot )
	{
		if ( team->GetPlayer( slot ) != victim )
			continue;
		m_RecklessTimer.Start();
		CUtlLinkedList< Hit_t > &hits = m_Hits[slot];
		while ( hits.Count() && hits[hits.Head()].time < gpGlobals->curtime - 10.0f )
			hits.Remove( hits.Head() );
		int atTime = hits.Head();
		while ( atTime != hits.InvalidIndex() && hits[atTime].time != gpGlobals->curtime )
			atTime = hits.Next( atTime );
		if ( atTime == hits.InvalidIndex() )
		{
			atTime = hits.AddToTail();
			hits[atTime].time = gpGlobals->curtime;
			hits[atTime].damage = 0;
		}
		hits[atTime].damage += event->GetInt( "dmg_health" );
		int damage = 0;
		FOR_EACH_LL( hits, i )
			damage += hits[i].damage;
		// The native OR is intentional; a single hit qualifies, even at zero damage.
		if ( ( hits.Count() >= 1 || damage >= 15 ) && m_FriendlyFireTimer.GetElapsedTime() >= 2.0f )
		{
			EarnAward( 87, NULL );
			EmitFriendlyFire( victim, attacker, type );
			m_nLastAttackSequence = serial;
			m_hasLastAttackSequence = true;
			hits.RemoveAll();
			attacker->OnFriendlyFire( victim, event );
			m_FriendlyFireTimer.Start();
		}
		return;
	}
}

void CRescuerAwardTemplate::Init() { ListenForGameEvent( "survivor_rescued" ); }
void CRescuerAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( player && player->GetTeamNumber() == TEAM_SURVIVOR &&
		!Q_stricmp( event->GetName(), "survivor_rescued" ) &&
		UTIL_PlayerByUserId( event->GetInt( "rescuer" ) ) == player )
		EarnAward( 80, UTIL_PlayerByUserId( event->GetInt( "victim" ) ) );
}

void CReviveFriendAwardTemplate::Init() { ListenForGameEvent( "revive_success" ); }
void CReviveFriendAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR || Q_stricmp( event->GetName(), "revive_success" ) )
		return;
	CTerrorPlayer *reviver = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
	CTerrorPlayer *subject = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( event->GetInt( "subject" ) ) );
	// Native S37EA40 dereferences subject before testing reviver != subject.
	if ( reviver && reviver == player && !subject->IsHangingFromLedge() && reviver != subject )
		EarnAward( 66, subject );
}

void CSharingAwardTemplate::Init()
{
	ListenForGameEvent( "weapon_given" );
	ListenForGameEvent( "heal_success" );
}
void CSharingAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR )
		return;
	if ( !Q_stricmp( event->GetName(), "weapon_given" ) )
	{
		CBasePlayer *recipient = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
		if ( recipient && UTIL_PlayerByUserId( event->GetInt( "giver" ) ) == player )
		{
			int id = event->GetInt( "weapon" );
			const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( id );
			if ( !info || info->m_WeaponType == TERROR_WEAPONTYPE_HEALING )
			{
				if ( id == 23 ) EarnAward( 69, recipient );
				else if ( id == 15 ) EarnAward( 68, recipient );
			}
		}
	}
	else if ( !Q_stricmp( event->GetName(), "heal_success" ) )
	{
		CBasePlayer *healer = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
		CBasePlayer *subject = UTIL_PlayerByUserId( event->GetInt( "subject" ) );
		if ( healer == player && subject != healer )
			EarnAward( 70, subject );
	}
}

void CHelpingHandAwardTemplate::Init() { ListenForGameEvent( "revive_success" ); }
void CHelpingHandAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR || Q_stricmp( event->GetName(), "revive_success" ) ) return;
	CTerrorPlayer *reviver = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
	CTerrorPlayer *subject = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( event->GetInt( "subject" ) ) );
	if ( reviver && reviver == player && subject->IsHangingFromLedge() ) EarnAward( 75, subject );
}

void CSaviorAwardTemplate::Init()
{
	ListenForGameEvent( "choke_stopped" );
	ListenForGameEvent( "pounce_stopped" );
}
void CSaviorAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( player && player->GetTeamNumber() == TEAM_SURVIVOR &&
		( !Q_stricmp( event->GetName(), "choke_stopped" ) || !Q_stricmp( event->GetName(), "pounce_stopped" ) ) &&
		UTIL_PlayerByUserId( event->GetInt( "userid" ) ) == player )
		EarnAward( 76, UTIL_PlayerByUserId( event->GetInt( "victim" ) ) );
}

void CSurvivorKillAwardTemplate::Init() { ListenForGameEvent( "player_death" ); }
void CSurvivorKillAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_INFECTED || Q_stricmp( event->GetName(), "player_death" ) ) return;
	CBasePlayer *victim = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	CBasePlayer *attacker = UTIL_PlayerByUserId( event->GetInt( "attacker" ) );
	if ( victim && attacker == player && victim->GetTeamNumber() == TEAM_SURVIVOR ) EarnAward( 123, NULL );
}

const char *GetAwardResponseName( int award )
{
	static const char *const names[] =
	{
		"ReviveFriend", "Protector", "Sharing", "", "Medic", "TeamAlive", "TeamSurvival", "TeamUntouchable", "GoodTeammate",
		"HelpingHand", "Savior", "TeamNarrowEscape", "PerfectTeam", "Hero", "Rescuer", "HulkShutOut", "TeamFaultlessMission", "TeamToughGuy",
		"Murder", "Knockdown", "Abandonment", "FriendlyFire", "Reckless", "BoomerBlunder", "GrenadeBlunder", "Greed", "HinderingEscape",
		"AbusiveBehavior", "WitchHarassment", "SecurityFailure", "Kicked", "Forgetful", "EarlyMourning", "Left4Dead", "LeftBehind", "Gotcha"
	};
	return award >= 66 && award <= 101 ? names[award - 66] : "";
}

int GetAwardResponseType( int award )
{
	if ( award >= 84 && award <= 101 ) return 2;
	switch ( award )
	{
	case 25: case 66: case 67: case 68: case 70: case 71: case 72: case 73: case 74: case 75: case 76: case 77:
	case 78: case 79: case 80: case 81: case 82: case 83: case 104: case 106: case 107: case 109: case 110:
	case 111: case 112: case 113: case 115: case 117: case 119: case 121: case 124: case 126: case 128:
	case 130: case 132: case 133: case 134: case 135: case 136: case 138: case 141: case 142: case 143:
	case 144: case 145: case 147: case 148:
		return 1;
	default: return 0;
	}
}
