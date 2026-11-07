//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/l4d_awards_infected.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav.h"
#include "BasePropDoor.h"
#include "team.h"
#include "ispatialpartition.h"
#include "tier0/memdbgon.h"

namespace
{
CTerrorPlayer *EventPlayer( IGameEvent *event, const char *key )
{
	return ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( key ) ) );
}

bool IsEvent( IGameEvent *event, const char *name )
{
	return event && !Q_stricmp( event->GetName(), name );
}

bool IsInfectedClass( CTerrorPlayer *player, int zombieClass )
{
	return player && player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == zombieClass;
}

bool IsClawWeapon( const char *weapon )
{
	return !Q_stricmp( weapon, "hunter_claw" ) || !Q_stricmp( weapon, "smoker_claw" ) ||
		!Q_stricmp( weapon, "boomer_claw" ) || !Q_stricmp( weapon, "knife" );
}

bool IsTankPunch( const char *weapon )
{
	return !Q_stricmp( weapon, "tank_claw" ) || !Q_stricmp( weapon, "knife" );
}

// S379C80 stops on the first checkpoint door, even if that door is open and a
// later door is closed. Do not replace this with a nearest/any-closed-door test.
class CheckpointDoorScan : public IPartitionEnumerator
{
public:
	CheckpointDoorScan() : m_door( NULL ) {}
	virtual IterationRetval_t EnumElement( IHandleEntity *handle ) OVERRIDE
	{
		CBaseEntity *entity = gEntList.GetBaseEntity( handle->GetRefEHandle() );
		CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( entity );
		if ( !door || !door->IsCheckpointDoor() )
			return ITERATION_CONTINUE;
		m_door = door;
		return ITERATION_STOP;
	}
	CBasePropDoor *GetDoor() const { return m_door; }
private:
	CBasePropDoor *m_door;
};
}

// S372F60 / S372FA0. These are the three original claw families, not every
// damage event caused by a playable infected. In particular, type 32 is excluded
// only on hurt; pounce/tongue victims are excluded on both paths.
void CClawHitAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
}

void CClawHitAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_INFECTED || IsInfectedClass( player, 8 ) )
		return;
	const bool hurt = IsEvent( event, "player_hurt" );
	if ( !hurt && !IsEvent( event, "player_incapacitated" ) )
		return;
	CTerrorPlayer *victim = EventPlayer( event, "userid" );
	CTerrorPlayer *attacker = EventPlayer( event, "attacker" );
	if ( !victim || attacker != player || victim->GetTeamNumber() == player->GetTeamNumber() ||
		!IsClawWeapon( event->GetString( "weapon" ) ) || ( hurt && event->GetInt( "type" ) == 32 ) ||
		victim->IsBeingPounced() || victim->IsHangingFromTongue() )
		return;
	if ( IsInfectedClass( player, 2 ) ) EarnAward( hurt ? 117 : 118 );
	else if ( IsInfectedClass( player, 3 ) ) EarnAward( hurt ? 121 : 122 );
	else if ( IsInfectedClass( player, 1 ) ) EarnAward( hurt ? 119 : 120 );
}

// S373380 / S3733A0. Native checks alive and not incapacitated, not ghost.
void CDoomsdayAwardTemplate::Init() { ListenForGameEvent( "mission_lost" ); }
void CDoomsdayAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( IsEvent( event, "mission_lost" ) && IsInfectedClass( player, 8 ) &&
		player->IsAlive() && !player->IsIncapacitated() )
		EarnAward( 103 );
}

// S3737F0 constructor; S3734A0 / S3734F0. The equality is the native same-tick
// death stamp, not a grace period. The blast attacker need only resolve: it is
// deliberately NOT compared with this template's owner.
CExplosionAwardTemplate::CExplosionAwardTemplate( int userID ) : CAwardTemplate( userID ), m_deathTime( 0.0f ) {}
void CExplosionAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_death" );
}
void CExplosionAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !IsInfectedClass( player, 2 ) )
		return;
	const bool hurt = IsEvent( event, "player_hurt" );
	const bool incapacitated = IsEvent( event, "player_incapacitated" );
	const bool death = IsEvent( event, "player_death" );
	if ( !hurt && !incapacitated && !death )
		return;
	CTerrorPlayer *victim = EventPlayer( event, "userid" );
	CTerrorPlayer *attacker = EventPlayer( event, "attacker" );
	if ( victim && attacker && m_deathTime == gpGlobals->curtime &&
		victim->GetTeamNumber() != player->GetTeamNumber() && !Q_stricmp( event->GetString( "weapon" ), "boomer" ) )
	{
		EarnAward( hurt ? 138 : 139 );
		return;
	}
	if ( death && victim == player )
		m_deathTime = gpGlobals->curtime;
}

