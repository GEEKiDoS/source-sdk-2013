// Compose into l4d2/terror_spawnmenus.cpp. C2E8FE0, checked against assembly.
static void CC_ToggleScores( const CCommand & )
{
	if ( !gViewPortInterface ) return;
	IViewPortPanel *scores = gViewPortInterface->FindPanelByName( "scores" );
	if ( !scores )
	{
		// Main-approved single-root projection (initial27C4 section5.3 /
		// TutorA68C sectionD): native fullscreen root uses the actual viewport.
		// Keep the second lookup and its fresh owner query, not a fake getter.
		IViewPort *fullscreen = dynamic_cast< IViewPort * >( g_pClientMode->GetViewport() );
		if ( !fullscreen ) return;
		scores = fullscreen->FindPanelByName( "scores" );
		if ( !scores ) return;
	}
	if ( scores->IsVisible() )
	{
		gViewPortInterface->ShowPanel( scores, false );
		GetClientVoiceMgr()->StopSquelchMode();
	}
	else gViewPortInterface->ShowPanel( scores, true );
}
