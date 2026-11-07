//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "l4d2/director/infected_population.h"
#include "l4d2/infected/infected_activities.h"
#include "l4d2/infected/common/infected_damage_cutouts.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/weapons/terror_weapon.h"
#include "nav_mesh.h"
#include "l4d2/nav/terror_nav_area.h"
#include "NextBot/NextBotManager.h"
#include "dt_utlvector_send.h"
#include "particle_parse.h"
#include "collisionproperty.h"

#include "l4d2/director/terror_director.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/gamerules/mapinfo.h"
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/weapons/terror_melee_weapon_parse.h"
#include "l4d2/infected/common/infected_behavior.h"
#include "NextBot/Path/NextBotPath.h"
#include "nav_ladder.h"
#include "debugoverlay_shared.h"
#include "ipredictionsystem.h"
#include "ammodef.h"
#include "util_shared.h"
#include "datacache/imdlcache.h"
#include "precache_register.h"
#include "activitylist.h"
#include "l4d2/terror_message_text.h"
#include "l4d2/infected/common/infected_console_server_cvars.inc"
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "l4d2/infected/common/infected_path_cost.h"
#include "world.h"
#include "ndebugoverlay.h"
#endif
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( infected, Infected );
#ifndef L4D2
PRECACHE_REGISTER( infected );
#endif

ConVar z_health( "z_health", "50", 0x00004004, "Zombie max health" );
ConVar z_infected_invuln( "z_infected_invuln", "0", 0x00004004 );
ConVar sv_infected_scale_min( "sv_infected_scale_min", "1", 0x00000006 );
ConVar sv_infected_scale_max( "sv_infected_scale_max", "1", 0x00000006 );
ConVar z_randomskins( "z_randomskins", "1", 0x00004004, "Set to zero to disable random skins on infected" );
ConVar z_non_head_damage_factor_easy( "z_non_head_damage_factor_easy", "2", 0x00004004, "Damage to zombie from non-headshots is multiplied by this factor" );
ConVar z_non_head_damage_factor_normal( "z_non_head_damage_factor_normal", "1.0", 0x00004004, "Damage to zombie from non-headshots is multiplied by this factor" );
ConVar z_non_head_damage_factor_hard( "z_non_head_damage_factor_hard", "0.75", 0x00004004, "Damage to zombie from non-headshots is multiplied by this factor" );
ConVar z_non_head_damage_factor_expert( "z_non_head_damage_factor_expert", "0.5", 0x00004004, "Damage to zombie from non-headshots is multiplied by this factor" );
ConVar z_non_head_damage_factor_multiplier( "z_non_head_damage_factor_multiplier", "1.0", 0x00000006, "Damage to zombie from non-headshots is multiplied by this factor. Used in realism mode." );
ConVar z_use_next_difficulty_damage_factor( "z_use_next_difficulty_damage_factor", "0", 0x00000006, "If set, uses the next higher difficulty mode for the purposes of damage factoring." );
ConVar z_head_damage_causes_wounds( "z_head_damage_causes_wounds", "0", 0x00000006, "Head damage with any weapon can cause wounds. Used in realism mode." );
ConVar z_gib_force_factor( "z_gib_force_factor", "10.0", 0x00004004 );
// Retail's bit 19 has no established target flag-name equivalent.
ConVar z_skip_wounds( "z_skip_wounds", "0", 0x00080004 );
ConVar z_mute_infected( "z_mute_infected", "0", 0x00004004, "If nonzero, common infected will not growl, yell, etc" );
ConVar z_max_neighbor_range( "z_max_neighbor_range", "200", 0x00004004, "Max range for neighbor collection for avoidance" );
static ConVar sv_infected_ceda_vomitjar_probability( "sv_infected_ceda_vomitjar_probability", "0.1", 0x00004004 );
static ConVar sv_infected_riot_control_tonfa_probability( "sv_infected_riot_control_tonfa_probability", "0.15", 0x00004004 );
static ConVar sv_infected_police_tonfa_probability( "sv_infected_police_tonfa_probability", "0.03", 0x00004004 );
static ConVar sv_fallen_survivor_health_multiplier( "sv_fallen_survivor_health_multiplier", "20.0", 0x00004004 );
static ConVar z_randombodygroups( "z_randombodygroups", "1", 0x00004004, "Set to zero to disable random body groups on infected" );

extern ConVar mp_gamemode;
extern ConVar z_population;
extern ConVar mix_dynamic_max_CI_emitters;
extern ConVar mix_dynamic_cull_max_CI_emitters;
extern ConVar z_mob_spawn_max_size;
static ConVar z_avoid_max_range( "z_avoid_max_range", "100", 0x00004004 );
static ConVar z_avoid_min_range( "z_avoid_min_range", "30", 0x00004004 );
static ConVar z_avoid_power( "z_avoid_power", "10", 0x00004004 );
static ConVar z_debug_avoid( "z_debug_avoid", "0", 0x00004004 );
ConVar z_gib_explosion_force_factor( "z_gib_explosion_force_factor", "0.1", 0x00004004 );
ConVar z_fatal_blast_incineration_probability( "z_fatal_blast_incineration_probability", "0", 0x00000006 );
ConVar z_fatal_blast_min_ragdolls( "z_fatal_blast_min_ragdolls", "2", 0x00000006 );
ConVar z_fatal_blast_max_ragdolls( "z_fatal_blast_max_ragdolls", "16", 0x00000006 );
ConVar z_shotgun_bonus_damage_range( "z_shotgun_bonus_damage_range", "100", 0x00004004,
    "A shotgun pellet hitting a common infected within this distance will get the z_shotgun_bonus_damage_multiplier bonus." );
ConVar z_shotgun_bonus_damage_multiplier( "z_shotgun_bonus_damage_multiplier", "5", 0x00004004,
    "The multiplier to damage from a shotgun pellet within z_shotgun_bonus_damage_range range." );

BEGIN_SEND_TABLE_NOBASE( InfectedAnimationLayer, DT_InfectedAnimationLayer )
	SendPropInt( SENDINFO_NOCHECK( m_nSequence ), 12, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO_NOCHECK( m_flStartTime ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO_NOCHECK( m_nOrder ), 4, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO_NOCHECK( m_bLooping ) ),
END_SEND_TABLE()

static void SendProxy_InfectedAngle( const SendProp *prop, const void *structure, const void *data, DVariant *out, int element, int objectID )
{
	Infected *infected = const_cast< Infected * >( static_cast< const Infected * >( structure ) );
	const QAngle *angles;
	int component = ( static_cast< const float * >( data ) - infected->GetLocalAngles().Base() );
	if ( infected->UseStepSimulationNetworkAngles( &angles ) )
		out->m_Float = anglemod( ( *angles )[component] );
	else
		out->m_Float = anglemod( *static_cast< const float * >( data ) );
}

IMPLEMENT_SERVERCLASS_ST( Infected, DT_Infected )
	SendPropExclude( "DT_BaseAnimatingOverlay", "overlay_vars" ),
	SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),
	SendPropExclude( "DT_BaseAnimating", "m_flPlaybackRate" ),
	SendPropExclude( "DT_BaseAnimating", "serveranimdata" ),
	SendPropFloat( SENDINFO( m_sequenceStartTime ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_nWaterLevel ), 2, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_fFlags ), 10, SPROP_UNSIGNED | SPROP_CHANGES_OFTEN ),
	SendPropEHandle( SENDINFO( m_clientLookatTarget ) ),
	SendPropFloat( SENDINFO( m_clientLeanYaw ), 8, SPROP_CHANGES_OFTEN, -90.0f, 90.0f ),
	SendPropUtlVector( SENDINFO_UTLVECTOR( m_clientAnimatedOverlay ), 6,
		SendPropDataTable( NULL, 0, &REFERENCE_SEND_TABLE( DT_InfectedAnimationLayer ) ) ),
	SendPropExclude( "DT_BaseAnimating", "m_nResetEventsParity" ),
	SendPropExclude( "DT_BaseAnimating", "m_nMuzzleFlashParity" ),
	SendPropExclude( "DT_BaseFlex", "m_flexWeight" ),
	SendPropExclude( "DT_BaseFlex", "m_blinktoggle" ),
	SendPropExclude( "DT_BaseEntity", "m_nRenderMode" ),
	SendPropExclude( "DT_BaseEntity", "m_fEffects" ),
	SendPropExclude( "DT_BaseEntity", "m_flElasticity" ),
	SendPropExclude( "DT_BaseEntity", "m_flShadowCastDistance" ),
	SendPropExclude( "DT_BaseEntity", "m_hOwnerEntity" ),
	SendPropExclude( "DT_BaseEntity", "m_hMoveParent" ),
	SendPropExclude( "DT_BaseEntity", "m_iParentAttachment" ),
	SendPropExclude( "DT_BaseEntity", "m_MoveType" ),
	SendPropExclude( "DT_BaseEntity", "m_MoveCollide" ),
	SendPropExclude( "DT_BaseEntity", "m_iTextureFrameIndex" ),
	SendPropExclude( "DT_BaseEntity", "predictable_id" ),
	SendPropExclude( "DT_BaseEntity", "m_bAlternateSorting" ),
	SendPropExclude( "DT_BaseEntity", "m_angRotation" ),
	SendPropExclude( "DT_BaseAnimating", "m_nHitboxSet" ),
	SendPropExclude( "DT_BaseAnimating", "m_hLightingOrigin" ),
	SendPropExclude( "DT_BaseAnimating", "m_flModelWidthScale" ),
	SendPropExclude( "DT_BaseAnimating", "m_fadeMinDist" ),
	SendPropExclude( "DT_BaseAnimating", "m_fadeMaxDist" ),
	SendPropExclude( "DT_BaseAnimating", "m_flFadeScale" ),
	SendPropExclude( "DT_BaseCombatCharacter", "bcc_localdata" ),
	SendPropExclude( "DT_BaseCombatCharacter", "m_hActiveWeapon" ),
	SendPropExclude( "DT_BaseEntity", "m_vecOrigin" ),
	SendPropVectorXY( SENDINFO( m_vecOrigin ), -1, SPROP_COORD_MP | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_OriginXY ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecOrigin, 2 ), -1, SPROP_COORD_MP | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_OriginZ ),
	SendPropFloat( SENDINFO_VECTORELEM( m_angRotation, 0 ), 8, SPROP_ROUNDDOWN, 0.0f, 360.0f, SendProxy_InfectedAngle ),
	SendPropFloat( SENDINFO_VECTORELEM( m_angRotation, 1 ), 8, SPROP_ROUNDDOWN | SPROP_CHANGES_OFTEN, 0.0f, 360.0f, SendProxy_InfectedAngle ),
	SendPropFloat( SENDINFO_VECTORELEM( m_angRotation, 2 ), 8, SPROP_ROUNDDOWN, 0.0f, 360.0f, SendProxy_InfectedAngle ),
	SendPropVector( SENDINFO( m_gibbedLimbForce ), -1, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_originalBody ), 32 ),
	SendPropBool( SENDINFO( m_mobRush ) ),
	SendPropBool( SENDINFO( m_bIsBurning ) ),
	SendPropInt( SENDINFO( m_iRequestedWound1 ), 32 ),
	SendPropInt( SENDINFO( m_iRequestedWound2 ), 32 ),
	SendPropInt( SENDINFO( m_nFallenFlags ), 32 ),
