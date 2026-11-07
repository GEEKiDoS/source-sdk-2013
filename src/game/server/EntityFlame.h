//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef ENTITYFLAME_H
#define ENTITYFLAME_H
#ifdef _WIN32
#pragma once
#endif

#define FLAME_DAMAGE_INTERVAL			0.2f // How often to deal damage.
#define FLAME_DIRECT_DAMAGE_PER_SEC		5.0f
#define FLAME_RADIUS_DAMAGE_PER_SEC		4.0f

#define FLAME_DIRECT_DAMAGE ( FLAME_DIRECT_DAMAGE_PER_SEC * FLAME_DAMAGE_INTERVAL )
#define FLAME_RADIUS_DAMAGE ( FLAME_RADIUS_DAMAGE_PER_SEC * FLAME_DAMAGE_INTERVAL )

#define FLAME_MAX_LIFETIME_ON_DEAD_NPCS	10.0f

class CEntityFlame : public CBaseEntity 
{
public:
	DECLARE_SERVERCLASS();
	DECLARE_CLASS( CEntityFlame, CBaseEntity );

	CEntityFlame( void );

	static CEntityFlame	*Create( CBaseEntity *pTarget, bool useHitboxes = true );
#if defined( L4D2 )
	static CEntityFlame *Create( CBaseEntity *pTarget, float flLifetime, float flSize, bool useHitboxes );
	virtual void Spawn() OVERRIDE;
	void SetAttacker( CBaseEntity *attacker ) { m_hAttacker = attacker; }
	CBaseEntity *GetAttacker() const { return m_hAttacker.Get(); }
	void SetDamageCustom( int damageCustom ) { m_nDamageCustom = damageCustom; }
	void SetCheapEffect( bool cheap ) { m_bCheapEffect = cheap; }
	bool IsCheapEffect() const { return m_bCheapEffect.Get(); }
#endif

	void	AttachToEntity( CBaseEntity *pTarget );
	void	SetLifetime( float lifetime );
	void	SetUseHitboxes( bool use );
	void	SetNumHitboxFires( int iNumHitBoxFires );
	void	SetHitboxFireScale( float flHitboxFireScale );

#if defined( L4D2 )
	double GetRemainingLife( void );
#else
	float	GetRemainingLife( void );
#endif
	int		GetNumHitboxFires( void );
	float	GetHitboxFireScale( void );

	virtual void Precache();
	virtual void UpdateOnRemove();

	void	SetSize( float size ) { m_flSize = size; }

	DECLARE_DATADESC();

protected:

#if !defined( L4D2 )
	void InputIgnite( inputdata_t &inputdata );
#endif

	void	FlameThink( void );

	CNetworkHandle( CBaseEntity, m_hEntAttached );		// The entity that we are burning (attached to).

	CNetworkVar( float, m_flSize );
	CNetworkVar( bool, m_bUseHitboxes );
	CNetworkVar( int, m_iNumHitboxFires );
	CNetworkVar( float, m_flHitboxFireScale );

	CNetworkVar( float, m_flLifetime );
#if defined( L4D2 )
	CNetworkVar( bool, m_bCheapEffect );
	string_t m_iszLoopingSound;
	EHANDLE m_hAttacker;
	int m_iDangerSound;
	float m_flDirectDamagePerSecond;
	int m_nDamageCustom;
#else
	bool	m_bPlayingSound;
#endif
};

#endif // ENTITYFLAME_H
