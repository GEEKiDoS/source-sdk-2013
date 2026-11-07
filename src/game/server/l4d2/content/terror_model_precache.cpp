//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/content/terror_model_precache.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "nav_mesh.h"
#include "filesystem.h"
#include "studio.h"
#include "datacache/imdlcache.h"
#include "tier1/KeyValues.h"

#include "l4d2/content/terror_model_precache_console_server_cvars.inc"
#include "tier0/memdbgon.h"

extern ConVar z_population;
extern int g_iNextInfectedVariant;
extern int GetTransitionPlayerBeingCreated();
// Bounded accessor on the EXISTING infected_population.cpp owner. No second
// population parser, cached KV, or empty-data substitute lives in this TU.
extern KeyValues *GetInfectedPopulationDefinition();

static ConVar precache_all_survivors( "precache_all_survivors", "0", 0x00004004 );
static ConVar z_female_boomer_spawn_chance( "z_female_boomer_spawn_chance", "25", 0x00004004 );

namespace
{
// Sole native107ADAB0/107ADB3C catalog. M3 removes the obsolete private copy
// from terrorplayer.cpp and migrates its remaining callers to the helpers.
const char *const s_SurvivorModels[] =
{
	"models/survivors/survivor_gambler.mdl",
	"models/survivors/survivor_producer.mdl",
	"models/survivors/survivor_coach.mdl",
	"models/survivors/survivor_mechanic.mdl",
	"models/survivors/survivor_namvet.mdl",
	"models/survivors/survivor_teenangst.mdl",
	"models/survivors/survivor_biker.mdl",
	"models/survivors/survivor_manager.mdl"
};
const char *const s_LightSurvivorModels[] =
{
	"", "", "", "",
	"",
	"models/survivors/survivor_teenangst_light.mdl",
	"models/survivors/survivor_biker_light.mdl",
	"models/survivors/survivor_manager_light.mdl"
};
CUtlVector< CUtlString > s_InfectedPlayerModels[10]; // native10855E10

bool IsSurvivorModelTeam( int team )
{
	return team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR;
}
int RemapCachedSurvivorCharacter( int character )
{
	if ( TerrorGameRules()->GetSurvivorSet() == 1 && (unsigned int)character < 4 )
	{
		const int originalSet[] = { 4, 5, 7, 6 };
		return originalSet[character];
	}
	return character;
}
bool IsBodygroupVariant( const char *part )
{
	return *part && atoi( part + 1 ) != -1 && *part != 'D';
}
void RandomizePlayerBodygroup( CBaseAnimating *actor, const char *name )
{
	// Native387F0 is called independently for Head/UpperBody/LowerBody.
	// Even a missing group consumes RandomInt(1,10000); a zero usable count
	// consumes none. Parts marked D or a suffix of-1 are not selectable.
	const int group = actor->FindBodygroupByName( name );
	int variants = 10000;
	if ( group >= 0 )
	{
		variants = 0;
		for ( int i = 0; i < actor->GetBodygroupCount( group ); ++i )
			if ( IsBodygroupVariant( actor->GetBodygroupPartName( group, i ) ) ) ++variants;
	}
	variants = MIN( variants, 10000 );
	if ( !variants ) return;
	const int selected = RandomInt( 1, variants );
	if ( group < 0 ) return;
	int variant = 0;
	for ( int i = 0; i < actor->GetBodygroupCount( group ); ++i )
	{
		if ( IsBodygroupVariant( actor->GetBodygroupPartName( group, i ) ) ) ++variant;
		if ( variant == selected ) { actor->SetBodygroup( group, i ); return; }
	}
}
void RandomizePlayerBodygroups( CBaseAnimating *actor )
{
	RandomizePlayerBodygroup( actor, "Head" );
	RandomizePlayerBodygroup( actor, "UpperBody" );
	RandomizePlayerBodygroup( actor, "LowerBody" );
}
}

