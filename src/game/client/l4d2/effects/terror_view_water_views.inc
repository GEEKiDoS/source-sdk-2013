// Included at the end of viewrender.cpp, after its private renderer helpers.
// Native C1D6D80 supplies the already selected fog volume, not a sky-origin query.
bool CAboveWaterSkyboxView::Setup( const CViewSetup &viewRender, int *pClearFlags,
	SkyboxVisibility_t *pSkyboxVisible, const VisibleFogVolumeInfo_t &fogInfo )
{
	bool bDraw = BaseClass::Setup( viewRender, pClearFlags, pSkyboxVisible );
	m_DrawFlags &= ~( DF_RENDER_UNDERWATER | DF_DRAW_ENTITITES );
	m_fogInfo = fogInfo;
	return bDraw;
}

// Native C1D6340: reflection is submitted BEFORE the ordinary 3-D skybox.
void CAboveWaterSkyboxView::Draw()
{
	if ( mat_skybox_water_reflection.GetBool() )
	{
		int nClearFlags = VIEW_CLEAR_DEPTH;
		SkyboxVisibility_t skyboxVisible;
		if ( m_ReflectionView.Setup( *this, &nClearFlags, &skyboxVisible ) )
			m_pMainView->AddViewToScene( &m_ReflectionView );
	}
	BaseClass::Draw();
}

// Native C1D08A0 repeats the visibility/sky-parameter query after its base Setup.
bool CAboveWaterSkyboxView::CSkyboxReflectionView::Setup( const CViewSetup &viewRender,
	int *pClearFlags, SkyboxVisibility_t *pSkyboxVisible )
{
	BaseClass::Setup( viewRender, pClearFlags, pSkyboxVisible );
	// C1CE670's setup near-plane division precedes the second query.
	// Reflection does not run ordinary skybox DrawInternal's plane reset.
	if ( m_pSky3dParams && m_pSky3dParams->scale > 1 )
		zNear = viewRender.zNear / MIN( m_pSky3dParams->scale, 4 );
	*pSkyboxVisible = ComputeSkyboxVisibility();
	m_pSky3dParams = PreRender3dSkyboxWorld( *pSkyboxVisible );
	if ( !m_pSky3dParams )
		return false;

	m_ClearFlags = *pClearFlags;
	*pClearFlags &= ~( VIEW_CLEAR_COLOR | VIEW_CLEAR_DEPTH | VIEW_CLEAR_STENCIL | VIEW_CLEAR_FULL_TARGET );
	*pClearFlags |= VIEW_CLEAR_DEPTH;
	m_DrawFlags = DF_RENDER_REFLECTION | DF_RENDER_ABOVEWATER | DF_DRAW_ENTITITES;
	if ( mat_skybox_reflection_clip.GetBool() )
		m_DrawFlags |= DF_CLIP_Z | DF_CLIP_BELOW;
	if ( r_skybox.GetBool() )
		m_DrawFlags |= DF_DRAWSKYBOX;
	return true;
}

// Native C1CEA70's 0x200000 branch skips ONLY the +/-2 water clip bias.
// The target's game-owned base helper is otherwise the same supported path.
void CAboveWaterSkyboxView::CSkyboxReflectionView::PushView( float waterHeight )
{
	CMatRenderContextPtr pRenderContext( materials );
	MaterialHeightClipMode_t clipMode = MATERIAL_HEIGHTCLIPMODE_DISABLE;
	if ( ( m_DrawFlags & DF_CLIP_Z ) && mat_clipz.GetBool() )
		clipMode = ( m_DrawFlags & DF_CLIP_BELOW ) ?
			MATERIAL_HEIGHTCLIPMODE_RENDER_ABOVE_HEIGHT : MATERIAL_HEIGHTCLIPMODE_RENDER_BELOW_HEIGHT;
	pRenderContext->SetFogZ( waterHeight );
	if ( g_pMaterialSystemHardwareConfig->UseFastClipping() &&
		origin.z > waterHeight - r_eyewaterepsilon.GetFloat() )
	{
		waterHeight = origin.z + r_eyewaterepsilon.GetFloat();
	}
	pRenderContext->SetHeightClipZ( waterHeight );
	pRenderContext->SetHeightClipMode( clipMode );
	render->Push3DView( *this, m_ClearFlags, GetWaterReflectionTexture(), GetFrustum() );
	SetLightmapScaleForWater();
}

void CAboveWaterSkyboxView::CSkyboxReflectionView::Draw()
{
	if ( m_pSky3dParams->scale > 0 )
		VectorScale( origin, 1.0f / m_pSky3dParams->scale, origin );
	VectorAdd( origin, m_pSky3dParams->origin, origin );
	Enable3dSkyboxFog();

	unsigned char **pAreaBits = render->GetAreaBits();
	unsigned char *pSavedAreaBits = *pAreaBits;
	unsigned char skyAreaBits[32];
	Q_memset( skyAreaBits, 0, sizeof( skyAreaBits ) );
	skyAreaBits[m_pSky3dParams->area >> 3] |= 1 << ( m_pSky3dParams->area & 7 );
	*pAreaBits = skyAreaBits;
	render->ViewSetupVis( false, 1, &m_pSky3dParams->origin.Get() );

	view_id_t nSavedViewID = CurrentViewID();
	SetupCurrentView( origin, angles, VIEW_REFLECTION );
	bool bSavedVisOcclusion = r_visocclusion.GetBool();
	r_visocclusion.SetValue( 0 );

	// Main decision 2026-10-23: native scale<=0 reuses local-player pointer
	// bits as a float. Only that undefined/address-dependent branch becomes0.
	float flScaledWaterHeight = 0.0f;
	if ( m_pSky3dParams->scale > 0 )
		flScaledWaterHeight = ( 1.0f / m_pSky3dParams->scale ) * GetOuter()->m_fogInfo.m_flWaterHeight;
	float flWaterHeight = m_pSky3dParams->origin.GetZ() + flScaledWaterHeight;
	DrawSetup( flWaterHeight, m_DrawFlags, 0.0f );
	DrawExecute( flWaterHeight, VIEW_REFLECTION, 0.0f );

	r_visocclusion.SetValue( bSavedVisOcclusion );
	SetupCurrentView( origin, angles, nSavedViewID );
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Flush();
	*render->GetAreaBits() = pSavedAreaBits;
}

