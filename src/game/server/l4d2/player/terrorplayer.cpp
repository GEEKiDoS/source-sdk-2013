//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "baseviewmodel_shared.h"
#include "usercmd.h"
#include "team.h"
#include "in_buttons.h"
#include "activitylist.h"
#include "animation.h"
#include "npcevent.h"
#include "NextBot/NextBotManager.h"
#include "utllinkedlist.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "l4d2/nav/terror_nav_area.h"
#include "nav_mesh.h"
#include "BasePropDoor.h"
#include "world.h"
#include "recipientfilter.h"
#include "usermessages.h"
#include "igameevents.h"
#include "l4d2/effects/terror_environment.h"
#include "EntityFlame.h"
#include "l4d2/infected/base_ability.h"
#include "ndebugoverlay.h"
#include <emmintrin.h>
#include "datacache/imdlcache.h"
#include "tier1/KeyValues.h"
#include <float.h>
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/content/terror_model_precache.h"
#include "l4d2/player/bots/survivor_speech.h"
#include "l4d2/sound/terror_expresser.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/entities/visibility_monitor.h"
#include "ammodef.h"
#include "engine/IEngineSound.h"
#include "vprof.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "eventlist.h"
#include "hintmessage.h"
#include "l4d2/terror_message_text.h"
#include "tier1/utlmap.h"
#include "SharedFunctorUtils.h"
#include "functorutils.h"
#include "l4d2/infected/boss_zombie_player_bot.h"
#include "l4d2/entities/survivor_rescue.h"
#include "l4d2/infected/common/infected.h"
#include "doors.h"
#include "l4d2/weapons/pipe_bomb_projectile.h"
#include "l4d2/weapons/inferno.h"
#include "particle_parse.h"
#include "l4d2/director/zombie_spawn.h"
#include "tier1/checksum_crc.h"
#include "cdll_int.h"
#include "steam/steam_api.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "l4d2/player/terrorplayer_console_server_cvars.inc"
#include "tier0/icommandline.h"
#include "tier0/memdbgon.h"

namespace
{
bool EntityCountClassLess( const char *const &left, const char *const &right )
{
	// Native10001DC0; strcmp is CASE-SENSITIVE, NULL sorts last.
	if ( !left ) return false;
	if ( right ) return Q_strcmp( left, right ) < 0;
	return true;
}
}
extern ConVar nav_show_light_intensity;
extern ConVar z_frustration_spawn_delay;
extern ConVar survivor_respawn_with_guns;
extern ConVar boomer_leaker_chance;
extern ConVar z_boomer_near_dist;
extern ConVar player_use_radius;
extern int g_iNextZombieClass;
bool OnGhostBoomerVisible( CBaseEntity *entity, CBasePlayer *observer );
static ConVar z_player_zombie_debug( "z_player_zombie_debug", "0", 0x00004004 );
static ConVar z_restrict_team_change( "z_restrict_team_change", "0", 0x00004004 ); // S59B230
extern ConVar mp_gamemode;
static ConVar vs_max_team_switches( "vs_max_team_switches", "1", 0x00004004 );

// S2FAD20. This reads the one constructor-owned command counter.
int CTerrorPlayer::GetTeamSwitchRule() const
{
	if ( !Q_strnicmp( mp_gamemode.GetString(), "team", 4 ) ) return 3;
	if ( m_teamChangeCount >= vs_max_team_switches.GetInt() ) return 1;
	return TheDirector->IsGamePlaying() ? 2 : 0;
}

bool CTerrorPlayer::HasSkippedOuttro() const
{
	return m_bHasSkippedOuttro;
}

int CTerrorPlayer::GetRematchVote() const
{
	return m_nRematchVote;
}

ConVar z_gas_health( "z_gas_health", "250", 0x00004004, "Gas Zombie max health" );
ConVar z_exploding_health( "z_exploding_health", "50", 0x00004004, "Exploding Zombie max health" );
ConVar z_hunter_health( "z_hunter_health", "250", 0x00004004, "Zombie max health" );
ConVar z_spitter_health( "z_spitter_health", "100", 0x00004004, "Spitter zombie max health" );
ConVar z_jockey_health( "z_jockey_health", "325", 0x00004004, "Zombie max health" );
ConVar z_charger_health( "z_charger_health", "600", 0x00004004, "Charger max health" );

// Native flags0x4002 include DEVELOPMENTONLY. Do not unlock these registrations.
ConVar z_versus_smoker_limit( "z_versus_smoker_limit", "1", 0x00004006 );
ConVar z_versus_boomer_limit( "z_versus_boomer_limit", "1", 0x00004006 );
ConVar z_versus_hunter_limit( "z_versus_hunter_limit", "1", 0x00004006 );
ConVar z_versus_spitter_limit( "z_versus_spitter_limit", "1", 0x00004006 );
ConVar z_versus_jockey_limit( "z_versus_jockey_limit", "1", 0x00004006 );
ConVar z_versus_charger_limit( "z_versus_charger_limit", "1", 0x00004006 );

static int s_zombieClassSpawnSerial; // Native1085AB30: BSS0, never reset per round.

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

LINK_ENTITY_TO_CLASS( player, CTerrorPlayer );

extern ConVar z_noise_level_hold_time;
extern ConVar z_noise_level_fade_rate;
extern ConVar z_noise_level_max;
extern ConVar z_exploding_shove_min;
extern ConVar z_exploding_shove_max;
// Native108523DC: the real late-Spawn ammo lookup owns this BSS cache.
int g_iMinigunAmmoIndex;

static ConVar tp_schedule_post_think( "tp_schedule_post_think", "1", 0x00004004,
	"Schedule post-think operations to run at 10hz" );
static ConVar z_burn_rate( "z_burn_rate", "0.01", 0x00000006,
	"How fast the burn effect grows on burning players" );
static ConVar z_burn_max( "z_burn_max", "0.85", 0x00000006, "Max amount for the burn effect" );
static ConVar z_bbq_rate( "z_bbq_rate", "0.005", 0x00000006,
	"How fast the bbq effect grows on burning players" );
static ConVar z_bbq_min( "z_bbq_min", "0.0", 0x00000006, "Min amount for the bbq effect" );
static CUtlLinkedList< CTerrorPlayer * > s_commonPostThinkPlayers;
static int s_commonPostThinkPlayerCount;
static int s_commonPostThinkFrame = 0;
extern ConVar z_scrimmage_creep_delay;
extern ConVar z_scrimmage_creep_rate;
extern ConVar survivor_ledge_grab_ground_check_time;
extern ConVar z_noise_level_vocalize;
static ConVar z_noise_level_display( "z_noise_level_display", "0", 0x00004004,
	"Show noise levels on players?" );
static ConVar l4d_show_weaponequip( "l4d_show_weaponequip", "0", 0x00000006 );

EXTERN_SEND_TABLE( DT_CountdownTimer );
EXTERN_SEND_TABLE( DT_IntervalTimer );


