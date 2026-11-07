// Compose into in_camera.cpp. C12E580 and its wrapper C12DC40.
extern ConVar c_thirdpersonshoulder;
extern ConVar c_thirdpersonshoulderdist;
void CInput::CAM_ToThirdPersonShoulder()
{
	// Native byte10758E6C has no store/RecvProp/datamap producer and is BSS0.
	const bool enabled = g_TerrorModes.IsPlayerControlledZombies() ? false : c_thirdpersonshoulder.GetBool();
	if ( enabled )
	{
		m_nCameraCommand = CAM_COMMAND_TOFIRSTPERSON;
		c_thirdpersonshoulder.SetValue( 0 );
		cam_idealdist.SetValue( cam_idealdist.GetDefault() );
	}
	else
	{
		m_nCameraCommand = CAM_COMMAND_TOTHIRDPERSON;
		c_thirdpersonshoulder.SetValue( 1 );
		// C12E140 caches atof(default) on the first query in PZ modes.
		static const float defaultDistance = atof( c_thirdpersonshoulderdist.GetDefault() );
		cam_idealdist.SetValue( g_TerrorModes.IsPlayerControlledZombies() ? defaultDistance : c_thirdpersonshoulderdist.GetFloat() );
	}
}
static void CC_ThirdPersonShoulder()
{
	static_cast< CInput * >( input )->CAM_ToThirdPersonShoulder();
}

void CInput::CAM_SetCommand( int command )
{
	m_nCameraCommand = command;
}

static void CC_CameraCommand( const CCommand &args )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "cam_command <0, 1, or 2>\n" );
		return;
	}
	static_cast< CInput * >( input )->CAM_SetCommand( Q_atoi( args[1] ) );
}

static ConCommand camera_command( "cam_command", CC_CameraCommand, "Tells camera to change modes", 0x4008 );
