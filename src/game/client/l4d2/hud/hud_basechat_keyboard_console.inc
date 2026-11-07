// Compose into hud_basechat.cpp. C135CD0 -> C116730, callback C113800,
// frame tail C116902..C11695A. Public interface/constructor rows in owner_operations.
void CBaseHudChat::OpenSteamKeyboard()
{
	if ( m_nMessageMode != 0 ) return;
	int x, y, width, height;
	GetBounds( x, y, width, height );
	m_nSteamKeyboardOldY = y;
	y = ScreenHeight() / 2 - height;
	SetPos( x, y );
	DevMsg( "Opening Steam keyboard - k_EFloatingGamepadTextInputModeModeSingleLine, %d %d %d %d\n", x, y, width, height );
	ISteamUtils *utils = steamapicontext ? steamapicontext->SteamUtils() : NULL;
	// Standing306: an absent service skips only the unavailable virtual call.
	if ( utils ) utils->ShowFloatingGamepadTextInput( k_EFloatingGamepadTextInputModeModeSingleLine, x, y, width, height );
	m_bSteamKeyboardVisible = true;
	StartMessageMode( MM_SAY );
}
void CBaseHudChat::OnFloatingGamepadTextInputDismissed( FloatingGamepadTextInputDismissed_t * )
{
	DevMsg( "Recieved OnFloatingGamepadTextInputDismissed\n" );
	m_nSteamKeyboardCloseFrame = (int)( (unsigned int)m_nSteamKeyboardFrame + 2u );
}
void CBaseHudChat::OnThink()
{
	m_nSteamKeyboardFrame = (int)( (unsigned int)m_nSteamKeyboardFrame + 1u );
	if ( m_nSteamKeyboardCloseFrame != m_nSteamKeyboardFrame ) return;
	if ( m_bSteamKeyboardVisible )
	{
		m_bSteamKeyboardVisible = false;
		int x, y;
		GetPos( x, y );
		SetPos( x, m_nSteamKeyboardOldY );
		if ( m_nMessageMode ) StopMessageMode();
	}
	m_nSteamKeyboardCloseFrame = -1;
}
static void CC_SteamControllerSay( const CCommand & )
{
	if ( engine->Con_IsVisible() || gpGlobals->maxClients == 1 ) return;
	CBaseHudChat *chat = dynamic_cast< CBaseHudChat * >( gHUD.FindElement( "CHudChat" ) );
	if ( chat ) chat->OpenSteamKeyboard();
}
CBaseHudChat::~CBaseHudChat()
{
	ISteamUtils *utils = steamapicontext ? steamapicontext->SteamUtils() : NULL;
	if ( utils ) utils->DismissFloatingGamepadTextInput();
}
