#pragma once
#include "cbase.h"
#include "entitylist.h"
#include "eventqueue.h"
#include "filters.h"
#include "igameevents.h"
#include "tier0/icommandline.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/entities/visibility_monitor.h"

class CInfoGameMode : public CBaseEntity
{
public:
	DECLARE_CLASS( CInfoGameMode, CBaseEntity );
	DECLARE_DATADESC();
	virtual void Activate() OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;
	void InputPostSpawnActivate( inputdata_t &inputdata );
	void InputPreRoundActivate( inputdata_t &inputdata );
	void InputFireScavengeMatchStart( inputdata_t &inputdata );
private:
	COutputEvent m_OnCoop, m_OnVersus, m_OnSurvival, m_OnScavenge;
	COutputEvent m_OnCoopPreRound, m_OnVersusPreRound, m_OnSurvivalPreRound, m_OnScavengePreRound;
	COutputEvent m_OnScavengeMatchStart, m_OnNavAnalyze;
};

class CVersusRandom : public CLogicalEntity
{
public:
	DECLARE_CLASS( CVersusRandom, CLogicalEntity );
	DECLARE_DATADESC();
	CVersusRandom();
	virtual void Spawn() OVERRIDE;
	void InputPickRandom( inputdata_t &inputdata );
private:
	void PickRandom( bool record, CBaseEntity *activator );
	COutputEvent m_OnRandom[16];
	int m_nOutput[16];
	int m_nOutputCount;
	CUtlVector< int > m_History;
	int m_nReplayIndex;
};

class CLogicGameEvent : public CLogicalEntity
{
public:
	DECLARE_CLASS( CLogicGameEvent, CLogicalEntity );
	DECLARE_DATADESC();
	void InputFireEvent( inputdata_t &inputdata );
private:
	string_t m_iszEventName;
};

class CInfoGameEventProxy : public CPointEntity
{
public:
	DECLARE_CLASS( CInfoGameEventProxy, CPointEntity );
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;
	void InputGenerateGameEvent( inputdata_t &inputdata );
private:
	static bool OnVisible( CBaseEntity *entity, CBasePlayer *observer );
	string_t m_iszEventName;
	float m_flRange;
};

class CPointEntityFinder : public CBaseEntity
{
public:
	DECLARE_CLASS( CPointEntityFinder, CBaseEntity );
	DECLARE_DATADESC();
	virtual void Activate() OVERRIDE;
	void InputFindEntity( inputdata_t &inputdata );
private:
	void FindEntity();
	void FindByDistance();
	void FindRandom();
	EHANDLE m_hFoundEntity; // Native1068: deliberately absent from the datamap.
	string_t m_iFilterName;
	CHandle< CBaseFilter > m_hFilter;
	string_t m_iRefName;
	EHANDLE m_hReference;
	int m_FindMethod;
	COutputEvent m_OnFoundEntity;
};

class CPointBroadcastClientCommand : public CPointEntity
{
public:
	DECLARE_CLASS( CPointBroadcastClientCommand, CPointEntity );
	DECLARE_DATADESC();
	void InputCommand( inputdata_t &inputdata );
};
