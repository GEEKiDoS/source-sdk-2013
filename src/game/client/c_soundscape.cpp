//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Soundscapes.txt resource file processor
//
// $NoKeywords: $
//=============================================================================//


#include "cbase.h"
#include <KeyValues.h>
#include "engine/IEngineSound.h"
#include "filesystem.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "soundchars.h"
#include "l4d2/sound/terror_engine_sound_name.h"
#include "view.h"
#include "engine/ivdebugoverlay.h"
#include "tier0/icommandline.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Only allow recursive references to be 8 levels deep.
// This test will flag any circular references and bail.
#define MAX_SOUNDSCAPE_RECURSION	8

const float DEFAULT_SOUND_RADIUS = 36.0f;
// Keep an array of all looping sounds so they can be faded in/out
// OPTIMIZE: Get a handle/pointer to the engine's sound channel instead 
//			of searching each frame!
struct loopingsound_t
{
	Vector		position;		// position (if !isAmbient)
	const char *pWaveName;		// name of the wave file
	float		volumeTarget;	// target volume level (fading towards this)
	float		volumeCurrent;	// current volume level
	soundlevel_t soundlevel;	// sound level (if !isAmbient)
	int			pitch;			// pitch shift
	int			id;				// Used to fade out sounds that don't belong to the most current setting
	bool		isAmbient;		// Ambient sounds have no spatialization - they play from everywhere
#if defined( L4D2 )
	float		volumeStart;
	float		fadeRate;
	float		fadeFraction;
	bool		smoothFade;
	int			guid;
	float		radius;
#endif
};

ConVar soundscape_fadetime( "soundscape_fadetime", "3.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
, "Time to crossfade sound effects between soundscapes" );

#if defined( L4D2 )
extern ConVar soundscape_radius_debug;
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
#endif

#include "interval.h"

#if defined( L4D2 )
#include <emmintrin.h>

// C137480 returns the interval sum in x87. Preserve it until the caller's
// actual float store, rather than changing the shared SDK interval ABI.
static double SoundscapeRandomInterval( const interval_t &interval )
{
	return interval.range != 0.0f ?
		static_cast< double >( RandomFloat( 0.0f, interval.range ) ) + interval.start : interval.start;
}

// Native C47F370's interval conversion takes the CVTTSD2SI signed32 path.
static int SoundscapeIntervalToInt( double value )
{
	return _mm_cvttsd_si32( _mm_set_sd( value ) );
}
#endif

struct randomsound_t
{
	Vector		position;
	float		nextPlayTime;	// time to play a sound from the set
	interval_t	time;
	interval_t	volume;
	interval_t	pitch;
	interval_t	soundlevel;
	float		masterVolume;
	int			waveCount;
	bool		isAmbient;
	bool		isRandom;
	KeyValues	*pWaves;

	void Init()
	{
		memset( this, 0, sizeof(*this) );
	}
};

struct subsoundscapeparams_t
{
	int		recurseLevel;		// test for infinite loops in the script / circular refs
	float	masterVolume;
#if defined( L4D2 )
	float	fadeRate;
#endif
	int		startingPosition;
	int		positionOverride;	// forces all sounds to this position
	int		ambientPositionOverride;	// forces all ambient sounds to this position
	bool	allowDSP;
	bool	wroteSoundMixer;
	bool	wroteDSPVolume;
};

class C_SoundscapeSystem : public CBaseGameSystemPerFrame
{
public:
	virtual char const *Name() { return "C_SoundScapeSystem"; }

	C_SoundscapeSystem()
	{
		m_nRestoreFrame = -1;
	}

	~C_SoundscapeSystem() {}

	void OnStopAllSounds()
	{
		m_params.entIndex = 0;
		m_params.soundscapeIndex = -1;
		m_loopingSounds.Purge();
		m_randomSounds.Purge();
	}

	// IClientSystem hooks, not needed
	virtual void LevelInitPreEntity()
	{
		Shutdown();
		Init();

		TouchSoundFiles();
	}

	virtual void LevelInitPostEntity() 
	{
		if ( !m_pSoundMixerVar )
		{
			m_pSoundMixerVar = (ConVar *)cvar->FindVar( "snd_soundmixer" );
		}
		if ( !m_pDSPVolumeVar )
		{
			m_pDSPVolumeVar = (ConVar *)cvar->FindVar( "dsp_volume" );
		}
	}

	// The level is shutdown in two parts
	virtual void LevelShutdownPreEntity() {}
	// Entities are deleted / released here...
	virtual void LevelShutdownPostEntity()
	{
		OnStopAllSounds();
	}

	virtual void OnSave() {}
	virtual void OnRestore()
	{
		m_nRestoreFrame = gpGlobals->framecount;
	}
	virtual void SafeRemoveIfDesired() {}

	// Called before rendering
	virtual void PreRender() { }

	// Called after rendering
	virtual void PostRender() { }

	// IClientSystem hooks used
	virtual bool Init();
	virtual void Shutdown();
	// Gets called each frame
	virtual void Update( float frametime );

	void PrintDebugInfo()
	{
		Msg( "\n------- CLIENT SOUNDSCAPES -------\n" );
		for ( int i=0; i < m_soundscapes.Count(); i++ )
		{
			Msg( "- %d: %s\n", i, m_soundscapes[i]->GetName() );
		}
		if ( m_forcedSoundscapeIndex >= 0 )
		{
			Msg( "- PLAYING DEBUG SOUNDSCAPE: %d [%s]\n", m_forcedSoundscapeIndex, SoundscapeNameByIndex(m_forcedSoundscapeIndex) );
		}
		Msg( "- CURRENT SOUNDSCAPE: %d [%s]\n", m_params.soundscapeIndex.Get(), SoundscapeNameByIndex(m_params.soundscapeIndex) );
		Msg( "----------------------------------\n\n" );
	}

	
	// local functions
	void UpdateAudioParams( audioparams_t &audio );
	void GetAudioParams( audioparams_t &out ) const { out = m_params; }
	int GetCurrentSoundscape() 
	{ 
		if ( m_forcedSoundscapeIndex >= 0 )
			return m_forcedSoundscapeIndex;
		return m_params.soundscapeIndex; 
	}
	void DevReportSoundscapeName( int index );
	void UpdateLoopingSounds( float frametime );
#if defined( L4D2 )
	int AddLoopingAmbient( const char *pSoundName, float volume, int pitch, float radius, float fadeRate );
#else
	int AddLoopingAmbient( const char *pSoundName, float volume, int pitch );
#endif
	void UpdateLoopingSound( loopingsound_t &loopSound );
	void StopLoopingSound( loopingsound_t &loopSound );
	int AddLoopingSound( const char *pSoundName, bool isAmbient, float volume,
#if defined( L4D2 )
		soundlevel_t soundLevel, int pitch, const Vector &position, float radius, float fadeRate );
#else
		soundlevel_t soundLevel, int pitch, const Vector &position );
