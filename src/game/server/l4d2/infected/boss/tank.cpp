//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/tank.h"
#include "l4d2/infected/boss/tank_intention.h"
#include "l4d2/infected/boss/tank_locomotion.h"
#include "l4d2/infected/common/zombiebot_vision.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "tier1/KeyValues.h"
#include "igameevents.h"
#include "precache_register.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
extern int g_iNextZombieClass;
LINK_ENTITY_TO_CLASS( tank, Tank );
#ifndef L4D2
PRECACHE_REGISTER( tank );
#endif
// Native map7C2960: one NULL descriptor, no leaf save/key/input/output fields.
BEGIN_DATADESC( Tank )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( Tank, DT_Tank )
END_SEND_TABLE()

// S:469F70 ->4253D0 ->395D60. Concrete leaf owns all four genuine components.
Tank::Tank()
{
	m_intention = new TankIntention( this );
	m_locomotion = new TankLocomotion( this );
	m_body = new PlayerBody( this );
	m_vision = new ZombieBotVision( this );
	m_damageTypes = 0;
	m_killEventSent = false;
}
Tank::~Tank() // S:46A0D0, original destruction order
{
	delete m_locomotion; m_locomotion = NULL;
	delete m_body; m_body = NULL;
	delete m_intention; m_intention = NULL;
	delete m_vision; m_vision = NULL;
}
IIntention *Tank::GetIntentionInterface() const { return m_intention; }
ILocomotion *Tank::GetLocomotionInterface() const { return m_locomotion; }
IBody *Tank::GetBodyInterface() const { return m_body; }
IVision *Tank::GetVisionInterface() const { return m_vision; }
CBasePlayer *Tank::AllocatePlayerEntity( edict_t *edict, const char *name ) // S:46A200
{
	return CTerrorPlayer::CreatePlayer( "tank", edict );
}
Tank *CreateTankBot( const char *name ) // S:2FDAD0
{
	return NextBotCreatePlayerBot< Tank >( name );
}
void Tank::Spawn() // S:469A60, arousal follows base Spawn (unlike Hunter).
{
	g_iNextZombieClass = 8;
	BaseClass::Spawn();
	m_body->SetArousal( IBody::NEUTRAL );
}
void Tank::Precache() // S:469B20
{
	CTerrorPlayer::Precache();
	if ( FClassnameIs( this, "player" ) ) return;
	static const char *const sounds[] =
	{
		"HulkZombie.Breathe", "HulkZombie.Growl", "HulkZombie.Yell", "HulkZombie.Attack",
		"HulkZombie.Punch", "HulkZombie.PunchIncap", "HulkZombie.Die", "HulkZombie.StartLedgeClimb"
	};
	for ( int i = 0; i < ARRAYSIZE( sounds ); ++i ) PrecacheScriptSound( sounds[i] );
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( chapter )
	{
		const char *arms = chapter->GetString( "TankArmsVariant", "" );
		if ( arms && arms[0] ) PrecacheModel( arms );
	}
}
int Tank::OnTakeDamage_Alive( const CTakeDamageInfo &info ) // S:46A2B0
{
	const int health = GetHealth();
	const int result = BaseClass::OnTakeDamage_Alive( info ); // native396F60, NextBot dispatch retained
	CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
	if ( attacker )
	{
		if ( !IsIncapacitated() && attacker != this ) attacker->m_checkpointDamageToTank += health - GetHealth(); //46A170
		const int userid = attacker->GetUserID();
		if ( m_attackerUserIDs.Find( userid ) == m_attackerUserIDs.InvalidIndex() ) m_attackerUserIDs.AddToTail( userid );
		if ( attacker->GetTeamNumber() == TEAM_SURVIVOR || attacker->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) m_damageTypes |= info.GetDamageType();
	}
	return result;
}
void Tank::Event_Killed( const CTakeDamageInfo &info ) // S:469CC0, full ASM in tank_final_seams_native.txt
{
	TheDirector->ClearTankControlWinner(); // unconditional, before event allocation
	if ( !m_killEventSent )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "tank_killed" );
		if ( event )
		{
			CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() );
			event->SetInt( "userid", GetUserID() );
			event->SetInt( "attacker", attacker ? attacker->GetUserID() : 0 );
			event->SetBool( "solo", m_attackerUserIDs.Count() == 1 );
			bool l4d1Only = false;
			for ( int i = 0; i < m_attackerUserIDs.Count(); ++i )
			{
				CBasePlayer *player = UTIL_PlayerByUserId( m_attackerUserIDs[i] );
				if ( player && player->GetTeamNumber() == TEAM_SURVIVOR ) { l4d1Only = false; break; }
				if ( player && player->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) l4d1Only = true;
			}
			event->SetBool( "l4d1_only", l4d1Only );
			bool meleeOnly = false;
			if ( m_damageTypes ) { m_damageTypes &= 0x3BD9FF7B; meleeOnly = attacker && m_damageTypes == 0; }
			event->SetBool( "melee_only", meleeOnly );
			gameeventmanager->FireEvent( event );
			m_damageTypes = 0;
			m_killEventSent = true;
		}
	}
	OnKilled( info ); // exactly one NextBot event; do not use NextBotPlayer::Event_Killed again
	CTerrorPlayer::Event_Killed( info );
}
void Tank::BeginControlOffer() // S:469EE0
{
	m_takedamage = DAMAGE_NO;
	SetInStasis( true );
}
void Tank::EndControlOffer() // S:469F20
{
	m_takedamage = DAMAGE_YES;
	if ( IsInStasis() ) SetInStasis( false );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Native server 0x4699e0; 0x5c56d0 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace tankPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "tank", 0x5c56d0 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "tank", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
