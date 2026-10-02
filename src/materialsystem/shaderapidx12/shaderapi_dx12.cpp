//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 shader API; source matrix, lighting and standard-constant CPU behavior adapted from Valve's shader API.
//
//=============================================================================//

#include "shaderapi_dx12.h"
#include "shadershadow_dx12.h"
#include "hardwareconfig_dx12.h"
#include "tracy_dx12.h"
#include "materialsystem/stdshaders/common_hlsl_cpp_consts.h"
#include "shaderapi/ishaderutil.h"
#include "shaderapi/commandbuffer.h"
#include "materialsystem/materialsystem_config.h"
#include "materialsystem/ishadersystem_declarations.h"
#include "renderparm.h"
#include "tier1/keyvalues.h"
#include "tier1/convar.h"
#include "tier1/utlsymbol.h"
#include "tier1/utlstring.h"
#include "tier1/strtools.h"
#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <climits>
#include <cmath>
#include <intrin.h>
#include <emmintrin.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>

using namespace shaderapidx12;

//-----------------------------------------------------------------------------
// Purpose: FNV-1a key of a vertex layout's semantics, formats and offsets
//-----------------------------------------------------------------------------
static uint64_t TranslationLayoutKey( const VertexLayoutDX12 &layout )
{
	uint64_t key = 1469598103934665603ull;
	const auto mix = [&]( uint64_t value )
	{
		key ^= value;
		key *= 1099511628211ull;
	};
	mix( layout.stride );
	mix( layout.inputCount );
	for ( uint32_t i = 0; i < layout.inputCount; ++i )
	{
		for ( const char *name = layout.inputs[i].semantic; *name; ++name )
			mix( static_cast<unsigned char>( *name ) );
		mix( layout.inputs[i].semanticIndex );
		mix( layout.inputs[i].format );
		mix( layout.inputs[i].inputSlot );
		mix( layout.inputs[i].byteOffset );
		mix( layout.inputs[i].integerToFloat );
	}
	return key;
}

//-----------------------------------------------------------------------------
// Purpose: Appends the canonical text of one reflected cbuffer member type (recursing into
//          struct members) to the string hashed by ReflectNativeCBuffersDX12
//-----------------------------------------------------------------------------
static void HashReflectedTypeDX12( ID3D12ShaderReflectionType *pType, const D3D12_SHADER_TYPE_DESC &desc, CUtlString &canonical )
{
	char szField[96];
	V_snprintf( szField, sizeof( szField ), "%d,%d,%u,%u,%u", static_cast<int>( desc.Class ), static_cast<int>( desc.Type ), desc.Rows, desc.Columns, desc.Elements );
	canonical += szField;
	if ( desc.Class != D3D_SVC_STRUCT )
		return;
	canonical += "{";
	for ( UINT i = 0; i < desc.Members; ++i )
	{
		ID3D12ShaderReflectionType *pMember = pType->GetMemberTypeByIndex( i );
		D3D12_SHADER_TYPE_DESC memberDesc{};
		if ( !pMember || FAILED( pMember->GetDesc( &memberDesc ) ) )
			continue;
		canonical += pType->GetMemberTypeName( i );
		canonical += ":";
		HashReflectedTypeDX12( pMember, memberDesc, canonical );
		UINT nEnd = 0;
		if ( i + 1 < desc.Members )
		{
			D3D12_SHADER_TYPE_DESC next{};
			if ( SUCCEEDED( pType->GetMemberTypeByIndex( i + 1 )->GetDesc( &next ) ) )
				nEnd = next.Offset;
		}
		else
			nEnd = desc.Elements ? memberDesc.Offset + desc.Elements : memberDesc.Offset;
		V_snprintf( szField, sizeof( szField ), ":%u:%u;", memberDesc.Offset, nEnd >= memberDesc.Offset ? nEnd - memberDesc.Offset : 0 );
		canonical += szField;
	}
	canonical += "}";
}

//-----------------------------------------------------------------------------
// Purpose: Reflects every register-space-1 cbuffer of a native record once: name, binding, size, member
//          table and the canonical FNV-1a layout hash shared with the packer and generated C++ blocks.
//-----------------------------------------------------------------------------
bool shaderapidx12::ReflectNativeCBuffersDX12( ShaderRecordDX12 *record )
{
	if ( !record->legacyBytecode.IsEmpty() || record->nativeReflectionReady )
		return true;
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
	const D3D12_SHADER_BYTECODE bytecode = record->Bytecode();
	if ( !bytecode.pShaderBytecode || FAILED( D3DReflect( bytecode.pShaderBytecode, bytecode.BytecodeLength, IID_PPV_ARGS( &reflection ) ) ) )
		return false;
	D3D12_SHADER_DESC shader{};
	if ( FAILED( reflection->GetDesc( &shader ) ) )
		return false;
	record->nativeCBuffers.RemoveAll();
	record->nativeAbiHash = dx12native::kFnvOffset;
	for ( UINT i = 0; i < shader.BoundResources; ++i )
	{
		D3D12_SHADER_INPUT_BIND_DESC binding{};
		if ( FAILED( reflection->GetResourceBindingDesc( i, &binding ) ) )
			return false;
		if ( binding.Type != D3D_SIT_CBUFFER )
			continue;
		ShaderRecordDX12::NativeCBufferBindingDX12 reflected;
		reflected.name = binding.Name;
		reflected.shaderRegister = binding.BindPoint;
		reflected.registerSpace = binding.Space;
		ID3D12ShaderReflectionConstantBuffer *pBuffer = reflection->GetConstantBufferByName( binding.Name );
		D3D12_SHADER_BUFFER_DESC bufferDesc{};
		if ( !pBuffer || FAILED( pBuffer->GetDesc( &bufferDesc ) ) )
			return false;
		reflected.byteSize = bufferDesc.Size;
		char szField[96];
		V_snprintf( szField, sizeof( szField ), "|%u;", bufferDesc.Size );
		CUtlString canonical = reflected.name;
		canonical += szField;
		for ( UINT v = 0; v < bufferDesc.Variables; ++v )
		{
			ID3D12ShaderReflectionVariable *pVariable = pBuffer->GetVariableByIndex( v );
			D3D12_SHADER_VARIABLE_DESC variableDesc{};
			D3D12_SHADER_TYPE_DESC typeDesc{};
			if ( !pVariable || FAILED( pVariable->GetDesc( &variableDesc ) ) || !pVariable->GetType() || FAILED( pVariable->GetType()->GetDesc( &typeDesc ) ) )
				return false;
			canonical += variableDesc.Name;
			canonical += ":";
			HashReflectedTypeDX12( pVariable->GetType(), typeDesc, canonical );
			V_snprintf( szField, sizeof( szField ), ":%u:%u;", variableDesc.StartOffset, variableDesc.Size );
			canonical += szField;
			ShaderRecordDX12::NativeCBufferMemberDX12 &member = reflected.members[reflected.members.AddToTail()];
			member.name = variableDesc.Name;
			member.offset = variableDesc.StartOffset;
			member.byteSize = variableDesc.Size;
		}
		reflected.layoutHash = dx12native::HashString( canonical.Get() );
		record->nativeAbiHash = dx12native::HashBytes( reflected.name.Get(), reflected.name.Length(), record->nativeAbiHash );
		record->nativeAbiHash = ( record->nativeAbiHash ^ reflected.layoutHash ) * dx12native::kFnvPrime;
		record->nativeCBuffers.AddToTail( reflected );
	}
	record->nativeReflectionReady = true;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Folds every raster-state field that changes translated bytecode into a translation key
//-----------------------------------------------------------------------------
static uint64_t TranslationStateKey( uint64_t key, const ShaderRasterStateDX12 &raster )
{
	ZoneNamedN( stateKey, "DX12 TranslationStateKey", DX12_DRAW_ZONES_ACTIVE );
	const auto mix = [&]( uint64_t value )
	{
		key ^= value;
		key *= 1099511628211ull;
	};
	for ( uint8_t type : raster.textureTypes )
		mix( type );
	for ( uint8_t wrap : raster.texcoordWrap )
		mix( wrap );
	mix( raster.texcoordMapping );
	mix( raster.projectedTexcoords );
	mix( raster.comparisonPixelSamplers );
	mix( raster.comparisonVertexSamplers );
	mix( raster.clipPlaneMask );
	mix( raster.alphaTest );
	mix( raster.alphaFunction );
	mix( raster.fog );
	mix( raster.fogTableMode );
	mix( raster.primitiveType );
	mix( raster.fillMode );
	mix( raster.shadeMode );
	mix( raster.wFog );
	mix( raster.pointSize );
	mix( raster.pointSprite );
	mix( raster.transformedVertices );
	return key;
}

//-----------------------------------------------------------------------------
// Purpose: Makes the stored translation `key` the record's active variant; false when it was never translated.
//-----------------------------------------------------------------------------
static bool ActivateTranslatedVariant( ShaderRecordDX12 *record, uint64_t key )
{
	if ( record->activeVariantValid && record->activeVariantKey == key )
		return true;
	for ( int index = 0; index < record->variants.Count(); ++index )
	{
		ShaderRecordDX12::Variant &variant = record->variants[index];
		if ( variant.key != key )
			continue;
		record->translated.Swap( variant.result );
		record->inputSignature.Swap( variant.inputSignature );
		V_swap( record->inputSignatureReady, variant.inputSignatureReady );
		record->derived.Swap( variant.derived );
		V_swap( record->activeVariantKey, variant.key );
		record->activeVariantValid = true;
		record->linkageHashValid = false;
		record->constantLayoutValid = false;
		return true;
	}
	return false;
}

static bool TranslateVariant( ShaderRecordDX12 *record, bool pixel, const VertexLayoutDX12 &layout, const ShaderLinkageDX12 *linked, size_t linkedCount, const ShaderRasterStateDX12 &raster, uint64_t key, SignDxbcFnDX12 signer );

//-----------------------------------------------------------------------------
// Purpose: Activates (translating on a miss) the variant of a legacy record for this layout, linkage and raster state
//-----------------------------------------------------------------------------
bool EnsureTranslated( ShaderRecordDX12 *record, bool pixel, const VertexLayoutDX12 &layout, const ShaderLinkageDX12 *linked, size_t linkedCount, uint64_t linkageHash, const ShaderRasterStateDX12 &raster, uint64_t stateKey, SignDxbcFnDX12 signer )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 EnsureTranslated", DX12_DRAW_ZONES_ACTIVE );
	if ( record->legacyBytecode.IsEmpty() )
		return record->bytecode.Count() >= 4;
	if ( !signer )
		return false;
	uint64_t key = stateKey;
	const auto mix = [&]( uint64_t value )
	{
		key ^= value;
		key *= 1099511628211ull;
	};
	mix( pixel );
	// linkageHash summarizes linked[0..linkedCount); translation still reads the array.
	if ( linkedCount )
		mix( linkageHash );
	if ( ActivateTranslatedVariant( record, key ) )
		return true;
	return TranslateVariant( record, pixel, layout, linked, linkedCount, raster, key, signer );
}

//-----------------------------------------------------------------------------
// Purpose: Translates a legacy record for one variant key and makes it active, parking the previous variant.
//          Out of line: translation locals would otherwise enlarge every caller's stack frame (DrawBuffers is per draw).
//-----------------------------------------------------------------------------
static __declspec( noinline ) bool TranslateVariant( ShaderRecordDX12 *record, bool pixel, const VertexLayoutDX12 &layout, const ShaderLinkageDX12 *linked, size_t linkedCount, const ShaderRasterStateDX12 &raster, uint64_t key, SignDxbcFnDX12 signer )
{
	ZoneNamedN( translationMiss, "DX12 TranslateMiss", DX12_ZONES_ACTIVE );
	ShaderVertexInputDX12 inputs[MAX_VERTEX_INPUTS_DX12] = {};
	for ( uint32_t i = 0; i < layout.inputCount; ++i )
	{
		const char *s = layout.inputs[i].semantic;
		uint32_t usage = 0, index = layout.inputs[i].semanticIndex;
		if ( !V_strcmp( s, "POSITION" ) )
			usage = 0;
		else if ( !V_strcmp( s, "BLENDWEIGHT" ) )
			usage = 1;
		else if ( !V_strcmp( s, "BLENDINDICES" ) )
			usage = 2;
		else if ( !V_strcmp( s, "NORMAL" ) )
			usage = 3;
		else if ( !V_strcmp( s, "PSIZE" ) )
			usage = 4;
		else if ( !V_strcmp( s, "TEXCOORD" ) )
			usage = 5;
		else if ( !V_strcmp( s, "TANGENT" ) )
			usage = 6;
		else if ( !V_strcmp( s, "BINORMAL" ) )
			usage = 7;
		else if ( !V_strcmp( s, "COLOR" ) )
			usage = 10;
		const bool convert = layout.inputs[i].integerToFloat;
		inputs[i] = { usage, index, i, convert ? 1u : 0u, convert ? ( layout.inputs[i].format == DXGI_FORMAT_R16G16_SINT ? 2u : 1u ) : 3u, false };
	}
	ShaderTranslationRequestDX12 request{};
	request.legacyBytes = record->legacyBytecode.Base();
	request.byteCount = record->legacyBytecode.Count();
	request.pixel = pixel;
	request.vertexInputs = inputs;
	request.vertexInputCount = layout.inputCount;
	request.linkedOutputs = linked;
	request.linkedOutputCount = linkedCount;
	request.raster = raster;
	request.centroidTexcoordMask = record->centroidTexcoordMask;
	CShaderTranslatorDX12 translator;
	ShaderTranslationResultDX12 translated;
	CUtlString error;
	if ( !translator.TranslateLegacy( request, signer, translated, error ) )
	{
		Warning( "ShaderAPIDX12: deferred shader translation failed: %s\n", error.Get() );
		return false;
	}
	if ( record->activeVariantValid )
	{
		ShaderRecordDX12::Variant &variant = record->variants[record->variants.AddToTail()];
		variant.key = record->activeVariantKey;
		variant.result.Swap( record->translated );
		variant.inputSignature.Swap( record->inputSignature );
		variant.inputSignatureReady = record->inputSignatureReady;
		variant.derived.Swap( record->derived );
	}
	record->translated.Swap( translated );
	record->activeVariantKey = key;
	record->activeVariantValid = true;
	record->inputSignatureReady = false;
	record->inputSignature.RemoveAll();
	record->derived = {};
	record->linkageHashValid = false;
	record->constantLayoutValid = false;
	return !record->translated.bytecode.IsEmpty();
}

struct CShaderAPIDX12::OcclusionQueryDX12
{
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
	Microsoft::WRL::ComPtr<ID3D12Resource> readback;
	uint64_t fence = 0;
	bool active = false, ended = false, error = false, destroyed = false;
};

//-----------------------------------------------------------------------------
// Purpose: Creates a one-slot occlusion query heap with its readback buffer; nullptr on failure
//-----------------------------------------------------------------------------
CShaderAPIDX12::OcclusionQueryDX12 *CShaderAPIDX12::CreateOcclusionQuery()
{
	if ( !m_pDevice || !m_pDevice->NativeDevice() )
		return nullptr;
	OcclusionQueryDX12 *pQuery = new OcclusionQueryDX12;
	D3D12_QUERY_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
	heapDesc.Count = 1;
	if ( FAILED( m_pDevice->NativeDevice()->CreateQueryHeap( &heapDesc, IID_PPV_ARGS( &pQuery->heap ) ) ) )
	{
		delete pQuery;
		return nullptr;
	}
	D3D12_HEAP_PROPERTIES properties{};
	properties.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = sizeof( uint64_t );
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if ( FAILED( m_pDevice->NativeDevice()->CreateCommittedResource( &properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &pQuery->readback ) ) ) )
	{
		delete pQuery;
		return nullptr;
	}
	return pQuery;
}

namespace shaderapidx12
{
CShaderAPIDX12 *g_pShaderAPIDX12 = nullptr;

//-----------------------------------------------------------------------------
// Purpose: Snapshot shader names are interned so snapshot copies and comparisons avoid string allocation.
//          The pool only grows; returned pointers stay valid for the module lifetime.
//-----------------------------------------------------------------------------
InternedNameDX12 InternShaderNameDX12( const char *pszName )
{
	if ( !pszName || !*pszName )
		return {};
	static CUtlSymbolTableMT s_Table( 0, 64, false );
	return InternedNameDX12{ s_Table.String( s_Table.AddString( pszName ) ) };
}

// The installed x64 material runtime implements these virtuals after the public
// IMaterial prefix. DrawMesh is slot 71 for both CMaterial and CMaterialSubRect;
// no engine function addresses or exported interface headers are changed.
class IMaterialDrawDX12 : public IMaterial
{
public:
	virtual int GetReferenceCount() const = 0;
	virtual void SetEnumerationID( int ) = 0;
	virtual void SetNeedsWhiteLightmap( bool ) = 0;
	virtual bool GetNeedsWhiteLightmap() const = 0;
	virtual void Uncache( bool ) = 0;
	virtual void Precache() = 0;
	virtual bool PrecacheVars( KeyValues *, KeyValues *, void *, int ) = 0;
	virtual void ReloadTextures() = 0;
	virtual void SetMinLightmapPageID( int ) = 0;
	virtual void SetMaxLightmapPageID( int ) = 0;
	virtual int GetMinLightmapPageID() const = 0;
	virtual int GetMaxLightmapPageID() const = 0;
	virtual void *GetShader() const = 0;
	virtual bool IsPrecachedVars() const = 0;
	virtual void DrawMesh( VertexCompressionType_t ) = 0;
};

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CShaderAPIDX12::CShaderAPIDX12()
{
	for ( VMatrix &m : m_Matrices )
		m.Identity();
	ResetNativeState();
	g_pShaderAPIDX12 = this;
}

//-----------------------------------------------------------------------------
// Purpose: Destructor; releases device resources and every owned record
//-----------------------------------------------------------------------------
CShaderAPIDX12::~CShaderAPIDX12()
{
	ShutdownDeviceResources();
	m_DynamicMeshes.PurgeAndDeleteElements();
	delete m_pFlexMesh;
	m_pFlexMesh = nullptr;
	if ( g_pShaderAPIDX12 == this )
		g_pShaderAPIDX12 = nullptr;
	FOR_EACH_HASHTABLE( m_Textures, entry )
	{
		delete m_Textures[entry];
	}
	FOR_EACH_HASHTABLE( m_NamedShaderCombos, entry )
	{
		delete m_NamedShaderCombos[entry];
	}
	FOR_EACH_HASHTABLE( m_NamedShaderFiles, entry )
	{
		delete m_NamedShaderFiles[entry];
	}
	for ( uint32_t entry = m_FixedShaders.FirstInorder(); entry != m_FixedShaders.InvalidIndex(); entry = m_FixedShaders.NextInorder( entry ) )
	{
		delete m_FixedShaders[entry];
	}
	if ( m_pDebugTextureEntries )
		m_pDebugTextureEntries->deleteThis();
}

//-----------------------------------------------------------------------------
// Purpose: Loads (once per file) and creates the record of one named VCS shader combo
//-----------------------------------------------------------------------------
ShaderRecordDX12 *CShaderAPIDX12::ResolveNamedShader( const char *pszName, bool bPixel, int nStaticIndex, int nDynamicIndex )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 ResolveNamedShader", DX12_DRAW_ZONES_ACTIVE );
	if ( !g_pShaderDeviceMgrDX12 || !g_pShaderDeviceMgrDX12->HostFileSystem() || !*pszName )
		return nullptr;
	const NamedShaderKeyView referenceKey{ pszName, nStaticIndex, -1, bPixel };
	UtlHashHandle_t &hReference = m_NamedReferenceHints[bPixel ? 1 : 0];
	if ( !m_NamedShaderReferences.IsValidHandle( hReference ) || !NamedShaderEqual()( m_NamedShaderReferences.Key( hReference ), referenceKey ) )
		hReference = m_NamedShaderReferences.Insert( referenceKey, true );
	m_NamedShaderReferences[hReference] = true;
	const int nDynamic = MAX( 0, nDynamicIndex );
	const NamedShaderKeyView key{ pszName, nStaticIndex, nDynamic, bPixel };
	UtlHashHandle_t &hFound = m_NamedComboHints[bPixel ? 1 : 0];
	if ( !m_NamedShaderCombos.IsValidHandle( hFound ) || !NamedShaderEqual()( m_NamedShaderCombos.Key( hFound ), key ) )
		hFound = m_NamedShaderCombos.Find( key );
	if ( hFound != m_NamedShaderCombos.InvalidHandle() )
		return m_NamedShaderCombos[hFound];
	hFound = m_NamedShaderCombos.Insert( key, nullptr );
	CUtlString fileKey( bPixel ? "p:" : "v:" );
	fileKey.Append( pszName );
	UtlHashHandle_t hShaderFile = m_NamedShaderFiles.Find( fileKey.String() );
	if ( hShaderFile == m_NamedShaderFiles.InvalidHandle() )
	{
		ShaderVcsFile *pNewFile = new ShaderVcsFile;
		CUtlString error;
		if ( !pNewFile->Open( *g_pShaderDeviceMgrDX12->HostFileSystem(), pszName, bPixel ? VcsStage::Pixel : VcsStage::Vertex, error ) )
		{
			Warning( "ShaderAPIDX12: unable to load named shader %s: %s\n", pszName, error.Get() );
			delete pNewFile;
			m_NamedShaderFiles.Insert( fileKey.String(), nullptr );
			return nullptr;
		}
		// -dx12shaderlog: one line per named shader file, stating which VCS path won (shaders/vsh|psh native DXBC or
		// shaders/fxc legacy). Development diagnostic; files open once, so this never runs per draw.
		static const bool s_bLogNamedShaders = CommandLine() && CommandLine()->CheckParm( "-dx12shaderlog" );
		if ( s_bLogNamedShaders )
			Msg( "ShaderAPIDX12: %s shader %s from %s\n", bPixel ? "pixel" : "vertex", pszName, pNewFile->Path() );
		hShaderFile = m_NamedShaderFiles.Insert( fileKey.String(), pNewFile );
	}
	ShaderVcsFile *pFile = m_NamedShaderFiles[hShaderFile];
	if ( !pFile )
		return nullptr;
	if ( nStaticIndex < 0 || static_cast<uint32_t>( nDynamic ) >= pFile->DynamicComboCount() )
	{
		Warning( "ShaderAPIDX12: shader %s has invalid static %d / dynamic %d combo\n", pszName, nStaticIndex, nDynamic );
		return nullptr;
	}
	CUtlString error;
	if ( !pFile->LoadStaticCombo( static_cast<uint32_t>( nStaticIndex ), error ) )
	{
		Warning( "ShaderAPIDX12: shader %s combo decode failed: %s\n", pszName, error.Get() );
		return nullptr;
	}
	const VcsPayload *pPayload = pFile->DynamicPayload( static_cast<uint32_t>( nStaticIndex ), static_cast<uint32_t>( nDynamic ) );
	if ( !pPayload )
	{
		Warning( "ShaderAPIDX12: shader %s missing static %d / dynamic %d combo\n", pszName, nStaticIndex, nDynamic );
		return nullptr;
	}

	class ShaderBufferView final : public IShaderBuffer
	{
	public:
		explicit ShaderBufferView( const VcsPayload &payload )
		    : m_Payload( payload ) {}

		size_t GetSize() const override { return m_Payload.tokens.Count(); }

		const void *GetBits() const override { return m_Payload.tokens.Base(); }

		void Release() override { Assert( 0 ); }

	private:
		const VcsPayload &m_Payload;
	} buffer( *pPayload );

	ShaderRecordDX12 *pRecord = bPixel ? reinterpret_cast<ShaderRecordDX12 *>( m_pDevice->CreatePixelShader( &buffer ) ) : reinterpret_cast<ShaderRecordDX12 *>( m_pDevice->CreateVertexShader( &buffer ) );
	if ( !pRecord )
	{
		Warning( "ShaderAPIDX12: failed to create %s shader %s combo %d/%d\n", bPixel ? "pixel" : "vertex", pszName, nStaticIndex, nDynamic );
		return nullptr;
	}
	pRecord->centroidTexcoordMask = pFile->CentroidMask();
	hFound = m_NamedShaderCombos.Find( key );
	m_NamedShaderCombos[hFound] = pRecord;
	return pRecord;
}

//-----------------------------------------------------------------------------
// Purpose: ResolveNamedShader for the active snapshot, memoized per snapshot/dynamic index
//-----------------------------------------------------------------------------
ShaderRecordDX12 *CShaderAPIDX12::ResolveActiveNamedShader( bool bPixel, int nDynamicIndex )
{
	const char *pszName = ( bPixel ? m_ActiveSnapshot.pixelShaderName : m_ActiveSnapshot.vertexShaderName ).c_str();
	const int nStaticIndex = bPixel ? m_ActiveSnapshot.staticPixelIndex : m_ActiveSnapshot.staticVertexIndex;
	if ( m_hActiveSnapshotId < 0 )
		return ResolveNamedShader( pszName, bPixel, nStaticIndex, nDynamicIndex );
	NamedResolveEntryDX12( &cache )[1024] = m_NamedResolveCache[bPixel ? 1 : 0];
	NamedResolveEntryDX12 &entry = cache[Mix32HashFunctor()( static_cast<uint32_t>( m_hActiveSnapshotId ) * 0x9E3779B1u ^ static_cast<uint32_t>( nDynamicIndex ) ) & ( ARRAYSIZE( cache ) - 1 )];
	if ( entry.record && entry.epoch == m_nNamedResolveEpoch && entry.snapshot == m_hActiveSnapshotId && entry.dynamicIndex == nDynamicIndex )
	{
		if ( entry.referenceEpoch == m_nNamedReferenceEpoch )
			return entry.record;
		// Reference marks were cleared: re-mark through the remembered handle (hints can move on rehash, so verify the key).
		const NamedShaderKeyView referenceKey{ pszName, nStaticIndex, -1, bPixel };
		if ( m_NamedShaderReferences.IsValidHandle( entry.reference ) && NamedShaderEqual()( m_NamedShaderReferences.Key( entry.reference ), referenceKey ) )
		{
			m_NamedShaderReferences[entry.reference] = true;
			entry.referenceEpoch = m_nNamedReferenceEpoch;
			return entry.record;
		}
	}
	ShaderRecordDX12 *pRecord = ResolveNamedShader( pszName, bPixel, nStaticIndex, nDynamicIndex );
	// Failures are not cached: they may depend on device or filesystem availability.
	entry = { m_hActiveSnapshotId, nDynamicIndex, m_nNamedResolveEpoch, pRecord, m_NamedReferenceHints[bPixel ? 1 : 0], m_nNamedReferenceEpoch };
	return pRecord;
}

//-----------------------------------------------------------------------------
// Purpose: Draws a mesh through the bound material's shader (DrawMesh dispatch), or directly without a material
//-----------------------------------------------------------------------------
void CShaderAPIDX12::DrawMaterialMesh( CMeshDX12 *pMesh, int nFirstIndex, int nIndexCount )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 MaterialDraw", DX12_DRAW_ZONES_ACTIVE );
	{
		ZoneNamedN( materialSync, "DX12 MaterialSyncMatrices", DX12_DRAW_ZONES_ACTIVE );
		if ( m_pShaderUtil )
			m_pShaderUtil->SyncMatrices();
	}
	{
		ZoneNamedN( materialTransforms, "DX12 MaterialTransforms", DX12_DRAW_ZONES_ACTIVE );
		CommitTransforms();
	}
	{
		ZoneNamedN( materialLighting, "DX12 MaterialLighting", DX12_DRAW_ZONES_ACTIVE );
		CommitVertexLighting();
	}
	if ( !pMesh || !m_pBoundMaterial )
	{
		DrawMesh( pMesh, nFirstIndex, nIndexCount );
		return;
	}
	if ( m_pShaderUtil && m_pShaderUtil->GetConfig().m_bSuppressRendering )
		return;
	CMeshDX12 *pPreviousMesh = m_pRenderMesh;
	const int nPreviousFirst = m_nRenderFirstIndex, nPreviousCount = m_nRenderIndexCount;
	m_pRenderMesh = pMesh;
	m_nRenderFirstIndex = nFirstIndex;
	m_nRenderIndexCount = nIndexCount;
	{
		ZoneNamedN( materialDispatch, "DX12 MaterialShaderDispatch", DX12_DRAW_ZONES_ACTIVE );
		reinterpret_cast<IMaterialDrawDX12 *>( m_pBoundMaterial )->DrawMesh( CompressionType( pMesh->GetVertexFormat() ) );
	}
	m_pRenderMesh = pPreviousMesh;
	m_nRenderFirstIndex = nPreviousFirst;
	m_nRenderIndexCount = nPreviousCount;
}

//-----------------------------------------------------------------------------
// Purpose: Draws a mesh with its color and flex streams bound to slots 1 and 2
//-----------------------------------------------------------------------------
void CShaderAPIDX12::DrawMesh( CMeshDX12 *pMesh, int nFirstIndex, int nIndexCount )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 DrawMesh", DX12_DRAW_ZONES_ACTIVE );
	if ( !pMesh )
		return;
	// Mesh draws use streams 0-2 only; slots 3-15 of the persistent array stay empty.
	VertexBindingDX12( &bindings )[16] = m_DrawMeshBindings;
	bindings[0] = { &pMesh->DrawVertices(), 0, 0, static_cast<uint32_t>( pMesh->DrawVertices().WrittenCount() ), 1, pMesh->GetVertexFormat() };
	const auto auxiliary = [&]( IMesh *pSource, int nOffset, unsigned nSlot ) -> bool
	{
		if ( !pSource )
		{
			bindings[nSlot] = {};
			return true;
		}
		CVertexBufferDX12 &vertices = static_cast<CMeshDX12 *>( pSource )->Vertices();
		const size_t nBytes = static_cast<size_t>( vertices.WrittenCount() ) * vertices.Stride();
		if ( nOffset < 0 || !vertices.Stride() || static_cast<size_t>( nOffset ) > nBytes )
			return false;
		bindings[nSlot] = { &vertices, static_cast<uint32_t>( nOffset ), 0, static_cast<uint32_t>( ( nBytes - nOffset ) / vertices.Stride() ), 1, vertices.GetVertexFormat() };
		return true;
	};
	if ( !auxiliary( pMesh->ColorMesh(), pMesh->ColorOffset(), 1 ) || !auxiliary( pMesh->FlexMesh(), pMesh->FlexOffset(), 2 ) )
		return;
	DrawBuffers( bindings, &pMesh->DrawIndices(), 0, pMesh->PrimitiveType(), nFirstIndex, nIndexCount, true );
}

