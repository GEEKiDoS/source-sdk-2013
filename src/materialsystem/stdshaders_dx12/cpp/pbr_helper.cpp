//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Material side of the DX12 PBR shader set (see pbr_helper.h).
//
// Structure mirrors DrawLightmappedGeneric_DX9_Internal / DrawVertexLitGeneric_DX9_Internal: a per-material context keeps a
// semi-static command buffer (texture binds and the PBRMaterialPS/VS constants staged through the legacy registers their
// @legacy annotations name: ps c0..c14, vs c48..c55) that is rebuilt when the material vars change; each draw replays it and
// adds the per-draw state (modulation color, local lights, morph, fog, shader indices).
//
//===========================================================================//
#include "pbr_helper.h"
#include "commandbuilder.h"
#include "convar.h"
#include "mathlib/mathlib.h"

#include "pbr_world_vs51.inc"
#include "pbr_world_ps51.inc"
#include "pbr_model_vs51.inc"
#include "pbr_model_ps51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// mat_pbr_override
//-----------------------------------------------------------------------------
// The client commits the requested value at FRAME_START inside a material transaction and then refreshes every loaded
// material, so draws and snapshots only ever see the committed value (DX12PbrOverride).
static void OnPbrOverrideChanged( IConVar *pVar, const char *, float )
{
	if ( IShaderAPIDX12Lighting *lighting = DX12ShadowmapLighting() )
		lighting->RequestPbrOverride( static_cast< ConVar * >( pVar )->GetBool() );
}

ConVar mat_pbr_override( "mat_pbr_override", "0", FCVAR_ARCHIVE,
	"1 = route LightmappedGeneric/VertexLitGeneric/WorldVertexTransition through the PBR adapters", OnPbrOverrideChanged );

bool DX12PbrOverride()
{
	IShaderAPIDX12Lighting *lighting = DX12ShadowmapLighting();
	return lighting && lighting->PbrOverride();
}

namespace
{
//-----------------------------------------------------------------------------
// Fixed layout shared with the native shaders
//-----------------------------------------------------------------------------
// Pixel shader texture slots, -1 = the logical has no such texture.
struct PBRSamplers
{
	int base, bump, envmap, lightmap, detail, envmapMask, mrao, specular, emission, thickness, base2, bump2, blendModulate, phongExponent, bumpMask;
};
const PBRSamplers kWorldSamplers = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, -1, 13 };
const PBRSamplers kModelSamplers = { 0, 1, 2, -1, 3, 4, 5, 6, 7, 8, -1, -1, -1, 9, -1 };

// SetPixelShaderStateAmbientLightCube / CommitPixelShaderLighting / SetPixelShaderFogParams feed the DX12PSEngine
// block whatever register they are given; these only mirror into the legacy register file, outside c0..c14.
const int kAmbientCubeRegister = 15;
const int kLightInfoRegister = 21;
const int kFogRegister = 27;
// PBRMaterialPS c30..c34 (detail tint/blend factor, world flags, blend flags, mask transform): clear of the engine mirrors above.
const int kExtraMaterialRegister = 30;
// What InitParamsPBRDefaults leaves in $roughness of a legacy-replacement material that did not set it: any real value is >= 0.
const float kUnsetRoughness = -1.0f;

const float kDefaultRoughness = 0.6f;
const float kDefaultThickness = 0.5f;
const float kDielectricF0 = 0.04f;

class CPBRContext : public CBasePerMaterialContextData
{
public:
	CCommandBufferBuilder< CFixedCommandStorageBuffer< 1500 > > m_SemiStaticCmdsOut;
	DX12ShadowmapSnapshot m_Snapshot;
	float m_SelfIllumTint[3] = {};
	bool m_bModel = false;
	bool m_bEnvmap = false;
	bool m_bBump = false;
	bool m_bAmbientOnly = false;
};

//-----------------------------------------------------------------------------
// Material var access
//-----------------------------------------------------------------------------
bool IsModel( IMaterialVar **params, const PBR_Vars_t &v )
{
	// PBR_Metalness / PBR_Specular mark studio models with the engine's own $model flag (a reserved material flag name:
	// the VMT parser consumes "$model" as MATERIAL_VAR_MODEL, so a shader parameter of that name never receives a value).
	return v.model || ( v.standalone && ( params[FLAGS]->GetIntValue() & MATERIAL_VAR_MODEL ) != 0 );
}

float DefinedFloat( IMaterialVar **params, int var, float defaultValue )
{
	return IS_PARAM_DEFINED( var ) ? params[var]->GetFloatValue() : defaultValue;
}

// sRGB-coded VMT color to linear. Components above 1 are HDR boosts, not sRGB codes (and GammaToLinear clamps them),
// the rule SetPixelShaderConstantGammaToLinear applies.
void LinearColor( IMaterialVar **params, int var, float defaultValue, float *rgb )
{
	if ( !IS_PARAM_DEFINED( var ) )
	{
		rgb[0] = rgb[1] = rgb[2] = defaultValue;
		return;
	}
	params[var]->GetVecValue( rgb, 3 );
	for ( int i = 0; i < 3; ++i )
		rgb[i] = rgb[i] > 1.0f ? rgb[i] : GammaToLinear( rgb[i] );
}

bool DetailAdditive( IMaterialVar **params, const PBR_Vars_t &v )
{
	// pbr_*_ps51 combine detail either mod2x (0) or additive (1); anything else is declined by PBR_LegacySupported.
	return GetIntParam( v.detailBlendMode, params ) == 1;
}

