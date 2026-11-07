//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_FLEX_EXPRESSER_H
#define L4D2_FLEX_EXPRESSER_H
#ifdef _WIN32
#pragma once
#endif
#include "baseflex.h"
#include "ai_speech.h"

// Declaration lifted from the existing baseflex.cpp owner. Its implementations,
// data description and generic cycler factory remain in that existing TU.
class CFlexCycler : public CBaseFlex
{
private:
	DECLARE_CLASS( CFlexCycler, CBaseFlex );
public:
	DECLARE_DATADESC();
	CFlexCycler() { m_iszSentence = NULL_STRING; m_sentence = 0; }
	void GenericCyclerSpawn( char *model, Vector mins, Vector maxs );
	virtual int ObjectCaps() { return BaseClass::ObjectCaps() | FCAP_IMPULSE_USE; }
	int OnTakeDamage( const CTakeDamageInfo &info );
	void Spawn();
	void Think();
	virtual void ProcessSceneEvents();
	virtual bool IsAlive() { return false; }
	float m_flextime;
	LocalFlexController_t m_flexnum;
	float m_flextarget[64];
	float m_blinktime;
	float m_looktime;
	Vector m_lookTarget;
	float m_speaktime;
	int m_istalking;
	int m_phoneme;
	string_t m_iszSentence;
	int m_sentence;
	void SetFlexTarget( LocalFlexController_t flexnum );
	LocalFlexController_t LookupFlex( const char *target );
};
class CFlexExpresserShim : public CFlexCycler
{
public:
	DECLARE_CLASS( CFlexExpresserShim, CFlexCycler );
	virtual CAI_Expresser *GetExpresser() = 0;
};
class CFlexExpresser : public CAI_ExpresserHost< CFlexExpresserShim >
{
public:
	DECLARE_CLASS( CFlexExpresser, CAI_ExpresserHost< CFlexExpresserShim > );
	DECLARE_DATADESC();
	CFlexExpresser();
	virtual ~CFlexExpresser();
	virtual void Spawn() OVERRIDE;
	virtual void Think() OVERRIDE;
	virtual bool IsAlive() OVERRIDE { return CBaseFlex::IsAlive(); }
	virtual int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE { return 0; } // Native297480.
	virtual void ProcessSceneEvents() OVERRIDE {} // Native071D90, not SDK cycler random-flex processing.
	virtual CAI_Expresser *GetExpresser() OVERRIDE { return m_pExpresser; }
	virtual IResponseSystem *GetResponseSystem() OVERRIDE;
	CAI_Expresser *CreateExpresser();
	virtual bool SpeakWithCriteria( AIConcept_t concept, AI_CriteriaSet &criteria,
		char *chosen = NULL, size_t size = 0, IRecipientFilter *filter = NULL );
	void InputSpeakResponseConcept( inputdata_t &inputdata );
	float GetThenAnyMaxDistance() const { return m_flThenAnyMaxDist; }
private:
	CAI_ExpresserWithFollowup *m_pExpresser;
	float m_flThenAnyMaxDist;
};
// Native029F8A: genuine orator range overrides global radius. NaN/nonpositive
// means FLT_MAX, NOT a zero radius or finite clamp. False preserves caller radius.
bool GetFlexExpresserThenAnyRangeSqr( CBaseEntity *issuer, float *rangeSqr );
#endif
