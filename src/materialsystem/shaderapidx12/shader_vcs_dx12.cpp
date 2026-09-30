//========= Copyright Valve Corporation, All rights reserved. ============//
#include "shader_vcs_dx12.h"

#include "filesystem.h"
#include "materialsystem/shader_vcs_version.h"
#include "tier1/diff.h"
#include "tier1/lzmaDecoder.h"
#include "thirdparty/bzip2/bzlib.h"
#include "tier0/threadtools.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <memory>

namespace shaderapidx12
{
namespace
{
// All file integers are LE, including unaligned records in compressed blocks.
bool U32(const uint8_t *p, size_t size, size_t offset, uint32_t &value)
{
    if (offset > size || size - offset < sizeof(value)) return false;
    std::memcpy(&value, p + offset, sizeof(value));
#if defined(PLATFORM_BIG_ENDIAN)
    value = (value >> 24) | ((value >> 8) & 0x0000ff00u) |
            ((value << 8) & 0x00ff0000u) | (value << 24);
#endif
    return true;
}

bool Span(size_t size, size_t start, size_t count)
{
    return start <= size && count <= size - start;
}

bool Product(size_t a, size_t b, size_t &out)
{
    if (b && a > (std::numeric_limits<size_t>::max)() / b) return false;
    out = a * b;
    return true;
}

// ApplyDiffs does unchecked pointer arithmetic on both source and output.
// Model its cursor transitions using signed offsets before invoking it.
bool ValidDiff(const uint8_t *diff, size_t length, size_t oldSize, size_t &outputSize)
{
    size_t pos = 0, out = 0;
    int64_t source = 0;
    while (pos < length)
    {
        const uint8_t op = diff[pos++];
        size_t count = 0;
        int64_t delta = 0;
        bool fromOld = false;
        if (op == 0)
        {
            if (!Span(length, pos, 4)) return false;
            count = static_cast<size_t>(diff[pos]) | (static_cast<size_t>(diff[pos + 1]) << 8);
            const uint16_t ofs = static_cast<uint16_t>(diff[pos + 2] | (diff[pos + 3] << 8));
            delta = static_cast<int16_t>(ofs);
            pos += 4;
            fromOld = true;
        }
        else if (op & 0x80)
        {
            count = op & 0x7f;
            if (!count)
            {
                if (!Span(length, pos, 1)) return false;
                count = diff[pos++];
                if (!count)
                {
                    if (!Span(length, pos, 3)) return false;
                    count = static_cast<size_t>(diff[pos]) |
                            (static_cast<size_t>(diff[pos + 1]) << 8) |
                            (static_cast<size_t>(diff[pos + 2]) << 16);
                    pos += 3;
                    if (!Span(length, pos, count)) return false;
                    pos += count;
                }
                else
                {
                    if (!Span(length, pos, 2)) return false;
                    const uint16_t ofs = static_cast<uint16_t>(diff[pos] | (diff[pos + 1] << 8));
                    delta = static_cast<int16_t>(ofs);
                    pos += 2;
                    fromOld = true;
                }
            }
            else
            {
                if (!Span(length, pos, 1)) return false;
                delta = static_cast<int8_t>(diff[pos++]);
                fromOld = true;
            }
        }
        else
        {
            count = op;
            if (!Span(length, pos, count)) return false;
            pos += count;
        }
        if (!count || count > oldSize - out) return false;
        out += count;
        if (fromOld)
        {
            source += delta;
            if (source < 0 || static_cast<uint64_t>(source) > oldSize ||
                count > oldSize - static_cast<size_t>(source)) return false;
            source += static_cast<int64_t>(count);
        }
    }
    outputSize = out;
    return out != 0;
}

} // namespace

ShaderVcsFile::ShaderVcsFile()
    : cache_(DefLessFunc(uint32_t)), loadedStatics_(DefLessFunc(uint32_t))
{
}

ShaderVcsFile::~ShaderVcsFile()
{
    loadedStatics_.Purge();
    cache_.PurgeAndDeleteElements();
}

bool ShaderVcsFile::Fail(const std::string &message, std::string &error) const
{
    error = path_ + " (" + (stage_ == VcsStage::Vertex ? "VS" : "PS") + "): " + message;
    return false;
}

// Parsed once per process from GAME shaders/native_dx12_legacy_names.txt ("<native> <vs|ps> <legacy>" per line).
std::string ShaderVcsFile::LegacyShaderName(IFileSystem &filesystem, const char *name, VcsStage stage)
{
    static CThreadFastMutex mutex;
    static bool loaded = false;
    static std::vector<std::pair<std::string, std::string>> names;   // "<vs|ps>:<native lowercase>" -> legacy
    {
        AUTO_LOCK(mutex);
        if (!loaded) {
            loaded = true;
            FileHandle_t file = filesystem.Open("shaders/native_dx12_legacy_names.txt", "rb", "GAME");
            if (file != FILESYSTEM_INVALID_HANDLE) {
                std::string text(filesystem.Size(file), '\0');
                filesystem.Read(text.data(), static_cast<int>(text.size()), file);
                filesystem.Close(file);
                for (size_t line = 0; line < text.size();) {
                    size_t end = text.find('\n', line); if (end == std::string::npos) end = text.size();
                    char logical[256] = {}, stageName[8] = {}, legacy[256] = {};
                    if (std::sscanf(text.substr(line, end - line).c_str(), "%255s %7s %255s", logical, stageName, legacy) == 3) {
                        std::string key = std::string(stageName) + ":" + logical;
                        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                        names.emplace_back(std::move(key), legacy);
                    }
                    line = end + 1;
                }
                std::sort(names.begin(), names.end());
            }
        }
    }
    std::string key = std::string(stage == VcsStage::Vertex ? "vs:" : "ps:") + name;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const auto found = std::lower_bound(names.begin(), names.end(), key,
        [](const std::pair<std::string, std::string> &entry, const std::string &k) { return entry.first < k; });
    return found != names.end() && found->first == key ? found->second : std::string(name);
}

bool ShaderVcsFile::Open(IFileSystem &filesystem, const char *name, VcsStage stage, std::string &error)
{
    if (!name || !*name || std::strchr(name, '/') || std::strchr(name, '\\') ||
        std::strstr(name, "..") || std::strchr(name, ':'))
    {
        path_ = name ? name : "<null>";
        stage_ = stage;
        return Fail("invalid shader name", error);
    }
    const std::string filename = std::string("shaders/") +
        (stage == VcsStage::Vertex ? "vsh/" : "psh/") + name + ".vcs";
    // A native logical (<base>_vs51/_ps51) whose native record is absent resolves to the legacy DX9 logical of the same
    // shader in shaders/fxc (shaders/native_dx12_legacy_names.txt, written by nativeshaderpack_dx12); other names use
    // their own shaders/fxc record.
    const std::string fallback = std::string("shaders/fxc/") + LegacyShaderName(filesystem, name, stage) + ".vcs";
    FileHandle_t file = filesystem.Open(filename.c_str(), "rb", "GAME");
    const std::string &selected = file == FILESYSTEM_INVALID_HANDLE ? fallback : filename;
    if (file == FILESYSTEM_INVALID_HANDLE) file = filesystem.Open(fallback.c_str(), "rb", "GAME");
    path_ = selected;
    stage_ = stage;
    if (file == FILESYSTEM_INVALID_HANDLE) return Fail("file not found in GAME", error);
    const size_t size = filesystem.Size(file);
    if (size < 28 || size > static_cast<size_t>((std::numeric_limits<int>::max)()))
    {
        filesystem.Close(file);
        return Fail("invalid file size", error);
    }
    std::vector<uint8_t> bytes(size);
    const int read = filesystem.Read(bytes.data(), static_cast<int>(size), file);
    filesystem.Close(file);
    if (read != static_cast<int>(size)) return Fail("short GAME file read", error);
    if (!ParseBytes(bytes.data(), bytes.size(), stage, selected.c_str(), error)) return false;
    file_ = std::move(bytes);
    return true;
}

bool ShaderVcsFile::OpenBytes(const uint8_t *bytes, size_t length, VcsStage stage,
                              const char *label, std::string &error)
{
    if (!ParseBytes(bytes, length, stage, label, error)) return false;
    file_.assign(bytes, bytes + length);
    return true;
}

bool ShaderVcsFile::ParseBytes(const uint8_t *bytes, size_t length, VcsStage stage,
                               const char *label, std::string &error)
{
    path_ = label ? label : "<memory>";
    stage_ = stage;
    version_ = totalCount_ = dynamicCount_ = flags_ = centroidMask_ = sourceCRC_ = 0;
    payloadStart_ = 0; headerBytes_ = 28;
    loadedStatics_.Purge();
    cache_.PurgeAndDeleteElements();
    file_.clear(); reference_.clear(); records_.clear(); aliases_.clear();
    if (!bytes || length < 24) return Fail("truncated VCS header", error);
    if (length > (std::numeric_limits<uint32_t>::max)())
        return Fail("VCS file exceeds 32-bit on-disk offsets", error);
    uint32_t version, total, dynamic, flags, centroid, field, crc = 0;
    if (!U32(bytes, length, 0, version) || !U32(bytes, length, 4, total) ||
        !U32(bytes, length, 8, dynamic) || !U32(bytes, length, 12, flags) ||
        !U32(bytes, length, 16, centroid) || !U32(bytes, length, 20, field))
        return Fail("truncated VCS header", error);
    if (version >= 4 && !U32(bytes, length, 24, crc)) return Fail("truncated VCS header", error);
    if (version != 2 && (version < 4 || version > 6)) return Fail("unsupported VCS version " + std::to_string(version), error);
    headerBytes_ = version == 2 ? 24 : 28;
    if (!total || !dynamic || total % dynamic)
        return Fail("invalid total/dynamic combo counts", error);
    const size_t staticCount = total / dynamic;
    if (version < 4)
    {
        if (field > length - headerBytes_ || field > static_cast<uint32_t>((std::numeric_limits<int>::max)()))
            return Fail("legacy VCS reference exceeds file", error);
        const size_t dictionaryStart = headerBytes_ + static_cast<size_t>(field);
        size_t dictionarySize;
        if (!Product(total, 8, dictionarySize) || !Span(length, dictionaryStart, dictionarySize))
            return Fail("legacy VCS dictionary exceeds file", error);
        payloadStart_ = dictionaryStart + dictionarySize;
        reference_.assign(bytes + headerBytes_, bytes + dictionaryStart);
        size_t previousEnd = payloadStart_;
        for (size_t i = 0; i < total; ++i)
        {
            uint32_t offset, size;
            U32(bytes, length, dictionaryStart + i * 8, offset);
            U32(bytes, length, dictionaryStart + i * 8 + 4, size);
            if (offset == 0xffffffffu) { if (size != 0 && size != 0xffffffffu) return Fail("invalid skipped legacy VCS entry", error); continue; }
            if (offset < payloadStart_ || !Span(length, offset, size) || offset < previousEnd)
                return Fail("invalid legacy VCS dictionary span", error);
            previousEnd = static_cast<size_t>(offset) + size;
        }
    }
    else if (version == 4)
    {
        if (field > length - 28 || field > static_cast<uint32_t>((std::numeric_limits<int>::max)()))
            return Fail("V4 reference exceeds file", error);
        const size_t dictionaryStart = 28 + static_cast<size_t>(field);
        size_t dictionarySize;
        if (!Product(total, 8, dictionarySize) || !Span(length, dictionaryStart, dictionarySize))
            return Fail("V4 dictionary exceeds file", error);
        payloadStart_ = dictionaryStart + dictionarySize;
        if (field) reference_.assign(bytes + 28, bytes + dictionaryStart);
        size_t previousEnd = payloadStart_;
        for (size_t i = 0; i < total; ++i)
        {
            if (i % dynamic == 0) previousEnd = payloadStart_;
            uint32_t offset, size;
            U32(bytes, length, dictionaryStart + i * 8, offset); U32(bytes, length, dictionaryStart + i * 8 + 4, size);
            if (offset == 0xffffffffu) { if (size != 0 && size != 0xffffffffu) return Fail("invalid skipped V4 dictionary entry", error); continue; }
            if (offset < payloadStart_ || !Span(length, offset, size) || offset < previousEnd) return Fail("invalid V4 dictionary span", error);
            previousEnd = static_cast<size_t>(offset) + size;
        }
    }
    else
    {
        if (!field || field > staticCount + 1)
            return Fail("invalid static record count", error);
        size_t recordBytes;
        if (!Product(field, 8, recordBytes) || !Span(length, 28, recordBytes))
            return Fail("static records exceed file", error);
        size_t cursor = 28 + recordBytes;
        if (version == 6)
        {
            uint32_t aliasCount;
            if (!U32(bytes, length, cursor, aliasCount)) return Fail("truncated alias count", error);
            cursor += 4;
            size_t aliasBytes;
            if (aliasCount > staticCount || !Product(aliasCount, 8, aliasBytes) ||
                !Span(length, cursor, aliasBytes)) return Fail("aliases exceed file", error);
            for (size_t i = 0; i < aliasCount; ++i)
            {
                uint32_t id, source;
                U32(bytes, length, cursor + i * 8, id);
                U32(bytes, length, cursor + i * 8 + 4, source);
                if (id >= staticCount || source >= staticCount ||
                    (!aliases_.empty() && id <= aliases_.back().id))
                    return Fail("invalid or unsorted static alias " + std::to_string(i), error);
                aliases_.push_back({id, source});
            }
            cursor += aliasBytes;
        }
        payloadStart_ = cursor;
        for (size_t i = 0; i < field; ++i)
        {
            uint32_t id, offset;
            U32(bytes, length, 28 + i * 8, id);
            U32(bytes, length, 28 + i * 8 + 4, offset);
            if (i + 1 == field)
            {
                if (id != 0xffffffffu || offset != length)
                    return Fail("invalid static sentinel/end offset", error);
            }
            else if (id >= staticCount || offset < payloadStart_ || offset >= length ||
                     (!records_.empty() &&
                      (id <= records_.back().id || offset <= records_.back().offset)))
                return Fail("invalid or unsorted static record " + std::to_string(i), error);
            records_.push_back({id, offset});
        }
        if (records_.size() > 1 && records_[records_.size() - 2].offset >= records_.back().offset)
            return Fail("empty last static span", error);
        if (records_.front().offset != payloadStart_)
            return Fail("unreferenced bytes before static payloads", error);
        for (const AliasRecord &alias : aliases_)
        {
            const auto overlap = std::lower_bound(records_.begin(), records_.end() - 1, alias.id,
                [](const StaticRecord &r, uint32_t id) { return r.id < id; });
            if (overlap != records_.end() - 1 && overlap->id == alias.id)
                return Fail("alias overlaps a static record", error);
            // A source may itself be an alias, but every chain must terminate.
            uint32_t current = alias.source;
            for (size_t hops = 0; hops <= aliases_.size(); ++hops)
            {
                const auto target = std::lower_bound(records_.begin(), records_.end() - 1, current,
                    [](const StaticRecord &r, uint32_t id) { return r.id < id; });
                if (target != records_.end() - 1 && target->id == current) break;
                const auto next = std::lower_bound(aliases_.begin(), aliases_.end(), current,
                    [](const AliasRecord &r, uint32_t id) { return r.id < id; });
                if (next == aliases_.end() || next->id != current)
                    return Fail("alias points to a missing static combo", error);
                if (hops == aliases_.size()) return Fail("static alias cycle", error);
                current = next->source;
            }
        }
    }
    // The caller installs its owned bytes only after the directory is validated.
    version_ = version; totalCount_ = total; dynamicCount_ = dynamic;
    flags_ = flags; centroidMask_ = centroid; sourceCRC_ = crc;
    error.clear();
    return true;
}

bool ShaderVcsFile::ValidateTokens(const uint8_t *data, size_t size, std::string &error) const
{
    if (size >= 4 && !std::memcmp(data, "DXBC", 4))
    {
        // DXBC header: magic, checksum[4], one, total bytes, chunk count, chunk offsets.
        uint32_t total = 0, chunks = 0, one = 0;
        if ((size & 3) || size < 36 || !U32(data, size, 20, one) || one != 1 ||
            !U32(data, size, 24, total) || total != size ||
            !U32(data, size, 28, chunks) || !chunks || chunks > 128 ||
            chunks > (size - 32) / 4)
            return Fail("invalid DXBC header or chunk directory", error);
        bool program = false;
        for (uint32_t i = 0; i < chunks; ++i)
        {
            uint32_t offset = 0, length = 0, token = 0;
            U32(data, size, 32 + 4 * i, offset);
            if ((offset & 3) || offset < 32 + chunks * 4 || offset > size - 8 ||
                !U32(data, size, offset + 4, length) || length > size - offset - 8)
                return Fail("truncated or misaligned DXBC chunk", error);
            if (!std::memcmp(data + offset, "SHDR", 4) || !std::memcmp(data + offset, "SHEX", 4))
            {
                // SM4+ version token: program type in bits 16-31 (0 = pixel, 1 = vertex),
                // major in bits 4-7, minor in bits 0-3; the second token is the length in DWORDs.
                uint32_t words = 0;
                if (program || length < 8 || (length & 3) ||
                    !U32(data, size, offset + 8, token) || !U32(data, size, offset + 12, words) ||
                    words < 2 || words > length / 4 ||
                    (token >> 16) != (stage_ == VcsStage::Vertex ? 1u : 0u) ||
                    ((token >> 4) & 0xfu) != 5u || (token & 0xfu) > 1u)
                    return Fail("invalid or wrong-stage DXBC shader program", error);
                program = true;
            }
        }
        if (!program) return Fail("DXBC has no SHDR/SHEX shader program", error);
        return true;
    }
    uint32_t profile, end;
    if (size < 8 || size % 4 || !U32(data, size, 0, profile) ||
        !U32(data, size, size - 4, end) || end != 0x0000ffffu)
        return Fail("invalid token alignment or END token", error);
    const uint32_t major = (profile >> 8) & 0xff;
    const uint32_t minor = profile & 0xff;
    const bool supported = stage_ == VcsStage::Vertex
        ? ((profile >> 16) == 0xfffeu &&
           ((major == 1 && minor == 1) || (major == 2 && minor == 0) ||
            (major == 3 && minor == 0)))
        : ((profile >> 16) == 0xffffu &&
           ((major == 1 && minor >= 1 && minor <= 4) ||
            (major == 2 && minor <= 1) || (major == 3 && minor == 0)));
    if (!supported)
        return Fail("unsupported stage/profile in shader tokens", error);
    return true;
}

bool ShaderVcsFile::DecodeV4(uint32_t staticIndex, StaticCombos &combos, std::string &error) const
{
    const size_t dictionary = headerBytes_ + reference_.size();
    for (uint32_t dynamic = 0; dynamic < dynamicCount_; ++dynamic)
    {
        const size_t entry = dictionary + (static_cast<size_t>(staticIndex) + dynamic) * 8;
        uint32_t offset, length;
        U32(file_.data(), file_.size(), entry, offset);
        U32(file_.data(), file_.size(), entry + 4, length);
        if (offset == 0xffffffffu || !length) continue;
        const uint8_t *data = file_.data() + offset;
        size_t output = length;
        std::vector<uint8_t> decoded;
        if (!reference_.empty())
        {
            if (length > static_cast<size_t>((std::numeric_limits<int>::max)()) ||
                !ValidDiff(data, length, reference_.size(), output))
                return Fail("invalid diff at static " + std::to_string(staticIndex) +
                            " dynamic " + std::to_string(dynamic), error);
            decoded.resize(reference_.size());
            int result = 0;
            ApplyDiffs(reference_.data(), data, static_cast<int>(reference_.size()),
                       static_cast<int>(length), result, decoded.data(),
                       static_cast<uint32_t>(decoded.size()));
            if (result < 0 || static_cast<size_t>(result) != output)
                return Fail("diff output length mismatch", error);
            decoded.resize(output);
        }
        else decoded.assign(data, data + length);
        if (!ValidateTokens(decoded.data(), decoded.size(), error))
        {
            error += " at static " + std::to_string(staticIndex) +
                     " dynamic " + std::to_string(dynamic);
            return false;
        }
        std::unique_ptr<VcsPayload> payload(new VcsPayload{std::move(decoded)});
        combos.dynamics.Insert(dynamic, payload.get());
        payload.release();
    }
    return true;
}

bool ShaderVcsFile::DecodeBlocks(uint32_t staticIndex, uint32_t recordID, size_t start,
                                  size_t end, StaticCombos &combos, std::string &error) const
{
    size_t cursor = start;
    while (cursor < end)
    {
        uint32_t tag;
        if (!U32(file_.data(), end, cursor, tag)) return Fail("truncated block tag", error);
        cursor += 4;
        if (tag == 0xffffffffu)
            return cursor == end ? true : Fail("bytes after static terminator", error);
        const uint32_t kind = tag & 0xc0000000u;
        const size_t packedSize = tag & 0x3fffffffu;
        if (kind == 0xc0000000u || !packedSize || !Span(end, cursor, packedSize))
            return Fail("invalid compression tag or packed span", error);
        std::vector<uint8_t> unpacked;
        if (kind == 0x80000000u)
        {
            if (packedSize > MAX_SHADER_UNPACKED_BLOCK_SIZE)
                return Fail("raw block exceeds unpacked limit", error);
            unpacked.assign(file_.data() + cursor, file_.data() + cursor + packedSize);
        }
        else if (kind == 0)
        {
            bz_stream stream = {};
            stream.next_in = reinterpret_cast<char *>(const_cast<uint8_t *>(file_.data() + cursor));
            stream.avail_in = static_cast<unsigned int>(packedSize);
            unpacked.resize(MAX_SHADER_UNPACKED_BLOCK_SIZE);
            stream.next_out = reinterpret_cast<char *>(unpacked.data());
            stream.avail_out = static_cast<unsigned int>(unpacked.size());
            if (BZ2_bzDecompressInit(&stream, 0, 1) != BZ_OK)
                return Fail("bzip2 initialization failed", error);
            const int status = BZ2_bzDecompress(&stream);
            const size_t used = stream.total_in_lo32, produced = stream.total_out_lo32;
            BZ2_bzDecompressEnd(&stream);
            if (status != BZ_STREAM_END || used != packedSize || !produced ||
                produced > MAX_SHADER_UNPACKED_BLOCK_SIZE)
                return Fail("bzip2 output or input length mismatch", error);
            unpacked.resize(produced);
        }
        else
        {
            // CLZMA has no input or output capacities. Verify its full Source header
            // (LZMA id, actualSize, lzmaSize, properties) before calling it.
            uint32_t id, actual, compressed, dict;
            const uint8_t *lz = file_.data() + cursor;
            if (packedSize < 17 || !U32(lz, packedSize, 0, id) ||
                !U32(lz, packedSize, 4, actual) || !U32(lz, packedSize, 8, compressed) ||
                !U32(lz, packedSize, 13, dict) || id != LZMA_ID ||
                compressed != packedSize - 17 || !compressed || !actual ||
                actual > MAX_SHADER_UNPACKED_BLOCK_SIZE || lz[12] >= 225 ||
                dict > 64u * 1024u * 1024u)
                return Fail("invalid Source LZMA header or span", error);
            unpacked.resize(actual);
            const unsigned int produced = CLZMA::Uncompress(const_cast<uint8_t *>(lz), unpacked.data());
            if (produced != actual) return Fail("LZMA output length mismatch", error);
        }
        cursor += packedSize;
        size_t pos = 0;
        while (pos < unpacked.size())
        {
            uint32_t id, length;
            if (!U32(unpacked.data(), unpacked.size(), pos, id) ||
                !U32(unpacked.data(), unpacked.size(), pos + 4, length))
                return Fail("truncated dynamic payload record", error);
            pos += 8;
            uint32_t dynamic;
            if (version_ == 5)
            {
                const uint64_t base = static_cast<uint64_t>(recordID) * dynamicCount_;
                if (id < base || static_cast<uint64_t>(id) >= base + dynamicCount_)
                    return Fail("V5 full combo ID outside static range", error);
                dynamic = id - static_cast<uint32_t>(base);
            }
            else dynamic = id;
            if (dynamic >= dynamicCount_ || !length || !Span(unpacked.size(), pos, length) ||
                combos.dynamics.Find(dynamic) != combos.dynamics.InvalidIndex())
                return Fail("invalid or duplicate dynamic combo " + std::to_string(id), error);
            if (!ValidateTokens(unpacked.data() + pos, length, error))
            {
                error += " at static " + std::to_string(staticIndex) +
                         " dynamic " + std::to_string(dynamic);
                return false;
            }
            std::unique_ptr<VcsPayload> payload(new VcsPayload);
            payload->tokens.assign(unpacked.data() + pos, unpacked.data() + pos + length);
            combos.dynamics.Insert(dynamic, payload.get());
            payload.release();
            pos += length;
        }
    }
    return Fail("missing static-combo terminator", error);
}

bool ShaderVcsFile::StaticComboIndex(size_t ordinal, uint32_t &index) const
{
    if (!version_) return false;
    if (version_ <= 4)
    {
        if (ordinal >= totalCount_ / dynamicCount_) return false;
        index = static_cast<uint32_t>(ordinal) * dynamicCount_;
        return true;
    }
    const size_t canonicalCount = records_.size() - 1;
    if (ordinal < canonicalCount)
        index = records_[ordinal].id * dynamicCount_;
    else
    {
        ordinal -= canonicalCount;
        if (ordinal >= aliases_.size()) return false;
        index = aliases_[ordinal].id * dynamicCount_;
    }
    return true;
}

bool ShaderVcsFile::LoadStaticCombo(uint32_t staticIndex, std::string &error)
{
    if (!version_) return Fail("VCS file has not been opened", error);
    if (staticIndex >= totalCount_ || staticIndex % dynamicCount_)
        return Fail("staticIndex must be an in-range, already-multiplied index: " +
                    std::to_string(staticIndex), error);
    if (loadedStatics_.Find(staticIndex) != loadedStatics_.InvalidIndex()) { error.clear(); return true; }
    uint32_t recordID = staticIndex / dynamicCount_;
    if (version_ > 4)
    {
        for (size_t hops = 0; hops < aliases_.size(); ++hops)
        {
            const auto it = std::lower_bound(aliases_.begin(), aliases_.end(), recordID,
                [](const AliasRecord &a, uint32_t id) { return a.id < id; });
            if (it == aliases_.end() || it->id != recordID) break;
            recordID = it->source;
        }
    }
    const uint32_t canonicalIndex = recordID * dynamicCount_;
    auto cached = cache_.Find(canonicalIndex);
    const StaticCombos *combos = nullptr;
    if (cached == cache_.InvalidIndex())
    {
        std::unique_ptr<StaticCombos> decodedCombos(new StaticCombos);
        bool decoded;
        if (version_ <= 4)
            decoded = DecodeV4(staticIndex, *decodedCombos, error);
        else
        {
            const auto record = std::lower_bound(records_.begin(), records_.end() - 1, recordID,
                [](const StaticRecord &r, uint32_t id) { return r.id < id; });
            if (record == records_.end() - 1 || record->id != recordID)
                return Fail("missing static combo " + std::to_string(staticIndex), error);
            decoded = DecodeBlocks(staticIndex, recordID, record->offset, (record + 1)->offset,
                                   *decodedCombos, error);
        }
        if (!decoded)
        {
            error += " [staticIndex=" + std::to_string(staticIndex) + "]";
            return false;
        }
        cached = cache_.Insert(canonicalIndex, decodedCombos.get());
        combos = decodedCombos.release();
    }
    else combos = cache_.Element(cached);
    loadedStatics_.Insert(staticIndex, combos);
    error.clear();
    return true;
}

const VcsPayload *ShaderVcsFile::DynamicPayload(uint32_t staticIndex, uint32_t dynamicIndex) const
{
    const auto found = loadedStatics_.Find(staticIndex);
    if (found == loadedStatics_.InvalidIndex() || dynamicIndex >= dynamicCount_) return nullptr;
    const StaticCombos *combos = loadedStatics_.Element(found);
    const auto payload = combos->dynamics.Find(dynamicIndex);
    return payload == combos->dynamics.InvalidIndex() ? nullptr : combos->dynamics.Element(payload);
}

} // namespace shaderapidx12
