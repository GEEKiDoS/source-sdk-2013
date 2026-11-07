//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BOSS_ZOMBIE_CONTACT_H
#define L4D2_BOSS_ZOMBIE_CONTACT_H
#ifdef _WIN32
#pragma once
#endif
class BossZombiePlayerBot;
class CBaseEntity;
class CGameTrace;
// Same concrete root contact body in Boomer42FC60, Spitter467C60,
// Smoker45CBD0 and Charger43B740. Each root retains its own stuck timestamp.
void BossZombieContact( BossZombiePlayerBot *me, CBaseEntity *other, CGameTrace *trace, float lastStuckTime, bool replyForActor );
bool IsLowHealthBossBreakable( CBaseEntity *entity ); // S351C10
#endif
