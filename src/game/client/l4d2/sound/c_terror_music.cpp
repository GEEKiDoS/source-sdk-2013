//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/sound/terror_sound_gamedata.h"
#include "c_world.h"
#include "soundenvelope.h"
#include "engine/IEngineSound.h"
#include "c_recipientfilter.h"
#include "c_playerresource.h"
#include "usermessages.h"
#include "KeyValues.h"
#include "interval.h"
#include "soundchars.h"
#include "l4d2/sound/terror_engine_sound_name.h"
#include "con_nprint.h"
#include <xmmintrin.h>
#include <emmintrin.h>
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// Original 0x4000 is FCVAR_CHEAT, not FCVAR_REPLICATED.
static ConVar music_master_debug( "music_master_debug", "0", 0x00004008 );
static ConVar music_master_tag_threshold( "music_master_tag_threshold", "0.0", 0x00004008 );
// Native zero-input constructors finish with flag 2 (DEVELOPMENTONLY).
static ConVar music_master_spew( "music_master_spew", "0", 0x0000000A );
static ConVar music_master_latency_report( "music_master_latency_report", "0", 0x0000000A );
static ConVar verbose_tank_music( "verbose_tank_music", "0", 0x0000000A );
static ConVar cl_music_debug( "cl_music_debug", "0", 0x0000000A );

static double MusicRandomInterval( const interval_t &interval )
{
	// C137480 returns the float RNG result plus start without a float spill.
	if ( interval.range == 0.0f )
		return interval.start;
	return static_cast< double >( RandomFloat( 0.0f, interval.range ) ) + interval.start;
}

static float GetMusicVolume()
{
	// Native constructs this reference on FIRST use, after engine connection.
	static ConVarRef volume( "snd_musicvolume" );
	return volume.GetFloat();
}

static void MusicDebug( const char *format, ... )
{
	if ( !cl_music_debug.GetBool() )
		return;
	char text[512];
	va_list args;
	va_start( args, format );
	Q_vsnprintf( text, sizeof( text ), format, args );
	va_end( args );
	ConColorMsg( Color( 0, 192, 255, 255 ), "%f: %s", gpGlobals->curtime, text );
}

static CTerrorPlayer *MusicLocalPlayer( int slot = -1 )
{
	// The target has two real local-player slots, but no slot-1 engine producer.
	// Do not manufacture a split-screen player or index its array with -1.
	return ToTerrorPlayer( slot < 0 ? C_BasePlayer::GetLocalPlayer() : C_BasePlayer::GetLocalPlayer( slot ) );
}

static CTerrorPlayer *MusicViewPlayer( int slot = -1 )
{
	CTerrorPlayer *player = MusicLocalPlayer( slot );
	if ( !player )
		return NULL;
	if ( player->GetObserverMode() == OBS_MODE_IN_EYE || player->GetObserverMode() == OBS_MODE_CHASE )
	{
		CTerrorPlayer *target = ToTerrorPlayer( player->GetObserverTarget() );
		if ( target )
			return target;
	}
	return player;
}

static CTerrorPlayer *MusicListener( int slot )
{
	CTerrorPlayer *player = MusicLocalPlayer( slot );
	if ( player && !player->IsAlive() && player->GetMusic()->HasEvent( "Event.SurvivorDeath" ) )
		return player;
	return MusicViewPlayer( slot );
}

static bool Engine_GetAutoDSPPlayInOutState()
{
	// CAPABILITY GAP, explicitly approved by Main: target IVEngineClient has no
	// auto-DSP room-state read. Original +580 -> engine 1005E220 -> 1000C310
	// reads byte_1043F669. Its PE virtual-tail storage is loader-zeroed, and its
	// only writes are in engine-internal room/DAS scan 100150B0. Retain that
	// ORIGINAL INITIAL value (false), not a demo/nav/trace substitute.
	return false;
}

static bool GetMusicTrackName( const char *event, char *track, int size )
{
	CSoundParameters parameters;
	if ( !C_BaseEntity::GetParametersForSound( event, parameters, NULL ) )
	{
		MusicDebug( "GetMusicTrackName: Unable to find sound %s\n", event );
		return false;
	}
	Q_strncpy( track, "default", size );
	KeyValues *data = GetSoundScriptGameData( event );
	if ( data )
	{
		const char *name = data->GetString( "MusicTrack", "" );
		if ( name[0] )
			Q_strncpy( track, name, size );
	}
	// Native MusicTrackType's QUEUED/MULTI result is write-only. It does not
	// select parallel playback; see the packet's write-only-field census.
	return true;
}

static bool IsTankMusicEvent( const char *event )
{
	return !Q_strcmp( event, "Event.Tank" ) || !Q_strcmp( event, "Event.TankMidpoint" ) || !Q_strcmp( event, "Event.TankBrothers" );
}

class MusicMasterClip
{
public:
	MusicMasterClip( Music *music, const char *event, float duration, int emitter, const Vector *position );
	~MusicMasterClip() { CSoundEnvelopeController::GetController().SoundDestroy( m_patch ); }
	bool IsEngaged() const { return m_engaged || ( m_splitEngaged && m_playSplit ); }
	double Elapsed() const;
	double AbsoluteTime() const;
	double Remaining() const;
	double Phase() const;
	double CycleBase() const;
	void SynchronizeClock();
	bool IsFinished() const;
	bool TagTimeMet( float target ) const;
	double NextTag( float offset, int delay, int index ) const;
	void Start();
	bool Stop( float fade, bool force = false );
	float EffectiveVolume() const;
	double EngagementVolume() const;
	void UpdateVolume();
	void DebugPrint() const;

private:
	friend class MusicMasterTrack;
	friend class Music;
	double NextTagOffset( float offset, int delay ) const;
	void ReadData( const char *event );
	Music *m_music;
	CSoundPatch *m_patch;
	char m_event[128];
	char m_wave[128];
	int m_emitter;
	Vector m_position;
	float m_duration;
	float m_startTime;
	float m_endTime;
	bool m_loops;
	bool m_clockSet;
	bool m_stopping;
	bool m_engaged;
	bool m_splitEngaged;
	bool m_playSplit;
	bool m_playToEnd;
	bool m_dontDisengage;
	bool m_dontEngage; // Native diagnostic state; deliberately not an admission gate.
	int m_priority;
	float m_fadeOut;
	char m_autoQueue[128];
	char m_duckTracks[256];
	char m_stopTracks[256];
	char m_blockTracks[256];
	char m_tagTrack[128];
	bool m_waitForTag;
	bool m_tagSelected;
	float m_tagTarget;
	interval_t m_tagDelay;
	int m_tagDelayMult;
	int m_tagIndex;
	interval_t m_delayTime;
	bool m_delayStarted;
	CountdownTimer m_delayTimer;
	CUtlVector<float> m_tags;
	float m_volume;
	float m_duckVolume;
	float m_trackVolume;
};