bool LightingOnlyDebug( IMaterialVar **params )
{
	static ConVarRef fullbright( "mat_fullbright" );
	return fullbright.GetInt() == 2 && !IS_FLAG_SET( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
}

//-----------------------------------------------------------------------------
// Everything the snapshot and the semi-static constants derive from the material
//-----------------------------------------------------------------------------
struct PBRState
{
	bool model, layered, hasBase, hasBase2, hasBump, ssBump, hasBump2, hasBumpMask, hasBlendModulate, hasMrao, hasSpecular, hasEnvmap, maskedBlending;
	bool hasDetail, detailAdditive, hasEmission, hasThickness, alphaTested, fullyOpaque, lightingOnly, vertexColor, noDiffuseBump;
	int envmapMask;		// 0 none, 1 $envmapmask, 2 base alpha, 3 normal map alpha
	int emissionMode;	// 0 none, 1 $selfillum (base alpha), 2 $emissiontexture
	int phong;			// bit 0 legacy phong, bit 1 $phongexponenttexture, bit 2 phong mask in base alpha
	bool explicitRoughness;	// $mraotexture or $roughness authored (or a native PBR shader): beats Phong and environment-mask gloss
	bool glossFromMask;		// legacy replacement without explicit roughness or Phong: roughness = 1 - environment mask (g_PbrFlags2.y)
	BlendType_t blendType;
};

PBRState Evaluate( CBaseVSShaderDX12 *pShader, IMaterialVar **params, const PBR_Vars_t &v )
{
	PBRState s = {};
	s.model = IsModel( params, v );
	s.hasBase = IsTextureSet( v.base, params );
	s.hasBump = IsTextureSet( v.bump, params );
	s.ssBump = !s.model && s.hasBump && GetIntParam( v.ssBump, params ) != 0;
	// The legacy helper's layers: $basetexture2 needs a base texture, $bumpmap2 a bump map, and either one makes the vertex alpha a blend
	// factor. The blend modulate texture is read only for the base layers.
	s.hasBase2 = !s.model && s.hasBase && IsTextureSet( v.base2, params );
	s.hasBump2 = !s.model && s.hasBump && IsTextureSet( v.bump2, params );
	s.layered = s.hasBase2 || s.hasBump2;
	s.hasBlendModulate = s.hasBase2 && IsTextureSet( v.blendModulate, params );
	s.maskedBlending = s.layered && GetIntParam( v.maskedBlending, params ) != 0;
	s.hasMrao = IsTextureSet( v.mrao, params );
	s.hasSpecular = v.specularWorkflow && IsTextureSet( v.specularTexture, params );
	// A sphere map is a 2D texture; the shaders only sample cubemaps.
	s.hasEnvmap = IsTextureSet( v.envmap, params ) && !IS_FLAG_SET( MATERIAL_VAR_ENVMAPSPHERE );
	s.hasDetail = IsTextureSet( v.detail, params );
	// $bumpmask (static mask normal over two scrolling ones) excludes the layers the legacy helper excludes it from.
	s.hasBumpMask = s.hasBump2 && !s.ssBump && IsTextureSet( v.bumpMask, params ) && !IS_FLAG_SET( MATERIAL_VAR_SELFILLUM ) && !s.hasDetail && !s.hasBase2 &&
		GetIntParam( v.baseTextureNoEnvmap, params ) == 0;
	s.detailAdditive = DetailAdditive( params, v );
	s.hasEmission = IsTextureSet( v.emissionTexture, params );
	s.hasThickness = IsTextureSet( v.thicknessTexture, params );
	s.lightingOnly = LightingOnlyDebug( params );
	// World $vertexcolor scales albedo and alpha by the mesh COLOR stream (on models the flag makes the stream replace the lighting).
	s.vertexColor = !s.model && IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR );
	// $nodiffusebumplighting: diffuse from the flat lightmap plane and the vertex normal; the normal map still shades reflections.
	s.noDiffuseBump = !s.model && s.hasBump && IsBoolSet( v.noDiffuseBumpLighting, params );

	if ( s.hasEnvmap )
	{
		if ( s.hasBump && IS_FLAG_SET( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK ) )
			s.envmapMask = 3;
		else if ( s.hasBase && IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK ) )
			s.envmapMask = 2;
		else if ( IsTextureSet( v.envmapMask, params ) )
			s.envmapMask = 1;
	}
	s.emissionMode = s.hasEmission ? 2 : ( s.hasBase && IS_FLAG_SET( MATERIAL_VAR_SELFILLUM ) ? 1 : 0 );

	// Precedence of roughness sources: explicit PBR inputs > legacy Phong exponent > environment mask gloss > default.
	s.explicitRoughness = v.standalone || s.hasMrao || params[v.roughness]->GetFloatValue() >= 0.0f;
	if ( s.model && v.phong != -1 && params[v.phong]->GetIntValue() != 0 && !s.explicitRoughness )
	{
		s.phong = 1;
		if ( IsTextureSet( v.phongExponentTexture, params ) )
			s.phong |= 2;
		if ( GetIntParam( v.baseMapAlphaPhongMask, params ) != 0 )
			s.phong |= 4;
	}
	s.glossFromMask = !s.explicitRoughness && s.hasEnvmap && ( s.phong & 1 ) == 0;

	s.alphaTested = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) != 0;
	s.blendType = pShader->EvaluateBlendRequirements( s.hasBase ? v.base : v.envmapMask, s.hasBase );
	s.fullyOpaque = s.blendType != BT_BLENDADD && s.blendType != BT_BLEND && !s.alphaTested;
	return s;
}

