//========== Copyright � 2008, Valve Corporation, All rights reserved. ========
//
// Purpose:
//
//=============================================================================

#ifndef VSCRIPT_SERVER_H
#define VSCRIPT_SERVER_H

#include "vscript/ivscript.h"
#include "tier1/KeyValues.h"
#include "vscript_shared.h"
#include "tier1/utlsymbol.h"
#include "GameEventListener.h"

#if defined( _WIN32 )
#pragma once
#endif

class ISaveRestoreBlockHandler;

bool VScriptServerReplaceClosures( const char *pszScriptName, HSCRIPT hScope, bool bWarnMissing = false );
ISaveRestoreBlockHandler *GetVScriptSaveRestoreBlockHandler();
#if defined( L4D2 )
void ClearScriptGameEventListeners( void );
#endif


class CBaseEntityScriptInstanceHelper : public IScriptInstanceHelper
{
	bool ToString( void *p, char *pBuf, int bufSize );
	void *BindOnRead( HSCRIPT hInstance, void *pOld, const char *pszId );
};

extern CBaseEntityScriptInstanceHelper g_BaseEntityScriptInstanceHelper;

#if defined( TF_DLL ) || defined( L4D2 )
class CNavAreaScriptInstanceHelper : public IScriptInstanceHelper
{
	bool ToString( void *p, char *pBuf, int bufSize );
};

extern CNavAreaScriptInstanceHelper g_NavAreaScriptInstanceHelper;

#endif

#ifdef TF_DLL
class INextBotComponentScriptInstanceHelper : public IScriptInstanceHelper
{
	bool ToString( void *p, char *pBuf, int bufSize );
};

extern INextBotComponentScriptInstanceHelper g_NextBotComponentScriptInstanceHelper;
#endif

#if defined( L4D2 )
void VScriptReleaseNavScriptInstances();
#endif
// Only allow scripts to create entities during map initialization
bool IsEntityCreationAllowedInScripts( void );

#if !defined( L4D2 )
// ----------------------------------------------------------------------------
// KeyValues access
// ----------------------------------------------------------------------------
class CScriptKeyValues
{
public:
	CScriptKeyValues( KeyValues *pKeyValues = NULL );
	~CScriptKeyValues( );

	HSCRIPT ScriptFindKey( const char *pszName );
	HSCRIPT ScriptGetFirstSubKey( void );
	HSCRIPT ScriptGetNextKey( void );
	int ScriptGetKeyValueInt( const char *pszName );
	float ScriptGetKeyValueFloat( const char *pszName );
	const char *ScriptGetKeyValueString( const char *pszName );
	bool ScriptIsKeyValueEmpty( const char *pszName );
	bool ScriptGetKeyValueBool( const char *pszName );
	void ScriptReleaseKeyValues( );

	KeyValues *m_pKeyValues;	// actual KeyValue entity
};
#endif

class CVScriptGameEventListener : public CGameEventListener
{
public:
	virtual void FireGameEvent( IGameEvent *event );
#if !defined( L4D2 )
	bool FireScriptHook( const char *pszHookName, HSCRIPT params );
#endif
	
	void RunGameEventCallbacks( const char* szName, HSCRIPT params );
#if !defined( L4D2 )
	void RunScriptHookCallbacks( const char* szName, HSCRIPT params );
#endif

	void Init();
	void CollectGameEventCallbacksInScope( HSCRIPT scope );

#if !defined( L4D2 )
	void ListenForScriptHook( const char *szName );
	bool HasScriptHook( const char *szName );
	void ClearAllScriptHooks();
#endif

private:

#if !defined( L4D2 )
	CUtlSymbolTable m_ScriptHooks;
#endif

	HSCRIPT m_RunGameEventCallbacksFunc;
	HSCRIPT m_CollectGameEventCallbacksFunc;
#if !defined( L4D2 )
	HSCRIPT m_ScriptHookCallbacksFunc;
#endif
};

extern CVScriptGameEventListener g_VScriptGameEventListener;

#if !defined( L4D2 )
bool ScriptHooksEnabled( void );
bool ScriptHookEnabled( const char *pszName );
bool RunScriptHook( const char *pszHookName, HSCRIPT params );
#endif

#endif // VSCRIPT_SERVER_H
