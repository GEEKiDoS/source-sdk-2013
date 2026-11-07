//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PERF_WIZARD_H
#define TERROR_PERF_WIZARD_H
#ifdef _WIN32
#pragma once
#endif
// Native RTTI is PerfWizard / IPerfWizard. Only Shutdown is reachable in
// this client build; see perf_reachability_census.json and integration_packet.txt.
class PerfWizard
{
public:
	void Shutdown(); // 267C20.
};
PerfWizard *GetTerrorPerfWizard(); // 267BE0.
void ShutdownTerrorPerfWizard();
#endif
