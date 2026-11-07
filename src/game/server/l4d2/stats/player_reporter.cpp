//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/player_reporter.h"
#include "l4d2/stats/l4d_awards.h"
#include "l4d2/stats/l4d_awards_faults.h"
#include "l4d2/stats/l4d_awards_combat.h"
#include "l4d2/stats/l4d_awards_campaign.h"
#include "l4d2/stats/l4d_awards_infected.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/terror_message_text.h"
#include "tier0/memdbgon.h"

static PlayerReporter g_PlayerReporter;
PlayerReporter &GetPlayerReporter() { return g_PlayerReporter; }

PlayerReport_t::PlayerReport_t( int id, int serial, int playerTeam ) : userID( id ), playerID( serial ), alive( false ), team( playerTeam ),
	enteredStartArea( false ), transitioned( false ), survivorKills( 0 ), selfDamage( 0 ), friendlyDamage( 0 ), medkitsUsed( 0 ), medkitsShared( 0 ),
	pillsUsed( 0 ), adrenalineUsed( 0 ), defibrillatorsUsed( 0 ), pipebombsUsed( 0 ), molotovsUsed( 0 ), boomerBilesUsed( 0 ),
	shotsFired( 0 ), shotsHit( 0 ), headshots( 0 ), meleeKills( 0 ), revives( 0 ), incaps( 0 ), deaths( 0 ), awardsTeam( -1 ),
	pendingHit( false ), pendingHeadshot( false )
{
	Q_memset( zombieKills, 0, sizeof( zombieKills ) );
	Q_memset( awards, 0, sizeof( awards ) );
	Q_memset( damageTaken, 0, sizeof( damageTaken ) );
}
PlayerReport_t::~PlayerReport_t() { awardTemplates.PurgeAndDeleteElements(); }

static void AddAwardTemplate( CUtlVector< CAwardTemplate * > &templates, CAwardTemplate *award )
{
	if ( award )
	{
		award->Init();
		templates.AddToTail( award );
	}
}

// S252730: validate the new team before deleting old listeners. Each native
// constructor/Init/insertion completes before the next constructor.
void PlayerReport_t::ResetAwards( CTerrorPlayer *player )
{
	int newTeam = player->GetTeamNumber();
	if ( newTeam != TEAM_SURVIVOR && newTeam != TEAM_INFECTED ) return;
	awardTemplates.PurgeAndDeleteElements();
	if ( newTeam == TEAM_SURVIVOR )
	{
		AddAwardTemplate( awardTemplates, new CAbandonmentAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CAbusiveBehaviorAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CAmbushAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CBoomerBlunderAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CBoomerComboAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CCheckpointCompleteAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CDarkMissionAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CEarlyMourningAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CForgetfulAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CFriendlyFireAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CGreedAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CGrenadeBlunderAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CGrenadeExpertAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CGrenadierAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHeadhunterAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHelpingHandAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHeroAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHinderingEscapeAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHulkShutOutAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHulkSlayerAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHurdleAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CItSurvivorAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CKnockdownAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CLeft4DeadAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CLeftBehindAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CMurderAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CMVPAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CNarrowEscapeAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CParryAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CPerfectTeamAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CProtectorAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CRescuerAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CReviveFriendAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CSaviorAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CScenarioWinAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CSecurityFailureAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CSharingAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CSharpshooterAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CSkeetShootAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CSniperAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CToughGuyAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CVomitStopAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CWeaponExpertAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CWhiplashAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CWitchHarassmentAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CWitchSlayerAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CGoodTeammateAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CGotchaAwardTemplate( userID ) );
	}
	else
	{
		AddAwardTemplate( awardTemplates, new CGotchaAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CPounceAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CRedecoratorAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CTongueChokeAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CFallCauserAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CSurvivorKillAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHulkPhysicsAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHulkRockAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHulkPunchAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CClawHitAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CVomitHitAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CExplosionAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHitEverybodyAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CHitAndRunAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CMostHitsAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CNoSurvivorsAwardTemplate( userID ) );
		AddAwardTemplate( awardTemplates, new CDoomsdayAwardTemplate( userID ) );
	}
	awardsTeam = newTeam;
}
int PlayerReport_t::DamageTaken() const
{
	int damage = selfDamage + friendlyDamage;
	for ( int i = 0; i < ARRAYSIZE( damageTaken ); ++i ) damage += damageTaken[i];
	return damage;
}
int PlayerReport_t::Accuracy() const { return shotsFired ? 100 * shotsHit / shotsFired : 0; }
int PlayerReport_t::HeadshotAccuracy() const { return shotsHit ? 100 * headshots / shotsHit : 0; }

