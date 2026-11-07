//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: L4D2 startup and chat usermessages.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/hud/hud_chat.h"
#include "hud_macros.h"
#include "c_playerresource.h"
#include "text_message.h"
#include "vguicenterprint.h"
#include "vgui/ILocalize.h"
#include "engine/IEngineSound.h"
#include "vgui/IScheme.h"
#include "vgui/IInput.h"
#include "vgui_controls/Label.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

DECLARE_HUDELEMENT( CHudChat );
DECLARE_HUD_MESSAGE( CHudChat, SayText );
DECLARE_HUD_MESSAGE( CHudChat, SayText2 );
DECLARE_HUD_MESSAGE( CHudChat, TextMsg );
DECLARE_HUD_MESSAGE( CHudChat, RadioText );

// C22C400 is lookup-only: no message reads, sound, or forwarding.
static void __MsgFunc_RawAudio( bf_read &msg )
{
	NOTE_UNUSED( msg );
	GET_HUDELEMENT( CHudChat );
}

void CHudChat::MsgFunc_RadioText( bf_read &msg )
{
	// C22C8D0 (wrapper C22D0F0). First byte is discarded, second is sender.
	if ( GetChatHistory() )
		GetChatHistory()->SetFont( vgui::scheme()->GetIScheme( GetScheme() )->GetFont( "ChatFont", false ) );
	msg.ReadByte();
	const int sender = msg.ReadByte();
	wchar_t format[128], first[128], second[128], third[128], fourth[128], output[128];
	wchar_t *formatText = ReadLocalizedString( msg, format, sizeof( format ), false );
	ReadChatTextString( msg, first, sizeof( first ) );
	ReadLocalizedString( msg, second, sizeof( second ), true );
	ReadLocalizedString( msg, third, sizeof( third ), true );
	ReadLocalizedString( msg, fourth, sizeof( fourth ), true );
	g_pVGuiLocalize->ConstructString( output, sizeof( output ), formatText, 4, first, second, third, fourth );
	char text[512];
	g_pVGuiLocalize->ConvertUnicodeToANSI( ConvertCRtoNL( output ), text, sizeof( text ) );
	ChatPrintf( sender, CHAT_FILTER_TEAMCHANGE, "%s", text );
	CLocalPlayerFilter filter;
	C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "HudChat.Message" );
}

CHudChat::CHudChat( const char *pElementName ) : BaseClass( pElementName )
{
	// C22C630: own the empty label before BaseChat.res applies its settings.
	m_pKeyStateLabel = new vgui::Label( this, "KeyStateLabel", "" );
}

void CHudChat::Init()
{
	BaseClass::Init();

	HOOK_HUD_MESSAGE( CHudChat, RadioText );
	HOOK_HUD_MESSAGE( CHudChat, SayText );
	HOOK_HUD_MESSAGE( CHudChat, SayText2 );
	HOOK_HUD_MESSAGE( CHudChat, TextMsg );
	HOOK_MESSAGE( RawAudio );
}

void CHudChat::OnTick()
{
	// C22C030: message mode alone controls this modifier-key status label.
	BaseClass::OnTick();
	const bool messageMode = GetMessageMode() != 0;
	m_pKeyStateLabel->SetVisible( messageMode );
	if ( !messageMode )
	{
		return;
	}

	const bool alt = vgui::input()->IsKeyDown( KEY_LALT ) || vgui::input()->IsKeyDown( KEY_RALT );
	const bool control = vgui::input()->IsKeyDown( KEY_LCONTROL ) || vgui::input()->IsKeyDown( KEY_RCONTROL );
	const wchar_t *pText = L"";
	if ( alt )
	{
		if ( control )
		{
			pText = L"[CTRL+ALT]";
		}
		else
		{
			pText = L"[ALT]";
		}
	}
	else if ( control )
	{
		pText = L"[CTRL]";
	}
	m_pKeyStateLabel->SetText( pText );
}

void CHudChat::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	m_pKeyStateLabel->SetPaintBackgroundEnabled( false ); // C22C890.
}

void CHudChat::MsgFunc_SayText( bf_read &msg )
{
	// Original client RVA 0x1149D0: the chat flag follows the string.
	int nClient = msg.ReadByte();
	char szText[256];
	msg.ReadString( szText, sizeof( szText ) );
	bool bWantsToChat = msg.ReadByte() != 0;

	if ( bWantsToChat )
	{
		ChatPrintf( nClient, CHAT_FILTER_NONE, "%s", szText );
	}
	else
	{
		Printf( CHAT_FILTER_NONE, "%s", hudtextmessage->LookupString( szText ) );
	}

	CLocalPlayerFilter filter;
	C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "HudChat.Message" );
}

