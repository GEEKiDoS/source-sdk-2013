//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 shadow (static) render state recorder for IShaderShadow
//
//=============================================================================//

#ifndef SHADERSHADOW_DX12_H
#define SHADERSHADOW_DX12_H
#pragma once

#include "shaderapi/ishadershadow.h"
#include "tier1/utlstring.h"

namespace shaderapidx12
{
class CShaderShadowDX12 final : public IShaderShadow
{
public:
	void SetDefaultState() override;
	void DepthFunc( ShaderDepthFunc_t depthFunc ) override;
	void EnableDepthWrites( bool bEnable ) override;
	void EnableDepthTest( bool bEnable ) override;
	void EnablePolyOffset( PolygonOffsetMode_t nOffsetMode ) override;
	void EnableStencil( bool bEnable ) override;
	void StencilFunc( ShaderStencilFunc_t stencilFunc ) override;
	void StencilPassOp( ShaderStencilOp_t stencilOp ) override;
	void StencilFailOp( ShaderStencilOp_t stencilOp ) override;
	void StencilDepthFailOp( ShaderStencilOp_t stencilOp ) override;
	void StencilReference( int nReference ) override;
	void StencilMask( int nMask ) override;
	void StencilWriteMask( int nMask ) override;
	void EnableColorWrites( bool bEnable ) override;
	void EnableAlphaWrites( bool bEnable ) override;
	void EnableBlending( bool bEnable ) override;
	void BlendFunc( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor ) override;
	void EnableAlphaTest( bool bEnable ) override;
	void AlphaFunc( ShaderAlphaFunc_t alphaFunc, float alphaRef ) override;
	void PolyMode( ShaderPolyModeFace_t face, ShaderPolyMode_t polyMode ) override;
	void EnableCulling( bool bEnable ) override;
	void EnableConstantColor( bool bEnable ) override;
	void VertexShaderVertexFormat( unsigned int nFlags, int nTexCoordCount, int *pTexCoordDimensions, int nUserDataSize ) override;
	void SetVertexShader( const char *pFileName, int nStaticVshIndex ) override;
	void SetPixelShader( const char *pFileName, int nStaticPshIndex = 0 ) override;
	void EnableLighting( bool bEnable ) override;
	void EnableSpecular( bool bEnable ) override;
	void EnableSRGBWrite( bool bEnable ) override;
	void EnableSRGBRead( Sampler_t sampler, bool bEnable ) override;
	void EnableVertexBlend( bool bEnable ) override;
	void OverbrightValue( TextureStage_t stage, float value ) override;
	void EnableTexture( Sampler_t sampler, bool bEnable ) override;
	void EnableTexGen( TextureStage_t stage, bool bEnable ) override;
	void TexGen( TextureStage_t stage, ShaderTexGenParam_t param ) override;
	void EnableCustomPixelPipe( bool bEnable ) override;
	void CustomTextureStages( int stageCount ) override;
	void CustomTextureOperation( TextureStage_t stage, ShaderTexChannel_t channel, ShaderTexOp_t op, ShaderTexArg_t arg1, ShaderTexArg_t arg2 ) override;
	void DrawFlags( unsigned int drawFlags ) override;
	void EnableAlphaPipe( bool bEnable ) override;
	void EnableConstantAlpha( bool bEnable ) override;
	void EnableVertexAlpha( bool bEnable ) override;
	void EnableTextureAlpha( TextureStage_t stage, bool bEnable ) override;
	void EnableBlendingSeparateAlpha( bool bEnable ) override;
	void BlendFuncSeparateAlpha( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor ) override;
	void FogMode( ShaderFogMode_t fogMode ) override;
	void SetDiffuseMaterialSource( ShaderMaterialSource_t materialSource ) override;
	void SetMorphFormat( MorphFormat_t flags ) override;
	void DisableFogGammaCorrection( bool bDisable ) override;
	void EnableAlphaToCoverage( bool bEnable ) override;
	void SetShadowDepthFiltering( Sampler_t stage ) override;
	void BlendOp( ShaderBlendOp_t blendOp ) override;
	void BlendOpSeparateAlpha( ShaderBlendOp_t blendOp ) override;

	const CUtlString &VertexShaderName() const { return m_VertexShader; }

	const CUtlString &PixelShaderName() const { return m_PixelShader; }

	int StaticVertexIndex() const { return m_nStaticVertexIndex; }

	int StaticPixelIndex() const { return m_nStaticPixelIndex; }

	unsigned int VertexFlags() const { return m_nVertexFlags; }

