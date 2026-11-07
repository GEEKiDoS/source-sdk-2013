//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/terror_sound_gamedata.h"
#include "l4d2/infected/common/infected_damage_cutouts.h"
#include "filesystem.h"
#include "ifilelist.h"
#include "tier1/KeyValues.h"
#include "tier1/utldict.h"
#include "tier1/utlmap.h"
#include "tier1/utlvector.h"
#include "tier0/memdbgon.h"

extern ISoundEmitterSystemBase *soundemitterbase;

namespace
{
// Native soundemittersystem1500 expands '$gender' first (even when a
// '$survivor' occurs earlier), otherwise the first '$survivor'. The SDK
// provider already expands gender to two tags but leaves survivor literal.
enum SoundNameMacroKind_t
{
	SOUND_MACRO_NONE,
	SOUND_MACRO_GENDER,
	SOUND_MACRO_SURVIVOR
};

SoundNameMacroKind_t FindSoundNameMacro( const char *sample, const char **position, int *length )
{
	*position = Q_stristr( sample, "$gender" );
	if ( *position )
	{
		*length = 7;
		return SOUND_MACRO_GENDER;
	}
	*position = Q_stristr( sample, "$survivor" );
	*length = 9;
	return *position ? SOUND_MACRO_SURVIVOR : SOUND_MACRO_NONE;
}

struct SoundNameMacro_t
{
	bool Split( const char *sample )
	{
		const char *position;
		int length;
		kind = FindSoundNameMacro( sample, &position, &length );
		if ( kind == SOUND_MACRO_NONE )
			return false;
		// Native SplitName retains one 256-character budget across both
		// portions (it does not reset its copied-character count at the
		// macro). The extra byte here holds its terminator without overruns.
		// Copy before AddWaveName can grow the provider's symbol table.
		const int beforeLength = MIN( (int)( position - sample ), 256 );
		Q_strncpy( before, sample, beforeLength + 1 );
		Q_strncpy( after, position + length, 257 - beforeLength );
		return true;
	}
	void Format( const char *replacement, char *output, int outputBytes ) const
	{
		Q_snprintf( output, outputBytes, "%s%s%s", before, replacement, after );
	}
	char before[257];
	char after[257];
	SoundNameMacroKind_t kind;
};

const char *SoundWaveName( const SoundFile &file )
{
	CUtlSymbol symbol = file.symbol; // GetWaveName's legacy API takes non-const.
	return soundemitterbase->GetWaveName( symbol );
}

bool NeedsLegacyGenderAlias( const CSoundParametersInternal &parameters, int index )
{
	const SoundFile &file = parameters.GetSoundNames()[index];
	if ( file.gender != GENDER_MALE && file.gender != GENDER_FEMALE )
		return false;
	const int legacyGender = file.gender + 20;
	if ( index + 1 < parameters.NumSoundNames() )
	{
		const SoundFile &next = parameters.GetSoundNames()[index + 1];
		if ( next.symbol == file.symbol && next.gender == legacyGender )
			return false;
	}
	for ( int i = 0; i < parameters.NumConvertedNames(); ++i )
	{
		SoundNameMacro_t macro;
		if ( !macro.Split( SoundWaveName( parameters.GetConvertedNames()[i] ) ) ||
			macro.kind != SOUND_MACRO_GENDER )
			continue;
		char expanded[256];
		macro.Format( file.gender == GENDER_MALE ? "male" : "female", expanded, sizeof( expanded ) );
		if ( !Q_stricmp( expanded, SoundWaveName( file ) ) )
			return true;
	}
	return false;
}

bool IsRawSurvivorTemplate( const SoundFile &file )
{
	if ( file.gender != GENDER_NONE )
		return false;
	const char *position;
	int length;
	return FindSoundNameMacro( SoundWaveName( file ), &position, &length ) == SOUND_MACRO_SURVIVOR;
}

bool IsFirstConvertedWave( const SoundFile &converted, const SoundFile &wave )
{
	if ( wave.gender != GENDER_MALE && wave.gender != GENDER_GAMBLER )
		return false;
	SoundNameMacro_t macro;
	if ( !macro.Split( SoundWaveName( converted ) ) )
		return false;
	const int firstGender = macro.kind == SOUND_MACRO_GENDER ? GENDER_MALE : GENDER_GAMBLER;
	if ( wave.gender != firstGender )
		return false;
	char expanded[256];
	macro.Format( macro.kind == SOUND_MACRO_GENDER ? "male" : "Gambler", expanded, sizeof( expanded ) );
	return !Q_stricmp( expanded, SoundWaveName( wave ) );
}

void NormalizeSoundNameMacros( CSoundParametersInternal &parameters )
{
	int survivorTemplates = 0;
	int genderAliases = 0;
	for ( int i = 0; i < parameters.NumSoundNames(); ++i )
	{
		survivorTemplates += IsRawSurvivorTemplate( parameters.GetSoundNames()[i] ) ? 1 : 0;
		genderAliases += NeedsLegacyGenderAlias( parameters, i ) ? 1 : 0;
	}
	if ( !survivorTemplates && !genderAliases )
		return;

	// Keep the actual SDK registry/selector/availability owner. Exact final
	// sizes avoid repeated AddToTail reallocations and a second array copy.
	const bool replaceConverted = survivorTemplates != 0;
	CSoundParametersInternal replacement;
	replacement.AllocateWaveNames( static_cast< uint16 >( parameters.NumSoundNames() + 7 * survivorTemplates + genderAliases ),
		static_cast< uint16 >( replaceConverted ? parameters.NumConvertedNames() + survivorTemplates : 0 ) );
	SoundFile *waves = replacement.GetSoundNames();
	SoundFile *converted = replacement.GetConvertedNames();
	int waveCount = 0;
	int convertedCount = 0;
	int oldConverted = 0;
	for ( int i = 0; i < parameters.NumSoundNames(); ++i )
	{
		const SoundFile file = parameters.GetSoundNames()[i];
		// Merge newly recognized survivor templates into the existing native
		// conversion order, including already-normalized prefixes on reload.
		if ( replaceConverted && oldConverted < parameters.NumConvertedNames() &&
			IsFirstConvertedWave( parameters.GetConvertedNames()[oldConverted], file ) )
			converted[convertedCount++] = parameters.GetConvertedNames()[oldConverted++];

		if ( IsRawSurvivorTemplate( file ) )
		{
			SoundNameMacro_t macro;
			CUtlSymbol originalSymbol = file.symbol;
			macro.Split( soundemitterbase->GetWaveName( originalSymbol ) );
			static const char *const names[8] = { "Gambler", "Producer", "Coach", "Mechanic", "NamVet", "TeenGirl", "Biker", "Manager" };
			static const byte genders[8] = { GENDER_GAMBLER, GENDER_PRODUCER, GENDER_COACH, GENDER_MECHANIC,
				GENDER_NAMVET, GENDER_TEENGIRL, GENDER_BIKER, GENDER_MANAGER };
			for ( int actor = 0; actor < ARRAYSIZE( names ); ++actor )
			{
				char expanded[256];
				macro.Format( names[actor], expanded, sizeof( expanded ) );
				SoundFile sound;
				sound.symbol = soundemitterbase->AddWaveName( expanded );
				sound.gender = genders[actor];
				waves[waveCount++] = sound;
			}
			SoundFile original;
			original.symbol = originalSymbol;
			converted[convertedCount++] = original;
		}
		else
		{
			waves[waveCount++] = file;
			if ( NeedsLegacyGenderAlias( parameters, i ) )
			{
				SoundFile alias = file;
				alias.gender += 20; // Native1500 order: male1, male21, female2, female22.
				waves[waveCount++] = alias;
			}
		}
	}
	while ( replaceConverted && oldConverted < parameters.NumConvertedNames() )
		converted[convertedCount++] = parameters.GetConvertedNames()[oldConverted++];
	Assert( waveCount == replacement.NumSoundNames() );
	Assert( convertedCount == replacement.NumConvertedNames() );
	parameters.SwapWaveNameStorage( replacement, replaceConverted );
	parameters.SetUsesGenderToken( true );
	// replacement now owns and frees only the changed old arrays. All packed interval
	// bits, preload/owner/missing flags and existing availability stay intact.
}

struct SoundDefinition_t
{
	SoundDefinition_t() : definition( NULL ), gameData( NULL ), definitionRoot( -1 ), gameDataRoot( -1 ), known( false ) {}
	KeyValues *definition;
	KeyValues *gameData;
	int definitionRoot;
	int gameDataRoot;
	bool known;
};

// One snapshot per native load, not per emission. Older snapshots are retained
// only while a live/replaced entry still refers to them (notably map overrides
// and an in-place reload which omits GameData).
class CSoundScriptSnapshot
{
public:
	CSoundScriptSnapshot( KeyValues *root, int rootIndex ) : m_root( root )
	{
		for ( KeyValues *sound = root; sound; sound = sound->GetNextKey() )
		{
			// ParseScriptFile ignores roots without children. The file is a
			// forest of sound roots, not a wrapper containing all sounds.
			if ( !sound->GetFirstSubKey() )
				continue;

			SoundDefinition_t entry;
			entry.definition = sound;
			entry.definitionRoot = rootIndex;
			entry.known = true;
			KeyValues *gameData = NULL;
			for ( KeyValues *key = sound->GetFirstSubKey(); key; key = key->GetNextKey() )
			{
				if ( !Q_stricmp( key->GetName(), "GameData" ) )
					gameData = key;
			}
			if ( gameData )
			{
				// Original SetGameData (soundemittersystem RVA 8C10) creates
				// an isolated GameData root and copies ONLY its children.
				// A scalar/empty GameData is still a present, empty payload;
				// peers after GameData must never leak into the callback.
				entry.gameData = new KeyValues( "GameData" );
				gameData->CopySubkeys( entry.gameData );
				entry.gameDataRoot = rootIndex;
				m_gameData.AddToTail( entry.gameData );
			}
			const int definition = m_definitions.AddToTail( entry );
			const int found = m_sounds.Find( sound->GetName() );
			if ( found == m_sounds.InvalidIndex() )
				m_sounds.Insert( sound->GetName(), definition );
		}
	}
	~CSoundScriptSnapshot()
	{
		for ( int i = 0; i < m_gameData.Count(); ++i )
			m_gameData[i]->deleteThis();
		m_root->deleteThis();
	}
	const SoundDefinition_t *FindFirst( const char *name ) const
	{
		const int i = m_sounds.Find( name );
		return i == m_sounds.InvalidIndex() ? NULL : &m_definitions[m_sounds[i]];
	}
	KeyValues *m_root;
	CUtlVector<SoundDefinition_t> m_definitions;
private:
	CUtlDict<int, int> m_sounds;
	CUtlVector<KeyValues *> m_gameData;
};

struct SoundEntry_t
{
	SoundEntry_t() : isOverride( false ), hasBase( false ) {}
	SoundDefinition_t active;
	SoundDefinition_t base;
	bool isOverride;
	bool hasBase;
};

struct ScriptFile_t
{
	ScriptFile_t() : root( -1 ), overrideAttempted( false ) {}
	int root;
	bool overrideAttempted;
};

class CSoundGameDataCache
{
public:
	CSoundGameDataCache() : m_files( DefLessFunc( FileNameHandle_t ) ), m_committed( false ), m_reloadFailed( false ) {}
	~CSoundGameDataCache() { Purge(); }

