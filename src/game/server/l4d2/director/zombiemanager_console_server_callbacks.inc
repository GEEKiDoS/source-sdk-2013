// Transplant into existing src/game/server/l4d2/zombiemanager.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x003589E0 (legacy_function).
static void CC_z_reset_population_counter(  )
{
	TheZombieManager->ResetPopulationCounter();
}

// Native object 0x0085D964; callback 0x003589E0; legacy_function.
static ConCommand z_reset_population_counter_command( "z_reset_population_counter", CC_z_reset_population_counter, "For debugging", 0x00004004 );

#endif
