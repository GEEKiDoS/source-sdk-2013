//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/campaign_map_entities.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/director/infected_population.h"
#include "l4d2/infected/boss/witch.h"
#include "l4d2/director/zombiemanager.h"
#include "tier1/KeyValues.h"
#include "tier1/fmtstr.h"
#include "tier1/strtools.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_item_position, CInfoItemPosition );

LINK_ENTITY_TO_CLASS( info_l4d1_survivor_spawn, CL4D1SurvivorSpawn );

LINK_ENTITY_TO_CLASS( info_ambient_mob_start, CAmbientMobMarker );

LINK_ENTITY_TO_CLASS( info_ambient_mob_end, CAmbientMobMarker );

LINK_ENTITY_TO_CLASS( info_zombie_spawn, CInfoZombieSpawn );

BEGIN_DATADESC( CInfoItemPosition )
	DEFINE_KEYFIELD( m_group, FIELD_INTEGER, "group" ),
	DEFINE_KEYFIELD( m_rarity, FIELD_INTEGER, "rarity" ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetGroup", InputSetGroup ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetRarity", InputSetRarity ),
	DEFINE_KEYFIELD( m_iszReplaceParm[0], FIELD_STRING, "Replace01" ),
	DEFINE_KEYFIELD( m_iszReplaceParm[1], FIELD_STRING, "Replace02" ),
	DEFINE_KEYFIELD( m_iszReplaceParm[2], FIELD_STRING, "Replace03" ),
	DEFINE_KEYFIELD( m_iszReplaceParm[3], FIELD_STRING, "Replace04" ),
	DEFINE_KEYFIELD( m_iszReplaceParm[4], FIELD_STRING, "Replace05" ),
	DEFINE_KEYFIELD( m_iszReplaceParm[5], FIELD_STRING, "Replace06" ),
	DEFINE_KEYFIELD( m_iszReplaceParm[6], FIELD_STRING, "Replace07" ),
	DEFINE_KEYFIELD( m_iszReplaceParm[7], FIELD_STRING, "Replace08" ),
	DEFINE_KEYFIELD( m_iszReplaceParm[8], FIELD_STRING, "Replace09" ),
	DEFINE_KEYFIELD( m_iszReplaceParm[9], FIELD_STRING, "Replace10" ),
END_DATADESC()

BEGIN_ENT_SCRIPTDESC( CInfoItemPosition, CBaseEntity, "info_item_position" )
	DEFINE_SCRIPTFUNC( SetGroup, "Set the group number for this item position" )
	DEFINE_SCRIPTFUNC( GetGroup, "Get the group number for this item position" )
	DEFINE_SCRIPTFUNC( SetRarity, "Set the rarity for this item position" )
	DEFINE_SCRIPTFUNC( GetRarity, "Get the rarity for this item position" )
	DEFINE_SCRIPTFUNC( GetReplaceParm, "Get a replacement parameter by index" )
END_SCRIPTDESC()

BEGIN_DATADESC( CL4D1SurvivorSpawn )
	DEFINE_INPUTFUNC( FIELD_VOID, "SpawnSurvivor", InputSpawnSurvivor ),
	DEFINE_KEYFIELD( m_character, FIELD_INTEGER, "Character" ),
END_DATADESC()

BEGIN_DATADESC( CInfoZombieSpawn )
	DEFINE_KEYFIELD( m_szPopulation, FIELD_STRING, "population" ),
	DEFINE_KEYFIELD( m_bOfferTank, FIELD_BOOLEAN, "offer_tank" ),
	{ FIELD_VOID, "SpawnZombie", { 0, 0 }, 1, FTYPEDESC_INPUT, "SpawnZombie", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputSpawnZombie ) },
	{ FIELD_STRING, "StartleZombie", { 0, 0 }, 1, FTYPEDESC_INPUT, "StartleZombie", NULL, static_cast< inputfunc_t >( &classNameTypedef::InputStartleZombie ) },
	{ FIELD_CUSTOM, "m_OnSpawn[ZOMBIE_NORMAL]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnSpawn[0] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnSpawnNormal", eventFuncs },
	{ FIELD_CUSTOM, "m_OnSpawn[ZOMBIE_SMOKER]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnSpawn[1] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnSpawnSmoker", eventFuncs },
	{ FIELD_CUSTOM, "m_OnSpawn[ZOMBIE_BOOMER]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnSpawn[2] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnSpawnBoomer", eventFuncs },
	{ FIELD_CUSTOM, "m_OnSpawn[ZOMBIE_HUNTER]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnSpawn[3] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnSpawnHunter", eventFuncs },
	{ FIELD_CUSTOM, "m_OnSpawn[ZOMBIE_SPITTER]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnSpawn[4] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnSpawnSpitter", eventFuncs },
	{ FIELD_CUSTOM, "m_OnSpawn[ZOMBIE_JOCKEY]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnSpawn[5] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnSpawnJockey", eventFuncs },
	{ FIELD_CUSTOM, "m_OnSpawn[ZOMBIE_CHARGER]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnSpawn[6] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnSpawnCharger", eventFuncs },
	{ FIELD_CUSTOM, "m_OnSpawn[ZOMBIE_WITCH]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnSpawn[7] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnSpawnWitch", eventFuncs },
	{ FIELD_CUSTOM, "m_OnSpawn[ZOMBIE_TANK]", { (int)_hacky_datamap_offsetof( classNameTypedef, m_OnSpawn[8] ), 0 }, 1, FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY, "OnSpawnTank", eventFuncs },
