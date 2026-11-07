#pragma once
#include "cbase.h"
#include "basecombatcharacter.h"
#include "basecombatweapon.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/boss/witch.h"
#include "l4d2/director/infected_population.h"
#include "l4d2/director/zombiemanager.h"
#include "utldict.h"
#include "tier1/KeyValues.h"
#include "datacache/imdlcache.h"
#include <float.h>
#include <math.h>
#include <stdio.h>

class CCommentaryDummy : public CBaseCombatCharacter
{
public:
    DECLARE_CLASS( CCommentaryDummy, CBaseCombatCharacter );
    DECLARE_DATADESC();
    virtual void Spawn() OVERRIDE;
    virtual void OnSequenceChanged( int oldSequence ) OVERRIDE;
	virtual void HandleAnimEvent( animevent_t *event ) OVERRIDE; // S381520 proven inert.
    void InputSetAnimation( inputdata_t &data );
private:
    void DummyThink();
    Vector FindLookTarget();
    void UpdateHead( const Vector &target );
    int m_iEyeHeight;
    float m_flHeadYaw;
    float m_flHeadPitch;
    float m_flLastHeadUpdate;
    int m_headYawPoseParameter;
    int m_headPitchPoseParameter;
    string_t m_iszHeadYawPoseParam;
    string_t m_iszHeadPitchPoseParam;
    string_t m_iszStartingAnim;
    string_t m_iszStartingWeapons;
    bool m_bLookAtPlayers;
};

class CCommentaryZombieSpawner : public CPointEntity
{
public:
    DECLARE_CLASS( CCommentaryZombieSpawner, CPointEntity );
    DECLARE_DATADESC();
    virtual void DeathNotice( CBaseEntity *victim ) OVERRIDE;
    void InputSpawnZombie( inputdata_t &data );
private:
    COutputEvent m_OnSpawnedZombieDeath;
};

class CInfectedZooPuppet : public Infected
{
public:
    DECLARE_CLASS( CInfectedZooPuppet, Infected );
    virtual void Spawn() OVERRIDE;
	virtual void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE; // S299420.
	virtual void Touch( CBaseEntity *entity ) OVERRIDE; // S299450.
	virtual void HandleAnimEvent( animevent_t *event ) OVERRIDE; // S299430.
	virtual void Ignite( float lifetime, bool npcOnly = true, float size = 0.0f, bool designer = false ) OVERRIDE; // S299460.
	virtual int OnTakeDamage_Alive( const CTakeDamageInfo &info ) OVERRIDE; // S299400.
	virtual int OnTakeDamage_Dying( const CTakeDamageInfo &info ) OVERRIDE; // S299410.
	virtual void OnNavAreaChanged( CNavArea *entered, CNavArea *left ) OVERRIDE; // S299440.
	virtual void Ignite( float lifetime, CBaseEntity *attacker ) OVERRIDE; // S299470.
};

class CInfectedZooMaker : public CPointEntity
{
public:
    DECLARE_CLASS( CInfectedZooMaker, CPointEntity );
    DECLARE_DATADESC();
	CInfectedZooMaker();
    virtual void Spawn() OVERRIDE;
    virtual void Precache() OVERRIDE;
    void Input_SpawnAll( inputdata_t &data );
private:
    void SpawnAllThink();
    int SpawnModel( const char *name, int *row );
    CUtlDict< int, unsigned short > m_models;
};
