// Compose into shared/voice_status.cpp, guarded CLIENT_DLL && L4D2. C1D8A90.
void CVoiceStatus::ResetVoiceIcons()
{
	m_VoicePlayers.ClearAll();
}
static void VoiceAllIconsChanged( IConVar *, const char *, float )
{
	CVoiceStatus *voice = GetClientVoiceMgr();
	if ( voice ) voice->ResetVoiceIcons();
}
ConVar voice_all_icons( "voice_all_icons", "0", 0x4008, "Draw all players' voice icons", VoiceAllIconsChanged );
