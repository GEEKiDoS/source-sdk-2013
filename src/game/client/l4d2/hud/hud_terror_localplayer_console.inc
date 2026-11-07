// Compose into l4d2/hud_terror_localplayer.cpp. C2C8190 / C2C8AC0.
static void CC_ReloadTeamDisplay( const CCommand & )
{
	FOR_EACH_VEC( s_terrorDisplayRows, i ) s_terrorDisplayRows[i]->InvalidateLayout( false, true );
}
static int s_nHUDGlowTest;
static void CC_TestHUDGlow( const CCommand & )
{
	CUtlVector< C_BasePlayer * > survivors;
	if ( g_PR )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			if ( g_PR->IsConnected( i ) && g_PR->GetTeam( i ) == TEAM_SURVIVOR )
			{
				C_BasePlayer *player = UTIL_PlayerByIndex( i );
				if ( player && player->IsPlayer() ) survivors.AddToTail( player );
			}
		// Standing305: count0 skips only the quotient-dependent event.
		if ( survivors.Count() )
		{
			static const int awards[] = { 75, 76, 66, 68, 67, 80, 70 };
			const int first = s_nHUDGlowTest % survivors.Count();
			const int nextCounter = (int)( (unsigned int)s_nHUDGlowTest + 1u );
			const int second = nextCounter % survivors.Count();
			IGameEvent *event = gameeventmanager->CreateEvent( "award_earned" );
			if ( event )
			{
				// Standing306: only an overflowed negative array index is skipped.
				if ( first >= 0 ) event->SetInt( "userid", survivors[first]->GetUserID() );
				event->SetInt( "award", awards[(unsigned int)s_nHUDGlowTest % ARRAYSIZE( awards )] );
				if ( second >= 0 ) event->SetInt( "subjectentid", survivors[second]->entindex() );
				gameeventmanager->FireEventClientSide( event );
			}
		}
		s_nHUDGlowTest = (int)( (unsigned int)s_nHUDGlowTest + 1u );
	}
}