class MusicMasterTrack
{
public:
	MusicMasterTrack( Music *music, const char *name ) :
		m_music( music ), m_active( NULL ), m_volume( 1.0f ),
		m_engaged( music->m_engaged ), m_splitEngaged( music->m_splitEngaged )
	{
		Q_strncpy( m_name, name, sizeof( m_name ) );
	}
	~MusicMasterTrack()
	{
		delete m_active;
		m_queued.PurgeAndDeleteElements();
		m_retiring.PurgeAndDeleteElements();
	}
	void SetEngaged( bool engaged );
	void SetSplitEngaged( bool engaged );
	bool Contains( const char *event ) const;
	bool Queue( MusicMasterClip *clip );
	void StopActive( float fade );
	void StopAll( float fade )
	{
		StopActive( fade );
		for ( int i = 0; i < m_queued.Count(); ++i ) delete m_queued[i];
		m_queued.RemoveAll(); // Native stop retains allocation for the next queue.
	}
	bool StopEvent( const char *event, float fade );
	void Update();
	void Transition();
	void DebugPrint() const;

	Music *m_music;
	char m_name[128];
	MusicMasterClip *m_active;
	CUtlVector<MusicMasterClip *> m_retiring;
	CUtlVector<MusicMasterClip *> m_queued;
	float m_volume;
	bool m_engaged;
	bool m_splitEngaged;
};

MusicMasterClip::MusicMasterClip( Music *music, const char *event, float, int emitter, const Vector *position ) :
	m_music( music ), m_patch( NULL ), m_emitter( emitter ), m_duration( 0.0f ),
	m_startTime( 0.0f ), m_endTime( -1.0f ), m_loops( false ), m_clockSet( false ),
	m_stopping( true ), m_engaged( true ), m_splitEngaged( false ), m_playSplit( false ),
	m_playToEnd( false ), m_dontDisengage( false ), m_dontEngage( false ),
	m_priority( 1 ), m_fadeOut( 0.0f ), m_waitForTag( false ), m_tagSelected( false ),
	m_tagTarget( 0.0f ), m_tagDelayMult( 1 ), m_tagIndex( -1 ), m_delayStarted( false ),
	m_volume( 1.0f ), m_duckVolume( 1.0f ), m_trackVolume( 1.0f )
{
	Q_strncpy( m_event, event, sizeof( m_event ) );
	m_autoQueue[0] = m_duckTracks[0] = m_stopTracks[0] = m_blockTracks[0] = m_tagTrack[0] = 0;
	m_delayTime = ReadInterval( "0.0" );
	m_tagDelay = ReadInterval( "0.0" );
	m_position.Init();
	if ( position )
	{
		m_position = *position;
		m_emitter = 0;
	}
	CBroadcastRecipientFilter filter;
	filter.MakeReliable();
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	if ( emitter >= 0 && !position )
		m_patch = controller.SoundCreateMusic( filter, m_emitter, event );
	else
	{
		if ( emitter < 0 && !position )
			MusicDebug( "MusicMasterClip: emit entity < 0 without position: %s\n", event );
		// Original 1024CB70 uses entity 0 and its initialized position vector.
		m_patch = controller.SoundCreateMusic( filter, 0, event, &m_position );
	}
	Q_strncpy( m_wave, PSkipSoundChars( STRING( controller.SoundGetName( m_patch ) ) ), sizeof( m_wave ) );
	m_duration = enginesound->GetSoundDuration( CTerrorEngineSoundName( m_wave ).Get() );
	m_loops = IsMusicSoundLooping( m_wave );
	ReadData( event );
	MusicDebug( "set data: %s,  %f, loop %i\n", event, m_duration, m_loops );
}

void MusicMasterClip::ReadData( const char *event )
{
	// Native calls GetParametersForSound again AFTER SoundCreate selected its
	// wave. Keep that RNG/volume order; retain only copied data, never this tree.
	CSoundParameters parameters;
	if ( !C_BaseEntity::GetParametersForSound( event, parameters, NULL ) )
	{
		MusicDebug( "MusicMasterClip: Unable to find sound %s\n", event );
		return;
	}
	KeyValues *data = GetSoundScriptGameData( event );
	if ( data )
	{
		KeyValues *tags = data->FindKey( "MusicMasterTags" );
		if ( tags )
			for ( KeyValues *tag = tags->GetFirstSubKey(); tag; tag = tag->GetNextKey() )
				m_tags.AddToTail( tag->GetFloat() );
		KeyValues *divTags = data->FindKey( "MusicMasterDivTags" );
		if ( divTags )
		{
			const float divisions = divTags->GetFloat();
			if ( divisions > 0.0f )
			{
				const float slice = m_duration / divisions;
				// Native fractional-division behavior includes i < divisions+1.
				for ( int i = 1; (float)i < (float)( divisions + 1.0f ); ++i )
					m_tags.AddToTail( (float)i * slice );
			}
		}
		const char *priority = data->GetString( "MusicPriority", "LOW" );
		if ( !Q_strcmp( priority, "LOW" ) ) m_priority = 1;
		else if ( !Q_strcmp( priority, "MEDIUM" ) ) m_priority = 2;
		else if ( !Q_strcmp( priority, "HIGH" ) ) m_priority = 3;
		else if ( !Q_strcmp( priority, "CRITICAL" ) ) m_priority = 4;
		const char *master = data->GetString( "MusicMaster", "" );
		m_playToEnd = Q_stristr( master, "PLAY_TO_END" ) != NULL;
		m_playSplit = Q_stristr( master, "PLAY_SPLIT" ) != NULL;
		m_dontDisengage = Q_stristr( master, "DONT_DISENGAGE" ) != NULL;
		m_dontEngage = Q_stristr( master, "DONT_ENGAGE" ) != NULL;
		const char *fade = data->GetString( "MusicFadeOut", "" );
		if ( fade[0] ) m_fadeOut = atof( fade );
		const char *delay = data->GetString( "MusicDelayTime", "0.0" );
		if ( delay[0] ) m_delayTime = ReadInterval( delay );
		const char *tagDelay = data->GetString( "MusicTagDelay", "0.0" );
		if ( tagDelay[0] ) m_tagDelay = ReadInterval( tagDelay );
		const char *tagIndex = data->GetString( "MusicTagIndex", "-1" );
		if ( tagIndex[0] ) m_tagIndex = atoi( tagIndex );
		const char *tagMult = data->GetString( "MusicTagDelayMult", "1.0" );
		if ( tagMult[0] ) m_tagDelayMult = atoi( tagMult );
		const char *tagTrack = data->GetString( "MusicTagTrack", "" );
		if ( tagTrack[0] )
		{
			m_waitForTag = true;
			Q_strncpy( m_tagTrack, tagTrack, sizeof( m_tagTrack ) );
		}
		Q_strncpy( m_autoQueue, data->GetString( "MusicAutoQueue", "" ), sizeof( m_autoQueue ) );
		Q_strncpy( m_duckTracks, data->GetString( "MusicDuckTrackList", "" ), sizeof( m_duckTracks ) );
		Q_strncpy( m_stopTracks, data->GetString( "MusicStopTrackList", "" ), sizeof( m_stopTracks ) );
		Q_strncpy( m_blockTracks, data->GetString( "MusicBlockTrackList", "" ), sizeof( m_blockTracks ) );
	}
	m_volume = parameters.volume;
}

