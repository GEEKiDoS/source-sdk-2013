//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/zombie_border.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_zombie_border, CZombieBorder );
CUtlVector< CZombieBorder * > CZombieBorder::s_borders;
BEGIN_DATADESC( CZombieBorder )
	DEFINE_KEYFIELD( m_bDisabled, FIELD_BOOLEAN, "StartDisabled" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
END_DATADESC()

CZombieBorder::CZombieBorder() : CBaseEntity( true ), m_bDisabled( false )
{
	m_normal.Init();
	s_borders.AddToTail( this );
}

CZombieBorder::~CZombieBorder()
{
	// S:0x48BED0 removes in order, not FastRemove.
	s_borders.FindAndRemove( this );
}

void CZombieBorder::Spawn()
{
	// S:0x48B0C0 caches the forward vector from actual absolute angles once.
	BaseClass::Spawn();
	AngleVectors( GetAbsAngles(), &m_normal );
}

void CZombieBorder::InputEnable( inputdata_t &inputdata )
{
	m_bDisabled = false;
}

void CZombieBorder::InputDisable( inputdata_t &inputdata )
{
	m_bDisabled = true;
}

bool CZombieBorder::IsPointBeyondBorder( const Vector &position )
{
	// S:0x48B4C0. All enabled borders are infinite planes; the negative side
	// is excluded, equality is not. No PVS/radius/checkpoint substitute.
	FOR_EACH_VEC( s_borders, i )
	{
		CZombieBorder *border = s_borders[i];
		if ( border->m_bDisabled ) continue;
		Vector offset = position - border->GetAbsOrigin();
		float distance = ( border->m_normal.y * offset.y + border->m_normal.x * offset.x ) + border->m_normal.z * offset.z;
		if ( distance < 0.0f ) return true;
	}
	return false;
}
