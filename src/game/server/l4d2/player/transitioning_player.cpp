//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/transitioning_player.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "iserver.h"
#include "iclient.h"
#include "datacache/imdlcache.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_transitioning_player, CTransitioningPlayer );
static CUtlVector< CTransitioningPlayer * > s_TransitioningPlayers; // S:85D280
static ConVar z_transitioning_players_remove( "z_transitioning_players_remove", "1", 0x00004004 );
extern ConVar director_transition_timeout;

static void SendProxy_TransitionHealth( const SendProp *prop, const void *object,
	const void *data, DVariant *out, int element, int objectID )
{
	// S:16A980 clamps only the upper bound; negative values are not sanitized.
	out->m_Int = MIN( *static_cast< const int * >( data ), 250 );
}
IMPLEMENT_SERVERCLASS_ST_NOBASE( CTransitioningPlayer, DT_TransitioningPlayer )
	SendPropDataTable( "baseclass", 0, CBaseFlex::m_pClassSendTable, SendProxy_DataTableToDataTable ),
	SendPropInt( SENDINFO( m_loadingProgress ), 32 ),
	SendPropInt( SENDINFO( m_userID ), 32 ),
	SendPropInt( SENDINFO( m_deaths ), 32 ),
	SendPropInt( SENDINFO( m_score ), 32 ),
	SendPropInt( SENDINFO( m_iHealth ), 10, 0, SendProxy_TransitionHealth ),
	SendPropInt( SENDINFO( m_healthBuffer ), 32 ),
	SendPropBool( SENDINFO( m_isAlive ) ),
	SendPropBool( SENDINFO( m_isDying ) ),
	SendPropBool( SENDINFO( m_isIncapacitated ) ),
	SendPropBool( SENDINFO( m_isIT ) ),
	SendPropString( SENDINFO( m_playerName ) ),
	SendPropInt( SENDINFO( m_character ), 32 ),
	SendPropBool( SENDINFO( m_hasMolotov ) ),
	SendPropBool( SENDINFO( m_hasGrenade ) ),
	SendPropBool( SENDINFO( m_hasFirstAidKit ) ),
	SendPropBool( SENDINFO( m_hasPainPills ) ),
	SendPropInt( SENDINFO( m_primaryWeaponID ), 32 ),
	SendPropInt( SENDINFO( m_secondaryWeaponID ), 32 ),
END_SEND_TABLE()
datamap_t CTransitioningPlayer::m_DataMap = { 0, 0, "CTransitioningPlayer", NULL };
datamap_t *CTransitioningPlayer::GetDataDescMap() { return &m_DataMap; }
datamap_t *CTransitioningPlayer::GetBaseMap()
{
	datamap_t *base;
	DataMapAccess( (CBaseFlex *)NULL, &base );
	return base;
}
BEGIN_DATADESC_GUTS( CTransitioningPlayer )
	DEFINE_THINKFUNC( TransitionThink ),
END_DATADESC()

