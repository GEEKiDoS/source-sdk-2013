//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef ZOMBIE_BORDER_H
#define ZOMBIE_BORDER_H
#ifdef _WIN32
#pragma once
#endif

class CZombieBorder : public CBaseEntity
{
public:
	DECLARE_CLASS( CZombieBorder, CBaseEntity );
	DECLARE_DATADESC();
	CZombieBorder();
	virtual ~CZombieBorder();
	virtual void Spawn() OVERRIDE;
	void InputEnable( inputdata_t &inputdata );
	void InputDisable( inputdata_t &inputdata );
	static bool IsPointBeyondBorder( const Vector &position );
private:
	static CUtlVector< CZombieBorder * > s_borders;
	bool m_bDisabled;
	Vector m_normal;
};

#endif // ZOMBIE_BORDER_H
