//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef INFO_DIRECTOR_H
#define INFO_DIRECTOR_H
#ifdef _WIN32
#pragma once
#endif

#include "baseentity.h"
#include "entityoutput.h"
#include "tier1/fmtstr.h"

// Native factory 594330 -> allocator 266E60 (1380 bytes) -> ctor 262910,
// vtable 10667D1C, datamap 7A51B4: 18 inputs (all FIELD_INPUT) followed by 13
// COutputEvent outputs. Every input body ignores the entity and forwards to the
// Director, rules, scenario or response owners. Map-defined; one per map.
class CInfoDirector : public CPointEntity
{
public:
	DECLARE_CLASS( CInfoDirector, CPointEntity );
	DECLARE_DATADESC();

	// Output producers live with their real Director owners; this entity only
	// owns the storage and fires through the accessor its producer calls.
	COutputEvent m_OnGameplayStart;                 // 1068, fired by S:262AC0
	COutputEvent m_OnPanicEventFinished;            // 1092
	COutputEvent m_OnCustomPanicStageFinished;      // 1116, fired by S:25CCB0 before the stage counter increment
	COutputEvent m_OnTeamScored;                    // 1140
	COutputEvent m_OnScavengeRoundStart;            // 1164
	COutputEvent m_OnScavengeOvertimeStart;         // 1188
	COutputEvent m_OnScavengeOvertimeCancel;        // 1212
	COutputEvent m_OnScavengeTimerExpired;          // 1236
	COutputEvent m_OnScavengeIntensityChanged;      // 1260
	COutputEvent m_OnUserDefinedScriptEvent[4];     // 1284/1308/1332/1356; datamap names m_OnOnUserDefinedScriptEvent[0..3]

private:
	void InputPanicEvent( inputdata_t &inputdata );                  // S:25E2D0
	void InputScriptedPanicEvent( inputdata_t &inputdata );          // S:26ED10 (string)
	void InputForcePanicEvent( inputdata_t &inputdata );             // S:25E320
	void InputEndCustomScriptedStage( inputdata_t &inputdata );      // S:25CB90
	void InputForceSurvivorPositions( inputdata_t &inputdata );      // S:270A00
	void InputReleaseSurvivorPositions( inputdata_t &inputdata );    // S:270A40
	void InputRefreshInitialSpawnPositions( inputdata_t &inputdata ); // S:270A90
	void InputStartIntro( inputdata_t &inputdata );                  // S:25E370
	void InputFinishIntro( inputdata_t &inputdata );                 // S:266560
	void InputFireConceptToAny( inputdata_t &inputdata );            // S:25E3D0 (string)
	void InputIncrementTeamScore( inputdata_t &inputdata );          // S:25E440 (integer)
	void InputBeginScript( inputdata_t &inputdata );                 // S:26EDA0 (string)
	void InputEndScript( inputdata_t &inputdata );                   // S:26AB90
	void InputOpenJournalForWrite( inputdata_t &inputdata );         // S:2665C0 (string), input "CreateNewJournal"
	void InputWriteToJournal( inputdata_t &inputdata );              // S:2725B0 (string)
	void InputExecuteJournalCategory( inputdata_t &inputdata );      // S:266600 (string), input "ExecuteJournal"
	void InputEnableTankFrustration( inputdata_t &inputdata );       // S:25CBB0
	void InputDisableTankFrustration( inputdata_t &inputdata );      // S:25CBC0
};

// Director-owned in-memory map-input journal at native Director+1288 (0x508).
// Distinct from the scripted-mode file journal at scripted context+376.
// Records are 804 bytes natively: three CFmtStrN<256> (category, target, input),
// each 268 bytes with the quiet-truncation flag set. Native fills their buffers with
// an unbounded strcpy (S:4F72A0), bypassing the class API.
class CDirectorMapJournal
{
public:
	typedef CFmtStrN< 256, true > JournalText_t;

	// Native ctor 27183E-27187 zeroes the record vector and empties the current
	// category (+1308); Director reset and level shutdown never clear either.
	CDirectorMapJournal() {}

	void OpenForWrite( const char *category );   // S:2643C0
	void Write( const char *text );              // S:271000
	void Execute( const char *category );        // S:264540

private:
	struct Record_t
	{
		JournalText_t m_category;
		JournalText_t m_target;
		JournalText_t m_input;
	};
	CUtlVector< Record_t > m_records;      // Director +1288..1307
	JournalText_t m_currentCategory;       // Director +1308..1575
};

#endif // INFO_DIRECTOR_H