	void Purge()
	{
		InvalidateHandles();
		m_sounds.Purge();
		m_files.Purge();
		m_reloadFiles.Purge();
		m_roots.PurgeAndDeleteElements();
		m_reloadFailed = false;
	}

	void Init()
	{
		Purge();
		for ( int i = soundemitterbase->First(); i != soundemitterbase->InvalidIndex(); i = soundemitterbase->Next( i ) )
		{
			const char *name = soundemitterbase->GetSoundName( i );
			const char *source = soundemitterbase->GetSourceFileForSound( i );
			SoundEntry_t entry;
			if ( source && source[0] )
			{
				const int root = Load( filesystem->FindOrAddFileName( source ), false );
				const SoundDefinition_t *definition = root < 0 ? NULL : m_roots[root]->FindFirst( name );
				if ( definition )
					entry.active = *definition;
				else if ( root >= 0 )
					Warning( "Sound GameData: native entry '%s' is missing from '%s'\n", name, source );
			}
			else
			{
				Warning( "Sound GameData: native entry '%s' has no source script\n", name );
			}
			m_sounds.Insert( name, entry );
		}
		RefreshHandles();
	}

	void InvalidateHandles()
	{
		m_committed = false;
		m_handles.RemoveAll();
	}

	void RefreshHandles()
	{
		m_handles.RemoveAll();
		int largest = -1;
		for ( int i = soundemitterbase->First(); i != soundemitterbase->InvalidIndex(); i = soundemitterbase->Next( i ) )
			largest = MAX( largest, i );
		m_handles.SetCount( largest + 1 );
		for ( int i = 0; i < m_handles.Count(); ++i )
			m_handles[i] = m_sounds.InvalidIndex();
		for ( int i = soundemitterbase->First(); i != soundemitterbase->InvalidIndex(); i = soundemitterbase->Next( i ) )
		{
			const char *name = soundemitterbase->GetSoundName( i );
			int entry = m_sounds.Find( name );
			if ( entry == m_sounds.InvalidIndex() )
			{
				// An unobserved native add/rename is a missing provider, not
				// proof that the new entry has no GameData.
				entry = m_sounds.Insert( name, SoundEntry_t() );
				Warning( "Sound GameData: unobserved native entry '%s'\n", name );
			}
			m_handles[i] = entry;
			CSoundParametersInternal *parameters = soundemitterbase->InternalGetParametersForSound( i );
			if ( parameters )
				NormalizeSoundNameMacros( *parameters );
		}
		m_committed = true;
	}

