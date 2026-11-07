// Transplant into existing src/game/server/recipientfilter.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native1B4E90. Remove the current slot, not an index-based FindAndRemove;
// this also preserves native ordering if a source filter contains duplicates.
void CRecipientFilter::RemoveBots()
{
 for ( int i = GetRecipientCount() - 1; i >= 0; --i )
 {
  CBasePlayer *player = UTIL_PlayerByIndex( m_Recipients[i] );
  if ( player && player->IsFakeClient() ) m_Recipients.Remove( i );
 }
}

#endif