//-----------------------------------------------------------------------------
// Snapshot
//-----------------------------------------------------------------------------
void Snapshot( CBaseVSShaderDX12 *pShader, IMaterialVar **params, IShaderShadow *pShaderShadow, const PBR_Vars_t &v, const PBRState &s, bool bEnhanced )
{
	const PBRSamplers &sm = s.model ? kModelSamplers : kWorldSamplers;
	const bool hdrNone = g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE;

	// The kill itself is the engine alpha-test state (cAlphaTest through DX12FinishPixel); ALPHATEST selects the variant.
	pShaderShadow->EnableAlphaTest( s.alphaTested );
	if ( v.alphaTestReference != -1 && params[v.alphaTestReference]->GetFloatValue() > 0.0f )
		pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GEQUAL, params[v.alphaTestReference]->GetFloatValue() );

	pShader->SetBlendingShadowState( s.blendType );

	const auto enable = [&]( int sampler, bool srgb )
	{
		pShaderShadow->EnableTexture( Sampler_t( sampler ), true );
		pShaderShadow->EnableSRGBRead( Sampler_t( sampler ), srgb );
	};
	enable( sm.base, true );
	if ( !s.model )
		enable( sm.lightmap, hdrNone );
	if ( s.hasBump )
		enable( sm.bump, false );
	if ( s.hasEnvmap )
		enable( sm.envmap, hdrNone );
	if ( s.hasDetail )
		enable( sm.detail, s.detailAdditive );
	if ( s.envmapMask == 1 )
		enable( sm.envmapMask, false );
	if ( s.hasMrao )
		enable( sm.mrao, false );
	if ( s.hasSpecular )
		enable( sm.specular, true );
	if ( s.hasEmission )
		enable( sm.emission, true );
	if ( s.hasThickness )
		enable( sm.thickness, false );
	if ( s.hasBase2 )
		enable( sm.base2, true );
	if ( s.hasBump2 )
		enable( sm.bump2, false );
	if ( s.hasBlendModulate )
		enable( sm.blendModulate, false );
	if ( s.hasBumpMask )
		enable( sm.bumpMask, false );
	if ( s.phong & 2 )
		enable( sm.phongExponent, false );

	if ( s.model )
	{
		// Same stream layout as the legacy bump vertex shader: compressed vertices, tangent as user data, vertex id for morphing.
		SET_FLAGS2( MATERIAL_VAR2_USES_VERTEXID );
		pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED, 1, 0, s.hasBump ? 4 : 0 );

		DECLARE_STATIC_VERTEX_SHADER( pbr_model_vs51 );
		SET_STATIC_VERTEX_SHADER_COMBO( TANGENT, s.hasBump );
		SET_STATIC_VERTEX_SHADER( pbr_model_vs51 );

		DECLARE_STATIC_PIXEL_SHADER( pbr_model_ps51 );
		SET_STATIC_PIXEL_SHADER_COMBO( TANGENT, s.hasBump );
		SET_STATIC_PIXEL_SHADER_COMBO( ENHANCED, bEnhanced );
		SET_STATIC_PIXEL_SHADER_COMBO( ALPHATEST, s.alphaTested );
		SET_STATIC_PIXEL_SHADER( pbr_model_ps51 );
	}
	else
	{
		// texcoord0 base, texcoord1 lightmap, texcoord2 bump lightmap offset; COLOR carries $vertexcolor and the layer blend.
		pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TANGENT_S | VERTEX_TANGENT_T | ( s.layered || s.vertexColor ? VERTEX_COLOR : 0 ), 3, 0, 0 );

		DECLARE_STATIC_VERTEX_SHADER( pbr_world_vs51 );
		SET_STATIC_VERTEX_SHADER_COMBO( LAYERED, s.layered );
		SET_STATIC_VERTEX_SHADER( pbr_world_vs51 );

		DECLARE_STATIC_PIXEL_SHADER( pbr_world_ps51 );
		SET_STATIC_PIXEL_SHADER_COMBO( LAYERED, s.layered );
		SET_STATIC_PIXEL_SHADER_COMBO( ALPHATEST, s.alphaTested );
		SET_STATIC_PIXEL_SHADER( pbr_world_ps51 );
	}

	pShaderShadow->EnableAlphaWrites( s.fullyOpaque );
	pShaderShadow->EnableSRGBWrite( true );
	pShader->DefaultFog();
}

//-----------------------------------------------------------------------------
// Semi-static constants and texture binds
//-----------------------------------------------------------------------------
// The rows CCommandBufferBuilder::SetVertexShaderTextureScaledTransform stages, identity for an absent transform.
void TextureTransform( IMaterialVar **params, int transformVar, float scale, float ( &rows )[2][4] )
{
	rows[0][0] = 1.0f; rows[0][1] = 0.0f; rows[0][2] = 0.0f; rows[0][3] = 0.0f;
	rows[1][0] = 0.0f; rows[1][1] = 1.0f; rows[1][2] = 0.0f; rows[1][3] = 0.0f;
	if ( transformVar != -1 && params[transformVar]->GetType() == MATERIAL_VAR_TYPE_MATRIX )
	{
		const VMatrix &mat = params[transformVar]->GetMatrixValue();
		for ( int i = 0; i < 4; ++i )
		{
			rows[0][i] = mat[0][i];
			rows[1][i] = mat[1][i];
		}
	}
	rows[0][0] *= scale; rows[0][1] *= scale; rows[0][3] *= scale;
	rows[1][0] *= scale; rows[1][1] *= scale; rows[1][3] *= scale;
}