	bool Cache( const char *path, bool reload )
	{
		const int root = Load( filesystem->FindOrAddFileName( path ), reload );
		if ( reload )
			CollectRoots();
		return root >= 0;
	}

	KeyValues *Definitions( const char *path ) const
	{
		// FindFileName, unlike FindOrAddFileName, never interns a new name
		// on a lookup. Explicit loading belongs to startup/precaching.
		const int file = m_files.Find( filesystem->FindFileName( path ) );
		if ( file == m_files.InvalidIndex() || m_files[file].root < 0 )
			return NULL;
		return m_roots[m_files[file].root]->m_root;
	}

	const SoundDefinition_t *Definition( HSOUNDSCRIPTHANDLE handle ) const
	{
		const unsigned int index = (unsigned short)handle;
		if ( !m_committed || handle == SOUNDEMITTER_INVALID_HANDLE || index >= (unsigned int)m_handles.Count() )
			return NULL;
		const int entry = m_handles[index];
		return entry == m_sounds.InvalidIndex() ? NULL : &m_sounds[entry].active;
	}

	const SoundDefinition_t *Definition( const char *name ) const
	{
		if ( !name || !m_committed )
			return NULL;
		const int index = soundemitterbase->GetSoundIndex( name );
		if ( index < 0 || !soundemitterbase->IsValidIndex( index ) )
			return NULL;
		return Definition( (HSOUNDSCRIPTHANDLE)index );
	}

