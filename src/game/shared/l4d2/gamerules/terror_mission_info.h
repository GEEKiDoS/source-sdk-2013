//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_MISSION_INFO_H
#define TERROR_MISSION_INFO_H
#ifdef _WIN32
#pragma once
#endif
class KeyValues;

// Native M19970 UPDATE (false) / BORROW (true) operations. BORROW recursively
// changes only already-existing keys; it never imports a missing Game subtree.
void TerrorMergeMissionKeyValues( KeyValues *storage, KeyValues *update, bool onlyExisting );

#ifndef CLIENT_DLL
// Caller owns the result. This is ONLY the consumed Game projection of native
// M9960 -> M4700, not a replacement full MatchFramework/session/details object.
KeyValues *TerrorBuildActiveServerGameProjection();
#endif
#endif