void StageMaterial( CPBRContext &ctx, CBaseVSShaderDX12 *pShader, IMaterialVar **params, const PBR_Vars_t &v, const PBRState &s )
{
	const PBRSamplers &sm = s.model ? kModelSamplers : kWorldSamplers;
	const auto sampler = []( int slot ) { return Sampler_t( slot ); };

	// PBRMaterialPS, ps c0..c14. c0 (modulation) and c10.w (local light count) are rewritten per draw.
	float ps[15][4] = {};

	// c1: $metalness / $roughness / $ambientocclusion are the values, or the multipliers of $mraotexture's channels
	// (InitParamsPBRDefaults made the unset ones 1 with an MRAO texture)
	ps[1][0] = params[v.metalness]->GetFloatValue();
	ps[1][1] = s.explicitRoughness ? params[v.roughness]->GetFloatValue() : kDefaultRoughness;
	ps[1][2] = params[v.ambientOcclusion]->GetFloatValue();
	ps[1][3] = v.specularWorkflow ? 1.0f : 0.0f;
	if ( s.phong & 1 )
	{
		// Phong adapter: roughness from the exponent; F0 = 0.04 * phong mask is the shader's.
		const float exponent = clamp( DefinedFloat( params, v.phongExponent, 5.0f ), 1.0f, 150.0f );
		ps[1][0] = 0.0f;
		ps[1][1] = powf( 2.0f / ( exponent + 2.0f ), 0.25f );
	}

	// c2: F0 without $speculartexture (linear); w is unused (the alpha test is engine state)
	LinearColor( params, v.specularColor, kDielectricF0, ps[2] );

	ps[3][0] = s.hasMrao ? 1.0f : 0.0f;
	ps[3][1] = s.ssBump ? 2.0f : ( s.hasBump ? 1.0f : 0.0f );
	ps[3][2] = s.hasSpecular ? 1.0f : 0.0f;
	ps[3][3] = float( s.emissionMode );

	ps[4][0] = s.hasEnvmap ? 1.0f : 0.0f;
	ps[4][1] = float( s.envmapMask );
	ps[4][2] = s.hasDetail ? 1.0f : 0.0f;
	ps[4][3] = s.detailAdditive ? 1.0f : 0.0f;

	// c5: envmap tint, w = parallax. Reflections are off exactly where the legacy helpers turn them off.
	if ( s.hasEnvmap && g_pConfig->bShowSpecular && !s.lightingOnly )
		LinearColor( params, v.envmapTint, 1.0f, ps[5] );
	// The parallax box is three vec4 rows the engine zero-initializes: an all-zero box is no box.
	if ( s.hasEnvmap )
	{
		const int obb[3] = { v.parallaxObb1, v.parallaxObb2, v.parallaxObb3 };
		float box[3][4];
		bool hasBox = false;
		for ( int row = 0; row < 3; ++row )
		{
			memcpy( box[row], params[obb[row]]->GetVecValue(), sizeof( box[row] ) );
			for ( float value : box[row] )
				hasBox = hasBox || value != 0.0f;
		}
		if ( hasBox )
		{
			ps[5][3] = 1.0f;
			memcpy( ps[6], box, sizeof( box ) );
			params[v.envmapOrigin]->GetVecValue( ps[9], 3 );
		}
	}

	LinearColor( params, v.selfIllumTint, 1.0f, ps[10] );
	memcpy( ctx.m_SelfIllumTint, ps[10], sizeof( ctx.m_SelfIllumTint ) );

	const float anisotropyRotation = DEG2RAD( params[v.anisotropyRotation]->GetFloatValue() );
	ps[11][0] = clamp( params[v.subsurface]->GetFloatValue(), 0.0f, 1.0f );
	ps[11][1] = clamp( params[v.backlight]->GetFloatValue(), 0.0f, 1.0f );
	ps[11][2] = params[v.thickness]->GetFloatValue();
	ps[11][3] = clamp( params[v.anisotropy]->GetFloatValue(), -1.0f, 1.0f );
	LinearColor( params, v.subsurfaceTint, 1.0f, ps[12] );
	ps[12][3] = s.hasThickness ? 1.0f : 0.0f;
	LinearColor( params, v.backlightTint, 1.0f, ps[13] );
	ps[13][3] = cosf( anisotropyRotation );
	ps[14][0] = sinf( anisotropyRotation );
	ps[14][1] = s.model && IS_FLAG_SET( MATERIAL_VAR_HALFLAMBERT ) ? 1.0f : 0.0f;
	ps[14][2] = float( s.phong );
	ps[14][3] = s.hasBlendModulate ? 1.0f : 0.0f;

	// PBRMaterialPS c30: $detailtint (world: raw, as the LightmappedGeneric shader multiplies it; models: gamma to linear as the
	// VertexLitGeneric helper stages it) and $detailblendfactor; c31: $nodiffusebumplighting, gloss from the mask, $basetexture2 layer
	float extra[2][4] = { { 1.0f, 1.0f, 1.0f, GetFloatParam( v.detailBlendFactor, params, 1.0f ) },
		{ s.noDiffuseBump ? 1.0f : 0.0f, s.glossFromMask ? 1.0f : 0.0f, s.hasBase2 ? 1.0f : 0.0f, !s.model && !v.standalone ? 1.0f : 0.0f } };
	if ( s.hasDetail && IS_PARAM_DEFINED( v.detailTint ) )
	{
		if ( s.model )
			LinearColor( params, v.detailTint, 1.0f, extra[0] );
		else
			params[v.detailTint]->GetVecValue( extra[0], 3 );
	}
	// PBRMaterialPS c32: $maskedblending, $alpha2 (a factor only when > 0, as the LightmappedGeneric helper stages it), the $bumpmap2 and
	// $bumpmask layers; c33/c34: the one transform the legacy vertex shader keeps for the $envmapmask texture and the second normal map
	// of $bumpmask (VERTEX_SHADER_SHADER_SPECIFIC_CONST_4: $envmapmasktransform with an $envmapmask texture, else $bumptransform2).
	// Models stage zeros and an identity: only the world pixel shader reads them.
	float world[3][4] = {};
	world[0][0] = s.maskedBlending ? 1.0f : 0.0f;
	world[0][1] = IS_PARAM_DEFINED( v.alpha2 ) && params[v.alpha2]->GetFloatValue() > 0.0f ? params[v.alpha2]->GetFloatValue() : 1.0f;
	world[0][2] = s.hasBump2 ? 1.0f : 0.0f;
	world[0][3] = s.hasBumpMask ? 1.0f : 0.0f;
	float rows[2][4];
	TextureTransform( params, IsTextureSet( v.envmapMask, params ) ? v.envmapMaskTransform : v.bumpTransform2, 1.0f, rows );
	memcpy( world[1], rows, sizeof( rows ) );
	// PBRMaterialVS c56: $vertexcolor
	const float vertexColor[4] = { s.vertexColor ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };

	// PBRMaterialVS, vs c48..c55: base, bump, detail (scaled by $detailscale), blend-modulate transforms
	float vs[4][2][4];
	TextureTransform( params, v.baseTransform, 1.0f, vs[0] );
	TextureTransform( params, v.bumpTransform, 1.0f, vs[1] );
	TextureTransform( params, IS_PARAM_DEFINED( v.detailTransform ) ? v.detailTransform : v.baseTransform, DefinedFloat( params, v.detailScale, 4.0f ), vs[2] );
	TextureTransform( params, v.blendMaskTransform, 1.0f, vs[3] );

	ctx.m_bModel = s.model;
	ctx.m_bEnvmap = s.hasEnvmap;
	ctx.m_bBump = s.hasBump;
	ctx.m_bAmbientOnly = IsBoolSet( v.ambientOnly, params );

	auto &cmds = ctx.m_SemiStaticCmdsOut;
	cmds.Reset();
	cmds.SetPixelShaderConstant( 0, ps[0], 15 );
	cmds.SetPixelShaderConstant( kExtraMaterialRegister, extra[0], 2 );
	cmds.SetPixelShaderConstant( kExtraMaterialRegister + 2, world[0], 3 );
	cmds.SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, vs[0][0], 8 );
	cmds.SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 + 8, vertexColor, 1 );
	cmds.SetPixelShaderFogParams( kFogRegister );

	// Textures
	if ( s.hasBase )
		cmds.BindTexture( pShader, sampler( sm.base ), v.base, v.baseFrame );
	else
		cmds.BindStandardTexture( sampler( sm.base ), s.hasEnvmap ? TEXTURE_BLACK : TEXTURE_WHITE );
	if ( !s.model )
		cmds.BindStandardTexture( sampler( sm.lightmap ), TEXTURE_LIGHTMAP );
	if ( s.hasBump )
	{
		if ( !g_pConfig->m_bFastNoBump )
			cmds.BindTexture( pShader, sampler( sm.bump ), v.bump, v.bumpFrame );
		else
			cmds.BindStandardTexture( sampler( sm.bump ), TEXTURE_NORMALMAP_FLAT );
	}
	if ( s.hasDetail )
		cmds.BindTexture( pShader, sampler( sm.detail ), v.detail, v.detailFrame );
	if ( s.envmapMask == 1 )
		cmds.BindTexture( pShader, sampler( sm.envmapMask ), v.envmapMask, v.envmapMaskFrame );
	if ( s.hasMrao )
		cmds.BindTexture( pShader, sampler( sm.mrao ), v.mrao, -1 );
	if ( s.hasSpecular )
		cmds.BindTexture( pShader, sampler( sm.specular ), v.specularTexture, -1 );
	if ( s.hasEmission )
		cmds.BindTexture( pShader, sampler( sm.emission ), v.emissionTexture, -1 );
	if ( s.hasThickness )
		cmds.BindTexture( pShader, sampler( sm.thickness ), v.thicknessTexture, -1 );
	if ( s.hasBase2 )
		cmds.BindTexture( pShader, sampler( sm.base2 ), v.base2, v.base2Frame );
	if ( s.hasBump2 )
	{
		if ( !g_pConfig->m_bFastNoBump )
			cmds.BindTexture( pShader, sampler( sm.bump2 ), v.bump2, v.bump2Frame );
		else
			cmds.BindStandardTexture( sampler( sm.bump2 ), TEXTURE_NORMALMAP_FLAT );
	}
	if ( s.hasBlendModulate )
		cmds.BindTexture( pShader, sampler( sm.blendModulate ), v.blendModulate, -1 );
	if ( s.hasBumpMask )
		cmds.BindTexture( pShader, sampler( sm.bumpMask ), v.bumpMask, -1 );
	if ( s.phong & 2 )
		cmds.BindTexture( pShader, sampler( sm.phongExponent ), v.phongExponentTexture, -1 );

	// mat_fullbright 2: lighting only, grey albedo and no reflections (envmap tint already zeroed above)
	if ( s.lightingOnly )
	{
		cmds.BindStandardTexture( sampler( sm.base ), s.emissionMode == 1 ? TEXTURE_GREY_ALPHA_ZERO : TEXTURE_GREY );
		if ( s.hasBase2 )
			cmds.BindStandardTexture( sampler( sm.base2 ), TEXTURE_GREY );
		if ( s.hasDetail )
			cmds.BindStandardTexture( sampler( sm.detail ), TEXTURE_GREY );
	}
	cmds.End();
}
} // namespace