#endif
	int AddRandomSound( const randomsound_t &sound );
	void PlayRandomSound( randomsound_t &sound );
	void UpdateRandomSounds( float gameClock );
	Vector GenerateRandomSoundPosition();

	void ForceSoundscape( const char *pSoundscapeName, float radius );

	int FindSoundscapeByName( const char *pSoundscapeName );
	const char *SoundscapeNameByIndex( int index );
	KeyValues *SoundscapeByIndex( int index );
	
	// main-level soundscape processing, called on new soundscape
	void StartNewSoundscape( KeyValues *pSoundscape );
	void StartSubSoundscape( KeyValues *pSoundscape, subsoundscapeparams_t &params );

	// root level soundscape keys
	// add a process for each new command here
	// "dsp"
	void ProcessDSP( KeyValues *pDSP );
	// "dsp_player"
	void ProcessDSPPlayer( KeyValues *pDSPPlayer );
	// "playlooping"
	void ProcessPlayLooping( KeyValues *pPlayLooping, const subsoundscapeparams_t &params );	
	// "playrandom"
	void ProcessPlayRandom( KeyValues *pPlayRandom, const subsoundscapeparams_t &params );
	// "playsoundscape"
	void ProcessPlaySoundscape( KeyValues *pPlaySoundscape, subsoundscapeparams_t &params );
	// "soundmixer"
	void ProcessSoundMixer( KeyValues *pSoundMixer, subsoundscapeparams_t &params );
	// "dsp_volume"
	void ProcessDSPVolume( KeyValues *pKey, subsoundscapeparams_t &params );


private:

	bool	IsBeingRestored() const
	{
		return gpGlobals->framecount == m_nRestoreFrame ? true : false;
	}

	void	AddSoundScapeFile( const char *filename );

	void		TouchPlayLooping( KeyValues *pAmbient );
	void		TouchPlayRandom( KeyValues *pPlayRandom );
	void		TouchWaveFiles( KeyValues *pSoundScape );
	void		TouchSoundFile( char const *wavefile );

	void		TouchSoundFiles();
	
	int							m_nRestoreFrame;

	CUtlVector< KeyValues * >	m_SoundscapeScripts;	// The whole script file in memory
	CUtlVector<KeyValues *>		m_soundscapes;			// Lookup by index of each root section
	audioparams_t				m_params;				// current player audio params
	CUtlVector<loopingsound_t>	m_loopingSounds;		// list of currently playing sounds
	CUtlVector<randomsound_t>	m_randomSounds;			// list of random sound commands
	float						m_nextRandomTime;		// next time to play a random sound
	int							m_loopingSoundId;		// marks when the sound was issued
	int							m_forcedSoundscapeIndex;// >= 0 if this a "forced" soundscape? i.e. debug mode?
	float						m_forcedSoundscapeRadius;// distance to spatialized sounds

	static ConVar *m_pDSPVolumeVar;
	static ConVar *m_pSoundMixerVar;

};


// singleton system
C_SoundscapeSystem g_SoundscapeSystem;
ConVar *C_SoundscapeSystem::m_pDSPVolumeVar = NULL;
ConVar *C_SoundscapeSystem::m_pSoundMixerVar = NULL;

IGameSystem *ClientSoundscapeSystem()
{
	return &g_SoundscapeSystem;
}


void Soundscape_OnStopAllSounds()
{
	g_SoundscapeSystem.OnStopAllSounds();
}


// player got a network update
void Soundscape_Update( audioparams_t &audio )
{
	g_SoundscapeSystem.UpdateAudioParams( audio );
}

#define SOUNDSCAPE_MANIFEST_FILE				"scripts/soundscapes_manifest.txt"

void C_SoundscapeSystem::AddSoundScapeFile( const char *filename )
{
	KeyValues *script = new KeyValues( filename );
#if !defined( _XBOX ) && !defined( L4D2 )
	if ( script->LoadFromFile( filesystem, filename ) )
#else
	if ( filesystem->LoadKeyValues( *script, IFileSystem::TYPE_SOUNDSCAPE, filename, "GAME" ) )
#endif
	{
		// parse out all of the top level sections and save their names
		KeyValues *pKeys = script;
		while ( pKeys )
		{
			// save pointers to all sections in the root
			// each one is a soundscape
			if ( pKeys->GetFirstSubKey() )
			{
				m_soundscapes.AddToTail( pKeys );
			}
			pKeys = pKeys->GetNextKey();
		}

		// Keep pointer around so we can delete it at exit
		m_SoundscapeScripts.AddToTail( script );
	}
	else
	{
		script->deleteThis();
	}
}

// parse the script file, setup index table
bool C_SoundscapeSystem::Init()
{
	m_loopingSoundId = 0;

	const char *mapname = MapName();
	const char *mapSoundscapeFilename = NULL;
	if ( mapname && *mapname )
	{
		mapSoundscapeFilename = VarArgs( "scripts/soundscapes_%s.txt", mapname );
	}

	KeyValues *manifest = new KeyValues( SOUNDSCAPE_MANIFEST_FILE );
	if ( filesystem->LoadKeyValues( *manifest, IFileSystem::TYPE_SOUNDSCAPE, SOUNDSCAPE_MANIFEST_FILE, "GAME" ) )
	{
		for ( KeyValues *sub = manifest->GetFirstSubKey(); sub != NULL; sub = sub->GetNextKey() )
		{
			if ( !Q_stricmp( sub->GetName(), "file" ) )
			{
				// Add
				AddSoundScapeFile( sub->GetString() );
				if ( mapSoundscapeFilename && FStrEq( sub->GetString(), mapSoundscapeFilename ) )
				{
					mapSoundscapeFilename = NULL; // we've already loaded the map's soundscape
				}
				continue;
			}

			Warning( "C_SoundscapeSystem::Init:  Manifest '%s' with bogus file type '%s', expecting 'file'\n", 
				SOUNDSCAPE_MANIFEST_FILE, sub->GetName() );
		}

		if ( mapSoundscapeFilename && filesystem->FileExists( mapSoundscapeFilename ) )
		{
			AddSoundScapeFile( mapSoundscapeFilename );
		}
	}
	else
	{
		Error( "Unable to load manifest file '%s'\n", SOUNDSCAPE_MANIFEST_FILE );
	}

	manifest->deleteThis();

	return true;
}


int C_SoundscapeSystem::FindSoundscapeByName( const char *pSoundscapeName )
{
	// UNDONE: Bad perf, linear search!
	for ( int i = m_soundscapes.Count()-1; i >= 0; --i )
	{
		if ( !Q_stricmp( m_soundscapes[i]->GetName(), pSoundscapeName ) )
			return i;
	}

	return -1;
}

KeyValues *C_SoundscapeSystem::SoundscapeByIndex( int index )
{
	if ( m_soundscapes.IsValidIndex(index) )
		return m_soundscapes[index];
	return NULL;
}

const char *C_SoundscapeSystem::SoundscapeNameByIndex( int index )
{
	if ( index < m_soundscapes.Count() )
	{
		return m_soundscapes[index]->GetName();
	}

	return NULL;
}

void C_SoundscapeSystem::Shutdown()
{
	for ( int i = m_loopingSounds.Count() - 1; i >= 0; --i )
	{
		loopingsound_t &sound = m_loopingSounds[i];

		// sound is done, remove from list.
		StopLoopingSound( sound );
	}
	
	// These are only necessary so we can use shutdown/init calls
	// to flush soundscape data
	m_loopingSounds.RemoveAll();
	m_randomSounds.RemoveAll();
	m_soundscapes.RemoveAll();
	m_params.entIndex = 0;
	m_params.soundscapeIndex = -1;

	while ( m_SoundscapeScripts.Count() > 0 )
	{
		KeyValues *kv = m_SoundscapeScripts[ 0 ];
		m_SoundscapeScripts.Remove( 0 );
		kv->deleteThis();
	}
}

