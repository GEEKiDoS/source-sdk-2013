//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include <ctype.h>
#include <KeyValues.h>
#include "engine/IEngineSound.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "igamesystem.h"
#include "soundchars.h"
#include "l4d2/sound/terror_engine_sound_name.h"
#include "filesystem.h"
#include "tier0/vprof.h"
#include "checksum_crc.h"
#include "tier0/icommandline.h"
#include "util_shared.h"
#if defined( L4D2 )
#include "l4d2/sound/terror_sound_gamedata.h"
#include "l4d2/sound/terror_music.h"
#if !defined( CLIENT_DLL )
#include "NextBot/NextBotManager.h"
#endif
#endif

#if defined( TF_CLIENT_DLL ) || defined( TF_DLL )
#include "tf_shareddefs.h"
#include "tf_classdata.h"
#endif

// NVNT haptic utils
#if !defined( L4D2 )
#include "haptics/haptic_utils.h"
#endif // !L4D2

#ifndef CLIENT_DLL
#include "envmicrophone.h"
#include "sceneentity.h"
#else
#include <vgui_controls/Controls.h>
#include <vgui/IVGui.h>
#include "hud_closecaption.h"
#define CRecipientFilter C_RecipientFilter
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "SoundEmitterSystem_console_server_cvars.inc"
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "captioncompiler.h"
#include "tier1/utlsortvector.h"
#include "tier1/utlsymbol.h"
#include "tier1/utlbuffer.h"
#include "tier1/utlmap.h"
#include "tier1/utlrbtree.h"
#include <xmmintrin.h>
#endif
#include "tier0/memdbgon.h"

static ConVar sv_soundemitter_trace( "sv_soundemitter_trace", 
#if defined( L4D2 )
	"-1"
#else
	"0"
#endif
, 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000200A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
, 
#if defined( L4D2 )
	"Show all EmitSound calls including their symbolic name and the actual wave file they resolved to. (-1 = for nobody, 0 = for everybody, n = for one entity)\n"
#else
	"Show all EmitSound calls including their symbolic name and the actual wave file they resolved to\n"
#endif
 );

extern ISoundEmitterSystemBase *soundemitterbase;
static ConVar *g_pClosecaption = NULL;

#ifdef _XBOX
int LookupStringFromCloseCaptionToken( char const *token );
const wchar_t *GetStringForIndex( int index );
#endif
bool g_bPermitDirectSoundPrecache = false;

#if !defined( CLIENT_DLL )

void ClearModelSoundsCache();

#endif // !CLIENT_DLL

#if defined( L4D2 ) && !defined( CLIENT_DLL )
static void PrecacheTerrorSoundForEmission( const CTerrorEngineSoundName &engineSoundName, int flags )
{
	// Sentences and updates/stops do not start a new sample-table sound.
	if ( ( flags & ( SND_STOP | SND_CHANGE_VOL | SND_CHANGE_PITCH ) ) ||
		engineSoundName.IsPrecached() )
		return;
	// Survivor response waves are admitted when selected for playback. Use the
	// real target table before emitting, without another selection/RNG draw;
	// retain genuine overflow/error behavior instead of dropping the sound.
	const char *sample = engineSoundName.Get();
	Warning( "Late precache of %s\n", sample );
	enginesound->PrecacheSound( sample, true );
}
#endif

