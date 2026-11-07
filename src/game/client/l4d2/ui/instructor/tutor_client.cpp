//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_basetempentity.h"
#include "l4d2/ui/instructor/tutor_lesson.h"
#include "cdll_client_int.h"
#include <math.h>
#include <emmintrin.h>

#include "tier0/memdbgon.h"

extern "C" float L4D2TutorFramingFOV( float distance );
#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// C:0x6E9768. This independent, older tutor is not the scripted game instructor.
// These names are sound script names, not localization tokens or achievement IDs.
static const char *const s_tutorSounds[] =
{
	"",
	"Tutor.SEE_BOOMER",
	"Tutor.SEE_SMOKER",
	"Tutor.SEE_HUNTER",
	"Tutor.SEE_WITCH",
	"Tutor.SEE_TANK",
	"Tutor.SEE_WANDERERING_INFECTED",
	"Tutor.SEE_ALERT_INFECTED",
	"Tutor.SEE_ENRAGED_INFECTED",
	"Tutor.SEE_MOB",
	"Tutor.SEE_AMBUSH_OPPORTUNITY",
	"Tutor.SEE_UNLOCKED_DOOR",
	"Tutor.EXPLAIN_LOCKED_DOOR",
	"Tutor.EXPLAIN_EQUIPMENT_NEEDS",
	"Tutor.SEE_COLLECTIBLE_ITEM",
	"Tutor.SEE_EQUIPMENT_TABLE",
	"Tutor.SEE_PAIN_PILLS",
	"Tutor.SEE_FIRST_AID_KIT",
	"Tutor.EXPLAIN_FLASHLIGHT",
	"Tutor.EXPLAIN_RELOADING",
	"Tutor.EXPLAIN_SHOVE_ATTACK",
	"Tutor.EXPLAIN_QUICKSWITCH",
	"Tutor.EXPLAIN_ZOOM",
	"Tutor.EXPLAIN_MOLOTOV",
	"Tutor.EXPLAIN_PIPE_BOMB",
	"Tutor.EXPLAIN_PAIN_PILLS",
	"Tutor.EXPLAIN_PAIN_PILLS_WEAR_OFF",
	"Tutor.EXPLAIN_FIRST_AID_KIT",
	"Tutor.EXPLAIN_INCAPACITATION",
	"Tutor.EXPLAIN_LEDGE_HANG",
	"Tutor.EXPLAIN_WAITING_FOR_RESCUE",
	"Tutor.EXPLAIN_HURT",
	"Tutor.EXPLAIN_HURT_USE_FIRST_AID_KIT",
	"Tutor.EXPLAIN_HURT_USE_PAIN_PILLS",
	"Tutor.EXPLAIN_GIFT_FROM_FRIEND",
	"Tutor.EXPLAIN_FRIEND_NEEDS_YOUR_HEALING_ITEM",
	"Tutor.SEE_FRIEND_IN_RESCUE_CLOSET",
	"Tutor.SEE_INCAPACITATED_FRIEND",
	"Tutor.SEE_LEDGE_HANGING_FRIEND",
	"Tutor.SEE_POUNCED_FRIEND",
	"Tutor.SEE_CHOKING_FRIEND",
	"Tutor.SEE_CHECKPOINT_DOOR",
	"Tutor.SEE_SMOKER_CLOUD",
	"Tutor.EXPLAIN_VOCALIZATION_ROSETTA",
	"Tutor.EXPLAIN_VOMITED_ON",
	"Tutor.EXPLAIN_EXPLODED_ON",
	"Tutor.EXPLAIN_GRABBED_BY_TONGUE",
	"Tutor.EXPLAIN_CHOKED_BY_TONGUE",
	"Tutor.EXPLAIN_LUNGED_INTO",
	"Tutor.EXPLAIN_POUNCED_ON",
	"Tutor.EXPLAIN_THIRD_STRIKE",
	"Tutor.EXPLAIN_LIMPING",
	"Tutor.EXPLAIN_LIMPING_CRITICAL",
	"Tutor.SEE_WITCH_ANGRY",
	"Tutor.EXPLAIN_WITCH_ATTACK",
	"Tutor.EXPLAIN_SLUGGED_BY_TANK",
	"Tutor.SEE_TANK_START_TO_THROW_ROCK",
	"Tutor.EXPLAIN_HIT_BY_TANK_ROCK",
	"Tutor.EXPLAIN_FINALE",
	"Tutor.SEE_FINALE_RADIO",
	"Tutor.SEE_MOUNTED_MACHINE_GUN",
	"Tutor.EXPLAIN_RESCUE_VEHICLE_APPROACHING",
	"Tutor.EXPLAIN_RESCUE_VEHICLE_WAITING",
	"Tutor.EXPLAIN_ATTACKED_FROM_BEHIND",
	"Tutor.EXPLAIN_DEATH",
	"Tutor.EXPLAIN_WIN",
	"Tutor.EXPLAIN_LOSE",
	"Tutor.EXPLAIN_REPORT_SCREEN",
	"Tutor.EXPLAIN_AWARD_EARNED",
	"Tutor.EXPLAIN_DEMERIT_EARNED",
	"Tutor.EXPLAIN_ACHIEVEMENT_EARNED",
	"Tutor.SEE_BREAKABLE_ITEM",
	"Tutor.SEE_BETTER_WEAPON",
	"Tutor.EXPLAIN_ACCURACY_BAD_WHILE_RUNNING",
	"Tutor.EXPLAIN_HIDING",
	"Tutor.EXPLAIN_VOTING"
};
COMPILE_TIME_ASSERT( ARRAYSIZE( s_tutorSounds ) == NUM_TUTOR_LESSONS );

