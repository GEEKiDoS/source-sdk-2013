//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/weapons/terror_weapon.h"
#include "vscript_server.h"
#include "tier0/memdbgon.h"

bool CDirectorScriptedMode::BotQuery( CBaseEntity *entity, int query, bool fallback )
{
	// Native cache belongs to the ACTUAL entity (1000/1004/1008), keyed by
	// this context's epoch312. It is not a ScriptedMode CUtlMap of fake actors.
	if ( entity->HasCachedScriptedBotQuery( m_nBotQueryEpoch, query ) ) return entity->GetCachedScriptedBotQuery( query );
	bool result = fallback;
	HSCRIPT scope = TheDirector->GetScriptScope( 3 );
	HSCRIPT function = g_pScriptVM->LookupFunction( "BotQuery", scope );
	if ( function )
	{
		ScriptVariant_t value;
		// Native279730 passes THREE arguments, and deliberately ignores status.
		g_pScriptVM->Call( function, scope, true, &value, query, entity->GetScriptInstance(), fallback );
		g_pScriptVM->ReleaseFunction( function );
		result = *reinterpret_cast< const unsigned char * >( &value ) != 0;
		value.Free();
	}
	entity->SetCachedScriptedBotQuery( m_nBotQueryEpoch, query, result );
	return result;
}
bool CDirectorScriptedMode::CanSurvivorBotBreak( CBaseEntity *entity )
{
	// S27AD50 tests BYTE0(active), not BYTE1(enabled).
	return !m_bActive || BotQuery( entity, 1, true );
}
void CDirector::OnZombieDeath( CBaseCombatCharacter *victim, const CTakeDamageInfo &info )
{
	// S25CE00 is MOV ECX,[ECX+648h]; JMP276550. +1608 is the owned pointer.
	m_pScriptedMode->OnZombieDeath( victim, info );
}
void CDirectorScriptedMode::OnZombieDeath( CBaseCombatCharacter *victim, const CTakeDamageInfo &info )
{
	if ( !victim || !m_bEnabled ) return;
	IGameEvent *event = gameeventmanager->CreateEvent( "zombie_death" );
	if ( !event ) return;
	bool headshot = ( info.GetDamageType() & 0x40000000 ) != 0;
	bool blast = ( info.GetDamageType() & DMG_BLAST ) != 0;
	CBaseEntity *inflictor = info.GetInflictor();
	bool minigun = inflictor && ( !Q_stricmp( inflictor->GetClassname(), "prop_minigun" ) || !Q_stricmp( inflictor->GetClassname(), "prop_minigun_l4d1" ) );
	if ( info.GetAttacker() ) event->SetInt( "attacker", info.GetAttacker()->entindex() );
	CBaseEntity *weaponEntity = info.GetWeapon();
	CBaseCombatWeapon *combatWeapon = weaponEntity ? weaponEntity->MyCombatWeaponPointer() : NULL;
	if ( combatWeapon ) event->SetInt( "weapon_id", static_cast< CTerrorWeapon * >( combatWeapon )->GetWeaponID() );
	else if ( inflictor && !Q_stricmp( inflictor->GetClassname(), "grenade_launcher_projectile" ) ) event->SetInt( "weapon_id", 21 );
	event->SetInt( "victim", victim->entindex() );
	event->SetInt( "gender", victim->GetGender() ); // Actual base entity224 getter, NOT infected modelType.
	event->SetInt( "infected_id", victim->GetZombieClass() );
	event->SetBool( "submerged", victim->GetWaterLevel() != 0 );
	event->SetBool( "headshot", headshot );
	event->SetBool( "minigun", minigun );
	event->SetBool( "blast", blast );
	gameeventmanager->FireEvent( event );
}

static bool CallOriginalItemBoolean( const char *name, int weaponID )
{
    HSCRIPT scope = TheDirector->GetOptionsScope();
    HSCRIPT function = g_pScriptVM->LookupFunction( name, scope );
    g_pScriptVM->ReleaseFunction( function );
    if ( !function )
        return true; // Genuine279D20/279DC0 missing-function result, not a fallback provider.
    function = g_pScriptVM->LookupFunction( name, scope );
    ScriptVariant_t value;
    value = false;
    const char *classname = WeaponIDToClassname( weaponID ); // Existing full S24EB70 owner, including original empty invalid result.
    if ( function )
    {
        g_pScriptVM->Call( function, scope, true, &value, classname );
        g_pScriptVM->ReleaseFunction( function );
    }
    const bool allowed = *reinterpret_cast< const unsigned char * >( &value ) != 0;
    value.Free();
    return allowed;
}
bool CDirectorScriptedMode::AllowFallenSurvivorItem( int weaponID ) const
{
    return CallOriginalItemBoolean( "AllowFallenSurvivorItem", weaponID );
}

bool CDirectorScriptedMode::ShouldAvoidItem( int weaponID ) const
{
	if ( !m_bEnabled ) return false;
	HSCRIPT scope = TheDirector->GetOptionsScope();
	HSCRIPT function = g_pScriptVM->LookupFunction( "ShouldAvoidItem", scope );
	g_pScriptVM->ReleaseFunction( function );
	if ( !function ) return false;
	ScriptVariant_t value;
	const char *classname = WeaponIDToClassname( weaponID ); // Actual S24EB70, not the distinct S24EB50 alias table.
	scope = TheDirector->GetOptionsScope();
	function = g_pScriptVM->LookupFunction( "ShouldAvoidItem", scope );
	if ( function )
	{
		g_pScriptVM->Call( function, TheDirector->GetOptionsScope(), true, &value, classname );
		g_pScriptVM->ReleaseFunction( function );
	}
	const bool avoid = *reinterpret_cast< const unsigned char * >( &value ) != 0;
	value.Free();
	return avoid;
}
