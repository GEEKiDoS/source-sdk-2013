//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_MESSAGE_TEXT_H
#define TERROR_MESSAGE_TEXT_H
#ifdef _WIN32
#pragma once
#endif
// Same existing Source CLIENT_DLL player-name mapping as terror_weapon.h,
// terror_fx_shared.h and the other shared Terror owners.
#ifdef CLIENT_DLL
#define CTerrorPlayer C_TerrorPlayer
#endif
class CTerrorPlayer;

// Server207D20: reliable single-user MessageText, three RGB bytes + string.
// Client2B4500: local player only, append + Msg + close-hint refresh.
// rgb is packed R | G<<8 | B<<16; alpha is forced255 on the client.
void TerrorMessageText( CTerrorPlayer *player, const char *text, unsigned int rgb );
#ifndef CLIENT_DLL
// Server207DA0: the SAME reliable single-user message with EMPTY payload.
void TerrorClearMessageText( CTerrorPlayer *player );
#endif
#endif
