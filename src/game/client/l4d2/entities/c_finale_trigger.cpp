//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/c_finale_trigger.h"
#include "l4d2/player/c_terrorplayer.h"
#include "vgui/ILocalize.h"
#include "tier3/tier3.h"
#include <ctype.h>
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_FinaleTrigger, DT_FinaleTrigger, CFinaleTrigger )
	RecvPropInt( RECVINFO( m_triggerState ) ),
	RecvPropBool( RECVINFO( m_bDisabled ) ),
END_RECV_TABLE()

int C_FinaleTrigger::ObjectCaps()
{
	int caps = BaseClass::ObjectCaps();
	return !m_bDisabled && ( m_triggerState == 0 || m_triggerState == 2 ) ? caps | 0x810 : caps;
}
const wchar_t *C_FinaleTrigger::GetUseString( C_BaseEntity *user )
{
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() || m_triggerState == 4 ) return L"";
	// C:0B4D60's actual four-slot bound-key label formatting. The disabled
	// bit does not participate in this native prompt predicate.
	static wchar_t output[4][256];
	static int next = 0;
	next = ( next + 1 ) & 3;
	const char *binding = engine->Key_LookupBinding( "+use" );
	if ( !binding ) binding = "<NOT BOUND>";
	char key[64];
	Q_strncpy( key, binding, sizeof( key ) );
	for ( char *p = key; *p; ++p ) *p = (char)toupper( *p );
	wchar_t wideKey[64];
	g_pVGuiLocalize->ConvertANSIToUnicode( key, wideKey, sizeof( wideKey ) );
	g_pVGuiLocalize->ConstructString( output[next], sizeof( output[next] ), g_pVGuiLocalize->Find( "L4D_Use_Radio" ), 1, wideKey );
	return output[next];
}