	void AddOverrides( const char *path )
	{
		const FileNameHandle_t fileName = filesystem->FindOrAddFileName( path );
		const int file = File( fileName );
		if ( m_files[file].overrideAttempted )
		{
			RefreshHandles();
			return;
		}
		// Native AddSoundOverrides records an attempt even if loading fails.
		m_files[file].overrideAttempted = true;
		const int root = Load( fileName, true );
		if ( root >= 0 )
		{
			const CUtlVector<SoundDefinition_t> &definitions = m_roots[root]->m_definitions;
			for ( int i = 0; i < definitions.Count(); ++i )
			{
				const SoundDefinition_t &definition = definitions[i];
				const char *name = definition.definition->GetName();
				const int index = soundemitterbase->GetSoundIndex( name );
				if ( !soundemitterbase->IsValidIndex( index ) )
					continue;
				int entry = m_sounds.Find( name );
				if ( entry == m_sounds.InvalidIndex() )
					entry = m_sounds.Insert( name, SoundEntry_t() );
				else if ( !m_sounds[entry].isOverride )
				{
					m_sounds[entry].base = m_sounds[entry].active;
					m_sounds[entry].hasBase = true;
				}
				m_sounds[entry].isOverride = true;
				// Each override constructs fresh native parameters. Thus the
				// LAST duplicate wins, including removal of old GameData.
				m_sounds[entry].active = definition;
			}
		}
		else
		{
			// A void native override call cannot report whether its read
			// succeeded when our matching metadata read failed. An empty
			// source (also possible for sparse native indices) is unknown.
			for ( int i = soundemitterbase->First(); i != soundemitterbase->InvalidIndex(); i = soundemitterbase->Next( i ) )
			{
				const char *source = soundemitterbase->GetSourceFileForSound( i );
				if ( !source || !source[0] || filesystem->FindOrAddFileName( source ) == fileName )
				{
					const char *name = soundemitterbase->GetSoundName( i );
					int entry = m_sounds.Find( name );
					if ( entry == m_sounds.InvalidIndex() )
						entry = m_sounds.Insert( name, SoundEntry_t() );
					else if ( !m_sounds[entry].isOverride )
					{
						m_sounds[entry].base = m_sounds[entry].active;
						m_sounds[entry].hasBase = true;
					}
					m_sounds[entry].isOverride = true;
					m_sounds[entry].active.known = false;
				}
			}
		}
		RefreshHandles();
		CollectRoots();
	}