END_SEND_TABLE()


// S:0x406800. Keep this draw on the actor/global stream, not weapon RNG.
Infected::Infected()
{
	SetBloodColor( BLOOD_COLOR_RED );
	m_potentiallyVisibleTimer.Invalidate();
	m_nInfectedFlags = 0;
	m_nFallenFlags = 0;
	m_locomotion = NULL;
	m_body = NULL;
	m_intention = NULL;
	m_vision = NULL;
	m_bShoveKilled = false;
	m_burnTimer.Invalidate();
	m_flActorRandomValue = RandomFloat( 0.0f, 1.0f );
	m_bHasStartedDying = false;
	m_nLastHitGroupFrame = 0;
	m_iRequestedWound1 = -1;
	m_iRequestedWound2 = -1;

	// S406800's four unused countdowns are elided only after the recorded census.
	UseClientSideAnimation();
	m_clientAnimatedOverlay.SetCount( 6 );
	SetNumAnimOverlays( 6 );
	for ( int i = 0; i < m_clientAnimatedOverlay.Count(); ++i )
	{
		InfectedAnimationLayer &layer = m_clientAnimatedOverlay[i];
		layer.m_nSequence = 0;
		layer.m_flStartTime = 0.0f;
		layer.m_nOrder = 6;
		layer.m_bLooping = false;
		layer.m_flPreviousCycle = 0.0f;
	}

	// S406800: raw world handle0, conditional on resolved-pointer inequality.
	// If slot0/serial0 does not resolve, the member's initial invalid handle stays.
	EHANDLE world;
	world.Init( 0, 0 );
	CBaseEntity *worldEntity = world.Get();
	if ( m_clientLookatTarget.Get() != worldEntity )
		m_clientLookatTarget.GetForModify().Init( 0, 0 );
	m_clientLeanYaw = 0.0f;
	m_bDecapitated = false;
	m_bShoveImmune = false;
}

Infected::~Infected()
{
	delete m_locomotion;
	delete m_body;
	delete m_intention;
	delete m_vision;
}

// S:0x3FDAE0 constructs the components in this order. The native component
// constructor registers each in INextBot's responder/update list.
void Infected::CreateComponents() const
{
	Infected *me = const_cast< Infected * >( this );
	if ( !m_locomotion )
		m_locomotion = new ZombieBotLocomotion( me );
	if ( !m_body )
		m_body = new ZombieBotBody( me );
	if ( !m_intention )
		m_intention = new InfectedIntention( me );
	if ( !m_vision )
		m_vision = new ZombieBotVision( me );
}

ZombieBotLocomotion *Infected::GetLocomotionInterface() const { return m_locomotion; }
ZombieBotBody *Infected::GetBodyInterface() const { return m_body; }
ZombieBotVision *Infected::GetVisionInterface() const { return m_vision; }
InfectedIntention *Infected::GetIntentionInterface() const { return m_intention; }




// S402F40: actual upstream resource precache, never a Spawn fallback.
void Infected::Precache()
{
	BaseClass::Precache();
	if ( GetMaxHealth() != 0 )
		return;

	PrecacheInfectedModels();
	PrecacheModel( "models/infected/gibs/gibs.mdl" );
	PrecacheModel( "models/infected/cim_fallen_survivor_pocket02.mdl" );
	PrecacheModel( "models/infected/cim_fallen_survivor_pocket01.mdl" );
	PrecacheModel( "models/infected/cim_fallen_survivor_pocket03.mdl" );
	PrecacheScriptSound( "Zombie.Sleeping" );
	PrecacheScriptSound( "Zombie.Wander" );
	PrecacheScriptSound( "Zombie.BecomeEnraged" );
	PrecacheScriptSound( "Zombie.Rage" );
	PrecacheScriptSound( "Zombie.RageAtVictim" );
	PrecacheScriptSound( "Zombie.BecomeAlert" );
	PrecacheScriptSound( "Zombie.Alert" );
	PrecacheScriptSound( "Zombie.Shoved" );
	PrecacheScriptSound( "Zombie.AttackMiss" );
	PrecacheScriptSound( "Zombie.ClawScrape" );
	PrecacheScriptSound( "Zombie.Die" );
	PrecacheScriptSound( "Zombie.IgniteScream" );
	PrecacheScriptSound( "Zombie.Shot" );
	PrecacheScriptSound( "Zombie.Punch" );
	PrecacheScriptSound( "Zombie.BulletImpact" );
	PrecacheScriptSound( "Zombie.HeadlessCough" );
	PrecacheScriptSound( "MegaMobIncoming" );
	PrecacheScriptSound( "Blood.Spurt" );
	PrecacheScriptSound( "Melee.HitHead" );
	PrecacheScriptSound( "Melee.HitBody" );
	PrecacheScriptSound( "Melee.HitLimb" );
	PrecacheScriptSound( "CEDA.suit.deflate" );
	PrecacheParticleSystem( "blood_atomized" );
	PrecacheParticleSystem( "blood_spray_clown" );
	PrecacheScriptSound( "Clown.Default.RunLeft" );
	PrecacheScriptSound( "Clown.Default.RunRight" );
	PrecacheModel( "error.mdl" );
	ACT_EXP_IDLE = ActivityList_RegisterPrivateActivity( "ACT_EXP_IDLE" );
	ACT_EXP_ANGRY = ActivityList_RegisterPrivateActivity( "ACT_EXP_ANGRY" );
}

static bool IsNativeSpawnBodyPart( const char *name )
{
    return name[0] != 0 && atoi( name + 1 ) != -1 && name[0] != 'D';
}
static void RandomizeSpawnBodygroup( Infected *me, const char *name )
{
    const int group = me->FindBodygroupByName( name );
    int minimumCount = 10000;
    if ( group >= 0 )
    {
        int eligible = 0;
        for ( int i = 0; i < me->GetBodygroupCount( group ); ++i )
            if ( IsNativeSpawnBodyPart( me->GetBodygroupPartName( group, i ) ) )
                ++eligible;
        minimumCount = MIN( minimumCount, eligible );
    }
    if ( minimumCount == 0 )
        return; // Native387F0 exits before RNG for an existing all-ineligible group.
    const int ordinal = RandomInt( 1, minimumCount );
    if ( group < 0 )
        return; // Missing group STILL consumes RandomInt(1,10000), native empty group-list case.
    int count = 0;
    for ( int i = 0; i < me->GetBodygroupCount( group ); ++i )
    {
        if ( IsNativeSpawnBodyPart( me->GetBodygroupPartName( group, i ) ) )
            ++count;
        if ( count == ordinal )
        {
            me->SetBodygroup( group, i );
            return;
        }
    }
}
const unsigned int *GetInfectedColorPalette( int *count )
{
	static const unsigned int colors[] =
	{
		0xffffffff, 0xffbfbfbf, 0xff7f7f7f, 0xff3f3f3f, 0xff9b7d7d, 0xffbf8c7f, 0xff9999b2,
		0xff59597f, 0xff7fbfbf, 0xff014c66, 0xff4c667f, 0xff264c26, 0xff26264c, 0xff4c2626
	};
	if ( count ) *count = ARRAYSIZE( colors );
	return colors;
}