PlayerReporter::PlayerReporter() : CAutoGameSystem( "PlayerReporter" ) {}
bool PlayerReporter::Init()
{
	static const char *const events[] =
	{
		"player_spawn", "player_team", "player_death", "player_hurt", "player_incapacitated", "player_entered_start_area",
		"weapon_fire", "infected_hurt", "heal_success", "revive_success", "pills_used", "adrenaline_used", "award_earned",
		"defibrillator_used", "round_end", "finale_win", "map_transition", "server_shutdown"
	};
	for ( int i = 0; i < ARRAYSIZE( events ); ++i ) ListenForGameEvent( events[i] );
	return true;
}
void PlayerReporter::Shutdown() { StopListeningForAllEvents(); Clear(); }
void PlayerReporter::Clear()
{
	FOR_EACH_LL( m_players, i ) delete m_players[i];
	m_players.RemoveAll();
}

PlayerReport_t *PlayerReporter::GetPlayerReport( CTerrorPlayer *player )
{
	// S2BC350: co-op infected have no report, even if their caller wants one.
	if ( !player || ( player->GetTeamNumber() == TEAM_INFECTED && !TerrorGameRules()->IsPlayerControlledZombies() ) )
		return NULL;
	int serial = player->GetStatsPlayerID();
	FOR_EACH_LL( m_players, i )
	{
		if ( m_players[i]->playerID == serial )
		{
			m_players[i]->userID = player->GetUserID();
			return m_players[i];
		}
	}
	PlayerReport_t *report = new PlayerReport_t( player->GetUserID(), serial, player->GetTeamNumber() );
	m_players.AddToTail( report );
	report->ResetAwards( player );
	return report;
}
void PlayerReporter::RemovePlayerReport( CTerrorPlayer *player )
{
	// S2BC550 gets or creates the real report before pointer-identity removal.
	if ( player->GetTeamNumber() == TEAM_INFECTED && !TerrorGameRules()->IsPlayerControlledZombies() )
		return;
	PlayerReport_t *report = GetPlayerReport( player );
	FOR_EACH_LL( m_players, i )
	{
		if ( m_players[i] == report )
		{
			m_players.Remove( i );
			break;
		}
	}
	delete report;
}

void PlayerReporter::OnPlayerTeamChanged( CTerrorPlayer *player )
{
	PlayerReport_t *report = GetPlayerReport( player );
	if ( report && ( report->awardsTeam != player->GetTeamNumber() ||
		( report->awardTemplates.Count() && report->awardTemplates[0]->GetPlayer() != player ) ) )
		report->ResetAwards( player );
}
void PlayerReporter::OnBulletGroupFinished( CTerrorPlayer *player )
{
	PlayerReport_t *report = GetPlayerReport( player );
	if ( !report ) return;
	if ( report->pendingHit )
	{
		++report->shotsHit;
		++player->m_checkpointShotsHit;
		if ( report->pendingHeadshot ) ++report->headshots;
	}
	report->pendingHit = report->pendingHeadshot = false;
}

static CTerrorPlayer *ReporterPlayer( IGameEvent *event, const char *field )
{
	return dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( event->GetInt( field ) ) );
}

