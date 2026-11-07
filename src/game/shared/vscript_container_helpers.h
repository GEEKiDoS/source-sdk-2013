//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef VSCRIPT_CONTAINER_HELPERS_H
#define VSCRIPT_CONTAINER_HELPERS_H
#ifdef _WIN32
#pragma once
#endif
#include "vscript/ivscript.h"
// Game-owned adapters for native L4D2 operations absent from engine VM010.
// One shared owner for server/client game consumers; no engine ABI additions.
bool VScriptIsTable( HSCRIPT object );
int VScriptGetArrayCount( HSCRIPT array );
bool VScriptGetScalarValue( HSCRIPT array, int index, ScriptVariant_t *value );
bool VScriptCreateArray( ScriptVariant_t &array );
bool VScriptArrayAddToTail( HSCRIPT array, const ScriptVariant_t &value );
bool VScriptIsSameObject( HSCRIPT left, HSCRIPT right, bool *same );
void VScriptReleaseContainerHelpers(); // MUST run before the owning DestroyVM.
#endif
