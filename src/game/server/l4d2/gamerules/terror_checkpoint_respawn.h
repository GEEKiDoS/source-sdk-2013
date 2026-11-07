//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_CHECKPOINT_RESPAWN_H
#define TERROR_CHECKPOINT_RESPAWN_H
#ifdef _WIN32
#pragma once
#endif
class CTerrorCheckpoint;
// Original83B080 is a synchronous real Spawn override, not a remembered safe
// position. The rules spawn selector reads it while CheckpointRespawn calls Spawn.
CTerrorCheckpoint *GetTerrorCheckpointSpawnOverride();
void ResetCheckpointSpawnOverride();
#endif