double MusicMasterClip::Elapsed() const
{
	if ( IsEngaged() )
		return CSoundEnvelopeController::GetController().SoundGetElapsedTimeApproximate( m_patch );
	return (double)gpGlobals->realtime - m_startTime;
}

double MusicMasterClip::AbsoluteTime() const
{
	const float elapsed = IsEngaged()
		? CSoundEnvelopeController::GetController().SoundGetElapsedTimeApproximate( m_patch )
		: gpGlobals->realtime - m_startTime;
	return (double)elapsed + m_startTime;
}

double MusicMasterClip::Remaining() const
{
	const float elapsed = IsEngaged()
		? CSoundEnvelopeController::GetController().SoundGetElapsedTimeApproximate( m_patch )
		: gpGlobals->realtime - m_startTime;
	return (double)m_duration - elapsed;
}

double MusicMasterClip::Phase() const
{
	const float elapsed = IsEngaged()
		? CSoundEnvelopeController::GetController().SoundGetElapsedTimeApproximate( m_patch )
		: gpGlobals->realtime - m_startTime;
	return !( m_duration >= elapsed ) ? fmod( (double)elapsed, (double)m_duration ) : elapsed;
}

double MusicMasterClip::CycleBase() const
{
	const float elapsed = IsEngaged()
		? CSoundEnvelopeController::GetController().SoundGetElapsedTimeApproximate( m_patch )
		: gpGlobals->realtime - m_startTime;
	if ( !( m_duration >= elapsed ) )
	{
		const float quotient = elapsed / m_duration;
		const int cycles = _mm_cvtt_ss2si( _mm_set_ss( quotient ) );
		return static_cast< double >( cycles ) * static_cast< double >( m_duration );
	}
	return 0.0f;
}

void MusicMasterClip::SynchronizeClock()
{
	if ( m_clockSet )
		return;
	if ( IsEngaged() )
	{
		const float elapsed = CSoundEnvelopeController::GetController().SoundGetElapsedTimeApproximate( m_patch );
		if ( !( elapsed > 0.0f ) )
			return;
		m_startTime = gpGlobals->realtime - elapsed;
	}
	else
		m_startTime = gpGlobals->realtime;
	m_endTime = m_loops ? -1.0f : m_duration + m_startTime;
	m_clockSet = true;
}

bool MusicMasterClip::IsFinished() const
{
	if ( m_endTime < 0.0f || !m_clockSet )
		return false;
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	if ( IsEngaged() && !controller.SoundIsPlaying( m_patch ) )
		return true;
	if ( AbsoluteTime() >= m_endTime )
	{
		if ( music_master_latency_report.GetBool() )
		{
			const float virtualElapsed = gpGlobals->realtime - m_startTime;
			const double mixerElapsed = controller.SoundGetElapsedTimeApproximate( m_patch );
			const float savedMixerElapsed = mixerElapsed;
			if ( virtualElapsed != savedMixerElapsed && savedMixerElapsed > 0.0f && fabs( virtualElapsed - mixerElapsed ) > 0.1 )
				MusicDebug( "END LATENCY: %s : %f\n", m_wave, virtualElapsed - mixerElapsed );
		}
		return true;
	}
	return false;
}

bool MusicMasterClip::TagTimeMet( float target ) const
{
	const float elapsed = IsEngaged()
		? CSoundEnvelopeController::GetController().SoundGetElapsedTimeApproximate( m_patch )
		: gpGlobals->realtime - m_startTime;
	return (float)( m_startTime + elapsed ) >= (float)( target - music_master_tag_threshold.GetFloat() );
}

double MusicMasterClip::NextTagOffset( float offset, int delay ) const
{
	if ( !m_tags.Count() )
		return static_cast< double >( static_cast< int >( static_cast< unsigned int >( delay ) + 1u ) ) * m_duration;
	const float elapsed = (float)( Phase() + offset );
	float smallest = -1.0f;
	int nearest = 0;
	for ( int i = 0; i < m_tags.Count(); ++i )
	{
		const float difference = m_tags[i] - elapsed;
		if ( m_tags[i] > elapsed && ( smallest < 0.0f || smallest > difference ) )
		{
			nearest = i;
			smallest = difference;
		}
	}
	float tag = m_tags[nearest];
	// C24D2C7 advances only a POSITIVE delay. Negative delays do not index
	// backward, even though the no-tag branch still uses delay + 1.
	if ( delay > 0 )
	{
		const int last = static_cast< int >( static_cast< unsigned int >( nearest ) + static_cast< unsigned int >( delay ) );
		if ( last < m_tags.Count() )
			tag = m_tags[last];
		else
		{
			const float cycles = static_cast< float >( last / m_tags.Count() );
			const float start = cycles * m_duration;
			tag = start + m_tags[last % m_tags.Count()];
		}
		for ( int i = nearest; i <= last; i = static_cast< int >( static_cast< unsigned int >( i ) + 1u ) )
		{
			const double queuedTag = i < m_tags.Count() ? m_tags[i] :
				static_cast< double >( i / m_tags.Count() ) * m_duration + m_tags[i % m_tags.Count()];
			MusicDebug( "TAG in queue: %f\n", queuedTag );
		}
	}
	return tag;
}

double MusicMasterClip::NextTag( float offset, int delay, int index ) const
{
	if ( !m_clockSet )
		return -1.0f;
	const float tag = index < 0 ? NextTagOffset( offset, delay ) : m_tags[MIN( index, m_tags.Count() - 1 )];
	const float phase = Phase();
	float base = CycleBase() + m_startTime;
	if ( phase > (double)tag )
		base = m_duration + base;
	return (double)tag + base;
}

float MusicMasterClip::EffectiveVolume() const
{
	return (float)( (float)( m_duckVolume * m_volume ) * m_trackVolume ) * GetMusicVolume();
}

double MusicMasterClip::EngagementVolume() const
{
	// Native 1024CC50 uses promoted x87 products; Start/Update use SSE singles.
	return (double)m_duckVolume * m_volume * m_trackVolume * GetMusicVolume();
}

void MusicMasterClip::Start()
{
	if ( !m_patch )
		return;
	m_stopping = false;
	SynchronizeClock();
	m_music->StopTracksByList( m_stopTracks );
	if ( IsEngaged() )
		CSoundEnvelopeController::GetController().Play( m_patch, EffectiveVolume(), 100.0f, 0.0f );
	// CAPABILITY GAP (Main-approved): native Tank.wav/Taank.wav L/R volume
	// diagnostic, original IEngineSound+88, and ONLY its quiet/not-playing log
	// branch are omitted. Target has no per-channel L/R query. No fake scalar.
}

