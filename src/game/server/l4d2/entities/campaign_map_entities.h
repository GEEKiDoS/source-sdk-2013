//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef CAMPAIGN_MAP_ENTITIES_H
#define CAMPAIGN_MAP_ENTITIES_H
#ifdef _WIN32
#pragma once
#endif
#include "baseentity.h"

// S29AEF0 / RTTI: CPointEntity. The ten pooled replacement parameters and
// signed BYTE rarity are native, including FIELD_INTEGER's one-byte metadata.
class CInfoItemPosition : public CPointEntity
{
public:
	DECLARE_CLASS( CInfoItemPosition, CPointEntity );
	DECLARE_DATADESC();
	DECLARE_ENT_SCRIPTDESC();
	CInfoItemPosition();
	void SetGroup( int group );
	int GetGroup() const;
	void SetRarity( int rarity );
	int GetRarity() const;
	const char *GetReplaceParm( int index ) const;
	void InputSetGroup( inputdata_t &data );
	void InputSetRarity( inputdata_t &data );
private:
	string_t m_iszReplaceParm[10];
	int m_group;
	signed char m_rarity;
};

// S29AAB0 / RTTI: CBaseEntity, NOT CPointEntity. No invented Spawn behavior.
class CL4D1SurvivorSpawn : public CBaseEntity
{
public:
	DECLARE_CLASS( CL4D1SurvivorSpawn, CBaseEntity );
	DECLARE_DATADESC();
	void InputSpawnSurvivor( inputdata_t &data );
private:
	int m_character;
};

// S40F800 / RTTI: both factories use this genuinely fieldless point class.
// Its full native primary table adds only inherited CPointEntity behavior;
// no private registry, datamap, tick, output, or map-start reset exists.
class CAmbientMobMarker : public CPointEntity
{
public:
	DECLARE_CLASS( CAmbientMobMarker, CPointEntity );
};

// S29BAC0 / RTTI: CPointEntity. Actual actor factories and the population KV
// stay with their existing owners, not a generic entity or alternate selector.
class CInfoZombieSpawn : public CPointEntity
{
public:
	DECLARE_CLASS( CInfoZombieSpawn, CPointEntity );
	DECLARE_DATADESC();
	CInfoZombieSpawn();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	void InputSpawnZombie( inputdata_t &data );
	void InputStartleZombie( inputdata_t &data );
private:
	string_t m_szPopulation;
	bool m_bOfferTank;
	EHANDLE m_hZombie;
	COutputEvent m_OnSpawn[9];
};
#endif // CAMPAIGN_MAP_ENTITIES_H
