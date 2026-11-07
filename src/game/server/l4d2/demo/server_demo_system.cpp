//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/demo/server_demo_system.h"
#include "tier0/memdbgon.h"

// Native10790224 starts true. Dump temporarily clears/restores this same byte.
bool g_bAllowServerDemoDump = true;
// Native108226E0 is never bound (complete raw-address census). In addition,
// native1CF200 needs VSERVERENGINETOOLS001 slot12, absent on the target.
// Do not install a fake recorder or reinterpret another engine-tools slot.
IServerDemoSystem *g_pServerDemoSystem = NULL;
