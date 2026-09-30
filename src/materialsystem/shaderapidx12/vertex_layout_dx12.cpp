//========= Copyright Valve Corporation, All rights reserved. ============//
//
// CPU counterpart of shaderapidx9/meshbase.h::ComputeVertexDesc and
// vertexdecl.cpp::ComputeVertexSpec, adapted to the current public imesh.h.
//=========================================================================
#include "vertex_layout_dx12.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include "shader_translate_dx12.h"

namespace shaderapidx12
{
bool ReadShaderInputSignatureDX12(const void *bytecode, size_t bytes,
                                 std::vector<ShaderInputElementDX12> &inputs,
                                 std::array<uint32_t,3> *constantRegisters,
                                 std::vector<ShaderLinkageDX12> *outputs)
{
    inputs.clear();
    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
    if (!bytecode || !bytes || FAILED(D3DReflect(bytecode, bytes, IID_PPV_ARGS(&reflection))))
        return false;
    D3D11_SHADER_DESC shader = {};
    if (FAILED(reflection->GetDesc(&shader))) return false;
    if (constantRegisters)
    {
        // Only register space 0 carries the legacy root-CBV register counts; space-1 blocks (engine and material
        // named cbuffers) are bound through the space-1 descriptor tables.
        constantRegisters->fill(0);
        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> spaces;
        if (FAILED(D3DReflect(bytecode, bytes, IID_PPV_ARGS(&spaces)))) return false;
        for (UINT i=0;i<shader.BoundResources;++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC binding = {};
            if (FAILED(spaces->GetResourceBindingDesc(i,&binding))) return false;
            if (binding.Type!=D3D_SIT_CBUFFER || binding.Space!=0 || binding.BindPoint>=3) continue;
            D3D12_SHADER_BUFFER_DESC buffer = {};
            if (FAILED(spaces->GetConstantBufferByName(binding.Name)->GetDesc(&buffer))) return false;
            (*constantRegisters)[binding.BindPoint]=(buffer.Size+(binding.BindPoint==2?3:15))/(binding.BindPoint==2?4:16);
        }
    }
    if (outputs)
    {
        outputs->clear();
        for (UINT i=0;i<shader.OutputParameters;++i)
        {
            D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
            if (FAILED(reflection->GetOutputParameterDesc(i,&parameter))) return false;
            uint32_t usage;
            if (!_stricmp(parameter.SemanticName,"POSITION") || !_stricmp(parameter.SemanticName,"SV_POSITION")) usage=0;
            else if (!_stricmp(parameter.SemanticName,"COLOR")) usage=10;
            else if (!_stricmp(parameter.SemanticName,"TEXCOORD")) usage=5;
            else if (!_stricmp(parameter.SemanticName,"SV_ClipDistance")) usage=16;
            else continue;
            outputs->push_back({usage,parameter.SemanticIndex,parameter.Register,parameter.Mask,false});
        }
    }
    inputs.reserve(shader.InputParameters);
    for (UINT i = 0; i < shader.InputParameters; ++i)
    {
        D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
        if (FAILED(reflection->GetInputParameterDesc(i, &parameter))) return false;
        if (parameter.SystemValueType != D3D_NAME_UNDEFINED) continue;
        ShaderInputElementDX12 input;
        input.semantic = parameter.SemanticName;
        input.semanticIndex = parameter.SemanticIndex;
        switch (parameter.ComponentType)
        {
        case D3D_REGISTER_COMPONENT_FLOAT32: input.format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
        case D3D_REGISTER_COMPONENT_UINT32: input.format = DXGI_FORMAT_R32G32B32A32_UINT; break;
        case D3D_REGISTER_COMPONENT_SINT32: input.format = DXGI_FORMAT_R32G32B32A32_SINT; break;
        default: return false;
        }
        inputs.push_back(std::move(input));
    }
    return true;
}

namespace
{
// Every missing CMeshBuilder destination is writable, and a zero stride keeps
// successive vertices writing to the same discard area. Separate threads must
// not race on the reference implementation's process-global dummy buffer.
alignas( 32 ) static thread_local unsigned char s_dummyVertex[512] = {};

template <typename T>
T *FieldPointer( unsigned char *data, uint32_t offset, bool present )
{
    if ( !present )
        return reinterpret_cast<T *>( s_dummyVertex );
    // Do not do arithmetic on a null pointer for format-only queries.
    return data ? reinterpret_cast<T *>( reinterpret_cast<uintptr_t>( data ) + offset ) : nullptr;
}

void AddInput( VertexLayoutDX12 &layout, const char *semantic, uint32_t semanticIndex,
               DXGI_FORMAT format, uint32_t stream, uint32_t offset,
               bool swapRedBlue = false, bool integerToFloat = false )
{
    Assert( layout.inputCount < MAX_VERTEX_INPUTS_DX12 );
    VertexInputDX12 &input = layout.inputs[layout.inputCount++];
    input.semantic = semantic;
    input.semanticIndex = semanticIndex;
    input.format = format;
    input.inputSlot = stream;
    input.byteOffset = offset;
    input.swapRedBlue = swapRedBlue;
    input.integerToFloat = integerToFloat;
}

DXGI_FORMAT FloatFormat( int count )
{
    switch ( count )
    {
    case 1: return DXGI_FORMAT_R32_FLOAT;
    case 2: return DXGI_FORMAT_R32G32_FLOAT;
    case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
} // namespace

VertexLayoutDX12 ComputeVertexLayoutDX12( VertexFormat_t format, unsigned char *vertexData,
                                           VertexDesc_t *desc, VertexInputStreamsDX12 streams )
{
    VertexLayoutDX12 layout = {};
    const int weights = NumBoneWeights( format );
    const int userdata = UserDataSize( format );
    const bool compressed = CompressionType( format ) == VERTEX_COMPRESSION_ON;
    const bool position = ( format & VERTEX_POSITION ) != 0;
    const bool wrinkle = ( format & VERTEX_WRINKLE ) != 0;
    const bool normal = ( format & VERTEX_NORMAL ) != 0;
    const bool boneIndices = ( format & VERTEX_BONE_INDEX ) != 0;

    // meshbase.h only defines the two-weight skinning layout. Packed tangent
    // userdata4 aliases the normal word, so without NORMAL it is unwritable.
    if ( ( wrinkle && !position ) || ( weights != 0 && weights != 2 ) ||
         ( ( weights == 2 ) != boneIndices ) || userdata > 4 ||
         ( compressed && userdata == 4 && !normal ) ||
         ( streams.staticLighting && ( format & VERTEX_SPECULAR ) ) ||
         ( format >> ( TEX_COORD_SIZE_BIT + 3 * VERTEX_MAX_TEXTURE_COORDINATES ) ) )
        return layout;

    for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i )
    {
        if ( TexCoordSize( i, format ) > 4 )
            return layout;
    }

    int *presentSizes[20];
    unsigned int sizeCount = 0;
    uint32_t offset = 0;
    const VertexCompressionType_t compression = CompressionType( format );
    if ( desc )
    {
        desc->m_CompressionType = compression;
        desc->m_NumBoneWeights = weights;
        desc->m_ActualVertexSize = 0;
    }

    if ( desc )
    {
        desc->m_pPosition = FieldPointer<float>( vertexData, offset, position );
        desc->m_VertexSize_Position = 0;
        if ( position ) presentSizes[sizeCount++] = &desc->m_VertexSize_Position;
    }
    if ( position )
    {
        AddInput( layout, "POSITION", 0, wrinkle ? FloatFormat( 4 ) : FloatFormat( 3 ), 0, offset );
        offset += GetVertexElementSize( VERTEX_ELEMENT_POSITION, compression );
    }
    if ( desc )
    {
        desc->m_pWrinkle = FieldPointer<float>( vertexData, offset, wrinkle );
        desc->m_VertexSize_Wrinkle = 0;
        if ( wrinkle ) presentSizes[sizeCount++] = &desc->m_VertexSize_Wrinkle;
    }
    if ( wrinkle )
        offset += GetVertexElementSize( VERTEX_ELEMENT_WRINKLE, compression );

    if ( desc )
    {
        desc->m_pBoneWeight = FieldPointer<float>( vertexData, offset, weights != 0 );
        desc->m_VertexSize_BoneWeight = 0;
        if ( weights ) presentSizes[sizeCount++] = &desc->m_VertexSize_BoneWeight;
    }
    if ( weights )
    {
        AddInput( layout, "BLENDWEIGHT", 0,
                  compressed ? DXGI_FORMAT_R16G16_SINT : FloatFormat( 2 ), 0, offset,
                  false, compressed );
        offset += GetVertexElementSize( VERTEX_ELEMENT_BONEWEIGHTS2, compression );
    }
    if ( desc )
    {
#ifndef NEW_SKINNING
        desc->m_pBoneMatrixIndex = FieldPointer<unsigned char>( vertexData, offset, boneIndices );
#else
        desc->m_pBoneMatrixIndex = FieldPointer<float>( vertexData, offset, boneIndices );
#endif
        desc->m_VertexSize_BoneMatrixIndex = 0;
        if ( boneIndices ) presentSizes[sizeCount++] = &desc->m_VertexSize_BoneMatrixIndex;
    }
    if ( boneIndices )
    {
        // D3DDECLTYPE_D3DCOLOR (not UBYTE4) in the PC reference declaration.
        AddInput( layout, "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offset, true );
        offset += GetVertexElementSize( VERTEX_ELEMENT_BONEINDEX, compression );
    }

    const uint32_t normalOffset = offset;
    if ( desc )
    {
        desc->m_pNormal = FieldPointer<float>( vertexData, offset, normal );
        desc->m_VertexSize_Normal = 0;
        if ( normal ) presentSizes[sizeCount++] = &desc->m_VertexSize_Normal;
    }
    if ( normal )
    {
        AddInput( layout, "NORMAL", 0,
                  compressed ? DXGI_FORMAT_R8G8B8A8_UINT : FloatFormat( 3 ), 0, offset,
                  false, compressed );
        offset += GetVertexElementSize( VERTEX_ELEMENT_NORMAL, compression );
    }

    const bool color = ( format & VERTEX_COLOR ) != 0;
    if ( desc )
    {
        desc->m_pColor = FieldPointer<unsigned char>( vertexData, offset, color );
        desc->m_VertexSize_Color = 0;
        if ( color ) presentSizes[sizeCount++] = &desc->m_VertexSize_Color;
    }
    if ( color )
    {
        AddInput( layout, "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offset, true );
        offset += GetVertexElementSize( VERTEX_ELEMENT_COLOR, compression );
    }

    const bool specular = ( format & VERTEX_SPECULAR ) != 0;
    if ( desc )
    {
        desc->m_pSpecular = FieldPointer<unsigned char>( vertexData, offset, specular );
        desc->m_VertexSize_Specular = 0;
        if ( specular ) presentSizes[sizeCount++] = &desc->m_VertexSize_Specular;
    }
    if ( specular )
    {
        AddInput( layout, "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offset, true );
        offset += GetVertexElementSize( VERTEX_ELEMENT_SPECULAR, compression );
    }

    for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i )
    {
        const int n = TexCoordSize( i, format );
        if ( desc )
        {
            desc->m_pTexCoord[i] = FieldPointer<float>( vertexData, offset, n != 0 );
            desc->m_VertexSize_TexCoord[i] = 0;
            if ( n ) presentSizes[sizeCount++] = &desc->m_VertexSize_TexCoord[i];
        }
        if ( n )
        {
            const VertexElement_t element = static_cast<VertexElement_t>( VERTEX_ELEMENT_TEXCOORD1D_0 + 8 * ( n - 1 ) + i );
            AddInput( layout, "TEXCOORD", i, FloatFormat( n ), 0, offset );
            offset += GetVertexElementSize( element, compression );
        }
    }

    const bool tangentS = ( format & VERTEX_TANGENT_S ) != 0;
    if ( desc )
    {
        desc->m_pTangentS = FieldPointer<float>( vertexData, offset, tangentS );
        desc->m_VertexSize_TangentS = 0;
        if ( tangentS ) presentSizes[sizeCount++] = &desc->m_VertexSize_TangentS;
    }
    if ( tangentS )
    {
        AddInput( layout, "TANGENT", 0, FloatFormat( 3 ), 0, offset );
        offset += GetVertexElementSize( VERTEX_ELEMENT_TANGENT_S, compression );
    }
    const bool tangentT = ( format & VERTEX_TANGENT_T ) != 0;
    if ( desc )
    {
        desc->m_pTangentT = FieldPointer<float>( vertexData, offset, tangentT );
        desc->m_VertexSize_TangentT = 0;
        if ( tangentT ) presentSizes[sizeCount++] = &desc->m_VertexSize_TangentT;
    }
    if ( tangentT )
    {
        AddInput( layout, "BINORMAL", 0, FloatFormat( 3 ), 0, offset );
        offset += GetVertexElementSize( VERTEX_ELEMENT_TANGENT_T, compression );
    }

    if ( desc )
    {
        desc->m_pUserData = FieldPointer<float>( vertexData, offset, userdata != 0 );
        desc->m_VertexSize_UserData = 0;
        if ( userdata ) presentSizes[sizeCount++] = &desc->m_VertexSize_UserData;
    }
    if ( userdata )
    {
        const bool packedTangent = compressed && userdata == 4;
        AddInput( layout, "TANGENT", 0,
                  packedTangent ? DXGI_FORMAT_R8G8B8A8_UINT : FloatFormat( userdata ),
                  0, packedTangent ? normalOffset : offset, false, packedTangent );
        offset += GetVertexElementSize( static_cast<VertexElement_t>( VERTEX_ELEMENT_USERDATA1 + userdata - 1 ), compression );
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

    if ( !( format & VERTEX_FORMAT_USE_EXACT_FORMAT ) && offset > 16 )
        offset = ( offset + 15 ) & ~uint32_t( 15 );
    layout.stride = offset;
    layout.valid = true;
    if ( desc )
    {
        desc->m_ActualVertexSize = static_cast<int>( offset );
        for ( unsigned int i = 0; i < sizeCount; ++i )
            *presentSizes[i] = static_cast<int>( offset );
    }
    return layout;
}

} // namespace shaderapidx12
