//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_SCRIPTED_CONTEXT_H
#define TERROR_SCRIPTED_CONTEXT_H
#ifdef _WIN32
#pragma once
#endif
#include "vscript/ivscript.h"
#include "util_shared.h"
#include "filesystem.h"

class KeyValues;
void TerrorShowViewPortPanel( int team, const char *name, bool show, KeyValues *data );
class CTerrorScriptedHUD
{
public:
	CTerrorScriptedHUD();
	void Reset();
	void Release();
	void Update();
	bool SetLayout( HSCRIPT table );
	void SetScope( int scope ) { m_nScope = scope; }
private:
	HSCRIPT m_hTable;
	HSCRIPT m_hUpdate;
	CountdownTimer m_UpdateTimer;
	int m_nNative20;
	int m_nScope;
};
class CTerrorScriptedJournal
{
public:
	CTerrorScriptedJournal();
	void Reset();
	void Toggle();
	void Update();
	void SetScope( int scope ) { m_nScope = scope; } // Native2798A0 ->journal owner+24 setter.
private:
	void WriteFrame();
	bool m_bEnabled;
	CountdownTimer m_UpdateTimer;
	FileHandle_t m_hFile;
	HSCRIPT m_hFunction;
	float m_flInterval;
	int m_nScope;
	int m_nNative32;
};
void RegisterTerrorScriptedHUDFunctions();
#endif
