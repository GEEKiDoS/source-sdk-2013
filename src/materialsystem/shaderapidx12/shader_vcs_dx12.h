//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: CPU-only, bounded reader for Source VCS shader files. No renderer objects.
//
//=============================================================================//
#ifndef SHADER_VCS_DX12_H
#define SHADER_VCS_DX12_H
#pragma once

#include "tier1/utlmap.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"

class IFileSystem;

namespace shaderapidx12
{

enum class VcsStage
{
	Vertex,
	Pixel,
	Compute
};

// LegacyShaderName() result for a native-only logical: it has no shaders/fxc record to fall back to.
inline constexpr const char *kNativeOnlyMarker = "-";

struct VcsPayload
{
	// Owned by ShaderVcsFile; valid until the file is reopened or destroyed.
	CUtlVector<uint8> tokens;
};

class ShaderVcsFile
{
public:
	ShaderVcsFile();
	~ShaderVcsFile();

	ShaderVcsFile( const ShaderVcsFile & ) = delete;
	ShaderVcsFile &operator=( const ShaderVcsFile & ) = delete;

	// The first existing GAME path is authoritative; an invalid file is an error,
	// not a reason to search a different shader in fxc. Native-only map markers
	// fail closed when their native record is missing.
	bool Open( IFileSystem &filesystem, const char *pszName, VcsStage stage, CUtlString &error );
	// Legacy DX9 logical for a native name, kNativeOnlyMarker for a native-only logical, else name.
	static CUtlString LegacyShaderName( IFileSystem &filesystem, const char *pszName, VcsStage stage );

	// Also usable for deterministic byte-for-byte fixtures without a filesystem.
	bool OpenBytes( const uint8_t *pBytes, size_t nLength, VcsStage stage,
	    const char *pszLabel, CUtlString &error );

	// nStaticIndex is the already-multiplied Source static combo index.
	// Decodes once per nStaticIndex. Skipped dynamics are nullptr, never combo zero.
	bool LoadStaticCombo( uint32_t nStaticIndex, CUtlString &error );
	const VcsPayload *DynamicPayload( uint32_t nStaticIndex, uint32_t nDynamicIndex ) const;
	// Enumerates present canonical records and aliases without assuming combo zero exists.
	bool StaticComboIndex( size_t nOrdinal, uint32_t &nIndex ) const;

	uint32_t Version() const { return m_nVersion; }

	uint32_t DynamicComboCount() const { return m_nDynamicCount; }

	uint32_t Flags() const { return m_nFlags; }

	uint32_t CentroidMask() const { return m_nCentroidMask; }

	uint32_t SourceCRC() const { return m_nSourceCRC; }

	const char *Path() const { return m_Path.Get(); }

private:
	struct StaticRecord
	{
		uint32_t m_nId;
		uint32_t m_nOffset;
	};

	struct AliasRecord
	{
		uint32_t m_nId;
		uint32_t m_nSource;
	};

	struct StaticCombos
	{
		// CUtlMap nodes may relocate as it grows; payload objects stay heap-owned
		// so DynamicPayload references remain valid until reopen/destruction.
		CUtlMap<uint32_t, VcsPayload *, uint32_t> m_Dynamics;

		StaticCombos()
		    : m_Dynamics( DefLessFunc( uint32_t ) ) {}

		StaticCombos( const StaticCombos & ) = delete;
		StaticCombos &operator=( const StaticCombos & ) = delete;

		~StaticCombos() { m_Dynamics.PurgeAndDeleteElements(); }
	};

	bool DecodeV4( uint32_t nStaticIndex, StaticCombos &combos, CUtlString &error ) const;
	bool DecodeBlocks( uint32_t nStaticIndex, uint32_t nRecordID, size_t nStart,
	    size_t nEnd, StaticCombos &combos, CUtlString &error ) const;
	bool ParseBytes( const uint8_t *pBytes, size_t nLength, VcsStage stage,
	    const char *pszLabel, CUtlString &error );
	bool ValidateTokens( const uint8_t *pData, size_t nSize, CUtlString &error ) const;
	bool Fail( const char *pszMessage, CUtlString &error ) const;

	CUtlVector<uint8> m_File;
	CUtlVector<uint8> m_Reference;
	CUtlVector<StaticRecord> m_Records;
	CUtlVector<AliasRecord> m_Aliases;
	// Heap ownership keeps pointers stable when CUtlMap grows or rebalances.
	CUtlMap<uint32_t, StaticCombos *, uint32_t> m_Cache;
	CUtlMap<uint32_t, const StaticCombos *, uint32_t> m_LoadedStatics;
	CUtlString m_Path;
	VcsStage m_Stage = VcsStage::Vertex;
	uint32_t m_nVersion = 0;
	uint32_t m_nTotalCount = 0;
	uint32_t m_nDynamicCount = 0;
	uint32_t m_nFlags = 0;
	uint32_t m_nCentroidMask = 0;
	uint32_t m_nSourceCRC = 0;
	size_t m_nPayloadStart = 0;
	size_t m_nHeaderBytes = 28;
};

} // namespace shaderapidx12

#endif // SHADER_VCS_DX12_H
