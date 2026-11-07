//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef POINT_SCRIPT_USE_TARGET_H
#define POINT_SCRIPT_USE_TARGET_H
#ifdef _WIN32
#pragma once
#endif

#include "baseentity.h"
#include "entityoutput.h"
#include "vscript_shared.h"

class CTerrorPlayer;

class CPointScriptUseTarget : public CBaseEntity
{
public:
	DECLARE_CLASS( CPointScriptUseTarget, CBaseEntity );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	DECLARE_ENT_SCRIPTDESC();

	CPointScriptUseTarget();
	virtual ~CPointScriptUseTarget();
	virtual void Spawn() OVERRIDE;
	virtual void Activate() OVERRIDE;
	virtual void Think() OVERRIDE;
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value ) OVERRIDE;
	virtual void UpdateOnRemove() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;

	void InputSetUseModel( inputdata_t &inputdata );
	void ScriptSetUseString( const char *pszText );
	void ScriptSetUseSubString( const char *pszText );
	void ScriptSetUseTime( float flDuration );
	void ScriptSetPreviousProgress( float flProgress );
	float StopUse();
	const char *GetUseModelName() const;
	CBaseEntity *GetUseModel() const { return m_hUseModel.Get(); }
	void CanShowBuildPanel( bool bCanShow );

private:
	void ResolveUseModel();
	void RemoveAdrenalineSpeedup();
	bool CallUseScript( HSCRIPT hFunction, ScriptVariant_t *pResult = NULL, const float *pElapsed = NULL );

	CNetworkVar( string_t, m_sUseString );
	CNetworkVar( string_t, m_sUseSubString );
	CNetworkVar( float, m_flPreviousProgressPercent );
	CNetworkVar( bool, m_bCanShowBuildPanel );
	float m_flDuration;
	float m_flPreviousProgress;
	float m_flActiveDuration;
	string_t m_sUseModelName;
	EHANDLE m_hUseModel;
	bool m_bStartedWithAdrenaline;
	HSCRIPT m_hOnUseStart;
	HSCRIPT m_hOnUseStop;
	HSCRIPT m_hOnUseFinished;
	bool m_bCallingScript;
	CHandle< CTerrorPlayer > m_hPlayer;
	COutputEvent m_OnUseStarted;
	COutputEvent m_OnUseFinished;
	COutputEvent m_OnUseCanceled;
};

#endif // POINT_SCRIPT_USE_TARGET_H
