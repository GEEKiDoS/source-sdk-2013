// Transplant into existing src/game/server/entity_tools_server.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x000D6ED0 (command_function).
static void CC_foundry_engine_get_mouse_control( const CCommand &args )
{
	// Native's first branch when the Foundry module interface is not loaded.
	// The target does not ship that module; there is no substitute tool interface.
	Warning( "Not in Foundry mode.\n" );
}

// Native object 0x007F4AAC; callback 0x000D6ED0; command_function.
static ConCommand foundry_engine_get_mouse_control_command( "foundry_engine_get_mouse_control", CC_foundry_engine_get_mouse_control, "Give the engine control of the mouse.", 0x00000004 );

// Native callback 0x000D6F00 (command_function).
static void CC_foundry_engine_release_mouse_control( const CCommand &args )
{
	// Native's first branch when the Foundry module interface is not loaded.
	// The target does not ship that module; there is no substitute tool interface.
	Warning( "Not in Foundry mode.\n" );
}

// Native object 0x007F4AD0; callback 0x000D6F00; command_function.
static ConCommand foundry_engine_release_mouse_control_command( "foundry_engine_release_mouse_control", CC_foundry_engine_release_mouse_control, "Give the control of the mouse back to Hammer.", 0x00000004 );

// Native callback 0x000D7340 (command_function).
static void CC_foundry_select_entity( const CCommand &args )
{
	// Native's first branch when the Foundry module interface is not loaded.
	// The target does not ship that module; there is no substitute tool interface.
	Warning( "Not in Foundry mode.\n" );
}

// Native object 0x007F4AF4; callback 0x000D7340; command_function.
static ConCommand foundry_select_entity_command( "foundry_select_entity", CC_foundry_select_entity, "Select the entity under the crosshair or select entities with the specified name.", 0x00000004 );

// Native callback 0x000D6E50 (command_function).
static void CC_foundry_sync_hammer_view( const CCommand &args )
{
	// Native's first branch when the Foundry module interface is not loaded.
	// The target does not ship that module; there is no substitute tool interface.
	Warning( "Not in Foundry mode.\n" );
}

// Native object 0x007F4A88; callback 0x000D6E50; command_function.
static ConCommand foundry_sync_hammer_view_command( "foundry_sync_hammer_view", CC_foundry_sync_hammer_view, "Move Hammer's 3D view to the same position as the engine's 3D view.", 0x00000004 );

// Native callback 0x000D7260 (command_function).
static void CC_foundry_update_entity( const CCommand &args )
{
	// Native's first branch when the Foundry module interface is not loaded.
	// The target does not ship that module; there is no substitute tool interface.
	Warning( "Not in Foundry mode.\n" );
}

// Native object 0x007F4A64; callback 0x000D7260; command_function.
static ConCommand foundry_update_entity_command( "foundry_update_entity", CC_foundry_update_entity, "Updates the entity's position/angles when in edit mode", 0x00000004 );

#endif