END_DATADESC()


CInfoItemPosition::CInfoItemPosition() : m_group( 0 ), m_rarity( 0 ) {}
void CInfoItemPosition::SetGroup( int group ) { m_group = group; } // S29AD60
int CInfoItemPosition::GetGroup() const { return m_group; } // S29AD70
void CInfoItemPosition::SetRarity( int rarity ) { m_rarity = static_cast< signed char >( rarity ); } // S29AD80
int CInfoItemPosition::GetRarity() const { return m_rarity; } // S29AD90 MOVSX
const char *CInfoItemPosition::GetReplaceParm( int index ) const
{
	if ( static_cast< unsigned int >( index ) > 9U ) return NULL;
	const char *parameter = STRING( m_iszReplaceParm[index] );
	return parameter && parameter[0] ? parameter : NULL; // S29AE50
}
void CInfoItemPosition::InputSetGroup( inputdata_t &data ) { SetGroup( data.value.Int() ); }
void CInfoItemPosition::InputSetRarity( inputdata_t &data ) { SetRarity( data.value.Int() ); }

void CL4D1SurvivorSpawn::InputSpawnSurvivor( inputdata_t &data )
{
	// S29A9E0 admits the native UNKNOWN8 as well as the four L4D1 characters.
	if ( m_character < 4 || m_character > 8 ) return;
	TheDirector->CreateSurvivorBot( static_cast< SurvivorCharacterType >( m_character ) );
	CTerrorPlayer *player = FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( m_character ) );
	if ( player ) player->Teleport( &GetAbsOrigin(), &vec3_angle, &vec3_origin );
}

