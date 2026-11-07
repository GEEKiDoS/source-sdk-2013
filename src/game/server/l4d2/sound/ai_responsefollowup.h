//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef AI_RESPONSEFOLLOWUP_H
#define AI_RESPONSEFOLLOWUP_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlstring.h"

// Game-owned response data, never an engine interface or a serialized x86 ABI.
// S0200B0 FIRE and S020140 THEN. The strings belong to the response, not to
// the parser's current token buffer or the caller's temporary criteria.
struct AI_ResponseFollowup
{
	AI_ResponseFollowup() : delay( 0.0f ), fireDelay( 0.0f ), hasFollowup( false ), hasEntityIO( false ) {}
	bool HasConcept() const { return hasFollowup; }
	bool HasEntityIO() const { return hasEntityIO; }
	void Clear()
	{
		target.Clear();
		concept.Clear();
		contexts.Clear();
		delay = 0.0f;
		fireTarget.Clear();
		fireInput.Clear();
		fireDelay = 0.0f;
		hasFollowup = false;
		hasEntityIO = false;
	}

	CUtlString target;
	CUtlString concept;
	CUtlString contexts;
	float delay;
	CUtlString fireTarget;
	CUtlString fireInput;
	float fireDelay;
	// Native pooled empty strings are non-NULL. Presence and length are
	// therefore distinct (S01FAA0), including THEN with no context text.
	bool hasFollowup;
	bool hasEntityIO;
};

#endif // AI_RESPONSEFOLLOWUP_H
