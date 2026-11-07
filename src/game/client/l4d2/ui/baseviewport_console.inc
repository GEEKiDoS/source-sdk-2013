// Compose into game_controls/baseviewport.cpp. C1E3D90.
// Add public void RecreatePanel( const char *name ) to CBaseViewport.
void CBaseViewport::RecreatePanel( const char *name )
{
	IViewPortPanel *panel = FindPanelByName( name );
	if ( !panel ) return;
	// The native also removes its name-cache entry; SDK FindPanelByName is linear.
	m_Panels.FindAndRemove( panel );
	vgui::ipanel()->DeletePanel( panel->GetVPanel() );
	if ( m_pActivePanel == panel ) m_pActivePanel = NULL;
	if ( m_pLastActivePanel == panel ) m_pLastActivePanel = NULL;
	AddNewPanel( CreatePanelByName( name ), name );
}