bool MusicMasterClip::Stop( float fade, bool force )
{
	MusicDebug( "Stop: %s : %f : %f\n", m_event, fade, m_duration );
	m_stopping = true;
	const float elapsed = IsEngaged()
		? CSoundEnvelopeController::GetController().SoundGetElapsedTimeApproximate( m_patch )
		: gpGlobals->realtime - m_startTime;
	const float now = m_startTime + elapsed;
	float duration = fade >= 0.0f ? fade : m_fadeOut;
	if ( m_playToEnd && !force && !m_loops && fade < 0.0f )
		return true;
	if ( !( duration > 0.0f ) || force )
	{
		m_endTime = now;
		return false;
	}
	if ( !m_loops )
	{
		const double remaining = Remaining();
		if ( duration > remaining )
			duration = static_cast< float >( remaining );
	}
	if ( IsEngaged() )
		CSoundEnvelopeController::GetController().SoundFadeOut( m_patch, duration, false );
	m_endTime = now + duration;
	return true;
}

void MusicMasterClip::UpdateVolume()
{
	const float volume = EffectiveVolume();
	if ( m_patch && IsEngaged() )
	{
		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
		// CAPABILITY GAP: ONLY native 1024CFF0's optional +88 L/R diagnostic
		// query/logs are omitted, as approved; the real gain update stays below.
		if ( volume != controller.SoundGetVolume( m_patch ) )
			controller.SoundChangeVolume( m_patch, volume, 0.0f );
	}
}

void MusicMasterClip::DebugPrint() const
{
	MusicDebug( "\tMusicMasterClip: %s\n", m_event );
	MusicDebug( "\t\tm_sndFile: %s\n", m_wave );
	MusicDebug( "\t\tm_engageEngine: %s\n", m_engaged ? "true" : "false" );
	MusicDebug( "\t\tm_dontDisengage: %s\n", m_dontDisengage ? "true" : "false" );
	MusicDebug( "\t\tm_dontEngage: %s\n", m_dontEngage ? "true" : "false" );
	MusicDebug( "\t\tm_patch: %s\n", m_patch ? "true" : "false" );
	MusicDebug( "\t\tm_duration: %f\n", m_duration );
	MusicDebug( "\t\tm_targetTime: %f\n", m_tagTarget );
	MusicDebug( "\t\tm_loops: %s\n", m_loops ? "true" : "false" );
	MusicDebug( "\t\tm_playToEnd: %s\n", m_playToEnd ? "true" : "false" );
	MusicDebug( "\t\tm_waitForTag: %s\n", m_waitForTag ? "true" : "false" );
	MusicDebug( "\t\tm_fadeOut: %f\n", m_fadeOut );
	MusicDebug( "\t\tm_priority: %i\n", m_priority );
	MusicDebug( "\t\tm_autoQueue: %s\n", m_autoQueue );
	MusicDebug( "\t\tm_duckTrackList: %s\n", m_duckTracks );
	MusicDebug( "\t\tm_volume: %f\n", m_volume );
	MusicDebug( "\t\tm_duckVolume: %f\n", m_duckVolume );
	MusicDebug( "\t\tm_sliceDenom: %f\n", 1.0 );
	MusicDebug( "\t\tm_startTime: %f\n", m_startTime );
	MusicDebug( "\t\tm_endTime: %f\n", m_endTime );
	MusicDebug( "\t\telapsed: %f\n", AbsoluteTime() );
}

void MusicMasterTrack::SetEngaged( bool engaged )
{
	if ( m_engaged == engaged )
		return;
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	if ( engaged )
	{
		MusicDebug( "Engaging Track: : %s\n", m_name );
		if ( m_active )
		{
			// Native takes the OLD engagement's loop phase before changing flags.
			const float offset = -m_active->Phase();
			if ( m_active->m_patch && !m_active->m_engaged )
			{
				m_active->m_engaged = true;
				m_active->m_clockSet = false;
				MusicDebug( "Engaging Clip: %s : %f\n", m_active->m_event, offset );
				controller.Play( m_active->m_patch, m_active->EngagementVolume(), 100.0f, offset );
			}
		}
	}
	else
	{
		MusicDebug( "DisEngaging Track:  : %s\n", m_name );
		if ( m_active )
		{
			if ( m_active->m_patch && m_active->m_engaged )
			{
				MusicDebug( "DisEngaging Clip: %s\n", m_active->m_event );
				if ( !m_active->m_dontDisengage ) controller.Shutdown( m_active->m_patch );
			}
			m_active->m_engaged = false;
		}
	}
	for ( int i = 0; i < m_queued.Count(); ++i ) m_queued[i]->m_engaged = engaged;
	m_engaged = engaged;
	// Retiring clips deliberately do not follow engagement changes.
}

void MusicMasterTrack::SetSplitEngaged( bool engaged )
{
	if ( m_splitEngaged == engaged )
		return;
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	if ( engaged )
	{
		MusicDebug( "Engaging Split Track: : %s\n", m_name );
		if ( m_active )
		{
			const float offset = -m_active->Phase();
			if ( m_active->m_patch && !m_active->m_splitEngaged )
			{
				m_active->m_splitEngaged = true;
				if ( m_active->m_playSplit )
				{
					m_active->m_clockSet = false;
					MusicDebug( "Engaging Split Clip: %s : %f\n", m_active->m_event, offset );
					controller.Play( m_active->m_patch, m_active->EngagementVolume(), 100.0f, offset );
				}
			}
		}
	}
	else
	{
		MusicDebug( "DisEngaging Split Track:  : %s\n", m_name );
		if ( m_active )
		{
			if ( m_active->m_patch && m_active->m_splitEngaged )
			{
				MusicDebug( "DisEngaging Clip: %s\n", m_active->m_event );
				if ( !m_active->m_dontDisengage && m_active->m_playSplit ) controller.Shutdown( m_active->m_patch );
			}
			m_active->m_splitEngaged = false;
		}
	}
	for ( int i = 0; i < m_queued.Count(); ++i ) m_queued[i]->m_splitEngaged = engaged;
	m_splitEngaged = engaged;
}

bool MusicMasterTrack::Contains( const char *event ) const
{
	for ( int i = 0; i < m_queued.Count(); ++i )
		if ( !Q_strcmp( m_queued[i]->m_event, event ) ) return true;
	return m_active && !Q_strcmp( m_active->m_event, event );
}