#pragma float_control( precise, on, push )
void Infected::Spawn()
{
	// Native primary1412 is the genuine virtual component factory (Witch overrides).
	CreateComponents();
	const char *population = z_population.GetString();
	CNavArea *area = TheNavMesh->GetNavArea( GetPosition(), 120.0f );
	if ( area && area->GetPlace() != UNDEFINED_PLACE )
		population = TheNavMesh->PlaceToName( area->GetPlace() );

	const char *model;
	if ( GetModelIndex() > 0 )
	{
		model = STRING( GetModelName() );
		if ( !model )
			model = ""; // S402516..402529: genuine explicit-model NULL-name result.
	}
	else
	{
		model = SelectInfectedModel( population );
		if ( !model )
		{
			UTIL_Remove( this );
			return;
		}
	}
	SetModel( model );
	if ( GetGender() == 14 )
	{
		TheZombieManager->OnFallenSurvivorSpawned();
		if ( !TheZombieManager->CanSpawnFallenSurvivor() )
		{
			UTIL_Remove( this );
			return;
		}
	}

	BaseClass::Spawn();
	m_flFadeScale = 0.0f; // S05FA70 is the real inherited fade-scale field setter.
	if ( GetZombieClass() != 2 )
		SetModelScale( RandomFloat( sv_infected_scale_min.GetFloat(), sv_infected_scale_max.GetFloat() ), 0.0f );
	CollisionProp()->SetSurroundingBoundsType( USE_SEQUENCE_BOUNDS );
	SetHealth( (int)z_health.GetFloat() );
	SetMaxHealth( (int)z_health.GetFloat() );
	m_takedamage = DAMAGE_YES;
	m_mobRush = false;
	m_bIsBurning = false;
	m_bAmbientMob = false;

	// Native S4026CF..402F1F lock always exists, regardless of SDK macro modes.
	CMDLCacheCriticalSection mdlCacheLock( mdlcache );
	CStudioHdr *studio = GetModelPtr();
	if ( studio && studio->IsValid() && z_randomskins.GetBool() )
	{
		if ( z_infected_damage_cutouts.GetBool() )
			m_nSkin = 4 * RandomInt( 0, ( studio->numskinfamilies() - 1 ) / 4 );
		else
			RandomInt( 0, studio->numskinfamilies() - 1 ); // Native unused draw; no assignment.
	}
	if ( z_randombodygroups.GetBool() )
		RandomizeSpawnBodygroup( this, "Head" );
	if ( z_randombodygroups.GetBool() )
		RandomizeSpawnBodygroup( this, "UpperBody" );
	if ( z_randombodygroups.GetBool() )
		RandomizeSpawnBodygroup( this, "LowerBody" );

	// S401360: keep original fourteen-entry order and packed RGBA bytes.
	int colorCount;
	const unsigned int *colors = GetInfectedColorPalette( &colorCount );
	const unsigned int color = colors[RandomInt( 0, colorCount - 1 )];
	SetRenderColor( color & 0xff, ( color >> 8 ) & 0xff, ( color >> 16 ) & 0xff, color >> 24 );
	SetBlocksLOS( false );
	ChangeTeam( TEAM_INFECTED );
	m_bIsInPlayerAAS = false;
	for ( int i = 0; i < 6; ++i )
	{
		CAnimationLayer *layer = GetAnimOverlay( i );
		if ( layer )
			layer->Init( this );
	}
	SetCalmExpression( this );
	m_originalBody = m_nBody;
	AddFlag( FL_NPC );
	AddFlag( FL_AIMTARGET );
	m_burnTimer.Invalidate();
	m_nLastHitGroupFrame = 0;
	if ( GetZombieClass() == 0 )
		m_nInfectedFlags |= TheDirector->GetInfectedFlags();
	ApplyUncommonSpawnRules();
	m_nInfectedFlags |= 0x40;
	m_vocalizationTimer.Start( RandomFloat( 1.0f, 2.0f ) );
	ApplyUncommonDifficultyHealth();
	m_hITSource = NULL;
	m_itTimer.Invalidate();
}
#pragma float_control( pop )

class CollectInfectedNeighbors
{
public:
	CollectInfectedNeighbors( Infected *me, CUtlVector< EHANDLE > &neighbors, float rangeSquared ) :
		m_me( me ), m_neighbors( neighbors ), m_rangeSquared( rangeSquared ) {}

	bool operator()( INextBot *bot )
	{
		Infected *other = dynamic_cast< Infected * >( bot->GetEntity() );
		if ( other && !m_me->IsSelf( other ) && ( other->GetAbsOrigin() - m_me->GetAbsOrigin() ).LengthSqr() < m_rangeSquared )
			m_neighbors.AddToTail( other );
		return true;
	}
private:
	Infected *m_me;
	CUtlVector< EHANDLE > &m_neighbors;
	float m_rangeSquared;
};

void Infected::UpdateNeighbors()
{
	// S:0x404A10 -> 0x404170. This timer consumes a global draw even in
	// the one-common encounter, when the real scan finds no neighbors.
	if ( m_neighborScanTimer.IsElapsed() )
	{
		m_neighborScanTimer.Start( RandomFloat( 0.5f, 1.0f ) );
		m_neighbors.RemoveAll();
		float range = z_max_neighbor_range.GetFloat();
		CollectInfectedNeighbors collect( this, m_neighbors, range * range );
		TheNextBots().ForEachBot( collect );
	}
}

void Infected::Update()
{
	INextBot::Update();
	UpdateCullingVisibility();
	UpdateNeighbors();
	PublishAnimationState();
	UpdatePopulation();
}

bool Infected::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	// S3FF270: common infected use the infected-team movement contents.
	if ( collisionGroup == COLLISION_GROUP_PLAYER_MOVEMENT &&
		z_avoidteammates.GetBool() && !( contentsMask & CONTENTS_TEAM1 ) )
		return false;
	return BaseClass::ShouldCollide( collisionGroup, contentsMask );
}

bool Infected::IsEnemy( const CBaseEntity *subject ) const
{
	return subject && ( subject->GetTeamNumber() == 2 || subject->GetTeamNumber() == 4 );
}

bool Infected::IsFriend( const CBaseEntity *subject ) const
{
	return subject && subject->GetTeamNumber() == 3;
}

int Infected::BloodColor() { return BLOOD_COLOR_RED; }

// Existing server/l4d2/infected.cpp owner. S401E10..401E6A, inherited by Witch.
// Old sequence is genuinely passed by S36AB0 but ignored by this override.
// Reuse SOLE m_sequenceStartTime7388, DT_Infected index5 at86EAC0, not a new clock.
void Infected::OnSequenceChanged( int oldSequence )
{
	// The reference remains live across the genuine network-change callback,
	// as native401E36/401E5C reload current time after notification.
	m_sequenceStartTime = gpGlobals->curtime;
}

#pragma float_control( precise, on, push )
void Infected::EmitVocalization( const char *sound, bool force )
{
    if ( z_mute_infected.GetBool() || ( !force && !( gpGlobals->curtime >= m_vocalizationTimer.m_timestamp ) ) )
        return;
    const float maxEmitters = (float)mix_dynamic_max_CI_emitters.GetInt();
    if ( TheDirector->GetDirectorOptionFloat( "MobMaxSize", z_mob_spawn_max_size.GetFloat() ) > maxEmitters &&
        mix_dynamic_cull_max_CI_emitters.GetBool() )
        CBaseEntity::EmitSound( m_vocalizationRecipients, entindex(), sound );
    else
        EmitSound( sound );
    m_vocalizationTimer.Start( RandomFloat( 1.0f, 2.0f ) );
}
#pragma float_control( pop )

// Insert as replacements in server/l4d2/infected.cpp, not as another TU.
// Native S404C10, S3FED30 and S405D90. See integration_packet.txt.
#pragma float_control( precise, on, push )

