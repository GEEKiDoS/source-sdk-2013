//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/nav/terror_nav_path.h"
#include "nav_mesh.h"
#include "l4d2/director/zombiemanager.h"
#include "NextBot/NextBotManager.h"
#include "team.h"
#include "world.h"
#include "ndebugoverlay.h"
#include "l4d2/sound/terror_music_dynamic_console_server_cvars.inc"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

extern ConVar music_manager;
extern ConVar music_dynamic_update_interval;
ConVar mix_dynamic_max_CI_emitters( "mix_dynamic_max_CI_emitters", "30", 0x00004004 );
ConVar mix_dynamic_cull_max_CI_emitters( "mix_dynamic_cull_max_CI_emitters", "0", 0x00004004 );
static ConVar mix_dynamic_debug_CI( "mix_dynamic_debug_CI", "0", 0x00004004 );
static ConVar mix_dynamic_num_attack_vox_CI( "mix_dynamic_num_attack_vox_CI", "2", 0x00004004 );
ConVar music_calm_min_interval( "music_calm_min_interval", "45", FCVAR_CHEAT | FCVAR_GAMEDLL, "Minimum time between DisturbingAtmosphere performances" );
static ConVar music_min_pending_threat_time( "music_min_pending_threat_time", "10", FCVAR_CHEAT | FCVAR_GAMEDLL, "Minimum time until the next mob or boss. Used when deciding to play MomentOfSilence" );
static ConVar music_min_safe_time( "music_min_safe_time", "3", FCVAR_CHEAT | FCVAR_GAMEDLL, "Minimum time that we haven't seen a threat or been injured. Used when deciding to play MomentOfSilence" );
static ConVar music_moment_of_silence_start_delay( "music_moment_of_silence_start_delay", "60", FCVAR_CHEAT | FCVAR_GAMEDLL, "Minimum time before we can play a MomentOfSilence after leaving a Checkpoint" );
static ConVar music_moment_of_silence_repeat_interval( "music_moment_of_silence_repeat_interval", "600", FCVAR_CHEAT | FCVAR_GAMEDLL, "Minimum time between successive MomentOfSilence performances" );
static ConVar music_dynamic_damage_decay( "music_dynamic_damage_decay", "10.0", 0x00004004 );
static ConVar music_dynamic_damage_increment( "music_dynamic_damage_increment", ".1", 0x00004004 );
static ConVar music_dynamic_damage_duck_damage_min( "music_dynamic_damage_duck_damage_min", "0.5", 0x00004004 );
static ConVar music_dynamic_damage_duck_damage_max( "music_dynamic_damage_duck_damage_max", "1.0", 0x00004004 );
static ConVar music_dynamic_damage_duck_min( "music_dynamic_damage_duck_min", "0.0", 0x00004004 );
static ConVar music_dynamic_damage_duck_max( "music_dynamic_damage_duck_max", "0.37", 0x00004004 );
static ConVar music_dynamic_mob_damage_min( "music_dynamic_mob_damage_min", "0.6", 0x00004004 );
static ConVar music_dynamic_mob_damage_max( "music_dynamic_mob_damage_max", "1.0", 0x00004004 );
static ConVar music_dynamic_mob_min( "music_dynamic_mob_min", "0.5", 0x00004004 );
static ConVar music_dynamic_mob_max( "music_dynamic_mob_max", "1.0", 0x00004004 );
static ConVar music_dynamic_mob_decay( "music_dynamic_mob_decay", "15", 0x00004004 );
static ConVar music_dynamic_mob_increment( "music_dynamic_mob_increment", ".1", 0x00004004 );
static ConVar music_dynamic_mob_small( "music_dynamic_mob_small", "20", 0x00004004, "Spawning a small mob" );
static ConVar music_dynamic_mob_med( "music_dynamic_mob_med", "29", 0x00004004, "Spawning a med mob" );
static ConVar music_dynamic_mob_large( "music_dynamic_mob_large", "49", 0x00004004, "Spawning a large mob" );
static ConVar z_mob_music_size( "z_mob_music_size", "2", 0x00004004, "Spotting a mob this large plays music" );
static ConVar music_dynamic_mob_action_min( "music_dynamic_mob_action_min", "0.0", 0x00004004 );
static ConVar music_dynamic_mob_action_max( "music_dynamic_mob_action_max", "1.0", 0x00004004 );
static ConVar music_dynamic_mob_action_close_min( "music_dynamic_mob_action_close_min", "0.0", 0x00004004 );
static ConVar music_dynamic_mob_action_close_max( "music_dynamic_mob_action_close_max", "0.42", 0x00004004 );
static ConVar music_dynamic_mob_action_decay( "music_dynamic_mob_action_decay", "15", 0x00004004 );
static ConVar music_dynamic_mob_action_increment( "music_dynamic_mob_action_increment", ".1", 0x00004004 );
static ConVar music_dynamic_attack_CI_distmax( "music_dynamic_attack_CI_distmax", "2400", 0x00004004 );
static ConVar music_dynamic_attack_CI_num( "music_dynamic_attack_CI_num", "30", 0x00004004 );
static ConVar music_dynamic_attack_CI_near_distmax( "music_dynamic_attack_CI_near_distmax", "300", 0x00004004 );
static ConVar music_dynamic_attack_CI_near_decay( "music_dynamic_attack_CI_near_decay", ".2", 0x00004004 );
static ConVar music_dynamic_attack_CI_close_distmax( "music_dynamic_attack_CI_close_distmax", "600", 0x00004004 );
static ConVar music_dynamic_attack_CI_close_decay( "music_dynamic_attack_CI_close_decay", ".2", 0x00004004 );
static ConVar music_dynamic_attack_CI_veryclose_distmax( "music_dynamic_attack_CI_veryclose_distmax", "94", 0x00004004 );
static ConVar music_dynamic_attack_CI_veryclose_decay( "music_dynamic_attack_CI_veryclose_decay", ".2", 0x00004004 );
static ConVar music_dynamic_SI_inrange_distmax( "music_dynamic_SI_inrange_distmax", "2400", 0x00004004 );
static ConVar music_dynamic_SI_close_distmax( "music_dynamic_SI_close_distmax", "1200", 0x00004004 );
static ConVar music_dynamic_SI_far_distmin( "music_dynamic_SI_far_distmin", "1800", 0x00004004 );
static ConVar music_dynamic_gunfire_decay( "music_dynamic_gunfire_decay", "1", 0x00004004 );
static ConVar music_dynamic_gunfire_increment( "music_dynamic_gunfire_increment", ".4", 0x00004004 );
static ConVar music_dynamic_gunfireslow_decay( "music_dynamic_gunfireslow_decay", "3", 0x00004004 );
static ConVar music_dynamic_gunfireslow_increment( "music_dynamic_gunfireslow_increment", ".3", 0x00004004 );
static ConVar music_dynamic_solosuccess_decay( "music_dynamic_solosuccess_decay", "4", 0x00004004 );
static ConVar music_dynamic_solosuccess_increment( "music_dynamic_solosuccess_increment", "1.0", 0x00004004 );
static ConVar music_dynamic_solosuccess_damagemin( "music_dynamic_solosuccess_damagemin", ".3", 0x00004004 );
static ConVar music_dynamic_CI_sight_decay( "music_dynamic_CI_sight_decay", "8", 0x00004004 );
static ConVar music_dynamic_action_decay( "music_dynamic_action_decay", "10", 0x00004004 );
static ConVar music_dynamic_threat_decay( "music_dynamic_threat_decay", "6", 0x00004004 );
static ConVar music_dynamic_calm_decay( "music_dynamic_calm_decay", "15", 0x00004004 );
static ConVar music_dynamic_ambient_decay( "music_dynamic_ambient_decay", "7", 0x00004004 );
static ConVar music_dynamic_ambient_in_min( "music_dynamic_ambient_in_min", ".5", 0x00004004 );
static ConVar music_dynamic_ambient_in_max( "music_dynamic_ambient_in_max", ".8", 0x00004004 );
static ConVar music_dynamic_ambient_out_min( "music_dynamic_ambient_out_min", "0.0", 0x00004004 );
static ConVar music_dynamic_ambient_out_max( "music_dynamic_ambient_out_max", "1.0", 0x00004004 );
static ConVar music_dynamic_ambient_vol_min_alert( "music_dynamic_ambient_vol_min_alert", "0.4", 0x00004004 );
static ConVar music_dynamic_PZ_BPM( "music_dynamic_PZ_BPM", "80", 0x00004004 );
static ConVar music_dynamic_PZ_interval_randmultmax( "music_dynamic_PZ_interval_randmultmax", "5", 0x00004004 );
static ConVar music_dynamic_specials_interval_beats( "music_dynamic_specials_interval_beats", "5", 0x00004004 );
static ConVar music_dynamic_mob_choir_BPM( "music_dynamic_mob_choir_BPM", "90", 0x00004004 );
static ConVar music_dynamic_mob_choir_interval_beats( "music_dynamic_mob_choir_interval_beats", "16", 0x00004004 );
static ConVar music_dynamic_mob_choir_interval_randmultmax( "music_dynamic_mob_choir_interval_randmultmax", "3", 0x00004004 );
static ConVar music_dynamic_witch_near_max( "music_dynamic_witch_near_max", "1800", 0x00004004 );
static ConVar music_dynamic_witch_near_min( "music_dynamic_witch_near_min", "360", 0x00004004 );
static ConVar music_dynamic_witch_alert_interval( "music_dynamic_witch_alert_interval", "37", 0x00004004 );
static ConVar music_dynamic_on( "music_dynamic_on", "1", 0x00004004 );
static ConVar music_dynamic_debug( "music_dynamic_debug", "0", 0x00004004, "Draw parameter meters" );

