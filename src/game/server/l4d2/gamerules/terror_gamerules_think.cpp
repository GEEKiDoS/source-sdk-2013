//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_mode.h"
#include "tier0/memdbgon.h"

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CTerrorGameRules::Think()
{
	// Native2E0F20. Keep the real existing892 queue BEFORE CS23F140; invalid
	// handles are removed, valid handles receive EF_NOINTERP before expiry.
	for ( int i = m_SpawnedPlayers.Count() - 1; i >= 0; --i )
	{
		const SpawnedPlayer_t &entry = m_SpawnedPlayers[i];
		const bool expired = gpGlobals->curtime > entry.m_flSpawnTime + 0.25f;
		CBaseEntity *player = entry.m_hPlayer.Get();
		if ( player )
		{
			player->AddEffects( EF_NOINTERP );
			if ( !expired ) continue; // Original retains on unordered time, not <= comparison.
		}
		m_SpawnedPlayers.Remove( i ); // Ordered Remove, not FastRemove or Purge.
	}
	BaseClass::Think();
	UpdatePlayerPhysicsCrush(); // Native2DEA20 ->2DD270, real trace/prop damage/depenetration.
	UpdatePlayerPushPhysics(); // Native2E1032..1757, SAME actor13248 push handle owner.
	UpdateDirectorRoundState(); // Native1172/1173/1174/1175, after actual player loop.
	if ( gpGlobals->curtime > m_flNextVersusScoreUpdate && g_TerrorModes.IsBaseMode( "versus" ) )
	{
		// Native COMISS/JA skips ONLY ordered-positive Director624; NaN is
		// admitted. This is not its game-state or transition1254 predicate.
		if ( !( TheDirector->GetMissionEndDeadline() > 0.0f ) ) CacheVersusScores( false );
		m_flNextVersusScoreUpdate = gpGlobals->curtime + 1.0f;
	}
	UpdateInfectedBotPathFailures(); // Existing sole M5 rules source85CF, actual native2E18F5..1B84.
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CTerrorGameRules::AddRecentlySpawnedPlayer( CBaseEntity *entity )
{
	// Native2E0D20: SAME existing892 queue, including NULL/duplicates.
	// GetPlayerSpawnSpot and real ZombieManager relocation both use it.
	SpawnedPlayer_t entry;
	entry.m_hPlayer = entity;
	entry.m_flSpawnTime = gpGlobals->curtime;
	m_SpawnedPlayers.AddToTail( entry );
}
