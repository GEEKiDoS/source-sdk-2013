//========= Copyright Valve Corporation, All rights reserved. ============//
// maps/<map>.hspot: optional stored member of the .light pak. vrad_restir pairs fake spotlight beams
// (point_spotlight / beam_spotlight) with the selected light_spot whose traced cone lights their volume;
// the DX12 client replaces exactly these beams with the light's volumetrics while the beam is live at its
// authored pose. A .light without the member keeps every stock beam.
#ifndef HSPOT_BSP_H
#define HSPOT_BSP_H
#include "tier0/platform.h"
#include <string.h>
#pragma pack(push,4)
namespace hspot
{
static const uint32 kMagic = ('H') | ('S'<<8) | ('P'<<16) | ('T'<<24);
static const uint32 kVersion = 1;
enum BeamClass { kPointSpotlight = 1, kBeamSpotlight = 2 };
struct Header { uint32 magic, version, byteSize, lightCount, beamCount; };	// lightCount: manifest selected lights
struct Beam { uint32 classname, light; float origin[3], axis[3]; };			// light: manifest selected-light index

// Validates the member against the manifest's selected-light count; beams points into bytes.
inline bool Validate(const void *bytes, uint32 count, uint32 lightCount, const Beam *&beams, uint32 &beamCount)
{
    beams = NULL; beamCount = 0;
    if (!bytes || count < sizeof(Header) || (reinterpret_cast<uintp>(bytes) & 3)) return false;
    Header h; memcpy(&h, bytes, sizeof(h));
    if (h.magic != kMagic || h.version != kVersion || h.byteSize != count || h.lightCount != lightCount ||
        h.beamCount > (count - sizeof(Header)) / sizeof(Beam) || sizeof(Header) + h.beamCount * sizeof(Beam) != count) return false;
    const Beam *records = reinterpret_cast<const Beam *>(static_cast<const uint8 *>(bytes) + sizeof(Header));
    for (uint32 i = 0; i < h.beamCount; ++i)
    {
        const Beam &b = records[i];
        if ((b.classname != kPointSpotlight && b.classname != kBeamSpotlight) || b.light >= lightCount) return false;
        for (int k = 0; k < 3; ++k)
            if (!(b.origin[k] == b.origin[k]) || !(b.axis[k] == b.axis[k])) return false;
    }
    beams = records; beamCount = h.beamCount;
    return true;
}
}
#pragma pack(pop)
COMPILE_TIME_ASSERT(sizeof(hspot::Header)==20);
COMPILE_TIME_ASSERT(sizeof(hspot::Beam)==32);
#endif