	void ClearOverrides()
	{
		for ( int i = m_sounds.First(); i != m_sounds.InvalidIndex(); )
		{
			const int next = m_sounds.Next( i );
			SoundEntry_t &entry = m_sounds[i];
			if ( entry.isOverride )
			{
				if ( entry.hasBase )
				{
					entry.active = entry.base;
					entry.base = SoundDefinition_t();
					entry.hasBase = entry.isOverride = false;
				}
				else
				{
					m_sounds.RemoveAt( i );
				}
			}
			i = next;
		}
		FOR_EACH_MAP( m_files, i )
			m_files[i].overrideAttempted = false;
		// Native clear removes/reinserts entries: old handles need not
		// survive. Rebind by native name, without rereading base scripts.
		RefreshHandles();
		CollectRoots();
	}

	void BeginReload( IFileList *files )
	{
		InvalidateHandles();
		m_reloadFiles.RemoveAll();
		CUtlVector<const char *> visited;
		const int count = soundemitterbase->GetNumSoundScripts();
		for ( int i = 0; i < count; ++i )
		{
			const char *path = soundemitterbase->GetSoundScriptName( i );
			if ( !path || !path[0] || visited.Find( path ) != visited.InvalidIndex() || !files->IsFileInList( path ) )
				continue;
			// Preserve the pinned native reload's POINTER comparison, not
			// a guessed filename comparison. GetSoundScriptName returns a
			// shared buffer in that provider, so later files may be skipped.
			m_reloadFiles.AddToTail( filesystem->FindOrAddFileName( path ) );
			visited.AddToTail( path );
		}
	}

