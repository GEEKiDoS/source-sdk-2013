//========= Copyright Valve Corporation, All rights reserved. ============//
// Exactly the four server-side Damage/Life providers. Registration remains
// gated on their real Director, VM/options, scenario and saved-player producers.
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "tier0/memdbgon.h"

static ConVar survivor_max_incapacitated_count( "survivor_max_incapacitated_count", "2", 0x00004004,
	"How many times you can be incapacitated instead of killed between health kits." );
bool CTerrorGameRules::IsBleedoutSuppressed() const { return TheDirector->IsBleedoutSuppressed(); }
int CTerrorGameRules::GetSurvivorMaxIncapacitatedCount() const
{
	return TheDirector->GetDirectorOptionInt( "SurvivorMaxIncapacitatedCount", survivor_max_incapacitated_count.GetInt() );
}
bool CTerrorGameRules::IsTempHealthOnly() const { return TheDirector->GetScriptedMode()->QueryOptionBool( "cm_TempHealthOnly", false ); }
void CTerrorGameRules::GetRestoringPlayerCounts( int team, int *players, int *restoring ) const
{
	TheDirector->GetSavedPlayerStates()->GetRestoringPlayerCounts( team, players, restoring );
}