BEGIN_SEND_TABLE_NOBASE( CTerrorPlayer, DT_TerrorLocalPlayerExclusive )
	SendPropVector( SENDINFO( m_shoveForce ), -1, SPROP_COORD_MP ),
	SendPropBool( SENDINFO( m_airMovementRestricted ) ),
	SendPropDataTable( SENDINFO_DT( m_stunTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropBool( SENDINFO( m_isInMissionStartArea ) ),
	SendPropBool( SENDINFO( m_isCulling ) ),
	SendPropBool( SENDINFO( m_isRelocating ) ),
	SendPropEHandle( SENDINFO( m_pushEntity ) ),
	SendPropFloat( SENDINFO( m_pushDistance ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_ghostSpawnState ), 32, 0 ),
	SendPropInt( SENDINFO( m_ghostSpawnClockMaxDelay ), 10, 0 ),
	SendPropInt( SENDINFO( m_ghostSpawnClockCurrentDelay ), 10, 0 ),
	SendPropFloat( SENDINFO( m_flNextShoveTime ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_iShovePenalty ), 4, SPROP_UNSIGNED ),
	SendPropEHandle( SENDINFO( m_dragTarget ) ),
	SendPropVector( SENDINFO( m_lineOfScrimmagePos ), 0, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_lineOfScrimmageAngle ), 10, SPROP_ROUNDDOWN, 0.0f, 359.6484375f, SendProxy_QAngles ),
	SendPropVector( SENDINFO( m_rearLineOfScrimmagePos ), 0, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_rearLineOfScrimmageAngle ), 10, SPROP_ROUNDDOWN, 0.0f, 359.6484375f, SendProxy_QAngles ),
	SendPropVector( SENDINFO( m_scrimmageSphereCenter ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_scrimmageSphereInitialRadius ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_scrimmageStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_survivorsLineOfScrimmageDistance ), 0, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_isAutoCrouchEnabled ) ),
	SendPropDataTable( SENDINFO_DT( m_autoCrouchTimer ), &REFERENCE_SEND_TABLE( DT_IntervalTimer ) ),
	SendPropArray( SendPropVector( SENDINFO_ARRAY( m_yardLineAngles ), 10, SPROP_ROUNDDOWN, 0.0f, 359.6484375f, SendProxy_QAngles ), m_yardLineAngles ),
	SendPropArray( SendPropVector( SENDINFO_ARRAY( m_yardLinePos ), -1, SPROP_COORD_MP ), m_yardLinePos ),
	SendPropDataTable( SENDINFO_DT( m_noAvoidanceTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropInt( SENDINFO( m_nVariantType ), 32, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_TimeForceExternalView ), 0, SPROP_NOSCALE ),
END_SEND_TABLE()

static void SendProxy_TerrorGestureActivity( const SendProp *pProp, const void *pStruct,
	const void *pData, DVariant *pOut, int iElement, int objectID )
{
	pOut->m_Int = TerrorActivityToOriginal( (Activity)*(const int *)pData );
}

IMPLEMENT_SERVERCLASS_ST( CTerrorPlayer, DT_TerrorPlayer )
	SendPropDataTable( "terrorlocaldata", 0, &REFERENCE_SEND_TABLE( DT_TerrorLocalPlayerExclusive ), SendProxy_SendLocalDataTable ),
	SendPropFloat( SENDINFO( m_mainSequenceStartTime ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_nSequence ), 12, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_grenadeLayerSequence ), 12, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_fireLayerSequence ), 12, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_flinchLayerSequence ), 12, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_fidgetLayerSequence ), 12, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_reloadLayerSequence ), 12, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_reloadAltLayerSequence ), 12, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_reloadLayerNumShells ), 5, 0 ),
	SendPropFloat( SENDINFO( m_grenadeLayerStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_fireLayerStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flinchLayerStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_fidgetLayerStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_reloadLayerStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_reloadAltLayerStartTime ), 0, SPROP_NOSCALE ),
	SendPropEHandle( SENDINFO( m_hBuildableButtonUseEnt ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_NetGestureSequence ),
		SendPropInt( SENDINFO_ARRAY( m_NetGestureSequence ), 12, 0 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_NetGestureActivity ),
		SendPropInt( SENDINFO_ARRAY( m_NetGestureActivity ), 12, 0, SendProxy_TerrorGestureActivity ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_NetGestureStartTime ),
		SendPropFloat( SENDINFO_ARRAY( m_NetGestureStartTime ), 0, SPROP_NOSCALE ) ),
	SendPropFloat( SENDINFO( m_fServerAnimStartTime ), 0, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_hasVisibleThreats ) ),
	SendPropFloat( SENDINFO( m_burnPercent ), 8, 0, 0.0f, 1.0f ),
	SendPropFloat( SENDINFO( m_bbqPercent ), 8, 0, 0.0f, 1.0f ),
	SendPropBool( SENDINFO( m_holdingObject ) ),
	SendPropFloat( SENDINFO( m_healthBuffer ), 10, 0, 0.0f, 200.0f ),
	SendPropFloat( SENDINFO( m_healthBufferTime ), 0, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_isGoingToDie ) ),
	SendPropFloat( SENDINFO( m_noiseLevel ), 8, 0, 0.0f, 255.0f ),
	SendPropInt( SENDINFO( m_noiseLevelLatch ), 3, SPROP_UNSIGNED | SPROP_CHANGES_OFTEN ),
	SendPropInt( SENDINFO( m_iBloodyHandsLevel ), 4, SPROP_UNSIGNED ),
	SendPropDataTable( SENDINFO_DT( m_itTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropBool( SENDINFO( m_bAdrenalineActive ) ),
	SendPropInt( SENDINFO( m_survivorCharacter ), 8 ),
	SendPropInt( SENDINFO( m_zombieClass ), 8 ),
	SendPropInt( SENDINFO( m_zombieState ), 8 ),
	SendPropBool( SENDINFO( m_isIncapacitated ) ),
	SendPropBool( SENDINFO( m_isCalm ) ),
	SendPropEHandle( SENDINFO( m_useActionOwner ) ),
	SendPropEHandle( SENDINFO( m_useActionTarget ) ),
	SendPropInt( SENDINFO( m_iCurrentUseAction ), 4, SPROP_UNSIGNED ),
	SendPropEHandle( SENDINFO( m_reviveOwner ) ),
	SendPropEHandle( SENDINFO( m_reviveTarget ) ),
	SendPropEHandle( SENDINFO( m_tongueVictim ) ),
	SendPropEHandle( SENDINFO( m_tongueOwner ) ),
	SendPropDataTable( SENDINFO_DT( m_tongueVictimTimer ), &REFERENCE_SEND_TABLE( DT_IntervalTimer ) ),
	SendPropVector( SENDINFO( m_initialTonguePullDir ), -1, SPROP_COORD_MP ),
	SendPropBool( SENDINFO( m_isHangingFromTongue ) ),
	SendPropBool( SENDINFO( m_reachedTongueOwner ) ),
	SendPropBool( SENDINFO( m_isProneTongueDrag ) ),
	SendPropEHandle( SENDINFO( m_positionEntity ) ),
	SendPropEHandle( SENDINFO( m_customAbility ) ),
	// Native stored statistics use classic arrays and retain signed widths.
	SendPropArray( SendPropInt( SENDINFO_ARRAY( m_checkpointAwardCounts ), 8, 0 ), m_checkpointAwardCounts ),
	SendPropArray( SendPropInt( SENDINFO_ARRAY( m_missionAwardCounts ), 8, 0 ), m_missionAwardCounts ),
	SendPropArray( SendPropInt( SENDINFO_ARRAY( m_checkpointZombieKills ), 16, 0 ), m_checkpointZombieKills ),
	SendPropArray( SendPropInt( SENDINFO_ARRAY( m_missionZombieKills ), 16, 0 ), m_missionZombieKills ),
	SendPropInt( SENDINFO( m_checkpointSurvivorDamage ), 16, 0 ),
	SendPropInt( SENDINFO( m_missionSurvivorDamage ), 16, 0 ),
	SendPropArray( SendPropInt( SENDINFO_ARRAY( m_classSpawnCount ), 16, 0 ), m_classSpawnCount ),
	SendPropInt( SENDINFO( m_iMaxHealth ), 16 ),
	SendPropFloat( SENDINFO( m_maxDeadDuration ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_totalDeadDuration ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_jumpSupressedUntil ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_checkpointMedkitsUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPillsUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionMedkitsUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionPillsUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointMolotovsUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionMolotovsUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPipebombsUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionPipebombsUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointBoomerBilesUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionBoomerBilesUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointAdrenalinesUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionAdrenalinesUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointDefibrillatorsUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionDefibrillatorsUsed ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointDamageTaken ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionDamageTaken ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointReviveOtherCount ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionReviveOtherCount ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointFirstAidShared ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionFirstAidShared ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointIncaps ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionIncaps ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointDamageToTank ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointDamageToWitch ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionAccuracy ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointHeadshots ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointHeadshotAccuracy ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionHeadshotAccuracy ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointDeaths ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionDeaths ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointMeleeKills ), 32, 0 ),
	SendPropInt( SENDINFO( m_missionMeleeKills ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZIncaps ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZTankDamage ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZHunterDamage ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZSmokerDamage ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZBoomerDamage ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZJockeyDamage ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZSpitterDamage ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZChargerDamage ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZKills ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZPounces ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZPushes ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZTankPunches ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZTankThrows ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZHung ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZPulled ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZBombed ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZVomited ), 32, 0 ),
	SendPropInt( SENDINFO( m_checkpointPZHighestDmgPounce ), 6, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_checkpointPZLongestSmokerGrab ), 10, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_checkpointPZLongestJockeyRide ), 8, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_checkpointPZNumChargeVictims ), 8, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_isHangingFromLedge ) ),
	SendPropBool( SENDINFO( m_isFallingFromLedge ) ),
	SendPropDataTable( SENDINFO_DT( m_hangTimer ), &REFERENCE_SEND_TABLE( DT_IntervalTimer ) ),
	SendPropVector( SENDINFO( m_hangAirPos ), -1, SPROP_COORD_MP ),
	SendPropVector( SENDINFO( m_hangPos ), -1, SPROP_COORD_MP ),
	SendPropVector( SENDINFO( m_hangStandPos ), -1, SPROP_COORD_MP ),
	SendPropVector( SENDINFO( m_hangNormal ), -1, SPROP_COORD_MP ),
	SendPropInt( SENDINFO( m_frustration ), 8, 0 ),
	SendPropInt( SENDINFO( m_clientIntensity ), 8, 0 ),
	SendPropEHandle( SENDINFO( m_pummelVictim ) ),
	SendPropEHandle( SENDINFO( m_pummelAttacker ) ),
	SendPropEHandle( SENDINFO( m_carryAttacker ) ),
	SendPropEHandle( SENDINFO( m_carryVictim ) ),
	SendPropEHandle( SENDINFO( m_pounceVictim ) ),
	SendPropEHandle( SENDINFO( m_pounceAttacker ) ),
	SendPropEHandle( SENDINFO( m_jockeyVictim ) ),
	SendPropEHandle( SENDINFO( m_jockeyAttacker ) ),
	SendPropVector( SENDINFO( m_jockeyDesiredMove ), -1, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_jockeyRotatedDesire ) ),
	SendPropInt( SENDINFO( m_knockdownReason ), 2, SPROP_UNSIGNED ),
	SendPropDataTable( SENDINFO_DT( m_knockdownTimer ), &REFERENCE_SEND_TABLE( DT_IntervalTimer ) ),
	SendPropDataTable( SENDINFO_DT( m_staggerTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropVector( SENDINFO( m_staggerStart ), -1, SPROP_COORD_MP ),
	SendPropVector( SENDINFO( m_staggerDir ), -1, SPROP_COORD_MP ),
	SendPropFloat( SENDINFO( m_staggerDist ), 0, SPROP_NOSCALE | SPROP_COORD_MP ),
	SendPropDataTable( SENDINFO_DT( m_tugTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropVector( SENDINFO( m_tugStart ), -1, SPROP_COORD_MP ),
	SendPropVector( SENDINFO( m_tugDir ), -1, SPROP_COORD_MP ),
	SendPropFloat( SENDINFO( m_tugDist ), 0, SPROP_NOSCALE | SPROP_COORD_MP ),
	SendPropInt( SENDINFO( m_currentReviveCount ), 8 ),
	SendPropBool( SENDINFO( m_bIsOnThirdStrike ) ),
	SendPropBool( SENDINFO( m_bIsFirstManOut ) ),
	SendPropDataTable( SENDINFO_DT( m_music ), &REFERENCE_SEND_TABLE( DT_Music ), SendProxy_DataTableToDataTable ),
	SendPropBool( SENDINFO( m_usingMountedGun ) ),
	SendPropBool( SENDINFO( m_usingMountedWeapon ) ),
	SendPropBool( SENDINFO( m_isGhost ) ),
	SendPropBool( SENDINFO( m_bWasPresentAtSurvivalStart ) ),
	SendPropEHandle( SENDINFO( m_vocalizationSubject ) ),
	SendPropDataTable( SENDINFO_DT( m_vocalizationSubjectTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropVector( SENDINFO( m_pounceStartPosition ), 0, SPROP_COORD_MP_INTEGRAL ),
	SendPropBool( SENDINFO( m_isAttemptingToPounce ) ),
	SendPropFloat( SENDINFO( m_vomitStart ), 0, SPROP_NOSCALE | SPROP_COORD_MP ),
	SendPropFloat( SENDINFO( m_vomitFadeStart ), 0, SPROP_NOSCALE | SPROP_COORD_MP ),
	SendPropFloat( SENDINFO( m_bashedStart ), 0, SPROP_NOSCALE | SPROP_COORD_MP ),
	SendPropFloat( SENDINFO( m_salivaStart ), 0, SPROP_NOSCALE | SPROP_COORD_MP ),
	SendPropInt( SENDINFO( m_iVersusTeam ), 3, 0 ),
	SendPropInt( SENDINFO( m_scrimmageType ), 32, 0 ),
	SendPropEHandle( SENDINFO( m_lookatPlayer ) ),
	SendPropBool( SENDINFO( m_bSurvivorGlowEnabled ) ),
	SendPropBool( SENDINFO( m_bSurvivorPositionHidingWeapons ) ),
END_SEND_TABLE()

BEGIN_DATADESC( CTerrorPlayer )
	DEFINE_USEFUNC( OnPlayerUse ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetCommentaryStatueMode", InputSetCommentaryStatueMode ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "IgnoreFallDamage", InputIgnoreFallDamage ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "IgnoreFallDamageWithoutReset", InputIgnoreFallDamageWithoutReset ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnableLedgeHang", InputEnableLedgeHang ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableLedgeHang", InputDisableLedgeHang ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SpeakResponseConcept", InputSpeakResponseConcept ),
	DEFINE_INPUTFUNC( FIELD_STRING, "TeleportToSurvivorPosition", InputTeleportToSurvivorPosition ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ReleaseFromSurvivorPosition", InputReleaseFromSurvivorPosition ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetGlowEnabled", InputSetGlowEnabled ),
	DEFINE_INPUTFUNC( FIELD_VOID, "RemoveWeaponUpgrades", InputRemoveWeaponUpgrades ),
	DEFINE_INPUTFUNC( FIELD_VOID, "CancelCurrentScene", InputCancelCurrentScene ),
END_DATADESC()


CTerrorPlayer::CTerrorPlayer()
	: m_GSClientAchievementStatusCallback( this, &CTerrorPlayer::OnGSClientAchievementStatus )
{
	m_music.SetOwner( this );
	s_commonPostThinkPlayers.AddToHead( this );
	m_lastCommonPostThink = -1.0f;
	m_commonPostThinkDue = true;
	m_flSpectatorIdleTime = FLT_MAX;
	m_wasIdle = false;
	CTerrorPlayerAnimState *state;
	{
		CMDLCacheCriticalSection cacheLock( mdlcache );
		state = new CTerrorPlayerAnimState( this );
	}
	m_PlayerAnimState = state;
	m_vecSpeechEyeVelocity.Init( 0.0f, 0.0f, 0.0f );
	m_vecSpeechEyeAcceleration.Init( 0.0f, 0.0f, 0.0f );
	m_angLastSpeechEye.Init( 0.0f, 0.0f, 0.0f );
	m_flLastSpeechEyeSample = 0.0f;
	m_hasVisibleThreats = false;
	if ( m_vomitStart.Get() != 0.0f )
		m_vomitStart.SetDirect( 0.0f );
	if ( m_vomitFadeStart.Get() != 0.0f )
		m_vomitFadeStart.SetDirect( 0.0f );
	if ( m_bashedStart.Get() != 0.0f )
		m_bashedStart.SetDirect( 0.0f );
	if ( m_salivaStart.Get() != 0.0f )
		m_salivaStart.SetDirect( 0.0f );
	m_lastAliveTime = gpGlobals->curtime;
	m_burnDamageType = DMG_BURN;
	m_bRestoringFromTransition = false;
	for ( int character = 0; character < 4; ++character )
	{
		m_survivorDirectDamage[character] = 0;
		m_survivorAssistDamage[character] = 0;
		m_survivorIncaps[character] = 0;
		m_survivorKills[character] = 0;
		m_survivorPounces[character] = 0;
		m_survivorLungeShoves[character] = 0;
		m_survivorSmokerDrags[character] = 0;
		m_survivorSmokerChokes[character] = 0;
		m_survivorVomitHits[character] = 0;
		m_survivorVomitExplosions[character] = 0;
		m_survivorClawHits[character] = 0;
		m_survivorTankRockHits[character] = 0;
		m_survivorPhysicsHits[character] = 0;
	}
	m_directSurvivorDamage = 0;
	m_assistSurvivorDamage = 0;
	m_totalSurvivorIncaps = 0;
	m_totalSurvivorKills = 0;
	m_totalSurvivorPounces = 0;
	m_totalSurvivorLungeShoves = 0;
	m_totalSurvivorSmokerDrags = 0;
	m_totalSurvivorSmokerChokes = 0;
	m_totalSurvivorVomitHits = 0;
	m_totalSurvivorVomitExplosions = 0;
	m_totalSurvivorClawHits = 0;
	m_totalSurvivorTankRockHits = 0;
	m_totalSurvivorPhysicsHits = 0;
	m_survivorIntensity.Reset();
	m_hasPendingCough = false;
	m_survivorKillScore = 0;
	m_checkpointSurvivorDamage = 0;
	m_missionSurvivorDamage = 0;
	m_totalSurvivorDamage = 0;
	for ( int index = 0; index < 9; ++index )
		m_classSpawnCount.Set( index, 0 );
	m_score = 0;
	for ( int team = 0; team < 2; ++team )
	{
		m_roundStartTime[team] = gpGlobals->curtime;
		m_hasSpawnedOnTeam[team] = false;
	}
	m_previousAASArea = NULL;
	m_pendingAASArea = NULL;
	// Native324A0F..324A2A speech construction is owned once by the embedded
	// CTerrorExpresser/PostConstructor; approved selected-source timing closureAAD2.
	// No actor speech pointer, reset, extra allocation or duplicate constructor.
	SetZombieClass( 0 );
	ResetClassSpawnStatistics();
	m_zombieClass = 9;
	m_nativeZombieVariantChoice = RandomInt( 1, 6 );
	m_survivorCharacter = 8;
	m_isGhost = false;
	m_bNativeGhostClockFlag = false;
	m_isInMissionStartArea = false;
	m_deathPosition = vec3_origin;
	m_deathTimestamp = 0.0f;
	m_minDeadDuration = 99999.9f;
	if ( m_maxDeadDuration.Get() != 0.0f )
		m_maxDeadDuration.SetDirect( 0.0f );
	if ( m_totalDeadDuration.Get() != 0.0f )
		m_totalDeadDuration.SetDirect( 0.0f );
	m_deadCount = 0;
	m_deathFlowDistance = 0.0f;
	m_bestVisibleEnemyDot = 0.0f;
	m_shoveForce = vec3_origin;
	{
		CBaseEntity *zeroHandleEntity = CHandle< CBaseEntity >( 0, 0 ).Get();
		if ( m_tongueOwner.Get() != zeroHandleEntity )
			m_tongueOwner.GetForModify().Init( 0, 0 );
	}
	{
		CBaseEntity *zeroHandleEntity = CHandle< CBaseEntity >( 0, 0 ).Get();
		if ( m_tongueVictim.Get() != zeroHandleEntity )
			m_tongueVictim.GetForModify().Init( 0, 0 );
	}
	// Native324D75 also clears unused13329; closed write-only byte is elided.
	// Only genuine13328 remains; no WORD member or second latch.
	m_tongueGrabPending = false;
	m_tongueVictimTimer.Invalidate();
	m_spawnedAsTankTimer.Invalidate();
	m_pounceSoundTimer.Invalidate();
	m_jockeyMountTimer.Invalidate();
	m_jockeyMountDuration = 0.0f;
	m_isFirmlyAttached = false;
	{
		CBaseEntity *zeroHandleEntity = CHandle< CBaseEntity >( 0, 0 ).Get();
		if ( m_customAbility.Get() != zeroHandleEntity )
			m_customAbility.GetForModify().Init( 0, 0 );
	}
	for ( int index = 0; index < 149; ++index )
		m_missionAwardCounts.Set( index, 0 );
	for ( int index = 0; index < 9; ++index )
		m_missionZombieKills.Set( index, 0 );
	ResetCheckpointStatistics();
	m_missionIncaps = 0;
	m_missionDeaths = 0;
	m_missionAccuracy = 0;
	m_missionHeadshotAccuracy = 0;
	m_missionMedkitsUsed = 0;
	m_missionFirstAidShared = 0;
	m_missionPillsUsed = 0;
	m_missionMolotovsUsed = 0;
	m_missionPipebombsUsed = 0;
	m_missionBoomerBilesUsed = 0;
	m_missionAdrenalinesUsed = 0;
	m_missionDefibrillatorsUsed = 0;
	m_missionMeleeKills = 0;
	m_missionReviveOtherCount = 0;
	m_missionDamageTaken = 0;
	m_lastPrimaryWeaponId = 0;
	m_lastPrimaryWeaponAmmo = 0;
	m_lastSecondaryWeaponId = 0;
	m_lastHadElites = false;
	m_hHiddenWeapon = NULL;
	m_teamChangeCount = 0;
	m_spawnArea = NULL;
	m_hangStandArea = NULL;
	m_pClientSettings = new KeyValues( "ClientSettings" );
	m_iNightVisionScale = 30;
	m_jockeyRawForwardMove = 0.0f;
	m_jockeyRawSideMove = 0.0f;
	m_jockeyRawYaw = 0.0f;
	m_nextCreditsLineIndex = 0;
	m_missionIDSpawnedOn = 0;
	m_teamWhenMissionStarted = 0;
	m_iBloodyHandsLevel = 0;
	m_potentiallyVisibleAreas.RemoveAll();
	m_deathEvent = NULL;
	m_hasPlayedSpawnMusic = false;
	m_isCommentaryStatue = false;
	// Preserve the real late global lifetime; only dead actor16420 is elided.
	++s_commonPostThinkPlayerCount;
	m_delayedFlashlightUpdateTime = 0.0f;
	m_bReusingInfectedSpawnState = false;
	m_statsPlayerID = 0;
	m_isIdle = false;
	m_idleTimer.Invalidate();
	m_bWasPresentAtSurvivalStart = false;
	m_flBestSurvivalTime = 0.0f;
	m_jockeyRideStartTime = 0.0f;
	m_fallDamageImmunityUntil = 0.0f;
	m_nSurvivalMedal = -1;
	m_nSpecialSpawnSlot = -1;
	m_bRespawningFromDefibrillator = false;
	m_clearFallDamageImmunityOnDamage = false;
	m_ledgeHangingEnabled = true;
	m_flGnomePickupTime = -1.0f;
	m_iGhostWarpCharacter = 0;
	{
		CBaseEntity *zeroHandleEntity = CHandle< CBaseEntity >( 0, 0 ).Get();
		if ( m_hBuildableButtonUseEnt.Get() != zeroHandleEntity )
			m_hBuildableButtonUseEnt.GetForModify().Init( 0, 0 );
	}
	m_senseFlags = 0;
}

CTerrorPlayer::~CTerrorPlayer()
{
	s_commonPostThinkPlayers.FindAndRemove( this );
	m_pClientSettings->deleteThis();
	// S325673 frees the separate native speech allocation. The sole target
	// speech is expresser-owned and has no resource-owning/destructor effects.
	if ( m_deathEvent )
	{
		m_deathEvent->deleteThis();
		m_deathEvent = NULL;
	}
	--s_commonPostThinkPlayerCount;
	// Genuine embedded members (Music first) and CCS destruct once afterward.
}

CTerrorPlayer *CTerrorPlayer::CreatePlayer( const char *pClassName, edict_t *pEdict )
{
	return static_cast< CTerrorPlayer * >( CBasePlayer::CreatePlayer( pClassName, pEdict ) );
}

void CTerrorPlayer::Precache()
{
	// S319942: live-player precache delegates without rebuilding model catalogs.
	if ( GetMaxHealth() != 0 )
	{
		BaseClass::Precache();
		return;
	}
	PrecacheTerrorPlayerModels( this );
	PrecacheMaterial( "particle/particle_smokegrenade" );
	BaseClass::Precache();
	// S3198C0: keep the original sound and particle insertion order.
	static const char *const s_Sounds[] =
	{
		"Player.JumpLand",
		"PlayerZombie.JumpLand",
		"Player.HitInternal",
		"Player.Heartbeat",
		"Player.Fall",
		"Player.BandagingWounds",
		"Player.AwardUpgrade",
		"Player.NeckSnap",
		"Blood.Splat",
		"Vomit.Use",
		"Player.UsingGasCan",
		"Player.UsingGasCanStop",
		"SmokerZombie.Pain",
		"SmokerZombie.PainShort",
		"SmokerZombie.Alert",
		"SmokerZombie.Warn",
		"SmokerZombie.TongueAttack",
		"SmokerZombie.TongueHit",
		"SmokerZombie.TongueRetract",
		"SmokerZombie.TongueFly",
		"Player.Choke",
		"HulkZombie.Thud",
		"HulkZombie.Pain",
		"HulkZombie.PainFire",
		"PlayerZombie.BecomeGhost",
		"PlayerZombie.BecomeReal",
		"PlayerZombie.Rage",
		"PlayerZombie.Breathe",
		"PlayerZombie.Attack",
		"PlayerZombie.AttackMiss",
		"PlayerZombie.AttackHit",
		"PlayerZombie.Hurt",
		"PlayerZombie.Die",
		"PlayerZombie.CullWarn",
		"PlayerZombie.Fall",
		"PlayerZombie.Thud",
		"HunterZombie.LungeLand",
		"HunterZombie.Pounce",
		"HunterZombie.Lunge",
		"HunterZombie.Pounce.Cancel",
		"HunterZombie.Pounce.Hit",
		"HunterZombie.Pounce.FlyLoop",
		"HunterZombie.Pounce.Miss",
		"HunterZombie.Growl",
		"HunterZombie.Warn",
		"HunterZombie.Alert",
		"HunterZombie.Pounce.shred",
		"HunterZombie.Death",
		"HunterZombie.Pain",
		"HunterZombie.PainShort",
		"BoomerZombie.Alert",
		"BoomerZombie.Pain",
		"BoomerZombie.PainShort",
		"JockeyZombie.Ride",
		"Hint.Critical",
		"Hint.Helpful",
		"Hint.LittleReward",
		"Hint.BigReward",
		"Instructor.LessonStart",
		"Instructor.ImportantLessonStart",
		"Scavenge.point_scored",
		"Scavenge.zero_time",
		"ScavengeSB.ZeroScore",
		"ScavengeSB.OpenTiePanel",
		"ScavengeSB.DisplayChange",
		"ScavengeSB.RoundScoreIncrement",
		"ScavengeSB.MatchScoreIncrement",
		"ScavengeSB.RoundTimeIncrement",
		"ScavengeSB.MatchScoreFinal",
		"Scavenge.ClusterSpawned",
		"Weapon.Swing",
		"Weapon.HitSurvivor",
		"Weapon.HitWorld",
		"Weapon.HitBoomer",
		"Weapon.HitInfected",
		"Weapon.HitClown",
		"Knife.StabHunter",
		"Knife.BreakTongue",
		"BlindingFlash",
		"ChargerZombie.Smash",
		"Player.LaserOn",
		"Event.NewAvailableZombie"
	};
	for ( int i = 0; i < ARRAYSIZE( s_Sounds ); ++i )
		PrecacheScriptSound( s_Sounds[i] );
	static const char *const s_Particles[] =
	{
		"water_trail_directional",
		"water_trail_directional_walk",
		"water_trail_idle",
		"burning_character",
		"burning_character_screen",
		"speech_voice",
		"water_splash",
		"cigarette_smoke",
		"smoker_spore_trail",
		"smoker_smokecloud",
		"smoker_smokecloud_camera",
		"boomer_vomit_screeneffect",
		"smoker_screen_effect",
		"spitter_slime_trail"
	};
	for ( int i = 0; i < ARRAYSIZE( s_Particles ); ++i )
		PrecacheParticleSystem( s_Particles[i] );
	PrecacheResponseSystem(); // Reuse SurvivorCharacters' S344560 owner, last.
}

void CTerrorPlayer::InitialSpawn()
{
	ResetFullConnectNotification();
	// 0x2FFF50 deliberately bypasses CCSPlayer::InitialSpawn (0x24A8B0).
	// Admission uses the deferred full-connect queue, not forced WELCOME.
	CBasePlayer::InitialSpawn();
	if ( !ShouldShowMotD() )
		return;
	if ( CommandLine()->FindParm( "-makereslists" ) )
		engine->ClientCommand( edict(), "jointeam 3\n" );
	else
		ShowMotD();
}




static void NotifySurvivorsOfSpecialSpawn( int zombieClass )
{
	for ( int index = 1; index <= gpGlobals->maxClients; ++index )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( index );
		if ( !base || FNullEnt( base->edict() ) || !base->IsPlayer() ||
			!static_cast< const CBasePlayer * >( base )->IsConnected() ||
			base->GetTeamNumber() != TEAM_SURVIVOR )
			continue;
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		// The outer specialization tests TEAM2 BEFORE the real functor tests
		// IsAlive and re-reads TEAM2. Keep that original callback order.
		if ( !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR )
			continue;
		switch ( zombieClass )
		{
		case 1: player->GetMusic()->PostponeSpecialProximity( 1 ); break;
		case 2: player->GetMusic()->PostponeSpecialProximity( 2 ); break;
		case 3: case 4: case 5:
			// Native 2FAE00 deliberately sends ALL THREE classes to Music+164.
			// This is NOT MaterializeFromGhost's true per-class notification.
			player->GetMusic()->PostponeSpecialProximity( 3 );
			break;
		case 6: player->GetMusic()->PostponeSpecialProximity( 6 ); break;
		default: break;
		}
	}
}

static void NotifyPlayersOfTankSpawn()
{
	for ( int index = 1; index <= gpGlobals->maxClients; ++index )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( index );
		if ( !base || FNullEnt( base->edict() ) || !base->IsPlayer() ||
			!static_cast< const CBasePlayer * >( base )->IsConnected() )
			continue;
		if ( TerrorGameRules()->IsPlayerControlledZombies() || base->GetTeamNumber() == TEAM_SURVIVOR )
		{
			// Native303FF0 has NO IsAlive gate: connected dead players receive it.
			static_cast< CTerrorPlayer * >( base )->GetMusic()->OnBossApproaching();
		}
	}
}

void CTerrorPlayer::Spawn()
{
	CMDLCacheCriticalSection cacheLock( mdlcache );
	VPROF_BUDGET( "CCSPlayer::Spawn", "ZOMBIE" );
	SetFadeScale( 0.0f ); // S5FA70; native540 is fade scale, not elasticity.
	KeyValues *deathEvent = m_deathEvent;
	RefreshSpectatorIdleTime();
	if ( deathEvent )
	{
		deathEvent->deleteThis();
		m_deathEvent = NULL;
	}
	if ( !m_isGhost.Get() )
		ResetInfectedLifeStatistics();
	ShowViewPortPanel( "specgui", false, NULL );
	const int shoveMaximum = z_exploding_shove_max.GetInt();
	const int shoveMinimum = z_exploding_shove_min.GetInt();
	m_staggerThreshold = RandomInt( shoveMinimum, shoveMaximum );
	if ( m_vomitStart.Get() != 0.0f )
		m_vomitStart.GetForModify() = 0.0f;
	if ( m_vomitFadeStart.Get() != 0.0f )
		m_vomitFadeStart.GetForModify() = 0.0f;
	if ( m_bashedStart.Get() != 0.0f )
		m_bashedStart.GetForModify() = 0.0f;
	if ( m_salivaStart.Get() != 0.0f )
		m_salivaStart.GetForModify() = 0.0f;
	m_lastUseActionProgressReset = 0.0f;
	m_nTerrorSplatter[0] = 0;
	m_nTerrorSplatter[1] = 0;
	m_nTerrorSplatter[2] = 0;
	m_nTerrorSplatter[3] = 0;
	m_potentiallyVisibleAreas.RemoveAll();
	m_navAreaChangedInterval.Invalidate();
	m_spawnedAsTankTimer.Invalidate();
	m_newlyVisibleArea = 0.0f;
	if ( m_burnPercent.Get() != 0.0f )
		m_burnPercent.GetForModify() = 0.0f;
	if ( m_bbqPercent.Get() != 1.0f )
		m_bbqPercent.GetForModify() = 1.0f;
	m_iBloodyHandsLevel = 0;
	m_physicsDamageImmunityTimer.Invalidate();
	m_hTugSource = NULL;
	{
		CHandle< CBaseEntity > zeroHandle;
		zeroHandle.Init( 0, 0 );
		CBaseEntity *zeroEntity = zeroHandle.Get();
		if ( m_dragTarget.Get() != zeroEntity )
			m_dragTarget.GetForModify().Init( 0, 0 );
	}
	m_dragOwner = NULL;
	m_dragOwnerLastPosition.Init();
	m_wasAirborne = false;
	m_isIncapacitated = false;
	m_incapacitatedByAttacker = NULL;
	m_incapacitatedTimestamp.Invalidate();
	m_incapacitationRecoveryTimer.Invalidate();
	m_invulnerabilityTimer.Invalidate();
	m_noAvoidanceTimer.Invalidate();
	{
		CHandle< CBaseEntity > zeroHandle;
		zeroHandle.Init( 0, 0 );
		CBaseEntity *zeroEntity = zeroHandle.Get();
		if ( m_positionEntity.Get() != zeroEntity )
			m_positionEntity.GetForModify().Init( 0, 0 );
	}
	m_isCalm = false;
	m_calmWeaponTimer.Invalidate();
	m_incapacitatedZombieKills = 0;
	{
		CHandle< CBaseEntity > zeroHandle;
		zeroHandle.Init( 0, 0 );
		CBaseEntity *zeroEntity = zeroHandle.Get();
		if ( m_reviveOwner.Get() != zeroEntity )
			m_reviveOwner.GetForModify().Init( 0, 0 );
	}
	{
		CHandle< CBaseEntity > zeroHandle;
		zeroHandle.Init( 0, 0 );
		CBaseEntity *zeroEntity = zeroHandle.Get();
		if ( m_reviveTarget.Get() != zeroEntity )
			m_reviveTarget.GetForModify().Init( 0, 0 );
	}
	m_currentReviveCount = 0;
	m_totalReviveCount = 0;
	m_bIsOnThirdStrike = false;
	m_bIsFirstManOut = false;
	SetUse( &CTerrorPlayer::OnPlayerUse ); // S320070 NONvirtual callback; inherited CBaseEntity::Use dispatch remains.
	m_itTimer.Invalidate();
	m_bWasIT = false;
	m_bIsITUntilNextAttack = false;
	m_bCarryingHealingGnome = false;
	m_adrenalineTimer.Invalidate();
	m_bAdrenalineActive = false;
	m_defibrillatorReturnToLifeTimer.Invalidate();
	m_positionStillTimer.Invalidate();
	m_viewStillTimer.Invalidate();
	m_vecSpeechEyeVelocity.Init();
	m_vecSpeechEyeAcceleration.Init();
	m_angLastSpeechEye.Init();
	m_flLastSpeechEyeSample = 0.0f;
	m_frustration = 0;
	{
		const float duration = z_frustration_spawn_delay.GetFloat();
		const double deadline = static_cast< double >( gpGlobals->curtime ) + static_cast< double >( duration );
		const float storedDeadline = static_cast< float >( deadline );
		if ( static_cast< double >( m_frustrationGraceTimer.m_timestamp.Get() ) != deadline )
			m_frustrationGraceTimer.m_timestamp.GetForModify() = storedDeadline;
		if ( m_frustrationGraceTimer.m_duration.Get() != duration )
			m_frustrationGraceTimer.m_duration.GetForModify() = duration;
	}
	if ( m_shoveForce.Get().x != 0.0f || m_shoveForce.Get().y != 0.0f || m_shoveForce.Get().z != 0.0f )
		m_shoveForce.GetForModify().Init();
	m_survivorDistressResponseTimer.Invalidate();
	m_pounceSoundTimer.Invalidate();
	m_downAndBeatenTimer.Invalidate();
	m_isBleedingOut = false;
	m_savedHealth = 0;
	m_savedHealthBuffer = 0;
	m_canGrabLedge = false;
	m_isHangingFromLedge = false;
	RemoveHangLighting();
	m_isFallingFromLedge = false;
	m_isHangingFromTongue = false;
	m_reachedTongueOwner = false;
	m_isProneTongueDrag = false;
	m_hangTimer.Invalidate();
	if ( m_hangAirPos.Get().x != 0.0f || m_hangAirPos.Get().y != 0.0f || m_hangAirPos.Get().z != 0.0f )
		m_hangAirPos.GetForModify().Init();
	if ( m_hangPos.Get().x != 0.0f || m_hangPos.Get().y != 0.0f || m_hangPos.Get().z != 0.0f )
		m_hangPos.GetForModify().Init();
	m_hangFallPos.Init();
	if ( m_hangStandPos.Get().x != 0.0f || m_hangStandPos.Get().y != 0.0f || m_hangStandPos.Get().z != 0.0f )
		m_hangStandPos.GetForModify().Init();
	if ( m_hangNormal.Get().x != 0.0f || m_hangNormal.Get().y != 0.0f || m_hangNormal.Get().z != 0.0f )
		m_hangNormal.GetForModify().Init();
	m_isReleasingLedge = false;
	m_hangStandArea = NULL;
	m_isFirmlyAttached = false;
	m_jockeyRotatedDesire = false;
	// S3275F6 XORPS dominates this store. Expiry346491 uses -1; Spawn DOES NOT.
	m_lastSpitTime = 0.0f;
	m_bSurvivorGlowEnabled = true;
	m_hInstancedScene = NULL; // No scene Cancel in this native phase.
	m_hasCommentedOnDeadFriend[0] = false;
	m_hasCommentedOnDeadFriend[1] = false;
	m_hasCommentedOnDeadFriend[2] = false;
	m_hasCommentedOnDeadFriend[3] = false;

	// S327844: the SINGLE genuine CCS Spawn, after all preceding Terror state.
	BaseClass::Spawn();
	ResetForSpawn(); // S2CB580: original Terror-only virtual558/+2232.
	if ( TerrorGameRules()->IsSurvivalMode() ||
		( !IsAwaitingTransitionRestore() && !g_bTerrorRestartAfterDeath && survivor_respawn_with_guns.GetInt() != 0 ) )
		RestorePreviousWeapons();
	m_hasSpawnedOnTeam[GetTeamNumber() % 2] = true;
	m_isInMissionStartArea = TheDirector->GetChapterNumber() == 0;
	if ( m_isInMissionStartArea.Get() )
	{
		TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
		if ( area && !( area->GetSpawnAttributes() & TERROR_NAV_CHECKPOINT ) && m_isInMissionStartArea.Get() )
			m_isInMissionStartArea = false;
	}
	m_commonRunningTimer.Invalidate();
	m_runExertionTimer.Invalidate();
	m_isSneaking = false;
	// S29C6F0, same existing intensity storage; not the broad perception reset.
	m_survivorIntensity.Reset(); // Genuine S29C6F0 subobject owner; preserves its hold countdown.
	m_hasPendingCough = false;
	m_seenByEnemyInterval.Invalidate();
	m_enemySeenInterval.Invalidate();
	m_staggerSourceTimer.Invalidate();
	m_damageStaggerTimer.Invalidate();
	m_vomitDamageSourceTimer.Invalidate();
	m_coughSourceTimer.Invalidate();
	m_tongueReleaseTimer.Invalidate();
	m_launchSourceTimer.Invalidate();
	{
		const double deadline = static_cast< double >( gpGlobals->curtime ) + 0.25;
		const float storedDeadline = static_cast< float >( deadline );
		if ( static_cast< double >( m_zombieStateUpdateTimer.m_timestamp.Get() ) != deadline )
			m_zombieStateUpdateTimer.m_timestamp.GetForModify() = storedDeadline;
		if ( m_zombieStateUpdateTimer.m_duration.Get() != 0.25f )
			m_zombieStateUpdateTimer.m_duration.GetForModify() = 0.25f;
	}
	DeactivateMovementConstraint(); // S327BA2 -> genuine CBasePlayer S178220.
	// Native12992's write-only store is elided by the complete92FC/B328 closure.
	m_burnAttacker = NULL;
	m_commonDamageTimer.Invalidate(); // S327BB8, actual15012 interval.
	m_previousDamageTime = -1.0f;
	m_previousTerrorFireTimestamp = -1.0f;
	m_damageTimer.Invalidate();
	m_burnDamageTimer.Invalidate();
	m_terrorInjuryInterval.Invalidate();
	if ( m_jumpSupressedUntil.Get() != -1.0f )
		m_jumpSupressedUntil.GetForModify() = -1.0f;
	SetSolid( SOLID_BBOX );
	RemoveFlag( FL_FROZEN );
	ClearZoomOwner();
	SetFOV( this, 0, 0.0f, 0 );
	if ( m_Local.m_vecPunchAngle.Get().x != 0.0f || m_Local.m_vecPunchAngle.Get().y != 0.0f || m_Local.m_vecPunchAngle.Get().z != 0.0f )
		m_Local.m_vecPunchAngle.GetForModify().Init();
	if ( m_Local.m_vecPunchAngleVel.Get().x != 0.0f || m_Local.m_vecPunchAngleVel.Get().y != 0.0f || m_Local.m_vecPunchAngleVel.Get().z != 0.0f )
		m_Local.m_vecPunchAngleVel.GetForModify().Init();
	m_friendlyFireSubject = 0;
	m_friendlyFireResponseTimer.Invalidate();
	m_painTimer.Invalidate();
	m_friendlyFireDamageType = 0;
	m_friendlyFireWeapon[0] = '\0';
	m_staggerTimer.Invalidate();
	if ( m_staggerStart.Get().x != 0.0f || m_staggerStart.Get().y != 0.0f || m_staggerStart.Get().z != 0.0f )
		m_staggerStart.GetForModify().Init();
	if ( m_staggerDir.Get().x != 0.0f || m_staggerDir.Get().y != 0.0f || m_staggerDir.Get().z != 0.0f )
		m_staggerDir.GetForModify().Init();
	if ( m_staggerDist.Get() != 0.0f )
		m_staggerDist.GetForModify() = 0.0f;
	m_pendingStaggerEventData = -1;
	m_tugTimer.Invalidate();
	if ( m_tugStart.Get().x != 0.0f || m_tugStart.Get().y != 0.0f || m_tugStart.Get().z != 0.0f )
		m_tugStart.GetForModify().Init(); // S327FBD: one notify before all three stores.
	if ( m_tugDir.Get().x != 0.0f || m_tugDir.Get().y != 0.0f || m_tugDir.Get().z != 0.0f )
		m_tugDir.GetForModify().Init();
	if ( m_tugDist.Get() != 0.0f )
		m_tugDist.GetForModify() = 0.0f;
	m_bHasSkippedOuttro = false;
	m_nRematchVote = 0;
	if ( GetTeamNumber() == TEAM_INFECTED )
	{
		m_bHasNightVision = true;
		m_bNightVisionOn = false;
	}
	else
	{
		m_bHasNightVision = false;
		m_bNightVisionOn = false;
		TheDirector->UpdateHumanSurvivorCount();
		m_flLastActivityTime = gpGlobals->curtime; // Angles are deliberately retained.
	}
	m_wasIdle = false;
	m_occupiedNavAreaInterval.Start();
	CSingleUserRecipientFilter filter( this );
	filter.MakeReliable();
	UserMessageBegin( filter, "Spawn" );
	WRITE_BYTE( GetTeamNumber() );
	MessageEnd();
	for ( int index = 1; index <= gpGlobals->maxClients; ++index )
	{
		CBasePlayer *observer = UTIL_PlayerByIndex( index );
		if ( observer && observer != this && !observer->IsBot() && observer->GetObserverMode() == OBS_MODE_IN_EYE && observer->GetObserverTarget() == this )
			filter.AddRecipient( observer );
	}
	UserMessageBegin( filter, "SplatterClear" );
	MessageEnd();
	m_isCulling = false;
	m_stunTimer.Invalidate();
	m_airMovementRestricted = false;
	m_flGhostSpawnDeadline = m_flPendingGhostSpawnDeadline;
	m_flPendingGhostSpawnDeadline = 0.0f;
	if ( m_survivorsLineOfScrimmageDistance.Get() != 0.0f )
		m_survivorsLineOfScrimmageDistance.GetForModify() = 0.0f;
	SetHealthBuffer( 0.0f );
	m_isGoingToDie = false;
	ApplyTempHealthOnlyMutation();
	if ( m_noiseLevel.Get() != 0.0f )
		m_noiseLevel.GetForModify() = 0.0f;
	m_lastNoiseTime = gpGlobals->curtime;
	m_noiseLevelLatch = 0;
	m_bReloadVoicePlayed = false;
	m_delayedDSP = 0;
	m_delayedDSPTimer.Invalidate();
	enginesound->SetPlayerDSP( filter, 0, false );
	m_spawnAreaSamplePosition = WorldSpaceCenter();
	m_spawnArea = TheNavMesh->GetNavArea( m_spawnAreaSamplePosition, 120.0f );
	m_suppressDeathEffects = false;
	m_deathEventAborted = false;

	if ( GetTeamNumber() == TEAM_INFECTED )
	{
		float waitingTime = 0.0f;
		if ( m_deathTimestamp > 0.0f )
		{
			m_deadCount = static_cast< int >( static_cast< unsigned int >( m_deadCount ) + 1U );
			waitingTime = gpGlobals->curtime - m_deathTimestamp;
			if ( m_minDeadDuration > waitingTime )
				m_minDeadDuration = waitingTime;
			if ( waitingTime > m_maxDeadDuration.Get() )
				m_maxDeadDuration.GetForModify() = waitingTime;
			const float oldTotal = m_totalDeadDuration.Get();
			const float total = oldTotal + waitingTime;
			if ( oldTotal != total )
				m_totalDeadDuration.GetForModify() = total;
			m_deathTimestamp = 0.0f;
		}
		if ( TerrorGameRules()->IsPlayerControlledZombies() && !IsBot() && !m_bReusingInfectedSpawnState && g_iNextZombieClass != 8 )
		{
			// S305060 is only this network int's dirty notifier, NOT a queue.
			if ( m_zombieClass.Get() != 0 )
				m_zombieClass.GetForModify() = 0;
			g_iNextZombieClass = SelectNextZombieClass( 1 );
			g_iNextZombieClass = TheDirector->GetScriptedMode()->ConvertZombieClass( g_iNextZombieClass );
		}
		SetZombieClass( g_iNextZombieClass );
		const int count = static_cast< int >( static_cast< unsigned int >( m_classSpawnCount[g_iNextZombieClass] ) + 1U );
		m_classSpawnCount.Set( g_iNextZombieClass, count );
		if ( !IsBot() && z_player_zombie_debug.GetInt() )
		{
			const int debugClass = GetZombieClass();
			const char *debugClassName = GetZombieClassName( debugClass );
			const char *debugPlayerName = GetPlayerName();
			DevMsg( "Player %s spawned as %s after waiting %3.2f seconds\n", debugPlayerName, debugClassName, static_cast< double >( waitingTime ) );
		}
		if ( IsGhost() )
			EmitPrivateSound( "PlayerZombie.BecomeGhost" );
		if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 2 )
		{
			const int chance = boomer_leaker_chance.GetInt();
			if ( RandomInt( 1, 100 ) < chance )
				SetVariantType( 1 );
			if ( !IsGhost() )
			{
				PlayTrackedSound( "BoomerZombie.Gurgle", 0.0f );
				NotifySurvivorsOfSpecialSpawn( 2 );
				EmitInfectedSound( "BoomerZombie.Alert", 0.0f, 0.0f );
				if ( m_nVariantType.Get() == 1 )
					ClientPrint( this, HUD_PRINTCENTER, "YOU ARE A LEAKER!" );
				VisibilityMonitor_AddEntity( this, z_boomer_near_dist.GetFloat(), OnGhostBoomerVisible, NULL );
			}
		}
		else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 1 )
		{
			if ( !IsGhost() )
			{
				NotifySurvivorsOfSpecialSpawn( 1 );
				EmitInfectedSound( "SmokerZombie.Alert", 0.0f, 0.0f );
			}
		}
		else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 )
		{
			if ( !IsGhost() )
			{
				NotifySurvivorsOfSpecialSpawn( 3 );
				EmitInfectedSound( "HunterZombie.Alert", 0.0f, 0.0f );
			}
		}
		else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 5 )
		{
			if ( !IsGhost() )
			{
				NotifySurvivorsOfSpecialSpawn( 5 );
				EmitInfectedSound( "JockeyZombie.Alert", 0.0f, 0.0f );
			}
		}
		else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 6 )
		{
			if ( !IsGhost() )
			{
				NotifySurvivorsOfSpecialSpawn( 6 );
				EmitInfectedSound( "ChargerZombie.Alert", 0.0f, 0.0f );
			}
		}
		else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 4 )
		{
			if ( !IsGhost() )
			{
				NotifySurvivorsOfSpecialSpawn( 4 );
				EmitInfectedSound( "SpitterZombie.Alert", 0.0f, 0.0f );
			}
		}
		else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
		{
			if ( !TheZombieManager->IsTransferringTank() )
			{
				if ( !MyNextBotPointer() )
					NotifyPlayersOfTankSpawn();
				IGameEvent *event = gameeventmanager->CreateEvent( "tank_spawn" );
				if ( event )
				{
					event->SetInt( "userid", GetUserID() );
					event->SetInt( "tankid", entindex() );
					gameeventmanager->FireEvent( event );
				}
			}
		}
	}
	else
	{
		ResetClassSpawnStatistics();
		m_deathTimestamp = 0.0f;
	}
	CollisionProp()->SetSurroundingBoundsType( USE_SEQUENCE_BOUNDS, NULL, NULL );
	Extinguish();
	if ( GetCustomAbility() )
		UTIL_Remove( GetCustomAbility() );
	// S328AFA dirties UNCONDITIONALLY, BEFORE the factory. Do not use an
	// ordinary handle assignment that compares after creation or skips NULL/NULL.
	CBaseHandle &abilityHandle = m_customAbility.GetForModify();
	abilityHandle.Set( CBaseAbility::CreateForPlayer( this ) );
	if ( m_bReusingInfectedSpawnState && GetCustomAbility() )
	{
		const float recharge = m_reusedInfectedRecharge;
		if ( recharge >= 0.0f )
			GetCustomAbility()->SetRechargeTime( recharge, m_reusedInfectedRechargeDuration );
	}
	m_hasLeftCheckpoint = false;
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
		m_delayedFlashlightUpdateTime = gpGlobals->curtime + 0.5f;
	else
		FlashlightTurnOn( false ); // S328C11, before checkpoint/music and for all teams.
	if ( m_spawnArea && GetTeamNumber() == TEAM_SURVIVOR )
	{
		TerrorNavArea *area = ToTerrorNavArea( m_spawnArea );
		if ( area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
		{
			CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetCheckpoint( area->GetCenter() );
			if ( checkpoint && !checkpoint->IsOutward() && TheDirector->GetChapterNumber() == 0 )
			{
				if ( !m_hasPlayedSpawnMusic )
				{
					m_hasPlayedSpawnMusic = true;
					if ( !TerrorGameRules()->IsSurvivalMode() )
						GetMusic()->OnMissionStart();
				}
			}
			else if ( TheTerrorNavMesh()->GetExitCheckpoint() )
			{
				if ( !TerrorGameRules()->IsSurvivalMode() )
					GetMusic()->OnCheckpointReached();
			}
			else if ( !TerrorGameRules()->IsSurvivalMode() )
				GetMusic()->OnFinaleCheckpointReached();
		}
	}
	if ( GetTeamNumber() == TEAM_SURVIVOR && TerrorGameRules()->IsSurvivalMode() )
		GetMusic()->OnEnterSurvival();
	m_pendingPounceAttacker = NULL;
	m_pendingPounceTime = 0.0f;
	m_pounceVictim = NULL; // Pure S30C0E0 network-handle setter.
	m_pounceAttacker = NULL; // Pure S30C1A0 network-handle setter.
	m_isAttemptingToPounce = false;
	m_knockdownTimer.Invalidate();
	m_knockdownReason = 0;
	m_pounceDamageTimer.Invalidate();
	m_jockeyDamageTimer.Invalidate();
	m_jockeyStaggerPhaseY = 0.0f;
	m_jockeyControlPhase = 0.0f;
	m_jockeyStaggerPhaseX = 0.0f;
	// S328E46 snapshots restore FIRST; the three stores precede even the
	// guarded Director epoch query/callback and remain unconditional.
	const bool recordMission = !m_bRestoringFromTransition;
	m_bReusingInfectedSpawnState = false;
	m_hasReportedStuck = false;
	m_ledgeHangingEnabled = true;
	if ( recordMission )
	{
		const int mission = TheDirector->GetMissionID();
		if ( m_missionIDSpawnedOn != mission )
		{
			m_missionIDSpawnedOn = mission;
			m_teamWhenMissionStarted = GetTeamNumber();
			OnFirstSpawnInMission();
		}
	}
	StopTrackedSound( "Player.Heartbeat" ); // S328E99, before speech Reset.
	static_cast< CTerrorExpresser * >( GetMultiplayerExpresser() )->GetSurvivorSpeech()->Reset(); // S328EA4 AFTER mission callback.
	const QAngle &eyes = EyeAngles();
	if ( m_angEyeAngles.Get().x != eyes.x || m_angEyeAngles.Get().y != eyes.y || m_angEyeAngles.Get().z != eyes.z )
		m_angEyeAngles.GetForModify() = eyes;
	GetTerrorAnimState()->Update( m_angEyeAngles.Get().y, m_angEyeAngles.Get().x );
	const int minigunAmmoType = GetAmmoDef()->Index( "AMMO_TYPE_MINIGUN" );
	m_bBoomerFalling = false;
	const float hearingDeadline = m_commonHearingTimer.m_timestamp.Get();
	g_iMinigunAmmoIndex = minigunAmmoType;
	if ( hearingDeadline != -1.0f )
		m_commonHearingTimer.m_timestamp.GetForModify() = -1.0f;
	const char *spawnClass;
	if ( GetTeamNumber() == TEAM_INFECTED )
		spawnClass = GetZombieClassName( GetZombieClass() );
	else
		spawnClass = GetResponseCharacterName();
	UTIL_LogPrintf( "%s spawned as a %s\n", GetPlayerName(), spawnClass );
	TheTerrorNavMesh()->ShowFlowErrors( this ); // EXACT S328FE9, not an early phase.
	if ( !IsGhost() )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "player_spawn" );
		if ( event )
		{
			event->SetInt( "userid", engine->GetPlayerUserId( edict() ) );
			gameeventmanager->FireEvent( event );
		}
	}
	if ( GetTeamNumber() == TEAM_INFECTED && GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
	{
		const double deadline = static_cast< double >( gpGlobals->curtime ) + 0.5;
		const float storedDeadline = static_cast< float >( deadline );
		if ( static_cast< double >( m_spawnedAsTankTimer.m_timestamp.Get() ) != deadline )
			m_spawnedAsTankTimer.m_timestamp.GetForModify() = storedDeadline;
		if ( m_spawnedAsTankTimer.m_duration.Get() != 0.5f )
			m_spawnedAsTankTimer.m_duration.GetForModify() = 0.5f;
	}
	if ( TerrorGameRules()->IsVersusMode() )
		TerrorGameRules()->CacheVersusScores( false );
	CTerrorExpresser *expresser = static_cast< CTerrorExpresser * >( GetMultiplayerExpresser() );
	if ( expresser )
		expresser->GetSurvivorSpeech()->ResetScenarioJoin(); // Final S3290A8, same nullable owner.
	// VProf closes first, then the captured-pointer MDLCache lock.
}

