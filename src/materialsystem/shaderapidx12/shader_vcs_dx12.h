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

struct ComboFoldDimensionDX12
{
	CUtlString name;
	int32_t minimum = 0, maximum = 0;
	int slot = -1; // -1: retained native radix; otherwise runtime uint slot.
};

struct ComboFoldTableDX12
{
	CUtlString name;
	VcsStage stage = VcsStage::Pixel;
	uint32_t originalStaticCount = 1, originalDynamicCount = 1;
	uint32_t nativeStaticCount = 1, nativeDynamicCount = 1;
	CUtlVector<ComboFoldDimensionDX12> statics, dynamics;
};

struct ComboFoldProjectionDX12
{
	uint32_t staticIndex = 0, dynamicIndex = 0;
	uint32_t payload[64]{};
};

// Pure mixed-radix projection. Input static index is already multiplied by originalDynamicCount.
bool ProjectComboFoldDX12( const ComboFoldTableDX12 &table, int64_t staticIndex,
	int64_t dynamicIndex, ComboFoldProjectionDX12 &projection );

// Parser for combo-fold descriptions (the native metadata trailer carries its shader's table in this text form).
// Tables are heap-owned to keep returned pointers stable; caller owns the vector's contents.
bool ParseComboFoldTextDX12( const char *text, CUtlVector<ComboFoldTableDX12 *> &tables, CUtlString &error );

// Native metadata trailer (ShaderCompiler3) appended to a v6 VCS after its last static block. All fields are uint32.
//   magic kVcsNativeMetadataMagic, version 1, byte size (multiple of 4, includes this header), flags,
//   lightmap sampler mask, fold-text byte count, then the fold text (empty or one "fold ... end" table), zero-padded.
inline constexpr uint32_t kVcsNativeMetadataMagic = 0x4d334353u; // "SC3M"
enum : uint32_t
{
	kVcsNativeHighresAbi = 1u << 0,		// highres lightmap receiver ABI
	kVcsNativeSamplerRoles = 1u << 1,	// lightmap sampler roles are declared (the mask may still be 0)
	kVcsNativeEarlyDepthTwin = 1u << 2,	// a forced-early-depth PS twin is published beside this shader
};

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

	// The first existing GAME path is authoritative (shaders/vsh|psh|csh, then shaders/fxc for vertex/pixel names);
	// an invalid file is an error, not a reason to search the other location.
	bool Open( IFileSystem &filesystem, const char *pszName, VcsStage stage, CUtlString &error );

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
	uint32_t TotalComboCount() const { return m_nTotalCount; }

	uint32_t Flags() const { return m_nFlags; }

	uint32_t CentroidMask() const { return m_nCentroidMask; }

	uint32_t SourceCRC() const { return m_nSourceCRC; }
	uint32_t LightmapSamplerMask() const { return m_nLightmapSamplerMask; }
	bool HighresAbi() const { return m_bHighresAbi; }
	bool SamplerRolesReady() const { return m_bSamplerRolesReady; }
	bool NativeCasterTwin() const { return m_bNativeCasterTwin; }
	// Optional safe PS twin, named by inserting "_earlydepth" before "_ps51".
	bool EarlyDepthTwin() const { return m_bEarlyDepthTwin; }
	// Combo fold of a folded native shader (from its metadata trailer), else nullptr.
	const ComboFoldTableDX12 *ComboFold() const { return m_ComboFold.Count() ? m_ComboFold[0] : nullptr; }

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
	bool ParseNativeMetadata( const uint8_t *pBytes, size_t nStart, size_t nLength, CUtlString &error );
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
	uint32_t m_nLightmapSamplerMask = 0;
	bool m_bHighresAbi = false, m_bSamplerRolesReady = false;
	bool m_bNativeCasterTwin = false;
	bool m_bEarlyDepthTwin = false;
	bool m_bNativeMetadata = false;
	CUtlVector<ComboFoldTableDX12 *> m_ComboFold;
	size_t m_nPayloadStart = 0;
	size_t m_nHeaderBytes = 28;
};

} // namespace shaderapidx12

#endif // SHADER_VCS_DX12_H