int Infected::OnTakeDamage( const CTakeDamageInfo &info )
{
	const unsigned int damageType = (unsigned int)info.GetDamageType();
	if ( ( damageType & 0x02000000u ) && GetZombieClass() != 7 )
		OnShoved( info.GetInflictor() );

	if ( damageType & 0x01000000u )
	{
		if ( info.GetDamage() >= (float)GetHealth() )
		{
			const InfectedDamageCutoutSet *set = GetInfectedDamageCutouts( GetGender() );
			if ( set->m_ExplosionCutout.Count() > 0 )
			{
				SelectExplosionWound();
				m_gibbedLimbForce = info.GetDamageForce();
			}
		}
		if ( !UTIL_IsLowViolence() && RandomInt( 0, 10 ) <= 4 )
		{
			QAngle angles;
			VectorAngles( Vector( 0.0f, 0.0f, -1.0f ), angles );
			DispatchParticleEffect( "blood_impact_headshot_01", info.GetDamagePosition(), angles );
		}
	}

	if ( ( m_nInfectedFlags & 0x20 ) && ( damageType & 0x20200002u ) &&
		!( damageType & DMG_BLAST ) && info.GetAttacker() && IsRiotArmorFacingDamage( info ) )
	{
		DispatchParticleEffect( "impact_armor", info.GetDamagePosition(), GetAbsAngles() );
		const Vector &origin = GetAbsOrigin();
		CBaseEntity *attacker = info.GetAttacker();
		// S404C10: skip only a vanished entity's dependent work.
		if ( attacker )
		{
			const Vector &attackerOrigin = attacker->GetAbsOrigin();
			bool shove = false;
			if ( damageType & 0x20000000u )
			{
				const Vector delta = attackerOrigin - origin;
				const float rangeSquared = ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z;
				shove = rangeSquared <= 207936.0f;
			}
			if ( ( shove || info.GetDamage() >= 90.0f ) && !IsShoved() )
				OnShoved( info.GetAttacker() );
		}
		return 0;
	}
	if ( ( m_nInfectedFlags & 0x800 ) && !m_bDecapitated &&
		( damageType & 0x20200002u ) && !( damageType & 0x41000040u ) && info.GetAttacker() )
	{
		if ( !IsShoved() )
			OnShoved( info.GetAttacker() );
		return 0;
	}
	return BaseClass::OnTakeDamage( info );
}

void Infected::EmitTraceAttackBlood( float amount, const CTakeDamageInfo &info, const Vector &direction, trace_t *trace )
{
	// S3FED30: the riot helper suppresses blood, not the rest of TraceAttack.
	// Unlike S404C10's damage block, this helper does not exclude blast.
	if ( BlocksRiotBlood( info ) || amount == 0.0f )
		return;
	TraceBleed( amount, direction, trace, info.GetDamageType() );
	if ( trace->hitgroup != HITGROUP_HEAD ||
		( (unsigned int)info.GetDamageType() != 0x80000000u && !z_head_damage_causes_wounds.GetBool() ) )
	{
		QAngle angles;
		VectorAngles( -direction, angles );
		DispatchParticleEffect( ( (unsigned int)info.GetDamageType() & 0x20000000u ) ?
			"blood_impact_infected_01_shotgun" : "blood_impact_infected_01",
			trace->endpos, GetAbsVelocity(), angles );
	}
}

void Infected::TraceAttack( const CTakeDamageInfo &info, const Vector &direction, trace_t *trace, CDmgAccumulator *accumulator )
{
	RecordHitGroup( *trace );
	CTakeDamageInfo damage = info;
	const unsigned int incomingType = (unsigned int)info.GetDamageType();
	if ( ( incomingType & DMG_BURN ) && ( incomingType & 0x20000002u ) )
		Ignite( 30.0f, info.GetAttacker() );

	if ( ( incomingType & 0x20000000u ) && info.GetAttacker() )
	{
		bool witchException = false;
		if ( GetZombieClass() == 7 )
		{
			const char *mode = mp_gamemode.GetString();
			const bool mutation12 = !Q_stricmp( mode, "mutation12" );
			const bool mutation8 = !Q_stricmp( mode, "mutation8" );
			witchException = mutation12 || mutation8 || TerrorGameRules()->IsRealismMode();
		}
		if ( !witchException )
		{
			CBaseEntity *attacker = info.GetAttacker();
			// S405D90: do not substitute another subject if the handle vanished.
			if ( attacker )
			{
				const float range = GetRangeTo( attacker );
				const float bonusRange = z_shotgun_bonus_damage_range.GetFloat();
				if ( bonusRange >= range )
				{
					const float fraction = 1.0f - range / bonusRange;
					damage.SetDamage( ( z_shotgun_bonus_damage_multiplier.GetFloat() - 1.0f ) * damage.GetDamage() *
						( fraction * fraction ) + damage.GetDamage() );
				}
			}
		}
	}

	if ( trace->hitgroup == HITGROUP_HEAD )
		damage.AddDamageType( (int)0x40000000u );
	else if ( !( (unsigned int)damage.GetDamageType() & 0x00200000u ) )
	{
		int difficulty = TerrorGameRules()->GetDifficultyLevel();
		if ( z_use_next_difficulty_damage_factor.GetBool() )
			difficulty = clamp( difficulty + 1, 0, 3 );
		switch ( difficulty )
		{
		case 0: damage.ScaleDamage( z_non_head_damage_factor_easy.GetFloat() ); break;
		case 1: damage.ScaleDamage( z_non_head_damage_factor_normal.GetFloat() ); break;
		case 2: damage.ScaleDamage( z_non_head_damage_factor_hard.GetFloat() ); break;
		case 3: damage.ScaleDamage( z_non_head_damage_factor_expert.GetFloat() ); break;
		}
		damage.ScaleDamage( z_non_head_damage_factor_multiplier.GetFloat() );
		const int ammoFlags = GetAmmoDef()->Flags( damage.GetAmmoType() );
		if ( ( GetTeamNumber() != TEAM_INFECTED || GetZombieClass() == 0 ) &&
			( ( ammoFlags & 8 ) || ( !TerrorGameRules()->IsRealismMode() && ( ammoFlags & 4 ) ) ) )
		{
			if ( GetGender() == 14 )
				damage.SetDamage( (float)( GetMaxHealth() >> 1 ) );
			else if ( (float)GetMaxHealth() > damage.GetDamage() )
				damage.SetDamage( (float)GetMaxHealth() );
		}
	}

	const bool fatalBlast = ( incomingType & DMG_BLAST ) && info.GetDamage() * 4.0f > info.GetMaxDamage();
	if ( ( fatalBlast || ( incomingType & 0x05000000u ) ) && GetTeamNumber() == TEAM_INFECTED && GetZombieClass() != 7 )
		damage.SetDamage( (float)GetHealth() );

	CDisablePredictionFiltering disablePredictionFiltering;
	EmitTraceAttackBlood( damage.GetDamage(), damage, direction, trace );
	if ( accumulator )
		accumulator->AccumulateMultiDamage( damage, this );
	else
		AddMultiDamage( damage, this );

	const InfectedDamageCutoutSet *set = GetInfectedDamageCutouts( GetGender() );
	const int explosionCount = set->m_ExplosionCutout.Count();
	CBaseEntity *weaponEntity = damage.GetWeapon();
	CBaseCombatWeapon *combatWeapon = weaponEntity ? weaponEntity->MyCombatWeaponPointer() : NULL;
	// S4061BB: only the invalid native weapon-ID query is skipped for a non-CS weapon.
	CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( combatWeapon );
	if ( weapon && weapon->GetWeaponID() == 19 )
	{
		CTerrorMeleeWeapon *melee = dynamic_cast< CTerrorMeleeWeapon * >( damage.GetWeapon() );
		// S40620F..406214: failed RTTI / absent melee info skips only this decap check.
		const CMeleeWeaponInfo *meleeInfo = melee ? melee->GetMeleeInfo() : NULL;
		if ( meleeInfo && meleeInfo->m_bDecapitates &&
			SelectWoundByDamageType( INFECTED_CUTOUT_DECAPITATE, trace->hitbox ) )
		{
			Vector force;
			if ( damage.GetDamageType() & DMG_SLASH )
			{
				const float lift = RandomFloat( 400.0f, 500.0f );
				force = damage.GetDamageForce() * 0.050000001f;
				force.z += lift;
			}
			else
			{
				force = damage.GetDamageForce() * z_gib_force_factor.GetFloat();
				if ( damage.GetDamageType() & DMG_CLUB )
					force.z += 300.0f;
			}
			m_gibbedLimbForce = force;
			if ( info.GetAttacker() && !m_bDecapitated )
			{
				CBasePlayer *player = ToBasePlayer( info.GetAttacker() );
				if ( player )
				{
					IGameEvent *event = gameeventmanager->CreateEvent( "infected_decapitated" );
					if ( event )
					{
						event->SetInt( "userid", player->GetUserID() );
						event->SetString( "weapon", melee->GetResponseRulesName() );
						gameeventmanager->FireEvent( event );
					}
				}
			}
			m_bDecapitated = true;
			return;
		}
	}

	// S4063EB..4063EF: COMISS/JBE rejects unordered values as well as <=.
	if ( fatalBlast && explosionCount > 0 )
	{
		m_gibbedLimbForce = damage.GetDamageForce() * z_gib_explosion_force_factor.GetFloat();
		static int lastFrame = 0;
		static int ragdollCount = 0;
		if ( lastFrame != gpGlobals->framecount )
		{
			lastFrame = gpGlobals->framecount;
			ragdollCount = 0;
		}
		if ( ragdollCount <= z_fatal_blast_max_ragdolls.GetInt() &&
			( ragdollCount < z_fatal_blast_min_ragdolls.GetInt() ||
			  RandomInt( 0, 100 ) >= z_fatal_blast_incineration_probability.GetInt() ) && !UTIL_IsLowViolence() )
		{
			++ragdollCount;
			SelectExplosionWound();
		}
		else
			UTIL_Remove( this );
		return;
	}

	m_gibbedLimbForce = damage.GetDamageForce() * z_gib_force_factor.GetFloat();
	if ( ( incomingType & 0x01000000u ) && explosionCount > 0 )
	{
		SelectExplosionWound();
		return;
	}
	if ( (unsigned int)damage.GetDamageType() & 0x04000000u )
	{
		const float lift = RandomFloat( 700.0f, 1000.0f );
		Vector force = damage.GetDamageForce();
		force.z = lift + force.z;
		m_gibbedLimbForce = force;
		SelectWoundByDamageType( INFECTED_CUTOUT_CHAINSAW, -1 );
	}
	else if ( (unsigned int)damage.GetDamageType() & 0x00200000u )
	{
		CTerrorMeleeWeapon *melee = dynamic_cast< CTerrorMeleeWeapon * >( info.GetWeapon() );
		if ( melee )
		{
			CBaseEntity *attacker = info.GetAttacker();
			if ( attacker )
			{
				const float yaw = AngleNormalize( attacker->GetAbsAngles().y - GetAbsAngles().y );
				const bool front = yaw > -90.0f && yaw < 90.0f;
				const MeleeAttackInfo_t *attack = melee->GetCurrentMeleeAttack();
				// S406642/S40664D: absent current attack skips only the dependent wound selection.
				if ( attack )
					SelectMeleeWound( trace->hitbox, trace->hitgroup, (unsigned char)damage.GetDamageType(),
						front, attack->m_StartDirection, attack->m_EndDirection );
			}
		}
		if ( damage.GetDamage() > 10.0f )
		{
			switch ( trace->hitgroup )
			{
			case HITGROUP_HEAD: EmitSound( "Melee.HitHead" ); break;
			case HITGROUP_CHEST:
			case HITGROUP_STOMACH: EmitSound( "Melee.HitBody" ); break;
			case HITGROUP_LEFTARM:
			case HITGROUP_RIGHTARM:
			case HITGROUP_LEFTLEG:
			case HITGROUP_RIGHTLEG: EmitSound( "Melee.HitLimb" ); break;
			}
		}
	}
	else if ( (int)damage.GetDamageType() < 0 || ( trace->hitgroup == HITGROUP_HEAD &&
		( ( m_nInfectedFlags & 0x800 ) || z_head_damage_causes_wounds.GetBool() ) ) )
	{
		if ( damage.GetAmmoType() == GetAmmoDef()->Index( "AMMO_TYPE_HUNTINGRIFLE" ) ||
			 damage.GetAmmoType() == GetAmmoDef()->Index( "AMMO_TYPE_SNIPERRIFLE" ) )
		{
			if ( trace->hitbox == 15 || trace->hitbox == 16 )
				trace->hitbox = 14;
		}
		bool fatal = damage.GetDamage() >= (float)GetHealth() ||
			( (unsigned int)damage.GetDamageType() & 0x40000000u ) != 0;
		if ( ( m_nInfectedFlags & 0x10 ) && trace->hitgroup == HITGROUP_HEAD )
			fatal = IsAmbush();
		SelectBulletWound( trace->hitbox, trace->hitgroup, fatal );
	}
}

