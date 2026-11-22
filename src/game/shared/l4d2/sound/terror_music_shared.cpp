//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/sound/terror_sound_gamedata.h"
#include "igamesystem.h"
#include "tier1/KeyValues.h"
#include "tier1/utlmap.h"
#include "tier1/checksum_crc.h"
#include "tier1/utldict.h"
#include "tier2/riff.h"
#include "soundchars.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "filesystem.h"
#ifdef CLIENT_DLL
#include "c_world.h"
#include "mapentities_shared.h"
#include "l4d2/gamerules/terror_gamerules.h"
#endif
#include "tier0/memdbgon.h"

extern ISoundEmitterSystemBase *soundemitterbase;

// Original observable console surface, shared and replicated in both DLLs.
// No direct MusicMaster read was recovered; do not invent a gameplay override.
static ConVar music_intensity_override( "music_intensity_override", "-1",
	
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Overrides the player's music intensity track for testing" );
static ConVar music_intensity_threshold( "music_intensity_threshold", "0.3",
	
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
static CUtlDict<bool, int> s_musicWaveLoops;

#ifdef CLIENT_DLL
static bool IsHordeMusicEvent( const char *name, const char *style, bool legacy )
{
	if ( !V_stricmp( name, "Event.Mobbed" ) || !V_stricmp( name, "Event.AmbientMob" ) ||
		!V_stricmp( name, legacy ? "Event.ZombieChoir_L4D1" : "Event.ZombieChoir" ) )
		return true;
	if ( V_strnicmp( name, "Event.Zombat", 12 ) && V_strnicmp( name, "Event.MobSignal", 15 ) &&
		V_strnicmp( name, "Event.DangerAtmosphere", 22 ) )
		return false;
	const char *suffix = V_strrchr( name, '_' );
	if ( !suffix )
		return false;
	++suffix;
	if ( !V_stricmp( suffix, style ) || ( legacy && !V_stricmp( suffix, "L4D1" ) ) )
		return true;
	// Numbered zombat tracks and their A/B continuations are shared by L4D2 campaigns.
	return !legacy && !V_strnicmp( name, "Event.Zombat_", 13 ) &&
		*suffix && strspn( suffix, "0123456789" ) == strlen( suffix );
}

static bool IsHordeEffectEvent( const char *name )
{
	return !V_strnicmp( name, "Zombie.", 7 ) || !V_stricmp( name, "MegaMobIncoming" ) ||
		!V_strnicmp( name, "Melee.Hit", 9 ) || !V_stricmp( name, "Blood.Spurt" );
}
#endif

// GameData has one native sound-registry lifecycle owner. Music owns only
// transport hashes and parsed wave-loop flags, never borrowed GameData trees.
class CMusicSoundHashes : public CAutoGameSystem
{
public:
	CMusicSoundHashes() : CAutoGameSystem( "MusicSoundHashes" ), m_hashes( DefLessFunc( unsigned int ) ), m_built( false ) {}
	void Invalidate() { m_hashes.RemoveAll(); m_built = false; s_musicWaveLoops.RemoveAll(); }
	virtual void LevelShutdownPostEntity() { Invalidate(); }
#ifdef CLIENT_DLL
	void LevelInitPostEntity() override
	{
		// Sign-on may not have delivered the world entity yet. The authored
		// worldspawn provides the same musicpostfix without guessing a campaign.
		char authored[MAPKEY_MAXLENGTH] = {}, token[MAPKEY_MAXLENGTH];
		const C_World *world = GetClientWorldEntity();
		const char *style = world ? world->GetMusicPostfix() : "";
		if ( !*style && engine->GetMapEntitiesString() )
		{
			const char *entity = MapEntity_ParseToken( engine->GetMapEntitiesString(), token );
			if ( entity && MapEntity_ExtractValue( entity, "musicpostfix", authored ) )
				style = authored;
		}
		if ( TerrorGameRules() && TerrorGameRules()->IsSurvivalMode() )
			style = "Survival";
		const bool legacy = TerrorGameRules() && TerrorGameRules()->GetSurvivorSet() == 1;
		// Separate caps keep long music tracks from starving short infected
		// effects. Charge mounted WAV bytes, not a guess at engine residency.
		const unsigned budget[] = { 32u << 20, 32u << 20 };
		unsigned bytes[2] = {};
		int waves[2] = {};
		CUtlDict<bool, int> seen;
		for ( int i = soundemitterbase->First(); i != soundemitterbase->InvalidIndex(); i = soundemitterbase->Next( i ) )
		{
			const char *name = soundemitterbase->GetSoundName( i );
			const bool music = IsHordeMusicEvent( name, style, legacy );
			const bool effect = IsHordeEffectEvent( name );
			// Surface-script footstep names vary; their wave family is stable.
			if ( !music && !effect && !V_stristr( name, "step" ) && !V_stristr( name, "run" ) && !V_stristr( name, "walk" ) )
				continue;
			CSoundParametersInternal *params = soundemitterbase->InternalGetParametersForSound( i );
			if ( !params )
				continue;
			for ( int j = 0; j < params->NumSoundNames(); ++j )
			{
				const char *sample = soundemitterbase->GetWaveName( params->GetSoundNames()[j].symbol );
				char wave[MAX_PATH], path[MAX_PATH];
				V_strncpy( wave, PSkipSoundChars( sample ), sizeof( wave ) );
				V_FixSlashes( wave, '/' );
				if ( !music && !effect && V_strnicmp( wave, "player/footsteps/infected/", 26 ) )
					continue;
				if ( seen.Find( wave ) != seen.InvalidIndex() )
					continue;
				seen.Insert( wave, true );
				V_snprintf( path, sizeof( path ), "sound/%s", wave );
				const unsigned size = filesystem->Size( path, "GAME" );
				if ( !size || size > budget[music] - bytes[music] )
					continue;
				CBaseEntity::PrefetchSound( sample );
				if ( music )
					IsMusicSoundLooping( wave );
				bytes[music] += size;
				++waves[music];
			}
		}
		DevMsg( "Horde audio warmup: style %s, %d effect waves (%u bytes), %d music waves (%u bytes)\n",
			*style ? style : "<shared>", waves[0], bytes[0], waves[1], bytes[1] );
	}
#endif
	const char *Find( unsigned int hash )
	{
		if ( !m_built )
		{
			for ( int i = soundemitterbase->First(); i != soundemitterbase->InvalidIndex(); i = soundemitterbase->Next( i ) )
			{
				const char *name = soundemitterbase->GetSoundName( i );
				m_hashes.InsertOrReplace( GetMusicSoundNameHash( name ), name );
			}
			m_built = true;
		}
		int index = m_hashes.Find( hash );
		return index == m_hashes.InvalidIndex() ? NULL : m_hashes[index];
	}
private:
	CUtlMap<unsigned int, const char *> m_hashes;
	bool m_built;
};
static CMusicSoundHashes s_musicHashes;

KeyValues *GetMusicScripts() { return GetSoundScriptDefinitions( "scripts/game_sounds_music.txt" ); }
KeyValues *GetMusicScript( const char *name ) { return GetSoundScriptDefinition( name ); }
void InvalidateMusicSoundHashes() { s_musicHashes.Invalidate(); }
unsigned int GetMusicSoundNameHash( const char *name )
{
	// Original soundemittersystem.dll 0x2170: CRC32 over strlen bytes, no
	// lowercasing and no terminating NUL. Init=~0 and final bitwise complement.
	CRC32_t hash;
	CRC32_Init( &hash );
	CRC32_ProcessBuffer( &hash, name, Q_strlen( name ) );
	CRC32_Final( &hash );
	return hash;
}
const char *GetMusicSoundNameForHash( unsigned int hash ) { return s_musicHashes.Find( hash ); }
bool MusicScriptHasParameter( const char *name, const char *parameter )
{
	KeyValues *data = GetSoundScriptGameData( name );
	return data && Q_stristr( data->GetString( "MusicParameters", "" ), parameter ) != NULL;
}
void PrecacheMusicScripts()
{
#if defined( GAME_DLL )
	// Native S2AB6D0 owns a temporary KeyValues tree, not the GameData cache.
	KeyValues *sounds = new KeyValues( "game_sounds_music.txt" );
	if ( sounds->LoadFromFile( filesystem, "scripts/game_sounds_music.txt", NULL ) )
	{
		KeyValues *sound = sounds;
		while ( sound )
		{
			// The original advances only child-bearing keys (S2AB731-S2AB80D).
			if ( sound->GetFirstSubKey() )
			{
				const char *name = sound->GetName();
				AllocPooledString( name );
				CBaseEntity::PrecacheScriptSound( name );
				int index = soundemitterbase->GetSoundIndex( name );
				if ( soundemitterbase->IsValidIndex( index ) )
				{
					CSoundParametersInternal *parameters = soundemitterbase->InternalGetParametersForSound( index );
					if ( parameters )
					{
						for ( int wave = 0; wave < parameters->NumSoundNames(); ++wave )
						{
							AllocPooledString( soundemitterbase->GetWaveName( parameters->GetSoundNames()[wave].symbol ) );
						}
					}
				}
				sound = sound->GetNextKey();
			}
		}
	}
	sounds->deleteThis();
#else
	if ( !CacheSoundScriptDefinitions( "scripts/game_sounds_music.txt" ) ) return;
	for ( KeyValues *sound = GetMusicScripts(); sound; sound = sound->GetNextKey() )
	{
		CBaseEntity::PrecacheScriptSound( sound->GetName() );
		int index = soundemitterbase->GetSoundIndex( sound->GetName() );
		if ( !soundemitterbase->IsValidIndex( index ) ) continue;
		CSoundParametersInternal *parameters = soundemitterbase->InternalGetParametersForSound( index );
		if ( parameters )
			for ( int wave = 0; wave < parameters->NumSoundNames(); ++wave )
				CBaseEntity::PrecacheSound( soundemitterbase->GetWaveName( parameters->GetSoundNames()[wave].symbol ) );
	}
#endif
}

class CMusicWaveRead : public IFileReadBinary
{
public:
	virtual FileHandle_t open( const char *name ) { return filesystem->Open( name, "rb", "GAME" ); }
	virtual int read( void *out, int size, FileHandle_t file ) { return filesystem->Read( out, size, file ); }
	virtual void close( FileHandle_t file ) { filesystem->Close( file ); }
	virtual void seek( FileHandle_t file, int position ) { filesystem->Seek( file, position, FILESYSTEM_SEEK_HEAD ); }
	virtual unsigned int tell( FileHandle_t file ) { return filesystem->Tell( file ); }
	virtual unsigned int size( FileHandle_t file ) { return filesystem->Size( file ); }
};
bool IsMusicSoundLooping( const char *wave )
{
	wave = PSkipSoundChars( wave );
	int cached = s_musicWaveLoops.Find( wave );
	if ( cached != s_musicWaveLoops.InvalidIndex() ) return s_musicWaveLoops[cached];
	CMusicWaveRead io;
	char filename[MAX_PATH];
	Q_snprintf( filename, sizeof( filename ), "sound/%s", wave );
	InFileRIFF riff( filename, io );
	int loopStart = -1;
	if ( riff.IsValid() && riff.RIFFName() == RIFF_WAVE )
	{
		// Original CAudioSourceWave::ParseChunk engine0x3B390 and IsLooped
		// 0x3A320. Chunk order is significant; the last accepted cue/sampler wins.
		IterateRIFF chunk( riff, riff.RIFFSize() );
		while ( chunk.ChunkAvailable() )
		{
			if ( chunk.ChunkName() == WAVE_CUE && chunk.ChunkReadInt() > 0 )
			{
				unsigned int cue[6];
				chunk.ChunkReadPartial( cue, sizeof( cue ) );
				loopStart = (int)cue[5];
			}
			else if ( chunk.ChunkName() == WAVE_SAMPLER )
			{
				unsigned int sampler[15];
				chunk.ChunkReadPartial( sampler, sizeof( sampler ) );
				if ( sampler[7] != 0 && sampler[10] == 0 ) loopStart = (int)sampler[11];
			}
			chunk.ChunkNext();
		}
	}
	const bool loops = loopStart >= 0;
	s_musicWaveLoops.Insert( wave, loops );
	return loops;
}
