//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terrorplayer.h"
#include "recipientfilter.h"
#include "vscript_server.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

CTerrorGameRules::~CTerrorGameRules()
{
	// Native2E0EB0 reverts actual saved convars BEFORE derived queue/field
	// destruction. The inherited destructor later sees the same emptied store.
	RevertSavedConvars();
}

void CTerrorGameRules::PlayerSpawn( CBasePlayer *base )
{
	// Native2E04D0. Its actual IsPlayer check is not an alive/connection or
	// SurvivorBot RTTI gate. Error is the ORIGINAL fatal bad-call guard.
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
	if ( !player || !player->IsPlayer() )
	{
		Error( "PlayerSpawn" );
		return;
	}
	if ( player->State_Get() != STATE_ACTIVE ) return; // Sole actual CS state9160.
	player->SetArmorValue( 0 );
	player->m_bHasNightVision = false; // Native9269 before9270 before2463F0/9268.
	player->m_bNightVisionOn = false;
	player->m_bHasDefuser = false;
	if ( !player->IsAwaitingTransitionRestore() || g_bTerrorRestartAfterDeath )
	{
		// Native10045FB0 removes weapons AND ammo before the actual2052
		// virtual. Reuse those existing inventory owners, not a pistol facade.
		player->CBaseCombatCharacter::RemoveAllWeapons();
		player->RemoveAllAmmo();
		player->GiveDefaultItems(); // Real2FAED0 in existing terrorplayer_inventory.cpp.
	}
	CSingleUserRecipientFilter filter( player );
	filter.MakeReliable();
	FOR_EACH_VEC( m_SavedConvars, i )
	{
		// Actual inherited CGameRules748 registry already has real pointer-
		// identity dedup, SaveConvar writers, client message and dtor revert.
		// Do NOT introduce another vector, per-round reset or value snapshot.
		const char *name = STRING( m_SavedConvars[i] );
		if ( !name ) name = ""; // Original string_t NULL -> empty guard only.
		const ConVarRef var( name );
		if ( var.IsFlagSet( FCVAR_REPLICATED ) )
		{
			UserMessageBegin( filter, "SavedConvar" );
			WRITE_STRING( name );
			MessageEnd();
		}
	}
	// Native queue892 insertion belongs to GetPlayerSpawnSpot2E09D0 and
	// explicit2E0D20, NOT this equipment callback. Existing sole queue retained.
}

bool CTerrorGameRules::GiveDefaultItemsFromScript( CTerrorPlayer *player )
{
	return TheDirector->GetScriptedMode()->GiveDefaultItems( player );
}

namespace
{
	void CallOptionsIntegerFunction( const char *name, int argument, ScriptVariant_t *result )
	{
		// Native279510: re-look up in DirectorOptions on EACH call;
		// a missing function leaves the previous result untouched.
		HSCRIPT function = g_pScriptVM->LookupFunction( name, TheDirector->GetOptionsScope(), false );
		if ( function )
		{
			g_pScriptVM->Call( function, TheDirector->GetOptionsScope(), true, result, argument );
			g_pScriptVM->ReleaseFunction( function );
		}
	}
}

bool CDirectorScriptedMode::GiveDefaultItems( CTerrorPlayer *player )
{
	// Native27A000 checks ENABLED byte1 and TEAM2, not active byte0 or both
	// survivor teams. Original enabled scope assumes its genuine live VM.
	if ( !m_bEnabled || player->GetTeamNumber() != TEAM_SURVIVOR ) return false;
	HSCRIPT function = g_pScriptVM->LookupFunction( "GetDefaultItem", TheDirector->GetOptionsScope(), false );
	g_pScriptVM->ReleaseFunction( function ); // Native releases evenNULL BEFORE testing the saved handle.
	if ( !function ) return false;
	ScriptVariant_t result;
	int index = 0;
	CallOptionsIntegerFunction( "GetDefaultItem", index, &result );
	while ( result.GetType() == FIELD_CSTRING )
	{
		player->GiveNamedItemInternal( static_cast< const char * >( result ), 0, true, NULL );
		index = (int)( (unsigned int)index + 1u );
		CallOptionsIntegerFunction( "GetDefaultItem", index, &result );
	}
	// Presence handles the WHOLE loadout even when the first result is not a
	// string or execution fails. Original destroys only the final variant;
	// do not add per-iteration VM ReleaseValue, a count cap, or fallback pistol.
	return true;
}

int CDirectorScriptedMode::ConvertZombieClass( int zombieClass ) const
{
	// Native279F40 tests ENABLED byte1, not active byte0. Its genuine live VM
	// and DirectorOptions scope are the same owners used by GetDefaultItem27A000.
	if ( !m_bEnabled ) return zombieClass;
	HSCRIPT function = g_pScriptVM->LookupFunction( "ConvertZombieClass", TheDirector->GetOptionsScope(), false );
	g_pScriptVM->ReleaseFunction( function ); // Original releases evenNULL before testing the saved handle.
	if ( !function ) return zombieClass;
	ScriptVariant_t result;
	CallOptionsIntegerFunction( "ConvertZombieClass", zombieClass, &result );
	// Native accepts INTEGER bits directly; EVERY other type uses raw FLOAT
	// bits and CVTTSS2SI. AssignTo(int) would incorrectly accept bool1 as1.
	uint32 bits = result.GetRawPayload32();
	float raw;
	Q_memcpy( &raw, &bits, sizeof( raw ) );
	const int converted = result.GetType() == FIELD_INTEGER ? static_cast< int32 >( bits ) : _mm_cvttss_si32( _mm_set_ss( raw ) );
	if ( (unsigned int)converted - 1u <= 7u ) return converted;
	return zombieClass; // Original guarded result range, including unordered/overflow CVTT indefinite.
}