#pragma float_control( pop )

// S:0x06F930 differs from the SDK's damage-dependent four-trace variant.
// The common's original decal path makes one trace and exactly three draws.
void Infected::TraceBleed( float damage, const Vector &direction, trace_t *trace, int damageType )
{
	if ( BloodColor() == DONT_BLEED || BloodColor() == BLOOD_COLOR_MECH || damage == 0.0f ||
		!( (unsigned int)damageType & 0x020000c7u ) )
		return;
	if ( !IsAlive() )
	{
		if ( GetMaxHealth() <= 0 )
			return;
		m_iMaxHealth -= 1;
	}
	Vector traceDirection = -direction;
	traceDirection.x += RandomFloat( -0.1f, 0.1f );
	traceDirection.y += RandomFloat( -0.1f, 0.1f );
	traceDirection.z += RandomFloat( -0.1f, 0.1f );
	float distance = ( (unsigned int)damageType & 0x02000000u ) ? 384.0f : 172.0f;
	trace_t bloodTrace;
	UTIL_TraceLine( trace->endpos, trace->endpos + traceDirection * -distance,
		MASK_SOLID_BRUSHONLY & ~CONTENTS_GRATE, this, COLLISION_GROUP_NONE, &bloodTrace );
	if ( bloodTrace.fraction != 1.0f )
		UTIL_BloodDecalTrace( &bloodTrace, BloodColor() );
}

void Infected::RecordHitGroup( const trace_t &trace )
{
	// S:0x4013E0 prioritizes head, then arms, then legs within one frame.
	bool replace = true;
	if ( m_nLastHitGroupFrame == gpGlobals->framecount )
	{
		switch ( LastHitGroup() )
		{
		case HITGROUP_HEAD:
			replace = false;
			break;
		case HITGROUP_LEFTARM:
		case HITGROUP_RIGHTARM:
			replace = trace.hitgroup == HITGROUP_HEAD;
			break;
		case HITGROUP_LEFTLEG:
		case HITGROUP_RIGHTLEG:
			replace = trace.hitgroup == HITGROUP_HEAD || trace.hitgroup == HITGROUP_LEFTARM || trace.hitgroup == HITGROUP_RIGHTARM;
			break;
		}
	}
	if ( replace )
	{
		SetLastHitGroup( trace.hitgroup );
		m_nLastHitGroupFrame = gpGlobals->framecount;
		m_nForceBone = trace.physicsbone;
	}
}



void Infected::FireHurtEvent( const CTakeDamageInfo &info )
{
	if ( m_lifeState != LIFE_ALIVE )
		return;
	IGameEvent *event = gameeventmanager->CreateEvent( "infected_hurt" );
	if ( event )
	{
		CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() );
		event->SetInt( "attacker", attacker ? attacker->GetUserID() : 0 );
		event->SetInt( "entityid", entindex() );
		event->SetInt( "hitgroup", ( info.GetDamageType() & ( DMG_BURN | DMG_BLAST ) ) ? 0 : LastHitGroup() );
		event->SetInt( "amount", (int)info.GetDamage() );
		event->SetInt( "type", info.GetDamageType() );
		gameeventmanager->FireEvent( event );
	}
}