// These ordered comparisons are intentional. In particular, a reversed decay
// range or a zero decay must retain S2ABC50's original behaviour.
static float MusicClamp( float value )
{
	return value > 1.0f ? 1.0f : ( 0.0f > value ? 0.0f : value );
}
static float MusicRemaining( const CountdownTimer &timer, float decay )
{
	// Native comparisons keep the timestamp subtraction in x87 precision;
	// the selected limit is stored as float before the remap.
	const double remaining = static_cast< double >( timer.m_timestamp.Get() ) - gpGlobals->curtime;
	const float limited = remaining > decay ? decay : ( 0.0 > remaining ? 0.0f : static_cast< float >( remaining ) );
	return decay == 0.0f ? ( limited >= decay ? 1.0f : 0.0f ) : limited / decay;
}
static bool MusicElapsed( const CountdownTimer &timer )
{
	return static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( timer.m_timestamp.Get() );
}
static float MusicRemap( float value, float inMin, float inMax, float outMin, float outMax, bool limit = true )
{
	if ( inMin == inMax ) return value < inMax ? outMin : outMax;
	if ( !limit ) return outMin + ( ( value - inMin ) * ( outMax - outMin ) ) / ( inMax - inMin );
	float fraction = ( value - inMin ) / ( inMax - inMin );
	if ( limit ) fraction = fraction >= 0.0f ? ( fraction > 1.0f ? 1.0f : fraction ) : 0.0f;
	return outMin + fraction * ( outMax - outMin );
}
void Music::DynamicValue::Reset() { value = previous = 0.0f; timer.Invalidate(); }
float Music::DynamicValue::Increment( float amount, float increment, float decay )
{
	const float before = value;
	if ( !( amount > 0.0f ) ) value = MusicRemaining( timer, decay );
	else { value = MusicClamp( value + amount * increment ); timer.Start( value * decay ); }
	value = MusicClamp( value );
	return value - before;
}
void Music::DynamicValue::Peak( float target, float decay )
{
	if ( !( target > value ) ) value = MusicRemaining( timer, decay );
	else { value = MusicClamp( target ); timer.Start( value * decay ); }
	value = MusicClamp( value );
}
void Music::DynamicValue::Valley( float target, float decay )
{
	if ( !( value > target ) ) value = 1.0f - MusicRemaining( timer, decay );
	else
	{
		value = MusicClamp( target );
		const float interval = static_cast< float >( ( 1.0 - static_cast< double >( value ) ) * static_cast< double >( decay ) );
		timer.Start( interval );
	}
	value = MusicClamp( value );
}

