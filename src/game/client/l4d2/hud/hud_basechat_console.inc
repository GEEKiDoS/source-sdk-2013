// Compose into hud_basechat.cpp; C287190. Color alpha is not rewritten.
Color g_ColorSurvivor( 138, 182, 220, 255 );
Color g_ColorZombie( 209, 21, 24, 255 );
static void TerrorPlayerColorChanged( IConVar *variable, const char *, float )
{
	ConVar *value = static_cast< ConVar * >( variable );
	// Main decision: zero only the native uninitialized parse outputs.
	int red = 0, green = 0, blue = 0;
	sscanf( value->GetString(), "%d %d %d", &red, &green, &blue );
	Color &color = !Q_stricmp( value->GetName(), "cl_color_zombie" ) ? g_ColorZombie : g_ColorSurvivor;
	color.SetColor( red, green, blue, color.a() );
}
ConVar cl_color_survivor( "cl_color_survivor", "138 182 220", 0xa, "", TerrorPlayerColorChanged );
ConVar cl_color_zombie( "cl_color_zombie", "209 199 151", 0xa, "", TerrorPlayerColorChanged );

// C22C1B0 and C287210. The default zombie ConVar text deliberately differs
// from the initial red global; no change callback runs during construction.
Color GetClientColor( int clientIndex )
{
	if ( clientIndex == 0 ) return Color( 153, 255, 153, 255 );
	if ( !g_PR ) return Color( 255, 178, 0, 255 );
	const int team = g_PR->GetTeam( clientIndex );
	if ( team == 2 || team == 4 ) return g_ColorSurvivor;
	if ( team == 3 ) return g_ColorZombie;
	return Color( 204, 204, 204, 255 );
}
Color GetPlayerColor( C_BasePlayer *player )
{
	const int team = player ? player->GetTeamNumber() : 0;
	return ( team == 2 || team == 4 ) ? g_ColorSurvivor : g_ColorZombie;
}
