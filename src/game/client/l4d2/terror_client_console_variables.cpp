// Native L4D2 client console-only definitions. Ledger294: registration is observable.
// Effective constructor flags are explicit: the SDK does not add native DEVELOPMENTONLY.
#include "cbase.h"
static void SurvivorViewmodelFOVChanged( IConVar *var, const char *, float oldValue )
{
	DevMsg( "debugfov: Changing cl_viewmodelfovsurvivor from %f to %f\n", oldValue, static_cast< ConVar * >( var )->GetFloat() );
}
#include "tier0/memdbgon.h"

// client:console:1 object 0x007dc970 ctor 0x0051b794
ConVar BlendBonesMode( "BlendBonesMode", "2", 0x200a, "" );

// client:console:2 object 0x0072c8a8 ctor 0x004fb7c1
ConVar IgnoreReflectGlassFrustumCheck( "IgnoreReflectGlassFrustumCheck", "0", 0xa, "" );

// client:console:5 object 0x0071fa08 ctor 0x004f8989
ConVar achievement_disable( "achievement_disable", "0", 0x6008, "Turn off achievements." );

// client:console:15 object 0x007c2270 ctor 0x00512624
ConVar ammo_ammo_pack_max( "ammo_ammo_pack_max", "1", 0x200a, "" );

// client:console:18 object 0x007c2300 ctor 0x00512684
ConVar ammo_chainsaw_max( "ammo_chainsaw_max", "20", 0x200a, "" );

// client:console:25 object 0x007cdaf8 ctor 0x00518354
ConVar ammo_pack_use_duration( "ammo_pack_use_duration", "3", 0x6008, "" );

// client:console:32 object 0x007c4a80 ctor 0x005136f9
ConVar ammo_turret_infected_damage( "ammo_turret_infected_damage", "15", 0x200a, "Turret damage per shot against the horde (turret fires at 10Hz)" );

// client:console:34 object 0x007c49a8 ctor 0x00513669
ConVar ammo_turret_pz_damage( "ammo_turret_pz_damage", "8", 0x200a, "Turret damage per shot against player zombies (turret fires at 10Hz)" );

// client:console:35 object 0x007c4960 ctor 0x00513639
ConVar ammo_turret_survivor_damage( "ammo_turret_survivor_damage", "1", 0x200a, "Turret damage per shot against survivors (turret fires at 10Hz)" );

// client:console:36 object 0x007c49f0 ctor 0x00513699
ConVar ammo_turret_tank_damage( "ammo_turret_tank_damage", "40", 0x200a, "Turret damage per shot against tanks (turret fires at 10Hz)" );

// client:console:37 object 0x007c4a38 ctor 0x005136c9
ConVar ammo_turret_witch_damage( "ammo_turret_witch_damage", "16", 0x200a, "Turret damage per shot against witches (turret fires at 10Hz)" );

// client:console:46 object 0x00752b08 ctor 0x004fdc49
ConVar bot_mimic_spec_buttons( "bot_mimic_spec_buttons", "1", 0x4008, "+attack, +jump etc are used for spectator control instead of being passed on to spectated bot" );

// client:console:48 object 0x007cbfc8 ctor 0x00517104
ConVar bullet_ff_through_walls( "bullet_ff_through_walls", "0", 0x200a, "" );

// client:console:57 object 0x007589c8 ctor 0x00500664
ConVar c_thirdpersonshoulder( "c_thirdpersonshoulder", "false", 0x88, "" );

// client:console:58 object 0x00758ae8 ctor 0x00500724
ConVar c_thirdpersonshoulderaimdist( "c_thirdpersonshoulderaimdist", "120.0", 0x88, "" );

// client:console:59 object 0x00758a58 ctor 0x005006c4
ConVar c_thirdpersonshoulderdist( "c_thirdpersonshoulderdist", "40.0", 0x88, "" );

// client:console:60 object 0x00758aa0 ctor 0x005006f4
ConVar c_thirdpersonshoulderheight( "c_thirdpersonshoulderheight", "5.0", 0x88, "" );

// client:console:61 object 0x00758a10 ctor 0x00500694
ConVar c_thirdpersonshoulderoffset( "c_thirdpersonshoulderoffset", "20.0", 0x88, "" );

// client:console:76 object 0x0079f978 ctor 0x00505709
ConVar cc_showmissing( "cc_showmissing", "0", 0x200a, "Show missing closecaption entries." );

// client:console:84 object 0x007b4f80 ctor 0x0050ef51
ConVar cl_addon_debug( "cl_addon_debug", "0", 0xa, "" );

// client:console:89 object 0x00759b40 ctor 0x00501419
ConVar cl_autoaim( "cl_autoaim", "1", 0x8288, "Set to 1 to use autoaim on joysticks, 0 to disable" );

// client:console:90 object 0x007b2210 ctor 0x0050d4b9
ConVar cl_autohelp( "cl_autohelp", "1", 0x288, "Auto-help" );

// client:console:91 object 0x007a4568 ctor 0x005080d6
ConVar cl_blurClearAlpha( "cl_blurClearAlpha", "0", 0xa, "0-255, but 0 has errors at the moment" );

// client:console:92 object 0x007a45b0 ctor 0x00508101
ConVar cl_blurDebug( "cl_blurDebug", "0", 0xa, "" );

// client:console:93 object 0x007a4640 ctor 0x00508161
ConVar cl_blurPasses( "cl_blurPasses", "1", 0xa, "" );

// client:console:94 object 0x007a45f8 ctor 0x00508131
ConVar cl_blurTapSize( "cl_blurTapSize", "0.5", 0xa, "" );

// client:console:99 object 0x007b0348 ctor 0x0050cb99
ConVar cl_buy_favorite_nowarn( "cl_buy_favorite_nowarn", "0", 0x40000088, "Skips the error prompt when saving an invalid buy favorite" );

// client:console:100 object 0x007b0300 ctor 0x0050cb69
ConVar cl_buy_favorite_quiet( "cl_buy_favorite_quiet", "0", 0x40000088, "Skips the prompt when saving a buy favorite in the buy menu" );

// client:console:101 object 0x007b1b10 ctor 0x0050d0e6
ConVar cl_c4dynamiclight( "cl_c4dynamiclight", "0", 0xa, "Draw dynamic light when planted c4 flashes" );

// client:console:103 object 0x007579b8 ctor 0x004ffca1
ConVar cl_chat_active( "cl_chat_active", "0", 0xa, "" );

// client:console:104 object 0x00757a48 ctor 0x004ffd09
ConVar cl_chat_wipehistory( "cl_chat_wipehistory", "0", 0x88, "Wipes chat history after all chat text faded out" );

// client:console:111 object 0x007b1680 ctor 0x0050cda6
ConVar cl_clientmenudistancecap( "cl_clientmenudistancecap", "10", 0xa, "Furthest the client menu will think the mouse has gone." );

// client:console:112 object 0x007b1638 ctor 0x0050cd76
ConVar cl_clientmenuscalefactor( "cl_clientmenuscalefactor", "0.05", 0xa, "Multiplier to the mouse's units to get the client menu's units." );

