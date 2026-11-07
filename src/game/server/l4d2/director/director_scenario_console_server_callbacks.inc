// Transplant into existing src/game/server/l4d2/director_scenario.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x00275D60 (command_function).
static void CC_scripted_mode_increment_stage( const CCommand &args )
{
	TheDirector->GetScenario()->AdvanceStage();
}

// Native object 0x0083DCB4; callback 0x00275D60; command_function.
static ConCommand scripted_mode_increment_stage_command( "scripted_mode_increment_stage", CC_scripted_mode_increment_stage, "Force the scripted mode to move to the next stage in the script.", 0x00004006 );

#endif
