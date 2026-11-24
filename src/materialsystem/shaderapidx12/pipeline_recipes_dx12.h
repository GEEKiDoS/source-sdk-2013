#pragma once
#include "persistent_pipeline_dx12.h"
#include <algorithm>
#include <map>

namespace shaderapidx12
{
// Descriptor schema for GetOrCreate (not a dump of a pointer-bearing D3D descriptor).
// Bump the magic if this schema or the root-signature ABIs change. Shader bytes are
// interned, so several hundred blend/layout variants do not duplicate a shader.
class CPipelineRecipesDX12
{
	using Bytes = std::vector<unsigned char>;
	struct Element
	{
		char name[64];
		UINT index, format, slot, offset, classification, step;
	};
	struct State
	{
		uint64_t key, stages[3];
		UINT root, topology, colors, elements;
		DXGI_FORMAT rt[8], ds;
		DXGI_SAMPLE_DESC samples;
		D3D12_RASTERIZER_DESC raster;
		D3D12_BLEND_DESC blend;
		D3D12_DEPTH_STENCIL_DESC depth;
	};
	struct Recipe { State state; std::vector<Element> layout; };
	std::map<uint64_t, Bytes> m_Blobs;
	std::map<uint64_t, Recipe> m_Recipes;
	std::map<uint64_t, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_Resident;
	std::vector<uint64_t> m_Pending;
	unsigned m_Next = 0;
	size_t m_Bytes = 0;
	size_t m_ExpandedRecipes = 0;
	bool m_Dirty = false;
	wchar_t m_Path[MAX_PATH]{};
	static constexpr UINT kMagic = 0x31525350, kLimit = 8192, kBytes = 96 * 1024 * 1024;
	static uint64_t Hash( const unsigned char *bytes, size_t size )
	{
		uint64_t h = 14695981039346656037ull;
		while ( size-- ) h = ( h ^ *bytes++ ) * 1099511628211ull;
		return h;
	}
	bool Read( FILE *f )
	{
		UINT header[3];
		if ( fread( header, sizeof( header ), 1, f ) != 1 || header[0] != kMagic || header[1] > kLimit || header[2] > kLimit ) return false;
		for ( UINT i = 0; i < header[1]; ++i )
		{
			uint64_t id; UINT size;
			if ( !fread( &id, sizeof( id ), 1, f ) || !fread( &size, sizeof( size ), 1, f ) || !size || size > kBytes ) return false;
			if ( !m_Blobs.count( id ) && ( size > kBytes - m_Bytes || m_Blobs.size() >= kLimit ) ) return false;
			Bytes data( size );
			if ( !fread( data.data(), size, 1, f ) || Hash( data.data(), size ) != id ) return false;
			if ( m_Blobs.emplace( id, std::move( data ) ).second ) m_Bytes += size;
		}
		for ( UINT i = 0; i < header[2]; ++i )
		{
			Recipe r; State &s = r.state;
			if ( !fread( &s, sizeof( s ), 1, f ) || s.root > 2 || s.colors > 8 || s.elements > 32 ) return false;
			for ( auto id : s.stages ) if ( id && !m_Blobs.count( id ) ) return false;
			r.layout.resize( s.elements );
			if ( fread( r.layout.data(), sizeof( Element ), s.elements, f ) != s.elements ) return false;
			for ( const Element &el : r.layout ) if ( !memchr( el.name, 0, sizeof( el.name ) ) ) return false;
			if ( m_Recipes.size() < kLimit && m_Recipes.emplace( s.key, std::move( r ) ).second ) m_Pending.push_back( s.key );
		}
		return true;
	}
public:
	void Open( const wchar_t *path, IFileSystem *fs )
	{
		wcscpy_s( m_Path, path );
		FILE *f = nullptr;
		if ( !_wfopen_s( &f, path, L"rb" ) )
		{
			if ( !Read( f ) ) DevWarning( "DX12: incomplete local PSO recipes: %ls\n", path );
			fclose( f );
		}
		char seed[MAX_PATH];
		if ( fs && fs->RelativePathToFullPath( "shaders/pso/graphics-v1.recipes", "GAME", seed, sizeof( seed ) ) && !fopen_s( &f, seed, "rb" ) )
		{
			if ( !Read( f ) ) DevWarning( "DX12: incomplete bundled PSO recipes: %s\n", seed );
			fclose( f );
		}
	}
	void Close()
	{
		if ( m_Dirty && m_Path[0] )
		{
			wchar_t tmp[MAX_PATH + 32]; swprintf_s( tmp, L"%s.%u.tmp", m_Path, GetCurrentProcessId() );
			FILE *f = nullptr;
			if ( !_wfopen_s( &f, tmp, L"wb" ) )
			{
				UINT header[] = { kMagic, UINT( m_Blobs.size() ), UINT( m_Recipes.size() ) };
				bool ok = fwrite( header, sizeof( header ), 1, f ) == 1;
				for ( const auto &blob : m_Blobs )
				{
					UINT size = UINT( blob.second.size() );
					ok = ok && fwrite( &blob.first, sizeof( blob.first ), 1, f ) && fwrite( &size, sizeof( size ), 1, f ) && fwrite( blob.second.data(), size, 1, f );
				}
				for ( const auto &it : m_Recipes )
				{
					const Recipe &r = it.second;
					ok = ok && fwrite( &r.state, sizeof( State ), 1, f ) && fwrite( r.layout.data(), sizeof( Element ), r.layout.size(), f ) == r.layout.size();
				}
				if ( !fclose( f ) && ok ) MoveFileExW( tmp, m_Path, MOVEFILE_REPLACE_EXISTING );
				DeleteFileW( tmp );
			}
		}
		*this = CPipelineRecipesDX12();
	}
	ID3D12PipelineState *Find( uint64_t key ) const
	{
		auto it = m_Resident.find( key ); return it == m_Resident.end() ? nullptr : it->second.Get();
	}
	void Record( uint64_t key, UINT root, const D3D12_GRAPHICS_PIPELINE_STATE_DESC &d, ID3D12PipelineState *pso )
	{
		if ( !m_Path[0] ) return;
		if ( m_Recipes.count( key ) ) { m_Resident[key] = pso; return; }
		if ( m_Recipes.size() >= kLimit || d.InputLayout.NumElements > 32 ) return;
		Recipe r{}; State &s = r.state;
		s.key = key; s.root = root; s.topology = d.PrimitiveTopologyType; s.colors = d.NumRenderTargets; s.ds = d.DSVFormat; s.samples = d.SampleDesc;
		s.raster = d.RasterizerState; s.blend = d.BlendState; s.depth = d.DepthStencilState; memcpy( s.rt, d.RTVFormats, sizeof( s.rt ) );
		s.elements = d.InputLayout.NumElements;
		for ( UINT i = 0; i < s.elements; ++i )
		{
			const auto &el = d.InputLayout.pInputElementDescs[i];
			Element out{ {}, el.SemanticIndex, UINT( el.Format ), el.InputSlot, el.AlignedByteOffset, UINT( el.InputSlotClass ), el.InstanceDataStepRate };
			if ( strlen( el.SemanticName ) >= sizeof( out.name ) ) return;
			strcpy_s( out.name, el.SemanticName ); r.layout.push_back( out );
		}
		unsigned i = 0;
		for ( auto stage : { d.VS, d.PS, d.GS } )
		{
			if ( stage.BytecodeLength )
			{
				auto *bytes = static_cast<const unsigned char *>( stage.pShaderBytecode );
				auto id = s.stages[i] = Hash( bytes, stage.BytecodeLength );
				if ( !m_Blobs.count( id ) )
				{
					if ( stage.BytecodeLength > kBytes - m_Bytes || m_Blobs.size() >= kLimit ) return;
					m_Blobs.emplace( id, Bytes( bytes, bytes + stage.BytecodeLength ) ); m_Bytes += stage.BytecodeLength;
				}
			}
			++i;
		}
		m_Recipes.emplace( key, std::move( r ) ); m_Resident[key] = pso; m_Dirty = true;
	}
	// Called only while the material owner is drained. Workers read immutable recipes;
	// publication and library mutation happen on the owner after the workers join.
	unsigned Total() const { return unsigned( m_Pending.size() ); }
	unsigned Next() const { return m_Next; }
	uint64_t Pop() { return m_Pending[m_Next++]; }
private:
	void Describe( const Recipe &r, const std::vector<Element> &input, ID3D12RootSignature *root, D3D12_INPUT_ELEMENT_DESC (&layout)[32],
		D3D12_GRAPHICS_PIPELINE_STATE_DESC &d ) const
	{
		const State &s = r.state;
		d.pRootSignature = root;
		D3D12_SHADER_BYTECODE *stages[] = { &d.VS, &d.PS, &d.GS };
		for ( unsigned i = 0; i < 3; ++i ) if ( s.stages[i] ) { const Bytes &b = m_Blobs.at( s.stages[i] ); *stages[i] = { b.data(), b.size() }; }
		for ( size_t i = 0; i < input.size(); ++i ) { const auto &el = input[i]; layout[i] = { el.name, el.index, DXGI_FORMAT( el.format ), el.slot, el.offset, D3D12_INPUT_CLASSIFICATION( el.classification ), el.step }; }
		d.InputLayout = { layout, UINT( input.size() ) }; d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE( s.topology );
		d.NumRenderTargets = s.colors; memcpy( d.RTVFormats, s.rt, sizeof( s.rt ) ); d.DSVFormat = s.ds; d.SampleDesc = s.samples; d.SampleMask = UINT_MAX;
		d.RasterizerState = s.raster; d.BlendState = s.blend; d.DepthStencilState = s.depth;
	}
	void WarmDescriptor( const D3D12_GRAPHICS_PIPELINE_STATE_DESC &d, UINT root, ID3D12Device *device, CPersistentPipelineDX12 &lib )
	{
		const uint64_t key = lib.Key( d, root );
		if ( m_Resident.count( key ) || m_Resident.size() >= kLimit ) return;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
		if ( !lib.Load( key, d, pso.GetAddressOf() ) )
		{
			if ( FAILED( device->CreateGraphicsPipelineState( &d, IID_PPV_ARGS( &pso ) ) ) ) return;
			lib.Store( key, pso.Get() );
		}
		m_Resident.emplace( key, std::move( pso ) );
	}
public:
	// The same material pass can arrive through different mesh streams. A
	// declaration observed with the identical VS is already signature-compatible;
	// don't wait for that declaration *and* this material to be drawn together.
	void WarmLayouts( ID3D12Device *device, ID3D12RootSignature *const roots[3], CPersistentPipelineDX12 &lib )
	{
		if ( m_ExpandedRecipes == m_Recipes.size() ) return;
		m_ExpandedRecipes = m_Recipes.size();
		using Layout = std::vector<Element>;
		std::map<uint64_t, std::vector<const Layout *>> layouts;
		const auto same = []( const Layout &a, const Layout &b ) {
			return a.size() == b.size() && ( a.empty() || !memcmp( a.data(), b.data(), a.size() * sizeof( Element ) ) );
		};
		for ( const auto &it : m_Recipes )
		{
			auto &seen = layouts[it.second.state.stages[0]];
			if ( std::none_of( seen.begin(), seen.end(), [&]( const Layout *l ) { return same( *l, it.second.layout ); } ) )
				seen.push_back( &it.second.layout );
		}
		for ( const auto &it : m_Recipes )
		{
			const auto &r = it.second;
			if ( !roots[r.state.root] ) continue;
			for ( auto *input : layouts.at( r.state.stages[0] ) )
			{
				if ( same( *input, r.layout ) ) continue;
				D3D12_INPUT_ELEMENT_DESC layout[32];
				D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
				Describe( r, *input, roots[r.state.root], layout, d );
				WarmDescriptor( d, r.state.root, device, lib );
			}
		}
	}
	void Replay( uint64_t key, ID3D12Device *device, ID3D12RootSignature *const roots[3], CPersistentPipelineDX12 &lib,
		Microsoft::WRL::ComPtr<ID3D12PipelineState> &out, bool &created ) const
	{
		const auto &r = m_Recipes.at( key ); const auto &s = r.state;
		if ( !roots[s.root] ) return;
		D3D12_INPUT_ELEMENT_DESC layout[32];
		D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
		Describe( r, r.layout, roots[s.root], layout, d );
		if ( lib.Key( d, s.root ) != key ) return;
		if ( !lib.Load( key, d, out.GetAddressOf() ) ) created = SUCCEEDED( device->CreateGraphicsPipelineState( &d, IID_PPV_ARGS( &out ) ) );
	}
	// A material's coverage and early-test choices can change between views.
	// Reuse the observed mesh layout, MRTs, root and raster state; substitute
	// the actual twin selected for the same material combo. Predicted descriptors
	// are device-resident only, not added to the portable observations.
	void WarmPixelVariant( D3D12_SHADER_BYTECODE from, D3D12_SHADER_BYTECODE to, bool readOnlyDepth,
		ID3D12Device *device, ID3D12RootSignature *const roots[3], CPersistentPipelineDX12 &lib )
	{
		if ( m_Recipes.empty() ) return;
		const auto id = Hash( static_cast<const unsigned char *>( from.pShaderBytecode ), from.BytecodeLength );
		if ( !m_Blobs.count( id ) ) return;
		for ( const auto &it : m_Recipes )
		{
			const State &s = it.second.state;
			if ( s.stages[1] != id || !roots[s.root] || !s.colors || !s.blend.RenderTarget[0].RenderTargetWriteMask ||
				s.blend.RenderTarget[0].BlendEnable ) continue;
			if ( readOnlyDepth ? !s.depth.DepthEnable || !s.depth.DepthWriteMask : s.root != 2 || s.blend.AlphaToCoverageEnable ) continue;
			D3D12_INPUT_ELEMENT_DESC layout[32];
			D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
			Describe( it.second, it.second.layout, roots[s.root], layout, d );
			d.PS = to;
			if ( readOnlyDepth )
			{
				d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
				d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
			}
			WarmDescriptor( d, s.root, device, lib );
		}
	}
	void Publish( uint64_t key, ID3D12PipelineState *pso ) { m_Resident[key] = pso; }
};
}