// S:0x30EA30 -> 0x2D9EC0. The optional variant belongs to the chapter KV.
void PrecachePlayerModelAndMaterials( CBaseAnimating *actor, const char *model,
	const char *chapterVariant, CUtlVector< CUtlString > *models )
{
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( chapter )
	{
		const char *variant = chapter->GetString( chapterVariant, "" );
		if ( variant && variant[0] )
			model = variant;
	}

	CBaseEntity::PrecacheModel( model );
	if ( !filesystem->FileExists( model ) )
	{
		Warning( "Couldn't precache %s\n", model );
		return;
	}

	if ( !models )
		return;

	// L4D2 capability gap: the target type-3 check rejects original materials.
#if !defined( L4D2 )
	engine->ForceModelBounds( model, Vector( -16.0f, -16.0f, -13.0f ), Vector( 16.0f, 16.0f, 75.0f ) );
#endif
	actor->SetModel( model );
	CStudioHdr *studio = actor->GetModelPtr();
	if ( !studio || !studio->GetRenderHdr() )
		return;

	const studiohdr_t *header = studio->GetRenderHdr();
	models->AddToTail( CUtlString( model ) );
	for ( int texture = 0; texture < header->numtextures; ++texture )
	{
		for ( int directory = 0; directory < header->numcdtextures; ++directory )
		{
			char material[MAX_PATH];
			Q_snprintf( material, sizeof( material ), "materials/%s%s.vmt",
				header->pCdtexture( directory ), header->pTexture( texture )->pszName() );
			if ( filesystem->FileExists( material ) )
			{
				engine->ForceSimpleMaterial( material );
				break;
			}
		}
	}
}

int GetCurrentTerrorSurvivorSet()
{
	// Uncached S2DBAE0 has the original missing-mission default2.
	KeyValues *mission = TerrorGameRules()->GetMissionInfo();
	return mission ? mission->GetInt( "survivor_set", 2 ) : 2;
}

// S350F60. Preserve the native identity/alias comparison order.
int GetTerrorSurvivorCharacterForName( const char *name )
{
	if ( !Q_stricmp( "Coach", name ) ) return 2;
	if ( !Q_stricmp( "Mechanic", name ) || !Q_stricmp( "Ellis", name ) ) return 3;
	if ( !Q_stricmp( "Gambler", name ) || !Q_stricmp( "Nick", name ) ) return 0;
	if ( !Q_stricmp( "Producer", name ) || !Q_stricmp( "Rochelle", name ) ) return 1;
	if ( !Q_stricmp( "NamVet", name ) || !Q_stricmp( "Bill", name ) ) return 4;
	if ( !Q_stricmp( "TeenGirl", name ) || !Q_stricmp( "Zoey", name ) || !Q_stricmp( "TeenAngst", name ) ) return 5;
	if ( !Q_stricmp( "Biker", name ) || !Q_stricmp( "Francis", name ) ) return 6;
	if ( !Q_stricmp( "Manager", name ) || !Q_stricmp( "Louis", name ) ) return 7;
	return SurvivorCharacter_Unknown;
}


int GetTerrorSurvivorCharacterForModel( const char *model )
{
	// S2FABE0 uses a CASE-SENSITIVE prefix, removes the last four characters,
	// and then uses S350F60's case-insensitive identity/alias lookup.
	char name[64] = { 0 };
	const char *prefix = "models/survivors/survivor_";
	if ( !model || Q_strncmp( model, prefix, Q_strlen( prefix ) ) )
		Q_strncpy( name, "invalid", sizeof( name ) );
	else
		V_StrSlice( model, Q_strlen( prefix ), -4, name, sizeof( name ) );
	return GetTerrorSurvivorCharacterForName( name );
}

const char *GetTerrorSurvivorModel( int character )
{
	// Native unknown8 indexes the adjacent light catalog's first empty string.
	// Keep that proven empty result; do not turn an unknown into a fake Nick.
	return character == SurvivorCharacter_Unknown ? "" : s_SurvivorModels[character];
}

const char *GetSurvivorBotName( int character )
{
	const char *const names[] = { "Nick", "Rochelle", "Coach", "Ellis", "Bill", "Zoey", "Francis", "Louis" };
	character = RemapCachedSurvivorCharacter( character );
	return (unsigned int)character < ARRAYSIZE( names ) ? names[character] : "Survivor";
}

const char *GetTerrorSurvivorModelForCharacter( int character )
{
	// S305530's light-model override only applies to actual L4D1 identities.
	if ( (unsigned int)( character - 4 ) <= 3 )
	{
		const char *identity = ::GetResponseCharacterName( character );
		if ( !Q_strcmp( identity, "TeenGirl" ) ) identity = "TeenAngst";
		const CFmtStr prefix( "survivor_%s_light", identity );
		KeyValues *population = GetInfectedPopulationDefinition();
		KeyValues *area = population ? population->FindKey( z_population.GetString() ) : NULL;
		if ( area )
		{
			for ( KeyValues *model = area->GetFirstSubKey(); model; model = model->GetNextKey() )
				if ( !Q_strnicmp( model->GetName(), prefix.String(), Q_strlen( prefix.String() ) ) )
					return s_LightSurvivorModels[character];
		}
	}
	return GetTerrorSurvivorModel( RemapCachedSurvivorCharacter( character ) );
}