// client:console:116 object 0x00780070 ctor 0x00503271
ConVar cl_colorfastpath( "cl_colorfastpath", "0", 0xa, "" );

// client:console:126 object 0x007b16c8 ctor 0x0050cdd6
ConVar cl_cycle_latch_tolerance( "cl_cycle_latch_tolerance", "0.125", 0xa, "How close an incoming Cycle reminder can be for us to ignore the input and trust the value we have." );

// client:console:127 object 0x00782008 ctor 0x005046a1
ConVar cl_debug_entity( "cl_debug_entity", "", 0xa, "" );

// client:console:138 object 0x007223d8 ctor 0x004f97b4
ConVar cl_disable_ragdolls( "cl_disable_ragdolls", "0", 0x4008, "" );

// client:console:153 object 0x00781a20 ctor 0x00504341
ConVar cl_flesh_sound_frequency( "cl_flesh_sound_frequency", "0.120", 0xa, "" );

// client:console:156 object 0x00754f48 ctor 0x004fec31
ConVar cl_foundry_ShowEntityHighlights( "cl_foundry_ShowEntityHighlights", "1", 0xa, "" );

// client:console:166 object 0x007c2b98 ctor 0x00512851
ConVar cl_glow_debug( "cl_glow_debug", "0", 0xa, "" );

// client:console:188 object 0x007bb438 ctor 0x00510561
ConVar cl_glow_range_end( "cl_glow_range_end", "1500", 0xa, "" );

// client:console:190 object 0x007bb3f0 ctor 0x00510531
ConVar cl_glow_range_start( "cl_glow_range_start", "100", 0xa, "" );

// client:console:239 object 0x007cc0b8 ctor 0x005171c6
ConVar cl_impacteffects_limit_water( "cl_impacteffects_limit_water", "2", 0xa, "Limits the number of water impact effects per frame" );

// client:console:241 object 0x0075a460 ctor 0x00501749
ConVar cl_inputmode( "cl_inputmode", "0", 0x8218, "" );

// client:console:246 object 0x007248d0 ctor 0x004f9e96
ConVar cl_interp_threadmodeticks( "cl_interp_threadmodeticks", "0", 0xa, "Additional interpolation ticks to use when interpolating with threaded engine mode set." );

// client:console:251 object 0x0075e748 ctor 0x00502d64
ConVar cl_jiggle_bone_invert( "cl_jiggle_bone_invert", "0", 0x4008, "" );

// client:console:252 object 0x0075e790 ctor 0x00502d96
ConVar cl_jiggle_bone_sanity( "cl_jiggle_bone_sanity", "1", 0xa, "Prevent jiggle bones from pointing directly away from their target in case of numerical instability." );

// client:console:253 object 0x0075a418 ctor 0x00501716
ConVar cl_lagcomp_errorcheck( "cl_lagcomp_errorcheck", "0", 0xa, "Player index of other player to check for position errors." );

// client:console:255 object 0x00752740 ctor 0x004fdaf4
ConVar cl_leafsystemvis( "cl_leafsystemvis", "0", 0x4008, "" );

// client:console:256 object 0x007b1488 ctor 0x0050cc56
ConVar cl_left_hand_ik( "cl_left_hand_ik", "0", 0xa, "Attach player's left hand to rifle with IK." );

// client:console:259 object 0x007a5390 ctor 0x00508229
ConVar cl_max_shadow_renderable_dist( "cl_max_shadow_renderable_dist", "3000", 0x4008, "Max distance from the camera at which things will be rendered for shadows" );

// client:console:261 object 0x007a62d0 ctor 0x00508b41
ConVar cl_modelfastpath( "cl_modelfastpath", "1", 0xa, "" );

// client:console:269 object 0x00781108 ctor 0x00503d56
ConVar cl_particle_fallback_base( "cl_particle_fallback_base", "0", 0xa, "Base for falling back to cheaper effects under load." );

// client:console:270 object 0x007810c0 ctor 0x00503d26
ConVar cl_particle_fallback_multiplier( "cl_particle_fallback_multiplier", "1", 0xa, "Multiplier for falling back to cheaper effects under load." );

// client:console:271 object 0x00781348 ctor 0x00503ed1
ConVar cl_particle_max_count( "cl_particle_max_count", "0", 0xa, "" );

// client:console:273 object 0x00781198 ctor 0x00503db6
ConVar cl_particle_sim_fallback_base_multiplier( "cl_particle_sim_fallback_base_multiplier", "5", 0xa, "How aggressive the switch to fallbacks will be depending on how far over the cl_particle_sim_fallback_threshold_ms the sim time is.  Higher numbers are more aggressive." );

// client:console:274 object 0x00781150 ctor 0x00503d86
ConVar cl_particle_sim_fallback_threshold_ms( "cl_particle_sim_fallback_threshold_ms", "6.0", 0xa, "Amount of simulation time that can elapse before new systems start falling back to cheaper versions" );

// client:console:275 object 0x00781078 ctor 0x00503cf4
ConVar cl_particles_show_bbox( "cl_particles_show_bbox", "0", 0x4008, "" );

// client:console:279 object 0x007818a8 ctor 0x00504261
ConVar cl_phys_block_dist( "cl_phys_block_dist", "1.0", 0xa, "" );

// client:console:280 object 0x00781860 ctor 0x00504231
ConVar cl_phys_block_fraction( "cl_phys_block_fraction", "0.1", 0xa, "" );

// client:console:281 object 0x007817d0 ctor 0x005041d6
ConVar cl_phys_maxticks( "cl_phys_maxticks", "0", 0xa, "Sets the max number of physics ticks allowed for client-side physics (ragdolls)" );

// client:console:286 object 0x007bc3c8 ctor 0x00511101
ConVar cl_player_max_decal_count( "cl_player_max_decal_count", "2", 0xa, "" );

// client:console:289 object 0x007aeec0 ctor 0x0050c1f9
ConVar cl_playerspraydisable( "cl_playerspraydisable", "1", 0x88, "Disable player sprays." );

// client:console:290 object 0x007558b0 ctor 0x004ff5d6
ConVar cl_pred_checkstuck( "cl_pred_checkstuck", "0", 0xa, "Perform the additional 'stuck' traces on the client side during prediction." );

// client:console:292 object 0x00782a70 ctor 0x00504b86
ConVar cl_pred_error_verbose( "cl_pred_error_verbose", "0", 0xa, "Show more field info when spewing prediction errors." );

// client:console:297 object 0x00722890 ctor 0x004f9bf1
ConVar cl_ragdoll_default_scale( "cl_ragdoll_default_scale", "1.0", 0xa, "" );

// client:console:298 object 0x007b1560 ctor 0x0050cce6
ConVar cl_ragdoll_force_multiplier( "cl_ragdoll_force_multiplier", "8", 0xa, "Multiplies the damage force applied to new ragdolls" );

// client:console:299 object 0x00781818 ctor 0x00504209
ConVar cl_ragdoll_gravity( "cl_ragdoll_gravity", "386", 0x4008, "Sets the gravity client-side ragdolls" );