CBaseEntity *CTerrorPlayer::EntSelectSpawnPoint()
{
	// 0x3254B0: placement belongs to CTerrorGameRules::GetPlayerSpawnSpot.
	return NULL;
}


bool CTerrorPlayer::ShouldAutoSelectTeam()
{
	if ( IsBot() )
		return false;
	static ConVarRef survivor_limit( "survivor_limit" );
	if ( GetGlobalTeam( TEAM_SURVIVOR )->GetNumPlayers() < survivor_limit.GetInt() )
		return HandleCommand_JoinTeam( TEAM_SURVIVOR, SurvivorCharacter_Unknown );
	return false;
}

bool CTerrorPlayer::HandleCommand_JoinTeam( int iTeam, int iCharacter, bool bForce )
{
	if ( iTeam == GetTeamNumber() )
		return true;
	if ( !GetGlobalTeam( iTeam ) )
	{
		Warning( "HandleCommand_JoinTeam( %d ) - invalid team index.\n", iTeam );
		return false;
	}
	if ( iTeam == TEAM_L4D1_SURVIVOR && !IsBot() )
		return false;
	if ( !TheTerrorNavMesh()->HasFlowDistances() )
	{
		ClientPrint( this, HUD_PRINTCENTER, "ERROR: FLOW IS BROKEN - DIRECTOR CANNOT POPULATE WORLD" );
		ClientPrint( this, HUD_PRINTTALK, "ERROR: FLOW IS BROKEN - DIRECTOR CANNOT POPULATE WORLD" );
	}
	ClearLastKnownArea();
	int team = iTeam;
	bool wantsToPlay = false;
	if ( ( team == TEAM_SURVIVOR || team == TEAM_INFECTED ) && !bForce )
	{
		if ( z_restrict_team_change.GetInt() == 0 )
		{
			if ( iCharacter == SurvivorCharacter_Unknown || IsBot() )
			{
				int countMode = team == TEAM_SURVIVOR ? 1 : ( IsBot() ? 2 : 0 );
				if ( TheDirector->IsTeamFull( team, countMode ) )
					team = team == TEAM_SURVIVOR ? TEAM_INFECTED : TEAM_SURVIVOR;
				countMode = team == TEAM_SURVIVOR ? 1 : ( IsBot() ? 2 : 0 );
				if ( TheDirector->IsTeamFull( team, countMode ) )
				{
					if ( GetTeamNumber() == TEAM_UNASSIGNED )
					{
						team = TEAM_SPECTATOR;
						wantsToPlay = true;
					}
					else
					{
						team = GetTeamNumber();
						wantsToPlay = team == TEAM_SPECTATOR;
					}
				}
			}
			else if ( TheDirector->IsTeamFull( team, 0 ) )
				return false;
			if ( team == TEAM_INFECTED && GetTeamNumber() != TEAM_INFECTED )
			{
				if ( IsAlive() )
				{
					m_suppressDeathEffects = true;
					m_deathEventAborted = true;
				}
				ResetClassSpawnStatistics();
				m_deathTimestamp = 0.0f;
			}
		}
		else if ( !TheDirector->IsTeamFull( TEAM_SURVIVOR, 1 ) )
			team = TEAM_SURVIVOR;
		else if ( TheDirector->IsTeamFull( TEAM_INFECTED, 0 ) )
		{
			team = TEAM_SPECTATOR;
			wantsToPlay = true;
		}
		else
		{
			team = TEAM_INFECTED;
			CommitSuicide( false );
			ResetClassSpawnStatistics();
			m_deathTimestamp = 0.0f;
		}
	}
	// Native11968 is a RAW int. -1/999 must not cause an enum cast or clamp.
	m_iDesiredSurvivorCharacter = iCharacter;
	if ( iCharacter == SurvivorCharacter_Unknown || GetTeamNumber() != team )
	{
		ChangeTeam( team );
		if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
		{
			const SurvivorCharacterType character = SelectSurvivorCharacter( m_iDesiredSurvivorCharacter );
			if ( m_iClass.Get() != character + 1 )
				m_iClass.GetForModify() = character + 1;
			const char *model = GetTerrorSurvivorModelForCharacter( character );
			const char *previousModel = STRING( GetModelName() );
			if ( !previousModel )
				previousModel = "";
			const char *playerName = GetPlayerName();
			UTIL_LogPrintf( "(SKIN) Survivor %s changed model from %s to %s via HandleCommand_JoinTeam\n", playerName, previousModel, model );
			SetModel( model );
			SetSurvivorCharacter( character );
		}
		m_wantsToPlay = wantsToPlay;
		ShowViewPortPanel( "takeover_survivor_bar", false, NULL );
		UpdateTeamDesired();
	}
	return true;
}

extern ConVar z_damage_screen_fade_alpha;
extern ConVar z_damage_screen_fade_duration;
extern ConVar tauntfrequency;
extern ConVar bot_mimic;
extern const char *GetInfectedVariantResponseName( int variant );