// S373870 / S3738B0. Damage is compared with the victim's actual health plus
// decaying temporary health at callback time, including the native <= boundary.
void CFallCauserAwardTemplate::Init()
{
	ListenForGameEvent( "player_ledge_grab" );
	ListenForGameEvent( "player_falldamage" );
}
void CFallCauserAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_INFECTED )
		return;
	const bool ledge = IsEvent( event, "player_ledge_grab" );
	if ( !ledge && !IsEvent( event, "player_falldamage" ) )
		return;
	CTerrorPlayer *victim = EventPlayer( event, "userid" );
	if ( !victim || EventPlayer( event, "causer" ) != player || victim->GetTeamNumber() == player->GetTeamNumber() )
		return;
	if ( ledge || event->GetInt( "damage" ) <= victim->GetHealth() + victim->GetHealthBuffer() )
		EarnAward( 115 );
	else
		EarnAward( 116 );
}

// S373E50; S373B80 / S373BD0. The shipping listener marks interaction but never
// advances m_hitCount or awards 141..145. Preserve that behavior rather than
// inventing a pin timer, hit counter, or an ability event to make it award.
CHitAndRunAwardTemplate::CHitAndRunAwardTemplate( int userID ) : CAwardTemplate( userID ), m_hasInteracted( 0 ), m_hitCount( 0 ) {}
void CHitAndRunAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_shoved" );
	ListenForGameEvent( "player_death" );
}
void CHitAndRunAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !IsInfectedClass( player, 3 ) )
		return;
	if ( !m_hasInteracted && IsEvent( event, "player_hurt" ) )
	{
		CTerrorPlayer *victim = EventPlayer( event, "userid" );
		CTerrorPlayer *attacker = EventPlayer( event, "attacker" );
		if ( !victim || !attacker )
			return;
		if ( attacker == player && victim->GetTeamNumber() != attacker->GetTeamNumber() )
			m_hasInteracted = 1;
		else if ( victim == player && attacker->GetTeamNumber() != victim->GetTeamNumber() &&
			( attacker->GetAbsOrigin() - player->GetAbsOrigin() ).Length() < 500.0f )
			m_hasInteracted = 1;
	}
	else if ( !m_hasInteracted && IsEvent( event, "player_shoved" ) )
	{
		CTerrorPlayer *victim = EventPlayer( event, "userid" );
		CTerrorPlayer *attacker = EventPlayer( event, "attacker" );
		if ( victim == player && attacker && attacker->GetTeamNumber() != victim->GetTeamNumber() )
			m_hasInteracted = 1;
	}
	else if ( IsEvent( event, "player_death" ) && EventPlayer( event, "userid" ) == player )
	{
		m_hasInteracted = 0;
		m_hitCount = 0;
	}
}
bool CHitAndRunAwardTemplate::EarnAward( int award, CBaseEntity *subject )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player ) return false;
	const bool result = CAwardTemplate::EarnAward( award, subject );
	if ( award >= 142 && award <= 145 ) player->RemoveAward( award - 1 );
	return result;
}

// S3740C0; S373ED0 / S373F10. History is indexed by entity index, not character
// or user ID; a slot reused before the owner's death remains hit. Slot 32's
// native count-byte alias is handled explicitly rather than invoking C++ UB.
CHitEverybodyAwardTemplate::CHitEverybodyAwardTemplate( int userID ) : CAwardTemplate( userID ), m_hitCount( 0 )
{
	Q_memset( m_hit, 0, sizeof( m_hit ) );
}
void CHitEverybodyAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_death" );
}
void CHitEverybodyAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_INFECTED ) return;
	if ( IsEvent( event, "player_death" ) )
	{
		if ( EventPlayer( event, "userid" ) == player )
		{
			Q_memset( m_hit, 0, sizeof( m_hit ) );
			m_hitCount = 0;
		}
		return;
	}
	if ( !IsEvent( event, "player_hurt" ) ) return;
	CTerrorPlayer *victim = EventPlayer( event, "userid" );
	if ( !victim || EventPlayer( event, "attacker" ) != player || victim->GetTeamNumber() == player->GetTeamNumber() ) return;
	const int slot = victim->entindex();
	if ( slot == 32 )
	{
		// Native slot 32 aliases the low byte of the adjacent count. Preserve
		// its observable behavior without indexing beyond the history array.
		if ( m_hitCount & 0xFF ) return;
		m_hitCount = ( m_hitCount & ~0xFF ) | 1;
	}
	else
	{
		if ( (unsigned int)slot < ARRAYSIZE( m_hit ) )
		{
			if ( m_hit[slot] ) return;
			m_hit[slot] = true;
		} // Skip only undefined read/write; the independent native count increment remains below.
	}
	++m_hitCount;
	CTeam *survivors = GetGlobalTeam( TEAM_SURVIVOR );
	if ( survivors && survivors->GetNumPlayers() == m_hitCount && survivors->GetNumPlayers() > 1 ) EarnAward( 140 );
}

