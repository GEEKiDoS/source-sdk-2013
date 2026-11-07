//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"

#include "tier0/memdbgon.h"

// Sole registrations for the native player damage/life consumers. The life
// transition itself belongs to CTerrorPlayer::OnTankIncapacitated.
ConVar z_tank_health( "z_tank_health", "4000", 0x00004004, "Tank Zombie max health" );
ConVar z_tank_incapacitated_health( "z_tank_incapacitated_health", "5000", 0x00004004, "Health Tank starts with in death throes." );
ConVar z_tank_incapacitated_decay_rate( "z_tank_incapacitated_decay_rate", "1", 0x00004004, "How much health a dying Tank loses each update." );
ConVar tank_burn_duration( "tank_burn_duration", "75", 0x00004004, "Number of seconds a burning Tank takes to die in easy, normal, versus and survival", true, 1.0f, false, 0.0f );
ConVar tank_burn_duration_hard( "tank_burn_duration_hard", "80", 0x00004004, "Number of seconds a burning Tank takes to die in hard", true, 1.0f, false, 0.0f );
ConVar tank_burn_duration_expert( "tank_burn_duration_expert", "85", 0x00004004, "Number of seconds a burning Tank takes to die in expert", true, 1.0f, false, 0.0f );
ConVar z_tank_autoshotgun_dmg_scale( "z_tank_autoshotgun_dmg_scale", "0.85", 0x00004004, "percent damage taken by the tank from the autoshotgun" );
ConVar z_tank_grenade_launcher_dmg_scale( "z_tank_grenade_launcher_dmg_scale", "3.0", 0x00004004, "percent increase in damage done to the tank by grenade launcher" );
ConVar z_tank_damage_slow_min_range( "z_tank_damage_slow_min_range", "200", 0x00004004, "Range at which Tank is slowed by gunfire" );
ConVar z_tank_damage_slow_max_range( "z_tank_damage_slow_max_range", "400", 0x00004004, "Range at which Tank is slowed by gunfire" );
ConVar z_tank_has_special_blood( "z_tank_has_special_blood", "0", 0x00004004, "Does the Tank get his own blood effect instead of the normal infected one?" );
ConVar vs_tank_damage( "vs_tank_damage", "24", 0x00004004, "Amount of damage done by a vs tank's melee attack" );