bool MusicMasterTrack::Queue( MusicMasterClip *clip )
{
	if ( !Q_strcmp( clip->m_wave, "common/null.wav" ) ) return false;
	clip->m_engaged = m_engaged;
	clip->m_splitEngaged = m_splitEngaged;
	if ( m_active && m_active->m_priority > clip->m_priority && !clip->m_waitForTag ) return false;
	m_queued.AddToTail( clip );
	Update(); // Native admission synchronously services this track, not the root.
	return true;
}

void MusicMasterTrack::StopActive( float fade )
{
	if ( !m_active ) return;
	m_active->m_engaged = m_engaged;
	m_active->m_splitEngaged = m_splitEngaged;
	if ( m_active->m_stopping ) return;
	if ( m_active->Stop( fade ) ) m_retiring.AddToTail( m_active );
	else delete m_active;
	m_active = NULL;
}

bool MusicMasterTrack::StopEvent( const char *event, float fade )
{
	if ( m_active && !Q_strcmp( m_active->m_event, event ) )
	{
		StopActive( fade );
		return true;
	}
	for ( int i = 0; i < m_queued.Count(); ++i )
		if ( !Q_strcmp( m_queued[i]->m_event, event ) )
		{
			delete m_queued[i];
			m_queued.Remove( i );
			return true;
		}
	return false;
}

void MusicMasterTrack::Transition()
{
	if ( !m_queued.Count() ) return;
	int emitter = 0;
	Vector position( 0, 0, 0 );
	if ( m_active )
	{
		emitter = m_active->m_emitter;
		position = m_active->m_position;
		StopActive( -1.0f );
	}
	m_active = m_queued[0];
	m_queued.Remove( 0 );
	m_active->m_engaged = m_engaged;
	m_active->m_splitEngaged = m_splitEngaged;
	m_active->m_trackVolume = m_volume;
	m_active->Start();
	if ( m_active->m_autoQueue[0] )
	{
		// Native inherits the PREVIOUS clip's emitter/position, not this clip's.
		m_music->AutoQueueSound( m_active->m_autoQueue, -1.0f, emitter, emitter > 0 ? NULL : &position );
		MusicDebug( "MusicMasterClip: Queued m_autoQueue %s\n", m_active->m_autoQueue );
	}
}

void MusicMasterTrack::Update()
{
	if ( music_master_spew.GetBool() && m_active )
	{
		const float mixer = CSoundEnvelopeController::GetController().SoundGetElapsedTimeApproximate( m_active->m_patch );
		if ( mixer > 0.0f )
		{
			const double elapsed = m_active->Elapsed();
			const double difference = m_active->Elapsed() - mixer;
			MusicDebug( "UPDATE: %f : %f : %f : %f : %s\n", difference, mixer, elapsed, m_active->m_duration, m_active->m_wave );
		}
	}
	// Original prunes BEFORE synchronization and synchronizes only the active
	// clip. Retiring clips never acquire a missing clock after being stopped.
	if ( m_active && m_active->IsFinished() ) { delete m_active; m_active = NULL; }
	for ( int i = m_retiring.Count() - 1; i >= 0; --i )
		if ( m_retiring[i]->IsFinished() ) { delete m_retiring[i]; m_retiring.Remove( i ); }
	if ( m_active ) { m_active->SynchronizeClock(); m_active->UpdateVolume(); }
	if ( !m_queued.Count() ) return;
	MusicMasterClip *clip = m_queued[0];
	// Native samples on EVERY service, even while a previously started delay
	// is still running. Do not change the global RNG stream by moving this.
	const float delay = RandomInterval( clip->m_delayTime );
	if ( clip->m_delayStarted && !clip->m_delayTimer.IsElapsed() ) return;
	if ( delay > 0.0f && !clip->m_delayStarted )
	{
		MusicDebug( "DelayTime: %f\n", delay );
		clip->m_delayTimer.Start( delay );
		clip->m_delayStarted = true;
		return;
	}
	if ( clip->m_tagTrack[0] )
	{
		if ( !clip->m_tagSelected )
		{
			// C250459 calls signed32 __ftol2_sse on the WIDE interval result,
			// then IMUL keeps the low32 bits. Do not round to float first.
			const int tagDelay = _mm_cvttsd_si32( _mm_set_sd( MusicRandomInterval( clip->m_tagDelay ) ) );
			const int count = static_cast< int >( static_cast< unsigned int >( tagDelay ) * static_cast< unsigned int >( clip->m_tagDelayMult ) );
			MusicMasterTrack *master = m_music->FindTrack( clip->m_tagTrack );
			const double target = master && master->m_active ? master->m_active->NextTag( 0.0f, count, clip->m_tagIndex ) : -1.0;
			if ( target < 0.0f ) return;
			clip->m_tagTarget = target;
			clip->m_tagSelected = true;
			MusicDebug( "TargetTime: %f\n", target );
		}
		MusicMasterTrack *master = m_music->FindTrack( clip->m_tagTrack );
		// Once selected, disappearance of the master means target MET natively.
		if ( master && master->m_active && !master->m_active->TagTimeMet( clip->m_tagTarget ) ) return;
		MusicDebug( "Target Time Met: %f : %f\n", clip->m_tagTarget, master && master->m_active ? master->m_active->Phase() : 0.0f );
	}
	Transition();
}

void MusicMasterTrack::DebugPrint() const
{
	MusicDebug( "MusicMasterTrack:\n" );
	if ( m_active )
	{
		MusicDebug( "ActiveClip:\n" );
		m_active->DebugPrint();
	}
	MusicDebug( "DeleteQue:\n" );
	for ( int i = 0; i < m_retiring.Count(); ++i )
	{
		MusicDebug( "%i)  ", i );
		m_retiring[i]->DebugPrint();
	}
}

BEGIN_RECV_TABLE_NOBASE( Music, DT_Music )
	RecvPropFloat( RECVINFO( nv_m_CIDamageDuck ) ),
	RecvPropFloat( RECVINFO( nv_m_CIDamageMob ) ),
	RecvPropFloat( RECVINFO( nv_m_zombatMusic ) ), // Native extra; intentionally not a server sendprop.
	RecvPropFloat( RECVINFO( nv_m_zombatMusic2 ) ),
	RecvPropFloat( RECVINFO( nv_m_zombatMusic3 ) ),
	RecvPropInt( RECVINFO( nv_m_inCheckpoint ) ),
	RecvPropFloat( RECVINFO( nv_m_ambientVolume ) ),
	RecvPropFloat( RECVINFO( nv_m_witchRage ) ),
	RecvPropFloat( RECVINFO( m_fNVAdrenaline ) ),
END_RECV_TABLE()

Music::Music() : m_me( NULL ), m_firstUpdate( true ), m_checkpointStarted( false ), m_gameOver( false ),
	m_engaged( true ), m_splitEngaged( false ), m_lastAmbientVolume( 0.0f )
{
	nv_m_zombatMusic = 0.0f;
	nv_m_witchRage = 0.0f;
	m_checkpointEvent[0] = m_deathTrack[0] = 0;
	GetMusicTrackName( "Event.SurvivorDeath", m_deathTrack, sizeof( m_deathTrack ) );
}