	void EndReload()
	{
		for ( int file = 0; file < m_reloadFiles.Count(); ++file )
		{
			const int root = Load( m_reloadFiles[file], true );
			if ( root < 0 )
			{
				// Reload parses into existing parameters regardless of the
				// entry's recorded source. Without this file we cannot know
				// which other-source names changed. Do not claim absence.
				m_reloadFailed = true;
				continue;
			}
			const CUtlVector<SoundDefinition_t> &definitions = m_roots[root]->m_definitions;
			for ( int i = 0; i < definitions.Count(); ++i )
			{
				const SoundDefinition_t &definition = definitions[i];
				const char *name = definition.definition->GetName();
				if ( !soundemitterbase->IsValidIndex( soundemitterbase->GetSoundIndex( name ) ) )
					continue;
				int entry = m_sounds.Find( name );
				if ( entry == m_sounds.InvalidIndex() )
				{
					entry = m_sounds.Insert( name, SoundEntry_t() );
					m_sounds[entry].active = definition;
				}
				else
				{
					SoundDefinition_t replacement = definition;
					if ( !replacement.gameData )
					{
						// ParseSoundParameters only calls SetGameData when
						// that key occurs. In-place updates do NOT clear an
						// old payload just because a later block omits it.
						replacement.gameData = m_sounds[entry].active.gameData;
						replacement.gameDataRoot = m_sounds[entry].active.gameDataRoot;
						replacement.known = m_sounds[entry].active.known;
					}
					m_sounds[entry].active = replacement;
				}
			}
		}
		m_reloadFiles.RemoveAll();
		RefreshHandles();
		if ( m_reloadFailed )
		{
			FOR_EACH_DICT( m_sounds, i )
				m_sounds[i].active.known = false;
			m_reloadFailed = false;
		}
		CollectRoots();
	}

private:
	int File( FileNameHandle_t name )
	{
		const int file = m_files.Find( name );
		return file == m_files.InvalidIndex() ? m_files.Insert( name, ScriptFile_t() ) : file;
	}

	int Load( FileNameHandle_t name, bool reload )
	{
		const int existing = m_files.Find( name );
		if ( !reload && existing != m_files.InvalidIndex() )
			return m_files[existing].root;
		const int file = File( name );
		char path[512];
		KeyValues *root = new KeyValues( "" );
		if ( !filesystem->String( name, path, sizeof( path ) ) ||
			!filesystem->LoadKeyValues( *root, IFileSystem::TYPE_SOUNDEMITTER, path, "GAME" ) )
		{
			Warning( "Sound GameData: unable to load native sound script '%s'\n", filesystem->String( name, path, sizeof( path ) ) ? path : "<unresolved>" );
			root->deleteThis();
			m_files[file].root = -1;
			return -1;
		}
		int index = m_roots.Find( NULL );
		if ( index == m_roots.InvalidIndex() )
			index = m_roots.AddToTail( NULL );
		m_roots[index] = new CSoundScriptSnapshot( root, index );
		m_files[file].root = index;
		return index;
	}

	void Mark( CUtlVector<bool> &used, const SoundDefinition_t &definition ) const
	{
		if ( definition.definitionRoot >= 0 )
			used[definition.definitionRoot] = true;
		if ( definition.gameDataRoot >= 0 )
			used[definition.gameDataRoot] = true;
	}

	void CollectRoots()
	{
		CUtlVector<bool> used;
		used.SetCount( m_roots.Count() );
		for ( int i = 0; i < used.Count(); ++i )
			used[i] = false;
		FOR_EACH_MAP( m_files, file )
		{
			if ( m_files[file].root >= 0 )
				used[m_files[file].root] = true;
		}
		FOR_EACH_DICT( m_sounds, sound )
		{
			Mark( used, m_sounds[sound].active );
			if ( m_sounds[sound].hasBase )
				Mark( used, m_sounds[sound].base );
		}
		for ( int i = 0; i < m_roots.Count(); ++i )
		{
			if ( !used[i] && m_roots[i] )
			{
				delete m_roots[i];
				m_roots[i] = NULL;
			}
		}
	}

