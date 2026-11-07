//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TUTOR_H
#define TUTOR_H
#ifdef _WIN32
#pragma once
#endif

class CBaseCombatCharacter;

// Called once by DLLInit, not on round/map changes or client reconnects.
void ResetTutorLessons();

// True means that the original tutor has consumed this warning. The caller
// still commits its special-warning timer/actor ID, but must not also Speak.
bool TryWarnSpecialTutorial( int zombieClass, CBaseCombatCharacter *target );

#endif // TUTOR_H
