//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "tier0/memdbgon.h"

ConVar tongue_bend_point_needs_LOS( "tongue_bend_point_needs_LOS", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Does a bent tongue still need LOS from the bend point?" );
ConVar tongue_gravity_force( "tongue_gravity_force", "4000", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "The speed that gravity tries to pull us downwards while being tongued." );
ConVar tongue_hit_delay( "tongue_hit_delay", "20", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "How long a smoker must wait to shoot his tongue after a hit, from the time he lets go." );
ConVar tongue_miss_delay( "tongue_miss_delay", "15", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "How long a smoker must wait to shoot his tongue after a miss." );
ConVar tongue_start_pull_delay( "tongue_start_pull_delay", "0.1", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "How long from tongue contact to tongue pulling." );
ConVar tongue_victim_acceleration( "tongue_victim_acceleration", "30", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Acceleration while tongued." );
ConVar tongue_victim_accuracy_penalty( "tongue_victim_accuracy_penalty", "0.133", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "How much someone's accuracy suffers while being dragged by a tongue." );
ConVar tongue_victim_max_speed( "tongue_victim_max_speed", "175", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "The fastest the tongue can get you going." );

#ifndef CLIENT_DLL
ConVar tongue_allow_voluntary_release( "tongue_allow_voluntary_release", "0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "Can a Smoker let go with his tongue by clicking or turning away?" );
ConVar tongue_bend_point_deflection( "tongue_bend_point_deflection", "5", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How far off the first obstacle the tongue bends." );
ConVar tongue_break_from_damage_amount( "tongue_break_from_damage_amount", "50", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How much damage to the smoker makes him let go of his victim." );
ConVar tongue_bullet_radius( "tongue_bullet_radius", "6", 
#if defined( GAME_DLL )
	0x00000006
#else
	0
#endif
 );
ConVar tongue_choke_damage_amount( "tongue_choke_damage_amount", "10", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How much damage the choke does." );
ConVar tongue_choke_damage_interval( "tongue_choke_damage_interval", "1", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How often the choke does damage." );
ConVar tongue_cone_start_tolerance( "tongue_cone_start_tolerance", ".1", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How wide the cone is for a tongue hit." );
ConVar tongue_debug( "tongue_debug", "0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "Print debug info for tongue" );
ConVar tongue_drag_damage_amount( "tongue_drag_damage_amount", "3", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How much damage the tongue drag does." );
ConVar tongue_dropping_to_ground_time( "tongue_dropping_to_ground_time", "2", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "A miss or a wall hit will wait this long before pulling back." );
ConVar tongue_fly_speed( "tongue_fly_speed", "1000", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How fast a tongue flies through the air." );
ConVar tongue_force_break( "tongue_force_break", "0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "Force an existing attached tongue to break, for debugging" );
ConVar tongue_health( "tongue_health", "100", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "Tongue health" );
ConVar tongue_los_forgiveness_time( "tongue_los_forgiveness_time", "1.0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "A traveling tongue can lose LOS for this amount of time and still hit." );
ConVar tongue_no_progress_break_interval( "tongue_no_progress_break_interval", "10", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How long of the victim making no progress until we break the tongue." );
ConVar tongue_no_progress_choke_early_ambush_delay( "tongue_no_progress_choke_early_ambush_delay", "0.5", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "Use a smaller delay if the smoker started choking us from behind. So this plus TongueNoProgressChokeTime equals the earliest start of a ground choke in the event of an ambush." );
ConVar tongue_no_progress_choke_early_delay( "tongue_no_progress_choke_early_delay", "1.5", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "We won't think about ground choking for this long after the hit. So this plus TongueNoProgressChokeTime equals the earliest start of a ground choke." );
ConVar tongue_no_progress_choke_time( "tongue_no_progress_choke_time", ".5", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "If our victim doesn't make tongue_no_progress_tolerance progress towards in this time, start to hurt him." );
ConVar tongue_no_progress_damage_interval( "tongue_no_progress_damage_interval", ".5", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How long of the victim making no progress until we start choking him." );
ConVar tongue_no_progress_tolerance( "tongue_no_progress_tolerance", "25", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "If our victim doesn't make this much progress in tongue_no_progress_release_time, start to hurt him." );
ConVar tongue_player_dropping_to_ground_time( "tongue_player_dropping_to_ground_time", "1", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How long after the tongue disconnects will a player need to wait." );
ConVar tongue_range( "tongue_range", "750", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How far a smoker can shoot his tongue." );
ConVar tongue_unbend( "tongue_unbend", "1", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "Can the smoker tongue unbend?" );
ConVar tongue_vertical_choke_dot( "tongue_vertical_choke_dot", "0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "DotProduct between tongue and vertical required to start choking." );
ConVar tongue_vertical_choke_height( "tongue_vertical_choke_height", "40", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "Need to have victim this high off ground to choke him." );
ConVar tongue_vertical_choke_time_off_ground( "tongue_vertical_choke_time_off_ground", ".5", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "Need to have victim off ground for this long to choke him." );
ConVar tongue_vs_cone_start_tolerance( "tongue_vs_cone_start_tolerance", "0.015", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How wide the cone is for a tongue hit in versus." );
ConVar tongue_release_fatigue_penalty( "tongue_release_fatigue_penalty", "2500", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "How much fatigue the victim gets when released, to slow him down." );
#endif
