//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/instructor/tutor.h"
#include "l4d2/ui/instructor/tutor_lesson.h"
#include "basetempentity.h"
#include "recipientfilter.h"

#include "tier0/memdbgon.h"

ConVar tutor( "tutor", "0", 0x00000006, "Enable the tutor to learn to play the game" );

// S:0x356210. This is a schema-backed temporary entity, not a byte usermessage.
class CTutorLesson : public CBaseTempEntity
{
public:
	DECLARE_CLASS( CTutorLesson, CBaseTempEntity );
	DECLARE_SERVERCLASS();

	CTutorLesson( const char *name ) : BaseClass( name )
	{
		m_lesson = TUTOR_LESSON_NONE;
		m_subject = NULL;
	}

	CNetworkVar( int, m_lesson );
	CNetworkHandle( CBaseEntity, m_subject );
};

IMPLEMENT_SERVERCLASS_ST_NOBASE( CTutorLesson, DT_TETutorLesson )
	SendPropInt( SENDINFO( m_lesson ), 7, SPROP_UNSIGNED ),
	SendPropEHandle( SENDINFO( m_subject ) ),
END_SEND_TABLE()

static CTutorLesson g_TutorLesson( "TutorLesson" );

// S:0x1B53E0. Reuse the SDK recipient/filter and observer machinery. Native
// tutor passes false: include IN_EYE and CHASE, never roaming/fixed observers.
class CSingleUserPlusObserversFilter : public CRecipientFilter
{
public:
	CSingleUserPlusObserversFilter( CBasePlayer *player, bool onlyInEye )
	{
		AddRecipient( player );
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *observer = UTIL_PlayerByIndex( i );
			if ( observer && observer->IsPlayer() && observer != player &&
				observer->GetObserverTarget() == player &&
				( observer->GetObserverMode() == OBS_MODE_IN_EYE ||
					( !onlyInEye && observer->GetObserverMode() == OBS_MODE_CHASE ) ) )
			{
				AddRecipient( observer );
			}
		}
	}
};

// S:0x3562B0. The target remains a serial-aware entity handle throughout the
// stock PlaybackTempEntity send/receive path, including invalid/deleted targets.
static void SendTutorLesson( CBasePlayer *player, TutorLesson_t lesson, CBaseEntity *subject )
{
	CSingleUserPlusObserversFilter filter( player, false );
	g_TutorLesson.m_lesson = lesson;
	g_TutorLesson.m_subject = subject;
	g_TutorLesson.Create( filter, 0.0f );
}

// Reconstructed semantic owner name; the native owner has exactly 76 once
// bytes. No savegame/config/achievement persistence or round reset is present.
class TutorServer
{
public:
	TutorServer()
	{
		Reset();
	}

	void Reset()
	{
		Q_memset( m_hasShown, 0, sizeof( m_hasShown ) );
	}

	bool TryShowLesson( TutorLesson_t lesson, CBaseEntity *subject )
	{
		// S:0x3560D0. Keep the original evaluation order and both real host
		// lookups. Dedicated servers do not consume a once flag or warning.
		if ( !tutor.GetBool() || !UTIL_GetListenServerHost() || m_hasShown[lesson] )
			return false;

		m_hasShown[lesson] = true;
		SendTutorLesson( UTIL_GetListenServerHost(), lesson, subject );
		return true;
	}

private:
	bool m_hasShown[NUM_TUTOR_LESSONS];
};

COMPILE_TIME_ASSERT( NUM_TUTOR_LESSONS == 76 );

static TutorServer &GetTutorServer()
{
	// S:0x3560A0: lazy, once per loaded DLL. The separate DLLInit reset is
	// S:0x356090, not a LevelInit/Spawn/round/reconnect callback.
	static TutorServer s_tutor;
	return s_tutor;
}

void ResetTutorLessons()
{
	GetTutorServer().Reset();
}

bool TryWarnSpecialTutorial( int zombieClass, CBaseCombatCharacter *target )
{
	TutorServer &owner = GetTutorServer();
	// S:0x356120: raw original class IDs, not survivor character IDs.
	switch ( zombieClass )
	{
	case 1: return owner.TryShowLesson( TUTOR_SEE_SMOKER, target );
	case 2: return owner.TryShowLesson( TUTOR_SEE_BOOMER, target );
	case 3: return owner.TryShowLesson( TUTOR_SEE_HUNTER, target );
	case 7: return owner.TryShowLesson( TUTOR_SEE_WITCH, target );
	case 8: return owner.TryShowLesson( TUTOR_SEE_TANK, target );
	default: return false;
	}
}