//-----------------------------------------------------------------------------
// Purpose: Range check without division: first+count elements fit in n bytes iff (first+count)*size <= n.
//          Counts are bounded by the (<=UINT_MAX) byte extents before multiplying, so the products cannot overflow.
//-----------------------------------------------------------------------------
static inline bool RangeFitsDX12( uint64_t nFirst, uint64_t nCount, uint64_t nSize, uint64_t nAvailable )
{
	const uint64_t nEnd = nFirst + nCount;
	return nEnd <= nAvailable && nEnd * nSize <= nAvailable;
}

//-----------------------------------------------------------------------------
// Purpose: Reflects a record's native cbuffers and input signature once per active variant; null passes
//-----------------------------------------------------------------------------
static bool ReflectRecordInputsDX12( ShaderRecordDX12 *pRecord )
{
	if ( !pRecord )
		return true;
	if ( !ReflectNativeCBuffersDX12( pRecord ) )
		return false;
	if ( pRecord->inputSignatureReady )
		return true;
	const D3D12_SHADER_BYTECODE bytecode = pRecord->Bytecode();
	const bool bNative = pRecord->legacyBytecode.IsEmpty();
	if ( !ReadShaderInputSignatureDX12( bytecode.pShaderBytecode, bytecode.BytecodeLength, pRecord->inputSignature,
	         bNative ? &pRecord->nativeConstantRegisters : nullptr, bNative && !pRecord->stagePixel ? &pRecord->translated.outputLinkage : nullptr ) )
		return false;
	pRecord->inputSignatureReady = true;
	pRecord->linkageHashValid = false;
	pRecord->constantLayoutValid = false;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Validates the stream/index ranges, resolves translation, input layout, PSO, bindings and
//          constants for the current state and records one draw
//-----------------------------------------------------------------------------
void CShaderAPIDX12::DrawBuffers( const VertexBindingDX12 ( &bindings )[16], CIndexBufferDX12 *indices,
    size_t indexOffset, MaterialPrimitiveType_t primitive, int firstIndex, int indexCount, bool meshStreams )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 DrawBuffers", DX12_DRAW_ZONES_ACTIVE );
	if ( !m_pDevice || m_bDisallowAccess || !m_pDevice->CommandList() || !m_pDevice->NativeDevice() || !bindings[0].buffer || firstIndex < 0 || indexCount <= 0 )
		return;
	if ( m_MotionPassState == MotionPassStateDX12::Suppressed )
		return;
	++m_nFrameDrawCount;
	++m_DrawStats.draws;
	const bool indexed = primitive != MATERIAL_POINTS;
	const size_t indexBytes = indices ? static_cast<size_t>( indices->WrittenCount() ) * indices->IndexSize() : 0;
	if ( indexed && ( !indices || indexOffset > indexBytes || ( indexOffset & ( indices->IndexSize() - 1 ) ) || indexBytes - indexOffset > UINT_MAX || !RangeFitsDX12( static_cast<uint32_t>( firstIndex ), static_cast<uint32_t>( indexCount ), indices->IndexSize(), indexBytes - indexOffset ) ) )
		return;
	for ( const VertexBindingDX12 &binding : bindings )
		if ( binding.buffer )
		{
			const size_t stride = binding.buffer->Stride(), bytes = static_cast<size_t>( binding.buffer->WrittenCount() ) * stride;
			if ( !stride || !binding.vertexCount || binding.byteOffset > bytes || bytes > UINT_MAX || !RangeFitsDX12( binding.firstVertex, binding.vertexCount, stride, bytes - binding.byteOffset ) )
				return;
		}
	if ( !indexed && ( static_cast<uint32_t>( firstIndex ) > bindings[0].firstVertex + bindings[0].vertexCount || static_cast<uint32_t>( indexCount ) > bindings[0].firstVertex + bindings[0].vertexCount - firstIndex ) )
		return;
	if ( m_Selection.Enabled() )
	{
		VMatrix modelView, modelToClip;
		MatrixMultiply( m_Matrices[MATERIAL_VIEW], m_Matrices[MATERIAL_MODEL], modelView );
		MatrixMultiply( m_Matrices[MATERIAL_PROJECTION], modelView, modelToClip );
		if ( indices )
			TestSelectionDX12( *bindings[0].buffer, *indices, primitive, firstIndex, indexCount, modelToClip,
			    m_ActiveSnapshot.culling && ( !m_bRasterOverride || m_RasterState.m_bCullEnable ), m_CullMode == MATERIAL_CULLMODE_CW, m_Selection, bindings[0].byteOffset, indexOffset );
		return;
	}
	{
		ZoneNamedN( drawCommit, "DX12 DrawCommit", DX12_DRAW_ZONES_ACTIVE );
		ProcessPendingTextureDeletes();
		CommitTransforms();
		CommitFogState();
		CommitVertexLighting();
	}
	if ( m_bNamedVertexShaderDirty )
	{
		ShaderRecordDX12 *record = ResolveActiveNamedShader( false, m_nVertexShaderIndex );
		m_hBoundVS = reinterpret_cast<VertexShaderHandle_t>( record );
		m_bBoundVertexShaderIsNamed = record != nullptr;
		m_bNamedVertexShaderDirty = false;
	}
	if ( m_bNamedPixelShaderDirty )
	{
		ShaderRecordDX12 *record = ResolveActiveNamedShader( true, m_nPixelShaderIndex );
		m_hBoundPS = reinterpret_cast<PixelShaderHandle_t>( record );
		m_bBoundPixelShaderIsNamed = record != nullptr;
		m_bNamedPixelShaderDirty = false;
	}
	VertexFormat_t format = bindings[0].format;
	const bool motionActive = MotionPassActive();
	VertexLayoutDX12 explicitLayout;
	// Mesh layouts depend only on (format, stream flags); a small direct-mapped cache covers alternating formats.
	// The stream-2 declaration follows the flex mesh's own format (28-byte position/wrinkle/normal from GetFlexMesh).
	const bool flexWrinkle = bindings[2].buffer && ( bindings[2].format & VERTEX_WRINKLE );
	const uint8_t meshLayoutFlags = static_cast<uint8_t>( ( bindings[1].buffer ? 1 : 0 ) | ( bindings[2].buffer ? 2 : 0 ) | ( flexWrinkle ? 4 : 0 ) );
	SourceLayoutEntryDX12 &meshLayout = m_SourceLayouts[( static_cast<uint32_t>( format ) ^ static_cast<uint32_t>( format >> 29 ) ^ static_cast<uint32_t>( format >> 41 ) ^ meshLayoutFlags * 0x9E3779B1u ) % ARRAYSIZE( m_SourceLayouts )];
	VertexLayoutDX12 &sourceLayout = meshStreams ? meshLayout.layout : explicitLayout;
	{
		ZoneNamedN( sourceLayoutSetup, "DX12 SourceLayout", DX12_DRAW_ZONES_ACTIVE );
		if ( meshStreams )
		{
			if ( !meshLayout.valid || meshLayout.format != format || meshLayout.flags != meshLayoutFlags )
			{
				const VertexInputStreamsDX12 streams{ bindings[1].buffer != nullptr, bindings[2].buffer != nullptr, false, flexWrinkle };
				sourceLayout = ComputeVertexLayoutDX12( format, nullptr, nullptr, streams );
				meshLayout.format = format;
				meshLayout.flags = meshLayoutFlags;
				meshLayout.translationKey = sourceLayout.valid ? TranslationLayoutKey( sourceLayout ) : 0;
				meshLayout.valid = true;
			}
			m_nSourceLayoutTranslationKey = meshLayout.translationKey;
		}
		else
		{
			sourceLayout = {};
			sourceLayout.valid = true;
			sourceLayout.stride = bindings[0].buffer->Stride();
			for ( unsigned slot = 0; slot < ARRAYSIZE( bindings ); ++slot )
				if ( bindings[slot].buffer )
				{
					const VertexLayoutDX12 &physical = bindings[slot].buffer->Layout();
					VertexLayoutDX12 subset;
					const VertexLayoutDX12 *usage = &physical;
					if ( bindings[slot].format != bindings[slot].buffer->GetVertexFormat() )
					{
						subset = ComputeVertexLayoutDX12( bindings[slot].format );
						usage = &subset;
					}
					if ( !physical.valid || !usage->valid )
						return;
					format |= bindings[slot].format;
					for ( uint32_t i = 0; i < usage->inputCount; ++i )
					{
						const VertexInputDX12 *element = nullptr;
						for ( uint32_t j = 0; j < physical.inputCount; ++j )
							if ( usage->inputs[i].semanticIndex == physical.inputs[j].semanticIndex && !V_strcmp( usage->inputs[i].semantic, physical.inputs[j].semantic ) )
							{
								element = &physical.inputs[j];
								break;
							}
						if ( !element || sourceLayout.inputCount == MAX_VERTEX_INPUTS_DX12 )
							return;
						for ( uint32_t j = 0; j < sourceLayout.inputCount; ++j )
							if ( sourceLayout.inputs[j].semanticIndex == element->semanticIndex && !V_strcmp( sourceLayout.inputs[j].semantic, element->semantic ) )
							{
								Warning( "ShaderAPIDX12: duplicate explicit vertex semantic %s%u\n", element->semantic, element->semanticIndex );
								return;
							}
						sourceLayout.inputs[sourceLayout.inputCount] = *element;
						sourceLayout.inputs[sourceLayout.inputCount++].inputSlot = slot;
					}
				}
		}
	}
	if ( !sourceLayout.valid )
		return;
	ShaderRecordDX12 *vsRecord = motionActive ? MotionVertexShader( format ) : ( m_hBoundVS == VERTEX_SHADER_HANDLE_INVALID ? nullptr : reinterpret_cast<ShaderRecordDX12 *>( m_hBoundVS ) );
	ShaderRecordDX12 *psRecord = motionActive ? m_pMotionPS : ( m_hBoundPS == PIXEL_SHADER_HANDLE_INVALID ? nullptr : reinterpret_cast<ShaderRecordDX12 *>( m_hBoundPS ) );
	// Until legacy analysis exists, retain the complete binding path. Native/fixed-function
	// shaders and explicit geometry shaders also retain it; their resource use is not in this metadata.
	const auto samplerMask = [&]( const ShaderRecordDX12 *record, uint32_t all )
	{
		return m_hBoundGS == GEOMETRY_SHADER_HANDLE_INVALID && record && !record->legacyBytecode.IsEmpty() && record->activeVariantValid ? record->translated.usedSamplerMask & all : all;
	};
	const uint32_t pixelSamplers = samplerMask( psRecord, 0xffff ), vertexSamplers = samplerMask( vsRecord, 0xf );
	const uint32_t sampledMask = pixelSamplers | ( vertexSamplers << 16 );
	if ( m_nPreparedSamplerMask != sampledMask )
	{
		m_PreparedSamplerTable.count = 0;
		m_nPreparedSamplerMask = sampledMask;
	}
	// Persistent binding input: only slots sampled by the previous draw but not this one need resetting.
	CPipelineCacheDX12::BindingInputDX12 &bindingInput = m_DrawBindingInput;
	if ( m_DrawBindingNull.ptr != m_Pipeline.NullShaderResourceView().ptr )
	{
		bindingInput = CPipelineCacheDX12::BindingInputDX12( m_Pipeline.NullShaderResourceView() );
		for ( size_t i = 0; i < ARRAYSIZE( bindingInput.samplerDescs ); ++i )
			bindingInput.samplerDescs[i] = CPipelineCacheDX12::DefaultSamplerDesc();
		m_DrawBindingNull = m_Pipeline.NullShaderResourceView();
		m_nDrawBindingMask = 0;
	}
	const auto clearSlot = [&]( size_t slot )
	{
		bindingInput.textures[slot] = nullptr;
		bindingInput.srvSources[slot] = m_DrawBindingNull;
		bindingInput.samplerDescs[slot] = CPipelineCacheDX12::DefaultSamplerDesc();
		bindingInput.samplerIds[slot] = 0;
	};
	for ( uint32_t stale = m_nDrawBindingMask & ~sampledMask; stale; stale &= stale - 1 )
	{
		unsigned long bit = 0;
		_BitScanForward( &bit, stale );
		clearSlot( bit );
	}
	m_nDrawBindingMask = sampledMask;
	// Every constant bank field is assigned below: shader banks by the constants lambda, extension banks after it.
	// Reserve a complete sampler table before recording any transient GPU addresses.
	// A flush after geometry/constant uploads could recycle addresses in this draw.
	const auto prepareTextures = [&]
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 PrepareTextures", DX12_DRAW_ZONES_ACTIVE );
		const auto prepareSlot = [&]( size_t slot, ShaderAPITextureHandle_t handle, bool srgb, bool comparison )
		{
			PreparedTextureSlot &cached = m_PreparedTextureSlots[slot];
			// No texture changed sampled state, sampler parameters, resource or lifetime since this slot was prepared.
			if ( cached.valid && cached.handle == handle && cached.srgb == srgb && cached.comparison == comparison && cached.epoch == m_nTextureStateEpoch )
			{
				bindingInput.textures[slot] = cached.resource;
				bindingInput.samplerDescs[slot] = cached.sampler;
				bindingInput.samplerIds[slot] = cached.samplerId;
				bindingInput.srvSources[slot] = cached.source;
				if ( !cached.source.ptr )
					bindingInput.srvDescs[slot] = cached.srv;
				return;
			}
			TextureRecord *record = cached.valid && cached.record && cached.handle == handle ? cached.record : ( handle > 0 ? FindTexture( handle ) : nullptr );
			const bool reusable = cached.valid && cached.handle == handle && cached.record == record && cached.srgb == srgb && cached.comparison == comparison && ( !record || ( record->sampledStateValid && record->resource.Get() == cached.resource && record->m_SamplerDescriptorValid[comparison ? 1 : 0] ) );
			if ( !reusable )
			{
				ID3D12Resource *resource = nullptr;
				D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
				D3D12_SAMPLER_DESC sampler{};
				D3D12_CPU_DESCRIPTOR_HANDLE source{};
				if ( !PrepareSampledTexture( handle, srgb, &resource, srv, sampler, &source, comparison ) )
				{
					cached.valid = false;
					m_PreparedSamplerTable.count = 0;
					clearSlot( slot );
					return;
				}
				if ( sampler.AddressU == 0 )
					sampler = CPipelineCacheDX12::DefaultSamplerDesc();
				// Interned ids are process-stable, so an unchanged description keeps its id without a lookup.
				if ( !cached.valid || memcmp( &cached.sampler, &sampler, sizeof( sampler ) ) )
				{
					m_PreparedSamplerTable.count = 0;
					cached.samplerId = m_Pipeline.InternSampler( sampler );
				}
				cached.handle = handle;
				cached.record = record;
				cached.resource = resource;
				cached.srv = srv;
				cached.sampler = sampler;
				cached.source = source;
				cached.srgb = srgb;
				cached.comparison = comparison;
				cached.valid = true;
			}
			cached.epoch = m_nTextureStateEpoch;
			bindingInput.textures[slot] = cached.resource;
			bindingInput.samplerDescs[slot] = cached.sampler;
			bindingInput.samplerIds[slot] = cached.samplerId;
			bindingInput.srvSources[slot] = cached.source;
			if ( !cached.source.ptr )
				bindingInput.srvDescs[slot] = cached.srv;
		};
		for ( size_t i = 0; i < ARRAYSIZE( m_BoundTextures ); ++i )
			if ( pixelSamplers & ( 1u << i ) )
				prepareSlot( i, m_BoundTextures[i], ( m_ActiveSnapshot.srgbReadMask & ( 1u << i ) ) != 0, ( m_ActiveSnapshot.comparisonSamplerMask & ( 1u << i ) ) != 0 );
		for ( size_t i = 0; i < ARRAYSIZE( m_VertexTextures ); ++i )
			if ( vertexSamplers & ( 1u << i ) )
				prepareSlot( 16 + i, m_VertexTextures[i], false, false );
	};
	// Whole-set reuse: identical bound textures, sampled masks and texture state since the previous draw in this
	// recording mean every slot would take its prepared fast path and the sampler table is still reserved.
	// Field-wise compare against the previous prepared set; the key is written only when the set is re-prepared.
	const uint64_t textureFence = m_pDevice->NextFenceValue();
	const TextureSetKeyDX12 &lastSet = m_LastTextureSet;
	bool textureSetReused = m_bTextureSetValid && m_PreparedSamplerTable.count == 32 && m_nPreparedSamplerFence == textureFence && lastSet.fence == textureFence && lastSet.epoch == m_nTextureStateEpoch &&
	    lastSet.sampledMask == sampledMask && lastSet.srgbMask == m_ActiveSnapshot.srgbReadMask && lastSet.comparisonMask == m_ActiveSnapshot.comparisonSamplerMask && lastSet.nullView == m_DrawBindingNull.ptr &&
	    !memcmp( lastSet.pixel, m_BoundTextures, sizeof( m_BoundTextures ) ) && !memcmp( lastSet.vertex, m_VertexTextures, sizeof( m_VertexTextures ) );
	if ( !textureSetReused )
		prepareTextures();
	bindingInput.texturesUnchanged = textureSetReused;
	// Completed-fence reclamation runs at submission/frame boundaries; a failed sampler reservation
	// first retries after reclaiming, and only then forces a GPU wait.
	bindingInput.samplerTable = m_PreparedSamplerTable.count == 32 && m_nPreparedSamplerFence == m_pDevice->NextFenceValue() ? m_PreparedSamplerTable : m_Pipeline.PrepareSamplerTable( bindingInput.samplerDescs, bindingInput.samplerIds, m_pDevice->NextFenceValue() );
	if ( bindingInput.samplerTable.count != 32 )
	{
		m_Pipeline.Reclaim( m_pDevice->CompletedFenceValue() );
		bindingInput.samplerTable = m_Pipeline.PrepareSamplerTable( bindingInput.samplerDescs, bindingInput.samplerIds, m_pDevice->NextFenceValue() );
	}
	if ( bindingInput.samplerTable.count != 32 )
	{
		if ( !m_pDevice->Submit( true ) )
			return;
		m_Pipeline.Reclaim( m_pDevice->CompletedFenceValue() );
		prepareTextures();
		bindingInput.samplerTable = m_Pipeline.PrepareSamplerTable( bindingInput.samplerDescs, bindingInput.samplerIds, m_pDevice->NextFenceValue() );
		if ( bindingInput.samplerTable.count != 32 )
		{
			Warning( "ShaderAPIDX12: sampler descriptors unavailable after completion\n" );
			return;
		}
	}
	// Reuse only within this recording fence: the cache reservation already protects its lifetime.
	m_PreparedSamplerTable = bindingInput.samplerTable;
	m_nPreparedSamplerFence = m_pDevice->NextFenceValue();
	// Recompute the key: the retry path may have submitted (new fence) and re-prepared slots.
	if ( !textureSetReused )
	{
		TextureSetKeyDX12 &key = m_LastTextureSet;
		memcpy( key.pixel, m_BoundTextures, sizeof( key.pixel ) );
		memcpy( key.vertex, m_VertexTextures, sizeof( key.vertex ) );
		key.sampledMask = sampledMask;
		key.srgbMask = m_ActiveSnapshot.srgbReadMask;
		key.comparisonMask = m_ActiveSnapshot.comparisonSamplerMask;
		key.nullView = m_DrawBindingNull.ptr;
	}
	m_LastTextureSet.fence = m_pDevice->NextFenceValue();
	m_LastTextureSet.epoch = m_nTextureStateEpoch;
	m_bTextureSetValid = true;
	bindingInput.retireFence = m_pDevice->NextFenceValue();
	const uint64_t retireFence = bindingInput.retireFence;
	CCommandRecorderDX12 *list = m_pDevice->CommandList();
	bool pipelineBound = false;
	RenderTargetBindingDX12 target; // PrepareRenderTargets assigns it before any use
	{
		ZoneNamedN( drawTargets, "DX12 DrawTargets", DX12_DRAW_ZONES_ACTIVE );
		if ( !( motionActive ? PrepareMotionBinding( target ) : PrepareRenderTargets( target ) ) || ( !target.colorCount && !target.depth ) )
		{
			static unsigned invalidTarget = 0;
			if ( invalidTarget++ < 6 )
				Warning( "ShaderAPIDX12: draw target unavailable colorCount=%u depth=%p\n", target.colorCount, target.depth );
			return;
		}
		m_Pipeline.BindRenderTargets( list, target.colorCount, target.rtvs, target.colors, target.depth ? &target.dsv : nullptr, target.depth, retireFence );
	}
	D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>( target.width ), static_cast<float>( target.height ), 0, 1 };
	if ( m_nViewportCount > 0 )
	{
		const ShaderViewport_t &v = m_Viewports[0];
		viewport.TopLeftX = static_cast<float>( v.m_nTopLeftX );
		viewport.TopLeftY = static_cast<float>( v.m_nTopLeftY );
		viewport.Width = static_cast<float>( MAX( 0, v.m_nWidth ) );
		viewport.Height = static_cast<float>( MAX( 0, v.m_nHeight ) );
		viewport.MinDepth = v.m_flMinZ;
		viewport.MaxDepth = v.m_flMaxZ;
	}
	D3D12_RECT scissor{ 0, 0, target.width, target.height };
	if ( m_FastIntParams[4] )
	{
		scissor.left = MAX( 0L, static_cast<LONG>( m_FastIntParams[0] ) );
		scissor.top = MAX( 0L, static_cast<LONG>( m_FastIntParams[1] ) );
		scissor.right = MAX( scissor.left, MIN( static_cast<LONG>( target.width ), static_cast<LONG>( m_FastIntParams[2] ) ) );
		scissor.bottom = MAX( scissor.top, MIN( static_cast<LONG>( target.height ), static_cast<LONG>( m_FastIntParams[3] ) ) );
	}
	m_Pipeline.BindDrawState( list, viewport, scissor, retireFence );
	float drawClipPlanes[6][4];
	bool bDrawClipPlanesOverride = false;
	uint32_t drawClipMask = m_nClipPlaneMask;
	const auto addClipPlane = [&]( const float *pPlane )
	{
		if ( !bDrawClipPlanesOverride )
		{
			memcpy( drawClipPlanes, m_WorldClipPlanes, sizeof( drawClipPlanes ) );
			bDrawClipPlanesOverride = true;
		}
		for ( size_t i = 0; i < ARRAYSIZE( drawClipPlanes ); ++i )
			if ( !( drawClipMask & ( 1u << i ) ) )
			{
				memcpy( drawClipPlanes[i], pPlane, sizeof( drawClipPlanes[i] ) );
				drawClipMask |= 1u << i;
				return true;
			}
		return false;
	};
	if ( m_HeightClipMode != MATERIAL_HEIGHTCLIPMODE_DISABLE )
	{
		const bool bAbove = m_HeightClipMode == MATERIAL_HEIGHTCLIPMODE_RENDER_ABOVE_HEIGHT;
		const float heightPlane[4] = { 0.f, 0.f, bAbove ? 1.f : -1.f, bAbove ? -m_flHeightClipZ : m_flHeightClipZ };
		if ( !addClipPlane( heightPlane ) )
		{
			Warning( "ShaderAPIDX12: no free shader user clip plane for height clipping\n" );
			return;
		}
	}
	if ( m_bFastClipEnabled && !addClipPlane( m_FastClipPlane ) )
	{
		Warning( "ShaderAPIDX12: no free shader user clip plane for fast clipping\n" );
		return;
	}
	const float( *pClipPlanes )[4] = bDrawClipPlanesOverride ? drawClipPlanes : m_WorldClipPlanes;
	// Pipeline-state memo: a mesh draw whose pipeline inputs equal the previous successful mesh draw in
	// this recording reuses its shader records, translated variants, input layout and PSO. The slow path
	// clears the memo before it can switch any record's active variant and stores it only on success.
	const uint8_t meshStreamFlags = static_cast<uint8_t>( meshStreams ? ( ( bindings[1].buffer ? 1 : 0 ) | ( bindings[2].buffer ? 2 : 0 ) | ( flexWrinkle ? 4 : 0 ) ) : 0 );
	// Texture dimensions (not identities) are the only texture inputs of pipeline selection.
	ShaderRasterStateDX12 raster{};
	uint32_t textureTypesPacked = 0;
	for ( size_t i = 0; i < ARRAYSIZE( m_BoundTextures ); ++i )
	{
		const ShaderAPITextureHandle_t handle = m_BoundTextures[i];
		if ( !handle )
			continue;
		// Handles are never reused for a different texture; DeleteTexture drops cached entries.
		if ( m_TextureTypeHandles[i] == handle )
		{
			raster.textureTypes[i] = m_TextureTypeValues[i];
			continue;
		}
		const PreparedTextureSlot &slot = m_PreparedTextureSlots[i];
		const TextureRecord *texture = slot.valid && slot.record && slot.handle == handle ? slot.record : FindTexture( handle );
		if ( texture )
		{
			raster.textureTypes[i] = ( texture->flags & TEXTURE_CREATE_CUBEMAP ) ? 2 : ( texture->depth > 1 ? 3 : 1 );
			m_TextureTypeHandles[i] = handle;
			m_TextureTypeValues[i] = raster.textureTypes[i];
		}
	}
	for ( unsigned i = 0; i < 16; ++i )
		textureTypesPacked |= static_cast<uint32_t>( raster.textureTypes[i] ) << ( 2 * i );
	PipelineSignatureDX12 signature;
	memset( &signature, 0, sizeof( signature ) );
	const bool memoEligible = meshStreams && m_hActiveSnapshotId >= 0;
	bool memoHit = false;
	PipelineMemoDX12 *memoSlot = nullptr;
	if ( memoEligible )
	{
		signature.resolveEpoch = m_nNamedResolveEpoch;
		signature.snapshot = m_hActiveSnapshotId;
		signature.vs = reinterpret_cast<uint64_t>( m_hBoundVS );
		signature.ps = reinterpret_cast<uint64_t>( m_hBoundPS );
		signature.gs = reinterpret_cast<uint64_t>( m_hBoundGS );
		signature.textureTypes = textureTypesPacked;
		signature.format = static_cast<uint64_t>( format );
		signature.layoutKey = m_nSourceLayoutTranslationKey;
		signature.policy = m_nUnusedVertexFields;
		for ( size_t i = 0; i < ARRAYSIZE( m_UnusedTextureCoordinates ); ++i )
			if ( m_UnusedTextureCoordinates[i] )
				signature.policy |= uint64_t( 1 ) << ( 32 + i );
		signature.instanceCount = bindings[0].repetitions;
		signature.primitive = static_cast<uint32_t>( primitive );
		signature.streamFlags = meshStreamFlags;
		signature.motionPass = motionActive ? 1 : 0;
		signature.clipMask = drawClipMask;
		memcpy( signature.colorFormats, target.colorFormats, sizeof( signature.colorFormats ) );
		signature.depthFormat = target.depthFormat;
		signature.colorCount = target.colorCount;
		signature.samples = target.sampleCount;
		signature.quality = target.sampleQuality;
		signature.hasDepth = target.depth != nullptr;
		memcpy( &signature.rasterState, &m_RasterState, sizeof( m_RasterState ) );
		signature.rasterOverride = m_bRasterOverride;
		signature.shadeMode = m_ShadeMode;
		signature.fogMode = m_FogMode;
		signature.pixelFog = ShouldUsePixelFog();
		signature.cullMode = m_CullMode;
		signature.stencilEnabled = m_bStencilEnabled;
		signature.stencilCompare = m_StencilCompare;
		signature.stencilFail = m_StencilFailOp;
		signature.stencilDepthFail = m_StencilDepthFailOp;
		signature.stencilPass = m_StencilPassOp;
		signature.stencilReadMask = m_nStencilReadMask;
		signature.stencilWriteMask = m_nStencilWriteMask;
		signature.alphaToCoverage = m_bAlphaToCoverage;
		signature.colorWriteOverride = m_bColorWriteOverride;
		signature.colorWriteValue = m_bColorWriteOverrideValue;
		signature.alphaWriteOverride = m_bAlphaWriteOverride;
		signature.alphaWriteValue = m_bAlphaWriteOverrideValue;
		signature.overrideDepthEnable = m_bOverrideDepthEnable;
		signature.overrideDepthValue = m_bOverrideDepthValue;
		signature.forceDepthEquals = m_bForceDepthEquals;
		signature.shadowSlope = m_FastFloatParams[0];
		signature.shadowDepth = m_FastFloatParams[1];
		if ( m_pShaderUtil )
		{
			const MaterialSystem_Config_t &config = m_pShaderUtil->GetConfig();
			signature.reverseDepth = config.bReverseDepth;
			signature.slopeDecal = config.m_SlopeScaleDepthBias_Decal;
			signature.slopeNormal = config.m_SlopeScaleDepthBias_Normal;
			signature.depthDecal = config.m_DepthBias_Decal;
			signature.depthNormal = config.m_DepthBias_Normal;
		}
		memoSlot = &m_PipelineMemos[Mix32HashFunctor()( static_cast<uint32_t>( signature.snapshot ) * 0x9E3779B1u ^ static_cast<uint32_t>( signature.vs >> 4 ) ^ static_cast<uint32_t>( signature.ps >> 4 ) * 31u ^ static_cast<uint32_t>( signature.textureTypes ) ^ static_cast<uint32_t>( signature.format ) ) & ( ARRAYSIZE( m_PipelineMemos ) - 1 )];
		// Records may have switched translated variant since the entry was stored; require the stored ones.
		// A shader alternating between translated variants (e.g. fog or clip state) still hits: the stored
		// variants are reactivated, which is exactly what the slow path's EnsureTranslated would do.
		memoHit = memoSlot->epoch == m_nPipelineMemoEpoch && memoSlot->psoEpoch == m_Pipeline.PipelineEpoch() && !memcmp( &signature, &memoSlot->signature, sizeof( signature ) ) &&
		    ActivateTranslatedVariant( memoSlot->vs, memoSlot->vsVariant ) && ( !memoSlot->ps || ActivateTranslatedVariant( memoSlot->ps, memoSlot->psVariant ) );
	}
	bool depthOnly = false, generatedVS = false, generatedPS = false, zeroInput = false, geometryStage = false;
	D3D12_SHADER_BYTECODE geometryCode{};
	uint64_t geometryIdentity = 0, geometryVariant = 0;
	if ( memoHit )
	{
		vsRecord = memoSlot->vs;
		psRecord = memoSlot->ps;
		geometryStage = memoSlot->geometryStage;
		depthOnly = memoSlot->depthOnly;
		generatedVS = memoSlot->generatedVS;
		generatedPS = memoSlot->generatedPS;
		zeroInput = memoSlot->zeroInput;
	}
	else
	{
		raster.clipPlaneMask = drawClipMask;
		raster.alphaTest = m_ActiveSnapshot.alphaTest;
		raster.alphaFunction = static_cast<uint32_t>( m_ActiveSnapshot.alphaFunction ) + 1;
		raster.shadeMode = m_ShadeMode == SHADER_FLAT ? 1 : 2;
		raster.fog = m_ActiveSnapshot.fogMode != SHADER_FOGMODE_DISABLED && m_FogMode != MATERIAL_FOG_NONE && !ShouldUsePixelFog();
		raster.fogTableMode = 0;
		raster.comparisonPixelSamplers = m_ActiveSnapshot.comparisonSamplerMask;
		raster.fillMode = m_bRasterOverride ? ( m_RasterState.m_FillMode == SHADER_FILL_WIREFRAME ? 2 : 3 ) : ( m_ActiveSnapshot.polyFront == SHADER_POLYMODE_POINT ? 1 : ( m_ActiveSnapshot.polyFront == SHADER_POLYMODE_LINE ? 2 : 3 ) );
		raster.primitiveType = primitive == MATERIAL_POINTS ? 1 : ( primitive == MATERIAL_LINES ? 2 : ( primitive == MATERIAL_LINE_STRIP ? 3 : ( primitive == MATERIAL_TRIANGLE_STRIP ? 5 : 4 ) ) );
		depthOnly = !motionActive && target.depth && !( m_bColorWriteOverride ? m_bColorWriteOverrideValue : m_ActiveSnapshot.colorWrites ) && !( m_bAlphaWriteOverride ? m_bAlphaWriteOverrideValue : m_ActiveSnapshot.alphaWrites ) && !m_ActiveSnapshot.alphaTest;
		generatedVS = !vsRecord;
		generatedPS = !psRecord && !depthOnly;
		auto fixedShader = [&]( bool pixel ) -> ShaderRecordDX12 *
		{
			uint32_t textureTypes = 0;
			uint64_t linkage = 1469598103934665603ull;
			if ( pixel )
			{
				for ( unsigned i = 0; i < 16; ++i )
					textureTypes |= static_cast<uint32_t>( raster.textureTypes[i] ) << ( 2 * i );
				if ( vsRecord )
					for ( const ShaderLinkageDX12 &output : vsRecord->translated.outputLinkage )
					{
						linkage ^= output.usage | ( uint64_t( output.usageIndex ) << 8 ) | ( uint64_t( output.registerIndex ) << 16 ) | ( uint64_t( output.writeMask ) << 24 );
						linkage *= 1099511628211ull;
					}
			}
			if ( pixel )
			{
				linkage ^= static_cast<uint64_t>( m_ShadeMode );
				linkage *= 1099511628211ull;
			}
			const FixedShaderKey key{ m_hActiveSnapshotId, format, pixel, textureTypes, pixel ? linkage : 0 };
			const uint32_t found = m_FixedShaders.Find( key );
			if ( found != m_FixedShaders.InvalidIndex() )
				return m_FixedShaders[found];
			FixedFunctionStateDX12 state = m_ActiveSnapshot.fixed;
			state.format = format;
			state.flatShade = m_ShadeMode == SHADER_FLAT;
			memcpy( state.textureTypes, raster.textureTypes, sizeof( state.textureTypes ) );
			ShaderRecordDX12 *pRecord = CreateFixedFunctionShaderDX12( m_pDevice, state, pixel, vsRecord ? &vsRecord->translated.outputLinkage : nullptr );
			if ( !pRecord )
			{
				Warning( "ShaderAPIDX12: unable to compile generated %s shader\n", pixel ? "pixel" : "vertex" );
				return nullptr;
			}
			m_FixedShaders.Insert( key, pRecord );
			return pRecord;
		};
		if ( generatedVS )
		{
			vsRecord = fixedShader( false );
			if ( !vsRecord )
				return;
		}
		const bool needsTranslationKey = ( vsRecord && !vsRecord->legacyBytecode.IsEmpty() ) || ( psRecord && !psRecord->legacyBytecode.IsEmpty() );
		uint64_t translationStateKey = 0;
		if ( needsTranslationKey )
		{
			const uint64_t layoutTranslationKey = meshStreams ? m_nSourceLayoutTranslationKey : TranslationLayoutKey( sourceLayout );
			// Compare only declared bytes; tail padding is indeterminate and would merely cause a recompute.
			constexpr size_t rasterBytes = offsetof( ShaderRasterStateDX12, transformedVertices ) + sizeof( bool );
			if ( m_bTranslationKeyMemoValid && m_nTranslationKeyMemoLayout == layoutTranslationKey && !memcmp( &m_TranslationKeyMemoRaster, &raster, rasterBytes ) )
				translationStateKey = m_nTranslationKeyMemo;
			else
			{
				translationStateKey = TranslationStateKey( layoutTranslationKey, raster );
				m_TranslationKeyMemoRaster = raster;
				m_nTranslationKeyMemoLayout = layoutTranslationKey;
				m_nTranslationKeyMemo = translationStateKey;
				m_bTranslationKeyMemoValid = true;
			}
		}
		if ( !EnsureTranslated( vsRecord, false, sourceLayout, nullptr, 0, 0, raster, translationStateKey, m_pDevice->Signer() ) )
			return;
		if ( !ReflectRecordInputsDX12( vsRecord ) )
			return;
		if ( m_hBoundGS != GEOMETRY_SHADER_HANDLE_INVALID )
		{
			const ShaderRecordDX12 *geometry = reinterpret_cast<ShaderRecordDX12 *>( m_hBoundGS );
			geometryCode = geometry->Bytecode();
			geometryIdentity = geometry->identity;
			geometryVariant = geometry->activeVariantKey;
		}
		else if ( raster.fillMode == 1 && raster.primitiveType >= 4 )
		{
			const uint32_t rasterKey = raster.primitiveType | ( raster.fillMode << 4 ) | ( raster.clipPlaneMask << 8 );
			CUtlBlockVector<ShaderRecordDX12::GeometryVariant> &variants = vsRecord->geometryVariants;
			int index = 0;
			for ( ; index < variants.Count(); ++index )
				if ( variants[index].vertexVariant == vsRecord->activeVariantKey && variants[index].rasterKey == rasterKey )
					break;
			if ( index == variants.Count() )
			{
				ShaderTranslationResultDX12 result;
				CShaderTranslatorDX12 translator;
				CUtlString error;
				const CUtlVector<ShaderLinkageDX12> &outputs = vsRecord->translated.outputLinkage;
				if ( !translator.GenerateGeometry( outputs.Base(), outputs.Count(), raster, m_pDevice->Signer(), result, error ) )
				{
					Warning( "ShaderAPIDX12: point-fill geometry conversion failed: %s\n", error.Get() );
					return;
				}
				ShaderRecordDX12::GeometryVariant &variant = variants[variants.AddToTail()];
				variant.vertexVariant = vsRecord->activeVariantKey;
				variant.rasterKey = rasterKey;
				variant.result.Swap( result );
			}
			const CUtlVector<uint8_t> &code = variants[index].result.bytecode;
			geometryCode = { code.Base(), static_cast<SIZE_T>( code.Count() ) };
			geometryIdentity = vsRecord->identity;
			geometryVariant = static_cast<uint64_t>( index ) + 1;
		}
		geometryStage = geometryCode.BytecodeLength != 0;
		if ( generatedPS )
		{
			psRecord = fixedShader( true );
			if ( !psRecord )
				return;
		}
		if ( psRecord )
		{
			CUtlVector<ShaderLinkageDX12> &linkage = vsRecord->translated.outputLinkage;
			if ( !vsRecord->linkageHashValid || vsRecord->linkageHashVariant != vsRecord->activeVariantKey )
			{
				uint64_t hash = 1469598103934665603ull;
				const auto mix = [&]( uint64_t value )
				{
					hash ^= value;
					hash *= 1099511628211ull;
				};
				for ( const ShaderLinkageDX12 &output : linkage )
				{
					mix( output.usage );
					mix( output.usageIndex );
					mix( output.registerIndex );
					mix( output.writeMask );
					mix( output.centroid );
				}
				vsRecord->linkageHash = hash;
				vsRecord->linkageHashVariant = vsRecord->activeVariantKey;
				vsRecord->linkageHashValid = true;
			}
			if ( !EnsureTranslated( psRecord, true, sourceLayout, linkage.Base(), linkage.Count(), vsRecord->linkageHash, raster, translationStateKey, m_pDevice->Signer() ) )
				return;
		}
		if ( !ReflectRecordInputsDX12( psRecord ) )
			return;
		// A native record's space-1 cbuffers must be engine blocks with the backend layout or material blocks
		// written through the bridge with the same layout hash; anything else rejects the draw before PSO creation.
		auto validateNative = [&]( ShaderRecordDX12 *record, bool pixel ) -> bool
		{
			if ( !record || !record->legacyBytecode.IsEmpty() )
				return true;
			const char *stageName = pixel ? "PS" : "VS";
			const char *logical = ( pixel ? m_ActiveSnapshot.pixelShaderName : m_ActiveSnapshot.vertexShaderName ).c_str();
			const int staticIndex = pixel ? m_ActiveSnapshot.staticPixelIndex : m_ActiveSnapshot.staticVertexIndex, dynamicIndex = pixel ? m_nPixelShaderIndex : m_nVertexShaderIndex;
			for ( const ShaderRecordDX12::NativeCBufferBindingDX12 &binding : record->nativeCBuffers )
			{
				// Space 0 is the existing native-source contract (Source register banks at root b0-b5); space 1 is the named-block ABI.
				if ( binding.registerSpace != 1 )
					continue;
				const dx12native::EngineCBufferLayoutDX12 *engine = nullptr;
				for ( const dx12native::EngineCBufferLayoutDX12 &candidate : dx12native::kEngineCBufferLayouts )
					if ( binding.name == candidate.name )
					{
						engine = &candidate;
						break;
					}
				if ( engine )
				{
					bool matches = engine->stage == ( pixel ? dx12native::kStagePixel : dx12native::kStageVertex ) && engine->shaderRegister == binding.shaderRegister && engine->byteSize == binding.byteSize && engine->memberCount == static_cast<uint32_t>( binding.members.Count() );
					for ( uint32_t m = 0; matches && m < engine->memberCount; ++m )
						matches = binding.members[m].name == engine->members[m].name && binding.members[m].offset == engine->members[m].offset && binding.members[m].byteSize == engine->members[m].size;
					if ( !matches )
					{
						Warning( "ShaderAPIDX12: native %s shader %s (static %d dynamic %d abi %016llx) engine cbuffer %s b%u size %u does not match the backend layout\n", stageName, logical, staticIndex, dynamicIndex, static_cast<unsigned long long>( record->nativeAbiHash ), binding.name.Get(), binding.shaderRegister, binding.byteSize );
						return false;
					}
					continue;
				}
				const unsigned firstMaterial = pixel ? 1u : 2u;
				const NativeCBufferSlotDX12 *slot = nullptr;
				if ( binding.shaderRegister >= firstMaterial && binding.shaderRegister <= 7u )
					slot = pixel ? &m_NativePSBlocks[binding.shaderRegister - 1] : &m_NativeVSBlocks[binding.shaderRegister - 2];
				if ( !slot || !slot->written || slot->layoutHash != binding.layoutHash || slot->byteSize != binding.byteSize )
				{
					Warning( "ShaderAPIDX12: native %s shader %s (static %d dynamic %d abi %016llx) material cbuffer %s b%u,space1 expected layout %016llx size %u, bound %016llx size %u\n", stageName, logical, staticIndex, dynamicIndex, static_cast<unsigned long long>( record->nativeAbiHash ), binding.name.Get(), binding.shaderRegister, static_cast<unsigned long long>( binding.layoutHash ), binding.byteSize,
					    static_cast<unsigned long long>( slot && slot->written ? slot->layoutHash : 0 ), slot && slot->written ? slot->byteSize : 0u );
					return false;
				}
			}
			return true;
		};
		if ( !validateNative( vsRecord, false ) || !validateNative( psRecord, true ) )
			return;
	}
	const auto constants = [&]( ShaderRecordDX12 *record, bool pixel, bool generated ) -> bool
	{
		ZoneNamedN( derivedConstants, "DX12 DerivedConstants", DX12_DRAW_ZONES_ACTIVE );
		const unsigned first = pixel ? 3 : 0, descriptor = pixel ? 4 : 0;
		if ( !record )
		{
			for ( unsigned bank = 0; bank < 3; ++bank )
			{
				bindingInput.constantVersions[first + bank] = 1;
				bindingInput.constantShaderIds[first + bank] = 0;
				bindingInput.consumedRegisters[first + bank] = 0;
				bindingInput.constantData[descriptor + bank] = nullptr;
				bindingInput.constantSizes[descriptor + bank] = 0;
			}
			return true;
		}
		// Compact per-variant layout on the record's first cache line: avoids touching translated metadata per draw.
		if ( !record->constantLayoutValid || record->constantLayoutVariant != record->activeVariantKey )
		{
			if ( record->legacyBytecode.IsEmpty() )
				memcpy( record->constantCounts, record->nativeConstantRegisters, sizeof( record->constantCounts ) );
			else
			{
				record->constantCounts[0] = record->translated.maxFloatConstants;
				record->constantCounts[1] = record->translated.maxIntConstants;
				record->constantCounts[2] = record->translated.maxBoolConstants;
			}
			record->inlineConstantMask = 0;
			for ( unsigned bank = 0; bank < 3; ++bank )
				if ( !record->translated.inlineConstants[bank].IsEmpty() )
					record->inlineConstantMask |= 1u << bank;
			record->constantLayoutVariant = record->activeVariantKey;
			record->constantLayoutValid = true;
		}
		const uint32_t( &counts )[3] = record->constantCounts;
		const uint32_t inlineMask = record->inlineConstantMask;
		ShaderRecordDX12::DerivedConstants &derived = record->derived;
		for ( unsigned bank = 0; bank < 3; ++bank )
		{
			uint64_t version = m_ConstantVersions[first + bank];
			if ( generated )
				version += pixel ? m_nFixedPSVersion + m_ConstantVersions[0] : m_nFixedVSVersion;
			if ( generated && !pixel && bank == 1 )
				version = m_nFixedVSVersion;
			bindingInput.constantVersions[first + bank] = version;
			// Unpatched banks are the same API register file across shaders. Inline definitions
			// belong to the original program, not its raster/linkage translation variant.
			const bool hasInline = ( inlineMask >> bank ) & 1;
			bindingInput.constantShaderIds[first + bank] = ( generated || hasInline ) ? record->identity : 0;
			bindingInput.consumedRegisters[first + bank] = counts[bank];
			if ( bank == 0 )
			{
				const float( *source )[4] = pixel ? m_PsFloat : m_VsFloat;
				const size_t available = pixel ? ARRAYSIZE( m_PsFloat ) : ARRAYSIZE( m_VsFloat );
				if ( counts[bank] > available )
					return false;
				const bool patch = generated || hasInline;
				if ( patch )
				{
					const bool changed = derived.versions[bank] != version;
					derived.versions[bank] = version;
					if ( changed )
					{
						derived.floats.SetCount( counts[bank] );
						if ( counts[bank] )
							memcpy( derived.floats.Base(), source, counts[bank] * sizeof( source[0] ) );
						for ( const ShaderInlineConstantDX12 &constant : record->translated.inlineConstants[bank] )
							if ( constant.registerIndex < counts[bank] )
								memcpy( derived.floats[constant.registerIndex].Base(), constant.words, 16 );
						if ( generated && !pixel )
						{
							if ( derived.floats.Count() > 1 )
								derived.floats[1].Init( m_AmbientLight.x, m_AmbientLight.y, m_AmbientLight.z, 0.f );
							for ( int face = 0; face < static_cast<int>( ARRAYSIZE( m_AmbientCube ) ) && 21 + face < derived.floats.Count(); ++face )
								memcpy( derived.floats[21 + face].Base(), m_AmbientCube[face], sizeof( m_AmbientCube[face] ) );
						}
						else if ( generated )
						{
							const auto set = [&]( size_t index, float x, float y, float z, float w )
							{
								if ( index < static_cast<size_t>( derived.floats.Count() ) )
									derived.floats[static_cast<int>( index )].Init( x, y, z, w );
							};
							set( 0, m_Color.x, m_Color.y, m_Color.z, m_flColorAlpha );
							set( 1, 0.f, 0.f, 0.f, m_ActiveSnapshot.alphaReference );
							const VMatrix &projection = m_Matrices[MATERIAL_PROJECTION];
							set( 2, projection[2][2], projection[2][3], projection[3][2], projection[3][3] );
							set( 28, m_flFogStart, m_flFogEnd, m_flFogEnd != m_flFogStart ? 1.f / ( m_flFogEnd - m_flFogStart ) : 1.f, m_FogMode == MATERIAL_FOG_NONE ? 0.f : m_flFogMaxDensity );
							set( 29, m_RasterFogColor[0], m_RasterFogColor[1], m_RasterFogColor[2], 0.f );
						}
					}
					bindingInput.constantData[descriptor + bank] = derived.floats.Base();
				}
				else
					bindingInput.constantData[descriptor + bank] = source;
				bindingInput.constantSizes[descriptor + bank] = counts[bank] * 16;
			}
			else if ( bank == 1 )
			{
				const int( *source )[4] = pixel ? m_PsInt : m_VsInt;
				if ( generated && !pixel )
				{
					if ( counts[bank] > 40 )
						return false;
					const bool changed = derived.versions[bank] != version;
					derived.versions[bank] = version;
					if ( changed )
					{
						derived.integers.SetCount( counts[bank] );
						for ( unsigned stage = 0; stage < 8; ++stage )
						{
							for ( unsigned row = 0; row < 4 && stage * 4 + row < counts[bank]; ++row )
								memcpy( derived.integers[stage * 4 + row].Base(), m_Matrices[MATERIAL_TEXTURE0 + stage][row], 16 );
							const float flags[4] = { m_TextureTransformEnabled[stage] ? 1.f : 0.f, m_TextureTransformProjected[stage] ? 1.f : 0.f, static_cast<float>( m_TextureTransformDimension[stage] ), 0.f };
							if ( 32 + stage < counts[bank] )
								memcpy( derived.integers[32 + stage].Base(), flags, 16 );
						}
					}
					bindingInput.constantData[descriptor + bank] = derived.integers.Base();
				}
				else
				{
					if ( counts[bank] > 16 )
						return false;
					if ( !hasInline )
						bindingInput.constantData[descriptor + bank] = source;
					else
					{
						const bool changed = derived.versions[bank] != version;
						derived.versions[bank] = version;
						if ( changed )
						{
							derived.integers.SetCount( counts[bank] );
							if ( counts[bank] )
								memcpy( derived.integers.Base(), source, counts[bank] * sizeof( source[0] ) );
							for ( const ShaderInlineConstantDX12 &constant : record->translated.inlineConstants[bank] )
								if ( constant.registerIndex < counts[bank] )
									memcpy( derived.integers[constant.registerIndex].Base(), constant.words, 16 );
						}
						bindingInput.constantData[descriptor + bank] = derived.integers.Base();
					}
				}
				bindingInput.constantSizes[descriptor + bank] = counts[bank] * 16;
			}
			else
			{
				if ( counts[bank] > 16 )
					return false;
				if ( !counts[bank] )
				{
					bindingInput.constantData[descriptor + bank] = nullptr;
					bindingInput.constantSizes[descriptor + bank] = 0;
					continue;
				}
				const bool changed = derived.versions[bank] != version;
				derived.versions[bank] = version;
				if ( changed )
				{
					derived.booleans.SetCount( counts[bank] );
					for ( unsigned i = 0; i < counts[bank]; ++i )
						derived.booleans[i] = ( pixel ? m_PsBool[i] : m_VsBool[i] ) ? 1u : 0u;
					for ( const ShaderInlineConstantDX12 &constant : record->translated.inlineConstants[bank] )
						if ( constant.registerIndex < counts[bank] )
							derived.booleans[constant.registerIndex] = constant.words[0];
				}
				bindingInput.constantData[descriptor + bank] = derived.booleans.Base();
				bindingInput.constantSizes[descriptor + bank] = counts[bank] * sizeof( uint32_t );
			}
		}
		return true;
	};
	if ( !constants( vsRecord, false, generatedVS ) || !constants( psRecord, true, generatedPS ) )
	{
		Warning( "ShaderAPIDX12: shader constant bank exceeds its source register range\n" );
		return;
	}
	// Pixel extension: fog and alpha reference; field compare, rebuilt only when an input changed.
	{
		const float fogInverse = m_flFogEnd != m_flFogStart ? 1.f / ( m_flFogEnd - m_flFogStart ) : 0.f;
		ShaderPixelExtensionDX12 &pixel = m_PreviousPixelExtension;
		if ( !m_ExtensionVersions[1] || pixel.fogColor[0] != m_RasterFogColor[0] || pixel.fogColor[1] != m_RasterFogColor[1] || pixel.fogColor[2] != m_RasterFogColor[2] || pixel.fogStart != m_flFogStart || pixel.fogEnd != m_flFogEnd ||
		    pixel.fogDistanceInverse != fogInverse || pixel.fogDensity != m_flFogMaxDensity || pixel.alphaReference != m_ActiveSnapshot.alphaReference )
		{
			pixel.fogColor[0] = m_RasterFogColor[0];
			pixel.fogColor[1] = m_RasterFogColor[1];
			pixel.fogColor[2] = m_RasterFogColor[2];
			pixel.fogStart = m_flFogStart;
			pixel.fogEnd = m_flFogEnd;
			pixel.fogDistanceInverse = fogInverse;
			pixel.fogDensity = m_flFogMaxDensity;
			pixel.alphaReference = m_ActiveSnapshot.alphaReference;
			++m_ExtensionVersions[1];
		}
	}
	// Bump matrices change only through SetBumpEnvMatrix; the colour-key buffer is always zero.
	if ( m_bBumpExtensionDirty )
	{
		ShaderBumpExtensionDX12 next = m_PreviousBumpExtension;
		for ( size_t stage = 0; stage < ARRAYSIZE( m_BumpMatrices ); ++stage )
			memcpy( next.matrix[stage], m_BumpMatrices[stage], sizeof( m_BumpMatrices[stage] ) );
		if ( !m_ExtensionVersions[2] || memcmp( &next, &m_PreviousBumpExtension, sizeof( next ) ) )
		{
			m_PreviousBumpExtension = next;
			++m_ExtensionVersions[2];
		}
		m_bBumpExtensionDirty = false;
	}
	if ( !m_ExtensionVersions[3] )
		m_ExtensionVersions[3] = 1;
	// Vertex extension: viewport scale, point size and user clip planes transformed to clip space. Without clip
	// planes (now and in the stored buffer) only the viewport can change it.
	// Native-AA jitter shifts eligible perspective scene draws by (jx, jy) pixels (y down) through the same clip-space
	// epilogue; projection and motion-vector matrices stay unjittered. .xy is the translated/native epilogue (half-pixel
	// offset plus jitter); .zw is the jitter alone for generated fixed-function shaders, which have no half-pixel offset.
	// Generated point-expansion geometry shaders also scale point size by .xy, so those draws stay unjittered.
	float viewportScale[4] = { viewport.Width > 0 ? 1.f / viewport.Width : 1.f, viewport.Height > 0 ? -1.f / viewport.Height : -1.f, 0.f, 0.f };
	if ( !geometryStage && UpscalerJitterActive( motionActive ) && viewport.Width > 0 && viewport.Height > 0 )
	{
		viewportScale[2] = 2.f * m_UpscalerJitter[0] / viewport.Width;
		viewportScale[3] = -2.f * m_UpscalerJitter[1] / viewport.Height;
		viewportScale[0] += viewportScale[2];
		viewportScale[1] += viewportScale[3];
	}
	if ( drawClipMask || m_nVertexExtensionClipMask || !m_ExtensionVersions[0] || memcmp( m_PreviousVertexExtension.viewportScale, viewportScale, sizeof( viewportScale ) ) )
	{
		ShaderVertexExtensionDX12 vertexExtension{};
		memcpy( vertexExtension.viewportScale, viewportScale, sizeof( viewportScale ) );
		vertexExtension.pointSize[0] = 1.f;
		if ( drawClipMask )
		{
			ZoneNamedN( clipTransform, "DX12 ClipPlaneTransform", DX12_DRAW_ZONES_ACTIVE );
			VMatrix worldToClip;
			MatrixMultiply( m_Matrices[MATERIAL_PROJECTION], m_bUserClipViewOverride ? m_UserClipView : m_Matrices[MATERIAL_VIEW], worldToClip );
			if ( !m_bClipInverseValid || memcmp( worldToClip.Base(), m_CachedWorldToClip.Base(), sizeof( float ) * 16 ) )
			{
				m_bClipInverseValid = MatrixInverseGeneral( worldToClip, m_CachedClipToWorld );
				if ( !m_bClipInverseValid )
				{
					Warning( "ShaderAPIDX12: cannot transform clip plane through singular projection/view matrix\n" );
					return;
				}
				m_CachedWorldToClip = worldToClip;
			}
			for ( size_t i = 0; i < ARRAYSIZE( m_WorldClipPlanes ); ++i )
				if ( drawClipMask & ( 1u << i ) )
					for ( int c = 0; c < 4; ++c )
						for ( int r = 0; r < 4; ++r )
							vertexExtension.clipPlanes[i][c] += m_CachedClipToWorld[r][c] * pClipPlanes[i][r];
		}
		if ( !m_ExtensionVersions[0] || memcmp( &vertexExtension, &m_PreviousVertexExtension, sizeof( vertexExtension ) ) )
		{
			m_PreviousVertexExtension = vertexExtension;
			++m_ExtensionVersions[0];
		}
		m_nVertexExtensionClipMask = drawClipMask;
	}
	if ( motionActive )
		CommitTransforms();
	// Native records read engine state from space-1 blocks built from the same values the legacy registers hold.
	const bool nativeVS = vsRecord && vsRecord->legacyBytecode.IsEmpty(), nativePS = psRecord && psRecord->legacyBytecode.IsEmpty();
	bindingInput.nativeStage[0] = nativeVS;
	bindingInput.nativeStage[1] = nativePS;
	memset( bindingInput.nativeData, 0, sizeof( bindingInput.nativeData ) );
	memset( bindingInput.nativeSizes, 0, sizeof( bindingInput.nativeSizes ) );
	memset( bindingInput.nativeVersions, 0, sizeof( bindingInput.nativeVersions ) );
	if ( nativeVS )
	{
		ZoneNamedN( nativeEngine, "DX12 NativeVSEngine", DX12_DRAW_ZONES_ACTIVE );
		// Rebuilt only when a source register bank, the vertex extension (viewport/clip planes) or the clip mask changed.
		const uint64_t engineInputs[5] = { m_ConstantVersions[0], m_ConstantVersions[1], m_ConstantVersions[2], m_ExtensionVersions[0], drawClipMask };
		if ( memcmp( m_NativeVSEngineInputs, engineInputs, sizeof( engineInputs ) ) )
		{
			dx12native::DX12VSEngine &e = m_NativeVSEngine;
			const auto copy = [&]( float *dst, unsigned reg, unsigned count )
			{
				memcpy( dst, m_VsFloat[reg], sizeof( float ) * 4 * count );
			};
			copy( e.cConstants0, VERTEX_SHADER_MATH_CONSTANTS0, 1 );
			copy( e.cConstants1, VERTEX_SHADER_MATH_CONSTANTS1, 1 );
			copy( e.cEyePosWaterZ, VERTEX_SHADER_CAMERA_POS, 1 );
			copy( e.cFlexScale, VERTEX_SHADER_FLEXSCALE, 1 );
			copy( e.cModelViewProj, VERTEX_SHADER_MODELVIEWPROJ, 4 );
			copy( e.cViewProj, VERTEX_SHADER_VIEWPROJ, 4 );
			copy( e.cModelViewProjZ, VERTEX_SHADER_MODELVIEWPROJ_THIRD_ROW, 1 );
			copy( e.cViewProjZ, VERTEX_SHADER_VIEWPROJ_THIRD_ROW, 1 );
			copy( e.cFogParams, VERTEX_SHADER_FOG_PARAMS, 1 );
			copy( e.cViewModel, VERTEX_SHADER_VIEWMODEL, 4 );
			copy( e.cAmbientCube[0], VERTEX_SHADER_AMBIENT_LIGHT, 6 );
			copy( e.cLightInfo[0].color, VERTEX_SHADER_LIGHTS, 20 );
			memcpy( e.cLightCount, m_VsInt[0], sizeof( e.cLightCount ) );
			for ( unsigned i = 0; i < 4; ++i )
				e.cLightEnabled[i] = m_VsBool[VERTEX_SHADER_LIGHT_ENABLE_BOOL_CONST + i] ? 1u : 0u;
			memcpy( e.cViewportScale, m_PreviousVertexExtension.viewportScale, sizeof( e.cViewportScale ) );
			memcpy( e.cClipPlanes, m_PreviousVertexExtension.clipPlanes, sizeof( e.cClipPlanes ) );
			e.cClipMask[0] = drawClipMask;
			e.cClipMask[1] = e.cClipMask[2] = e.cClipMask[3] = 0;
			static_assert( sizeof( m_NativeVSBones.cModel ) == sizeof( float ) * 4 * 3 * 53, "bone rows" );
			memcpy( m_NativeVSBones.cModel, m_VsFloat[VERTEX_SHADER_MODEL], sizeof( m_NativeVSBones.cModel ) );
			memcpy( m_NativeVSEngineInputs, engineInputs, sizeof( m_NativeVSEngineInputs ) );
			++m_nNativeVSEngineVersion;
		}
		bindingInput.nativeData[0] = &m_NativeVSEngine;
		bindingInput.nativeSizes[0] = sizeof( m_NativeVSEngine );
		bindingInput.nativeVersions[0] = m_nNativeVSEngineVersion;
		bindingInput.nativeData[1] = &m_NativeVSBones;
		bindingInput.nativeSizes[1] = sizeof( m_NativeVSBones );
		bindingInput.nativeVersions[1] = m_nNativeVSEngineVersion;
		for ( unsigned slot = 0; slot < ARRAYSIZE( m_NativeVSBlocks ); ++slot )
			if ( m_NativeVSBlocks[slot].written )
			{
				bindingInput.nativeData[2 + slot] = m_NativeVSBlocks[slot].bytes.Base();
				bindingInput.nativeSizes[2 + slot] = m_NativeVSBlocks[slot].byteSize;
				bindingInput.nativeVersions[2 + slot] = m_NativeVSBlocks[slot].version;
			}
		if ( motionActive )
		{
			FillMotionBlock( bindings[0], indices, indexOffset, firstIndex, indexCount );
			bindingInput.nativeData[7] = &m_MotionBlock;
			bindingInput.nativeSizes[7] = sizeof( m_MotionBlock );
			bindingInput.nativeVersions[7] = m_nMotionBlockVersion;
		}
	}
	if ( nativePS )
	{
		dx12native::DX12PSEngine &e = m_NativePSEngine;
		const float alphaTest[4] = { m_ActiveSnapshot.alphaTest ? 1.f : 0.f, static_cast<float>( m_ActiveSnapshot.alphaFunction ) + 1.f, m_PreviousPixelExtension.alphaReference, 0.f };
		const float rasterFog[4] = { m_PreviousPixelExtension.fogColor[0], m_PreviousPixelExtension.fogColor[1], m_PreviousPixelExtension.fogColor[2], raster.fog ? 1.f : 0.f };
		const float rasterFogParams[4] = { m_PreviousPixelExtension.fogStart, m_PreviousPixelExtension.fogEnd, m_PreviousPixelExtension.fogDistanceInverse, m_PreviousPixelExtension.fogDensity };
		const float lightScale[4] = { m_ToneScale.x, m_ToneScale.y, m_ToneScale.z, m_flToneScaleGamma };
		const auto update = [&]( float *dst, const float *value, size_t bytes )
		{
			if ( memcmp( dst, value, bytes ) )
			{
				memcpy( dst, value, bytes );
				++m_nNativePSEngineVersion;
			}
		};
		update( e.cPixelFogParams, m_FogPixelParams, sizeof( e.cPixelFogParams ) );
		update( e.cLinearFogColor, m_FogPixelColor, sizeof( e.cLinearFogColor ) );
		update( e.cLightScale, lightScale, sizeof( lightScale ) );
		update( e.cAlphaTest, alphaTest, sizeof( alphaTest ) );
		update( e.cRasterFogColor, rasterFog, sizeof( rasterFog ) );
		update( e.cRasterFogParams, rasterFogParams, sizeof( rasterFogParams ) );
		bindingInput.nativeData[8] = &m_NativePSEngine;
		bindingInput.nativeSizes[8] = sizeof( m_NativePSEngine );
		bindingInput.nativeVersions[8] = m_nNativePSEngineVersion;
		for ( unsigned slot = 0; slot < ARRAYSIZE( m_NativePSBlocks ); ++slot )
			if ( m_NativePSBlocks[slot].written )
			{
				bindingInput.nativeData[9 + slot] = m_NativePSBlocks[slot].bytes.Base();
				bindingInput.nativeSizes[9 + slot] = m_NativePSBlocks[slot].byteSize;
				bindingInput.nativeVersions[9 + slot] = m_NativePSBlocks[slot].version;
			}
	}
	bindingInput.constantData[3] = &m_PreviousVertexExtension;
	bindingInput.constantSizes[3] = sizeof( m_PreviousVertexExtension );
	bindingInput.constantData[7] = &m_PreviousPixelExtension;
	bindingInput.constantSizes[7] = sizeof( m_PreviousPixelExtension );
	bindingInput.constantData[8] = &m_PreviousBumpExtension;
	bindingInput.constantSizes[8] = sizeof( m_PreviousBumpExtension );
	static const ShaderColorKeyExtensionDX12 colorKeyExtension{};
	bindingInput.constantData[9] = &colorKeyExtension;
	bindingInput.constantSizes[9] = sizeof( colorKeyExtension );
	for ( unsigned bank = 0; bank < 4; ++bank )
		bindingInput.constantVersions[6 + bank] = m_ExtensionVersions[bank];
	bindingInput.vertexTextures = vertexSamplers != 0;
	bindingInput.geometryStage = geometryStage;
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 PrepareBindings", DX12_DRAW_ZONES_ACTIVE );
		if ( !m_Pipeline.PrepareBindings( list, bindingInput ) )
		{
			Warning( "ShaderAPIDX12: binding descriptors unavailable\n" );
			return;
		}
	}
	const D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = primitive == MATERIAL_POINTS ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT : ( primitive == MATERIAL_LINES || primitive == MATERIAL_LINE_STRIP || primitive == MATERIAL_LINE_LOOP ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE );
	const D3D12_PRIMITIVE_TOPOLOGY iaTopology = primitive == MATERIAL_POINTS ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST : ( primitive == MATERIAL_LINES ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : ( primitive == MATERIAL_LINE_STRIP ? D3D_PRIMITIVE_TOPOLOGY_LINESTRIP : ( primitive == MATERIAL_TRIANGLE_STRIP ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST ) ) );
	const UINT instanceCount = bindings[0].repetitions;
	if ( memoHit )
	{
		++m_DrawStats.memoHits;
		m_Pipeline.BindPipelineState( list, memoSlot->pso, m_bStencilEnabled ? m_nStencilRef : m_ActiveSnapshot.stencilReference, retireFence );
		pipelineBound = true;
	}
	else if ( vsRecord && ( psRecord || depthOnly ) )
	{
		ZoneNamedN( pipelineSetup, "DX12 PipelineSetup", DX12_DRAW_ZONES_ACTIVE );
		ShaderRecordDX12 *vs = vsRecord;
		ShaderRecordDX12 *ps = psRecord;
		D3D12_INPUT_ELEMENT_DESC elements[MAX_VERTEX_INPUTS_DX12];
		if ( vs->inputSignature.Count() > static_cast<int>( ARRAYSIZE( elements ) ) )
			return;
		const D3D12_INPUT_ELEMENT_DESC *inputElements = elements;
		const UINT signatureCount = static_cast<UINT>( vs->inputSignature.Count() );
		const uint8_t streamFlags = meshStreamFlags;
		uint64_t policy = m_nUnusedVertexFields;
		for ( size_t i = 0; i < ARRAYSIZE( m_UnusedTextureCoordinates ); ++i )
			if ( m_UnusedTextureCoordinates[i] )
				policy |= uint64_t( 1 ) << ( 32 + i );
		uint64_t layoutKey = 0;
		UINT inputCount = signatureCount;
		InputLayoutCache &layoutCache = m_InputLayoutCaches[Mix32HashFunctor()( static_cast<uint32_t>( reinterpret_cast<uintptr_t>( vs ) >> 4 ) ^ static_cast<uint32_t>( vs->activeVariantKey ) ^ static_cast<uint32_t>( format ) ^ static_cast<uint32_t>( policy >> 32 ) ^ static_cast<uint32_t>( policy ) ^ ( uint32_t( streamFlags ) << 24 ) ^ ( instanceCount << 16 ) ) & ( ARRAYSIZE( m_InputLayoutCaches ) - 1 )];
		const bool cachedLayout = meshStreams && layoutCache.valid && layoutCache.shader == vs && layoutCache.variant == vs->activeVariantKey && layoutCache.policy == policy && layoutCache.format == format && layoutCache.instanceCount == instanceCount && layoutCache.streamFlags == streamFlags && layoutCache.count == signatureCount;
		if ( cachedLayout )
		{
			inputElements = layoutCache.elements;
			layoutKey = layoutCache.layoutKey;
			zeroInput = layoutCache.zeroInput;
		}
		else
		{
			layoutKey = 1469598103934665603ull;
			const auto hashLayout = [&]( uint64_t value )
			{
				layoutKey ^= value;
				layoutKey *= 1099511628211ull;
			};
			for ( int i = 0; i < vs->inputSignature.Count(); ++i )
			{
				const ShaderInputElementDX12 &required = vs->inputSignature[i];
				D3D12_INPUT_ELEMENT_DESC &element = elements[i];
				element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
				element.InstanceDataStepRate = 0;
				element.SemanticName = required.semantic.Get();
				element.SemanticIndex = required.semanticIndex;
				const VertexInputDX12 *provided = nullptr;
				for ( uint32_t j = 0; j < sourceLayout.inputCount; ++j )
					if ( required.semanticIndex == sourceLayout.inputs[j].semanticIndex && !V_strcmp( required.semantic.Get(), sourceLayout.inputs[j].semantic ) )
					{
						provided = &sourceLayout.inputs[j];
						break;
					}
				const unsigned int field = required.semantic == "POSITION" ? VERTEX_POSITION : ( required.semantic == "NORMAL" ? VERTEX_NORMAL : ( required.semantic == "COLOR" ? ( required.semanticIndex == 0 ? VERTEX_COLOR : VERTEX_SPECULAR ) : ( required.semantic == "BLENDINDICES" ? VERTEX_BONE_INDEX : 0 ) ) );
				if ( ( field & m_nUnusedVertexFields ) || ( required.semantic == "TEXCOORD" && required.semanticIndex < ARRAYSIZE( m_UnusedTextureCoordinates ) && m_UnusedTextureCoordinates[required.semanticIndex] ) )
					provided = nullptr;
				if ( provided )
				{
					element.Format = provided->format;
					element.InputSlot = provided->inputSlot;
					element.AlignedByteOffset = provided->byteOffset;
					if ( !meshStreams && element.InputSlot > 0 && instanceCount > 1 )
					{
						element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
						element.InstanceDataStepRate = bindings[element.InputSlot].repetitions;
					}
				}
				else
				{
					element.Format = required.format;
					element.InputSlot = 16;
					element.AlignedByteOffset = 0;
					zeroInput = true;
				}
				for ( const char *name = element.SemanticName; *name; ++name )
					hashLayout( static_cast<unsigned char>( *name ) );
				hashLayout( element.SemanticIndex );
				hashLayout( element.Format );
				hashLayout( element.InputSlot );
				hashLayout( element.AlignedByteOffset );
				hashLayout( element.InputSlotClass );
				hashLayout( element.InstanceDataStepRate );
			}
			if ( meshStreams )
			{
				layoutCache.shader = vs;
				layoutCache.variant = vs->activeVariantKey;
				layoutCache.policy = policy;
				layoutCache.format = format;
				layoutCache.instanceCount = instanceCount;
				layoutCache.streamFlags = streamFlags;
				layoutCache.count = signatureCount;
				layoutCache.layoutKey = layoutKey;
				layoutCache.zeroInput = zeroInput;
				memcpy( layoutCache.elements, elements, signatureCount * sizeof( elements[0] ) );
				layoutCache.valid = true;
			}
		}
		D3D12_INPUT_LAYOUT_DESC input{ inputElements, inputCount };
		PipelineKeyDX12 key{};
		key.vs = vs->identity;
		key.ps = ps ? ps->identity : 0;
		key.vsVariant = vs->activeVariantKey;
		key.psVariant = ps ? ps->activeVariantKey : 0;
		key.gs = geometryIdentity;
		key.gsVariant = geometryVariant;
		key.input = layoutKey;
		key.topology = topologyType;
		key.color = target.colorFormat;
		memcpy( key.colorFormats, target.colorFormats, sizeof( key.colorFormats ) );
		key.colorCount = target.colorCount;
		key.depth = target.depthFormat;
		key.samples = target.sampleCount;
		key.sampleQuality = target.sampleQuality;
		key.blend = m_ActiveSnapshot.translucent || m_ActiveSnapshot.separateAlpha ? 1 : 0;
		key.blendSource = m_ActiveSnapshot.translucent ? m_ActiveSnapshot.blendSource : SHADER_BLEND_ONE;
		key.blendDestination = m_ActiveSnapshot.translucent ? m_ActiveSnapshot.blendDestination : SHADER_BLEND_ZERO;
		key.blendOperation = m_ActiveSnapshot.translucent ? m_ActiveSnapshot.blendOperation : SHADER_BLEND_OP_ADD;
		key.separateAlpha = m_ActiveSnapshot.separateAlpha;
		key.blendAlphaSource = m_ActiveSnapshot.blendAlphaSource;
		key.blendAlphaDestination = m_ActiveSnapshot.blendAlphaDestination;
		key.blendAlphaOperation = m_ActiveSnapshot.blendAlphaOperation;
		key.depthState = m_ActiveSnapshot.depthWrite ? 1 : 0;
		key.depthTest = m_bOverrideDepthEnable ? m_bOverrideDepthValue : m_ActiveSnapshot.depthTest;
		key.depthWrite = m_ActiveSnapshot.depthWrite;
		key.depthFunction = m_bForceDepthEquals ? SHADER_DEPTHFUNC_EQUAL : m_ActiveSnapshot.depthFunction;
		const bool reverseDepth = m_pShaderUtil && m_pShaderUtil->GetConfig().bReverseDepth;
		if ( reverseDepth )
		{
			switch ( key.depthFunction )
			{
			case SHADER_DEPTHFUNC_NEARER:
				key.depthFunction = SHADER_DEPTHFUNC_FARTHER;
				break;
			case SHADER_DEPTHFUNC_NEAREROREQUAL:
				key.depthFunction = SHADER_DEPTHFUNC_FARTHEROREQUAL;
				break;
			case SHADER_DEPTHFUNC_FARTHER:
				key.depthFunction = SHADER_DEPTHFUNC_NEARER;
				break;
			case SHADER_DEPTHFUNC_FARTHEROREQUAL:
				key.depthFunction = SHADER_DEPTHFUNC_NEAREROREQUAL;
				break;
			default:
				break;
			}
		}
		key.culling = m_ActiveSnapshot.culling && ( !m_bRasterOverride || m_RasterState.m_bCullEnable );
		key.frontCounterClockwise = m_CullMode == MATERIAL_CULLMODE_CW;
		key.wireframe = raster.fillMode == 2;
		key.scissor = m_bRasterOverride && m_RasterState.m_bScissorEnable;
		static const MaterialSystem_Config_t defaultConfig;
		const MaterialSystem_Config_t &config = m_pShaderUtil ? m_pShaderUtil->GetConfig() : defaultConfig;
		const PolygonOffsetMode_t offset = m_ActiveSnapshot.polygonOffset != SHADER_POLYOFFSET_DISABLE ? m_ActiveSnapshot.polygonOffset : ( m_bRasterOverride && m_RasterState.m_bDepthBias ? SHADER_POLYOFFSET_DECAL : SHADER_POLYOFFSET_DISABLE );
		const float slope = offset == SHADER_POLYOFFSET_SHADOW_BIAS ? m_FastFloatParams[0] : ( offset == SHADER_POLYOFFSET_DECAL ? config.m_SlopeScaleDepthBias_Decal : config.m_SlopeScaleDepthBias_Normal );
		const float depth = offset == SHADER_POLYOFFSET_SHADOW_BIAS ? m_FastFloatParams[1] : ( offset == SHADER_POLYOFFSET_DECAL ? config.m_DepthBias_Decal : config.m_DepthBias_Normal );
		const float direction = reverseDepth ? -1.f : 1.f;
		// DX9 (ApplyZBias) feeds decal/normal biases as the reciprocal of the configured values but passes the
		// shadow-map factors through unchanged. D3D12's integer depth bias is in units of 1/2^24 for a 24-bit depth buffer.
		const bool shadowBias = offset == SHADER_POLYOFFSET_SHADOW_BIAS;
		const float slopeFactor = slope != 0.f ? ( shadowBias ? slope : 1.f / slope ) : 0.f;
		const float depthFactor = depth != 0.f ? ( shadowBias ? depth : 1.f / depth ) : 0.f;
		key.slopeScaledDepthBias = direction * slopeFactor;
		key.depthBiasValue = static_cast<int>( clamp( roundf( direction * 16777216.f * depthFactor ), -16777216.f, 16777216.f ) );
		key.colorWrites = m_bColorWriteOverride ? m_bColorWriteOverrideValue : m_ActiveSnapshot.colorWrites;
		key.alphaWrites = m_bAlphaWriteOverride ? m_bAlphaWriteOverrideValue : m_ActiveSnapshot.alphaWrites;
		key.alphaToCoverage = m_bAlphaToCoverage || m_ActiveSnapshot.alphaToCoverage;
		key.stencil = m_bStencilEnabled || m_ActiveSnapshot.stencil;
		key.raster = ( key.alphaToCoverage ? 1 : 0 ) | ( key.stencil ? 2 : 0 ) | ( m_ActiveSnapshot.alphaTest ? 4 : 0 );
		if ( m_bStencilEnabled )
		{
			key.stencilFunction = m_StencilCompare;
			key.stencilFail = m_StencilFailOp;
			key.stencilDepthFail = m_StencilDepthFailOp;
			key.stencilPass = m_StencilPassOp;
			key.stencilReadMask = m_nStencilReadMask;
			key.stencilWriteMask = m_nStencilWriteMask;
		}
		else
		{
			key.stencilFunction = static_cast<uint32_t>( m_ActiveSnapshot.stencilFunction ) + 1;
			key.stencilFail = static_cast<uint32_t>( m_ActiveSnapshot.stencilFail ) + 1;
			key.stencilDepthFail = static_cast<uint32_t>( m_ActiveSnapshot.stencilDepthFail ) + 1;
			key.stencilPass = static_cast<uint32_t>( m_ActiveSnapshot.stencilPass ) + 1;
			key.stencilReadMask = m_ActiveSnapshot.stencilReadMask;
			key.stencilWriteMask = m_ActiveSnapshot.stencilWriteMask;
		}
		if ( motionActive )
		{
			key.depthState = 0;
			key.depthWrite = false;
			key.depthTest = true;
			key.depthFunction = reverseDepth ? SHADER_DEPTHFUNC_FARTHEROREQUAL : SHADER_DEPTHFUNC_NEAREROREQUAL;
			key.blend = 0;
			key.blendSource = SHADER_BLEND_ONE;
			key.blendDestination = SHADER_BLEND_ZERO;
			key.blendOperation = SHADER_BLEND_OP_ADD;
			key.separateAlpha = false;
			key.colorWrites = true;
			key.alphaWrites = true;
			key.alphaToCoverage = false;
			key.stencil = false;
			key.raster = 0;
			key.slopeScaledDepthBias = 0.f;
			key.depthBiasValue = reverseDepth ? -kMotionDepthBias : kMotionDepthBias;
		}
		ID3D12PipelineState *pso = nullptr;
		{
			ZoneNamedN( ___tracy_scoped_zone, "DX12 GetOrCreatePSO", DX12_DRAW_ZONES_ACTIVE );
			pso = m_Pipeline.GetOrCreate( key, vs->Bytecode(), ps ? ps->Bytecode() : D3D12_SHADER_BYTECODE{}, geometryCode, input, retireFence );
		}
		if ( pso )
		{
			m_Pipeline.BindPipelineState( list, pso, m_bStencilEnabled ? m_nStencilRef : m_ActiveSnapshot.stencilReference, retireFence );
			pipelineBound = true;
			if ( memoSlot )
			{
				memoSlot->signature = signature;
				memoSlot->vs = vsRecord;
				memoSlot->ps = psRecord;
				memoSlot->pso = pso;
				memoSlot->vsVariant = vsRecord->activeVariantKey;
				memoSlot->psVariant = psRecord ? psRecord->activeVariantKey : 0;
				memoSlot->depthOnly = depthOnly;
				memoSlot->generatedVS = generatedVS;
				memoSlot->generatedPS = generatedPS;
				memoSlot->zeroInput = zeroInput;
				memoSlot->geometryStage = geometryStage;
				memoSlot->epoch = m_nPipelineMemoEpoch;
				memoSlot->psoEpoch = m_Pipeline.PipelineEpoch();
			}
		}
	}
	if ( !pipelineBound )
	{
		static unsigned invalidShaders = 0;
		if ( invalidShaders++ < 10 )
			Warning( "ShaderAPIDX12: shaders unavailable vs=%p ps=%p materialVS=%s materialPS=%s\n", reinterpret_cast<void *>( m_hBoundVS ), reinterpret_cast<void *>( m_hBoundPS ), m_ActiveSnapshot.vertexShaderName.c_str(), m_ActiveSnapshot.pixelShaderName.c_str() );
		return;
	}
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 GeometryUploads", DX12_DRAW_ZONES_ACTIVE );
		// Only views [0,vertexViewCount) and the zero stream are read; unused slots below the count are zeroed.
		D3D12_VERTEX_BUFFER_VIEW vertexViews[17];
		UINT vertexViewCount = 0;
		for ( unsigned slot = 0; slot < ARRAYSIZE( bindings ); ++slot )
			if ( bindings[slot].buffer )
			{
				for ( UINT gap = vertexViewCount; gap < slot; ++gap )
					vertexViews[gap] = {};
				const VertexBindingDX12 &binding = bindings[slot];
				CVertexBufferDX12 &vertices = *binding.buffer;
				const size_t bytes = static_cast<size_t>( vertices.WrittenCount() ) * vertices.Stride();
				size_t swapCount = 0;
				const uint32_t *swaps = vertices.SwapOffsets( swapCount );
				D3D12_GPU_VIRTUAL_ADDRESS address = 0;
				if ( vertices.IsDynamic() )
				{
					ZoneNamedN( ___tracy_scoped_zone, "DX12 DynamicVertexUpload", DX12_DRAW_ZONES_ACTIVE );
					if ( !m_Pipeline.UploadDynamic( vertices, bytes, 16, retireFence, address, swaps, swapCount, vertices.Stride() ) )
						return;
				}
				else if ( !m_Pipeline.EnsureGeometryBuffer( list, vertices, bytes, retireFence, address, swaps, swapCount, vertices.Stride() ) )
					return;
				// firstVertex is the declared minimum index, not an additional base vertex.
				vertexViews[slot] = { address + binding.byteOffset, static_cast<UINT>( ( binding.firstVertex + binding.vertexCount ) * vertices.Stride() ), vertices.Stride() };
				vertexViewCount = slot + 1;
			}
		if ( zeroInput )
		{
			static constexpr uint32_t zero[4]{};
			D3D12_GPU_VIRTUAL_ADDRESS address = 0;
			if ( !m_Pipeline.UploadTransient( zero, sizeof( zero ), sizeof( zero ), 16, retireFence, address ) )
				return;
			vertexViews[16] = { address, sizeof( zero ), 0 };
		}
		{
			ZoneNamedN( ___tracy_scoped_zone, "DX12 VertexBindings", DX12_DRAW_ZONES_ACTIVE );
			// The input layout reads only supplied streams and the optional zero stream.
			m_Pipeline.BindInputAssembler( list, vertexViews, vertexViewCount, zeroInput ? &vertexViews[16] : nullptr, iaTopology, retireFence );
		}
	}
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 IndexUploadAndDraw", DX12_DRAW_ZONES_ACTIVE );
		if ( indexed )
		{
			D3D12_GPU_VIRTUAL_ADDRESS address = 0;
			if ( indices->IsDynamic() )
			{
				if ( !m_Pipeline.UploadDynamic( *indices, indexBytes, 4, retireFence, address ) )
					return;
			}
			else if ( !m_Pipeline.EnsureIndexBuffer( list, *indices, indexBytes, retireFence, address ) )
				return;
			const D3D12_INDEX_BUFFER_VIEW view{ address + indexOffset, static_cast<UINT>( indexBytes - indexOffset ), indices->IndexFormat() == MATERIAL_INDEX_FORMAT_32BIT ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT };
			m_Pipeline.BindIndexBuffer( list, &view, retireFence );
			list->DrawIndexedInstanced( indexCount, instanceCount, firstIndex, 0, 0 );
		}
		else
		{
			m_Pipeline.BindIndexBuffer( list, nullptr, retireFence );
			list->DrawInstanced( indexCount, instanceCount, firstIndex, 0 );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Drops every cached pipeline, input layout and binding that references a destroyed shader record
//-----------------------------------------------------------------------------
void CShaderAPIDX12::RetireShaderPipelines( ShaderRecordDX12 *pRecord )
{
	if ( !pRecord )
		return;
	m_Pipeline.NotifyShaderDestroyed( pRecord->identity );
	for ( InputLayoutCache &layoutCache : m_InputLayoutCaches )
		if ( layoutCache.shader == pRecord )
			layoutCache.valid = false;
	++m_nPipelineMemoEpoch;
	if ( m_hBoundVS == reinterpret_cast<VertexShaderHandle_t>( pRecord ) )
	{
		m_hBoundVS = VERTEX_SHADER_HANDLE_INVALID;
		m_bBoundVertexShaderIsNamed = false;
	}
	if ( m_hBoundPS == reinterpret_cast<PixelShaderHandle_t>( pRecord ) )
	{
		m_hBoundPS = PIXEL_SHADER_HANDLE_INVALID;
		m_bBoundPixelShaderIsNamed = false;
	}
	if ( m_hBoundGS == reinterpret_cast<GeometryShaderHandle_t>( pRecord ) )
		m_hBoundGS = GEOMETRY_SHADER_HANDLE_INVALID;
}

//-----------------------------------------------------------------------------
// Purpose: Resets per-pass dynamic state (texture transforms, color, shade mode, shader indices)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetDefaultState()
{
	// Material shaders call this inside a pass; the snapshot and bindings must survive.
	for ( int stage = 0; stage < 4; ++stage )
	{
		DisableTextureTransform( static_cast<TextureStage_t>( stage ) );
		MatrixMode( static_cast<MaterialMatrixMode_t>( MATERIAL_TEXTURE0 + stage ) );
		LoadIdentity();
	}
	MatrixMode( MATERIAL_MODEL );
	Color4ub( 255, 255, 255, 255 );
	ShadeMode( SHADER_SMOOTH );
	SetVertexShaderIndex();
	SetPixelShaderIndex();
	MarkUnusedVertexFields( 0, 0, nullptr );
}

//-----------------------------------------------------------------------------
// Viewports
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetViewports( int nCount, const ShaderViewport_t *pViewports )
{
	AUTO_LOCK( m_StateMutex );
	m_nViewportCount = MAX( 0, MIN( nCount, 16 ) );
	if ( pViewports && m_nViewportCount )
		memcpy( m_Viewports, pViewports, m_nViewportCount * sizeof( ShaderViewport_t ) );
}

int CShaderAPIDX12::GetViewports( ShaderViewport_t *pViewports, int nMax ) const
{
	const int nViewports = MIN( m_nViewportCount, MAX( 0, nMax ) );
	if ( pViewports && nViewports )
		memcpy( pViewports, m_Viewports, nViewports * sizeof( ShaderViewport_t ) );
	return nViewports;
}

double CShaderAPIDX12::CurrentTime() const
{
	return Plat_FloatTime();
}

//-----------------------------------------------------------------------------
// Scene queries forwarded to the shader utility / fog state
//-----------------------------------------------------------------------------
void CShaderAPIDX12::GetLightmapDimensions( int *w, int *h )
{
	if ( m_pShaderUtil )
		m_pShaderUtil->GetLightmapDimensions( w, h );
	else
	{
		if ( w )
			*w = 0;
		if ( h )
			*h = 0;
	}
}

MaterialFogMode_t CShaderAPIDX12::GetSceneFogMode()
{
	return m_FogMode;
}

void CShaderAPIDX12::GetSceneFogColor( unsigned char *rgb )
{
	if ( rgb )
	{
		rgb[0] = m_FogColor[0];
		rgb[1] = m_FogColor[1];
		rgb[2] = m_FogColor[2];
	}
}

//-----------------------------------------------------------------------------
// Purpose: Flags transforms or the fixed-function texture matrices dirty after a write to the current matrix
//-----------------------------------------------------------------------------
void CShaderAPIDX12::MatrixChanged()
{
	if ( m_MatrixMode == MATERIAL_VIEW || m_MatrixMode == MATERIAL_PROJECTION || m_MatrixMode >= MATERIAL_MODEL )
	{
		// Fog depends on the view only through the camera position; CommitTransforms flags that change.
		m_bTransformsDirty = true;
		if ( m_MatrixMode >= MATERIAL_MODEL )
			m_nMaxBoneLoaded = MAX( m_nMaxBoneLoaded, MIN( NUM_MODEL_TRANSFORMS - 1, int( m_MatrixMode ) - int( MATERIAL_MODEL ) ) );
	}
	if ( m_MatrixMode >= MATERIAL_TEXTURE0 && m_MatrixMode <= MATERIAL_TEXTURE7 )
	{
		++m_nFixedVSVersion;
		m_nTextureMatrixIdentityMask &= ~( 1u << ( m_MatrixMode - MATERIAL_TEXTURE0 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: out = a*b for row-major VMatrix, accumulating each element in VMatrix::MatrixMul's k order.
//-----------------------------------------------------------------------------
static inline void MultiplyMatrixSSE( const VMatrix &a, const VMatrix &b, VMatrix &out )
{
	const __m128 b0 = _mm_loadu_ps( b[0] ), b1 = _mm_loadu_ps( b[1] ), b2 = _mm_loadu_ps( b[2] ), b3 = _mm_loadu_ps( b[3] );
	for ( int row = 0; row < 4; ++row )
	{
		__m128 sum = _mm_mul_ps( _mm_set1_ps( a[row][0] ), b0 );
		sum = _mm_add_ps( sum, _mm_mul_ps( _mm_set1_ps( a[row][1] ), b1 ) );
		sum = _mm_add_ps( sum, _mm_mul_ps( _mm_set1_ps( a[row][2] ), b2 ) );
		sum = _mm_add_ps( sum, _mm_mul_ps( _mm_set1_ps( a[row][3] ), b3 ) );
		_mm_storeu_ps( out[row], sum );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Uploads the model/view/projection derived vertex constants when a matrix changed
//-----------------------------------------------------------------------------
void CShaderAPIDX12::CommitTransforms()
{
	if ( !m_bTransformsDirty )
		return;
	const VMatrix &view = m_Matrices[MATERIAL_VIEW], &projection = m_Matrices[MATERIAL_PROJECTION];
	const bool viewChanged = !m_bCachedTransformValid || memcmp( view.Base(), m_CachedTransformView.Base(), sizeof( float ) * 16 );
	if ( viewChanged || memcmp( projection.Base(), m_CachedTransformProjection.Base(), sizeof( float ) * 16 ) )
	{
		m_CachedViewProjection = projection * view;
		m_CachedTransformProjection = projection;
	}
	if ( viewChanged )
	{
		VMatrix cameraToWorld;
		m_bCachedCameraValid = MatrixInverseGeneral( view, cameraToWorld );
		if ( m_bCachedCameraValid )
			m_CachedCameraPosition.Init( cameraToWorld[0][3], cameraToWorld[1][3], cameraToWorld[2][3] );
		m_CachedTransformView = view;
		m_bCachedTransformValid = true;
	}
	const VMatrix &viewProjection = m_CachedViewProjection;
	VMatrix modelViewProjection, modelView;
	MultiplyMatrixSSE( viewProjection, m_Matrices[MATERIAL_MODEL], modelViewProjection );
	MultiplyMatrixSSE( view, m_Matrices[MATERIAL_MODEL], modelView );
	SetVertexShaderConstant( VERTEX_SHADER_MODELVIEWPROJ, modelViewProjection.Base(), 4 );
	SetVertexShaderConstant( VERTEX_SHADER_VIEWPROJ, viewProjection.Base(), 4 );
	SetVertexShaderConstant( VERTEX_SHADER_MODELVIEWPROJ_THIRD_ROW, modelViewProjection[2], 1 );
	SetVertexShaderConstant( VERTEX_SHADER_VIEWPROJ_THIRD_ROW, viewProjection[2], 1 );
	SetVertexShaderConstant( VERTEX_SHADER_VIEWMODEL, modelView.Base(), 4 );
	const int nBoneRows = MAX( m_nMaxBoneLoaded + 1, m_nMotionBoneRows );
	for ( int bone = 0; bone < nBoneRows; ++bone )
		SetVertexShaderConstant( VERTEX_SHADER_MODEL + bone * 3, m_Matrices[MATERIAL_MODEL + bone].Base(), 3 );
	if ( m_bCachedCameraValid )
	{
		if ( m_CameraPosition != m_CachedCameraPosition )
			m_bFogDirty = true;
		m_CameraPosition = m_CachedCameraPosition;
		const float camera[4] = { m_CameraPosition.x, m_CameraPosition.y, m_CameraPosition.z, m_flFogZ };
		SetVertexShaderConstant( VERTEX_SHADER_CAMERA_POS, camera, 1 );
	}
	m_nMaxBoneLoaded = 0;
	m_bTransformsDirty = false;
}

//-----------------------------------------------------------------------------
// Matrix stack
//-----------------------------------------------------------------------------
void CShaderAPIDX12::MatrixMode( MaterialMatrixMode_t mode )
{
	if ( mode >= MATERIAL_VIEW && mode <= MATERIAL_MODEL_MAX )
		m_MatrixMode = mode;
}

void CShaderAPIDX12::PushMatrix()
{
	m_MatrixStacks[m_MatrixMode].AddToTail( m_Matrices[m_MatrixMode] );
}

void CShaderAPIDX12::PopMatrix()
{
	CUtlVector<VMatrix> &stack = m_MatrixStacks[m_MatrixMode];
	if ( stack.Count() )
	{
		m_Matrices[m_MatrixMode] = stack.Tail();
		stack.RemoveMultipleFromTail( 1 );
		MatrixChanged();
	}
}

void CShaderAPIDX12::LoadMatrix( float *m )
{
	if ( !m )
		return;
	for ( int row = 0; row < 4; ++row )
		for ( int column = 0; column < 4; ++column )
			m_Matrices[m_MatrixMode][row][column] = m[column * 4 + row];
	MatrixChanged();
}

void CShaderAPIDX12::MultMatrix( float *m )
{
	if ( !m )
		return;
	VMatrix rhs;
	for ( int row = 0; row < 4; ++row )
		for ( int column = 0; column < 4; ++column )
			rhs[row][column] = m[column * 4 + row];
	m_Matrices[m_MatrixMode] = rhs * m_Matrices[m_MatrixMode];
	MatrixChanged();
}

void CShaderAPIDX12::MultMatrixLocal( float *m )
{
	if ( !m )
		return;
	VMatrix rhs;
	for ( int row = 0; row < 4; ++row )
		for ( int column = 0; column < 4; ++column )
			rhs[row][column] = m[column * 4 + row];
	m_Matrices[m_MatrixMode] = m_Matrices[m_MatrixMode] * rhs;
	MatrixChanged();
}

void CShaderAPIDX12::GetMatrix( MaterialMatrixMode_t mode, float *dst )
{
	if ( !dst || mode < MATERIAL_VIEW || mode > MATERIAL_MODEL_MAX )
		return;
	for ( int row = 0; row < 4; ++row )
		for ( int column = 0; column < 4; ++column )
			dst[row * 4 + column] = m_Matrices[mode][column][row];
}

void CShaderAPIDX12::LoadIdentity()
{
	// Texture matrices feed only the fixed-function VS version; SetDefaultState reloads them every pass.
	const bool texture = m_MatrixMode >= MATERIAL_TEXTURE0 && m_MatrixMode <= MATERIAL_TEXTURE7;
	const uint32_t bit = texture ? 1u << ( m_MatrixMode - MATERIAL_TEXTURE0 ) : 0u;
	if ( texture && ( m_nTextureMatrixIdentityMask & bit ) )
		return;
	m_Matrices[m_MatrixMode].Identity();
	MatrixChanged();
	m_nTextureMatrixIdentityMask |= bit;
}

void CShaderAPIDX12::LoadCameraToWorld()
{
	VMatrix inverse;
	if ( !MatrixInverseGeneral( m_Matrices[MATERIAL_VIEW], inverse ) )
		return;
	inverse[0][3] = inverse[1][3] = inverse[2][3] = 0;
	m_Matrices[m_MatrixMode] = inverse;
	MatrixChanged();
}

void CShaderAPIDX12::Ortho( double left, double top, double right, double bottom, double zNear, double zFar )
{
	VMatrix projection;
	MatrixBuildOrtho( projection, left, top, right, bottom, zNear, zFar );
	m_Matrices[m_MatrixMode] = m_Matrices[m_MatrixMode] * projection;
	MatrixChanged();
}

void CShaderAPIDX12::PerspectiveX( double fovx, double aspect, double zNear, double zFar )
{
	VMatrix projection;
	MatrixBuildPerspectiveX( projection, fovx, aspect, zNear, zFar );
	m_Matrices[m_MatrixMode] = m_Matrices[m_MatrixMode] * projection;
	MatrixChanged();
}

void CShaderAPIDX12::PickMatrix( int x, int y, int width, int height )
{
	if ( width <= 0 || height <= 0 || m_nViewportCount <= 0 )
		return;
	const ShaderViewport_t &viewport = m_Viewports[0];
	if ( viewport.m_nWidth <= 0 || viewport.m_nHeight <= 0 )
		return;
	VMatrix pick;
	pick.Identity();
	pick[0][0] = float( viewport.m_nWidth ) / width;
	pick[1][1] = float( viewport.m_nHeight ) / height;
	pick[0][3] = float( viewport.m_nWidth - 2 * ( x - viewport.m_nTopLeftX ) ) / width;
	pick[1][3] = float( viewport.m_nHeight - 2 * ( y - viewport.m_nTopLeftY ) ) / height;
	m_Matrices[m_MatrixMode] = m_Matrices[m_MatrixMode] * pick;
	MatrixChanged();
}

void CShaderAPIDX12::Rotate( float angle, float x, float y, float z )
{
	MatrixRotate( m_Matrices[m_MatrixMode], Vector( x, y, z ), angle );
	MatrixChanged();
}

void CShaderAPIDX12::Translate( float x, float y, float z )
{
	MatrixTranslate( m_Matrices[m_MatrixMode], Vector( x, y, z ) );
	MatrixChanged();
}

void CShaderAPIDX12::Scale( float x, float y, float z )
{
	VMatrix scale;
	MatrixBuildScale( scale, x, y, z );
	m_Matrices[m_MatrixMode] = m_Matrices[m_MatrixMode] * scale;
	MatrixChanged();
}

void CShaderAPIDX12::ScaleXY( float x, float y )
{
	Scale( x, y, 1.f );
}

//-----------------------------------------------------------------------------
// Fixed-function color
//-----------------------------------------------------------------------------
void CShaderAPIDX12::Color3f( float r, float g, float b )
{
	if ( m_Color.x != r || m_Color.y != g || m_Color.z != b || m_flColorAlpha != 1.f )
	{
		m_Color.Init( r, g, b );
		m_flColorAlpha = 1.f;
		++m_nFixedPSVersion;
	}
}

void CShaderAPIDX12::Color3fv( float const *pColor )
{
	if ( pColor )
		Color3f( pColor[0], pColor[1], pColor[2] );
}

void CShaderAPIDX12::Color4f( float r, float g, float b, float a )
{
	if ( m_Color.x != r || m_Color.y != g || m_Color.z != b || m_flColorAlpha != a )
	{
		m_Color.Init( r, g, b );
		m_flColorAlpha = a;
		++m_nFixedPSVersion;
	}
}

void CShaderAPIDX12::Color4fv( float const *pColor )
{
	if ( pColor )
		Color4f( pColor[0], pColor[1], pColor[2], pColor[3] );
}

void CShaderAPIDX12::Color3ub( unsigned char r, unsigned char g, unsigned char b )
{
	Color3f( r / 255.0f, g / 255.0f, b / 255.0f );
}

void CShaderAPIDX12::Color3ubv( unsigned char const *pColor )
{
	if ( pColor )
		Color3ub( pColor[0], pColor[1], pColor[2] );
}

void CShaderAPIDX12::Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	Color4f( r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f );
}

void CShaderAPIDX12::Color4ubv( unsigned char const *pColor )
{
	if ( pColor )
		Color4ub( pColor[0], pColor[1], pColor[2], pColor[3] );
}

//-----------------------------------------------------------------------------
// Purpose: Stores a validated bridge write and mirrors it into the legacy register files immediately, so a legacy
//          record for the same logical name reads identical values with last-writer-wins ordering against direct
//          register writes (exactly as the DX9 material code's own SetPixel/VertexShaderConstant calls would).
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ApplyNativeCBufferWrite( const dx12native::NativeCBufferWriteDX12 &write, bool bPixel )
{
	NativeCBufferSlotDX12 &slot = bPixel ? m_NativePSBlocks[write.shaderRegister - 1] : m_NativeVSBlocks[write.shaderRegister - 2];
	slot.bytes.SetCount( write.byteSize );
	memcpy( slot.bytes.Base(), write.data, write.byteSize );
	slot.legacyMap = write.legacyMap;
	slot.layoutHash = write.layoutHash;
	slot.byteSize = write.byteSize;
	slot.written = true;
	slot.version = bPixel ? ++m_nNativePSVersion : ++m_nNativeVSVersion;
	const unsigned nFirstBank = bPixel ? 3 : 0;
	bool bFloats = false, bInts = false, bBools = false;
	const unsigned char *pBytes = static_cast<const unsigned char *>( write.data );
	for ( uint32_t e = 0; e < write.legacyMap->entryCount; ++e )
	{
		const dx12native::NativeCBufferLegacyEntryDX12 &entry = write.legacyMap->entries[e];
		for ( uint32_t n = 0; n < entry.elementCount; ++n )
		{
			const unsigned char *pSrc = pBytes + entry.byteOffset + size_t( n ) * entry.srcStride;
			const size_t nReg = size_t( entry.reg ) + n;
			if ( size_t( entry.byteOffset ) + size_t( n ) * entry.srcStride + entry.elementBytes > write.byteSize )
				break;
			if ( entry.bank == dx12native::kLegacyFloat && nReg < ARRAYSIZE( m_VsFloat ) )
			{
				float( &dst )[4] = bPixel ? m_PsFloat[nReg] : m_VsFloat[nReg];
				const size_t nBytes = MIN( static_cast<size_t>( entry.elementBytes ), static_cast<size_t>( 16u - entry.component * 4u ) );
				if ( memcmp( dst + entry.component, pSrc, nBytes ) )
				{
					memcpy( dst + entry.component, pSrc, nBytes );
					bFloats = true;
				}
			}
			else if ( entry.bank == dx12native::kLegacyInt && nReg < ARRAYSIZE( m_VsInt ) )
			{
				int( &dst )[4] = bPixel ? m_PsInt[nReg] : m_VsInt[nReg];
				const size_t nBytes = MIN( static_cast<size_t>( entry.elementBytes ), static_cast<size_t>( 16u - entry.component * 4u ) );
				if ( memcmp( dst + entry.component, pSrc, nBytes ) )
				{
					memcpy( dst + entry.component, pSrc, nBytes );
					bInts = true;
				}
			}
			else if ( entry.bank == dx12native::kLegacyBool && nReg < ARRAYSIZE( m_VsBool ) )
			{
				uint32_t nValue;
				memcpy( &nValue, pSrc, 4 );
				bool &bDst = bPixel ? m_PsBool[nReg] : m_VsBool[nReg];
				if ( bDst != ( nValue != 0 ) )
				{
					bDst = nValue != 0;
					bBools = true;
				}
			}
		}
	}
	if ( bFloats )
		++m_ConstantVersions[nFirstBank];
	if ( bInts )
		++m_ConstantVersions[nFirstBank + 1];
	if ( bBools )
		++m_ConstantVersions[nFirstBank + 2];
}

//-----------------------------------------------------------------------------
// Purpose: Writes float vertex registers, or applies a native cbuffer bridge write for the pointer register
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetVertexShaderConstant( int var, float const *values, int count, bool )
{
	ZoneNamedN( constants, "DX12 SetVertexConstants sampled", DX12_DRAW_ZONES_ACTIVE && ( m_nFrameCounter & 63 ) == 0 );
	if ( var == dx12native::kDX12NativeCBufferPointerVar )
	{
		const dx12native::NativeCBufferWriteDX12 *write = reinterpret_cast<const dx12native::NativeCBufferWriteDX12 *>( values );
		if ( !write || write->magic != dx12native::kDX12NativeCBufferWriteMagic || write->version != dx12native::kDX12NativeCBufferVersion || write->stage != dx12native::kStageVertex || write->registerSpace != 1 || write->shaderRegister < 2 || write->shaderRegister > 7 || !write->data || !write->legacyMap || write->byteSize == 0 || ( write->byteSize & 15 ) || write->byteSize > 65536 || write->legacyMap->layoutHash != write->layoutHash )
		{
			Warning( "ShaderAPIDX12: rejected native VS cbuffer write (magic/version/stage/space/register/size/data/map validation failed)\n" );
			return;
		}
		ApplyNativeCBufferWrite( *write, false );
		return;
	}
	if ( !values || var < 0 || count <= 0 )
		return;
	const int end = MIN( var + count, static_cast<int>( ARRAYSIZE( m_VsFloat ) ) );
	bool changed = false;
	for ( int r = var; r < end; ++r )
	{
		const __m128i incoming = _mm_loadu_si128( reinterpret_cast<const __m128i *>( values + ( r - var ) * 4 ) );
		__m128i *slot = reinterpret_cast<__m128i *>( m_VsFloat[r] );
		if ( _mm_movemask_epi8( _mm_cmpeq_epi32( incoming, _mm_loadu_si128( slot ) ) ) != 0xFFFF )
		{
			_mm_storeu_si128( slot, incoming );
			changed = true;
		}
	}
	if ( changed )
		++m_ConstantVersions[0];
}

//-----------------------------------------------------------------------------
// Purpose: Writes float pixel registers, or applies a native cbuffer bridge write for the pointer register
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetPixelShaderConstant( int var, float const *values, int count, bool )
{
	ZoneNamedN( constants, "DX12 SetPixelConstants sampled", DX12_DRAW_ZONES_ACTIVE && ( m_nFrameCounter & 63 ) == 0 );
	if ( var == dx12native::kDX12NativeCBufferPointerVar )
	{
		const dx12native::NativeCBufferWriteDX12 *write = reinterpret_cast<const dx12native::NativeCBufferWriteDX12 *>( values );
		if ( !write || write->magic != dx12native::kDX12NativeCBufferWriteMagic || write->version != dx12native::kDX12NativeCBufferVersion || write->stage != dx12native::kStagePixel || write->registerSpace != 1 || write->shaderRegister < 1 || write->shaderRegister > 7 || !write->data || !write->legacyMap || write->byteSize == 0 || ( write->byteSize & 15 ) || write->byteSize > 65536 || write->legacyMap->layoutHash != write->layoutHash )
		{
			Warning( "ShaderAPIDX12: rejected native PS cbuffer write (magic/version/stage/space/register/size/data/map validation failed)\n" );
			return;
		}
		ApplyNativeCBufferWrite( *write, true );
		return;
	}
	if ( !values || var < 0 || count <= 0 )
		return;
	const int end = MIN( var + count, static_cast<int>( ARRAYSIZE( m_PsFloat ) ) );
	bool changed = false;
	for ( int r = var; r < end; ++r )
	{
		const __m128i incoming = _mm_loadu_si128( reinterpret_cast<const __m128i *>( values + ( r - var ) * 4 ) );
		__m128i *slot = reinterpret_cast<__m128i *>( m_PsFloat[r] );
		if ( _mm_movemask_epi8( _mm_cmpeq_epi32( incoming, _mm_loadu_si128( slot ) ) ) != 0xFFFF )
		{
			_mm_storeu_si128( slot, incoming );
			changed = true;
		}
	}
	if ( changed )
		++m_ConstantVersions[3];
}

//-----------------------------------------------------------------------------
// Purpose: Restores the dynamic state of a freshly created shader API
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ResetNativeState()
{
	m_ShadowState = Snapshot{};
	m_ActiveSnapshot = Snapshot{};
	m_hBoundVS = VERTEX_SHADER_HANDLE_INVALID;
	m_hBoundGS = GEOMETRY_SHADER_HANDLE_INVALID;
	m_hBoundPS = PIXEL_SHADER_HANDLE_INVALID;
	m_hActiveSnapshotId = static_cast<StateSnapshot_t>( -1 );
	m_bForceDepthEquals = m_bOverrideDepthEnable = false;
	m_bOverrideDepthValue = true;
	m_ShadeMode = SHADER_SMOOTH;
	for ( int stage = 0; stage < static_cast<int>( ARRAYSIZE( m_TextureTransformEnabled ) ); ++stage )
	{
		m_TextureTransformEnabled[stage] = false;
		m_TextureTransformProjected[stage] = false;
		m_TextureTransformDimension[stage] = 2;
	}
	m_Color.Init( 1.f, 1.f, 1.f );
	m_flColorAlpha = 1.f;
	++m_nFixedPSVersion;
	++m_nFixedVSVersion;
	m_bNamedVertexShaderDirty = m_bNamedPixelShaderDirty = false;
	m_bBoundVertexShaderIsNamed = m_bBoundPixelShaderIsNamed = false;
	memset( m_BoundTextures, 0, sizeof( m_BoundTextures ) );
	memset( m_VertexTextures, 0, sizeof( m_VertexTextures ) );
	m_pBoundMaterial = nullptr;
	for ( VertexBindingDX12 &binding : m_BoundVertexBuffers )
		binding = VertexBindingDX12{};
	m_pBoundIndexBuffer = nullptr;
	m_nBoundIndexOffset = 0;
	m_nUnusedVertexFields = 0;
	memset( m_UnusedTextureCoordinates, 0, sizeof( m_UnusedTextureCoordinates ) );
	m_nVertexShaderIndex = -1;
	m_nPixelShaderIndex = 0;
	m_RasterState = ShaderRasterState_t{};
	m_bRasterOverride = false;
	m_CullMode = MATERIAL_CULLMODE_CCW;
	m_bStencilEnabled = false;
	m_nStencilRef = 0;
	m_nStencilReadMask = 0xff;
	m_nStencilWriteMask = 0xff;
	m_StencilCompare = STENCILCOMPARISONFUNCTION_ALWAYS;
	m_StencilPassOp = STENCILOPERATION_KEEP;
	m_StencilFailOp = STENCILOPERATION_KEEP;
	m_StencilDepthFailOp = STENCILOPERATION_KEEP;
	m_bAlphaToCoverage = false;
	m_bColorWriteOverride = false;
	m_bAlphaWriteOverride = false;
	m_FastIntParams[4] = 0;
	const float mathConstants0[4] = { 0.f, 1.f, 2.f, .5f };
	const float mathConstants1[4] = { 1.f / GAMMA, OVERBRIGHT, 1.f / 3.f, OO_OVERBRIGHT };
	memcpy( m_VsFloat[VERTEX_SHADER_MATH_CONSTANTS0], mathConstants0, sizeof( mathConstants0 ) );
	memcpy( m_VsFloat[VERTEX_SHADER_MATH_CONSTANTS1], mathConstants1, sizeof( mathConstants1 ) );
	++m_ConstantVersions[0];
	m_bTransformsDirty = true;
	m_bFogDirty = true;
	m_nPixelFogRegister = -1;
	SetToneMappingScaleLinear( m_ToneScale );
	SetDefaultState();
}

//-----------------------------------------------------------------------------
// Purpose: Lighting/camera queries used by shaders while building their dynamic state
//-----------------------------------------------------------------------------
void CShaderAPIDX12::GetWorldSpaceCameraPosition( float *pPos ) const
{
	if ( pPos )
		memcpy( pPos, &m_CameraPosition, sizeof( float ) * 3 );
}

int CShaderAPIDX12::GetCurrentNumBones( void ) const
{
	return m_nBoneCount;
}

//-----------------------------------------------------------------------------
// Purpose: SortLocalLights order: spot lights first, then point, then directional
//-----------------------------------------------------------------------------
static inline int LightSortRankDX12( LightType_t type )
{
	return type == MATERIAL_LIGHT_SPOT ? 0 : ( type == MATERIAL_LIGHT_POINT ? 1 : 2 );
}

//-----------------------------------------------------------------------------
// Purpose: DX9 vertex-lit light combo index for the current lights (at most two local lights)
//-----------------------------------------------------------------------------
int CShaderAPIDX12::GetCurrentLightCombo() const
{
	LightState_t state{};
	GetDX9LightState( &state );
	if ( !state.m_nNumLights && !state.m_bAmbientLight )
		return state.m_bStaticLightVertex ? 1 : 0;
	if ( state.m_nNumLights > 2 )
	{
		Warning( "ShaderAPIDX12: legacy lighting combos support at most two local lights\n" );
		return 0;
	}
	int indices[4] = {};
	SortLocalLights( indices );
	const int first = state.m_nNumLights ? LightSortRankDX12( m_Lights[indices[0]].m_Type ) : -1;
	const int second = state.m_nNumLights > 1 ? LightSortRankDX12( m_Lights[indices[1]].m_Type ) : -1;
	static constexpr int combinations[10][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { 2, -1 }, { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 1 }, { 1, 2 }, { 2, 2 } };
	for ( int i = 0; i < 10; ++i )
		if ( combinations[i][0] == first && combinations[i][1] == second )
			return i + 2 + ( state.m_bStaticLightVertex ? 10 : 0 );
	return 0;
}

MaterialFogMode_t CShaderAPIDX12::GetCurrentFogType( void ) const
{
	return m_FogMode;
}

//-----------------------------------------------------------------------------
// Purpose: Fixed-function texture transform and bump-matrix state (stages 0-7)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetTextureTransformDimension( TextureStage_t stage, int dimension, bool projected )
{
	if ( stage < 0 || stage >= 8 )
		return;
	dimension = clamp( dimension, 1, 4 );
	if ( m_TextureTransformEnabled[stage] && m_TextureTransformDimension[stage] == dimension && m_TextureTransformProjected[stage] == projected )
		return;
	m_TextureTransformEnabled[stage] = true;
	m_TextureTransformDimension[stage] = dimension;
	m_TextureTransformProjected[stage] = projected;
	++m_nFixedVSVersion;
}

void CShaderAPIDX12::DisableTextureTransform( TextureStage_t stage )
{
	if ( stage >= 0 && stage < 8 && m_TextureTransformEnabled[stage] )
	{
		m_TextureTransformEnabled[stage] = false;
		++m_nFixedVSVersion;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Assigns one float4 register value
//-----------------------------------------------------------------------------
static inline void SetFloat4DX12( float ( &dst )[4], float x, float y, float z, float w )
{
	dst[0] = x;
	dst[1] = y;
	dst[2] = z;
	dst[3] = w;
}

void CShaderAPIDX12::SetBumpEnvMatrix( TextureStage_t stage, float m00, float m01, float m10, float m11 )
{
	if ( stage >= 0 && stage < 8 )
	{
		SetFloat4DX12( m_BumpMatrices[stage], m00, m01, m10, m11 );
		m_bBumpExtensionDirty = true;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Named shaders resolve lazily at draw time; an index change only marks them dirty
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetVertexShaderIndex( int index )
{
	if ( m_ActiveSnapshot.vertexShaderName.empty() )
		m_bNamedVertexShaderDirty = false;
	else
		m_bNamedVertexShaderDirty |= m_nVertexShaderIndex != index || !m_bBoundVertexShaderIsNamed;
	m_nVertexShaderIndex = index;
}

void CShaderAPIDX12::SetPixelShaderIndex( int index )
{
	if ( m_ActiveSnapshot.pixelShaderName.empty() )
		m_bNamedPixelShaderDirty = false;
	else
		m_bNamedPixelShaderDirty |= m_nPixelShaderIndex != index || !m_bBoundPixelShaderIsNamed;
	m_nPixelShaderIndex = index;
}

//-----------------------------------------------------------------------------
// Purpose: Device and light queries
//-----------------------------------------------------------------------------
void CShaderAPIDX12::GetBackBufferDimensions( int &width, int &height ) const
{
	if ( m_pDevice )
		m_pDevice->GetBackBufferDimensions( width, height );
	else
	{
		width = height = 0;
	}
}

int CShaderAPIDX12::GetMaxLights( void ) const
{
	return 4;
}

const LightDesc_t &CShaderAPIDX12::GetLight( int lightNum ) const
{
	static const LightDesc_t disabled{};
	return lightNum >= 0 && lightNum < static_cast<int>( ARRAYSIZE( m_Lights ) ) ? m_Lights[lightNum] : disabled;
}

bool CShaderAPIDX12::ShouldUsePixelFog() const
{
	return m_FogMode == MATERIAL_FOG_LINEAR_BELOW_FOG_Z ||
	    ( m_FogMode == MATERIAL_FOG_LINEAR && ( !m_pPixelFogConVar || m_pPixelFogConVar->GetBool() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Recomputes fog constants when an input changed and re-issues the fog register writes
//-----------------------------------------------------------------------------
void CShaderAPIDX12::CommitFogState()
{
	const bool pixelFog = ShouldUsePixelFog(), srgbWrite = EffectiveSRGBWrite();
	const HDRType_t hdr = g_pHardwareConfigDX12 ? g_pHardwareConfigDX12->GetHDRType() : HDR_TYPE_NONE;
	if ( !m_bFogDirty && pixelFog == m_bLastPixelFog && srgbWrite == m_bLastFogSRGBWrite && hdr == m_nLastFogHDR )
		return;
	m_bLastPixelFog = pixelFog;
	m_bLastFogSRGBWrite = srgbWrite;
	m_nLastFogHDR = hdr;
	m_bFogDirty = false;
	FogInputsDX12 inputs;
	memset( &inputs, 0, sizeof( inputs ) );
	inputs.start = m_flFogStart;
	inputs.end = m_flFogEnd;
	inputs.z = m_flFogZ;
	inputs.density = m_flFogMaxDensity;
	inputs.camera[0] = m_CameraPosition.x;
	inputs.camera[1] = m_CameraPosition.y;
	inputs.camera[2] = m_CameraPosition.z;
	inputs.tone = m_ToneScale.x;
	inputs.mode = m_FogMode;
	inputs.passFog = m_ActiveSnapshot.fogMode;
	inputs.hdr = hdr;
	memcpy( inputs.color, m_FogColor, sizeof( inputs.color ) );
	inputs.pixelFog = pixelFog;
	inputs.srgbWrite = srgbWrite;
	inputs.gammaDisabled = m_ActiveSnapshot.fogGammaDisabled;
	// Unchanged inputs: the shaders already see these values; re-issue the (compare-filtered) register writes
	// in case other callers overwrote them, but keep the fixed-function constant version stable.
	if ( !m_bFogOutputsValid || memcmp( &inputs, &m_LastFogInputs, sizeof( inputs ) ) )
	{
		ZoneNamedN( fogRecompute, "DX12 FogRecompute", DX12_DRAW_ZONES_ACTIVE );
		m_LastFogInputs = inputs;
		m_bFogOutputsValid = true;
		++m_nFixedPSVersion;
		const float inverseRange = m_flFogEnd != m_flFogStart ? 1.f / ( m_flFogEnd - m_flFogStart ) : 1.f;
		const float density = clamp( m_flFogMaxDensity, 0.f, 1.f );
		SetFloat4DX12( m_FogVertexParams, m_flFogEnd * inverseRange, 1.f, 1.f - density, inverseRange );
		SetFloat4DX12( m_FogCameraParams, m_CameraPosition.x, m_CameraPosition.y, m_CameraPosition.z, m_flFogZ );
		SetFloat4DX12( m_FogPixelParams, 0.f, m_flFogZ, 1.f, 0.f );
		// Only uploaded to a pixel fog register, so computed as if one is bound; register changes need no recompute.
		if ( pixelFog && m_ActiveSnapshot.fogMode != SHADER_FOGMODE_DISABLED )
		{
			m_FogPixelParams[0] = m_FogMode == MATERIAL_FOG_LINEAR_BELOW_FOG_Z ? 0.f : m_flFogStart * inverseRange;
			m_FogPixelParams[2] = m_FogMode == MATERIAL_FOG_LINEAR_BELOW_FOG_Z ? 1.f : density;
			m_FogPixelParams[3] = inverseRange;
		}
		float color[3] = { 0.f, 0.f, 0.f };
		const ShaderFogMode_t passFog = m_ActiveSnapshot.fogMode;
		if ( passFog == SHADER_FOGMODE_FOGCOLOR )
		{
			for ( int i = 0; i < 3; ++i )
				color[i] = m_FogColor[i] / 255.f;
		}
		else if ( passFog == SHADER_FOGMODE_WHITE )
		{
			for ( float &c : color )
				c = 1.f;
		}
		else if ( passFog == SHADER_FOGMODE_GREY || passFog == SHADER_FOGMODE_OO_OVERBRIGHT )
		{
			for ( float &c : color )
				c = 128.f / 255.f;
		}
		const bool correctGamma = !m_ActiveSnapshot.fogGammaDisabled && passFog != SHADER_FOGMODE_BLACK && passFog != SHADER_FOGMODE_WHITE;
		SetFloat4DX12( m_FogPixelColor, 0.f, 0.f, 0.f, 1.f / ( hdr == HDR_TYPE_FLOAT ? 8192.f : 192.f ) );
		for ( int i = 0; i < 3; ++i )
		{
			float fixed = color[i];
			if ( correctGamma )
			{
				if ( srgbWrite )
					fixed = GammaToLinear( fixed );
				if ( hdr == HDR_TYPE_INTEGER )
					fixed *= srgbWrite ? m_ToneScale.x : LinearToGammaFullRange( m_ToneScale.x );
			}
			m_RasterFogColor[i] = clamp( fixed, 0.f, 1.f );
			if ( pixelFog && passFog != SHADER_FOGMODE_DISABLED )
			{
				m_FogPixelColor[i] = ( srgbWrite || m_FogMode == MATERIAL_FOG_LINEAR_BELOW_FOG_Z ) ? GammaToLinear_HardwareSpecific( color[i] ) : color[i];
				if ( correctGamma && hdr == HDR_TYPE_INTEGER )
					m_FogPixelColor[i] *= m_ToneScale.x;
			}
		}
	}
	SetVertexShaderConstant( VERTEX_SHADER_FOG_PARAMS, m_FogVertexParams, 1 );
	SetVertexShaderConstant( VERTEX_SHADER_CAMERA_POS, m_FogCameraParams, 1 );
	if ( m_nPixelFogRegister >= 0 )
		SetPixelShaderConstant( m_nPixelFogRegister, m_FogPixelParams, 1 );
	SetPixelShaderConstant( LINEAR_FOG_COLOR, m_FogPixelColor, 1 );
}

//-----------------------------------------------------------------------------
// Purpose: Binds the pixel fog parameters to a register and restores the cached fog constants
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetPixelShaderFogParams( int reg )
{
	if ( reg < 0 || reg >= static_cast<int>( ARRAYSIZE( m_PsFloat ) ) )
		return;
	// Recompute only when an input changed; otherwise restore the cached values, since the
	// material may have written these registers after the previous commit.
	// Fog outputs do not depend on the register; the new register just receives them below.
	m_nPixelFogRegister = reg;
	const bool recompute = m_bFogDirty || !m_bFogOutputsValid;
	CommitFogState();
	if ( !recompute && m_bFogOutputsValid )
	{
		SetVertexShaderConstant( VERTEX_SHADER_FOG_PARAMS, m_FogVertexParams, 1 );
		SetVertexShaderConstant( VERTEX_SHADER_CAMERA_POS, m_FogCameraParams, 1 );
		SetPixelShaderConstant( m_nPixelFogRegister, m_FogPixelParams, 1 );
		SetPixelShaderConstant( LINEAR_FOG_COLOR, m_FogPixelColor, 1 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Ambient cube upload to the vertex / pixel registers (and the native PS engine block)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetVertexShaderStateAmbientLightCube()
{
	SetVertexShaderConstant( VERTEX_SHADER_AMBIENT_LIGHT, m_AmbientCube[0], 6 );
}

void CShaderAPIDX12::SetPixelShaderStateAmbientLightCube( int reg, bool forceBlack )
{
	static constexpr float black[24]{};
	const float *cube = forceBlack ? black : m_AmbientCube[0];
	SetPixelShaderConstant( reg, cube, 6 );
	// Native pixel shaders read the same values from DX12PSEngine.cAmbientCube.
	if ( memcmp( m_NativePSEngine.cAmbientCube, cube, sizeof( m_NativePSEngine.cAmbientCube ) ) )
	{
		memcpy( m_NativePSEngine.cAmbientCube, cube, sizeof( m_NativePSEngine.cAmbientCube ) );
		++m_nNativePSEngineVersion;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Indices of the enabled lights, spot lights first, then point, then directional (stable)
//-----------------------------------------------------------------------------
int CShaderAPIDX12::SortLocalLights( int ( &indices )[4] ) const
{
	int count = 0;
	for ( int i = 0; i < static_cast<int>( ARRAYSIZE( m_Lights ) ); ++i )
	{
		const LightType_t type = m_Lights[i].m_Type;
		if ( type != MATERIAL_LIGHT_POINT && type != MATERIAL_LIGHT_SPOT && type != MATERIAL_LIGHT_DIRECTIONAL )
			continue;
		int position = count;
		while ( position > 0 && LightSortRankDX12( m_Lights[indices[position - 1]].m_Type ) > LightSortRankDX12( type ) )
		{
			indices[position] = indices[position - 1];
			--position;
		}
		indices[position] = i;
		++count;
	}
	return count;
}

//-----------------------------------------------------------------------------
// Purpose: Uploads the fixed-function/vertex-lit light registers when the light state changed
//-----------------------------------------------------------------------------
void CShaderAPIDX12::CommitVertexLighting()
{
	if ( !m_bLightingDirty )
		return;
	int indices[4] = {};
	const int count = SortLocalLights( indices );
	for ( int i = 0; i < count; ++i )
	{
		const LightDesc_t &light = m_Lights[indices[i]];
		float constants[5][4]{};
		memcpy( constants[0], light.m_Color.Base(), 3 * sizeof( float ) );
		constants[0][3] = light.m_Type == MATERIAL_LIGHT_DIRECTIONAL ? 1.f : 0.f;
		if ( light.m_Type != MATERIAL_LIGHT_POINT )
			memcpy( constants[1], light.m_Direction.Base(), 3 * sizeof( float ) );
		constants[1][3] = light.m_Type == MATERIAL_LIGHT_SPOT ? 1.f : 0.f;
		if ( light.m_Type != MATERIAL_LIGHT_DIRECTIONAL )
			memcpy( constants[2], light.m_Position.Base(), 3 * sizeof( float ) );
		constants[2][3] = 1.f;
		if ( light.m_Type == MATERIAL_LIGHT_SPOT )
		{
			const float phi = MIN( light.m_Phi, float( M_PI ) );
			const float theta = MIN( light.m_Theta, phi - .001f );
			constants[3][0] = light.m_Falloff;
			constants[3][1] = cosf( theta * .5f );
			constants[3][2] = cosf( phi * .5f );
			constants[3][3] = constants[3][1] > constants[3][2] ? 1.f / ( constants[3][1] - constants[3][2] ) : 0.f;
		}
		else
		{
			constants[3][1] = constants[3][2] = constants[3][3] = 1.f;
		}
		constants[4][0] = light.m_Attenuation0;
		constants[4][1] = light.m_Attenuation1;
		constants[4][2] = light.m_Attenuation2;
		SetVertexShaderConstant( VERTEX_SHADER_LIGHTS + i * 5, constants[0], 5 );
	}
	const int loop[4] = { count, 0, 1, 0 };
	SetIntegerVertexShaderConstant( 0, loop, 1 );
	BOOL enabled[4] = { count > 0, count > 1, count > 2, count > 3 };
	SetBooleanVertexShaderConstant( VERTEX_SHADER_LIGHT_ENABLE_BOOL_CONST, enabled, 4 );
	m_bLightingDirty = false;
}

//-----------------------------------------------------------------------------
// Purpose: Uploads the pixel-shader light registers (and the native PS engine block)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::CommitPixelShaderLighting( int reg )
{
	int indices[4] = {};
	const int count = SortLocalLights( indices );
	float constants[6][4]{};
	for ( int i = 0; i < count; ++i )
	{
		const LightDesc_t &light = m_Lights[indices[i]];
		const Vector position = light.m_Type == MATERIAL_LIGHT_DIRECTIONAL ? m_LightingOrigin - light.m_Direction * 10000.f : light.m_Position;
		if ( i < 3 )
		{
			memcpy( constants[i * 2], light.m_Color.Base(), 3 * sizeof( float ) );
			memcpy( constants[i * 2 + 1], position.Base(), 3 * sizeof( float ) );
		}
		else
			for ( int component = 0; component < 3; ++component )
			{
				constants[component][3] = light.m_Color[component];
				constants[component + 3][3] = position[component];
			}
	}
	SetPixelShaderConstant( reg, constants[0], 6 );
	// Native pixel shaders read the same values from DX12PSEngine.cLightInfo.
	if ( memcmp( m_NativePSEngine.cLightInfo, constants, sizeof( m_NativePSEngine.cLightInfo ) ) )
	{
		memcpy( m_NativePSEngine.cLightInfo, constants, sizeof( m_NativePSEngine.cLightInfo ) );
		++m_nNativePSEngineVersion;
	}
}

CMeshBuilder *CShaderAPIDX12::GetVertexModifyBuilder()
{
	return &m_VertexModifyBuilder;
}

const FlashlightState_t &CShaderAPIDX12::GetFlashlightState( VMatrix &worldToTexture ) const
{
	worldToTexture = m_FlashlightMatrix;
	return m_Flashlight;
}

bool CShaderAPIDX12::InFlashlightMode() const
{
	return m_pShaderUtil && m_pShaderUtil->InFlashlightMode();
}

bool CShaderAPIDX12::InEditorMode() const
{
	return m_bEditorMode;
}

MorphFormat_t CShaderAPIDX12::GetBoundMorphFormat()
{
	return m_pShaderUtil ? m_pShaderUtil->GetBoundMorphFormat() : 0;
}

//-----------------------------------------------------------------------------
// Purpose: Binds an engine standard texture: a registered handle, else the shader utility's binding
//-----------------------------------------------------------------------------
void CShaderAPIDX12::BindStandardTexture( Sampler_t sampler, StandardTextureId_t id )
{
	if ( id < 0 || id >= TEXTURE_MAX_STD_TEXTURES || sampler < 0 || sampler >= static_cast<int>( ARRAYSIZE( m_BoundTextures ) ) )
		return;
	const ShaderAPITextureHandle_t handle = m_StandardTextures[id];
	if ( handle != INVALID_SHADERAPI_TEXTURE_HANDLE )
		BindTexture( sampler, handle );
	else if ( m_pShaderUtil )
		m_pShaderUtil->BindStandardTexture( sampler, id );
}

//-----------------------------------------------------------------------------
// Purpose: Updates the tone-mapping scale constant; fog is recomputed only when the scale changed
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetToneMappingScaleLinear( const Vector &scale )
{
	const HDRType_t hdr = g_pHardwareConfigDX12 ? g_pHardwareConfigDX12->GetHDRType() : HDR_TYPE_NONE;
	const Vector next( hdr == HDR_TYPE_NONE ? 1.f : scale.x, GetLightMapScaleFactor(), hdr == HDR_TYPE_INTEGER ? MAX_HDR_OVERBRIGHT : 1.f );
	// Fog output and the gamma-space term depend only on these values; skip recomputation when unchanged.
	if ( !m_bToneScaleConstantValid || next != m_ToneScale )
	{
		m_ToneScale = next;
		m_bFogDirty = true;
		m_flToneScaleGamma = LinearToGammaFullRange( m_ToneScale.x );
		m_bToneScaleConstantValid = true;
	}
	const float constants[4] = { m_ToneScale.x, m_ToneScale.y, m_ToneScale.z, m_flToneScaleGamma };
	SetPixelShaderConstant( TONE_MAPPING_SCALE_PSH_CONSTANT, constants, 1 );
}

const Vector &CShaderAPIDX12::GetToneMappingScaleLinear( void ) const
{
	return m_ToneScale;
}

float CShaderAPIDX12::GetLightMapScaleFactor() const
{
	const HDRType_t hdr = g_pHardwareConfigDX12 ? g_pHardwareConfigDX12->GetHDRType() : HDR_TYPE_NONE;
	return hdr == HDR_TYPE_FLOAT ? 1.f : ( hdr == HDR_TYPE_INTEGER ? MAX_HDR_OVERBRIGHT : GammaToLinearFullRange( 2.f ) );
}

//-----------------------------------------------------------------------------
// Purpose: Loads a 3x4 bone matrix into the model matrix palette
//-----------------------------------------------------------------------------
void CShaderAPIDX12::LoadBoneMatrix( int boneIndex, const float *m )
{
	if ( !m || boneIndex < 0 || boneIndex >= NUM_MODEL_TRANSFORMS )
		return;
	VMatrix &bone = m_Matrices[MATERIAL_MODEL + boneIndex];
	bone.Identity();
	memcpy( bone.Base(), m, 12 * sizeof( float ) );
	m_nMaxBoneLoaded = MAX( m_nMaxBoneLoaded, boneIndex );
	m_nMotionBoneRows = MAX( m_nMotionBoneRows, boneIndex + 1 );
	m_bTransformsDirty = true;
	if ( boneIndex == 0 )
		m_MatrixMode = MATERIAL_MODEL;
}

void CShaderAPIDX12::PerspectiveOffCenterX( double fovx, double aspect, double zNear, double zFar, double bottom, double top, double left, double right )
{
	VMatrix projection;
	MatrixBuildPerspectiveOffCenterX( projection, fovx, aspect, zNear, zFar, bottom, top, left, right );
	m_Matrices[m_MatrixMode] = m_Matrices[m_MatrixMode] * projection;
	MatrixChanged();
}

void CShaderAPIDX12::SetFloatRenderingParameter( int parm_number, float value )
{
	if ( parm_number >= 0 && parm_number < (int)ARRAYSIZE( m_RenderingFloats ) )
		m_RenderingFloats[parm_number] = value;
}

//-----------------------------------------------------------------------------
// Stencil state
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetStencilEnable( bool onoff )
{
	m_bStencilEnabled = onoff;
}

void CShaderAPIDX12::SetStencilFailOperation( StencilOperation_t op )
{
	m_StencilFailOp = op;
}

void CShaderAPIDX12::SetStencilZFailOperation( StencilOperation_t op )
{
	m_StencilDepthFailOp = op;
}

void CShaderAPIDX12::SetStencilPassOperation( StencilOperation_t op )
{
	m_StencilPassOp = op;
}

void CShaderAPIDX12::SetStencilCompareFunction( StencilComparisonFunction_t cmpfn )
{
	m_StencilCompare = cmpfn;
}

void CShaderAPIDX12::SetStencilReferenceValue( int ref )
{
	m_nStencilRef = ref;
}

void CShaderAPIDX12::SetStencilTestMask( uint32 msk )
{
	m_nStencilReadMask = static_cast<uint8_t>( msk );
}

void CShaderAPIDX12::SetStencilWriteMask( uint32 msk )
{
	m_nStencilWriteMask = static_cast<uint8_t>( msk );
}

void CShaderAPIDX12::GetDXLevelDefaults( uint &max_dxlevel, uint &recommended_dxlevel )
{
	max_dxlevel = 95;
	recommended_dxlevel = 95;
}

const FlashlightState_t &CShaderAPIDX12::GetFlashlightStateEx( VMatrix &worldToTexture, ITexture **pFlashlightDepthTexture ) const
{
	worldToTexture = m_FlashlightMatrix;
	if ( pFlashlightDepthTexture )
		*pFlashlightDepthTexture = m_pFlashlightDepthTexture;
	return m_Flashlight;
}

float CShaderAPIDX12::GetAmbientLightCubeLuminance()
{
	float luminance = 0.f;
	for ( const float( &face )[4] : m_AmbientCube )
		luminance += .3f * face[0] + .59f * face[1] + .11f * face[2];
	return luminance / 6.f;
}

//-----------------------------------------------------------------------------
// Purpose: DX9-style light state summary (local light count, ambient, static vertex lighting)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::GetDX9LightState( LightState_t *state ) const
{
	if ( !state )
		return;
	*state = {};
	int indices[4] = {};
	state->m_nNumLights = SortLocalLights( indices );
	for ( const float( &face )[4] : m_AmbientCube )
		if ( face[0] != 0.f || face[1] != 0.f || face[2] != 0.f )
		{
			state->m_bAmbientLight = true;
			break;
		}
	state->m_bStaticLightVertex = m_pRenderMesh && m_pRenderMesh->ColorMesh() != nullptr;
}

int CShaderAPIDX12::GetPixelFogCombo()
{
	return ShouldUsePixelFog() && m_FogMode == MATERIAL_FOG_LINEAR_BELOW_FOG_Z ? 1 : 0;
}

void CShaderAPIDX12::BindStandardVertexTexture( VertexTextureSampler_t sampler, StandardTextureId_t id )
{
	if ( id >= 0 && id < TEXTURE_MAX_STD_TEXTURES && sampler >= 0 && sampler < static_cast<int>( ARRAYSIZE( m_VertexTextures ) ) && m_pShaderUtil )
		m_pShaderUtil->BindStandardVertexTexture( sampler, id );
}

bool CShaderAPIDX12::IsHWMorphingEnabled() const
{
	return m_bMorphing;
}

void CShaderAPIDX12::GetStandardTextureDimensions( int *width, int *height, StandardTextureId_t id )
{
	if ( width )
		*width = 0;
	if ( height )
		*height = 0;
	if ( m_pShaderUtil && id >= 0 && id < TEXTURE_MAX_STD_TEXTURES )
		m_pShaderUtil->GetStandardTextureDimensions( width, height, id );
}

//-----------------------------------------------------------------------------
// Integer and boolean shader registers; a changed bank bumps its constant version
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetBooleanVertexShaderConstant( int var, BOOL const *values, int count, bool )
{
	if ( !values || var < 0 || count <= 0 )
		return;
	bool changed = false;
	for ( int i = 0; i < count && var + i < (int)ARRAYSIZE( m_VsBool ); ++i )
	{
		const bool value = values[i] != FALSE;
		if ( m_VsBool[var + i] != value )
		{
			m_VsBool[var + i] = value;
			changed = true;
		}
	}
	if ( changed )
		++m_ConstantVersions[2];
}

void CShaderAPIDX12::SetIntegerVertexShaderConstant( int var, int const *values, int count, bool )
{
	if ( !values || var < 0 || count <= 0 )
		return;
	bool changed = false;
	for ( int i = 0; i < count && var + i < (int)ARRAYSIZE( m_VsInt ); ++i )
		if ( memcmp( values + i * 4, m_VsInt[var + i], sizeof( int ) * 4 ) )
		{
			memcpy( m_VsInt[var + i], values + i * 4, sizeof( int ) * 4 );
			changed = true;
		}
	if ( changed )
		++m_ConstantVersions[1];
}

void CShaderAPIDX12::SetBooleanPixelShaderConstant( int var, BOOL const *values, int count, bool )
{
	if ( !values || var < 0 || count <= 0 )
		return;
	bool changed = false;
	for ( int i = 0; i < count && var + i < (int)ARRAYSIZE( m_PsBool ); ++i )
	{
		const bool value = values[i] != FALSE;
		if ( m_PsBool[var + i] != value )
		{
			m_PsBool[var + i] = value;
			changed = true;
		}
	}
	if ( changed )
		++m_ConstantVersions[5];
}

void CShaderAPIDX12::SetIntegerPixelShaderConstant( int var, int const *values, int count, bool )
{
	if ( !values || var < 0 || count <= 0 )
		return;
	bool changed = false;
	for ( int i = 0; i < count && var + i < (int)ARRAYSIZE( m_PsInt ); ++i )
		if ( memcmp( values + i * 4, m_PsInt[var + i], sizeof( int ) * 4 ) )
		{
			memcpy( m_PsInt[var + i], values + i * 4, sizeof( int ) * 4 );
			changed = true;
		}
	if ( changed )
		++m_ConstantVersions[4];
}

bool CShaderAPIDX12::ShouldWriteDepthToDestAlpha() const
{
	return m_FogMode != MATERIAL_FOG_LINEAR_BELOW_FOG_Z && GetIntRenderingParameter( INT_RENDERPARM_WRITE_DEPTH_TO_DESTALPHA ) != 0;
}

void CShaderAPIDX12::PushDeformation( DeformationBase_t const *deformation )
{
	if ( deformation && deformation->m_eType == DEFORMATION_CLAMP_TO_BOX_IN_WORLDSPACE )
		m_Deformations.AddToTail( *static_cast<const BoxDeformation_t *>( deformation ) );
	else if ( deformation )
		Warning( "ShaderAPIDX12: unsupported deformation type %d\n", deformation->m_eType );
}

void CShaderAPIDX12::PopDeformation()
{
	if ( !( m_Deformations.Count() == 0 ) )
		m_Deformations.RemoveMultipleFromTail( 1 );
}

int CShaderAPIDX12::GetNumActiveDeformations() const
{
	return static_cast<int>( m_Deformations.Count() );
}

void CShaderAPIDX12::SetStandardTextureHandle( StandardTextureId_t id, ShaderAPITextureHandle_t handle )
{
	if ( id >= 0 && id < TEXTURE_MAX_STD_TEXTURES )
		m_StandardTextures[id] = handle;
}

//-----------------------------------------------------------------------------
// Purpose: Unaligned reads of command buffer operands
//-----------------------------------------------------------------------------
static inline int ReadCommandIntDX12( const uint8 *pData )
{
	int nValue;
	memcpy( &nValue, pData, sizeof( nValue ) );
	return nValue;
}

static inline uint8 *ReadCommandPointerDX12( const uint8 *pData )
{
	uint8 *pValue;
	memcpy( &pValue, pData, sizeof( pValue ) );
	return pValue;
}

//-----------------------------------------------------------------------------
// Purpose: Interprets a material command buffer (CBCMD_* opcodes, nested JSR up to 20 deep)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ExecuteCommandBuffer( uint8 *buffer )
{
	ZoneNamedN( commandDispatch, "DX12 ExecuteCommandBuffer sampled", DX12_ZONES_ACTIVE && ( m_nFrameCounter & 63 ) == 0 );
	if ( !buffer )
		return;
	uint8 *returns[20];
	int depth = 0;
	size_t steps = 0;
	for ( uint8 *pc = buffer; pc && steps++ < 1000000; )
	{
		int opcode = ReadCommandIntDX12( pc );
		pc += sizeof( int );
		switch ( opcode )
		{
		case CBCMD_END:
			if ( !depth )
				return;
			pc = returns[--depth];
			break;
		case CBCMD_JUMP:
			pc = ReadCommandPointerDX12( pc );
			break;
		case CBCMD_JSR:
			if ( depth == 20 )
			{
				Warning( "ShaderAPIDX12: command buffer call stack overflow\n" );
				return;
			}
			returns[depth++] = pc + sizeof( void * );
			pc = ReadCommandPointerDX12( pc );
			break;
		case CBCMD_SET_PIXEL_SHADER_FLOAT_CONST:
		case CBCMD_SET_VERTEX_SHADER_FLOAT_CONST:
		{
			int reg = ReadCommandIntDX12( pc ), count = ReadCommandIntDX12( pc + sizeof( int ) );
			pc += 2 * sizeof( int );
			if ( count < 0 || count > 256 )
			{
				Warning( "ShaderAPIDX12: invalid command buffer constant count\n" );
				return;
			}
			if ( opcode == CBCMD_SET_PIXEL_SHADER_FLOAT_CONST )
				SetPixelShaderConstant( reg, reinterpret_cast<const float *>( pc ), count );
			else
				SetVertexShaderConstant( reg, reinterpret_cast<const float *>( pc ), count );
			pc += static_cast<size_t>( count ) * 4 * sizeof( float );
			break;
		}
		case CBCMD_SET_VERTEX_SHADER_FLOAT_CONST_REF:
		{
			int reg = ReadCommandIntDX12( pc ), count = ReadCommandIntDX12( pc + sizeof( int ) );
			pc += 2 * sizeof( int );
			const float *values = nullptr;
			memcpy( &values, pc, sizeof( values ) );
			pc += sizeof( values );
			if ( count < 0 || count > 256 )
				return;
			SetVertexShaderConstant( reg, values, count );
			break;
		}
		case CBCMD_SETPIXELSHADERFOGPARAMS:
			SetPixelShaderFogParams( ReadCommandIntDX12( pc ) );
			pc += sizeof( int );
			break;
		case CBCMD_STORE_EYE_POS_IN_PSCONST:
		{
			float eye[4] = { m_CameraPosition.x, m_CameraPosition.y, m_CameraPosition.z, 1.f };
			SetPixelShaderConstant( ReadCommandIntDX12( pc ), eye, 1 );
			pc += sizeof( int );
			break;
		}
		case CBCMD_COMMITPIXELSHADERLIGHTING:
			CommitPixelShaderLighting( ReadCommandIntDX12( pc ) );
			pc += sizeof( int );
			break;
		case CBCMD_SETPIXELSHADERSTATEAMBIENTLIGHTCUBE:
			SetPixelShaderStateAmbientLightCube( ReadCommandIntDX12( pc ), false );
			pc += sizeof( int );
			break;
		case CBCMD_SETAMBIENTCUBEDYNAMICSTATEVERTEXSHADER:
			SetVertexShaderStateAmbientLightCube();
			break;
		case CBCMD_SET_DEPTH_FEATHERING_CONST:
		{
			int reg = ReadCommandIntDX12( pc );
			float scale;
			memcpy( &scale, pc + sizeof( int ), sizeof( scale ) );
			pc += sizeof( int ) + sizeof( scale );
			SetDepthFeatheringPixelShaderConstant( reg, scale );
			break;
		}
		case CBCMD_BIND_STANDARD_TEXTURE:
		{
			int sampler = ReadCommandIntDX12( pc ), id = ReadCommandIntDX12( pc + sizeof( int ) );
			pc += 2 * sizeof( int );
			if ( id >= 0 && id < TEXTURE_MAX_STD_TEXTURES )
				BindStandardTexture( static_cast<Sampler_t>( sampler ), static_cast<StandardTextureId_t>( id ) );
			break;
		}
		case CBCMD_BIND_SHADERAPI_TEXTURE_HANDLE:
		{
			int sampler = ReadCommandIntDX12( pc );
			pc += sizeof( int );
			ShaderAPITextureHandle_t handle;
			memcpy( &handle, pc, sizeof( handle ) );
			pc += sizeof( handle );
			BindTexture( static_cast<Sampler_t>( sampler ), handle );
			break;
		}
		case CBCMD_SET_PSHINDEX:
			SetPixelShaderIndex( ReadCommandIntDX12( pc ) );
			pc += sizeof( int );
			break;
		case CBCMD_SET_VSHINDEX:
			SetVertexShaderIndex( ReadCommandIntDX12( pc ) );
			pc += sizeof( int );
			break;
		default:
			Warning( "ShaderAPIDX12: unsupported material command opcode %d\n", opcode );
			return;
		}
	}
	Warning( "ShaderAPIDX12: command buffer exceeded execution limit\n" );
}

//-----------------------------------------------------------------------------
// Purpose: Packs the active box deformations selected by mask into float4 constants
//-----------------------------------------------------------------------------
int CShaderAPIDX12::GetPackedDeformationInformation( int mask, float *constants, int byteCount, int maximum, int *combos ) const
{
	if ( maximum < 0 || byteCount < 0 )
		return 0;
	if ( combos )
		memset( combos, 0, maximum * sizeof( int ) );
	if ( !constants || !combos )
		return 0;
	int found = 0;
	constexpr int floats = 16;
	for ( const BoxDeformation_t *it = m_Deformations.begin(); it != m_Deformations.end() && found < maximum; ++it )
	{
		if ( !( static_cast<unsigned int>( mask ) & ( 1u << static_cast<unsigned int>( it->m_eType ) ) ) || byteCount < static_cast<int>( floats * sizeof( float ) ) )
			continue;
		const float values[floats] = { it->m_SourceMins.x, it->m_SourceMins.y, it->m_SourceMins.z, it->m_flPad0, it->m_SourceMaxes.x, it->m_SourceMaxes.y, it->m_SourceMaxes.z, it->m_flPad1, it->m_ClampMins.x, it->m_ClampMins.y, it->m_ClampMins.z, it->m_flPad2, it->m_ClampMaxes.x, it->m_ClampMaxes.y, it->m_ClampMaxes.z, it->m_flPad3 };
		memcpy( constants, values, sizeof( values ) );
		constants += floats;
		byteCount -= sizeof( values );
		combos[found++] = it->m_eType;
	}
	return found;
}

void CShaderAPIDX12::MarkUnusedVertexFields( unsigned int flags, int count, bool *unused )
{
	m_nUnusedVertexFields = flags;
	memset( m_UnusedTextureCoordinates, 0, sizeof( m_UnusedTextureCoordinates ) );
	if ( unused && count > 0 )
		memcpy( m_UnusedTextureCoordinates, unused, MIN( count, static_cast<int>( ARRAYSIZE( m_UnusedTextureCoordinates ) ) ) * sizeof( bool ) );
}

//-----------------------------------------------------------------------------
// Purpose: Color correction weights from the shader utility, or the identity default
//-----------------------------------------------------------------------------
void CShaderAPIDX12::GetCurrentColorCorrection( ShaderColorCorrectionInfo_t *info )
{
	if ( !info )
		return;
	if ( m_pShaderUtil )
		m_pShaderUtil->GetCurrentColorCorrection( info );
	else
	{
		*info = {};
		info->m_flDefaultWeight = 1.f;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Uploads (near, far) distances derived from the projection matrix
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetPSNearAndFarZ( int reg )
{
	const VMatrix &projection = m_Matrices[MATERIAL_PROJECTION];
	const float z = projection[2][2], w = projection[2][3];
	if ( z == 0.f )
		return;
	const float nearDistance = w / z;
	if ( nearDistance + w == 0.f )
		return;
	const float farDistance = w * nearDistance / ( nearDistance + w );
	float values[4] = { nearDistance, farDistance, 0.f, 0.f };
	SetPixelShaderConstant( reg, values, 1 );
}

//-----------------------------------------------------------------------------
// Purpose: shaderapidx8.cpp:5225-5253 (PC): x = dest-alpha depth range / scale (8192 float HDR, else 192), yzw = 0.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetDepthFeatheringPixelShaderConstant( int reg, float scale )
{
	const HDRType_t hdr = g_pHardwareConfigDX12 ? g_pHardwareConfigDX12->GetHDRType() : HDR_TYPE_NONE;
	const float values[4] = { ( hdr == HDR_TYPE_FLOAT ? 8192.f : 192.f ) / scale, 0.f, 0.f, 0.f };
	SetPixelShaderConstant( reg, values, 1 );
}

int CShaderAPIDX12::GetPixelFogCombo1( bool supportsRadial )
{
	return !ShouldUsePixelFog() ? 0 : ( m_FogMode == MATERIAL_FOG_LINEAR_BELOW_FOG_Z ? 1 : ( supportsRadial && m_bFogRadial ? 2 : 0 ) );
}

void CShaderAPIDX12::ClearColor3ub( unsigned char r, unsigned char g, unsigned char b )
{
	ClearColor4ub( r, g, b, 255 );
}

void CShaderAPIDX12::ClearColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	m_ClearColor[0] = r / 255.0f;
	m_ClearColor[1] = g / 255.0f;
	m_ClearColor[2] = b / 255.0f;
	m_ClearColor[3] = a / 255.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Releases every device-owned resource; safe to call repeatedly and before initialization
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ShutdownDeviceResources()
{
	SetShaderPrecacheAccepting( false );
	ProcessPendingTextureDeletes();
	if ( m_pDevice && m_pDevice->IsRecordingOwner() && m_pDevice->CommandList() )
		m_pDevice->Submit( true );
	ReleaseTextureDeviceResources();
	for ( OcclusionQueryDX12 *query : m_OcclusionQueries )
		delete query;
	m_OcclusionQueries.RemoveAll();
	m_Pipeline.Shutdown();
	for ( CMeshDX12 *mesh : m_DynamicMeshes )
	{
		mesh->Vertices().NativeResourceRef().Reset();
		mesh->Indices().NativeResourceRef().Reset();
	}
	if ( m_pFlexMesh )
	{
		m_pFlexMesh->Vertices().NativeResourceRef().Reset();
		m_pFlexMesh->Indices().NativeResourceRef().Reset();
	}
	for ( CachedResourceDescDX12 &cached : m_TargetDescs )
		cached = CachedResourceDescDX12{};
	++m_nNamedResolveEpoch;
	++m_nPipelineMemoEpoch;
	SetDevice( nullptr );
}

//-----------------------------------------------------------------------------
// Purpose: Binds the shader API to a freshly initialized device and creates the pipeline caches
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::InitializeDeviceResources( CShaderDeviceDX12 *pDevice )
{
	ShutdownDeviceResources();
	SetDevice( pDevice );
	SetShaderUtil( g_pShaderDeviceMgrDX12 ? g_pShaderDeviceMgrDX12->HostShaderUtil() : nullptr );
	ConVarRef pixelFog( "r_pixelfog", true );
	m_pPixelFogConVar = pixelFog.IsValid() ? static_cast<ConVar *>( pixelFog.GetLinkedConVar() ) : nullptr;
	m_bFogDirty = true;
	if ( !pDevice->NativeDevice() || !m_Pipeline.Initialize( pDevice->NativeDevice() ) )
	{
		Warning( "ShaderAPIDX12: pipeline initialization failed\n" );
		ShutdownDeviceResources();
		return false;
	}
	SetShaderPrecacheAccepting( true );
	return true;
}

bool CShaderAPIDX12::SetMode( void *hwnd, int adapter, const ShaderDeviceInfo_t &info )
{
	return g_pShaderDeviceMgrDX12 && g_pShaderDeviceMgrDX12->SetMode( hwnd, adapter, info ) != nullptr;
}

void CShaderAPIDX12::BindVertexShader( VertexShaderHandle_t shader )
{
	m_hBoundVS = shader;
	m_bNamedVertexShaderDirty = false;
	m_bBoundVertexShaderIsNamed = false;
}

void CShaderAPIDX12::BindGeometryShader( GeometryShaderHandle_t hGeometryShader )
{
	m_hBoundGS = hGeometryShader;
}

void CShaderAPIDX12::BindPixelShader( PixelShaderHandle_t shader )
{
	m_hBoundPS = shader;
	m_bNamedPixelShaderDirty = false;
	m_bBoundPixelShaderIsNamed = false;
}

void CShaderAPIDX12::SetRasterState( const ShaderRasterState_t &state )
{
	m_RasterState = state;
	m_bRasterOverride = true;
	m_CullMode = state.m_CullMode;
}

void CShaderAPIDX12::ChangeVideoMode( const ShaderDeviceInfo_t &info )
{
	if ( m_pDevice && !m_pDevice->ChangeMode( info ) )
		Warning( "ShaderAPIDX12: video mode change failed\n" );
}

//-----------------------------------------------------------------------------
// Purpose: Captures the shadow state (CShaderShadowDX12) into an append-only snapshot
//-----------------------------------------------------------------------------
StateSnapshot_t CShaderAPIDX12::TakeSnapshot()
{
	if ( m_Snapshots.Count() >= 32767 )
		return static_cast<StateSnapshot_t>( -1 );
	Snapshot snapshot = m_ShadowState;
	if ( g_pShaderShadowDX12 )
	{
		const CShaderShadowDX12 &shadow = *g_pShaderShadowDX12;
		snapshot.translucent = shadow.Blending();
		snapshot.alphaTest = shadow.AlphaTest();
		snapshot.depthWrite = shadow.DepthWrites();
		snapshot.depthTest = shadow.DepthTest();
		snapshot.colorWrites = shadow.ColorWrites();
		snapshot.alphaWrites = shadow.AlphaWrites();
		snapshot.culling = shadow.CullEnabled();
		snapshot.stencil = shadow.StencilEnabled();
		snapshot.alphaToCoverage = shadow.AlphaToCoverage();
		snapshot.fogMode = shadow.FogModeValue();
		snapshot.fogGammaDisabled = shadow.FogGammaDisabled();
		snapshot.srgbWrite = shadow.SRGBWrite();
		snapshot.srgbReadMask = shadow.SRGBReadMask();
		snapshot.alphaReference = shadow.AlphaReference();
		snapshot.alphaFunction = shadow.AlphaFunction();
		snapshot.blendSource = shadow.BlendSource();
		snapshot.blendDestination = shadow.BlendDestination();
		snapshot.separateAlpha = shadow.SeparateAlphaBlending();
		snapshot.blendAlphaSource = shadow.BlendAlphaSource();
		snapshot.blendAlphaDestination = shadow.BlendAlphaDestination();
		snapshot.blendOperation = shadow.BlendOperation();
		snapshot.blendAlphaOperation = shadow.BlendAlphaOperation();
		snapshot.stencilFunction = shadow.StencilFunction();
		snapshot.stencilFail = shadow.StencilFailOperation();
		snapshot.stencilDepthFail = shadow.StencilDepthFailOperation();
		snapshot.stencilPass = shadow.StencilPassOperation();
		snapshot.stencilReadMask = shadow.StencilTestMask();
		snapshot.stencilWriteMask = shadow.StencilWriteMask();
		snapshot.stencilReference = shadow.StencilReferenceValue();
		snapshot.depthFunction = shadow.DepthFunction();
		snapshot.vertexShaderName = InternShaderNameDX12( shadow.VertexShaderName().Get() );
		snapshot.pixelShaderName = InternShaderNameDX12( shadow.PixelShaderName().Get() );
		snapshot.staticVertexIndex = shadow.StaticVertexIndex();
		snapshot.staticPixelIndex = shadow.StaticPixelIndex();
		snapshot.shaders = !snapshot.vertexShaderName.empty() && !snapshot.pixelShaderName.empty();
		snapshot.format = shadow.VertexFormat();
		snapshot.usage = snapshot.format;
		snapshot.morph = shadow.MorphFormat();
		snapshot.comparisonSamplerMask = shadow.ComparisonSamplerMask();
		snapshot.polygonOffset = shadow.PolyOffset();
		snapshot.polyFront = shadow.PolyModeFront();
		snapshot.polyBack = shadow.PolyModeBack();
		FixedFunctionStateDX12 &ff = snapshot.fixed;
		ff.drawFlags = shadow.DrawFlagsValue();
		ff.customPipe = shadow.CustomPixelPipe();
		ff.lighting = shadow.Lighting();
		ff.specular = shadow.Specular();
		ff.vertexBlend = shadow.VertexBlend();
		ff.constantColor = shadow.ConstantColor();
		ff.alphaPipe = shadow.AlphaPipe();
		ff.constantAlpha = shadow.ConstantAlpha();
		ff.vertexAlpha = shadow.VertexAlpha();
		ff.materialSource = shadow.DiffuseMaterialSource();
		ff.fogMode = shadow.FogModeValue();
		ff.texCoordCount = shadow.CustomTextureStageCount();
		ff.alphaTest = snapshot.alphaTest || ( snapshot.translucent && snapshot.blendSource == SHADER_BLEND_SRC_ALPHA && snapshot.blendDestination == SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
		ff.alphaFunction = snapshot.alphaTest ? snapshot.alphaFunction : SHADER_ALPHAFUNC_GEQUAL;
		if ( !snapshot.alphaTest && ff.alphaTest )
			snapshot.alphaReference = 1.f / 255.f;
		if ( snapshot.alphaToCoverage && ( snapshot.alphaTest == false || snapshot.translucent ) )
			snapshot.alphaToCoverage = false;
		for ( int stage = 0; stage < 16; ++stage )
		{
			ff.textureEnabled[stage] = shadow.TextureEnabled( stage );
			ff.texgen[stage] = shadow.TexGenEnabled( stage );
			ff.textureAlpha[stage] = shadow.TextureAlphaEnabled( stage );
			ff.texgenParam[stage] = shadow.TexGenParam( stage );
			ff.overbright[stage] = shadow.Overbright( stage );
			ff.colorOp[stage] = shadow.TextureOperation( stage, SHADER_TEXCHANNEL_COLOR );
			ff.alphaOp[stage] = shadow.TextureOperation( stage, SHADER_TEXCHANNEL_ALPHA );
			ff.colorArg1[stage] = shadow.TextureArgument( stage, SHADER_TEXCHANNEL_COLOR, 0 );
			ff.colorArg2[stage] = shadow.TextureArgument( stage, SHADER_TEXCHANNEL_COLOR, 1 );
			ff.alphaArg1[stage] = shadow.TextureArgument( stage, SHADER_TEXCHANNEL_ALPHA, 0 );
			ff.alphaArg2[stage] = shadow.TextureArgument( stage, SHADER_TEXCHANNEL_ALPHA, 1 );
		}
		if ( snapshot.vertexShaderName.empty() )
		{
			VertexFormat_t format = VERTEX_POSITION;
			const unsigned flags = ff.drawFlags;
			if ( ( flags & SHADER_DRAW_NORMAL ) || ff.lighting || ff.specular )
				format |= VERTEX_NORMAL;
			if ( ( flags & SHADER_DRAW_COLOR ) || ff.lighting )
				format |= VERTEX_COLOR;
			if ( flags & SHADER_DRAW_SPECULAR )
				format |= VERTEX_SPECULAR;
			if ( flags & SHADER_TEXCOORD_MASK )
				format |= VERTEX_TEXCOORD_SIZE( 0, 2 );
			if ( flags & SHADER_LIGHTMAP_TEXCOORD_MASK )
				format |= VERTEX_TEXCOORD_SIZE( 1, 2 );
			if ( flags & SHADER_SECONDARY_TEXCOORD_MASK )
				format |= VERTEX_TEXCOORD_SIZE( 2, 2 );
			if ( ff.vertexBlend )
				format |= VERTEX_BONEWEIGHT( 2 ) | VERTEX_BONE_INDEX;
			snapshot.format = format;
			snapshot.usage = format;
		}
		ff.format = snapshot.format;
	}
	if ( !snapshot.vertexShaderName.empty() )
		m_NamedShaderReferences[m_NamedShaderReferences.Insert( NamedShaderKeyView{ snapshot.vertexShaderName.c_str(), snapshot.staticVertexIndex, -1, false }, true )] = true;
	if ( !snapshot.pixelShaderName.empty() )
		m_NamedShaderReferences[m_NamedShaderReferences.Insert( NamedShaderKeyView{ snapshot.pixelShaderName.c_str(), snapshot.staticPixelIndex, -1, true }, true )] = true;
	m_Snapshots.AddToTail( snapshot );
	return static_cast<StateSnapshot_t>( m_Snapshots.Count() - 1 );
}

//-----------------------------------------------------------------------------
// Purpose: Binds a material; switching within one material page keeps buffered primitives
//-----------------------------------------------------------------------------
void CShaderAPIDX12::Bind( IMaterial *pMaterial )
{
	if ( m_pBoundMaterial == pMaterial )
		return;
	if ( m_pBoundMaterial && pMaterial && m_pBoundMaterial->InMaterialPage() && pMaterial->InMaterialPage() && m_pBoundMaterial->GetMaterialPage() == pMaterial->GetMaterialPage() )
		return;
	FlushBufferedPrimitives();
	m_pBoundMaterial = pMaterial;
}

void CShaderAPIDX12::FlushBufferedPrimitives()
{
	if ( m_pShaderUtil )
		m_pShaderUtil->OnFlushBufferedPrimitives();
}

IMesh *CShaderAPIDX12::GetDynamicMesh( IMaterial *material, int skinBoneCount, bool buffered, IMesh *vertexOverride, IMesh *indexOverride )
{
	return GetDynamicMeshEx( material, 0, skinBoneCount, buffered, vertexOverride, indexOverride );
}

//-----------------------------------------------------------------------------
// Purpose: Returns a dynamic mesh for the material's (or requested) vertex format
//-----------------------------------------------------------------------------
IMesh *CShaderAPIDX12::GetDynamicMeshEx( IMaterial *material, VertexFormat_t requestedFormat, int skinBoneCount, bool buffered, IMesh *vertexOverride, IMesh *indexOverride )
{
	if ( material )
		Bind( material );
	IMaterial *effectiveMaterial = material ? material : m_pBoundMaterial;
	if ( skinBoneCount < 0 || skinBoneCount > 4 )
	{
		Warning( "ShaderAPIDX12: unsupported dynamic mesh skin bone count %d\n", skinBoneCount );
		return nullptr;
	}
	VertexFormat_t format = vertexOverride ? vertexOverride->GetVertexFormat() : ( requestedFormat ? requestedFormat : ( effectiveMaterial ? effectiveMaterial->GetVertexFormat() & ~VERTEX_FORMAT_COMPRESSED : 0 ) );
	if ( !vertexOverride )
	{
		skinBoneCount = MAX( skinBoneCount, NumBoneWeights( format ) );
		format &= ~VERTEX_BONE_WEIGHT_MASK;
		if ( skinBoneCount > 0 )
			format |= VERTEX_BONEWEIGHT( 2 ) | VERTEX_BONE_INDEX;
	}
	if ( !VertexFormatSizeDX12( format ) )
	{
		Warning( "ShaderAPIDX12: dynamic mesh requires a valid material or explicit vertex format\n" );
		return nullptr;
	}
	m_nBoneCount = skinBoneCount;
	m_nMotionBoneRows = MAX( m_nMotionBoneRows, MAX( 1, m_nBoneCount ) );
	(void)buffered;
	CMeshDX12 *vertexSource = vertexOverride ? static_cast<CMeshDX12 *>( vertexOverride )->VertexSourceMesh() : nullptr;
	CMeshDX12 *indexSource = indexOverride ? static_cast<CMeshDX12 *>( indexOverride )->IndexSourceMesh() : nullptr;
	CMeshDX12 *selected = nullptr;
	for ( CMeshDX12 *candidate : m_DynamicMeshes )
	{
		if ( candidate == vertexOverride || candidate == indexOverride || candidate == m_pRenderMesh || candidate == vertexSource || candidate == indexSource || candidate->Vertices().GetVertexFormat() != format )
			continue;
		if ( m_pRenderMesh && m_pRenderMesh->DependsOn( candidate ) )
			continue;
		selected = candidate;
		break;
	}
	if ( !selected )
	{
		selected = new CMeshDX12( format, 65536, true, []( void *context, CMeshDX12 *draw, int first, int count )
		    {
			    static_cast<CShaderAPIDX12 *>( context )->DrawMaterialMesh( draw, first, count );
		    },
		    this );
		m_DynamicMeshes.AddToTail( selected );
	}
	if ( !selected->OverrideBuffers( vertexSource, indexSource ) )
	{
		Warning( "ShaderAPIDX12: cyclic dynamic mesh override\n" );
		return nullptr;
	}
	return selected;
}

//-----------------------------------------------------------------------------
// Snapshot queries
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::IsTranslucent( StateSnapshot_t id ) const
{
	return id >= 0 && id < (StateSnapshot_t)m_Snapshots.Count() ? m_Snapshots[id].translucent : false;
}

bool CShaderAPIDX12::IsAlphaTested( StateSnapshot_t id ) const
{
	return id >= 0 && id < (StateSnapshot_t)m_Snapshots.Count() ? m_Snapshots[id].alphaTest : false;
}

bool CShaderAPIDX12::UsesVertexAndPixelShaders( StateSnapshot_t id ) const
{
	return id >= 0 && id < (StateSnapshot_t)m_Snapshots.Count() ? m_Snapshots[id].shaders : false;
}

bool CShaderAPIDX12::IsDepthWriteEnabled( StateSnapshot_t id ) const
{
	return id >= 0 && id < (StateSnapshot_t)m_Snapshots.Count() ? m_Snapshots[id].depthWrite : true;
}

VertexFormat_t CShaderAPIDX12::ComputeVertexFormat( int count, StateSnapshot_t *ids ) const
{
	return ComputeVertexUsage( count, ids );
}

//-----------------------------------------------------------------------------
// Purpose: Union of the vertex usages of the given snapshots (max bone weights / user data / texcoord sizes)
//-----------------------------------------------------------------------------
VertexFormat_t CShaderAPIDX12::ComputeVertexUsage( int count, StateSnapshot_t *ids ) const
{
	if ( count <= 0 || !ids )
		return 0;
	VertexFormat_t flags = 0;
	int boneWeights = 0, userData = 0, coordinates[VERTEX_MAX_TEXTURE_COORDINATES] = {};
	bool compressed = false, uncompressed = false;
	for ( int i = 0; i < count; ++i )
	{
		if ( ids[i] < 0 || static_cast<size_t>( ids[i] ) >= m_Snapshots.Count() )
			continue;
		VertexFormat_t format = m_Snapshots[ids[i]].usage;
		flags |= VertexFlags( format );
		boneWeights = MAX( boneWeights, NumBoneWeights( format ) );
		userData = MAX( userData, UserDataSize( format ) );
		for ( int j = 0; j < VERTEX_MAX_TEXTURE_COORDINATES; ++j )
		{
			const int nCoordinateSize = TexCoordSize( j, format );
			coordinates[j] = MAX( coordinates[j], nCoordinateSize );
		}
		compressed |= ( format & VERTEX_FORMAT_COMPRESSED ) != 0;
		uncompressed |= ( format & VERTEX_FORMAT_COMPRESSED ) == 0;
	}
	if ( compressed && uncompressed )
		flags &= ~VERTEX_FORMAT_COMPRESSED;
	VertexFormat_t result = flags | VERTEX_BONEWEIGHT( boneWeights ) | VERTEX_USERDATA_SIZE( userData );
	for ( int j = 0; j < VERTEX_MAX_TEXTURE_COORDINATES; ++j )
		result |= VERTEX_TEXCOORD_SIZE( j, coordinates[j] );
	return result;
}

//-----------------------------------------------------------------------------
// Purpose: Activates a snapshot; named shaders are re-resolved lazily when their identity changed
//-----------------------------------------------------------------------------
void CShaderAPIDX12::BeginPass( StateSnapshot_t snapshot )
{
	ZoneNamedN( beginPass, "DX12 BeginPass", DX12_DRAW_ZONES_ACTIVE );
	if ( snapshot < 0 || snapshot >= static_cast<StateSnapshot_t>( m_Snapshots.Count() ) )
		return;
	if ( m_hActiveSnapshotId != snapshot )
	{
		const Snapshot &next = m_Snapshots[snapshot];
		m_bFogDirty |= m_ActiveSnapshot.fogMode != next.fogMode || m_ActiveSnapshot.fogGammaDisabled != next.fogGammaDisabled;
		m_bNamedVertexShaderDirty |= m_ActiveSnapshot.vertexShaderName != next.vertexShaderName || m_ActiveSnapshot.staticVertexIndex != next.staticVertexIndex;
		m_bNamedPixelShaderDirty |= m_ActiveSnapshot.pixelShaderName != next.pixelShaderName || m_ActiveSnapshot.staticPixelIndex != next.staticPixelIndex;
		m_ActiveSnapshot = next;
		m_hActiveSnapshotId = snapshot;
	}
	if ( m_ActiveSnapshot.vertexShaderName.empty() )
	{
		m_hBoundVS = VERTEX_SHADER_HANDLE_INVALID;
		m_bBoundVertexShaderIsNamed = m_bNamedVertexShaderDirty = false;
	}
	else
		m_bNamedVertexShaderDirty |= !m_bBoundVertexShaderIsNamed;
	if ( m_ActiveSnapshot.pixelShaderName.empty() )
	{
		m_hBoundPS = PIXEL_SHADER_HANDLE_INVALID;
		m_bBoundPixelShaderIsNamed = m_bNamedPixelShaderDirty = false;
	}
	else
		m_bNamedPixelShaderDirty |= !m_bBoundPixelShaderIsNamed;
}

void CShaderAPIDX12::RenderPass( int nPass, int nPassCount )
{
	if ( !m_pRenderMesh || nPass < 0 || nPass >= nPassCount )
		return;
	DrawMesh( m_pRenderMesh, m_nRenderFirstIndex, m_nRenderIndexCount );
}

void CShaderAPIDX12::SetNumBoneWeights( int numBones )
{
	m_nBoneCount = clamp( numBones, 0, NUM_MODEL_TRANSFORMS );
	m_nMotionBoneRows = MAX( 1, m_nBoneCount );
}

void CShaderAPIDX12::SetLight( int number, const LightDesc_t &light )
{
	if ( number < 0 || number >= static_cast<int>( ARRAYSIZE( m_Lights ) ) )
		return;
	m_Lights[number] = light;
	m_bLightingDirty = true;
}

void CShaderAPIDX12::SetLightingOrigin( Vector origin )
{
	m_LightingOrigin = origin;
}

void CShaderAPIDX12::SetAmbientLight( float r, float g, float b )
{
	if ( m_AmbientLight.x != r || m_AmbientLight.y != g || m_AmbientLight.z != b )
	{
		m_AmbientLight.Init( r, g, b );
		++m_nFixedVSVersion;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Stores the ambient cube used by the fixed-function and vertex-lit constants
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetAmbientLightCube( Vector4D cube[6] )
{
	if ( !cube )
		return;
	bool changed = false;
	for ( int i = 0; i < 6; ++i )
		if ( memcmp( cube[i].Base(), m_AmbientCube[i], 4 * sizeof( float ) ) )
		{
			memcpy( m_AmbientCube[i], cube[i].Base(), 4 * sizeof( float ) );
			changed = true;
		}
	if ( changed )
		++m_nFixedVSVersion;
}

void CShaderAPIDX12::ShadeMode( ShaderShadeMode_t mode )
{
	m_ShadeMode = mode;
}

void CShaderAPIDX12::CullMode( MaterialCullMode_t mode )
{
	m_CullMode = mode;
}

void CShaderAPIDX12::ForceDepthFuncEquals( bool enable )
{
	m_bForceDepthEquals = enable;
}

void CShaderAPIDX12::OverrideDepthEnable( bool enable, bool depthEnable )
{
	m_bOverrideDepthEnable = enable;
	m_bOverrideDepthValue = depthEnable;
}

void CShaderAPIDX12::SetHeightClipZ( float z )
{
	m_flHeightClipZ = z;
}

void CShaderAPIDX12::SetHeightClipMode( enum MaterialHeightClipMode_t heightClipMode )
{
	m_HeightClipMode = heightClipMode;
}

void CShaderAPIDX12::SetClipPlane( int index, const float *pPlane )
{
	if ( index < 0 || index >= static_cast<int>( ARRAYSIZE( m_WorldClipPlanes ) ) || !pPlane )
		return;
	SetFloat4DX12( m_WorldClipPlanes[index], pPlane[0], pPlane[1], pPlane[2], -pPlane[3] );
}

void CShaderAPIDX12::EnableClipPlane( int index, bool bEnable )
{
	if ( index < 0 || index >= static_cast<int>( ARRAYSIZE( m_WorldClipPlanes ) ) )
		return;
	const uint32_t bit = 1u << index;
	if ( bEnable )
		m_nClipPlaneMask |= bit;
	else
		m_nClipPlaneMask &= ~bit;
}

void CShaderAPIDX12::SetSkinningMatrices()
{
	m_nMaxBoneLoaded = MAX( m_nMaxBoneLoaded, MAX( 0, m_nBoneCount - 1 ) );
	m_nMotionBoneRows = MAX( m_nMotionBoneRows, MAX( 1, m_nBoneCount ) );
	m_bTransformsDirty = true;
	CommitTransforms();
}

//-----------------------------------------------------------------------------
// Frame boundaries and submission
//-----------------------------------------------------------------------------
void CShaderAPIDX12::FlushHardware()
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 FlushHardware", DX12_ZONES_ACTIVE );
	++m_nFrameFlushCount;
	if ( m_pDevice && m_pDevice->IsRecordingOwner() && m_pDevice->CommandList() )
	{
		ProcessPendingTextureDeletes();
		m_pDevice->Submit( false );
		m_Pipeline.Reclaim( m_pDevice->CompletedFenceValue() );
	}
}

void CShaderAPIDX12::BeginFrame()
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 BeginFrame", DX12_ZONES_ACTIVE );
	ProcessPendingTextureDeletes();
	ProcessShaderPrecacheRequests();
	m_nFrameDrawCount = 0;
	m_nFrameFlushCount = 0;
	m_nFrameSyncCount = 0;
	m_bFrameActive = true;
	++m_nFrameCounter;
	ConsumeUpscalerReplays( false );
	if ( m_pDevice && m_pDevice->IsRecordingOwner() && m_pDevice->CommandList() )
		ReleaseIdleUpscaler();
}

//-----------------------------------------------------------------------------
// Purpose: Ends the frame: optional -dx12stats report, texture deletions and completed-fence reclamation
//-----------------------------------------------------------------------------
void CShaderAPIDX12::EndFrame()
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 EndFrame", DX12_ZONES_ACTIVE );
	if ( !m_bFrameActive )
		return;
#ifdef TRACY_ENABLE
	if ( TracyIsStarted )
	{
		TracyPlot( "DX12 draws/frame", static_cast<int64_t>( m_nFrameDrawCount ) );
		TracyPlot( "DX12 flushes/frame", static_cast<int64_t>( m_nFrameFlushCount ) );
		TracyPlot( "DX12 forced syncs/frame", static_cast<int64_t>( m_nFrameSyncCount ) );
	}
#endif
	// -dx12stats: per-frame averages of draw-path cache behavior over 1000 frames (diagnostic; counters are plain increments).
	{
		static const bool s_bStatsEnabled = CommandLine() && CommandLine()->CheckParm( "-dx12stats" );
		if ( s_bStatsEnabled && ++m_nDrawStatsFrames >= 1000 )
		{
			const CPipelineCacheDX12::StatsDX12 &p = m_Pipeline.Stats();
			const double n = static_cast<double>( m_nDrawStatsFrames );
			Msg( "ShaderAPIDX12 stats/frame: draws %.1f memoHits %.1f srvTableHits %.1f srvTableCopies %.1f constHits %.1f constUploads %.1f transientConst %.1f rootCbvSets %.1f rootTableSets %.1f\n",
			    m_DrawStats.draws / n, m_DrawStats.memoHits / n, p.srvTableHits / n, p.srvTableCopies / n, p.constantHits / n, p.constantUploads / n, p.transientConstants / n, p.rootCbvSets / n, p.rootTableSets / n );
			double gpuMs = 0.0;
			uint32_t gpuFrames = 0;
			if ( m_pDevice && m_pDevice->ConsumeGpuTime( gpuMs, gpuFrames ) )
				Msg( "ShaderAPIDX12 GPU frame time: %.3f ms (%u frames)\n", gpuMs, gpuFrames );
			m_DrawStats = {};
			m_Pipeline.ResetStats();
			m_nDrawStatsFrames = 0;
		}
	}
	ProcessPendingTextureDeletes();
	if ( m_pDevice )
		m_Pipeline.Reclaim( m_pDevice->CompletedFenceValue() );
	m_bFrameActive = false;
}

//-----------------------------------------------------------------------------
// Selection mode
//-----------------------------------------------------------------------------
int CShaderAPIDX12::SelectionMode( bool enabled )
{
	return m_Selection.SetMode( enabled );
}

void CShaderAPIDX12::SelectionBuffer( unsigned int *buffer, int words )
{
	m_Selection.SetBuffer( buffer, words );
}

void CShaderAPIDX12::ClearSelectionNames()
{
	m_Selection.ClearNames();
}

void CShaderAPIDX12::LoadSelectionName( int name )
{
	m_Selection.LoadName( static_cast<unsigned int>( name ) );
}

void CShaderAPIDX12::PushSelectionName( int name )
{
	m_Selection.PushName( static_cast<unsigned int>( name ) );
}

void CShaderAPIDX12::PopSelectionName()
{
	m_Selection.PopName();
}

void CShaderAPIDX12::ForceHardwareSync()
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 ForceHardwareSync", DX12_ZONES_ACTIVE );
	++m_nFrameSyncCount;
	if ( m_pDevice && m_pDevice->IsRecordingOwner() && m_pDevice->CommandList() )
	{
		ProcessPendingTextureDeletes();
		m_pDevice->SubmitFrameSync();
		m_Pipeline.Reclaim( m_pDevice->CompletedFenceValue() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Drops every snapshot and the generated fixed-function records built for them
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ClearSnapshots()
{
	for ( uint32_t entry = m_FixedShaders.FirstInorder(); entry != m_FixedShaders.InvalidIndex(); entry = m_FixedShaders.NextInorder( entry ) )
	{
		RetireShaderPipelines( m_FixedShaders[entry] );
		delete m_FixedShaders[entry];
	}
	m_FixedShaders.RemoveAll();
	m_Snapshots.RemoveAll();
	m_ActiveSnapshot = Snapshot{};
	m_hActiveSnapshotId = static_cast<StateSnapshot_t>( -1 );
	m_bFogDirty = true;
	m_nPixelFogRegister = -1;
	++m_nNamedResolveEpoch;
	++m_nPipelineMemoEpoch;
}

//-----------------------------------------------------------------------------
// Scene fog
//-----------------------------------------------------------------------------
void CShaderAPIDX12::FogStart( float start )
{
	if ( m_flFogStart != start )
	{
		m_flFogStart = start;
		m_bFogDirty = true;
	}
}

void CShaderAPIDX12::FogEnd( float end )
{
	if ( m_flFogEnd != end )
	{
		m_flFogEnd = end;
		m_bFogDirty = true;
	}
}

void CShaderAPIDX12::SetFogZ( float height )
{
	if ( m_flFogZ != height )
	{
		m_flFogZ = height;
		m_bFogDirty = true;
	}
}

void CShaderAPIDX12::SceneFogColor3ub( unsigned char r, unsigned char g, unsigned char b )
{
	if ( m_FogColor[0] != r || m_FogColor[1] != g || m_FogColor[2] != b )
	{
		m_FogColor[0] = r;
		m_FogColor[1] = g;
		m_FogColor[2] = b;
		m_bFogDirty = true;
	}
}

void CShaderAPIDX12::SceneFogMode( MaterialFogMode_t mode )
{
	if ( m_FogMode != mode )
	{
		m_FogMode = mode;
		m_bFogDirty = true;
	}
}

bool CShaderAPIDX12::CanDownloadTextures() const
{
	return true;
}

void CShaderAPIDX12::ResetRenderState( bool bFullReset )
{
	if ( bFullReset )
		ResetNativeState();
}

int CShaderAPIDX12::GetCurrentDynamicVBSize( void )
{
	return DYNAMIC_VERTEX_BUFFER_MEMORY;
}

void CShaderAPIDX12::DestroyVertexBuffers( bool )
{
	for ( VertexBindingDX12 &binding : m_BoundVertexBuffers )
		binding = VertexBindingDX12{};
	m_pBoundIndexBuffer = nullptr;
	m_nBoundIndexOffset = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Sets the anisotropy level; prepared samplers are rebuilt on change
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetAnisotropicLevel( int nAnisotropyLevel )
{
	const int level = MAX( 1, MIN( 16, nAnisotropyLevel ) );
	if ( level != m_nAnisotropy )
	{
		m_nAnisotropy = level;
		for ( PreparedTextureSlot &slot : m_PreparedTextureSlots )
			slot.valid = false;
		++m_nTextureStateEpoch;
		m_bTextureSetValid = false;
	}
}

void CShaderAPIDX12::SyncToken( const char *pToken )
{
}

void CShaderAPIDX12::SetStandardVertexShaderConstants( float overbright )
{
	const float math[4] = { 0.f, 1.f, 2.f, .5f };
	const float gamma[4] = { 1.f / GAMMA, overbright, 1.f / 3.f, 1.f / overbright };
	const float flex[4] = { 0.f, 0.f, 0.f, 0.f };
	SetVertexShaderConstant( VERTEX_SHADER_MATH_CONSTANTS0, math, 1 );
	SetVertexShaderConstant( VERTEX_SHADER_MATH_CONSTANTS1, gamma, 1 );
	SetVertexShaderConstant( VERTEX_SHADER_FLEXSCALE, flex, 1 );
}

//-----------------------------------------------------------------------------
// Purpose: Creates an occlusion query, first freeing destroyed queries the GPU no longer references
//-----------------------------------------------------------------------------
ShaderAPIOcclusionQuery_t CShaderAPIDX12::CreateOcclusionQueryObject()
{
	const uint64_t completed = m_pDevice ? m_pDevice->CompletedFenceValue() : 0;
	for ( int i = 0; i < m_OcclusionQueries.Count(); )
	{
		OcclusionQueryDX12 *query = m_OcclusionQueries[i];
		if ( query->destroyed && ( !query->ended || completed >= query->fence ) )
		{
			delete query;
			m_OcclusionQueries.Remove( i );
		}
		else
			++i;
	}
	OcclusionQueryDX12 *pQuery = CreateOcclusionQuery();
	if ( !pQuery )
		return INVALID_SHADERAPI_OCCLUSION_QUERY_HANDLE;
	m_OcclusionQueries.AddToTail( pQuery );
	return reinterpret_cast<ShaderAPIOcclusionQuery_t>( pQuery );
}

//-----------------------------------------------------------------------------
// Occlusion queries: destruction is deferred until the GPU finished with the query
//-----------------------------------------------------------------------------
void CShaderAPIDX12::DestroyOcclusionQueryObject( ShaderAPIOcclusionQuery_t handle )
{
	OcclusionQueryDX12 *query = reinterpret_cast<OcclusionQueryDX12 *>( handle );
	if ( !query )
		return;
	for ( OcclusionQueryDX12 *entry : m_OcclusionQueries )
		if ( entry == query )
		{
			query->destroyed = true;
			if ( query->active )
				query->error = true;
			return;
		}
}

void CShaderAPIDX12::BeginOcclusionQueryDrawing( ShaderAPIOcclusionQuery_t handle )
{
	OcclusionQueryDX12 *query = reinterpret_cast<OcclusionQueryDX12 *>( handle );
	if ( !query || query->destroyed || !m_pDevice || !m_pDevice->CommandList() || query->active )
	{
		if ( query )
			query->error = true;
		return;
	}
	m_pDevice->CommandList()->BeginQuery( query->heap.Get(), D3D12_QUERY_TYPE_OCCLUSION, 0 );
	query->active = true;
	query->ended = false;
	query->error = false;
}

void CShaderAPIDX12::EndOcclusionQueryDrawing( ShaderAPIOcclusionQuery_t handle )
{
	OcclusionQueryDX12 *query = reinterpret_cast<OcclusionQueryDX12 *>( handle );
	if ( !query || !query->active || !m_pDevice || !m_pDevice->CommandList() )
	{
		if ( query )
			query->error = true;
		return;
	}
	CCommandRecorderDX12 *list = m_pDevice->CommandList();
	list->EndQuery( query->heap.Get(), D3D12_QUERY_TYPE_OCCLUSION, 0 );
	list->ResolveQueryData( query->heap.Get(), D3D12_QUERY_TYPE_OCCLUSION, 0, 1, query->readback.Get(), 0 );
	query->active = false;
	query->ended = true;
	query->fence = m_pDevice->NextFenceValue();
	m_pDevice->RetainResource( query->readback.Get() );
}

//-----------------------------------------------------------------------------
// Purpose: Reads a resolved occlusion result once its fence completed (optionally submitting to get there)
//-----------------------------------------------------------------------------
int CShaderAPIDX12::OcclusionQuery_GetNumPixelsRendered( ShaderAPIOcclusionQuery_t handle, bool flush )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 OcclusionQueryResult", DX12_ZONES_ACTIVE );
	OcclusionQueryDX12 *query = reinterpret_cast<OcclusionQueryDX12 *>( handle );
	if ( !query || query->destroyed || query->error )
		return OCCLUSION_QUERY_RESULT_ERROR;
	if ( query->active || !query->ended )
		return OCCLUSION_QUERY_RESULT_PENDING;
	if ( m_pDevice->CompletedFenceValue() < query->fence )
	{
		if ( flush )
		{
			TracyPlot( "DX12 occlusion flush", static_cast<int64_t>( 1 ) );
			if ( m_pDevice->IsRecordingOwner() && m_pDevice->CommandList() )
			{
				ProcessPendingTextureDeletes();
				m_pDevice->Submit( true );
				m_Pipeline.Reclaim( m_pDevice->CompletedFenceValue() );
			}
		}
		if ( m_pDevice->CompletedFenceValue() < query->fence )
			return OCCLUSION_QUERY_RESULT_PENDING;
	}
	uint64_t value = 0;
	void *mapped = nullptr;
	D3D12_RANGE range{ 0, sizeof( value ) };
	if ( FAILED( query->readback->Map( 0, &range, &mapped ) ) || !mapped )
	{
		query->error = true;
		return OCCLUSION_QUERY_RESULT_ERROR;
	}
	memcpy( &value, mapped, sizeof( value ) );
	query->readback->Unmap( 0, nullptr );
	return value > INT_MAX ? INT_MAX : static_cast<int>( value );
}

void CShaderAPIDX12::SetFlashlightState( const FlashlightState_t &state, const VMatrix &worldToTexture )
{
	m_Flashlight = state;
	m_pFlashlightDepthTexture = nullptr;
	m_FlashlightMatrix = worldToTexture;
}

//-----------------------------------------------------------------------------
// Purpose: Clears every named-shader reference mark ahead of PurgeUnusedVertexAndPixelShaders
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ClearVertexAndPixelShaderRefCounts()
{
	FOR_EACH_HASHTABLE( m_NamedShaderReferences, entry )
	m_NamedShaderReferences[entry] = false;
	++m_nNamedReferenceEpoch;
	// The next use must re-mark cached named bindings before an unused-shader purge.
	m_bNamedVertexShaderDirty |= m_bBoundVertexShaderIsNamed && !m_ActiveSnapshot.vertexShaderName.empty();
	m_bNamedPixelShaderDirty |= m_bBoundPixelShaderIsNamed && !m_ActiveSnapshot.pixelShaderName.empty();
}

//-----------------------------------------------------------------------------
// Purpose: Deletes named combos and files whose (name, static index) is no longer referenced
//-----------------------------------------------------------------------------
void CShaderAPIDX12::PurgeUnusedVertexAndPixelShaders()
{
	++m_nNamedResolveEpoch;
	for ( UtlHashHandle_t it = m_NamedShaderCombos.FirstHandle(); it != m_NamedShaderCombos.InvalidHandle(); )
	{
		const NamedShaderKey &key = m_NamedShaderCombos.Key( it );
		const UtlHashHandle_t reference = m_NamedShaderReferences.Find( NamedShaderKeyView{ key.name.String(), key.staticIndex, -1, key.pixel } );
		if ( reference != m_NamedShaderReferences.InvalidHandle() && m_NamedShaderReferences[reference] )
		{
			it = m_NamedShaderCombos.NextHandle( it );
			continue;
		}
		ShaderRecordDX12 *record = m_NamedShaderCombos[it];
		const bool vertexBound = record && reinterpret_cast<ShaderRecordDX12 *>( m_hBoundVS ) == record;
		const bool pixelBound = record && reinterpret_cast<ShaderRecordDX12 *>( m_hBoundPS ) == record;
		RetireShaderPipelines( record );
		delete record;
		it = m_NamedShaderCombos.RemoveAndAdvance( it );
		if ( vertexBound )
			m_bNamedVertexShaderDirty = !m_ActiveSnapshot.vertexShaderName.empty();
		if ( pixelBound )
			m_bNamedPixelShaderDirty = !m_ActiveSnapshot.pixelShaderName.empty();
	}
	for ( UtlHashHandle_t it = m_NamedShaderFiles.FirstHandle(); it != m_NamedShaderFiles.InvalidHandle(); )
	{
		bool referenced = false;
		const char *fileKey = m_NamedShaderFiles.Key( it ).String();
		FOR_EACH_HASHTABLE( m_NamedShaderReferences, entry )
		{
			const NamedShaderKey &key = m_NamedShaderReferences.Key( entry );
			if ( m_NamedShaderReferences[entry] && key.pixel == ( fileKey[0] == 'p' ) && !V_strcmp( fileKey + 2, key.name.String() ) )
			{
				referenced = true;
				break;
			}
		}
		if ( referenced )
			it = m_NamedShaderFiles.NextHandle( it );
		else
		{
			delete m_NamedShaderFiles[it];
			it = m_NamedShaderFiles.RemoveAndAdvance( it );
		}
	}
	for ( UtlHashHandle_t it = m_NamedShaderReferences.FirstHandle(); it != m_NamedShaderReferences.InvalidHandle(); )
		if ( !m_NamedShaderReferences[it] )
			it = m_NamedShaderReferences.RemoveAndAdvance( it );
		else
			it = m_NamedShaderReferences.NextHandle( it );
}

void CShaderAPIDX12::DXSupportLevelChanged()
{
	ClearSnapshots();
}

void CShaderAPIDX12::EnableUserClipTransformOverride( bool bEnable )
{
	m_bUserClipViewOverride = bEnable;
}

void CShaderAPIDX12::UserClipTransform( const VMatrix &worldToView )
{
	m_UserClipView = worldToView;
}

//-----------------------------------------------------------------------------
// Purpose: Union of the morph formats of the given snapshots
//-----------------------------------------------------------------------------
MorphFormat_t CShaderAPIDX12::ComputeMorphFormat( int count, StateSnapshot_t *ids ) const
{
	MorphFormat_t format = 0;
	if ( ids )
		for ( int i = 0; i < count; ++i )
			if ( ids[i] >= 0 && static_cast<size_t>( ids[i] ) < m_Snapshots.Count() )
				format |= m_Snapshots[ids[i]].morph;
	return format;
}

void CShaderAPIDX12::HandleDeviceLost()
{
	if ( m_pDevice && m_pDevice->NativeDevice() )
		Warning( "ShaderAPIDX12: device removal reason 0x%08x\n", static_cast<unsigned>( m_pDevice->NativeDevice()->GetDeviceRemovedReason() ) );
}

void CShaderAPIDX12::EnableLinearColorSpaceFrameBuffer( bool bEnable )
{
	if ( m_bLinearColorSpaceFramebuffer != bEnable )
	{
		FlushBufferedPrimitives();
		m_bLinearColorSpaceFramebuffer = bEnable;
	}
}

void CShaderAPIDX12::SetFullScreenTextureHandle( ShaderAPITextureHandle_t h )
{
}

//-----------------------------------------------------------------------------
// Rendering parameters; DX12 status parameters are read-only and the DX12 control parameters dispatch
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetIntRenderingParameter( int parm_number, int value )
{
	if ( parm_number == INT_RENDERPARM_DX12_MOTION_STATUS || parm_number == INT_RENDERPARM_DX12_UPSCALE_STATUS || parm_number == INT_RENDERPARM_DX12_NR_STATUS )
		return;
	if ( parm_number >= 0 && parm_number < (int)ARRAYSIZE( m_RenderingInts ) )
		m_RenderingInts[parm_number] = value;
	if ( parm_number == INT_RENDERPARM_DX12_MOTION_PASS )
		SetMotionPass( value );
	else if ( parm_number == INT_RENDERPARM_DX12_MOTION_OBJECT )
		m_nMotionObjectKey = value;
	else if ( parm_number == INT_RENDERPARM_DX12_UPSCALE_MODE )
		SetUpscalerMode( value );
	else if ( parm_number == INT_RENDERPARM_DX12_UPSCALE_DISPATCH )
		DispatchUpscaler( value );
}

void CShaderAPIDX12::SetVectorRenderingParameter( int parm_number, Vector const &value )
{
	if ( parm_number >= 0 && parm_number < (int)ARRAYSIZE( m_RenderingVectors ) )
		m_RenderingVectors[parm_number] = value;
}

float CShaderAPIDX12::GetFloatRenderingParameter( int parm_number ) const
{
	return parm_number >= 0 && parm_number < (int)ARRAYSIZE( m_RenderingFloats ) ? m_RenderingFloats[parm_number] : 0.0f;
}

int CShaderAPIDX12::GetIntRenderingParameter( int parm_number ) const
{
	return parm_number >= 0 && parm_number < (int)ARRAYSIZE( m_RenderingInts ) ? m_RenderingInts[parm_number] : 0;
}

Vector CShaderAPIDX12::GetVectorRenderingParameter( int parm_number ) const
{
	return parm_number >= 0 && parm_number < (int)ARRAYSIZE( m_RenderingVectors ) ? m_RenderingVectors[parm_number] : Vector( 0, 0, 0 );
}

void CShaderAPIDX12::SetFastClipPlane( const float *pPlane )
{
	if ( pPlane )
		SetFloat4DX12( m_FastClipPlane, pPlane[0], pPlane[1], pPlane[2], -pPlane[3] );
}

void CShaderAPIDX12::EnableFastClip( bool bEnable )
{
	m_bFastClipEnabled = bEnable;
}

void CShaderAPIDX12::GetMaxToRender( IMesh *pMesh, bool bMaxUntilFlush, int *pMaxVerts, int *pMaxIndices )
{
	if ( pMaxVerts )
		*pMaxVerts = 65536;
	if ( pMaxIndices )
		*pMaxIndices = INDEX_BUFFER_SIZE;
}

int CShaderAPIDX12::GetMaxVerticesToRender( IMaterial *pMaterial )
{
	return 65536;
}

int CShaderAPIDX12::GetMaxIndicesToRender()
{
	return INDEX_BUFFER_SIZE;
}

void CShaderAPIDX12::DisableAllLocalLights()
{
	for ( LightDesc_t &light : m_Lights )
		light.m_Type = MATERIAL_LIGHT_DISABLE;
	m_bLightingDirty = true;
}

//-----------------------------------------------------------------------------
// Purpose: Snapshot sort order: non-alpha-tested first, then non-translucent, then by id
//-----------------------------------------------------------------------------
int CShaderAPIDX12::CompareSnapshots( StateSnapshot_t snapshot0, StateSnapshot_t snapshot1 )
{
	if ( snapshot0 == snapshot1 )
		return 0;
	if ( snapshot0 < 0 || snapshot1 < 0 || snapshot0 >= (StateSnapshot_t)m_Snapshots.Count() || snapshot1 >= (StateSnapshot_t)m_Snapshots.Count() )
		return snapshot0 < snapshot1 ? -1 : 1;
	const Snapshot &a = m_Snapshots[snapshot0], &b = m_Snapshots[snapshot1];
	if ( a.alphaTest != b.alphaTest )
		return a.alphaTest ? 1 : -1;
	if ( a.translucent != b.translucent )
		return a.translucent ? 1 : -1;
	return snapshot0 < snapshot1 ? -1 : 1;
}

//-----------------------------------------------------------------------------
// Purpose: Flex mesh in DX9's CMeshMgr::GetFlexMesh layout (28-byte position, wrinkle, normal delta)
//-----------------------------------------------------------------------------
IMesh *CShaderAPIDX12::GetFlexMesh()
{
	if ( !m_pFlexMesh )
		m_pFlexMesh = new CMeshDX12( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_WRINKLE | VERTEX_FORMAT_USE_EXACT_FORMAT, 65536, true, []( void *context, CMeshDX12 *m, int f, int n )
		    {
			    static_cast<CShaderAPIDX12 *>( context )->DrawMaterialMesh( m, f, n );
		    },
		    this );
	return m_pFlexMesh;
}

void CShaderAPIDX12::SetFlashlightStateEx( const FlashlightState_t &state, const VMatrix &worldToTexture, ITexture *pFlashlightDepthTexture )
{
	m_Flashlight = state;
	m_pFlashlightDepthTexture = pFlashlightDepthTexture;
	m_FlashlightMatrix = worldToTexture;
}

bool CShaderAPIDX12::SupportsMSAAMode( int nMSAAMode )
{
	return m_pDevice && m_pDevice->SupportsMSAA( nMSAAMode );
}

//-----------------------------------------------------------------------------
// Purpose: Releases or reacquires GPU resources for the recording owner
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::OwnGPUResources( bool bEnable )
{
	if ( !m_pDevice || !m_pDevice->IsRecordingOwner() )
		return false;
	if ( bEnable )
		m_pDevice->ReacquireResources();
	else
		m_pDevice->ReleaseResources();
	return m_pDevice->IsUsingGraphics();
}

void CShaderAPIDX12::GetFogDistances( float *fStart, float *fEnd, float *fFogZ )
{
	if ( fStart )
		*fStart = m_flFogStart;
	if ( fEnd )
		*fEnd = m_flFogEnd;
	if ( fFogZ )
		*fFogZ = m_flFogZ;
}

void CShaderAPIDX12::BeginPIXEvent( unsigned long color, const char *szName ) {}

void CShaderAPIDX12::EndPIXEvent() {}

void CShaderAPIDX12::SetPIXMarker( unsigned long color, const char *szName ) {}

void CShaderAPIDX12::EnableAlphaToCoverage()
{
	m_bAlphaToCoverage = true;
}

void CShaderAPIDX12::DisableAlphaToCoverage()
{
	m_bAlphaToCoverage = false;
}

void CShaderAPIDX12::ComputeVertexDescription( unsigned char *pBuffer, VertexFormat_t vertexFormat, MeshDesc_t &desc ) const
{
	ComputeVertexLayoutDX12( vertexFormat, pBuffer, &desc );
}

bool CShaderAPIDX12::SupportsShadowDepthTextures( void )
{
	return true;
}

void CShaderAPIDX12::SetDisallowAccess( bool bDisallow )
{
	m_bDisallowAccess = bDisallow;
}

void CShaderAPIDX12::EnableShaderShaderMutex( bool bEnable )
{
	m_bMutexEnabled = bEnable;
}

void CShaderAPIDX12::ShaderLock()
{
	if ( m_bMutexEnabled )
		m_ShaderMutex.Lock();
}

void CShaderAPIDX12::ShaderUnlock()
{
	if ( m_bMutexEnabled )
		m_ShaderMutex.Unlock();
}

ImageFormat CShaderAPIDX12::GetShadowDepthTextureFormat( void )
{
	return IMAGE_FORMAT_NV_INTZ;
}

bool CShaderAPIDX12::SupportsFetch4( void )
{
	return false;
}

void CShaderAPIDX12::SetShadowDepthBiasFactors( float fShadowSlopeScaleDepthBias, float fShadowDepthBias )
{
	m_FastFloatParams[0] = fShadowSlopeScaleDepthBias;
	m_FastFloatParams[1] = fShadowDepthBias;
}

//-----------------------------------------------------------------------------
// Explicit vertex/index buffer binding and drawing
//-----------------------------------------------------------------------------
void CShaderAPIDX12::BindVertexBuffer( int stream, IVertexBuffer *buffer, int offset, int firstVertex, int vertexCount, VertexFormat_t format, int repetitions )
{
	if ( stream < 0 || stream >= static_cast<int>( ARRAYSIZE( m_BoundVertexBuffers ) ) )
		return;
	VertexBindingDX12 &binding = m_BoundVertexBuffers[stream];
	binding = {};
	if ( !buffer || offset < 0 || firstVertex < 0 || vertexCount <= 0 || repetitions <= 0 )
		return;
	binding = { static_cast<CVertexBufferDX12 *>( buffer ), static_cast<uint32_t>( offset ), static_cast<uint32_t>( firstVertex ), static_cast<uint32_t>( vertexCount ), static_cast<uint32_t>( repetitions ), format ? format : buffer->GetVertexFormat() };
}

void CShaderAPIDX12::BindIndexBuffer( IIndexBuffer *buffer, int offset )
{
	m_pBoundIndexBuffer = offset >= 0 ? static_cast<CIndexBufferDX12 *>( buffer ) : nullptr;
	m_nBoundIndexOffset = offset >= 0 ? static_cast<size_t>( offset ) : 0;
}

void CShaderAPIDX12::Draw( MaterialPrimitiveType_t primitive, int firstIndex, int indexCount )
{
	DrawBuffers( m_BoundVertexBuffers, m_pBoundIndexBuffer, m_nBoundIndexOffset, primitive, firstIndex, indexCount );
}

void CShaderAPIDX12::PerformFullScreenStencilOperation()
{
	FlushBufferedPrimitives();
	DrawMaskedClear( false, false, false, nullptr, true );
}

void CShaderAPIDX12::SetScissorRect( const int nLeft, const int nTop, const int nRight, const int nBottom, const bool bEnableScissor )
{
	m_FastIntParams[0] = nLeft;
	m_FastIntParams[1] = nTop;
	m_FastIntParams[2] = nRight;
	m_FastIntParams[3] = nBottom;
	m_FastIntParams[4] = bEnableScissor ? 1 : 0;
}

bool CShaderAPIDX12::SupportsCSAAMode( int nNumSamples, int nQualityLevel )
{
	(void)nNumSamples;
	(void)nQualityLevel;
	return false;
}

void CShaderAPIDX12::InvalidateDelayedShaderConstants()
{
	m_nPixelFogRegister = -1;
}

float CShaderAPIDX12::GammaToLinear_HardwareSpecific( float gamma ) const
{
	return gamma <= 0.04045f ? gamma / 12.92f : powf( ( gamma + 0.055f ) / 1.055f, 2.4f );
}

float CShaderAPIDX12::LinearToGamma_HardwareSpecific( float linear ) const
{
	return linear <= 0.0031308f ? linear * 12.92f : 1.055f * powf( linear, 1.f / 2.4f ) - 0.055f;
}

void CShaderAPIDX12::SetLinearToGammaConversionTextures( ShaderAPITextureHandle_t hSRGBWriteEnabledTexture, ShaderAPITextureHandle_t hIdentityTexture )
{
}

ImageFormat CShaderAPIDX12::GetNullTextureFormat( void )
{
	return IMAGE_FORMAT_RGBA8888;
}

void CShaderAPIDX12::BindVertexTexture( VertexTextureSampler_t nSampler, ShaderAPITextureHandle_t textureHandle )
{
	if ( nSampler >= 0 && nSampler < (int)ARRAYSIZE( m_VertexTextures ) )
		m_VertexTextures[nSampler] = textureHandle;
}

void CShaderAPIDX12::EnableHWMorphing( bool bEnable )
{
	m_bMorphing = bEnable;
}

void CShaderAPIDX12::SetFlexWeights( int nFirstWeight, int nCount, const MorphWeight_t *pWeights )
{
	(void)nFirstWeight;
	(void)nCount;
	(void)pWeights;
}

//-----------------------------------------------------------------------------
// Purpose: Clamped maximum fog density
//-----------------------------------------------------------------------------
void CShaderAPIDX12::FogMaxDensity( float density )
{
	density = clamp( density, 0.f, 1.f );
	if ( m_flFogMaxDensity != density )
	{
		m_flFogMaxDensity = density;
		m_bFogDirty = true;
	}
}

void CShaderAPIDX12::CreateTextures( ShaderAPITextureHandle_t *pHandles, int count, int width, int height, int depth, ImageFormat dstImageFormat, int numMipLevels, int numCopies, int flags, const char *pDebugName, const char *pTextureGroupName )
{
	if ( !pHandles )
		return;
	for ( int i = 0; i < count; ++i )
		pHandles[i] = CreateTexture( width, height, depth, dstImageFormat, numMipLevels, numCopies, flags, pDebugName, pTextureGroupName );
}

void CShaderAPIDX12::AcquireThreadOwnership()
{
	if ( m_pDevice && !m_pDevice->AcquireRecordingOwnership() )
		Warning( "ShaderAPIDX12: AcquireThreadOwnership failed\n" );
}

void CShaderAPIDX12::ReleaseThreadOwnership()
{
	if ( m_pDevice )
		m_pDevice->ReleaseRecordingOwnership();
}

void CShaderAPIDX12::EnableBuffer2FramesAhead( bool bEnable )
{
	(void)bEnable;
}

void CShaderAPIDX12::PrintfVA( char *fmt, va_list vargs )
{
	if ( fmt )
		vprintf( fmt, vargs );
}

//-----------------------------------------------------------------------------
// Purpose: printf to stdout
//-----------------------------------------------------------------------------
void CShaderAPIDX12::Printf( const char *fmt, ... )
{
	if ( fmt )
	{
		va_list args;
		va_start( args, fmt );
		vprintf( fmt, args );
		va_end( args );
	}
}

float CShaderAPIDX12::Knob( char *name, float *value )
{
	if ( !name )
		return 0.f;
	if ( value )
	{
		m_Knobs[m_Knobs.Insert( name, *value )] = *value;
		return *value;
	}
	const UtlHashHandle_t found = m_Knobs.Find( name );
	return found == m_Knobs.InvalidHandle() ? 0.f : m_Knobs[found];
}

void CShaderAPIDX12::OverrideAlphaWriteEnable( bool bEnable, bool bAlphaWriteEnable )
{
	m_bAlphaWriteOverride = bEnable;
	m_bAlphaWriteOverrideValue = bAlphaWriteEnable;
}

void CShaderAPIDX12::OverrideColorWriteEnable( bool bOverrideEnable, bool bColorWriteEnable )
{
	m_bColorWriteOverride = bOverrideEnable;
	m_bColorWriteOverrideValue = bColorWriteEnable;
}

int CShaderAPIDX12::VertexFormatSize( VertexFormat_t vertexFormat ) const
{
	return VertexFormatSizeDX12( vertexFormat );
}

void CShaderAPIDX12::SceneFogRadial( bool bRadial )
{
	m_bFogRadial = bRadial;
}

bool CShaderAPIDX12::GetSceneFogRadial()
{
	return m_bFogRadial;
}

} // namespace shaderapidx12
