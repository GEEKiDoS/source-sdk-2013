#include "shader_vcs_dx12.h"
#include "native_engine_cbuffers_dx12.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using namespace shaderapidx12;

namespace {
struct Member {
    std::string name, typeName, annotation, canonical;
    unsigned offset = 0, size = 0, type = 0, kind = 0, rows = 0, cols = 0, elements = 0, stride = 0;
    dx12native::NativeCBufferLegacyEntryDX12 legacy{};
    bool mapped = false;
};
struct Block {
    std::string name;
    unsigned stage = 0, reg = 0, space = 0, size = 0;
    uint64_t hash = 0;
    bool engine = false;
    std::vector<Member> members;
    std::string canonical;
};
struct Shader {
    // logical: native name (<base>_vs51|_ps51|_cs51). legacyName: the DX9 logical of the same shader (<base>_vs20, ps20b,
    // ...), i.e. the combo-ABI reference and the shaders/fxc record name. Native-only logicals (profile "native")
    // have neither legacySource nor legacyName.
    std::string source, legacySource, stage, logical, profile, generatedBase, inc, legacyInc, legacyName;
    fs::path artifactRoot;
    fs::path vcs;
    unsigned dynamicCount = 0, staticCount = 0, present = 0;
    std::set<uint64_t> presentCombos;
    std::map<std::string, Block> blocks;
};
std::string readText(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot read " + p.string());
    return {std::istreambuf_iterator<char>(f), {}};
}
std::vector<uint8_t> readBytes(const fs::path &p) {
    const std::string s = readText(p);
    return {s.begin(), s.end()};
}
void writeText(const fs::path &p, const std::string &text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f || !(f << text)) throw std::runtime_error("Cannot write " + p.string());
}
std::string trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
std::string hex(uint64_t n) {
    std::ostringstream s; s << "0x" << std::hex << std::setw(16) << std::setfill('0') << n;
    return s.str();
}
std::string stripSpace(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
    return s;
}
std::string baseName(const std::string &source, const std::string &stage, const std::string &profile) {
    auto base = fs::path(source).stem().string();
    const std::regex suffix("_(vs|ps|cs)(2x|xx|20b|20|30|40|41|50|51)$", std::regex::icase);
    return std::regex_replace(base, suffix, "") + "_" + stage + profile;
}
std::string tokenRename(const std::string &input, const std::string &from, const std::string &to) {
    std::string out = input;
    for (size_t pos = 0; (pos = out.find(from, pos)) != std::string::npos; pos += to.size()) out.replace(pos, from.size(), to);
    return out;
}
// The generated .inc is the compiler's combo ABI: comparing its class bodies
// // compares ranges, ordering/multipliers and static/dynamic index arithmetic.
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
std::string comboABI(std::string text, const std::string &base) {
    // ShaderCompile2 lowercases the identifiers it derives from mixed-case file names.
    text = tokenRename(lower(text), lower(base), "shader");
    const auto start = text.find("#pragma once");
    if (start == std::string::npos) throw std::runtime_error("Missing combo index classes");
    return stripSpace(text.substr(start));
}
std::vector<std::string> skips(const std::string &text) {
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("#pragma once") != std::string::npos) break;
        if (line.find("//") == std::string::npos || line.find("ALL SKIP STATEMENTS") != std::string::npos) continue;
        line = line.substr(line.find("//") + 2);
        out.push_back(stripSpace(line));
    }
    return out;
}
void hashType(ID3D12ShaderReflectionType *type, const D3D12_SHADER_TYPE_DESC &t, std::string &out) {
    out += std::to_string(static_cast<int>(t.Class)) + "," + std::to_string(static_cast<int>(t.Type)) + "," +
           std::to_string(t.Rows) + "," + std::to_string(t.Columns) + "," + std::to_string(t.Elements);
    if (t.Class == D3D_SVC_STRUCT) {
        out += "{";
        for (unsigned j = 0; j < t.Members; ++j) {
            auto *sub = type->GetMemberTypeByIndex(j);
            D3D12_SHADER_TYPE_DESC td{};
            if (!sub || FAILED(sub->GetDesc(&td))) throw std::runtime_error("Failed to reflect struct member");
            out += std::string(type->GetMemberTypeName(j)) + ":";
            hashType(sub, td, out);
            // Struct type offsets are relative to their containing struct; reflection
            // exposes no per-member size, so use the next member's offset or struct size.
            unsigned end = 0;
            if (j + 1 < t.Members) {
                D3D12_SHADER_TYPE_DESC next{};
                if (FAILED(type->GetMemberTypeByIndex(j + 1)->GetDesc(&next))) throw std::runtime_error("Bad struct reflection");
                end = next.Offset;
            } else end = t.Elements ? td.Offset + t.Elements : td.Offset;
            out += ":" + std::to_string(td.Offset) + ":" + std::to_string(end >= td.Offset ? end - td.Offset : 0) + ";";
        }
        out += "}";
    }
}
Block reflectBlock(ID3D12ShaderReflection *reflection, ID3D12ShaderReflectionConstantBuffer *buffer,
                   const D3D12_SHADER_BUFFER_DESC &bd, const D3D12_SHADER_INPUT_BIND_DESC &binding, unsigned stage) {
    Block b; b.name = bd.Name; b.size = bd.Size; b.stage = stage; b.reg = binding.BindPoint; b.space = 1;
    b.canonical = b.name + "|" + std::to_string(b.size) + ";";
    for (unsigned i = 0; i < bd.Variables; ++i) {
        auto *var = buffer->GetVariableByIndex(i);
        D3D12_SHADER_VARIABLE_DESC vd{};
        D3D12_SHADER_TYPE_DESC td{};
        if (FAILED(var->GetDesc(&vd)) || FAILED(var->GetType()->GetDesc(&td))) throw std::runtime_error("Bad variable reflection: " + b.name);
        Member m; m.name = vd.Name; m.offset = vd.StartOffset; m.size = vd.Size;
        m.typeName = td.Name ? td.Name : ""; m.type = td.Type; m.kind = td.Class;
        m.rows = td.Rows; m.cols = td.Columns; m.elements = td.Elements;
        // cbuffer array elements start on 16-byte rows; the last element is not padded in Size.
        m.stride = m.elements ? ((m.size + 15u) & ~15u) / m.elements : 0;
        m.canonical = m.name + ":";
        hashType(var->GetType(), td, m.canonical);
        m.canonical += ":" + std::to_string(m.offset) + ":" + std::to_string(m.size) + ";";
        b.canonical += m.canonical;
        b.members.push_back(m);
    }
    b.hash = dx12native::HashString(b.canonical.c_str());
    return b;
}
std::map<std::string, unsigned> declaredSpaces(const fs::path &source) {
    std::map<std::string, unsigned> out;
    const std::regex declaration(R"(\bcbuffer\s+([A-Za-z_]\w*)\s*:\s*register\s*\(\s*b\d+\s*(?:,\s*space\s*(\d+))?\s*\))");
    auto consume = [&](const fs::path &file) {
        const auto text = readText(file);
        for (std::sregex_iterator i(text.begin(), text.end(), declaration), end; i != end; ++i) {
            const auto name = (*i)[1].str();
            const unsigned space = (*i)[2].matched ? static_cast<unsigned>(std::stoul((*i)[2])) : 0u;
            if (out.count(name) && out[name] != space) throw std::runtime_error("Conflicting cbuffer register spaces: " + name);
            out[name] = space;
        }
    };
    if (fs::is_directory(source)) {
        for (const auto &entry : fs::recursive_directory_iterator(source))
            if (entry.is_regular_file() && (entry.path().extension() == ".h" || entry.path().extension() == ".fxc")) consume(entry.path());
    } else consume(source);
    return out;
}

