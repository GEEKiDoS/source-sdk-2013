// Compose into clientshadowmgr.cpp after s_ClientShadowMgr. C0CF440/C0D2A00/C0D3D60.
// Header/constructor/scheduling cutovers are in owner_operations.txt.
void CClientShadowMgr::RequeueDeferredShadows()
{
	for ( unsigned short i = m_DeferredDirtyShadows.FirstInorder(); i != m_DeferredDirtyShadows.InvalidIndex(); i = m_DeferredDirtyShadows.NextInorder( i ) )
		m_DirtyShadows.InsertIfNotFound( m_DeferredDirtyShadows[i] );
	m_DeferredDirtyShadows.RemoveAll();
}
void CClientShadowMgr::DirtyAllNonFlashlightShadows()
{
	for ( ClientShadowHandle_t i = m_Shadows.Head(); i != m_Shadows.InvalidIndex(); i = m_Shadows.Next( i ) )
	{
		if ( m_Shadows[i].m_Flags & SHADOW_FLAGS_FLASHLIGHT ) continue;
		IClientRenderable *renderable = ClientEntityList().GetClientRenderableFromHandle( m_Shadows[i].m_Entity );
		if ( renderable ) AddToDirtyShadowList( renderable, true );
	}
}
void CClientShadowMgr::SetShadowFromWorldLightsEnabled( bool enabled )
{
	const bool old = m_bShadowFromWorldLightsEnabled && !m_bShadowFromWorldLightsDisabled;
	m_bShadowFromWorldLightsEnabled = enabled;
	if ( old != ( enabled && !m_bShadowFromWorldLightsDisabled ) ) DirtyAllNonFlashlightShadows();
}
void CClientShadowMgr::ShadowFromWorldLightsChanged()
{
	const bool old = m_bShadowFromWorldLightsEnabled && !m_bShadowFromWorldLightsDisabled;
	ConVarRef setting( "r_shadowfromworldlights" );
	m_bShadowFromWorldLightsDisabled = !setting.GetBool();
	if ( old != ( m_bShadowFromWorldLightsEnabled && !m_bShadowFromWorldLightsDisabled ) ) DirtyAllNonFlashlightShadows();
}
static void ShadowHalfUpdateRateChanged( IConVar *, const char *, float )
{
	s_ClientShadowMgr.RequeueDeferredShadows();
}
static void ShadowFromWorldLightsChanged( IConVar *, const char *, float )
{
	s_ClientShadowMgr.ShadowFromWorldLightsChanged();
}
ConVar r_shadow_half_update_rate( "r_shadow_half_update_rate", "0", 0xa, "Updates shadows at half the framerate", ShadowHalfUpdateRateChanged );
ConVar r_shadowfromworldlights( "r_shadowfromworldlights", "1", 0xa, "Enable shadowing from world lights", ShadowFromWorldLightsChanged );

// C0D48E0/C0D4A00: replace the dirty/transparent queue portion of PreRender
// by a call to this method, with the existing m_bUpdatingDirtyShadows guard held.
void CClientShadowMgr::ReprojectDirtyShadows()
{
	ConVarRef spew( "r_shadow_debug_spew" );
	if ( r_shadow_half_update_rate.GetBool() )
	{
		for ( unsigned short i = m_DeferredDirtyShadows.FirstInorder(); i != m_DeferredDirtyShadows.InvalidIndex(); i = m_DeferredDirtyShadows.NextInorder( i ) )
		{
			ClientShadowHandle_t handle = m_DeferredDirtyShadows[i];
			UpdateProjectedTextureInternal( handle, false );
			m_DirtyShadows.Remove( handle );
		}
		const int deferred = m_DeferredDirtyShadows.Count();
		const int budget = ( deferred + m_DirtyShadows.Count() ) / 2 + 1;
		if ( spew.GetBool() ) DevMsg( "dirty shadows: %3d\n", budget );
		int count = clamp( budget - deferred, 0, m_DirtyShadows.Count() );
		m_DeferredDirtyShadows.RemoveAll();
		unsigned short i = m_DirtyShadows.FirstInorder();
		for ( ; count > 0; --count )
		{
			UpdateProjectedTextureInternal( m_DirtyShadows[i], false );
			i = m_DirtyShadows.NextInorder( i );
		}
		while ( i != m_DirtyShadows.InvalidIndex() )
		{
			ClientShadowHandle_t handle = m_DirtyShadows[i];
			i = m_DirtyShadows.NextInorder( i );
			m_DeferredDirtyShadows.InsertIfNotFound( handle );
		}
	}
	else
	{
		if ( spew.GetBool() ) DevMsg( "dirty shadows: %3d\n", m_DirtyShadows.Count() );
		for ( unsigned short i = m_DirtyShadows.FirstInorder(); i != m_DirtyShadows.InvalidIndex(); i = m_DirtyShadows.NextInorder( i ) )
			UpdateProjectedTextureInternal( m_DirtyShadows[i], false );
	}
	m_DirtyShadows.RemoveAll();
	FOR_EACH_VEC( m_TransparentShadows, i )
	{
		ClientShadowHandle_t handle = m_TransparentShadows[i];
		IClientRenderable *renderable = ClientEntityList().GetClientRenderableFromHandle( m_Shadows[handle].m_Entity );
		// Standing306: skip only the dereference if its entity was removed.
		if ( renderable ) renderable->MarkShadowDirty( true );
		m_DirtyShadows.InsertIfNotFound( handle );
	}
	m_TransparentShadows.RemoveAll();
}
