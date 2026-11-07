//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: L4D2 startup and chat usermessages.
//
//=============================================================================//

#ifndef L4D2_HUD_CHAT_H
#define L4D2_HUD_CHAT_H
#ifdef _WIN32
#pragma once
#endif

#include "hud_basechat.h"

class CHudChat : public CBaseHudChat
{
	DECLARE_CLASS_SIMPLE( CHudChat, CBaseHudChat );

public:
	CHudChat( const char *pElementName );

	virtual void Init() OVERRIDE;
	virtual void OnTick() OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme ) OVERRIDE;
	virtual void MsgFunc_SayText( bf_read &msg ) OVERRIDE;
	virtual void MsgFunc_SayText2( bf_read &msg ) OVERRIDE;
	virtual void MsgFunc_TextMsg( bf_read &msg ) OVERRIDE;
	void MsgFunc_RadioText( bf_read &msg );
	virtual int GetFilterForString( const char *pString ) OVERRIDE;
	void PrepareForStats();
	virtual void OnTransitionRestore() OVERRIDE;

private:
	vgui::Label *m_pKeyStateLabel;
};

#endif // L4D2_HUD_CHAT_H