void validateEngine(const Block &b) {
    for (const auto &layout : dx12native::kEngineCBufferLayouts) {
        if (b.name != layout.name) continue;
        if (b.stage != layout.stage || b.reg != layout.shaderRegister || b.space != 1 ||
            b.size != layout.byteSize || b.members.size() != layout.memberCount)
            throw std::runtime_error("Engine block layout mismatch: " + b.name);
        for (size_t i = 0; i < b.members.size(); ++i) {
            const auto &m = b.members[i];
            const auto &expected = layout.members[i];
            if (m.name != expected.name || m.offset != expected.offset || m.size != expected.size)
                throw std::runtime_error("Engine block member mismatch: " + b.name + "." + m.name);
        }
        return;
    }
    throw std::runtime_error("Unknown engine block: " + b.name);
}
bool engineBlock(const std::string &name) {
    for (const auto &layout : dx12native::kEngineCBufferLayouts) if (name == layout.name) return true;
    return false;
}
// Match the declaration on the same line as its @legacy annotation.  Includes
// are searched as well because material blocks are shared across shaders.
std::map<std::string, std::string> annotations(const fs::path &hlsl) {
    std::map<std::string, std::string> result;
    const std::regex declaration(R"(\bcbuffer\s+([A-Za-z_]\w*)\b)");
    const std::regex member(R"(^\s*(?:(?:row_major|column_major|const|static)\s+)*[A-Za-z_]\w*(?:\s*<[^;>]+>)?\s+([A-Za-z_]\w*)(?:\s*\[\s*\d+\s*\])?\s*;\s*//\s*@legacy\s+(none|(?:vs|ps):[cib]\d+(?:\.[xyzw])?)\s*$)");
    for (const auto &entry : fs::recursive_directory_iterator(hlsl)) {
        if (!entry.is_regular_file() || (entry.path().extension() != ".h" && entry.path().extension() != ".fxc")) continue;
        std::istringstream lines(readText(entry.path())); std::string line, current;
        while (std::getline(lines, line)) {
            std::smatch block;
            if (std::regex_search(line, block, declaration)) current = block[1];
            std::smatch match;
            if (std::regex_match(line, match, member)) {
                const std::string name = match[1], tag = match[2];
                const std::string key = current.empty() ? name : current + "." + name;
                if (result.count(key) && result.at(key) != tag) throw std::runtime_error("Conflicting @legacy annotation for " + key);
                result[key] = tag;
                if (!current.empty() && !result.count(name)) result[name] = tag;
            }
            if (!current.empty() && trim(line).rfind("};", 0) == 0) current.clear();
        }
    }
    return result;
}
void annotate(Block &b, const std::map<std::string, std::string> &tags) {
    b.engine = engineBlock(b.name);
    if (b.engine) validateEngine(b);
    if (b.space != 1) throw std::runtime_error("Space-0 cbuffer rejected: " + b.name);
    if (b.name == "$Globals") throw std::runtime_error("$Globals is forbidden");
    if (!b.engine && (b.reg < (b.stage == 0 ? 2u : 1u) || b.reg > 7))
        throw std::runtime_error("Material cbuffer out of slot range: " + b.name);
    for (auto &m : b.members) {
        auto found = tags.find(b.name + "." + m.name);
        if (found == tags.end()) found = tags.find(m.name);
        if (found == tags.end()) throw std::runtime_error("Missing @legacy annotation: " + b.name + "." + m.name);
        m.annotation = found->second;
        if (b.engine) {
            if (m.annotation != "none") throw std::runtime_error("Engine member must annotate none: " + m.name);
            continue;
        }
        if (m.annotation == "none") continue;
        std::smatch tag;
        if (!std::regex_match(m.annotation, tag, std::regex(R"((vs|ps):(c|i|b)(\d+)(?:\.([xyzw]))?)")))
            throw std::runtime_error("Bad @legacy annotation: " + m.annotation);
        if ((tag[1] == "vs") != (b.stage == 0)) throw std::runtime_error("Legacy stage mismatch: " + m.name);
        const char bank = tag.str(2)[0];
        unsigned comp = 0;
        if (tag[4].matched) comp = static_cast<unsigned>(std::string("xyzw").find(tag.str(4)));
        const unsigned reg = std::stoul(tag[3]);
        const unsigned count = m.elements ? m.elements * (m.kind == D3D_SVC_MATRIX_COLUMNS ? m.cols : m.kind == D3D_SVC_MATRIX_ROWS ? m.rows : 1u)
                                          : (m.kind == D3D_SVC_MATRIX_COLUMNS ? m.cols : m.kind == D3D_SVC_MATRIX_ROWS ? m.rows : 1u);
        const unsigned bytes = bank == 'b' ? 4u : (count > 1 ? 16u : m.size);
        const unsigned stride = count > 1 ? (m.elements && (m.kind == D3D_SVC_MATRIX_COLUMNS || m.kind == D3D_SVC_MATRIX_ROWS) ? m.stride / (count / m.elements) : m.stride ? m.stride : 16) : 0;
        if (count > 65535 || bytes > 16 || bytes == 0 || reg + count > 65536 ||
            (bank == 'b' && (comp || m.size / count != 4)) || (bank != 'b' && (comp * 4 + bytes > 16)))
            throw std::runtime_error("Unrepresentable legacy mapping: " + b.name + "." + m.name);
        m.legacy = {m.offset, static_cast<uint16_t>(bytes), static_cast<uint16_t>(count), static_cast<uint16_t>(stride),
                    static_cast<uint8_t>(bank == 'c' ? 0 : bank == 'i' ? 1 : 2), static_cast<uint8_t>(comp), static_cast<uint16_t>(reg)};
        m.mapped = true;
    }
}
std::string fieldType(const Member &m) {
    if (m.kind == D3D_SVC_STRUCT) throw std::runtime_error("Material struct member needs a matching C++ struct: " + m.name);
    if (m.type != D3D_SVT_FLOAT && m.type != D3D_SVT_INT && m.type != D3D_SVT_UINT && m.type != D3D_SVT_BOOL)
        throw std::runtime_error("Unsupported material member type: " + m.name);
    return m.type == D3D_SVT_FLOAT ? "float" : m.type == D3D_SVT_INT ? "int32_t" : "uint32_t";
}
std::string header(const Block &b) {
    std::ostringstream s;
    s << "#pragma once\n#include <cstddef>\n#include <cstdint>\n#include \"native_cbuffer_dx12.h\"\nnamespace dx12cb {\n";
    if (b.engine) {
        std::ostringstream e;
        e << "#pragma once\n#include \"native_engine_cbuffers_dx12.h\"\nnamespace dx12cb {\n"
          << "// Engine-owned: the backend fills this block from native state; materials never write it.\n"
          << "using " << b.name << " = dx12native::" << b.name << ";\n} // namespace dx12cb\n";
        return e.str();
    }
    s << "struct alignas(16) " << b.name << " {\n";
    unsigned cursor = 0, pad = 0;
    for (const auto &m : b.members) {
        if (m.offset < cursor || m.size % 4) throw std::runtime_error("Unsupported overlapping/unaligned material member: " + m.name);
        if (cursor < m.offset) s << "    uint8_t _pad" << pad++ << "[" << m.offset - cursor << "]{};\n";
        // Arrays and matrices spanning whole 16-byte rows are declared as [rows][4]: one C++ row per legacy
        // register, the shape the BaseVSShaderDX12 row helpers take. Everything else is a flat float/int run.
        const bool rowed = (m.elements || m.kind == D3D_SVC_MATRIX_COLUMNS || m.kind == D3D_SVC_MATRIX_ROWS) &&
                           m.size > 16 && m.size % 16 == 0;
        s << "    " << fieldType(m) << " " << m.name;
        if (rowed) s << "[" << m.size / 16 << "][4]{};\n";
        else s << "[" << m.size / 4 << "]{};\n";
        cursor = m.offset + m.size;
    }
    if (cursor < b.size) s << "    uint8_t _pad" << pad++ << "[" << b.size - cursor << "]{};\n";
    s << "    static constexpr uint32_t kStage = " << b.stage << ", kRegister = " << b.reg << ", kSize = " << b.size << ";\n"
      << "    static constexpr uint64_t kLayoutHash = " << hex(b.hash) << "ull;\n"
      << "    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {\n";
    unsigned maps = 0;
    for (const auto &m : b.members) maps += m.mapped;
    if (maps) {
        s << "        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {\n";
        for (const auto &m : b.members) if (m.mapped) {
            const auto &e = m.legacy;
            s << "            {" << e.byteOffset << ", " << e.elementBytes << ", " << e.elementCount << ", "
              << e.srcStride << ", " << unsigned(e.bank) << ", " << unsigned(e.component) << ", " << e.reg << "},\n";
        }
        s << "        };\n";
    }
    s << "        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, " << maps << ", "
      << (maps ? "entries" : "nullptr") << " };\n        return map;\n    }\n};\n";
    for (const auto &m : b.members)
        s << "static_assert(offsetof(" << b.name << ", " << m.name << ") == " << m.offset << ", \"HLSL member offset\");\n";
    s << "static_assert(sizeof(" << b.name << ") == " << b.size << ", \"HLSL cbuffer size\");\n} // namespace dx12cb\n";
    return s.str();
}
std::string describe(const Shader &shader) {
    std::ostringstream s;
    s << "logical=" << shader.logical << " stage=" << shader.stage << " source=" << shader.source
      << " legacy=" << (shader.legacySource.empty() ? "-" : shader.legacySource)
      << " legacyName=" << (shader.legacyName.empty() ? "-" : shader.legacyName) << " profile=" << shader.profile
      << " static=" << shader.staticCount << " dynamic=" << shader.dynamicCount << " present=" << shader.present << "\n";
    for (const auto &[name, b] : shader.blocks) {
        s << "  cbuffer=" << name << " space=" << b.space << " register=b" << b.reg << " size=" << b.size
          << " layoutHash=" << hex(b.hash) << (b.engine ? " engine" : " material") << "\n";
        for (const auto &m : b.members)
            s << "    member=" << m.name << " type=" << m.type << " class=" << m.kind << " offset=" << m.offset
              << " size=" << m.size << " elements=" << m.elements << " arrayStride=" << m.stride
              << " legacy=" << m.annotation << "\n";
        if (!b.engine && shader.stage != "cs") {
            // Exact legacy map entries (same values as the generated C++ LegacyMap()); consumed by the parity harness.
            for (const auto &m : b.members) if (m.mapped) {
                const auto &e = m.legacy;
                s << "    legacyEntry=" << e.byteOffset << "," << e.elementBytes << "," << e.elementCount << "," << e.srcStride << ","
                  << unsigned(e.bank) << "," << unsigned(e.component) << "," << e.reg << "\n";
            }
            s << "    bridge=WriteNativeCBuffer(api, block)\n";
        }
    }
    return s.str();
}
// ShaderCompile2 -dynamic -ver 20b names outputs <base>_vs20/<base>_ps20b; -ver 30 names them <base>_vs30/<base>_ps30.
std::string legacyIncludeBase(const std::string &legacySource, const std::string &stage, const std::string &profile) {
    // Profile "20" (ps_2_0-only logicals such as cloud_ps20) has no ShaderCompile2 target: the reference is the
    // -ver 20b compile, which names its output <base>_ps20b and equals the ps20 ABI when the source has no
    // [ps20]/[ps20b]-qualified directives (a qualified source mismatches and is rejected).
    const std::string token = profile == "30" ? "30" : (stage == "vs" ? "20" : "20b");
    return baseName(legacySource, stage, token);
}
void run(const fs::path &root, const fs::path &staging, const fs::path &game) {
    std::vector<Shader> shaders;
    std::vector<std::string> fallback;
    std::set<std::string> names;
    // native-map.txt: source|stage|logical|compiledBase|artifactDir, one per manifest line (per-line compile roots).
    std::map<std::string, std::pair<std::string, fs::path>> nativeMap;
    {
        std::istringstream mapText(readText(staging / "native-map.txt")); std::string mapLine;
        while (std::getline(mapText, mapLine)) {
            mapLine = trim(mapLine);
            if (mapLine.empty()) continue;
            std::istringstream fields(mapLine); std::string source, stage, logical, generated, artifact;
            if (!(std::getline(fields, source, '|') && std::getline(fields, stage, '|') && std::getline(fields, logical, '|') &&
                  std::getline(fields, generated, '|') && std::getline(fields, artifact)))
                throw std::runtime_error("Malformed native-map line: " + mapLine);
            nativeMap[logical + "|" + stage + "|" + source] = {generated, staging / artifact};
        }
    }
    std::vector<fs::path> manifests;
    for (const auto &entry : fs::directory_iterator(root / "manifests")) if (entry.path().extension() == ".txt") manifests.push_back(entry.path());
    std::sort(manifests.begin(), manifests.end());
    const std::regex manifest(R"(^\s*(\S+\.fxc)\s+(vs|ps|cs)\s+(\w+)\s+(20b|20|30|native)(?:\s+(\S+))?\s*$)");
    for (const auto &file : manifests) {
        std::istringstream text(readText(file)); std::string line;
        while (std::getline(text, line)) {
            line = trim(line);
            if (line.empty() || line.rfind("//", 0) == 0) continue;
            if (line.rfind("# legacy ", 0) == 0) { fallback.push_back(line); continue; }
            if (line[0] == '#') continue;
            std::smatch m;
            if (!std::regex_match(line, m, manifest)) throw std::runtime_error("Malformed manifest line in " + file.string() + ": " + line);
            Shader shader; shader.source = m[1]; shader.stage = m[2]; shader.logical = m[3]; shader.profile = m[4];
            shader.legacySource = m[5].matched && m[5].str() != "-" ? m[5].str() : "";
            if (shader.profile == "native" && !shader.legacySource.empty())
                throw std::runtime_error("Native-only manifest line has a legacy source: " + line);
            if (shader.stage == "cs" && shader.profile != "native")
                throw std::runtime_error("Compute shaders must be native-only: " + line);
            const auto mapIt = nativeMap.find(shader.logical + "|" + shader.stage + "|" + shader.source);
            if (mapIt == nativeMap.end()) throw std::runtime_error("No compiled artifacts for manifest line: " + line);
            shader.generatedBase = mapIt->second.first; shader.artifactRoot = mapIt->second.second;
            if (!names.insert(shader.logical).second) throw std::runtime_error("Duplicate logical name " + shader.logical);
            shaders.push_back(std::move(shader));
        }
    }
    if (shaders.empty()) throw std::runtime_error("No shaders in manifests");
    // Native-only hand-authored sources live outside generated hlsl/ but participate in the same
    // cbuffer-space and @legacy annotation validation.
    auto tags = annotations(root / "hlsl");
    const auto nativeTags = annotations(root / "native_src");
    for (const auto &[name, tag] : nativeTags) {
        if (tags.count(name) && tags.at(name) != tag) throw std::runtime_error("Conflicting @legacy annotation for " + name);
        tags[name] = tag;
    }
    auto spaces = declaredSpaces(root / "hlsl");
    const auto nativeSpaces = declaredSpaces(root / "native_src");
    for (const auto &[name, space] : nativeSpaces) {
        if (spaces.count(name) && spaces.at(name) != space) throw std::runtime_error("Conflicting cbuffer register spaces: " + name);
        spaces[name] = space;
    }
    for (const auto &[name, space] : spaces) if (space != 1) throw std::runtime_error("Space-0 cbuffer rejected: " + name);
    std::map<std::string, Block> shared;
    std::map<std::string, std::string> output;
    std::vector<std::pair<fs::path, fs::path>> publish;
    std::ostringstream report;
    struct RegistryEntry { std::string logical, stage, block; };
    std::vector<RegistryEntry> registry;
    for (auto &sh : shaders) {
        const fs::path nativeInc = sh.artifactRoot / "include" / (sh.generatedBase + ".inc");
        const bool nativeOnly = sh.profile == "native";
        if (nativeOnly) {
            sh.inc = readText(nativeInc);
        } else {
            const std::string legacyBase = legacyIncludeBase(sh.legacySource, sh.stage, sh.profile);
            // Profile "20" logicals keep their _ps20 runtime name (the -ver 20b reference is only the ABI check).
            sh.legacyName = sh.profile == "20" ? baseName(sh.legacySource, sh.stage, "20") : legacyBase;
            const fs::path legacyInc = staging / "legacy" / sh.profile / "include" / (legacyBase + ".inc");
            if (!fs::exists(legacyInc)) throw std::runtime_error("Missing legacy combo include: " + legacyInc.string());
            sh.inc = readText(nativeInc); sh.legacyInc = readText(legacyInc);
            if (comboABI(sh.inc, sh.generatedBase) != comboABI(sh.legacyInc, legacyBase) || skips(sh.inc) != skips(sh.legacyInc))
                throw std::runtime_error("Combo ABI mismatch: " + sh.logical + " vs " + sh.legacySource + " (" + sh.profile + ")");
        }
        const fs::path nativeVcs = sh.artifactRoot / "shaders" / "fxc" / (sh.generatedBase + ".vcs");
        sh.vcs = nativeVcs;
        const auto bytes = readBytes(nativeVcs);
        ShaderVcsFile vcs; CUtlString error;
        const auto stage = sh.stage == "vs" ? VcsStage::Vertex : sh.stage == "ps" ? VcsStage::Pixel : VcsStage::Compute;
        if (!vcs.OpenBytes(bytes.data(), bytes.size(), stage, nativeVcs.string().c_str(), error)) throw std::runtime_error(error.Get());
        if (vcs.Version() != 6) throw std::runtime_error("Native compiler did not emit VCS v6: " + sh.logical);
        sh.dynamicCount = vcs.DynamicComboCount();
        const unsigned total = static_cast<unsigned>(*reinterpret_cast<const uint32_t *>(bytes.data() + 4));
        std::vector<unsigned> staticIndices;
        for (size_t ordinal = 0;; ++ordinal) { unsigned index = 0; if (!vcs.StaticComboIndex(ordinal, index)) break; staticIndices.push_back(index); }
        sh.staticCount = static_cast<unsigned>(staticIndices.size());
        for (const unsigned index : staticIndices) {
            if (!vcs.LoadStaticCombo(index, error)) throw std::runtime_error(error.Get());
            for (unsigned dynamic = 0; dynamic < sh.dynamicCount; ++dynamic) {
                const auto *payload = vcs.DynamicPayload(index, dynamic);
                if (!payload) continue;
                ++sh.present;
                sh.presentCombos.insert(uint64_t(index) * sh.dynamicCount + dynamic);
                ComPtr<ID3D12ShaderReflection> reflection;
                HRESULT hr = D3DReflect(payload->tokens.Base(), payload->tokens.Count(), IID_PPV_ARGS(&reflection));
                if (FAILED(hr)) throw std::runtime_error("D3DReflect rejected DXBC: " + sh.logical);
                D3D12_SHADER_DESC desc{};
                const unsigned expectedStage = stage == VcsStage::Vertex ? D3D12_SHVER_VERTEX_SHADER :
                                               stage == VcsStage::Pixel ? D3D12_SHVER_PIXEL_SHADER : D3D12_SHVER_COMPUTE_SHADER;
                if (FAILED(reflection->GetDesc(&desc)) || static_cast<unsigned>(D3D12_SHVER_GET_TYPE(desc.Version)) != expectedStage)
                    throw std::runtime_error("DXBC stage mismatch: " + sh.logical);
                if (stage == VcsStage::Compute) {
                    for (unsigned resource = 0; resource < desc.BoundResources; ++resource) {
                        D3D12_SHADER_INPUT_BIND_DESC binding{};
                        if (FAILED(reflection->GetResourceBindingDesc(resource, &binding)))
                            throw std::runtime_error("Cannot reflect resource binding: " + sh.logical);
                        if (binding.Type == D3D_SIT_CBUFFER) {
                            if (binding.Space != 1 || binding.BindPoint != 0 || binding.BindCount != 1)
                                throw std::runtime_error("Compute cbuffer must be b0 space1: " + sh.logical);
                        } else {
                            if (binding.Space != 0)
                                throw std::runtime_error("Compute resource must use space0: " + sh.logical);
                            const unsigned maxSlots = binding.Type == D3D_SIT_SAMPLER ? 2u : 8u;
                            if (binding.BindPoint >= maxSlots || !binding.BindCount || binding.BindCount > maxSlots - binding.BindPoint)
                                throw std::runtime_error("Compute resource exceeds root signature slot range: " + sh.logical);
                        }
                    }
                }
                for (unsigned i = 0; i < desc.ConstantBuffers; ++i) {
                    auto *buffer = reflection->GetConstantBufferByIndex(i);
                    D3D12_SHADER_BUFFER_DESC bd{};
                    if (FAILED(buffer->GetDesc(&bd))) throw std::runtime_error("Cannot reflect cbuffer");
                    D3D12_SHADER_INPUT_BIND_DESC binding{};
                    if (FAILED(reflection->GetResourceBindingDescByName(bd.Name, &binding)))
                        throw std::runtime_error("Cannot bind reflected cbuffer " + std::string(bd.Name));
                    const unsigned blockStage = stage == VcsStage::Vertex ? 0u : stage == VcsStage::Pixel ? 1u : 2u;
                    auto b = reflectBlock(reflection.Get(), buffer, bd, binding, blockStage);
                    if (stage == VcsStage::Compute && (binding.Space != 1 || binding.BindPoint != 0 || bd.Size > 256))
                        throw std::runtime_error("Compute cbuffer violates b0 space1/256-byte contract: " + sh.logical);
                    const auto declared = spaces.find(b.name);
                    if (declared != spaces.end() && declared->second != binding.Space)
                        throw std::runtime_error("Reflected cbuffer register space disagrees with source: " + b.name);
                    b.space = binding.Space;
                    if (stage != VcsStage::Compute)
                        annotate(b, tags);
                    const auto existing = sh.blocks.find(b.name);
                    if (existing != sh.blocks.end() && (existing->second.canonical != b.canonical || existing->second.reg != b.reg || existing->second.space != b.space))
                        throw std::runtime_error("Cbuffer differs between combos: " + sh.logical + "." + b.name);
                    sh.blocks[b.name] = std::move(b);
                }
            }
        }
        if (!sh.present) throw std::runtime_error("No present DXBC payloads: " + sh.logical);
        // Native-only logicals have no legacy runtime record or combo-ABI comparison.
        const fs::path legacyVcs = game / "shaders" / "fxc" / (sh.legacyName + ".vcs");
        if (!nativeOnly && fs::exists(legacyVcs)) {
            const auto old = readBytes(legacyVcs);
            ShaderVcsFile legacy; if (!legacy.OpenBytes(old.data(), old.size(), stage, legacyVcs.string().c_str(), error)) throw std::runtime_error(error.Get());
            if (legacy.DynamicComboCount() != sh.dynamicCount || *reinterpret_cast<const uint32_t *>(old.data() + 4) != total)
                throw std::runtime_error("Legacy runtime combo count mismatch: " + sh.logical);
            for (const unsigned index : staticIndices) {
                if (!legacy.LoadStaticCombo(index, error)) throw std::runtime_error(error.Get());
                for (unsigned dynamic = 0; dynamic < sh.dynamicCount; ++dynamic)
                    if ((legacy.DynamicPayload(index, dynamic) != nullptr) !=
                        (sh.presentCombos.count(uint64_t(index) * sh.dynamicCount + dynamic) != 0))
                        throw std::runtime_error("Legacy runtime skip mismatch: " + sh.logical);
            }
        }
        if (sh.stage != "cs") {
            for (const auto &[name, block] : sh.blocks) {
                auto found = shared.find(name);
                if (found != shared.end() && (found->second.canonical != block.canonical || found->second.reg != block.reg || found->second.space != block.space || found->second.stage != block.stage))
                    throw std::runtime_error("Shared cbuffer layout mismatch: " + name);
                shared[name] = block;
            }
        }
        std::string inc = tokenRename(sh.inc, sh.generatedBase, sh.logical);
        if (sh.stage == "cs") {
            // Compute constants are copied raw by IShaderAPIDX12Compute; retain only the combo index classes.
            output["inc/" + sh.logical + ".inc"] = inc;
        } else {
            // <logical>_Block names the shader's material block type. Constructing <logical>_Dynamic_Index (every
            // dynamic-state selection path does) selects it, so BaseVSShaderDX12 builds and writes exactly that block
            // from its staged legacy-register values at Draw().
            std::string block;
            for (const auto &[name, b] : sh.blocks) {
                if (b.engine) continue;
                if (!block.empty()) throw std::runtime_error("More than one material block in " + sh.logical);
                block = name;
            }
            const std::string anchor = "#include \"shaderlib/cshader.h\"\n";
            const auto at = inc.find(anchor);
            if (at == std::string::npos) throw std::runtime_error("Unexpected include layout: " + sh.logical);
            std::string decl = "#include \"BaseVSShaderDX12.h\"\n";
            if (block.empty()) decl += "typedef DX12NoNativeBlock " + sh.logical + "_Block;\n";
            else decl += "#include \"" + block + ".h\"\ntypedef dx12cb::" + block + " " + sh.logical + "_Block;\n";
            inc.insert(at + anchor.size(), decl);
            const std::regex ctor("(\\n\\t" + sh.logical + "_Dynamic_Index\\([^)]*\\)\\s*\\n\\t\\{\\n)");
            std::smatch c;
            if (!std::regex_search(inc, c, ctor)) throw std::runtime_error("Dynamic index constructor not found: " + sh.logical);
            inc.insert(c.position(0) + c.length(0), "\t\tDX12SelectNativeBlock< " + sh.logical + "_Block >( dx12native::" +
                       (sh.stage == "vs" ? "kStageVertex" : "kStagePixel") + " );\n");
            output["inc/" + sh.logical + ".inc"] = inc;
            registry.push_back({sh.logical, sh.stage, block});
        }
        const auto detail = describe(sh);
        output["meta/" + sh.logical + ".txt"] = detail;
        report << detail;
        const char *directory = sh.stage == "vs" ? "vsh" : sh.stage == "ps" ? "psh" : "csh";
        publish.emplace_back(nativeVcs, game / "shaders" / directory / (sh.logical + ".vcs"));
    }
    for (const auto &[name, b] : shared) output["cbuffers/" + name + ".h"] = header(b);
    {
        // Name -> block writer table for shaders selected by name (IShaderShadow::Set*Shader(name) +
        // Set*ShaderIndex), e.g. screenspace_general's $pixshader. Sorted for binary search.
        std::sort(registry.begin(), registry.end(), [](const RegistryEntry &a, const RegistryEntry &b) { return lower(a.logical) < lower(b.logical); });
        std::ostringstream r;
        r << "// generated by nativeshaderpack_dx12: every native logical shader and its material block writer\n";
        std::set<std::string> included;
        for (const auto &e : registry) if (!e.block.empty() && included.insert(e.block).second) r << "#include \"" << e.block << ".h\"\n";
        r << "static const DX12NativeBlockRegistryEntry kDX12NativeBlockRegistry[] = {\n";
        for (const auto &e : registry)
            r << "\t{ \"" << lower(e.logical) << "\", dx12native::" << (e.stage == "vs" ? "kStageVertex" : "kStagePixel") << ", "
              << (e.block.empty() ? std::string("nullptr") : "&DX12WriteBlockFromStaging< dx12cb::" + e.block + " >") << " },\n";
        r << "};\n";
        output["inc/native_block_registry.inc"] = r.str();
    }
    for (const auto &line : fallback) {
        report << "legacy-fallback " << line << "\n";
        // "# legacy <logical> <vs|ps> <reason>": the SDK's legacy .inc with a passthrough selection, so material C++
        // keeps its DECLARE_*_SHADER code and the staged constants reach the legacy register files. The stage is
        // explicit (assembly names such as WorldVertexAlpha carry no profile suffix).
        std::istringstream words(line.substr(9)); std::string logical, stage; words >> logical >> stage;
        if (stage != "vs" && stage != "ps") throw std::runtime_error("Legacy-only line without vs|ps stage: " + line);
        const fs::path legacyInc = root.parent_path() / "stdshaders" / "include" / (logical + ".inc");
        if (!fs::exists(legacyInc)) throw std::runtime_error("Legacy-only logical without an SDK include: " + logical);
        std::string inc = readText(legacyInc);
        // SDK include files are CRLF; the constructor anchor (like the generated includes) is LF.
        inc.erase(std::remove(inc.begin(), inc.end(), '\r'), inc.end());
        inc = "#pragma once\n#include \"BaseVSShaderDX12.h\"\n" + inc;
        const std::regex ctor("(\\n\\t" + logical + "_Dynamic_Index\\([^)]*\\)\\s*\\n\\t\\{\\n)", std::regex::icase);
        std::smatch c;
        if (!std::regex_search(inc, c, ctor)) throw std::runtime_error("Legacy dynamic index constructor not found: " + logical);
        const bool vertex = stage == "vs";
        inc.insert(c.position(0) + c.length(0), std::string("\t\tDX12SelectLegacyPassthrough( dx12native::") + (vertex ? "kStageVertex" : "kStagePixel") + " );\n");
        output["inc/" + logical + ".inc"] = inc;
    }
    // Assembly .vsh/.psh entries of the DX9 manifests are never compiled natively; they stay on shaders/fxc.
    const fs::path legacyManifestRoot = root.parent_path() / "stdshaders";
    std::set<std::string> assembly;
    for (const char *legacyManifest : {"stdshader_dx9_20b.txt", "stdshader_dx9_30.txt"}) {
        if (!fs::exists(legacyManifestRoot / legacyManifest)) continue;
        std::istringstream lines(readText(legacyManifestRoot / legacyManifest)); std::string entry;
        while (std::getline(lines, entry)) {
            entry = trim(entry);
            const auto ext = fs::path(entry).extension().string();
            if (!entry.empty() && entry.rfind("//", 0) != 0 && (ext == ".vsh" || ext == ".psh")) assembly.insert(entry);
        }
    }
    for (const auto &entry : assembly) report << "legacy-fallback assembly " << entry << "\n";
    output["report.txt"] = report.str();
    // generated/ and the published VCS set are owned by this tool: each run replaces them completely.
    const fs::path generated = root / "generated";
    for (const char *dir : {"inc", "cbuffers", "meta"}) fs::remove_all(generated / dir);
    for (const auto &[name, contents] : output) writeText(generated / name, contents);
    const fs::path owned = game / "shaders" / "native_dx12_published.txt";
    std::set<fs::path> targets;
    for (const auto &[source, target] : publish) targets.insert(target);
    if (fs::exists(owned)) {
        std::istringstream previous(readText(owned)); std::string line;
        while (std::getline(previous, line)) {
            line = trim(line);
            if (!line.empty() && !targets.count(game / line)) fs::remove(game / line);
        }
    }
    std::ostringstream ownedList;
    for (const auto &[source, target] : publish) {
        fs::create_directories(target.parent_path());
        fs::copy_file(source, target, fs::copy_options::overwrite_existing);
        ownedList << fs::relative(target, game).generic_string() << '\n';
        std::cout << "published " << target.string() << '\n';
    }
    // Native logical -> legacy DX9 logical for fallback. Native-only logicals get a two-column
    // marker so runtime can fail closed without attempting a shaders/fxc fallback.
    std::ostringstream legacyNames;
    for (const auto &sh : shaders) {
        if (sh.profile == "native") legacyNames << sh.logical << ' ' << sh.stage << '\n';
        else legacyNames << sh.logical << ' ' << sh.stage << ' ' << sh.legacyName << '\n';
    }
    const fs::path legacyNamesPath = game / "shaders" / "native_dx12_legacy_names.txt";
    writeText(legacyNamesPath, legacyNames.str());
    ownedList << fs::relative(legacyNamesPath, game).generic_string() << '\n';
    writeText(owned, ownedList.str());
    std::cout << "validated " << shaders.size() << " shaders, " << shared.size() << " cbuffers; generated " << generated.string() << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        fs::path root, staging, game;
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 == argc) throw std::runtime_error("Usage: nativeshaderpack_dx12 -root <dir> -staging <dir> -game <dir>");
            const std::string key = argv[i];
            if (key == "-root") root = argv[i + 1];
            else if (key == "-staging") staging = argv[i + 1];
            else if (key == "-game") game = argv[i + 1];
            else throw std::runtime_error("Unknown option " + key);
        }
        if (root.empty() || staging.empty() || game.empty()) throw std::runtime_error("Missing -root/-staging/-game");
        run(root, staging, game);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "nativeshaderpack_dx12: " << e.what() << '\n';
        return 1;
    }
}
