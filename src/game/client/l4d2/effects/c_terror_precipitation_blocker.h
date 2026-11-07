//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_TERROR_PRECIPITATION_BLOCKER_H
#define C_TERROR_PRECIPITATION_BLOCKER_H
#ifdef _WIN32
#pragma once
#endif
#include "tier1/utlvector.h"
#include "mathlib/vector.h"
class C_BaseEntity;
extern CUtlVector< C_BaseEntity * > g_PrecipitationBlockers;
bool IsInPrecipitationBlocker( const Vector &point );
#endif