void Infected::Event_Killed( const CTakeDamageInfo &info )
{
	if ( z_infected_invuln.GetBool() )
		return;
	DropUncommonItems( info );
	CBaseEntity *attacker = info.GetAttacker();
	if ( attacker && ( attacker->GetTeamNumber() == TEAM_SURVIVOR || attacker->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
	{
		CNavArea *area = GetLastKnownArea();
		if ( area )
			TheNavMesh->IncreaseDangerNearby( TEAM_INFECTED, 1.0f, area, GetAbsOrigin(), 100.0f, 10.0f );
	}
	SetActive( false );
	m_Glow.SetType( 0 );
	TheDirector->OnZombieDeath( this, info );
	L4DGameStats().Event_Kill( this, info );
	NextBotCombatCharacter::Event_Killed( info );
}

void Infected::Event_Dying( const CTakeDamageInfo &info )
{
	// S:0x043880 is an intentional return; InfectedDying owns the handoff.
}

void Infected::Retire( const CTakeDamageInfo &info )
{
	if ( !m_bRetired )
	{
		// S:0x4A94F0 -> 0x03D9A0 -> BecomeRagdollOnClient. Native ownership
		// sets kRenderFxRagdoll and schedules SUB_Remove at curtime + 2.0.
		m_bRetired = NextBotCombatCharacter::BecomeRagdoll( info, info.GetDamageForce() );
	}
}

bool Infected::IsAmbush() const
{
	Activity activity = GetBodyInterface()->GetActivity();
	return activity == ACT_TERROR_IDLE_NEUTRAL || activity == ACT_TERROR_IDLE_ALERT_AHEAD ||
		activity == ACT_TERROR_WALK_NEUTRAL || activity == ACT_TERROR_CROUCH_IDLE_NEUTRAL || activity == ACT_TERROR_SIT_IDLE;
}

void Infected::ShoveKill( CBaseEntity *pusher )
{
	// S:0x400600: an eligible rear/tolerance shove really kills; this is not
	// an anger helper, nor ordinary shove damage.
	if ( pusher && !( m_nInfectedFlags & ( 0x800 | 0x10 ) ) && !m_bShoveImmune )
	{
		m_bShoveKilled = true;
		bool ambush = IsAmbush();
		CTakeDamageInfo damage( pusher, pusher, (float)GetHealth() + 1.0f, DMG_CLUB );
		TakeDamage( damage );
		IGameEvent *event = gameeventmanager->CreateEvent( "melee_kill" );
		if ( event )
		{
			CBasePlayer *player = ToBasePlayer( pusher );
			event->SetInt( "userid", player ? player->GetUserID() : 0 );
			event->SetInt( "entityid", entindex() );
			event->SetBool( "ambush", ambush );
			gameeventmanager->FireEvent( event );
		}
	}
}

// S:0x4034B0. Wound indices are compact parsed indices in the original set.
bool Infected::SetRequestedWound( int wound )
{
	if ( UTIL_IsLowViolence() || z_skip_wounds.GetInt() == 1 )
		return false;
	if ( wound == -1 )
	{
		m_iRequestedWound1 = -1;
		m_iRequestedWound2 = -1;
		return true;
	}
	if ( wound == m_iRequestedWound1 || wound == m_iRequestedWound2 || m_iRequestedWound2 != -1 )
		return false;
	const InfectedDamageCutoutSet *set = GetInfectedDamageCutouts( GetGender() );
	if ( wound < 0 || wound >= set->m_Cutout.Count() )
		return false;
	const InfectedDamageCutout_t &next = set->m_Cutout[wound];
	if ( !next.m_nHitboxCount )
	{
		m_iRequestedWound1 = wound;
		m_iRequestedWound2 = -1;
		return true;
	}
	if ( m_iRequestedWound1 == -1 )
	{
		m_iRequestedWound1 = wound;
		return true;
	}
	const InfectedDamageCutout_t &first = set->m_Cutout[m_iRequestedWound1];
	if ( first.m_nRegionCount >= 2 || next.m_nRegionCount >= 2 )
		return false;
	for ( int i = 0; i < first.m_nExclusionCount; ++i )
		if ( first.m_iExclusion[i] == wound )
			return false;
	m_iRequestedWound2 = wound;
	return true;
}

void Infected::SelectBulletWound( int hitbox, int hitgroup, bool fatal )
{
	// S:0x4050A0. Preserve the fixed-eight candidate list and RNG draw even
	// when the later setter refuses a duplicate/excluded wound.
	if ( m_iRequestedWound1 != -1 && m_iRequestedWound2 != -1 )
		return;
	if ( ( m_nInfectedFlags & 0x800 ) &&
		!( ( hitgroup == HITGROUP_HEAD || ( m_iRequestedWound1 == -1 && m_iRequestedWound2 == -1 ) ) &&
		( hitgroup < HITGROUP_LEFTLEG || hitgroup > HITGROUP_RIGHTLEG ) ) )
		return;
	int candidates[8];
	int count = 0;
	const InfectedDamageCutoutSet *set = GetInfectedDamageCutouts( GetGender() );
	FOR_EACH_VEC( set->m_Cutout, i )
	{
		const InfectedDamageCutout_t &wound = set->m_Cutout[i];
		if ( ( wound.m_bFatal && !fatal ) || !( wound.m_nDamageType & INFECTED_CUTOUT_GUNSHOT ) )
			continue;
		for ( int h = 0; h < wound.m_nHitboxCount; ++h )
			if ( wound.m_iHitbox[h] >= 0 && wound.m_iHitbox[h] == hitbox && count < ARRAYSIZE( candidates ) )
				candidates[count++] = i;
	}
	if ( count )
		SetRequestedWound( candidates[RandomInt( 0, count - 1 )] );
}

const char *Infected::GetWoundDeathActivity( int slot ) const
{
	int wound = slot == 0 ? m_iRequestedWound1.Get() : m_iRequestedWound2.Get();
	const InfectedDamageCutoutSet *set = GetInfectedDamageCutouts( GetGender() );
	return wound >= 0 && wound < set->m_Cutout.Count() ? set->m_Cutout[wound].m_szDeathActivity : "";
}

void Infected::PublishAnimationState()
{
	// S:0x3FFE80 -> 0x3FF500. Preserve the original inactive-record and
	// same-looping-sequence quirks rather than treating every order change
	// or gesture request as a new animation clock.
	for ( int i = 0; i < 6; ++i )
	{
		CAnimationLayer *native = i < GetNumAnimOverlays() ? GetAnimOverlay( i ) : NULL;
		InfectedAnimationLayer &record = m_clientAnimatedOverlay[i];
		if ( native && native->IsActive() && native->m_nOrder != CBaseAnimatingOverlay::MAX_OVERLAYS )
		{
			if ( record.m_nSequence != native->m_nSequence || record.m_bLooping != native->m_bLooping ||
				record.m_nOrder == CBaseAnimatingOverlay::MAX_OVERLAYS ||
				( !record.m_bLooping && record.m_flPreviousCycle > native->m_flCycle ) )
			{
				NetworkStateChanged();
				record.m_nSequence = native->m_nSequence;
				record.m_nOrder = native->m_nOrder;
				record.m_bLooping = native->m_bLooping;
				record.m_flStartTime = gpGlobals->curtime;
			}
			record.m_flPreviousCycle = native->m_flCycle;
		}
		else if ( record.m_nOrder != 6 )
		{
			NetworkStateChanged();
			record.m_nOrder = 6;
		}
	}
}

#pragma float_control( precise, on, push )
void Infected::ApplyUncommonSpawnRules()
{
    switch ( GetGender() )
    {
    case 11:
        m_nInfectedFlags |= 2;
        if ( sv_infected_ceda_vomitjar_probability.GetFloat() > RandomFloat( 0.0f, 1.0f ) )
            m_nFallenFlags |= 1;
        break;
    case 12: m_nInfectedFlags |= 4; break;
    case 13: m_nInfectedFlags |= 8; break;
    case 14: m_nInfectedFlags |= 0x10; break;
    case 15:
        m_nInfectedFlags |= 0x20;
        if ( sv_infected_riot_control_tonfa_probability.GetFloat() > RandomFloat( 0.0f, 1.0f ) )
            m_nFallenFlags |= 1;
        break;
    case 20:
        if ( sv_infected_police_tonfa_probability.GetFloat() > RandomFloat( 0.0f, 1.0f ) )
            m_nFallenFlags |= 1;
        break;
    case 17:
        m_nInfectedFlags |= 0x0A;
        if ( TerrorGameRules()->GetDifficultyLevel() > 0 )
        {
            SetMaxHealth( (int)( (float)GetMaxHealth() * 20.0f ) );
            SetHealth( GetMaxHealth() );
        }
        break;
    }
    if ( m_nInfectedFlags & 0x10 )
    {
        m_nInfectedFlags = ( m_nInfectedFlags & ~0x800 ) | 0x40B;
        SetMaxHealth( (int)( (float)GetMaxHealth() * sv_fallen_survivor_health_multiplier.GetFloat() ) );
        SetHealth( GetMaxHealth() );
        CMapInfo *map = GetMapInfo(); // Native invariant: real owner creates it.
        static const int itemID[] = { 13, 14, 15, 12 };
        for ( int i = 0; i < ARRAYSIZE( itemID ); ++i )
        {
            CDirectorScriptedMode *scripted = TheDirector->GetScriptedMode();
            if ( !scripted->AllowWeaponSpawn( itemID[i] ) || !scripted->AllowFallenSurvivorItem( itemID[i] ) )
                continue;
            bool selected = false;
            switch ( itemID[i] )
            {
            case 13:
                selected = true;
                break;
            case 14:
                {
                    const float density = map->GetParameter( CMapInfo::PIPE_BOMB_DENSITY );
                    selected = density > RandomFloat( 0.0f, 3.0f );
                }
                break;
            case 12:
                if ( scripted->IsEnabled() && scripted->QueryOptionBool( "cm_TempHealthOnly", false ) )
                    break;
                // Native falls through, sharing ID15's random draw and mode veto.
            case 15:
                {
                    const float density = map->GetParameter( CMapInfo::PAIN_PILL_DENSITY );
                    if ( density > RandomFloat( 0.0f, 3.0f ) && !TerrorGameRules()->IsPlayerControlledZombies() )
                        selected = true;
                }
                break;
            }
            if ( selected )
                m_nFallenFlags |= 1 << i;
        }
    }
}

// S402DE4..402E8F. Invoke AFTER vocalization RandomFloat(1,2), not adjacent
// to Jimmy's first multiplier: original consumes the timer draw in between.
void Infected::ApplyUncommonDifficultyHealth()
{
    if ( IsUncommon() && GetGender() != 15 && GetGender() != 14 && TerrorGameRules()->GetDifficultyLevel() > 0 )
    {
        SetMaxHealth( (int)( (float)GetMaxHealth() * 3.0f ) );
        SetHealth( GetMaxHealth() );
    }
}
#pragma float_control( pop )

// S3FE110. Merge into M5ZombiePopulation's actual removal method if it lands
// one; this call is before BaseClass::UpdateOnRemove and is based on TYPE,
// not successful-Spawn latch or flags. No extra per-actor accounting field.
void Infected::UpdateOnRemove()
{
    if ( GetGender() == 14 )
        TheZombieManager->OnFallenSurvivorRemoved( !IsAlive() );
    BaseClass::UpdateOnRemove();
}

// S3FDFA0. This is the float+attacker overload ONLY. Keep inherited
// bool-signature Ignite; a blanket burn-damage early return is not native.
void Infected::Ignite( float lifetime, CBaseEntity *attacker )
{
    if ( !( m_nInfectedFlags & 2 ) )
        BaseClass::Ignite( lifetime, attacker );
}

// S3FEBD0. Main's two minimal undefined-case skips are deliberately local.
#pragma float_control( precise, on, push )
bool Infected::IsRiotArmorFacingDamage( const CTakeDamageInfo &info )
{
    Vector direction;
    if ( info.GetDamageType() & 0x00200000 )
    {
        CBaseEntity *attacker = info.GetAttacker();
        if ( !attacker ) // Standing NULL-re-read rule; original S3FEC15.
            return false;
        direction = WorldSpaceCenter() - attacker->WorldSpaceCenter();
    }
    else
    {
        CBaseEntity *inflictor = info.GetInflictor();
        if ( !inflictor ) // Main: S3FEC88 native NULL dereference -> armor false.
            return false;
        direction = info.GetDamagePosition() - inflictor->WorldSpaceCenter();
    }
    VectorNormalize( direction );
    const int chest = LookupAttachment( "chest" );
    matrix3x4_t transform;
    if ( !GetAttachment( chest, transform ) ) // Main: S3FECE2/3FECE7 failed native output -> armor false.
        return false;
    Vector chestForward;
    MatrixGetColumn( transform, 0, chestForward );
    return DotProduct( chestForward, direction ) < 0.0f;
}

bool Infected::BlocksRiotBlood( const CTakeDamageInfo &info )
{
    return ( m_nInfectedFlags & 0x20 ) &&
        ( info.GetDamageType() & 0x20200002 ) && info.GetAttacker() && IsRiotArmorFacingDamage( info );
}
#pragma float_control( pop )

// S403C80 launch helper: native virtual Drop, then no-cleanup/removeable flag.
// Keeping the x/y draws as separate statements preserves original global RNG.
static void LaunchUncommonItem( Infected *infected, CTerrorWeapon *weapon, bool recordSource )
{
    weapon->SetAbsOrigin( infected->WorldSpaceCenter() );
    Vector velocity;
    velocity.x = RandomFloat( -50.0f, 50.0f );
    velocity.y = RandomFloat( -50.0f, 50.0f );
    velocity.z = 100.0f;
    weapon->Drop( velocity );
    weapon->SetRemoveable( false );
    weapon->CollisionProp()->SetSolidFlags( 152 );
    weapon->SetMoveCollide( MOVECOLLIDE_FLY_BOUNCE ); // Native byte371=1 is movecollide, NOT collisiongroup544.
    if ( recordSource )
        weapon->m_DroppedByInfectedGender = infected->GetGender();
}

void Infected::DropUncommonItems( const CTakeDamageInfo &info )
{
    if ( GetGender() == 11 )
    {
        if ( info.GetDamageType() & DMG_BULLET )
            EmitSound( "CEDA.suit.deflate" );
        if ( m_nFallenFlags & 1 )
        {
            CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( CreateEntityByName( "weapon_vomitjar" ) );
            if ( weapon )
            {
                DispatchSpawn( weapon );
                LaunchUncommonItem( this, weapon, true );
            }
        }
    }
    else if ( ( GetGender() == 15 || GetGender() == 20 ) && ( m_nFallenFlags & 1 ) )
    {
        const MeleeWeaponInfoHandle_t tonfa = MeleeWeaponInfoStore()->GetMeleeWeaponInfoIndex( "tonfa" );
        if ( tonfa != INVALID_MELEE_WEAPON_INFO_HANDLE )
        {
            CTerrorMeleeWeapon *weapon = static_cast< CTerrorMeleeWeapon * >( CreateEntityByName( "weapon_melee" ) );
            if ( weapon )
            {
                weapon->SetMeleeWeaponInfo( tonfa );
                DispatchSpawn( weapon );
                LaunchUncommonItem( this, weapon, false );
            }
        }
    }
    if ( m_nInfectedFlags & 0x10 )
    {
        static const char *const classname[] = { "weapon_molotov", "weapon_pipe_bomb", "weapon_pain_pills", "weapon_first_aid_kit" };
        for ( int i = 0; i < ARRAYSIZE( classname ); ++i )
        {
            if ( !( m_nFallenFlags & ( 1 << i ) ) )
                continue;
            CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( CreateEntityByName( classname[i] ) );
            if ( weapon )
            {
                DispatchSpawn( weapon );
                LaunchUncommonItem( this, weapon, true );
            }
        }
    }
}

void Infected::OnIgnite()
{
    BaseClass::OnIgnite(); // Native403B90 forwards contained responders FIRST.
    m_bIsBurning = true;
    m_nSkin += 2;
    m_burnTimer.Start();
}

bool Infected::IsShoved() const
{
    switch ( LookupTerrorOriginalActivity( GetBodyInterface()->GetActivity() ) )
    {
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD_INTO_WALL:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD_INTO_WALL:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD_INTO_WALL:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_INTO_WALL:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_BACKWARD_FROM_SIT:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_FROM_SIT:
    case TERROR_ORIGINAL_ACT_TERROR_SHOVED_LEFTWARD_FROM_SIT:
        return true;
    default:
        return false;
    }
}

// Exact S2E3550 connectivity/width predicate, not NAV_MESH_CROUCH.
static bool IsNativeNarrowAvoidanceArea( const CNavArea *area )
{
    if ( area->GetAdjacentCount( NORTH ) == 0 )
    {
        return area->GetAdjacentCount( SOUTH ) == 0 && area->GetAdjacentCount( EAST ) > 0 &&
            area->GetAdjacentCount( WEST ) > 0 && area->GetSizeY() < 52.499996f;
    }
    return area->GetAdjacentCount( SOUTH ) > 0 && area->GetAdjacentCount( EAST ) == 0 &&
        area->GetAdjacentCount( WEST ) == 0 && area->GetSizeX() < 52.499996f;
}

#pragma float_control( precise, on, push )
void Infected::UpdateCommonApproachSteering( CBaseEntity *victim, const Path *path )
{
    ILocomotion *mover = GetLocomotionInterface();
    if ( mover->IsClimbingOrJumping() || !mover->IsOnGround() || ( victim && IsRangeLessThan( victim, 250.0f ) ) )
        return;
    float ahead = 0.0f;
    path->MoveCursorToClosestPosition( mover->GetFeet() );
    for ( const Path::Segment *segment = path->GetCursorData().segmentPrior; segment && ahead < 750.0f; segment = path->NextSegment( segment ) )
    {
        if ( segment->ladder )
        {
            const Vector ladderPos = segment->ladder->GetPosAtHeight( mover->GetFeet().z );
            ahead = ( ladderPos - mover->GetFeet() ).Length();
            if ( z_debug_avoid.GetBool() )
                NDebugOverlay::HorzArrow( GetAbsOrigin(), ladderPos, 10.0f, 255, 0, 255, 255, true, 0.1f );
        }
        else if ( segment->area && IsNativeNarrowAvoidanceArea( segment->area ) )
        {
            ahead = (float)sqrt( (double)segment->area->GetDistanceSquaredToPoint( mover->GetFeet() ) );
            if ( z_debug_avoid.GetBool() )
                NDebugOverlay::HorzArrow( WorldSpaceCenter(), segment->area->GetCenter(), 10.0f, 255, 0, 255, 255, true, 0.1f );
        }
        else
        {
            ahead += segment->length;
            continue;
        }
        if ( ahead > 0.0f && ahead < 300.0f )
            return;
        break;
    }
    const CUtlVector< EHANDLE > &neighbors = GetNeighbors();
    if ( neighbors.Count() == 0 )
        return;
    Vector repel( 0.0f, 0.0f, 0.0f );
    const float maximum = z_avoid_max_range.GetFloat();
    const float minimum = z_avoid_min_range.GetFloat();
    const float squaredMaximum = maximum * maximum;
    const float inverseSpan = 1.0f / ( maximum - minimum );
    FOR_EACH_VEC( neighbors, i )
    {
        CBaseEntity *other = neighbors[i].Get();
        if ( !other )
            continue;
        Vector delta = mover->GetFeet() - other->GetAbsOrigin();
        delta.z = 0.0f;
        const float squaredDistance = delta.LengthSqr();
        if ( !( squaredMaximum > squaredDistance ) )
            continue;
        // Native FPU holds sqrt(floatSquared)+double0.001 past the float spill.
        const double distance = sqrt( (double)squaredDistance ) + 0.001;
        const float roundedDistance = (float)distance;
        delta *= 1.0f / roundedDistance;
        const float weight = (double)minimum < distance ? ( maximum - roundedDistance ) * inverseSpan : 1.0f;
        repel += delta * weight;
        if ( z_debug_avoid.GetBool() )
            NDebugOverlay::HorzArrow( other->WorldSpaceCenter(), WorldSpaceCenter(), weight * 15.0f, 255, 0, 0, 255, true, 0.1f );
    }
    const float squaredLength = repel.LengthSqr();
    if ( squaredLength > 1.0f )
        repel *= 1.0f / (float)sqrt( (double)squaredLength );
    if ( repel.x != 0.0f || repel.y != 0.0f || repel.z != 0.0f )
    {
        const Vector goal = mover->GetFeet() + repel * z_avoid_power.GetFloat();
        mover->Approach( goal, 1.0f );
        if ( z_debug_avoid.GetBool() )
            NDebugOverlay::EntityText( entindex(), 2, "Repel", 0.1f, 255, 0, 255, 255 );
    }
}
#pragma float_control( pop )

Action< Infected > *Infected::CreateDyingAction( const CTakeDamageInfo &info )
{
	m_bHasStartedDying = true;
	return new InfectedDying( info );
}

float Infected::GetFlowDistance() const
{
	const TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
	return area ? area->GetFlowDistance( FLOW_TOWARD_GOAL ) : -1.0f;
}

void Infected::SelectExplosionWound()
{
    const InfectedDamageCutoutSet *set = GetInfectedDamageCutouts( GetGender() );
    const int count = set->m_ExplosionCutout.Count();
    if ( count > 0 )
    {
        const int wound = set->m_ExplosionCutout[RandomInt( 0, 32767 ) % count];
        if ( wound >= 0 && wound < set->m_Cutout.Count() )
            SetRequestedWound( wound );
    }
}

void Infected::SelectMeleeWound( int hitbox, int hitgroup, unsigned char damageType, bool front, int startDirection, int endDirection )
{
    if ( m_iRequestedWound1 != -1 && m_iRequestedWound2 != -1 )
        return;
    if ( ( m_nInfectedFlags & 0x800 ) &&
        !( ( hitgroup == HITGROUP_HEAD || ( m_iRequestedWound1 == -1 && m_iRequestedWound2 == -1 ) ) &&
           ( hitgroup < HITGROUP_LEFTLEG || hitgroup > HITGROUP_RIGHTLEG ) ) )
        return;
    int candidates[8];
    int count = 0;
    const InfectedDamageCutoutSet *set = GetInfectedDamageCutouts( GetGender() );
    FOR_EACH_VEC( set->m_Cutout, i )
    {
        const InfectedDamageCutout_t &wound = set->m_Cutout[i];
        if ( ( damageType & DMG_SLASH ) && !( wound.m_nDamageType & ( INFECTED_CUTOUT_DISMEMBER | INFECTED_CUTOUT_SLASH ) ) )
            continue;
        if ( ( damageType & DMG_CLUB ) && !( wound.m_nDamageType & INFECTED_CUTOUT_BLUNT ) )
            continue;
        if ( wound.m_nDamageType & INFECTED_CUTOUT_SLASH )
        {
            if ( wound.m_nSwingStart != startDirection || wound.m_nSwingEnd != endDirection || wound.m_bBack != front )
                continue;
            if ( m_iRequestedWound1 != -1 &&
                 ( set->m_Cutout[m_iRequestedWound1].m_nDamageType & INFECTED_CUTOUT_SLASH ) )
                continue;
        }
        for ( int h = 0; h < wound.m_nHitboxCount; ++h )
            if ( wound.m_iHitbox[h] >= 0 && wound.m_iHitbox[h] == hitbox && count < ARRAYSIZE( candidates ) )
                candidates[count++] = i;
    }
    if ( count > 0 )
        SetRequestedWound( candidates[RandomInt( 0, count - 1 )] );
}

// Sole S4051B0 implementation in existing server/l4d2/infected.cpp.
// Main's corrected policy applies ONLY to this unbounded native selector.
// S4050A0 SelectBulletWound retains its genuine count<8 cap unchanged.
bool Infected::SelectWoundByDamageType( int damageType, int hitbox )
{
	if ( m_iRequestedWound1 != -1 && m_iRequestedWound2 != -1 )
		return false;
	int candidates[8];
	int count = 0;
	const InfectedDamageCutoutSet *set = GetInfectedDamageCutouts( GetGender() );
	FOR_EACH_VEC( set->m_Cutout, i )
	{
		const InfectedDamageCutout_t &wound = set->m_Cutout[i];
		if ( !( damageType & wound.m_nDamageType ) )
			continue;
		if ( hitbox == -1 )
		{
			// S4051B0: ignore only an out-of-array store; retain native count.
			if ( count < ARRAYSIZE( candidates ) )
				candidates[count] = i;
			++count;
		}
		else
		{
			for ( int h = 0; h < wound.m_nHitboxCount; ++h )
			{
				if ( wound.m_iHitbox[h] >= 0 && wound.m_iHitbox[h] == hitbox )
				{
					if ( count < ARRAYSIZE( candidates ) )
						candidates[count] = i;
					++count;
				}
			}
		}
	}
	if ( count <= 0 )
		return false;
	const int selected = RandomInt( 0, count - 1 );
	// S4051B0: only the selected out-of-array read is defined to return zero.
	const int wound = selected < ARRAYSIZE( candidates ) ? candidates[selected] : 0;
	SetRequestedWound( wound );
	return true;
}

void Infected::PrintAnimationLayers( CTerrorPlayer *recipient )
{
	MDLCACHE_CRITICAL_SECTION(); // Native locks once, including the invalid-model return.
	CStudioHdr *studio = GetModelPtr();
	if ( !studio ) return; // Existing GetModelPtr validates the real render header.
	CFmtStrN<256, true> text( "%f: Infected %d(%s): main sequence is %d\n",
		(double)gpGlobals->curtime, entindex(), studio->pszName(), GetSequence() );
	TerrorMessageText( recipient, text, 0 ); // Native3FF600 header.
	FOR_EACH_VEC( m_clientAnimatedOverlay, i )
	{
		const int sequence = m_clientAnimatedOverlay[i].m_nSequence;
		const Activity activity = GetSequenceActivity( sequence );
		const char *name = activity == ACT_INVALID ? "ACT_INVALID" : ActivityList_NameForIndex( activity );
		const InfectedAnimationLayer &layer = m_clientAnimatedOverlay[i]; // Native reloads the vector AFTER lookup.
		text.sprintf( "layer %d: sequence %d(%s), start time %f, order %d, looping %d\n",
			i, sequence, name, (double)layer.m_flStartTime, layer.m_nOrder, layer.m_bLooping ? 1 : 0 );
		TerrorMessageText( recipient, text, 0 ); // Native second callsite, one per actual presentation record.
	}
}
CON_COMMAND_F( z_debug_infected_anim_report, "Prints the server's layer info for the specified infected", 0x00004004 )
{
	CTerrorPlayer *recipient = ToTerrorPlayer( UTIL_GetCommandClient() );
	const unsigned int index = args.ArgC() <= 1 ? 0u : (unsigned int)atoi( args[1] );
	// Native401000 uses an UNSIGNED2048 bound (negative atoi values reject too),
	// not maxClients. No alternate nearby-actor selection or broadcast fallback.
	if ( index >= 2048u ) return;
	edict_t *edict = engine->PEntityOfEntIndex( (int)index );
	if ( edict && !edict->IsFree() )
	{
		Infected *infected = dynamic_cast< Infected * >( CBaseEntity::Instance( edict ) );
		if ( infected ) infected->PrintAnimationLayers( recipient );
	}
}

// Native server 0x3fd920; 0x5c5318 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace infectedPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "infected", 0x5c5318 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "infected", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#include "l4d2/infected/common/infected_console_server_callbacks.inc"

const char *Infected::GetTerrorFootstepSoundName( const char *sound, bool right, float speed, bool force )
{
	// S3FDED0: one native process-static buffer, no surface substitution.
	static char name[64];
	Q_snprintf( name, sizeof( name ), "Infected.%s", sound );
	return name;
}