//-----------------------------------------------------------------------------
// Var tables
//-----------------------------------------------------------------------------
// Members both legacy var tables have.
template < class T >
static void FillSharedVars( PBR_Vars_t &v, const T &info )
{
	v.base = info.m_nBaseTexture;
	v.baseFrame = info.m_nBaseTextureFrame;
	v.baseTransform = info.m_nBaseTextureTransform;
	v.bump = info.m_nBumpmap;
	v.bumpFrame = info.m_nBumpFrame;
	v.bumpTransform = info.m_nBumpTransform;
	v.envmap = info.m_nEnvmap;
	v.envmapFrame = info.m_nEnvmapFrame;
	v.envmapMask = info.m_nEnvmapMask;
	v.envmapMaskFrame = info.m_nEnvmapMaskFrame;
	v.envmapTint = info.m_nEnvmapTint;
	v.detail = info.m_nDetail;
	v.detailFrame = info.m_nDetailFrame;
	v.detailScale = info.m_nDetailScale;
	v.detailBlendMode = info.m_nDetailTextureCombineMode;
	v.detailBlendFactor = info.m_nDetailTextureBlendFactor;
	v.detailTint = info.m_nDetailTint;
	v.selfIllumTint = info.m_nSelfIllumTint;
	v.alphaTestReference = info.m_nAlphaTestReference;
}

