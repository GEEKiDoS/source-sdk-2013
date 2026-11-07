// Standalone production shared-validator probe; no BSP admission or engine launch.
#include "hlight_bsp.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <iomanip>
#include <cstdlib>
#include <cmath>

static bool Load(const char *path, std::vector<uint32> &storage, uint32 &bytes)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff length = file.tellg();
    if (length < 0 || uint64(length) > hlight::kMaxFileBytes) return false;
    bytes = uint32(length);
    storage.resize((bytes + 3u) / 4u);
    file.seekg(0);
    return !bytes || bool(file.read(reinterpret_cast<char *>(storage.data()), bytes));
}
static void Entries(const hlight::VisibilityView &view, uint32 first, uint32 count)
{
    std::cout << '[';
    for (uint32 i = 0; i < count; ++i)
    {
        if (i) std::cout << ',';
        const hlight::VisibilityEntryDisk &entry = view.entries[first + i];
        const uint32 samples = entry.encoding == hlight::kVisibilityDense ? entry.sampleCount : 1;
        std::cout << "{\"light\":" << entry.selectedLightIndex << ",\"encoding\":" << entry.encoding
                  << ",\"samples\":" << samples << ",\"values\":[";
        // Test fixtures are tiny. Real assets may contain large planes; keep probe output bounded.
        const uint32 shown = MIN(samples, 64u);
        for (uint32 sample = 0; sample < shown; ++sample)
        {
            if (sample) std::cout << ',';
            std::cout << hlight::VisibilitySample(view, entry, sample);
        }
        std::cout << "]}";
    }
    std::cout << ']';
}
static double Half(uint16 word)
{
    const uint32 exponent = (word >> 10) & 31u, mantissa = word & 1023u;
    const double value = exponent ? std::ldexp(double(1024u + mantissa), int(exponent) - 25) :
                                    std::ldexp(double(mantissa), -24);
    return word & 0x8000u ? -value : value;
}
static void Direct(const hlight::VisibilityView &view, const hlight::PropMeshVisibilityDisk &mesh)
{
    const hlight::PropDirectView direct = hlight::GetPropDirect(view, mesh);
    if (!direct.record) { std::cout << "null"; return; }
    const hlight::PropDirectDisk &d = *direct.record;
    std::cout << "{\"flags\":" << d.flags << ",\"vertices\":" << d.vertexCount
              << ",\"planes\":" << d.angularPlaneCount << ",\"radiance_bytes\":" << d.radianceBytes
              << ",\"styles\":[";
    for (uint32 i = 0; i < d.styleCount; ++i)
    {
        if (i) std::cout << ',';
        std::cout << d.styles[i];
    }
    std::cout << "],\"unbaked\":[";
    for (uint32 i = 0; i < d.unbakedLightCount; ++i)
    {
        if (i) std::cout << ',';
        std::cout << direct.unbakedLightIndices[i];
    }
    std::cout << "],\"pixels\":[";
    // Production fixture output is bounded; the tiny synthetic cases show every pixel.
    const uint32 count = MIN(d.radianceBytes / 8u, 4096u);
    for (uint32 i = 0; i < count; ++i)
    {
        if (i) std::cout << ',';
        std::cout << '[';
        for (uint32 c = 0; c < 4; ++c)
        {
            if (c) std::cout << ',';
            std::cout << Half(direct.pixels[i * 4u + c]);
        }
        std::cout << ']';
    }
    std::cout << "]}";
}
int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4) return 2;
    std::vector<uint32> fileBytes, manifestBytes;
    uint32 fileCount = 0, manifestCount = 0;
    if (!Load(argv[1], fileBytes, fileCount) || !Load(argv[2], manifestBytes, manifestCount)) return 3;
    hlight::FileView file;
    hlight::ManifestView manifest;
    memset(&file, 0xa5, sizeof(file));
    memset(&manifest, 0xa5, sizeof(manifest));
    char fileError[256] = {}, manifestError[256] = {}, pairError[256] = {};
    const bool fileValid = hlight::ValidateFile(fileBytes.data(), fileCount, file, fileError, sizeof(fileError));
    const uint32 flags = argc == 4 ? uint32(std::strtoul(argv[3], nullptr, 0)) :
        ShadowMap_LevelFlagDirect(0) | ShadowMap_LevelFlagDirect(1);
    const bool manifestValid = hlight::ValidateManifest(manifestBytes.data(), manifestCount,
        flags, manifest, manifestError, sizeof(manifestError));
    const bool pairValid = fileValid && manifestValid && hlight::ValidateManifestAsset(file, manifest, pairError, sizeof(pairError));
    std::cout << std::setprecision(9) << "{\"file_valid\":" << fileValid << ",\"manifest_valid\":" << manifestValid
              << ",\"pair_valid\":" << pairValid << ",\"file_cleared\":" << (!fileValid && hlight::ZeroBytes(&file, sizeof(file)))
              << ",\"manifest_cleared\":" << (!manifestValid && hlight::ZeroBytes(&manifest, sizeof(manifest)))
              << ",\"file_error\":" << std::quoted(fileError) << ",\"manifest_error\":" << std::quoted(manifestError)
              << ",\"pair_error\":" << std::quoted(pairError) << ",\"sets\":[";
    if (fileValid)
    {
        for (uint32 si = 0; si < file.header->visibilitySetCount; ++si)
        {
            if (si) std::cout << ',';
            const hlight::VisibilityView &view = file.visibility[si];
            std::cout << "{\"sun\":" << view.record->sunLightIndex << ",\"faces\":[";
            for (uint32 fi = 0; fi < view.record->faceCount; ++fi)
            {
                if (fi) std::cout << ',';
                const hlight::FaceVisibilityDisk &face = view.faces[fi];
                // Exercise canonical lookup and required-bit helpers, not only record traversal.
                for (uint32 li = 0; li < view.record->selectedLightCount; ++li)
                {
                    const hlight::VisibilityEntryDisk *entry = hlight::FindVisibilityEntry(view, face.firstEntry, face.entryCount, li);
                    if (bool(entry) != hlight::VisibilityRequired(view, fi, li)) return 4;
                }
                Entries(view, face.firstEntry, face.entryCount);
            }
            std::cout << "],\"unbaked_faces\":[";
            for (uint32 uf = 0; uf < view.record->unbakedFaceCount; ++uf)
            {
                if (uf) std::cout << ',';
                const hlight::UnbakedFaceDisk &face = view.unbakedFaces[uf];
                std::cout << "{\"face\":" << face.faceOrdinal << ",\"first\":" << face.firstLightIndex
                          << ",\"count\":" << face.lightCount << '}';
            }
            std::cout << "],\"unbaked_light_indices\":[";
            for (uint32 li = 0; li < view.record->unbakedLightIndexCount; ++li)
            {
                if (li) std::cout << ',';
                std::cout << view.unbakedLightIndices[li];
            }
            std::cout << "],\"props\":[";
            for (uint32 pi = 0; pi < view.record->propCount; ++pi)
            {
                if (pi) std::cout << ',';
                const hlight::PropVisibilityDisk &prop = view.props[pi];
                std::cout << "{\"ordinal\":" << prop.staticPropOrdinal << ",\"checksum\":" << prop.modelChecksum
                          << ",\"pose\":" << prop.poseIdentity << ",\"meshes\":[";
                for (uint32 mi = 0; mi < prop.meshCount; ++mi)
                {
                    if (mi) std::cout << ',';
                    const hlight::PropMeshVisibilityDisk &mesh = view.meshes[prop.firstMesh + mi];
                    std::cout << "{\"lod\":" << mesh.lod << ",\"vertices\":" << mesh.vertexCount
                              << ",\"vertex_crc\":" << mesh.vertexOrderCRC32 << ",\"entries\":";
                    Entries(view, mesh.firstEntry, mesh.entryCount);
                    std::cout << ",\"direct\":";
                    Direct(view, mesh);
                    std::cout << '}';
                }
                std::cout << "]}";
            }
            std::cout << "]}";
        }
    }
    std::cout << "]}" << std::endl;
    return 0;
}