void PlayerReporter::FireGameEvent( IGameEvent *event )
{
	const char *name = event->GetName();
	if ( !Q_stricmp( name, "player_spawn" ) )
	{
		CTerrorPlayer *player = ReporterPlayer( event, "userid" );
		PlayerReport_t *report = GetPlayerReport( player );
		if ( report )
		{
			report->alive = true;
			report->team = player->GetTeamNumber();
			report->enteredStartArea = true;
			report->transitioned = false;
		}
		return;
	}
	if ( !Q_stricmp( name, "player_team" ) )
	{
		PlayerReport_t *report = GetPlayerReport( ReporterPlayer( event, "userid" ) );
		if ( report && report->team != event->GetInt( "team" ) )
		{
			report->alive = false;
			report->team = event->GetInt( "team" );
		}
		return;
	}
	if ( !Q_stricmp( name, "player_entered_start_area" ) )
	{
		CTerrorPlayer *player = ReporterPlayer( event, "userid" );
		PlayerReport_t *report = GetPlayerReport( player );
		if ( report && report->enteredStartArea )
		{
			report->enteredStartArea = false;
			if ( player->GetTeamNumber() == TEAM_SURVIVOR ) report->alive = true;
		}
		return;
	}
	if ( !Q_stricmp( name, "map_transition" ) )
	{
		FOR_EACH_LL( m_players, i )
		{
			m_players[i]->transitioned = true;
			if ( m_players[i]->team == TEAM_SURVIVOR ) m_players[i]->alive = true;
		}
		return;
	}
	if ( !Q_stricmp( name, "player_death" ) )
	{
		CBaseEntity *victim = event->GetInt( "userid" ) ? UTIL_PlayerByUserId( event->GetInt( "userid" ) ) : UTIL_EntityByIndex( event->GetInt( "entityid" ) );
		CTerrorPlayer *victimPlayer = dynamic_cast< CTerrorPlayer * >( victim );
		CTerrorPlayer *attacker = ReporterPlayer( event, "attacker" );
		if ( attacker && victim && !( victimPlayer && victimPlayer->IsGhost() ) ) // Native stale victim NULL: skip only dependent kill work.
		{
			PlayerReport_t *report = GetPlayerReport( attacker );
			if ( report && attacker != victim && victim->GetTeamNumber() == TEAM_SURVIVOR ) ++report->survivorKills;
			if ( victim->GetTeamNumber() == TEAM_INFECTED )
			{
				int zombieClass = victimPlayer ? victimPlayer->GetZombieClass() : 0;
				if ( !zombieClass && FClassnameIs( victim, "witch" ) ) zombieClass = 7;
				if ( zombieClass >= 0 && zombieClass <= 8 && report )
				{
					++report->zombieKills[zombieClass];
					attacker->RecordZombieKilled( zombieClass );
					const char *weapon = event->GetString( "weapon" );
					if ( !Q_strncmp( weapon, "melee", 5 ) || !Q_strncmp( weapon, "chainsaw", 8 ) )
					{
						++report->meleeKills;
						++attacker->m_checkpointMeleeKills;
					}
				}
			}
		}
		if ( victimPlayer )
		{
			PlayerReport_t *report = GetPlayerReport( victimPlayer );
			if ( report ) { ++report->deaths; report->alive = false; }
		}
		return;
	}
	if ( !Q_stricmp( name, "player_hurt" ) )
	{
		CTerrorPlayer *victim = ReporterPlayer( event, "userid" );
		CTerrorPlayer *attacker = ReporterPlayer( event, "attacker" );
		if ( !victim ) return;
		float damage = MIN( static_cast< float >( event->GetInt( "dmg_health" ) ), static_cast< float >( victim->GetMaxHealth() ) );
		PlayerReport_t *taken = GetPlayerReport( victim );
		PlayerReport_t *given = GetPlayerReport( attacker );
		bool friendly = attacker && attacker->GetTeamNumber() == victim->GetTeamNumber();
		if ( given && ( event->GetInt( "type" ) & 0x20000002 ) && !friendly )
		{
			given->pendingHit = true;
			if ( event->GetInt( "hitgroup" ) == 1 ) given->pendingHeadshot = true;
		}
		if ( !taken ) return;
		if ( victim == attacker ) { taken->selfDamage = static_cast< int >( taken->selfDamage + damage ); return; }
		if ( friendly ) taken->friendlyDamage = static_cast< int >( taken->friendlyDamage + damage );
		if ( victim->GetTeamNumber() == TEAM_SURVIVOR && attacker && attacker->GetTeamNumber() == TEAM_INFECTED )
		{
			int zombieClass = attacker->GetZombieClass();
			if ( zombieClass >= 0 && zombieClass <= 8 ) taken->damageTaken[zombieClass] = static_cast< int >( taken->damageTaken[zombieClass] + damage );
			return;
		}
		// Native also adds friendly damage to class zero; DamageTaken sums both.
		taken->damageTaken[0] = static_cast< int >( taken->damageTaken[0] + damage );
		return;
	}
	if ( !Q_stricmp( name, "infected_hurt" ) )
	{
		PlayerReport_t *report = GetPlayerReport( ReporterPlayer( event, "attacker" ) );
		if ( report && ( event->GetInt( "type" ) & 0x20000002 ) )
		{
			report->pendingHit = true;
			if ( event->GetInt( "hitgroup" ) == 1 ) report->pendingHeadshot = true;
		}
		return;
	}
	if ( !Q_stricmp( name, "pills_used" ) || !Q_stricmp( name, "adrenaline_used" ) )
	{
		CTerrorPlayer *player = ReporterPlayer( event, "subject" );
		PlayerReport_t *report = GetPlayerReport( player );
		if ( report )
		{
			if ( !Q_stricmp( name, "pills_used" ) ) ++report->pillsUsed;
			else { ++report->adrenalineUsed; ++player->m_checkpointAdrenalinesUsed; }
		}
		return;
	}
	if ( !Q_stricmp( name, "heal_success" ) )
	{
		CTerrorPlayer *player = ReporterPlayer( event, "userid" );
		PlayerReport_t *report = GetPlayerReport( player );
		if ( report )
		{
			if ( player == ReporterPlayer( event, "subject" ) ) ++report->medkitsUsed;
			else ++report->medkitsShared;
		}
		return;
	}
	if ( !Q_stricmp( name, "defibrillator_used" ) )
	{
		PlayerReport_t *subject = GetPlayerReport( ReporterPlayer( event, "subject" ) );
		if ( subject ) { --subject->deaths; subject->alive = true; }
		PlayerReport_t *report = GetPlayerReport( ReporterPlayer( event, "userid" ) );
		if ( report ) ++report->defibrillatorsUsed;
		return;
	}
	if ( !Q_stricmp( name, "revive_success" ) || !Q_stricmp( name, "player_incapacitated" ) )
	{
		PlayerReport_t *report = GetPlayerReport( ReporterPlayer( event, "userid" ) );
		if ( report )
		{
			if ( !Q_stricmp( name, "revive_success" ) ) ++report->revives;
			else ++report->incaps;
		}
		return;
	}
	if ( !Q_stricmp( name, "finale_win" ) )
	{
		for ( int i = 1; i <= 32; ++i )
		{
			CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
			if ( !player ) continue;
			if ( player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR )
			{
				PlayerReport_t *report = GetPlayerReport( player );
				if ( report && report->team == TEAM_SURVIVOR && report->alive ) report->alive = true;
			}
			if ( !TerrorGameRules()->IsSacrificeEscapee( player ) )
			{
				PlayerReport_t *report = GetPlayerReport( player );
				if ( report ) { ++report->deaths; report->alive = false; }
			}
		}
		return;
	}
	if ( !Q_stricmp( name, "round_end" ) )
	{
		for ( int i = 1; i <= 32; ++i )
		{
			CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
			PlayerReport_t *report = GetPlayerReport( player );
			if ( report && event->GetInt( "reason" ) == 1 && player->IsAlive() && player->IsIncapacitated() )
			{ ++report->deaths; report->alive = false; }
		}
		return;
	}
	if ( !Q_stricmp( name, "weapon_fire" ) )
	{
		CTerrorPlayer *player = ReporterPlayer( event, "userid" );
		PlayerReport_t *report = GetPlayerReport( player );
		if ( !report ) return;
		const char *weapon = event->GetString( "weapon" );
		if ( !Q_stricmp( weapon, "molotov" ) ) { ++report->molotovsUsed; ++player->m_checkpointMolotovsUsed; }
		else if ( !Q_stricmp( weapon, "pipe_bomb" ) ) { ++report->pipebombsUsed; ++player->m_checkpointPipebombsUsed; }
		else if ( !Q_stricmp( weapon, "vomitjar" ) ) { ++report->boomerBilesUsed; ++player->m_checkpointBoomerBilesUsed; }
		else { ++report->shotsFired; ++player->m_checkpointShotsFired; }
		return;
	}
	if ( !Q_stricmp( name, "award_earned" ) )
	{
		PlayerReport_t *report = GetPlayerReport( ReporterPlayer( event, "userid" ) );
		const int award = event->GetInt( "award" );
		if ( report && (unsigned int)award < ARRAYSIZE( report->awards ) ) ++report->awards[award]; // Standing OOB-write skip only.
		return;
	}
	if ( !Q_stricmp( name, "server_shutdown" ) ) Clear();
}

