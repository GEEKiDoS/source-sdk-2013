//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/vscript/terror_scripted_context.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "vscript_server.h"
#include "tier0/memdbgon.h"

void CTerrorGameRules::SetScriptedModeActive( bool active ) { m_bChallengeModeActive = active; }
void CTerrorGameRules::ResetScriptedHUD()
{
	m_flHoldoutCooldownEndTime = 0.0f;
	for ( int i = 0; i < 15; ++i )
	{
		m_iScriptedHUDInts.Set( i, 0 ); m_fScriptedHUDFloats.Set( i, 0.0f );
		m_iScriptedHUDFlags.Set( i, 0 );
		m_fScriptedHUDPosX.Set( i, 0.0f ); m_fScriptedHUDPosY.Set( i, 0.0f );
		m_fScriptedHUDWidth.Set( i, 0.0f ); m_fScriptedHUDHeight.Set( i, 0.0f );
		SetScriptedHUDString( i, "" );
	}
	for ( int i = 0; i < 4; ++i )
	{
		m_iScriptedHUDTimerMode.Set( i, 0 );
		m_fScriptedHUDTimerBase.Set( i, 0.0f );
		m_fScriptedHUDTimerAdd.Set( i, 0.0f );
	}
}
void CTerrorGameRules::SetScriptedHUDString( int slot, const char *text )
{
	char value[128];
	Q_strncpy( value, text, sizeof( value ) );
	// Native2C5880 compares/copies the whole fixed128-byte cell, not a new
	// separately allocated string. Retain its native trailing-byte behavior.
	if ( Q_memcmp( m_szScriptedHUDStringSet[slot], value, sizeof( value ) ) )
	{
		NetworkStateChanged();
		Q_memcpy( m_szScriptedHUDStringSet[slot], value, sizeof( value ) );
	}
}
void CTerrorGameRules::SetScriptedHUDLayout( int slot, float x, float y, float width, float height )
{
	m_fScriptedHUDPosX.Set( slot, x ); m_fScriptedHUDPosY.Set( slot, y );
	m_fScriptedHUDWidth.Set( slot, width ); m_fScriptedHUDHeight.Set( slot, height );
}
void CTerrorGameRules::SetScriptedHUDTimerMode( int timer, int mode )
{
	if ( mode > 0 )
	{
		if ( mode <= 2 ) m_fScriptedHUDTimerBase.Set( timer, gpGlobals->curtime );
		else if ( mode == 3 )
		{
			float value = ReadScriptedHUDTimer( timer );
			m_fScriptedHUDTimerAdd.Set( timer, value < 0.0f ? 0.0f : value );
		}
	}
	m_iScriptedHUDTimerMode.Set( timer, mode );
}
void CTerrorGameRules::ManageScriptedHUDTimer( int timer, int command, float value )
{
	switch ( command )
	{
	case 0: m_fScriptedHUDTimerAdd.Set( timer, 0.0f ); SetScriptedHUDTimerMode( timer, 0 ); break;
	case 1: case 2:
		if ( value != 0.0f ) m_fScriptedHUDTimerAdd.Set( timer, value );
		SetScriptedHUDTimerMode( timer, command ); break;
	case 3: SetScriptedHUDTimerMode( timer, command ); break;
	default: m_fScriptedHUDTimerAdd.Set( timer, value ); break;
	}
}
static bool HUDInteger( const ScriptVariant_t &value, const char *name )
{
	if ( value.GetType() == FIELD_INTEGER ) return true;
	Warning( "Wrong field type (%s) for <%s> in HUD Table", VariantFieldTypeName( value.GetType() ), name );
	return false;
}
static int HUDRawInteger( const ScriptVariant_t &value )
{
	if ( value.GetType() == FIELD_INTEGER ) return (int)value;
	float raw;
	Q_memcpy( &raw, &value, sizeof( raw ) );
	return (int)raw;
}
int CTerrorGameRules::UpdateScriptedHUDField( HSCRIPT table )
{
	ScriptVariant_t value;
	if ( !g_pScriptVM->GetValue( table, "slot", &value ) || !HUDInteger( value, "slot" ) || (int)value == 15 )
	{
		DevWarning( "Cant parse a HUD Element without a Slot\n" ); value.Free(); return -1;
	}
	int slot = (int)value;
	value.Free();
	unsigned int flags = 0;
	int errors = 0;
	if ( g_pScriptVM->GetValue( table, "flags", &value ) )
	{
		if ( HUDInteger( value, "flags" ) ) flags = (int)value;
		else ++errors;
	}
	value.Free();
	if ( g_pScriptVM->GetValue( table, "dataval", &value ) )
	{
		switch ( value.GetType() )
		{
		case FIELD_FLOAT:
			flags = ( flags & ~0x3000u ) | 0x1000;
			m_fScriptedHUDFloats.Set( slot, (float)value ); break;
		case FIELD_INTEGER:
			flags &= ~0x3000u; m_iScriptedHUDInts.Set( slot, (int)value ); break;
		case FIELD_CSTRING:
			flags = ( flags & ~0x3000u ) | 0x2000;
			SetScriptedHUDString( slot, (const char *)value ); break;
		default:
			DevWarning( "Illegal type <%s> in dataval field\n", VariantFieldTypeName( value.GetType() ) ); ++errors; break;
		}
	}
	else if ( g_pScriptVM->GetValue( table, "special", &value ) )
	{
		m_iScriptedHUDInts.Set( slot, HUDRawInteger( value ) ); flags |= 0x3000;
	}
	value.Free();
	if ( g_pScriptVM->GetValue( table, "staticstring", &value ) )
	{
		SetScriptedHUDString( slot, (const char *)value );
		if ( !( flags & 3 ) ) flags |= 1;
	}
	value.Free();
	if ( errors ) flags |= 0x4000;
	m_iScriptedHUDFlags.Set( slot, flags );
	return errors ? -1 : slot;
}
CTerrorScriptedHUD::CTerrorScriptedHUD() : m_nNative20( 0 ), m_nScope( 1 ) { Reset(); }
void CTerrorScriptedHUD::Reset()
{
	m_UpdateTimer.Invalidate(); m_hTable = INVALID_HSCRIPT; m_hUpdate = INVALID_HSCRIPT;
	if ( g_pGameRules )
		for ( int i = 0; i < 15; ++i ) TerrorGameRules()->SetScriptedHUDFlags( i, 0x4000 );
}
void CTerrorScriptedHUD::Release()
{
	if ( g_pScriptVM )
	{
		if ( m_hUpdate && m_hUpdate != INVALID_HSCRIPT ) { g_pScriptVM->ReleaseFunction( m_hUpdate ); m_hUpdate = INVALID_HSCRIPT; }
		if ( m_hTable && m_hTable != INVALID_HSCRIPT ) { g_pScriptVM->ReleaseScope( m_hTable ); m_hTable = INVALID_HSCRIPT; }
	}
}
bool CTerrorScriptedHUD::SetLayout( HSCRIPT table )
{
	m_hTable = g_pScriptVM->ReferenceScope( table );
	m_hUpdate = g_pScriptVM->LookupFunction( "UpdateHUDTable", TheDirector->GetScriptScope( m_nScope ), true );
	m_UpdateTimer.Invalidate();
	Update();
	IGameEvent *event = gameeventmanager->CreateEvent( "scriptedmode_reloadhud" );
	gameeventmanager->FireEvent( event );
	return true;
}
void CTerrorScriptedHUD::Update()
{
	if ( m_hTable == INVALID_HSCRIPT || ( m_UpdateTimer.m_timestamp > 0.0f && gpGlobals->curtime < m_UpdateTimer.m_timestamp ) ) return;
	ScriptVariant_t fields;
	if ( !g_pScriptVM->GetValue( m_hTable, "Fields", &fields ) )
		DevWarning( "Hey, you are trying to FrameUpdate the HUD but there isn't a .Fields in your HUDTable\n" );
	else
	{
		if ( m_hUpdate )
		{
			ScriptVariant_t result;
			if ( g_pScriptVM->Call( m_hUpdate, TheDirector->GetScriptScope( m_nScope ), true, &result, m_hTable ) != SCRIPT_DONE )
				DevWarning( "Couldnt call Scripted HUD Table Update Function!\n" );
			result.Free();
		}
		bool used[15] = {};
		ScriptVariant_t key, value;
		int iterator = 0;
		for ( int count = g_pScriptVM->GetNumTableEntries( (HSCRIPT)fields ); count; --count )
		{
			iterator = g_pScriptVM->GetKeyValue( (HSCRIPT)fields, iterator, &key, &value );
			if ( value.GetType() == FIELD_HSCRIPT )
			{
				int slot = TerrorGameRules()->UpdateScriptedHUDField( (HSCRIPT)value );
				if ( slot == -1 ) Warning( "Error parsing HUD table element named %s\n", (const char *)key );
				else if ( used[slot] ) Warning( "Error: You used slot %d more than once in your HUD Table (key %s)\n", slot, (const char *)key );
				else used[slot] = true;
			}
			else Warning( "Your HUD Table has something in it that isnt a table! [key %s type %s]\n", (const char *)key, VariantFieldTypeName( value.GetType() ) );
			g_pScriptVM->ReleaseValue( key ); g_pScriptVM->ReleaseValue( value );
		}
		key.Free(); value.Free();
		for ( int i = 0; i < 15; ++i )
			if ( !used[i] ) TerrorGameRules()->SetScriptedHUDFlags( i, TerrorGameRules()->GetScriptedHUDFlags( i ) | 0x4000 );
		m_UpdateTimer.Start( 0.2f );
	}
	fields.Free();
}
static void Script_HUDSetLayout( HSCRIPT table )
{
	TheDirector->GetScriptedMode()->GetHUD()->SetLayout( table );
}
static void Script_HUDPlace( int slot, float x, float y, float width, float height ) { TerrorGameRules()->SetScriptedHUDLayout( slot, x, y, width, height ); }
static void Script_HUDManageTimers( int timer, int command, float value ) { if ( g_pGameRules ) TerrorGameRules()->ManageScriptedHUDTimer( timer, command, value ); }
static float Script_HUDReadTimer( int timer )
{
	if ( !g_pGameRules ) return 0.0f;
	if ( (unsigned int)timer <= 3 ) return TerrorGameRules()->ReadScriptedHUDTimer( timer );
	if ( timer != 4 ) return 0.0f;
	float end = TerrorGameRules()->GetHoldoutCooldownEndTime();
	float left = end - gpGlobals->curtime;
	return end < 0.0f || left <= 0.0f ? 0.0f : left;
}
void RegisterTerrorScriptedHUDFunctions()
{
	ScriptRegisterFunctionNamed( g_pScriptVM, Script_HUDManageTimers, "HUDManageTimers", "Manage the system timers" );
	ScriptRegisterFunctionNamed( g_pScriptVM, Script_HUDReadTimer, "HUDReadTimer", "Read value from a system timer" );
	ScriptRegisterFunctionNamed( g_pScriptVM, Script_HUDSetLayout, "HUDSetLayout", "Pass the HUD definition table for your mode" );
	ScriptRegisterFunctionNamed( g_pScriptVM, Script_HUDPlace, "HUDPlace", "Place the HUD element for Slot at X,Y,W,H" );
}
