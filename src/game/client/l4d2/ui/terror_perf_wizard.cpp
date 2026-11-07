//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/terror_perf_wizard.h"
#include "vgui_controls/Panel.h"
#include "tier0/memdbgon.h"

// Native CRT registration105117D0 remains observable even without a reader.
static ConVar cl_perf_wizard_enable( "cl_perf_wizard_enable", "1", 0x00000088, "Enables a performance-tuning wizard" );

static PerfWizard s_TerrorPerfWizard; // Native singleton106E73A0.
static vgui::Panel *s_pPerfWizardPanel; // Native107C09B8, initial NULL.

PerfWizard *GetTerrorPerfWizard() { return &s_TerrorPerfWizard; } // 267BE0.
void PerfWizard::Shutdown() // 267C20.
{
	if ( s_pPerfWizardPanel )
	{
		s_pPerfWizardPanel->SetParent( (vgui::Panel *)NULL );
		s_pPerfWizardPanel->DeletePanel();
		s_pPerfWizardPanel = NULL;
	}
}
void ShutdownTerrorPerfWizard() { GetTerrorPerfWizard()->Shutdown(); }

// Main-authorized reachability elision, NOT a replacement PerfWizardPanel:
// producer268220 is referenced only by vtable105BAB94. The only object using
// that vtable is106E73A0; its only outward reference is getter267BE0, called
// only by VGui_Shutdown228730. Neither Init267C10 nor Update268220 is ever
// dispatched. Panel ctor267C50 has only two callers, both inside268220.
// No other producer/global writer, factory registration, export or shipped
// wizard layout exists. Keep the native Shutdown body even though the real
// panel pointer consequently stays NULL. Do not add a writer or update hook.