// S374150 / S3741A0. The native exact type is DMG_CRUSH (1), not a bit test.
void CHulkPhysicsAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_death" );
}
void CHulkPhysicsAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !IsInfectedClass( player, 8 ) ) return;
	const bool hurt = IsEvent( event, "player_hurt" );
	if ( !hurt && !IsEvent( event, "player_incapacitated" ) && !IsEvent( event, "player_death" ) ) return;
	CTerrorPlayer *victim = EventPlayer( event, "userid" );
	if ( !victim || EventPlayer( event, "attacker" ) != player || victim->GetTeamNumber() == player->GetTeamNumber() || event->GetInt( "type" ) != 1 ) return;
	if ( !hurt ) EarnAward( 125 );
	else if ( event->GetInt( "dmg_health" ) >= 10 )
	{
		EarnAward( 124 );
		player->RecordPhysicsHit( victim->GetSurvivorCharacter() );
	}
}

// S3748B0; S374400 / S374480. Incap history is updated for every resolved
// victim, even if another player caused the incap/death. Own death does not
// clear the array; own spawn snapshots the actual survivors' incap states.
CHulkPunchAwardTemplate::CHulkPunchAwardTemplate( int userID ) : CAwardTemplate( userID )
{
	Q_memset( m_incapacitated, 0, sizeof( m_incapacitated ) );
}
void CHulkPunchAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "revive_success" );
	ListenForGameEvent( "player_spawn" );
	ListenForGameEvent( "player_death" );
}
void CHulkPunchAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !IsInfectedClass( player, 8 ) ) return;
	if ( IsEvent( event, "revive_success" ) )
	{
		CTerrorPlayer *victim = EventPlayer( event, "subject" );
		if ( victim && (unsigned int)victim->entindex() < ARRAYSIZE( m_incapacitated ) ) m_incapacitated[victim->entindex()] = false;
		return;
	}
	if ( IsEvent( event, "player_spawn" ) )
	{
		if ( EventPlayer( event, "userid" ) == player )
		{
			Q_memset( m_incapacitated, 0, sizeof( m_incapacitated ) );
			// The original snapshots entity indices 0 through 31 only.
			for ( int slot = 0; slot < 32; ++slot )
			{
				CTerrorPlayer *survivor = ToTerrorPlayer( UTIL_PlayerByIndex( slot ) );
				if ( survivor && survivor->GetTeamNumber() == TEAM_SURVIVOR ) m_incapacitated[slot] = survivor->IsIncapacitated();
			}
		}
		return;
	}
	const bool hurt = IsEvent( event, "player_hurt" );
	const bool incapacitated = IsEvent( event, "player_incapacitated" );
	const bool death = IsEvent( event, "player_death" );
	if ( !hurt && !incapacitated && !death ) return;
	CTerrorPlayer *victim = EventPlayer( event, "userid" );
	if ( !victim ) return;
	const int slot = victim->entindex();
	const bool stored = (unsigned int)slot < ARRAYSIZE( m_incapacitated );
	const bool wasIncapacitated = stored ? m_incapacitated[slot] : false; // Standing OOB read0, write omitted.
	const bool punch = EventPlayer( event, "attacker" ) == player && victim->GetTeamNumber() != player->GetTeamNumber() && IsTankPunch( event->GetString( "weapon" ) );
	if ( hurt )
	{
		if ( punch ) EarnAward( wasIncapacitated ? 130 : 128 );
	}
	else if ( incapacitated )
	{
		if ( punch ) EarnAward( 129 );
		if ( stored ) m_incapacitated[slot] = true;
	}
	else
	{
		if ( punch ) EarnAward( wasIncapacitated ? 131 : 129 );
		if ( stored ) m_incapacitated[slot] = false;
	}
}

