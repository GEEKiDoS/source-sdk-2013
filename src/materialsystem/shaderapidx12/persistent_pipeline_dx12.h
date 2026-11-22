#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <set>
#include <vector>
#include <cstdio>

namespace shaderapidx12
{
// The driver validates the adapter, driver and root signature. Each entry is
// additionally keyed by the actual compiled shader bytes, not record addresses
// or just VCS source CRCs (compiler changes can preserve a source CRC).
class CPersistentPipelineDX12
{
public:
	void Open( ID3D12Device *device )
	{
		Microsoft::WRL::ComPtr<ID3D12Device1> device1;
		if ( FAILED( device->QueryInterface( IID_PPV_ARGS( &device1 ) ) ) )
			return;
		wchar_t dir[MAX_PATH];
		const DWORD length = GetEnvironmentVariableW( L"LOCALAPPDATA", dir, ARRAYSIZE( dir ) );
		if ( !length || length >= ARRAYSIZE( dir ) - 80 )
			return;
		wcscat_s( dir, L"\\SourceDX12" );
		CreateDirectoryW( dir, nullptr );
		const LUID id = device->GetAdapterLuid();
		Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		LARGE_INTEGER driver{};
		if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( &factory ) ) ) ||
			FAILED( factory->EnumAdapterByLuid( id, IID_PPV_ARGS( &adapter ) ) ) )
			return;
		// Some DXGI drivers don't expose a UMD version. In that case
		// CreatePipelineLibrary still rejects a blob from a different driver.
		adapter->CheckInterfaceSupport( __uuidof( IDXGIDevice ), &driver );
		swprintf_s( m_Path, L"%s\\pipeline-v1-%08x%08x-%016llx.bin", dir, id.HighPart, id.LowPart, driver.QuadPart );
		FILE *file = nullptr;
		if ( !_wfopen_s( &file, m_Path, L"rb" ) )
		{
			uint32_t count;
			if ( fread( &count, sizeof( count ), 1, file ) == 1 && count && count <= kMaxEntries )
			{
				std::vector<uint64_t> keys( count );
				if ( fread( keys.data(), sizeof( uint64_t ), count, file ) == count )
				{
					const long start = ftell( file );
					fseek( file, 0, SEEK_END );
					const long size = ftell( file ) - start;
					if ( size > 0 && size <= kMaxBytes && !fseek( file, start, SEEK_SET ) )
					{
						m_Backing.resize( size );
						if ( fread( m_Backing.data(), 1, size, file ) == size && SUCCEEDED( device1->CreatePipelineLibrary( m_Backing.data(), size, IID_PPV_ARGS( &m_Library ) ) ) )
							m_Keys.insert( keys.begin(), keys.end() );
					}
				}
			}
			fclose( file );
		}
		if ( !m_Library )
		{
			m_Backing.clear();
			device1->CreatePipelineLibrary( nullptr, 0, IID_PPV_ARGS( &m_Library ) );
		}
	}

	void Close()
	{
		if ( m_Dirty && m_Library )
		{
			const size_t bytes = m_Library->GetSerializedSize();
			if ( bytes && bytes <= kMaxBytes )
			{
				std::vector<unsigned char> blob( bytes );
				if ( SUCCEEDED( m_Library->Serialize( blob.data(), bytes ) ) )
				{
					wchar_t tmp[MAX_PATH + 32];
					swprintf_s( tmp, L"%s.%u.tmp", m_Path, GetCurrentProcessId() );
					FILE *file = nullptr;
					if ( !_wfopen_s( &file, tmp, L"wb" ) )
					{
						const uint32_t count = uint32_t( m_Keys.size() );
						const std::vector<uint64_t> keys( m_Keys.begin(), m_Keys.end() );
						const bool written = fwrite( &count, sizeof( count ), 1, file ) == 1 && fwrite( keys.data(), sizeof( uint64_t ), count, file ) == count && fwrite( blob.data(), 1, bytes, file ) == bytes;
						if ( !fclose( file ) && written )
							MoveFileExW( tmp, m_Path, MOVEFILE_REPLACE_EXISTING );
						DeleteFileW( tmp );
					}
				}
			}
		}
		m_Library.Reset();
		std::vector<unsigned char>().swap( m_Backing );
		m_Keys.clear();
		m_Dirty = false;
	}

	bool Enabled() const { return m_Library != nullptr; }
	uint64_t Key( D3D12_GRAPHICS_PIPELINE_STATE_DESC desc, unsigned root ) const
	{
		uint64_t hash = 14695981039346656037ull;
		Mix( hash, &root, sizeof( root ) );
		desc.pRootSignature = nullptr;
		for ( D3D12_SHADER_BYTECODE *stage : { &desc.VS, &desc.PS, &desc.GS } )
		{
			Mix( hash, stage->pShaderBytecode, stage->BytecodeLength );
			stage->pShaderBytecode = nullptr;
		}
		for ( UINT i = 0; i < desc.InputLayout.NumElements; ++i )
		{
			D3D12_INPUT_ELEMENT_DESC el = desc.InputLayout.pInputElementDescs[i];
			Mix( hash, el.SemanticName, strlen( el.SemanticName ) + 1 );
			// Exclude pointer and tail padding, which are not stable across launches.
			Mix( hash, &el.SemanticIndex, 6 * sizeof( UINT ) );
		}
		desc.InputLayout.pInputElementDescs = nullptr;
		Mix( hash, &desc, sizeof( desc ) );
		return hash;
	}

	bool Load( uint64_t key, const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc, ID3D12PipelineState **out )
	{
		if ( !m_Keys.count( key ) )
			return false;
		wchar_t name[17];
		swprintf_s( name, L"%016llx", key );
		return SUCCEEDED( m_Library->LoadGraphicsPipeline( name, &desc, IID_PPV_ARGS( out ) ) );
	}

	void Store( uint64_t key, ID3D12PipelineState *pso )
	{
		if ( !m_Library || m_Keys.count( key ) || m_Keys.size() >= kMaxEntries )
			return;
		wchar_t name[17];
		swprintf_s( name, L"%016llx", key );
		if ( SUCCEEDED( m_Library->StorePipeline( name, pso ) ) )
		{
			m_Keys.insert( key );
			m_Dirty = true;
		}
	}

private:
	static void Mix( uint64_t &hash, const void *data, size_t bytes )
	{
		const unsigned char *p = static_cast<const unsigned char *>( data );
		for ( size_t i = 0; i < bytes; ++i )
			hash = ( hash ^ p[i] ) * 1099511628211ull;
	}
	static constexpr size_t kMaxEntries = 65536, kMaxBytes = 128 * 1024 * 1024;
	wchar_t m_Path[MAX_PATH]{};
	std::set<uint64_t> m_Keys;
	// CreatePipelineLibrary borrows this buffer for its entire lifetime.
	std::vector<unsigned char> m_Backing;
	Microsoft::WRL::ComPtr<ID3D12PipelineLibrary> m_Library;
	bool m_Dirty = false;
};
}
