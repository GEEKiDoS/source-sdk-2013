//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef DIRECTOR_MUSIC_BANKS_H
#define DIRECTOR_MUSIC_BANKS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/mem_zero_on_new.h"
#include "tier1/utlvector.h"
#include "util_shared.h"
class KeyValues;

// Original CDirector-owned object (+1584), not a per-player music copy.
class DirectorMusicBanks : public CMemZeroOnNew
{
public:
	DirectorMusicBanks();
	void OnMapStart();
	void OnRoundStart( bool isRestartingScenario );
	void OnRoundEnd( bool saveRemainingTime );
	void Update();
private:
	struct Bank
	{
		Bank() : current( 0 ) {}
		~Bank() { Clear(); }
		CUtlVector<char *> events;
		int current;
		void Clear();
		void Load( const char *prefix, KeyValues *sounds );
		void ShuffleForNextCycle();
	};
	Bank m_atmosphere;
	Bank m_silence;
	Bank m_reveal;
	CountdownTimer m_bankTimer;
	float m_remainingTime;
};
#endif // DIRECTOR_MUSIC_BANKS_H