// client:console:300 object 0x007b1518 ctor 0x0050ccb9
ConVar cl_ragdoll_limit( "cl_ragdoll_limit", "20", 0x88, "Maximum number of ragdolls to show (-1 disables limit)" );

// client:console:308 object 0x00722848 ctor 0x004f9bc1
ConVar cl_ragdoll_reload( "cl_ragdoll_reload", "0", 0xa, "" );

// client:console:309 object 0x0079f268 ctor 0x00505301
ConVar cl_ragdoll_version( "cl_ragdoll_version", "1", 0xa, "" );

// client:console:317 object 0x00733160 ctor 0x004fc9d6
ConVar cl_sceneentity_debug( "cl_sceneentity_debug", "0", 0xa, "Display all thinking scene entities and its data." );

// client:console:319 object 0x007536c8 ctor 0x004fe1f1
ConVar cl_show_bounds_errors( "cl_show_bounds_errors", "0", 0xa, "" );

// client:console:324 object 0x0071fc80 ctor 0x004f8c89
ConVar cl_showanimstate_activities( "cl_showanimstate_activities", "1", 0x4008, "Show activities in the (client) animation state display." );

// client:console:332 object 0x007dc920 ctor 0x0051b739
ConVar cl_simdbones( "cl_simdbones", "0", 0x200a, "Use SIMD bone setup." );

// client:console:333 object 0x007800b8 ctor 0x005032a9
ConVar cl_skipfastpath( "cl_skipfastpath", "0", 0x4008, "Set to 1 to stop all models that go through the model fast path from rendering" );

// client:console:334 object 0x007a6318 ctor 0x00508b79
ConVar cl_skipslowpath( "cl_skipslowpath", "0", 0x4008, "Set to 1 to skip any models that don't go through the model fast path" );

// client:console:335 object 0x007a8ca8 ctor 0x0050a361
ConVar cl_smoke_alpha( "cl_smoke_alpha", "0.4", 0xa, "" );

// client:console:336 object 0x007a8cf0 ctor 0x0050a3a9
ConVar cl_smoke_color_percent( "cl_smoke_color_percent", "0.5", 0x4008, "", true, 0.0f, true, 1.0f );

// client:console:337 object 0x007a8c60 ctor 0x0050a331
ConVar cl_smoke_far( "cl_smoke_far", "150", 0xa, "" );

// client:console:338 object 0x007a8bd0 ctor 0x0050a2d4
ConVar cl_smoke_fog_percent( "cl_smoke_fog_percent", "0", 0x400a, "" );

// client:console:339 object 0x007a8c18 ctor 0x0050a301
ConVar cl_smoke_near( "cl_smoke_near", "0", 0xa, "" );

// client:console:357 object 0x007360e0 ctor 0x004fd841
ConVar cl_threaded_init( "cl_threaded_init", "0", 0xa, "" );

// client:console:358 object 0x007a63a8 ctor 0x00508bd1
ConVar cl_tlucfastpath( "cl_tlucfastpath", "1", 0xa, "" );

// client:console:359 object 0x007532b8 ctor 0x004fe151
ConVar cl_tracer_whiz_distance( "cl_tracer_whiz_distance", "72", 0xa, "" );

// client:console:363 object 0x007dc9c8 ctor 0x0051b769
ConVar cl_use_simd_bones( "cl_use_simd_bones", "1", 0x200a, "1 use SIMD bones 0 use scalar bones." );

// client:console:372 object 0x007c30a8 ctor 0x00512bb1
ConVar cl_viewmodelfovboomer( "cl_viewmodelfovboomer", "50", 0xa, "" );

// client:console:373 object 0x007c3018 ctor 0x00512b51
ConVar cl_viewmodelfovhunter( "cl_viewmodelfovhunter", "50", 0xa, "" );

// client:console:374 object 0x007c30f0 ctor 0x00512be1
ConVar cl_viewmodelfovsmoker( "cl_viewmodelfovsmoker", "50", 0xa, "" );

// client:console:375 object 0x007c2fd0 ctor 0x00512b2e
ConVar cl_viewmodelfovsurvivor( "cl_viewmodelfovsurvivor", "51", 0x80088, "", SurvivorViewmodelFOVChanged );

// client:console:376 object 0x007c3060 ctor 0x00512b81
ConVar cl_viewmodelfovtank( "cl_viewmodelfovtank", "50", 0xa, "" );

// client:console:378 object 0x00726660 ctor 0x004fa401
ConVar cl_viewtarget_clamp( "cl_viewtarget_clamp", "1", 0xa, "" );

// client:console:426 object 0x00722588 ctor 0x004f98d9
ConVar enable_skeleton_draw( "enable_skeleton_draw", "0", 0x4008, "Render skeletons in wireframe" );

// client:console:435 object 0x007a5b28 ctor 0x00508734
ConVar fog_2dskyboxfogfactor( "fog_2dskyboxfogfactor", "-1", 0x4008, "" );

// client:console:442 object 0x007a5a98 ctor 0x005086d4
ConVar fog_hdrcolorscale( "fog_hdrcolorscale", "-1", 0x4008, "" );

// client:console:443 object 0x007a5ae0 ctor 0x00508704
ConVar fog_hdrcolorscaleskybox( "fog_hdrcolorscaleskybox", "-1", 0x4008, "" );

// client:console:449 object 0x0079ffb0 ctor 0x00505974
ConVar force_disablex360( "force_disablex360", "0", 0x4008, "" );

// client:console:455 object 0x007adf90 ctor 0x0050bae6
ConVar fx_glass_velocity_cap( "fx_glass_velocity_cap", "0", 0xa, "Maximum downwards speed of shattered glass particles" );

// client:console:459 object 0x007c0a28 ctor 0x00511814
ConVar g_debug_physcannon( "g_debug_physcannon", "0", 0x6008, "" );

// client:console:477 object 0x007a29d0 ctor 0x00506d59
ConVar gl_clear_gray( "gl_clear_gray", "0", 0x4008, "Clear the back buffer to gray every frame." );

// client:console:487 object 0x007a6c88 ctor 0x00509319
ConVar hud_classautokill( "hud_classautokill", "1", 0x88, "Automatically kill player after choosing a new playerclass." );

// client:console:496 object 0x007c9ca8 ctor 0x00515bd1
ConVar hud_showtargetid( "hud_showtargetid", "1", 0xa, "" );

// client:console:502 object 0x007c9fc0 ctor 0x00515de6
ConVar hud_targetid_name_height_hunter( "hud_targetid_name_height_hunter", "10", 0xa, "Player names are this many inches over player eye positions" );

// client:console:505 object 0x007c9d38 ctor 0x00515c36
ConVar hud_targetid_player_view_cone( "hud_targetid_player_view_cone", "0.92", 0xa, "View cone for survivors seeing names of other survivors" );

// client:console:506 object 0x007c9d80 ctor 0x00515c66
ConVar hud_targetid_pz_ss_view_cone( "hud_targetid_pz_ss_view_cone", "0.98", 0xa, "View cone for seeing player names as a PZ in splitscreen" );

