//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 shadow (static) render state recorder for IShaderShadow
//
//=============================================================================//

#include "shadershadow_dx12.h"

namespace shaderapidx12
{
CShaderShadowDX12 *g_pShaderShadowDX12 = nullptr;

//-----------------------------------------------------------------------------
// Purpose: Resets every piece of shadow state to the Source defaults
//-----------------------------------------------------------------------------
void CShaderShadowDX12::SetDefaultState()
{
	*this = CShaderShadowDX12();
	m_DepthFunc = SHADER_DEPTHFUNC_NEAREROREQUAL;
	m_bDepthWrites = true;
	m_bDepthTest = true;
	m_bColorWrites = true;
	m_bAlphaWrites = false;
	m_bAlphaTest = false;
	m_AlphaFunc = SHADER_ALPHAFUNC_GEQUAL;
	m_flAlphaRef = 0.7f;
	m_bCulling = true;
	m_bBlending = false;
	m_BlendSrc = SHADER_BLEND_ONE;
	m_BlendDst = SHADER_BLEND_ZERO;
	m_BlendOp = SHADER_BLEND_OP_ADD;
	m_bSeparateAlpha = false;
	m_BlendAlphaSrc = SHADER_BLEND_ONE;
	m_BlendAlphaDst = SHADER_BLEND_ZERO;
	m_BlendAlphaOp = SHADER_BLEND_OP_ADD;
	m_PolyFront = m_PolyBack = SHADER_POLYMODE_FILL;
	m_PolyOffset = SHADER_POLYOFFSET_DISABLE;
	m_nDrawFlags = SHADER_DRAW_POSITION;
	m_MaterialSource = SHADER_MATERIALSOURCE_MATERIAL;
	for ( int i = 0; i < ARRAYSIZE( m_Overbright ); ++i )
	{
		m_Overbright[i] = 1.f;
		m_Texgen[i] = false;
		m_TextureAlpha[i] = false;
		m_TexgenParam[i] = SHADER_TEXGENPARAM_OBJECT_LINEAR;
		for ( int c = 0; c < 2; ++c )
		{
			m_TexOps[i][c] = SHADER_TEXOP_DISABLE;
			m_TexArgs[i][c][0] = SHADER_TEXARG_TEXTURE;
			m_TexArgs[i][c][1] = SHADER_TEXARG_PREVIOUSSTAGE;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Depth and stencil state
//-----------------------------------------------------------------------------
void CShaderShadowDX12::DepthFunc( ShaderDepthFunc_t depthFunc )
{
	m_DepthFunc = depthFunc;
}

void CShaderShadowDX12::EnableDepthWrites( bool bEnable )
{
	m_bDepthWrites = bEnable;
}

void CShaderShadowDX12::EnableDepthTest( bool bEnable )
{
	m_bDepthTest = bEnable;
}

void CShaderShadowDX12::EnablePolyOffset( PolygonOffsetMode_t nOffsetMode )
{
	m_PolyOffset = nOffsetMode;
}

void CShaderShadowDX12::EnableStencil( bool bEnable )
{
	m_bStencil = bEnable;
}

void CShaderShadowDX12::StencilFunc( ShaderStencilFunc_t stencilFunc )
{
	m_StencilFunc = stencilFunc;
}

void CShaderShadowDX12::StencilPassOp( ShaderStencilOp_t stencilOp )
{
	m_StencilPass = stencilOp;
}

void CShaderShadowDX12::StencilFailOp( ShaderStencilOp_t stencilOp )
{
	m_StencilFail = stencilOp;
}

void CShaderShadowDX12::StencilDepthFailOp( ShaderStencilOp_t stencilOp )
{
	m_StencilDepthFail = stencilOp;
}

void CShaderShadowDX12::StencilReference( int nReference )
{
	m_nStencilReference = static_cast<uint8_t>( Clamp( nReference, 0, 255 ) );
}

void CShaderShadowDX12::StencilMask( int nMask )
{
	m_nStencilMask = static_cast<uint8_t>( Clamp( nMask, 0, 255 ) );
}

void CShaderShadowDX12::StencilWriteMask( int nMask )
{
	m_nStencilWriteMask = static_cast<uint8_t>( Clamp( nMask, 0, 255 ) );
}

//-----------------------------------------------------------------------------
// Purpose: Output merger and rasterizer state
//-----------------------------------------------------------------------------
void CShaderShadowDX12::EnableColorWrites( bool bEnable )
{
	m_bColorWrites = bEnable;
}

void CShaderShadowDX12::EnableAlphaWrites( bool bEnable )
{
	m_bAlphaWrites = bEnable;
}

void CShaderShadowDX12::EnableBlending( bool bEnable )
{
	m_bBlending = bEnable;
}

void CShaderShadowDX12::BlendFunc( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor )
{
	m_BlendSrc = srcFactor;
	m_BlendDst = dstFactor;
}

void CShaderShadowDX12::EnableAlphaTest( bool bEnable )
{
	m_bAlphaTest = bEnable;
}

void CShaderShadowDX12::AlphaFunc( ShaderAlphaFunc_t alphaFunc, float flAlphaRef )
{
	m_AlphaFunc = alphaFunc;
	m_flAlphaRef = Clamp( flAlphaRef, 0.0f, 1.0f );
}

void CShaderShadowDX12::PolyMode( ShaderPolyModeFace_t face, ShaderPolyMode_t polyMode )
{
	if ( polyMode < SHADER_POLYMODE_POINT || polyMode > SHADER_POLYMODE_FILL )
		return;
	if ( face == SHADER_POLYMODEFACE_FRONT || face == SHADER_POLYMODEFACE_FRONT_AND_BACK )
		m_PolyFront = polyMode;
	if ( face == SHADER_POLYMODEFACE_BACK || face == SHADER_POLYMODEFACE_FRONT_AND_BACK )
		m_PolyBack = polyMode;
}

void CShaderShadowDX12::EnableCulling( bool bEnable )
{
	m_bCulling = bEnable;
}

void CShaderShadowDX12::EnableConstantColor( bool bEnable )
{
	m_bConstantColor = bEnable;
}

//-----------------------------------------------------------------------------
// Purpose: Vertex format and shader selection
//-----------------------------------------------------------------------------
void CShaderShadowDX12::VertexShaderVertexFormat( unsigned int nFlags, int nTexCoordCount, int *pTexCoordDimensions, int nUserDataSize )
{
	m_nVertexFlags = nFlags & ~VERTEX_BONE_INDEX;
	m_nTexCoordCount = Clamp( nTexCoordCount, 0, 8 );
	m_nUserDataSize = Clamp( nUserDataSize, 0, 4 );
	for ( int i = 0; i < m_nTexCoordCount; ++i )
		m_TexCoordDimensions[i] = pTexCoordDimensions ? Clamp( pTexCoordDimensions[i], 1, 4 ) : 2;
}

void CShaderShadowDX12::SetVertexShader( const char *pFileName, int nStaticVshIndex )
{
	m_VertexShader = pFileName ? pFileName : "";
	m_nStaticVertexIndex = nStaticVshIndex;
}

void CShaderShadowDX12::SetPixelShader( const char *pFileName, int nStaticPshIndex )
{
	m_PixelShader = pFileName ? pFileName : "";
	m_nStaticPixelIndex = nStaticPshIndex;
}

//-----------------------------------------------------------------------------
// Purpose: Fixed-function and texture stage state
//-----------------------------------------------------------------------------
void CShaderShadowDX12::EnableLighting( bool bEnable )
{
	m_bLighting = bEnable;
}

void CShaderShadowDX12::EnableSpecular( bool bEnable )
{
	m_bSpecular = bEnable;
}

void CShaderShadowDX12::EnableSRGBWrite( bool bEnable )
{
	m_bSrgbWrite = bEnable;
}

void CShaderShadowDX12::EnableSRGBRead( Sampler_t sampler, bool bEnable )
{
	if ( sampler >= 0 && sampler < ARRAYSIZE( m_SrgbRead ) )
		m_SrgbRead[sampler] = bEnable;
}

void CShaderShadowDX12::EnableVertexBlend( bool bEnable )
{
	m_bVertexBlend = bEnable;
}

void CShaderShadowDX12::OverbrightValue( TextureStage_t stage, float flValue )
{
	if ( stage >= 0 && stage < ARRAYSIZE( m_Overbright ) )
		m_Overbright[stage] = Max( flValue, 0.f );
}

void CShaderShadowDX12::EnableTexture( Sampler_t sampler, bool bEnable )
{
	if ( sampler >= 0 && sampler < ARRAYSIZE( m_Textures ) )
		m_Textures[sampler] = bEnable;
}

void CShaderShadowDX12::EnableTexGen( TextureStage_t stage, bool bEnable )
{
	if ( stage >= 0 && stage < ARRAYSIZE( m_Texgen ) )
		m_Texgen[stage] = bEnable;
}

void CShaderShadowDX12::TexGen( TextureStage_t stage, ShaderTexGenParam_t param )
{
	if ( stage >= 0 && stage < ARRAYSIZE( m_TexgenParam ) )
		m_TexgenParam[stage] = param;
}

void CShaderShadowDX12::EnableCustomPixelPipe( bool bEnable )
{
	m_bCustomPipe = bEnable;
}

void CShaderShadowDX12::CustomTextureStages( int nStageCount )
{
	m_nCustomStages = Clamp( nStageCount, 0, 16 );
}

void CShaderShadowDX12::CustomTextureOperation( TextureStage_t stage, ShaderTexChannel_t channel, ShaderTexOp_t op, ShaderTexArg_t arg1, ShaderTexArg_t arg2 )
{
	if ( stage < 0 || stage >= ARRAYSIZE( m_TexOps ) || channel < 0 || channel > 1 )
		return;
	m_TexOps[stage][channel] = op;
	m_TexArgs[stage][channel][0] = arg1;
	m_TexArgs[stage][channel][1] = arg2;
}

void CShaderShadowDX12::DrawFlags( unsigned int nDrawFlags )
{
	m_nDrawFlags = nDrawFlags;
}

void CShaderShadowDX12::EnableAlphaPipe( bool bEnable )
{
	m_bAlphaPipe = bEnable;
}

void CShaderShadowDX12::EnableConstantAlpha( bool bEnable )
{
	m_bConstantAlpha = bEnable;
}

void CShaderShadowDX12::EnableVertexAlpha( bool bEnable )
{
	m_bVertexAlpha = bEnable;
}

void CShaderShadowDX12::EnableTextureAlpha( TextureStage_t stage, bool bEnable )
{
	if ( stage >= 0 && stage < ARRAYSIZE( m_TextureAlpha ) )
		m_TextureAlpha[stage] = bEnable;
}

void CShaderShadowDX12::EnableBlendingSeparateAlpha( bool bEnable )
{
	m_bSeparateAlpha = bEnable;
}

void CShaderShadowDX12::BlendFuncSeparateAlpha( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor )
{
	m_BlendAlphaSrc = srcFactor;
	m_BlendAlphaDst = dstFactor;
}

void CShaderShadowDX12::FogMode( ShaderFogMode_t fogMode )
{
	m_FogMode = fogMode;
}

void CShaderShadowDX12::SetDiffuseMaterialSource( ShaderMaterialSource_t materialSource )
{
	m_MaterialSource = materialSource;
}

void CShaderShadowDX12::SetMorphFormat( MorphFormat_t flags )
{
	m_MorphFormat = flags;
}

void CShaderShadowDX12::DisableFogGammaCorrection( bool bDisable )
{
	m_bFogGammaDisabled = bDisable;
}

void CShaderShadowDX12::EnableAlphaToCoverage( bool bEnable )
{
	m_bAlphaToCoverage = bEnable;
}

void CShaderShadowDX12::SetShadowDepthFiltering( Sampler_t stage )
{
	if ( stage >= 0 && stage < 16 )
		m_nComparisonSamplerMask |= static_cast<uint16_t>( 1u << stage );
}

void CShaderShadowDX12::BlendOp( ShaderBlendOp_t blendOp )
{
	m_BlendOp = blendOp;
}

void CShaderShadowDX12::BlendOpSeparateAlpha( ShaderBlendOp_t blendOp )
{
	m_BlendAlphaOp = blendOp;
}
} // namespace shaderapidx12
