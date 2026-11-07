//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_ROUND_END_H
#define TERROR_ROUND_END_H
#ifdef _WIN32
#pragma once
#endif
// S269130's stack result, passed by reference through native reason arbitration.
// Not a session object, event listener, or independently maintained scoreboard.
struct TerrorRoundEndInfo
{
	int reason;
	int winner;
	const char *message;
	float time;
};
#endif
