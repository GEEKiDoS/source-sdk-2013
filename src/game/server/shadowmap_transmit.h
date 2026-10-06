//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Server-side caster transmission for selected runtime lights:
//          once per tick, conservative spatial eligibility of the complete
//          public entity population against every selected local influence
//          volume (union of both admitted modes) and the map-wide selected-sun
//          domain; per-recipient overrides in CheckTransmit after FULLCHECK /
//          ShouldTransmit rejection and before PVS rejection, with the full
//          network-parent chain validated against DONTSEND / recipient privacy.
//
//          Implementation: game/server/shadowmap_transmit.cpp + gameinterface /
//          baseentity / world integration (ServerTransmission). Works on a
//          dedicated server (no client DLL or engine hooks): the manifest v4
//          'rshd' and its named face/lighting/worldlight CRC sources are read
//          from maps/<mapname>.bsp using public/hlight_bsp.h and streaming BSP
//          CRC helpers. No high-resolution pixel asset or GPU resource is loaded.
//
//=============================================================================//
#ifndef SHADOWMAP_TRANSMIT_H
#define SHADOWMAP_TRANSMIT_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"

class CBaseEntity;
struct edict_t;
class CCheckTransmitInfo;

// CServerGameDLL::LevelInit: reads and validates manifest v4 with hlight::ValidateManifest, checks exact named
// BSP face/lighting versions and all three source sizes, ranges and CRCs, then builds unioned selected-light volumes.
// Ordinary maps: no-op, Ready() false. Legacy v3, corrupt metadata and orphan feature flags: Ready() false
// (clients reject with SHADOWMAP_ERR_TRANSMIT_UNAVAILABLE); never fall back to inverse receiver parsing.
// Valid RGB-only feature modes with no selected lights advertise readiness without any caster volumes.
void ShadowMapTransmit_LevelInit();
void ShadowMapTransmit_LevelShutdown();
// True after metadata and enumeration are ready; mirrored into CWorld::m_bShadowMapTransmitReady.
bool ShadowMapTransmit_Ready();
// Whether the loaded map has high-resolution feature metadata or ownership flags, including rejected candidates.
bool ShadowMapTransmit_FeatureMap();

// Once per server tick (before the first CheckTransmit of the tick): rebuilds spatial eligibility over the
// complete entity population (not only the engine callback's index list) using current world AABBs.
void ShadowMapTransmit_BeginTick();
// Cached per-tick spatial eligibility (recipient-independent): entity overlaps a selected volume / sun domain.
bool ShadowMapTransmit_IsSpatiallyEligible( const CBaseEntity *pEntity );

// CServerGameEnts::CheckTransmit integration: after semantic rejection, before PVS rejection. For every
// spatially eligible entity not yet set for `pInfo`, validates the whole network-parent chain for this recipient
// (DONTSEND / recipient-private ancestors exclude the caster) and calls SetTransmit(pInfo, true).
void ShadowMapTransmit_CheckTransmit( CCheckTransmitInfo *pInfo, const unsigned short *pEdictIndices, int nEdicts );

#endif // SHADOWMAP_TRANSMIT_H