void PBR_FillWorldVars( PBR_Vars_t &v, const LightmappedGeneric_DX9_Vars_t &info )
{
	v.model = false;
	FillSharedVars( v, info );
	v.base2 = info.m_nBaseTexture2;
	v.base2Frame = info.m_nBaseTexture2Frame;
	v.bump2 = info.m_nBumpmap2;
	v.bump2Frame = info.m_nBumpFrame2;
	v.blendModulate = info.m_nBlendModulateTexture;
	v.blendMaskTransform = info.m_nBlendMaskTransform;
	v.maskedBlending = info.m_nMaskedBlending;
	v.alpha2 = info.m_nAlpha2;
	v.envmapMaskTransform = info.m_nEnvmapMaskTransform;
	v.bumpTransform2 = info.m_nBumpTransform2;
	v.bumpMask = info.m_nBumpMask;
	v.baseTextureNoEnvmap = info.m_nBaseTextureNoEnvmap;
	v.ssBump = info.m_nSelfShadowedBumpFlag;
	v.seamlessScale = info.m_nSeamlessMappingScale;
	v.lightWarp = info.m_nLightWarpTexture;
	v.outline = info.m_nOutline;
	v.softEdges = info.m_nSoftEdges;
	v.noDiffuseBumpLighting = info.m_nNoDiffuseBumpLighting;
}

void PBR_FillModelVars( PBR_Vars_t &v, const VertexLitGeneric_DX9_Vars_t &info )
{
	v.model = true;
	FillSharedVars( v, info );
	v.detailTransform = info.m_nDetailTextureTransform;
	v.phong = info.m_nPhong;
	v.phongExponent = info.m_nPhongExponent;
	v.phongExponentTexture = info.m_nPhongExponentTexture;
	v.baseMapAlphaPhongMask = info.m_nBaseMapAlphaPhongMask;
	v.lightWarp = info.m_nDiffuseWarpTexture;
	v.treeSway = info.m_nTreeSway;
	v.distanceAlpha = info.m_nDistanceAlpha;
	v.seamlessBase = info.m_nSeamlessBase;
	v.seamlessDetail = info.m_nSeamlessDetail;
	v.selfIllumFresnel = info.m_nSelfIllumFresnel;
	v.selfIllumMask = info.m_nSelfIllumMask;
	v.lightmap = info.m_nLightmap;
	v.ambientOnly = info.m_nAmbientOnly;
}

//-----------------------------------------------------------------------------
// Init
//-----------------------------------------------------------------------------
void InitParamsPBRDefaults( IMaterialVar **params, const PBR_Vars_t &v )
{
	// With $mraotexture the scalars multiply its channels, so unset means 1; without it they are the values.
	const bool mrao = IS_PARAM_DEFINED( v.mrao );
	InitFloatParam( v.metalness, params, mrao ? 1.0f : 0.0f );
	InitFloatParam( v.roughness, params, mrao ? 1.0f : ( v.standalone ? kDefaultRoughness : kUnsetRoughness ) );
	InitFloatParam( v.ambientOcclusion, params, 1.0f );
	InitFloatParam( v.thickness, params, kDefaultThickness );
	// $specularcolor is authored like $color (gamma coded); the default is the linear dielectric F0.
	if ( !params[v.specularColor]->IsDefined() )
		params[v.specularColor]->SetVecValue( LinearToGamma( kDielectricF0 ), LinearToGamma( kDielectricF0 ), LinearToGamma( kDielectricF0 ) );
}

void InitParamsPBR( IMaterialVar **params, const PBR_Vars_t &v )
{
	InitParamsPBRDefaults( params, v );
	InitFloatParam( v.detailScale, params, 4.0f );

	const bool model = IsModel( params, v );

	// $normaltexture is an alias of $bumpmap
	if ( v.normalTexture != -1 && params[v.normalTexture]->IsDefined() && !params[v.bump]->IsDefined() )
		params[v.bump]->SetStringValue( params[v.normalTexture]->GetStringValue() );

	if ( !params[v.envmap]->IsDefined() )
		params[v.envmap]->SetStringValue( "env_cubemap" );
	// If mat_specular 0, then get rid of envmap
	if ( !g_pConfig->UseSpecular() )
		params[v.envmap]->SetUndefined();

	const bool bump = g_pConfig->UseBumpmapping() && params[v.bump]->IsDefined();
	if ( !bump )
		CLEAR_FLAGS( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK );

	if ( IS_FLAG_SET( MATERIAL_VAR_DECAL ) )
		SET_FLAGS( MATERIAL_VAR_NO_DEBUG_OVERRIDE );

	if ( model )
	{
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );
		if ( bump )
			SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
	}
	else
	{
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_LIGHTMAP );
		if ( bump )
			SET_FLAGS2( MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP );
		// The world vertex shader always reads the tangent space.
		SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
	}
}

