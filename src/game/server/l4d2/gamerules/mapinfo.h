//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_MAPINFO_H
#define L4D2_MAPINFO_H
#ifdef _WIN32
#pragma once
#endif

// info_map_parameters is the server-only item-density entity. It is not
// CWorld (which owns the networked music postfix) or mission/session metadata.
class CMapInfo : public CBaseEntity
{
public:
	DECLARE_CLASS( CMapInfo, CBaseEntity );
	DECLARE_DATADESC();
	CMapInfo();
	virtual ~CMapInfo();
	virtual void Spawn() OVERRIDE;
	virtual bool KeyValue( const char *key, const char *value ) OVERRIDE;
	virtual bool IsVersusMapInfo() const { return false; }
	enum Parameter
	{
		PAIN_PILL_DENSITY, MOLOTOV_DENSITY, AMMO_DENSITY, PIPE_BOMB_DENSITY,
		PISTOL_DENSITY, GAS_CAN_DENSITY, PROPANE_TANK_DENSITY, OXYGEN_TANK_DENSITY,
		MELEE_WEAPON_DENSITY, ADRENALINE_DENSITY, DEFIBRILLATOR_DENSITY,
		VOMITJAR_DENSITY, UPGRADEPACK_DENSITY, SUPER_WEAPON_DENSITY,
		CONFIGURABLE_WEAPON_DENSITY, CONFIGURABLE_WEAPON_CLUSTER_RANGE,
		MAGNUM_DENSITY, ITEM_CLUSTER_RANGE, PARAMETER_COUNT
	};
	float GetParameter( Parameter parameter ) const;
	int GetFinaleItemClusterCount() const;
	void InputUpdateCvars( inputdata_t &inputdata );
private:
	float m_parameters[PARAMETER_COUNT];
	int m_finaleItemClusterCount;
};

class CMapInfoVersus : public CMapInfo
{
public:
	DECLARE_CLASS( CMapInfoVersus, CMapInfo );
	virtual ~CMapInfoVersus();
	virtual void Spawn() OVERRIDE;
	virtual bool IsVersusMapInfo() const OVERRIDE { return true; }
};
CMapInfo *GetMapInfo();
#endif