// NOTE: This will not flush the server side so you cannot add or remove
// soundscapes from the list, only change their parameters!!!!

#if defined( L4D2 )
CON_COMMAND_F(cl_soundscape_flush, "Flushes the client side soundscapes", 0x10004008)
#else
CON_COMMAND_F(cl_soundscape_flush, "Flushes the client side soundscapes", FCVAR_SERVER_CAN_EXECUTE|FCVAR_CHEAT)
#endif
{
	// save the current soundscape
	audioparams_t tmp;
	g_SoundscapeSystem.GetAudioParams( tmp );

	// kill the system
	g_SoundscapeSystem.Shutdown();

	// restart the system
	g_SoundscapeSystem.Init();

	// reload the soundscape params from the temp copy
	Soundscape_Update( tmp );
}


static int SoundscapeCompletion( const char *partial, char commands[ COMMAND_COMPLETION_MAXITEMS ][ COMMAND_COMPLETION_ITEM_LENGTH ] )
{
	int current = 0;

	const char *cmdname = "playsoundscape";
	char *substring = NULL;
	int substringLen = 0;
	if ( Q_strstr( partial, cmdname ) && strlen(partial) > strlen(cmdname) + 1 )
	{
		substring = (char *)partial + strlen( cmdname ) + 1;
		substringLen = strlen(substring);
	}
	
	int i = 0;
	const char *pSoundscapeName = g_SoundscapeSystem.SoundscapeNameByIndex( i );
	while ( pSoundscapeName && current < COMMAND_COMPLETION_MAXITEMS )
	{
		if ( !substring || !Q_strncasecmp( pSoundscapeName, substring, substringLen ) )
		{
			Q_snprintf( commands[ current ], sizeof( commands[ current ] ), "%s %s", cmdname, pSoundscapeName );
			current++;
		}
		i++;
		pSoundscapeName = g_SoundscapeSystem.SoundscapeNameByIndex( i );
	}

	return current;
}


#if defined( L4D2 )
CON_COMMAND_F_COMPLETION( playsoundscape, "Forces a soundscape to play", 0x00004008, SoundscapeCompletion )
#else
CON_COMMAND_F_COMPLETION( playsoundscape, "Forces a soundscape to play", FCVAR_CHEAT, SoundscapeCompletion )
#endif
{
	if ( args.ArgC() < 2 )
	{
		g_SoundscapeSystem.DevReportSoundscapeName( g_SoundscapeSystem.GetCurrentSoundscape() );
		return;
	}
	const char *pSoundscapeName = args[1];
	float radius = args.ArgC() > 2 ? atof( args[2] ) : DEFAULT_SOUND_RADIUS;
	g_SoundscapeSystem.ForceSoundscape( pSoundscapeName, radius );
}



#if defined( L4D2 )
CON_COMMAND_F( stopsoundscape, "Stops all soundscape processing and fades current looping sounds", 0x00004008 )
#else
CON_COMMAND_F( stopsoundscape, "Stops all soundscape processing and fades current looping sounds", FCVAR_CHEAT )
#endif
{
	g_SoundscapeSystem.StartNewSoundscape( NULL );
}

void C_SoundscapeSystem::ForceSoundscape( const char *pSoundscapeName, float radius )
{
	int index = g_SoundscapeSystem.FindSoundscapeByName( pSoundscapeName );
	if ( index >= 0 )
	{
		m_forcedSoundscapeIndex = index;
		m_forcedSoundscapeRadius = radius;
		g_SoundscapeSystem.StartNewSoundscape( SoundscapeByIndex(index) );
	}
	else
	{
		DevWarning("Can't find soundscape %s\n", pSoundscapeName );
	}
}

void C_SoundscapeSystem::DevReportSoundscapeName( int index )
{
	const char *pName = "none";
	if ( index >= 0 && index < m_soundscapes.Count() )
	{
		pName = m_soundscapes[index]->GetName();
	}
	DevMsg( 1, "Soundscape: %s\n", pName  );
}


// This makes all currently playing loops fade toward their target volume
void C_SoundscapeSystem::UpdateLoopingSounds( float frametime )
{
#if defined( L4D2 )
	// C9C420: each loop owns a normalized sine/cosine fade, not an absolute
	// Approach step. Radius gain is game-owned and uses the local actor origin.
	for ( int i = m_loopingSounds.Count() - 1; i >= 0; --i )
	{
		loopingsound_t &sound = m_loopingSounds[i];
		const bool changed = sound.volumeCurrent != sound.volumeTarget;
		C_BasePlayer *player = sound.radius > 0.0f ? C_BasePlayer::GetLocalPlayer() : NULL;
		if ( player )
		{
			const Vector delta = player->GetAbsOrigin() - sound.position;
			const float range = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
			if ( range > sound.radius * 100.0f )
			{
				sound.volumeStart = sound.volumeCurrent;
				sound.volumeTarget = 0.01f;
				sound.fadeFraction = 0.0f;
				sound.fadeRate = 1.0f;
				sound.smoothFade = false;
			}
			else
			{
				float volume = 1.0f;
				if ( range >= sound.radius )
					volume = 1.0f / ( ( range - sound.radius ) * 0.5f / sound.radius + 1.0f );
				sound.volumeTarget = sound.volumeCurrent = volume;
				sound.fadeFraction = 1.0f;
				sound.fadeRate = 0.0f;
			}
			if ( soundscape_radius_debug.GetInt() != 0 )
				DevMsg( 1, "Updated looping radius sound %d to vol=%f\n", i, sound.volumeTarget );
		}
		else if ( !changed )
		{
			continue;
		}
		sound.fadeFraction = sound.fadeRate * frametime + sound.fadeFraction;
		if ( sound.fadeFraction >= 1.0f )
		{
			sound.volumeCurrent = sound.volumeTarget;
			sound.fadeFraction = 1.0f;
			sound.fadeRate = 0.0f;
		}
		else
		{
			float fraction = sound.fadeFraction;
			const float delta = sound.volumeTarget - sound.volumeStart;
			if ( sound.smoothFade )
			{
				const double angle = static_cast< double >( fraction ) * M_PI * 0.5;
				fraction = static_cast< float >( delta >= 0.0f ? sin( angle ) : 1.0 - cos( angle ) );
			}
			sound.volumeCurrent = delta * fraction + sound.volumeStart;
		}
		if ( sound.volumeTarget == 0.0f && sound.volumeCurrent == 0.0f )
		{
			StopLoopingSound( sound );
			m_loopingSounds.FastRemove( i );
		}
		else
		{
			UpdateLoopingSound( sound );
		}
	}
#else
	float period = soundscape_fadetime.GetFloat();
	float amount = frametime;
	if ( period > 0 )
	{
		amount *= 1.0 / period;
	}

	int fadeCount = m_loopingSounds.Count();
	while ( fadeCount > 0 )
	{
		fadeCount--;
		loopingsound_t &sound = m_loopingSounds[fadeCount];

		if ( sound.volumeCurrent != sound.volumeTarget )
		{
			sound.volumeCurrent = Approach( sound.volumeTarget, sound.volumeCurrent, amount );
			if ( sound.volumeTarget == 0 && sound.volumeCurrent == 0 )
			{
				// sound is done, remove from list.
				StopLoopingSound( sound );
				m_loopingSounds.FastRemove( fadeCount );
			}
			else
			{
				// tell the engine about the new volume
				UpdateLoopingSound( sound );
			}
		}
	}
#endif
}

