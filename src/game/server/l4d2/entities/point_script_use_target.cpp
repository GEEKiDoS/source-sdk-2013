//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "l4d2/entities/point_script_use_target.h"
#include "l4d2/player/terrorplayer.h"
#include "in_buttons.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( point_script_use_target, CPointScriptUseTarget );

IMPLEMENT_SERVERCLASS_ST( CPointScriptUseTarget, DT_PointScriptUseTarget )
	SendPropStringT( SENDINFO( m_sUseString ) ),
	SendPropStringT( SENDINFO( m_sUseSubString ) ),
	SendPropFloat( SENDINFO( m_flPreviousProgressPercent ), 0, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_bCanShowBuildPanel ) ),
END_SEND_TABLE()

BEGIN_DATADESC( CPointScriptUseTarget )
	DEFINE_KEYFIELD( m_sUseModelName, FIELD_STRING, "model" ),
	DEFINE_FIELD( m_flDuration, FIELD_FLOAT ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetUseModel", InputSetUseModel ),
	DEFINE_OUTPUT( m_OnUseStarted, "OnUseStarted" ),
	DEFINE_OUTPUT( m_OnUseFinished, "OnUseFinished" ),
	DEFINE_OUTPUT( m_OnUseCanceled, "OnUseCanceled" ),
	DEFINE_THINKFUNC( Think ),
END_DATADESC()

BEGIN_ENT_SCRIPTDESC( CPointScriptUseTarget, CBaseEntity, "Script driven player use functionality." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSetUseString, "SetProgressBarText", "Set the display text on the progress bar." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSetUseSubString, "SetProgressBarSubText", "Set the display subtext on the progress bar." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSetUseTime, "SetProgressBarFinishTime", "Set the time required to use this button. If '0', no progress bar will be displayed." )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSetPreviousProgress, "SetProgressBarCurrentProgress", "Set the ammount of time has been 'used' so far." )
	DEFINE_SCRIPTFUNC( StopUse, "Stop the current use action." )
	DEFINE_SCRIPTFUNC( GetUseModelName, "Get the name of the use model." )
	DEFINE_SCRIPTFUNC( CanShowBuildPanel, "Set whether the pop-up build panel should appear." )
END_SCRIPTDESC()

CPointScriptUseTarget::CPointScriptUseTarget()
{
	m_sUseString = NULL_STRING;
	m_sUseSubString = NULL_STRING;
	m_flPreviousProgressPercent = 0.0f;
	m_bCanShowBuildPanel = true;
	m_flDuration = 0.0f;
	m_flPreviousProgress = 0.0f;
	m_flActiveDuration = 0.0f;
	m_sUseModelName = NULL_STRING;
	m_hUseModel = NULL;
	m_bStartedWithAdrenaline = false;
	m_hOnUseStart = NULL;
	m_hOnUseStop = NULL;
	m_hOnUseFinished = NULL;
	m_bCallingScript = false;
	m_hPlayer = NULL;
}

CPointScriptUseTarget::~CPointScriptUseTarget()
{
	if ( g_pScriptVM )
	{
		if ( m_hOnUseStart )
			g_pScriptVM->ReleaseFunction( m_hOnUseStart );
		if ( m_hOnUseStop )
			g_pScriptVM->ReleaseFunction( m_hOnUseStop );
		if ( m_hOnUseFinished )
			g_pScriptVM->ReleaseFunction( m_hOnUseFinished );
	}
}

void CPointScriptUseTarget::Spawn()
{
	Precache();
	SetThink( &CPointScriptUseTarget::Think );
	SetNextThink( TICK_NEVER_THINK );
	BaseClass::Spawn();
}

int CPointScriptUseTarget::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_PVSCHECK );
}

