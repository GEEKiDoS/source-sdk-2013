//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Development-only `shader_precache` command. The console callback only parses and queues a
//          copy of its arguments; the recording owner drains the queue in BeginFrame, opens the VCS
//          files read-only, decodes every present static/dynamic combo, reflects each payload and
//          reports native/legacy counts and failures. It never writes, replaces or compiles shaders,
//          and PSO warming is not implied: pipeline state depends on per-draw snapshot/raster/format state.
//
//=============================================================================//
#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "shader_vcs_dx12.h"
#include "vertex_layout_dx12.h"
#include "native_engine_cbuffers_dx12.h"
#include "filesystem.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"

#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <wrl/client.h>

namespace shaderapidx12
{

struct PrecacheCounts
{
	unsigned m_nFiles = 0;
	unsigned m_nStatics = 0;
	unsigned m_nCombos = 0;
	unsigned m_nSkipped = 0;
	unsigned m_nNative = 0;
	unsigned m_nLegacy = 0;
	unsigned m_nFailures = 0;

	void Add( const PrecacheCounts &other )
	{
		m_nFiles += other.m_nFiles;
		m_nStatics += other.m_nStatics;
		m_nCombos += other.m_nCombos;
		m_nSkipped += other.m_nSkipped;
		m_nNative += other.m_nNative;
		m_nLegacy += other.m_nLegacy;
		m_nFailures += other.m_nFailures;
	}
};

static bool IsDxbc( const VcsPayload &payload )
{
	return payload.tokens.Count() >= 4 && !memcmp( payload.tokens.Base(), "DXBC", 4 );
}

static bool ValidateLightingStructuredType( ID3D12ShaderReflection *reflection, const D3D12_SHADER_INPUT_BIND_DESC &binding )
{
	auto *buffer = reflection->GetConstantBufferByName( binding.Name );
	D3D12_SHADER_BUFFER_DESC bd{};
	if ( !buffer || FAILED( buffer->GetDesc( &bd ) ) || bd.Type != D3D_CT_RESOURCE_BIND_INFO || bd.Variables != 1 )
		return false;
	auto *element = buffer->GetVariableByIndex( 0 );
	auto *elementType = element ? element->GetType() : nullptr;
	D3D12_SHADER_TYPE_DESC type{};
	if ( !elementType || FAILED( elementType->GetDesc( &type ) ) || type.Elements )
		return false;
	if ( binding.BindPoint == DX12_LIGHTING_T_LIGHTS )
	{
		const UINT memberCount = sizeof( dx12native::kRuntimeShadowLightGpuMembers ) /
		                         sizeof( *dx12native::kRuntimeShadowLightGpuMembers );
		if ( type.Class != D3D_SVC_STRUCT || !type.Name ||
		     V_strcmp( type.Name, "RuntimeShadowLightGpu" ) || type.Members != memberCount )
			return false;
		// Match the authored layout, including row-major matrices and both six-face arrays.
		for ( UINT member = 0; member < memberCount; ++member )
		{
			const auto &expected = dx12native::kRuntimeShadowLightGpuMembers[member];
			const char *name = elementType->GetMemberTypeName( member );
			auto *memberType = elementType->GetMemberTypeByIndex( member );
			D3D12_SHADER_TYPE_DESC memberDesc{};
			if ( !name || V_strcmp( name, expected.name ) || !memberType ||
			     FAILED( memberType->GetDesc( &memberDesc ) ) || memberDesc.Offset != expected.offset ||
			     memberDesc.Class != expected.valueClass || memberDesc.Type != expected.scalarType ||
			     memberDesc.Rows != expected.rows || memberDesc.Columns != expected.columns ||
			     memberDesc.Elements != expected.elements )
				return false;
		}
		return true;
	}
	const UINT columns = binding.BindPoint == DX12_LIGHTING_T_TILE_RANGES ? 2 : 1;
	return type.Type == D3D_SVT_UINT && type.Rows == 1 && type.Columns == columns &&
	       type.Class == ( columns == 2 ? D3D_SVC_VECTOR : D3D_SVC_SCALAR );
}

// Optimized shaders retain only the resources they use. The view block is the ABI marker;
// every retained binding must match, but unused textures/samplers may disappear.
bool ValidateLightingShaderDX12( const D3D12_SHADER_BYTECODE &bytecode, bool pixelStage, bool *lightingAbi, CUtlString &error, bool *sunVisibility )
{
	if ( lightingAbi )
		*lightingAbi = false;
	if ( sunVisibility ) *sunVisibility = false;
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
	D3D12_SHADER_DESC desc{};
	if ( !bytecode.pShaderBytecode || !bytecode.BytecodeLength ||
	     FAILED( D3DReflect( bytecode.pShaderBytecode, bytecode.BytecodeLength, IID_PPV_ARGS( &reflection ) ) ) ||
	     FAILED( reflection->GetDesc( &desc ) ) )
	{
		error = "lighting shader reflection failed";
		return false;
	}
	if ( !ValidateHighresShaderResourcesDX12( reflection.Get(), desc, pixelStage, nullptr, error ) )
		return false;
	bool marker = false, space2 = false;
	unsigned seen = 0;
	for ( UINT i = 0; i < desc.BoundResources; ++i )
	{
		D3D12_SHADER_INPUT_BIND_DESC binding{};
		if ( FAILED( reflection->GetResourceBindingDesc( i, &binding ) ) || !binding.Name )
		{
			error = "lighting resource binding reflection failed";
			return false;
		}
		const dx12native::EngineCBufferLayoutDX12 *layout = nullptr;
		for ( const auto &candidate : dx12native::kLightingCBufferLayouts )
			if ( candidate.shaderRegister == DX12_LIGHTING_B_VIEW &&
			     !V_strcmp( candidate.name, "DX12LightingViewConstantsV1" ) &&
			     !V_strcmp( binding.Name, candidate.name ) )
				layout = &candidate;
		if ( binding.Space != DX12_LIGHTING_REGISTER_SPACE && !layout )
			continue;
		space2 = true;
		unsigned slot = 0;
		bool valid = pixelStage && D3D12_SHVER_GET_TYPE( desc.Version ) == D3D12_SHVER_PIXEL_SHADER &&
		             binding.Space == DX12_LIGHTING_REGISTER_SPACE;
		if ( layout )
		{
			slot = layout->shaderRegister;
			auto *buffer = reflection->GetConstantBufferByName( binding.Name );
			D3D12_SHADER_BUFFER_DESC bd{};
			valid = valid && layout->stage == dx12native::kStagePixel && binding.Type == D3D_SIT_CBUFFER &&
			        binding.BindPoint == layout->shaderRegister && binding.BindCount == 1 &&
			        buffer && SUCCEEDED( buffer->GetDesc( &bd ) ) && bd.Type == D3D_CT_CBUFFER &&
			        bd.Name && !V_strcmp( bd.Name, layout->name ) &&
			        bd.Size == layout->byteSize && bd.Variables == layout->memberCount;
			if ( valid )
			{
				for ( UINT member = 0; member < layout->memberCount; ++member )
				{
					const auto &expected = layout->members[member];
					D3D12_SHADER_VARIABLE_DESC vd{};
					auto *variable = buffer->GetVariableByIndex( member );
					if ( !variable || FAILED( variable->GetDesc( &vd ) ) || !vd.Name ||
					     V_strcmp( vd.Name, expected.name ) || vd.StartOffset != expected.offset || vd.Size != expected.size )
					{
						valid = false;
						break;
					}
				}
			}
			marker = true;
		}
		else if ( binding.Type == D3D_SIT_SAMPLER )
		{
			const bool comparison = ( binding.uFlags & D3D_SIF_COMPARISON_SAMPLER ) != 0;
			const bool shadow = comparison && binding.BindPoint == DX12_LIGHTING_S_COMPARISON && !V_strcmp( binding.Name, "g_ShadowCmpSampler" );
			valid = valid && binding.BindCount == 1 && shadow;
			slot = 8;
		}
		else
		{
			struct Resource
			{
				const char *name;
				UINT reg, count, stride;
			};
			static const Resource resources[] = {
				{ "g_ShadowLocalAtlas", DX12_LIGHTING_T_LOCAL_ATLAS_FIRST, DX12_SHADOW_MAX_LOCAL_PAGES, 0 },
				{ "g_ShadowCascadeAtlas", DX12_LIGHTING_T_CASCADE_ATLAS, 1, 0 },
				{ "g_ShadowStaticSun", DX12_LIGHTING_T_STATIC_SUN, 1, 0 },
				{ "g_ShadowLights", DX12_LIGHTING_T_LIGHTS, 1, sizeof( RuntimeShadowLightGpu ) },
				{ "g_ShadowTileRanges", DX12_LIGHTING_T_TILE_RANGES, 1, 8 },
				{ "g_ShadowTileIndices", DX12_LIGHTING_T_TILE_INDICES, 1, 4 },
				{ "g_ShadowSunVisibility", DX12_LIGHTING_T_SUN_VISIBILITY, 1, 0 },
			};
			const Resource *resource = nullptr;
			for ( unsigned r = 0; r < sizeof( resources ) / sizeof( *resources ); ++r )
			{
				if ( !V_strcmp( binding.Name, resources[r].name ) )
				{
					resource = &resources[r];
					slot = 1 + r;
					break;
				}
			}
			valid = valid && resource && binding.BindPoint == resource->reg && binding.BindCount == resource->count;
			if ( valid && resource->stride )
				valid = binding.Type == D3D_SIT_STRUCTURED && binding.Dimension == D3D_SRV_DIMENSION_BUFFER &&
				        binding.NumSamples == resource->stride && ValidateLightingStructuredType( reflection.Get(), binding );
			else if ( valid )
				valid = binding.Type == D3D_SIT_TEXTURE && binding.Dimension == D3D_SRV_DIMENSION_TEXTURE2D &&
				        binding.ReturnType == ( resource->reg == DX12_LIGHTING_T_SUN_VISIBILITY ? D3D_RETURN_TYPE_UINT : D3D_RETURN_TYPE_FLOAT ) &&
				        !( binding.uFlags & D3D_SIF_TEXTURE_COMPONENTS );
		}
		if ( !valid || ( seen & ( 1u << slot ) ) )
		{
			error = CUtlString( "lighting ABI 3 binding/layout mismatch: " ) + binding.Name;
			return false;
		}
		seen |= 1u << slot;
	}
	if ( space2 && !marker )
	{
		error = "space-2 resources require DX12LightingViewConstantsV1";
		return false;
	}
	if ( sunVisibility ) *sunVisibility = ( seen & ( 1u << 7 ) ) != 0;
	if ( lightingAbi )
		*lightingAbi = marker;
	return true;
}

static bool ValidateDepthRestoreConstants( ID3D12ShaderReflection *reflection, const D3D12_SHADER_INPUT_BIND_DESC &binding,
                                          const char *logical, VcsStage stage )
{
	const bool restore = ( stage == VcsStage::Vertex && !V_strcmp( logical, "shadow_depth_restore_vs51" ) ) ||
	                     ( stage == VcsStage::Pixel && !V_strcmp( logical, "shadow_depth_restore_ps51" ) );
	if ( !restore || binding.Type != D3D_SIT_CBUFFER || binding.Space != 0 || binding.BindPoint != 0 ||
	     binding.BindCount != 1 || V_strcmp( binding.Name, "ShadowDepthRestoreConstants" ) )
		return false;
	auto *buffer = reflection->GetConstantBufferByName( binding.Name );
	D3D12_SHADER_BUFFER_DESC bd{};
	if ( !buffer || FAILED( buffer->GetDesc( &bd ) ) || bd.Type != D3D_CT_CBUFFER || bd.Size != 64 || bd.Variables != 4 )
		return false;
	static const char *const names[] = { "srcRect", "dstRect", "srcSize", "dstSize" };
	for ( UINT i = 0; i < 4; ++i )
	{
		auto *variable = buffer->GetVariableByIndex( i );
		D3D12_SHADER_VARIABLE_DESC vd{};
		D3D12_SHADER_TYPE_DESC td{};
		if ( !variable || FAILED( variable->GetDesc( &vd ) ) || !vd.Name || V_strcmp( vd.Name, names[i] ) ||
		     vd.StartOffset != i * 16 || vd.Size != 16 || FAILED( variable->GetType()->GetDesc( &td ) ) ||
		     td.Class != D3D_SVC_VECTOR || td.Type != D3D_SVT_UINT || td.Rows != 1 || td.Columns != 4 || td.Elements )
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Reflects a native payload's space-1 cbuffers against the backend engine layouts; material
//          blocks are reported by name/register/size (their layout hash is checked at draw time against
//          the bridge write).
//-----------------------------------------------------------------------------
static bool ReflectNative( const VcsPayload &payload, VcsStage stage, const char *logical, CUtlString &error )
{
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
	if ( FAILED( D3DReflect( payload.tokens.Base(), payload.tokens.Count(), IID_PPV_ARGS( &reflection ) ) ) )
	{
		error = "D3DReflect failed";
		return false;
	}
	D3D12_SHADER_DESC desc{};
	if ( FAILED( reflection->GetDesc( &desc ) ) )
	{
		error = "reflection description failed";
		return false;
	}
	const UINT nType = D3D12_SHVER_GET_TYPE( desc.Version );
	const UINT expectedType = stage == VcsStage::Vertex ? D3D12_SHVER_VERTEX_SHADER :
	                          stage == VcsStage::Pixel ? D3D12_SHVER_PIXEL_SHADER : D3D12_SHVER_COMPUTE_SHADER;
	if ( nType != expectedType )
	{
		error = "DXBC stage does not match the VCS stage";
		return false;
	}
	CUtlVector<ShaderInputElementDX12> inputs;
	if ( stage == VcsStage::Vertex && !ReadShaderInputSignatureDX12( payload.tokens.Base(), payload.tokens.Count(), inputs ) )
	{
		error = "input signature reflection failed";
		return false;
	}
	D3D12_SHADER_BYTECODE bytecode{ payload.tokens.Base(), static_cast<SIZE_T>( payload.tokens.Count() ) };
	if ( !ValidateLightingShaderDX12( bytecode, stage == VcsStage::Pixel, nullptr, error ) )
		return false;
	for ( UINT i = 0; i < desc.BoundResources; ++i )
	{
		D3D12_SHADER_INPUT_BIND_DESC binding{};
		if ( FAILED( reflection->GetResourceBindingDesc( i, &binding ) ) )
		{
			error = "resource binding reflection failed";
			return false;
		}
		if ( stage == VcsStage::Compute )
		{
			if ( binding.Type == D3D_SIT_CBUFFER && binding.Space != 1 )
			{
				error = "compute cbuffer is outside space 1";
				return false;
			}
			if ( binding.Type != D3D_SIT_CBUFFER )
			{
				if ( binding.Space != 0 )
				{
					error = "compute SRV/UAV/sampler is outside space 0";
					return false;
				}
				const UINT nMaxSlots = binding.Type == D3D_SIT_SAMPLER ? 2u : 8u;
				if ( binding.BindPoint >= nMaxSlots || !binding.BindCount || binding.BindCount > nMaxSlots - binding.BindPoint )
				{
					error = "compute resource exceeds root-signature slot range";
					return false;
				}
			}
		}
		if ( binding.Type == D3D_SIT_CBUFFER && binding.Space == 0 &&
		     !ValidateDepthRestoreConstants( reflection.Get(), binding, logical, stage ) )
		{
			error = CUtlString( "space-0 cbuffer outside depth-restore contract: " ) + binding.Name;
			return false;
		}
		if ( binding.Type != D3D_SIT_CBUFFER || binding.Space != 1 )
			continue;
		if ( stage == VcsStage::Compute && ( binding.BindPoint != 0 || binding.BindCount != 1 ) )
		{
			error = "compute cbuffer is not a single b0 space 1 binding";
			return false;
		}
		ID3D12ShaderReflectionConstantBuffer *pBuffer = reflection->GetConstantBufferByName( binding.Name );
		D3D12_SHADER_BUFFER_DESC bufferDesc{};
		if ( !pBuffer || FAILED( pBuffer->GetDesc( &bufferDesc ) ) )
		{
			error = CUtlString( "cbuffer reflection failed: " ) + binding.Name;
			return false;
		}
		for ( const dx12native::EngineCBufferLayoutDX12 &engine : dx12native::kEngineCBufferLayouts )
		{
			if ( !V_strcmp( engine.name, binding.Name ) && ( engine.shaderRegister != binding.BindPoint || engine.byteSize != bufferDesc.Size ) )
			{
				error = CUtlString( "engine cbuffer layout mismatch: " ) + binding.Name;
				return false;
			}
		}
		const unsigned nFirst = stage == VcsStage::Vertex ? 2u : stage == VcsStage::Pixel ? 1u : 0u;
		bool bEngine = false;
		for ( const dx12native::EngineCBufferLayoutDX12 &layout : dx12native::kEngineCBufferLayouts )
			bEngine |= !V_strcmp( layout.name, binding.Name );
		const unsigned nMaxBytes = stage == VcsStage::Compute ? 256u : 65536u;
		if ( !bEngine && ( binding.BindPoint < nFirst || binding.BindPoint > 7 || ( bufferDesc.Size & 15 ) || bufferDesc.Size > nMaxBytes ) )
		{
			error = CUtlString( "material cbuffer outside the space-1 slot contract: " ) + binding.Name;
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Opens one VCS file and decodes/reflects every present combo, or only
//          nOnlyStatic/nOnlyDynamic when they are non-negative.
//-----------------------------------------------------------------------------
static PrecacheCounts ValidateFile( IFileSystem &filesystem, const char *pszName, VcsStage stage, int nOnlyStatic, int nOnlyDynamic )
{
	PrecacheCounts counts;
	ShaderVcsFile file;
	CUtlString error;
	if ( !file.Open( filesystem, pszName, stage, error ) )
	{
		++counts.m_nFailures;
		Warning( "shader_precache: %s\n", error.Get() );
		return counts;
	}
	++counts.m_nFiles;
	const char *pszStage = stage == VcsStage::Vertex ? "vs" : stage == VcsStage::Pixel ? "ps" : "cs";
	uint32_t nStaticIndex = 0;
	bool bFoundStatic = nOnlyStatic < 0;
	for ( size_t nOrdinal = 0; file.StaticComboIndex( nOrdinal, nStaticIndex ); ++nOrdinal )
	{
		if ( nOnlyStatic >= 0 && nStaticIndex != static_cast<uint32_t>( nOnlyStatic ) )
			continue;
		bFoundStatic = true;
		if ( !file.LoadStaticCombo( nStaticIndex, error ) )
		{
			++counts.m_nFailures;
			Warning( "shader_precache: %s\n", error.Get() );
			continue;
		}
		++counts.m_nStatics;
		for ( uint32_t nDynamic = 0; nDynamic < file.DynamicComboCount(); ++nDynamic )
		{
			if ( nOnlyDynamic >= 0 && nDynamic != static_cast<uint32_t>( nOnlyDynamic ) )
				continue;
			const VcsPayload *pPayload = file.DynamicPayload( nStaticIndex, nDynamic );
			if ( !pPayload )
			{
				++counts.m_nSkipped;
				if ( nOnlyDynamic >= 0 )
					Msg( "shader_precache: %s %s static %u dynamic %u is a skipped combo (not combo 0)\n", pszStage, pszName, nStaticIndex, nDynamic );
				continue;
			}
			++counts.m_nCombos;
			if ( !IsDxbc( *pPayload ) )
			{
				++counts.m_nLegacy;
				continue;
			}
			++counts.m_nNative;
			if ( !ReflectNative( *pPayload, stage, pszName, error ) )
			{
				++counts.m_nFailures;
				Warning( "shader_precache: %s %s static %u dynamic %u: %s\n", pszStage, pszName, nStaticIndex, nDynamic, error.Get() );
			}
		}
	}
	if ( !bFoundStatic )
	{
		++counts.m_nFailures;
		Warning( "shader_precache: %s %s has no static combo %d\n", pszStage, pszName, nOnlyStatic );
	}
	Msg( "shader_precache: %s %s (%s) statics=%u combos=%u skipped=%u native=%u legacy=%u failures=%u\n", pszStage, pszName,
	    file.Path(), counts.m_nStatics, counts.m_nCombos, counts.m_nSkipped, counts.m_nNative, counts.m_nLegacy, counts.m_nFailures );
	return counts;
}

//-----------------------------------------------------------------------------
// Purpose: Logical names from the published native manifest mapping (generated/meta equivalent at
//          runtime): every file present under shaders/vsh, shaders/psh, and shaders/csh.
//-----------------------------------------------------------------------------
static void ForEachPublished( IFileSystem &filesystem, const char *pszPattern, VcsStage stage, PrecacheCounts &total )
{
	FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
	for ( const char *pszFile = filesystem.FindFirstEx( pszPattern, "GAME", &hFind ); pszFile; pszFile = filesystem.FindNext( hFind ) )
	{
		if ( pszFile[0] == '.' || filesystem.FindIsDirectory( hFind ) )
			continue;
		CUtlString name( pszFile );
		// Cut at the last ".vcs".
		char *pszDot = nullptr;
		for ( char *pszHit = V_strstr( name.GetForModify(), ".vcs" ); pszHit; pszHit = V_strstr( pszHit + 1, ".vcs" ) )
			pszDot = pszHit;
		if ( !pszDot )
			continue;
		*pszDot = '\0';
		total.Add( ValidateFile( filesystem, name.Get(), stage, -1, -1 ) );
	}
	if ( hFind != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem.FindClose( hFind );
}

//-----------------------------------------------------------------------------
// Purpose: Queues a request from the console thread; drained by ProcessShaderPrecacheRequests.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::QueueShaderPrecacheRequest( const char *pszName, int nStaticIndex, int nDynamicIndex )
{
	AUTO_LOCK( m_PrecacheMutex );
	if ( !m_bPrecacheAccepting )
	{
		Warning( "shader_precache: rejected, the renderer is shut down\n" );
		return;
	}
	m_PrecacheRequests.AddToTail( { CUtlString( pszName ), nStaticIndex, nDynamicIndex } );
}

//-----------------------------------------------------------------------------
// Purpose: Runs the queued requests on the recording owner and reports totals.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ProcessShaderPrecacheRequests()
{
	CUtlVector<PrecacheRequestDX12> requests;
	{
		AUTO_LOCK( m_PrecacheMutex );
		if ( !m_PrecacheRequests.Count() )
			return;
		requests.Swap( m_PrecacheRequests );
	}
	IFileSystem *pFileSystem = g_pShaderDeviceMgrDX12 ? g_pShaderDeviceMgrDX12->HostFileSystem() : nullptr;
	if ( !pFileSystem || !m_pDevice || !m_pDevice->IsRecordingOwner() )
	{
		Warning( "shader_precache: dropped %d request(s): no filesystem or not on the recording owner\n", requests.Count() );
		return;
	}
	for ( int i = 0; i < requests.Count(); ++i )
	{
		const PrecacheRequestDX12 &request = requests[i];
		PrecacheCounts total;
		if ( !V_stricmp( request.name.String(), "all" ) )
		{
			ForEachPublished( *pFileSystem, "shaders/vsh/*.vcs", VcsStage::Vertex, total );
			ForEachPublished( *pFileSystem, "shaders/psh/*.vcs", VcsStage::Pixel, total );
			ForEachPublished( *pFileSystem, "shaders/csh/*.vcs", VcsStage::Compute, total );
		}
		else
		{
			// A logical name identifies a stage by its _vs/_ps/_cs token; resolve both graphics stages when ambiguous.
			const char *pszName = request.name.String();
			const bool bVertex = V_strstr( pszName, "_vs" ) != nullptr;
			const bool bPixel = V_strstr( pszName, "_ps" ) != nullptr;
			if ( V_strstr( pszName, "_cs" ) )
			{
				total.Add( ValidateFile( *pFileSystem, pszName, VcsStage::Compute, request.staticIndex, request.dynamicIndex ) );
			}
			else
			{
				if ( bVertex || !bPixel )
					total.Add( ValidateFile( *pFileSystem, pszName, VcsStage::Vertex, request.staticIndex, request.dynamicIndex ) );
				if ( bPixel || !bVertex )
					total.Add( ValidateFile( *pFileSystem, pszName, VcsStage::Pixel, request.staticIndex, request.dynamicIndex ) );
			}
		}
		Msg( "shader_precache %s: files=%u statics=%u combos=%u skipped=%u native=%u legacy=%u failures=%u (validation only; PSOs are created per draw state)\n",
		    request.name.String(), total.m_nFiles, total.m_nStatics, total.m_nCombos, total.m_nSkipped, total.m_nNative, total.m_nLegacy, total.m_nFailures );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Opens/closes the request queue; closing drops pending requests.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetShaderPrecacheAccepting( bool bAccepting )
{
	AUTO_LOCK( m_PrecacheMutex );
	m_bPrecacheAccepting = bAccepting;
	if ( !bAccepting )
		m_PrecacheRequests.Purge();
}

} // namespace shaderapidx12

//-----------------------------------------------------------------------------
// Purpose: shader_precache <logicalName> [staticIndex] [dynamicIndex] | shader_precache all
//-----------------------------------------------------------------------------
static void ShaderPrecacheCommand( const CCommand &args )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "usage: shader_precache <logicalName> [staticIndex] [dynamicIndex] | shader_precache all\n" );
		return;
	}
	if ( !shaderapidx12::g_pShaderAPIDX12 )
	{
		Warning( "shader_precache: rejected, the DX12 renderer is not active\n" );
		return;
	}
	const int nStaticIndex = args.ArgC() > 2 ? V_atoi( args[2] ) : -1;
	const int nDynamicIndex = args.ArgC() > 3 ? V_atoi( args[3] ) : -1;
	if ( ( args.ArgC() > 2 && nStaticIndex < 0 ) || ( args.ArgC() > 3 && nDynamicIndex < 0 ) )
	{
		Warning( "shader_precache: indices must be non-negative (static is the already-multiplied Source index)\n" );
		return;
	}
	shaderapidx12::g_pShaderAPIDX12->QueueShaderPrecacheRequest( args[1], nStaticIndex, nDynamicIndex );
	Msg( "shader_precache: queued %s; results are reported by the render thread at the next frame\n", args[1] );
}

// Development command: FCVAR_CHEAT (requires sv_cheats 1). Not FCVAR_DEVELOPMENTONLY, which retail engines refuse
// to dispatch at all ("Unknown command").
static ConCommand shader_precache( "shader_precache", ShaderPrecacheCommand,
    "Development only: validate/reflect published VCS payloads (logical name [static] [dynamic], or 'all') on the render thread", FCVAR_CHEAT );