namespace
{
int CommandActivityNameCompare( const char *const *left, const char *const *right )
{
	return Q_stricmp( *left, *right ); // S2FBBD0; uniqueness is still pointer identity.
}

void NotifyCommandOuttroStateChanged()
{
	CBaseEntity *stats = gEntList.FindEntityByClassname( NULL, "env_outtro_stats" );
	if ( stats )
	{
		variant_t empty;
		stats->AcceptInput( "SkipStateChanged", NULL, NULL, empty, 0 );
	}
}
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// S3120E0. Weapon-source RTTI, actor scan, then the distinct close-door trace.
const char *CTerrorPlayer::GetSmartLookConcept( CBaseEntity *target, char *modifiers, int size )
{
	const char *concept = "PlayerLook";
	if ( !target )
		target = FindUseEntity( player_use_radius.GetFloat() * 2.0f, 0.0f, 0.0f, NULL, false );
	IResponseRulesWeaponSource *weaponSource = dynamic_cast< IResponseRulesWeaponSource * >( target );
	if ( weaponSource )
	{
		const char *weaponName = weaponSource->GetResponseRulesName();
		if ( weaponName )
		{
			Q_strncat( modifiers, ",weaponname:", size );
			Q_strncat( modifiers, weaponName, size );
			return "PlayerSpotWeapon";
		}
	}
	fogparams_t fog;
	GetFogParams( &fog );
	static ConVarRef survivor_fog_vocalize_percent( "survivor_fog_vocalize_percent" );
	const float range = fog.enable ? survivor_fog_vocalize_percent.GetFloat() * fog.end : 8192.0f;
	TargetScan< CBaseCombatCharacter > scan( this, TEAM_INFECTED, 0.3f, range, 50.0f, false );
	ForEachActor( scan );
	CBaseCombatCharacter *actor = scan.GetTarget();
	if ( actor )
	{
		CTerrorPlayer *player = ToTerrorPlayer( actor );
		Infected *infected = player ? NULL : dynamic_cast< Infected * >( actor );
		const char *special = NULL;
		if ( player || infected )
		{
			const int zombieClass = actor->GetZombieClass();
			if ( zombieClass != 0 )
				special = GetZombieClassName( zombieClass );
			else if ( infected )
				special = GetInfectedVariantResponseName( infected->GetGender() );
		}
		if ( special && special[0] )
		{
			concept = "PlayerWarnSpecial";
			Q_strncat( modifiers, ",SpecialType:", size );
			Q_strncat( modifiers, special, size );
		}
		else
		{
			Vector forward;
			AngleVectors( EyeAngles(), &forward );
			Vector velocity = actor->GetAbsVelocity();
			const float speedSquared = ( velocity.y * velocity.y + velocity.x * velocity.x ) + velocity.z * velocity.z;
			VectorNormalize( velocity );
			if ( forward.y * velocity.y + velocity.x * forward.x + forward.z * velocity.z < -0.3f &&
				speedSquared >= 20000.0f )
				concept = "PlayerIncoming";
		}
	}
	if ( FStrEq( concept, "PlayerLook" ) )
	{
		Vector forward;
		AngleVectors( EyeAngles(), &forward );
		const Vector eye = EyePosition();
		const Vector end = eye + forward * 1024.0f;
		CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
		trace_t trace;
		UTIL_TraceLine( eye, end, 1174421507, this, COLLISION_GROUP_NONE, &trace );
		CBaseEntity *hit = trace.m_pEnt;
		if ( hit && hit->ClassMatches( "func_door*" ) )
		{
			CBaseDoor *door = dynamic_cast< CBaseDoor * >( hit );
			if ( door && !door->IsDoorLocked() )
				return "PlayerLockTheDoor";
		}
		hit = trace.m_pEnt;
		if ( hit && hit->ClassMatches( "prop_door*" ) )
		{
			CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( hit );
			if ( door && !door->IsDoorLocked() )
				return "PlayerLockTheDoor";
		}
		if ( trace.fraction < 0.2f )
			concept = "PlayerLookHere";
	}
	return concept;
}

// S312650. The original owns one shared, overwritten 32-byte result.
const char *CTerrorPlayer::GetHurrahConcept()
{
	static char concept[32];
	Q_strncpy( concept, "PlayerHurrah", sizeof( concept ) );
	fogparams_t fog;
	GetFogParams( &fog );
	static ConVarRef survivor_fog_vocalize_percent( "survivor_fog_vocalize_percent" );
	const float range = fog.enable ? survivor_fog_vocalize_percent.GetFloat() * fog.end : 8192.0f;
	TargetScan< CTerrorPlayer > scan( this, TEAM_SURVIVOR, 0.1f, range, 50.0f, false );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() != 0 && player->IsPlayer() &&
			player->IsConnected() && player->GetTeamNumber() == TEAM_SURVIVOR )
			scan( player );
	}
	if ( scan.GetTarget() )
		Q_strncpy( concept, "PlayerNiceJob", sizeof( concept ) );
	return concept;
}

// S338D90. Command-specific providers and order are deliberately not folded
// into a generic team-change, console-command or speech-event dispatcher.
bool CTerrorPlayer::ClientCommand( const CCommand &args )
{
	const char *command = args[0];
	if ( FStrEq( command, "activitylist" ) )
	{
		if ( sv_cheats->GetBool() )
		{
			CUtlVector< const char * > names;
			CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
			CStudioHdr *studio = GetModelPtr();
			if ( studio )
			{
				for ( int i = 0; i < studio->GetNumSeq(); ++i )
				{
					const char *name = GetSequenceActivityName( i );
					if ( name && name[0] && names.Find( name ) == names.InvalidIndex() )
						names.AddToTail( name );
				}
			}
			names.Sort( CommandActivityNameCompare );
			Msg( "=====================================\n" );
			const char *model = STRING( GetModelName() );
			Msg( "%s\n", model ? model : "" );
			FOR_EACH_VEC( names, i ) Msg( "%s\n", names[i] );
		}
		return true;
	}
	if ( sv_cheats->GetBool() )
	{
		if ( FStrEq( command, "francis" ) ) { TheDirector->CreateSurvivorBot( SurvivorCharacter_Biker ); return true; }
		if ( FStrEq( command, "louis" ) ) { TheDirector->CreateSurvivorBot( SurvivorCharacter_Manager ); return true; }
		if ( FStrEq( command, "zoey" ) ) { TheDirector->CreateSurvivorBot( SurvivorCharacter_TeenGirl ); return true; }
	}
	if ( FStrEq( command, "spectate" ) && TerrorGameRules()->IsPlayerControlledZombies() )
		return true;
	if ( FStrEq( command, "unstick" ) )
	{
		if ( sv_cheats->GetBool() ) UnstickAfterPin();
		return true;
	}
	if ( FStrEq( command, "red" ) )
	{
		const int alpha = _mm_cvttss_si32( _mm_set_ss( z_damage_screen_fade_alpha.GetFloat() ) );
		const color32 fade = { 255, 0, 0, static_cast< unsigned char >( alpha ) };
		UTIL_ScreenFade( this, fade,
			z_damage_screen_fade_duration.GetFloat(), 0.0f, FFADE_IN );
		return true;
	}
	if ( FStrEq( command, "dsp" ) )
	{
		if ( args.ArgC() == 2 )
		{
			CSingleUserRecipientFilter filter( this );
			enginesound->SetPlayerDSP( filter, atoi( args[1] ), false );
		}
		return true;
	}
	// S3390B8: native V_stristr returns NULL when "spec" occurs NOWHERE.
	// These are the commands forwarded to mimicking bots, not a spec-prefix gate.
	const int mimic = bot_mimic.GetInt();
	const uint32 mimicMagnitude = mimic < 0 ? 0u - static_cast< uint32 >( mimic ) : static_cast< uint32 >( mimic );
	if ( mimicMagnitude == static_cast< uint32 >( entindex() ) && !IsBot() &&
		sv_cheats->GetBool() && !Q_stristr( command, "spec" ) )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( player && player->IsBot() ) player->ClientCommand( args );
		}
		return true;
	}
	if ( FStrEq( command, "invnext" ) || FStrEq( command, "invprev" ) )
	{
		if ( !IsBot() ) return true;
		CBaseCombatWeapon *active = GetActiveWeapon();
		CBaseCombatWeapon *selected = NULL;
		int selectedSlot = 0;
		const bool next = FStrEq( command, "invnext" );
		for ( int i = 0; i < 56; ++i )
		{
			if ( i >= WeaponCount() ) continue; // Only the out-of-array read is omitted.
			CBaseCombatWeapon *weapon = GetWeapon( i );
			if ( !weapon || weapon == active || !weapon->HasAnyAmmo() || !active ) continue;
			const int slot = weapon->GetSlot();
			const int activeSlot = active->GetSlot();
			if ( selectedSlot != 0 || ( next ? slot >= activeSlot : slot <= activeSlot ) ) continue;
			selectedSlot = slot;
			selected = weapon;
		}
		if ( selected ) Weapon_Switch( selected, 0 );
		return true;
	}
	if ( FStrEq( command, "survival_record" ) )
	{
		uint64 record;
		uint32 checksum;
		if ( sscanf( args[1], "%llu", &record ) != 1 || sscanf( args[2], "%u", &checksum ) != 1 )
			return false;
		checksum ^= static_cast< uint32 >( record >> 56 );
		record &= 0x00ffffffffffffffull;
		player_info_t info;
		if ( !engine->GetPlayerInfo( entindex(), &info ) ) return false;
		const CSteamID *identity = engine->GetClientSteamID( edict() );
		if ( !identity ) return false; // No substitute identity for the dependent check.
		char buffer[256];
		const char *map = STRING( gpGlobals->mapname );
		if ( !map ) return false;
		Q_snprintf( buffer, sizeof( buffer ), "%s*%llu", map, identity->ConvertToUint64() );
		Q_strlower( buffer );
		if ( checksum != CRC32_ProcessSingleBuffer( buffer, Q_strlen( buffer ) ) ) return false;
		const float time = static_cast< float >( static_cast< double >( static_cast< uint32 >( record ) ) * static_cast< double >( 0.01f ) );
		UpdateSurvivalRecord( time );
		// Native success falls through to the remaining command/base dispatcher.
	}
	if ( FStrEq( command, "jointeam" ) )
	{
		if ( args.ArgC() < 2 ) Warning( "Player sent bad jointeam syntax\n" );
		if ( !ShouldRunRateLimitedCommand( args ) ) return true;
		m_flLastActivityTime = gpGlobals->curtime;
		if ( GetTeamSwitchRule() != 0 && !sv_cheats->GetBool() ) return true;
		int requestedTeam = atoi( args[1] );
		if ( requestedTeam == 0 )
		{
			if ( FStrEq( args[1], "Survivor" ) ) requestedTeam = TEAM_SURVIVOR;
			else if ( FStrEq( args[1], "Infected" ) ) requestedTeam = TEAM_INFECTED;
			else requestedTeam = FStrEq( args[1], "Spectator" ) ? TEAM_SPECTATOR : TEAM_UNASSIGNED;
		}
		static ConVarRef developer( "developer" );
		if ( ( developer.GetInt() == 0 && !sv_cheats->GetBool() &&
			requestedTeam != TEAM_SURVIVOR && requestedTeam != TEAM_INFECTED ) ||
			requestedTeam == GetTeamNumber() ) return true;
		const int oldTeam = GetTeamNumber();
		if ( requestedTeam == TEAM_SURVIVOR )
		{
			CTerrorPlayer *candidate = NULL;
			if ( args.ArgC() <= 2 )
			{
				for ( int i = 1; i <= gpGlobals->maxClients; ++i )
				{
					CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
					if ( !player || !player->edict() || player->entindex() == 0 ||
						!player->IsPlayer() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
					SurvivorBot *bot = dynamic_cast< SurvivorBot * >( player );
					if ( bot && bot->GetHumanSpectatorUserID() <= 0 && bot->IsAlive() ) { candidate = bot; break; }
				}
			}
			else
			{
				const int character = GetTerrorSurvivorCharacterForName( args[2] );
				if ( character != SurvivorCharacter_Unknown )
				{
					candidate = FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( character ) );
					if ( candidate && !candidate->IsBot() ) candidate = NULL;
				}
			}
			SurvivorBot *bot = dynamic_cast< SurvivorBot * >( candidate );
			if ( bot )
			{
				CTerrorPlayer *associated = GetSpectatedBot();
				SurvivorBot *survivor = dynamic_cast< SurvivorBot * >( associated );
				if ( survivor ) survivor->ClearHumanSpectator();
				BossZombiePlayerBot *infected = dynamic_cast< BossZombiePlayerBot * >( associated );
				if ( infected ) infected->ClearHumanSpectator();
				ChangeTeam( TEAM_SPECTATOR );
				bot->SetHumanSpectator( this );
				SetObserverTarget( bot );
				if ( GetObserverMode() == OBS_MODE_ROAMING ) SetObserverMode( OBS_MODE_CHASE );
				ShowViewPortPanel( "specgui", false, NULL );
				if ( !bot->IsAlive() || TerrorGameRules()->IsPlayerControlledZombies() )
					TakeOverBot( false );
				else
				{
					KeyValues *data = new KeyValues( "data" );
					data->SetInt( "character", bot->GetSurvivorCharacter() );
					ShowViewPortPanel( "takeover_survivor_bar", true, data );
					data->deleteThis();
				}
				if ( GetTeamNumber() != oldTeam )
					m_teamChangeCount = static_cast< int32 >( static_cast< uint32 >( m_teamChangeCount ) + 1u ); // S3396EF
				return true;
			}
		}
		else
		{
			HandleCommand_JoinTeam( requestedTeam, SurvivorCharacter_Unknown, false );
			if ( GetTeamNumber() != oldTeam )
			{
				ShowViewPortPanel( "team", false, NULL );
				m_teamChangeCount = static_cast< int32 >( static_cast< uint32 >( m_teamChangeCount ) + 1u ); // S339732
				return true;
			}
			if ( GetTeamNumber() == requestedTeam ) return true;
		}
		ShowViewPortPanel( "team", true, NULL );
		return true;
	}
	if ( FStrEq( command, "spec_next" ) && TakeOverBot( false ) )
	{
		ShowViewPortPanel( "takeover_survivor_bar", false, NULL );
		return true;
	}
	if ( FStrEq( command, "dance" ) )
	{
		if ( sv_cheats->GetBool() && GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 && IsAlive() )
		{
			CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
			DoTerrorAnimationEvent( TERROR_ANIMEVENT_TANK_SPECIAL, 2 );
		}
		return true;
	}
	if ( FStrEq( command, "stagger_forward" ) )
	{
		if ( !sv_cheats->GetBool() ) return true;
		if ( IsAlive() && GetTeamNumber() == TEAM_INFECTED )
		{
			Vector forward;
			GetVectors( &forward, NULL, NULL );
			MeleeRepel( NULL, forward * 300.0f );
			return true;
		}
		if ( !IsAlive() || GetTeamNumber() != TEAM_SURVIVOR ) return true;
		Vector forward;
		GetVectors( &forward, NULL, NULL );
		forward *= 300.0f; // The survivor branch computes this unused vector natively.
		StaggerPlayer( NULL, NULL );
		return true;
	}
	if ( FStrEq( command, "give_tank" ) )
	{
		if ( sv_cheats->GetBool() )
		{
			RemoveAllItems( true );
			GiveNamedItemInternal( "weapon_tank_claw", 0, true, NULL );
			if ( m_customAbility.Get() ) UTIL_Remove( m_customAbility.Get() );
			CBaseHandle &abilityHandle = m_customAbility.GetForModify();
			const QAngle angles = GetAbsAngles();
			const Vector position = EyePosition();
			abilityHandle.Set( static_cast< CBaseAbility * >( CBaseEntity::Create( "ability_throw", position, angles, this ) ) );
			if ( m_customAbility.Get() ) m_customAbility->SetOwner( this );
		}
		return true;
	}
	if ( FStrEq( command, "give_survivor" ) )
	{
		if ( sv_cheats->GetBool() )
		{
			RemoveAllItems( true );
			GiveNamedItemInternal( "weapon_pistol", 0, true, NULL );
			if ( m_customAbility.Get() ) UTIL_Remove( m_customAbility.Get() );
		}
		return true;
	}
	if ( FStrEq( command, "smoke" ) )
	{
		if ( sv_cheats->GetBool() )
		{
			CDisablePredictionFiltering filtering;
			const QAngle angles = GetAbsAngles();
			const Vector origin = GetAbsOrigin();
			DispatchParticleEffect( "smoker_smokecloud", origin, angles, this );
			Vector cameraOrigin = GetAbsOrigin();
			cameraOrigin.z += 24.0f;
			const Vector cameraStart = GetAbsOrigin();
			DispatchParticleEffect( "smoker_smokecloud_camera", cameraStart, cameraOrigin, vec3_angle );
		}
		return true;
	}
	if ( FStrEq( command, "fullpull" ) )
	{
		if ( !sv_cheats->GetBool() ) return true;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( player && player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 1 &&
				player->IsAlive() && player != this )
			{
				player->StaggerPlayer( NULL, NULL );
				return true;
			}
		}
		StaggerPlayer( NULL, NULL );
		return true;
	}
	if ( FStrEq( command, "damage_me" ) )
	{
		if ( IsAlive() && sv_cheats->GetBool() )
			TakeDamage( CTakeDamageInfo( this, this, static_cast< float >( GetMaxHealth() / 4 ), DMG_NEVERGIB ) );
		return true;
	}
	if ( FStrEq( command, "limp" ) )
	{
		if ( IsAlive() && GetTeamNumber() == TEAM_SURVIVOR && sv_cheats->GetBool() )
		{
			if ( IsIncapacitated() ) OnRevived();
			const float buffer = args.ArgC() == 2 ? static_cast< float >( atof( args[1] ) ) : 50.0f;
			SetHealthBuffer( buffer );
			TakeDamage( CTakeDamageInfo( this, this, static_cast< float >( GetHealth() ), DMG_NEVERGIB ) );
		}
		return true;
	}
	if ( FStrEq( command, "fumes" ) )
	{
		if ( IsAlive() && GetTeamNumber() == TEAM_SURVIVOR && sv_cheats->GetBool() )
		{
			if ( IsIncapacitated() ) OnRevived();
			SetHealthBuffer( 1.0f );
			TakeDamage( CTakeDamageInfo( this, this, static_cast< float >( GetHealth() ), DMG_NEVERGIB ) );
		}
		return true;
	}
	if ( FStrEq( command, "incap" ) )
	{
		if ( IsAlive() && sv_cheats->GetBool() )
		{
			if ( GetMoveType() == MOVETYPE_NOCLIP ) { RemoveEFlags( EFL_NOCLIP_ACTIVE ); SetMoveType( MOVETYPE_WALK ); }
			SetHealthBuffer( 0.0f );
			TakeDamage( CTakeDamageInfo( this, this, static_cast< float >( GetHealth() ), DMG_NEVERGIB ) );
		}
		return true;
	}
	if ( FStrEq( command, "revive" ) )
	{
		if ( sv_cheats->GetBool() )
		{
			if ( IsAlive() && GetTeamNumber() == TEAM_SURVIVOR && IsIncapacitated() )
			{
				if ( !IsHangingFromLedge() && !m_isHangingFromTongue ) BeginReviving( this );
			}
			else
			{
				CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
				if ( team )
				{
					for ( int i = 0; i < team->GetNumPlayers(); ++i )
					{
						CTerrorPlayer *player = ToTerrorPlayer( team->GetPlayer( i ) );
						if ( player && player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR &&
							player->IsIncapacitated() && !player->IsHangingFromLedge() && !player->m_isHangingFromTongue )
							player->BeginReviving( player );
					}
				}
			}
		}
		return true;
	}
	if ( FStrEq( command, "it" ) )
	{
		if ( IsAlive() && GetTeamNumber() == TEAM_SURVIVOR && sv_cheats->GetBool() ) OnVomitedUpon( NULL, false );
		return true;
	}
	if ( FStrEq( command, "it_fp" ) )
	{
		if ( IsAlive() && GetTeamNumber() == TEAM_SURVIVOR && sv_cheats->GetBool() &&
			m_vomitStart.Get() != gpGlobals->curtime ) m_vomitStart.GetForModify() = gpGlobals->curtime;
		return true;
	}
	if ( FStrEq( command, "saliva" ) )
	{
		if ( IsAlive() && GetTeamNumber() == TEAM_SURVIVOR && sv_cheats->GetBool() &&
			m_salivaStart.Get() != gpGlobals->curtime ) m_salivaStart.GetForModify() = gpGlobals->curtime;
		return true;
	}
	if ( FStrEq( command, "bashed" ) )
	{
		if ( IsAlive() && GetTeamNumber() == TEAM_SURVIVOR && sv_cheats->GetBool() &&
			m_bashedStart.Get() != gpGlobals->curtime ) m_bashedStart.GetForModify() = gpGlobals->curtime;
		return true;
	}
	if ( FStrEq( command, "rescue" ) )
	{
		if ( !IsAlive() && sv_cheats->GetBool() && GetTeamNumber() == TEAM_SURVIVOR )
		{
			CSurvivorRescue *selected = NULL;
			float selectedFlow = 0.0f;
			CTerrorPlayer *leader = FindHighestFlowSurvivor( 0 );
			const float highestFlow = leader ? static_cast< float >( leader->GetFlowDistance( 0 ) ) : -9999.0f;
			if ( highestFlow > 0.0f )
			{
				for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "info_survivor_rescue" ); entity;
					entity = gEntList.FindEntityByClassname( entity, "info_survivor_rescue" ) )
				{
					CSurvivorRescue *rescue = static_cast< CSurvivorRescue * >( entity );
					if ( !rescue->IsCallingForRescue() )
					{
						const float flow = rescue->GetFlowDistance();
						if ( flow <= highestFlow + 50.0f && selectedFlow <= flow ) { selectedFlow = flow; selected = rescue; }
					}
				}
			}
			if ( !selected )
			{
				for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "info_survivor_rescue" ); entity;
					entity = gEntList.FindEntityByClassname( entity, "info_survivor_rescue" ) )
				{
					CSurvivorRescue *rescue = static_cast< CSurvivorRescue * >( entity );
					if ( !rescue->IsCallingForRescue() ) { selected = rescue; break; }
				}
			}
			if ( selected )
			{
				selected->SetRescueTarget( this );
				inputdata_t input;
				input.pActivator = this;
				input.pCaller = NULL;
				input.nOutputID = 0;
				selected->InputRescue( input );
			}
		}
		return true;
	}
	if ( FStrEq( command, "respawn" ) )
	{
		if ( sv_cheats->GetBool() && !IsAlive() && GetTeamNumber() == TEAM_SURVIVOR )
		{
			State_Transition( STATE_ACTIVE );
			Spawn();
			TakeHealth( static_cast< float >( GetMaxHealth() ), 0 );
		}
		return true;
	}
	if ( FStrEq( command, "suppress" ) )
	{
		if ( sv_cheats->GetBool() && GetActiveWeapon() )
		{
			CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( GetActiveWeapon() );
			if ( weapon ) weapon->SuppressHelpingHand( 0.0f );
		}
		return true;
	}
	if ( FStrEq( command, "respawn_all" ) )
	{
		if ( sv_cheats->GetBool() )
		{
			CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
			if ( team )
			{
				for ( int i = 0; i < team->GetNumPlayers(); ++i )
				{
					CTerrorPlayer *player = ToTerrorPlayer( team->GetPlayer( i ) );
					if ( player && !player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR )
					{
						player->State_Transition( STATE_ACTIVE );
						player->Spawn();
						player->TakeHealth( static_cast< float >( player->GetMaxHealth() ), 0 );
					}
				}
			}
		}
		return true;
	}
	if ( FStrEq( command, "materialize" ) )
	{
		if ( sv_cheats->GetBool() && GetTeamNumber() == TEAM_INFECTED && IsGhost() ) MaterializeFromGhost();
		return true;
	}
	if ( FStrEq( command, "ghost" ) )
	{
		if ( sv_cheats->GetBool() && GetTeamNumber() == TEAM_INFECTED && !IsAlive() )
		{
			int zombieClass = 3;
			if ( args.ArgC() == 2 )
			{
				zombieClass = ZombieClassFromName( args[1] );
				if ( zombieClass != 3 && zombieClass != 1 && zombieClass != 2 ) zombieClass = 3;
			}
			m_zombieClass = zombieClass;
			State_Transition( STATE_GHOST );
			if ( m_ghostSpawnClockMaxDelay <= 0 )
			{
				m_ghostSpawnClockMaxDelay = 1;
				m_ghostSpawnClockCurrentDelay = m_ghostSpawnClockMaxDelay;
			}
		}
		return true;
	}
	if ( FStrEq( command, "scrimmage" ) )
	{
		if ( sv_cheats->GetBool() && args.ArgC() >= 2 )
		{
			const float wantedFront = static_cast< float >( atof( args[1] ) );
			const float wantedRear = args.ArgC() > 2 ? static_cast< float >( atof( args[2] ) ) : wantedFront;
			Vector frontPosition, rearPosition;
			QAngle frontAngles, rearAngles;
			const float front = static_cast< float >( FindScrimmagePointAtFlow( wantedFront, false, &frontPosition, &frontAngles ) );
			const float rear = static_cast< float >( FindScrimmagePointAtFlow( wantedRear, false, &rearPosition, &rearAngles ) );
			if ( front <= 0.0f || rear <= 0.0f )
			{
				Msg( "%s ignoring line of scrimmage - can't step forward from start %f units\n", GetPlayerName(), static_cast< double >( wantedFront ) );
				m_flLineOfScrimmageFlow = 0.0f;
				m_flRearLineOfScrimmageFlow = 0.0f;
				if ( m_rearLineOfScrimmagePos.Get() != vec3_origin ) m_rearLineOfScrimmagePos.GetForModify() = vec3_origin;
				if ( m_rearLineOfScrimmageAngle.Get() != vec3_angle ) m_rearLineOfScrimmageAngle.GetForModify() = vec3_angle;
				m_scrimmageType = 0;
			}
			else
			{
				if ( m_lineOfScrimmagePos.Get() != frontPosition ) m_lineOfScrimmagePos.GetForModify() = frontPosition;
				if ( m_lineOfScrimmageAngle.Get() != frontAngles ) m_lineOfScrimmageAngle.GetForModify() = frontAngles;
				m_flLineOfScrimmageFlow = front;
				if ( m_rearLineOfScrimmagePos.Get() != rearPosition ) m_rearLineOfScrimmagePos.GetForModify() = rearPosition;
				if ( m_rearLineOfScrimmageAngle.Get() != rearAngles ) m_rearLineOfScrimmageAngle.GetForModify() = rearAngles;
				m_flRearLineOfScrimmageFlow = rear;
				Msg( "%s has a line of scrimmage of %f/%f flow (wanted %f/%f flow)\n", GetPlayerName(),
					static_cast< double >( front ), static_cast< double >( rear ), static_cast< double >( wantedFront ), static_cast< double >( wantedRear ) );
				m_scrimmageType = 1;
				UpdateScrimmageYardLines( 0.0f );
			}
		}
		return true;
	}
	if ( FStrEq( command, "boom" ) )
	{
		if ( sv_cheats->GetBool() )
		{
			const Vector velocity = GetAbsVelocity();
			const QAngle angles = GetAbsAngles();
			const Vector position = WorldSpaceCenter();
			CPipeBombProjectile::Create( position, angles, velocity, vec3_origin, this );
		}
		return true;
	}
	if ( FStrEq( command, "fire" ) )
	{
		if ( sv_cheats->GetBool() )
		{
			Vector end = GetAbsOrigin();
			end.z -= 999999.88f;
			Vector start = GetAbsOrigin();
			start.z += 10.0f;
			trace_t trace;
			UTIL_TraceLine( start, end, 33636363, this, COLLISION_GROUP_NONE, &trace );
			if ( !( trace.surface.flags & SURF_SKY ) )
			{
				CInferno *inferno = static_cast< CInferno * >( CBaseEntity::Create( "inferno", trace.endpos, vec3_angle, NULL ) );
				if ( inferno ) inferno->StartBurning( trace.endpos, trace.plane.normal, vec3_origin, 0 );
			}
		}
		return true;
	}
	if ( FStrEq( command, "burn" ) )
	{
		if ( IsAlive() && sv_cheats->GetBool() ) Ignite( 9999.0f, false, 0.0f, false );
		return true;
	}
	if ( FStrEq( command, "ent_count" ) )
	{
		if ( sv_cheats->GetBool() && ShouldRunRateLimitedCommand( args ) )
		{
			CUtlMap< const char *, int > counts( EntityCountClassLess );
			int total = 0;
			for ( CBaseEntity *entity = gEntList.NextEnt( NULL ); entity; entity = gEntList.NextEnt( entity ) )
			{
				const char *name = entity->GetClassname();
				if ( !name ) name = "";
				const unsigned short index = counts.Find( name );
				if ( index == counts.InvalidIndex() ) counts.Insert( name, 1 );
				else ++counts[index];
				++total;
			}
			// Native census completes BEFORE its one reliable ZERO-BODY clear.
			TerrorClearMessageText( this );
			const unsigned int rgb = 0;
			TerrorMessageText( this, UTIL_VarArgs( "--- %d ents ---\n", total ), rgb );
			// Native iterates physical node indices0..Count-1, NOT sorted tree order.
			// No removals occur, so every one of these freshly inserted slots is live.
			for ( unsigned short i = 0; i < counts.Count(); ++i )
				TerrorMessageText( this, UTIL_VarArgs( "%d %s\n", counts[i], counts.Key( i ) ), rgb );
		}
		return true;
	}
	if ( FStrEq( command, "taunt" ) )
	{
		if ( m_flNextTauntTime <= gpGlobals->curtime && IsAlive() && !IsIncapacitated() && !IsGhost() &&
			( GetTeamNumber() != TEAM_INFECTED || GetZombieClass() == 3 ) )
		{
			SpeakIfAllowed( "PlayerTaunt", 2 );
			m_flNextTauntTime = tauntfrequency.GetFloat() + gpGlobals->curtime;
		}
		return true;
	}
	if ( FStrEq( command, "choose_revive" ) )
	{
		if ( sv_cheats->GetBool() && args.ArgC() == 2 )
		{
			CTerrorPlayer *target = ToTerrorPlayer( UTIL_PlayerByUserId( atoi( args[1] ) ) );
			if ( target ) BeginReviving( target );
		}
		return true;
	}
	if ( FStrEq( command, "choose_drag" ) )
	{
		if ( sv_cheats->GetBool() && args.ArgC() == 2 )
		{
			CTerrorPlayer *target = ToTerrorPlayer( UTIL_PlayerByUserId( atoi( args[1] ) ) );
			if ( target ) BeginDragging( target );
		}
		return true;
	}
	if ( FStrEq( command, "choose_opendoor" ) || FStrEq( command, "choose_closedoor" ) ||
		FStrEq( command, "choose_lockdoor" ) || FStrEq( command, "choose_unlockdoor" ) )
	{
		CBaseEntity *selected = FindUseEntity( player_use_radius.GetFloat(), 0.0f, 0.0f, NULL, false );
		if ( selected )
		{
			CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( selected );
			if ( !door ) door = dynamic_cast< CBasePropDoor * >( selected->GetMoveParent() );
			if ( door )
			{
				variant_t empty;
				const char *input = FStrEq( command, "choose_opendoor" ) ? "PlayerOpen" :
					FStrEq( command, "choose_closedoor" ) ? "PlayerClose" :
					FStrEq( command, "choose_lockdoor" ) ? "PlayerLock" : "PlayerUnlock";
				door->AcceptInput( input, this, this, empty, 16 );
			}
		}
		return true;
	}
	if ( FStrEq( command, "z_abort" ) ) { HandleInfectedUse(); return true; }
	if ( FStrEq( command, "vocalize" ) )
	{
		if ( IsAlive() && args.ArgC() >= 2 && ShouldRunRateLimitedCommand( args ) )
		{
			const char *concept = args[1];
			char modifiers[256];
			modifiers[0] = '\0';
			if ( !IsIncapacitated() || m_vocalizeTimer.IsElapsed() || FStrEq( concept, "PlayerHelp" ) )
			{
				if ( FStrEq( concept, "smartlook" ) )
				{
					const float ready = m_lastSmartLookTime + 2.0f;
					if ( ready > gpGlobals->curtime ) return true;
					if ( args.ArgC() <= 2 || args[2][0] == '#' )
						Q_strncat( modifiers, "smartlooktype:manual", sizeof( modifiers ) );
					else
					{
						if ( IsIncapacitated() ) return true;
						Q_strncat( modifiers, "smartlooktype:auto", sizeof( modifiers ) );
					}
					concept = GetSmartLookConcept( NULL, modifiers, sizeof( modifiers ) );
					m_lastSmartLookTime = gpGlobals->curtime;
					SpeakIfAllowed( concept, 2, modifiers );
				}
				else
				{
					if ( FStrEq( concept, "PlayerHurrah" ) ) concept = GetHurrahConcept();
					if ( args.ArgC() > 2 && args[2][0] == '#' && Q_strlen( args[2] ) > 1 &&
						fabs( static_cast< double >( gpGlobals->curtime ) - atof( args[2] + 1 ) * 0.1 ) <= 2.0 )
						SpeakIfAllowed( concept, 2, modifiers );
				}
			}
		}
		return true;
	}
	if ( FStrEq( command, "motd" ) )
	{
		if ( ShouldRunRateLimitedCommand( args ) ) ShowMotD();
		return true;
	}
	if ( FStrEq( command, "SkipOuttro" ) )
	{
		DevMsg( "Player %d is skipping the outtro\n", entindex() );
		if ( !m_bHasSkippedOuttro ) { m_bHasSkippedOuttro = true; NotifyCommandOuttroStateChanged(); }
		return true;
	}
	if ( ( FStrEq( command, "PZEndGameYes" ) || FStrEq( command, "PZEndGameNo" ) ) &&
		TerrorGameRules()->IsPlayerControlledZombies() )
	{
		if ( m_nRematchVote == 0 )
		{
			m_nRematchVote = 2 - FStrEq( command, "PZEndGameYes" );
			Msg( "Player %d has voted %s on the rematch.\n", entindex(), m_nRematchVote == 1 ? "Yes" : "No" );
			CSingleUserRecipientFilter filter( this );
			filter.MakeReliable();
			UserMessageBegin( filter, "PZEndGamePanelVoteRegisteredMsg" );
				WRITE_BYTE( m_nRematchVote == 1 );
			MessageEnd();
			NotifyCommandOuttroStateChanged();
		}
		return true;
	}
	if ( FStrEq( command, "host_validate_session" ) )
	{
		// Main-approved target capability boundary: native engine virtual528
		// has no IVEngineServer023 counterpart. Omit ONLY that call.
		return true;
	}
	return BaseClass::ClientCommand( args );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