// S374940 / S374990. Rock damage has no invented threshold or ghost gate.
void CHulkRockAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_death" );
}
void CHulkRockAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !IsInfectedClass( player, 8 ) ) return;
	const bool hurt = IsEvent( event, "player_hurt" );
	if ( !hurt && !IsEvent( event, "player_incapacitated" ) && !IsEvent( event, "player_death" ) ) return;
	CTerrorPlayer *victim = EventPlayer( event, "userid" );
	if ( victim && EventPlayer( event, "attacker" ) == player && victim->GetTeamNumber() != player->GetTeamNumber() && !Q_stricmp( event->GetString( "weapon" ), "tank_rock" ) )
		EarnAward( hurt ? 126 : 127 );
}

// S374BF0 / S374D40. award_earned is registered but ignored by the original.
// S374C30 starts at one damage, includes connected non-survivor players (not
// just live infected), and gives a tie (including one damage) to this owner.
void CMostHitsAwardTemplate::Init()
{
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "award_earned" );
}
void CMostHitsAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_INFECTED || !IsEvent( event, "map_transition" ) ) return;
	CTerrorPlayer *best = NULL;
	int mostDamage = 1;
	for ( int slot = 1; slot <= gpGlobals->maxClients; ++slot )
	{
		CTerrorPlayer *candidate = ToTerrorPlayer( UTIL_PlayerByIndex( slot ) );
		if ( !candidate || !candidate->IsConnected() || candidate->GetTeamNumber() == TEAM_SURVIVOR ) continue;
		const int damage = candidate->m_checkpointSurvivorDamage.Get();
		if ( damage > mostDamage || ( damage == mostDamage && candidate == player ) )
		{
			mostDamage = damage;
			best = candidate;
		}
	}
	if ( best == player ) EarnAward( 146 );
}

// S374DD0 / S374E20. lunge_pounce userid is the hunter, victim is the survivor.
// The incapacitation/death path checks the victim's genuine pounce handle, not
// health, elapsed time, or an attacker identity inferred from a new event.
void CPounceAwardTemplate::Init()
{
	ListenForGameEvent( "lunge_pounce" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_death" );
}
void CPounceAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !IsInfectedClass( player, 3 ) ) return;
	if ( IsEvent( event, "lunge_pounce" ) )
	{
		if ( EventPlayer( event, "userid" ) == player && EventPlayer( event, "victim" ) )
			EarnAward( event->GetFloat( "distance" ) < 300.0f ? 106 : 107 );
	}
	else if ( IsEvent( event, "player_incapacitated" ) || IsEvent( event, "player_death" ) )
	{
		CTerrorPlayer *victim = EventPlayer( event, "userid" );
		if ( victim && EventPlayer( event, "attacker" ) == player && victim->IsBeingPounced() ) EarnAward( 108 );
	}
}

// S375030 / S375050.
void CRedecoratorAwardTemplate::Init() { ListenForGameEvent( "break_breakable" ); }
void CRedecoratorAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( IsInfectedClass( player, 8 ) && IsEvent( event, "break_breakable" ) && EventPlayer( event, "userid" ) == player && event->GetBool( "hulkonly" ) ) EarnAward( 147 );
}

// S3756F0; S375380 / S3753F0. Only the first exact choke damage after each
// tongue_grab counts; a grab itself is not a choke. Death deliberately uses the
// stored choke flag/type rather than requiring a still-live tongue owner.
CTongueChokeAwardTemplate::CTongueChokeAwardTemplate( int userID ) : CAwardTemplate( userID ), m_hasChoked( false ), m_chokeCount( 0 ) {}
void CTongueChokeAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "tongue_grab" );
}
void CTongueChokeAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !IsInfectedClass( player, 1 ) ) return;
	if ( IsEvent( event, "tongue_grab" ) )
	{
		if ( EventPlayer( event, "userid" ) == player ) m_hasChoked = false;
		return;
	}
	const bool hurt = IsEvent( event, "player_hurt" );
	const bool incapacitated = IsEvent( event, "player_incapacitated" );
	const bool death = IsEvent( event, "player_death" );
	if ( !hurt && !incapacitated && !death ) return;
	CTerrorPlayer *victim = EventPlayer( event, "userid" );
	CTerrorPlayer *attacker = EventPlayer( event, "attacker" );
	if ( hurt )
	{
		if ( attacker == player && victim && victim->GetTongueOwner() == player && event->GetInt( "type" ) == 0x100000 && !m_hasChoked )
		{
			++m_chokeCount;
			m_hasChoked = true;
			if ( m_chokeCount >= 1 && m_chokeCount <= 5 ) EarnAward( 108 + m_chokeCount );
		}
	}
	else if ( incapacitated )
	{
		if ( attacker == player && victim && victim->GetTongueOwner() == player )
		{
			m_hasChoked = false;
			EarnAward( 114 );
		}
	}
	else if ( victim == player )
	{
		m_chokeCount = 0;
		m_hasChoked = false;
	}
	else if ( attacker == player && victim && event->GetInt( "type" ) == 0x100000 && m_hasChoked )
	{
		m_hasChoked = false;
		EarnAward( 114 );
	}
}
bool CTongueChokeAwardTemplate::EarnAward( int award, CBaseEntity *subject )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player ) return false;
	const bool result = CAwardTemplate::EarnAward( award, subject );
	if ( award >= 110 && award <= 113 ) player->RemoveAward( award - 1 );
	return result;
}

