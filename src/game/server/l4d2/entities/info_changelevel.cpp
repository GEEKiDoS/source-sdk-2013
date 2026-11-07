//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/info_changelevel.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/entities/terror_transition_entities.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/director/zombiemanager.h"
#include "NextBot/NextBotManager.h"
#include "ai_speech.h"
#include "igameevents.h"
#include "rumble_shared.h"
#include <float.h>
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_changelevel, InfoChangelevel );
LINK_ENTITY_TO_CLASS( trigger_changelevel, InfoChangelevel );
BEGIN_DATADESC( InfoChangelevel )
	DEFINE_AUTO_ARRAY( m_mapName, FIELD_CHARACTER ),
	DEFINE_AUTO_ARRAY( m_landmarkName, FIELD_CHARACTER ),
	DEFINE_ENTITYFUNC( TouchChangeLevel ),
	{ FIELD_VOID, "OnCheckpointDoorOpened", { 0, 0 }, 1, FTYPEDESC_INPUT, "CheckpointDoorOpened", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputCheckpointDoorOpened ) },
	{ FIELD_VOID, "OnCheckpointDoorClosed", { 0, 0 }, 1, FTYPEDESC_INPUT, "CheckpointDoorClosed", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputCheckpointDoorClosed ) },
	DEFINE_THINKFUNC( ChangelevelThink ),
	DEFINE_THINKFUNC( ChangeLevelSpeechDelay ),
	DEFINE_THINKFUNC( ChangeLevelNow ),
END_DATADESC()

static ConVar changelevel_pause_interval( "changelevel_pause_interval", "8", 0x00004004 );
static ConVar changelevel_inhibit( "changelevel_inhibit", "0", 0x00004004 );
static ConVarRef allow_all_bot_survivor_team( "allow_all_bot_survivor_team" );
static char s_transitionLandmark[32];
static Vector s_oldLandmarkOrigin;
static Vector s_newLandmarkOrigin;

const char *GetTerrorTransitionLandmarkName() { return s_transitionLandmark; }
void SetTerrorTransitionLandmark( const char *name, const Vector &origin )
{
	Q_strncpy( s_transitionLandmark, name, sizeof( s_transitionLandmark ) );
	s_oldLandmarkOrigin = origin;
}
void ClearTerrorTransitionLandmark()
{
	s_transitionLandmark[0] = '\0';
	s_oldLandmarkOrigin.Init();
}
Vector GetTerrorTransitionOffset()
{
	CBaseEntity *landmark = gEntList.FindEntityByName( NULL, s_transitionLandmark );
	Vector origin = vec3_origin;
	if ( landmark )
	{
		origin = landmark->GetAbsOrigin();
		s_newLandmarkOrigin = origin;
	}
	// Missing landmark deliberately leaves the cached NEW origin unchanged.
	return origin - s_oldLandmarkOrigin;
}
void FixupTerrorTransitionPosition( Vector &origin, QAngle &angles )
{
	if ( !Q_stricmp( s_transitionLandmark, "coldstream3_coldstream4" ) )
	{
		origin.x = s_newLandmarkOrigin.x + ( origin.x - s_newLandmarkOrigin.x ) * -1.0f;
		origin.y = s_newLandmarkOrigin.y + ( origin.y - s_newLandmarkOrigin.y ) * -1.0f;
		origin.z = ( origin.z - s_newLandmarkOrigin.z ) + s_newLandmarkOrigin.z;
		angles.y += 180.0f;
	}
}
void ChangeTerrorLevel( const char *map, const char *landmark )
{
	TheDirector->ShutdownScripts( 3, map );
	engine->ChangeLevel( map, landmark );
}

