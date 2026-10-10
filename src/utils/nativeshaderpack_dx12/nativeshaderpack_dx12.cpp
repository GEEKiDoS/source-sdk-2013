#include "shader_vcs_dx12.h"
#include "native_engine_cbuffers_dx12.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
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
struct Combo {
	std::string name;
	uint32_t minimum = 0, maximum = 0;
	bool dynamic = false;
	int slot = -1; // Retained combos have no payload slot.
};
struct ComboFold {
	std::vector<Combo> original;
	uint32_t originalStatic = 1, originalDynamic = 1, nativeStatic = 1, nativeDynamic = 1;
	bool enabled = false;
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
	ComboFold fold;
	uint32_t lightmapSamplerMask = 0, highresAbi = 0;
	bool earlyDepthTwin = false;
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
std::string embedHlsl(const char *name, const std::string &text) {
    // Numeric initializers avoid MSVC's individual/concatenated string limits.
    // The consumer still gets one zero-terminated, read-only array with no assembly.
    std::ostringstream out;
    out << "static const char " << name << "[] = {\n";
    size_t column = 0;
    for (unsigned char byte : text) {
        if (byte < 128) out << unsigned(byte);
        else out << "char(" << unsigned(byte) << ")";
        out << ',';
        if (++column == 32) { out << '\n'; column = 0; }
    }
    out << "0};\n";
    return out.str();
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
// ShaderCompile2 visits quoted includes in source order, independent of HLSL
// preprocessor conditions. Its first declared combo is the least-significant radix.
bool selectedComboDirective(const std::string &line, const std::string &stage, const std::string &version) {
	if (line.find("[XBOX]") != std::string::npos) return false;
	static const std::regex qualifier(R"(\[([vpgdhc]s)(\d+\w?)\])");
	bool qualified = false, matched = false;
	for (std::sregex_iterator i(line.begin(), line.end(), qualifier), end; i != end; ++i) {
		if ((*i)[1].str() != stage) return false;
		qualified = true;
		matched = matched || (*i)[2].str() == version;
	}
	return !qualified || matched;
}
Combo comboDeclaration(const std::smatch &m, bool folded) {
	Combo c;
	c.name = m[2].str();
	const auto minimum = std::stoull(m[3].str()), maximum = std::stoull(m[4].str());
	if (minimum > maximum || maximum > INT32_MAX)
		throw std::runtime_error("Invalid combo range: " + c.name);
	c.minimum = static_cast<uint32_t>(minimum);
	c.maximum = static_cast<uint32_t>(maximum);
	c.dynamic = folded ? m[5].str() == "DYNAMIC" : m[1].str() == "DYNAMIC";
	if (folded) {
		if (minimum == maximum) throw std::runtime_error("Fixed-value combo must remain retained: " + c.name);
		const auto slot = std::stoull(m[6].str());
		if (slot >= 64) throw std::runtime_error("Fold slot exceeds 63: " + c.name);
		c.slot = static_cast<int>(slot);
	}
	return c;
}
const std::regex &comboDirectivePattern() {
	static const std::regex pattern(R"combo(^\s*//\s*(STATIC|DYNAMIC)\s*:\s*"([^"]+)"\s+"(\d+)\.\.(\d+)".*$)combo");
	return pattern;
}
void originalCombos(const fs::path &file, const fs::path &includeRoot, const std::string &stage, const std::string &version,
                    std::vector<Combo> &out, std::set<fs::path> &active) {
	const auto canonical = fs::weakly_canonical(file);
	if (!active.insert(canonical).second) throw std::runtime_error("Recursive shader include: " + file.string());
	static const std::regex include(R"inc(#\s*include\s*"([^"]+)")inc");
	static const std::regex inlineComment(R"(/\*.*?\*/)");
	std::istringstream lines(readText(file));
	std::string line;
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back(); // legacy sources are CRLF; '.' never matches '\r'
		line = std::regex_replace(line, inlineComment, "");
		std::smatch m;
		if (line.rfind("//", 0) != 0 && std::regex_search(line, m, include)) {
			auto included = file.parent_path() / m[1].str();
			if (!fs::exists(included)) included = includeRoot / m[1].str();
			originalCombos(included, includeRoot, stage, version, out, active);
		} else if (std::regex_match(line, m, comboDirectivePattern()) && selectedComboDirective(line, stage, version)) {
			out.push_back(comboDeclaration(m, false));
		}
	}
	active.erase(canonical);
}
uint32_t comboCount(const std::vector<Combo> &combos, bool dynamic, bool retainedOnly) {
	uint64_t count = 1;
	for (const auto &c : combos) {
		if (c.dynamic != dynamic || (retainedOnly && c.slot >= 0)) continue;
		const uint64_t radix = uint64_t(c.maximum) - c.minimum + 1;
		if (count > (std::numeric_limits<uint32_t>::max)() / radix)
			throw std::runtime_error("Combo count overflows uint32: " + c.name);
		count *= radix;
	}
	return static_cast<uint32_t>(count);
}
std::string foldDescription(const Shader &sh) {
	if (!sh.fold.enabled) return {};
	std::ostringstream out;
	out << "fold " << sh.logical << ' ' << sh.stage << ' ' << sh.fold.originalStatic << ' '
	    << sh.fold.originalDynamic << ' ' << sh.fold.nativeStatic << ' ' << sh.fold.nativeDynamic << '\n';
	for (bool dynamic : {false, true}) {
		for (const auto &c : sh.fold.original) {
			if (c.dynamic != dynamic) continue;
			out << (dynamic ? "D " : "S ") << c.name << ' ' << c.minimum << ' ' << c.maximum << ' ';
			if (c.slot < 0) out << "native";
			else out << "slot" << c.slot;
			out << '\n';
		}
	}
	out << "end\n";
	return out.str();
}
std::string ordinaryLogical(const std::string &logical) {
	return tokenRename(tokenRename(tokenRename(logical, "_earlydepth_", "_"), "_shadowmap_", "_"), "_highres_", "_");
}
void loadComboFolds(const fs::path &root, std::vector<Shader> &shaders) {
	static const std::regex foldPrefix(R"(^\s*//\s*FOLD\s*:)");
	static const std::regex foldLine(R"combo(^\s*//\s*(FOLD)\s*:\s*"([^"]+)"\s+"(\d+)\.\.(\d+)"\s+(STATIC|DYNAMIC)\s+slot=(\d+)\s*$)combo");
	static const std::regex skipLine(R"(^\s*//\s*SKIP\s*:\s*(.*)$)");
	static const std::regex skipIdentifier(R"(\$?([A-Za-z_]\w*))");
	for (auto &sh : shaders) {
		const auto native = root / "hlsl" / sh.source;
		// Hand-authored native-only shaders live outside hlsl and cannot fold a legacy ABI.
		if (!fs::exists(native)) continue;
		std::vector<Combo> declared, folded;
		std::vector<std::string> skipExpressions;
		std::istringstream lines(readText(native));
		std::string line;
		while (std::getline(lines, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			std::smatch m;
			if (std::regex_search(line, foldPrefix)) {
				if (!std::regex_match(line, m, foldLine))
					throw std::runtime_error("Malformed FOLD directive in " + sh.logical + ": " + line);
				folded.push_back(comboDeclaration(m, true));
			} else if (std::regex_match(line, m, comboDirectivePattern()) && selectedComboDirective(line, sh.stage, "51")) {
				declared.push_back(comboDeclaration(m, false));
			} else if (std::regex_match(line, m, skipLine)) {
				skipExpressions.push_back(m[1].str());
			}
		}
		if (folded.empty()) continue;
		const Shader *reference = &sh;
		if (sh.profile == "native") {
			const auto ordinary = ordinaryLogical(sh.logical);
			const auto found = std::find_if(shaders.begin(), shaders.end(), [&](const Shader &s) { return s.logical == ordinary; });
			if (ordinary == sh.logical || found == shaders.end() || found->profile == "native" || found->stage != sh.stage)
				throw std::runtime_error("Folded feature shader has no ordinary legacy reference: " + sh.logical);
			reference = &*found;
		}
		if (reference->legacySource.empty()) throw std::runtime_error("Folded shader has no legacy source: " + sh.logical);
		auto source = root / "legacy_reference" / reference->legacySource;
		if (!fs::exists(source)) source = root.parent_path() / "stdshaders" / reference->legacySource;
		// Reference overrides only replace the main file; quoted includes stay in stdshaders.
		std::set<fs::path> active;
		const auto version = reference->profile == "30" ? "30" : sh.stage == "vs" ? "20" : reference->profile == "20" ? "20" : "20b";
		originalCombos(source, root.parent_path() / "stdshaders", sh.stage, version, sh.fold.original, active);
		std::map<std::string, Combo> nativeByName;
		for (const auto &c : declared)
			if (!nativeByName.emplace(c.name, c).second) throw std::runtime_error("Duplicate retained combo in " + sh.logical + ": " + c.name);
		unsigned nextSlot = 0;
		for (bool dynamic : {false, true}) {
			for (const auto &c : folded) {
				if (c.dynamic != dynamic) continue;
				if (c.slot != static_cast<int>(nextSlot++))
					throw std::runtime_error("FOLD slots must be dense STATIC then DYNAMIC order: " + sh.logical + "." + c.name);
				if (!nativeByName.emplace(c.name, c).second) throw std::runtime_error("Duplicate folded combo in " + sh.logical + ": " + c.name);
			}
		}
		std::set<std::string> originals;
		for (auto &c : sh.fold.original) {
			const auto found = nativeByName.find(c.name);
			if (!originals.insert(c.name).second || found == nativeByName.end() || found->second.dynamic != c.dynamic ||
			    found->second.minimum != c.minimum || found->second.maximum != c.maximum)
				throw std::runtime_error("Fold combo ABI mismatch: " + sh.logical + "." + c.name);
			c.slot = found->second.slot;
		}
		if (originals.size() != nativeByName.size()) throw std::runtime_error("Extra native combo in " + sh.logical);
		for (const auto &expression : skipExpressions) {
			for (std::sregex_iterator i(expression.begin(), expression.end(), skipIdentifier), end; i != end; ++i) {
				const auto name = (*i)[1].str();
				const auto found = nativeByName.find(name);
				if ((found != nativeByName.end() && found->second.slot >= 0) ||
				    (found == nativeByName.end() && (*i)[0].str().front() == '$'))
					throw std::runtime_error("SKIP references a non-retained combo: " + sh.logical + "." + name);
			}
		}
		for (bool dynamic : {false, true}) {
			size_t at = 0;
			for (const auto &c : sh.fold.original) {
				if (c.dynamic != dynamic || c.slot >= 0) continue;
				while (at < declared.size() && declared[at].dynamic != dynamic) ++at;
				if (at == declared.size() || declared[at++].name != c.name)
					throw std::runtime_error("Retained combo order mismatch: " + sh.logical);
			}
		}
		sh.fold.originalStatic = comboCount(sh.fold.original, false, false);
		sh.fold.originalDynamic = comboCount(sh.fold.original, true, false);
		sh.fold.nativeStatic = comboCount(sh.fold.original, false, true);
		sh.fold.nativeDynamic = comboCount(sh.fold.original, true, true);
		sh.fold.enabled = true;
	}
	for (const auto &sh : shaders) {
		const auto ordinary = ordinaryLogical(sh.logical);
		if (ordinary == sh.logical) continue;
		const auto found = std::find_if(shaders.begin(), shaders.end(), [&](const Shader &s) { return s.logical == ordinary; });
		if (found == shaders.end()) continue;
		if (sh.fold.enabled != found->fold.enabled ||
		    (sh.fold.enabled && tokenRename(foldDescription(sh), sh.logical, ordinary) != foldDescription(*found)))
			throw std::runtime_error("Ordinary/shadowmap fold table mismatch: " + sh.logical);
	}
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
    Block b; b.name = bd.Name; b.size = bd.Size; b.stage = stage; b.reg = binding.BindPoint; b.space = binding.Space;
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
const dx12native::EngineCBufferLayoutDX12 *lightingLayout(const std::string &name) {
    for (const auto &layout : dx12native::kLightingCBufferLayouts)
        if (name == layout.name) return &layout;
    return nullptr;
}
void validateLightingBlock(const Block &b) {
    const auto *layout = lightingLayout(b.name);
    if (!layout || b.stage != layout->stage || b.reg != layout->shaderRegister ||
        b.space != DX12_LIGHTING_REGISTER_SPACE || b.size != layout->byteSize || b.members.size() != layout->memberCount)
        throw std::runtime_error("Lighting ABI 6 cbuffer mismatch: " + b.name);
    for (size_t i = 0; i < b.members.size(); ++i) {
        const auto &m = b.members[i];
        const auto &expected = layout->members[i];
        if (m.name != expected.name || m.offset != expected.offset || m.size != expected.size)
            throw std::runtime_error("Lighting ABI 6 member mismatch: " + b.name + "." + m.name);
        if (b.reg == DX12_LIGHTING_B_PROP_DRAW &&
            (m.kind != D3D_SVC_VECTOR || m.type != (i == 0 || i == 2 ? D3D_SVT_UINT : D3D_SVT_FLOAT) ||
             m.rows != 1 || m.cols != 4 || m.elements != (i == 1 ? 3u : 0u) || m.stride != (i == 1 ? 16u : 0u)))
            throw std::runtime_error("Lighting ABI 6 static-prop member shape mismatch: " + b.name + "." + m.name);
    }
}
// Element struct of a structured-buffer binding (a single struct, not an array); null when the reflection shape is unexpected.
ID3D12ShaderReflectionType *structuredElementType(ID3D12ShaderReflection *reflection, const D3D12_SHADER_INPUT_BIND_DESC &binding, D3D12_SHADER_TYPE_DESC &type) {
    auto *buffer = reflection->GetConstantBufferByName(binding.Name);
    D3D12_SHADER_BUFFER_DESC bd{};
    if (!buffer || FAILED(buffer->GetDesc(&bd)) || bd.Type != D3D_CT_RESOURCE_BIND_INFO || bd.Variables != 1) return nullptr;
    auto *element = buffer->GetVariableByIndex(0);
    auto *elementType = element ? element->GetType() : nullptr;
    return elementType && SUCCEEDED(elementType->GetDesc(&type)) && !type.Elements ? elementType : nullptr;
}
// Matches the authored layout, including row-major matrices and arrays.
bool matchStructuredMembers(ID3D12ShaderReflectionType *elementType, const D3D12_SHADER_TYPE_DESC &type, const char *typeName,
                            const dx12native::LightingStructuredMemberDX12 *members, unsigned memberCount) {
    if (type.Class != D3D_SVC_STRUCT || !type.Name || strcmp(type.Name, typeName) || type.Members != memberCount) return false;
    for (unsigned i = 0; i < memberCount; ++i) {
        const auto &expected = members[i];
        const char *name = elementType->GetMemberTypeName(i);
        auto *memberType = elementType->GetMemberTypeByIndex(i);
        D3D12_SHADER_TYPE_DESC member{};
        if (!name || strcmp(name, expected.name) || !memberType || FAILED(memberType->GetDesc(&member)) ||
            member.Offset != expected.offset || member.Class != expected.valueClass ||
            member.Type != expected.scalarType || member.Rows != expected.rows ||
            member.Columns != expected.columns || member.Elements != expected.elements) return false;
    }
    return true;
}
bool lightingStructuredType(ID3D12ShaderReflection *reflection, const D3D12_SHADER_INPUT_BIND_DESC &binding) {
    D3D12_SHADER_TYPE_DESC type{};
    auto *elementType = structuredElementType(reflection, binding, type);
    if (!elementType) return false;
    if (binding.BindPoint == DX12_LIGHTING_T_LIGHTS)
        return matchStructuredMembers(elementType, type, "RuntimeShadowLightGpu", dx12native::kRuntimeShadowLightGpuMembers,
            sizeof(dx12native::kRuntimeShadowLightGpuMembers) / sizeof(*dx12native::kRuntimeShadowLightGpuMembers));
    if (binding.BindPoint == DX12_LIGHTING_T_PROP_TRIANGLES)
        return matchStructuredMembers(elementType, type, "DX12StaticPropTriangleGpu", dx12native::kDX12StaticPropTriangleGpuMembers,
            sizeof(dx12native::kDX12StaticPropTriangleGpuMembers) / sizeof(*dx12native::kDX12StaticPropTriangleGpuMembers));
    const unsigned columns = binding.BindPoint == DX12_LIGHTING_T_TILE_RANGES ? 2 :
        binding.BindPoint == DX12_LIGHTING_T_VISIBILITY_FACES || binding.BindPoint == DX12_LIGHTING_T_VISIBILITY_ENTRIES ||
        binding.BindPoint == DX12_LIGHTING_T_PROP_MESHES ? 4 : 1;
    return type.Type == D3D_SVT_UINT && type.Rows == 1 && type.Columns == columns &&
           type.Class == (columns > 1 ? D3D_SVC_VECTOR : D3D_SVC_SCALAR);
}
void validateLightingResources(ID3D12ShaderReflection *reflection, const D3D12_SHADER_DESC &desc, VcsStage stage, const std::string &logical) {
    struct Resource { const char *name; unsigned reg, count, stride; };
    static const Resource resources[] = {
        {"g_ShadowLocalAtlas", DX12_LIGHTING_T_LOCAL_ATLAS_FIRST, DX12_SHADOW_MAX_LOCAL_PAGES, 0},
        {"g_ShadowCascadeAtlas", DX12_LIGHTING_T_CASCADE_ATLAS, 1, 0},
        {"g_ShadowStaticSun", DX12_LIGHTING_T_STATIC_SUN, 1, 0},
        {"g_ShadowLights", DX12_LIGHTING_T_LIGHTS, 1, sizeof(RuntimeShadowLightGpu)},
        {"g_ShadowTileRanges", DX12_LIGHTING_T_TILE_RANGES, 1, 8},
        {"g_ShadowTileIndices", DX12_LIGHTING_T_TILE_INDICES, 1, 4},
        {"g_ShadowSunVisibility", DX12_LIGHTING_T_SUN_VISIBILITY, 1, 0},
        {"g_ShadowVisibilityFaces", DX12_LIGHTING_T_VISIBILITY_FACES, 1, 16},
        {"g_ShadowVisibilityEntries", DX12_LIGHTING_T_VISIBILITY_ENTRIES, 1, 16},
        {"g_ShadowVisibilityPayload", DX12_LIGHTING_T_VISIBILITY_PAYLOAD, 1, 0},
        {"g_ShadowVisibilityPropMeshes", DX12_LIGHTING_T_PROP_MESHES, 1, 16},
        {"g_ShadowPropTriangles", DX12_LIGHTING_T_PROP_TRIANGLES, 1, sizeof(DX12StaticPropTriangleGpu)},
    };
    bool marker = false, space2 = false;
    unsigned seen = 0;
    // Reflection contains only retained resources. The view block marks the ABI;
    // unused textures/samplers may disappear, but every retained binding must match.
    for (unsigned i = 0; i < desc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        if (FAILED(reflection->GetResourceBindingDesc(i, &binding)) || !binding.Name)
            throw std::runtime_error("Cannot reflect lighting resource binding");
        const auto *layout = lightingLayout(binding.Name);
        if (binding.Space != DX12_LIGHTING_REGISTER_SPACE && !layout) continue;
        space2 = true;
        unsigned slot = 0;
        bool valid = stage == VcsStage::Pixel && binding.Space == DX12_LIGHTING_REGISTER_SPACE;
        if (layout) {
            slot = layout->shaderRegister == DX12_LIGHTING_B_VIEW ? 0 : 14;
            valid = valid && binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == layout->shaderRegister && binding.BindCount == 1;
            auto *buffer = reflection->GetConstantBufferByName(binding.Name);
            D3D12_SHADER_BUFFER_DESC bd{};
            valid = valid && buffer && SUCCEEDED(buffer->GetDesc(&bd)) && bd.Type == D3D_CT_CBUFFER && bd.Name;
            if (valid) validateLightingBlock(reflectBlock(reflection, buffer, bd, binding, dx12native::kStagePixel));
            if (layout->shaderRegister == DX12_LIGHTING_B_VIEW) marker = true;
        } else if (binding.Type == D3D_SIT_SAMPLER) {
            const bool comparison = (binding.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0;
            const bool shadow = comparison && binding.BindPoint == DX12_LIGHTING_S_COMPARISON && !strcmp(binding.Name, "g_ShadowCmpSampler");
            valid = valid && binding.BindCount == 1 && shadow;
            slot = 13;
        } else {
            const Resource *resource = nullptr;
            for (unsigned r = 0; r < sizeof(resources) / sizeof(*resources); ++r)
                if (!strcmp(binding.Name, resources[r].name)) { resource = &resources[r]; slot = 1 + r; break; }
            valid = valid && resource && binding.BindPoint == resource->reg && binding.BindCount == resource->count;
            if (valid && resource->stride)
                valid = binding.Type == D3D_SIT_STRUCTURED && binding.Dimension == D3D_SRV_DIMENSION_BUFFER &&
                        binding.NumSamples == resource->stride && lightingStructuredType(reflection, binding);
            else if (valid && resource->reg == DX12_LIGHTING_T_VISIBILITY_PAYLOAD)
                valid = binding.Type == D3D_SIT_BYTEADDRESS && binding.Dimension == D3D_SRV_DIMENSION_BUFFER;
            else if (valid)
                valid = binding.Type == D3D_SIT_TEXTURE && binding.Dimension == D3D_SRV_DIMENSION_TEXTURE2D &&
                        binding.ReturnType == (resource->reg == DX12_LIGHTING_T_SUN_VISIBILITY ? D3D_RETURN_TYPE_UINT : D3D_RETURN_TYPE_FLOAT) &&
                        !(binding.uFlags & D3D_SIF_TEXTURE_COMPONENTS);
        }
        if (!valid || (seen & (1u << slot))) throw std::runtime_error("Lighting ABI 6 binding mismatch: " + std::string(binding.Name));
        seen |= 1u << slot;
    }
    if (space2 && !marker) throw std::runtime_error("Space-2 resources require DX12LightingViewConstantsV1");
    constexpr unsigned sharedVisibility = (1u << 9) | (1u << 10);
    constexpr unsigned propVisibility = (1u << 11) | (1u << 12) | (1u << 14);
    if (((seen & sharedVisibility) && (seen & sharedVisibility) != sharedVisibility) ||
        ((seen & (1u << 8)) && (seen & sharedVisibility) != sharedVisibility) ||
        ((seen & propVisibility) && ((seen & propVisibility) != propVisibility || (seen & sharedVisibility) != sharedVisibility)))
        throw std::runtime_error("Lighting ABI 6 incomplete visibility resource group: " + logical);
    static const char *const propReceivers[] = {
        "vertexlit_and_unlit_generic_shadowmap_ps51", "vertexlit_and_unlit_generic_bump_shadowmap_ps51",
        "skin_shadowmap_ps51", "eyes_shadowmap_ps51", "eye_refract_shadowmap_ps51",
        "teeth_shadowmap_ps51", "teeth_bump_shadowmap_ps51", "treeleaf_shadowmap_ps51",
        "cable_shadowmap_ps51", "vortwarp_shadowmap_ps51",
    };
    for (const char *receiver : propReceivers)
        if (marker && logical == receiver && (seen & propVisibility) != propVisibility)
            throw std::runtime_error("Lighting ABI 6 model receiver requires static-prop visibility: " + logical);
    if (seen & propVisibility) {
        bool primitiveId = false;
        for (unsigned i = 0; i < desc.InputParameters; ++i) {
            D3D12_SIGNATURE_PARAMETER_DESC input{};
            if (FAILED(reflection->GetInputParameterDesc(i, &input)))
                throw std::runtime_error("Cannot reflect static-prop primitive identity: " + logical);
            if (input.SystemValueType == D3D_NAME_PRIMITIVE_ID && input.ComponentType == D3D_REGISTER_COMPONENT_UINT32 &&
                input.Mask == 1) primitiveId = true;
        }
        if (!primitiveId) throw std::runtime_error("Static-prop visibility requires uint SV_PrimitiveID: " + logical);
    }
    const bool carrier = logical == "lightmappedgeneric_shadowmap_ps51" || logical == "worldtwotextureblend_shadowmap_ps51" ||
                         logical == "lightmappedreflective_shadowmap_ps51" || logical == "lightmappedgeneric_decal_shadowmap_ps51";
    if (marker && carrier && !(seen & (1u << 7)))
        throw std::runtime_error("Lighting ABI 6 receiver requires packed uint t1029: " + logical);
}
void validateHighresResources(ID3D12ShaderReflection *reflection, const D3D12_SHADER_DESC &desc, VcsStage stage, const Shader &shader) {
	static const char *const names[] = {"HlightFaceIds", "HlightFaces", "HlightTiles", "HlightDynamic",
		"HlightPages2048", "HlightPages4096", "HlightPages8192", "HlightPages16384"};
	unsigned seen = 0;
	for (unsigned i = 0; i < desc.BoundResources; ++i) {
		D3D12_SHADER_INPUT_BIND_DESC b{};
		if (FAILED(reflection->GetResourceBindingDesc(i, &b))) throw std::runtime_error("Highres binding reflection failed");
		if (b.Space != 3) continue;
		bool valid = stage == VcsStage::Pixel && shader.highresAbi && b.BindCount == 1;
		unsigned slot = 0;
		if (b.Type == D3D_SIT_CBUFFER) {
			slot = 8;
			valid = valid && b.BindPoint == 0 && !strcmp(b.Name, "DX12HighresDrawConstants");
		} else if (b.Type == D3D_SIT_SAMPLER) {
			slot = 9;
			valid = valid && b.BindPoint == 0 && !strcmp(b.Name, "HlightLinear") && !(b.uFlags & D3D_SIF_COMPARISON_SAMPLER);
		} else if (b.Type == D3D_SIT_UAV_RWBYTEADDRESS) {
			slot = 10;
			valid = valid && b.BindPoint == 0 && !strcmp(b.Name, "HlightFailure");
		} else {
			slot = b.BindPoint;
			valid = valid && slot < 8 && !strcmp(b.Name, names[slot]);
			if (valid && (slot == 1 || slot == 2))
				valid = b.Type == D3D_SIT_STRUCTURED && b.Dimension == D3D_SRV_DIMENSION_BUFFER &&
					b.NumSamples == (slot == 1 ? 160u : 32u);
			else if (valid)
				valid = b.Type == D3D_SIT_TEXTURE &&
					b.Dimension == (slot < 4 ? D3D_SRV_DIMENSION_TEXTURE2D : D3D_SRV_DIMENSION_TEXTURE2DARRAY) &&
					b.ReturnType == (slot == 0 ? D3D_RETURN_TYPE_UINT : D3D_RETURN_TYPE_FLOAT);
		}
		if (!valid || (seen & (1u << slot))) throw std::runtime_error("Highres resource contract mismatch: " + std::string(b.Name));
		seen |= 1u << slot;
	}
	if (seen && seen != 0x7ffu) throw std::runtime_error("Incomplete highres resource contract: " + shader.logical);
}
bool pbrSpotGpuType(ID3D12ShaderReflection *reflection, const D3D12_SHADER_INPUT_BIND_DESC &binding) {
	D3D12_SHADER_TYPE_DESC type{};
	auto *elementType = structuredElementType(reflection, binding, type);
	return elementType && matchStructuredMembers(elementType, type, "PBRSpotGpu", dx12native::kPBRSpotGpuMembers,
		sizeof(dx12native::kPBRSpotGpuMembers) / sizeof(*dx12native::kPBRSpotGpuMembers));
}
const dx12native::EngineCBufferLayoutDX12 *probeLayout(const std::string &name) {
	for (const auto &layout : dx12native::kProbeCBufferLayouts)
		if (name == layout.name) return &layout;
	return nullptr;
}
// DX12ProbeConstantsV1: b0 space4, four vectors (float4, uint4, float4, uint4).
void validateProbeBlock(const Block &b) {
	const auto *layout = probeLayout(b.name);
	if (!layout || b.stage != layout->stage || b.reg != layout->shaderRegister || b.space != DX12_PROBE_REGISTER_SPACE ||
	    b.size != layout->byteSize || b.members.size() != layout->memberCount)
		throw std::runtime_error("Probe ABI cbuffer mismatch: " + b.name);
	for (size_t i = 0; i < b.members.size(); ++i) {
		const auto &m = b.members[i];
		const auto &expected = layout->members[i];
		if (m.name != expected.name || m.offset != expected.offset || m.size != expected.size || m.kind != D3D_SVC_VECTOR ||
		    m.type != unsigned(i & 1 ? D3D_SVT_UINT : D3D_SVT_FLOAT) || m.rows != 1 || m.cols != 4 || m.elements)
			throw std::runtime_error("Probe ABI member mismatch: " + b.name + "." + m.name);
	}
}
// Space 4: the DX12ProbeConstantsV1 block, ProbeIndirection t0 (Texture3D<uint>), ProbeDC t1 (float3), ProbeBands t2..t7
// (array of six float4), ProbeValidity t8 (float), ProbeLinear s0 (non-comparison). Reflection contains only retained
// resources, so any subset of the textures may appear, but each binding must match exactly and appear once. seen bit 0 is the block.
void validateProbeBinding(const D3D12_SHADER_INPUT_BIND_DESC &b, const std::string &logical, unsigned &seen) {
	struct Texture { const char *name; unsigned reg, count, components; D3D_RESOURCE_RETURN_TYPE returnType; };
	static const Texture textures[] = {
		{"ProbeIndirection", DX12_PROBE_T_INDIRECTION, 1, 1, D3D_RETURN_TYPE_UINT},
		{"ProbeDC", DX12_PROBE_T_DC, 1, 3, D3D_RETURN_TYPE_FLOAT},
		{"ProbeBands", DX12_PROBE_T_BANDS_FIRST, DX12_PROBE_T_VALIDITY - DX12_PROBE_T_BANDS_FIRST, 4, D3D_RETURN_TYPE_FLOAT},
		{"ProbeValidity", DX12_PROBE_T_VALIDITY, 1, 1, D3D_RETURN_TYPE_FLOAT},
	};
	unsigned bit = 0;
	bool valid = false;
	if (b.Type == D3D_SIT_CBUFFER) {
		const auto *layout = probeLayout(b.Name);
		bit = 1;
		valid = layout && b.BindPoint == layout->shaderRegister && b.BindCount == 1;
	} else if (!strcmp(b.Name, "ProbeLinear")) {
		bit = 1u << 5;
		valid = b.Type == D3D_SIT_SAMPLER && b.BindPoint == DX12_PROBE_S_LINEAR && b.BindCount == 1 && !(b.uFlags & D3D_SIF_COMPARISON_SAMPLER);
	} else {
		for (unsigned i = 0; i < sizeof(textures) / sizeof(*textures); ++i)
			if (!strcmp(b.Name, textures[i].name)) {
				bit = 2u << i;
				valid = b.Type == D3D_SIT_TEXTURE && b.Dimension == D3D_SRV_DIMENSION_TEXTURE3D && b.ReturnType == textures[i].returnType &&
					b.BindPoint == textures[i].reg && b.BindCount == textures[i].count &&
					(b.uFlags & D3D_SIF_TEXTURE_COMPONENTS) == ((textures[i].components - 1) << 2);
			}
	}
	if (!valid || (seen & bit)) throw std::runtime_error("Probe resource contract mismatch: " + logical + "." + b.Name);
	seen |= bit;
}
// Spaces 4 and 5 are backend-owned and pixel-stage only; space-4 bindings go through validateProbeBinding.
// Space 5: StructuredBuffer<PBRSpotGpu> g_ProjectedLights t0, g_ProjectedCookies[8] t1..t8 (Texture2D<float4>),
// g_ProjectedDepth[8] t9..t16 (Texture2D<float>), g_CookieSampler s0, g_ProjectedCmpSampler s1 (comparison).
// Reflection contains only retained resources, so any subset may appear, but every retained binding must match.
void validateProbePbrResources(ID3D12ShaderReflection *reflection, const D3D12_SHADER_DESC &desc, VcsStage stage, const std::string &logical) {
	unsigned probeSeen = 0;
	for (unsigned i = 0; i < desc.BoundResources; ++i) {
		D3D12_SHADER_INPUT_BIND_DESC b{};
		if (FAILED(reflection->GetResourceBindingDesc(i, &b))) throw std::runtime_error("PBR binding reflection failed");
		if (b.Space != DX12_PROBE_REGISTER_SPACE && b.Space != DX12_PBR_REGISTER_SPACE) continue;
		if (stage != VcsStage::Pixel)
			throw std::runtime_error("Space-" + std::to_string(b.Space) + " resource outside pixel stage: " + logical + "." + b.Name);
		if (b.Space == DX12_PROBE_REGISTER_SPACE) { validateProbeBinding(b, logical, probeSeen); continue; }
		if (b.Type == D3D_SIT_CBUFFER) throw std::runtime_error("Space-5 cbuffer rejected: " + logical + "." + b.Name);
		const bool lights = !strcmp(b.Name, "g_ProjectedLights"), cookies = !strcmp(b.Name, "g_ProjectedCookies"),
			depth = !strcmp(b.Name, "g_ProjectedDepth"), linear = !strcmp(b.Name, "g_CookieSampler"),
			comparison = !strcmp(b.Name, "g_ProjectedCmpSampler");
		bool valid = false;
		if (lights)
			valid = b.Type == D3D_SIT_STRUCTURED && b.BindPoint == DX12_PBR_T_LIGHTS && b.BindCount == 1 && b.Dimension == D3D_SRV_DIMENSION_BUFFER &&
				b.NumSamples == sizeof(DX12ProjectedLightDesc) && pbrSpotGpuType(reflection, b);
		else if (cookies || depth)
			valid = b.Type == D3D_SIT_TEXTURE && b.BindPoint == unsigned(cookies ? DX12_PBR_T_COOKIE_FIRST : DX12_PBR_T_DEPTH_FIRST) &&
				b.BindCount == DX12_PBR_MAX_PROJECTED_LIGHTS && b.Dimension == D3D_SRV_DIMENSION_TEXTURE2D && b.ReturnType == D3D_RETURN_TYPE_FLOAT &&
				(b.uFlags & D3D_SIF_TEXTURE_COMPONENTS) == (cookies ? unsigned(D3D_SIF_TEXTURE_COMPONENTS) : 0u);
		else if (linear || comparison)
			valid = b.Type == D3D_SIT_SAMPLER && b.BindPoint == unsigned(comparison ? DX12_PBR_S_COMPARISON : DX12_PBR_S_LINEAR) && b.BindCount == 1 &&
				((b.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0) == comparison;
		if (!valid) throw std::runtime_error("PBR resource contract mismatch: " + logical + "." + b.Name);
	}
	if (probeSeen && !(probeSeen & 1)) throw std::runtime_error("Space-4 resources require DX12ProbeConstantsV1: " + logical);
}
void validateDepthRestoreBlock(const Block &b, const Shader &shader, const D3D12_SHADER_INPUT_BIND_DESC &binding) {
    const bool restore = shader.profile == "native" &&
        ((shader.stage == "vs" && shader.logical == "shadow_depth_restore_vs51") ||
         (shader.stage == "ps" && shader.logical == "shadow_depth_restore_ps51"));
    if (!restore || b.name != "ShadowDepthRestoreConstants" || b.space != 0 || b.reg != 0 ||
        binding.Type != D3D_SIT_CBUFFER || binding.BindCount != 1 || b.size != 64 || b.members.size() != 4)
        throw std::runtime_error("Space-0 cbuffer outside depth-restore contract: " + shader.logical + "." + b.name);
    static const char *const names[] = {"srcRect", "dstRect", "srcSize", "dstSize"};
    for (unsigned i = 0; i < 4; ++i) {
        const auto &m = b.members[i];
        if (m.name != names[i] || m.offset != i * 16 || m.size != 16 || m.kind != D3D_SVC_VECTOR ||
            m.type != D3D_SVT_UINT || m.rows != 1 || m.cols != 4 || m.elements)
            throw std::runtime_error("Depth-restore member mismatch: " + m.name);
    }
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
	if (b.name == "DX12HighresDrawConstants") {
		if (b.stage != dx12native::kStagePixel || b.space != 3 || b.reg != 0 ||
		    b.size != sizeof(dx12native::DX12HighresDrawConstants) || b.members.size() != 3)
			throw std::runtime_error("Highres draw cbuffer mismatch");
		for (size_t i = 0; i < 3; ++i) {
			const auto &expected = dx12native::kDX12HighresDrawConstantsMembers[i];
			const auto &member = b.members[i];
			if (member.name != expected.name || member.offset != expected.offset || member.size != expected.size)
				throw std::runtime_error("Highres draw member mismatch: " + member.name);
		}
		b.engine = true;
		return;
	}
    if (lightingLayout(b.name) || b.space == DX12_LIGHTING_REGISTER_SPACE) {
        validateLightingBlock(b);
        b.engine = true;
        return;
    }
    // Space 4 ambient-probe block (pixel stage): exact layout, backend-owned, never a material or @legacy block.
    if (probeLayout(b.name) || b.space == DX12_PROBE_REGISTER_SPACE) {
        validateProbeBlock(b);
        b.engine = true;
        return;
    }
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
          << "using " << b.name << " = " << (b.name == "DX12StaticPropDrawConstants" ? "::" : "dx12native::")
          << ((b.name == "DX12ComboFoldPS" || b.name == "DX12ComboFoldVS") ? "DX12ComboFold" : b.name)
          << ";\n} // namespace dx12cb\n";
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
	std::map<std::string, std::string> earlyDepthTwins;
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
			if (line.rfind("# earlydepth ", 0) == 0) {
				std::istringstream fields(line.substr(13));
				std::string base, twin, extra;
				if (!(fields >> base >> twin) || (fields >> extra) || base.size() < 5 ||
				    base.compare(base.size() - 5, 5, "_ps51") || base.find("_highres_") == std::string::npos ||
				    base.find("_earlydepth_") != std::string::npos ||
				    twin != base.substr(0, base.size() - 5) + "_earlydepth_ps51" ||
				    !earlyDepthTwins.emplace(base, twin).second)
					throw std::runtime_error("Malformed early-depth twin declaration: " + line);
				continue;
			}
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
	loadComboFolds(root, shaders);
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
    for (const auto &[name, space] : spaces) {
        const auto *lighting = lightingLayout(name);
        if (name == "DX12HighresDrawConstants" ? space != 3 :
            lighting ? space != DX12_LIGHTING_REGISTER_SPACE :
            probeLayout(name) ? space != DX12_PROBE_REGISTER_SPACE :
            name == "ShadowDepthRestoreConstants" ? space != 0 : space != 1)
            throw std::runtime_error("Cbuffer outside its declared register-space contract: " + name);
    }
    std::map<std::string, Block> shared;
    std::map<std::string, std::string> output;
	output["inc/highres_lightmaps_hlsl.inc"] = embedHlsl("kHighresLightmapsHlsl",
		readText(root / "native_src" / "highres_lightmaps.hlsli"));
	// Fixed-function shaders are compiled without an include handler. Embed the same
	// authored selected-direct kernel and the space2-only constant/type declarations.
	const std::string engineHlsl = readText(root / "hlsl" / "common" / "dx12_engine_cbuffers.h");
	const size_t lightingStart = engineHlsl.find("#if defined(DX12_SHADOWMAPS)");
	const size_t outerEnd = engineHlsl.rfind("#endif");
	if (lightingStart == std::string::npos || outerEnd <= lightingStart)
		throw std::runtime_error("Cannot locate standalone space2 lighting mirror");
	output["inc/shadowmap_lighting_hlsl.inc"] = embedHlsl("kShadowmapLightingHlsl",
		std::string("#define DX12_SHADOWMAPS 1\n") + engineHlsl.substr(lightingStart, outerEnd - lightingStart) +
		readText(root / "native_src" / "shadowmap_lighting.hlsli"));
    std::vector<std::pair<fs::path, fs::path>> publish;
    std::ostringstream report;
    struct RegistryEntry { std::string logical, stage, block; };
    std::vector<RegistryEntry> registry;
    for (auto &sh : shaders) {
		const fs::path authored = fs::exists(root / "hlsl" / sh.source) ? root / "hlsl" / sh.source : root / "native_src" / sh.source;
		const std::string authoredText = readText(authored);
		std::smatch samplerRoles;
		if (std::regex_search(authoredText, samplerRoles, std::regex(R"(//\s*HIGHLIGHT_SAMPLERS:\s*(\d+))")))
			sh.lightmapSamplerMask = uint32_t(std::stoul(samplerRoles[1].str()));
		sh.highresAbi = sh.logical.find("_highres_") != std::string::npos ? 1u : 0u;
		sh.earlyDepthTwin = earlyDepthTwins.count(sh.logical) != 0;
		if (sh.stage != "ps" && sh.lightmapSamplerMask)
			throw std::runtime_error("Lightmap roles declared outside pixel stage: " + sh.logical);
		if (sh.lightmapSamplerMask & ~0xffffu)
			throw std::runtime_error("Lightmap sampler mask exceeds native samplers: " + sh.logical);
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
			sh.legacyInc = readText(legacyInc);
			if (sh.fold.enabled) {
				// C++ keeps the full, pre-fold combo ABI; only the compiled VCS is reduced.
				sh.inc = tokenRename(sh.legacyInc, legacyBase, sh.generatedBase);
			} else {
				sh.inc = readText(nativeInc);
				if (comboABI(sh.inc, sh.generatedBase) != comboABI(sh.legacyInc, legacyBase) || skips(sh.inc) != skips(sh.legacyInc))
					throw std::runtime_error("Combo ABI mismatch: " + sh.logical + " vs " + sh.legacySource + " (" + sh.profile + ")");
			}
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
		if (sh.fold.enabled && (sh.dynamicCount != sh.fold.nativeDynamic ||
		    uint64_t(total) != uint64_t(sh.fold.nativeStatic) * sh.fold.nativeDynamic))
			throw std::runtime_error("Folded VCS combo count mismatch: " + sh.logical);
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
                validateLightingResources(reflection.Get(), desc, stage, sh.logical);
				validateHighresResources(reflection.Get(), desc, stage, sh);
				validateProbePbrResources(reflection.Get(), desc, stage, sh.logical);
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
				bool reflectedFold = false;
                for (unsigned i = 0; i < desc.ConstantBuffers; ++i) {
                    auto *buffer = reflection->GetConstantBufferByIndex(i);
                    D3D12_SHADER_BUFFER_DESC bd{};
                    if (FAILED(buffer->GetDesc(&bd))) throw std::runtime_error("Cannot reflect cbuffer");
                    // Structured-buffer element types are reflected as RESOURCE_BIND_INFO, not cbuffers.
                    if (bd.Type == D3D_CT_RESOURCE_BIND_INFO) continue;
                    if (bd.Type != D3D_CT_CBUFFER) throw std::runtime_error("Unsupported constant-buffer type: " + std::string(bd.Name));
                    D3D12_SHADER_INPUT_BIND_DESC binding{};
                    if (FAILED(reflection->GetResourceBindingDescByName(bd.Name, &binding)))
                        throw std::runtime_error("Cannot bind reflected cbuffer " + std::string(bd.Name));
                    const unsigned blockStage = stage == VcsStage::Vertex ? 0u : stage == VcsStage::Pixel ? 1u : 2u;
                    auto b = reflectBlock(reflection.Get(), buffer, bd, binding, blockStage);
					if (b.name == "DX12ComboFoldPS" || b.name == "DX12ComboFoldVS") {
						const auto expected = sh.stage == "ps" ? "DX12ComboFoldPS" : "DX12ComboFoldVS";
						if (!sh.fold.enabled || sh.stage == "cs" || b.name != expected || binding.BindCount != 1)
							throw std::runtime_error("Unexpected combo-fold cbuffer: " + sh.logical + "." + b.name);
						validateEngine(b);
						if (b.members.size() != 1) throw std::runtime_error("Combo-fold member count mismatch: " + sh.logical);
						const auto &m = b.members.front();
						if (m.name != "cComboFold" || m.type != D3D_SVT_UINT || m.kind != D3D_SVC_VECTOR ||
						    m.rows != 1 || m.cols != 4 || m.elements != 16 || m.offset != 0 || m.size != 256 || m.stride != 16)
							throw std::runtime_error("Combo-fold uint4[16] member mismatch: " + sh.logical);
						reflectedFold = true;
					}
                    if (stage == VcsStage::Compute && (binding.Space != 1 || binding.BindPoint != 0 || bd.Size > 256))
                        throw std::runtime_error("Compute cbuffer violates b0 space1/256-byte contract: " + sh.logical);
                    const auto declared = spaces.find(b.name);
                    if (declared != spaces.end() && declared->second != binding.Space)
                        throw std::runtime_error("Reflected cbuffer register space disagrees with source: " + b.name);
                    b.space = binding.Space;
                    if (binding.Space == 0) {
                        validateDepthRestoreBlock(b, sh, binding);
                        b.engine = true;
                    } else if (stage != VcsStage::Compute) {
                        annotate(b, tags);
                    }
                    const auto existing = sh.blocks.find(b.name);
                    if (existing != sh.blocks.end() && (existing->second.canonical != b.canonical || existing->second.reg != b.reg || existing->second.space != b.space))
                        throw std::runtime_error("Cbuffer differs between combos: " + sh.logical + "." + b.name);
                    sh.blocks[b.name] = std::move(b);
                }
				if (sh.fold.enabled && !reflectedFold)
					throw std::runtime_error("Folded shader does not reflect its combo-fold cbuffer: " + sh.logical);
            }
        }
        if (!sh.present) throw std::runtime_error("No present DXBC payloads: " + sh.logical);
        // Native-only logicals have no legacy runtime record or combo-ABI comparison.
        const fs::path legacyVcs = game / "shaders" / "fxc" / (sh.legacyName + ".vcs");
        if (!nativeOnly && !sh.fold.enabled && fs::exists(legacyVcs)) {
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
                // Backend-owned space-2/3/4 and restore blocks never become material writers/aliases or generated headers.
                if (block.space != 1) continue;
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
		if (sh.lightmapSamplerMask || sh.highresAbi) {
			const uint32_t metadata[4] = {0x544c484eu, 1u, sh.lightmapSamplerMask, sh.highresAbi};
			const fs::path sidecar = sh.artifactRoot / "hlight" / (sh.logical + ".hlight");
			writeText(sidecar, std::string(reinterpret_cast<const char *>(metadata), sizeof(metadata)));
			publish.emplace_back(sidecar, game / "shaders" / directory / (sh.logical + ".hlight"));
		}
		if (sh.earlyDepthTwin) {
			const uint32_t metadata[2] = {0x59445245u, 1u};
			const fs::path sidecar = sh.artifactRoot / "earlydepth" / (sh.logical + ".earlydepth");
			writeText(sidecar, std::string(reinterpret_cast<const char *>(metadata), sizeof(metadata)));
			publish.emplace_back(sidecar, game / "shaders" / directory / (sh.logical + ".earlydepth"));
		}
    }
	// A declaration grants backend early-depth admission; publish it only with an
	// output/combo-compatible native twin from this same pack, never a stale GAME file.
	for (const auto &[baseName, twinName] : earlyDepthTwins) {
		const auto base = std::find_if(shaders.begin(), shaders.end(), [&](const Shader &s) { return s.logical == baseName; });
		const auto twin = std::find_if(shaders.begin(), shaders.end(), [&](const Shader &s) { return s.logical == twinName; });
		if (base == shaders.end() || twin == shaders.end() || base->stage != "ps" || twin->stage != "ps" ||
		    !base->highresAbi || !twin->highresAbi || base->lightmapSamplerMask != twin->lightmapSamplerMask ||
		    base->dynamicCount != twin->dynamicCount || base->staticCount != twin->staticCount ||
		    base->presentCombos != twin->presentCombos || base->fold.enabled != twin->fold.enabled ||
		    base->fold.originalStatic != twin->fold.originalStatic || base->fold.originalDynamic != twin->fold.originalDynamic ||
		    base->fold.nativeStatic != twin->fold.nativeStatic || base->fold.nativeDynamic != twin->fold.nativeDynamic ||
		    base->fold.original.size() != twin->fold.original.size() || base->blocks.size() != twin->blocks.size())
			throw std::runtime_error("Incompatible early-depth twin: " + baseName);
		for (size_t i = 0; i < base->fold.original.size(); ++i) {
			const auto &a = base->fold.original[i], &b = twin->fold.original[i];
			if (a.name != b.name || a.minimum != b.minimum || a.maximum != b.maximum || a.dynamic != b.dynamic || a.slot != b.slot)
				throw std::runtime_error("Early-depth combo-fold mismatch: " + baseName);
		}
		for (const auto &[name, block] : base->blocks) {
			const auto found = twin->blocks.find(name);
			if (found == twin->blocks.end() || block.canonical != found->second.canonical)
				throw std::runtime_error("Early-depth cbuffer mismatch: " + baseName + ": " + name);
		}
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
	std::ostringstream comboFolds;
	for (const auto &sh : shaders) comboFolds << foldDescription(sh);
	const fs::path comboFoldsPath = game / "shaders" / "native_dx12_combo_fold.txt";
	writeText(comboFoldsPath, comboFolds.str());
	ownedList << fs::relative(comboFoldsPath, game).generic_string() << '\n';
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