// client:console:508 object 0x007c9e10 ctor 0x00515cc1
ConVar hud_targetid_skill( "hud_targetid_skill", "0", 0xa, "" );

// client:console:509 object 0x007c9e58 ctor 0x00515cf1
ConVar hud_targetid_upgrades( "hud_targetid_upgrades", "0", 0xa, "" );

// client:console:520 object 0x0075a4a8 ctor 0x00501779
ConVar in_forceuser( "in_forceuser", "0", 0x400a, "Force user input to this split screen player." );

// client:console:540 object 0x00759a68 ctor 0x00501384
ConVar joy_cfg_preset( "joy_cfg_preset", "0", 0x100800a, "" );

// client:console:541 object 0x00759798 ctor 0x005011a4
ConVar joy_circle_correct( "joy_circle_correct", "1", 0x88, "" );

// client:console:547 object 0x00759360 ctor 0x00500ed9
ConVar joy_legacy( "joy_legacy", "0", 0x1008088, "Turn on/off 'Legacy' mapping for control sticks." );

// client:console:548 object 0x00759a20 ctor 0x00501351
ConVar joy_lookspin_default( "joy_lookspin_default", "0.35", 0xa, "" );

// client:console:560 object 0x007596c0 ctor 0x00501114
ConVar joy_sensitive_step0( "joy_sensitive_step0", "0.1", 0x88, "" );

// client:console:561 object 0x00759708 ctor 0x00501144
ConVar joy_sensitive_step1( "joy_sensitive_step1", "0.4", 0x88, "" );

// client:console:562 object 0x00759750 ctor 0x00501174
ConVar joy_sensitive_step2( "joy_sensitive_step2", "0.90", 0x88, "" );

// client:console:565 object 0x00758e98 ctor 0x00500ba1
ConVar joy_variable_frametime( "joy_variable_frametime", "1", 0xa, "" );

// client:console:568 object 0x00730140 ctor 0x004fc939
ConVar joy_vibration( "joy_vibration", "1", 0x88088, "Controller vibration." );

// client:console:577 object 0x00755780 ctor 0x004ff549
ConVar ladder_autocenter( "ladder_autocenter", "1", 0x200a, "Auto-center players on ladders." );

// client:console:578 object 0x00755738 ctor 0x004ff519
ConVar ladder_console_controls( "ladder_console_controls", "0", 0x200a, "Use absolute (up is always up and down always down) ladder controls." );

// client:console:579 object 0x00755868 ctor 0x004ff5a4
ConVar ladder_horizon( "ladder_horizon", "-0.3", 0x200a, "" );

// client:console:589 object 0x007c0928 ctor 0x00511781
ConVar lower_body( "lower_body", "1", 0xa, "" );

// client:console:590 object 0x007c08e0 ctor 0x00511754
ConVar lower_body_debug( "lower_body_debug", "0", 0x4008, "" );

// client:console:591 object 0x007c0970 ctor 0x005117b1
ConVar lower_body_hulk_layers( "lower_body_hulk_layers", "0", 0xa, "" );

// client:console:614 object 0x00753730 ctor 0x004fe231
ConVar mat_colcorrection_editor( "mat_colcorrection_editor", "0", 0xa, "" );

// client:console:616 object 0x007a38e8 ctor 0x00507771
ConVar mat_colorcorrection( "mat_colorcorrection", "1", 0xa, "" );

// client:console:651 object 0x007a6230 ctor 0x00508a94
ConVar mat_lpreview_mode( "mat_lpreview_mode", "-1", 0x4008, "" );

// client:console:652 object 0x007a40e8 ctor 0x00507dc1
ConVar mat_motion_blur_enabled( "mat_motion_blur_enabled", "1", 0xa, "" );

// client:console:669 object 0x007a61e8 ctor 0x00508a41
ConVar mat_skybox_reflection_clip( "mat_skybox_reflection_clip", "0", 0xa, "" );

// client:console:670 object 0x007a61a0 ctor 0x00508a11
ConVar mat_skybox_water_reflection( "mat_skybox_water_reflection", "1", 0xa, "" );

// client:console:692 object 0x007a70a8 ctor 0x00509624
ConVar motd_intros( "motd_intros", "0", 0x88, "" );

// client:console:698 object 0x007b20f0 ctor 0x0050d3cd
ConVar mp_buytime( "mp_buytime", "1.5", 0x200a, "How many minutes after round start players can buy items for.", true, 0.25f, false, 0.0f );

// client:console:724 object 0x007b2bd8 ctor 0x0050d994
ConVar mp_weapon_ak47_price( "mp_weapon_ak47_price", "2500", 0x2c0a, "" );

// client:console:725 object 0x007b2c68 ctor 0x0050d9f4
ConVar mp_weapon_aug_price( "mp_weapon_aug_price", "3500", 0x2c0a, "" );

// client:console:726 object 0x007b2d40 ctor 0x0050da84
ConVar mp_weapon_awp_price( "mp_weapon_awp_price", "4750", 0x2c0a, "" );

// client:console:727 object 0x007b2830 ctor 0x0050d724
ConVar mp_weapon_deagle_price( "mp_weapon_deagle_price", "650", 0x2c0a, "" );

// client:console:728 object 0x007b28c0 ctor 0x0050d784
ConVar mp_weapon_elite_price( "mp_weapon_elite_price", "800", 0x2c0a, "" );

// client:console:729 object 0x007b2b00 ctor 0x0050d904
ConVar mp_weapon_famas_price( "mp_weapon_famas_price", "2250", 0x2c0a, "" );

// client:console:730 object 0x007b2878 ctor 0x0050d754
ConVar mp_weapon_fiveseven_price( "mp_weapon_fiveseven_price", "750", 0x2c0a, "" );

// client:console:731 object 0x007b2d88 ctor 0x0050dab4
ConVar mp_weapon_g3sg1_price( "mp_weapon_g3sg1_price", "5000", 0x2c0a, "" );

// client:console:732 object 0x007b2b48 ctor 0x0050d934
ConVar mp_weapon_galil_price( "mp_weapon_galil_price", "2000", 0x2c0a, "" );

// client:console:733 object 0x007b2758 ctor 0x0050d694
ConVar mp_weapon_glock_price( "mp_weapon_glock_price", "400", 0x2c0a, "" );

// client:console:734 object 0x007b2dd0 ctor 0x0050dae4
ConVar mp_weapon_m249_price( "mp_weapon_m249_price", "5750", 0x2c0a, "" );

// client:console:735 object 0x007b2908 ctor 0x0050d7b4
ConVar mp_weapon_m3_price( "mp_weapon_m3_price", "1700", 0x2c0a, "" );

// client:console:736 object 0x007b2c20 ctor 0x0050d9c4
ConVar mp_weapon_m4a1_price( "mp_weapon_m4a1_price", "3100", 0x2c0a, "" );

// client:console:737 object 0x007b29e0 ctor 0x0050d844
ConVar mp_weapon_mac10_price( "mp_weapon_mac10_price", "1400", 0x2c0a, "" );