InfoChangelevel::InfoChangelevel()
{
	m_speechTimer.Invalidate();
}
bool InfoChangelevel::KeyValue( const char *key, const char *value )
{
	if ( !Q_stricmp( key, "map" ) )
	{
		if ( Q_strlen( value ) >= sizeof( m_mapName ) ) Warning( "Map name '%s' too long (%d chars)\n", value, 32 );
		Q_strncpy( m_mapName, value, sizeof( m_mapName ) );
		return true;
	}
	if ( !Q_stricmp( key, "landmark" ) )
	{
		if ( Q_strlen( value ) >= sizeof( m_landmarkName ) ) Warning( "Landmark name '%s' too long (%d chars)\n", value, 32 );
		Q_strncpy( m_landmarkName, value, sizeof( m_landmarkName ) );
		return true;
	}
	return BaseClass::KeyValue( key, value );
}
bool InfoChangelevel::IsOutward() const
{
	return m_mapName[0] && Q_stricmp( m_mapName, STRING( gpGlobals->mapname ) ) && m_landmarkName[0];
}
void InfoChangelevel::Spawn()
{
	if ( !m_mapName[0] ) Msg( "an info_changelevel doesn't have a map\n" );
	else if ( !Q_stricmp( m_mapName, STRING( gpGlobals->mapname ) ) ) Msg( "an info_changelevel points to the current map\n" );
	else if ( !m_landmarkName[0] ) Msg( "info_changelevel to %s doesn't have a landmark\n", m_mapName );
	else
	{
		SetClassname( "info_changelevel" );
		InitTrigger();
		if ( HasSpawnFlags( 2 ) ) { UTIL_Remove( this ); return; }
		SetTouch( &InfoChangelevel::TouchChangeLevel );
		AddSpawnFlags( 1 );
		AddSpawnFlags( 64 );
		SetThink( &InfoChangelevel::ChangelevelThink );
		SetNextThink( gpGlobals->curtime + 10.0f + float( entindex() % 10 ) * 0.1f );
		return;
	}
	UTIL_Remove( this );
}
void InfoChangelevel::Activate()
{
	BaseClass::Activate();
	CBaseEntity *landmark = gEntList.FindEntityByName( NULL, m_landmarkName );
	if ( landmark && engine->GetClusterForOrigin( landmark->GetAbsOrigin() ) < 0 )
		Warning( "info_changelevel to map %s has a landmark embedded in solid!\nThis will break level transitions!\n", m_mapName );
	m_changeLevelStarted = false;
}
void InfoChangelevel::TouchChangeLevel( CBaseEntity *other )
{
	// S:253900 is genuinely empty. Touch membership is owned by CBaseTrigger.
}
InfoChangelevel::CheckpointPlayers::CheckpointPlayers() : infectedInside( false ), survivorOutside( false ), humanInside( false ) {}
void InfoChangelevel::CollectCheckpointPlayer( CheckpointPlayers &players, CBaseCombatCharacter *actor )
{
	if ( !actor->IsAlive() ) return;
	CTerrorPlayer *player = ToTerrorPlayer( actor );
	if ( player && player->IsGhost() ) return;
	TerrorNavArea *area = ToTerrorNavArea( actor->GetLastKnownArea() );
	bool touching = IsTouching( actor );
	if ( actor->GetTeamNumber() == TEAM_INFECTED )
	{
		if ( touching && area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) players.infectedInside = true;
	}
	else if ( actor->GetTeamNumber() == TEAM_SURVIVOR && player )
	{
		if ( touching && actor->GetMoveType() != MOVETYPE_NOCLIP && area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
		{
			players.inside.AddToTail( player );
			if ( allow_all_bot_survivor_team.GetBool() || !player->IsBot() ) players.humanInside = true;
		}
		else
		{
			players.outside.AddToTail( player );
			players.survivorOutside = true;
		}
	}
}
void InfoChangelevel::CollectCheckpointPlayers( CheckpointPlayers &players, bool includeNextBots )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() && player->IsPlayer() && player->IsConnected() &&
			( includeNextBots ? player->MyNextBotPointer() == NULL : player->GetTeamNumber() == TEAM_SURVIVOR ) )
			CollectCheckpointPlayer( players, player );
	}
	if ( includeNextBots )
	{
		struct Collect
		{
			InfoChangelevel *changelevel;
			CheckpointPlayers *players;
			bool operator()( CBaseCombatCharacter *actor ) { changelevel->CollectCheckpointPlayer( *players, actor ); return true; }
		} collect = { this, &players };
		TheNextBots().ForEachCombatCharacter( collect );
	}
}
void InfoChangelevel::InputCheckpointDoorOpened( inputdata_t &data )
{
	m_checkpointDoorOpen = true;
	CheckpointPlayers players;
	CollectCheckpointPlayers( players, false );
}
void InfoChangelevel::InputCheckpointDoorClosed( inputdata_t &data )
{
	CheckpointPlayers players;
	CollectCheckpointPlayers( players, true );
	UpdateCheckpoint( players );
	if ( !m_changeLevelStarted && players.humanInside && players.survivorOutside )
	{
		CTerrorPlayer *speaker = players.inside[RandomInt( 0, players.inside.Count() - 1 )];
		CTerrorPlayer *subject = players.outside[RandomInt( 0, players.outside.Count() - 1 )];
		speaker->SpeakIfAllowed( "PlayerOutsideCheckpoint", 3, CFmtStr( "Subject:%s", subject->GetResponseCharacterName() ) );
	}
}
void InfoChangelevel::ChangelevelThink()
{
	SetNextThink( gpGlobals->curtime + 1.0f );
	CheckpointPlayers players;
	CollectCheckpointPlayers( players, true );
	UpdateCheckpoint( players );
}
void InfoChangelevel::UpdateCheckpoint( const CheckpointPlayers &players )
{
	CTerrorCheckpoint *checkpoint = NULL;
	if ( !players.humanInside || players.infectedInside ||
		!( checkpoint = TheTerrorNavMesh()->GetCheckpoint( WorldSpaceCenter() ) ) )
		goto refreshDoorState;
	m_checkpointDoorOpen = !checkpoint->AreAllDoorsClosed();
	if ( m_speechTimer.HasStarted() )
	{
		if ( m_speechTimer.GetRemainingTime() <= 0.0f )
		{
			if ( players.inside.Count() )
			{
				CTerrorPlayer *speaker = players.inside[RandomInt( 0, players.inside.Count() - 1 )];
				if ( !speaker->IsSpeaking() )
				{
					if ( players.survivorOutside )
					{
						double closestDistance = DBL_MAX;
						int closest = 0;
						FOR_EACH_VEC( players.outside, i )
						{
							const Vector &a = speaker->GetAbsOrigin(), &b = players.outside[i]->GetAbsOrigin();
							double x = double( b.x ) - a.x, y = double( b.y ) - a.y, z = double( b.z ) - a.z;
							double distance = x * x + y * y + z * z;
							if ( closestDistance > distance ) { closestDistance = distance; closest = i; }
						}
						speaker->SpeakIfAllowed( "PlayerGetInsideCheckPoint", 1, CFmtStr( "Subject:%s,Distance:%g", players.outside[closest]->GetResponseCharacterName(), sqrt( closestDistance ) ) );
					}
					else if ( m_checkpointDoorOpen ) speaker->SpeakIfAllowed( "PlayerLockTheDoorCheckPoint", 1 );
				}
			}
			m_speechTimer.Start( float( RandomInt( 5, 8 ) ) );
		}
	}
	else m_speechTimer.Start( 3.0f );
	if ( m_checkpointDoorOpen ) return;
	if ( !players.survivorOutside )
	{
		if ( TheDirector->GetScriptedMode()->HasRequiredGnome() ) BeginChangeLevel( checkpoint );
		return;
	}
refreshDoorState:
	if ( players.inside.Count() )
	{
		checkpoint = TheTerrorNavMesh()->GetCheckpoint( WorldSpaceCenter() );
		if ( checkpoint ) m_checkpointDoorOpen = !checkpoint->AreAllDoorsClosed();
	}
}
void InfoChangelevel::BeginChangeLevel( CTerrorCheckpoint *checkpoint )
{
	if ( changelevel_inhibit.GetBool() ) { Msg( "Would change level, but not going to!\n" ); return; }
	if ( nav_edit.GetBool() || m_changeLevelStarted ) return;
	m_changeLevelStarted = true;
	CBaseEntity *landmark = gEntList.FindEntityByName( NULL, m_landmarkName );
	SetTerrorTransitionLandmark( m_landmarkName, landmark ? landmark->GetAbsOrigin() : vec3_origin );
	if ( !Q_stricmp( m_landmarkName, "." ) )
	{
		CTerrorCheckpoint *localCheckpoint = TheTerrorNavMesh()->GetCheckpoint( WorldSpaceCenter() );
		if ( localCheckpoint ) localCheckpoint->RespawnPlayers();
		return;
	}
	TheZombieManager->DisableSpawning();
	if ( checkpoint ) checkpoint->CloseAndLockDoors();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() ) continue;
		if ( player->GetTeamNumber() == TEAM_INFECTED && player->IsAlive() && ( player->GetFlags() & FL_ONFIRE ) )
			player->TakeDamage( CTakeDamageInfo( player, player, float( player->GetHealth() ), DMG_BURN ) );
	}
	if ( !TerrorGameRules()->IsPlayerControlledZombies() ) SaveTerrorTransitionEntities( this );
	IGameEvent *event = gameeventmanager->CreateEvent( "map_transition" );
	if ( event ) gameeventmanager->FireEvent( event );
	if ( TheDirector->CanChangeLevel() )
	{
		TheDirector->OnRoundEnd( true );
		TerrorGameRules()->PrepareForChangeLevel( m_mapName );
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() ) continue;
		if ( player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR ) player->SpeakIfAllowed( "PlayerTransition", 3 );
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() && player->IsPlayer() && player->IsConnected() &&
			player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR ) player->GetMusic()->OnEnterCheckpoint();
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() && player->IsPlayer() && player->IsConnected() &&
			player->MyNextBotPointer() == NULL )
		{
			player->AddFlag( FL_GODMODE | FL_FROZEN );
			player->RumbleEffect( RUMBLE_STOP_ALL, 0, 0 );
		}
	}
	struct FreezeBots { bool operator()( CBaseCombatCharacter *actor ) { actor->AddFlag( FL_FROZEN ); return true; } } freeze;
	TheNextBots().ForEachCombatCharacter( freeze );
	m_OnChangeLevel.FireOutput( this, this );
	SetThink( &InfoChangelevel::ChangeLevelSpeechDelay );
	SetNextThink( gpGlobals->curtime + 0.5f );
}
void InfoChangelevel::ChangeLevelSpeechDelay()
{
	// S254E90 discards S254E00's maximum of raw vocalizeTimer12468 minus
	// S20D060(curtime). That pure reduction has no observable writer/callback.
	SetThink( &InfoChangelevel::ChangeLevelNow );
	SetNextThink( gpGlobals->curtime + changelevel_pause_interval.GetFloat() );
}
void InfoChangelevel::ChangeLevelNow()
{
	if ( TheDirector->CanChangeLevel() ) ChangeTerrorLevel( m_mapName, NULL );
}
