//========= Copyright Valve Corporation, All rights reserved. ============//
// CPU profile selection adapted from shaderdevicebase.cpp; no legacy device ABI.
#include "dxsupport_dx12.h"
#include "filesystem.h"
#include "tier1/KeyValues.h"
#include "tier1/utlbuffer.h"
#include "tier1/strtools.h"
#include "tier0/icommandline.h"
#include "tier0/platform.h"
#include "tier0/dbg.h"
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <climits>
#include <vector>
#include <cctype>

namespace shaderapidx12
{
namespace
{
// KeyValues::LoadFromBuffer reports syntax errors but still returns true.
// Validate the key/value and brace structure before allowing partial profiles.
bool LoadProfile(KeyValues *values, IFileSystem *fs, const char *name, const char *path)
{
    FileHandle_t file = fs->Open(name, "rb", path);
    if (file == FILESYSTEM_INVALID_HANDLE) return false;
    const unsigned length = fs->Size(file);
    if (length > static_cast<unsigned>(INT_MAX - 1)) { fs->Close(file); return false; }
    CUtlBuffer data(0, static_cast<int>(length + 1), CUtlBuffer::TEXT_BUFFER);
    const int count = fs->Read(data.Base(), static_cast<int>(length), file);
    fs->Close(file);
    if (count != static_cast<int>(length)) return false;
    data.SeekPut(CUtlBuffer::SEEK_HEAD, count);
    const char *text = static_cast<const char *>(data.Base());
    const size_t size = length;
    std::vector<bool> expectsValue(1, false);
    bool sawRoot = false;
    for (size_t i = 0; i < size; )
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isspace(c)) { ++i; continue; }
        if (c == '/' && i + 1 < size && text[i + 1] == '/')
        {
            while (i < size && text[i] != '\n') ++i;
            continue;
        }
        if (c == '{')
        {
            if (!expectsValue.back() || expectsValue.size() >= 128) return false;
            expectsValue.back() = false;
            expectsValue.push_back(false); sawRoot = true; ++i; continue;
        }
        if (c == '}')
        {
            if (expectsValue.size() == 1 || expectsValue.back()) return false;
            expectsValue.pop_back(); ++i; continue;
        }
        if (c == '[')
        {
            while (i < size && text[i] != ']') ++i;
            if (i == size) return false;
            ++i; continue;
        }
        if (c == '"')
        {
            ++i;
            while (i < size && text[i] != '"') ++i;
            if (i == size) return false;
            ++i;
        }
        else
        {
            const size_t start = i;
            while (i < size && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != '{' && text[i] != '}') ++i;
            if (i == start || !c) return false;
        }
        expectsValue.back() = !expectsValue.back();
    }
    if (!sawRoot || expectsValue.size() != 1 || expectsValue.back()) return false;
    return values->LoadFromBuffer(name, data, fs, path);
}
int Hex(KeyValues *group, const char *key)
{
    const char *text = group->GetString(key, nullptr);
    if (!text) return -1;
    char *end = nullptr;
    long value = strtol(text, &end, 16);
    return end != text && !*end && value >= 0 && value <= INT_MAX ? static_cast<int>(value) : -1;
}
void AddKey(KeyValues *dest, KeyValues *src)
{
    switch (src->GetDataType())
    {
    case KeyValues::TYPE_STRING: dest->SetString(src->GetName(), src->GetString()); break;
    case KeyValues::TYPE_INT: dest->SetInt(src->GetName(), src->GetInt()); break;
    case KeyValues::TYPE_FLOAT: dest->SetFloat(src->GetName(), src->GetFloat()); break;
    case KeyValues::TYPE_PTR: dest->SetPtr(src->GetName(), src->GetPtr()); break;
    case KeyValues::TYPE_WSTRING: dest->SetWString(src->GetName(), src->GetWString()); break;
    case KeyValues::TYPE_COLOR: dest->SetColor(src->GetName(), src->GetColor()); break;
    case KeyValues::TYPE_UINT64: dest->SetUint64(src->GetName(), src->GetUint64()); break;
    default: break;
    }
}
void OverrideValues(KeyValues *dest, KeyValues *src)
{
    for (KeyValues *value = src->GetFirstValue(); value; value = value->GetNextValue()) AddKey(dest, value);
    for (KeyValues *dir = src->GetFirstTrueSubKey(); dir; dir = dir->GetNextTrueSubKey())
    {
        KeyValues *match = dest->FindKey(dir->GetName());
        if (match && match->GetDataType() == KeyValues::TYPE_NONE) OverrideValues(match, dir);
    }
}
void MergeOverrides(KeyValues *dest, KeyValues *src)
{
    for (KeyValues *overrideGroup = src->GetFirstTrueSubKey(); overrideGroup; overrideGroup = overrideGroup->GetNextTrueSubKey())
    {
        const char *name = overrideGroup->GetString("name", nullptr);
        const int vendor = Hex(overrideGroup, "VendorID");
        const int lo = Hex(overrideGroup, "MinDeviceID"), hi = Hex(overrideGroup, "MaxDeviceID");
        for (KeyValues *group = dest->GetFirstTrueSubKey(); group; group = group->GetNextTrueSubKey())
        {
            if (name && Q_stricmp(name, group->GetString("name", ""))) continue;
            if (vendor >= 0 && vendor != Hex(group, "VendorID")) continue;
            if (lo >= 0 && hi >= 0 && (Hex(group, "MinDeviceID") < lo || Hex(group, "MaxDeviceID") < 0 || Hex(group, "MaxDeviceID") > hi)) continue;
            OverrideValues(group, overrideGroup);
            break;
        }
    }
}
KeyValues *FindDXLevel(KeyValues *root, int level, int vendor = -1)
{
    for (KeyValues *group = root->GetFirstTrueSubKey(); group; group = group->GetNextTrueSubKey())
        if (group->GetInt("name", 0) == level && (vendor < 0 || Hex(group, "VendorID") == vendor)) return group;
    return nullptr;
}
KeyValues *FindCard(KeyValues *root, unsigned vendor, unsigned device)
{
    for (KeyValues *group = root->GetFirstTrueSubKey(); group; group = group->GetNextTrueSubKey())
    {
        const int lo = Hex(group, "MinDeviceID"), hi = Hex(group, "MaxDeviceID");
        if (Hex(group, "VendorID") == static_cast<int>(vendor) && lo >= 0 && hi >= lo && device >= static_cast<unsigned>(lo) && device <= static_cast<unsigned>(hi)) return group;
    }
    return nullptr;
}
KeyValues *FindRange(KeyValues *root, const char *minKey, const char *maxKey, uint64_t value, const char *name = nullptr)
{
    for (KeyValues *group = root->GetFirstTrueSubKey(); group; group = group->GetNextTrueSubKey())
    {
        if (name && !Q_stristr(group->GetString("name", ""), name)) continue;
        const int lo = group->GetInt(minKey, -1), hi = group->GetInt(maxKey, -1);
        if (lo >= 0 && hi >= 0 && value >= static_cast<uint64_t>(lo) && value < static_cast<uint64_t>(hi)) return group;
    }
    return nullptr;
}
template<typename Apply>
void ApplyGPUProfiles(KeyValues *root, int level, unsigned vendor, unsigned device, Apply apply)
{
    apply(FindDXLevel(root, level));
    KeyValues *card = FindCard(root, vendor, device);
    apply(card);
    // A precise card profile supersedes the vendor/level profile. A catch-all does not.
    if (card && Hex(card, "MinDeviceID") == 0 && Hex(card, "MaxDeviceID") == 0xffff)
        apply(FindDXLevel(root, level, static_cast<int>(vendor)));
}
void Dump(KeyValues *values)
{
    CUtlBuffer text;
    values->RecursiveSaveToFile(text, 0);
    Warning("%s\n", static_cast<const char *>(text.Base()));
}
void LoadConfig(KeyValues *group, KeyValues *dest)
{
    if (!group) return;
    for (KeyValues *value = group->GetFirstSubKey(); value; value = value->GetNextKey()) AddKey(dest, value);
}
}
CDXSupportDX12::~CDXSupportDX12() { Clear(); }
void CDXSupportDX12::Clear()
{
    if (config_) config_->deleteThis();
    config_ = nullptr;
}
bool CDXSupportDX12::Load(IFileSystem *filesystem)
{
    Clear();
    if (CommandLine()->CheckParm("-ignoredxsupportcfg")) return true;
    if (!filesystem) return false;
    if (!filesystem->FileExists("dxsupport.cfg", "EXECUTABLE_PATH")) return true;
    KeyValues *base = new KeyValues("dxsupport");
    if (!LoadProfile(base, filesystem, "dxsupport.cfg", "EXECUTABLE_PATH"))
    {
        Warning("shaderapidx12: malformed dxsupport.cfg; retaining hardware capabilities\n");
        base->deleteThis();
        return false;
    }
    if (filesystem->FileExists("dxsupport_override.cfg", "GAME"))
    {
        KeyValues *overrides = new KeyValues("dxsupport_override");
        if (!LoadProfile(overrides, filesystem, "dxsupport_override.cfg", "GAME"))
        {
            Warning("shaderapidx12: malformed dxsupport_override.cfg; discarding configuration overrides\n");
            overrides->deleteThis();
            base->deleteThis();
            return false;
        }
        MergeOverrides(base, overrides);
        overrides->deleteThis();
    }
    config_ = base;
    if (CommandLine()->CheckParm("-debugdxsupport")) Dump(config_);
    return true;
}
void CDXSupportDX12::ReadDXSupportLevels(DXSupportCapsDX12 &caps) const
{
    if (!config_) return;
    KeyValues *card = FindCard(config_, caps.vendor, caps.device);
    if (!card) return;
    const int maxLevel = card->GetInt("MaxDXLevel", 0);
    const int preferred = card->GetInt("DXLevel", 0);
    if (maxLevel) caps.max = std::min(caps.max, maxLevel);
    caps.recommended = std::min(caps.max, preferred ? preferred : caps.max);
}
void CDXSupportDX12::ReadHardwareCaps(DXSupportCapsDX12 &caps, int dxLevel) const
{
    if (config_) ApplyGPUProfiles(config_, dxLevel, caps.vendor, caps.device, [&](KeyValues *group)
    {
        if (!group) return;
        caps.fastClipping = group->GetInt("NoUserClipPlanes", caps.fastClipping ? 1 : 0) != 0;
        caps.centroidHack = group->GetInt("CentroidHack", caps.centroidHack ? 1 : 0) != 0;
        caps.disableShaderOptimizations = group->GetInt("DisableShaderOptimizations", caps.disableShaderOptimizations ? 1 : 0) != 0;
    });
    if (CommandLine()->CheckParm("-nouserclip")) caps.fastClipping = true;
}
bool CDXSupportDX12::GetRecommendedConfigurationInfo(const DXSupportCapsDX12 &caps, int level, KeyValues *configuration) const
{
    return GetRecommendedConfigurationInfo(caps, level, caps.vendor, caps.device, configuration);
}
bool CDXSupportDX12::GetRecommendedConfigurationInfo(const DXSupportCapsDX12 &caps, int level, unsigned vendor, unsigned device, KeyValues *configuration) const
{
    if (!configuration) return false;
    if (!level) level = caps.recommended;
    // This backend implements the current 90/95 material paths, not legacy fixed-function levels.
    if (level < 90 || level > caps.max || level > 95) return false;
    level = level < 95 ? 90 : 95;
    if (!config_) return true;
    ApplyGPUProfiles(config_, level, vendor, device, [&](KeyValues *group) { LoadConfig(group, configuration); });
    const CPUInformation &cpu = *GetCPUInformation();
    const uint64_t mhz = static_cast<uint64_t>(cpu.m_Speed / 1000000);
    LoadConfig(FindRange(config_, "min megahertz", "max megahertz", mhz, Q_stristr(cpu.m_szProcessorID, "amd") ? "AMD" : "Intel"), configuration);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) LoadConfig(FindRange(config_, "min megabytes", "max megabytes", memory.ullTotalPhys / (1024ull * 1024)), configuration);
    const uint64_t videoMB = caps.memory / (1024ull * 1024);
    KeyValues *video = FindRange(config_, "min megatexels", "max megatexels", videoMB);
    if (video && caps.memory && (level == caps.max || videoMB < 100))
    {
        KeyValues *picmip = video->FindKey("ConVar.mat_picmip");
        if (picmip) configuration->SetInt("ConVar.mat_picmip", std::max(picmip->GetInt(), configuration->GetInt("ConVar.mat_picmip", 0)));
    }
    configuration->SetInt("ConVar.mat_dxlevel", level);
    if (CommandLine()->CheckParm("-debugdxsupport")) Dump(configuration);
    return true;
}
} // namespace shaderapidx12
