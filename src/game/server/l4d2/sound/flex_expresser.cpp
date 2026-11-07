//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/flex_expresser.h"
#include "l4d2/player/terrorplayer.h"
#include <float.h>
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( func_orator, CFlexExpresser );

BEGIN_DATADESC( CFlexExpresser )
	DEFINE_INPUTFUNC( FIELD_STRING, "SpeakResponseConcept", InputSpeakResponseConcept ),
	DEFINE_KEYFIELD( m_flThenAnyMaxDist, FIELD_FLOAT, "maxThenAnyDispatchDist" ),
END_DATADESC()


CFlexExpresser::CFlexExpresser() : m_pExpresser( NULL ), m_flThenAnyMaxDist( 0.0f ) {}
CFlexExpresser::~CFlexExpresser()
{
	delete m_pExpresser;
	m_pExpresser = NULL;
}
IResponseSystem *CFlexExpresser::GetResponseSystem()
{
	return g_pTerrorPlayerResponseSystem; // S297490; CSurvivorRescue inherits this same owner.
}
CAI_Expresser *CFlexExpresser::CreateExpresser()
{
	// Native2975B0 allocates here, not in the entity constructor/Spawn shim.
	// A second explicit Spawn overwrites this pointer natively; do not add an
	// independent expresser owner or silently change that lifetime contract.
	m_pExpresser = new CAI_ExpresserWithFollowup( this );
	if ( m_pExpresser ) m_pExpresser->Connect( this );
	return m_pExpresser;
}
void CFlexExpresser::Spawn()
{
	const char *model = STRING( GetModelName() );
	if ( model && model[0] )
	{
		PrecacheModel( model );
		SetModel( model );
	}
	Precache();
	if ( HasSpawnFlags( 1 ) ) SetSolid( SOLID_NONE );
	else
	{
		SetSolid( SOLID_BBOX );
		AddSolidFlags( FSOLID_NOT_STANDABLE );
	}
	SetMoveType( MOVETYPE_NONE );
	m_takedamage = DAMAGE_NO;
	SetHealth( 80000 );
	SetPlaybackRate( 1.0f );
	m_flGroundSpeed = 0.0f;
	SetNextThink( gpGlobals->curtime + 1.0f );
	ResetSequenceInfo();
	InitBoneControllers();
	CreateExpresser();
}
void CFlexExpresser::Think()
{
	SetNextThink( gpGlobals->curtime + 0.1f );
	StudioFrameAdvance();
}
void CFlexExpresser::InputSpeakResponseConcept( inputdata_t &inputdata )
{
	const inputdata_t snapshot = inputdata;
	HandleInputSpeakResponseConcept( this, GetExpresser(), snapshot );
}
bool CFlexExpresser::SpeakWithCriteria( AIConcept_t concept, AI_CriteriaSet &criteria,
	char *chosen, size_t size, IRecipientFilter *filter )
{
	// Native297960 mutates the caller's criteria before the derived/find path.
	m_pExpresser->PrepareSpeechCriteria( concept, criteria, NULL );
	return m_pExpresser->SpeakWithCriteria( concept, criteria, chosen, size, filter );
}
bool GetFlexExpresserThenAnyRangeSqr( CBaseEntity *issuer, float *rangeSqr )
{
	if ( !issuer || !FClassnameIs( issuer, "func_orator" ) ) return false;
	CFlexExpresser *orator = dynamic_cast< CFlexExpresser * >( issuer );
	if ( !orator ) return false; // Main-approved minimal skip of classname-spoof type confusion.
	const float range = orator->GetThenAnyMaxDistance();
	*rangeSqr = range > 0.0f ? range * range : FLT_MAX; // Native COMISS/JBE includes NaN.
	return true;
}