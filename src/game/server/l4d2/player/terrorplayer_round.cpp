//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "recipientfilter.h"
#include "datacache/imdlcache.h"
#include "tier0/icommandline.h"
#include "tier0/memdbgon.h"

void CTerrorPlayer::ShowRoundStats()
{
	// S2FCAA0. Original single-user filter is made reliable before the empty message.
	if ( IsBot() || CommandLine()->FindParm( "-makedevshots" ) ) return;
	CSingleUserRecipientFilter filter( this );
	filter.MakeReliable();
	UserMessageBegin( filter, "ShowStats" );
	MessageEnd();
}
void CTerrorPlayer::PrepareForFinaleLoss()
{
	// S2652C0. EnableControl(false) adds FL_FROZEN; it is not an observer/FOV setter.
	SetAbsVelocity( vec3_origin );
	EnableControl( false );
	ShowViewPortPanel( "specmenu", false );
	ShowViewPortPanel( "specgui", false );
	ShowViewPortPanel( "spawnmode_bar", false );
	ShowViewPortPanel( "overview", false );
	CBaseEntity *camera = gEntList.FindEntityByName( NULL, "camera_finale_loss" );
	if ( !camera ) camera = gEntList.FindEntityByName( NULL, "camera_finale" );
	if ( camera )
	{
		m_Local.m_bDrawViewmodel = false;
		SetViewEntity( camera );
		m_Local.m_iHideHUD |= 0x77B;
	}
	if ( GetTeamNumber() == TEAM_INFECTED ) ReleaseTongue( false );
}
void CTerrorPlayer::OnMissionLost()
{
	// S316DA0. SavedStates owns this SAME15957 marker, including replacement snapshots.
	GetMusic()->OnMissionLost();
	m_hasPlayedSpawnMusic = false;
	if ( GetTeamNumber() != TEAM_INFECTED || GetZombieClass() != 8 || !IsAlive() || !( GetFlags() & FL_ONGROUND ) ) return;
	CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
	CUtlVector< CTerrorPlayer * > tanks;
	CollectTerrorPlayers( tanks, TEAM_INFECTED, 2, 3, 8 );
	int emote = 2;
	Activity victory = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_HULK_VICTORY );
	// Only the FIRST TWO collected Tanks are examined, and only when count>1.
	if ( tanks.Count() > 1 && ( tanks[0]->GetTerrorAnimState()->GetTerrorActivity() == victory || tanks[1]->GetTerrorAnimState()->GetTerrorActivity() == victory ) ) emote = 3;
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_TANK_SPECIAL, emote );
}
void CTerrorPlayer::FreezeForScavengeRound()
{
	// S286670/2866E0/286750. The L4D2 FL layout preserves frozen/godmode
	// and opaque native bit31 without inventing that bit's enum spelling.
	// Actual SurvivorBot3963CD / existing SimulatePlayerBot reads this bit.
	AddFlag( FL_FROZEN | FL_GODMODE | (int)0x80000000u );
	m_Local.m_iHideHUD |= 0x149;
	// Native2FCA40 differs from ShowRoundStats: no -makedevshots veto.
	if ( !IsBot() )
	{
		CSingleUserRecipientFilter filter( this );
		filter.MakeReliable();
		UserMessageBegin( filter, "BlurFade" );
		MessageEnd();
	}
	// Native16C700's final Rumble branch is unreachable in the pinned PC
	// build: actual VEngineServer022 slot126/+504 ->engine1319E0 is
	// XOR AL,AL; RET (m4/damage_handoff2C10:52-56, scenarioR28).
	// Do NOT call SDK RumbleEffect: its extra IsAlive guard is not native.
}
void CTerrorPlayer::OnScavengeRoundHalftime()
{
	GetMusic()->OnScavengeRoundHalftime(); // Native286670, before flags/HUD/BlurFade.
	FreezeForScavengeRound();
}
void CTerrorPlayer::OnScavengeRoundEnd()
{
	GetMusic()->OnScavengeRoundEnd(); // Native2866E0.
	FreezeForScavengeRound();
}
void CTerrorPlayer::OnScavengeMatchEnd()
{
	GetMusic()->OnMissionWon(); // Native286750 ->316D80.
	UpdateMissionStats(); // Same real30F4B0 statistics/report owner, before flags.
	FreezeForScavengeRound();
}
void CTerrorPlayer::PrepareForRoundRestart()
{
	// Actual inline per-player2E0650 sequence (no added connected gate), not either
	// RoundRespawn virtual and not ClearUseEntity's extra +USE callbacks.
	SetParent( NULL, -1 );
	if ( m_hUseEntity.Get() != NULL ) m_hUseEntity = NULL; // Stale unresolved handle is retained.
	OnUseEntityChanged(); // Existing sole317BF0 timer13256/mounted bits13264/13265.
	ResetNumSpawns(); // Same actual CS9200, no second spawn counter.
	m_bTeamChanged = false; // Same actual CS9208, constructor/ChangeTeam/Spawn owner.
	if ( GetTeamNumber() == TEAM_INFECTED || GetTeamNumber() == TEAM_SURVIVOR )
		AddSolidFlags( FSOLID_NOT_SOLID ); // Native416 is Collision384+32, NOT effects204.
	const color32 clear = { 0, 0, 0, 0 };
	UTIL_ScreenFade( this, clear, 0.01f, 0.0f, FFADE_MODULATE | FFADE_PURGE );
}

void CTerrorPlayer::OnTankSpawned( CTerrorPlayer *tank )
{
	// Native3176F0. All admitted actors are notified; only dead/ghost actors
	// update the actual scrimmage owner. No alive/team/type inference.
	if ( !tank || tank->GetTeamNumber() != TEAM_INFECTED || tank->GetZombieClass() != 8 || ( IsAlive() && !IsGhost() ) ) return;
	if ( m_scrimmageType == 1 )
	{
		CTerrorPlayer *highest = FindHighestFlowSurvivor( 0, 0 );
		if ( highest )
		{
			const float firstFlow = (float)highest->GetFlowDistance( 0 );
			bool update = GetLineOfScrimmageRange() + (double)firstFlow > (double)m_flLineOfScrimmageFlow;
			if ( !update )
			{
				highest = FindHighestFlowSurvivor( 0, 0 ); // Original calls selector AGAIN on this OR arm.
				if ( highest )
				{
					const float secondFlow = (float)highest->GetFlowDistance( 0 );
					update = (double)m_flRearLineOfScrimmageFlow > (double)secondFlow - GetLineOfScrimmageRange();
				}
			}
			if ( update )
			{
				UTIL_LogPrintf( "%s is updating line of scrimmage at tank battle start\n", GetPlayerName() );
				UpdateLineOfScrimmage( -1.0f, -1.0f );
			}
		}
	}
	else if ( m_scrimmageType == 2 )
	{
		UTIL_LogPrintf( "%s is updating line of scrimmage at tank battle start\n", GetPlayerName() );
		UpdateScrimmageSphere();
		if ( m_scrimmageStartTime.Get() != 0.0f ) m_scrimmageStartTime = 0.0f; // Original retains -0, clears NaN.
	}
}
