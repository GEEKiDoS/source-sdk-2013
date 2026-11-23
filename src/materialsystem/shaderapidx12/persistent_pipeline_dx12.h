#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <set>
#include <vector>
#include <cstdio>
#include <string>
#include "filesystem.h"

namespace shaderapidx12
{
// The driver validates the adapter, driver and root signature. Each entry is
// additionally keyed by the actual compiled shader bytes, not record addresses
// or just VCS source CRCs (compiler changes can preserve a source CRC).
class CPersistentPipelineDX12
{
public:
	void Open( ID3D12Device *device, IFileSystem *filesystem )
	{
		wchar_t dir[MAX_PATH];
		// Give capture/QA runs a private cache without disturbing the user's library
		// (even if the game or the harness is forcibly terminated).
		DWORD length = GetEnvironmentVariableW( L"SOURCE_DX12_CACHE_HOME", dir, ARRAYSIZE( dir ) );
		if ( !length )
			length = GetEnvironmentVariableW( L"LOCALAPPDATA", dir, ARRAYSIZE( dir ) );
		if ( !length || length >= ARRAYSIZE( dir ) - 96 )
			return;
		wcscat_s( dir, L"\\SourceDX12" );
		CreateDirectoryW( dir, nullptr );
		uint64_t pack;
		if ( !filesystem || !PackIdentity( *filesystem, pack ) )
			return;
		swprintf_s( m_RecipePath, L"%s\\graphics-v1-%016llx.recipes", dir, pack );
		Microsoft::WRL::ComPtr<ID3D12Device1> device1;
		if ( FAILED( device->QueryInterface( IID_PPV_ARGS( &device1 ) ) ) )
			return;
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
		DXGI_ADAPTER_DESC desc{};
		if ( FAILED( adapter->GetDesc( &desc ) ) )
			return;
		swprintf_s( m_Prefix, L"%s\\pipeline-v1-%04x-%04x-%08x-", dir, desc.VendorId, desc.DeviceId, desc.SubSysId );
		swprintf_s( m_Path, L"%s%016llx-%016llx.bin", m_Prefix, driver.QuadPart, pack );
		// A legacy name can only be attributed to this adapter for its current LUID.
		swprintf_s( m_LegacyPrefix, L"%s\\pipeline-v1-%08x%08x-", dir, id.HighPart, id.LowPart );
		FILE *file = nullptr;
		if ( !_wfopen_s( &file, m_Path, L"rb" ) )
		{
			uint32_t count;
			if ( fread( &count, sizeof( count ), 1, file ) == 1 && count && count <= kMaxEntries )
			{
				std::vector<uint64_t> keys( count );
				if ( fread( keys.data(), sizeof( uint64_t ), count, file ) == count )
				{
					const auto start = _ftelli64( file );
					_fseeki64( file, 0, SEEK_END );
					const auto size = _ftelli64( file ) - start;
					if ( size > kMaxBytes )
						m_DiscardDisk = true;
					if ( size > 0 && size <= kMaxBytes && !_fseeki64( file, start, SEEK_SET ) )
					{
						m_Backing.resize( size );
						if ( fread( m_Backing.data(), 1, size, file ) == size && SUCCEEDED( device1->CreatePipelineLibrary( m_Backing.data(), size, IID_PPV_ARGS( &m_Library ) ) ) )
							m_Keys.insert( keys.begin(), keys.end() );
					}
				}
			}
			fclose( file );
			if ( m_DiscardDisk )
				DeleteFileW( m_Path );
		}
		if ( !m_Library )
		{
			m_Backing.clear();
			device1->CreatePipelineLibrary( nullptr, 0, IID_PPV_ARGS( &m_Library ) );
		}
		if ( m_Library )
			DevMsg( "DX12 pipeline library: %ls (%zu keys)\n", m_Path, m_Keys.size() );
	}