void C_SoundscapeSystem::Update( float frametime ) 
{
	if ( m_forcedSoundscapeIndex >= 0 )
	{
		// generate fake positional sources
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		if ( pPlayer )
		{
			Vector origin, forward, right;
			pPlayer->EyePositionAndVectors( &origin, &forward, &right, NULL );
			
			// put the sound origins at the corners of a box around the player
			m_params.localSound.Set( 0, origin + m_forcedSoundscapeRadius * (forward-right) );
			m_params.localSound.Set( 1, origin + m_forcedSoundscapeRadius * (forward+right) );
			m_params.localSound.Set( 2, origin + m_forcedSoundscapeRadius * (-forward-right) );
			m_params.localSound.Set( 3, origin + m_forcedSoundscapeRadius * (-forward+right) );
			m_params.localBits = 0x0007;
		}
	}
	// fade out the old sounds over soundscape_fadetime seconds
	UpdateLoopingSounds( frametime );
	UpdateRandomSounds( gpGlobals->curtime );
}


void C_SoundscapeSystem::UpdateAudioParams( audioparams_t &audio )
{
	if ( m_params.soundscapeIndex == audio.soundscapeIndex && m_params.entIndex == audio.entIndex )
		return;

	m_params = audio;
	m_forcedSoundscapeIndex = -1;
	if ( audio.entIndex > 0 && audio.soundscapeIndex >= 0 && audio.soundscapeIndex < m_soundscapes.Count() )
	{
		DevReportSoundscapeName( audio.soundscapeIndex );
		StartNewSoundscape( m_soundscapes[audio.soundscapeIndex] );
	}
	else
	{
		// bad index (and the soundscape file actually existed...)
		if ( audio.entIndex > 0 &&
			 audio.soundscapeIndex != -1 )
		{
			DevMsg(1, "Error: Bad soundscape!\n");
		}
	}
}



// Called when a soundscape is activated (leading edge of becoming the active soundscape)
void C_SoundscapeSystem::StartNewSoundscape( KeyValues *pSoundscape )
{
	int i;
#if defined( L4D2 )
	const float period = soundscape_fadetime.GetFloat();
	const float defaultFadeRate = 1.0f / ( period > 0.0f ? period : 3.0f );
	const int oldLoopCount = m_loopingSounds.Count();
#endif

	// Reset the system
	// fade out the current loops
	for ( i = m_loopingSounds.Count()-1; i >= 0; --i )
	{
		m_loopingSounds[i].volumeTarget = 0;
#if defined( L4D2 )
		loopingsound_t &sound = m_loopingSounds[i];
		sound.volumeStart = sound.volumeCurrent;
		sound.fadeRate = defaultFadeRate;
		sound.fadeFraction = 0.0f;
		sound.smoothFade = true;
#endif
		if ( !pSoundscape )
		{
			// if we're cancelling the soundscape, stop the sound immediately
			m_loopingSounds[i].volumeCurrent = 0;
#if defined( L4D2 )
			sound.fadeFraction = 1.0f;
			sound.fadeRate = 0.0f;
#endif
		}
	}
	// update ID
	m_loopingSoundId++;

	// clear all random sounds
	m_randomSounds.RemoveAll();
	m_nextRandomTime = gpGlobals->curtime;

	if ( pSoundscape )
	{
		subsoundscapeparams_t params;
		params.allowDSP = true;
		params.wroteSoundMixer = false;
		params.wroteDSPVolume = false;

		params.masterVolume = 1.0;
		params.startingPosition = 0;
		params.recurseLevel = 0;
		params.positionOverride = -1;
		params.ambientPositionOverride = -1;
#if defined( L4D2 )
		params.fadeRate = defaultFadeRate;
#endif
		StartSubSoundscape( pSoundscape, params );

		if ( !params.wroteDSPVolume )
		{
#if defined( L4D2 )
			// C9DBB0 reverts to the original engine's "0.8" default.
			// The target engine defaults to "1.0"; retain the native string setter.
			m_pDSPVolumeVar->SetValue( "0.8" );
#else
			m_pDSPVolumeVar->Revert();
#endif
		}
		if ( !params.wroteSoundMixer )
		{
			m_pSoundMixerVar->Revert();
		}
#if defined( L4D2 )
		// C9DBB0 changes only pre-existing, still-retiring default-rate loops.
		// Earlier playlooping keys keep the rate that was current at their key.
		if ( params.fadeRate != defaultFadeRate )
		{
			for ( i = oldLoopCount - 1; i >= 0; --i )
			{
				loopingsound_t &sound = m_loopingSounds[i];
				if ( sound.volumeTarget == 0.0f && sound.fadeRate == defaultFadeRate )
					sound.fadeRate = params.fadeRate;
			}
		}
#endif
	}
}

void C_SoundscapeSystem::StartSubSoundscape( KeyValues *pSoundscape, subsoundscapeparams_t &params )
{
	// Parse/process all of the commands
	KeyValues *pKey = pSoundscape->GetFirstSubKey();
	while ( pKey )
	{
		if ( !Q_strcasecmp( pKey->GetName(), "dsp" ) )
		{
			if ( params.allowDSP )
			{
				ProcessDSP( pKey );
			}
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "dsp_player" ) )
		{
			if ( params.allowDSP )
			{
				ProcessDSPPlayer( pKey );
			}
		}
#if defined( L4D2 )
		else if ( !Q_strcasecmp( pKey->GetName(), "fadetime" ) )
		{
			if ( params.recurseLevel < 1 )
			{
				const float period = pKey->GetFloat();
				if ( period > 0.0f )
					params.fadeRate = 1.0f / period;
			}
		}
#endif
		else if ( !Q_strcasecmp( pKey->GetName(), "playlooping" ) )
		{
			ProcessPlayLooping( pKey, params );
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "playrandom" ) )
		{
			ProcessPlayRandom( pKey, params );
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "playsoundscape" ) )
		{
			ProcessPlaySoundscape( pKey, params );
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "Soundmixer" ) )
		{
			if ( params.allowDSP )
			{
				ProcessSoundMixer( pKey, params );
			}
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "dsp_volume" ) )
		{
			if ( params.allowDSP )
			{
				ProcessDSPVolume( pKey, params );
			}
		}
		// add new commands here
		else
		{
			DevMsg( 1, "Soundscape %s:Unknown command %s\n", pSoundscape->GetName(), pKey->GetName() );
		}
		pKey = pKey->GetNextKey();
	}
}

// add a process for each new command here

// change DSP effect
void C_SoundscapeSystem::ProcessDSP( KeyValues *pDSP )
{
	int roomType = pDSP->GetInt();
	CLocalPlayerFilter filter;
	enginesound->SetRoomType( filter, roomType );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pDSPPlayer - 
//-----------------------------------------------------------------------------
void C_SoundscapeSystem::ProcessDSPPlayer( KeyValues *pDSPPlayer )
{
	int dspType = pDSPPlayer->GetInt();
	CLocalPlayerFilter filter;
	enginesound->SetPlayerDSP( filter, dspType, false );
}


void C_SoundscapeSystem::ProcessSoundMixer( KeyValues *pSoundMixer, subsoundscapeparams_t &params )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer || pPlayer->CanSetSoundMixer() )
	{
		m_pSoundMixerVar->SetValue( pSoundMixer->GetString() );
		params.wroteSoundMixer = true;
	}
}