const CUtlVector< CUtlString > &GetTerrorInfectedPlayerModels( int zombieClass )
{
	if ( zombieClass == 9 ) zombieClass = 0;
	const CUtlVector< CUtlString > &models = s_InfectedPlayerModels[zombieClass];
	return models.Count() ? models : s_InfectedPlayerModels[3];
}

const char *GetTerrorDefaultPlayerModel()
{
	// Native32B5F0 reads the actual HUNTER catalog's FIRST entry without an
	// added missing-service/model fallback. Precache must have populated it.
	return s_InfectedPlayerModels[3][0].String();
}

void PrecacheTerrorPlayerModels( CTerrorPlayer *actor )
{
	// S3198C0: every actual unnamed-player precache clears all ten lists.
	// CUtlString owns the native per-entry allocations; RemoveAll keeps capacity.
	for ( int i = 0; i < ARRAYSIZE( s_InfectedPlayerModels ); ++i )
		s_InfectedPlayerModels[i].RemoveAll();
	int first = GetCurrentTerrorSurvivorSet() == 1 ? 4 : 0;
	int end = first + 4;
	if ( precache_all_survivors.GetBool() ) { first = 0; end = 8; }
	for ( int i = first; i < end; ++i )
		PrecachePlayerModelAndMaterials( actor, s_SurvivorModels[i], "null" );

	// S30EDD0 restores the population default, then reads ONLY the actual NAV
	// header/custom pre-area data before consulting the canonical population KV.
	z_population.Revert();
	TheNavMesh->GetPlacesFromNavFile( NULL );
	KeyValues *population = GetInfectedPopulationDefinition();
	KeyValues *area = population ? population->FindKey( z_population.GetString() ) : NULL;
	if ( area )
	{
		for ( KeyValues *model = area->GetFirstSubKey(); model; model = model->GetNextKey() )
			if ( !Q_strnicmp( model->GetName(), "survivor", 8 ) )
				PrecachePlayerModelAndMaterials( actor, CFmtStr( "models/survivors/%s.mdl", model->GetName() ), "null" );
	}
	static const char *const s_BoomerGibs[] =
	{
		"models/infected/limbs/exploded_boomer.mdl",
		"models/infected/limbs/exploded_boomer_head.mdl",
		"models/infected/limbs/exploded_boomer_rarm.mdl",
		"models/infected/limbs/exploded_boomer_steak1.mdl",
		"models/infected/limbs/exploded_boomer_steak2.mdl",
		"models/infected/limbs/exploded_boomer_steak3.mdl"
	};
	for ( int i = 0; i < ARRAYSIZE( s_BoomerGibs ); ++i )
		CBaseEntity::PrecacheModel( s_BoomerGibs[i] );

	// Preserve the native female-before-male ordering and the actual mission
	// guard; per-model *Variant overrides still belong to CHAPTER data30EA30.
	KeyValues *mission = TerrorGameRules()->GetMissionInfo();
	if ( mission && !mission->GetInt( "no_female_boomers", 0 ) )
	{
		CBaseEntity::PrecacheModel( "models/infected/limbs/exploded_boomette.mdl" );
		PrecachePlayerModelAndMaterials( actor, "models/infected/boomette.mdl", "BoometteVariant", &s_InfectedPlayerModels[2] );
	}
	CBaseEntity::PrecacheModel( "models/infected/smoker_tongue_attach.mdl" );
	CBaseEntity::PrecacheModel( "models/w_models/weapons/w_eq_defibrillator_no_paddles.mdl" );
	// Native engine+504 is false on PC; +500 is the existing reslist-tool query.
	if ( ( !Engine_IsCreatingXboxReslist() && !IsX360() ) ||
		( mission && mission->GetInt( "allow_diesel", 0 ) ) )
		CBaseEntity::PrecacheModel( "models/props_junk/dieselcan.mdl" );
	PrecachePlayerModelAndMaterials( actor, "models/infected/hunter.mdl", "HunterVariant", &s_InfectedPlayerModels[3] );
	PrecachePlayerModelAndMaterials( actor, "models/infected/smoker.mdl", "SmokerVariant", &s_InfectedPlayerModels[1] );
	PrecachePlayerModelAndMaterials( actor, "models/infected/boomer.mdl", "BoomerVariant", &s_InfectedPlayerModels[2] );
	PrecachePlayerModelAndMaterials( actor, "models/infected/spitter.mdl", "SpitterVariant", &s_InfectedPlayerModels[4] );
	PrecachePlayerModelAndMaterials( actor, "models/infected/jockey.mdl", "JockeyVariant", &s_InfectedPlayerModels[5] );
	PrecachePlayerModelAndMaterials( actor, "models/infected/charger.mdl", "ChargerVariant", &s_InfectedPlayerModels[6] );
	PrecachePlayerModelAndMaterials( actor, "models/infected/hulk.mdl", "TankVariant", &s_InfectedPlayerModels[8] );
}

