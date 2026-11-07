//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/terror_message_text.h"
#include "recipientfilter.h"
#include "usermessages.h"
#include "tier0/memdbgon.h"

void TerrorMessageText( CTerrorPlayer *player, const char *text, unsigned int rgb )
{
	CSingleUserRecipientFilter filter( player );
	filter.MakeReliable();
	UserMessageBegin( filter, "MessageText" );
		WRITE_BYTE( rgb & 0xff );
		WRITE_BYTE( ( rgb >> 8 ) & 0xff );
		WRITE_BYTE( ( rgb >> 16 ) & 0xff );
		WRITE_STRING( text );
	MessageEnd();
}
void TerrorClearMessageText( CTerrorPlayer *player )
{
	CSingleUserRecipientFilter filter( player );
	filter.MakeReliable();
	UserMessageBegin( filter, "MessageText" );
	MessageEnd();
}