void C_SoundscapeSystem::ProcessDSPVolume( KeyValues *pKey, subsoundscapeparams_t &params )
{
	m_pDSPVolumeVar->SetValue( pKey->GetFloat() );
	params.wroteDSPVolume = true;
}

// start a new looping sound
void C_SoundscapeSystem::ProcessPlayLooping( KeyValues *pAmbient, const subsoundscapeparams_t &params )
{
	float volume = 0;
#if defined( L4D2 )
	soundlevel_t soundlevel = static_cast< soundlevel_t >( 74 ); // C9D264, not SNDLVL_NORM.
#else
	soundlevel_t soundlevel = ATTN_TO_SNDLVL(ATTN_NORM);
#endif
	const char *pSoundName = NULL;
	int pitch = PITCH_NORM;
	int positionIndex = -1;
	bool suppress = false;
#if defined( L4D2 )
	Vector explicitOrigin;
	unsigned int originComponents = 0;
	bool hasOrigin = false;
	bool randomPosition = false;
	float radius = 0.0f;
#endif
	KeyValues *pKey = pAmbient->GetFirstSubKey();
	while ( pKey )
	{
		if ( !Q_strcasecmp( pKey->GetName(), "volume" ) )
		{
#if defined( L4D2 )
			volume = static_cast< float >( params.masterVolume * SoundscapeRandomInterval( ReadInterval( pKey->GetString() ) ) );
#else
			volume = params.masterVolume * RandomInterval( ReadInterval( pKey->GetString() ) );
#endif
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "pitch" ) )
		{
#if defined( L4D2 )
			pitch = SoundscapeIntervalToInt( SoundscapeRandomInterval( ReadInterval( pKey->GetString() ) ) );
#else
			pitch = RandomInterval( ReadInterval( pKey->GetString() ) );
#endif
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "wave" ) )
		{
			pSoundName = pKey->GetString();
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "position" ) )
		{
#if defined( L4D2 )
			if ( !Q_strcasecmp( pKey->GetString(), "random" ) )
				randomPosition = true;
			else
#endif
				positionIndex = params.startingPosition + pKey->GetInt();
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "attenuation" ) )
		{
#if defined( L4D2 )
			// C9D240 draws once for the zero test, and again for a nonzero
			// reciprocal. Do not collapse those distinct RNG calls.
			if ( SoundscapeRandomInterval( ReadInterval( pKey->GetString() ) ) == 0.0 )
				soundlevel = SNDLVL_NONE;
			else
			{
				const float level = static_cast< float >(
					20.0 / SoundscapeRandomInterval( ReadInterval( pKey->GetString() ) ) + 50.0 );
				soundlevel = static_cast< soundlevel_t >( _mm_cvttss_si32( _mm_set_ss( level ) ) );
			}
#else
			soundlevel = ATTN_TO_SNDLVL( RandomInterval( ReadInterval( pKey->GetString() ) ) );
#endif
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "soundlevel" ) )
		{
			if ( !Q_strncasecmp( pKey->GetString(), "SNDLVL_", strlen( "SNDLVL_" ) ) )
			{
				soundlevel = TextToSoundLevel( pKey->GetString() );
			}
			else
			{
#if defined( L4D2 )
				soundlevel = static_cast< soundlevel_t >( SoundscapeIntervalToInt( SoundscapeRandomInterval( ReadInterval( pKey->GetString() ) ) ) );
#else
				soundlevel = (soundlevel_t)((int)RandomInterval( ReadInterval( pKey->GetString() ) ));
#endif
			}
		}
#if defined( L4D2 )
		else if ( !Q_strcasecmp( pKey->GetName(), "origin" ) )
		{
			char buffer[128];
			Q_strncpy( buffer, pKey->GetString(), sizeof( buffer ) );
			char *component = strtok( buffer, "," );
			for ( int i = 0; component && i < 3; ++i, component = strtok( NULL, "," ) )
			{
				explicitOrigin[i] = atof( component );
				originComponents |= 1u << i;
			}
			hasOrigin = true;
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "radius" ) )
		{
			radius = atof( pKey->GetString() );
		}
#endif
		else if ( !Q_strcasecmp( pKey->GetName(), "suppress_on_restore" ) )
		{
			suppress = Q_atoi( pKey->GetString() ) != 0 ? true : false;
		}
		else
		{
			DevMsg( 1, "Ambient %s:Unknown command %s\n", pAmbient->GetName(), pKey->GetName() );
		}
		pKey = pKey->GetNextKey();
	}

	if ( positionIndex < 0 )
	{
		positionIndex = params.ambientPositionOverride;
	}
	else if ( params.positionOverride >= 0 )
	{
		positionIndex = params.positionOverride;
	}

	// Sound is mared as "suppress_on_restore" so don't restart it
	if ( IsBeingRestored() && suppress )
	{
		return;
	}

	if ( volume != 0 && pSoundName != NULL )
	{
#if defined( L4D2 )
		if ( randomPosition )
		{
			AddLoopingSound( pSoundName, false, volume, soundlevel, pitch, GenerateRandomSoundPosition(), radius, params.fadeRate );
		}
		else if ( hasOrigin )
		{
			// Native incomplete coordinates have no defined vector. Omit only
			// this dependent emission, after every key and earlier RNG draw.
			if ( originComponents != 7u )
				return;
			AddLoopingSound( pSoundName, false, volume, soundlevel, pitch, explicitOrigin, radius, params.fadeRate );
		}
		else
#endif
		if ( positionIndex < 0 )
		{
#if defined( L4D2 )
			AddLoopingAmbient( pSoundName, volume, pitch, radius, params.fadeRate );
#else
			AddLoopingAmbient( pSoundName, volume, pitch );
#endif
		}
		else
		{
			if ( positionIndex > 31 || !(m_params.localBits & (1<<positionIndex) ) )
			{
				// suppress sounds if the position isn't available
				//DevMsg( 1, "Bad position %d\n", positionIndex );
				return;
			}
#if defined( L4D2 )
			AddLoopingSound( pSoundName, false, volume, soundlevel, pitch, m_params.localSound[positionIndex], radius, params.fadeRate );
#else
			AddLoopingSound( pSoundName, false, volume, soundlevel, pitch, m_params.localSound[positionIndex] );
#endif
		}
	}
}

void C_SoundscapeSystem::TouchSoundFile( char const *wavefile )
{
	filesystem->GetFileTime( VarArgs( "sound/%s", PSkipSoundChars( wavefile ) ), "GAME" );
}

// start a new looping sound
void C_SoundscapeSystem::TouchPlayLooping( KeyValues *pAmbient )
{
	KeyValues *pKey = pAmbient->GetFirstSubKey();
	while ( pKey )
	{
		if ( !Q_strcasecmp( pKey->GetName(), "wave" ) )
		{
			char const *pSoundName = pKey->GetString();

			// Touch the file
			TouchSoundFile( pSoundName );
		}

		pKey = pKey->GetNextKey();
	}
}


Vector C_SoundscapeSystem::GenerateRandomSoundPosition()
{
	float angle = random->RandomFloat( -180, 180 );
	float sinAngle, cosAngle;
#if defined( L4D2 )
	cosAngle = static_cast< float >( cos( static_cast< double >( angle ) ) );
	sinAngle = static_cast< float >( sin( static_cast< double >( angle ) ) );
#else
	SinCos( angle, &sinAngle, &cosAngle );
#endif
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer )
	{
		Vector origin, forward, right;
		pPlayer->EyePositionAndVectors( &origin, &forward, &right, NULL );
		return origin + DEFAULT_SOUND_RADIUS * (cosAngle * right + sinAngle * forward);
	}
	else
	{
		return CurrentViewOrigin() + DEFAULT_SOUND_RADIUS * (cosAngle * CurrentViewRight() + sinAngle * CurrentViewForward());
	}
}

