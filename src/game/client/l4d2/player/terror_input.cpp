//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: L4D2 client input interface.
//
//=============================================================================//

#include "cbase.h"
#include "input.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static CInput g_Input;
IInput *input = &g_Input;
