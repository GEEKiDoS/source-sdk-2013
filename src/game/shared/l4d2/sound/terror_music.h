//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_MUSIC_H
#define TERROR_MUSIC_H
#ifdef _WIN32
#pragma once
#endif

#include "networkvar.h"
#include "tier1/utlvector.h"
#include "tier1/utlstring.h"
#include "util_shared.h"
#ifdef CLIENT_DLL
#include "tier1/utldict.h"
#endif

#ifdef CLIENT_DLL
#define CTerrorPlayer C_TerrorPlayer
#endif

class CTerrorPlayer;
class KeyValues;
class CSoundPatch;
class MusicMasterTrack;
class MusicMasterClip;
#ifndef CLIENT_DLL
class Infected;
#endif

// MusicCmd is a bit-packed message, not an EmitSound replacement.
enum MusicCommand
{
	MUSIC_PLAY = 0,
	MUSIC_PLAY_POSITION = 1,
	MUSIC_STOP = 2,
	MUSIC_STOP_TRACK = 3,
	MUSIC_GENERAL = 4
};

KeyValues *GetMusicScript( const char *name );
KeyValues *GetMusicScripts();
unsigned int GetMusicSoundNameHash( const char *name );
const char *GetMusicSoundNameForHash( unsigned int hash );
void InvalidateMusicSoundHashes();
void PrecacheMusicScripts();
bool MusicScriptHasParameter( const char *name, const char *parameter );
bool IsMusicSoundLooping( const char *wave );

class Music
{
public:
	DECLARE_CLASS_NOBASE( Music );
	DECLARE_EMBEDDED_NETWORKVAR();
	Music();
	~Music();
	void SetOwner( CTerrorPlayer *player );
	void Reset();
	void Update();
	void Play( const char *event, int emitEntity = 0, float duration = -1.0f, bool otherSurvivors = false, bool spatial = false );
	void Stop( const char *event, float fade = -1.0f, bool track = false );
	void GeneralCommand( const char *command, const char *arg1 = NULL, const char *arg2 = NULL, int entity = -10 );

#ifndef CLIENT_DLL
	void ResetAfterRescue();
	void ClearPlayerDSP();
	void PostponeSpecialProximity( int zombieClass );
	void OnDamageDealt( float damage );
	void OnWeaponFired();
	void OnMobApproachingNear( int count );
	void OnMobSpawn( int count, int direction );
	void OnAmbientMob();
	void AddWanderingWitch( Infected *witch );
	void RemoveWanderingWitch( Infected *witch );
	bool IsWitchChoirPlaying() const { return m_witchChoirPlaying; }
	bool IsInCombat() const;
	void OnMissionStart();
	void OnLeaveMissionStartArea();
	void OnEnterSurvival();
	void OnSurvivalStart();
	void OnCheckpointReached();
	void OnFinaleCheckpointReached();
	void OnEnterCheckpoint();
	void OnLeaveCheckpoint();
	void OnMissionWon();
	void OnMissionWonDone();
	void OnMissionLost();
	void OnFinaleFinalBattle();
	void OnPlayerRestored( CTerrorPlayer *oldPlayer );
	void OnKilled( bool wasBleedingOut );
	void OnDeathEnded();
	void OnDown();
	void OnDownEnded();
	void OnDownAndBeaten();
	void OnDownAndBeatenEnded();
	void OnBleedingOut();
	void OnBleedingOutEnded();
	void OnLedgeHangTwoHands();
	void OnLedgeHangOneHand();
	void OnLedgeHangFingers();
	void OnLedgeHangBleedingOut();
	void OnSavedFromLedgeHang();
	void OnDroppedFromLedgeHang();
	void OnFellToDeathFromLedgeHang();
	void OnVomitedUpon();
	void OnITExpired();
	void OnPulledByTongue();
	void OnChoked();
	void OnReleasedByTongue();
	void OnPounced();
	void OnPounceEnded();
	void OnJockeyRide();
	void OnJockeyRideEnded();
	void OnStruck();
	void OnStruckEnded();
	void OnPummeled();
	void OnPummeledEnd();
	void OnSpitterSpit( int entity );
	void OnSpitterSpitEnded();
	void OnSpitterSpitAttack();
	void OnSpitterSpitAttackEnded();
	void OnAdrenalineUsed();
	void OnAdrenalineExpired();
	void OnBossApproaching();
	void OnBossDefeated( int zombieClass );
	void OnLandmarkRevealed( const char *landmark );
	void OnSmallAreaRevealed();
	void OnLargeAreaRevealed();
	void OnApproachingScavengeRoundWin();
	void OnScavengeRoundStart();
	void OnScavengeRoundHalftime();
	void OnScavengeRoundEnd();
	void OnScavengeOvertimeStart();
	void OnScavengeOvertimeTimeAdded();
	void OnScavengeLevelChange( int level );
	void PlayCampaignEvent( const char *event );
	void SetMusicVariant( const char *variant ) { Q_strncpy( m_musicVariant, variant, sizeof( m_musicVariant ) ); }
#else
	void PlayCommand( const char *event, float duration, int emitEntity, const Vector *position );
	void StopCommand( const char *event, float fade, bool track );
	void ReceiveGeneralCommand( const char *command, const char *arg1, const char *arg2, int entity );
	void SetTrackVolume( const char *track, float volume );
	void SetEngaged( bool engaged );
	void SetSplitEngaged( bool engaged );
	bool HasEvent( const char *event ) const;
	void DebugPrint() const;
	void DrawDebug( bool viewOwner, int playerIndex ) const;
#endif

