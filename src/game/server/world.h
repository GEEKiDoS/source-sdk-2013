//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: The worldspawn entity. This spawns first when each level begins.
//
// $NoKeywords: $
//=============================================================================//

#ifndef WORLD_H
#define WORLD_H
#ifdef _WIN32
#pragma once
#endif


class CWorld : public CBaseEntity
{
public:
	DECLARE_CLASS( CWorld, CBaseEntity );

	CWorld();
	~CWorld();

	DECLARE_SERVERCLASS();

	virtual int RequiredEdictIndex( void ) { return 0; }   // the world always needs to be in slot 0
	
	static void RegisterSharedActivities( void );
	static void RegisterSharedEvents( void );
	virtual void Spawn( void );
	virtual void Precache( void );
	virtual bool KeyValue( const char *szKeyName, const char *szValue );
	virtual void DecalTrace( trace_t *pTrace, char const *decalName );
	virtual void VPhysicsCollision( int index, gamevcollisionevent_t *pEvent ) {}
	virtual void VPhysicsFriction( IPhysicsObject *pObject, float energy, int surfaceProps, int surfacePropsHit ) {}

	inline void GetWorldBounds( Vector &vecMins, Vector &vecMaxs )
	{
		VectorCopy( m_WorldMins, vecMins );
		VectorCopy( m_WorldMaxs, vecMaxs );
	}

	inline float GetWaveHeight() const
	{
		return (float)m_flWaveHeight;
	}

	bool GetDisplayTitle() const;
	bool GetStartDark() const;

	void SetDisplayTitle( bool display );
	void SetStartDark( bool startdark );
	void RefreshShadowMapTransmitReady();

	bool IsColdWorld( void );
#if defined( L4D2 )
	int GetTimeOfDay() const { return m_iTimeOfDay; }
	void SetTimeOfDay( int timeOfDay );
	int GetStartMusicType() const { return m_iStartMusicType; }
	const char *GetMusicPostfix() const { return STRING( m_iszMusicPostFix.Get() ); }
	void SetStartMusicType( int type ) { m_iStartMusicType = type; }
	void SetMusicPostfix( const char *postfix ) { m_iszMusicPostFix = AllocPooledString( postfix ); }
#endif

private:
	DECLARE_DATADESC();

	string_t m_iszChapterTitle;

	CNetworkVar( float, m_flWaveHeight );
	CNetworkVector( m_WorldMins );
	CNetworkVector( m_WorldMaxs );
	CNetworkVar( float, m_flMaxOccludeeArea );
	CNetworkVar( float, m_flMinOccluderArea );
	CNetworkVar( float, m_flMinPropScreenSpaceWidth );
	CNetworkVar( float, m_flMaxPropScreenSpaceWidth );
	CNetworkVar( string_t, m_iszDetailSpriteMaterial );
#if defined( L4D2 )
	CNetworkVar( int, m_iTimeOfDay ); // S1124, deliberately UNSENT in DT_World.
	CNetworkVar( int, m_iStartMusicType );
	CNetworkVar( string_t, m_iszMusicPostFix );
#endif

	// start flags
	CNetworkVar( bool, m_bStartDark );
	CNetworkVar( bool, m_bColdWorld );
	CNetworkVar( bool, m_bShadowMapTransmitReady );
	bool m_bDisplayTitle;
};


CWorld* GetWorldEntity();
#if defined( L4D2 )
bool IsMapTimeOfDay( int first, int last );
#endif
extern const char *GetDefaultLightstyleString( int styleIndex );


#endif // WORLD_H
