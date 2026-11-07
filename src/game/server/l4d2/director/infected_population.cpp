//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Original server: selection 0x3FFAD0, population precache 0x401E70/0x402130,
// list fallbacks 0x402F40, population loading 0x2627E0/0x260690.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/director/infected_population.h"
#include "filesystem.h"
#include "nav_mesh.h"
#include "tier1/KeyValues.h"
#include "l4d2/gamerules/terror_mode.h"

#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_session_manager.h"
#include "l4d2/director/zombiemanager.h"
#include "tier1/fmtstr.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar z_population( "z_population", "default", 0x00000006, "Default population on the current map (saved/loaded in the nav mesh)" );
ConVar z_debug_population( "z_debug_population", "0", 0x00004004 );
ConVar z_forcezombiemodel( "z_forcezombiemodel", "0", 0x00004004, "remove variability in infected models and instead use the specified model from z_zombieforcemodelname" );
ConVar z_forcezombiemodelname( "z_forcezombiemodelname", "common_male01", 0x00004004, "base name for the model from the models/infected directory to use for all infected if z_forcezombiemodel is set." );


namespace
{
const char *const s_EmptyPopulationModel = "models/infected/common_male_tshirt_cargos.mdl";

typedef CUtlVector< string_t > InfectedModelList;

int FindModel( const InfectedModelList &models, const char *name )
{
	FOR_EACH_VEC( models, i )
	{
		if ( !Q_stricmp( STRING( models[i] ), name ) )
			return i;
	}
	return models.InvalidIndex();
}

class CInfectedPopulation
{
public:
	CInfectedPopulation() : m_pPopulation( NULL ) {}

	// Native Director+1044 survives level shutdown; 2752D0 releases it first.
	void Shutdown()
	{
		if ( m_pPopulation )
		{
			m_pPopulation->deleteThis();
			m_pPopulation = NULL;
		}
		m_Models.Purge();
		m_LowViolenceModels.Purge();
	}

	void Precache();
	void Reset()
	{
		if ( m_pPopulation )
		{
			m_pPopulation->deleteThis();
			m_pPopulation = NULL;
		}
		LoadPopulation();
	}
	KeyValues *GetData() { return m_pPopulation; } // S25C110 pure1044 read; explicit loader owners are unchanged.
	const char *Select( const char *population );

private:
	void LoadPopulation();
	void LoadModePopulation( const char *mode );
	void PrecachePopulation( const char *population );

