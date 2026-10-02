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

//-----------------------------------------------------------------------------
// Purpose: Reflects a native payload's space-1 cbuffers against the backend engine layouts; material
//          blocks are reported by name/register/size (their layout hash is checked at draw time against
//          the bridge write).
//-----------------------------------------------------------------------------
static bool ReflectNative( const VcsPayload &payload, VcsStage stage, CUtlString &error )
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
	if ( nType != static_cast<UINT>( stage == VcsStage::Vertex ? D3D12_SHVER_VERTEX_SHADER : D3D12_SHVER_PIXEL_SHADER ) )
	{
		error = "DXBC stage does not match the VCS stage";
		return false;
	}
	CUtlVector<ShaderInputElementDX12> inputs;
	if ( !ReadShaderInputSignatureDX12( payload.tokens.Base(), payload.tokens.Count(), inputs ) )
	{
		error = "input signature reflection failed";
		return false;
	}
	for ( UINT i = 0; i < desc.BoundResources; ++i )
	{
		D3D12_SHADER_INPUT_BIND_DESC binding{};
		if ( FAILED( reflection->GetResourceBindingDesc( i, &binding ) ) )
		{
			error = "resource binding reflection failed";
			return false;
		}
		if ( binding.Type != D3D_SIT_CBUFFER || binding.Space != 1 )
			continue;
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
		const unsigned nFirst = stage == VcsStage::Vertex ? 2u : 1u;
		bool bEngine = false;
		for ( const dx12native::EngineCBufferLayoutDX12 &layout : dx12native::kEngineCBufferLayouts )
			bEngine |= !V_strcmp( layout.name, binding.Name );
		if ( !bEngine && ( binding.BindPoint < nFirst || binding.BindPoint > 7 || ( bufferDesc.Size & 15 ) || bufferDesc.Size > 65536 ) )
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
	const char *pszStage = stage == VcsStage::Vertex ? "vs" : "ps";
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
			if ( !ReflectNative( *pPayload, stage, error ) )
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
//          runtime): every file present under shaders/vsh and shaders/psh.
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
		}
		else
		{
			// A logical name identifies a stage by its _vs/_ps token; resolve both stages when ambiguous.
			const char *pszName = request.name.String();
			const bool bVertex = V_strstr( pszName, "_vs" ) != nullptr;
			const bool bPixel = V_strstr( pszName, "_ps" ) != nullptr;
			if ( bVertex || !bPixel )
				total.Add( ValidateFile( *pFileSystem, pszName, VcsStage::Vertex, request.staticIndex, request.dynamicIndex ) );
			if ( bPixel || !bVertex )
				total.Add( ValidateFile( *pFileSystem, pszName, VcsStage::Pixel, request.staticIndex, request.dynamicIndex ) );
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