void Music::ResetDynamicState()
{
	m_gunfireFast.Reset(); m_gunfireSlow.Reset(); m_damageTaken.Reset();
	m_soloSuccess.Reset(); m_mobbed.Reset(); m_mobApproach.Reset(); m_threat.Reset();
	m_action.Reset(); m_calm.Reset(); m_ambient.Reset();
	m_closeSmoothed = m_veryCloseSmoothed = m_inCombatMusic = m_lastMobbed = 0.0f;
	m_weaponFired = m_witchPresent = m_witchAttacking = m_witchBurning = false;
	m_witchChoirPlaying = m_bossMusicActive = m_hadTank = m_rephaseAfterMobSpawn = false;
	m_dangerAtmospherePlaying = m_dangerAtmospherePending = m_safeAtmospherePending = false;
	m_pendingMobCount = m_pendingChoirEntity = 0;
	m_actorRanges.RemoveAll(); m_attackingNear.RemoveAll(); m_calmNear.RemoveAll(); m_wanderingWitches.RemoveAll();
	m_totalInfected = m_attackingCount = m_attackingNearCount = m_calmCount = m_calmNearCount = 0;
	m_runningCount = m_nearCount = m_closeCount = m_veryCloseCount = 0;
	m_attackingFraction = m_calmFraction = m_nearWeight = m_closeWeight = m_veryCloseWeight = m_runningFraction = 0.0f;
	static const int resetOrder[] = { 1, 2, 3, 6, 4, 5 };
	for ( int i = 0; i < ARRAYSIZE( resetOrder ); ++i ) m_specialAlertTimer[resetOrder[i]].Reset();
	m_witchChoirTimer.Reset();
	m_choirDelay.Start( 0.1f );
	m_atmosphereCooldown.Start( 3.0f );
	m_dangerAtmosphereTimer.Start( music_calm_min_interval.GetFloat() );
	m_safeAtmosphereTimer.Start( music_calm_min_interval.GetFloat() + 10.0f );
	m_choirTimer.Start( static_cast< float >( static_cast< double >( RandomInt( 1, music_dynamic_mob_choir_interval_randmultmax.GetInt() ) ) *
		music_dynamic_mob_choir_interval_beats.GetFloat() * ( 60.0 / music_dynamic_mob_choir_BPM.GetFloat() ) ) );
	m_musicVariant[0] = '\0';
}
void Music::OnWeaponFired() { m_weaponFired = true; }
void Music::OnMobApproachingNear( int count ) { m_pendingMobCount = count; }
bool Music::IsInCombat() const { return m_inCombatMusic > 0.0f; }
void Music::AddWanderingWitch( Infected *witch )
{
	const EHANDLE handle( witch );
	if ( m_wanderingWitches.Find( handle ) == m_wanderingWitches.InvalidIndex() )
		m_wanderingWitches.AddToTail( handle );
}
void Music::RemoveWanderingWitch( Infected *witch )
{
	m_wanderingWitches.FindAndRemove( EHANDLE( witch ) );
}
void Music::OnAmbientMob() { Play( "Event.AmbientMob" ); }
void Music::MakeCampaignEvent( const char *event, char *out, int size ) const
{
	Q_snprintf( out, size, "%s_%s", event, TerrorGameRules()->IsSurvivalMode() ? "Survival" : GetWorldEntity()->GetMusicPostfix() );
}
void Music::MakeVariantEvent( const char *event, char *out, int size ) const
{
	if ( m_musicVariant[0] ) Q_snprintf( out, size, "%s_%s", event, m_musicVariant );
	else MakeCampaignEvent( event, out, size );
}
void Music::OnMobSpawn( int count, int direction )
{
	m_updateTimer.Reset();
	m_rephaseAfterMobSpawn = true;
	DevMsg( "%s Music::OnMobSpawn %s : %i\n", m_me->GetPlayerName(), direction == 0 ? "ANYWHERE" : direction == 1 ? "BEHIND" : "OTHER", count );
	// S2AE800 deliberately discards this actual native query's result.
	GetCommonInfectedCount( 1 );
	if ( TheDirector->GetScenario()->GetFinaleType() == 5 )
	{
		char event[128]; MakeCampaignEvent( direction == 1 ? "Event.MobSignal2" : "Event.MobSignal1", event, sizeof( event ) );
		Play( event );
	}
}

