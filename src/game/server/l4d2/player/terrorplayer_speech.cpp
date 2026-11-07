//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_expresser.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "AI_ResponseSystem.h"
#include "NextBot/NextBotManager.h"
#include "soundent.h"
#include "in_buttons.h"
#include "team.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/entities/info_remarkable.h"
#include "l4d2/director/director_session_manager.h"
#include "tier1/fmtstr.h"
#include <xmmintrin.h>
#include "sceneentity.h"
#include "variant_t.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/player/terrorplayer_speech_console_server_cvars.inc"
#include "tier0/memdbgon.h"

ConVar z_noise_level_vocalize( "z_noise_level_vocalize", "135", 0x00004004, "How much noise we make when we say things." );
static ConVar scene_clientplayback( "scene_clientplayback", "1", 0x00000006, "Play all vcds on the clients." );
static ConVar z_reload_chatter_shotgun_interval( "z_reload_chatter_shotgun_interval", "10", 0x00004004 );
static ConVar z_reload_chatter_shotgun_ammo_threshold( "z_reload_chatter_shotgun_ammo_threshold", "6", 0x00004004,
	"Players don't say \"Reloading\" when reloading a shotgun that has this many shells already" );
static ConVar z_reload_chatter_intensity( "z_reload_chatter_intensity", "0.25", 0x00004004, "Intensity level at which players start saying \"Reloading\"" );
static ConVar z_reload_chatter_recent_enemy( "z_reload_chatter_recent_enemy", "1.5", 0x00004004, "An enemy needs to have been seen this recently to say \"Reloading\"" );
static ConVar z_reload_chatter_nearby_friend_range( "z_reload_chatter_nearby_friend_range", "600", 0x00004004, "A friend needs to be this close to say\"Reloading\"" );
static ConVar z_reload_chatter_debug( "z_reload_chatter_debug", "0", 0x00004004, "Should we give devtext explanations of why we didn't say \"Reloading\"?" );

IResponseSystem *g_pTerrorPlayerResponseSystem = NULL; // Native1085BB78; dictionary-owned, not player-owned.

void CTerrorPlayer::PrecacheResponseSystem()
{
	// S344560 uses the existing shared instanced-system owner on each precache.
	g_pTerrorPlayerResponseSystem = PrecacheCustomResponseSystem( "scripts/talker/terror_player.txt" );
}

IResponseSystem *CTerrorPlayer::GetResponseSystem()
{
	return g_pTerrorPlayerResponseSystem; // S344580: no generic-system fallback.
}

const char *GetResponseCharacterName( int character )
{
	if ( TerrorGameRules()->GetSurvivorSet() == 1 )
	{
		switch ( character )
		{
		case 0: character = 4; break;
		case 1: character = 5; break;
		case 2: character = 7; break;
		case 3: character = 6; break;
		}
	}
	static const char *names[] = { "Gambler", "Producer", "Coach", "Mechanic", "NamVet", "TeenGirl", "Biker", "Manager" };
	return character >= 0 && character < ARRAYSIZE( names ) ? names[character] : "Unknown";
}

const char *CTerrorPlayer::GetResponseCharacterName() const
{
	return ::GetResponseCharacterName( GetSurvivorCharacter() );
}

CAI_Expresser *CTerrorPlayer::CreateExpresser()
{
	// Called once by the inherited PostConstructor, after the complete player
	// object exists. The inherited destructor owns this same allocation.
	CTerrorExpresser *expresser = new CTerrorExpresser( this );
	m_pExpresser = expresser;
	expresser->Connect( this );
	return expresser;
}

void CTerrorPlayer::InputSpeakResponseConcept( inputdata_t &inputdata )
{
	// S309100 copies the complete input before the virtual expresser query.
	inputdata_t snapshot = inputdata;
	CAI_Expresser *expresser = GetExpresser();
	HandleInputSpeakResponseConcept( this, expresser, snapshot );
}

// S2FB9A0, native DeathSound slot388: direct speech, not the priority gate.
void CTerrorPlayer::DeathSound( const CTakeDamageInfo &info )
{
	if ( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR )
		Speak( "PlayerDeath" );
}

bool CTerrorPlayer::SpeakIfAllowed( AIConcept_t concept, int priority, const char *modifiers, char *chosen, size_t size, IRecipientFilter *filter )
{
	if ( !IsAlive() )
		return false;
	if ( priority < m_nSpeechPriority && m_vocalizeTimer.GetRemainingTime() > 0.0f )
		return false;
	m_nNextSpeechPriority = priority;
	return Speak( concept, modifiers, chosen, size, filter );
}