void CTerrorPlayer::ProcessUsercmds( CUserCmd *pCommands, int nNumCommands, int nTotalCommands, int nDroppedPackets, bool bPaused )
{
	// Target adaptation: SDK engine 0x65B00 has no original FULL-ack event
	// (L4D2 engine 0x4DA50). Receive the first real command batch instead.
	// gameinterface.cpp calls this virtual only after decoding and !ignore;
	// RunNullCommand/PlayerRunCommand cannot generate this notification.
	if ( !m_bFullConnectNotified && !IsBot() && nNumCommands > 0 && pCommands[0].command_number > 0 )
	{
		m_bFullConnectNotified = true;
		TheDirector->GetSessionManager()->OnFullConnect( this );
	}
	BaseClass::ProcessUsercmds( pCommands, nNumCommands, nTotalCommands, nDroppedPackets, bPaused );
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
void CTerrorPlayer::PlayerRunCommand( CUserCmd *pCommand, IMoveHelper *pMoveHelper )
{
	// 0x319F50 clears the entire angle vector if any component is nonfinite,
	// but sanitizes the three movement components individually.
	if ( !IsFinite( pCommand->viewangles.x ) || !IsFinite( pCommand->viewangles.y ) || !IsFinite( pCommand->viewangles.z ) )
		pCommand->viewangles.Init();
	if ( !IsFinite( pCommand->forwardmove ) )
		pCommand->forwardmove = 0.0f;
	if ( !IsFinite( pCommand->sidemove ) )
		pCommand->sidemove = 0.0f;
	if ( !IsFinite( pCommand->upmove ) )
		pCommand->upmove = 0.0f;
	// S319F50: after native command/incap restrictions, before qualified CCS.
	if ( m_usingMountedWeapon && ( pCommand->buttons & 0xE3A ) )
	{
		const float timestamp = m_useEntityChangedTimer.GetStartTime();
		if ( timestamp <= 0.0f ||
			static_cast< float >( static_cast< double >( gpGlobals->curtime ) - timestamp ) > 1.0f )
			ClearUseEntity();
	}
	BaseClass::PlayerRunCommand( pCommand, pMoveHelper );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

void CTerrorPlayer::CreateViewModel( int iViewModel )
{
	Assert( iViewModel >= 0 && iViewModel < MAX_VIEWMODELS );
	// The original lookup (0x16E710) reads this player's own handle only.
	if ( GetViewModel( iViewModel, false ) )
		return;
	CBaseViewModel *pViewModel = static_cast< CBaseViewModel * >( CreateEntityByName( "predicted_viewmodel" ) );
	if ( pViewModel )
	{
		pViewModel->SetAbsOrigin( GetAbsOrigin() );
		pViewModel->SetOwner( this );
		pViewModel->SetIndex( iViewModel );
		DispatchSpawn( pViewModel );
		pViewModel->FollowEntity( this );
		m_hViewModel.Set( iViewModel, pViewModel );
	}
}



// S34C280. These are increments per scheduled alive update, not rates per second.
void CTerrorPlayer::UpdateBurnEffects()
{
	if ( !( GetFlags() & FL_ONFIRE ) )
		return;
	if ( GetWaterLevel() != 0 )
	{
		Extinguish();
		m_burnPercent = 0.0f;
		return;
	}

	const float burn = z_burn_rate.GetFloat() + m_burnPercent;
	const float burnMax = z_burn_max.GetFloat();
	m_burnPercent = burn < burnMax ? burn : burnMax;

	const float bbq = m_bbqPercent - z_bbq_rate.GetFloat();
	const float bbqMin = z_bbq_min.GetFloat();
	m_bbqPercent = bbq > bbqMin ? bbq : bbqMin;
}

// S338740. The two alive tests, cached entry team/area, and repeated
// weapon-visibility calls are distinct native phases.
void CTerrorPlayer::PostThink()
{
	VPROF_BUDGET( "CTerrorPlayer::PostThink", "ZombieExpensive" );
	const int entryTeam = GetTeamNumber();
	const int entryCharacter = GetSurvivorCharacter();
	if ( m_pendingAASArea && IsAlive() )
	{
		const int team = GetTeamNumber();
		if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
			gpGlobals->tickcount % 4 == entryCharacter % 4 )
		{
			UpdatePotentiallyVisibleAreas( m_pendingAASArea, m_previousAASArea );
			m_previousAASArea = NULL;
			m_pendingAASArea = NULL;
		}
	}

	UpdatePummel();
	UpdateCarry();
	UpdateJockeyRide();
	NetworkStateChanged();
	if ( !IsCommonPostThinkDue() )
	{
		CCSPlayer::PostThink();
		return;
	}

	m_holdingObject = IsHoldingEntity( NULL );
	UpdateDelayedDSP();
	UpdateSpitterBurnMusic();
	UpdateInfectedVocalizations();
	DecayLineOfScrimmage();
	UpdateStagger();
	UpdatePounce();
	UpdateDragging();
	UpdateBeingDragged();
	Terror_UpdatePlayerEnvironment( this );
	NotifyCommonHearing();
	UpdateWeaponVisibility();
	UpdateCommonDamageDPS();

	if ( m_bAdrenalineActive && static_cast< double >( gpGlobals->curtime ) >=
		static_cast< double >( m_adrenalineTimer.m_timestamp.Get() ) )
		ExpireAdrenaline();
	if ( m_isHangingFromLedge && m_ledgeGroundCheckTimer.m_timestamp.Get() > 0.0f &&
		static_cast< double >( gpGlobals->curtime ) >=
		static_cast< double >( m_ledgeGroundCheckTimer.m_timestamp.Get() ) )
	{
		ReleaseLedge();
		RestartLedgeGroundCheckTimer();
	}
	if ( m_defibrillatorReturnToLifeTimer.m_timestamp.Get() > 0.0f )
		UpdateDefibrillatorReturnToLife();

	// S3389E1/S3389E9: MULSS then CVTTSS2SI, including integer-indefinite.
	m_clientIntensity = _mm_cvttss_si32( _mm_mul_ss(
		_mm_set_ss( m_survivorIntensity.GetCurrent() ), _mm_set_ss( 100.0f ) ) );
	CNavArea *knownArea = GetLastKnownArea();
	if ( IsAlive() )
	{
		FlushTerrorSplatter();
		UpdateBurnEffects();
		if ( nav_show_light_intensity.GetInt() == entindex() && !engine->IsDedicatedServer() )
		{
			CNavArea *area = GetLastKnownArea();
			if ( area )
			{
				const double intensity = area->GetLightIntensity( GetAbsOrigin() );
				// S338ABA dispatches S079690, the actual player-name buffer.
				engine->Con_NPrintf( 20, "%s: nav intensity: %f", GetPlayerName(), intensity );
			}
		}
		m_lastAliveTime = gpGlobals->curtime;
		if ( m_isInMissionStartArea &&
			( !knownArea || !ToTerrorNavArea( knownArea )->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) &&
			m_isInMissionStartArea )
			m_isInMissionStartArea = false;
		UpdateWeaponVisibility();
		UpdateFriendlyFireResponse();
		if ( static_cast< double >( gpGlobals->curtime ) >=
			static_cast< double >( m_vomitDamageSourceTimer.m_timestamp.Get() ) &&
			m_hVomitDamageSource.Get() )
			m_hVomitDamageSource = NULL;
		UpdateStaggerSource();
		UpdateFallingFromLedge();
		UpdateTongueVictim();
		UpdateReviving();
		UpdateCulling();
	}

	if ( IsAlive() )
	{
		m_isCalm = ShouldBeCalm();
		UpdateSurvivorSpeech();
		CheckEnemySightings();
		UpdateFriendSeenTimers();
		UpdateSneaking();
		const int team = GetTeamNumber();
		if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
		{
			m_survivorIntensity.Update();
			UpdateCoughAndExertion();
			UpdateIdleSpeech();
			DrawSurvivorNavDebug();
		}
		else if ( entryTeam == TEAM_INFECTED )
		{
			const float now = gpGlobals->curtime;
			const int zombieClass = GetZombieClass();
			m_zombieClassDeathTimestamp[zombieClass] = now;
			m_bBoomerFalling = UpdateBoomerFalling();
			if ( IsIncapacitated() )
			{
				UpdateIncapacitatedTank();
				m_frustration = 0;
			}
			else
				UpdateTankFrustration();
			if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
				UpdateTankBump();
			DrawInfectedDebug();
		}
		if ( knownArea && ToTerrorNavArea( knownArea )->IsInPlayerAAS( SurvivorCharacter_Unknown ) )
			m_occupiedNavAreaInterval.Start();
	}
	UpdateNoise();
	UpdateITEvents();
	UpdateHealingGnome();
	if ( entryTeam == TEAM_SPECTATOR && m_wantsToPlay )
		UpdateAutoJoinTeam();
	m_music.Update();
	CCSPlayer::PostThink();
	UpdateAirborneState();
}

// S311A40. Slot queries and network writes retain native order and stale-array quirks.
void CTerrorPlayer::UpdateAddonBits()
{
	const int team = GetTeamNumber();
	if ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR )
	{
		m_iAddonBits = 0;
		return;
	}

	CBaseCombatWeapon *active = GetActiveWeapon();
	const int activeSlot = active ? active->GetSlot() : -1;
	if ( ( GetTerrorAnimState()->IsGrenadeProjectilePresentationActive() && activeSlot != 3 ) ||
		( active && !active->IsWeaponVisible() ) )
		active = NULL;

	uint32 addonBits = 0;
	uint32 slotBit = 1;
	for ( int slot = 0; slot < 5; ++slot, slotBit <<= 1 )
	{
		CBaseCombatWeapon *weapon = Weapon_GetSlot( slot );
		if ( !weapon || weapon == active )
		{
			m_AddonModelWeaponIDs.Set( slot, 0 );
			continue;
		}

		const int weaponID = static_cast< CWeaponCSBase * >( weapon )->GetWeaponID();
		if ( weaponID == TERROR_WEAPON_MELEE )
		{
			CTerrorMeleeWeapon *melee = dynamic_cast< CTerrorMeleeWeapon * >( weapon );
			if ( melee )
				m_AddonModelWeaponIDs.Set( slot, melee->GetMeleeWeaponInfoHandle() | 0x40 );
			// A failed native RTTI cast leaves the old ID but STILL sets the slot bit.
		}
		else
			m_AddonModelWeaponIDs.Set( slot, weaponID );
		addonBits |= slotBit;
	}

	if ( m_tongueOwner.Get() )
		addonBits |= 0x20;
	else if ( IsAlive() && GetTeamNumber() == TEAM_SURVIVOR &&
		TheDirector->GetDirectorOptionInt( "GasCansOnBacks", 0 ) != 0 )
	{
		addonBits = ( addonBits & ~0x08u ) | 0x80u;
		m_AddonModelWeaponIDs.Set( 3, 0 );
	}

	if ( active && static_cast< CWeaponCSBase * >( active )->GetWeaponID() == TERROR_WEAPON_DEFIBRILLATOR )
		addonBits |= 0x40;
	m_iAddonBits = static_cast< int >( addonBits );
}

static ConVar nav_show_player_area( "nav_show_player_area", "0", 0x00004004,
	"Draws the last known area for the given entindex" );
ConVar survivor_debug_active_area_set( "survivor_debug_active_area_set", "0", 0x00004004 );
static ConVar survivor_debug_visibility( "survivor_debug_visibility", "0", 0x00004004 );
static ConVar z_show_escape_route( "z_show_escape_route", "0", 0x00004004 );
static ConVarRef z_debug( "z_debug" );
extern ConVar z_tank_incapacitated_decay_rate;
extern ConVar z_tank_incapacitated_health;
static ConVar l4d_show_incapupdate( "l4d_show_incapupdate", "0", 0x00000006 );
static ConVarRef tank_swing_miss_interval( "tank_swing_miss_interval" );
static ConVar sv_healing_gnome_replenish_rate( "sv_healing_gnome_replenish_rate", "1.5", FCVAR_CHEAT | FCVAR_GAMEDLL );

