//========= Copyright Valve Corporation, All rights reserved. ============//
#pragma once

#include <stddef.h>

// Process-local module load redirects for the Win64 launcher. A load whose base name matches a registered
// module ("shaderapidx9", "GameUI", with or without ".dll") is served from the replacement path instead.
// Loads requested by a replacement module itself (and the loader calls nested inside them) are never redirected,
// so a replacement can load the original module it wraps. Paths are copied and retained until process exit.
bool AddModuleRedirect( const wchar_t *pModuleBaseName, const wchar_t *pReplacementDll, wchar_t *pError, size_t errorChars );
// Installs the LoadLibrary hooks serving every redirect added so far. Call once, after AddModuleRedirect.
bool InstallModuleRedirects( wchar_t *pError, size_t errorChars );