void C_SoundscapeSystem::TouchSoundFiles()
{
	if ( !CommandLine()->FindParm( "-makereslists" ) )
		return;

	int c = m_soundscapes.Count();
	for ( int i = 0; i < c ; ++i )
	{
		TouchWaveFiles( m_soundscapes[ i ] );
	}
}

void C_SoundscapeSystem::TouchWaveFiles( KeyValues *pSoundScape )
{
	KeyValues *pKey = pSoundScape->GetFirstSubKey();
	while ( pKey )
	{
		if ( !Q_strcasecmp( pKey->GetName(), "playlooping" ) )
		{
			TouchPlayLooping( pKey );
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "playrandom" ) )
		{
			TouchPlayRandom( pKey );
		}

		pKey = pKey->GetNextKey();
	}

}

// puts a recurring random sound event into the queue
void C_SoundscapeSystem::TouchPlayRandom( KeyValues *pPlayRandom )
{
	KeyValues *pKey = pPlayRandom->GetFirstSubKey();
	while ( pKey )
	{
		if ( !Q_strcasecmp( pKey->GetName(), "rndwave" ) )
		{
			KeyValues *pWaves = pKey->GetFirstSubKey();
			while ( pWaves )
			{
				TouchSoundFile( pWaves->GetString() );

				pWaves = pWaves->GetNextKey();
			}
		}

		pKey = pKey->GetNextKey();
	}
}

// puts a recurring random sound event into the queue
void C_SoundscapeSystem::ProcessPlayRandom( KeyValues *pPlayRandom, const subsoundscapeparams_t &params )
{
	randomsound_t sound;
	sound.Init();
	sound.masterVolume = params.masterVolume;
	int positionIndex = -1;
	bool suppress = false;
	bool randomPosition = false;
#if defined( L4D2 )
	Vector explicitOrigin;
	unsigned int originComponents = 0;
	bool hasOrigin = false;
#endif
	KeyValues *pKey = pPlayRandom->GetFirstSubKey();
	while ( pKey )
	{
		if ( !Q_strcasecmp( pKey->GetName(), "volume" ) )
		{
			sound.volume = ReadInterval( pKey->GetString() );
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "pitch" ) )
		{
			sound.pitch = ReadInterval( pKey->GetString() );
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "attenuation" ) )
		{
			interval_t atten = ReadInterval( pKey->GetString() );
			sound.soundlevel.start = ATTN_TO_SNDLVL( atten.start );
			sound.soundlevel.range = ATTN_TO_SNDLVL( atten.start + atten.range ) - sound.soundlevel.start;
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "soundlevel" ) )
		{
			if ( !Q_strncasecmp( pKey->GetString(), "SNDLVL_", strlen( "SNDLVL_" ) ) )
			{
				sound.soundlevel.start = TextToSoundLevel( pKey->GetString() );
				sound.soundlevel.range = 0;
			}
			else
			{
				sound.soundlevel = ReadInterval( pKey->GetString() );
			}
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "time" ) )
		{
			sound.time = ReadInterval( pKey->GetString() );
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "rndwave" ) )
		{
			KeyValues *pWaves = pKey->GetFirstSubKey();
			sound.pWaves = pWaves;
			sound.waveCount = 0;
			while ( pWaves )
			{
				sound.waveCount++;
				pWaves = pWaves->GetNextKey();
			}
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "position" ) )
		{
			if ( !Q_strcasecmp( pKey->GetString(), "random" ) )
			{
				randomPosition = true;
			}
			else
			{
				positionIndex = params.startingPosition + pKey->GetInt();
			}
		}
#if defined( L4D2 )
		else if ( !Q_strcasecmp( pKey->GetName(), "origin" ) )
		{
			char buffer[128];
			Q_strncpy( buffer, pKey->GetString(), sizeof( buffer ) );
			char *component = strtok( buffer, "," );
			for ( int i = 0; component && i < 3; ++i, component = strtok( NULL, "," ) )
			{
				explicitOrigin[i] = atof( component );
				originComponents |= 1u << i;
			}
			hasOrigin = true;
		}
#endif
		else if ( !Q_strcasecmp( pKey->GetName(), "suppress_on_restore" ) )
		{
			suppress = Q_atoi( pKey->GetString() ) != 0 ? true : false;
		}
		else
		{
			DevMsg( 1, "Random Sound %s:Unknown command %s\n", pPlayRandom->GetName(), pKey->GetName() );
		}

		pKey = pKey->GetNextKey();
	}

	if ( positionIndex < 0 )
	{
		positionIndex = params.ambientPositionOverride;
	}
	else if ( params.positionOverride >= 0 )
	{
		positionIndex = params.positionOverride;
		randomPosition = false; // override trumps random position
	}

	// Sound is mared as "suppress_on_restore" so don't restart it
	if ( IsBeingRestored() && suppress )
	{
		return;
	}

	if ( sound.waveCount != 0 )
	{
		if ( positionIndex < 0 && !randomPosition
#if defined( L4D2 )
			&& !hasOrigin
#endif
			)
		{
			sound.isAmbient = true;
			AddRandomSound( sound );
		}
		else
		{
			sound.isAmbient = false;
			if ( randomPosition )
			{
				sound.isRandom = true;
			}
#if defined( L4D2 )
			else if ( hasOrigin )
			{
				if ( originComponents != 7u )
				{
					// C9BF5D samples the independent initial schedule at the
					// add boundary even when no defined origin can be emitted.
					(void)SoundscapeRandomInterval( sound.time );
					return; // No registration, replacement coordinates or fallback.
				}
				sound.position = explicitOrigin;
			}
#endif
			else
			{
				if ( positionIndex > 31 || !(m_params.localBits & (1<<positionIndex) ) )
				{
					// suppress sounds if the position isn't available
					//DevMsg( 1, "Bad position %d\n", positionIndex );
					return;
				}
				sound.position = m_params.localSound[positionIndex];
			}
			AddRandomSound( sound );
		}
	}
}