// S346D80. The native routine IGNORES the requested character argument.
// In debug modes 4/visibility, every pass consequently uses THIS player's color.
void CTerrorPlayer::GetNavDebugColor( SurvivorCharacterType requestedCharacter, int *red, int *green, int *blue ) const
{
	switch ( GetSurvivorCharacter() )
	{
	case 0: *red = 0;   *green = 255; *blue = 0;   break;
	case 1: *red = 255; *green = 0;   *blue = 0;   break;
	case 2: *red = 0;   *green = 0;   *blue = 255; break;
	case 3: *red = 150; *green = 150; *blue = 0;   break;
	case 4: *red = 255; *green = 255; *blue = 0;   break;
	case 5: *red = 255; *green = 0;   *blue = 255; break;
	case 6: *red = 0;   *green = 150; *blue = 150; break;
	case 7: *red = 150; *green = 0;   *blue = 150; break;
	default: *red = 255; *green = 0; *blue = 255; break;
	}
}

// S34ACD0. Scheduled, second-IsAlive, team2/4 branch, AFTER UpdateIdleSpeech.
void CTerrorPlayer::DrawSurvivorNavDebug()
{
	if ( nav_show_player_area.GetInt() == entindex() )
	{
		CNavArea *area = GetLastKnownArea();
		if ( area ) area->DrawFilled( 0, 0, 255, 50, 0.1f, true );
		if ( m_isHangingFromLedge && m_hangStandArea )
			m_hangStandArea->DrawFilled( 0, 255, 0, 50, 0.1f, true );
	}

	const int mode = survivor_debug_active_area_set.GetInt();
	if ( mode != 0 )
	{
		int red, green, blue;
		GetNavDebugColor( GetSurvivorCharacter(), &red, &green, &blue );
		switch ( mode )
		{
		case 1:
			FOR_EACH_VEC( m_potentiallyVisibleAreas, i )
				m_potentiallyVisibleAreas[i]->DrawFilled( red, green, blue, 100, 0.1f, true );
			break;
		case 2:
			FOR_EACH_VEC( TheNavAreas, i )
			{
				TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
				if ( area->IsInPlayerAAS( GetSurvivorCharacter() ) )
					area->DrawFilled( red, green, blue, 100, 0.1f, true );
			}
			break;
		case 3:
			FOR_EACH_VEC( TheNavAreas, i )
			{
				TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
				if ( area->IsInPlayerAAS( GetSurvivorCharacter() ) &&
					m_potentiallyVisibleAreas.Find( area ) == m_potentiallyVisibleAreas.InvalidIndex() )
					area->DrawFilled( 255, 0, 0, 100, 0.1f, true );
			}
			break;
		default:
			if ( mode == 4 && this == UTIL_GetListenServerHost() )
			{
				FOR_EACH_VEC( TheNavAreas, i )
				{
					TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
					for ( int character = 0; character < 8; ++character )
					{
						if ( area->IsInPlayerAAS( static_cast< SurvivorCharacterType >( character ) ) )
						{
							GetNavDebugColor( static_cast< SurvivorCharacterType >( character ), &red, &green, &blue );
							area->DrawFilled( red, green, blue, 100, 0.1f, true );
						}
					}
				}
			}
			break;
		}
	}

	if ( survivor_debug_visibility.GetInt() != 0 )
	{
		FOR_EACH_VEC( TheNavAreas, i )
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
			for ( int character = 0; character < 8; ++character )
			{
				if ( area->IsPotentiallyVisibleByCharacter( static_cast< SurvivorCharacterType >( character ) ) )
				{
					int red, green, blue;
					GetNavDebugColor( static_cast< SurvivorCharacterType >( character ), &red, &green, &blue );
					area->DrawFilled( red, green, blue, 100, 0.1f, true );
				}
			}
		}
	}

	if ( z_show_escape_route.GetInt() != 0 )
	{
		TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
		if ( area )
		{
			Vector from = area->GetCenter();
			CNavArea::MakeNewMarker();
			area->Mark();
			// Visibility remains relative to the ORIGINAL area, not the preceding one.
			for ( TerrorNavArea *next = area->GetNextHighestFlowConnectedArea( NULL ); next;
				next = next->GetNextHighestFlowConnectedArea( NULL ) )
			{
				if ( next->IsMarked() || !area->IsPotentiallyVisible( next ) ) break;
				NDebugOverlay::HorzArrow( from, next->GetCenter(), 10.0f, 255, 255, 0, 255, true, 0.01023f );
				from = next->GetCenter();
				next->Mark();
			}
		}
	}
}

// S349940. Its return value replaces m_bBoomerFalling on EVERY infected phase.
// A true result therefore suppresses the next query only, not all later falls.
bool CTerrorPlayer::UpdateBoomerFalling()
{
	if ( GetTeamNumber() != TEAM_INFECTED || GetZombieClass() != 2 || GetGroundEntity() ||
		GetMoveType() == MOVETYPE_LADDER || IsGhost() )
		return false;
	if ( !( GetAbsVelocity().z < 0.0f ) || m_bBoomerFalling ) return false;

	CTraceFilterNoNPCsOrPlayer filter( this, COLLISION_GROUP_NONE );
	Vector below = GetAbsOrigin();
	below.z -= 75.0f;
	trace_t trace;
	UTIL_TraceHull( GetAbsOrigin(), below, Vector( -10.0f, -10.0f, 0.0f ),
		Vector( 10.0f, 10.0f, 10.0f ), 0x0200408B, &filter, &trace );
	if ( trace.DidHit() ) return false;
	EmitInfectedSound( "BoomerZombie.Fall", 0.0f, 0.0f );
	return true;
}

// S346EC0. EVERY scheduled infected/incap update; damage is per call, not per second.
void CTerrorPlayer::UpdateIncapacitatedTank()
{
	float damage = z_tank_incapacitated_decay_rate.GetFloat();
	if ( IsSequenceFinished() &&
		( GetTerrorAnimState()->GetTerrorActivity() == TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_DIE_WHILE_RUNNING ) ||
		  GetTerrorAnimState()->GetTerrorActivity() == TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_DIE_FROM_STAND ) ) )
		damage = z_tank_incapacitated_health.GetFloat();
	if ( l4d_show_incapupdate.GetInt() != 0 )
		Msg( "%s: delta = %f\n", GetCharacterDisplayName(), static_cast< double >( damage ) );

	if ( m_incapacitatedByAttacker.Get() )
	{
		CTakeDamageInfo info( m_incapacitatedByAttacker.Get(), m_incapacitatedByAttacker.Get(), damage, 0x00020000 );
		TakeDamage( info );
	}
	else
	{
		CTakeDamageInfo info( GetWorldEntity(), GetWorldEntity(), damage, 0x00020000 );
		TakeDamage( info );
	}
}

// S349AD0. The shoulder/elbow names are ATTACHMENTS, not skeleton bones.
bool CTerrorPlayer::IsTankBumpAttachmentObstructed( int attachment )
{
	const Vector eye = EyePosition();
	Vector position;
	if ( !GetAttachment( attachment, position, NULL, NULL, NULL ) ) return false;
	trace_t trace;
	UTIL_TraceLine( eye, position, 0x400B, this, COLLISION_GROUP_NONE, &trace );
	return !trace.startsolid && trace.m_pEnt == GetWorldEntity();
}

// S34B630. Caller admits only native100B2FD0(this,8): team3 AND virtual class8.
void CTerrorPlayer::UpdateTankBump()
{
	if ( !( static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_tankBumpTimer.m_timestamp.Get() ) ) ) return;
	const double deadline = static_cast< double >( gpGlobals->curtime ) + static_cast< double >( 0.1f );
	if ( static_cast< double >( m_tankBumpTimer.m_timestamp.Get() ) != deadline )
		m_tankBumpTimer.m_timestamp.GetForModify() = static_cast< float >( deadline );
	m_tankBumpTimer.m_duration = 0.1f;
	const Vector &velocity = GetAbsVelocity();
	if ( velocity.x > -0.01f && velocity.x < 0.01f && velocity.y > -0.01f && velocity.y < 0.01f &&
		velocity.z > -0.01f && velocity.z < 0.01f ) return;
	if ( !CanAttack() ) return;
	CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( GetActiveWeapon() );
	if ( !weapon || weapon->IsAttacking() || weapon->m_bSwingInProgress ) return;
	CBaseAbility *ability = GetCustomAbility();
	if ( !ability || ability->IsActive() ) return;

	int attachment = LookupAttachment( "lshoulder" );
	bool leftBlocked = false;
	if ( attachment > 0 )
	{
		leftBlocked = IsTankBumpAttachmentObstructed( attachment );
		if ( !leftBlocked )
		{
			attachment = LookupAttachment( "lelbow" );
			leftBlocked = attachment > 0 && IsTankBumpAttachmentObstructed( attachment );
		}
	}
	int sequence;
	if ( leftBlocked )
		sequence = SelectWeightedSequence( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_SMASH_LEFT ) );
	else
	{
		attachment = LookupAttachment( "rshoulder" );
		if ( attachment <= 0 ) return;
		if ( !IsTankBumpAttachmentObstructed( attachment ) )
		{
			attachment = LookupAttachment( "relbow" );
			if ( attachment <= 0 || !IsTankBumpAttachmentObstructed( attachment ) ) return;
		}
		sequence = SelectWeightedSequence( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_SMASH_RIGHT ) );
	}
	if ( sequence <= 0 ) return;
	m_tankBumpTimer.Start( 2.0f );
	weapon->SendWeaponAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK ) );
	float next = tank_swing_miss_interval.GetFloat() + gpGlobals->curtime;
	if ( weapon->m_flNextPrimaryAttack > next ) next = weapon->m_flNextPrimaryAttack;
	weapon->m_flNextPrimaryAttack = next;
	ability->SuppressActivation( tank_swing_miss_interval.GetFloat() );
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_CUSTOM_ATTACK_GESTURE, sequence );
	EmitInfectedSound( "HulkZombie.Attack", 0.0f, 0.0f );
}

// S347040. AFTER the optional Tank bump, for every living team3 class.
void CTerrorPlayer::DrawInfectedDebug()
{
	if ( z_debug.GetInt() == 0 ) return;
	int red, green, blue;
	switch ( GetZombieClass() )
	{
	case 1: red = 255; green = 100; blue = 0; break;
	case 2: red = 255; green = 0; blue = 0; break;
	case 3: red = 0; green = 0; blue = 255; break;
	case 8: red = 0; green = 255; blue = 0; break;
	default: red = 255; green = 255; blue = 0; break;
	}
	const Vector &origin = GetAbsOrigin();
	Vector top = origin;
	top.z += 70.0f;
	NDebugOverlay::VertArrow( top, GetAbsOrigin(), 30.0f, red, green, blue, 255, true, 0.1f );
	if ( z_debug.GetInt() != 0 )
	{
		char text[512];
		TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
		if ( area ) Q_snprintf( text, sizeof( text ), "Flow: %3.2f", area->GetFlowDistanceTowardGoal() );
		else Q_snprintf( text, sizeof( text ), "Flow: NULL" );
		NDebugOverlay::EntityText( entindex(), 1, text, 0.1f, 255, 255, 255, 255 );
	}
}

// S315850. Scheduled tail AFTER UpdateITEvents, with no alive/team outer gate.
void CTerrorPlayer::UpdateHealingGnome()
{
	if ( m_bCarryingHealingGnome && TerrorGameRules()->IsHealingGnomeMutation() )
	{
		const float previous = m_flGnomePickupTime;
		if ( previous > -1.0f )
		{
			const float now = gpGlobals->curtime;
			m_flGnomePickupTime = now;
			const float rate = sv_healing_gnome_replenish_rate.GetFloat();
			const int maximum = GetMaxHealth();
			GiveHealthBuffer( rate * ( now - previous ), static_cast< float >( maximum ) );
		}
	}
}

// S3467E0. Caller: cached entry-team1 && m_wantsToPlay; no alive gate.
void CTerrorPlayer::UpdateAutoJoinTeam()
{
	if ( !TheDirector->IsTeamFull( TEAM_SURVIVOR, 1 ) )
	{
		UTIL_LogPrintf( "%s is auto-joining survivors\n", GetPlayerName() );
		HandleCommand_JoinTeam( TEAM_SURVIVOR, SurvivorCharacter_Unknown, false );
	}
	else if ( TheDirector->HasAnySurvivorLeftSafeArea() && !TheDirector->IsTeamFull( TEAM_INFECTED, 0 ) )
	{
		int saved, restoring;
		TheDirector->GetSavedPlayerStates()->GetRestoringPlayerCounts( TEAM_INFECTED, &saved, &restoring );
		const int players = TheDirector->CountPlayersOnTeam( TEAM_INFECTED, false );
		UTIL_LogPrintf( "%s is auto-joining infected - %d in game, %d/%d transitioning\n", GetPlayerName(), players, saved, restoring );
		HandleCommand_JoinTeam( TEAM_INFECTED, SurvivorCharacter_Unknown, false );
	}
}

// S347230. Scheduled tail AFTER CCSPlayer::PostThink (not on the not-due path).
bool CTerrorPlayer::UpdateAirborneState()
{
	if ( m_wasAirborne && GetGroundEntity() )
	{
		SetLaunchSource( m_hLaunchSource.Get() );
	}
	else if ( GetGroundEntity() && m_launchSourceTimer.m_timestamp.Get() > 0.0f &&
		static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_launchSourceTimer.m_timestamp.Get() ) )
		SetLaunchSource( NULL );

	const Vector velocity = GetAbsVelocity();
	const float previousZ = m_lastPostThinkVelocity.z;
	if ( IsAlive() && !GetGroundEntity() && previousZ >= 0.0f && velocity.z <= 0.0f && !m_isHangingFromLedge )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "player_jump_apex", false );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			gameeventmanager->FireEvent( event, false );
		}
	}
	m_lastPostThinkVelocity = velocity;
	m_wasAirborne = GetGroundEntity() == NULL;
	return m_wasAirborne;
}

extern ConVar z_frustration;
extern ConVar z_frustration_lifetime;
extern ConVar z_frustration_los_delay;

// S:34C610. ResetFrustration30E660, spawn3269D0 and visible-enemy12600 are
// existing owners. This update never substitutes an LOS ray for that timer.
void CTerrorPlayer::UpdateTankFrustration()
{
	if ( !z_frustration.GetBool() || GetTeamNumber() != TEAM_INFECTED || GetZombieClass() != 8 ||
		IsBot() || IsGhost() || !TheDirector->IsTankFrustrationEnabled() )
	{
		m_frustration = 0;
		return;
	}
	if ( m_frustrationGraceTimer.m_timestamp <= 0.0f || !m_frustrationGraceTimer.IsElapsed() ) return;
	if ( !( GetTimeSinceVisibleEnemy() > z_frustration_los_delay.GetFloat() ) ) return;
	m_frustrationGraceTimer.Start( 1.0f );

	// S:347440/025170, flag1: connected living team2, excluding incapacitated;
	// team4 is NOT admitted by this particular native collector.
	const int livingSurvivors = CountLivingSurvivors( 1 );
	if ( livingSurvivors == 0 )
	{
		m_frustration = 0;
		return;
	}
	if ( !TheDirector->HasAnySurvivorLeftSafeArea() || TheDirector->IsTankFrustrationSuppressed() || TheDirector->IsFinaleWon() ) return;
	const int lifetime = z_frustration_lifetime.GetInt();
	// S34C610: skip only the undefined quotient, not its following phase.
	if ( lifetime != 0 )
	{
		const int frustration = static_cast< int32 >( static_cast< uint32 >( m_frustration ) +
			static_cast< uint32 >( 100 / lifetime ) );
		m_frustration = frustration <= 100 ? frustration : 100;
	}
	// Native flags +316 bit0x08000000 is the pinned game's ONFIRE flag.
	if ( m_frustration < 100 || IsOnFire() ) return;
	if ( TheDirector->GetScriptedMode()->IsTankRun() )
	{
		CommitSuicide( false, false );
		return;
	}
	int candidates = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		// This second native scan has a valid-player edict gate, NOT IsConnected.
		if ( player && player->edict() && player->GetTeamNumber() == TEAM_INFECTED &&
			!player->IsBot() && ( !player->IsAlive() || player->GetZombieClass() != 8 ) ) ++candidates;
	}
	if ( !( TheDirector->GetTankOfferLotteryProgress() <= 0.0 && TheDirector->GetTankControlTransferProgress() <= 0.0 ) ) return;
	IGameEvent *event = gameeventmanager->CreateEvent( "tank_frustrated" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	if ( candidates <= 0 || TheDirector->GetTankControlPassCount() >= 2 )
	{
		ReplaceWithBot( false ); // S:32B5F0, real serialization and Tank factory chain.
		m_zombieClass = 3; // S316490: raw class change, NOT SetZombieState.
		State_Transition( STATE_GHOST ); // S:24A9B0(8).
	}
	else TheDirector->OfferTank( this, false ); // S:2609D0; sole Director owner.
}

static ConVar z_splatterdistance( "z_splatterdistance", "100", 0x00002006 );

void CTerrorPlayer::AddTerrorSplatter( int amount, float distance, int kind )
{
	if ( z_splatterdistance.GetFloat() > distance )
		m_nTerrorSplatter[ kind >= 1 && kind <= 3 ? kind : 0 ] += amount;
}

void CTerrorPlayer::FlushTerrorSplatter()
{
	if ( m_nTerrorSplatter[0] == 0 && m_nTerrorSplatter[1] == 0 &&
		 m_nTerrorSplatter[2] == 0 && m_nTerrorSplatter[3] == 0 )
		return;
	CRecipientFilter filter;
	filter.MakeReliable();
	filter.AddRecipient( this );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *observer = UTIL_PlayerByIndex( i );
		if ( observer && observer != this && observer->GetObserverMode() == OBS_MODE_IN_EYE &&
			 observer->GetObserverTarget() == this )
			filter.AddRecipient( observer );
	}
	static const char *names[4] = { "Splatter", "MeleeSlashSplatter", "MeleeClubSplatter", "MudSplatter" };
	for ( int i = 0; i < 4; ++i )
	{
		if ( m_nTerrorSplatter[i] != 0 )
		{
			UserMessageBegin( filter, names[i] );
				WRITE_BYTE( m_nTerrorSplatter[i] );
			MessageEnd();
			m_nTerrorSplatter[i] = 0;
		}
	}
}

void CTerrorPlayer::DoTerrorAnimationEvent( TerrorPlayerAnimEvent event, int nData )
{
	// S2FBB30 dispatches locally first, then broadcasts only the genuine7/66 pair.
	GetTerrorAnimState()->DoTerrorAnimationEvent( event, nData );
	if ( event == TERROR_ANIMEVENT_JUMP )
		TE_TerrorPlayerAnimEvent( this, 7, nData );
	else if ( event == TERROR_ANIMEVENT_CUSTOM_ATTACK_GESTURE_ALT )
		TE_TerrorPlayerAnimEvent( this, 66, TerrorActivityToOriginal( static_cast< Activity >( nData ) ) );
}

static ConVar survivor_calm_weapon_delay( "survivor_calm_weapon_delay", "5", 0x00004004,
	"survivors are not calm for this many seconds after firing" );

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
void CTerrorPlayer::NoteTerrorWeaponFired()
{
	if ( !GetUseEntity() )
		CCSPlayer::NoteTerrorWeaponFired();
	m_previousTerrorFireTimestamp = m_terrorFireInterval.GetStartTime();
	m_terrorFireInterval.Start();
	// S309320/S3074D0: notify every connected living survivor, including self.
	const Vector firedPosition = GetAbsOrigin();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || ENTINDEX( player->edict() ) == 0 ||
			!player->IsPlayer() || !player->IsConnected() )
			continue;
		const int team = player->GetTeamNumber();
		if ( ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR ) || !player->IsAlive() )
			continue;
		const Vector delta = firedPosition - player->GetAbsOrigin();
		const float rangeSquared = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
		if ( rangeSquared > 250000.0f )
			continue;
		const float delayMinusOne = survivor_calm_weapon_delay.GetFloat() - 1.0f;
		const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		const float remaining = 500.0f - distance;
		const float fraction = remaining * 0.0020000001f;
		const float duration = fraction * delayMinusOne + 1.0f;
		player->DelayCalm( duration );
	}
	GetMusic()->OnWeaponFired();
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1930
#pragma fp_contract( off )
#endif
#endif

double CTerrorPlayer::GetTimeSinceWeaponFired() const
{
	const float timestamp = m_terrorFireInterval.m_timestamp.Get();
	if ( !( timestamp > 0.0f ) )
		return gpGlobals->curtime;
	const float elapsed = static_cast< float >( static_cast< double >( gpGlobals->curtime ) - timestamp );
	if ( elapsed <= 0.0f )
		return static_cast< double >( gpGlobals->curtime ) - m_previousTerrorFireTimestamp;
	if ( !( m_terrorFireInterval.m_timestamp.Get() > 0.0f ) )
		return static_cast< double >( 99999.9f );
	return static_cast< double >( gpGlobals->curtime ) - m_terrorFireInterval.m_timestamp.Get();
}
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif



bool CTerrorPlayer::IsCommonPostThinkDue()
{
	if ( !tp_schedule_post_think.GetBool() )
		return true;
	if ( s_commonPostThinkFrame != gpGlobals->framecount )
	{
		const int periodTicks = _mm_cvttss_si32( _mm_set_ss( 0.1f / gpGlobals->interval_per_tick + 0.5f ) );
		s_commonPostThinkFrame = gpGlobals->framecount;
		const float period = static_cast< float >( periodTicks ) * gpGlobals->interval_per_tick;
		const float countPerTick = static_cast< float >( s_commonPostThinkPlayerCount ) / static_cast< float >( periodTicks );
		// Native ftol2_sse stores double, then CVTTSD2SI eax. Invalid
		// conversion yields INT_MIN, and the subsequent SUB wraps in32bits.
		unsigned int quota = static_cast< unsigned int >(
			_mm_cvttsd_si32( _mm_set_sd( ceil( static_cast< double >( countPerTick ) ) ) ) );
		int i = s_commonPostThinkPlayers.Head();
		if ( quota != 0 )
		{
			while ( i != s_commonPostThinkPlayers.InvalidIndex() )
			{
				--quota;
				s_commonPostThinkPlayers[i]->m_commonPostThinkDue = true;
				i = s_commonPostThinkPlayers.Next( i );
				if ( quota == 0 )
					break;
			}
		}
		while ( i != s_commonPostThinkPlayers.InvalidIndex() )
		{
			CTerrorPlayer *player = s_commonPostThinkPlayers[i];
			const float elapsed = gpGlobals->curtime - player->m_lastCommonPostThink;
			// The first not-overdue entry terminates the pass. Native does
			// not keep scanning later entries, even when one is overdue.
			if ( !( elapsed > period * 2.0f ) )
				break;
			player->m_commonPostThinkDue = true;
			i = s_commonPostThinkPlayers.Next( i );
		}
	}
	if ( !m_commonPostThinkDue )
		return false;
	m_commonPostThinkDue = false;
	m_lastCommonPostThink = gpGlobals->curtime;
	const int index = s_commonPostThinkPlayers.Find( this );
	s_commonPostThinkPlayers.LinkToTail( index );
	return true;
}

float CTerrorPlayer::GetNoiseLevel() const
{
	float flValue = m_noiseLevel;
	if ( flValue > 0.0f )
	{
		const float flElapsed = gpGlobals->curtime - m_lastNoiseTime;
		if ( flElapsed > z_noise_level_hold_time.GetFloat() )
		{
			flValue -= ( flElapsed - z_noise_level_hold_time.GetFloat() ) * z_noise_level_fade_rate.GetFloat();
			if ( flValue < 0.0f )
				return 0.0f;
		}
	}
	return flValue;
}

