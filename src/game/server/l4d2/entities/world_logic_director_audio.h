#pragma once
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/sound/terror_music.h"
#include <xmmintrin.h>

class CAmbientMusic : public CPointEntity
{
public:
	DECLARE_CLASS( CAmbientMusic, CPointEntity );
	DECLARE_DATADESC();
	virtual void Precache() OVERRIDE; // Native251CC0 is genuinely empty.
	virtual void Spawn() OVERRIDE;
	void InputPlaySound( inputdata_t &inputdata );
	void InputStopSound( inputdata_t &inputdata );
private:
	void SendMusic( bool stop );
	string_t m_iszSound;
};

class CSoundMixLayer : public CBaseEntity
{
public:
	DECLARE_CLASS( CSoundMixLayer, CBaseEntity );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	CSoundMixLayer();
	virtual void Precache() OVERRIDE; // Native2CBEF0.
	virtual void Think() OVERRIDE; // Native2CBF30; not a missing effect provider.
	virtual int UpdateTransmitState() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	void InputSetLevel( inputdata_t &inputdata );
private:
	CNetworkVar( string_t, m_iszMixLayerName );
	CNetworkVar( float, m_fMixLayerLevel );
};

class CLogicDirectorQuery : public CPointEntity
{
public:
	DECLARE_CLASS( CLogicDirectorQuery, CPointEntity );
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
	void QueryThink();
	void InputHowAngry( inputdata_t &inputdata );
private:
	COutputInt m_OutAnger;
	COutputEvent m_On20SecondsToMob, m_On60SecondsToMob;
	int m_nMinRemapVal, m_nMaxRemapVal, m_nNoise;
	bool m_bWithin20, m_bWithin60; // Native1152/1153 are intentionally not saved.
};
