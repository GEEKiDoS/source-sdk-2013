//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "GameEventListener.h"
#include "igamesystem.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "tier1/utlvector.h"
#include "tier0/memdbgon.h"

// Native S2513A0/S251480, global ctor592310. This is the ORIGINAL event
// projection owner, not an award-local hurt/kill/achievement substitute.
class CAchievementMsgHandler : public CGameEventListener, public CAutoGameSystem
{
public:
	CAchievementMsgHandler() : CAutoGameSystem( "CAchievementMsgHandler" ) {}
	virtual void PostInit() OVERRIDE // Native secondary IGameSystem vtable slot2.
	{
		ListenForGameEvent( "weapon_fire" );
		ListenForGameEvent( "round_start_pre_entity" );
		ListenForGameEvent( "survival_round_start" );
		ListenForGameEvent( "player_hurt" );
	}
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE
	{
		const char *name = event->GetName();
		if ( !name ) return;
		if ( !Q_strcmp( name, "round_start_pre_entity" ) || !Q_strcmp( name, "survival_round_start" ) )
		{
			m_nonPistolFired.RemoveAll(); m_nonMeleeFired.RemoveAll(); return;
		}
		if ( !Q_strcmp( name, "player_hurt" ) )
		{
			CTerrorPlayer *victim = ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
			if ( !victim || victim->GetTeamNumber() != TEAM_SURVIVOR ) return;
			IGameEvent *concise = gameeventmanager->CreateEvent( "player_hurt_concise" );
			if ( concise )
			{
				concise->SetInt( "userid", event->GetInt( "userid" ) );
				concise->SetInt( "attackerentid", event->GetInt( "attackerentid" ) );
				concise->SetInt( "dmg_health", event->GetInt( "dmg_health" ) );
				concise->SetInt( "type", event->GetInt( "type" ) );
				gameeventmanager->FireEvent( concise );
			}
			return;
		}
		if ( Q_strcmp( name, "weapon_fire" ) ) return;
		const int weapon = event->GetInt( "weaponid" );
		if ( !weapon ) { DevMsg( "weapon_fire message for %s is missing weapon ID!\n", event->GetString( "weapon", "" ) ); return; }
		const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( weapon );
		if ( !info ) return; // Real registry's native missing-info path, including out-of-array adaptation.
		const int userID = event->GetInt( "userid" ), type = info->m_WeaponType;
		if ( m_nonPistolFired.Find( userID ) == m_nonPistolFired.InvalidIndex() && type != 0 && type != 13 && type != 7 && type != 8 && type != 9 )
			FireFirstUse( "non_pistol_fired", event, userID, m_nonPistolFired );
		if ( m_nonMeleeFired.Find( userID ) == m_nonMeleeFired.InvalidIndex() && type != 10 && type != 11 && type != 15 && type != 13 && type != 7 && type != 8 && type != 9 )
			FireFirstUse( "non_melee_fired", event, userID, m_nonMeleeFired );
	}
private:
	void FireFirstUse( const char *name, IGameEvent *source, int userID, CUtlVector< int > &history )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( name );
		if ( event )
		{
			event->SetInt( "userid", source->GetInt( "userid" ) );
			gameeventmanager->FireEvent( event );
			history.AddToTail( userID ); // Native append is AFTER FireEvent, not an early dedup latch.
		}
	}
	CUtlVector< int > m_nonPistolFired; //Native24..43.
	CUtlVector< int > m_nonMeleeFired; //Native44..63.
};
static CAchievementMsgHandler g_AchievementMsgHandler;