// S:2BF160. Lookup is cached at Activate, not repeated when an action starts.
void CPointScriptUseTarget::Activate()
{
	if ( g_pScriptVM )
	{
		m_hOnUseStart = g_pScriptVM->LookupFunction( "OnUseStart", GetScriptScope() );
		m_hOnUseStop = g_pScriptVM->LookupFunction( "OnUseStop", GetScriptScope() );
		m_hOnUseFinished = g_pScriptVM->LookupFunction( "OnUseFinished", GetScriptScope() );
	}
	else
	{
		Warning( "point_script_use_target '%s' requires VScript; use is unavailable.\n", GetDebugName() );
	}
	ResolveUseModel();
	BaseClass::Activate();
}

void CPointScriptUseTarget::ResolveUseModel()
{
	m_hUseModel = gEntList.FindEntityByName( NULL, STRING( m_sUseModelName ) );
	if ( gEntList.FindEntityByName( m_hUseModel.Get(), STRING( m_sUseModelName ) ) )
	{
		Warning( "Level design error: Two entities with the name '%s' found. point_script_use_target named '%s' may not be finding the correct use entity.", STRING( m_sUseModelName ), GetDebugName() );
	}
	if ( m_hUseModel.Get() )
		m_hUseModel->m_hScriptUseTarget = this;
	else
		Warning( "Level design error: point_script_use_target '%s' has an invalid use model.\n", GetDebugName() );

	if ( g_pScriptVM )
		g_pScriptVM->SetValue( GetScriptScope(), "UseModelEntity", ScriptVariant_t( m_hUseModel.Get() ? m_hUseModel->GetScriptInstance() : NULL ) );
}

void CPointScriptUseTarget::InputSetUseModel( inputdata_t &inputdata )
{
	m_sUseModelName = inputdata.value.StringID();
	// Original does not detach a formerly named model here.
	ResolveUseModel();
}

void CPointScriptUseTarget::ScriptSetUseString( const char *pszText )
{
	m_sUseString = AllocPooledString( pszText );
}

void CPointScriptUseTarget::ScriptSetUseSubString( const char *pszText )
{
	m_sUseSubString = AllocPooledString( pszText );
}

void CPointScriptUseTarget::ScriptSetUseTime( float flDuration )
{
	m_flDuration = flDuration;
}

void CPointScriptUseTarget::ScriptSetPreviousProgress( float flProgress )
{
	m_flPreviousProgress = flProgress;
	if ( m_flDuration > 0.0f )
		m_flPreviousProgressPercent = flProgress / m_flDuration;
}

const char *CPointScriptUseTarget::GetUseModelName() const
{
	return STRING( m_sUseModelName );
}

void CPointScriptUseTarget::CanShowBuildPanel( bool bCanShow )
{
	m_bCanShowBuildPanel = bCanShow;
}

bool CPointScriptUseTarget::CallUseScript( HSCRIPT hFunction, ScriptVariant_t *pResult, const float *pElapsed )
{
	if ( !hFunction || m_bCallingScript )
		return false;
	Assert( g_pScriptVM );
	m_bCallingScript = true;
	if ( pElapsed )
	{
		ScriptVariant_t argument( *pElapsed );
		g_pScriptVM->ExecuteFunction( hFunction, &argument, 1, pResult, GetScriptScope(), true );
	}
	else
	{
		g_pScriptVM->ExecuteFunction( hFunction, NULL, 0, pResult, GetScriptScope(), true );
	}
	m_bCallingScript = false;
	return true;
}

