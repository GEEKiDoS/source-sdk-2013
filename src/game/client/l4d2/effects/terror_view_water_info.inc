// Native C1CBAC0. L4D2 has no SDK cheap-water distance/forceexpensive gate.
info.m_bCheapWater = true;
info.m_bReflect = false;
info.m_bRefract = false;
info.m_bReflectEntities = false;
info.m_bDrawWaterSurface = false;
info.m_bOpaqueWater = true;
info.m_bReflectSkyboxOnly = false;
info.m_bReflect2DSkybox = true;
// Native does not read this field on either early return.
info.m_flWaterFogEnd = 0.0f;
IMaterial *pWaterMaterial = fogVolumeInfo.m_pFogVolumeMaterial;
if ( fogVolumeInfo.m_nVisibleFogVolume == -1 || !pWaterMaterial )
	return;
info.m_bDrawWaterSurface = mat_drawwater.GetBool();
if ( !info.m_bDrawWaterSurface )
{
	info.m_bOpaqueWater = false;
	return;
}

bool bForceReflectEntities = r_waterforcereflectentities.GetBool();
info.m_bOpaqueWater = !pWaterMaterial->IsTranslucent();
IMaterialVar *pForceCheap = pWaterMaterial->FindVar( "$forcecheap", NULL, false );
bool bForceCheap = pForceCheap && pForceCheap->IsDefined() && pForceCheap->GetIntValueFast() != 0;
bool bDebugCheapWater = r_debugcheapwater.GetBool();
if ( bDebugCheapWater )
	Msg( "Water material: %s dist to water: %f\nforcecheap: %s\n",
		pWaterMaterial->GetName(), fogVolumeInfo.m_flDistanceToWater, bForceCheap ? "true" : "false" );
// Native reads $forcecheap for the diagnostic only. It does not force cheap water.
if ( r_WaterDrawReflection.GetBool() )
{
	IMaterialVar *pReflectTexture = pWaterMaterial->FindVar( "$reflecttexture", NULL, false );
	info.m_bReflect = pReflectTexture && pReflectTexture->GetType() == MATERIAL_VAR_TYPE_TEXTURE;
}
if ( r_WaterDrawRefraction.GetBool() )
{
	IMaterialVar *pRefract = pWaterMaterial->FindVar( "$refract", NULL, false );
	info.m_bRefract = pRefract && pRefract->GetIntValue() != 0;
	if ( info.m_bRefract )
		info.m_bOpaqueWater = false;
}
if ( info.m_bReflect )
{
	if ( bForceReflectEntities )
		info.m_bReflectEntities = true;
	else
	{
		IMaterialVar *pReflectEntities = pWaterMaterial->FindVar( "$reflectentities", NULL, false );
		info.m_bReflectEntities = pReflectEntities && pReflectEntities->GetIntValueFast() != 0;
	}
	IMaterialVar *pSkyboxOnly = pWaterMaterial->FindVar( "$reflectskyboxonly", NULL, false );
	info.m_bReflectSkyboxOnly = pSkyboxOnly && pSkyboxOnly->GetIntValueFast() != 0;
	bool bFound;
	IMaterialVar *pReflect2D = pWaterMaterial->FindVar( "$reflect2dskybox", &bFound, false );
	if ( bFound && pReflect2D )
		info.m_bReflect2DSkybox = pReflect2D->GetIntValueFast() != 0;
}
info.m_bCheapWater = !info.m_bReflect && !info.m_bRefract;
IMaterialVar *pFogEnable = pWaterMaterial->FindVar( "$fogenable", NULL, false );
if ( pFogEnable && pFogEnable->GetIntValueFast() != 0 )
{
	IMaterialVar *pFogEnd = pWaterMaterial->FindVar( "$fogend", NULL, false );
	if ( pFogEnd )
		info.m_flWaterFogEnd = pFogEnd->GetFloatValue();
}
if ( bDebugCheapWater )
	Warning( "refract: %s reflect: %s\n", info.m_bRefract ? "true" : "false", info.m_bReflect ? "true" : "false" );