void WaveTrace( char const *wavname, char const *funcname )
{
	if ( IsX360() && !IsDebug() )
	{
		return;
	}

	static CUtlSymbolTable s_WaveTrace;

	// Make sure we only show the message once
	if ( UTL_INVAL_SYMBOL == s_WaveTrace.Find( wavname ) )
	{
		DevMsg( "%s directly referenced wave %s (should use game_sounds.txt system instead)\n", 
			funcname, wavname );
		s_WaveTrace.AddString( wavname );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &src - 
//-----------------------------------------------------------------------------
EmitSound_t::EmitSound_t( const CSoundParameters &src )
{
	m_nChannel = src.channel;
	m_pSoundName = src.soundname;
	m_flVolume = src.volume;
	m_SoundLevel = src.soundlevel;
	m_nFlags = 0;
	m_nPitch = src.pitch;
	m_nSpecialDSP = 0;
	m_pOrigin = 0;
	m_flSoundTime = ( src.delay_msec == 0 ) ? 0.0f : gpGlobals->curtime + ( (float)src.delay_msec / 1000.0f );
	m_pflSoundDuration = 0;
	m_bEmitCloseCaption = true;
	m_bWarnOnMissingCloseCaption = false;
	m_bWarnOnDirectWaveReference = false;
	m_nSpeakerEntity = -1;
}

void Hack_FixEscapeChars( char *str )
{
	int len = Q_strlen( str ) + 1;
	char *i = str;
	char *o = (char *)_alloca( len );
	char *osave = o;
	while ( *i )
	{
		if ( *i == '\\' )
		{
			switch (  *( i + 1 ) )
			{
			case 'n':
				*o = '\n';
				++i;
				break;
			default:
				*o = *i;
				break;
			}
		}
		else
		{
			*o = *i;
		}

		++i;
		++o;
	}
	*o = 0;
	Q_strncpy( str, osave, len );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 ) && !defined( CLIENT_DLL )
static void ReloadServerCaptionDictionaries();
static void ClearServerCaptionRepeat();
static void EmitServerCloseCaption( IRecipientFilter &filter, int entindex, bool fromplayer, const char *token, CUtlVector< Vector > &origins, float duration, bool warnifmissing, bool direct );
#endif

class CSoundEmitterSystem : public CBaseGameSystem
{
public:
	virtual char const *Name() { return "CSoundEmitterSystem"; }

#if !defined( CLIENT_DLL )
	bool			m_bLogPrecache;
	FileHandle_t	m_hPrecacheLogFile;
	CUtlSymbolTable m_PrecachedScriptSounds;
public:
	CSoundEmitterSystem( char const *pszName ) :
		m_bLogPrecache( false ),
		m_hPrecacheLogFile( FILESYSTEM_INVALID_HANDLE )
	{
	}

	void LogPrecache( char const *soundname )
	{
		if ( !m_bLogPrecache )
			return;

		// Make sure we only show the message once
		if ( UTL_INVAL_SYMBOL != m_PrecachedScriptSounds.Find( soundname ) )
			return;

		if (m_hPrecacheLogFile == FILESYSTEM_INVALID_HANDLE)
		{
			StartLog();
		}

		m_PrecachedScriptSounds.AddString( soundname );

		if (m_hPrecacheLogFile != FILESYSTEM_INVALID_HANDLE)
		{
			filesystem->Write("\"", 1, m_hPrecacheLogFile);
			filesystem->Write(soundname, Q_strlen(soundname), m_hPrecacheLogFile);
			filesystem->Write("\"\n", 2, m_hPrecacheLogFile);
		}
		else
		{
			Warning( "Disabling precache logging due to file i/o problem!!!\n" );
			m_bLogPrecache = false;
		}
	}

	void StartLog()
	{
		m_PrecachedScriptSounds.RemoveAll();

		if ( !m_bLogPrecache )
			return;

		if ( FILESYSTEM_INVALID_HANDLE != m_hPrecacheLogFile )
		{
			return;
		}

		filesystem->CreateDirHierarchy("reslists", "DEFAULT_WRITE_PATH");

		// open the new level reslist
		char path[_MAX_PATH];
		Q_snprintf(path, sizeof(path), "reslists\\%s.snd", gpGlobals->mapname.ToCStr() );
		m_hPrecacheLogFile = filesystem->Open(path, "wt", "GAME");
	}

	void FinishLog()
	{
		if ( FILESYSTEM_INVALID_HANDLE != m_hPrecacheLogFile )
		{
			filesystem->Close( m_hPrecacheLogFile );
			m_hPrecacheLogFile = FILESYSTEM_INVALID_HANDLE;
		}

		m_PrecachedScriptSounds.RemoveAll();
	}
#else
	CSoundEmitterSystem( char const *name )
	{
	}

#endif

	// IServerSystem stuff
	virtual bool Init()
	{
		Assert( soundemitterbase );
#if !defined( CLIENT_DLL )
		m_bLogPrecache = CommandLine()->CheckParm( "-makereslists" ) ? true : false;
#endif
		g_pClosecaption = cvar->FindVar("closecaption");
		Assert(g_pClosecaption);
#if defined( L4D2 ) && !defined( CLIENT_DLL )
		ReloadServerCaptionDictionaries();
#endif
#if defined( L4D2 )
		PurgeSoundGameData();
		InvalidateMusicSoundHashes();
		const bool initialized = soundemitterbase->ModInit();
		if ( initialized )
		{
			InitSoundGameData();
			InvalidateMusicSoundHashes();
		}
		return initialized;
#else
		return soundemitterbase->ModInit();
#endif
	}

	virtual void Shutdown()
	{
		Assert( soundemitterbase );
#if !defined( CLIENT_DLL )
		FinishLog();
#endif
#if defined( L4D2 )
		PurgeSoundGameData();
		InvalidateMusicSoundHashes();
#endif
		soundemitterbase->ModShutdown();
	}

	void ReloadSoundEntriesInList( IFileList *pFilesToReload )
	{
#if defined( L4D2 )
		BeginSoundGameDataReload( pFilesToReload );
		InvalidateMusicSoundHashes();
#endif
		soundemitterbase->ReloadSoundEntriesInList( pFilesToReload );
#if defined( L4D2 )
		EndSoundGameDataReload();
		InvalidateMusicSoundHashes();
#endif
	}

	void AddSoundOverrides( const char *scriptfile, bool bPreload = false )
	{
#if defined( L4D2 )
		InvalidateSoundGameDataHandles();
		InvalidateMusicSoundHashes();
#endif
		soundemitterbase->AddSoundOverrides( scriptfile, bPreload );
#if defined( L4D2 )
		AddSoundGameDataOverrides( scriptfile );
		InvalidateMusicSoundHashes();
#endif
	}

	#if defined( L4D2 )
	virtual void TraceEmitSound( int entindex, char const *fmt, ... )
#else
	virtual void TraceEmitSound( char const *fmt, ... )
#endif
	{
		#if defined( L4D2 )
		const int nTraceEntity = sv_soundemitter_trace.GetInt();
		if ( nTraceEntity == -1 || ( nTraceEntity != 0 && nTraceEntity != entindex ) )
#else
		if ( !sv_soundemitter_trace.GetBool() )
#endif
			return;

		va_list	argptr;
		char string[256];
		va_start (argptr, fmt);
		Q_vsnprintf( string, sizeof( string ), fmt, argptr );
		va_end (argptr);

		// Spew to console
		Msg( "%s %s", CBaseEntity::IsServer() ? "(sv)" : "(cl)", string );
	}

	// Precache all wave files referenced in wave or rndwave keys
	virtual void LevelInitPreEntity()
	{
#if defined( L4D2 )
		InvalidateSoundGameDataHandles();
		InvalidateMusicSoundHashes();
#endif
		char mapname[ 256 ];
#if !defined( CLIENT_DLL )
		StartLog();
		Q_snprintf( mapname, sizeof( mapname ), "maps/%s", STRING( gpGlobals->mapname ) );
#else
		Q_snprintf( mapname, sizeof( mapname ), "maps/%s", V_GetFileName( engine->GetLevelName() ) );
#endif

		Q_FixSlashes( mapname );
		Q_strlower( mapname );

		char maptmp[256];
		const char *pszCleanMapName = GetCleanMapName( mapname, maptmp );

		// Load in any map specific overrides
		char scriptfile[ 512 ];
#if defined( TF_CLIENT_DLL ) || defined( TF_DLL )
		if( V_stristr( pszCleanMapName, "mvm" ) )
		{
			V_strncpy( scriptfile, "scripts/mvm_level_sounds.txt", sizeof( scriptfile ) );
			if ( filesystem->FileExists( "scripts/mvm_level_sounds.txt", "GAME" ) )
			{
				AddSoundOverrides( "scripts/mvm_level_sounds.txt", true );
			}
			if ( filesystem->FileExists( "scripts/mvm_level_sound_tweaks.txt", "GAME" ) )
			{
				AddSoundOverrides( "scripts/mvm_level_sound_tweaks.txt" );
 			}
			if ( filesystem->FileExists( "scripts/game_sounds_vo_mvm.txt", "GAME" ) )
			{
				AddSoundOverrides( "scripts/game_sounds_vo_mvm.txt", true );
			}
			if ( filesystem->FileExists( "scripts/game_sounds_vo_mvm_mighty.txt", "GAME" ) )
			{
				AddSoundOverrides( "scripts/game_sounds_vo_mvm_mighty.txt", true );
			}
			g_pTFPlayerClassDataMgr->AddAdditionalPlayerDeathSounds();
		}
		else
		{
			Q_StripExtension( pszCleanMapName, scriptfile, sizeof( scriptfile ) );
			Q_strncat( scriptfile, "_level_sounds.txt", sizeof( scriptfile ), COPY_ALL_CHARACTERS );
			if ( filesystem->FileExists( scriptfile, "GAME" ) )
			{
				AddSoundOverrides( scriptfile );
			}
		}
#else
		Q_StripExtension( pszCleanMapName, scriptfile, sizeof( scriptfile ) );
		Q_strncat( scriptfile, "_level_sounds.txt", sizeof( scriptfile ), COPY_ALL_CHARACTERS );

		if ( filesystem->FileExists( scriptfile, "GAME" ) )
		{
			AddSoundOverrides( scriptfile );
		}
#endif
#if defined( L4D2 )
		RefreshSoundGameDataHandles();
		InvalidateMusicSoundHashes();
#endif

#if !defined( CLIENT_DLL )
		for ( int i=soundemitterbase->First(); i != soundemitterbase->InvalidIndex(); i=soundemitterbase->Next( i ) )
		{
			CSoundParametersInternal *pParams = soundemitterbase->InternalGetParametersForSound( i );
			if ( pParams->ShouldPreload() )
			{
				InternalPrecacheWaves( i );
			}
		}
#endif
	}

	virtual void LevelInitPostEntity()
	{
	}

	virtual void LevelShutdownPostEntity()
	{
#if defined( L4D2 )
		InvalidateSoundGameDataHandles();
		InvalidateMusicSoundHashes();
#endif
		soundemitterbase->ClearSoundOverrides();
#if defined( L4D2 )
		ClearSoundGameDataOverrides();
		InvalidateMusicSoundHashes();
#endif

#if !defined( CLIENT_DLL )
		FinishLog();
#endif
	#if defined( L4D2 ) && !defined( CLIENT_DLL )
		ClearServerCaptionRepeat();
#endif
	}

	void Flush()
	{
		Assert( soundemitterbase );
#if !defined( CLIENT_DLL )
		FinishLog();
#endif
#if defined( L4D2 )
		PurgeSoundGameData();
		InvalidateMusicSoundHashes();
#endif
		soundemitterbase->Flush();
#if defined( L4D2 )
		InitSoundGameData();
		InvalidateMusicSoundHashes();
#endif
	}
		
	void InternalPrecacheWaves( int soundIndex )
	{
		CSoundParametersInternal *internal = soundemitterbase->InternalGetParametersForSound( soundIndex );
		if ( !internal )
			return;

		int waveCount = internal->NumSoundNames();
		if ( !waveCount )
		{
			DevMsg( "CSoundEmitterSystem:  sounds.txt entry '%s' has no waves listed under 'wave' or 'rndwave' key!!!\n",
				soundemitterbase->GetSoundName( soundIndex ) );
		}
		else
		{
			g_bPermitDirectSoundPrecache = true;

			for( int wave = 0; wave < waveCount; wave++ )
			{
				CBaseEntity::PrecacheSound( soundemitterbase->GetWaveName( internal->GetSoundNames()[ wave ].symbol ) );
			}

			g_bPermitDirectSoundPrecache = false;
		}
	}

	void InternalPrefetchWaves( int soundIndex )
	{
		CSoundParametersInternal *internal = soundemitterbase->InternalGetParametersForSound( soundIndex );
		if ( !internal )
			return;

		int waveCount = internal->NumSoundNames();
		if ( !waveCount )
		{
			DevMsg( "CSoundEmitterSystem:  sounds.txt entry '%s' has no waves listed under 'wave' or 'rndwave' key!!!\n",
				soundemitterbase->GetSoundName( soundIndex ) );
		}
		else
		{
			for( int wave = 0; wave < waveCount; wave++ )
			{
				CBaseEntity::PrefetchSound( soundemitterbase->GetWaveName( internal->GetSoundNames()[ wave ].symbol ) );
			}
		}
	}

	HSOUNDSCRIPTHANDLE PrecacheScriptSound( const char *soundname )
	{
		int soundIndex = soundemitterbase->GetSoundIndex( soundname );
		if ( !soundemitterbase->IsValidIndex( soundIndex ) )
		{
			if ( Q_stristr( soundname, ".wav" ) || Q_strstr( soundname, ".mp3" ) )
			{
				g_bPermitDirectSoundPrecache = true;
	
				CBaseEntity::PrecacheSound( soundname );
	
				g_bPermitDirectSoundPrecache = false;
				return SOUNDEMITTER_INVALID_HANDLE;
			}

#if !defined( CLIENT_DLL )
			if ( soundname[ 0 ] )
			{
				static CUtlSymbolTable s_PrecacheScriptSoundFailures;

				// Make sure we only show the message once
				if ( UTL_INVAL_SYMBOL == s_PrecacheScriptSoundFailures.Find( soundname ) )
				{
					DevMsg( "PrecacheScriptSound '%s' failed, no such sound script entry\n", soundname );
					s_PrecacheScriptSoundFailures.AddString( soundname );
				}
			}
#endif
			return (HSOUNDSCRIPTHANDLE)soundIndex;
		}
#if !defined( CLIENT_DLL )
		LogPrecache( soundname );
#endif

		InternalPrecacheWaves( soundIndex );
		return (HSOUNDSCRIPTHANDLE)soundIndex;
	}

	void PrefetchScriptSound( const char *soundname )
	{
		int soundIndex = soundemitterbase->GetSoundIndex( soundname );
		if ( !soundemitterbase->IsValidIndex( soundIndex ) )
		{
			if ( Q_stristr( soundname, ".wav" ) || Q_strstr( soundname, ".mp3" ) )
			{
				CBaseEntity::PrefetchSound( soundname );
			}
			return;
		}

		InternalPrefetchWaves( soundIndex );
	}
public:

	void EmitSoundByHandle( IRecipientFilter& filter, int entindex, const EmitSound_t & ep, HSOUNDSCRIPTHANDLE& handle )
	{
		// Pull data from parameters
		CSoundParameters params;

		// Try to deduce the actor's gender
		gender_t gender = GENDER_NONE;
		CBaseEntity *ent = CBaseEntity::Instance( entindex );
		if ( ent )
		{
			char const *actorModel = STRING( ent->GetModelName() );
#if defined( L4D2 )
			gender = GetTerrorSoundActorGender( actorModel );
#else
			gender = soundemitterbase->GetActorGender( actorModel );
#endif
		}

		if ( !soundemitterbase->GetParametersForSoundEx( ep.m_pSoundName, handle, params, gender, true ) )
		{
			return;
		}

		if ( !params.soundname[0] )
			return;


		// handle SND_CHANGEPITCH/SND_CHANGEVOL and other sound flags.etc.
		if( ep.m_nFlags & SND_CHANGE_PITCH )
		{
			params.pitch = ep.m_nPitch;
		}


		if( ep.m_nFlags & SND_CHANGE_VOL )
		{
			params.volume = ep.m_flVolume;
		}

#if !defined( CLIENT_DLL )
		bool bSwallowed = CEnvMicrophone::OnSoundPlayed( 
			entindex, 
			params.soundname, 
			params.soundlevel, 
			params.volume, 
			ep.m_nFlags, 
			params.pitch, 
			ep.m_pOrigin, 
			ep.m_flSoundTime,
			ep.m_UtlVecSoundOrigin );
		if ( bSwallowed )
			return;
#endif
		CTerrorEngineSoundName engineSoundName( params.soundname );

#if defined( _DEBUG ) && !defined( CLIENT_DLL )
		if ( !engineSoundName.IsPrecached() )
		{
			Msg( "Sound %s:%s was not precached\n", ep.m_pSoundName, params.soundname );
		}
#endif

		float st = ep.m_flSoundTime;
		if ( !st && 
			params.delay_msec != 0 )
		{
			st = gpGlobals->curtime + (float)params.delay_msec / 1000.f;
		}

#if defined( L4D2 ) && !defined( CLIENT_DLL )
		PrecacheTerrorSoundForEmission( engineSoundName, ep.m_nFlags );
#endif
		enginesound->EmitSound( 
			filter, 
			entindex, 
			params.channel, 
			engineSoundName.Get(),
			params.volume,
			(soundlevel_t)params.soundlevel,
			ep.m_nFlags,
			params.pitch,
			ep.m_nSpecialDSP,
			ep.m_pOrigin,
			NULL,
			&ep.m_UtlVecSoundOrigin,
			true,
			st,
			ep.m_nSpeakerEntity );
		if ( ep.m_pflSoundDuration )
		{
			*ep.m_pflSoundDuration = enginesound->GetSoundDuration( engineSoundName.Get() );
		}

		TraceEmitSound(
#if defined( L4D2 )
			entindex,
#endif
			"EmitSound:  '%s' emitted as '%s' (ent %i)\n",
			ep.m_pSoundName, params.soundname, entindex );


		// Don't caption modulations to the sound
		if ( !( ep.m_nFlags & ( SND_CHANGE_PITCH | SND_CHANGE_VOL ) ) )
		{
			EmitCloseCaption( filter, entindex, params, ep, engineSoundName.Get() );
		}
#if defined( L4D2 ) && !defined( CLIENT_DLL )
		if ( ent )
		{
			KeyValues *gameData = NULL;
			if ( GetSoundGameData( handle, &gameData ) == SOUND_GAMEDATA_PRESENT )
			{
				// S1D7344 copies before synchronous dispatch; callbacks may mutate ep's source.
				const Vector origin = ep.m_pOrigin ? *ep.m_pOrigin : ent->WorldSpaceCenter();
				TheNextBots().OnSound( ent, origin, gameData );
			}
		}
#endif
#if defined( WIN32 ) && !defined( _X360 ) && !defined( L4D2 )
		// NVNT notify the haptics system of this sound
		HapticProcessSound(ep.m_pSoundName, entindex);
#endif
	}

	void EmitSound( IRecipientFilter& filter, int entindex, const EmitSound_t & ep )
	{
		VPROF( "CSoundEmitterSystem::EmitSound (calls engine)" );


		if ( ep.m_pSoundName && 
			( Q_stristr( ep.m_pSoundName, ".wav" ) || 
			  Q_stristr( ep.m_pSoundName, ".mp3" ) || 
			  ep.m_pSoundName[0] == '!' ) )
		{
#if !defined( CLIENT_DLL )
			bool bSwallowed = CEnvMicrophone::OnSoundPlayed( 
				entindex, 
				ep.m_pSoundName, 
				ep.m_SoundLevel, 
				ep.m_flVolume, 
				ep.m_nFlags, 
				ep.m_nPitch, 
				ep.m_pOrigin, 
				ep.m_flSoundTime,
				ep.m_UtlVecSoundOrigin );
			if ( bSwallowed )
				return;
#endif

			if ( ep.m_bWarnOnDirectWaveReference && 
				Q_stristr( ep.m_pSoundName, ".wav" ) )
			{
				WaveTrace( ep.m_pSoundName, "Emitsound" );
			}
			CTerrorEngineSoundName engineSoundName( ep.m_pSoundName );

#if defined( _DEBUG ) && !defined( CLIENT_DLL )
			if ( !engineSoundName.IsPrecached() )
			{
				Msg( "Sound %s was not precached\n", ep.m_pSoundName );
			}
#endif
#if defined( L4D2 ) && !defined( CLIENT_DLL )
			PrecacheTerrorSoundForEmission( engineSoundName, ep.m_nFlags );
#endif
			enginesound->EmitSound( 
				filter, 
				entindex, 
				ep.m_nChannel, 
				engineSoundName.Get(),
				ep.m_flVolume, 
				ep.m_SoundLevel, 
				ep.m_nFlags, 
				ep.m_nPitch, 
				ep.m_nSpecialDSP,
				ep.m_pOrigin,
				NULL, 
				&ep.m_UtlVecSoundOrigin,
				true, 
				ep.m_flSoundTime,
				ep.m_nSpeakerEntity );
			if ( ep.m_pflSoundDuration )
			{
				*ep.m_pflSoundDuration = enginesound->GetSoundDuration( engineSoundName.Get() );
			}

			TraceEmitSound(
#if defined( L4D2 )
				entindex,
#endif
				"EmitSound:  Raw wave emitted '%s' (ent %i)\n",
				ep.m_pSoundName, entindex );
			return;
		}

		if ( ep.m_hSoundScriptHandle == SOUNDEMITTER_INVALID_HANDLE )
		{
			ep.m_hSoundScriptHandle = (HSOUNDSCRIPTHANDLE)soundemitterbase->GetSoundIndex( ep.m_pSoundName );
		}

		if ( ep.m_hSoundScriptHandle == -1 )
			return;

		EmitSoundByHandle( filter, entindex, ep, ep.m_hSoundScriptHandle );
	}

#if defined( L4D2 ) && defined( CLIENT_DLL )
	void EmitCloseCaption( IRecipientFilter& filter, int entindex, bool fromplayer, char const *token, CUtlVector< Vector >& originlist, float duration, bool warnifmissing, bool direct = false )
#else
	void EmitCloseCaption( IRecipientFilter& filter, int entindex, bool fromplayer, char const *token, CUtlVector< Vector >& originlist, float duration, bool warnifmissing /*= false*/ )
#endif
	{
#if defined( L4D2 ) && !defined( CLIENT_DLL )
EmitServerCloseCaption( filter, entindex, fromplayer, token, originlist, duration, warnifmissing, false );
#else
#if defined( L4D2 ) && defined( CLIENT_DLL )
		if ( gpGlobals->maxClients == 1 && !g_pClosecaption->GetBool() )
#else
		// No close captions in multiplayer...
		if ( gpGlobals->maxClients > 1 || (gpGlobals->maxClients==1 && !g_pClosecaption->GetBool()))
#endif
		{
			return;
		}

		// A negative duration means fill it in from the wav file if possible
		if ( duration < 0.0f )
		{
			char const *wav = soundemitterbase->GetWavFileForSound( token, GENDER_NONE );
			if ( wav )
			{
				duration = enginesound->GetSoundDuration( CTerrorEngineSoundName( wav ).Get() );
			}
			else
			{
				duration = 2.0f;
			}
		}

		char lowercase[ 256 ];
		Q_strncpy( lowercase, token, sizeof( lowercase ) );
		Q_strlower( lowercase );
		if ( Q_strstr( lowercase, "\\" ) )
		{
			Hack_FixEscapeChars( lowercase );
		}

		// NOTE:  We must make a copy or else if the filter is owned by a SoundPatch, we'll end up destructively removing
		//  all players from it!!!!
		CRecipientFilter filterCopy;
		filterCopy.CopyFrom( (CRecipientFilter &)filter );

#if defined( L4D2 ) && defined( CLIENT_DLL )
		// C1FA770 removes secondary split-screen slots; the target has only slot 0.
		if ( !direct )
#endif
		// Remove any players who don't want close captions
		CBaseEntity::RemoveRecipientsIfNotCloseCaptioning( (CRecipientFilter &)filterCopy );

#if !defined( CLIENT_DLL )
		{
			// Defined in sceneentity.cpp
			bool AttenuateCaption( const char *token, const Vector& listener, CUtlVector< Vector >& soundorigins );

			if ( filterCopy.GetRecipientCount() > 0 )
			{
				int c = filterCopy.GetRecipientCount();
				for ( int i = c - 1 ; i >= 0; --i )
				{
					CBasePlayer *player = UTIL_PlayerByIndex( filterCopy.GetRecipientIndex( i ) );
					if ( !player )
						continue;

					Vector playerOrigin = player->GetAbsOrigin();

					if ( AttenuateCaption( lowercase, playerOrigin, originlist ) )
					{
						filterCopy.RemoveRecipient( player );
					}
				}
			}
		}
#endif
		// Anyone left?
		if ( filterCopy.GetRecipientCount() > 0 )
		{

#if !defined( CLIENT_DLL )

			byte byteflags = 0;
			if ( warnifmissing )
			{
				byteflags |= CLOSE_CAPTION_WARNIFMISSING;
			}
			if ( fromplayer )
			{
				byteflags |= CLOSE_CAPTION_FROMPLAYER;
			}

			CBaseEntity *pActor = CBaseEntity::Instance( entindex );
			if ( pActor )
			{
				char const *pszActorModel = STRING( pActor->GetModelName() );
				gender_t gender = soundemitterbase->GetActorGender( pszActorModel );

				if ( gender == GENDER_MALE )
				{
					byteflags |= CLOSE_CAPTION_GENDER_MALE;
				}
				else if ( gender == GENDER_FEMALE )
				{ 
					byteflags |= CLOSE_CAPTION_GENDER_FEMALE;
				}
			}

			// Send caption and duration hint down to client
			UserMessageBegin( filterCopy, "CloseCaption" );
				WRITE_STRING( lowercase );
				WRITE_SHORT( MIN( 255, (int)( duration * 10.0f ) ) ),
				WRITE_BYTE( byteflags ),
			MessageEnd();
#else
			// Direct dispatch
			CHudCloseCaption *cchud = GET_HUDELEMENT( CHudCloseCaption );
			if ( cchud )
			{
				cchud->ProcessCaption( lowercase, duration, fromplayer );
			}
#endif
		}
	#endif
	}

	void EmitCloseCaption( IRecipientFilter& filter, int entindex, const CSoundParameters & params, const EmitSound_t & ep, const char *pEngineSoundName )
	{
#if defined( L4D2 ) && !defined( CLIENT_DLL )
if ( gpGlobals->maxClients == 1 && !g_pClosecaption->GetBool() ) return;
const bool direct = TestSoundChar( params.soundname, '$' );
if ( !direct && !ep.m_bEmitCloseCaption ) return;
CRecipientFilter recipients;
recipients.CopyFrom( static_cast< CRecipientFilter & >( filter ) );
if ( !direct ) CBaseEntity::RemoveRecipientsIfNotCloseCaptioning( recipients );
if ( recipients.GetRecipientCount() <= 0 ) return;
const float duration = ep.m_pflSoundDuration ? *ep.m_pflSoundDuration : enginesound->GetSoundDuration( pEngineSoundName );
bool fromplayer = false;
for ( CBaseEntity *owner = CBaseEntity::Instance( entindex ); owner; owner = owner->GetOwnerEntity() )
{
 if ( owner->IsPlayer() ) { fromplayer = true; break; }
}
EmitServerCloseCaption( filter, entindex, fromplayer, ep.m_pSoundName, ep.m_UtlVecSoundOrigin, duration, ep.m_bWarnOnMissingCloseCaption, direct );
#elif defined( L4D2 ) && defined( CLIENT_DLL )
		if ( gpGlobals->maxClients == 1 && !g_pClosecaption->GetBool() )
			return;

		const bool direct = TestSoundChar( params.soundname, '$' );
		if ( !direct && !ep.m_bEmitCloseCaption )
			return;

		CRecipientFilter recipients;
		recipients.CopyFrom( static_cast< CRecipientFilter & >( filter ) );
		if ( !direct )
		{
			CBaseEntity::RemoveRecipientsIfNotCloseCaptioning( recipients );
		}
		if ( recipients.GetRecipientCount() <= 0 )
			return;

		const float duration = ep.m_pflSoundDuration ? *ep.m_pflSoundDuration : enginesound->GetSoundDuration( pEngineSoundName );
		bool fromplayer = false;
		for ( CBaseEntity *owner = CBaseEntity::Instance( entindex ); owner; owner = owner->GetOwnerEntity() )
		{
			if ( owner->IsPlayer() )
			{
				fromplayer = true;
				break;
			}
		}
		EmitCloseCaption( filter, entindex, fromplayer, ep.m_pSoundName, ep.m_UtlVecSoundOrigin, duration, ep.m_bWarnOnMissingCloseCaption, direct );
#else
		// No close captions in multiplayer...
		if ( gpGlobals->maxClients > 1 || (gpGlobals->maxClients==1 && !g_pClosecaption->GetBool()))
		{
			return;
		}

		if ( !ep.m_bEmitCloseCaption )
		{
			return;
		}

		// NOTE:  We must make a copy or else if the filter is owned by a SoundPatch, we'll end up destructively removing
		//  all players from it!!!!
		CRecipientFilter filterCopy;
		filterCopy.CopyFrom( (CRecipientFilter &)filter );

		// Remove any players who don't want close captions
		CBaseEntity::RemoveRecipientsIfNotCloseCaptioning( (CRecipientFilter &)filterCopy );

		// Anyone left?
		if ( filterCopy.GetRecipientCount() <= 0 )
		{
			return;
		}

		float duration = 0.0f;
		if ( ep.m_pflSoundDuration )
		{
			duration = *ep.m_pflSoundDuration;
		}
		else
		{
			duration = enginesound->GetSoundDuration( pEngineSoundName );
		}

		bool fromplayer = false;
		CBaseEntity *ent = CBaseEntity::Instance( entindex );
		if ( ent )
		{
			while ( ent )
			{
				if ( ent->IsPlayer() )
				{
					fromplayer = true;
					break;
				}

				ent = ent->GetOwnerEntity();
			}
		}
		EmitCloseCaption( filter, entindex, fromplayer, ep.m_pSoundName, ep.m_UtlVecSoundOrigin, duration, ep.m_bWarnOnMissingCloseCaption );
	#endif
	}

	void EmitAmbientSound( int entindex, const Vector& origin, const char *soundname, float flVolume, int iFlags, int iPitch, float soundtime /*= 0.0f*/, float *duration /*=NULL*/ )
	{
		// Pull data from parameters
		CSoundParameters params;

		if ( !soundemitterbase->GetParametersForSound( soundname, params, GENDER_NONE ) )
		{
			return;
		}


		if( iFlags & SND_CHANGE_PITCH )
		{
			params.pitch = iPitch;
		}

		if( iFlags & SND_CHANGE_VOL )
		{
			params.volume = flVolume;
		}

		CTerrorEngineSoundName engineSoundName( params.soundname );
#if defined( CLIENT_DLL )
		enginesound->EmitAmbientSound( engineSoundName.Get(), params.volume, params.pitch, iFlags, soundtime );
#else
#if defined( L4D2 )
		PrecacheTerrorSoundForEmission( engineSoundName, iFlags );
#endif
		engine->EmitAmbientSound(entindex, origin, engineSoundName.Get(), params.volume, params.soundlevel, iFlags, params.pitch, soundtime );
#endif

		bool needsCC = !( iFlags & ( SND_STOP | SND_CHANGE_VOL | SND_CHANGE_PITCH ) );

		float soundduration = 0.0f;
		
		if ( duration || needsCC )
		{
			soundduration = enginesound->GetSoundDuration( engineSoundName.Get() );
			if ( duration )
			{
				*duration = soundduration;
			}
		}

		TraceEmitSound(
#if defined( L4D2 )
			entindex,
#endif
			"EmitAmbientSound:  '%s' emitted as '%s' (ent %i)\n",
			soundname, params.soundname, entindex );

		// We only want to trigger the CC on the start of the sound, not on any changes or halting of the sound
		if ( needsCC )
		{
			CRecipientFilter filter;
			filter.AddAllPlayers();
			filter.MakeReliable();

			CUtlVector< Vector > dummy;
			EmitCloseCaption( filter, entindex, false, soundname, dummy, soundduration, false );
		}

	}

	void StopSoundByHandle( int entindex, const char *soundname, HSOUNDSCRIPTHANDLE& handle )
	{
		if ( handle == SOUNDEMITTER_INVALID_HANDLE )
		{
			handle = (HSOUNDSCRIPTHANDLE)soundemitterbase->GetSoundIndex( soundname );
		}

		if ( handle == SOUNDEMITTER_INVALID_HANDLE )
			return;

		CSoundParametersInternal *params;

		params = soundemitterbase->InternalGetParametersForSound( (int)handle );
		if ( !params )
		{
			return;
		}

		// HACK:  we have to stop all sounds if there are > 1 in the rndwave section...
		int c = params->NumSoundNames();
		for ( int i = 0; i < c; ++i )
		{
			char const *wavename = soundemitterbase->GetWaveName( params->GetSoundNames()[ i ].symbol );
			Assert( wavename );

			enginesound->StopSound( 
				entindex, 
				params->GetChannel(), 
				CTerrorEngineSoundName( wavename ).Get() );

			TraceEmitSound(
#if defined( L4D2 )
				entindex,
#endif
				"StopSound:  '%s' stopped as '%s' (ent %i)\n",
				soundname, wavename, entindex );
		}
	}

	void StopSound( int entindex, const char *soundname )
	{
		HSOUNDSCRIPTHANDLE handle = (HSOUNDSCRIPTHANDLE)soundemitterbase->GetSoundIndex( soundname );
		if ( handle == SOUNDEMITTER_INVALID_HANDLE )
		{
			return;
		}

		StopSoundByHandle( entindex, soundname, handle );
	}


	void StopSound( int iEntIndex, int iChannel, const char *pSample )
	{
		if ( pSample && ( Q_stristr( pSample, ".wav" ) || Q_stristr( pSample, ".mp3" ) || pSample[0] == '!' ) )
		{
			enginesound->StopSound( iEntIndex, iChannel, CTerrorEngineSoundName( pSample ).Get() );

			TraceEmitSound(
#if defined( L4D2 )
				iEntIndex,
#endif
				"StopSound:  Raw wave stopped '%s' (ent %i)\n",
				pSample, iEntIndex );
		}
		else
		{
			// Look it up in sounds.txt and ignore other parameters
			StopSound( iEntIndex, pSample );
		}
	}

	void EmitAmbientSound( int entindex, const Vector &origin, const char *pSample, float volume, soundlevel_t soundlevel, int flags, int pitch, float soundtime /*= 0.0f*/, float *duration /*=NULL*/ )
	{

#if !defined( CLIENT_DLL )
		CUtlVector< Vector > dummyorigins;

		// Loop through all registered microphones and tell them the sound was just played
		// NOTE: This means that pitch shifts/sound changes on the original ambient will not be reflected in the re-broadcasted sound
		bool bSwallowed = CEnvMicrophone::OnSoundPlayed( 
							entindex, 
							pSample, 
							soundlevel, 
							volume, 
							flags, 
							pitch, 
							&origin, 
							soundtime,
							dummyorigins );
		if ( bSwallowed )
			return;
#endif

		if ( pSample && ( Q_stristr( pSample, ".wav" ) || Q_stristr( pSample, ".mp3" )) )
		{
			CTerrorEngineSoundName engineSoundName( pSample );
#if defined( CLIENT_DLL )
			enginesound->EmitAmbientSound( engineSoundName.Get(), volume, pitch, flags, soundtime );
#else
#if defined( L4D2 )
			PrecacheTerrorSoundForEmission( engineSoundName, flags );
#endif
			engine->EmitAmbientSound( entindex, origin, engineSoundName.Get(), volume, soundlevel, flags, pitch, soundtime );
#endif

			if ( duration )
			{
				*duration = enginesound->GetSoundDuration( engineSoundName.Get() );
			}

			TraceEmitSound(
#if defined( L4D2 )
				entindex,
#endif
				"EmitAmbientSound:  Raw wave emitted '%s' (ent %i)\n",
				pSample, entindex );
		}
		else
		{
			EmitAmbientSound( entindex, origin, pSample, volume, flags, pitch, soundtime, duration );
		}
	}
};

static CSoundEmitterSystem g_SoundEmitterSystem( "CSoundEmitterSystem" );

IGameSystem *SoundEmitterSystem()
{
	return &g_SoundEmitterSystem;
}

#if defined( CLIENT_DLL )
void ReloadSoundEntriesInList( IFileList *pFilesToReload )
{
	g_SoundEmitterSystem.ReloadSoundEntriesInList( pFilesToReload );
}
#endif

void S_SoundEmitterSystemFlush( void ) 
{
#if !defined( CLIENT_DLL )
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
#endif

	// save the current soundscape
	// kill the system
	g_SoundEmitterSystem.Flush();

#if !defined( CLIENT_DLL )
	// Redo precache all wave files... (this should work now that we have dynamic string tables)
	g_SoundEmitterSystem.LevelInitPreEntity();

	// These store raw sound indices for faster precaching, blow them away.
	ClearModelSoundsCache();
#endif

	// TODO:  when we go to a handle system, we'll need to invalidate handles somehow
}

#if !defined( L4D2 ) || !defined( CLIENT_DLL )
#if defined( CLIENT_DLL )
CON_COMMAND_F( cl_soundemitter_flush, "Flushes the sounds.txt system (client only)", FCVAR_CHEAT )
#else

#if defined( L4D2 ) && defined( GAME_DLL )
CON_COMMAND_F( sv_soundemitter_flush, "Flushes the sounds.txt system (server only)", 0x00000006 )
#else
CON_COMMAND_F( sv_soundemitter_flush, "Flushes the sounds.txt system (server only)", FCVAR_DEVELOPMENTONLY )
#endif
#endif
{
	S_SoundEmitterSystemFlush( );
}
#endif

#if !defined(_RETAIL)

#if !defined( CLIENT_DLL ) 

#if !defined( _XBOX )


#if defined( L4D2 ) && defined( GAME_DLL )
CON_COMMAND_F( sv_soundemitter_filecheck, "Report missing wave files for sounds and game_sounds files.", 0x00000006 )
#else
CON_COMMAND_F( sv_soundemitter_filecheck, "Report missing wave files for sounds and game_sounds files.", FCVAR_DEVELOPMENTONLY )
#endif
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	int missing = soundemitterbase->CheckForMissingWavFiles( true );
	DevMsg( "---------------------------\nTotal missing files %i\n", missing );
}


#if defined( L4D2 ) && defined( GAME_DLL )
CON_COMMAND_F( sv_findsoundname, "Find sound names which reference the specified wave files.", 0x00000006 )
#else
CON_COMMAND_F( sv_findsoundname, "Find sound names which reference the specified wave files.", FCVAR_DEVELOPMENTONLY )
#endif
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	if ( args.ArgC() != 2 )
		return;

	int c = soundemitterbase->GetSoundCount();
	int i;

	char const *search = args[ 1 ];
	if ( !search )
		return;

	for ( i = 0; i < c; i++ )
	{
		CSoundParametersInternal *internal = soundemitterbase->InternalGetParametersForSound( i );
		if ( !internal )
			continue;

		int waveCount = internal->NumSoundNames();
		if ( waveCount > 0 )
		{
			for( int wave = 0; wave < waveCount; wave++ )
			{
				char const *wavefilename = soundemitterbase->GetWaveName( internal->GetSoundNames()[ wave ].symbol );

				if ( Q_stristr( wavefilename, search ) )
				{
					char const *soundname = soundemitterbase->GetSoundName( i );
					char const *scriptname = soundemitterbase->GetSourceFileForSound( i );

					Msg( "Referenced by '%s:%s' -- %s\n", scriptname, soundname, wavefilename );
				}
			}
		}
	}
}
#endif // !_XBOX