void InitPBR( CBaseVSShaderDX12 *pShader, IMaterialVar **params, const PBR_Vars_t &v )
{
	// A texture the legacy helper already loaded is a texture var; only the rest loads here.
	const auto load = [&]( int var, int flags = 0 )
	{
		if ( IS_PARAM_DEFINED( var ) && !params[var]->IsTexture() )
			pShader->LoadTexture( var, flags );
	};

	load( v.base, TEXTUREFLAGS_SRGB );
	load( v.base2, TEXTUREFLAGS_SRGB );
	if ( g_pConfig->UseBumpmapping() )
	{
		if ( IS_PARAM_DEFINED( v.bump ) && !params[v.bump]->IsTexture() )
			pShader->LoadBumpMap( v.bump );
		if ( IS_PARAM_DEFINED( v.bump2 ) && !params[v.bump2]->IsTexture() )
			pShader->LoadBumpMap( v.bump2 );
	}
	load( v.blendModulate );
	load( v.detail, DetailAdditive( params, v ) ? TEXTUREFLAGS_SRGB : 0 );
	if ( IS_PARAM_DEFINED( v.envmap ) && !params[v.envmap]->IsTexture() && !IS_FLAG_SET( MATERIAL_VAR_ENVMAPSPHERE ) )
		pShader->LoadCubeMap( v.envmap, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE ? TEXTUREFLAGS_SRGB : 0 );
	load( v.envmapMask );
	load( v.mrao );
	load( v.specularTexture, TEXTUREFLAGS_SRGB );
	load( v.emissionTexture, TEXTUREFLAGS_SRGB );
	load( v.thicknessTexture );
	load( v.phongExponentTexture );

	if ( !v.standalone )
		return;

	// What the legacy Init/InitParams helpers do for the other shaders: base alpha only carries self illumination or the
	// envmap mask when the texture has an alpha channel, and then it cannot also be alpha tested.
	if ( !IS_PARAM_DEFINED( v.base ) || !params[v.base]->GetTextureValue()->IsTranslucent() )
	{
		CLEAR_FLAGS( MATERIAL_VAR_SELFILLUM );
		CLEAR_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );
	}
	if ( IS_FLAG_SET( MATERIAL_VAR_SELFILLUM ) || IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK ) )
		CLEAR_FLAGS( MATERIAL_VAR_ALPHATEST );
	if ( IsModel( params, v ) && IS_PARAM_DEFINED( v.bump ) && g_pConfig->UseBumpmapping() )
		SET_FLAGS2( MATERIAL_VAR2_DIFFUSE_BUMPMAPPED_MODEL );
}

