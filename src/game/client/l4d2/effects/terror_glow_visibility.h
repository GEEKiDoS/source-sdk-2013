//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_GLOW_VISIBILITY_H
#define TERROR_GLOW_VISIBILITY_H
#ifdef _WIN32
#pragma once
#endif
class C_BaseCombatCharacter;
class C_BaseEntity;
// Native C100150D0/C10015000 with the caller's disregard-FOV flag true.
// Combat-character pairs share the serial-handle-keyed, 0.09-second cache.
bool TerrorGlow_IsAbleToSee( C_BaseCombatCharacter *pViewer, C_BaseEntity *pTarget );
#endif
