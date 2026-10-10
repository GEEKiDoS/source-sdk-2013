//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: r_sky_ambientmatch (DX12 renderer only): scales $color of the six 2D skybox face materials by the
//          _skyscale_ldr / _skyscale_hdr factor vrad_restir stores in worldspawn, so the sky has the brightness of
//          the map's light_environment ambient. The Sky shaders multiply the sky colour by $color in linear space,
//          which leaves the hue untouched.
//
//          The scale is re-applied every frame (only a changed colour is written), so the cvar, a material reload
//          that re-reads the VMT, or a video restart cannot leave the sky in the wrong state.
//
//===========================================================================//
#include "cbase.h"
#include "igamesystem.h"
#include "mapentities_shared.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/imaterialvar.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_sky_ambientmatch( "r_sky_ambientmatch", "1", FCVAR_ARCHIVE,
	"DX12: scale the 2D skybox so its brightness matches the map's light_environment ambient (factor baked by vrad_restir); 0 = authored sky",
	true, 0, true, 1 );

namespace
{
struct SkyFace_t
{
	IMaterial *pMaterial;
	Vector vecColor;	// authored $color
};

const char *const s_pFaceSuffix[6] = { "rt", "lf", "bk", "ft", "up", "dn" };
SkyFace_t s_Faces[6];
int s_nFaces = 0;		// faces held for the current map; 6 while a baked factor applies
float s_flScale = 1.0f;
}

static void SetSkyScale( float flScale )
{
	for ( int i = 0; i < s_nFaces; ++i )
	{
		bool bFound;
		IMaterialVar *pColor = s_Faces[i].pMaterial->FindVar( "$color", &bFound, false );
		const Vector vecColor = s_Faces[i].vecColor * flScale;
		Vector vecCurrent;
		pColor->GetVecValue( vecCurrent.Base(), 3 );
		if ( vecCurrent != vecColor )
			pColor->SetVecValue( vecColor.x, vecColor.y, vecColor.z );
	}
}

static void ReleaseSky()
{
	SetSkyScale( 1.0f );
	for ( int i = 0; i < s_nFaces; ++i )
		s_Faces[i].pMaterial->DecrementReferenceCount();
	s_nFaces = 0;
}

static void AcquireSky()
{
	const char *pShaderDLL = g_pMaterialSystemHardwareConfig->GetShaderDLLName();
	if ( !pShaderDLL || V_stricmp( pShaderDLL, "stdshader_dx12" ) )
		return;

	// worldspawn is the first entity of the lump; the key lookups stop at its closing brace.
	char szToken[MAPKEY_MAXLENGTH], szSky[MAPKEY_MAXLENGTH], szScale[MAPKEY_MAXLENGTH];
	const char *pWorld = MapEntity_ParseToken( engine->GetMapEntitiesString(), szToken );
	const char *pScaleKey = g_pMaterialSystemHardwareConfig->GetHDRType() != HDR_TYPE_NONE ? "_skyscale_hdr" : "_skyscale_ldr";
	if ( !MapEntity_ExtractValue( pWorld, "skyname", szSky ) || !MapEntity_ExtractValue( pWorld, pScaleKey, szScale ) )
		return;

	for ( ; s_nFaces < 6; ++s_nFaces )
	{
		char szName[MAX_PATH];
		V_snprintf( szName, sizeof( szName ), "skybox/%s%s", szSky, s_pFaceSuffix[s_nFaces] );
		IMaterial *pMaterial = materials->FindMaterial( szName, TEXTURE_GROUP_SKYBOX, false );
		bool bFound;
		IMaterialVar *pColor = pMaterial->FindVar( "$color", &bFound, false );
		if ( pMaterial->IsErrorMaterial() || !bFound || pColor->GetType() != MATERIAL_VAR_TYPE_VECTOR )
		{
			Warning( "r_sky_ambientmatch: %s is missing or has no vector $color; the sky stays unscaled\n", szName );
			ReleaseSky();
			return;
		}
		pMaterial->IncrementReferenceCount();
		s_Faces[s_nFaces].pMaterial = pMaterial;
		pColor->GetVecValue( s_Faces[s_nFaces].vecColor.Base(), 3 );
	}

	s_flScale = (float)atof( szScale );
	DevMsg( "r_sky_ambientmatch: skybox/%s %s %g\n", szSky, pScaleKey, s_flScale );
}

class CSkyAmbientMatchGameSystem : public CAutoGameSystemPerFrame
{
public:
	CSkyAmbientMatchGameSystem() : CAutoGameSystemPerFrame( "CSkyAmbientMatchGameSystem" ) {}
	virtual void LevelInitPostEntity() { AcquireSky(); }
	virtual void LevelShutdownPostEntity() { ReleaseSky(); }
	virtual void Update( float frametime ) { SetSkyScale( r_sky_ambientmatch.GetBool() ? s_flScale : 1.0f ); }
};
static CSkyAmbientMatchGameSystem s_SkyAmbientMatchGameSystem;
