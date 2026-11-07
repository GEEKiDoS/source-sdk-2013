//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PROVIDER_FILES_H
#define TERROR_PROVIDER_FILES_H
#ifdef _WIN32
#pragma once
#endif
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"

// Rebuild the native local add-on list and mounts before resource discovery.
void TerrorMountAddons();

// The reached patterns are missions/*.txt, modes/*.txt and named *_addon.nut.
// mounted GAME path order and each physical match, including archive members.
void TerrorFindProviderFiles( const char *pattern, CUtlVector< CUtlString > &files );
#endif