// S375870; S375820 / S375920. One count per elapsed five-second window, NOT one
// count per victim in a single vomit. Death clears the count but not the timer;
// clock rewind invalidates the timer without resetting the accumulated count.
CVomitHitAwardTemplate::CVomitHitAwardTemplate( int userID ) : CAwardTemplate( userID ), m_hitCount( 0 ) {}
void CVomitHitAwardTemplate::Init()
{
	ListenForGameEvent( "player_now_it" );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "fatal_vomit" );
}
void CVomitHitAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player || player->GetTeamNumber() != TEAM_INFECTED ) return;
	if ( IsEvent( event, "player_now_it" ) )
	{
		if ( !event->GetBool( "by_boomer" ) || !EventPlayer( event, "userid" ) || EventPlayer( event, "attacker" ) != player ) return;
		if ( m_hitTimer.GetElapsedTime() < 0.0f ) m_hitTimer.Invalidate();
		// Native S2E480 uses >=, while the SDK IsElapsed uses strict >.
		if ( m_hitTimer.GetRemainingTime() <= 0.0f )
		{
			++m_hitCount;
			m_hitTimer.Start( 5.0f );
			if ( m_hitCount >= 1 && m_hitCount <= 5 ) EarnAward( 131 + m_hitCount );
		}
	}
	else if ( IsEvent( event, "player_death" ) )
	{
		if ( EventPlayer( event, "userid" ) == player ) m_hitCount = 0;
	}
	else if ( IsEvent( event, "fatal_vomit" ) && EventPlayer( event, "userid" ) == player && IsInfectedClass( player, 2 ) )
		EarnAward( 137 );
}
bool CVomitHitAwardTemplate::EarnAward( int award, CBaseEntity *subject )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player ) return false;
	const bool result = CAwardTemplate::EarnAward( award, subject );
	if ( award >= 133 && award <= 136 ) player->RemoveAward( award - 1 );
	return result;
}

// S379D10 / S379D50. Shared victim-side Gotcha101 and attacker-side148.
void CGotchaAwardTemplate::Init()
{
	ListenForGameEvent( "player_hurt" );
	ListenForGameEvent( "player_incapacitated" );
}
void CGotchaAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( !player || ( !IsEvent( event, "player_hurt" ) && !IsEvent( event, "player_incapacitated" ) ) ) return;
	CTerrorPlayer *victim = EventPlayer( event, "userid" );
	CTerrorPlayer *attacker = EventPlayer( event, "attacker" );
	if ( !victim || !attacker || victim->GetTeamNumber() != TEAM_SURVIVOR || attacker->GetTeamNumber() != TEAM_INFECTED ) return;
	TerrorNavArea *victimArea = ToTerrorNavArea( victim->GetLastKnownArea() );
	TerrorNavArea *attackerArea = ToTerrorNavArea( attacker->GetLastKnownArea() );
	if ( !victimArea || !victimArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) || !attackerArea || attackerArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) return;
	CheckpointDoorScan scan;
	partition->EnumerateElementsInSphere( PARTITION_ENGINE_SOLID_EDICTS, victim->WorldSpaceCenter(), 100.0f, true, &scan );
	CBasePropDoor *door = scan.GetDoor();
	if ( !door || !door->IsDoorClosed() ) return;
	if ( victim == player ) EarnAward( 101 );
	else if ( attacker == player ) EarnAward( 148 );
}

// S37DDC0 / S37DDE0. Mission loss is the original authority; no second all-dead
// approximation, live-state/ghost restriction, or award-generated round event.
void CNoSurvivorsAwardTemplate::Init() { ListenForGameEvent( "mission_lost" ); }
void CNoSurvivorsAwardTemplate::FireGameEvent( IGameEvent *event )
{
	CTerrorPlayer *player = GetPlayer();
	if ( player && player->GetTeamNumber() == TEAM_INFECTED && IsEvent( event, "mission_lost" ) ) EarnAward( 102 );
}
