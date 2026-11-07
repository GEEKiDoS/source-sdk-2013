//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_NAV_COMMANDS_H
#define TERROR_NAV_COMMANDS_H
#ifdef _WIN32
#pragma once
#endif

class CCommand;
void CC_clear_attribute( const CCommand &args );
void CC_current_flow_distance( const CCommand &args );
void CC_mark( const CCommand &args );
void CC_nav_auto_build_area();
void CC_nav_fix_playerclips( const CCommand &args );
void CC_nav_fog_mark();
void CC_nav_fog_pick();
void CC_nav_fog_print_used( const CCommand &args );
void CC_nav_fog_replace( const CCommand &args );
void CC_nav_fog_select_place( const CCommand &args );
void CC_nav_fog_set( const CCommand &args );
void CC_nav_gui();
void CC_nav_print_visibility_info( const CCommand &args );
void CC_nav_print_visible_set_counts( const CCommand &args );
void CC_nav_select_completely_visible();
void CC_nav_select_partially_visible();
void CC_nav_select_threat();
void CC_nav_select_view_distance( const CCommand &args );
void CC_nav_select_visible_set_size( const CCommand &args );
void CC_nav_test_visibility();
void CC_nav_trouble_report( const CCommand &args );
void CC_nav_trouble_report_corner_to_corner( const CCommand &args );
void CC_nav_trouble_report_invalid( const CCommand &args );
void CC_nav_trouble_report_suggestions( const CCommand &args );
void CC_select_cliff();
void CC_select_invalid_finale_nospawn();
void CC_select_with_attribute( const CCommand &args );
void CC_travel_distance();
void CC_wipe_attributes();
int Complete_RVA002E2880( const char *partial, char commands[64][64] );
int Complete_RVA002E84C0( const char *partial, char commands[64][64] );
int Complete_RVA002F5730( const char *partial, char commands[64][64] );

#endif // TERROR_NAV_COMMANDS_H
