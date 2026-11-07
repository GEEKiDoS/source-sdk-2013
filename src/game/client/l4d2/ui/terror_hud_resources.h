//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Mounted L4D2 HUD scheme and memory-font preparation.
//
//=============================================================================//

#ifndef TERROR_HUD_RESOURCES_H
#define TERROR_HUD_RESOURCES_H
#ifdef _WIN32
#pragma once
#endif

class IFileSystem;

// Called by the existing render-target startup before EngineVGUI loads schemes.
bool PrepareTerrorHudBootstrap( IFileSystem *pFileSystem );

// Call before the first L4D2 viewport is constructed. Failure is not a request
// to use a substitute scheme or an installed system font.
bool PrepareTerrorHudResources();

class KeyValues;
// Reuse the sole mounted-resource condition evaluator for client-owned GameUI.
bool LoadTerrorUiResource( KeyValues *resource, const char *filename );

#endif // TERROR_HUD_RESOURCES_H