	VertexFormat_t VertexFormat() const
	{
		VertexFormat_t format = m_nVertexFlags | VERTEX_USERDATA_SIZE( m_nUserDataSize );
		for ( int i = 0; i < m_nTexCoordCount; ++i )
			format |= VERTEX_TEXCOORD_SIZE( i, m_TexCoordDimensions[i] );
		return format;
	}

	MorphFormat_t MorphFormat() const { return m_MorphFormat; }

	bool DepthWrites() const { return m_bDepthWrites; }

	bool DepthTest() const { return m_bDepthTest; }

	bool Blending() const { return m_bBlending; }

	bool AlphaTest() const { return m_bAlphaTest; }

	float AlphaReference() const { return m_flAlphaRef; }

	ShaderAlphaFunc_t AlphaFunction() const { return m_AlphaFunc; }

	bool CullEnabled() const { return m_bCulling; }

	bool ColorWrites() const { return m_bColorWrites; }

	bool AlphaWrites() const { return m_bAlphaWrites; }

	bool AlphaToCoverage() const { return m_bAlphaToCoverage; }

	ShaderBlendFactor_t BlendSource() const { return m_BlendSrc; }

	ShaderBlendFactor_t BlendDestination() const { return m_BlendDst; }

	ShaderDepthFunc_t DepthFunction() const { return m_DepthFunc; }

	PolygonOffsetMode_t PolyOffset() const { return m_PolyOffset; }

	ShaderPolyMode_t PolyModeFront() const { return m_PolyFront; }

	ShaderPolyMode_t PolyModeBack() const { return m_PolyBack; }

	bool SeparateAlphaBlending() const { return m_bSeparateAlpha; }

	ShaderBlendFactor_t BlendAlphaSource() const { return m_BlendAlphaSrc; }

	ShaderBlendFactor_t BlendAlphaDestination() const { return m_BlendAlphaDst; }

	ShaderBlendOp_t BlendOperation() const { return m_BlendOp; }

	ShaderBlendOp_t BlendAlphaOperation() const { return m_BlendAlphaOp; }

	ShaderStencilFunc_t StencilFunction() const { return m_StencilFunc; }

	ShaderStencilOp_t StencilPassOperation() const { return m_StencilPass; }

	ShaderStencilOp_t StencilFailOperation() const { return m_StencilFail; }

	ShaderStencilOp_t StencilDepthFailOperation() const { return m_StencilDepthFail; }

	uint8_t StencilTestMask() const { return m_nStencilMask; }

	uint8_t StencilWriteMask() const { return m_nStencilWriteMask; }

	uint8_t StencilReferenceValue() const { return m_nStencilReference; }

	bool StencilEnabled() const { return m_bStencil; }

	ShaderFogMode_t FogModeValue() const { return m_FogMode; }

	bool FogGammaDisabled() const { return m_bFogGammaDisabled; }

	bool SRGBWrite() const { return m_bSrgbWrite; }

	uint16_t SRGBReadMask() const
	{
		uint16_t nMask = 0;
		for ( int i = 0; i < ARRAYSIZE( m_SrgbRead ); ++i )
			if ( m_SrgbRead[i] )
				nMask |= static_cast<uint16_t>( 1u << i );
		return nMask;
	}

	uint16_t ComparisonSamplerMask() const { return m_nComparisonSamplerMask; }

	bool Lighting() const { return m_bLighting; }

	bool Specular() const { return m_bSpecular; }

	bool VertexBlend() const { return m_bVertexBlend; }

	bool ConstantColor() const { return m_bConstantColor; }

	bool CustomPixelPipe() const { return m_bCustomPipe; }

	int CustomTextureStageCount() const { return m_nCustomStages; }

	unsigned int DrawFlagsValue() const { return m_nDrawFlags; }

	bool AlphaPipe() const { return m_bAlphaPipe; }

	bool ConstantAlpha() const { return m_bConstantAlpha; }

	bool VertexAlpha() const { return m_bVertexAlpha; }

	ShaderMaterialSource_t DiffuseMaterialSource() const { return m_MaterialSource; }

	bool TextureEnabled( int nStage ) const { return nStage >= 0 && nStage < ARRAYSIZE( m_Textures ) && m_Textures[nStage]; }

	float Overbright( int nStage ) const { return nStage >= 0 && nStage < ARRAYSIZE( m_Overbright ) ? m_Overbright[nStage] : 1.f; }

	bool TexGenEnabled( int nStage ) const { return nStage >= 0 && nStage < ARRAYSIZE( m_Texgen ) && m_Texgen[nStage]; }

