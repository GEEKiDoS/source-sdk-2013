//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/vscript/terror_script_globals.h"
#include "tier0/memdbgon.h"

static int s_nTerrorDifficulty = 1;
static const char *s_DifficultyNames[] = { "Easy", "Normal", "Hard", "Impossible" };

const char *Script_GetDifficultyString()
{
	// S21CA70 ->350BF0 ->350A90: reuse the genuine cached difficulty and names.
	unsigned int difficulty = (unsigned int)TerrorGameRules()->GetDifficultyLevel();
	return difficulty < ARRAYSIZE( s_DifficultyNames ) ? s_DifficultyNames[difficulty] : "Unknown";
}
static ConVar z_attack_incapacitated_damage( "z_attack_incapacitated_damage", "10", 0x00004004 );
static ConVar z_attack_pz_it_damage( "z_attack_pz_it_damage", "20", 0x00004004, "Damage done by infected on IT PZs" );

void TerrorDifficultyChanged( IConVar *variable, const char *, float )
{
	ConVar *var = static_cast<ConVar *>( variable );
	int difficulty = 1;
	for ( int i = 0; i < ARRAYSIZE( s_DifficultyNames ); ++i )
	{
		if ( !Q_stricmp( var->GetString(), s_DifficultyNames[i] ) )
		{
			difficulty = i;
			break;
		}
	}
	if ( difficulty != 1 && !g_TerrorModes.HasDifficulty() ) difficulty = 1;
	int oldDifficulty = s_nTerrorDifficulty;
	if ( oldDifficulty == difficulty ) return;
	s_nTerrorDifficulty = difficulty;
	if ( gameeventmanager )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "difficulty_changed" );
		if ( event )
		{
			event->SetInt( "newDifficulty", difficulty );
			event->SetInt( "oldDifficulty", oldDifficulty );
			event->SetString( "strDifficulty", s_DifficultyNames[difficulty] );
			gameeventmanager->FireEvent( event );
		}
	}
	var->SetValue( s_DifficultyNames[difficulty] );
}
int CTerrorGameRules::GetDifficultyLevel() const { return s_nTerrorDifficulty; }
float CTerrorGameRules::GetCommonAttackDamage( CBasePlayer *victim ) const
{
	CTerrorPlayer *player = ToTerrorPlayer( victim );
	if ( player )
	{
		if ( player->IsIncapacitated() ) return z_attack_incapacitated_damage.GetFloat();
		if ( player->GetTeamNumber() == TEAM_INFECTED && player->IsIT() ) return z_attack_pz_it_damage.GetFloat();
	}
	float damage = 1.0f;
	if ( TheDirector->GetScriptedMode()->IsEnabled() )
	{
		float custom = TheDirector->GetDirectorOptionFloat( "cm_BaseCommonAttackDamage", -1.0f );
		if ( custom >= 0.0f ) damage = custom;
	}
	switch ( GetDifficultyLevel() )
	{
	case 1: damage *= 2.0f; break;
	case 2: damage *= 5.0f; break;
	case 3: damage *= 20.0f; break;
	}
	return damage >= 1.0f ? damage : 1.0f;
}