#else
void Playgamesound_f( const CCommand &args )
{
	CBasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer )
	{
		if ( args.ArgC() > 2 )
		{
			Vector position = pPlayer->EyePosition();
			Vector forward;
			pPlayer->GetVectors( &forward, NULL, NULL );
			position += atof( args[2] ) * forward;
#if defined( L4D2 )
			const bool absQueriesValid = CBaseEntity::IsAbsQueriesValid();
			CBaseEntity::SetAbsQueriesValid( true );
			{
#endif
			CPASAttenuationFilter filter( pPlayer );
			EmitSound_t params;
			params.m_pSoundName = args[1];
			params.m_pOrigin = &position;
			params.m_flVolume = 0.0f;
			params.m_nPitch = 0;
			g_SoundEmitterSystem.EmitSound( filter, 0, params );
#if defined( L4D2 )
			}
			CBaseEntity::SetAbsQueriesValid( absQueriesValid );
#endif
		}
		else
		{
			pPlayer->EmitSound( args[1] );
		}
	}
	else
	{
		Msg("Can't play until a game is started.\n");
		// UNDONE: Make something like this work?
		//CBroadcastRecipientFilter filter;
		//g_SoundEmitterSystem.EmitSound( filter, 1, args[1], 0.0, 0, 0, &vec3_origin, 0, NULL );
	}
}

