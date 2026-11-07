// Include exactly once in viewpostprocess.cpp after SetRenderTargetAndViewPort
// and the existing CTerrorPostMaterials owner. L4D2 only.
static ConVar mat_dof_enabled( "mat_dof_enabled", "1", 0x0000000A );
static ConVar mat_dof_override( "mat_dof_override", "0", 0x0000000A );
static ConVar mat_dof_near_blur_depth( "mat_dof_near_blur_depth", "20.0", 0x0000000A );
static ConVar mat_dof_near_focus_depth( "mat_dof_near_focus_depth", "100.0", 0x0000000A );
static ConVar mat_dof_far_focus_depth( "mat_dof_far_focus_depth", "250.0", 0x0000000A );
static ConVar mat_dof_far_blur_depth( "mat_dof_far_blur_depth", "1000.0", 0x0000000A );
static ConVar mat_dof_near_blur_radius( "mat_dof_near_blur_radius", "10.0", 0x0000000A );
static ConVar mat_dof_far_blur_radius( "mat_dof_far_blur_radius", "5.0", 0x0000000A );
static ConVar mat_dof_quality( "mat_dof_quality", "0", 0x0000000A );

bool Terror_IsDepthOfFieldEnabled( const CViewSetup *viewSetup )
{
	if ( !viewSetup || g_pMaterialSystemHardwareConfig->GetHDRType() != HDR_TYPE_FLOAT || g_pMaterialSystemHardwareConfig->GetDXSupportLevel() < 92 ) return false;
	// Original engine/SFM view overrides are unavailable on this engine ABI.
	return mat_dof_enabled.GetInt() != 0 && ( mat_dof_override.GetInt() != 0 || g_TerrorDOFState.enabled );
}
static void Terror_SetDOFFloat( IMaterial *material, const char *name, float value )
{
	bool found;
	IMaterialVar *variable = material->FindVar( name, &found );
	if ( found ) variable->SetFloatValue( value );
}
static void Terror_SetDOFInt( IMaterial *material, const char *name, int value )
{
	bool found;
	IMaterialVar *variable = material->FindVar( name, &found );
	if ( found ) variable->SetIntValue( value );
}
void Terror_DrawDepthOfField( const CViewSetup &viewSetup )
{
	if ( !Terror_IsDepthOfFieldEnabled( view->GetViewSetup() ) ) return;
	UpdateScreenEffectTexture( 0, viewSetup.x, viewSetup.y, viewSetup.width, viewSetup.height, false );
	CMatRenderContextPtr context( materials );
	ITexture *full = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
	const int fullWidth = full->GetActualWidth(), fullHeight = full->GetActualHeight();
	if ( mat_dof_quality.GetInt() < 2 )
	{
		context->PushRenderTargetAndViewport();
		ITexture *small0 = materials->FindTexture( "_rt_SmallFB0", TEXTURE_GROUP_RENDER_TARGET );
		SetRenderTargetAndViewPort( small0 );
		// Reuse the real existing native HDR four-tap downsample, not the
		// non-HDR bloom/luma pass and not a second prepare-material owner.
		context->DrawScreenSpaceRectangle( g_TerrorPostMaterials.GetPrepare( 3 ), 0, 0,
			fullWidth / 4, fullHeight / 4, 0, 0, fullWidth - 2, fullHeight - 2, fullWidth, fullHeight );
		IMaterial *blur = g_TerrorPostMaterials.GetDOFMaterial( 1 );
		const int smallWidth = small0->GetActualWidth(), smallHeight = small0->GetActualHeight();
		Terror_SetDOFFloat( blur, "$c0_x", 0.5f / smallWidth );
		Terror_SetDOFFloat( blur, "$c0_y", 0.5f / smallHeight );
		Terror_SetDOFFloat( blur, "$c1_x", -0.5f / smallWidth );
		Terror_SetDOFFloat( blur, "$c1_y", 0.5f / smallHeight );
		ITexture *small1 = materials->FindTexture( "_rt_SmallFB1", TEXTURE_GROUP_RENDER_TARGET );
		SetRenderTargetAndViewPort( small1 );
		context->DrawScreenSpaceRectangle( blur, 0, 0, fullWidth / 4, fullHeight / 4,
			0, 0, smallWidth - 1, smallHeight - 1, smallWidth, smallHeight );
		context->PopRenderTargetAndViewport();
	}
	int x, y, width, height;
	context->GetViewport( x, y, width, height );
	IMaterial *dof = g_TerrorPostMaterials.GetDOFMaterial( 0 );
	Terror_SetDOFFloat( dof, "$nearPlane", viewSetup.zNear );
	Terror_SetDOFFloat( dof, "$farPlane", viewSetup.zFar );
	const bool overrideValues = mat_dof_override.GetInt() != 0;
	Terror_SetDOFFloat( dof, "$nearBlurDepth", overrideValues ? mat_dof_near_blur_depth.GetFloat() : g_TerrorDOFState.nearBlurDepth );
	Terror_SetDOFFloat( dof, "$nearFocusDepth", overrideValues ? mat_dof_near_focus_depth.GetFloat() : g_TerrorDOFState.nearFocusDepth );
	Terror_SetDOFFloat( dof, "$farFocusDepth", overrideValues ? mat_dof_far_focus_depth.GetFloat() : g_TerrorDOFState.farFocusDepth );
	Terror_SetDOFFloat( dof, "$farBlurDepth", overrideValues ? mat_dof_far_blur_depth.GetFloat() : g_TerrorDOFState.farBlurDepth );
	Terror_SetDOFFloat( dof, "$nearBlurRadius", overrideValues ? mat_dof_near_blur_radius.GetFloat() : g_TerrorDOFState.nearBlurRadius );
	Terror_SetDOFFloat( dof, "$farBlurRadius", overrideValues ? mat_dof_far_blur_radius.GetFloat() : g_TerrorDOFState.farBlurRadius );
	Terror_SetDOFInt( dof, "$quality", mat_dof_quality.GetInt() );
	C_World *world = GetClientWorldEntity();
	context->DrawScreenSpaceRectangle( dof, 0, 0, width, height, 0, 0,
		fullWidth - 1, fullHeight - 1, fullWidth, fullHeight, world ? world->GetClientRenderable() : NULL );
}
void Terror_ClearSkyboxDepthAlpha( const CViewSetup *viewSetup )
{
	if ( !Terror_IsDepthOfFieldEnabled( viewSetup ) ) return;
	CMatRenderContextPtr context( materials );
	int x, y, width, height;
	context->GetViewport( x, y, width, height );
	context->DrawScreenSpaceRectangle( g_TerrorPostMaterials.GetDOFMaterial( 2 ),
		0, 0, width, height, 0, 0, width - 1, height - 1, width, height );
}