// client:console:738 object 0x007b2a28 ctor 0x0050d874
ConVar mp_weapon_mp5navy_price( "mp_weapon_mp5navy_price", "1500", 0x2c0a, "" );

// client:console:739 object 0x007b27e8 ctor 0x0050d6f4
ConVar mp_weapon_p228_price( "mp_weapon_p228_price", "600", 0x2c0a, "" );

// client:console:740 object 0x007b2ab8 ctor 0x0050d8d4
ConVar mp_weapon_p90_price( "mp_weapon_p90_price", "2350", 0x2c0a, "" );

// client:console:741 object 0x007b2b90 ctor 0x0050d964
ConVar mp_weapon_scout_price( "mp_weapon_scout_price", "2750", 0x2c0a, "" );

// client:console:742 object 0x007b2cf8 ctor 0x0050da54
ConVar mp_weapon_sg550_price( "mp_weapon_sg550_price", "4200", 0x2c0a, "" );

// client:console:743 object 0x007b2cb0 ctor 0x0050da24
ConVar mp_weapon_sg552_price( "mp_weapon_sg552_price", "3500", 0x2c0a, "" );

// client:console:744 object 0x007b2998 ctor 0x0050d814
ConVar mp_weapon_tmp_price( "mp_weapon_tmp_price", "1250", 0x2c0a, "" );

// client:console:745 object 0x007b2a70 ctor 0x0050d8a4
ConVar mp_weapon_ump45_price( "mp_weapon_ump45_price", "1700", 0x2c0a, "" );

// client:console:746 object 0x007b27a0 ctor 0x0050d6c4
ConVar mp_weapon_usp_price( "mp_weapon_usp_price", "500", 0x2c0a, "" );

// client:console:747 object 0x007b2950 ctor 0x0050d7e4
ConVar mp_weapon_xm1014_price( "mp_weapon_xm1014_price", "3000", 0x2c0a, "" );

// client:console:756 object 0x007ca628 ctor 0x005161d1
ConVar nb_shadow_blobby_dist( "nb_shadow_blobby_dist", "400", 0xa, "" );

// client:console:757 object 0x007ca670 ctor 0x00516201
ConVar nb_shadow_cull_dist( "nb_shadow_cull_dist", "1600", 0xa, "" );

// client:console:769 object 0x007a1950 ctor 0x00506256
ConVar net_showusercmd( "net_showusercmd", "0", 0xa, "Show user command encoding" );

// client:console:770 object 0x007a19b0 ctor 0x00506284
ConVar net_showusermessages( "net_showusermessages", "0", 0x4008, "" );

// client:console:771 object 0x007a67a8 ctor 0x00508e4d
ConVar next_weapon_timer( "next_weapon_timer", "1.25", 0x88, "", true, 0.0f, true, 5.0f );

// client:console:785 object 0x007cc1d8 ctor 0x00517284
ConVar perf_fire_bullet_single( "perf_fire_bullet_single", "0", 0x200a, "" );

// client:console:801 object 0x00781b48 ctor 0x00504451
ConVar phys_debug_check_contacts( "phys_debug_check_contacts", "0", 0xa, "" );

// client:console:803 object 0x0079ed90 ctor 0x00504fe4
ConVar phys_ragdoll_force_magnitude( "phys_ragdoll_force_magnitude", "0.3", 0x200a, "" );

// client:console:805 object 0x007c0ab8 ctor 0x00511874
ConVar player_throwforce( "player_throwforce", "2000", 0x6008, "" );

// client:console:807 object 0x007cc5e8 ctor 0x00517364
ConVar prop_use( "prop_use", "0", 0x200a, "" );

// client:console:814 object 0x007a54f8 ctor 0x00508319
ConVar r_3dskyonly( "r_3dskyonly", "0", 0x4008, "Only draw the 3d skybox. . skip the rest of the world." );

// client:console:820 object 0x0075f790 ctor 0x00503224
ConVar r_DrawModelLightOrigin( "r_DrawModelLightOrigin", "0", 0x4008, "" );

// client:console:822 object 0x00753a90 ctor 0x004fe416
ConVar r_FlashlightDetailProps( "r_FlashlightDetailProps", "1", 0xa, "Enable a flashlight drawing pass on detail props. 0 = off, 1 = single pass, 2 = multipass (multipass is PC ONLY)" );

// client:console:832 object 0x007a7ba0 ctor 0x005097d6
ConVar r_RainAllowInSplitScreen( "r_RainAllowInSplitScreen", "1", 0xa, "Allows rain in splitscreen" );

// client:console:833 object 0x007a7cc0 ctor 0x00509899
ConVar r_RainCheck( "r_RainCheck", "0", 0x4008, "Enable/disable IsInAir() check for rain drops?" );

// client:console:834 object 0x007a7de0 ctor 0x00509959
ConVar r_RainDebugDuration( "r_RainDebugDuration", "0", 0x4008, "Shows rain tracelines for this many seconds (0 disables)" );

// client:console:836 object 0x007a7b58 ctor 0x005097a6
ConVar r_RainParticleDensity( "r_RainParticleDensity", "1", 0xa, "Density of Particle Rain 0-1" );

// client:console:867 object 0x007c2ac0 ctor 0x005127c1
ConVar r_bloomtintexponent( "r_bloomtintexponent", "2.2", 0xa, "" );

// client:console:869 object 0x007a63f0 ctor 0x00508c09
ConVar r_clip3Dskyboxneartoworldfar( "r_clip3Dskyboxneartoworldfar", "-1", 0x400a, "-1: do whatever the sky_camera clip_3D_skybox_near_to_world_far says, 0: force no clip, 1: force clip" );

// client:console:870 object 0x007a6438 ctor 0x00508c39
ConVar r_clip3Dskyboxneartoworldfarfudge( "r_clip3Dskyboxneartoworldfarfudge", "0.0", 0x400a, "offset to pull the near clip plane in for the 3dskybox when using r_clip3Dskyboxneartoworldfar.  This ensures that there is some overlap between the 3dskybox and the world,  PROFIT!" );

// client:console:871 object 0x007221e0 ctor 0x004f9661
ConVar r_debug_sequencesets( "r_debug_sequencesets", "-2", 0xa, "" );

// client:console:879 object 0x007529b8 ctor 0x004fdbb9
ConVar r_drawallrenderables( "r_drawallrenderables", "0", 0x4008, "Draw all renderables, even ones inside solid leaves." );

// client:console:882 object 0x00722800 ctor 0x004f9b94
ConVar r_drawmodelstatsoverlay( "r_drawmodelstatsoverlay", "0", 0x4008, "" );

// client:console:895 object 0x007a6480 ctor 0x00508c61
ConVar r_drawunderwaterfogblocker( "r_drawunderwaterfogblocker", "1", 0xa, "" );

// client:console:899 object 0x007a6278 ctor 0x00508ac1
ConVar r_fade360style( "r_fade360style", "1", 0xa, "" );

// client:console:901 object 0x007a5e40 ctor 0x00508946
ConVar r_fastzreject( "r_fastzreject", "0", 0xa, "Activate/deactivates a fast z-setting algorithm to take advantage of hardware with fast z reject. Use -1 to default to hardware settings" );