SurvivorCharacterType CTerrorPlayer::SelectSurvivorCharacter( int requested ) const
{
	// S30F1B0: raw integer request/desired substitution precedes the genuine
	// availability construction and roster traversal. Never cast raw IDs to enum.
	const int team = GetTeamNumber();
	if ( static_cast< unsigned int >( requested ) >= 8U )
		requested = m_iDesiredSurvivorCharacter;
	CUtlVector< SurvivorCharacterType > available;
	const int first = team == TEAM_SURVIVOR ? 0 : 4;
	for ( int i = 0; i < 4; ++i )
		available.AddToTail( static_cast< SurvivorCharacterType >( first + i ) );
	if ( team == TEAM_SURVIVOR )
	{
		FOR_EACH_VEC_BACK( available, i )
			if ( g_SavedTerrorPlayerStates.HasPendingSurvivorCharacter( available[i], GetTransitionPlayerBeingCreated() ) )
				available.Remove( i );
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *other = UTIL_PlayerByIndex( i );
		// S30B450: do not use ToTerrorPlayer here. Its IsPlayer query would
		// precede edict/index, rather than the native first and second queries.
		if ( !other || !other->edict() || !other->entindex() || !other->IsPlayer() ||
			!other->IsConnected() || other == this || other->GetTeamNumber() != team || !other->IsPlayer() )
			continue;
		available.FindAndRemove( static_cast< CTerrorPlayer * >( other )->GetSurvivorCharacter() );
	}
	if ( requested != SurvivorCharacter_Unknown )
	{
		FOR_EACH_VEC( available, i )
			if ( static_cast< int >( available[i] ) == requested )
				return available[i];
	}
	if ( available.Count() )
		return available[RandomInt( 0, available.Count() - 1 )];
	return static_cast< SurvivorCharacterType >( RandomInt( first, first + 3 ) );
}

void CTerrorPlayer::SetModelFromClass()
{
	// S31A3B0. This is the sole replacement for the old survivor-only body.
	MDLCACHE_CRITICAL_SECTION();
	if ( IsSurvivorModelTeam( GetTeamNumber() ) )
	{
		int character = GetSurvivorCharacter();
		bool survivorModel = false;
		for ( int i = 0; i < ARRAYSIZE( s_SurvivorModels ); ++i )
			if ( !Q_stricmp( STRING( GetModelName() ), s_SurvivorModels[i] ) ) { survivorModel = true; break; }
		if ( !survivorModel )
		{
			character = SelectSurvivorCharacter( character );
			m_iClass = character + 1;
			const char *model = GetTerrorSurvivorModelForCharacter( character );
			DevMsg( "SetModelFromClass: Survivor %s changed model from %s to %s via SetModelFromClass\n", GetPlayerName(), STRING( GetModelName() ), model );
			SetModel( model );
		}
		SetRenderMode( kRenderTransColor );
		m_nRenderFX = kRenderFxNone;
		SetRenderColor( 255, 255, 255 );
		SetRenderColorA( 255 );
		m_nSkin = 0;
		m_survivorKillScore = 0;
		for ( int i = 0; i < 9; ++i ) m_classSpawnCount.Set( i, 0 );
		SetSurvivorCharacter( (SurvivorCharacterType)character );
	}
	else if ( GetTeamNumber() != TEAM_INFECTED || GetZombieClass() != 0 )
	{
		int zombieClass = GetTeamNumber() == TEAM_INFECTED ? GetZombieClass() : 0;
		if ( zombieClass == 9 ) zombieClass = 0;
		if ( GetTeamNumber() == TEAM_INFECTED )
		{
			const CUtlVector< CUtlString > &models = GetTerrorInfectedPlayerModels( zombieClass );
			int selected = RandomInt( 0, models.Count() - 1 );
			if ( zombieClass == 2 )
			{
				selected = 0;
				KeyValues *mission = TerrorGameRules()->GetMissionInfo();
				if ( mission && !mission->GetInt( "no_female_boomers", 0 ) && g_iNextInfectedVariant != 1 &&
					( g_iNextInfectedVariant == 2 || z_female_boomer_spawn_chance.GetFloat() >= (float)RandomInt( 1, 100 ) ) )
					selected = 1;
			}
			SetModel( models[selected].String() );
		}
		else SetModel( GetTerrorDefaultPlayerModel() );
		if ( GetTeamNumber() == TEAM_INFECTED && zombieClass == 3 ) m_nSkin = 0;
		else
		{
			CStudioHdr *studio = GetModelPtr();
			if ( studio && studio->GetRenderHdr() )
				m_nSkin = RandomInt( 0, studio->GetRenderHdr()->numskinfamilies - 1 );
		}
	}
	RandomizePlayerBodygroups( this );
}