	KeyValues *m_pPopulation;
	InfectedModelList m_Models;
	InfectedModelList m_LowViolenceModels;
};

CInfectedPopulation s_InfectedPopulation;


void CInfectedPopulation::LoadModePopulation( const char *mode )
{
	char filename[MAX_PATH];
	Q_snprintf( filename, sizeof( filename ), "scripts/population_%s.txt", mode );
	KeyValues::AutoDelete modePopulation( new KeyValues( "Population" ) );
	if ( !modePopulation->LoadFromFile( filesystem, filename, "GAME" ) )
		return;

	// 0x260690 replaces whole named populations, not individual model weights.
	// Move their nodes to preserve both file order and duplicate-value order.
	KeyValues *entry = modePopulation->GetFirstSubKey();
	while ( entry )
	{
		KeyValues *next = entry->GetNextKey();
		KeyValues *old = m_pPopulation->FindKey( entry->GetName() );
		if ( old )
		{
			m_pPopulation->RemoveSubKey( old );
			old->deleteThis();
		}
		modePopulation->RemoveSubKey( entry );
		m_pPopulation->AddSubKey( entry );
		entry = next;
	}
}

void CInfectedPopulation::LoadPopulation()
{
	if ( m_pPopulation )
		return;

	m_pPopulation = new KeyValues( "Population" );
	m_pPopulation->LoadFromFile( filesystem, "scripts/population.txt", "GAME" );

	const char *base = g_TerrorModes.GetBaseName();
	LoadModePopulation( base );
	const char *mode = g_TerrorModes.GetName();
	if ( Q_stricmp( mode, base ) )
		LoadModePopulation( mode );
}

void CInfectedPopulation::PrecachePopulation( const char *population )
{
	KeyValues *areaPopulation = m_pPopulation->FindKey( population );
	if ( !areaPopulation )
		areaPopulation = m_pPopulation->FindKey( z_population.GetString() );
	if ( !areaPopulation )
	{
		Msg( "No area population for %s\n", z_population.GetString() );
		return;
	}

	// 0x401E70 walks all subkeys, including zero-weight entries, and precaches
	// names beginning with the case-insensitive six-character prefix common.
	for ( KeyValues *model = areaPopulation->GetFirstSubKey(); model; model = model->GetNextKey() )
	{
		if ( Q_strnicmp( model->GetName(), "common", 6 ) )
			continue;

		CFmtStrN< 256 > filename( "models/infected/%s.mdl", model->GetName() );
		if ( FindModel( m_Models, filename ) != m_Models.InvalidIndex() )
			continue;

		CBaseEntity::PrecacheModel( filename );
		string_t pooledName = AllocPooledString( filename );
		m_Models.AddToTail( pooledName );

		// 0x25C120 and DirectorSessionManager::Update 0x28DAE0: a connected
		// low-violence client selects the list excluding this one model.
		if ( Q_stricmp( model->GetName(), "common_police_male01" ) )
			m_LowViolenceModels.AddToTail( pooledName );
	}
}

void CInfectedPopulation::Precache()
{
	m_Models.RemoveAll();
	m_LowViolenceModels.RemoveAll();
	// S402130 resets the cvar BEFORE the NAV custom-data/Place read, then
	// obtains existing population data. Repeated precache keeps this reset.
	z_population.Revert();
	bool hasUnnamedPlaces = false;
	const CUtlVector< Place > *places = TheNavMesh->GetPlacesFromNavFile( &hasUnnamedPlaces );
	if ( m_pPopulation )
	{
		if ( hasUnnamedPlaces || !places || !places->Count() )
			PrecachePopulation( z_population.GetString() );
		if ( places )
		{
			FOR_EACH_VEC( ( *places ), i )
				PrecachePopulation( TheNavMesh->PlaceToName( ( *places )[i] ) );

			// Native Jimmy precache is INSIDE the non-NULL Place-vector
			// branch, including an empty vector, not an unconditional map test.
			if ( !Q_stricmp( STRING( gpGlobals->mapname ), "c1m4_atrium" ) )
			{
				const char *jimmy = "models/infected/common_male_jimmy.mdl";
				CBaseEntity::PrecacheModel( jimmy );
				string_t pooledName = AllocPooledString( jimmy );
				m_Models.AddToTail( pooledName );
				m_LowViolenceModels.AddToTail( pooledName );
			}
		}
	}
	else
	{
		Msg( "Failed to get population data\n" );
	}

	// Native402F40 fills LV first, then normal, and precaches this one model
	// once if either list was empty. This is the proven native fallback.
	const bool needsFallback = !m_LowViolenceModels.Count() || !m_Models.Count();
	if ( needsFallback )
	{
		const string_t fallback = AllocPooledString( s_EmptyPopulationModel );
		if ( !m_LowViolenceModels.Count() )
			m_LowViolenceModels.AddToTail( fallback );
		if ( !m_Models.Count() )
			m_Models.AddToTail( fallback );
		CBaseEntity::PrecacheModel( s_EmptyPopulationModel );
	}

	// Native25D460 is the REAL background predicate (load-type3 OR
	// director_force_background), NOT coop. 402F40 also checks map substring.
	const char *mapName = STRING( gpGlobals->mapname );
	if ( !mapName )
		mapName = "";
	if ( TheDirector->IsBackgroundMap() || Q_stristr( mapName, "background" ) )
	{
		CUtlVector< string_t > additionalModels;
		for ( KeyValues *population = m_pPopulation ? m_pPopulation->GetFirstTrueSubKey() : NULL;
			population; population = population->GetNextTrueSubKey() )
		{
			for ( KeyValues *model = population->GetFirstValue(); model; model = model->GetNextValue() )
			{
				CFmtStrN< 256 > filename( "models/infected/%s.mdl", model->GetName() );
				if ( FindModel( m_Models, filename ) != m_Models.InvalidIndex() ||
					FindModel( additionalModels, filename ) != additionalModels.InvalidIndex() )
					continue;
				additionalModels.AddToTail( AllocPooledString( filename ) );
				CBaseEntity::PrecacheModel( filename );
			}
		}
		// These resources do NOT enter either eligible spawn-model list.
	}
}

const char *CInfectedPopulation::Select( const char *population )
{
	const InfectedModelList &models = TheDirector->GetSessionManager()->IsLowViolence() ? m_LowViolenceModels : m_Models;
	if ( !models.Count() )
		return NULL;

	// S3FFAD0: Jimmy selection precedes population-data availability. Its
	// native failed33-percent attempt still consumes the sole attempt flag.
	if ( TheZombieManager->TrySelectJimmyGibbs() )
		return "models/infected/common_male_jimmy.mdl";

	if ( !m_pPopulation )
	{
		if ( z_debug_population.GetBool() )
			Warning( "spawning a random zombie without population data\n" );
		return STRING( models[RandomInt( 0, models.Count() - 1 )] );
	}
	if ( !population || !population[0] )
		population = z_population.GetString();

	KeyValues *areaPopulation = NULL;
	if ( TheZombieManager->GetCurrentCommonSpawnType() == ZombieManager::MOB )
	{
		CFmtStrN< 256 > mobPopulation( "%s_mob", population );
		areaPopulation = m_pPopulation->FindKey( mobPopulation );
	}
	if ( !areaPopulation )
		areaPopulation = m_pPopulation->FindKey( population );
	if ( !areaPopulation )
		areaPopulation = m_pPopulation->FindKey( z_population.GetString() );
	if ( !areaPopulation )
	{
		if ( z_debug_population.GetBool() )
			Warning( "spawning a random zombie in unpopulated area '%s'\n", population );
		return STRING( models[RandomInt( 0, models.Count() - 1 )] );
	}

	const int percent = RandomInt( 1, 100 );
	int cumulative = 0;
	for ( KeyValues *model = areaPopulation->GetFirstSubKey(); model; model = model->GetNextKey() )
	{
		cumulative = (int)( (unsigned int)cumulative + (unsigned int)model->GetInt() );
		if ( cumulative < percent )
			continue;
		const char *name = z_forcezombiemodel.GetBool() ? z_forcezombiemodelname.GetString() : model->GetName();
		CFmtStrN< 256 > filename( "models/infected/%s.mdl", name );
		const int index = FindModel( models, filename );
		if ( index == models.InvalidIndex() )
			continue;
		if ( z_debug_population.GetBool() )
			Msg( "spawning %s in %s, %d%%\n", name, population, percent );
		return STRING( models[index] );
	}
	if ( z_debug_population.GetBool() )
		Warning( "spawning a random zombie in area '%s' - %d%% < 100%%?\n", population, cumulative );
	return STRING( models[RandomInt( 0, models.Count() - 1 )] );
}
} // namespace
void ResetInfectedPopulation()
{
	s_InfectedPopulation.Reset();
}

void ShutdownInfectedPopulation()
{
	s_InfectedPopulation.Shutdown();
}

KeyValues *GetInfectedPopulationDefinition()
{
	return s_InfectedPopulation.GetData();
}

void PrecacheInfectedModels()
{
	s_InfectedPopulation.Precache();
}

const char *SelectInfectedModel( const char *population )
{
	return s_InfectedPopulation.Select( population );
}