	// Names and quantization match original DT_Music. The original server does
	// not send nv_m_zombatMusic, although the original client declares it.
	CNetworkVar( float, nv_m_CIDamageDuck );
	CNetworkVar( float, nv_m_CIDamageMob );
	CNetworkVar( float, nv_m_zombatMusic2 );
	CNetworkVar( float, nv_m_zombatMusic3 );
	CNetworkVar( int, nv_m_inCheckpoint );
	CNetworkVar( float, nv_m_ambientVolume );
	CNetworkVar( float, nv_m_witchRage );
	CNetworkVar( float, m_fNVAdrenaline );
#ifdef CLIENT_DLL
	float nv_m_zombatMusic;
#endif
private:
	Music( const Music & );
	Music &operator=( const Music & );
	CTerrorPlayer *m_me;
#ifndef CLIENT_DLL
	bool IsL4D1() const;
	void PlayPersonalAndTeamHit( const char *event, const char *hit );
	void StopLedgeEvents( float fade );
	bool m_isPounced;
	bool m_isPummeled;
	bool m_isJockeyRidden;
	bool m_isTongued;
	bool m_isIT;
	bool m_stopCombatMusic;
	float m_damageDealt;
	float m_damageDealtSignal;
	CountdownTimer m_damageDealtDecayTimer;
	CountdownTimer m_updateTimer;
	CountdownTimer m_largeAreaRevealTimer;
	struct DynamicValue
	{
		float value;
		float previous;
		CountdownTimer timer;
		void Reset();
		float Increment( float amount, float increment, float decay );
		void Peak( float target, float decay );
		void Valley( float target, float decay );
	};
	struct ActorRange
	{
		Infected *actor;
		float range;
	};
	void ResetDynamicState();
	void UpdatePerception();
	void UpdateDynamicMusic();
	void UpdateDamageDealt();
	void SortActorRanges();
	void SiftActorHeap( int hole, int top, int count, const ActorRange &value );
	void UpdateSpecialProximity( int zombieClass, int entity, float range );
	void StopCombatTracks( bool includePassing );
	void MakeCampaignEvent( const char *event, char *out, int size ) const;
	void MakeVariantEvent( const char *event, char *out, int size ) const;
	DynamicValue m_gunfireFast;
	DynamicValue m_gunfireSlow;
	DynamicValue m_damageTaken;
	DynamicValue m_soloSuccess;
	DynamicValue m_mobbed;
	DynamicValue m_mobApproach;
	DynamicValue m_threat;
	DynamicValue m_action;
	DynamicValue m_calm;
	DynamicValue m_ambient;
	CUtlVector< ActorRange > m_actorRanges;
	CUtlVector< Infected * > m_attackingNear;
	CUtlVector< Infected * > m_calmNear;
	CUtlVector< EHANDLE > m_wanderingWitches;
	CountdownTimer m_specialAlertTimer[7];
	CountdownTimer m_witchChoirTimer;
	CountdownTimer m_choirDelay;
	CountdownTimer m_atmosphereCooldown;
	CountdownTimer m_dangerAtmosphereTimer;
	CountdownTimer m_safeAtmosphereTimer;
	CountdownTimer m_choirTimer;
	int m_totalInfected;
	int m_attackingCount;
	int m_attackingNearCount;
	int m_calmCount;
	int m_calmNearCount;
	int m_runningCount;
	int m_nearCount;
	int m_closeCount;
	int m_veryCloseCount;
	int m_pendingMobCount;
	int m_pendingChoirEntity;
	float m_attackingFraction;
	float m_calmFraction;
	float m_nearWeight;
	float m_closeWeight;
	float m_veryCloseWeight;
	float m_runningFraction;
	float m_closeSmoothed;
	float m_veryCloseSmoothed;
	float m_inCombatMusic;
	float m_lastMobbed;
	bool m_weaponFired;
	bool m_witchPresent;
	bool m_witchAttacking;
	bool m_witchBurning;
	bool m_witchChoirPlaying;
	bool m_bossMusicActive;
	bool m_hadTank;
	bool m_rephaseAfterMobSpawn;
	bool m_dangerAtmospherePlaying;
	bool m_dangerAtmospherePending;
	bool m_safeAtmospherePending;
	char m_musicVariant[128];
#else
	friend class MusicMasterClip;
	friend class MusicMasterTrack;
	void UpdateTracks();
	void UpdateMix();
	MusicMasterTrack *FindTrack( const char *name, bool create );
	MusicMasterTrack *FindTrack( const char *name ) const;
	bool AutoQueueSound( const char *event, float duration, int emitEntity, const Vector *position );
	bool IsTrackDucked( const char *track ) const;
	bool IsTrackBlocked( const char *track ) const;
	void StopTracksByList( const char *list );
	CUtlDict<MusicMasterTrack *, int> m_tracks;
	bool m_firstUpdate;
	bool m_checkpointStarted;
	bool m_gameOver;
	bool m_engaged;
	bool m_splitEngaged;
	float m_lastAmbientVolume;
	char m_checkpointEvent[64];
	char m_deathTrack[128];
#endif
};

#ifdef CLIENT_DLL
EXTERN_RECV_TABLE( DT_Music );
void HookMusicUserMessage();
void UpdateMusic();
#else
EXTERN_SEND_TABLE( DT_Music );
// S303EC0 (Tank) / S304330 (other specials), one connected-player fanout.
void NotifySpecialDeathMusic( int zombieClass );
#endif
#endif // TERROR_MUSIC_H