// Native06F4 is retained authority for these existing real PlayerReport_t fields.
const char *PlayerReport_t::GetName() const
{
	CBasePlayer *player = UTIL_PlayerByUserId( userID );
	if ( !player || !player->IsPlayer() ) return "";
	player = UTIL_PlayerByUserId( userID ); // Native2BB080 does the lookup TWICE.
	CTerrorPlayer *terror = player && player->IsPlayer() ? static_cast< CTerrorPlayer * >( player ) : NULL;
	return terror->GetPlayerName(); // Native second-lookup omission is not repaired.
}
const char *PlayerReport_t::FormatVictims( const char *prefix ) const
{
	static char text[256]; // Native849668; shared across records, not stored history.
	Q_strncpy( text, UTIL_VarArgs( "%s: ", prefix ), sizeof( text ) );
	for ( int i = 0; i < 9; ++i )
		if ( zombieKills[i] != 0 ) Q_strncat( text, UTIL_VarArgs( "%s(%d)  ", GetZombieClassName( i ), zombieKills[i] ), sizeof( text ) );
	return text;
}
const char *PlayerReport_t::FormatInjuries( const char *prefix ) const
{
	static char text[256]; // Native849568.
	Q_strncpy( text, UTIL_VarArgs( "%s: ", prefix ), sizeof( text ) );
	if ( selfDamage != 0 ) Q_strncat( text, UTIL_VarArgs( "SELF(%d)  ", selfDamage ), sizeof( text ) );
	if ( friendlyDamage != 0 ) Q_strncat( text, UTIL_VarArgs( "FRIEND(%d)\n", friendlyDamage ), sizeof( text ) );
	for ( int i = 0; i < 9; ++i )
		if ( damageTaken[i] != 0 ) Q_strncat( text, UTIL_VarArgs( "%s(%d)  ", GetZombieClassName( i ), damageTaken[i] ), sizeof( text ) );
	return text;
}
const char *PlayerReport_t::FormatDeath( const char *prefix ) const
{
	static char text[256]; // Native849768, also directly used by2BB470.
	Q_strncpy( text, UTIL_VarArgs( "%s: Incapacitated %d times, Died %d times", prefix, incaps, deaths ), sizeof( text ) );
	return text;
}
const char *PlayerReport_t::FormatMedic( const char *prefix ) const
{
	static char text[256]; // Native849868.
	Q_strncpy( text, UTIL_VarArgs( "%s: ", prefix ), sizeof( text ) );
	Q_strncat( text, UTIL_VarArgs( "Used %d pills. ", pillsUsed ), sizeof( text ) );
	Q_strncat( text, UTIL_VarArgs( "Healed self %dx. ", medkitsUsed ), sizeof( text ) );
	Q_strncat( text, UTIL_VarArgs( "Healed friends %dx. ", medkitsShared ), sizeof( text ) );
	return text;
}
const char *PlayerReport_t::FormatSkill( const char *prefix ) const
{
	static char text[256]; // Native849968.
	Q_strncpy( text, UTIL_VarArgs( "%s: ", prefix ), sizeof( text ) );
	if ( shotsFired != 0 )
	{
		const int32 percentage = static_cast< int32 >( static_cast< uint32 >( shotsHit ) * 100u ); // Native low32 IMUL.
		Q_strncat( text, UTIL_VarArgs( "Accuracy = %d%%.  %d rounds fired (%d hits, %d headshots).", percentage / shotsFired, shotsFired, shotsHit, headshots ), sizeof( text ) );
	}
	else Q_strncat( text, "No rounds fired", sizeof( text ) );
	return text;
}
void PlayerReport_t::PrintTo( CTerrorPlayer *recipient, const char * ) const
{
	// Native2BB470's supplied title argument is unused. Preserve all SEVEN sends.
	TerrorMessageText( recipient, UTIL_VarArgs( "============= %s =============\n", GetName() ), 0 );
	TerrorMessageText( recipient, FormatVictims( "Victims" ), 0 );
	TerrorMessageText( recipient, FormatInjuries( "Injuries" ), 0 );
	TerrorMessageText( recipient, FormatDeath( "Death" ), 0 );
	TerrorMessageText( recipient, FormatMedic( "Medic" ), 0 );
	TerrorMessageText( recipient, FormatSkill( "Skill" ), 0 );
	TerrorMessageText( recipient, "\n", 0 );
}

