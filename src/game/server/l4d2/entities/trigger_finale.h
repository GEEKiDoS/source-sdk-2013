//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TRIGGER_FINALE_H
#define TRIGGER_FINALE_H
#ifdef _WIN32
#pragma once
#endif
#include "props.h"
class CTerrorPlayer;

// Despite its map classname, the retail finale owner is a CBreakableProp,
// not a CBaseTrigger. Both original factory names bind this same class.
class CFinaleTrigger : public CBreakableProp
{
public:
	DECLARE_CLASS( CFinaleTrigger, CBreakableProp );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	CFinaleTrigger();
	virtual void Spawn() OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;
	virtual void Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE useType, float value ) OVERRIDE;
	bool IsEscapeStarted() const { return m_escapeStarted; }
	bool IsSacrificeFinale() const { return m_bIsSacrificeFinale; }
	float GetVersusTravelCompletion() const { return m_flVersusTravelCompletion; }
	int GetFinaleType() const { return m_type; }
	void InputFinaleEscapeStarted( inputdata_t &data );
	void InputFinaleEscapeVehicleReadyForSurvivors( inputdata_t &data );
	void InputFinaleEscapeForceSurvivorPositions( inputdata_t &data );
	void InputFinaleEscapeFinished( inputdata_t &data );
	void InputFinalePause( inputdata_t &data );
	void InputFinaleWon( inputdata_t &data );
	void InputFinaleLost( inputdata_t &data );
	void InputForceFinaleStart( inputdata_t &data );
	void InputEnable( inputdata_t &data );
	void InputDisable( inputdata_t &data );
	void InputForceTankSpawn( inputdata_t &data );
	void InputGauntletStopPanic( inputdata_t &data );
	void InputAdvanceFinaleState( inputdata_t &data );
	void InputSacrificeEscapeFailed( inputdata_t &data );
	void InputSacrificeEscapeSucceeded( inputdata_t &data );
	void InputSacrificePlayerBeginsRun( inputdata_t &data );
private:
	void FirstUseDelayThink();
	void DelayedFinaleStartThink();
	void DelayedSurvivalStartThink();
	void StartFinale( CTerrorPlayer *activator );
	void StartSurvival( CTerrorPlayer *activator );
	COutputEvent m_OnFinaleEscapeStarted;
	COutputEvent m_OnFinaleWon;
	COutputEvent m_OnFinaleLost;
	COutputEvent m_OnFirstUseStart;
	COutputEvent m_OnUseStart;
	COutputEvent m_OnFinaleStart;
	COutputEvent m_OnFinalePause;
	COutputEvent m_OnEscapeVehicleLeaving;
	bool m_escapeStarted;
	string_t m_scriptfile;
	float m_firstUseDelay;
	float m_useDelay;
	CNetworkVar( bool, m_bDisabled );
	CNetworkVar( int, m_triggerState );
	int m_type;
	float m_flVersusTravelCompletion;
	bool m_bIsSacrificeFinale;
	CHandle< CTerrorPlayer > m_hActivator;
};

// S:25BB10/25959E0: alive survivor slots, with the original optional incap
// exclusion, and their actual nav FINALE bit. No entity-local "exit" latch.
bool AreAllSurvivorsInFinaleArea( bool includeIncapacitated );
#endif // TRIGGER_FINALE_H
