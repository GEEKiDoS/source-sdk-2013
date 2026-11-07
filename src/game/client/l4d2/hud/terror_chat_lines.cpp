//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/hud/terror_chat_lines.h"
#include "vgui_controls/Label.h"
#include "vgui/IScheme.h"
#include "tier0/memdbgon.h"

CHudChatInputLine::CHudChatInputLine( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ) {}
void CHudChatInputLine::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	vgui::HFont font = scheme->GetFont( "ChatFont", false );
	m_pPrompt->SetFont( font );
	m_pInput->SetFont( font );
	m_pInput->SetFgColor( scheme->GetColor( "Chat.TypingText", scheme->GetColor( "Panel.FgColor", Color( 255, 255, 255, 255 ) ) ) );
}
CHudChatLine::CHudChatLine( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ) {}
void CHudChatLine::ApplySchemeSettings( vgui::IScheme *scheme )
{
	// C22BFE0 is a direct tail call to the original CBaseHudChatLine scheme.
	BaseClass::ApplySchemeSettings( scheme );
}
