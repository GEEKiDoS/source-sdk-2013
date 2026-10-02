//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: CPU counterpart of shaderapidx9/meshbase.h::ComputeVertexDesc and
//          vertexdecl.cpp::ComputeVertexSpec, adapted to the current public imesh.h.
//
//=============================================================================//

#include "vertex_layout_dx12.h"
#include "shader_translate_dx12.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <d3d12shader.h>
#include <wrl/client.h>

namespace shaderapidx12
{
//-----------------------------------------------------------------------------
// Purpose: Reflects the IA input signature, optionally the space-0 legacy constant
//          register counts and the VS output linkage
//-----------------------------------------------------------------------------
bool ReadShaderInputSignatureDX12( const void *pBytecode, size_t nBytes,
    CUtlVector<ShaderInputElementDX12> &inputs,
    uint32_t ( *pConstantRegisters )[3],
    CUtlVector<ShaderLinkageDX12> *pOutputs )
{
	inputs.RemoveAll();
	Microsoft::WRL::ComPtr<ID3D11ShaderReflection> pReflection;
	if ( !pBytecode || !nBytes || FAILED( D3DReflect( pBytecode, nBytes, IID_PPV_ARGS( &pReflection ) ) ) )
		return false;
	D3D11_SHADER_DESC shader = {};
	if ( FAILED( pReflection->GetDesc( &shader ) ) )
		return false;
	if ( pConstantRegisters )
	{
		// Only register space 0 carries the legacy root-CBV register counts; space-1 blocks (engine and material
		// named cbuffers) are bound through the space-1 descriptor tables.
		memset( *pConstantRegisters, 0, sizeof( *pConstantRegisters ) );
		Microsoft::WRL::ComPtr<ID3D12ShaderReflection> pSpaces;
		if ( FAILED( D3DReflect( pBytecode, nBytes, IID_PPV_ARGS( &pSpaces ) ) ) )
			return false;
		for ( UINT i = 0; i < shader.BoundResources; ++i )
		{
			D3D12_SHADER_INPUT_BIND_DESC binding = {};
			if ( FAILED( pSpaces->GetResourceBindingDesc( i, &binding ) ) )
				return false;
			if ( binding.Type != D3D_SIT_CBUFFER || binding.Space != 0 || binding.BindPoint >= 3 )
				continue;
			D3D12_SHADER_BUFFER_DESC buffer = {};
			if ( FAILED( pSpaces->GetConstantBufferByName( binding.Name )->GetDesc( &buffer ) ) )
				return false;
			( *pConstantRegisters )[binding.BindPoint] = ( buffer.Size + ( binding.BindPoint == 2 ? 3 : 15 ) ) / ( binding.BindPoint == 2 ? 4 : 16 );
		}
	}
	if ( pOutputs )
	{
		pOutputs->RemoveAll();
		for ( UINT i = 0; i < shader.OutputParameters; ++i )
		{
			D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
			if ( FAILED( pReflection->GetOutputParameterDesc( i, &parameter ) ) )
				return false;
			uint32_t nUsage;
			if ( !_stricmp( parameter.SemanticName, "POSITION" ) || !_stricmp( parameter.SemanticName, "SV_POSITION" ) )
				nUsage = 0;
			else if ( !_stricmp( parameter.SemanticName, "COLOR" ) )
				nUsage = 10;
			else if ( !_stricmp( parameter.SemanticName, "TEXCOORD" ) )
				nUsage = 5;
			else if ( !_stricmp( parameter.SemanticName, "SV_ClipDistance" ) )
				nUsage = 16;
			else
				continue;
			ShaderLinkageDX12 &link = ( *pOutputs )[pOutputs->AddToTail()];
			link.usage = nUsage;
			link.usageIndex = parameter.SemanticIndex;
			link.registerIndex = parameter.Register;
			link.writeMask = parameter.Mask;
			link.centroid = false;
		}
	}
	inputs.EnsureCapacity( static_cast<int>( shader.InputParameters ) );
	for ( UINT i = 0; i < shader.InputParameters; ++i )
	{
		D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
		if ( FAILED( pReflection->GetInputParameterDesc( i, &parameter ) ) )
			return false;
		if ( parameter.SystemValueType != D3D_NAME_UNDEFINED )
			continue;
		DXGI_FORMAT format;
		switch ( parameter.ComponentType )
		{
		case D3D_REGISTER_COMPONENT_FLOAT32:
			format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			break;
		case D3D_REGISTER_COMPONENT_UINT32:
			format = DXGI_FORMAT_R32G32B32A32_UINT;
			break;
		case D3D_REGISTER_COMPONENT_SINT32:
			format = DXGI_FORMAT_R32G32B32A32_SINT;
			break;
		default:
			return false;
		}
		ShaderInputElementDX12 &input = inputs[inputs.AddToTail()];
		input.semantic = parameter.SemanticName;
		input.semanticIndex = parameter.SemanticIndex;
		input.format = format;
	}
	return true;
}

namespace
{
// Every missing CMeshBuilder destination is writable, and a zero stride keeps
// successive vertices writing to the same discard area. Separate threads must
// not race on the reference implementation's process-global dummy buffer.
alignas( 32 ) static thread_local unsigned char s_DummyVertex[512] = {};

//-----------------------------------------------------------------------------
// Purpose: Descriptor field pointer: real field, discard area, or null for format-only queries
//-----------------------------------------------------------------------------
template <typename T>
T *FieldPointer( unsigned char *pData, uint32_t nOffset, bool bPresent )
{
	if ( !bPresent )
		return reinterpret_cast<T *>( s_DummyVertex );
	// Do not do arithmetic on a null pointer for format-only queries.
	return pData ? reinterpret_cast<T *>( reinterpret_cast<uintptr_t>( pData ) + nOffset ) : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Appends one IA element to the layout
//-----------------------------------------------------------------------------
void AddInput( VertexLayoutDX12 &layout, const char *pszSemantic, uint32_t nSemanticIndex,
    DXGI_FORMAT format, uint32_t nStream, uint32_t nOffset,
    bool bSwapRedBlue = false, bool bIntegerToFloat = false )
{
	Assert( layout.inputCount < MAX_VERTEX_INPUTS_DX12 );
	VertexInputDX12 &input = layout.inputs[layout.inputCount++];
	input.semantic = pszSemantic;
	input.semanticIndex = nSemanticIndex;
	input.format = format;
	input.inputSlot = nStream;
	input.byteOffset = nOffset;
	input.swapRedBlue = bSwapRedBlue;
	input.integerToFloat = bIntegerToFloat;
}

//-----------------------------------------------------------------------------
// Purpose: 32-bit float format with nCount components
//-----------------------------------------------------------------------------
DXGI_FORMAT FloatFormat( int nCount )
{
	switch ( nCount )
	{
	case 1:
		return DXGI_FORMAT_R32_FLOAT;
	case 2:
		return DXGI_FORMAT_R32G32_FLOAT;
	case 3:
		return DXGI_FORMAT_R32G32B32_FLOAT;
	case 4:
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Computes stride, IA elements and (optionally) the CMeshBuilder descriptor
//          for a Source vertex format
//-----------------------------------------------------------------------------
VertexLayoutDX12 ComputeVertexLayoutDX12( VertexFormat_t format, unsigned char *pVertexData,
    VertexDesc_t *pDesc, VertexInputStreamsDX12 streams )
{
	VertexLayoutDX12 layout = {};
	const int nWeights = NumBoneWeights( format );
	const int nUserData = UserDataSize( format );
	const bool bCompressed = CompressionType( format ) == VERTEX_COMPRESSION_ON;
	const bool bPosition = ( format & VERTEX_POSITION ) != 0;
	const bool bWrinkle = ( format & VERTEX_WRINKLE ) != 0;
	const bool bNormal = ( format & VERTEX_NORMAL ) != 0;
	const bool bBoneIndices = ( format & VERTEX_BONE_INDEX ) != 0;

	// meshbase.h only defines the two-weight skinning layout. Packed tangent
	// userdata4 aliases the normal word, so without NORMAL it is unwritable.
	if ( ( bWrinkle && !bPosition ) || ( nWeights != 0 && nWeights != 2 ) ||
	    ( ( nWeights == 2 ) != bBoneIndices ) || nUserData > 4 ||
	    ( bCompressed && nUserData == 4 && !bNormal ) ||
	    ( streams.staticLighting && ( format & VERTEX_SPECULAR ) ) ||
	    ( format >> ( TEX_COORD_SIZE_BIT + 3 * VERTEX_MAX_TEXTURE_COORDINATES ) ) )
		return layout;

	for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i )
	{
		if ( TexCoordSize( i, format ) > 4 )
			return layout;
	}

	int *pPresentSizes[20];
	unsigned int nSizeCount = 0;
	uint32_t nOffset = 0;
	const VertexCompressionType_t compression = CompressionType( format );
	if ( pDesc )
	{
		pDesc->m_CompressionType = compression;
		pDesc->m_NumBoneWeights = nWeights;
		pDesc->m_ActualVertexSize = 0;
	}

	if ( pDesc )
	{
		pDesc->m_pPosition = FieldPointer<float>( pVertexData, nOffset, bPosition );
		pDesc->m_VertexSize_Position = 0;
		if ( bPosition )
			pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_Position;
	}
	if ( bPosition )
	{
		AddInput( layout, "POSITION", 0, bWrinkle ? FloatFormat( 4 ) : FloatFormat( 3 ), 0, nOffset );
		nOffset += GetVertexElementSize( VERTEX_ELEMENT_POSITION, compression );
	}
	if ( pDesc )
	{
		pDesc->m_pWrinkle = FieldPointer<float>( pVertexData, nOffset, bWrinkle );
		pDesc->m_VertexSize_Wrinkle = 0;
		if ( bWrinkle )
			pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_Wrinkle;
	}
	if ( bWrinkle )
		nOffset += GetVertexElementSize( VERTEX_ELEMENT_WRINKLE, compression );

	if ( pDesc )
	{
		pDesc->m_pBoneWeight = FieldPointer<float>( pVertexData, nOffset, nWeights != 0 );
		pDesc->m_VertexSize_BoneWeight = 0;
		if ( nWeights )
			pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_BoneWeight;
	}
	if ( nWeights )
	{
		AddInput( layout, "BLENDWEIGHT", 0,
		    bCompressed ? DXGI_FORMAT_R16G16_SINT : FloatFormat( 2 ), 0, nOffset,
		    false, bCompressed );
		nOffset += GetVertexElementSize( VERTEX_ELEMENT_BONEWEIGHTS2, compression );
	}
	if ( pDesc )
	{
#ifndef NEW_SKINNING
		pDesc->m_pBoneMatrixIndex = FieldPointer<unsigned char>( pVertexData, nOffset, bBoneIndices );
#else
		pDesc->m_pBoneMatrixIndex = FieldPointer<float>( pVertexData, nOffset, bBoneIndices );
#endif
		pDesc->m_VertexSize_BoneMatrixIndex = 0;
		if ( bBoneIndices )
			pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_BoneMatrixIndex;
	}
	if ( bBoneIndices )
	{
		// D3DDECLTYPE_D3DCOLOR (not UBYTE4) in the PC reference declaration.
		AddInput( layout, "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, nOffset, true );
		nOffset += GetVertexElementSize( VERTEX_ELEMENT_BONEINDEX, compression );
	}

	const uint32_t nNormalOffset = nOffset;
	if ( pDesc )
	{
		pDesc->m_pNormal = FieldPointer<float>( pVertexData, nOffset, bNormal );
		pDesc->m_VertexSize_Normal = 0;
		if ( bNormal )
			pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_Normal;
	}
	if ( bNormal )
	{
		AddInput( layout, "NORMAL", 0,
		    bCompressed ? DXGI_FORMAT_R8G8B8A8_UINT : FloatFormat( 3 ), 0, nOffset,
		    false, bCompressed );
		nOffset += GetVertexElementSize( VERTEX_ELEMENT_NORMAL, compression );
	}

	const bool bColor = ( format & VERTEX_COLOR ) != 0;
	if ( pDesc )
	{
		pDesc->m_pColor = FieldPointer<unsigned char>( pVertexData, nOffset, bColor );
		pDesc->m_VertexSize_Color = 0;
		if ( bColor )
			pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_Color;
	}
	if ( bColor )
	{
		AddInput( layout, "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, nOffset, true );
		nOffset += GetVertexElementSize( VERTEX_ELEMENT_COLOR, compression );
	}

	const bool bSpecular = ( format & VERTEX_SPECULAR ) != 0;
	if ( pDesc )
	{
		pDesc->m_pSpecular = FieldPointer<unsigned char>( pVertexData, nOffset, bSpecular );
		pDesc->m_VertexSize_Specular = 0;
		if ( bSpecular )
			pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_Specular;
	}
	if ( bSpecular )
	{
		AddInput( layout, "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, nOffset, true );
		nOffset += GetVertexElementSize( VERTEX_ELEMENT_SPECULAR, compression );
	}

	for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i )
	{
		const int nTexCoordSize = TexCoordSize( i, format );
		if ( pDesc )
		{
			pDesc->m_pTexCoord[i] = FieldPointer<float>( pVertexData, nOffset, nTexCoordSize != 0 );
			pDesc->m_VertexSize_TexCoord[i] = 0;
			if ( nTexCoordSize )
				pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_TexCoord[i];
		}
		if ( nTexCoordSize )
		{
			const VertexElement_t element = static_cast<VertexElement_t>( VERTEX_ELEMENT_TEXCOORD1D_0 + 8 * ( nTexCoordSize - 1 ) + i );
			AddInput( layout, "TEXCOORD", i, FloatFormat( nTexCoordSize ), 0, nOffset );
			nOffset += GetVertexElementSize( element, compression );
		}
	}

	const bool bTangentS = ( format & VERTEX_TANGENT_S ) != 0;
	if ( pDesc )
	{
		pDesc->m_pTangentS = FieldPointer<float>( pVertexData, nOffset, bTangentS );
		pDesc->m_VertexSize_TangentS = 0;
		if ( bTangentS )
			pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_TangentS;
	}
	if ( bTangentS )
	{
		AddInput( layout, "TANGENT", 0, FloatFormat( 3 ), 0, nOffset );
		nOffset += GetVertexElementSize( VERTEX_ELEMENT_TANGENT_S, compression );
	}
	const bool bTangentT = ( format & VERTEX_TANGENT_T ) != 0;
	if ( pDesc )
	{
		pDesc->m_pTangentT = FieldPointer<float>( pVertexData, nOffset, bTangentT );
		pDesc->m_VertexSize_TangentT = 0;
		if ( bTangentT )
			pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_TangentT;
	}
	if ( bTangentT )
	{
		AddInput( layout, "BINORMAL", 0, FloatFormat( 3 ), 0, nOffset );
		nOffset += GetVertexElementSize( VERTEX_ELEMENT_TANGENT_T, compression );
	}

	if ( pDesc )
	{
		pDesc->m_pUserData = FieldPointer<float>( pVertexData, nOffset, nUserData != 0 );
		pDesc->m_VertexSize_UserData = 0;
		if ( nUserData )
			pPresentSizes[nSizeCount++] = &pDesc->m_VertexSize_UserData;
	}
	if ( nUserData )
	{
		const bool bPackedTangent = bCompressed && nUserData == 4;
		AddInput( layout, "TANGENT", 0,
		    bPackedTangent ? DXGI_FORMAT_R8G8B8A8_UINT : FloatFormat( nUserData ),
		    0, bPackedTangent ? nNormalOffset : nOffset, false, bPackedTangent );
		nOffset += GetVertexElementSize( static_cast<VertexElement_t>( VERTEX_ELEMENT_USERDATA1 + nUserData - 1 ), compression );
	}

	// Auxiliary declarations do not change the mesh's interleaved stride.
	if ( streams.staticLighting )
		AddInput( layout, "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 0, true );
	if ( streams.flex )
	{
		AddInput( layout, "POSITION", 1, FloatFormat( streams.flexWrinkle ? 4 : 3 ), 2, 0 );
		AddInput( layout, "NORMAL", 1, FloatFormat( 3 ), 2, streams.flexWrinkle ? 16 : 12 );
	}
	if ( streams.morph )
		AddInput( layout, "POSITION", 2, FloatFormat( 1 ), 3, 0 );

	if ( !( format & VERTEX_FORMAT_USE_EXACT_FORMAT ) && nOffset > 16 )
		nOffset = ( nOffset + 15 ) & ~uint32_t( 15 );
	layout.stride = nOffset;
	layout.valid = true;
	if ( pDesc )
	{
		pDesc->m_ActualVertexSize = static_cast<int>( nOffset );
		for ( unsigned int i = 0; i < nSizeCount; ++i )
			*pPresentSizes[i] = static_cast<int>( nOffset );
	}
	return layout;
}

} // namespace shaderapidx12