void CTerrorPlayer::SetSurvivorCharacter( SurvivorCharacterType character )
{
	// S3164E0 updates NAV ownership, actual fake-client name and the active
	// weapon's viewmodel. It is not merely a disconnected network-field setter.
	if ( IsSurvivorModelTeam( GetTeamNumber() ) )
	{
		if ( GetSurvivorCharacter() != SurvivorCharacter_Unknown ) ClearLastKnownArea();
		m_survivorCharacter = character;
		DevMsg( "(SKIN) SetCharacter: Survivor %s chose character %s(%d) from model %s\n",
			GetPlayerName(), ::GetResponseCharacterName( GetSurvivorCharacter() ), GetSurvivorCharacter(), STRING( GetModelName() ) );
		if ( IsFakeClient() ) engine->SetFakeClientConVarValue( edict(), "name", GetSurvivorBotName( GetSurvivorCharacter() ) );
	}
	else m_survivorCharacter = SurvivorCharacter_Unknown;
	CBaseCombatWeapon *weapon = GetActiveWeapon();
	if ( weapon )
	{
		weapon->SetViewModel();
		weapon->SetModel( weapon->GetViewModel( 0 ) );
	}
}

void CTerrorPlayer::RestoreTransitionModel( KeyValues *data )
{
	// S2B7BE0: first whitelist, class selection, second whitelist, character,
	// diagnostic, and skin-LAST. Whitelist comparisons are case-insensitive.
	const char *requested = data->GetString( "modelName", "" );
	if ( IsSurvivorModelTeam( GetTeamNumber() ) && *requested )
	{
		for ( int i = 0; i < ARRAYSIZE( s_SurvivorModels ); ++i )
			if ( !Q_stricmp( s_SurvivorModels[i], requested ) ) { SetModel( s_SurvivorModels[i] ); break; }
	}
	m_iClass = data->GetInt( "playerClass", 0 );
	SetModelFromClass();
	if ( *requested )
	{
		if ( GetTeamNumber() == TEAM_INFECTED )
		{
			const CUtlVector< CUtlString > &models = GetTerrorInfectedPlayerModels( GetZombieClass() );
			FOR_EACH_VEC( models, i )
				if ( !Q_stricmp( models[i].String(), requested ) ) { SetModel( models[i].String() ); break; }
		}
		else
		{
			for ( int i = 0; i < ARRAYSIZE( s_SurvivorModels ); ++i )
				if ( !Q_stricmp( s_SurvivorModels[i], requested ) ) { SetModel( s_SurvivorModels[i] ); break; }
		}
	}
	SetSurvivorCharacter( (SurvivorCharacterType)data->GetInt( "character", 0 ) );
	if ( IsSurvivorModelTeam( GetTeamNumber() ) )
		DevMsg( "(SKIN) Survivor %s changed model to %s (wanted %s) via RestoreModelData\n", GetPlayerName(), STRING( GetModelName() ), requested );
	m_nSkin = data->GetInt( "skin", 0 );
}
