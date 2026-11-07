// Native public-API console callbacks, PE32 addresses retained in native_objects.json.
#include "cbase.h"
#include "c_baseanimating.h"
#include "cliententitylist.h"
#include "predictioncopy.h"
#include "view.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "tier1/utlvector.h"
#include "tier0/memdbgon.h"
extern void UpdateVisibilityAllEntities();
extern ConVar steamworks_client_sessionid;
extern ConVar steamworks_server_sessionid;
static void CC_ShowSteamStatsSessionID()
{
	DevMsg( "Client session ID (%s).\n", steamworks_client_sessionid.GetString() );
	DevMsg( "Server session ID (%s).\n", steamworks_server_sessionid.GetString() );
}
static void CC_ClientCrash( const CCommand & )
{
	// Main decision: deliberately preserve C27B350's debug access violation.
	*(volatile float *)0x18A0 = 1.0f;
}
static void CC_DestroyRagdolls( const CCommand & )
{
	CUtlVector< C_BaseEntity * > ragdolls;
	C_BaseEntityIterator iterator;
	for ( C_BaseEntity *entity = iterator.Next(); entity; entity = iterator.Next() )
		if ( dynamic_cast< C_ClientRagdoll * >( entity ) ) ragdolls.AddToTail( entity );
	FOR_EACH_VEC( ragdolls, i ) ClientEntityList().AddToDeleteList( ragdolls[i] );
}
static void CC_DumpPlayer( const CCommand &args )
{
	if ( args.ArgC() != 2 ) return;
	C_BaseEntity *entity = ClientEntityList().GetBaseEntity( atoi( args[1] ) );
	if ( !entity || !entity->IsPlayer() ) return;
	Vector minimum, maximum;
	entity->GetRenderBoundsWorldspace( minimum, maximum );
	Msg( "Effects: %x\n", entity->GetEffects() );
	Msg( "Dormant: %d\n", entity->IsDormant() );
	Msg( "Worldspace Render Bounds: [%.2f %.2f %.2f] -> [%.2f %.2f %.2f]\n", minimum.x, minimum.y, minimum.z, maximum.x, maximum.y, maximum.z );
	CPredictionDescribeData describe( entity, TD_OFFSET_NORMAL );
	describe.DumpDescription( entity->GetPredDescMap() );
}
static void CC_SplitScreenOrigin( const CCommand & )
{
	const Vector &origin = MainViewOrigin(); // Main-approved single primary slot.
	Warning( "\"origin\"\t\"%.1f, %.1f, %.1f\"\n", origin.x, origin.y, origin.z );
}
static void CC_GetPosExact( const CCommand & )
{
	Vector origin = MainViewOrigin();
	QAngle angles = MainViewAngles();
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player ) { origin = player->GetAbsOrigin(); angles = player->GetAbsAngles(); }
	Warning( "setpos_exact %f %f %f;setang_exact %f %f %f\n", origin.x, origin.y, origin.z, angles.x, angles.y, angles.z );
}
// The original material-context has no corresponding target setters. Main decision
// omits only those operations; obtaining/releasing a real context remains native.
static void CC_SkyboxFogFactor( const CCommand & ) { CMatRenderContextPtr context( materials ); }
static void CC_WindDirection( const CCommand & ) { CMatRenderContextPtr context( materials ); }
static void CC_UpdateVisibility( const CCommand & ) { UpdateVisibilityAllEntities(); }

static void CC_QueryCacheStats( const CCommand & )
{
	// C184730's five BSS inputs have no writers or address-taken aliases.
	Warning( "%d queries, %d misses (%d free) suc spec = %d wasted spec=%d\n", 0, 0, 0, 0, 0 );
}
static void CC_ClientPhysics2Stats( const CCommand & )
{
	Msg( "Phys2 World does not exist\n" );
}
static void CC_Physics2Stats( const CCommand & )
{
	Msg( "Phys2 is not connected\n" );
}
static void CC_SaveClientPhysics2( const CCommand & )
{
	// C174210 only calls the unavailable Physics2 world's Save interface.
	// Standing missing-module rule: preserve registration, omit only that call.
}
static void CC_DumpSplitScreenHacks( const CCommand & )
{
	// C0B0370 enumerates names recorded by original-engine split-screen guards.
	// The single-primary-slot target has no such guards or recorded names.
}
static void CC_SplitScreenTeleport( const CCommand & )
{
	// C131010/C130F00 returns when the other local slot has no player.
	// The target engine has exactly one local slot, so this is that native path.
}
#include "l4d2/terror_client_console_commands_registrations.inc"
