//========= Copyright Valve Corporation, All rights reserved. ============//
// CPU-only, bounded reader for Source VCS shader files. No renderer objects.
#ifndef SHADER_VCS_DX12_H
#define SHADER_VCS_DX12_H
#pragma once

#include <cstddef>
#include <cstdint>
#include "tier1/utlmap.h"
#include <string>
#include <vector>

class IFileSystem;

namespace shaderapidx12
{

enum class VcsStage { Vertex, Pixel };

struct VcsPayload
{
    // Owned by ShaderVcsFile; valid until the file is reopened or destroyed.
    std::vector<uint8_t> tokens;
};

class ShaderVcsFile
{
public:
    ShaderVcsFile();
    ~ShaderVcsFile();

    ShaderVcsFile(const ShaderVcsFile &) = delete;
    ShaderVcsFile &operator=(const ShaderVcsFile &) = delete;

    // The first existing GAME path is authoritative; an invalid file is an error,
    // not a reason to search a different shader in fxc.
    bool Open(IFileSystem &filesystem, const char *name, VcsStage stage, std::string &error);
    // Legacy DX9 logical for a native <base>_vs51/_ps51 name (shaders/native_dx12_legacy_names.txt), else name.
    static std::string LegacyShaderName(IFileSystem &filesystem, const char *name, VcsStage stage);

    // Also usable for deterministic byte-for-byte fixtures without a filesystem.
    bool OpenBytes(const uint8_t *bytes, size_t length, VcsStage stage,
                   const char *label, std::string &error);

    // staticIndex is the already-multiplied Source static combo index.
    // Decodes once per staticIndex. Skipped dynamics are nullptr, never combo zero.
    bool LoadStaticCombo(uint32_t staticIndex, std::string &error);
    const VcsPayload *DynamicPayload(uint32_t staticIndex, uint32_t dynamicIndex) const;
    // Enumerates present canonical records and aliases without assuming combo zero exists.
    bool StaticComboIndex(size_t ordinal, uint32_t &index) const;

    uint32_t Version() const { return version_; }
    uint32_t DynamicComboCount() const { return dynamicCount_; }
    uint32_t Flags() const { return flags_; }
    uint32_t CentroidMask() const { return centroidMask_; }
    uint32_t SourceCRC() const { return sourceCRC_; }
    const std::string &Path() const { return path_; }

private:
    struct StaticRecord { uint32_t id, offset; };
    struct AliasRecord { uint32_t id, source; };
    struct StaticCombos
    {
        // CUtlMap nodes may relocate as it grows; payload objects stay heap-owned
        // so DynamicPayload references remain valid until reopen/destruction.
        CUtlMap<uint32_t, VcsPayload *, uint32_t> dynamics;
        StaticCombos() : dynamics(DefLessFunc(uint32_t)) {}
        StaticCombos(const StaticCombos &) = delete;
        StaticCombos &operator=(const StaticCombos &) = delete;
        ~StaticCombos() { dynamics.PurgeAndDeleteElements(); }
    };

    bool DecodeV4(uint32_t staticIndex, StaticCombos &combos, std::string &error) const;
    bool DecodeBlocks(uint32_t staticIndex, uint32_t recordID, size_t start,
                      size_t end, StaticCombos &combos, std::string &error) const;
    bool ParseBytes(const uint8_t *bytes, size_t length, VcsStage stage,
                    const char *label, std::string &error);
    bool ValidateTokens(const uint8_t *data, size_t size, std::string &error) const;
    bool Fail(const std::string &message, std::string &error) const;

    std::vector<uint8_t> file_;
    std::vector<uint8_t> reference_;
    std::vector<StaticRecord> records_;
    std::vector<AliasRecord> aliases_;
    // Heap ownership keeps pointers stable when CUtlMap grows or rebalances.
    CUtlMap<uint32_t, StaticCombos *, uint32_t> cache_;
    CUtlMap<uint32_t, const StaticCombos *, uint32_t> loadedStatics_;
    std::string path_;
    VcsStage stage_ = VcsStage::Vertex;
    uint32_t version_ = 0, totalCount_ = 0, dynamicCount_ = 0;
    uint32_t flags_ = 0, centroidMask_ = 0, sourceCRC_ = 0;
    size_t payloadStart_ = 0, headerBytes_ = 28;
};

} // namespace shaderapidx12
#endif