// client:console:938 object 0x007a5660 ctor 0x00508401
ConVar r_flashlightvolumetrics( "r_flashlightvolumetrics", "1", 0xa, "" );

// client:console:939 object 0x007a56a8 ctor 0x00508431
ConVar r_flashlightvolumetricsdrawnatend( "r_flashlightvolumetricsdrawnatend", "1", 0xa, "" );

// client:console:940 object 0x007554a8 ctor 0x004ff021
ConVar r_impactparticles( "r_impactparticles", "1", 0xa, "" );

// client:console:942 object 0x0075f700 ctor 0x005031c1
ConVar r_lod( "r_lod", "-1", 0xa, "" );

// client:console:944 object 0x007a5348 ctor 0x005081f9
ConVar r_novis( "r_novis", "0", 0x4008, "Turn off the PVS." );

// client:console:961 object 0x00752e40 ctor 0x004fde24
ConVar r_shadow_debug_spew( "r_shadow_debug_spew", "0", 0x4008, "" );

// client:console:963 object 0x00752cd8 ctor 0x004fdd31
ConVar r_shadow_lightpos_lerptime( "r_shadow_lightpos_lerptime", "0.5", 0xa, "" );

// client:console:964 object 0x00752db0 ctor 0x004fddc6
ConVar r_shadow_shortenfactor( "r_shadow_shortenfactor", "2", 0xa, "Makes shadows cast from local lights shorter" );

// client:console:965 object 0x00752d68 ctor 0x004fdd94
ConVar r_shadowfromanyworldlight( "r_shadowfromanyworldlight", "0", 0x4008, "" );

// client:console:967 object 0x00752d20 ctor 0x004fdd64
ConVar r_shadowfromworldlights_debug( "r_shadowfromworldlights_debug", "0", 0x4008, "" );

// client:console:973 object 0x007526f8 ctor 0x004fdac6
ConVar r_shadows_on_renderables_enable( "r_shadows_on_renderables_enable", "0", 0xa, "Support casting RTT shadows onto other renderables" );

// client:console:977 object 0x00753ad8 ctor 0x004fe446
ConVar r_threadeddetailprops( "r_threadeddetailprops", "1", 0xa, "enable threading of detail prop drawing" );

// client:console:978 object 0x007a6360 ctor 0x00508ba1
ConVar r_unlimitedrefract( "r_unlimitedrefract", "0", 0xa, "" );

// client:console:986 object 0x0079ee20 ctor 0x00505054
ConVar ragdoll_collide( "ragdoll_collide", "0", 0x200a, "" );

// client:console:987 object 0x0079f1b0 ctor 0x005052a4
ConVar ragdoll_door_uncollide_time( "ragdoll_door_uncollide_time", "4.0", 0x200a, "" );

// client:console:988 object 0x0079f1f8 ctor 0x005052d4
ConVar ragdoll_force_to_sleep_time( "ragdoll_force_to_sleep_time", "10", 0x200a, "" );

// client:console:989 object 0x0079edd8 ctor 0x00505024
ConVar ragdoll_self_collision( "ragdoll_self_collision", "1", 0x200a, "" );

// client:console:991 object 0x0079f120 ctor 0x00505244
ConVar ragdoll_slowdown( "ragdoll_slowdown", "1.0", 0x200a, "" );

// client:console:992 object 0x0079f168 ctor 0x00505274
ConVar ragdoll_slowdown_time( "ragdoll_slowdown_time", "5.0", 0x200a, "" );

// client:console:993 object 0x007b1840 ctor 0x0050cf99
ConVar ragdoll_velocity_multiplier( "ragdoll_velocity_multiplier", "0", 0x4008, "Acceleration to last living velocity given to new ragdolls" );

// client:console:995 object 0x0073a500 ctor 0x004fd9c9
ConVar report_clientthinklist( "report_clientthinklist", "0", 0x4008, "List all clientside entities thinking and time - will report and turn itself off." );

// client:console:1014 object 0x007bbd98 ctor 0x00510cc9
ConVar sb_perf_crawl( "sb_perf_crawl", "0", 0x6008, "If true, survivor bot will be doing a perf-crawl walk through the map" );

// client:console:1021 object 0x0075e1a0 ctor 0x00502939
ConVar sc_enable( "sc_enable", "1.0", 0x8088, "Enable SteamController" );

// client:console:1022 object 0x0075e2e8 ctor 0x00502a29
ConVar sc_mouse_sensitivity( "sc_mouse_sensitivity", "0.10", 0x8088, "SteamInput global mouse sensitivity factor" );

// client:console:1033 object 0x00733208 ctor 0x004fca86
ConVar scene_vcdautosave( "scene_vcdautosave", "0", 0xa, "Create a savegame before VCD playback" );

// client:console:1036 object 0x007224b0 ctor 0x004f9841
ConVar sfm_record_hz( "sfm_record_hz", "30", 0xa, "" );

// client:console:1038 object 0x007cbc28 ctor 0x00516e19
ConVar shotgun_reload_cancel_delay( "shotgun_reload_cancel_delay", "0.5", 0x200a, "On reload from empty, time before a fire command is recognized to cancel the reload." );

// client:console:1045 object 0x007336e8 ctor 0x004fcb91
ConVar soundscape_message( "soundscape_message", "0", 0xa, "" );

// client:console:1046 object 0x00733730 ctor 0x004fcbc9
ConVar soundscape_radius_debug( "soundscape_radius_debug", "0", 0x4008, "Prints current volume of radius sounds" );

// client:console:1049 object 0x00726c90 ctor 0x004fa619
ConVar spec_centerchasecam( "spec_centerchasecam", "0", 0x88, "Looks at the target player's center, instead of his eye position, in chase came mode" );

// client:console:1077 object 0x00720fe0 ctor 0x004f9204
ConVar steam_controller_haptics( "steam_controller_haptics", "1", 0x82008, "" );

// client:console:1079 object 0x007a0360 ctor 0x00505b86
ConVar steamworks_client_sessionid( "steamworks_client_sessionid", "0", 0x1a, "The client session ID for the new steam works gamestats." );

// client:console:1080 object 0x007a0480 ctor 0x00505c49
ConVar steamworks_immediate_upload( "steamworks_immediate_upload", "1", 0x201a, "If set to zero the stats will only be uploaded at the end of the session. If set to non-zero, stats will immediately be uploaded." );

// client:console:1081 object 0x007a03a8 ctor 0x00505bb9
ConVar steamworks_server_sessionid( "steamworks_server_sessionid", "0", 0x201a, "The server session ID for the new steam works gamestats." );

// client:console:1082 object 0x007a03f0 ctor 0x00505be9
ConVar steamworks_show_uploads( "steamworks_show_uploads", "0", 0x200a, "If set to non-zero, will show when submitting tables." );

// client:console:1083 object 0x007a0438 ctor 0x00505c19
ConVar steamworks_stats_disable( "steamworks_stats_disable", "0", 0x201a, "If set to non-zero then no steam works game stats will be used." );