//-----------------------------------------------------------------------------
// Legacy features the PBR shaders cannot render
//-----------------------------------------------------------------------------
bool PBR_LegacySupported( IMaterialVar **params, const PBR_Vars_t &v )
{
	const int flags = params[FLAGS]->GetIntValue();
	if ( flags & MATERIAL_VAR_ENVMAPSPHERE )
		return false;
	if ( IS_PARAM_DEFINED( v.lightWarp ) )
		return false;

	if ( IS_PARAM_DEFINED( v.detail ) )
	{
		// Modes 0 (mod2x) and 1 (additive) only; $detailblendfactor and $detailtint are staged (g_PbrDetail).
		const int mode = GetIntParam( v.detailBlendMode, params );
		if ( mode != 0 && mode != 1 )
			return false;
	}

	if ( v.model )
	{
		// A prop lit by a $lightmap texture (STATIC_LIGHT_LIGHTMAP) has no carrier in the PBR model shaders.
		if ( IsTextureSet( v.lightmap, params ) )
			return false;
		if ( flags & MATERIAL_VAR_DECAL )
			return false;
		if ( GetIntParam( v.treeSway, params ) != 0 || IsBoolSet( v.distanceAlpha, params ) )
			return false;
		if ( IsBoolSet( v.seamlessBase, params ) || IsBoolSet( v.seamlessDetail, params ) )
			return false;
		// One emission source: base alpha or $emissiontexture
		if ( ( flags & MATERIAL_VAR_SELFILLUM ) && ( IsBoolSet( v.selfIllumFresnel, params ) || IsTextureSet( v.selfIllumMask, params ) ) )
			return false;
		// $vertexcolor replaces the lighting with the mesh COLOR stream on models.
		if ( flags & MATERIAL_VAR_VERTEXCOLOR )
			return false;
	}
	else
	{
		if ( GetFloatParam( v.seamlessScale, params ) != 0.0f )
			return false;
		if ( IsBoolSet( v.outline, params ) || IsBoolSet( v.softEdges, params ) )
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Draw
//-----------------------------------------------------------------------------
void DrawPBR( CBaseVSShaderDX12 *pShader, IMaterialVar **params, IShaderDynamicAPI *pShaderAPI, IShaderShadow *pShaderShadow,
	const PBR_Vars_t &v, VertexCompressionType_t vertexCompression, CBasePerMaterialContextData **pContextDataPtr )
{
	// Flashlight and env_projectedtexture lights are evaluated inside the PBR pass, so the engine's additive flashlight
	// pass draws nothing (the snapshot of that pass is built like any other, as Refract does).
	if ( pShaderAPI && pShader->UsingFlashlight( params ) )
	{
		pShader->Draw( false );
		return;
	}

	// Only the model pixel shader has an ENHANCED variant. The world logicals are always the ordinary ones: on enhanced
	// maps the backend swaps them for their highres twins at draw time (the lightmap page must sit at SHADER_SAMPLER3), so
	// a world snapshot does not depend on the receiver generation.
	const bool bModel = IsModel( params, v );
	CPBRContext *pContextData = static_cast< CPBRContext * >( *pContextDataPtr );
	bool bEnhanced = false;
	if ( pContextData && bModel && !pContextData->m_Snapshot.Select( pShaderShadow != NULL, pShader->GetName(), bEnhanced ) )
		return;
	if ( pShaderShadow || !pContextData || pContextData->m_bMaterialVarsChanged )
	{
		if ( !pContextData )
		{
			pContextData = new CPBRContext;
			*pContextDataPtr = pContextData;
			if ( bModel && !pContextData->m_Snapshot.Select( pShaderShadow != NULL, pShader->GetName(), bEnhanced ) )
				return;
		}
		const PBRState s = Evaluate( pShader, params, v );
		if ( pShaderShadow )
			Snapshot( pShader, params, pShaderShadow, v, s, bEnhanced );
		if ( pShaderAPI && pContextData->m_bMaterialVarsChanged )
		{
			pContextData->m_bMaterialVarsChanged = false;
			StageMaterial( *pContextData, pShader, params, v, s );
		}
	}
	DYNAMIC_STATE
	{
		CCommandBufferBuilder< CFixedCommandStorageBuffer< 600 > > DynamicCmdsOut;
		DynamicCmdsOut.Call( pContextData->m_SemiStaticCmdsOut.Base() );
		const PBRSamplers &sm = pContextData->m_bModel ? kModelSamplers : kWorldSamplers;
		if ( pContextData->m_bEnvmap )
			DynamicCmdsOut.BindTexture( pShader, Sampler_t( sm.envmap ), v.envmap, v.envmapFrame );

		const bool bWaterFog = pShaderAPI->GetSceneFogMode() == MATERIAL_FOG_LINEAR_BELOW_FOG_Z;
		if ( pContextData->m_bModel )
		{
			LightState_t lightState = { 0, false, false, false };
			pShaderAPI->GetDX9LightState( &lightState );
			if ( pContextData->m_bAmbientOnly && !pContextData->m_bBump )
			{
				// $ambientonly exists only in the unbumped legacy path.
				lightState.m_bAmbientLight = true;
				lightState.m_bStaticLightVertex = false;
				lightState.m_nNumLights = 0;
			}
			// Zeroes the DX12PSEngine ambient cube when the model has no ambient light.
			pShaderAPI->SetPixelShaderStateAmbientLightCube( kAmbientCubeRegister, !lightState.m_bAmbientLight );
			pShaderAPI->CommitPixelShaderLighting( kLightInfoRegister );
			const float selfIllumTintAndLights[4] = { pContextData->m_SelfIllumTint[0], pContextData->m_SelfIllumTint[1],
				pContextData->m_SelfIllumTint[2], float( lightState.m_nNumLights ) };
			DynamicCmdsOut.SetPixelShaderConstant( 10, selfIllumTintAndLights, 1 );

			pShader->SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, VERTEX_SHADER_SHADER_SPECIFIC_CONST_11, SHADER_VERTEXTEXTURE_SAMPLER0 );

			DECLARE_DYNAMIC_VERTEX_SHADER( pbr_model_vs51 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( MORPHING, pShaderAPI->IsHWMorphingEnabled() );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, bWaterFog );
			// COLOR1 (static prop vertex lighting) exists only in the unbumped variant; the backend binds the mesh's color
			// stream because the variant's input signature declares it.
			SET_DYNAMIC_VERTEX_SHADER_COMBO( STATIC_LIGHT_VERTEX, !pContextData->m_bBump && lightState.m_bStaticLightVertex );
			SET_DYNAMIC_VERTEX_SHADER_CMD( DynamicCmdsOut, pbr_model_vs51 );

			DECLARE_DYNAMIC_PIXEL_SHADER( pbr_model_ps51 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo1( true ) );
			SET_DYNAMIC_PIXEL_SHADER_CMD( DynamicCmdsOut, pbr_model_ps51 );
		}
		else
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( pbr_world_vs51 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, bWaterFog );
			SET_DYNAMIC_VERTEX_SHADER_CMD( DynamicCmdsOut, pbr_world_vs51 );

			DECLARE_DYNAMIC_PIXEL_SHADER( pbr_world_ps51 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo1( true ) );
			SET_DYNAMIC_PIXEL_SHADER_CMD( DynamicCmdsOut, pbr_world_ps51 );
		}
		DynamicCmdsOut.End();
		DX12ExecuteCommandBuffer( pShaderAPI, DynamicCmdsOut.Base() );

		// c0 sits inside the semi-static c0..c14 block: stage it after the replay.
		pShader->SetModulationPixelShaderDynamicState_LinearColorSpace( 0 );
		// The ENHANCED variants are the receiver variants (lighting ABI); the draw check in CBaseVSShaderDX12::Draw needs
		// the same admission DX12_SET_DYNAMIC_PIXEL_SHADER_CMD gives the legacy receivers.
		if ( bEnhanced )
			g_DX12Constants.shadowmapPassAdmitted = true;
	}
	pShader->Draw();
}
