//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef HSPOT_BSP_H
#define HSPOT_BSP_H
#include "tier0/platform.h"
#pragma pack(push,4)
namespace hspot
{
static const uint32 kMagic = ('H') | ('S'<<8) | ('P'<<16) | ('T'<<24);
static const uint32 kVersion = 1;
enum BeamClass { kPointSpotlight = 1, kBeamSpotlight = 2 };
struct Header { uint32 magic, version, byteSize, lightCount, beamCount; };
struct Light { uint32 selectedOrdinal, volumetric; };
struct Beam { uint32 classname, pairedLight, hammerId; float origin[3]; };
}
#pragma pack(pop)
COMPILE_TIME_ASSERT(sizeof(hspot::Header)==20);
COMPILE_TIME_ASSERT(sizeof(hspot::Light)==8);
COMPILE_TIME_ASSERT(sizeof(hspot::Beam)==24);
#endif
