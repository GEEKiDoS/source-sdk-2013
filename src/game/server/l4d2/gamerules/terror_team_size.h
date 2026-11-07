//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_TEAM_SIZE_H
#define L4D2_TERROR_TEAM_SIZE_H
#ifdef _WIN32
#pragma once
#endif
#include "convar.h"
void OnSurvivorLimitChanged( IConVar *var, const char *oldValue, float oldFloat );
void OnMaxPlayerZombiesChanged( IConVar *var, const char *oldValue, float oldFloat );
#endif
