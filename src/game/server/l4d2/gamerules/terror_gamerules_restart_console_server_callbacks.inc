// Transplant into existing src/game/server/l4d2/terror_gamerules_restart.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// S2DB180. This native round-ending surface is distinct from Director::EndRound.
void CTerrorGameRules::TerminateRound( float delay, int reason )
{
 int winner = 0;
 const char *message = "#Game_Commencing";
 switch ( reason )
 {
 case 1: message = "The helicopter has arrived!"; winner = 2; break;
 case 7: message = "The radio has been destroyed!"; winner = 3; break;
 case 8: message = "#CTs_Win"; winner = 3; break;
 case 9: message = "#Terrorists_Win"; winner = 2; break;
 case 10: message = "#Round_Draw"; winner = 1; break;
 case 16: winner = 1; break;
 default: DevMsg( "TerminateRound: unknown round end ID %i\n", reason ); break;
 }
 m_iRoundWinStatus = winner;
 if ( !TheDirector->IsChangingMapOrMission() ) m_flRestartRoundTime = gpGlobals->curtime + delay;
 IGameEvent *event = gameeventmanager->CreateEvent( "round_end" );
 if ( event )
 {
  event->SetInt( "winner", winner );
  event->SetInt( "reason", reason );
  event->SetString( "message", message );
  gameeventmanager->FireEvent( event );
 }
}

// Native callback 0x0023B700 (legacy_function).
static void CC_endround(  )
{
	TerrorGameRules()->TerminateRound( 0.0f, 10 );
}

// Native object 0x00836BA4; callback 0x0023B700; legacy_function.
static ConCommand endround_command( "endround", CC_endround, "End the current round.", 0x00004004 );

#endif
