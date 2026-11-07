//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/witch_body.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_music.h"
#include "igameevents.h"

#include "tier0/memdbgon.h"

WitchBody::WitchBody( Infected *me ) : ZombieBotBody( me )
{
}

// S:0x41CF30. This replaces, rather than extends, the common-infected injury
// response. In particular there is no common RGB darkening or common shot voice.
void WitchBody::OnInjured( const CTakeDamageInfo &info )
{
	Infected *me = static_cast< Infected * >( GetBot()->GetEntity() );
	if ( me->m_lifeState != LIFE_ALIVE )
		return;

	if ( info.GetDamageType() & DMG_BURN )
	{
		static ConVarRef burnMin( "z_vocalize_burn_min_interval" );
		static ConVarRef burnMax( "z_vocalize_burn_max_interval" );
		if ( !me->IsOnFire() )
		{
			me->Ignite( 9999.0f, info.GetAttacker() );
			me->EmitSound( "Zombie.IgniteScream" );
			m_shotVocalizeTimer.Start( RandomFloat( burnMin.GetFloat(), burnMax.GetFloat() ) );
		}
		if ( gpGlobals->curtime >= m_shotVocalizeTimer.m_timestamp )
		{
			m_shotVocalizeTimer.Start( RandomFloat( burnMin.GetFloat(), burnMax.GetFloat() ) );
			me->EmitSound( "WitchZombie.PainFire" );
		}

		CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() );
		if ( attacker )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "zombie_ignited" );
			if ( event )
			{
				event->SetInt( "userid", attacker->GetUserID() );
				event->SetInt( "entityid", me->entindex() );
				event->SetString( "victimname", "Witch" );
				gameeventmanager->FireEvent( event );
			}
		}
	}
	else if ( info.GetDamageType() & DMG_BULLET )
	{
		me->EmitSound( "Zombie.BulletImpact" );
		if ( gpGlobals->curtime >= m_shotVocalizeTimer.m_timestamp )
		{
			static ConVarRef shotInterval( "z_vocalize_shot_interval" );
			m_shotVocalizeTimer.Start( shotInterval.GetFloat() );
			me->EmitSound( "WitchZombie.Pain" );
		}
	}

	CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
	if ( attacker && ( attacker->GetTeamNumber() == TEAM_SURVIVOR || attacker->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
		attacker->GetMusic()->OnDamageDealt( info.GetDamage() );

	// Witch uses only DMG_BLAST for the zero hitgroup branch, unlike the
	// common-infected helper. Do not erase a burning Witch's recorded hitgroup.
	IGameEvent *event = gameeventmanager->CreateEvent( "infected_hurt" );
	if ( event )
	{
		CBasePlayer *player = ToBasePlayer( info.GetAttacker() );
		event->SetInt( "attacker", player ? player->GetUserID() : 0 );
		event->SetInt( "hitgroup", ( info.GetDamageType() & DMG_BLAST ) ? 0 : me->LastHitGroup() );
		event->SetInt( "entityid", me->entindex() );
		event->SetInt( "amount", (int)info.GetDamage() );
		event->SetInt( "type", info.GetDamageType() );
		gameeventmanager->FireEvent( event );
	}
}
