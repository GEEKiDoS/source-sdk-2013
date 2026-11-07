//========= Copyright Valve Corporation, All rights reserved. ============//

#ifndef SURVIVOR_POSITION_H
#define SURVIVOR_POSITION_H
#ifdef _WIN32
#pragma once
#endif

#include "baseentity.h"

class CTerrorPlayer;

class CSurvivorPosition : public CBaseEntity
{
public:
	DECLARE_CLASS( CSurvivorPosition, CBaseEntity );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CSurvivorPosition();

	int GetOrder() const;
	const char *GetSurvivorName() const;
	const char *GetGameMode() const;
	const char *GetSurvivorIntroSequence() const;
	const char *GetSurvivorConcept() const;
	bool ShouldHideWeapons() const { return m_bHideWeapons; }
	void SetAttachedSurvivor( CTerrorPlayer *player );
	void InputSetViewControl( inputdata_t &inputdata );

private:
	CNetworkVar( int, m_order );
	CHandle< CTerrorPlayer > m_attachedSurvivor;
	string_t m_iszSurvivorName;
	string_t m_iszSurvivorIntroSequence;
	string_t m_iszGameMode;
	string_t m_iszSurvivorConcept;
	bool m_bHideWeapons;
};

#endif // SURVIVOR_POSITION_H