	void Close()
	{
		if ( m_Dirty && m_Library )
		{
			const size_t bytes = m_Library->GetSerializedSize();
			if ( bytes > kMaxBytes )
				m_DiscardDisk = true;
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
						if ( !fclose( file ) && written && MoveFileExW( tmp, m_Path, MOVEFILE_REPLACE_EXISTING ) )
						{
							m_DiscardDisk = false;
							RemoveStale( m_Prefix, false );
							RemoveStale( m_LegacyPrefix, true );
						}
						DeleteFileW( tmp );
					}
				}
			}
		}
		if ( m_DiscardDisk )
			DeleteFileW( m_Path );
		m_DiscardDisk = false;
		m_Library.Reset();
		std::vector<unsigned char>().swap( m_Backing );
		m_Keys.clear();
		m_Dirty = false;
		m_RecipePath[0] = L'\0';
	}

	const wchar_t *RecipePath() const { return m_RecipePath; }
	bool Enabled() const { return m_Library != nullptr; }
	uint64_t Key( const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc, unsigned root ) const
	{
		uint64_t hash = 14695981039346656037ull;
		Mix( hash, &root, sizeof( root ) );
		for ( const D3D12_SHADER_BYTECODE *stage : { &desc.VS, &desc.PS, &desc.GS } )
		{
			Mix( hash, stage->pShaderBytecode, stage->BytecodeLength );
		}
		for ( UINT i = 0; i < desc.InputLayout.NumElements; ++i )
		{
			D3D12_INPUT_ELEMENT_DESC el = desc.InputLayout.pInputElementDescs[i];
			Mix( hash, el.SemanticName, strlen( el.SemanticName ) + 1 );
			// Exclude pointer and tail padding, which are not stable across launches.
			Mix( hash, &el.SemanticIndex, 6 * sizeof( UINT ) );
		}
		// Keep v1's byte positions so an existing zero-padded library key
		// still hits. Copy only defined bytes: in particular assigning InputLayout
		// wholesale from a draw copies four bytes of caller stack padding on x64.
		D3D12_GRAPHICS_PIPELINE_STATE_DESC canonical;
		memset( &canonical, 0, sizeof( canonical ) );
		canonical.VS.BytecodeLength = desc.VS.BytecodeLength;
		canonical.PS.BytecodeLength = desc.PS.BytecodeLength;
		canonical.GS.BytecodeLength = desc.GS.BytecodeLength;
		canonical.InputLayout.NumElements = desc.InputLayout.NumElements;
		canonical.PrimitiveTopologyType = desc.PrimitiveTopologyType;
		canonical.NumRenderTargets = desc.NumRenderTargets;
		memcpy( canonical.RTVFormats, desc.RTVFormats, sizeof( desc.RTVFormats ) );
		canonical.DSVFormat = desc.DSVFormat;
		canonical.SampleDesc = desc.SampleDesc;
		canonical.SampleMask = desc.SampleMask;
		canonical.RasterizerState = desc.RasterizerState;
		canonical.BlendState.AlphaToCoverageEnable = desc.BlendState.AlphaToCoverageEnable;
		canonical.BlendState.IndependentBlendEnable = desc.BlendState.IndependentBlendEnable;
		for ( UINT i = 0; i < ARRAYSIZE( desc.RTVFormats ); ++i )
			memcpy( &canonical.BlendState.RenderTarget[i], &desc.BlendState.RenderTarget[i],
				offsetof( D3D12_RENDER_TARGET_BLEND_DESC, RenderTargetWriteMask ) + sizeof( UINT8 ) );
		memcpy( &canonical.DepthStencilState, &desc.DepthStencilState,
			offsetof( D3D12_DEPTH_STENCIL_DESC, StencilWriteMask ) + sizeof( UINT8 ) );
		canonical.DepthStencilState.FrontFace = desc.DepthStencilState.FrontFace;
		canonical.DepthStencilState.BackFace = desc.DepthStencilState.BackFace;
		Mix( hash, &canonical, sizeof( canonical ) );
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
			// Bound a long-running session too. Don't serialize or touch disk in a draw.
			if ( m_Library->GetSerializedSize() > kMaxBytes )
			{
				Microsoft::WRL::ComPtr<ID3D12Device1> device;
				m_Library->GetDevice( IID_PPV_ARGS( &device ) );
				m_Library.Reset();
				m_Keys.clear();
				std::vector<unsigned char>().swap( m_Backing );
				m_DiscardDisk = true;
				m_Dirty = false;
				if ( device )
					device->CreatePipelineLibrary( nullptr, 0, IID_PPV_ARGS( &m_Library ) );
			}
		}
	}