Music::~Music()
{
	for ( int i = 0; i < m_tracks.Count(); ++i ) delete m_tracks[i];
	m_tracks.Purge();
}

void Music::SetOwner( CTerrorPlayer *player )
{
	m_me = player;
	if ( !player->IsLocalPlayer() || player->GetTeamNumber() == TEAM_SPECTATOR )
	{
		SetEngaged( false );
		SetSplitEngaged( false );
	}
}

void Music::Reset()
{
	MusicDebug( "RESET: %s\n", m_me->GetPlayerName() );
	StopCommand( "all", 1.0f, false );
	for ( int i = 0; i < m_tracks.Count(); ++i ) delete m_tracks[i];
	m_tracks.Purge();
	m_engaged = true;
	m_splitEngaged = false;
	if ( !m_me->IsLocalPlayer() || m_me->GetTeamNumber() == TEAM_SPECTATOR )
	{
		SetEngaged( false );
		SetSplitEngaged( false );
	}
	nv_m_witchRage = 0.0f;
	m_lastAmbientVolume = 0.0f;
	m_checkpointStarted = false;
	m_gameOver = false;
	m_firstUpdate = true;
}

MusicMasterTrack *Music::FindTrack( const char *name ) const
{
	const int i = name ? m_tracks.Find( name ) : m_tracks.InvalidIndex();
	return i != m_tracks.InvalidIndex() ? m_tracks[i] : NULL;
}

MusicMasterTrack *Music::FindTrack( const char *name, bool create )
{
	MusicMasterTrack *track = FindTrack( name );
	if ( !track && create )
	{
		track = new MusicMasterTrack( this, name );
		m_tracks.Insert( name, track );
		MusicDebug( "ADDED TRACK: %s\n", name );
	}
	return track;
}

bool Music::HasEvent( const char *event ) const
{
	// Native listener selection uses the death-track name cached at construction,
	// not a fresh metadata lookup and not queued/retiring death clips.
	MusicMasterTrack *track = FindTrack( m_deathTrack );
	return track && track->m_active && !Q_strcmp( track->m_active->m_event, event );
}

void Music::SetEngaged( bool engaged )
{
	m_engaged = engaged;
	for ( int i = 0; i < m_tracks.Count(); ++i ) m_tracks[i]->SetEngaged( engaged );
}

void Music::SetSplitEngaged( bool engaged )
{
	m_splitEngaged = engaged;
	for ( int i = 0; i < m_tracks.Count(); ++i ) m_tracks[i]->SetSplitEngaged( engaged );
}

bool Music::IsTrackDucked( const char *track ) const
{
	for ( int i = 0; i < m_tracks.Count(); ++i )
	{
		MusicMasterClip *clip = m_tracks[i]->m_active;
		if ( clip && ( !Q_strcmp( clip->m_duckTracks, "all" ) || Q_stristr( clip->m_duckTracks, track ) ) ) return true;
	}
	return false;
}

bool Music::IsTrackBlocked( const char *track ) const
{
	for ( int i = 0; i < m_tracks.Count(); ++i )
	{
		MusicMasterClip *clip = m_tracks[i]->m_active;
		if ( clip && ( !Q_strcmp( clip->m_blockTracks, "all" ) || Q_stristr( clip->m_blockTracks, track ) ) ) return true;
	}
	return false;
}

void Music::StopTracksByList( const char *list )
{
	if ( !list[0] ) return;
	const bool all = !Q_strcmp( list, "all" );
	for ( int i = 0; i < m_tracks.Count(); ++i )
	{
		MusicMasterTrack *track = m_tracks[i];
		if ( all || ( track->m_active && Q_stristr( list, track->m_name ) ) ) track->StopAll( -1.0f );
	}
}

void Music::SetTrackVolume( const char *name, float volume )
{
	MusicMasterTrack *track = FindTrack( name );
	if ( track )
	{
		track->m_volume = volume;
		if ( track->m_active ) track->m_active->m_trackVolume = volume;
	}
}

bool Music::AutoQueueSound( const char *event, float duration, int emitter, const Vector *position )
{
	char trackName[128] = "";
	if ( !GetMusicTrackName( event, trackName, sizeof( trackName ) ) ) return false;
	MusicMasterTrack *track = FindTrack( trackName, true );
	if ( IsTrackBlocked( trackName ) ) { MusicDebug( "BLOCKED: %s\n", trackName ); return false; }
	if ( track->Contains( event ) ) return false;
	MusicMasterClip *clip = new MusicMasterClip( this, event, duration, emitter, position );
	if ( !track->Queue( clip ) ) delete clip;
	// Native returns true after creation/admission attempt, even if lower
	// priority or common/null.wav caused Queue to reject the new clip.
	return true;
}

void Music::Play( const char *event, int emitter, float duration, bool, bool )
{
	PlayCommand( event, duration, emitter, NULL );
}

void Music::Stop( const char *event, float fade, bool track )
{
	StopCommand( event, fade, track );
}

void Music::GeneralCommand( const char *command, const char *arg1, const char *arg2, int entity )
{
	ReceiveGeneralCommand( command, arg1, arg2, entity );
}

void Music::PlayCommand( const char *event, float duration, int emitter, const Vector *position )
{
	if ( Q_stristr( event, "Checkpoint" ) )
	{
		C_World *world = GetClientWorldEntity();
		Q_snprintf( m_checkpointEvent, sizeof( m_checkpointEvent ), "%s_%s",
			world->GetStartMusicType() ? "Event.CheckPointIntro" : "Event.MissionStart", world->GetMusicPostfix() );
		event = m_checkpointEvent;
	}
	if ( position ) MusicDebug( "Music::PLAYPOS : %s : %s : %i : %i : (%f, %f, %f)\n", m_me->GetPlayerName(), event, m_me->entindex(), emitter, position->x, position->y, position->z );
	else MusicDebug( "Music::PLAY : %s : %s : %i : %i\n", m_me->GetPlayerName(), event, m_me->entindex(), emitter );
	if ( verbose_tank_music.GetBool() && IsTankMusicEvent( event ) )
		Msg( "%f: %s: Client starting tank music: %s\n", gpGlobals->curtime, m_me->GetPlayerName(), event );
	CSoundParameters parameters;
	if ( !C_BaseEntity::GetParametersForSound( event, parameters, NULL ) )
	{
		MusicDebug( "PlayCommand: %s : Unable to find sound %s\n", m_me->GetPlayerName(), event );
		return;
	}
	const char *trackName = "main";
	bool afterDeath = false, afterMission = false;
	KeyValues *data = GetSoundScriptGameData( event );
	if ( data )
	{
		const char *flags = data->GetString( "MusicParameters", "" );
		afterDeath = Q_stristr( flags, "AFTER_DEATH" ) != NULL;
		afterMission = Q_stristr( flags, "AFTER_MISSION_END" ) != NULL;
		const char *name = data->GetString( "MusicTrack", "" );
		if ( name[0] ) trackName = name;
	}
	if ( !m_me->IsAlive() && !afterDeath ) return;
	if ( m_gameOver && !afterMission ) { MusicDebug( "GAME OVER: %s\n", m_me->GetPlayerName() ); return; }
	if ( !MusicLocalPlayer() ) MusicDebug( "PlayCommand: Unable to get local player!\n" );
	MusicMasterTrack *track = FindTrack( trackName, true );
	if ( IsTrackBlocked( trackName ) ) { MusicDebug( "BLOCKED: %s\n", trackName ); return; }
	if ( track->Contains( event ) )
	{
		MusicDebug( "PlayCommand: %s : %s already queued.\n", m_me->GetPlayerName(), event );
		return;
	}
	MusicMasterClip *clip = new MusicMasterClip( this, event, duration, emitter, position );
	if ( !track->Queue( clip ) ) delete clip;
}