// C:0x28A550/0x28A5A0/0x28A6C0. Like the native singleton, this owner is not
// reset by map changes, target death, disconnect, or game-instructor settings.
class TutorClient
{
public:
	TutorClient() : m_lesson( TUTOR_LESSON_NONE ), m_startTime( 0.0f ),
		m_endTime( 0.0f ), m_previousFOV( 0.0f ), m_lessonFOV( 0.0f )
	{
	}

	virtual ~TutorClient()
	{
	}

	void StartLesson( int lesson, C_BaseEntity *subject )
	{
		if ( m_lesson != TUTOR_LESSON_NONE )
		{
			DevMsg( "Tutor lesson collision\n" );
			return;
		}

		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
		if ( !player )
			return;

		player->EmitSound( "Tutor.StartLesson" );
		if ( subject )
		{
			subject->AddEffects( EF_BRIGHTLIGHT );
			m_previousFOV = player->GetFOV();
			const Vector delta = subject->GetAbsOrigin() - player->GetAbsOrigin();
			// Native SSE squared-distance order and x87 angle expression.
			const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
			m_lessonFOV = L4D2TutorFramingFOV( distance );
			if ( m_lessonFOV > m_previousFOV )
				m_lessonFOV = m_previousFOV;
		}

		m_lesson = lesson;
		m_subject = subject;
		engine->ClientCmd( "unpause; pause\n" );
		m_startTime = gpGlobals->realtime + 1.5f;
		m_endTime = -1.0f;
	}

	void Update()
	{
		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
		if ( !player || m_lesson == TUTOR_LESSON_NONE )
			return;

		if ( m_endTime < 0.0f )
		{
			if ( gpGlobals->realtime > m_startTime )
			{
				// Main standing caller-local output rule: native missing Tutor.*
				// scripts leave duration unwritten. Initialize only that local to 0;
				// do not change EmitSound, assets, pause/FOV/effects or strict deadlines.
				float duration = 0.0f;
				player->EmitSound( s_tutorSounds[m_lesson], 0.0f, &duration );
				m_endTime = ( m_startTime + duration ) + 1.0f;
			}
		}
		else if ( gpGlobals->realtime > m_endTime )
		{
			if ( m_subject.Get() )
				m_subject->RemoveEffects( EF_BRIGHTLIGHT );
			engine->ClientCmd( "unpause\n" );
			m_lesson = TUTOR_LESSON_NONE;
			// Native restores the DEFAULT, not the saved previous FOV. A
			// different zoom owner can reject both requests through SetFOV.
			player->SetFOV( player, player->GetDefaultFOV(), 0.2f, 0 );
			player->EmitSound( "Tutor.EndLesson" );
		}
		else
		{
			player->SetFOV( player, _mm_cvttss_si32( _mm_set_ss( m_lessonFOV ) ), 0.1f, 0 );
		}
	}

private:
	int m_lesson;
	CHandle< C_BaseEntity > m_subject;
	float m_startTime;
	float m_endTime;
	float m_previousFOV;
	float m_lessonFOV;
};

static TutorClient &GetTutorClient()
{
	static TutorClient s_tutor;
	return s_tutor;
}

class C_TutorLesson : public C_BaseTempEntity
{
public:
	DECLARE_CLASS( C_TutorLesson, C_BaseTempEntity );
	DECLARE_CLIENTCLASS();

	C_TutorLesson() : m_lesson( TUTOR_LESSON_NONE )
	{
	}

	virtual void PostDataUpdate( DataUpdateType_t updateType ) OVERRIDE
	{
		// C:0x28A910. DATA_UPDATE_CHANGED is deliberately ignored.
		if ( updateType == DATA_UPDATE_CREATED )
			GetTutorClient().StartLesson( m_lesson, m_subject.Get() );
	}

	int m_lesson;
	CHandle< C_BaseEntity > m_subject;
};

IMPLEMENT_CLIENTCLASS_EVENT( C_TutorLesson, DT_TETutorLesson, CTutorLesson )
BEGIN_RECV_TABLE_NOBASE( C_TutorLesson, DT_TETutorLesson )
	RecvPropInt( RECVINFO( m_lesson ) ),
	RecvPropEHandle( RECVINFO( m_subject ) ),
END_RECV_TABLE()

// Native274010 invokes this actual tutor from the sole mode Update owner.
void UpdateTutorClient()
{
	GetTutorClient().Update();
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
