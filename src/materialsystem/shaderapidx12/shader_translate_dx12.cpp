//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Legacy D3D9 shader reconstruction into signed DXBC through the private D3D9On12 converter.
//          Compiled ONLY in dx12_shaderconv: DDI headers never enter renderer translation units.
//
// Signature construction adapted from Microsoft D3D9On12, commit
// ee599de26b8e304ff2556a5f79527b742eedfac3, src/9on12Shader.cpp and include/9on12Util.h.
// Copyright (c) Microsoft Corporation. Licensed under the MIT license.
// See thirdparty/dx12_shaderconv/LICENSE-D3D9On12.txt.
//
//=============================================================================//

#include "../../thirdparty/dx12_shaderconv/ShaderConverter/ShaderConv/pch.h"
#include <d3dcommon.h>
#include <ShaderConv.h>
#include "tracy/Tracy.hpp"
#include <DxbcBuilder.hpp>
// ShaderConv.h defines a function-like Check() macro that collides with CThreadEvent::Check() in tier0.
#pragma push_macro( "Check" )
#undef Check
#include "shader_translate_dx12.h"
#pragma pop_macro( "Check" )
#include <exception>

namespace shaderapidx12
{
namespace
{
static_assert( sizeof( ShaderVertexExtensionDX12 ) == sizeof( ShaderConv::VSCBExtension ), "VS extension ABI" );
static_assert( sizeof( ShaderPixelExtensionDX12 ) == sizeof( ShaderConv::PSCBExtension ), "PS extension ABI" );
static_assert( sizeof( ShaderBumpExtensionDX12 ) == sizeof( ShaderConv::PSCBExtension2 ), "Bump extension ABI" );
static_assert( sizeof( ShaderColorKeyExtensionDX12 ) == sizeof( ShaderConv::PSCBExtension3 ), "Color-key extension ABI" );

//-----------------------------------------------------------------------------
// Purpose: Stores a failure message and returns false
//-----------------------------------------------------------------------------
bool Fail( CUtlString &error, const char *pszMessage )
{
	error = pszMessage;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Formats a failed HRESULT into the error string
//-----------------------------------------------------------------------------
bool CheckResult( HRESULT hr, const char *pszOperation, CUtlString &error )
{
	if ( SUCCEEDED( hr ) )
		return true;
	error.Format( "%s: HRESULT 0x%08lx", pszOperation, static_cast<unsigned long>( hr ) );
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Reads one unaligned 32-bit legacy token
//-----------------------------------------------------------------------------
uint32_t Token( const void *pBytes, size_t nIndex )
{
	uint32_t nValue;
	memcpy( &nValue, static_cast<const uint8_t *>( pBytes ) + nIndex * 4, 4 );
	return nValue;
}

//-----------------------------------------------------------------------------
// Purpose: Operand count of a legacy instruction, or -1 for unknown opcodes
//-----------------------------------------------------------------------------
int OperandCount( uint32_t nOpcode, uint32_t nVersion )
{
	const bool bOldPixel = ( nVersion >> 16 ) == 0xffff && ( ( nVersion >> 8 ) & 255 ) == 1;
	switch ( nOpcode )
	{
	case D3DSIO_NOP:
	case D3DSIO_RET:
	case D3DSIO_ENDLOOP:
	case D3DSIO_ENDREP:
	case D3DSIO_ELSE:
	case D3DSIO_ENDIF:
	case D3DSIO_BREAK:
	case D3DSIO_PHASE:
		return 0;
	case D3DSIO_CALL:
	case D3DSIO_LABEL:
	case D3DSIO_REP:
	case D3DSIO_IF:
	case D3DSIO_BREAKP:
	case D3DSIO_TEXKILL:
	case D3DSIO_TEXDEPTH:
		return 1;
	case D3DSIO_MOV:
	case D3DSIO_RCP:
	case D3DSIO_RSQ:
	case D3DSIO_EXP:
	case D3DSIO_LOG:
	case D3DSIO_EXPP:
	case D3DSIO_LOGP:
	case D3DSIO_LIT:
	case D3DSIO_FRC:
	case D3DSIO_ABS:
	case D3DSIO_NRM:
	case D3DSIO_MOVA:
	case D3DSIO_DSX:
	case D3DSIO_DSY:
	case D3DSIO_DCL:
	case D3DSIO_DEFB:
	case D3DSIO_CALLNZ:
	case D3DSIO_LOOP:
	case D3DSIO_IFC:
	case D3DSIO_BREAKC:
	case D3DSIO_TEXBEM:
	case D3DSIO_TEXBEML:
	case D3DSIO_TEXREG2AR:
	case D3DSIO_TEXREG2GB:
	case D3DSIO_TEXREG2RGB:
	case D3DSIO_TEXM3x2PAD:
	case D3DSIO_TEXM3x2TEX:
	case D3DSIO_TEXM3x3PAD:
	case D3DSIO_TEXM3x3TEX:
	case D3DSIO_TEXM3x3VSPEC:
	case D3DSIO_TEXDP3TEX:
	case D3DSIO_TEXM3x2DEPTH:
	case D3DSIO_TEXDP3:
	case D3DSIO_TEXM3x3:
		return 2;
	case D3DSIO_ADD:
	case D3DSIO_SUB:
	case D3DSIO_MUL:
	case D3DSIO_DP3:
	case D3DSIO_DP4:
	case D3DSIO_MIN:
	case D3DSIO_MAX:
	case D3DSIO_SLT:
	case D3DSIO_SGE:
	case D3DSIO_DST:
	case D3DSIO_M4x4:
	case D3DSIO_M4x3:
	case D3DSIO_M3x4:
	case D3DSIO_M3x3:
	case D3DSIO_M3x2:
	case D3DSIO_POW:
	case D3DSIO_CRS:
	case D3DSIO_BEM:
	case D3DSIO_SETP:
	case D3DSIO_TEXM3x3SPEC:
	case D3DSIO_TEXLDL:
		return 3;
	case D3DSIO_MAD:
	case D3DSIO_LRP:
	case D3DSIO_CND:
	case D3DSIO_CMP:
	case D3DSIO_DP2ADD:
	case D3DSIO_SGN:
		return 4;
	case D3DSIO_DEF:
	case D3DSIO_DEFI:
	case D3DSIO_TEXLDD:
		return 5;
	case D3DSIO_SINCOS:
		return ( ( nVersion >> 8 ) & 255 ) >= 3 ? 2 : 4;
	case D3DSIO_TEXCOORD:
		return ( nVersion & 255 ) >= 4 ? 2 : 1;
	case D3DSIO_TEX:
		return bOldPixel ? ( ( nVersion & 255 ) >= 4 ? 2 : 1 ) : 3;
	default:
		return -1;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Validates the legacy token stream: profile, instruction lengths and END
//-----------------------------------------------------------------------------
bool ValidateLegacy( const ShaderTranslationRequestDX12 &request, CUtlString &error )
{
	if ( !request.legacyBytes || request.byteCount < 8 || request.byteCount % 4 || request.byteCount > UINT32_MAX )
		return Fail( error, "invalid legacy shader byte span" );
	const uint32_t nVersion = Token( request.legacyBytes, 0 );
	const uint32_t nMajor = ( nVersion >> 8 ) & 255, nMinor = nVersion & 255;
	if ( ( nVersion >> 16 ) != ( request.pixel ? 0xffffu : 0xfffeu ) || nMajor < 1 || nMajor > 3 ||
	    ( nMajor == 1 && nMinor > ( request.pixel ? 4u : 1u ) ) || ( nMajor == 2 && nMinor > 1 ) || ( nMajor == 3 && nMinor ) )
		return Fail( error, "unsupported or mismatched legacy shader profile" );
	const size_t nCount = request.byteCount / 4;
	for ( size_t nOffset = 1; nOffset < nCount; )
	{
		const uint32_t nInstruction = Token( request.legacyBytes, nOffset ), nOpcode = nInstruction & D3DSI_OPCODE_MASK;
		if ( nInstruction == D3DSIO_END )
			return nOffset + 1 == nCount || Fail( error, "trailing data after legacy shader END" );
		size_t nLength;
		if ( nOpcode == D3DSIO_COMMENT )
			nLength = 1 + ( ( nInstruction & D3DSI_COMMENTSIZE_MASK ) >> D3DSI_COMMENTSIZE_SHIFT );
		else
		{
			const int nOperands = OperandCount( nOpcode, nVersion );
			if ( nOperands < 0 )
				return Fail( error, "unknown legacy shader instruction" );
			nLength = nMajor >= 2 ? 1 + ( ( nInstruction & D3DSI_INSTLENGTH_MASK ) >> D3DSI_INSTLENGTH_SHIFT ) : 1 + nOperands;
			if ( nLength < 1 + static_cast<size_t>( nOperands ) )
				return Fail( error, "short legacy shader instruction" );
		}
		if ( nLength > nCount - nOffset )
			return Fail( error, "legacy instruction exceeds byte span" );
		nOffset += nLength;
	}
	return Fail( error, "legacy shader has no terminal END" );
}

//-----------------------------------------------------------------------------
// Purpose: Converts the renderer raster state into the converter's raster states
//-----------------------------------------------------------------------------
ShaderConv::RasterStates Raster( const ShaderRasterStateDX12 &state )
{
	ShaderConv::RasterStates result;
	for ( int i = 0; i < ARRAYSIZE( state.textureTypes ); ++i )
	{
		result.PSSamplers[i].Value = 0;
		result.PSSamplers[i].TextureType = state.textureTypes[i];
		result.PSSamplers[i].TexCoordWrap = state.texcoordWrap[i];
	}
	result.TCIMapping = state.texcoordMapping;
	result.ProjectedTCsMask = state.projectedTexcoords;
	result.UserClipPlanes = state.clipPlaneMask;
	result.FillMode = state.fillMode;
	result.ShadeMode = state.shadeMode;
	result.PrimitiveType = state.primitiveType;
	result.AlphaFunc = state.alphaFunction;
	result.AlphaTestEnable = state.alphaTest;
	result.FogEnable = state.fog;
	result.FogTableMode = state.fogTableMode;
	result.WFogEnable = state.wFog;
	result.PointSizeEnable = state.pointSize;
	result.PointSpriteEnable = state.pointSprite;
	result.HasTLVertices = state.transformedVertices;
	result.HardwareShadowMappingRequiredPS = state.comparisonPixelSamplers;
	result.HardwareShadowMappingRequiredVS = state.comparisonVertexSamplers;
	// Channel conversion belongs to SRV component mapping, not a second shader swizzle.
	result.SamplerSwizzleMask = 0;
	return result;
}

//-----------------------------------------------------------------------------
// Purpose: Validates predecessor linkage and adds it to a converter declaration list
//-----------------------------------------------------------------------------
bool ImportLinkage( const ShaderLinkageDX12 *pSource, size_t nCount, uint32_t nCentroidMask,
    ShaderConv::VSOutputDecls &dest, CUtlString &error )
{
	if ( ( !pSource && nCount ) || nCount > ShaderConv::VSOutputDecls::MAX_SIZE )
		return Fail( error, "invalid predecessor linkage span" );
	for ( size_t i = 0; i < nCount; ++i )
	{
		const ShaderLinkageDX12 &entry = pSource[i];
		if ( entry.usage > D3DDECLUSAGE_POINTSPRITE || entry.usageIndex >= 16 || entry.registerIndex >= 16 || !entry.writeMask || entry.writeMask > 15 )
			return Fail( error, "invalid predecessor linkage declaration" );
		const bool bCentroid = entry.centroid || ( entry.usage == D3DDECLUSAGE_TEXCOORD && ( nCentroidMask & ( 1u << entry.usageIndex ) ) );
		dest.AddDecl( entry.usage, entry.usageIndex, entry.registerIndex, entry.writeMask << 16, bCentroid );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Appends a converter declaration list to renderer linkage
//-----------------------------------------------------------------------------
void ExportLinkage( const ShaderConv::VSOutputDecls &source, CUtlVector<ShaderLinkageDX12> &dest )
{
	dest.EnsureCapacity( dest.Count() + static_cast<int>( source.GetSize() ) );
	for ( UINT i = 0; i < source.GetSize(); ++i )
	{
		const ShaderConv::VSOutputDecl &decl = source[i];
		ShaderLinkageDX12 &link = dest[dest.AddToTail()];
		link.usage = decl.Usage;
		link.usageIndex = decl.UsageIndex;
		link.registerIndex = decl.RegIndex;
		link.writeMask = decl.WriteMask;
		link.centroid = ( source.CentroidMask & ( uint64_t( 1 ) << i ) ) != 0;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Signature semantic name for a D3D9 declaration usage
//-----------------------------------------------------------------------------
const char *Semantic( uint32_t nUsage, bool bVertexInput )
{
	// Usage order is the D3D9 declaration ABI, including converter system semantics.
	static const char *const s_pszNames[] = { "POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD", "TANGENT", "BINORMAL", "TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH", "SAMPLE", "SV_IsFrontFace", "SV_POSITION", "SV_ClipDistance", "POINTSPRITE" };
	if ( !bVertexInput && nUsage == D3DDECLUSAGE_POSITION )
		return "SV_POSITION";
	return nUsage < ARRAYSIZE( s_pszNames ) ? s_pszNames[nUsage] : nullptr;
}

// Serialized ISG1/OSG1 parameter layout (_D3D11_INTERNALSHADER_PARAMETER_11_1).
struct SignatureParameter
{
	uint32_t stream = 0, semanticName = 0, semanticIndex = 0, systemValue = 0, componentType = 3, reg = 0;
	uint8_t mask = 0, usedMask = 0;
	uint16_t padding = 0;
	uint32_t minPrecision = 0;
};

static_assert( sizeof( SignatureParameter ) == 32, "DXBC signature record ABI" );

struct NamedParameter
{
	SignatureParameter parameter;
	const char *name;
};

//-----------------------------------------------------------------------------
// Purpose: Stable insertion sort by register; equal registers keep declaration order
//-----------------------------------------------------------------------------
void SortParametersByRegister( CUtlVector<NamedParameter> &parameters )
{
	for ( int i = 1; i < parameters.Count(); ++i )
	{
		const NamedParameter entry = parameters[i];
		int j = i - 1;
		while ( j >= 0 && parameters[j].parameter.reg > entry.parameter.reg )
		{
			parameters[j + 1] = parameters[j];
			--j;
		}
		parameters[j + 1] = entry;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Serializes a register-sorted ISG1/OSG1 signature blob
//-----------------------------------------------------------------------------
void PackSignature( CUtlVector<NamedParameter> &parameters, CUtlVector<uint8_t> &bytes )
{
	SortParametersByRegister( parameters );
	const size_t nHeaderBytes = 8 + parameters.Count() * sizeof( SignatureParameter );
	size_t nSize = nHeaderBytes;
	for ( int i = 0; i < parameters.Count(); ++i )
		nSize += V_strlen( parameters[i].name ) + 1;
	const int nPaddedSize = static_cast<int>( ( nSize + 3 ) & ~size_t( 3 ) );
	bytes.SetCount( nPaddedSize );
	memset( bytes.Base(), 0, nPaddedSize );
	const uint32_t header[2] = { static_cast<uint32_t>( parameters.Count() ), 8 };
	memcpy( bytes.Base(), header, sizeof( header ) );
	size_t nStringOffset = nHeaderBytes;
	for ( int i = 0; i < parameters.Count(); ++i )
	{
		SignatureParameter parameter = parameters[i].parameter;
		parameter.semanticName = static_cast<uint32_t>( nStringOffset );
		memcpy( bytes.Base() + 8 + i * sizeof( parameter ), &parameter, sizeof( parameter ) );
		const size_t nLength = V_strlen( parameters[i].name ) + 1;
		memcpy( bytes.Base() + nStringOffset, parameters[i].name, nLength );
		nStringOffset += nLength;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Builds the signature blob of a VS output / PS input declaration list
//-----------------------------------------------------------------------------
void LinkageSignature( const ShaderConv::VSOutputDecls &decls, CUtlVector<uint8_t> &bytes )
{
	CUtlVector<NamedParameter> parameters;
	parameters.EnsureCapacity( static_cast<int>( decls.GetSize() ) );
	for ( UINT i = 0; i < decls.GetSize(); ++i )
	{
		const ShaderConv::VSOutputDecl &decl = decls[i];
		NamedParameter entry{};
		entry.name = Semantic( decl.Usage, false );
		entry.parameter.semanticIndex = decl.UsageIndex;
		entry.parameter.reg = decl.RegIndex;
		entry.parameter.mask = decl.WriteMask;
		if ( decl.Usage == D3DDECLUSAGE_POSITION || decl.Usage == D3DDECLUSAGE_VPOS )
			entry.parameter.systemValue = D3D_NAME_POSITION;
		if ( decl.Usage == D3DDECLUSAGE_CLIPDISTANCE )
			entry.parameter.systemValue = D3D_NAME_CLIP_DISTANCE;
		if ( decl.Usage == D3DDECLUSAGE_VFACE )
		{
			entry.parameter.systemValue = D3D_NAME_IS_FRONT_FACE;
			entry.parameter.componentType = D3D_REGISTER_COMPONENT_UINT32;
		}
		parameters.AddToTail( entry );
	}
	PackSignature( parameters, bytes );
}

// Releases converter-owned output tokens when the translation scope exits.
struct ConvertedTokens
{
	ShaderConv::ByteCode &code;

	~ConvertedTokens() { ShaderConv::ShaderConverterAPI::CleanUpConvertedShader( code ); }
};

//-----------------------------------------------------------------------------
// Purpose: Assembles signatures and converted tokens into a signed DXBC container
//-----------------------------------------------------------------------------
bool Assemble( const ShaderConv::ByteCode &tokens, const CUtlVector<uint8_t> &input, const CUtlVector<uint8_t> &output,
    SignDxbcFnDX12 pfnSigner, CUtlVector<uint8_t> &bytecode, CUtlString &error )
{
	if ( !pfnSigner || !tokens.m_pByteCode || tokens.m_byteCodeSize < 8 || tokens.m_byteCodeSize > UINT32_MAX )
		return Fail( error, "missing signer or converted shader tokens" );
	CDXBCBuilder builder( false );
	if ( !CheckResult( builder.AppendBlob( DXBC_InputSignature11_1, static_cast<UINT32>( input.Count() ), input.Base() ), "append input signature", error ) ||
	    !CheckResult( builder.AppendBlob( DXBC_OutputSignature11_1, static_cast<UINT32>( output.Count() ), output.Base() ), "append output signature", error ) ||
	    !CheckResult( builder.AppendBlob( DXBC_GenericShader, static_cast<UINT32>( tokens.m_byteCodeSize ), tokens.m_pByteCode ), "append shader", error ) )
		return false;
	UINT32 nSize = 0;
	if ( !CheckResult( builder.GetFinalDXBC( nullptr, &nSize ), "size DXBC", error ) || !nSize )
		return false;
	bytecode.SetCount( static_cast<int>( nSize ) );
	if ( !CheckResult( builder.GetFinalDXBC( bytecode.Base(), &nSize ), "assemble DXBC", error ) ||
	    !CheckResult( pfnSigner( bytecode.Base(), nSize ), "sign DXBC", error ) )
	{
		bytecode.RemoveAll();
		return false;
	}
	return true;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Translates one legacy VS/PS variant into signed SM5 DXBC plus linkage
//          and inline-constant metadata; result stays empty on failure
//-----------------------------------------------------------------------------
bool CShaderTranslatorDX12::TranslateLegacy( const ShaderTranslationRequestDX12 &request, SignDxbcFnDX12 pfnSigner,
    ShaderTranslationResultDX12 &result, CUtlString &error ) const
{
	result.Purge();
	error.Clear();
	ZoneScopedN( "DX12 TranslateLegacy" );
	if ( !ValidateLegacy( request, error ) )
		return false;
	if ( ( !request.vertexInputs && request.vertexInputCount ) || request.vertexInputCount > ShaderConv::MAX_VS_INPUT_REGS )
		return Fail( error, "invalid vertex declaration span" );
	try
	{
		ShaderTranslationResultDX12 converted;
		ShaderConv::VSInputDecls inputs( ShaderConv::MAX_VS_INPUT_REGS );
		for ( size_t i = 0; i < request.vertexInputCount; ++i )
		{
			const ShaderVertexInputDX12 &input = request.vertexInputs[i];
			if ( input.usage > D3DDECLUSAGE_SAMPLE || input.usageIndex >= 16 || input.registerIndex >= 16 || input.conversion > 3 || input.componentType < 1 || input.componentType > 3 )
				return Fail( error, "invalid vertex declaration element" );
			inputs.AddDecl( input.usage, input.usageIndex, input.registerIndex, input.transformedPosition, input.conversion );
		}
		ShaderConv::VSOutputDecls linked, outputs;
		if ( !ImportLinkage( request.linkedOutputs, request.linkedOutputCount, request.centroidTexcoordMask, linked, error ) )
			return false;
		const ShaderConv::RasterStates raster = Raster( request.raster );
		ShaderConv::ConvertShaderArgs args( 9, ShaderConv::AnythingTimes0Equals0, raster );
		args.type = request.pixel ? ShaderConv::ConvertShaderArgs::SHADER_TYPE::SHADER_TYPE_PIXEL : ShaderConv::ConvertShaderArgs::SHADER_TYPE::SHADER_TYPE_VERTEX;
		args.outputRegistersMask = 0;
		args.pVsInputDecl = &inputs;
		args.pVsOutputDecl = &outputs;
		args.pPsInputDecl = request.pixel ? &linked : nullptr;
		args.legacyByteCode.m_pByteCode = const_cast<void *>( request.legacyBytes );
		args.legacyByteCode.m_byteCodeSize = request.byteCount;
		ConvertedTokens cleanup{ args.convertedByteCode };
		ShaderConv::ShaderConverterAPI compiler;
		if ( !CheckResult( compiler.ConvertShader( args ), "convert legacy shader", error ) )
			return false;
		CUtlVector<uint8_t> inputSignature, outputSignature;
		if ( request.pixel )
		{
			for ( const ShaderConv::VSOutputDecl &semantic : args.AddedSystemSemantics )
				linked.AddDecl( semantic );
			ExportLinkage( linked, converted.inputLinkage );
			LinkageSignature( linked, inputSignature );
			CUtlVector<NamedParameter> parameters;
			for ( UINT i = 0; i < ShaderConv::MAX_PS_COLOROUT_REGS; ++i )
			{
				if ( !( args.outputRegistersMask & ( 1u << i ) ) )
					continue;
				NamedParameter entry{};
				entry.name = "SV_Target";
				entry.parameter.semanticIndex = i;
				entry.parameter.systemValue = D3D_NAME_TARGET;
				entry.parameter.reg = i;
				entry.parameter.mask = 15;
				parameters.AddToTail( entry );
			}
			if ( args.outputRegistersMask & ShaderConv::DEPTH_OUTPUT_MASK )
			{
				NamedParameter entry{};
				entry.name = "SV_Depth";
				entry.parameter.systemValue = D3D_NAME_DEPTH;
				entry.parameter.reg = UINT32_MAX;
				entry.parameter.mask = 1;
				parameters.AddToTail( entry );
			}
			PackSignature( parameters, outputSignature );
		}
		else
		{
			// ConvertShader replaces inputs with the shader's declarations, including absent stream inputs.
			// The emitted IA signature below omits those inputs, but c3 must still clear for an unflexed draw.
			converted.cpuFlexInput = inputs.FindRegisterIndex( D3DDECLUSAGE_POSITION, 1 ) != ShaderConv::VSInputDecls::INVALID_INDEX;
			CUtlVector<NamedParameter> parameters;
			for ( size_t i = 0; i < request.vertexInputCount; ++i )
			{
				ShaderVertexInputDX12 input = request.vertexInputs[i];
				const UINT nReg = inputs.FindRegisterIndex( input.usage, input.usageIndex );
				if ( nReg == ShaderConv::VSInputDecls::INVALID_INDEX )
					continue;
				input.registerIndex = nReg;
				converted.vertexInputs.AddToTail( input );
				NamedParameter entry{};
				entry.name = Semantic( input.usage, true );
				entry.parameter.semanticIndex = input.usageIndex;
				entry.parameter.componentType = input.componentType;
				entry.parameter.reg = nReg;
				entry.parameter.mask = entry.parameter.usedMask = 15;
				parameters.AddToTail( entry );
			}
			PackSignature( parameters, inputSignature );
			ExportLinkage( outputs, converted.outputLinkage );
			LinkageSignature( outputs, outputSignature );
		}
		if ( !Assemble( args.convertedByteCode, inputSignature, outputSignature, pfnSigner, converted.bytecode, error ) )
			return false;
		for ( int nFile = 0; nFile < ARRAYSIZE( converted.inlineConstants ); ++nFile )
		{
			converted.inlineConstants[nFile].EnsureCapacity( static_cast<int>( args.m_inlineConsts[nFile].size() ) );
			for ( const ShaderConv::ShaderConst &constant : args.m_inlineConsts[nFile] )
			{
				// Float/int offsets count scalar words; the renderer indexes float4/int4 registers.
				ShaderInlineConstantDX12 entry;
				entry.registerIndex = nFile == 2 ? constant.RegIndex : constant.RegIndex / 4;
				memcpy( entry.words, constant.Value, nFile == 2 ? sizeof( uint32_t ) : sizeof( constant.Value ) );
				converted.inlineConstants[nFile].AddToTail( entry );
			}
		}
		converted.maxFloatConstants = ( args.maxFloatConstsUsed + 3 ) / 4;
		converted.maxIntConstants = ( args.maxIntConstsUsed + 3 ) / 4;
		converted.maxBoolConstants = args.maxBoolConstsUsed;
		converted.outputRegistersMask = args.outputRegistersMask;
		converted.usedSamplerMask = args.usedSamplerMask;
		result.Swap( converted );
		return true;
	}
	catch ( const std::exception &exception )
	{
		error.Format( "shader reconstruction: %s", exception.what() );
		return false;
	}
	catch ( ... )
	{
		return Fail( error, "shader reconstruction raised an exception" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Generates the SM5 point-expansion geometry shader for a VS output linkage
//-----------------------------------------------------------------------------
bool CShaderTranslatorDX12::GenerateGeometry( const ShaderLinkageDX12 *pInputs, size_t nInputCount,
    const ShaderRasterStateDX12 &raster, SignDxbcFnDX12 pfnSigner,
    ShaderTranslationResultDX12 &result, CUtlString &error ) const
{
	ZoneScopedN( "DX12 GenerateGeometry" );
	result.Purge();
	error.Clear();
	try
	{
		ShaderTranslationResultDX12 converted;
		ShaderConv::VSOutputDecls input, output;
		if ( !ImportLinkage( pInputs, nInputCount, 0, input, error ) )
			return false;
		const ShaderConv::RasterStates rasterStates = Raster( raster );
		ShaderConv::CreateGeometryShaderArgs args( 9, 0, input, &output, rasterStates );
		ConvertedTokens cleanup{ args.m_GSByteCode };
		ShaderConv::ShaderConverterAPI compiler;
		if ( !CheckResult( compiler.CreateGeometryShader( args ), "generate geometry shader", error ) )
			return false;
		CUtlVector<uint8_t> inputSignature, outputSignature;
		LinkageSignature( input, inputSignature );
		LinkageSignature( output, outputSignature );
		if ( !Assemble( args.m_GSByteCode, inputSignature, outputSignature, pfnSigner, converted.bytecode, error ) )
			return false;
		ExportLinkage( input, converted.inputLinkage );
		ExportLinkage( output, converted.outputLinkage );
		result.Swap( converted );
		return true;
	}
	catch ( const std::exception &exception )
	{
		error.Format( "geometry reconstruction: %s", exception.what() );
		return false;
	}
	catch ( ... )
	{
		return Fail( error, "geometry reconstruction raised an exception" );
	}
}
} // namespace shaderapidx12