private:
	static bool PackIdentity( IFileSystem &fs, uint64_t &hash )
	{
		// Directory enumeration order isn't stable. Hash the mounted native pack
		// metadata in path order; entry keys still include the exact bytecode.
		std::set<std::string> paths;
		for ( const char *dir : { "shaders/vsh/", "shaders/psh/", "shaders/csh/" } )
		{
			const std::string pattern = std::string( dir ) + "*.vcs";
			FileFindHandle_t find = FILESYSTEM_INVALID_FIND_HANDLE;
			for ( const char *name = fs.FindFirstEx( pattern.c_str(), "GAME", &find ); name; name = fs.FindNext( find ) )
				if ( !fs.FindIsDirectory( find ) )
					paths.insert( std::string( dir ) + name );
			if ( find != FILESYSTEM_INVALID_FIND_HANDLE )
				fs.FindClose( find );
		}
		hash = 14695981039346656037ull;
		for ( const std::string &path : paths )
		{
			const unsigned size = fs.Size( path.c_str(), "GAME" );
			const long time = fs.GetFileTime( path.c_str(), "GAME" );
			Mix( hash, path.c_str(), path.size() + 1 );
			Mix( hash, &size, sizeof( size ) );
			Mix( hash, &time, sizeof( time ) );
		}
		return !paths.empty();
	}

	void RemoveStale( const wchar_t *prefix, bool legacy ) const
	{
		wchar_t pattern[MAX_PATH];
		swprintf_s( pattern, L"%s*.bin", prefix );
		const size_t prefixLen = wcslen( wcsrchr( prefix, L'\\' ) + 1 );
		WIN32_FIND_DATAW data;
		HANDLE find = FindFirstFileW( pattern, &data );
		if ( find == INVALID_HANDLE_VALUE )
			return;
		do
		{
			if ( data.dwFileAttributes & ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT ) )
				continue;
			const wchar_t *tail = data.cFileName + prefixLen;
			const wchar_t *hex = L"0123456789abcdefABCDEF";
			if ( wcslen( tail ) != ( legacy ? 20u : 37u ) || wcsspn( tail, hex ) != 16 ||
				( !legacy && ( tail[16] != L'-' || wcsspn( tail + 17, hex ) != 16 ) ) )
				continue;
			wchar_t path[MAX_PATH];
			swprintf_s( path, L"%s%s", prefix, tail );
			if ( _wcsicmp( path, m_Path ) )
				DeleteFileW( path );
		} while ( FindNextFileW( find, &data ) );
		FindClose( find );
	}

	static void Mix( uint64_t &hash, const void *data, size_t bytes )
	{
		const unsigned char *p = static_cast<const unsigned char *>( data );
		for ( size_t i = 0; i < bytes; ++i )
			hash = ( hash ^ p[i] ) * 1099511628211ull;
	}
	static constexpr size_t kMaxEntries = 65536, kMaxBytes = 256u * 1024 * 1024;
	wchar_t m_Path[MAX_PATH]{};
	wchar_t m_RecipePath[MAX_PATH]{};
	wchar_t m_Prefix[MAX_PATH]{}, m_LegacyPrefix[MAX_PATH]{};
	bool m_DiscardDisk = false;
	std::set<uint64_t> m_Keys;
	// CreatePipelineLibrary borrows this buffer for its entire lifetime.
	std::vector<unsigned char> m_Backing;
	Microsoft::WRL::ComPtr<ID3D12PipelineLibrary> m_Library;
	bool m_Dirty = false;
};
}