void CTerrorPlayer::OnSpokeConcept( AIConcept_t concept, AI_Response *response )
{
	// S344630 is the REAL successful-dispatch priority writer. A rejected
	// response must not commit m_nNextSpeechPriority.
	TheNextBots().OnSpokeConcept( this, concept, response );
	m_nSpeechPriority = m_nNextSpeechPriority;
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

void CTerrorPlayer::OnSpoke( CBaseCombatCharacter *speaker, const char *sound, float duration )
{
	// S344B60 compares against the unspilled x87 timestamp-minus-clock.
	// The sound argument is unused; do not infer a response-dispatch caller.
	if ( speaker == this && static_cast< double >( duration ) >
		static_cast< double >( m_vocalizeTimer.m_timestamp ) - static_cast< double >( gpGlobals->curtime ) )
	{
		SetNoiseLevel( z_noise_level_vocalize.GetFloat() );
		m_vocalizeTimer.Start( duration );
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void CTerrorPlayer::EmitInfectedSound( const char *sound, float minimumInterval, float noiseDuration )
{
	// S347590 tests negative, then positive. Zero AND unordered intervals use
	// the real emitted duration; neither case leaves the vocalize timer alone.
	if ( minimumInterval < 0.0f )
	{
		EmitSound( sound );
	}
	else if ( minimumInterval > 0.0f )
	{
		EmitSound( sound );
		m_vocalizeTimer.Start( minimumInterval );
	}
	else
	{
		float duration = 0.0f;
		EmitSound( sound, 0.0f, &duration );
		const float interval = minimumInterval > duration ? minimumInterval : duration;
		m_vocalizeTimer.Start( interval + 0.2f );
	}
	if ( noiseDuration > 0.0f )
		CSoundEnt::InsertSound( SOUND_PLAYER, GetAbsOrigin(), 2048, noiseDuration, this, 10 );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CTerrorPlayer::OnTerrorReloadStarted( bool shotgun, int oldClip, bool partial )
{
	IGameEvent *event = gameeventmanager->CreateEvent( "weapon_reload" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetBool( "manual", ( m_nButtons & IN_RELOAD ) != 0 );
		gameeventmanager->FireEvent( event );
	}
	if ( IsSneaking() || partial )
		return;
	if ( m_bReloadVoicePlayed )
	{
		if ( z_reload_chatter_debug.GetBool() )
			DevMsg( "No reload voice because: we already said Reloading and haven't finished.\n" );
		return;
	}
	float remaining = m_vocalizeTimer.GetRemainingTime();
	if ( remaining > 0.0f )
	{
		if ( z_reload_chatter_debug.GetBool() )
			DevMsg( "No reload voice because: VocalizeTimer still running.\n" );
		return;
	}
	if ( remaining > -3.0f )
		return;
	if ( shotgun )
	{
		if ( m_reloadChatterTimer.GetRemainingTime() > 0.0f )
		{
			if ( z_reload_chatter_debug.GetBool() )
				DevMsg( "No reload voice because: Second or later shotgun shell.\n" );
			return;
		}
		if ( oldClip > z_reload_chatter_shotgun_ammo_threshold.GetInt() )
		{
			if ( z_reload_chatter_debug.GetBool() )
				DevMsg( "No reload voice because: Topping off a shotgun.\n" );
			return;
		}
	}
	if ( GetTeamNumber() == TEAM_SURVIVOR )
	{
		int living = 0;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = UTIL_PlayerByIndex( i );
			if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && player->IsAlive() )
				++living;
		}
		if ( living == 1 )
		{
			if ( z_reload_chatter_debug.GetBool() )
				DevMsg( "No reload voice because: Everyone else is dead.\n" );
			return;
		}
	}
	bool intense = GetIntensity() >= z_reload_chatter_intensity.GetFloat();
	if ( !intense )
	{
		if ( m_enemySeenInterval.HasStarted() && m_enemySeenInterval.GetElapsedTime() > z_reload_chatter_recent_enemy.GetFloat() )
		{
			if ( z_reload_chatter_debug.GetBool() )
				DevMsg( "No reload voice because: Haven't seen a zombie recently.\n" );
			return;
		}
		int friends = CountNearbyFriends( z_reload_chatter_nearby_friend_range.GetFloat(), false );
		if ( !friends )
		{
			if ( z_reload_chatter_debug.GetBool() )
				DevMsg( "No reload voice because: Nobody nearby to hear.\n" );
			return;
		}
	}
	if ( z_reload_chatter_debug.GetBool() )
		DevMsg( intense ? "YES reload voice because: Personal Intensity high enough to demand it.\n" : "YES reload voice because: No reason not to.\n" );
	m_bReloadVoicePlayed = true;
	SetNoiseLevel( z_noise_level_vocalize.GetFloat() );
	SpeakIfAllowed( "PlayerReloading", 0 );
	m_vocalizeTimer.Start( 2.0f );
	if ( shotgun )
		m_reloadChatterTimer.Start( z_reload_chatter_shotgun_interval.GetFloat() );
}

void CTerrorPlayer::OnTerrorReloadFinished()
{
	// S2FC070. No voice, event, timer or animation is generated here.
	m_bReloadVoicePlayed = false;
}


void CTerrorPlayer::OnFriendlyFire( CTerrorPlayer *victim, IGameEvent *event )
{
	if ( !Q_stricmp( event->GetString( "weapon" ), "inferno" ) )
		return;
	if ( !m_friendlyFireResponseTimer.HasStarted() )
	{
		m_friendlyFireResponseTimer.Start( 0.5f );
		m_painTimer.Start( 1.5f );
		m_friendlyFireSubject = victim->GetSurvivorCharacter();
		m_friendlyFireDamageType = event->GetInt( "type" );
		Q_strncpy( m_friendlyFireWeapon, event->GetString( "weapon" ), sizeof( m_friendlyFireWeapon ) );
	}
	if ( victim && victim->IsPlayer() && InSameTeam( victim ) && TheDirector->GetDifficulty() >= 2 )
		SendPZDamageMessage( 9, victim, this, NULL, true );
}

void CTerrorPlayer::UpdateFriendlyFireResponse()
{
	if ( !m_friendlyFireResponseTimer.HasStarted() || m_friendlyFireResponseTimer.GetRemainingTime() > 0.0f )
		return;
	m_friendlyFireResponseTimer.Invalidate();
	const char *damageType = "DMG_GENERIC";
	if ( m_friendlyFireDamageType & DMG_BULLET )
		damageType = "DMG_BULLET";
	else if ( m_friendlyFireDamageType & DMG_BLAST )
		damageType = "DMG_BLAST";
	else if ( m_friendlyFireDamageType & DMG_BURN )
		damageType = "DMG_BURN";
	char modifiers[256];
	Q_snprintf( modifiers, sizeof( modifiers ), "Subject:%s,DamageType:%s,Weapon:%s",
		::GetResponseCharacterName( m_friendlyFireSubject ), damageType, m_friendlyFireWeapon );
	SpeakIfAllowed( "PlayerFriendlyFire", 1, modifiers );
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar ai_talk_idle_enabled( "ai_talk_idle_enabled", "1", 0x00004004, "Set to 0 to disable TLK_IDLEs on survivors" );
ConVar chet_debug_idle( "chet_debug_idle", "0", 0x00004086,
	"If set one, many debug prints to help track down the TLK_IDLE issue. Set two for super verbose info" ); // Native16514.
ConVar rr_remarkable_maxdist( "rr_remarkable_maxdist", "1500", 0x00004004,
	"info_remarkables more distant than this from a player will not even be tested to see if a rule matches them." );
ConVar rr_remarkable_world_entities_replay_limit( "rr_remarkable_world_entities_replay_limit", "1", 0x00004004,
	"TLK_REMARKs will be dispatched no more than this many times for any given info_remarkable" );

namespace
{
	int s_nextRemarkCharacter; // Native1085BC00, BSS0; not infected's distinct globals.
	float s_nextRemarkCheckTime; // Native1085BC04, BSS0.
	unsigned int IdleFloatOrder( float value )
	{
		unsigned int bits = FloatBits( value );
		if ( !( bits & 0x7fffffffu ) ) bits = 0;
		return bits & 0x80000000u ? ~bits : bits | 0x80000000u;
	}
	bool IdleFloatNaN( float value ) { return ( FloatBits( value ) & 0x7fffffffu ) > 0x7f800000u; }
	bool IdleGreater( float a, float b ) { return !IdleFloatNaN(a) && !IdleFloatNaN(b) && IdleFloatOrder(a) > IdleFloatOrder(b); }
	bool IdleGreaterEqual( float a, float b ) { return !IdleFloatNaN(a) && !IdleFloatNaN(b) && IdleFloatOrder(a) >= IdleFloatOrder(b); }
	float IdleAdd( float a, float b ) { return _mm_cvtss_f32( _mm_add_ss( _mm_set_ss(a), _mm_set_ss(b) ) ); }
}
void ResetTerrorIdleSpeechState()
{
	// Native2FD3C0, ONLY actual2659A0 caller. No per-map/game-system reset.
	s_nextRemarkCharacter = 0;
	s_nextRemarkCheckTime = 0.0f;
}
bool CTerrorPlayer::TryRemarkableSpeech()
{
	// Native34A0A0: frozen session only guards remarks, not the idle branch.
	if ( TheDirector->GetSessionManager()->IsTeamFrozen() ) return false;
	const CUtlLinkedList< CInfoRemarkable *, unsigned short > &entities = GetTerrorRemarkableEntities();
	const int replayLimit = rr_remarkable_world_entities_replay_limit.GetInt();
	const __m128 range = _mm_set_ss( rr_remarkable_maxdist.GetFloat() );
	const float rangeSquared = _mm_cvtss_f32( _mm_mul_ss( range, range ) );
	FOR_EACH_LL( entities, i )
	{
		CInfoRemarkable *remarkable = entities[i]; // Native registry pointer, no fake actor/discovery scan.
		const Vector &here = GetAbsOrigin(), &there = remarkable->GetAbsOrigin();
		__m128 dx = _mm_sub_ss( _mm_set_ss(there.x), _mm_set_ss(here.x) );
		__m128 dy = _mm_sub_ss( _mm_set_ss(there.y), _mm_set_ss(here.y) );
		__m128 dz = _mm_sub_ss( _mm_set_ss(there.z), _mm_set_ss(here.z) );
		const float distanceSquared = _mm_cvtss_f32( _mm_add_ss( _mm_add_ss( _mm_mul_ss(dy,dy), _mm_mul_ss(dx,dx) ), _mm_mul_ss(dz,dz) ) );
		if ( remarkable->GetRemarkCount() >= replayLimit || !IdleGreaterEqual( rangeSquared, distanceSquared ) ) continue;
		// Native virtual1060 IsInFieldOfView, then1068 IsLineOfSightClear with
		// IGNORE_ACTORS. Existing same-owner base helpers are extended by the
		// exact L4D2 FOV/query-cache packet; NEVER PVS/FVisible/self-render tests.
		if ( !IsInFieldOfView( remarkable ) || !IsLineOfSightClear( remarkable, IGNORE_ACTORS ) ) continue;
		const float reciprocal = _mm_cvtss_f32( _mm_rsqrt_ss( _mm_set_ss( distanceSquared ) ) );
		const double distance = (double)reciprocal * (double)distanceSquared; // Native RSQRTSS then x87 product; zero->NaN intentionally.
		CFmtStr modifiers( "Subject:%s,Distance:%f", remarkable->GetRemarkContext(), distance );
		if ( SpeakIfAllowed( "TLK_REMARK", 0, modifiers.Access() ) )
		{
			remarkable->OnRemarkSpoken();
			return true;
		}
	}
	return false;
}
void CTerrorPlayer::UpdateIdleSpeech()
{
	// Native34B3D0, reached from full scheduled338740 AFTER3495D0 and
	// BEFORE34ACD0 in the actual survivor-team branch, no new think cadence.
	if ( IdleGreater( gpGlobals->curtime, s_nextRemarkCheckTime ) )
	{
		if ( GetSurvivorCharacter() == s_nextRemarkCharacter ||
			!FindSurvivorByCharacter( (SurvivorCharacterType)s_nextRemarkCharacter ) )
		{
			s_nextRemarkCheckTime = IdleAdd( gpGlobals->curtime, 0.25f );
			s_nextRemarkCharacter = ( s_nextRemarkCharacter + 1 ) % 4;
			if ( TryRemarkableSpeech() )
			{
				if ( chet_debug_idle.GetInt() ) Msg( "TLK_IDLE: %s is remarking; will not idle\n", GetResponseCharacterName() );
				return;
			}
		}
	}
	if ( !ai_talk_idle_enabled.GetBool() ) return;
	const bool canVocalize = IdleGreaterEqual( gpGlobals->curtime, m_vocalizeTimer.m_timestamp );
	if ( chet_debug_idle.GetInt() == 2 )
		Msg( "TLK_IDLE: %s : vocalizetimer(%d), timeToNextVoc(%.1f)\n", GetResponseCharacterName(), canVocalize ? 1 : 0,
			(double)m_flNextIdleVocalizeTime - (double)gpGlobals->curtime );
	if ( !canVocalize || !IdleGreater( gpGlobals->curtime, m_flNextIdleVocalizeTime ) ) return;
	if ( chet_debug_idle.GetInt() ) Msg( "TLK_IDLE: %s will SpeakIfAllowed\n", GetResponseCharacterName() );
	SpeakIfAllowed( "TLK_IDLE", 0 ); // Native updates the deadline even if speech was rejected.
	// Native x87 computes (now+10)+RandomFloat before ONE float store.
	m_flNextIdleVocalizeTime = (float)( ( (double)gpGlobals->curtime + 10.0 ) + (double)RandomFloat( -2.0f, 2.0f ) );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

float CTerrorPlayer::PlayScene( const char *scene, float delay, AI_Response *response, IRecipientFilter *filter )
{
	// S344710: the actual scene provider writes the sole actor scene handle.
	return InstancedScriptedScene( this, scene, &m_hInstancedScene, delay, false,
		response, scene_clientplayback.GetInt() != 0, filter );
}

void CTerrorPlayer::InputCancelCurrentScene( inputdata_t &inputdata )
{
	// S344BC0: native Main-A inactive-union disposition uses ordinary FIELD_VOID.
	if ( m_hInstancedScene.Get() )
	{
		variant_t value;
		m_hInstancedScene.Get()->AcceptInput( "Cancel", NULL, NULL, value, 0 );
	}
	SpeakIfAllowed( "SceneCancelled", 1, NULL, NULL, 0, NULL );
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// S304110 / S2D1B80. Life/death/ledge broadcasts query every connected
// survivor except the actor. They are neither game events nor delayed queue
// concepts, and there is deliberately no additional recipient alive gate.
class CSpeakConceptToSurvivors
{
public:
	CSpeakConceptToSurvivors( CBaseEntity *actor, AIConcept_t concept, const char *modifiers ) :
		m_Actor( actor ), m_Concept( concept ), m_Modifiers( modifiers ) {}
	bool operator()( CBasePlayer *base )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		if ( player == m_Actor || ( player->GetTeamNumber() != TEAM_SURVIVOR && player->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) )
			return true;
		int distance = 0;
		bool visible = false;
		if ( m_Actor )
		{
			const Vector delta = m_Actor->GetAbsOrigin() - player->GetAbsOrigin();
			distance = static_cast< int >( sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x ) );
			visible = player->IsAbleToSee( m_Actor, CBaseCombatCharacter::USE_FOV );
		}
		char modifiers[256];
		Q_snprintf( modifiers, sizeof( modifiers ), "distancetoactor:%d,actorvisible:%d,%s", distance, visible ? 1 : 0, m_Modifiers );
		player->SpeakIfAllowed( m_Concept, 1, modifiers );
		return true;
	}
private:
	CBaseEntity *m_Actor;
	AIConcept_t m_Concept;
	const char *m_Modifiers;
};

void SpeakConceptToSurvivors( CBaseEntity *actor, AIConcept_t concept, const char *modifiers )
{
	CSpeakConceptToSurvivors speak( actor, concept, modifiers );
	ForEachPlayer( speak );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

void CTerrorPlayer::OnLeftCheckpoint()
{
	// S2FC490: latch FIRST, even when no spawn-area checkpoint qualifies.
	if ( m_hasLeftCheckpoint ) return;
	m_hasLeftCheckpoint = true;
	TerrorNavArea *spawn = ToTerrorNavArea( GetSpawnArea() );
	if ( !spawn || !spawn->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) return;
	if ( TheDirector->GetChapterNumber() > 0 )
	{
		GetMusic()->OnLeaveCheckpoint(); // Music+56=S2AFA50; intentionally not +44.
		SpeakIfAllowed( "SurvivorLeavingInitialCheckpoint", 1 );
	}
	else
	{
		GetMusic()->OnLeaveMissionStartArea(); // Music+44=S2AF610.
		SpeakIfAllowed( "SurvivorLeavingCheckpoint", 1 );
	}
}
