//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "world.h"
#include "l4d2/sound/terror_sound_gamedata.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "engine/IEngineSound.h"
#include "recipientfilter.h"
#include "usermessages.h"
#include "team.h"
#include "tier1/KeyValues.h"
#include "l4d2/sound/terror_music_console_server_cvars.inc"
#include "tier0/memdbgon.h"

static ConVar music_dynamic_dodamage_decay( "music_dynamic_dodamage_decay", "4", 0x00004004 );
static ConVar music_dynamic_dodamage_increment( "music_dynamic_dodamage_increment", ".02", 0x00004004 );
ConVar music_dynamic_update_interval( "music_dynamic_update_interval", ".1", 0x00004004, "interval between music status/action update" );
ConVar music_manager( "music_manager", "1", 0x00004004, "Using the new music manager system." );
extern ConVar music_calm_min_interval;
static ConVar music_dynamic_scavenge_beat( "music_dynamic_scavenge_beat", "1", FCVAR_CHEAT | FCVAR_GAMEDLL, "play beat music during scavenge mode" );
static ConVar music_large_area_reveal_repeat_threshold( "music_large_area_reveal_repeat_threshold", "60", 0x00004004,
	"The minimum time in seconds between performances of this music" );

BEGIN_SEND_TABLE_NOBASE( Music, DT_Music )
	SendPropFloat( SENDINFO( nv_m_zombatMusic3 ), 8, 0, 0.0f, 1.0f ),
	SendPropFloat( SENDINFO( nv_m_CIDamageDuck ), 8, 0, 0.0f, 1.0f ),
	SendPropFloat( SENDINFO( nv_m_CIDamageMob ), 8, 0, 0.0f, 1.0f ),
	SendPropFloat( SENDINFO( nv_m_zombatMusic2 ), 8, 0, 0.0f, 1.0f ),
	SendPropInt( SENDINFO( nv_m_inCheckpoint ), 5, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( nv_m_ambientVolume ), 8, 0, 0.0f, 1.0f ),
	SendPropFloat( SENDINFO( nv_m_witchRage ), 8, 0, 0.0f, 1.0f ),
	SendPropFloat( SENDINFO( m_fNVAdrenaline ), 8, 0, 0.0f, 1.0f ),
END_SEND_TABLE()

// Original sends each player's music commands reliably to all real clients.
// The player index in the payload selects that client's embedded Music owner;
// it is NOT the recipient filter. This also supplies spectator music state.
static void MusicRecipients( CRecipientFilter &filter )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( player && player->IsPlayer() && !player->IsSplitScreenPlayer() )
			filter.AddRecipient( player );
	}
	filter.MakeReliable();
}
static void WriteMusicName( const char *name )
{
	const unsigned int hash = GetMusicSoundNameHash( name );
	const bool valid = GetMusicSoundNameForHash( hash ) != NULL;
	WRITE_BOOL( valid );
	if ( valid ) WRITE_UBITLONG( hash, 32 );
	else WRITE_STRING( name );
}
static void WriteMusicHeader( MusicCommand command, int player )
{
	WRITE_UBITLONG( command, 3 );
	WRITE_UBITLONG( player, 6 );
}