void Music::StopCommand( const char *event, float fade, bool byTrack )
{
	MusicDebug( "Music::STOP : %s : %s\n", m_me->GetPlayerName(), event );
	if ( verbose_tank_music.GetBool() && IsTankMusicEvent( event ) )
		Msg( "%f: %s: Client stopping tank music: %s\n", gpGlobals->curtime, m_me->GetPlayerName(), event );
	if ( byTrack )
	{
		MusicDebug( "Music::STOPTRACK : %s : %s\n", m_me->GetPlayerName(), event );
		MusicMasterTrack *track = FindTrack( event );
		if ( track ) track->StopAll( fade );
		else MusicDebug( "StopTrackCommand: %s : %s unable to find track.\n", m_me->GetPlayerName(), event );
		return;
	}
	if ( !Q_strcmp( event, "all" ) )
	{
		for ( int i = 0; i < m_tracks.Count(); ++i ) m_tracks[i]->StopAll( fade );
		return;
	}
	if ( Q_stristr( event, "Checkpoint" ) )
	{
		char name[128] = "";
		if ( GetMusicTrackName( m_checkpointEvent, name, sizeof( name ) ) )
		{
			MusicMasterTrack *track = FindTrack( name );
			if ( track ) track->StopAll( 5.0f );
		}
		return;
	}
	CSoundParameters parameters;
	if ( !C_BaseEntity::GetParametersForSound( event, parameters, NULL ) )
	{
		MusicDebug( "StopCommand: %s : Unable to find sound %s\n", m_me->GetPlayerName(), event );
		return;
	}
	const char *trackName = "main";
	KeyValues *data = GetSoundScriptGameData( event );
	if ( data )
	{
		const char *name = data->GetString( "MusicTrack", "" );
		if ( name[0] ) trackName = name;
	}
	MusicMasterTrack *track = FindTrack( trackName );
	if ( !track || !track->StopEvent( event, fade ) )
		MusicDebug( "StopCommand: %s : %s not playing or queued.\n", m_me->GetPlayerName(), event );
}

void Music::ReceiveGeneralCommand( const char *command, const char *arg1, const char *arg2, int entity )
{
	MusicDebug( "**GeneralCommand\n" );
	if ( Q_stristr( command, "Reset" ) ) Reset();
	else if ( Q_stristr( command, "GameOver" ) ) m_gameOver = true;
	else if ( Q_stristr( command, "PlayInOut" ) )
	{
		MusicDebug( "PlayInOut\n" );
		if ( m_me == MusicViewPlayer() )
		{
			const char *event = Engine_GetAutoDSPPlayInOutState() ? arg2 : arg1;
			C_BaseEntity *speaker = cl_entitylist->GetEnt( entity );
			speaker->EmitSound( event );
			MusicDebug( "Played: %s\n", event );
		}
	}
	else if ( Q_stristr( command, "PlayerRestored" ) && arg1 && arg1[0] )
	{
		atoi( arg1 ); // Native discarded conversion; no state copy from that player.
		Reset();
	}
}

void Music::UpdateTracks()
{
	// Native root performs duck traversal BEFORE servicing any track.
	for ( int i = 0; i < m_tracks.Count(); ++i )
	{
		MusicMasterTrack *track = m_tracks[i];
		if ( track->m_active && Q_strcmp( track->m_active->m_duckTracks, "all" ) )
			track->m_active->m_duckVolume = IsTrackDucked( m_tracks.GetElementName( i ) ) ? 0.05f : 1.0f;
	}
	for ( int i = 0; i < m_tracks.Count(); ++i ) m_tracks[i]->Update();
}

void Music::UpdateMix()
{
	// CAPABILITY GAP (Main-approved): target engine lacks original
	// IVEngineClient +592/+596 dynamic layer lookup/set. Native selection and
	// exact values, in call order, are retained here; ONLY these calls are
	// omitted. No static mixer preset, substitute gains, or no-op layer API.
	// GetMixLayerIndex("mobBeatingLayer"); SetMixLayerLevel(index, 1.0f - nv_m_CIDamageDuck);
	// GetMixLayerIndex("mobRulesLayer"); SetMixLayerLevel(index, 1.0f - nv_m_CIDamageMob);
	// GetMixLayerIndex("witchRageLayer"); SetMixLayerLevel(index, 1.0f - nv_m_witchRage);
	// GetMixLayerIndex("zombatMusicLayer"); SetMixLayerLevel(index, 1.0f - nv_m_zombatMusic);
	// GetMixLayerIndex("zombatMusic2Layer"); SetMixLayerLevel(index, 1.0f - nv_m_zombatMusic2);
	// GetMixLayerIndex("zombatMusic3Layer"); SetMixLayerLevel(index, 1.0f - nv_m_zombatMusic3);
	// GetMixLayerIndex("adrenalineLayer"); SetMixLayerLevel(index, m_fNVAdrenaline);
	if ( nv_m_ambientVolume != m_lastAmbientVolume ) SetTrackVolume( "ambient", nv_m_ambientVolume );
	m_lastAmbientVolume = nv_m_ambientVolume;
}

static void MusicDebugColor( con_nprint_t &print, bool engaged, bool viewOwner )
{
	print.time_to_live = 2.0f;
	print.fixed_width_font = true;
	if ( engaged ) { print.color[0] = 1.0f; print.color[1] = 0.1f; print.color[2] = 0.1f; }
	else if ( viewOwner ) { print.color[0] = 1.0f; print.color[1] = 0.8f; print.color[2] = 0.1f; }
	else { print.color[0] = 0.1f; print.color[1] = 0.9f; print.color[2] = 1.0f; }
}