void C_SoundscapeSystem::ProcessPlaySoundscape( KeyValues *pPlaySoundscape, subsoundscapeparams_t &paramsIn )
{
	subsoundscapeparams_t subParams = paramsIn;
	
	// sub-soundscapes NEVER set the DSP effects
	subParams.allowDSP = false;
	subParams.recurseLevel++;
	if ( subParams.recurseLevel > MAX_SOUNDSCAPE_RECURSION )
	{
		DevMsg( "Error!  Soundscape recursion overrun!\n" );
		return;
	}
	KeyValues *pKey = pPlaySoundscape->GetFirstSubKey();
	const char *pSoundscapeName = NULL;
	while ( pKey )
	{
		if ( !Q_strcasecmp( pKey->GetName(), "volume" ) )
		{
#if defined( L4D2 )
			subParams.masterVolume = static_cast< float >( paramsIn.masterVolume * SoundscapeRandomInterval( ReadInterval( pKey->GetString() ) ) );
#else
			subParams.masterVolume = paramsIn.masterVolume * RandomInterval( ReadInterval( pKey->GetString() ) );
#endif
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "position" ) )
		{
			subParams.startingPosition = paramsIn.startingPosition + pKey->GetInt();
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "positionoverride" ) )
		{
			if ( paramsIn.positionOverride < 0 )
			{
				subParams.positionOverride = paramsIn.startingPosition + pKey->GetInt();
				// positionoverride is only ever used to make a whole soundscape come from a point in space
				// So go ahead and default ambients there too.
				subParams.ambientPositionOverride = paramsIn.startingPosition + pKey->GetInt();
			}
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "ambientpositionoverride" ) )
		{
			if ( paramsIn.ambientPositionOverride < 0 )
			{
				subParams.ambientPositionOverride = paramsIn.startingPosition + pKey->GetInt();
			}
		}
		else if ( !Q_strcasecmp( pKey->GetName(), "name" ) )
		{
			pSoundscapeName = pKey->GetString();
		}
		else if ( !Q_strcasecmp(pKey->GetName(), "soundlevel") )
		{
			DevMsg(1,"soundlevel not supported on sub-soundscapes\n");
		}
		else
		{
			DevMsg( 1, "Playsoundscape %s:Unknown command %s\n", pSoundscapeName ? pSoundscapeName : pPlaySoundscape->GetName(), pKey->GetName() );
		}
		pKey = pKey->GetNextKey();
	}

	if ( pSoundscapeName )
	{
		KeyValues *pSoundscapeKeys = SoundscapeByIndex( FindSoundscapeByName( pSoundscapeName ) );
		if ( pSoundscapeKeys )
		{
			StartSubSoundscape( pSoundscapeKeys, subParams );
		}
		else
		{
			DevMsg( 1, "Trying to play unknown soundscape %s\n", pSoundscapeName );
		}
	}
}

// special kind of looping sound with no spatialization
#if defined( L4D2 )
int C_SoundscapeSystem::AddLoopingAmbient( const char *pSoundName, float volume, int pitch, float radius, float fadeRate )
{
	return AddLoopingSound( pSoundName, true, volume, SNDLVL_NORM, pitch, vec3_origin, radius, fadeRate );
}
#else
int C_SoundscapeSystem::AddLoopingAmbient( const char *pSoundName, float volume, int pitch )
{
	return AddLoopingSound( pSoundName, true, volume, SNDLVL_NORM, pitch, vec3_origin );
}

#endif
// add a looping sound to the list
// NOTE: will reuse existing entry (fade from current volume) if possible
//		this prevents pops
#if defined( L4D2 )
int C_SoundscapeSystem::AddLoopingSound( const char *pSoundName, bool isAmbient, float volume, soundlevel_t soundlevel, int pitch, const Vector &position, float radius, float fadeRate )
{
	int slot = m_loopingSounds.Count() - 1;
	for ( ; slot >= 0; --slot )
	{
		const loopingsound_t &sound = m_loopingSounds[slot];
		if ( sound.id != m_loopingSoundId && sound.pitch == pitch &&
			!Q_strcasecmp( pSoundName, sound.pWaveName ) &&
			( ( isAmbient && sound.isAmbient ) ||
			( isAmbient == sound.isAmbient &&
			!( fabs( static_cast< double >( position.x ) - static_cast< double >( sound.position.x ) ) > static_cast< double >( 0.1f ) ) &&
			!( fabs( static_cast< double >( position.y ) - static_cast< double >( sound.position.y ) ) > static_cast< double >( 0.1f ) ) &&
			static_cast< double >( 0.1f ) >= fabs( static_cast< double >( position.z ) - static_cast< double >( sound.position.z ) ) ) ) )
			break;
	}
	if ( slot < 0 )
	{
		slot = m_loopingSounds.AddToTail();
		if ( isAmbient )
		{
			enginesound->EmitAmbientSound( CTerrorEngineSoundName( pSoundName ).Get(), 0.0f, pitch );
			m_loopingSounds[slot].volumeCurrent = 0.0f;
		}
		else
		{
			CLocalPlayerFilter filter;
			EmitSound_t ep;
			ep.m_nChannel = CHAN_STATIC;
			ep.m_pSoundName = pSoundName;
			ep.m_flVolume = 0.05f;
			ep.m_SoundLevel = soundlevel;
			ep.m_nPitch = pitch;
			ep.m_pOrigin = &position;
			C_BaseEntity::EmitSound( filter, SOUND_FROM_WORLD, ep );
			m_loopingSounds[slot].volumeCurrent = 0.05f;
		}
		m_loopingSounds[slot].guid = enginesound->GetGuidForLastSoundEmitted();
	}
	loopingsound_t &sound = m_loopingSounds[slot];
	sound.pWaveName = pSoundName;
	sound.volumeStart = sound.volumeCurrent;
	sound.volumeTarget = volume;
	sound.fadeRate = fadeRate;
	sound.fadeFraction = 0.0f;
	sound.smoothFade = true;
	sound.pitch = pitch;
	sound.id = m_loopingSoundId;
	sound.isAmbient = isAmbient;
	sound.position = position;
	sound.radius = radius;
	sound.soundlevel = radius > 0.0f ? SNDLVL_NONE : soundlevel;
	return slot;
}
#else
int C_SoundscapeSystem::AddLoopingSound( const char *pSoundName, bool isAmbient, float volume, soundlevel_t soundlevel, int pitch, const Vector &position )
{
	loopingsound_t *pSoundSlot = NULL;
	int soundSlot = m_loopingSounds.Count() - 1;
	bool bForceSoundUpdate = false;
	while ( soundSlot >= 0 )
	{
		loopingsound_t &sound = m_loopingSounds[soundSlot];

		// NOTE: Will always restart/crossfade positional sounds
		if ( sound.id != m_loopingSoundId && 
			sound.pitch == pitch && 
			!Q_strcasecmp( pSoundName, sound.pWaveName ) )
		{
			// Ambient sounds can reuse the slots.
			if ( isAmbient == true && 
				sound.isAmbient == true )
			{
				// reuse this sound
				pSoundSlot = &sound;
				break;
			}
			// Positional sounds can reuse the slots if the positions are the same.
			else if ( isAmbient == sound.isAmbient )
			{
				if ( VectorsAreEqual( position, sound.position, 0.1f ) )
				{
					// reuse this sound
					pSoundSlot = &sound;
					break;
				}
				else
				{
					// If it's trying to fade out one positional sound and fade in another, then it gets screwy
					// because it'll be sending alternating commands to the sound engine, referencing the same sound
					// (SOUND_FROM_WORLD, CHAN_STATIC, pSoundName). One of the alternating commands will be as
					// it fades the sound out, and one will be fading the sound in. 
					// Because this will occasionally cause the sound to vanish entirely, we stop the old sound immediately.
					StopLoopingSound(sound);
					pSoundSlot = &sound;

					// make a note to update the sound immediately. Otherwise, if its volume happens to be
					// the same as the old sound's volume, it will never update at all.
					bForceSoundUpdate = true; 
					break;
				}
			}
		}
		soundSlot--;
	}

	if ( soundSlot < 0 )
	{
		// can't find the sound in the list, make a new one
		soundSlot = m_loopingSounds.AddToTail();
		if ( isAmbient )
		{
			// start at 0 and fade in
			enginesound->EmitAmbientSound( CTerrorEngineSoundName( pSoundName ).Get(), 0, pitch );
			m_loopingSounds[soundSlot].volumeCurrent = 0.0;
		}
		else
		{
			// non-ambients at 0 volume are culled, so start at 0.05
			CLocalPlayerFilter filter;

			EmitSound_t ep;
			ep.m_nChannel = CHAN_STATIC;
			ep.m_pSoundName =  pSoundName;
			ep.m_flVolume = 0.05;
			ep.m_SoundLevel = soundlevel;
			ep.m_nPitch = pitch;
			ep.m_pOrigin = &position;

			C_BaseEntity::EmitSound( filter, SOUND_FROM_WORLD, ep );
			m_loopingSounds[soundSlot].volumeCurrent = 0.05;
		}
	}
	loopingsound_t &sound = m_loopingSounds[soundSlot];
	// fill out the slot
	sound.pWaveName = pSoundName;
	sound.volumeTarget = volume;
	sound.pitch = pitch;
	sound.id = m_loopingSoundId;
	sound.isAmbient = isAmbient;
	sound.position = position;
	sound.soundlevel = soundlevel;
	
	if (bForceSoundUpdate)
	{
		UpdateLoopingSound(sound);
	}

	return soundSlot;
}