CTransitioningPlayer::CTransitioningPlayer()
{
	// S:3545C0. Registration is constructor order, not entindex order.
	s_TransitioningPlayers.AddToTail( this );
	ListenForGameEvent( "player_transitioned" );
	ListenForGameEvent( "player_disconnect" );
}
CTransitioningPlayer::~CTransitioningPlayer()
{
	// S:354410 marks the first saved user DONE even if removal was caused
	// by a reconnect, event, shutdown, or explicit command rather than timeout.
	s_TransitioningPlayers.FindAndRemove( this );
	g_SavedTerrorPlayerStates.MarkPlayerRestoreDone( m_userID );
}
const CUtlVector< CTransitioningPlayer * > &CTransitioningPlayer::GetTransitioningPlayers()
{
	return s_TransitioningPlayers;
}
void CTransitioningPlayer::ResetTransitioningPlayerRegistry()
{
	// S2B7895/S2BA658 clear only the existing pointer-vector count.
	s_TransitioningPlayers.RemoveAll();
}
void CTransitioningPlayer::RemoveByUserID( int userID )
{
	FOR_EACH_VEC( s_TransitioningPlayers, i )
	{
		if ( s_TransitioningPlayers[i]->GetUserID() == userID )
		{
			UTIL_Remove( s_TransitioningPlayers[i] );
			return;
		}
	}
}
void CTransitioningPlayer::Initialize( const char *name, int score, int deaths,
	int userID, int team, int health, int healthBuffer, bool alive, bool dying,
	bool incapacitated, bool isIT, SurvivorCharacterType character )
{
	// S:354900. GetForModify supplies the original whole-entity network
	// dirty notification; the +108/+112 writes are not a new gameplay clock.
	Q_strncpy( m_playerName.GetForModify(), name, 32 );
	m_userID = userID;
	m_score = score;
	m_deaths = deaths;
	ChangeTeam( team );
	SetHealth( health );
	m_healthBuffer = healthBuffer;
	m_isAlive = alive;
	m_isDying = dying;
	m_isIncapacitated = incapacitated;
	m_isIT = isIT;
	m_character = character;
	m_loadingProgress = 0;
	SetThink( &CTransitioningPlayer::TransitionThink );
	SetNextThink( gpGlobals->curtime + 1.0f );
	SetSolid( SOLID_NONE );
	const CViewVectors *vectors = TerrorGameRules()->GetViewVectors();
	SetViewOffset( vectors->m_vView );
	if ( team != TEAM_SURVIVOR || character == SurvivorCharacter_Unknown )
	{
		SetRenderMode( kRenderNone );
		return;
	}
	static const char *models[] =
	{
		"models/survivors/survivor_gambler.mdl",
		"models/survivors/survivor_producer.mdl",
		"models/survivors/survivor_coach.mdl",
		"models/survivors/survivor_mechanic.mdl",
		"models/survivors/survivor_namvet.mdl",
		"models/survivors/survivor_teenangst.mdl",
		"models/survivors/survivor_biker.mdl",
		"models/survivors/survivor_manager.mdl"
	};
	SetModel( models[character] );
	MDLCACHE_CRITICAL_SECTION();
	SetSequence( SelectWeightedSequence( TerrorActivityFromOriginal( 1069 ) ) );
	UTIL_SetSize( this, vectors->m_vHullMin, vectors->m_vHullMax );
}
void CTransitioningPlayer::SetWeapons( int primary, int secondary, bool molotov,
	bool grenade, bool firstAidKit, bool painPills )
{
	m_primaryWeaponID = primary;
	m_secondaryWeaponID = secondary;
	m_hasMolotov = molotov;
	m_hasGrenade = grenade;
	m_hasFirstAidKit = firstAidKit;
	m_hasPainPills = painPills;
}
void CTransitioningPlayer::FireGameEvent( IGameEvent *event )
{
	const char *name = event->GetName();
	if ( !Q_strcmp( name, "player_transitioned" ) )
	{
		if ( event->GetInt( "userid" ) == m_userID && z_transitioning_players_remove.GetBool() )
			UTIL_Remove( this );
	}
	else if ( !Q_strcmp( name, "player_disconnect" ) )
	{
		const int userID = event->GetInt( "userid" );
		if ( userID == m_userID )
		{
			if ( z_transitioning_players_remove.GetBool() )
				UTIL_Remove( this );
			char command[64];
			Q_snprintf( command, sizeof( command ), "kick_transitioning %d;", userID );
			engine->ServerCommand( command );
		}
	}
}
bool TerrorEngine_IsUserIDInServer( int userID )
{
	// SDK2013's real IServer/IClient surface can answer membership before
	// ClientPutInServer creates an edict. It is NOT an ack-6 callback.
	IServer *server = engine->GetIServer();
	if ( !server->IsActive() ) // original server state<2 returns false
		return false;
	for ( int i = 0; i < server->GetClientCount(); ++i )
	{
		IClient *client = server->GetClient( i );
		if ( client && client->GetUserID() == userID )
			return true;
	}
	return false;
}

int TerrorEngine_GetLoadingProgressForUserID( int )
{
	// Main F10: target engine has no per-client loading-progress query.
	// Explicit capability gap, not native progress or a signon-state mapping.
	return 0;
}
void CTransitioningPlayer::TransitionThink()
{
	// S:3547F0 compares absolute map curtime with the cvar, not entity age.
	if ( gpGlobals->curtime >= director_transition_timeout.GetFloat() && !TerrorEngine_IsUserIDInServer( m_userID ) )
	{
		DevMsg( "UserID %d (%s) has gone away - removing transition record\n", m_userID.Get(), m_playerName.Get() );
		g_SavedTerrorPlayerStates.MarkPlayerRestoreDone( m_userID );
		if ( z_transitioning_players_remove.GetBool() )
			UTIL_Remove( this );
		return; // Native does not reschedule when removal is disabled.
	}
	if ( UTIL_PlayerByUserId( m_userID ) )
	{
		if ( z_transitioning_players_remove.GetBool() )
			UTIL_Remove( this );
		return; // Native also stops thinking on a reconnect with removal disabled.
	}
	SetNextThink( gpGlobals->curtime + 1.0f );
	m_loadingProgress = TerrorEngine_GetLoadingProgressForUserID( m_userID );
}