// S2AEA40 / S2B0B50: native heap construction and sort, including tie order.
// CUtlVector::Sort/qsort would change equal-distance voice/witch selection.
void Music::SiftActorHeap( int hole, int top, int count, const ActorRange &value )
{
	int child = 2 * hole + 2;
	while ( child < count )
	{
		if ( m_actorRanges[child].range < m_actorRanges[child - 1].range ) --child;
		m_actorRanges[hole] = m_actorRanges[child]; hole = child; child = 2 * hole + 2;
	}
	if ( child == count ) { m_actorRanges[hole] = m_actorRanges[count - 1]; hole = count - 1; }
	while ( hole > top )
	{
		const int parent = ( hole - 1 ) / 2;
		if ( !( m_actorRanges[parent].range < value.range ) ) break;
		m_actorRanges[hole] = m_actorRanges[parent]; hole = parent;
	}
	m_actorRanges[hole] = value;
}
void Music::SortActorRanges()
{
	const int count = m_actorRanges.Count();
	for ( int parent = count / 2 - 1; parent >= 0; --parent )
	{
		const ActorRange value = m_actorRanges[parent]; SiftActorHeap( parent, parent, count, value );
	}
	for ( int last = count - 1; last > 0; --last )
	{
		const ActorRange value = m_actorRanges[last]; m_actorRanges[last] = m_actorRanges[0];
		SiftActorHeap( 0, 0, last, value );
	}
}

// Original S2B0CE0. This is a real native actor/component producer, not a
// snapshot supplied with default-zero actor counts or personal intensity.
void Music::UpdatePerception()
{
	m_actorRanges.RemoveAll(); m_attackingNear.RemoveAll(); m_calmNear.RemoveAll();
	m_totalInfected = m_attackingCount = m_attackingNearCount = m_calmCount = m_calmNearCount = 0;
	m_runningCount = m_nearCount = m_closeCount = m_veryCloseCount = 0;
	m_attackingFraction = m_calmFraction = m_nearWeight = m_closeWeight = m_veryCloseWeight = m_runningFraction = 0.0f;
	struct CollectActors
	{
		Music *music;
		explicit CollectActors( Music *owner ) : music( owner ) {}
		bool operator()( INextBot *bot )
		{
			Infected *actor = bot->GetEntity()->MyInfectedPointer();
			if ( !actor || actor->IsMarkedForDeletion() ) return true;
			++music->m_totalInfected;
			if ( actor->HasStartedDying() ) return true;
			ActorRange &entry = music->m_actorRanges[music->m_actorRanges.AddToTail()];
			entry.actor = actor;
			entry.range = actor->MyNextBotPointer()->GetRangeTo( music->m_me->GetAbsOrigin() );
			return true;
		}
	} collect( this );
	TheNextBots().ForEachBot( collect );
	SortActorRanges();
	const float far = music_dynamic_attack_CI_distmax.GetFloat();
	const float near = music_dynamic_attack_CI_near_distmax.GetFloat();
	const float close = music_dynamic_attack_CI_close_distmax.GetFloat();
	const float veryClose = music_dynamic_attack_CI_veryclose_distmax.GetFloat();
	const float inverseCount = static_cast< float >( 1.0 / music_dynamic_attack_CI_num.GetFloat() );
	const bool debug = mix_dynamic_debug_CI.GetInt() > 0 && m_me->entindex() == 1;
	int recipients = 0;
	for ( int i = 0; i < m_actorRanges.Count(); ++i )
	{
		Infected *actor = m_actorRanges[i].actor;
		if ( actor->GetZombieClass() != 0 ) continue;
		const float range = m_actorRanges[i].range;
		if ( actor->GetBodyInterface()->IsArousal( IBody::INTENSE ) )
		{
			++m_attackingCount;
			if ( !( far > range ) ) continue;
			m_attackingNear.AddToTail( actor ); ++m_attackingNearCount;
			if ( debug ) NDebugOverlay::Cross3D( actor->GetAbsOrigin(), 24.0f, 255, 0, 255, true, 0.02f );
			if ( near >= range ) { ++m_nearCount; m_nearWeight += ( 1.0f - ( near == 0.0f ? ( range < 0.0f ? 0.0f : 1.0f ) : range / near ) ) * inverseCount; }
			if ( close >= range ) { ++m_closeCount; m_closeWeight += ( 1.0f - ( close == 0.0f ? ( range < 0.0f ? 0.0f : 1.0f ) : range / close ) ) * inverseCount; }
			if ( veryClose >= range ) { ++m_veryCloseCount; m_veryCloseWeight += ( 1.0f - ( veryClose == 0.0f ? ( range < 0.0f ? 0.0f : 1.0f ) : range / veryClose ) ) * inverseCount; }
			if ( actor->GetLocomotionInterface()->IsRunning() ) { ++m_runningCount; m_runningFraction = static_cast< float >( static_cast< double >( m_runningFraction ) + 0.03333333333333333 ); }
			if ( recipients >= mix_dynamic_max_CI_emitters.GetInt() ) actor->GetVocalizationRecipients().RemoveRecipient( m_me );
			else { actor->GetVocalizationRecipients().AddRecipient( m_me ); ++recipients; if ( debug ) NDebugOverlay::Cross3D( actor->GetAbsOrigin(), 48.0f, 0, 0, 255, true, 0.02f ); }
		}
		else
		{
			++m_calmCount;
			if ( far > range ) { m_calmNear.AddToTail( actor ); ++m_calmNearCount; if ( debug ) NDebugOverlay::Cross3D( actor->GetAbsOrigin(), 24.0f, 0, 255, 0, true, 0.02f ); }
		}
	}
	for ( int i = 0; i < m_calmNear.Count(); ++i )
	{
		// The calm pass uses >, not the attacking pass's >= (original extra slot).
		if ( recipients > mix_dynamic_max_CI_emitters.GetInt() ) m_calmNear[i]->GetVocalizationRecipients().RemoveRecipient( m_me );
		else { m_calmNear[i]->GetVocalizationRecipients().AddRecipient( m_me ); ++recipients; }
	}
	for ( int i = 0; i <= mix_dynamic_num_attack_vox_CI.GetInt() && i < m_attackingNear.Count(); ++i )
		m_attackingNear[i]->EmitVocalization( "Zombie.RageAtVictim", false );
	m_attackingFraction = m_attackingNearCount * inverseCount;
	m_calmFraction = m_calmNearCount * inverseCount;
}