static ConVar witch_rage_ramp_duration( "witch_rage_ramp_duration", "5.0", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL );
CInfoZombieSpawn::CInfoZombieSpawn() : m_bOfferTank( false ) {} // S29BAC0; EHANDLE/output constructors own their defaults.
void CInfoZombieSpawn::Spawn()
{
	BaseClass::Spawn(); // S29BA70 -> genuine CPointEntity::Spawn.
	Precache();
}
void CInfoZombieSpawn::Precache()
{
	// S29BBD0: a pure read of the existing loaded population, no new loader.
	if ( m_szPopulation == NULL_STRING || !TheDirector ) return;
	KeyValues *population = GetInfectedPopulationDefinition();
	if ( !population ) return;
	KeyValues *group = population->FindKey( STRING( m_szPopulation ), false );
	if ( !group ) return;
	for ( KeyValues *entry = group->GetFirstSubKey(); entry; entry = entry->GetNextKey() )
	{
		const char *name = entry->GetName();
		if ( StringHasPrefix( name, "common_" ) ) PrecacheModel( CFmtStr( "models/infected/%s.mdl", name ) );
	}
}
void CInfoZombieSpawn::InputSpawnZombie( inputdata_t &data )
{
	// S29BC90: preserve failed-selection diagnostics, the one 1..100 draw,
	// ordered first cumulative weight >= roll, and no renormalization/fallback.
	KeyValues *population = GetInfectedPopulationDefinition();
	if ( !population )
	{
		DevMsg( "%s failed to spawn: missing director population data\n", GetClassname() );
		return;
	}
	const char *populationName = STRING( m_szPopulation );
	KeyValues *group = population->FindKey( populationName, false );
	if ( !group )
	{
		DevMsg( "%s failed to spawn: invalid population '%s'\n", GetClassname(), populationName );
		return;
	}
	const int roll = RandomInt( 1, 100 );
	int cumulative = 0;
	KeyValues *selected = group->GetFirstSubKey();
	for ( ; selected; selected = selected->GetNextKey() )
	{
		// Original ADD wraps as an x86 int; do not introduce signed overflow.
		cumulative = static_cast< int >( static_cast< unsigned int >( cumulative ) + static_cast< unsigned int >( selected->GetInt() ) );
		if ( cumulative >= roll ) break;
	}
	if ( !selected )
	{
		DevMsg( "%s failed to spawn: incomplete population '%s'\n", GetClassname(), populationName );
		return;
	}
	const char *name = selected->GetName();
	const string_t pooledModel = AllocPooledString( CFmtStr( "models/infected/%s.mdl", name ) );
	int zombieClass = 0;
	if ( StringHasPrefix( name, "hunter" ) ) zombieClass = 3;
	else if ( StringHasPrefix( name, "charger" ) ) zombieClass = 6;
	else if ( StringHasPrefix( name, "smoker" ) ) zombieClass = 1;
	else if ( StringHasPrefix( name, "boomer" ) ) zombieClass = 2;
	else if ( StringHasPrefix( name, "witch" ) ) zombieClass = 7;
	else if ( StringHasPrefix( name, "tank" ) ) zombieClass = 8;
	// Retail intentionally has no Spitter/Jockey prefix branch on this path.
	if ( pooledModel == NULL_STRING )
	{
		DevMsg( "%s failed to spawn: incomplete population '%s'\n", GetClassname(), populationName );
		return;
	}
	const Vector position = GetAbsOrigin();
	const QAngle angles( 0.0f, GetAbsAngles().y, 0.0f );
	g_bZombieSpawnBypass = true; // S3589F0, SAME global1085D945.
	CBaseEntity *spawned = NULL;
	if ( static_cast< unsigned int >( zombieClass ) - 1U <= 5U )
		spawned = TheZombieManager->SpawnSpecial( zombieClass, position, angles );
	else if ( zombieClass == 7 )
		spawned = Q_stristr( STRING( pooledModel ), "bride" ) ?
			TheZombieManager->SpawnWitchBride( position, angles ) : TheZombieManager->SpawnWitch( position, angles );
	else if ( zombieClass == 8 )
	{
		CTerrorPlayer *tank = TheZombieManager->SpawnTank( position, angles );
		spawned = tank;
		if ( m_bOfferTank && TerrorGameRules()->IsPlayerControlledZombies() ) TheDirector->OfferTank( tank, true );
	}
	else
	{
		spawned = CreateEntityByName( "infected" );
		if ( spawned )
		{
			spawned->SetAbsAngles( angles );
			spawned->SetAbsOrigin( position );
			spawned->SetModel( STRING( pooledModel ) );
			DispatchSpawn( spawned, true );
			Infected *infected = spawned->MyInfectedPointer();
			if ( infected ) infected->m_mobRush = true; // S403B50, SAME7361 notifier.
		}
	}
	if ( spawned )
	{
		CBaseCombatCharacter *combat = spawned->MyCombatCharacterPointer();
		if ( combat )
		{
			const unsigned int type = static_cast< unsigned int >( combat->GetZombieClass() );
			if ( type <= 8U ) m_OnSpawn[type].FireOutput( data.pActivator, this );
		}
	}
	// S29C0D7: output observers still see the PREVIOUS handle; replace only now.
	m_hZombie = spawned;
	g_bZombieSpawnBypass = false; // S358A00; native clears, never restores a prior value.
}
void CInfoZombieSpawn::InputStartleZombie( inputdata_t &data )
{
	// S29C100: only the last actually-created Witch can be startled.
	Witch *witch = dynamic_cast< Witch * >( m_hZombie.Get() );
	if ( !witch ) return;
	CBaseEntity *harasser = data.pActivator;
	const char *name = data.value.String();
	if ( name && name[0] )
	{
		CBaseEntity *named = gEntList.FindEntityByName( NULL, name );
		if ( named ) harasser = named;
	}
	witch->m_pendingHarasser = harasser; // SAME7600, not SetHarasser7596's event producer.
	witch->StartRageRamp( witch_rage_ramp_duration.GetFloat() );
}
