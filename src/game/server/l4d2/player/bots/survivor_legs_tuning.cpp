//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_legs_tuning.h"
#include "tier0/memdbgon.h"

// Original server initializers 105A2840 through 105A2AA0.
ConVar sb_max_battlestation_range_from_human( "sb_max_battlestation_range_from_human", "750", 0x00004004 );
ConVar sb_battlestation_give_up_range_from_human( "sb_battlestation_give_up_range_from_human", "1500", 0x00004004 );
ConVar sb_battlestation_human_hold_time( "sb_battlestation_human_hold_time", "4", 0x00004004,
	"How long the nearest human must hold their place before SurvivorBots will re-evaluate their Battlestations" );
ConVar sb_rescue_vehicle_loading_range( "sb_rescue_vehicle_loading_range", "300", 0x00004004,
	"How close to the arrival point of the rescue vehicle SurvivorBots try to get" );
ConVar sb_debug_retreat( "sb_debug_retreat", "0", 0x00004004 );
ConVar sb_separation_range( "sb_separation_range", "200", 0x00004004, "Desired distance between Survivors" );
ConVar sb_separation_danger_min_range( "sb_separation_danger_min_range", "200", 0x00004004,
	"A Survivor teammate this far away is straying from the group" );
ConVar sb_separation_danger_max_range( "sb_separation_danger_max_range", "600", 0x00004004,
	"A Survivor teammate this far away needs to be gathered back into the group" );
ConVar sb_allow_leading( "sb_allow_leading", "0", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL,
	"If nonzero, SurvivorBots can take the lead and won't always wait behind the lead human player" );
ConVar sb_path_lookahead_range( "sb_path_lookahead_range", "200", 0x00004004 );
ConVar sb_debug_team_avoidance( "sb_debug_team_avoidance", "0", 0x00004004 );