void Music::UpdateSpecialProximity( int zombieClass, int entity, float range )
{
	CountdownTimer &timer = m_specialAlertTimer[zombieClass];
	// S2D9B70 is playercontrolledzombies, not the independent scavenge base.
	const float remaining = timer.GetRemainingTime();
	if ( TerrorGameRules()->IsPlayerControlledZombies() || !( 0.1 > static_cast< double >( remaining ) ) ||
		!( static_cast< double >( nv_m_ambientVolume.Get() ) > 0.1 ) ) return;
	static const char *names[] = { NULL, "Smoker", "Boomer", "Hunter", "Spitter", "Jockey", "Charger" };
	const int base = music_dynamic_PZ_interval_randmultmax.GetInt();
	int maxMultiplier;
	int minMultiplier = base;
	const char *suffix;
	if ( music_dynamic_SI_close_distmax.GetFloat() >= range ) { maxMultiplier = MAX( 1, (int)( base * 1.4 ) ); suffix = "Close"; }
	else if ( range > music_dynamic_SI_far_distmin.GetFloat() ) { minMultiplier = 2 * base; maxMultiplier = MAX( 3, (int)( base * 2.5 ) ); suffix = "Far"; }
	else { maxMultiplier = MAX( 2, 2 * base ); suffix = ""; }
	char event[128];
	Q_snprintf( event, sizeof( event ), "Event.%sAlert%s", names[zombieClass], suffix );
	Play( event, entity, -1.0f, false, true );
	const double interval = static_cast< double >( RandomInt( minMultiplier, maxMultiplier ) ) *
		music_dynamic_specials_interval_beats.GetFloat() * ( 60.0 / music_dynamic_PZ_BPM.GetFloat() );
	if ( interval >= 0.0 )
	{
		timer.Start( static_cast< float >( interval ) );
		PostponeSpecialProximity( zombieClass );
	}
}

void Music::StopCombatTracks( bool includePassing )
{
	if ( includePassing && !Q_stricmp( GetWorldEntity()->GetMusicPostfix(), "ThePassing" ) ) Stop( "zombat_tp", 2.0f, true );
	Stop( "zombat", 2.0f, true ); Stop( "zombat2", 2.0f, true );
	Stop( "zombat2c", 2.0f, true ); Stop( "zombat3", 2.0f, true );
}

