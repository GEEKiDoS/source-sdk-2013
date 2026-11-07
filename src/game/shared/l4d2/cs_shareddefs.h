//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Definitions shared by the CS player/rules layers that L4D2 builds on.
//
//=============================================================================//

#ifndef CS_SHAREDDEFS_H
#define CS_SHAREDDEFS_H
#ifdef _WIN32
#pragma once
#endif

// Player states, in the order of the original ten-entry state table (server 0x248F10).
enum CSPlayerState
{
	// Happily running around in the game.
	STATE_ACTIVE = 0,

	// First entering the server (shows the MOTD).
	STATE_WELCOME,

	// Choosing a team.
	STATE_PICKINGTEAM,

	// Choosing a class.
	STATE_PICKINGCLASS,

	// Playing the death animation.
	STATE_DEATH_ANIM,

	// Done playing the death animation, waiting for a key.
	STATE_DEATH_WAIT_FOR_KEY,

	// Free-roaming observer.
	STATE_OBSERVER_MODE,

	// Dead survivor waiting in a rescue closet.
	STATE_WAITING_FOR_RESCUE,

	// Infected ghost.
	STATE_GHOST,

	// Watching the intro camera.
	STATE_INTRO_CAMERA,

	NUM_PLAYER_STATES
};

#endif // CS_SHAREDDEFS_H