static int GamesoundCompletion( const char *partial, char commands[ COMMAND_COMPLETION_MAXITEMS ][ COMMAND_COMPLETION_ITEM_LENGTH ] )
{
	int current = 0;

	const char *cmdname = "playgamesound";
	char *substring = NULL;
	int substringLen = 0;
	if ( Q_strstr( partial, cmdname ) && strlen(partial) > strlen(cmdname) + 1 )
	{
		substring = (char *)partial + strlen( cmdname ) + 1;
		substringLen = strlen(substring);
	}
	
	for ( int i = soundemitterbase->GetSoundCount()-1; i >= 0 && current < COMMAND_COMPLETION_MAXITEMS; i-- )
	{
		const char *pSoundName = soundemitterbase->GetSoundName( i );
		if ( pSoundName )
		{
			if ( !substring || !Q_strncasecmp( pSoundName, substring, substringLen ) )
			{
				Q_snprintf( commands[ current ], sizeof( commands[ current ] ), "%s %s", cmdname, pSoundName );
				current++;
			}
		}
	}

	return current;
}

static ConCommand Command_Playgamesound( "playgamesound", Playgamesound_f, "Play a sound from the game sounds txt file", 
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x50000008
#else
	FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE
#endif
, GamesoundCompletion );
#endif