// Original S2B1710. The state is owned by this embedded Music, while every
// actor, weapon, health, navigation and Director input comes from its native
// owner. No personal-intensity surrogate or default-zero provider is involved.
void Music::UpdateDynamicMusic()
{
	const float health = (float)m_me->GetHealth();
	const float previousHealth = m_damageTaken.previous;
	m_damageTaken.previous = health;
	float damage = 0.0f;
	if ( previousHealth != 0.0f )
	{
		damage = previousHealth - health;
		m_damageTaken.Increment( damage, music_dynamic_damage_increment.GetFloat(), music_dynamic_damage_decay.GetFloat() );
	}
	float firing = 0.0f;
	if ( m_weaponFired )
	{
		CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( m_me->GetActiveWeapon() );
		if ( weapon ) firing = weapon->GetTerrorWpnData().m_flMusicDynamicSpeed;
	}
	m_weaponFired = false;
	m_gunfireFast.Increment( firing, music_dynamic_gunfire_increment.GetFloat(), music_dynamic_gunfire_decay.GetFloat() );
	m_gunfireSlow.Increment( firing, music_dynamic_gunfireslow_increment.GetFloat(), music_dynamic_gunfireslow_decay.GetFloat() );
	UpdateDamageDealt();
	m_mobApproach.Peak( (float)m_pendingMobCount / z_mob_music_size.GetFloat(), music_dynamic_CI_sight_decay.GetFloat() );

	const bool daytime = IsMapTimeOfDay( 2, 4 );
	const bool wasWitchPresent = m_witchPresent, wasWitchAttacking = m_witchAttacking, wasWitchBurning = m_witchBurning;
	m_witchPresent = m_witchAttacking = m_witchBurning = false;
	nv_m_witchRage = 0.0f;
	Infected *witch = NULL;
	float nearestPathSquared = 268435456.0f;
	for ( int i = 0; i < m_actorRanges.Count(); ++i )
	{
		Infected *actor = m_actorRanges[i].actor;
		if ( actor->GetZombieClass() != 7 ) continue;
		if ( !witch ) witch = actor;
		IBody *body = actor->GetBodyInterface();
		if ( !body ) continue;
		m_witchPresent = true;
		if ( body->IsArousal( IBody::INTENSE ) )
		{
			const EHANDLE handle( actor );
			m_wanderingWitches.FindAndRemove( handle );
			m_witchAttacking = true;
			if ( actor->GetFlags() & FL_ONFIRE ) m_witchBurning = true;
		}
		if ( !daytime )
		{
			CNavArea *witchArea = actor->GetLastKnownArea();
			CNavArea *myArea = m_me->GetLastKnownArea();
			if ( witchArea && myArea )
			{
				float distanceSquared = 268435456.0f;
				const float maxRange = music_dynamic_witch_near_max.GetFloat();
				if ( ( myArea->GetCenter() - witchArea->GetCenter() ).LengthSqr() < maxRange * maxRange )
				{
					const float path = GetTerrorNavTravelDistance( myArea, witchArea, maxRange );
					if ( path >= 0.0f ) distanceSquared = path * path;
				}
				if ( nearestPathSquared > distanceSquared ) { nearestPathSquared = distanceSquared; witch = actor; }
			}
		}
	}
	if ( !m_witchPresent )
	{
		if ( wasWitchPresent && !daytime ) { Stop( "Event.WitchRage", 1.0f ); Stop( "Event.WitchRageBride", 1.0f ); }
	}
	else
	{
		const float rage = MusicRemap( sqrtf( nearestPathSquared ), music_dynamic_witch_near_min.GetFloat(), music_dynamic_witch_near_max.GetFloat(), 0.01f, 1.0f );
		nv_m_witchRage = MusicClamp( 1.0f - rage );
		if ( m_witchBurning ) { if ( !wasWitchBurning ) Play( "Event.WitchBurning" ); }
		else if ( m_witchAttacking ) { if ( !wasWitchAttacking || wasWitchBurning ) Play( "Event.WitchAttack" ); }
		else if ( !wasWitchPresent && !daytime && witch ) Play( witch->GetGender() == 19 ? "Event.WitchRageBride" : "Event.WitchRage", witch->entindex(), -1.0f, false, true );
	}
	if ( wasWitchBurning && !m_witchBurning ) Stop( "Event.WitchBurning", 2.0f );
	if ( wasWitchAttacking && ( !m_witchAttacking || m_witchBurning ) ) Stop( "Event.WitchAttack", 2.0f );
	if ( daytime && !m_witchBurning && !m_witchAttacking )
	{
		if ( !m_wanderingWitches.Count() )
		{
			if ( m_witchChoirPlaying ) { Stop( "witchChoir", -1.0f, true ); m_witchChoirPlaying = false; }
		}
		else
		{
			float remaining = 1.0f;
			for ( int i = 0; i < m_wanderingWitches.Count(); ++i )
			{
				Infected *actor = dynamic_cast< Infected * >( m_wanderingWitches[i].Get() );
				if ( !actor ) { m_wanderingWitches.FastRemove( i-- ); continue; }
				const float ratio = actor->GetWitchAttentionRemainingRatio();
				if ( ratio <= remaining ) remaining = ratio;
			}
			if ( MusicElapsed( m_witchChoirTimer ) )
			{
				Play( remaining < 0.25f ? "Event.WanderingAngryWitch4" : remaining < 0.5f ? "Event.WanderingAngryWitch3" : remaining < 0.75f ? "Event.WanderingAngryWitch2" : "Event.WanderingAngryWitch" );
				m_witchChoirTimer.Start( 2.5f ); m_witchChoirPlaying = true;
			}
		}
	}
	m_closeSmoothed += ( m_closeWeight - m_closeSmoothed ) * music_dynamic_attack_CI_close_decay.GetFloat();
	m_veryCloseSmoothed += ( m_veryCloseWeight - m_veryCloseSmoothed ) * music_dynamic_attack_CI_veryclose_decay.GetFloat();

	bool specialInRange = false;
	CTeam *infectedTeam = GetGlobalTeam( TEAM_INFECTED );
	const int specialCount = infectedTeam ? infectedTeam->GetNumPlayers() : 0;
	for ( int i = 0; i < specialCount; ++i )
	{
		CBasePlayer *enemy = infectedTeam->GetPlayer( i );
		if ( !enemy || !enemy->IsPlayer() || !enemy->IsAlive() ) continue;
		const float range = ( enemy->GetAbsOrigin() - m_me->GetAbsOrigin() ).Length();
		if ( !( music_dynamic_SI_inrange_distmax.GetFloat() > range ) ) continue;
		specialInRange = true;
		const int zombieClass = enemy->GetZombieClass();
		if ( enemy->GetTeamNumber() == TEAM_INFECTED && zombieClass >= 1 && zombieClass <= 6 ) UpdateSpecialProximity( zombieClass, enemy->entindex(), range );
	}
	float duckDamage = m_veryCloseSmoothed * 5.0f;
	if ( duckDamage > m_damageTaken.value ) duckDamage = m_damageTaken.value;
	nv_m_CIDamageDuck = 1.0f;
	if ( damage != 0.0f )
	{
		const float lo = music_dynamic_damage_duck_damage_min.GetFloat(), hi = music_dynamic_damage_duck_damage_max.GetFloat();
		duckDamage = duckDamage <= hi ? ( lo > duckDamage ? lo : duckDamage ) : hi;
		nv_m_CIDamageDuck = 1.0f - MusicRemap( duckDamage, lo, hi, music_dynamic_damage_duck_min.GetFloat(), music_dynamic_damage_duck_max.GetFloat(), false );
	}
	float mobDamage = m_damageTaken.value;
	if ( m_veryCloseSmoothed * 4.0f <= mobDamage ) mobDamage = m_veryCloseSmoothed * 4.0f;
	const float mobMin = music_dynamic_mob_damage_min.GetFloat(), mobMax = music_dynamic_mob_damage_max.GetFloat();
	const float limitedMobDamage = mobDamage <= mobMax ? ( mobMin <= mobDamage ? mobDamage : mobMin ) : mobMax;
	const float mobLevel = mobMin <= mobDamage ? MusicRemap( limitedMobDamage, mobMin, mobMax, music_dynamic_mob_min.GetFloat(), music_dynamic_mob_max.GetFloat(), false ) : 0.0f;
	m_mobbed.Peak( mobLevel, music_dynamic_mob_decay.GetFloat() );
	nv_m_CIDamageMob = m_mobbed.value < 0.1f ? 0.0f : m_mobbed.value;
	if ( m_me->IsIncapacitated() ) nv_m_CIDamageMob = 0.0f;
	if ( nv_m_CIDamageMob != m_lastMobbed )
	{
		if ( nv_m_CIDamageMob == 0.0f ) Stop( "Event.Mobbed" );
		else if ( m_lastMobbed == 0.0f && !m_me->IsIncapacitated() ) { m_stopCombatMusic = true; Play( "Event.Mobbed" ); }
	}
	m_lastMobbed = nv_m_CIDamageMob;
	const bool forceAction = TheDirector->GetScenario()->GetFinaleType() != 5 || m_isJockeyRidden || m_isPummeled || m_isPounced || m_isTongued || m_isIT || TheDirector->GetTankCount() > 0 || m_witchAttacking;
	const float actionBase = forceAction ? 1.0f : 0.0f;
	const float attackingAction = m_attackingFraction * 5.0f;
	const float firstAction = actionBase <= attackingAction ? attackingAction : actionBase;
	float action = actionBase;
	if ( firstAction <= m_gunfireFast.value ) action = m_gunfireFast.value;
	else if ( !( actionBase > attackingAction ) ) action = attackingAction;
	m_action.Peak( action, music_dynamic_action_decay.GetFloat() );
	const float threatBase = specialInRange || m_hadTank || m_witchPresent ? 1.0f : 0.0f;
	const float firstThreat = threatBase <= m_calmFraction ? m_calmFraction : threatBase;
	float threat = threatBase;
	if ( firstThreat <= m_attackingFraction * 2.0f ) threat = m_attackingFraction * 2.0f;
	else if ( !( threatBase > m_calmFraction ) ) threat = m_calmFraction;
	m_threat.Valley( threat, music_dynamic_threat_decay.GetFloat() );
	m_calm.Valley( 1.0f - actionBase, music_dynamic_calm_decay.GetFloat() );
	m_ambient.Peak( ( m_calm.value - 0.3f ) * 1.4285715f, music_dynamic_ambient_decay.GetFloat() );
	float ambience = MusicRemap( m_ambient.value, music_dynamic_ambient_in_min.GetFloat(), music_dynamic_ambient_in_max.GetFloat(), music_dynamic_ambient_out_min.GetFloat(), music_dynamic_ambient_out_max.GetFloat() );
	if ( ambience > 1.0f ) ambience = 1.0f;
	else if ( ambience < 0.0f ) ambience = 0.0f;
	nv_m_ambientVolume = ambience * MusicClamp( 1.0f - nv_m_witchRage );
	if ( static_cast< double >( m_ambient.value ) > 0.2 )
	{
		if ( MusicElapsed( m_choirTimer ) && m_calmNearCount > 0 && m_pendingChoirEntity == 0 )
		{
			m_choirTimer.Start( static_cast< float >( static_cast< double >( RandomInt( 1, music_dynamic_mob_choir_interval_randmultmax.GetInt() ) ) * music_dynamic_mob_choir_interval_beats.GetFloat() * ( 60.0 / music_dynamic_mob_choir_BPM.GetFloat() ) ) );
			m_pendingChoirEntity = m_calmNear[RandomInt( 0, m_calmNearCount - 1 )]->entindex();
			m_choirDelay.Start( 0.1f );
		}
		else if ( m_pendingChoirEntity > 0 && MusicElapsed( m_choirDelay ) )
		{
			Play( IsL4D1() ? "Event.ZombieChoir_L4D1" : "Event.ZombieChoir", m_pendingChoirEntity, -1.0f, false, true );
			m_pendingChoirEntity = 0;
		}
	}
	float solo = MusicRemap( 1.0f - m_damageTaken.value, music_dynamic_solosuccess_damagemin.GetFloat(), 1.0f, 0.0f, 1.0f );
	if ( solo > m_damageDealtSignal ) solo = m_damageDealtSignal;
	const float previousSolo = m_soloSuccess.previous;
	m_soloSuccess.previous = solo;
	if ( previousSolo != 0.0f ) m_soloSuccess.Increment( solo - previousSolo, music_dynamic_solosuccess_increment.GetFloat(), music_dynamic_solosuccess_decay.GetFloat() );
	const float dealt = m_damageDealtSignal * 2.0f;
	nv_m_zombatMusic2 = dealt >= 0.0f ? ( dealt > 1.0f ? 1.0f : dealt ) : 0.0f;

	if ( music_dynamic_on.GetInt() > 0 )
	{
		if ( m_me->GetTeamNumber() == TEAM_SURVIVOR )
		{
			if ( m_pendingMobCount > 0 && !TerrorGameRules()->IsScavengeMode() && m_inCombatMusic == 0.0f && !m_me->IsIncapacitated() && TheDirector->GetTankCount() <= 0 && !m_witchAttacking )
			{
				m_inCombatMusic = 1.0f; m_stopCombatMusic = false;
				m_damageDealtSignal = 0.0f; m_damageDealtDecayTimer.Invalidate();
				char event[128];
				if ( IsL4D1() ) Play( "Event.ZombatIntro_L4D1" );
				else { Q_snprintf( event, sizeof( event ), "Event.Zombat_Intro_%i", RandomInt( 1, 11 ) ); Play( event ); }
				if ( !Q_stricmp( GetWorldEntity()->GetMusicPostfix(), "ThePassing" ) ) Play( "Event.Zombat_ThePassing" );
				if ( IsL4D1() ) { Play( "Event.Zombat2_L4D1" ); Play( "Event.Zombat3_L4D1" ); }
				else { MakeVariantEvent( "Event.Zombat2_Intro", event, sizeof( event ) ); Play( event ); MakeVariantEvent( "Event.Zombat3_Intro", event, sizeof( event ) ); Play( event ); }
			}
			if ( m_inCombatMusic > 0.0f )
			{
				static ConVarRef mobStop( "director_music_dynamic_mob_stop_size" );
				static ConVarRef scanStop( "director_music_dynamic_scanmobstop_size" );
				const float active = m_mobApproach.value <= m_gunfireSlow.value ? m_gunfireSlow.value : m_mobApproach.value;
				if ( static_cast< double >( active ) < 0.01 || m_stopCombatMusic || m_me->IsIncapacitated() || TheDirector->GetTankCount() > 0 || m_witchAttacking ||
					( m_attackingCount <= TheDirector->GetDirectorOptionInt( "MusicDynamicMobStopSize", mobStop.GetInt() ) && m_runningCount <= TheDirector->GetDirectorOptionInt( "MusicDynamicMobScanStopSize", scanStop.GetInt() ) ) )
				{
					m_pendingMobCount = 0; m_stopCombatMusic = true; m_inCombatMusic = 0.0f; StopCombatTracks( true );
				}
			}
		}
		else if ( m_inCombatMusic > 0.0f )
		{
			m_pendingMobCount = 0; m_stopCombatMusic = true; m_inCombatMusic = 0.0f;
			StopCombatTracks( false );
			if ( !Q_stricmp( GetWorldEntity()->GetMusicPostfix(), "ThePassing" ) ) Stop( "zombat_tp", 2.0f, true );
		}
		float mobAction = MusicRemap( m_closeWeight, music_dynamic_mob_action_close_min.GetFloat(), music_dynamic_mob_action_close_max.GetFloat(), music_dynamic_mob_action_min.GetFloat(), music_dynamic_mob_action_max.GetFloat(), false );
		if ( mobAction > 1.0f ) mobAction = 1.0f;
		else if ( mobAction < 0.0f ) mobAction = 0.0f;
		nv_m_zombatMusic3 = mobAction;
	}
}

