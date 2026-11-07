// Compose into shared/soundenvelope.cpp after CSoundPatch, CLIENT_DLL && L4D2. C199F10.
static void CC_ClientReportSoundPatch( const CCommand & )
{
	Msg( "Current sound patches: %d\n", CSoundPatch::g_SoundPatchCount );
}