namespace
{
bool IsReportCategory( const char *value, const char *category )
{
	// Native2BB3E0 compares only the shorter string's length. Empty matches too.
	if ( !value || !category ) return false;
	return Q_strncmp( value, category, MIN( Q_strlen( value ), Q_strlen( category ) ) ) == 0;
}
}
void PlayerReporter::PrintReport( CTerrorPlayer *recipient, const char *who, const char *category, const char *title )
{
	FOR_EACH_LL( m_players, i )
	{
		PlayerReport_t *report = m_players[i];
		if ( !who || !Q_strcmp( who, "me" ) )
		{
			if ( report->userID != engine->GetPlayerUserId( recipient->edict() ) ) continue;
		}
		else if ( Q_strcmp( who, "all" ) )
		{
			if ( !Q_strcmp( who, "survivor" ) && report->team != TEAM_SURVIVOR ) continue;
			// Native "survivor" ALSO falls through to exact player-name matching.
			if ( Q_strcmp( who, report->GetName() ) ) continue;
		}
		if ( !category ) report->PrintTo( recipient, title );
		else if ( IsReportCategory( category, "injuries" ) ) report->FormatInjuries( report->GetName() );
		else if ( IsReportCategory( category, "victims" ) ) report->FormatVictims( report->GetName() );
		else if ( IsReportCategory( category, "death" ) ) report->FormatDeath( report->GetName() );
		else if ( IsReportCategory( category, "medic" ) ) report->FormatMedic( report->GetName() );
		else if ( IsReportCategory( category, "victims" ) ) report->FormatVictims( report->GetName() );
		else if ( IsReportCategory( category, "skill" ) ) report->FormatSkill( report->GetName() );
		// Deliberately no send in native category-specific paths: the formatters
		// only write their actual static buffers. Do not repair that native bug.
	}
}
CON_COMMAND_F( report, "", 0x00000004 )
{
	CTerrorPlayer *recipient = ToTerrorPlayer( UTIL_GetCommandClient() );
	if ( recipient ) GetPlayerReporter().PrintReport( recipient, args.ArgC() >= 2 ? args[1] : NULL,
		args.ArgC() >= 3 ? args[2] : NULL, "Results for This Round" );
}
