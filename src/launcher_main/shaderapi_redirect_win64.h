//========= Copyright Valve Corporation, All rights reserved. ============//
#pragma once

#include <stddef.h>

// Installs the process-local DX9 shader API load redirect. The returned path is
// copied and retained by the hooks until process exit.
bool InstallShaderApiDx12Redirect( const wchar_t *pRendererDll, wchar_t *pError, size_t errorChars );