void CHudChat::MsgFunc_SayText2( bf_read &msg )
{
	if ( !g_PR )
		return;

	// Original client RVA 0x22CC00: client, chat flag, format and four strings.
	int nClient = msg.ReadByte();
	bool bWantsToChat = msg.ReadByte() != 0;

	wchar_t szText[6][256];
	char szUntranslatedText[256];
	wchar_t *pFormat = ReadLocalizedString( msg, szText[0], sizeof( szText[0] ), false,
		szUntranslatedText, sizeof( szUntranslatedText ) );
	ReadChatTextString( msg, szText[1], sizeof( szText[1] ) );
	ReadChatTextString( msg, szText[2], sizeof( szText[2] ) );
	ReadLocalizedString( msg, szText[3], sizeof( szText[3] ), true );
	ReadLocalizedString( msg, szText[4], sizeof( szText[4] ), true );

	g_pVGuiLocalize->ConstructString_safe( szText[5], pFormat, 4,
		szText[1], szText[2], szText[3], szText[4] );

	char szAnsiText[512];
	g_pVGuiLocalize->ConvertUnicodeToANSI( ConvertCRtoNL( szText[5] ), szAnsiText, sizeof( szAnsiText ) );

	if ( bWantsToChat )
	{
		int nFilter = CHAT_FILTER_NONE;
		if ( nClient > 0 && g_PR->GetTeam( nClient ) != g_PR->GetTeam( GetLocalPlayerIndex() ) )
		{
			nFilter = CHAT_FILTER_PUBLICCHAT;
		}

		ChatPrintf( nClient, nFilter, "%s", szAnsiText );

		CLocalPlayerFilter filter;
		C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, "HudChat.Message" );
	}
	else
	{
		ChatPrintf( nClient, GetFilterForString( szUntranslatedText ), "%s", szAnsiText );
	}
}

int CHudChat::GetFilterForString( const char *pString )
{
	int nFilter = BaseClass::GetFilterForString( pString );
	if ( nFilter != CHAT_FILTER_NONE )
		return nFilter;

	if ( !Q_stricmp( pString, "#CStrike_Name_Change" ) )
		return CHAT_FILTER_NAMECHANGE;

	return CHAT_FILTER_NONE;
}

void CHudChat::MsgFunc_TextMsg( bf_read &msg )
{
	// Original client RVA 0x115110 also accepts destination 5: talk in color 4.
	enum { HUD_PRINTTALK_COLORED = 5 };

	int nDestination = msg.ReadByte();
	char szText[2048];
	wchar_t szParameters[5][256];
	wchar_t szOutput[256];

	for ( int i = 0; i < ARRAYSIZE( szParameters ); ++i )
	{
		msg.ReadString( szText, sizeof( szText ) );
		char *pText = hudtextmessage->LookupString( szText, &nDestination );
		const wchar_t *pLocalizedText = g_pVGuiLocalize->Find( pText );
		if ( pLocalizedText )
		{
			V_wcsncpy( szParameters[i], pLocalizedText, sizeof( szParameters[i] ) );
		}
		else
		{
			if ( i != 0 )
			{
				StripEndNewlineFromString( pText );
			}
			g_pVGuiLocalize->ConvertANSIToUnicode( pText, szParameters[i], sizeof( szParameters[i] ) );
		}
	}

	if ( !cl_showtextmsg.GetBool() )
		return;

	switch ( nDestination )
	{
	case HUD_PRINTCENTER:
		g_pVGuiLocalize->ConstructString_safe( szOutput, szParameters[0], 4,
			szParameters[1], szParameters[2], szParameters[3], szParameters[4] );
		internalCenterPrint->Print( ConvertCRtoNL( szOutput ) );
		break;

	case HUD_PRINTNOTIFY:
	case HUD_PRINTCONSOLE:
	case HUD_PRINTTALK:
	case HUD_PRINTTALK_COLORED:
		g_pVGuiLocalize->ConstructString_safe( szOutput, szParameters[0], 4,
			szParameters[1], szParameters[2], szParameters[3], szParameters[4] );
		g_pVGuiLocalize->ConvertUnicodeToANSI( szOutput, szText, sizeof( szText ) );
		int nLength = Q_strlen( szText );
		if ( nLength != 0 && szText[nLength - 1] != '\n' && szText[nLength - 1] != '\r' )
		{
			Q_strncat( szText, "\n", sizeof( szText ), 1 );
		}

		ConvertCRtoNL( szText );
		if ( nDestination == HUD_PRINTTALK_COLORED )
		{
			Printf( CHAT_FILTER_NONE, "%c%s", COLOR_LOCATION, szText );
		}
		else if ( nDestination == HUD_PRINTTALK )
		{
			Printf( CHAT_FILTER_NONE, "%s", szText );
		}
		else
		{
			Msg( "%s", szText );
		}
		break;
	}
}

static int s_nStatsChatMode;
 static wchar_t s_StatsChatEntry[256];
 void CHudChat::PrepareForStats()
 {
  s_nStatsChatMode=GetMessageMode();
  if(s_nStatsChatMode) GetChatInput()->GetMessageText(s_StatsChatEntry,sizeof(s_StatsChatEntry));
  StopMessageMode();
  SetVisible(false);
 }
 void CHudChat::OnTransitionRestore()
 {
  if(s_nStatsChatMode)
  {
   StartMessageMode(s_nStatsChatMode);
   GetChatInput()->SetEntry(s_StatsChatEntry);
  }
 }