#endif
// stop this loop forever
void C_SoundscapeSystem::StopLoopingSound( loopingsound_t &loopSound )
{
#if defined( L4D2 )
	enginesound->StopSoundByGuid( loopSound.guid );
#else
	if ( loopSound.isAmbient )
	{
		enginesound->EmitAmbientSound( CTerrorEngineSoundName( loopSound.pWaveName ).Get(), 0, 0, SND_STOP );
	}
	else
	{
		C_BaseEntity::StopSound( SOUND_FROM_WORLD, CHAN_STATIC, loopSound.pWaveName );
	}
#endif
}

// update with new volume
void C_SoundscapeSystem::UpdateLoopingSound( loopingsound_t &loopSound )
{
#if defined( L4D2 )
	if ( enginesound->IsSoundStillPlaying( loopSound.guid ) )
	{
		enginesound->SetVolumeByGuid( loopSound.guid, loopSound.volumeCurrent );
		return;
	}
#endif
	if ( loopSound.isAmbient )
	{
		enginesound->EmitAmbientSound( CTerrorEngineSoundName( loopSound.pWaveName ).Get(), loopSound.volumeCurrent, loopSound.pitch, SND_CHANGE_VOL );
	}
	else
	{
		CLocalPlayerFilter filter;

		EmitSound_t ep;
		ep.m_nChannel = CHAN_STATIC;
		ep.m_pSoundName =  loopSound.pWaveName;
		ep.m_flVolume = loopSound.volumeCurrent;
		ep.m_SoundLevel = loopSound.soundlevel;
		ep.m_nFlags = SND_CHANGE_VOL;
		ep.m_nPitch = loopSound.pitch;
		ep.m_pOrigin = &loopSound.position;

		C_BaseEntity::EmitSound( filter, SOUND_FROM_WORLD, ep );
	}
#if defined( L4D2 )
	loopSound.guid = enginesound->GetGuidForLastSoundEmitted();
#endif
}

// add a recurring random sound event
int C_SoundscapeSystem::AddRandomSound( const randomsound_t &sound )
{
	int index = m_randomSounds.AddToTail( sound );
#if defined( L4D2 )
	m_randomSounds[index].nextPlayTime = static_cast< float >( gpGlobals->curtime + 0.5 * SoundscapeRandomInterval( sound.time ) );
#else
	m_randomSounds[index].nextPlayTime = gpGlobals->curtime + 0.5 * RandomInterval( sound.time );
#endif
	
	return index;
}

// play a random sound randomly from this parameterization table
void C_SoundscapeSystem::PlayRandomSound( randomsound_t &sound )
{
	Assert( sound.waveCount > 0 );

	int waveId = random->RandomInt( 0, sound.waveCount-1 );
	KeyValues *pWaves = sound.pWaves;
	while ( waveId > 0 && pWaves )
	{
		pWaves = pWaves->GetNextKey();
		waveId--;
	}
	if ( !pWaves )
		return;
	
	const char *pWaveName = pWaves->GetString();
	
	if ( !pWaveName )
		return;

	if ( sound.isAmbient )
	{
#if defined( L4D2 )
		// C9BFD4 draws pitch BEFORE volume on the ambient branch.
		const int pitch = SoundscapeIntervalToInt( SoundscapeRandomInterval( sound.pitch ) );
		const float volume = static_cast< float >( sound.masterVolume * SoundscapeRandomInterval( sound.volume ) );
		enginesound->EmitAmbientSound( CTerrorEngineSoundName( pWaveName ).Get(), volume, pitch );
#else
		enginesound->EmitAmbientSound( CTerrorEngineSoundName( pWaveName ).Get(), sound.masterVolume * RandomInterval( sound.volume ), (int)RandomInterval( sound.pitch ) );
#endif
	}
	else
	{
		CLocalPlayerFilter filter;

		EmitSound_t ep;
		ep.m_nChannel = CHAN_STATIC;
		ep.m_pSoundName =  pWaveName;
#if defined( L4D2 )
		ep.m_flVolume = static_cast< float >( sound.masterVolume * SoundscapeRandomInterval( sound.volume ) );
		ep.m_SoundLevel = static_cast< soundlevel_t >( SoundscapeIntervalToInt( SoundscapeRandomInterval( sound.soundlevel ) ) );
		ep.m_nPitch = SoundscapeIntervalToInt( SoundscapeRandomInterval( sound.pitch ) );
#else
		ep.m_flVolume = sound.masterVolume * RandomInterval( sound.volume );
		ep.m_SoundLevel = (soundlevel_t)(int)RandomInterval( sound.soundlevel );
		ep.m_nPitch = (int)RandomInterval( sound.pitch );
#endif
		if ( sound.isRandom )
		{
			sound.position = GenerateRandomSoundPosition();
		}
		ep.m_pOrigin = &sound.position;

		C_BaseEntity::EmitSound( filter, SOUND_FROM_WORLD, ep );
	}
}

// walk the list of random sound commands and update
void C_SoundscapeSystem::UpdateRandomSounds( float gameTime )
{
	if ( gameTime < m_nextRandomTime )
		return;

	m_nextRandomTime = gameTime + 3600;	// add some big time to check again (an hour)

	for ( int i = m_randomSounds.Count()-1; i >= 0; i-- )
	{
		// time to play?
		if ( gameTime >= m_randomSounds[i].nextPlayTime )
		{
			// UNDONE: add this in to fix range?
			// float dt = m_randomSounds[i].nextPlayTime - gameTime;
			PlayRandomSound( m_randomSounds[i] );

			// now schedule the next occurrance
			// UNDONE: add support for "play once" sounds? FastRemove() here.
#if defined( L4D2 )
			m_randomSounds[i].nextPlayTime = static_cast< float >( gameTime + SoundscapeRandomInterval( m_randomSounds[i].time ) );
#else
			m_randomSounds[i].nextPlayTime = gameTime + RandomInterval( m_randomSounds[i].time );
#endif
		}

		// update next time to check the queue
		if ( m_randomSounds[i].nextPlayTime < m_nextRandomTime )
		{
			m_nextRandomTime = m_randomSounds[i].nextPlayTime;
		}
	}
}




#if defined( L4D2 )
CON_COMMAND_F(cl_soundscape_printdebuginfo, "print soundscapes", 0x00000008)
#else
CON_COMMAND(cl_soundscape_printdebuginfo, "print soundscapes")
#endif
{
	g_SoundscapeSystem.PrintDebugInfo();
}

#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif
