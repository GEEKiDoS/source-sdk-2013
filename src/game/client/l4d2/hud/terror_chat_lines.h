//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_CHAT_LINES_H
#define TERROR_CHAT_LINES_H
#ifdef _WIN32
#pragma once
#endif
#include "hud_basechat.h"
// Actual controls allocated by the existing base-chat constructor factories.
class CHudChatInputLine : public CBaseHudChatInputLine
{
	DECLARE_CLASS_SIMPLE( CHudChatInputLine, CBaseHudChatInputLine );
public:
	CHudChatInputLine( vgui::Panel *parent, const char *name );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
};
// Native C22C420/C22BFE0 has no distinct painting/fading/text parser.
class CHudChatLine : public CBaseHudChatLine
{
	DECLARE_CLASS_SIMPLE( CHudChatLine, CBaseHudChatLine );
public:
	CHudChatLine( vgui::Panel *parent, const char *name );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
};
#endif
