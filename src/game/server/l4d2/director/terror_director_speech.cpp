//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/entities/info_remarkable.h"
#include "ai_responsesystem.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "entitylist.h"
#include "tier0/memdbgon.h"

void CDirector::ResetRoundSpeech()
{
	// Native265450, reached from2659A0 AFTER player/scrimmage reset and BEFORE
	// the initial intensity RandomFloat. Same entity-owned response contexts.
	inputdata_t input;
	edict_t *worldEdict = INDEXENT( 0 );
	if ( worldEdict && !worldEdict->IsFree() )
	{
		CBaseEntity *world = CBaseEntity::Instance( worldEdict );
		if ( world ) world->InputClearContext( input ); // Actual1005A590, not a fabricated world setter.
	}
	for ( CBaseEntity *orator = gEntList.FindEntityByClassname( NULL, "func_orator" ); orator;
		orator = gEntList.FindEntityByClassname( orator, "func_orator" ) )
		orator->InputClearContext( input );
	const CUtlLinkedList< CInfoRemarkable *, unsigned short > &entities = GetTerrorRemarkableEntities();
	FOR_EACH_LL( entities, i ) if ( entities[i] ) entities[i]->ResetRemarkCount();
	ResetAllResponseSystemGroups(); // Native016260: reverse actual instanced dictionary, THEN sole default groups.
	ResponseQueueManager().Clear(); // Native029B80->029930, SAME actual owned pending queue.
}