// client:console:1087 object 0x007d05b0 ctor 0x0051a424
ConVar survivor_accuracy_upgrade_factor( "survivor_accuracy_upgrade_factor", "0.5", 0x6008, "" );

// client:console:1092 object 0x007c11e0 ctor 0x00511b49
ConVar survivor_drag_continuous_fire( "survivor_drag_continuous_fire", "1", 0x200a, "Survivors need to hold +attack2 to drag incapacitated survivors" );

// client:console:1101 object 0x007c1618 ctor 0x00511e24
ConVar survivor_incap_decay_rate( "survivor_incap_decay_rate", "3", 0x200a, "" );

// client:console:1110 object 0x007c1348 ctor 0x00511c34
ConVar survivor_it_duration( "survivor_it_duration", "20", 0x6008, "" );

// client:console:1113 object 0x007c57e0 ctor 0x005137c4
ConVar survivor_lean( "survivor_lean", "0", 0x200a, "" );

// client:console:1118 object 0x007c1198 ctor 0x00511b19
ConVar survivor_revive_after_drag( "survivor_revive_after_drag", "0", 0x200a, "Standing still after dragging someone will start reviving him" );

// client:console:1120 object 0x007c1420 ctor 0x00511cc9
ConVar survivor_speed_boost_factor( "survivor_speed_boost_factor", "1.2", 0x6008, "Speed boost from upgrade" );

// client:console:1121 object 0x007c3c80 ctor 0x00513274
ConVar survivor_sprint_multiplier( "survivor_sprint_multiplier", "1.5", 0x6008, "" );

// client:console:1125 object 0x007c13d8 ctor 0x00511c94
ConVar survivor_unstoppable_speed( "survivor_unstoppable_speed", "150", 0x6008, "" );

// client:console:1126 object 0x007d12a0 ctor 0x0051a82d
ConVar survivor_upgrade_reload_duration( "survivor_upgrade_reload_duration", "0.5", 0x200a, "Upgraded survivors reload in this fraction of the base time.", true, 0.100000001f, true, 1.0f );

// client:console:1148 object 0x00780228 ctor 0x005033b9
ConVar sv_noclipfriction( "sv_noclipfriction", "4", 0x210a, "Friction during noclip move." );

// client:console:1156 object 0x00780c58 ctor 0x00503aa9
ConVar sv_pushaway_min_force( "sv_pushaway_min_force", "500", 0x200a, "Minimum amount of force applied to physics objects by players." );

// client:console:1161 object 0x007b2638 ctor 0x0050d5c9
ConVar sv_showdamage( "sv_showdamage", "0", 0x200a, "Shows base damage below what sv_showimpacts would show, and if a player was hit he'll show the damage he took above it (as healthdamage (armorabsorbed)).  1 = show hits.  2 = show hits and misses" );

// Native client object007B2680; qualified final flags.
ConVar sv_showdebugtracers( "sv_showdebugtracers", "0", 0x0000200a, "1: Shows a line following bullet path. Blue on client, red on server. (They should always match.) 2:Show autoaim candidates." );

// client:console:1164 object 0x007b25a8 ctor 0x0050d569
ConVar sv_showimpacts( "sv_showimpacts", "0", 0x200a, "Shows client (red) and server (blue) bullet impact point (1=both, 2=client-only, 3=server-only)" );

// client:console:1165 object 0x007b25f0 ctor 0x0050d599
ConVar sv_showplayerhitboxes( "sv_showplayerhitboxes", "0", 0x200a, "Show lag compensated hitboxes for the specified player index whenever a player fires." );

// client:console:1175 object 0x0071fee0 ctor 0x004f8e79
ConVar sv_tracereffects_limit_general( "sv_tracereffects_limit_general", "15", 0x200a, "Limits the number of visible tracers per frame" );

// client:console:1196 object 0x007ca0e0 ctor 0x00515ee1
ConVar teamwork_event_fade_in_time( "teamwork_event_fade_in_time", "0.25", 0xa, "" );

// client:console:1197 object 0x007ca128 ctor 0x00515f11
ConVar teamwork_event_fade_out_time( "teamwork_event_fade_out_time", "2.0", 0xa, "" );

// client:console:1205 object 0x007c03b0 ctor 0x005113b1
ConVar timeline_show_all_events( "timeline_show_all_events", "0", 0xa, "" );

// client:console:1206 object 0x0075e470 ctor 0x00502add
ConVar tir_maxpitch( "tir_maxpitch", "15", 0x4008, "TrackIR Max Pitch", true, 0.0f, true, 180.0f );

// client:console:1207 object 0x0075e548 ctor 0x00502b1d
ConVar tir_maxroll( "tir_maxroll", "90", 0x4008, "TrackIR Max Roll", true, 0.0f, true, 180.0f );

// client:console:1208 object 0x0075e3d0 ctor 0x00502b5d
ConVar tir_maxx( "tir_maxx", "4", 0x4008, "TrackIR Max X", true, 0.0f, true, 50.0f );

// client:console:1209 object 0x0075e5d8 ctor 0x00502b9d
ConVar tir_maxy( "tir_maxy", "6", 0x4008, "TrackIR Max Y", true, 0.0f, true, 50.0f );

// client:console:1210 object 0x0075e418 ctor 0x00502a9d
ConVar tir_maxyaw( "tir_maxyaw", "90", 0x4008, "TrackIR Max Yaw", true, 0.0f, true, 180.0f );

// client:console:1211 object 0x0075e500 ctor 0x00502bdd
ConVar tir_maxz( "tir_maxz", "1", 0x4008, "TrackIR Max Z", true, 0.0f, true, 50.0f );

// client:console:1212 object 0x0075e590 ctor 0x00502c16
ConVar tir_start( "tir_start", "0", 0xa, "TrackIR Start", true, 0.0f, true, 1.0f );

// client:console:1213 object 0x0075e4b8 ctor 0x00502c56
ConVar tir_stop( "tir_stop", "0", 0xa, "TrackIR Stop", true, 0.0f, true, 1.0f );

// client:console:1225 object 0x007cc148 ctor 0x00517224
ConVar upgrade_explosive_bullet_force( "upgrade_explosive_bullet_force", "2", 0x6008, "" );

// client:console:1226 object 0x007cc100 ctor 0x005171f4
ConVar upgrade_explosive_slug_force( "upgrade_explosive_slug_force", "4", 0x6008, "" );

// client:console:1229 object 0x007cc190 ctor 0x00517254
ConVar upgrade_show_explosive_ammo_radius( "upgrade_show_explosive_ammo_radius", "0", 0x6008, "" );

// client:console:1255 object 0x00728b90 ctor 0x004fab01
ConVar vm_draw_always( "vm_draw_always", "0", 0xa, "" );

// client:console:1259 object 0x007a6668 ctor 0x00508d26
ConVar voice_head_icon_height( "voice_head_icon_height", "20", 0xa, "Voice icons are this many inches over player eye positions" );

// client:console:1260 object 0x007a6620 ctor 0x00508cf6
ConVar voice_head_icon_size( "voice_head_icon_size", "6", 0xa, "Size of voice icon over player heads in inches" );