#endif

//-----------------------------------------------------------------------------
// Purpose:  Non-static override for doing the general case of CPASAttenuationFilter( this ), and EmitSound( filter, entindex(), etc. );
// Input  : *soundname - 
//-----------------------------------------------------------------------------
void CBaseEntity::EmitSound( const char *soundname, float soundtime /*= 0.0f*/, float *duration /*=NULL*/ )
{
	//VPROF( "CBaseEntity::EmitSound" );
	VPROF_BUDGET( "CBaseEntity::EmitSound", _T( "CBaseEntity::EmitSound" ) );

	CPASAttenuationFilter filter( this, soundname );

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = soundtime;
	params.m_pflSoundDuration = duration;
	params.m_bWarnOnDirectWaveReference = true;

	EmitSound( filter, entindex(), params );
}

#if !defined ( CLIENT_DLL )
void CBaseEntity::ScriptEmitSound( const char *soundname )
{
	EmitSound( soundname );
}

void CBaseEntity::ScriptStopSound( const char *soundname )
{
	StopSound( soundname );
}

float CBaseEntity::ScriptSoundDuration( const char *soundname, const char *actormodel )
{
	float duration = CBaseEntity::GetSoundDuration( soundname, actormodel );
	return duration;
}
#endif // !CLIENT