Music::Music() : m_me( NULL ) { Reset(); }
Music::~Music()
{
	// S2B0A50 sends the real stop-all command before member destruction.
	Stop( "all", -1.0f, false );
}
void Music::SetOwner( CTerrorPlayer *player )
{
	m_me = player;
	m_updateTimer.Start( (float)( music_dynamic_update_interval.GetFloat() * 0.125 * player->entindex() ) );
}
bool Music::IsL4D1() const { return TerrorGameRules()->GetSurvivorSet() == 1; }
void Music::Reset()
{
	nv_m_CIDamageDuck = 1.0f;
	nv_m_CIDamageMob = 0.0f;
	nv_m_zombatMusic2 = 0.0f;
	nv_m_zombatMusic3 = 0.0f;
	nv_m_inCheckpoint = 0;
	nv_m_ambientVolume = 0.0f;
	nv_m_witchRage = 0.0f;
	m_fNVAdrenaline = 0.0f;
	m_isPounced = m_isPummeled = m_isJockeyRidden = m_isTongued = m_isIT = false;
	m_stopCombatMusic = true;
	m_damageDealt = 0.0f;
	m_damageDealtSignal = 0.0f;
	m_damageDealtDecayTimer.Invalidate();
	m_updateTimer.Start( (float)( music_dynamic_update_interval.GetFloat() * 0.125 * ( m_me ? m_me->entindex() : 0 ) ) );
	m_largeAreaRevealTimer.Start( music_large_area_reveal_repeat_threshold.GetFloat() );
	ResetDynamicState();
}
void Music::ResetAfterRescue()
{
	Reset();
	GeneralCommand( "Reset", NULL, NULL, -10 );
}
void Music::Stop( const char *event, float fade, bool track )
{
	if ( !music_manager.GetBool() )
		return;
	CRecipientFilter filter;
	MusicRecipients( filter );
	UserMessageBegin( filter, "MusicCmd" );
	WriteMusicHeader( track ? MUSIC_STOP_TRACK : MUSIC_STOP, m_me->entindex() );
	WriteMusicName( event );
	WRITE_FLOAT( fade );
	MessageEnd();
}
void Music::GeneralCommand( const char *command, const char *arg1, const char *arg2, int entity )
{
	if ( !music_manager.GetBool() )
		return;
	CRecipientFilter filter;
	MusicRecipients( filter );
	UserMessageBegin( filter, "MusicCmd" );
	WriteMusicHeader( MUSIC_GENERAL, m_me->entindex() );
	WRITE_STRING( command ? command : "" );
	WRITE_STRING( arg1 ? arg1 : "" );
	WRITE_STRING( arg2 ? arg2 : "" );
	WRITE_UBITLONG( entity + 1, 11 );
	MessageEnd();
}
void Music::Play( const char *event, int emitEntity, float duration, bool otherSurvivors, bool spatial )
{
	if ( !music_manager.GetBool() || !m_me || m_me->GetTeamNumber() == TEAM_L4D1_SURVIVOR || !event )
		return;
	if ( emitEntity < 0 || m_me->entindex() < 0 )
	{
		DevMsg( "Music: invalid emit/play entity index %i/%i\n", emitEntity, m_me->entindex() );
		return;
	}
	if ( !Q_stristr( event, "Checkpoint" ) )
	{
		CSoundParameters parameters;
		if ( !CBaseEntity::GetParametersForSound( event, parameters, NULL ) )
		{
			DevMsg( "PlayCommand: Unable to find sound %s\n", event );
			return;
		}
		if ( GetSoundScriptGameData( event ) &&
			( ( !m_me->IsAlive() && !MusicScriptHasParameter( event, "AFTER_DEATH" ) ) ||
			( TheDirector->IsMissionLostOrWon() && !MusicScriptHasParameter( event, "AFTER_MISSION_END" ) ) ) )
			return;
	}
	CRecipientFilter filter;
	MusicRecipients( filter );
	CBaseEntity *emitter = spatial ? UTIL_EntityByIndex( emitEntity ) : NULL;
	CTeam *survivorTeam = otherSurvivors ? GetGlobalTeam( TEAM_SURVIVOR ) : NULL;
	for ( int i = 0; i < ( otherSurvivors ? survivorTeam->GetNumPlayers() : 1 ); ++i )
	{
		int playEntity = m_me->entindex();
		if ( otherSurvivors )
		{
			// S2ABAE7 uses team order, not global entity-slot order. Re-read
			// the player after IsAlive, before selecting its transport owner.
			if ( !survivorTeam->GetPlayer( i )->IsAlive() )
				continue;
			playEntity = survivorTeam->GetPlayer( i )->entindex();
			if ( playEntity == m_me->entindex() )
				continue;
		}
		UserMessageBegin( filter, "MusicCmd" );
		WriteMusicHeader( emitter ? MUSIC_PLAY_POSITION : MUSIC_PLAY, playEntity );
		WriteMusicName( event );
		WRITE_FLOAT( duration );
		WRITE_UBITLONG( emitEntity + 1, 11 );
		if ( emitter )
			MessageWriteBitVecIntegral( emitter->GetAbsOrigin() );
		MessageEnd();
	}
}
void Music::PlayPersonalAndTeamHit( const char *event, const char *hit )
{
	Play( hit, m_me->entindex(), -1.0f, true );
	Play( event );
}
void Music::PlayCampaignEvent( const char *event )
{
	char sound[128];
	Q_snprintf( sound, sizeof( sound ), "%s_%s", event, TerrorGameRules()->IsSurvivalMode() ? "Survival" : GetWorldEntity()->GetMusicPostfix() );
	Play( sound );
}

void Music::OnMissionStart() { Reset(); GeneralCommand( "Reset" ); if ( music_manager.GetBool() ) nv_m_inCheckpoint = 1; }
void Music::OnCheckpointReached() { Reset(); GeneralCommand( "Reset" ); if ( music_manager.GetBool() ) nv_m_inCheckpoint = 2; }
void Music::OnFinaleCheckpointReached() { Reset(); GeneralCommand( "Reset" ); if ( music_manager.GetBool() ) nv_m_inCheckpoint = 3; }
void Music::OnEnterSurvival()
{
	Reset();
	GeneralCommand( "Reset" );
	if ( music_manager.GetBool() ) nv_m_inCheckpoint = 4;
	m_safeAtmosphereTimer.Start( music_calm_min_interval.GetFloat() + 10.0f );
	m_dangerAtmosphereTimer.Start( music_calm_min_interval.GetFloat() );
}
void Music::OnLeaveMissionStartArea()
{
	if ( music_manager.GetBool() ) nv_m_inCheckpoint = 0;
	Stop( "checkpoint_0", 3.0f );
	PlayCampaignEvent( "Event.LeavingSafety" );
	PlayCampaignEvent( "Event.StartAtmosphere" );
	m_safeAtmosphereTimer.Start( music_calm_min_interval.GetFloat() + 10.0f );
	m_dangerAtmosphereTimer.Start( music_calm_min_interval.GetFloat() );
}
void Music::OnLeaveCheckpoint()
{
	if ( music_manager.GetBool() ) nv_m_inCheckpoint = 0;
	Stop( "checkpoint_2", 3.0f );
	if ( !TerrorGameRules()->IsScavengeMode() )
	{
		PlayCampaignEvent( "Event.LeavingSafety" );
		PlayCampaignEvent( "Event.StartAtmosphere" );
		m_safeAtmosphereTimer.Start( music_calm_min_interval.GetFloat() + 10.0f );
		m_dangerAtmosphereTimer.Start( music_calm_min_interval.GetFloat() );
	}
}
void Music::OnEnterCheckpoint() { Stop( "all", 1.0f ); GeneralCommand( "GameOver" ); Play( IsL4D1() ? "Event.SafeRoom_L4D1" : "Event.SafeRoom" ); }
void Music::OnSurvivalStart() { Stop( "all", 1.0f ); Play( "Event.SurvivalStart" ); }
void Music::OnMissionLost()
{
	if ( music_manager.GetBool() ) nv_m_inCheckpoint = 0;
	Stop( "all" );
	GeneralCommand( "GameOver" );
	Play( IsL4D1() ? "Event.ScenarioLose_L4D1" : "Event.ScenarioLose" );
}
void Music::OnMissionWon()
{
	Stop( "all" );
	GeneralCommand( "GameOver" );
	Play( IsL4D1() ? "Event.ScenarioWin_L4D1" : "Event.ScenarioWin" );
}
void Music::OnMissionWonDone() { Stop( IsL4D1() ? "Event.ScenarioWin_L4D1" : "Event.ScenarioWin", 2.0f ); }
void Music::OnFinaleFinalBattle() { Play( IsL4D1() ? "Event.FinalBattle_L4D1" : "Event.FinalBattle" ); }
void Music::OnPlayerRestored( CTerrorPlayer *oldPlayer )
{
	Reset();
	if ( oldPlayer )
	{
		char index[16];
		Q_snprintf( index, sizeof( index ), "%d", oldPlayer->entindex() );
		GeneralCommand( "PlayerRestored", index, "" );
	}
	else
		GeneralCommand( "Reset" );
}
void Music::OnDown() { m_stopCombatMusic = true; PlayPersonalAndTeamHit( IsL4D1() ? "Event.Down_L4D1" : "Event.Down", IsL4D1() ? "Event.DownHit_L4D1" : "Event.DownHit" ); }
void Music::OnDownEnded() { Stop( IsL4D1() ? "Event.Down_L4D1" : "Event.Down", 2.0f ); }
void Music::OnDownAndBeaten() { DevMsg( "%s Music::OnDownAndBeaten\n", m_me->GetPlayerName() ); }
void Music::OnDownAndBeatenEnded() { DevMsg( "%s Music::OnDownAndBeatenEnded\n", m_me->GetPlayerName() ); }
void Music::OnBleedingOut()
{
	// Original spelling is OnDown, not Down. Do not silently repair it.
	m_stopCombatMusic = true;
	Stop( IsL4D1() ? "Event.OnDown_L4D1" : "Event.OnDown" );
	PlayPersonalAndTeamHit( IsL4D1() ? "Event.BleedingOut_L4D1" : "Event.BleedingOut", IsL4D1() ? "Event.BleedingOutHit_L4D1" : "Event.BleedingOutHit" );
}
void Music::OnBleedingOutEnded() { Stop( IsL4D1() ? "Event.BleedingOut_L4D1" : "Event.BleedingOut", 1.0f ); }
void Music::OnLedgeHangTwoHands()
{
	m_stopCombatMusic = true;
	PlayPersonalAndTeamHit( "Event.LedgeHangTwoHands", "Event.LedgeHangTwoHandsHit" );
}
void Music::OnLedgeHangOneHand() { PlayPersonalAndTeamHit( "Event.LedgeHangOneHand", "Event.LedgeHangOneHandHit" ); }
void Music::OnLedgeHangFingers() { PlayPersonalAndTeamHit( "Event.LedgeHangFingers", "Event.LedgeHangFingersHit" ); }
void Music::OnLedgeHangBleedingOut() { PlayPersonalAndTeamHit( "Event.LedgeHangAboutToFall", "Event.LedgeHangAboutToFallHit" ); }
void Music::StopLedgeEvents( float fade )
{
	Stop( "Event.LedgeHangTwoHands", fade );
	Stop( "Event.LedgeHangOneHand", fade );
	Stop( "Event.LedgeHangFingers", fade );
	Stop( "Event.LedgeHangAboutToFall", fade );
	Stop( "Event.LedgeHangFalling", fade );
}
void Music::OnSavedFromLedgeHang() { StopLedgeEvents( 2.0f ); }
void Music::OnDroppedFromLedgeHang() { StopLedgeEvents( -1.0f ); }
void Music::OnFellToDeathFromLedgeHang() { Play( "Event.LedgeHangFalling" ); }
void Music::OnVomitedUpon() { m_isIT = true; m_stopCombatMusic = true; PlayPersonalAndTeamHit( "Event.VomitInTheFace", "Event.BoomerHit" ); }
void Music::OnITExpired() { m_isIT = false; Stop( "Event.VomitInTheFace" ); }
void Music::OnPulledByTongue() { Play( "Event.SmokerDrag" ); Play( "Event.SmokerDragHit", m_me->entindex(), -1.0f, true ); m_stopCombatMusic = true; }
void Music::OnChoked() { m_isTongued = true; m_stopCombatMusic = true; PlayPersonalAndTeamHit( "Event.SmokerChoke", "Event.SmokerChokeHit" ); }
void Music::OnReleasedByTongue() { m_isTongued = false; Stop( "Event.SmokerChoke" ); Stop( "Event.SmokerDrag" ); }
void Music::OnPounced() { m_isPounced = true; m_stopCombatMusic = true; PlayPersonalAndTeamHit( "Event.HunterPounce", "Event.HunterHit" ); }
void Music::OnPounceEnded() { m_isPounced = false; Stop( "Event.HunterPounce" ); }
void Music::OnJockeyRide() { m_isJockeyRidden = true; m_stopCombatMusic = true; PlayPersonalAndTeamHit( "Event.JockeyRide", "Event.JockeyHit" ); }
void Music::OnJockeyRideEnded() { m_isJockeyRidden = false; Stop( "Event.JockeyRide" ); }
void Music::OnStruck() { PlayPersonalAndTeamHit( "Event.ChargerSmash", "Event.ChargerHit" ); }
void Music::OnStruckEnded() { Stop( "Event.ChargerSmash", 1.0f ); }
void Music::OnPummeled() { m_isPummeled = true; m_stopCombatMusic = true; PlayPersonalAndTeamHit( "Event.ChargerSlam", "Event.ChargerSlamHit" ); }
void Music::OnPummeledEnd() { m_isPummeled = false; Stop( "Event.ChargerSlam" ); }
void Music::OnSpitterSpit( int entity ) { Play( "Event.SpitterSpit", entity, -1.0f, false, true ); }
void Music::OnSpitterSpitEnded() { Stop( "Event.SpitterSpit", 1.0f ); }
void Music::OnSpitterSpitAttack() { Play( "Event.SpitterBurn" ); }
void Music::OnSpitterSpitAttackEnded() { Stop( "Event.SpitterBurn", 1.0f ); }
void Music::OnAdrenalineUsed()
{
	m_fNVAdrenaline = 1.0f;
	if ( m_me->IsNetClient() ) { CSingleUserRecipientFilter filter( m_me ); enginesound->SetPlayerDSP( filter, 143, false ); }
}
void Music::OnAdrenalineExpired()
{
	if ( m_me->IsNetClient() ) { CSingleUserRecipientFilter filter( m_me ); enginesound->SetPlayerDSP( filter, 0, false ); }
	m_fNVAdrenaline = 0.0f;
}
void Music::OnBossApproaching() { m_stopCombatMusic = true; m_bossMusicActive = true; if ( TheDirector->ShouldPlayBossMusic() ) Play( "Event.Tank" ); }
void Music::OnBossDefeated( int zombieClass )
{
	const char *tracks[] = { NULL, "Event.SmokerAlert", "Event.BoomerAlert", "Event.HunterAlert", "Event.SpitterAlert", "Event.JockeyAlert", "Event.ChargerAlert" };
	if ( zombieClass > 0 && zombieClass < ARRAYSIZE( tracks ) ) Stop( tracks[zombieClass], 0.0f, true );
	if ( zombieClass == 6 ) { OnStruckEnded(); OnPummeledEnd(); }
	if ( zombieClass == 8 && TheDirector->GetTankCount() <= 0 ) { m_bossMusicActive = false; m_hadTank = false; Stop( "tank", 2.0f, true ); Stop( "tank2", 2.0f, true ); }
}
void Music::OnLandmarkRevealed( const char *landmark ) { Play( landmark ? landmark : "" ); }
void Music::OnSmallAreaRevealed() { DevMsg( "%s Music::OnSmallAreaRevealed\n", m_me->GetPlayerName() ); }
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void Music::OnLargeAreaRevealed()
{
	DevMsg( "%s Music::OnLargeAreaRevealed\n", m_me->GetPlayerName() );
	// S2AE950 admits equality but rejects an unordered deadline.
	if ( !( static_cast< double >( gpGlobals->curtime ) >=
		static_cast< double >( m_largeAreaRevealTimer.m_timestamp.Get() ) ) ) return;
	Play( IsL4D1() ? "Event.LargeAreaRevealed_L4D1" : "Event.LargeAreaRevealed" );
	m_largeAreaRevealTimer.Start( music_large_area_reveal_repeat_threshold.GetFloat() );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
void Music::OnApproachingScavengeRoundWin() { Stop( "all", 1.0f ); Play( "Event.ApproachingScavengeRoundWin" ); }
void Music::OnScavengeRoundStart() { Stop( "all", 1.0f ); Play( "Event.ScavengeRoundStart" ); }
void Music::OnScavengeRoundHalftime() { Stop( "all", 1.0f ); Play( "Event.ScavengeRoundHalftime" ); }
void Music::OnScavengeRoundEnd() { Stop( "all", 1.0f ); Play( "Event.ScavengeRoundEnd" ); }
void Music::OnScavengeOvertimeStart() { Stop( "all", 1.0f ); Play( "Event.ScavengeOvertimeStart" ); }
void Music::OnScavengeOvertimeTimeAdded() { Stop( "all", 1.0f ); Play( "Event.ScavengeOvertimeTimeAdded" ); }
void Music::OnScavengeLevelChange( int level )
{
	if ( !music_dynamic_scavenge_beat.GetBool() ) return;
	if ( level < 1 ) Stop( "all", 1.0f );
	level = MIN( level, 10 );
	char event[64];
	if ( level == 0 ) Play( "Event.MomentOfSilence" );
	else { Q_snprintf( event, sizeof( event ), "Event.ScavengeLevel_%02d", level ); Play( event ); }
}

void Music::OnKilled( bool wasBleedingOut )
{
	int livingSurvivors = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->IsConnected() && player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR )
			++livingSurvivors;
	}
	// The original additionally excludes both split-screen children and hosts.
	// The target has no admitted split-screen slots (see paired source gate).
	if ( livingSurvivors <= 1 ) return;
	Stop( "all", 2.0f );
	if ( wasBleedingOut ) Play( IsL4D1() ? "Event.BleedingOutEnd_L4D1" : "Event.BleedingOutEnd" );
	PlayPersonalAndTeamHit( IsL4D1() ? "Event.SurvivorDeath_L4D1" : "Event.SurvivorDeath", IsL4D1() ? "Event.SurvivorDeathHit_L4D1" : "Event.SurvivorDeathHit" );
}
void Music::OnDeathEnded() { Stop( IsL4D1() ? "Event.SurvivorDeath_L4D1" : "Event.SurvivorDeath" ); }