	ShaderTexGenParam_t TexGenParam( int nStage ) const { return nStage >= 0 && nStage < ARRAYSIZE( m_TexgenParam ) ? m_TexgenParam[nStage] : SHADER_TEXGENPARAM_OBJECT_LINEAR; }

	bool TextureAlphaEnabled( int nStage ) const { return nStage >= 0 && nStage < ARRAYSIZE( m_TextureAlpha ) && m_TextureAlpha[nStage]; }

	ShaderTexOp_t TextureOperation( int nStage, ShaderTexChannel_t channel ) const { return nStage >= 0 && nStage < ARRAYSIZE( m_TexOps ) ? m_TexOps[nStage][channel] : SHADER_TEXOP_DISABLE; }

	ShaderTexArg_t TextureArgument( int nStage, ShaderTexChannel_t channel, int nArg ) const { return nStage >= 0 && nStage < ARRAYSIZE( m_TexArgs ) && channel < 2 && nArg < 2 ? m_TexArgs[nStage][channel][nArg] : SHADER_TEXARG_NONE; }

	bool EnableBlendingSeparateAlphaValue() const { return m_bSeparateAlpha; }

private:
	float m_Overbright[16] = {};
	bool m_Texgen[16] = {};
	bool m_TextureAlpha[16] = {};
	ShaderTexGenParam_t m_TexgenParam[16] = {};
	ShaderTexOp_t m_TexOps[16][2] = {};
	ShaderTexArg_t m_TexArgs[16][2][2] = {};
	ShaderDepthFunc_t m_DepthFunc = SHADER_DEPTHFUNC_NEAREROREQUAL;
	ShaderPolyMode_t m_PolyFront = SHADER_POLYMODE_FILL, m_PolyBack = SHADER_POLYMODE_FILL;
	PolygonOffsetMode_t m_PolyOffset = SHADER_POLYOFFSET_DISABLE;
	ShaderStencilFunc_t m_StencilFunc = SHADER_STENCILFUNC_ALWAYS;
	ShaderStencilOp_t m_StencilPass = SHADER_STENCILOP_KEEP, m_StencilFail = SHADER_STENCILOP_KEEP, m_StencilDepthFail = SHADER_STENCILOP_KEEP;
	ShaderBlendFactor_t m_BlendSrc = SHADER_BLEND_ONE, m_BlendDst = SHADER_BLEND_ZERO, m_BlendAlphaSrc = SHADER_BLEND_ONE, m_BlendAlphaDst = SHADER_BLEND_ZERO;
	ShaderBlendOp_t m_BlendOp = SHADER_BLEND_OP_ADD, m_BlendAlphaOp = SHADER_BLEND_OP_ADD;
	ShaderAlphaFunc_t m_AlphaFunc = SHADER_ALPHAFUNC_ALWAYS;
	ShaderFogMode_t m_FogMode = SHADER_FOGMODE_DISABLED;
	ShaderMaterialSource_t m_MaterialSource = SHADER_MATERIALSOURCE_MATERIAL;
	int m_nStaticVertexIndex = 0, m_nStaticPixelIndex = 0;
	unsigned int m_nVertexFlags = 0, m_nDrawFlags = 0;
	uint16_t m_nComparisonSamplerMask = 0;
	uint8_t m_nStencilMask = 0xff, m_nStencilWriteMask = 0xff, m_nStencilReference = 0;
	int m_nTexCoordCount = 0, m_nUserDataSize = 0, m_nCustomStages = 0;
	MorphFormat_t m_MorphFormat = 0;
	float m_flAlphaRef = 0.0f;
	int m_TexCoordDimensions[8] = {};
	bool m_Textures[16] = {};
	bool m_SrgbRead[16] = {};
	bool m_bDepthWrites = true, m_bDepthTest = true, m_bStencil = false, m_bColorWrites = true, m_bAlphaWrites = true, m_bBlending = false, m_bAlphaTest = false, m_bCulling = true, m_bConstantColor = false, m_bLighting = false, m_bSpecular = false, m_bSrgbWrite = false, m_bVertexBlend = false, m_bCustomPipe = false, m_bAlphaPipe = false, m_bConstantAlpha = false, m_bVertexAlpha = false, m_bSeparateAlpha = false, m_bFogGammaDisabled = false, m_bAlphaToCoverage = false;
	CUtlString m_VertexShader;
	CUtlString m_PixelShader;
};

extern CShaderShadowDX12 *g_pShaderShadowDX12;
} // namespace shaderapidx12

#endif // SHADERSHADOW_DX12_H