//-----------------------------------------------------------------------------
// Purpose:  Non-static override for doing the general case of CPASAttenuationFilter( this ), and EmitSound( filter, entindex(), etc. );
// Input  : *soundname - 
//-----------------------------------------------------------------------------
void CBaseEntity::EmitSound( const char *soundname, HSOUNDSCRIPTHANDLE& handle, float soundtime /*= 0.0f*/, float *duration /*=NULL*/ )
{
	VPROF_BUDGET( "CBaseEntity::EmitSound", _T( "CBaseEntity::EmitSound" ) );

	// VPROF( "CBaseEntity::EmitSound" );
	CPASAttenuationFilter filter( this, soundname, handle );

	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = soundtime;
	params.m_pflSoundDuration = duration;
	params.m_bWarnOnDirectWaveReference = true;

	EmitSound( filter, entindex(), params, handle );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : filter - 
//			iEntIndex - 
//			*soundname - 
//			*pOrigin - 
//-----------------------------------------------------------------------------
void CBaseEntity::EmitSound( IRecipientFilter& filter, int iEntIndex, const char *soundname, const Vector *pOrigin /*= NULL*/, float soundtime /*= 0.0f*/, float *duration /*=NULL*/ )
{
	if ( !soundname )
		return;

	VPROF_BUDGET( "CBaseEntity::EmitSound", _T( "CBaseEntity::EmitSound" ) );

	// VPROF( "CBaseEntity::EmitSound" );
	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = soundtime;
	params.m_pOrigin = pOrigin;
	params.m_pflSoundDuration = duration;
	params.m_bWarnOnDirectWaveReference = true;

	EmitSound( filter, iEntIndex, params, params.m_hSoundScriptHandle );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : filter - 
//			iEntIndex - 
//			*soundname - 
//			*pOrigin - 
//-----------------------------------------------------------------------------
void CBaseEntity::EmitSound( IRecipientFilter& filter, int iEntIndex, const char *soundname, HSOUNDSCRIPTHANDLE& handle, const Vector *pOrigin /*= NULL*/, float soundtime /*= 0.0f*/, float *duration /*=NULL*/ )
{
	VPROF_BUDGET( "CBaseEntity::EmitSound", _T( "CBaseEntity::EmitSound" ) );

	//VPROF( "CBaseEntity::EmitSound" );
	EmitSound_t params;
	params.m_pSoundName = soundname;
	params.m_flSoundTime = soundtime;
	params.m_pOrigin = pOrigin;
	params.m_pflSoundDuration = duration;
	params.m_bWarnOnDirectWaveReference = true;

	EmitSound( filter, iEntIndex, params, handle );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : filter - 
//			iEntIndex - 
//			params - 
//-----------------------------------------------------------------------------
void CBaseEntity::EmitSound( IRecipientFilter& filter, int iEntIndex, const EmitSound_t & params )
{
	VPROF_BUDGET( "CBaseEntity::EmitSound", _T( "CBaseEntity::EmitSound" ) );

#ifdef GAME_DLL
	CBaseEntity *pEntity = UTIL_EntityByIndex( iEntIndex );
#else
	C_BaseEntity *pEntity = ClientEntityList().GetEnt( iEntIndex );
#endif
	if ( pEntity )
	{
		pEntity->ModifyEmitSoundParams( const_cast< EmitSound_t& >( params ) );
	}

	// VPROF( "CBaseEntity::EmitSound" );
	// Call into the sound emitter system...
	g_SoundEmitterSystem.EmitSound( filter, iEntIndex, params );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : filter - 
//			iEntIndex - 
//			params - 
//-----------------------------------------------------------------------------
void CBaseEntity::EmitSound( IRecipientFilter& filter, int iEntIndex, const EmitSound_t & params, HSOUNDSCRIPTHANDLE& handle )
{
	VPROF_BUDGET( "CBaseEntity::EmitSound", _T( "CBaseEntity::EmitSound" ) );

#ifdef GAME_DLL
	CBaseEntity *pEntity = UTIL_EntityByIndex( iEntIndex );
#else
	C_BaseEntity *pEntity = ClientEntityList().GetEnt( iEntIndex );
#endif
	if ( pEntity )
	{
		pEntity->ModifyEmitSoundParams( const_cast< EmitSound_t& >( params ) );
	}

	// VPROF( "CBaseEntity::EmitSound" );
	// Call into the sound emitter system...
	g_SoundEmitterSystem.EmitSoundByHandle( filter, iEntIndex, params, handle );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *soundname - 
//-----------------------------------------------------------------------------
void CBaseEntity::StopSound( const char *soundname )
{
#if defined( CLIENT_DLL )
	if ( entindex() == -1 )
	{
		// If we're a clientside entity, we need to use the soundsourceindex instead of the entindex
		StopSound( GetSoundSourceIndex(), soundname );
		return;
	}
#endif

	StopSound( entindex(), soundname );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *soundname - 
//-----------------------------------------------------------------------------
void CBaseEntity::StopSound( const char *soundname, HSOUNDSCRIPTHANDLE& handle )
{
#if defined( CLIENT_DLL )
	if ( entindex() == -1 )
	{
		// If we're a clientside entity, we need to use the soundsourceindex instead of the entindex
		StopSound( GetSoundSourceIndex(), soundname );
		return;
	}
#endif

	g_SoundEmitterSystem.StopSoundByHandle( entindex(), soundname, handle );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : iEntIndex - 
//			*soundname - 
//-----------------------------------------------------------------------------
void CBaseEntity::StopSound( int iEntIndex, const char *soundname )
{
	g_SoundEmitterSystem.StopSound( iEntIndex, soundname );
}

void CBaseEntity::StopSound( int iEntIndex, int iChannel, const char *pSample )
{
	g_SoundEmitterSystem.StopSound( iEntIndex, iChannel, pSample );
}

soundlevel_t CBaseEntity::LookupSoundLevel( const char *soundname )
{
	return soundemitterbase->LookupSoundLevel( soundname );
}


soundlevel_t CBaseEntity::LookupSoundLevel( const char *soundname, HSOUNDSCRIPTHANDLE& handle )
{
	return soundemitterbase->LookupSoundLevelByHandle( soundname, handle );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *entity - 
//			origin - 
//			flags - 
//			*soundname - 
//-----------------------------------------------------------------------------
void CBaseEntity::EmitAmbientSound( int entindex, const Vector& origin, const char *soundname, int flags, float soundtime /*= 0.0f*/, float *duration /*=NULL*/ )
{
	g_SoundEmitterSystem.EmitAmbientSound( entindex, origin, soundname, 0.0, flags, 0, soundtime, duration );
}

// HACK HACK:  Do we need to pull the entire SENTENCEG_* wrapper over to the client .dll?
#if defined( CLIENT_DLL )
int SENTENCEG_Lookup(const char *sample)
{
	return engine->SentenceIndexFromName( sample + 1 );
}
#endif

void UTIL_EmitAmbientSound( int entindex, const Vector &vecOrigin, const char *samp, float vol, soundlevel_t soundlevel, int fFlags, int pitch, float soundtime /*= 0.0f*/, float *duration /*=NULL*/ )
{

	if (samp && *samp == '!')
	{
		int sentenceIndex = SENTENCEG_Lookup(samp);
		if (sentenceIndex >= 0)
		{
			char name[32];
			Q_snprintf( name, sizeof(name), "!%d", sentenceIndex );
#if !defined( CLIENT_DLL )
			engine->EmitAmbientSound( entindex, vecOrigin, CTerrorEngineSoundName( name ).Get(), vol, soundlevel, fFlags, pitch, soundtime );
#else
			enginesound->EmitAmbientSound( CTerrorEngineSoundName( name ).Get(), vol, pitch, fFlags, soundtime );
#endif
			if ( duration )
			{
				*duration = enginesound->GetSoundDuration( CTerrorEngineSoundName( name ).Get() );
			}

			g_SoundEmitterSystem.TraceEmitSound(
#if defined( L4D2 )
				entindex,
#endif
				"UTIL_EmitAmbientSound:  Sentence emitted '%s' (ent %i)\n",
				name, entindex );
		}
	}
	else
	{
		g_SoundEmitterSystem.EmitAmbientSound( entindex, vecOrigin, samp, vol, soundlevel, fFlags, pitch, soundtime, duration );
	}
}

static const char *UTIL_TranslateSoundName( const char *soundname, const char *actormodel )
{
	Assert( soundname );

	if ( Q_stristr( soundname, ".wav" ) || Q_stristr( soundname, ".mp3" ) )
	{
		if ( Q_stristr( soundname, ".wav" ) )
		{
			WaveTrace( soundname, "UTIL_TranslateSoundName" );
		}
		return soundname;
	}

#if defined( L4D2 )
	return soundemitterbase->GetWavFileForSound( soundname, GetTerrorSoundActorGender( actormodel ) );
#else
	return soundemitterbase->GetWavFileForSound( soundname, actormodel );
#endif
}

void CBaseEntity::GenderExpandString( char const *in, char *out, int maxlen )
{
#if defined( L4D2 )
	ExpandTerrorSoundActorString( GetTerrorSoundActorGender( STRING( GetModelName() ) ), in, out, maxlen );
#else
	soundemitterbase->GenderExpandString( STRING( GetModelName() ), in, out, maxlen );
#endif
}

bool CBaseEntity::GetParametersForSound( const char *soundname, CSoundParameters &params, const char *actormodel )
{
#if defined( L4D2 )
	gender_t gender = GetTerrorSoundActorGender( actormodel );
#else
	gender_t gender = soundemitterbase->GetActorGender( actormodel );
#endif
	
	return soundemitterbase->GetParametersForSound( soundname, params, gender );
}

bool CBaseEntity::GetParametersForSound( const char *soundname, HSOUNDSCRIPTHANDLE& handle, CSoundParameters &params, const char *actormodel )
{
#if defined( L4D2 )
	gender_t gender = GetTerrorSoundActorGender( actormodel );
#else
	gender_t gender = soundemitterbase->GetActorGender( actormodel );
#endif
	
	return soundemitterbase->GetParametersForSoundEx( soundname, handle, params, gender );
}

HSOUNDSCRIPTHANDLE CBaseEntity::PrecacheScriptSound( const char *soundname )
{
#if !defined( CLIENT_DLL )
	return g_SoundEmitterSystem.PrecacheScriptSound( soundname );
#else
	return soundemitterbase->GetSoundIndex( soundname );
#endif
}

#if !defined ( CLIENT_DLL )
// Same as server version of above, but signiture changed so it can be deduced by the macros
void CBaseEntity::VScriptPrecacheScriptSound( const char *soundname )
{
	g_SoundEmitterSystem.PrecacheScriptSound( soundname );
}
#endif // !CLIENT_DLL

void CBaseEntity::PrefetchScriptSound( const char *soundname )
{
	g_SoundEmitterSystem.PrefetchScriptSound( soundname );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *soundname - 
// Output : float
//-----------------------------------------------------------------------------
float CBaseEntity::GetSoundDuration( const char *soundname, char const *actormodel )
{
	return enginesound->GetSoundDuration( CTerrorEngineSoundName( PSkipSoundChars( UTIL_TranslateSoundName( soundname, actormodel ) ) ).Get() );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : filter - 
//			*token - 
//			duration - 
//			warnifmissing - 
//-----------------------------------------------------------------------------
void CBaseEntity::EmitCloseCaption( IRecipientFilter& filter, int entindex, char const *token, CUtlVector< Vector >& soundorigin, float duration, bool warnifmissing /*= false*/ )
{
	bool fromplayer = false;
	CBaseEntity *ent = CBaseEntity::Instance( entindex );
	while ( ent )
	{
		if ( ent->IsPlayer() )
		{
			fromplayer = true;
			break;
		}
		ent = ent->GetOwnerEntity();
	}

	g_SoundEmitterSystem.EmitCloseCaption( filter, entindex, fromplayer, token, soundorigin, duration, warnifmissing );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *name - 
//			preload - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseEntity::PrecacheSound( const char *name )
{
	if ( IsPC() && !g_bPermitDirectSoundPrecache )
	{
		Warning( "Direct precache of %s\n", name );
	}

	const CTerrorEngineSoundName engineSoundName( name );
	// If this is out of order, warn
	if ( !CBaseEntity::IsPrecacheAllowed() )
	{
		if ( !engineSoundName.IsPrecached() )
		{
			Assert( !"CBaseEntity::PrecacheSound:  too late" );

			Warning( "Late precache of %s\n", name );
		}
	}

	bool bret = enginesound->PrecacheSound( engineSoundName.Get(), true );
	return bret;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *name - 
//-----------------------------------------------------------------------------
void CBaseEntity::PrefetchSound( const char *name )
{
	 enginesound->PrefetchSound( CTerrorEngineSoundName( name ).Get() );
}

#if defined( L4D2 ) && defined( CLIENT_DLL )
#include "l4d2/sound/soundemittersystem_console.inc"
#include "l4d2/sound/soundemittersystem_console_registrations.inc"
#endif

#include "SoundEmitterSystem_console_server_callbacks.inc"