// Original S2B3C10: one server update owner and one original cadence. The
// personal-intensity and player sighting updates remain on CTerrorPlayer.
void Music::Update()
{
	if ( !music_manager.GetBool() ||
		!( static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_updateTimer.m_timestamp.Get() ) ) )
		return;
	m_updateTimer.Start( music_dynamic_update_interval.GetFloat() );
	if ( TheDirector->GetTankCount() <= 0 )
	{
		if ( m_hadTank )
		{
			m_hadTank = false;
			OnBossDefeated( 8 );
		}
	}
	else
	{
		m_hadTank = true;
		if ( !m_bossMusicActive ) OnBossApproaching();
	}

	// S2D9B70 reads playercontrolledzombies>0; S2D9AB0 tests base=="scavenge" and is NOT this gate.
	if ( m_me->GetTeamNumber() != TEAM_SURVIVOR && !TerrorGameRules()->IsPlayerControlledZombies() ) return;
	UpdatePerception();
	if ( m_rephaseAfterMobSpawn )
	{
		m_rephaseAfterMobSpawn = false;
		// S2D9B70, NOT S2D9AB0: PZ gets .125; other modes get .25. Both native multiplies are float.
		const bool playerControlledZombies = TerrorGameRules()->IsPlayerControlledZombies();
		const float interval = music_dynamic_update_interval.GetFloat();
		const int playerIndex = m_me->edict() ? m_me->entindex() : 0;
		const float phase = interval * ( playerControlledZombies ? 0.125f : 0.25f );
		m_updateTimer.Start( phase * static_cast< float >( playerIndex ) );
	}
	UpdateDynamicMusic();
	if ( !m_me->HasLeftCheckpoint() ) return;

	m_safeAtmospherePending = ( m_threat.value == 0.0f || TheDirector->GetIntensityState() == 3 ) &&
		static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_safeAtmosphereTimer.m_timestamp.Get() );
	// This exclusion REALLY calls S2D9AB0 (base=="scavenge"), NOT S2D9B70 (playercontrolledzombies>0).
	m_dangerAtmospherePending = nv_m_ambientVolume > 0.0f &&
		static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_dangerAtmosphereTimer.m_timestamp.Get() ) &&
		!TerrorGameRules()->IsSurvivalMode() && !TerrorGameRules()->IsScavengeMode();
	char event[128];
	if ( m_dangerAtmospherePlaying && nv_m_ambientVolume == 0.0f )
	{
		m_dangerAtmospherePlaying = false;
		MakeCampaignEvent( "Event.DangerAtmosphere", event, sizeof( event ) );
		Stop( event, 0.0f, false );
	}
	if ( m_safeAtmospherePending )
	{
		if ( !( static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_atmosphereCooldown.m_timestamp.Get() ) ) ) return;
		MakeCampaignEvent( "Event.SafeAtmosphere", event, sizeof( event ) );
		Play( event );
		m_safeAtmosphereTimer.Start( music_calm_min_interval.GetFloat() + 10.0f );
		m_safeAtmospherePending = false;
	}
	else if ( m_dangerAtmospherePending )
	{
		if ( !( static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_atmosphereCooldown.m_timestamp.Get() ) ) ) return;
		MakeCampaignEvent( "Event.DangerAtmosphere", event, sizeof( event ) );
		Play( event );
		m_dangerAtmospherePlaying = true;
		m_dangerAtmosphereTimer.Start( music_calm_min_interval.GetFloat() );
		m_dangerAtmospherePending = false;
	}
	// This also executes when neither event was pending. Preserve native restart order.
	m_atmosphereCooldown.Start( 3.0f );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