// client:console:1261 object 0x007a66b0 ctor 0x00508d56
ConVar voice_local_icon( "voice_local_icon", "0", 0xa, "Draw local player's voice icon" );

// client:console:1263 object 0x007c2a78 ctor 0x00512799
ConVar voice_spatialize( "voice_spatialize", "0", 0x208, "Spatializes voice chat", true, 0.0f, true, 1.0f );

// client:console:1265 object 0x00781b90 ctor 0x00504481
ConVar vphysics_client_threaded( "vphysics_client_threaded", "1", 0xa, "" );

// client:console:1266 object 0x00781bd8 ctor 0x005044b1
ConVar vphysics_client_threaded_min_cpus( "vphysics_client_threaded_min_cpus", "4", 0xa, "" );

// client:console:1268 object 0x007c4740 ctor 0x00513419
ConVar vs_incap_bonus( "vs_incap_bonus", "10", 0x6008, "Bonus score for infected, per incap in a chapter" );

// client:console:1272 object 0x007b5058 ctor 0x0050efe4
ConVar z_added_light_debug( "z_added_light_debug", "0", 0x4008, "" );

// client:console:1273 object 0x007b5010 ctor 0x0050efb4
ConVar z_added_light_max( "z_added_light_max", "0.25", 0x200a, "" );

// client:console:1274 object 0x007b4fc8 ctor 0x0050ef84
ConVar z_added_light_min( "z_added_light_min", "0.025", 0x200a, "" );

// client:console:1275 object 0x007b50a0 ctor 0x0050f026
ConVar z_added_light_scale( "z_added_light_scale", "0.2", 0xa, "Scales added light after min/max clamping", true, 0.0f, true, 1.0f );

// client:console:1276 object 0x007c5900 ctor 0x00513889
ConVar z_anim_idle_speed_max( "z_anim_idle_speed_max", "85", 0x200a, "Players moving slower than this and not pressing any movement buttons use an ACT_IDLE variant" );

// client:console:1277 object 0x007c58b8 ctor 0x00513859
ConVar z_anim_idle_speed_min( "z_anim_idle_speed_min", "20", 0x200a, "Players moving slower than this use an ACT_IDLE variant" );

// client:console:1278 object 0x007d17e8 ctor 0x0051ac31
ConVar z_atomize_gibbed_limbs_probability( "z_atomize_gibbed_limbs_probability", "100", 0xa, "" );

// client:console:1279 object 0x007c3728 ctor 0x00512ee4
ConVar z_avoidforce( "z_avoidforce", "2500", 0x6008, "" );

// client:console:1280 object 0x007c36e0 ctor 0x00512eb4
ConVar z_avoidteammates( "z_avoidteammates", "1", 0x6008, "" );

// client:console:1281 object 0x007b50e8 ctor 0x0050f059
ConVar z_award_debug( "z_award_debug", "0", 0x6008, "Get debugging info on all award calcs to the console. (0|1|2|3 for none|yes-no thoughts|every thought|every spammy calc)" );

// client:console:1288 object 0x007c0a70 ctor 0x00511844
ConVar z_carry_max_mass( "z_carry_max_mass", "250", 0x6008, "" );

// client:console:1300 object 0x007a1c68 ctor 0x00506344
ConVar z_debug_shared_random( "z_debug_shared_random", "0", 0x200a, "" );

// client:console:1302 object 0x007b2710 ctor 0x0050d669
ConVar z_do_tracers( "z_do_tracers", "1", 0x6008, "Do we want tracers at all?" );

// client:console:1315 object 0x007c1b28 ctor 0x00512189
ConVar z_ghost_ahead_flow( "z_ghost_ahead_flow", "500", 0x200a, "Ghosts must be this far ahead of the survivors to materialize" );

// client:console:1316 object 0x007c3770 ctor 0x00512f14
ConVar z_ghost_friction( "z_ghost_friction", "1.5", 0x200a, "" );

// client:console:1319 object 0x007d1a58 ctor 0x0051ae99
ConVar z_gib_limb_distance( "z_gib_limb_distance", "400", 0x4008, "Rifles and sniper rifles can dismember at this range" );

// client:console:1320 object 0x007d1aa0 ctor 0x0051aec9
ConVar z_gib_limb_distance_zoomed( "z_gib_limb_distance_zoomed", "800", 0x4008, "Sniper rifles can dismember at this range when zoomed" );

// client:console:1344 object 0x007c18a0 ctor 0x00511fd4
ConVar z_hunter_speed( "z_hunter_speed", "300", 0x6008, "" );

// client:console:1345 object 0x007d1998 ctor 0x0051ad74
ConVar z_infected_burn_strength( "z_infected_burn_strength", "0.9", 0x4008, "" );

// client:console:1348 object 0x007d1710 ctor 0x0051aba1
ConVar z_infected_gib_count( "z_infected_gib_count", "1", 0xa, "" );

// client:console:1349 object 0x007d17a0 ctor 0x0051ac01
ConVar z_infected_gib_max_speed( "z_infected_gib_max_speed", "500", 0xa, "" );

// client:console:1350 object 0x007d1758 ctor 0x0051abd1
ConVar z_infected_gib_min_speed( "z_infected_gib_min_speed", "300", 0xa, "" );

// client:console:1352 object 0x007c5ef8 ctor 0x00513c14
ConVar z_infected_tinting( "z_infected_tinting", "1", 0x200a, "" );

// client:console:1379 object 0x007d1680 ctor 0x0051ab46
ConVar z_mob_simple_shadows( "z_mob_simple_shadows", "0", 0xa, "Control shadow quality on mobs: 0 - full shadows, 1 - blobby, 2 - no shadows" );

// client:console:1388 object 0x007c3b18 ctor 0x00513184
ConVar z_puking_eye_height( "z_puking_eye_height", "44", 0x6008, "" );

// client:console:1389 object 0x00722390 ctor 0x004f9781
ConVar z_ragdoll_impact_strength( "z_ragdoll_impact_strength", "500", 0xa, "" );

// client:console:1412 object 0x007c3920 ctor 0x00513034
ConVar z_spawn_speed( "z_spawn_speed", "450", 0x6008, "" );

// client:console:1429 object 0x007b26c8 ctor 0x0050d639
ConVar z_tracer_spacing( "z_tracer_spacing", "1", 0x6008, "One in how many bullets is a tracer in automatic weapons.", true, 1.0f, false, 0.0f );

// client:console:1436 object 0x007c8a48 ctor 0x00514e59
ConVar z_vomit_fade_duration( "z_vomit_fade_duration", "5", 0x6008, "How long the fade takes" );

// client:console:1443 object 0x007c8928 ctor 0x00514d94
ConVar z_vomit_target_dot( "z_vomit_target_dot", "0.6", 0x4008, "" );

// client:console:1444 object 0x007c88e0 ctor 0x00514d64
ConVar z_vomit_target_range( "z_vomit_target_range", "280", 0x4008, "" );

// client:console:1448 object 0x007d1950 ctor 0x0051ad44
ConVar z_wound_offset_enabled( "z_wound_offset_enabled", "0", 0x4008, "" );