void CTerrorPlayer::SetNoiseLevel( float flRequested )
{
	if ( !TerrorGameRules()->IsPlayerControlledZombies() || GetTeamNumber() != TEAM_SURVIVOR )
		return;
	if ( z_noise_level_max.GetFloat() <= flRequested )
		flRequested = z_noise_level_max.GetFloat();
	const float flCurrent = GetNoiseLevel();
	if ( flCurrent <= flRequested )
	{
		m_lastNoiseTime = gpGlobals->curtime;
		m_noiseLevel = flRequested;
		m_noiseLevelLatch = ( m_noiseLevelLatch + 1 ) % 8;
	}
}

namespace
{
	class CollectTerrorPlayersFunctor
	{
	public:
		CollectTerrorPlayersFunctor( CUtlVector< CTerrorPlayer * > &out, int team, int stateMask, int controlMask, int zombieClass ) :
			m_out( out ), m_team( team ), m_stateMask( stateMask ), m_controlMask( controlMask ), m_zombieClass( zombieClass ) {}

		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = ToTerrorPlayer( base );
			if ( !player || ( m_team != -1 && player->GetTeamNumber() != m_team ) )
				return true;
			if ( m_team == TEAM_INFECTED && m_zombieClass != 9 && player->GetZombieClass() != m_zombieClass )
				return true;
			if ( !player->IsBot() && !( m_controlMask & 1 ) )
				return true;
			if ( player->IsBot() && !( m_controlMask & 2 ) )
				return true;
			if ( !player->IsAlive() && ( m_stateMask & 1 ) )
				m_out.AddToTail( player );
			if ( player->IsAlive() && !player->IsGhost() && ( m_stateMask & 2 ) )
				m_out.AddToTail( player );
			if ( player->IsAlive() && player->IsGhost() && ( m_stateMask & 4 ) )
				m_out.AddToTail( player );
			return true;
		}

	private:
		CUtlVector< CTerrorPlayer * > &m_out;
		int m_team;
		int m_stateMask;
		int m_controlMask;
		int m_zombieClass;
	};
}

void CollectTerrorPlayers( CUtlVector< CTerrorPlayer * > &out, int team, int stateMask, int controlMask, int zombieClass )
{
	CollectTerrorPlayersFunctor collect( out, team, stateMask, controlMask, zombieClass );
	ForEachPlayer( collect );
}

Activity CTerrorPlayer::GetTerrorActivity() const
{
	return GetTerrorAnimState()->GetTerrorActivity();
}

// S25170/25B50. Reuse the actual connected-player iterator without a list.
int CountLivingSurvivors( unsigned char flags )
{
	class CountFunctor
	{
	public:
		explicit CountFunctor( unsigned char flags ) : m_flags( flags ), m_count( 0 ) {}
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
			if ( !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ||
				( ( m_flags & 4 ) && player->IsBot() ) )
				return true;
			const bool bIncapacitated = player->IsIncapacitated();
			if ( ( bIncapacitated && ( m_flags & 1 ) ) || ( !bIncapacitated && ( m_flags & 2 ) ) )
				return true;
			++m_count;
			return true;
		}
		unsigned char m_flags;
		int m_count;
	} count( flags );
	ForEachPlayer( count );
	return count.m_count;
}

// S296530/295BB0 appends mobile survivors; it is not the generic alive list.
void CollectMobileSurvivors( CUtlVector< CTerrorPlayer * > &out )
{
	class CollectFunctor
	{
	public:
		explicit CollectFunctor( CUtlVector< CTerrorPlayer * > &out ) : m_out( out ) {}
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
			if ( player->GetTeamNumber() == TEAM_SURVIVOR && player->IsAlive() &&
				!player->IsIncapacitated() && !player->IsHangingFromLedge() && !player->IsDominated() )
				m_out.AddToTail( player );
			return true;
		}
		CUtlVector< CTerrorPlayer * > &m_out;
	} collect( out );
	ForEachPlayer( collect );
}

bool CTerrorPlayer::IsAnimationBusy() const
{
	return GetTerrorAnimState()->IsAnimationBusy();
}

// S346440. Scheduled, immediately after UpdateStagger and before S347670.
void CTerrorPlayer::UpdateSpitterBurnMusic()
{
	if ( GetTeamNumber() == TEAM_SURVIVOR )
	{
		if ( gpGlobals->curtime - m_lastSpitTime > 1.0f && m_lastSpitTime >= 0.0f )
		{
			GetMusic()->OnSpitterSpitAttackEnded();
			m_lastSpitTime = -1.0f;
		}
	}
}

// S347670. Scheduled, between S346440 and DecayLineOfScrimmage.
void CTerrorPlayer::UpdateInfectedVocalizations()
{
	if ( GetTeamNumber() != TEAM_INFECTED || !IsAlive() || IsGhost() )
		return;

	const bool neutral = m_zombieState == 0;
	const bool alert = m_zombieState == 1;
	const bool engaged = m_zombieState == 2;
	if ( static_cast< double >( gpGlobals->curtime ) >= m_vocalizeTimer.m_timestamp.Get() &&
		static_cast< double >( gpGlobals->curtime ) >= m_infectedAmbientVocalizeTimer.m_timestamp.Get() )
	{
		switch ( GetZombieClass() )
		{
		case 1:
			{
				float interval;
				if ( GetTongueVictim() )
				{
					interval = RandomFloat( 1.0f, 2.0f );
					EmitInfectedSound( "SmokerZombie.Attack", 0.0f, 0.0f );
				}
				else
				{
					interval = RandomFloat( 4.0f, 6.0f );
					EmitInfectedSound( "SmokerZombie.Breathe", interval, interval );
				}
				m_infectedAmbientVocalizeTimer.Start( interval );
				break;
			}
		case 2:
			if ( !alert && !engaged )
			{
				const float interval = RandomFloat( 2.0f, 3.0f );
				EmitInfectedSound( "BoomerZombie.Groan", interval, interval );
				m_infectedAmbientVocalizeTimer.Start( interval );
			}
			else
			{
				EmitInfectedSound( "BoomerZombie.Voice", 0.0f, 0.0f );
				m_infectedAmbientVocalizeTimer.Start( RandomFloat( 1.0f, 2.0f ) );
			}
			break;
		case 3:
		case 5:
			m_infectedAmbientVocalizeTimer.Start( RandomFloat( 4.0f, 6.0f ) );
			break;
		case 4:
			if ( !engaged )
			{
				const float interval = RandomFloat( 3.0f, 5.0f );
				EmitInfectedSound( "SpitterZombie.Voice", interval, neutral ? interval : 0.0f );
				m_infectedAmbientVocalizeTimer.Start( interval );
			}
			break;
		case 6:
			if ( !m_pummelVictim.Get() )
			{
				const float interval = RandomFloat( 3.0f, 5.0f );
				EmitInfectedSound( "ChargerZombie.Voice", interval, neutral ? interval : 0.0f );
				m_infectedAmbientVocalizeTimer.Start( interval );
			}
			break;
		case 8:
			if ( !IsIncapacitated() )
			{
				// The two ability-handle resolutions and the RandomInt before
				// the arousal branch are both present in the original.
				if ( GetCustomAbility() && GetCustomAbility()->IsActive() )
					break;
				if ( IsOnFire() )
				{
					m_infectedAmbientVocalizeTimer.Start( RandomFloat( 3.0f, 5.0f ) );
					const float noiseDuration = static_cast< float >(
						static_cast< double >( m_infectedAmbientVocalizeTimer.m_timestamp.Get() ) - gpGlobals->curtime );
					const float minimumInterval = static_cast< float >(
						static_cast< double >( m_infectedAmbientVocalizeTimer.m_timestamp.Get() ) - gpGlobals->curtime );
					EmitInfectedSound( "HulkZombie.PainFire", minimumInterval, noiseDuration );
					break;
				}
				const int choice = RandomInt( 0, 100 );
				if ( alert || engaged )
				{
					EmitInfectedSound( "HulkZombie.Yell", 0.0f, 0.0f );
					m_infectedAmbientVocalizeTimer.Start( RandomFloat( 4.0f, 6.0f ) );
				}
				else if ( choice < 25 )
				{
					const float interval = RandomFloat( 1.0f, 2.0f );
					EmitInfectedSound( "HulkZombie.Growl", interval, interval );
					m_infectedAmbientVocalizeTimer.Start( interval );
				}
				else
				{
					EmitInfectedSound( "HulkZombie.Breathe", 0.0f, 0.0f );
					m_infectedAmbientVocalizeTimer.Start( RandomFloat( 1.0f, 2.0f ) );
				}
			}
			break;
		default:
			EmitInfectedSound( alert || engaged ? "PlayerZombie.Rage" : "PlayerZombie.Breathe", 0.0f, 0.0f );
			m_infectedAmbientVocalizeTimer.Start( RandomFloat( 4.0f, 6.0f ) );
			break;
		}
	}

	// Re-query the team/class and the voice timer after ambient sound emission.
	if ( ( GetTeamNumber() != TEAM_INFECTED || GetZombieClass() != 8 ) &&
		static_cast< double >( gpGlobals->curtime ) >= m_vocalizeTimer.m_timestamp.Get() )
	{
		CWeaponCSBase *weapon = static_cast< CWeaponCSBase * >( GetActiveWeapon() );
		if ( weapon && weapon->IsAttacking() )
		{
			switch ( GetZombieClass() )
			{
			case 1: EmitInfectedSound( "SmokerZombie.Attack", 0.0f, 0.0f ); break;
			case 2: EmitInfectedSound( "BoomerZombie.Attack", 0.0f, 0.0f ); break;
			case 3: EmitInfectedSound( "PlayerZombie.Attack", 0.0f, 0.0f ); break;
			}
		}
	}
}

// S34B9F0. Scheduled, between S347670 and UpdateTongueVictim.
void CTerrorPlayer::DecayLineOfScrimmage()
{
	if ( IsAlive() || GetTeamNumber() != TEAM_INFECTED ||
		!( static_cast< double >( gpGlobals->curtime ) >= m_scrimmageTimer.m_timestamp.Get() ) )
		return;
	m_scrimmageTimer.Start( 0.5f );
	if ( m_scrimmageType != 1 )
		return;

	float elapsed = 99999.8984375f;
	if ( m_stateEnterTimer.m_timestamp.Get() > 0.0f )
		elapsed = static_cast< float >( static_cast< double >( gpGlobals->curtime ) - m_stateEnterTimer.m_timestamp.Get() );
	elapsed -= z_scrimmage_creep_delay.GetFloat();
	if ( !( elapsed > 0.0f ) )
		return;
	float decay = z_scrimmage_creep_rate.GetFloat() * elapsed;
	float flow;
	Vector position;
	QAngle angles;
	if ( m_flLineOfScrimmageFlow > 0.0f )
	{
		const float wanted = static_cast< float >( static_cast< double >( m_flLineOfScrimmageCurrentFlow ) - decay );
		if ( ComputeLineOfScrimmage( true, wanted, &flow, &position, &angles ) &&
			m_flLineOfScrimmageFlow > flow && wanted > 0.0f )
		{
			m_flLineOfScrimmageFlow = flow;
			if ( m_lineOfScrimmagePos.Get() != position )
				m_lineOfScrimmagePos.GetForModify() = position;
			if ( m_lineOfScrimmageAngle.Get() != angles )
				m_lineOfScrimmageAngle.GetForModify() = angles;
			DevMsg( "DecayLineOfScrimmage: %s has a front line of scrimmage of %f flow (wanted %f flow)\n",
				GetPlayerName(), static_cast< double >( m_flLineOfScrimmageFlow ), static_cast< double >( wanted ) );
			UpdateScrimmageYardLines( 0.0f );
		}
	}
	if ( m_flRearLineOfScrimmageFlow > 0.0f )
	{
		decay = static_cast< float >( static_cast< double >( m_flRearLineOfScrimmageCurrentFlow ) + decay );
		// Native passes front=true, and the successful REAR calculation writes
		// the FRONT flow/position/angles. This is a proven original bug.
		if ( ComputeLineOfScrimmage( true, decay, &flow, &position, &angles ) &&
			flow > m_flRearLineOfScrimmageFlow && decay > 0.0f )
		{
			m_flLineOfScrimmageFlow = flow;
			if ( m_lineOfScrimmagePos.Get() != position )
				m_lineOfScrimmagePos.GetForModify() = position;
			if ( m_lineOfScrimmageAngle.Get() != angles )
				m_lineOfScrimmageAngle.GetForModify() = angles;
			DevMsg( "DecayLineOfScrimmage: %s has a rear line of scrimmage of %f flow (wanted %f flow)\n",
				GetPlayerName(), static_cast< double >( m_flRearLineOfScrimmageFlow ), static_cast< double >( decay ) );
		}
	}
}

// S348AF0. Scheduled, after Terror_UpdatePlayerEnvironment and before the
// FIRST UpdateWeaponVisibility. Replaces the partial existing definition.
void CTerrorPlayer::NotifyCommonHearing()
{
	if ( !( static_cast< double >( gpGlobals->curtime ) >= m_commonHearingTimer.m_timestamp.Get() ) )
		return;
	m_commonHearingTimer.Start( 0.25f );
	const bool recentlyFired = GetTimeSinceWeaponFired() < 1.0;
	if ( ( IsRunning() && ( m_commonRunningTimer.m_timestamp.Get() <= 0.0f ||
		static_cast< float >( static_cast< double >( gpGlobals->curtime ) - m_commonRunningTimer.m_timestamp.Get() ) > 2.0f ) ) || recentlyFired )
	{
		TheNextBots().OnSound( this, WorldSpaceCenter(), NULL );
	}
}

// S2C8D60, with native S350DE0 display names. This is NOT the response-rule
// character name (S350EA0), and must not substitute that different mapper.
const char *CTerrorPlayer::GetCharacterDisplayName() const
{
	const int team = GetTeamNumber();
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
		return GetSurvivorCharacterDisplayName( GetSurvivorCharacter() );
	return GetZombieClassName( GetZombieClass() );
}

// S348C00. One body, TWO calls: scheduled before common-damage DPS, then
// again inside the first IsAlive branch before UpdateFriendlyFireResponse.
void CTerrorPlayer::UpdateWeaponVisibility()
{
	CBaseCombatWeapon *active = GetActiveWeapon();
	if ( !active )
	{
		if ( !m_jockeyAttacker.Get() && !SwitchToNextBestWeapon( NULL ) )
		{
			CBaseCombatWeapon *secondary = Weapon_GetSlot( 1 );
			if ( secondary && secondary->CanDeploy() )
				Weapon_Switch( secondary, 0 );
		}
	}
	else
	{
		if ( l4d_show_weaponequip.GetBool() )
		{
			for ( int i = 0; i < MAX_WEAPONS; ++i )
			{
				CBaseCombatWeapon *weapon = GetWeapon( i );
				if ( !weapon )
					continue;
				Msg( "%s weapon %d: %s", GetCharacterDisplayName(), i, weapon->GetPrintName() );
				if ( active == weapon ) Msg( ", IsActive" );
				if ( weapon->IsWeaponVisible() ) Msg( ", IsVisible" );
				if ( weapon->CanDeploy() ) Msg( ", CanDeploy" );
				if ( weapon->IsDormant() ) Msg( ", Dormant" );
				Msg( "\n" );
			}
		}
		if ( !active->IsWeaponVisible() && active->CanDeploy() )
		{
			active->Deploy();
		}
		else if ( active->IsWeaponVisible() && !active->CanDeploy() )
		{
			active->Holster( NULL );
		}
		else if ( !active->IsWeaponVisible() && static_cast< CWeaponCSBase * >( active )->GetWeaponID() != 1 )
		{
			if ( !IsIncapacitated() )
			{
				SwitchToNextBestWeapon( active );
			}
			else
			{
				CBaseCombatWeapon *pistol = GetWeaponOfType( 1 );
				if ( !pistol )
					pistol = GetWeaponOfType( 32 );
				if ( pistol && pistol->CanDeploy() )
					Weapon_Switch( pistol, 0 );
			}
		}
	}
	CBaseCombatWeapon *carried = Weapon_GetSlot( 5 );
	if ( carried && carried != active )
		DropWeapon( carried, true, NULL );
}

// S309E00. Immediately after ReleaseLedge316920, under the native
// m_isHangingFromLedge && deadline>0 && now>=deadline gate.
void CTerrorPlayer::RestartLedgeGroundCheckTimer()
{
	m_ledgeGroundCheckTimer.Start( survivor_ledge_grab_ground_check_time.GetFloat() );
}

// S348F90. First scheduled IsAlive branch, after the tongue-release-source
// timeout clear and before UpdateFallingFromLedge.
void CTerrorPlayer::UpdateStaggerSource()
{
	if ( m_isHangingFromLedge && m_hStaggerSource.Get() )
	{
		m_hStaggerSource = NULL;
		return;
	}
	if ( !GetGroundEntity() && m_hStaggerSource.Get() )
	{
		if ( static_cast< double >( m_staggerSourceTimer.m_timestamp.Get() ) - gpGlobals->curtime < 2.0 )
			m_staggerSourceTimer.Start( 2.0f );
		return;
	}
	if ( GetGroundEntity() && m_hStaggerSource.Get() &&
		static_cast< double >( gpGlobals->curtime ) >= m_staggerSourceTimer.m_timestamp.Get() )
	{
		m_hStaggerSource = NULL;
	}
}

// S346B30. First scheduled IsAlive branch, immediately after S348F90 and
// before S3490A0. This is ledge-fall damage, NOT a hearing update.
void CTerrorPlayer::UpdateFallingFromLedge()
{
	if ( m_isFallingFromLedge )
	{
		if ( GetGroundEntity() || GetAbsVelocity().z >= 0.0f )
		{
			SetAbsVelocity( vec3_origin );
			CBaseEntity *attacker = GetWorldEntity();
			if ( GetTongueReleaseSource() )
				attacker = GetTongueReleaseSource();
			CTakeDamageInfo info( attacker, attacker, 1.0f, DMG_FALL );
			TakeDamage( info );
		}
	}
}

// S346BF0. Scheduled, outside both IsAlive branches, after the team phase
// and weapon-flag callback, immediately before UpdateITEvents3497A0.
void CTerrorPlayer::UpdateNoise()
{
	if ( IsAlive() && ( IsIT() || IsDominated() ) )
	{
		const float maximum = z_noise_level_max.GetFloat();
		if ( static_cast< double >( maximum ) * 0.75 > static_cast< double >( GetNoiseLevel() ) )
			SetNoiseLevel( z_noise_level_vocalize.GetFloat() );
	}
	if ( z_noise_level_display.GetBool() )
	{
		const double noise = GetNoiseLevel();
		if ( noise != 0.0 )
		{
			Vector position = GetAbsOrigin();
			position.z += 40.0f;
			const float hold = z_noise_level_hold_time.GetFloat();
			const float remaining = hold - ( gpGlobals->curtime - m_lastNoiseTime );
			float displayed;
			if ( remaining > hold )
				displayed = hold;
			else if ( 0.0f > remaining )
				displayed = 0.0f;
			else
				displayed = remaining;
			char text[20];
			Q_snprintf( text, sizeof( text ), "%.1f (%.1f)", noise, static_cast< double >( displayed ) );
			NDebugOverlay::Text( position, text, false, 0.0f );
		}
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

void CTerrorPlayer::Ignite( float flFlameLifetime, bool bNPCOnly, float flSize, bool bCalledByLevelDesigner )
{
	m_burnDamageType = DMG_BURN;
	if ( IsOnFire() )
	{
		CEntityFlame *flame = dynamic_cast< CEntityFlame * >( GetEffectEntity() );
		if ( flame )
		{
			if ( flame->GetRemainingLife() > flFlameLifetime )
				flame->SetLifetime( static_cast< float >( flame->GetRemainingLife() ) );
			else
				flame->SetLifetime( flFlameLifetime );
		}
		return;
	}
	if ( !IsAlive() || IsGhost() )
		return;

	CEntityFlame *flame = CEntityFlame::Create( this, flFlameLifetime, flSize, true );
	if ( flame )
	{
		flame->SetAttacker( m_burnAttacker.Get() );
		// The factory has already initialized a lifetime. Native deliberately
		// writes the ORIGINAL requested lifetime again, including zero/negative.
		flame->SetLifetime( flFlameLifetime );
		AddFlag( FL_ONFIRE );
		SetEffectEntity( flame );
		if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 && GetSkin() == 0 )
		{
			CStudioHdr *studio = GetModelPtr();
			if ( studio && studio->numskinfamilies() > 1 && GetSkin() != 1 )
				SetSkin( 1 );
		}
	}
	// These execute even when entity creation returns NULL, but never on the
	// already-burning/dead/ghost branches above. Both bool arguments are ignored.
	m_OnIgnite.FireOutput( this, this, 0.0f );
	if ( GetTeamNumber() == TEAM_INFECTED &&
		static_cast< double >( gpGlobals->curtime ) >= m_vocalizeTimer.m_timestamp.Get() )
	{
		if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
		{
			EmitInfectedSound( "HulkZombie.PainFire", 3.0f, 0.0f );
			m_infectedAmbientVocalizeTimer.Start( 3.0f );
		}
		else
		{
			EmitInfectedSound( "Zombie.IgniteScream", 5.0f, 0.0f );
			m_infectedAmbientVocalizeTimer.Start( 5.0f );
		}
	}
}

// S2FBCC0: preserve the raw desired-character command domain.
void CTerrorPlayer::SetDesiredSurvivorCharacter( int character )
{
	m_iDesiredSurvivorCharacter = character;
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

void CTerrorPlayer::InputSetGlowEnabled( inputdata_t &inputdata )
{
	// S3189A0: independent survivor-outline permission, not GlowProperty type.
	m_bSurvivorGlowEnabled = inputdata.value.Bool();
}
// S30A720: this is not a general statistics/Spawn reset.
void CTerrorPlayer::ResetClassSpawnStatistics()
{
	for ( int index = 0; index < 9; ++index )
	{
		m_zombieClassDeathTimestamp[index] = gpGlobals->curtime;
		if ( m_classSpawnCount[index] != 0 )
			m_classSpawnCount.Set( index, 0 );
	}
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif


// S3048B0 + S301C30. Actual client-avatar reservations, NOT current team,
// liveness, SurvivorBot occupancy or current m_survivorCharacter snapshots.
static void MarkReservedSurvivorAvatars( bool available[4] )
{
	for ( int index = 1; index <= gpGlobals->maxClients; ++index )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( index );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() )
			continue;
		int team = TEAM_UNASSIGNED;
		SurvivorCharacterType character = static_cast< SurvivorCharacterType >( 4 );
		KeyValues *info = TheDirector->GetPlayerAvatarInfo( player->edict() );
		CDirector::DecodePlayerAvatarInfo( info, &team, &character );
		if ( team == TEAM_SURVIVOR && character < 4 )
			available[character] = false;
	}
}

// S307770..3078CC. GetPlayerAvatarInfo returns the BORROWED existing map value.
// Mutate that value in place, then MakeCopy ONLY for real engine ownership transfer.
void CTerrorPlayer::UpdateTeamDesired()
{
	if ( IsBot() )
		return;
	int previousTeam = TEAM_UNASSIGNED;
	SurvivorCharacterType previousCharacter = SurvivorCharacter_Unknown;
	KeyValues *info = TheDirector->GetPlayerAvatarInfo( edict() );
	CDirector::DecodePlayerAvatarInfo( info, &previousTeam, &previousCharacter );
	int team = GetTeamNumber();
	int character = GetSurvivorCharacter();
	if ( TheDirector->AreTeamsFlipped() )
	{
		if ( team == TEAM_SURVIVOR )
			team = TEAM_INFECTED;
		else if ( team == TEAM_INFECTED )
		{
			team = TEAM_SURVIVOR;
			bool available[4] = { true, true, true, true };
			MarkReservedSurvivorAvatars( available );
			character = SurvivorCharacter_Unknown;
			for ( int i = 0; i < 4; ++i )
			{
				if ( available[i] )
				{
					character = i;
					break;
				}
			}
		}
	}
	if ( ( team != previousTeam || character != previousCharacter ) && info )
	{
		CTeam *selectedTeam = GetGlobalTeam( team );
		// S1EB040's genuine missing-team result, not a fabricated admission fallback.
		info->SetString( "team", selectedTeam ? selectedTeam->GetName() : "UNKNOWN TEAM" );
		if ( team != TEAM_INFECTED )
		{
			if ( team != TEAM_SURVIVOR || character == SurvivorCharacter_Unknown )
			{
				// Native mutates the borrowed TEAM value BEFORE this return.
				DevWarning( "CTerrorPlayer::UpdateTeamDesired error ( team %d : character %d )\n", team, character );
				return;
			}
			info->SetString( "avatar", ::GetResponseCharacterName( character ) );
		}
		if ( !IsX360() )
			engine->ClientCommandKeyValues( edict(), info->MakeCopy() );
	}
}

static ConVar z_state_debug( "z_state_debug", "0", 0x00004004,
	"Gives feedback on current zombie state." ); // S59C0E0
static ConVar z_show_last_area( "z_show_last_area", "0", 0x00004004,
	"Draws a line from each player to the center of their last known nav area" ); // S59B260
static ConVar survivor_debug_in_combat( "survivor_debug_in_combat", "0", 0x00004004 ); // S59B2F0

// S311D10..311DEA. State11932 is not class11192. Read the state again after
// starting its interval, then invoke the real class-specific callbacks.
void CTerrorPlayer::SetZombieState( int state )
{
	if ( m_zombieState == state )
		return;

	m_zombieState.GetForModify() = state;
	m_zombieStateTimer.Start();
	switch ( m_zombieState.Get() )
	{
	case 0:
		if ( z_state_debug.GetInt() )
			DevMsg( "STATE: Player %s going normal.\n", GetPlayerName() );
		RecalculateMaxSpeed();
		break;
	case 1:
		OnZombieAlert();
		break;
	case 2:
		OnZombieEngaged();
		break;
	}
}

// S301090..30127D. Each reached recognition consumes one float RNG draw,
// stored to float before the sound call. Preserve the repeated team/class gates.
void CTerrorPlayer::OnZombieAlert()
{
	if ( z_state_debug.GetInt() )
		DevMsg( "STATE: Player %s going Alert.\n", GetPlayerName() );
	RecalculateMaxSpeed();
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
	{
		if ( !IsIncapacitated() && !( GetFlags() & FL_ONFIRE ) )
		{
			const float interval = RandomFloat( 3.0f, 5.0f );
			EmitInfectedSound( "HulkZombie.Yell", interval, 0.0f );
		}
	}
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 1 )
	{
		const float interval = RandomFloat( 3.0f, 5.0f );
		EmitInfectedSound( "SmokerZombie.Recognize", interval, 0.0f );
	}
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 6 )
	{
		const float interval = RandomFloat( 3.0f, 5.0f );
		EmitInfectedSound( "ChargerZombie.Recognize", interval, 0.0f );
	}
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 4 )
	{
		const float interval = RandomFloat( 3.0f, 5.0f );
		EmitInfectedSound( "SpitterZombie.Recognize", interval, 0.0f );
	}
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 2 )
	{
		const float interval = RandomFloat( 3.0f, 5.0f );
		EmitInfectedSound( "BoomerZombie.Rage", interval, 0.0f );
	}
}