void Music::DrawDebug( bool viewOwner, int playerIndex ) const
{
	con_nprint_t print;
	print.index = 10 * ( playerIndex > 0 ? playerIndex - 1 : playerIndex );
	MusicDebugColor( print, m_engaged, viewOwner );
	engine->Con_NXPrintf( &print, "Player: %s", m_me->GetPlayerName() );
	++print.index;
	for ( int i = 0; i < m_tracks.Count(); ++i )
	{
		MusicMasterTrack *track = m_tracks[i];
		engine->Con_NXPrintf( &print, "MasterMultiTrack:: %s", m_tracks.GetElementName( i ) );
		++print.index;
		if ( track->m_active )
		{
			con_nprint_t clipPrint;
			clipPrint.index = print.index;
			MusicDebugColor( clipPrint, track->m_active->m_engaged, viewOwner );
			engine->Con_NXPrintf( &clipPrint, "MusicMasterClip: %s", track->m_active->m_event );
			print.index = clipPrint.index + 1;
		}
	}
}

void Music::DebugPrint() const
{
	for ( int i = 0; i < m_tracks.Count(); ++i )
	{
		MusicDebug( "TRACK: %s\n", m_tracks.GetElementName( i ) );
		m_tracks[i]->DebugPrint();
	}
}

void Music::Update()
{
	if ( m_firstUpdate ) { m_firstUpdate = false; return; }
	if ( !m_me ) return;
	CTerrorPlayer *viewPlayer = MusicViewPlayer();
	if ( m_me == viewPlayer ) UpdateMix();
	if ( music_master_debug.GetBool() ) DrawDebug( m_me == viewPlayer, m_me->entindex() );
	if ( m_gameOver ) m_checkpointStarted = false;
	if ( !m_checkpointStarted && nv_m_inCheckpoint > 0 )
	{
		m_gameOver = false;
		switch ( nv_m_inCheckpoint )
		{
		case 1: PlayCommand( "Checkpoint_0", -1.0f, 0, NULL ); break;
		case 2: case 3: PlayCommand( "Checkpoint_1", -1.0f, 0, NULL ); break;
		case 4:
			PlayCommand( "Event.MissionStart_Survival", -1.0f, 0, NULL );
			PlayCommand( "Event.LeavingSafety_Survival", -1.0f, 0, NULL );
			break;
		}
		m_checkpointStarted = true;
	}
	UpdateTracks();
}

static void __MsgFunc_MusicCmd( bf_read &message )
{
	const int command = message.ReadUBitLong( 3 );
	const int playerIndex = message.ReadUBitLong( 6 );
	char event[260] = "", general[128] = "", arg1[128] = "", arg2[128] = "";
	float duration = -1.0f;
	int emitter = 0;
	Vector position( 0, 0, 0 );
	if ( command == MUSIC_GENERAL )
	{
		message.ReadString( general, sizeof( general ) );
		message.ReadString( arg1, sizeof( arg1 ) );
		message.ReadString( arg2, sizeof( arg2 ) );
		emitter = message.ReadUBitLong( 11 ) - 1;
	}
	else if ( command <= MUSIC_STOP_TRACK )
	{
		if ( message.ReadOneBit() )
		{
			const char *name = GetMusicSoundNameForHash( message.ReadUBitLong( 32 ) );
			Q_strncpy( event, name ? name : "", sizeof( event ) );
		}
		else message.ReadString( event, sizeof( event ) );
		duration = message.ReadFloat();
		if ( command == MUSIC_PLAY || command == MUSIC_PLAY_POSITION ) emitter = message.ReadUBitLong( 11 ) - 1;
		if ( command == MUSIC_PLAY_POSITION )
		{
			for ( int axis = 0; axis < 3; ++axis )
				position[axis] = message.ReadBitCoordMP( true, false );
		}
	}
	CTerrorPlayer *player = ToTerrorPlayer( cl_entitylist->GetEnt( playerIndex ) );
	if ( !player ) { MusicDebug( "ERROR: NO ENTITY: %s\n", general ); return; }
	MusicDebug( "MusicTransport: %s : %i : %i : %s : (%f, %f, %f)\n",
		player->GetPlayerName(), command, playerIndex, event, position.x, position.y, position.z );
	Music *music = player->GetMusic();
	switch ( command )
	{
	case MUSIC_PLAY: music->PlayCommand( event, duration, emitter, NULL ); break;
	case MUSIC_PLAY_POSITION: music->PlayCommand( event, duration, emitter, &position ); break;
	case MUSIC_STOP: music->StopCommand( event, duration, false ); break;
	case MUSIC_STOP_TRACK: music->StopCommand( event, duration, true ); break;
	case MUSIC_GENERAL: music->ReceiveGeneralCommand( general, arg1, arg2, emitter ); break;
	}
}

void HookMusicUserMessage()
{
	usermessages->HookMessage( "MusicCmd", __MsgFunc_MusicCmd );
}

void UpdateMusic()
{
	CTerrorPlayer *players[MAX_PLAYERS];
	bool primary[MAX_PLAYERS], secondary[MAX_PLAYERS];
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( cl_entitylist->GetEnt( i ) );
		if ( player && g_PR && g_PR->IsConnected( i ) ) players[count++] = player;
	}
	CTerrorPlayer *local0 = MusicLocalPlayer( 0 );
	CTerrorPlayer *local1 = MusicLocalPlayer( 1 );
	const int primarySlot = local0 && local1 && local0->GetTeamNumber() != 2 && local1->GetTeamNumber() == 2 ? 1 : 0;
	CTerrorPlayer *primaryListener = MusicListener( primarySlot );
	CTerrorPlayer *secondaryListener = MusicListener( 1 - primarySlot );
	for ( int i = 0; i < count; ++i )
	{
		primary[i] = players[i] == primaryListener;
		secondary[i] = players[i] == secondaryListener;
	}
	// Native globally disengages BOTH selectors before ANY re-engage/update.
	for ( int i = 0; i < count; ++i )
	{
		if ( !primary[i] ) players[i]->GetMusic()->SetEngaged( false );
		if ( !secondary[i] ) players[i]->GetMusic()->SetSplitEngaged( false );
	}
	for ( int i = 0; i < count; ++i )
	{
		if ( primary[i] ) players[i]->GetMusic()->SetEngaged( true );
		if ( secondary[i] ) players[i]->GetMusic()->SetSplitEngaged( true );
		players[i]->GetMusic()->Update();
	}
}

CON_COMMAND_F( music_dynamic_play, "" , 0x00000008)
{
	MusicLocalPlayer()->GetMusic()->PlayCommand( args.ArgC() > 1 ? args[1] : "", -1.0f, 0, NULL );
}

CON_COMMAND_F( music_dynamic_stop_playing, "" , 0x00000008)
{
	MusicLocalPlayer()->GetMusic()->StopCommand( args.ArgC() > 1 ? args[1] : "", -1.0f, false );
}

CON_COMMAND_F( music_print, "client" , 0x00000008)
{
	CTerrorPlayer *player = MusicViewPlayer();
	if ( player ) player->GetMusic()->DebugPrint();
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
