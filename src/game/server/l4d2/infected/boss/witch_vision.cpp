//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/witch_vision.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/player/terrorplayer.h"

#include "tier0/memdbgon.h"

extern bool IsInCommentaryMode();

WitchVision::WitchVision( Infected *me ) : ZombieBotVision( me )
{
}

// S:0x41C220. Burning changes which teams are considered, not sight geometry.
bool WitchVision::IsIgnored( CBaseEntity *subject ) const
{
	if ( IsInCommentaryMode() && subject->IsPlayer() && !ToBasePlayer( subject )->IsFakeClient() )
		return true;

	if ( !GetBot()->GetEntity()->IsOnFire() )
	{
		CBaseCombatCharacter *combat = subject->MyCombatCharacterPointer();
		if ( !combat || combat->GetTeamNumber() != TEAM_SURVIVOR )
			return true;
	}

	CTerrorPlayer *player = ToTerrorPlayer( subject );
	return player && player->IsIncapacitated();
}

// S:0x41C210; this is distinct from disturbance and flashlight ranges.
float WitchVision::GetMaxVisionRange() const
{
	return 200.0f;
}
