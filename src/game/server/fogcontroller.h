//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef FOGCONTROLLER_H
#define FOGCONTROLLER_H
#ifdef _WIN32
#pragma once
#endif

#include "playernet_vars.h"
#include "igamesystem.h"
#if defined( L4D2 )
#include "GameEventListener.h"
#endif

// Spawn Flags
#define SF_FOG_MASTER		0x0001

//=============================================================================
//
// Class Fog Controller:
// Compares a set of integer inputs to the one main input
// Outputs true if they are all equivalant, false otherwise
//
class CFogController : public CBaseEntity
{
public:
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	DECLARE_CLASS( CFogController, CBaseEntity );

	CFogController();
	~CFogController();

	// Parse data from a map file
	virtual void Activate();
	virtual int UpdateTransmitState();

	// Input handlers
	void InputSetStartDist(inputdata_t &data);
	void InputSetEndDist(inputdata_t &data);
	void InputTurnOn(inputdata_t &data);
	void InputTurnOff(inputdata_t &data);
	void InputSetColor(inputdata_t &data);
	void InputSetColorSecondary(inputdata_t &data);
	void InputSetFarZ( inputdata_t &data );
	void InputSetAngles( inputdata_t &inputdata );
#if !defined( L4D2 )
	void InputSetRadial( inputdata_t& inputdata );
#endif
	void InputSetMaxDensity( inputdata_t &inputdata );

	void InputSetColorLerpTo(inputdata_t &data);
	void InputSetColorSecondaryLerpTo(inputdata_t &data);
	void InputSetStartDistLerpTo(inputdata_t &data);
	void InputSetEndDistLerpTo(inputdata_t &data);
#if defined( L4D2 )
	void InputSetMaxDensityLerpTo( inputdata_t &data );
	void InputSet2DSkyboxFogFactor( inputdata_t &data );
	void InputSet2DSkyboxFogFactorLerpTo( inputdata_t &data );
#endif

	void InputStartFogTransition(inputdata_t &data);

	int DrawDebugTextOverlays(void);

	void SetLerpValues( void );
	void Spawn( void );

	bool IsMaster( void )					{ return HasSpawnFlags( SF_FOG_MASTER ); }

public:

	CNetworkVarEmbedded( fogparams_t, m_fog );
	bool					m_bUseAngles;
	int						m_iChangedVariables;
};

//=============================================================================
//
// Fog Controller System.
//
class CFogSystem : public CAutoGameSystem
#if defined( L4D2 )
	, public CGameEventListener
#endif
{
public:

	// Creation/Init.
	CFogSystem( char const *name ) : CAutoGameSystem( name ) 
	{
#if defined( L4D2 )
		m_hMasterController = NULL;
#else
		m_pMasterController = NULL;
#endif
	}

	~CFogSystem()
	{
#if defined( L4D2 )
		m_hMasterController = NULL;
#else
		m_pMasterController = NULL;
#endif
	}

	virtual void LevelInitPreEntity();
	virtual void LevelInitPostEntity();
#if defined( L4D2 )
	CFogController *GetMasterFogController( void ) { return m_hMasterController.Get(); }
	void FireGameEvent( IGameEvent *event ) OVERRIDE;
#else
	CFogController *GetMasterFogController( void )			{ return m_pMasterController; }
#endif

private:

#if defined( L4D2 )
	CHandle< CFogController > m_hMasterController;
	void FindMasterController();
#else
	CFogController	*m_pMasterController;
#endif
};

CFogSystem *FogSystem( void );

#endif // FOGCONTROLLER_H