// S301280..3012B4.
void CTerrorPlayer::OnZombieEngaged()
{
	if ( z_state_debug.GetInt() )
		DevMsg( "STATE: Player %s going ENGAGED.\n", GetPlayerName() );
	RecalculateMaxSpeed();
}

// S31D9C0..31DD67. The tank event timer is consumed before its eligibility
// checks. The update timer tests raw now>=deadline, without HasStarted().
void CTerrorPlayer::UpdateZombieState()
{
	if ( GetTeamNumber() != TEAM_INFECTED )
		return;

	if ( m_spawnedAsTankTimer.m_timestamp.Get() > 0.0f &&
		static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_spawnedAsTankTimer.m_timestamp.Get() ) )
	{
		m_spawnedAsTankTimer.Invalidate();
		if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 && IsAlive() )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "spawned_as_tank" );
			if ( event )
			{
				event->SetInt( "userid", GetUserID() );
				gameeventmanager->FireEvent( event );
			}
		}
	}
	if ( !( static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_zombieStateUpdateTimer.m_timestamp.Get() ) ) ||
		!IsAlive() || IsGhost() )
		return;

	// S2E670: compare the unspilled deadline, store its float spill, then
	// notify/store duration. The generic float-add Start would narrow too soon.
	const double deadline = static_cast< double >( gpGlobals->curtime ) + 0.25;
	const float storedDeadline = static_cast< float >( deadline );
	if ( static_cast< double >( m_zombieStateUpdateTimer.m_timestamp.Get() ) != deadline )
		m_zombieStateUpdateTimer.m_timestamp.GetForModify() = storedDeadline;
	if ( m_zombieStateUpdateTimer.m_duration.Get() != 0.25f )
		m_zombieStateUpdateTimer.m_duration.GetForModify() = 0.25f;

	switch ( m_zombieState.Get() )
	{
	case 2:
		if ( z_state_debug.GetInt() )
		{
			const char *debugName = GetPlayerName();
			const int debugLine = entindex() + 12;
			engine->Con_NPrintf( debugLine, "Player %s, state ENGAGED.", debugName );
		}
		if ( GetTimeSinceEnemySeen() > 25.0 )
			SetZombieState( 0 );
		break;
	case 1:
		{
			if ( z_state_debug.GetInt() )
			{
				const char *debugName = GetPlayerName();
				const int debugLine = entindex() + 12;
				engine->Con_NPrintf( debugLine, "Player %s, state Alert.", debugName );
			}
			bool mayReturnToNormal = true;
			if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 2 )
			{
				// S28FFB0 snapshots this actor's WorldSpaceCenter. The existing
				// selector has the same true/true exclusions; its ignored source
				// is infected and cannot be a team2 candidate.
				CTerrorPlayer *closest = FindClosestSurvivorPlayer( WorldSpaceCenter(), true, true );
				if ( closest )
				{
					const Vector &ownOrigin = GetAbsOrigin();
					const Vector &theirOrigin = closest->GetAbsOrigin();
					Vector direction = theirOrigin - ownOrigin;
					VectorNormalize( direction );
					// Do not replace by !(z<threshold): unordered is NOT admitted.
					mayReturnToNormal = direction.z >= -0.70709997f;
				}
			}
			if ( mayReturnToNormal && GetTimeSinceEnemySeen() > 5.0 )
				SetZombieState( 0 );
		}
		break;
	case 0:
		{
			if ( z_state_debug.GetInt() )
			{
				const char *debugName = GetPlayerName();
				const int debugLine = entindex() + 12;
				engine->Con_NPrintf( debugLine, "Player %s, state normal.", debugName );
			}
			if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 2 )
			{
				CTerrorPlayer *closest = FindClosestSurvivorPlayer( WorldSpaceCenter(), true, true );
				if ( closest )
				{
					const Vector &ownOrigin = GetAbsOrigin();
					const Vector &theirOrigin = closest->GetAbsOrigin();
					Vector direction = theirOrigin - ownOrigin;
					VectorNormalize( direction );
					if ( direction.z < -0.70709997f )
						SetZombieState( 1 );
				}
			}
			// Independent of the preceding transition and its callback/RNG.
			if ( HasSeenEnemy() && GetTimeSinceEnemySeen() < 1.0 )
				SetZombieState( 1 );
		}
		break;
	}
}

// S306BD0..306D3F. This is NOT the S2FC3F0 console-command clock update.
// The native last EAX value is not consumed by its callers; descriptive void API.
void CTerrorPlayer::GoIdle()
{
	const SurvivorCharacterType oldCharacter = GetSurvivorCharacter();
	HandleCommand_JoinTeam( TEAM_SPECTATOR, SurvivorCharacter_Unknown, false );
	CTerrorPlayer *matchingBot = NULL;
	if ( oldCharacter != SurvivorCharacter_Unknown )
	{
		// Genuine sole shared S350D40/C2864E0 owner, cached survivor set.
		const SurvivorCharacterType findCharacter = static_cast< SurvivorCharacterType >( MapTerrorSurvivorCharacter( oldCharacter ) );
		matchingBot = FindSurvivorBotForCharacter( findCharacter );
	}
	if ( matchingBot )
	{
		SurvivorBot *bot = static_cast< SurvivorBot * >( matchingBot );
		bot->SetHumanSpectator( this );
		SetObserverTarget( bot );
		if ( GetObserverMode() == OBS_MODE_ROAMING )
			SetObserverMode( OBS_MODE_CHASE );
		KeyValues *data = new KeyValues( "data" );
		// Deliberately the ORIGINAL character, not the remapped bot character.
		data->SetInt( "character", oldCharacter );
		ShowViewPortPanel( "takeover_survivor_bar", true, data );
		data->deleteThis();
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "player_afk" );
	if ( event )
	{
		event->SetInt( "player", GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	m_idleTimer.Invalidate();
}

// S32AC70..32AF9E. The genuine CCS frame entry owns CBasePlayer::PreThink,
// activity sampling, game-over gate, speed/state/hints/forced buttons/autokick.
void CTerrorPlayer::PreThink()
{
	UpdateZombieState();
	BaseClass::PreThink();
	if ( GetLastKnownArea() && z_show_last_area.GetInt() && IsAlive() )
	{
		CNavArea *area = GetLastKnownArea();
		const Vector &origin = GetAbsOrigin();
		NDebugOverlay::Line( origin, area->GetCenter(), 0, 255, 0, true, 0.1f );
	}
	if ( survivor_debug_in_combat.GetInt() && IsInCombat( false ) )
		NDebugOverlay::Circle( EyePosition(), 10.0f, 255, 0, 0, 255, true, 0.1f );

	if ( !IsBot() )
	{
		if ( m_idleTimer.m_timestamp.Get() > 0.0f &&
			static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_idleTimer.m_timestamp.Get() ) )
			GoIdle();
		else
		{
			static ConVarRef director_afk_timeout( "director_afk_timeout" );
			if ( !TerrorGameRules()->IsPlayerControlledZombies() && !TheDirector->IsFinaleVehicleLeaving() &&
				!TheDirector->IsFinaleWon() && IsIdle() && GetTeamNumber() == TEAM_SURVIVOR && IsAlive() &&
				( director_afk_timeout.GetInt() < 0 ||
				  ( !TheDirector->AreSurvivorBotsDisabled() && TheDirector->CountPlayersOnTeam( TEAM_SURVIVOR, true ) > 1 ) ) )
			{
				UTIL_ClientPrintAll( HUD_PRINTTALK, "#L4D_idle_spectator", GetPlayerName() );
				GoIdle();
			}
		}
	}
	if ( IsAlive() && GetMoveType() == MOVETYPE_LADDER && ( m_nButtons & ( IN_ATTACK | IN_ATTACK2 ) ) )
	{
		const Vector &firstOrigin = GetAbsOrigin();
		Vector end( firstOrigin.x, firstOrigin.y, firstOrigin.z - 100.0f );
		const Vector &start = GetAbsOrigin();
		const unsigned int mask = PlayerSolidMask( false );
		trace_t trace;
		UTIL_TraceEntity( this, start, end, mask, &trace );
		if ( ( trace.fraction < 1.0f || trace.allsolid || trace.startsolid ) && trace.plane.normal.z > 0.7f )
		{
			SetMoveType( MOVETYPE_WALK, MOVECOLLIDE_DEFAULT );
			m_nButtons &= ~( IN_ATTACK | IN_ATTACK2 );
			UpdateWeaponVisibility();
		}
	}
	const float flashlightAt = m_delayedFlashlightUpdateTime;
	if ( flashlightAt > 0.0f && gpGlobals->curtime >= flashlightAt )
	{
		const int team = GetTeamNumber();
		if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
			FlashlightTurnOn( false );
		else
			RemoveEffects( EF_DIMLIGHT );
		m_delayedFlashlightUpdateTime = 0.0f;
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

ConVar survivor_respawn_with_guns( "survivor_respawn_with_guns", "1", 0x00000006,
	"0: Just a pistol, 1: Downgrade of last primary weapon, 2: Last primary weapon." );

void CTerrorPlayer::RestorePreviousWeapons()
{
	if ( m_bRespawningFromDefibrillator )
		return;

	int mode = survivor_respawn_with_guns.GetInt();
	int ammo = m_lastPrimaryWeaponAmmo;
	if ( TerrorGameRules()->IsScavengeMode() || TerrorGameRules()->IsSurvivalMode() )
	{
		ammo = -1;
		mode = 0;
	}
	else if ( TheDirector->ShouldResetTeamsOnMissionStart() )
	{
		mode = 0;
	}

	const int secondary = m_lastSecondaryWeaponId;
	if ( mode == 2 )
	{
		if ( secondary == 1 && m_lastHadElites )
		{
			GiveNamedItemInternal( "weapon_pistol", 0, true, NULL );
		}
		else if ( secondary != 0 )
		{
			// Native also gives this pistol when !m_lastHadElites. Do not repair it.
			GiveNamedItemInternal( WeaponIDToClassname( secondary ), 0, true, NULL );
		}
	}

	unsigned int weaponID = static_cast< unsigned int >( m_lastPrimaryWeaponId );
	if ( weaponID == 0 )
		return;
	if ( mode == 1 )
	{
		const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( weaponID );
		if ( info && ( info->m_iTier == 2 || weaponID == 21 ) )
		{
			int type = info->m_WeaponType;
			if ( type == 2 || type == 4 )
				type = 1;
			if ( weaponID == 21 )
				type = 3;
			for ( int candidate = 1; candidate < 45; ++candidate )
			{
				const CTerrorWeaponInfo *candidateInfo = GetTerrorWeaponInfo( candidate );
				if ( candidateInfo && candidateInfo->m_WeaponType == type && candidateInfo->m_iTier == 1 )
				{
					weaponID = candidate;
					ammo = -1;
					break;
				}
			}
		}
	}
	else if ( mode == 0 )
	{
		return;
	}

	// Modes other than 0/1/2 intentionally retain native primary-only behavior.
	CBaseCombatWeapon *weapon = static_cast< CBaseCombatWeapon * >(
		GiveNamedItemInternal( UTIL_VarArgs( "weapon_%s", WeaponIDToAlias( weaponID ) ), 0, true, NULL ) );
	if ( weapon && ammo != -1 )
	{
		const int ammoType = weapon->GetPrimaryAmmoType();
		if ( ammoType != -1 )
			SetAmmoCount( m_lastPrimaryWeaponAmmo, ammoType );
		// Death's primary-slot producer guarantees the original TerrorGun owner.
		// This is DT_TerrorGun+6110, not BaseCombatWeapon's reload state.
		CTerrorGun *gun = static_cast< CTerrorGun * >( weapon );
		if ( gun->m_inInitialPickup )
			gun->m_inInitialPickup = false;
	}
}

void CTerrorPlayer::HandleAnimEvent( animevent_t *event )
{
	if ( !( event->type & AE_TYPE_NEWEVENTSYSTEM ) || !( event->type & AE_TYPE_SERVER ) )
	{
		BaseClass::HandleAnimEvent( event );
		return;
	}
	if ( event->type & AE_TYPE_WEAPON )
	{
		Weapon_HandleAnimEvent( event );
		return;
	}
	// Native never assigns a valid entity to this handle. Keep the real gate;
	// ctor, ClearTug and Spawn are its ONLY three invalidating owners.
	if ( ( event->event == 69 && m_hTugSource.Get() ) || event->event == 56 )
		return;

	switch ( event->event )
	{
	case 70:
		OnChargerPound();
		return;
	case 71:
		EmitSound( "ChargerZombie.VocalizePummel", 0.0f );
		return;
	case 72:
		EmitSound( "ChargerZombie.Pummel", 0.0f );
		return;
	default:
		break;
	}
	// Two distinct original calls, S302F26/S302F2E; do not deduplicate.
	BaseClass::HandleAnimEvent( event );
	BaseClass::HandleAnimEvent( event );
}

// S3201C0. Class/model/health/claw setup; ability creation is the separate
// later phase of the complete native3269D0 Spawn, not a bot-local bootstrap.
void CTerrorPlayer::SetZombieClass( int zombieClass )
{
	if ( GetTeamNumber() != TEAM_INFECTED )
		return;

	++s_zombieClassSpawnSerial;
	if ( s_zombieClassSpawnSerial > 1024 )
		s_zombieClassSpawnSerial = 1;
	m_zombieClassSerial = s_zombieClassSpawnSerial; // Native11196, not bullet serial.
	m_zombieClass = zombieClass;
	if ( m_pHintMessageQueue )
		m_pHintMessageQueue->Reset();
	SetRenderMode( kRenderNormal );
	m_nRenderFX = kRenderFxNone;
	SetRenderColor( 255, 255, 255 );
	SetRenderColorA( 255 );
	SetModelFromClass();

	ConVar *health = NULL;
	const char *claw = NULL;
	switch ( zombieClass )
	{
	case 1: health = &z_gas_health; claw = "weapon_smoker_claw"; break;
	case 2: health = &z_exploding_health; claw = "weapon_boomer_claw"; break;
	case 3: health = &z_hunter_health; claw = "weapon_hunter_claw"; break;
	case 4: health = &z_spitter_health; claw = "weapon_spitter_claw"; break;
	case 5: health = &z_jockey_health; claw = "weapon_jockey_claw"; break;
	case 6: health = &z_charger_health; claw = "weapon_charger_claw"; break;
	case 8:
		SetHealth( _mm_cvttsd_si32( _mm_set_sd( TheDirector->GetTankHealth() ) ) ); // S320383 -> SSE2 __ftol2_sse, double spill then CVTTSD2SI.
		// Native keeps the health reference live across max-health dirtying.
		if ( m_iMaxHealth.Get() != m_iHealth.Get() )
		{
			int &maximum = m_iMaxHealth.GetForModify();
			maximum = m_iHealth.Get(); // S3203CF reloads health AFTER max-health dirty notification.
		}
		GiveNamedItemInternal( "weapon_tank_claw", 0, true, NULL );
		break;
	}
	if ( health )
	{
		// Separate native reads straddle the health-change callback.
		SetHealth( _mm_cvttss_si32( _mm_set_ss( health->GetFloat() ) ) );
		SetMaxHealth( _mm_cvttss_si32( _mm_set_ss( health->GetFloat() ) ) );
		GiveNamedItemInternal( claw, 0, true, NULL );
	}
	if ( m_bReusingInfectedSpawnState && m_reusedInfectedSpawnHealth > 0 )
		m_iHealth = m_reusedInfectedSpawnHealth;
	TheDirector->OnPlayerSpawned( this );
	if ( GetZombieClass() == 8 )
	{
		const double deadline = static_cast< double >( gpGlobals->curtime ) + 2.0;
		const float storedDeadline = static_cast< float >( deadline );
		if ( static_cast< double >( m_vocalizeTimer.m_timestamp.Get() ) != deadline )
			m_vocalizeTimer.m_timestamp.GetForModify() = storedDeadline;
		if ( m_vocalizeTimer.m_duration.Get() != 2.0f )
			m_vocalizeTimer.m_duration.GetForModify() = 2.0f;
	}
}

// Full native2D1830 reached PLAYER census, shared by the two debug displays and
// existing SelectNextZombieClass. No second persistent population/stat model.
TerrorInfectedClassCounts::TerrorInfectedClassCounts( bool includeDeadPlayers )
{
	tanks = witches = commons = 0;
	Q_memset( special, 0, sizeof( special ) );
	Q_memset( living, 0, sizeof( living ) );
	includeDead = includeDeadPlayers;
}
void TerrorInfectedClassCounts::Count( int team )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player || !player->edict() || player->entindex() == 0 || !player->IsPlayer() ||
			!player->IsConnected() || ( team != -1 && player->GetTeamNumber() != team ) ) continue;
		if ( ( !player->IsAlive() && !includeDead ) || player->GetTeamNumber() != TEAM_INFECTED ) continue;
		if ( !player->IsPlayer() ) continue; // Repeated native player query.
		const int zombieClass = static_cast< CTerrorPlayer * >( player )->GetZombieClass();
		if ( (unsigned int)zombieClass - 1u <= 5u )
		{
			++special[zombieClass - 1];
			if ( player->IsAlive() ) ++living[zombieClass - 1];
		}
		else if ( zombieClass == 7 ) ++witches;
		else if ( zombieClass == 8 ) ++tanks;
		else if ( zombieClass == 0 ) ++commons;
	}
}

// S305850. Preserve the native byte==1 gate, seed-minus-one signed remainder,
// six repeated live censuses, Hunter fallback and the literal99999 best count.
int CTerrorPlayer::SelectNextZombieClass( unsigned char enforceClassLimits )
{
	static ConVar *limits[6] =
	{
		&z_versus_smoker_limit, &z_versus_boomer_limit, &z_versus_hunter_limit,
		&z_versus_spitter_limit, &z_versus_jockey_limit, &z_versus_charger_limit
	};
	static const bool pinningClass[6] = { true, false, true, false, true, true };
	const int first = TheDirector->GetSpecialClassRotationSeed() - 1;
	int selected = 3;
	int bestCount = 99999;
	for ( int i = 0; i < 6; ++i )
	{
		const int index = ( i + first ) % 6;
		const int zombieClass = index + 1;
		const int count = m_classSpawnCount[zombieClass];
		if ( count >= bestCount )
			continue;
		if ( enforceClassLimits == 1 && (unsigned int)index <= 5 && limits[index] )
		{
			TerrorInfectedClassCounts counts( true );
			counts.Count( TEAM_INFECTED );
			if ( counts.special[index] >= limits[index]->GetInt() )
				continue;
			if ( pinningClass[index] )
			{
				int pinning = 0;
				for ( int j = 0; j < 6; ++j )
					if ( pinningClass[j] ) pinning += counts.living[j];
				if ( pinning >= 3 )
					continue;
			}
		}
		selected = zombieClass;
		bestCount = count;
	}
	return selected;
}

// Native server 0x2fa520; 0x5c3e8c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace playerPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "player", 0x5c3e8c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "player", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#include "l4d2/player/terrorplayer_console_server_callbacks.inc"