CON_COMMAND_F( sv_play_music, "Play some music through the music engine", 0x00004004 )
{
	if ( args.ArgC() < 2 ) { Msg( "USAGE: sv_play_music <name of gamesound music>\n" ); return; }
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->IsConnected() && player->IsAlive() )
			player->GetMusic()->Play( args[1], 0, -1.0f, false, true );
	}
}
CON_COMMAND_F( sv_stop_music, "Stop some music through the music engine", 0x00004004 )
{
	if ( args.ArgC() < 2 ) { Msg( "USAGE: sv_stop_music <name of gamesound music>\n" ); return; }
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->IsConnected() && player->IsAlive() )
			player->GetMusic()->Stop( args[1] );
	}
}

// ZombieBotBody::OnInjured 0x3F5375 writes the real embedded Music+8.
void Music::OnDamageDealt( float damage ) { m_damageDealt += damage; }
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void Music::UpdateDamageDealt()
{
	// Original 0x2B1710 applies 0x2ABC50 to Music+72, not a damage statistic.
	const float decay = music_dynamic_dodamage_decay.GetFloat();
	if ( !( m_damageDealt > 0.0f ) )
	{
		const double remainingTime = static_cast< double >( m_damageDealtDecayTimer.m_timestamp.Get() ) - gpGlobals->curtime;
		const float remaining = remainingTime > decay ? decay : ( 0.0 > remainingTime ? 0.0f : static_cast< float >( remainingTime ) );
		m_damageDealtSignal = decay == 0.0f ? ( remaining >= decay ? 1.0f : 0.0f ) : remaining / decay;
	}
	else
	{
		const float value = m_damageDealtSignal + m_damageDealt * music_dynamic_dodamage_increment.GetFloat();
		m_damageDealtSignal = value > 1.0f ? 1.0f : ( 0.0f > value ? 0.0f : value );
		m_damageDealtDecayTimer.Start( m_damageDealtSignal * decay );
	}
	// Retain the original comparison order, including unusual cvar/float values.
	m_damageDealtSignal = m_damageDealtSignal > 1.0f ? 1.0f : ( 0.0f > m_damageDealtSignal ? 0.0f : m_damageDealtSignal );
	m_damageDealt = 0.0f;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S2AA750 / S5977F0. The original command permits an empty suffix and checks
// the actual command client, player identity, life state and survivor team.
CON_COMMAND_F( snd_set_custom_zombat_suffix, "Overrides the per campaign zombat music suffix", 0x00004004 )
{
	CTerrorPlayer *player = ToTerrorPlayer( UTIL_GetCommandClient() );
	if ( player && player->IsPlayer() && player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR )
		player->GetMusic()->SetMusicVariant( args.ArgC() > 1 ? args[1] : "" );
}

#include "l4d2/sound/terror_music_console_server_callbacks.inc"