// Native uses the opaque main framebuffer, not an underwater-clipped view.
// This does not mark later screen-effect copies current: their consumers still
// refresh FB0 after water and translucents through the normal SDK paths.
static void CopyL4D2WaterRefractionTexture( IMatRenderContext *pRenderContext, const CViewSetup &view )
{
	Rect_t rect;
	rect.x = view.x;
	rect.y = view.y;
	rect.width = view.width;
	rect.height = view.height;
	static bool bTools = CommandLine()->FindParm( "-tools" ) != 0;
	ITexture *pTexture = bTools ? GetPowerOfTwoFrameBufferTexture() : GetFullFrameFrameBufferTexture( 0 );
	pRenderContext->CopyRenderTargetToTextureEx( pTexture, 0, &rect, bTools ? NULL : &rect );
}

// Native C1CED80 fills the clipped underwater background at the authored fog
// distance. Black supplies the current fog color; the active height clip keeps
// this quad out of the above-water refraction image.
void CViewRender::DrawUnderwaterFogBlocker( float flWaterFogEnd )
{
	extern ConVar r_drawunderwaterfogblocker;
	if ( !r_drawunderwaterfogblocker.GetBool() )
		return;

	Vector right = CurrentViewRight();
	right.z = 0.0f;
	if ( right.x > -0.01f && right.x < 0.01f && right.y > -0.01f && right.y < 0.01f )
		return;

	VectorNormalize( right );
	Vector up( 0.0f, 0.0f, 1.0f );
	Vector forward;
	CrossProduct( up, right, forward );
	VectorNormalize( forward );
	CrossProduct( forward, up, right );
	VectorNormalize( right );

	Vector center = CurrentViewOrigin() + forward * flWaterFogEnd;
	Vector rightOffset = ( right * 2.0f ) * flWaterFogEnd;
	Vector upOffset = up * ( flWaterFogEnd * 2.0f );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( m_UnderWaterFogMaterial );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true );
	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

	Vector vertex = center - rightOffset + upOffset;
	meshBuilder.Position3fv( vertex.Base() );
	meshBuilder.AdvanceVertex();
	vertex = center + rightOffset + upOffset;
	meshBuilder.Position3fv( vertex.Base() );
	meshBuilder.AdvanceVertex();
	vertex = center + rightOffset - upOffset;
	meshBuilder.Position3fv( vertex.Base() );
	meshBuilder.AdvanceVertex();
	vertex = center - rightOffset - upOffset;
	meshBuilder.Position3fv( vertex.Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.End();
	pMesh->Draw();
}

void CAboveWaterView::DrawL4D2Water()
{
	if ( m_waterInfo.m_bReflect && !m_waterInfo.m_bReflectSkyboxOnly )
	{
		m_ReflectionView.Setup( m_waterInfo.m_bReflectEntities, m_waterInfo.m_bReflect2DSkybox );
		m_pMainView->AddViewToScene( &m_ReflectionView );
	}
	if ( !m_waterInfo.m_bRefract && !(m_DrawFlags & DF_DRAWSKYBOX) )
		m_ClearFlags |= VIEW_CLEAR_COLOR;

	DrawSetup( m_waterHeight, m_DrawFlags, m_waterZAdjust );
	EnableWorldFog();

	// Native water start/end are independent of the ordinary RGB fog range.
	// Disabled water fog leaves the previous range and plane untouched.
	IMaterial *pWaterMaterial = m_fogInfo.m_pFogVolumeMaterial;
	{
		IMaterialVar *pFogEnable = pWaterMaterial->FindVar( "$fogenable", NULL, true );
		IMaterialVar *pFogStart = pWaterMaterial->FindVar( "$fogstart", NULL, true );
		IMaterialVar *pFogEnd = pWaterMaterial->FindVar( "$fogend", NULL, true );
		CMatRenderContextPtr pRenderContext( materials );
		if ( pFogEnable->GetIntValue() && fog_enable.GetBool() )
		{
			pRenderContext->SetFogZ( m_fogInfo.m_flWaterHeight + 2.0f );
			float start = pFogStart->GetFloatValue();
			float end = pFogEnd->GetFloatValue();
			Vector range( start, end, 0 );
			pRenderContext->SetVectorRenderingParameter( VECTOR_RENDERPARM_L4D2_WATER_FOG_RANGE, range );
		}
	}

	// No refraction/intersection child above water. The supported target API
	// draws its complete opaque world/model phases before this explicit copy.
	DrawExecute( m_waterHeight, CurrentViewID(), m_waterZAdjust, m_waterInfo.m_flWaterFogEnd, (m_DrawFlags & DF_RENDER_WATER) != 0 );
}