	CUtlMap<FileNameHandle_t, ScriptFile_t, int> m_files;
	CUtlDict<SoundEntry_t, int> m_sounds;
	CUtlVector<CSoundScriptSnapshot *> m_roots;
	CUtlVector<int> m_handles;
	CUtlVector<FileNameHandle_t> m_reloadFiles;
	bool m_committed;
	bool m_reloadFailed;
};

CSoundGameDataCache s_soundGameData;
}

gender_t GetTerrorSoundActorGender( const char *actorModel )
{
	// Reuse the original catalog already mounted/owned for base actor types;
	// do not replace the entity/model query with survivor-team inference.
	return static_cast< gender_t >( InfectedModelTypeForModel( actorModel ) );
}

void ExpandTerrorSoundActorString( gender_t gender, const char *input, char *output, int outputBytes )
{
	Q_strncpy( output, input, outputBytes );
	// Native1B30 deliberately leaves 7..20 and unknown types unchanged,
	// even though parse-time '$survivor' expansion includes the L4D2 four.
	const char *replacement = NULL;
	switch ( gender )
	{
	case GENDER_MALE:
	case GENDER_MALE_L4D1: replacement = "male"; break;
	case GENDER_FEMALE:
	case GENDER_FEMALE_L4D1: replacement = "female"; break;
	case GENDER_NAMVET: replacement = "namvet"; break;
	case GENDER_TEENGIRL: replacement = "teengirl"; break;
	case GENDER_BIKER: replacement = "biker"; break;
	case GENDER_MANAGER: replacement = "manager"; break;
	}
	if ( !replacement )
		return;
	SoundNameMacro_t macro;
	if ( macro.Split( input ) )
		macro.Format( replacement, output, outputBytes );
}

SoundGameDataResult_t GetSoundGameData( HSOUNDSCRIPTHANDLE handle, KeyValues **gameData )
{
	const SoundDefinition_t *definition = s_soundGameData.Definition( handle );
	*gameData = NULL;
	if ( !definition || !definition->known )
		return SOUND_GAMEDATA_UNAVAILABLE;
	*gameData = definition->gameData;
	return *gameData ? SOUND_GAMEDATA_PRESENT : SOUND_GAMEDATA_ABSENT;
}

KeyValues *GetSoundScriptDefinition( const char *name )
{
	const SoundDefinition_t *definition = s_soundGameData.Definition( name );
	return definition ? definition->definition : NULL;
}

KeyValues *GetSoundScriptGameData( const char *name )
{
	const SoundDefinition_t *definition = s_soundGameData.Definition( name );
	return definition && definition->known ? definition->gameData : NULL;
}

KeyValues *GetSoundScriptDefinitions( const char *path ) { return s_soundGameData.Definitions( path ); }
bool CacheSoundScriptDefinitions( const char *path, bool reload ) { return s_soundGameData.Cache( path, reload ); }
void InitSoundGameData() { s_soundGameData.Init(); }
void PurgeSoundGameData() { s_soundGameData.Purge(); }
void InvalidateSoundGameDataHandles() { s_soundGameData.InvalidateHandles(); }
void RefreshSoundGameDataHandles() { s_soundGameData.RefreshHandles(); }
void AddSoundGameDataOverrides( const char *path ) { s_soundGameData.AddOverrides( path ); }
void ClearSoundGameDataOverrides() { s_soundGameData.ClearOverrides(); }
void BeginSoundGameDataReload( IFileList *files ) { s_soundGameData.BeginReload( files ); }
void EndSoundGameDataReload() { s_soundGameData.EndReload(); }
