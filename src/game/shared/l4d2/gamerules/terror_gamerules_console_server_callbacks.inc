// Transplant into existing src/game/shared/l4d2/terror_gamerules.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// The absent MatchFramework interface is reduced to its exact single-method
// game-side contract. This is NOT a replacement match service or subscription.
class IMatchEventsSink
{
public:
 virtual void OnEvent( KeyValues *event ) = 0;
};
class CTerrorGameRules_MatchEventsSink : public IMatchEventsSink
{
public:
 // Native2DBFA0 is exactly RETN4. Its intentionally empty body is proven,
 // not a stub, and must not be replaced by invented endgame consequences.
 virtual void OnEvent( KeyValues *event ) OVERRIDE {}
};
static CTerrorGameRules_MatchEventsSink s_TerrorRulesMatchEvents;

// Native callback 0x0025BC70 (command_function).
static void CC_multiplayerendgame( const CCommand &args )
{
	KeyValues *event = new KeyValues( "OnEngineEndGame", "reason", "multiplayerendgame cheat" );
	// Main-approved direct native sink delivery, bypassing only unavailable
	// MatchFramework subscription transport. Native sink itself is empty.
	s_TerrorRulesMatchEvents.OnEvent( event );
	event->deleteThis();
}

// Native object 0x0083D87C; callback 0x0025BC70; command_function.
static ConCommand multiplayerendgame_command( "multiplayerendgame", CC_multiplayerendgame, "Take all of the connected clients and take them to the end game state.", 0x00004004 );

#endif