// S:2BF1E0: no new action broker, team filter, distance check, or damage policy.
void CPointScriptUseTarget::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pActivator );
	if ( !pPlayer || m_hPlayer.Get() )
		return;
	if ( !g_pScriptVM )
	{
		Warning( "point_script_use_target '%s' requires VScript; rejecting use.\n", GetDebugName() );
		return;
	}

	m_hPlayer = pPlayer;
	g_pScriptVM->SetValue( GetScriptScope(), "PlayerUsingMe", ScriptVariant_t( pPlayer->GetScriptInstance() ) );
	ScriptVariant_t result;
	if ( CallUseScript( m_hOnUseStart, &result ) && !(bool)result )
	{
		// Deliberately leave PlayerUsingMe unchanged on a script veto.
		m_hPlayer = NULL;
		g_pScriptVM->ReleaseValue( result );
		return;
	}

	m_OnUseStarted.FireOutput( pPlayer, this );
	SetNextThink( gpGlobals->curtime );
	m_flActiveDuration = m_flDuration;
	float flPreviousProgress = m_flPreviousProgress;
	m_bStartedWithAdrenaline = m_hPlayer->m_bAdrenalineActive.Get();
	if ( m_bStartedWithAdrenaline )
	{
		m_flActiveDuration *= 0.5f;
		flPreviousProgress *= 0.5f;
	}
	pPlayer->SetUseAction( 11, m_hUseModel.Get(), m_flActiveDuration, m_flActiveDuration > 0.0f );
	pPlayer->m_flProgressBarStartTime = pPlayer->m_flProgressBarStartTime.Get() - flPreviousProgress;
	g_pScriptVM->ReleaseValue( result );
}

void CPointScriptUseTarget::RemoveAdrenalineSpeedup()
{
	m_bStartedWithAdrenaline = false;
	CTerrorPlayer *pPlayer = m_hPlayer.Get();
	if ( pPlayer )
	{
		m_flActiveDuration = m_flDuration;
		m_flPreviousProgress = 2.0f * ( gpGlobals->curtime - pPlayer->m_flProgressBarStartTime.Get() );
		pPlayer->UpdateUseActionProgress( m_flPreviousProgress, m_flActiveDuration );
	}
}

// S:2BED60. StopUse alone does not fire either finish or cancel output.
float CPointScriptUseTarget::StopUse()
{
	CTerrorPlayer *pPlayer = m_hPlayer.Get();
	if ( !pPlayer )
		return 0.0f;

	float flElapsed = gpGlobals->curtime - pPlayer->m_flProgressBarStartTime.Get();
	if ( pPlayer->m_bAdrenalineActive.Get() )
		flElapsed *= 2.0f;
	CallUseScript( m_hOnUseStop, NULL, &flElapsed );
	pPlayer->StopUseAction();
	if ( pPlayer->GetUseEntity() )
		pPlayer->SetUseEntity( NULL );
	pPlayer->OnUseEntityChanged();
	m_hPlayer = NULL;
	if ( g_pScriptVM )
		g_pScriptVM->SetValue( GetScriptScope(), "PlayerUsingMe", ScriptVariant_t( 0 ) );
	SetNextThink( TICK_NEVER_THINK );
	return flElapsed;
}

void CPointScriptUseTarget::Think()
{
	CTerrorPlayer *pPlayer = m_hPlayer.Get();
	if ( !pPlayer )
		return;
	if ( m_bStartedWithAdrenaline && !pPlayer->m_bAdrenalineActive.Get() )
		RemoveAdrenalineSpeedup();

	const float flElapsed = gpGlobals->curtime - pPlayer->m_flProgressBarStartTime.Get();
	if ( pPlayer->m_lifeState != LIFE_DEAD && !pPlayer->IsIncapacitated()
		&& ( pPlayer->m_nButtons & IN_USE ) && pPlayer->m_iCurrentUseAction == 11
		&& pPlayer->m_useActionTarget.Get() == m_hUseModel.Get() )
	{
		// S:2BF49C takes JB for both below and unordered comparisons.
		if ( !( flElapsed >= m_flActiveDuration ) )
		{
			SetNextThink( gpGlobals->curtime + 0.033f );
			return;
		}
		StopUse();
		m_OnUseFinished.FireOutput( pPlayer, this );
		CallUseScript( m_hOnUseFinished );
	}
	else
	{
		StopUse();
		m_OnUseCanceled.FireOutput( pPlayer, this );
	}
}

void CPointScriptUseTarget::UpdateOnRemove()
{
	if ( m_hUseModel.Get() && m_hUseModel->m_hScriptUseTarget.Get() )
		m_hUseModel->m_hScriptUseTarget = NULL;
	StopUse();
	BaseClass::UpdateOnRemove();
}
