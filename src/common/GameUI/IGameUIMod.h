//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Interface exported by gameui_mod.dll next to IGameUI. The client registers a
//          loading sink so the game-owned loading screen receives the engine's real progress.
//
//=============================================================================//
#ifndef IGAMEUIMOD_H
#define IGAMEUIMOD_H
#ifdef _WIN32
#pragma once
#endif

// Owned by the client, which unregisters it before unloading. gameui_mod retains the
// current progress so a sink registered mid-load starts from the real state.
abstract_class IGameUIModClient
{
public:
	virtual void LoadingStarted() = 0;
	virtual void LoadingProgress( float fraction, const char *statusText ) = 0;
	virtual void LoadingFinished( bool error, const char *failureReason, const char *extendedReason ) = 0;
};

abstract_class IGameUIMod
{
public:
	virtual void RegisterClient( IGameUIModClient *client ) = 0;
};

#define GAMEUI_MOD_INTERFACE_VERSION "L4D2GameUIMod001"

#endif // IGAMEUIMOD_H
