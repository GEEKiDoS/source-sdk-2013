// Include in ragdoll_shared.cpp for L4D2 INSTEAD of the SDK LRU section.
#include "l4d2/effects/terror_ragdoll_fader.h"
#ifdef CLIENT_DLL
#include "l4d2/effects/terror_view_frustum.h"
#include "iviewrender.h"
#include "view_shared.h"
#endif

#ifdef CLIENT_DLL
ConVar g_debug_ragdoll_removal( "g_debug_ragdoll_removal", "0", 0x00006008 );
static ConVar g_ragdoll_max_fps( "g_ragdoll_max_fps", "30", 0x0000200A, "Clients with framerates less than this will reduce the ideal number of ragdolls to display" );
static ConVar g_ragdoll_min_fps( "g_ragdoll_min_fps", "10", 0x0000200A, "Clients with framerates less than this will only show 1 ragdoll of each type" );
#else
ConVar g_debug_ragdoll_removal( "g_debug_ragdoll_removal", "0", 0x00006004 );
static ConVar g_ragdoll_max_fps( "g_ragdoll_max_fps", "30", 0x00002006, "Clients with framerates less than this will reduce the ideal number of ragdolls to display" );
static ConVar g_ragdoll_min_fps( "g_ragdoll_min_fps", "10", 0x00002006, "Clients with framerates less than this will only show 1 ragdoll of each type" );
#endif
#ifdef CLIENT_DLL
static ConVar cl_ragdoll_maxcount_gib( "cl_ragdoll_maxcount_gib", "30", 0x0000000A, "Clients will only show this many gib ragdolls" );
static ConVar cl_ragdoll_maxcount_generic( "cl_ragdoll_maxcount_generic", "45", 0x0000000A, "Clients will only show this many generic infected ragdolls" );
static ConVar cl_ragdoll_maxcount_special( "cl_ragdoll_maxcount_special", "9", 0x0000000A, "Clients will only show this many special ragdolls" );
static ConVar cl_ragdoll_maxcount_boss( "cl_ragdoll_maxcount_boss", "3", 0x0000000A, "Clients will only show this many boss ragdolls" );
static ConVar cl_ragdoll_maxcount_survivor( "cl_ragdoll_maxcount_survivor", "3", 0x0000000A, "Clients will only show this many survivor ragdolls" );
static ConVar cl_ragdoll_maxcount( "cl_ragdoll_maxcount", "75", 0x0000000A, "Clients will only show this many ragdolls overall" );
static ConVar cl__ragdoll_max_remove_per_frame( "cl__ragdoll_max_remove_per_frame", "1", 0x0000000A );
static ConVar *s_RagdollCategoryLimit[] = { &cl_ragdoll_maxcount_gib, &cl_ragdoll_maxcount_generic, &cl_ragdoll_maxcount_special, &cl_ragdoll_maxcount_boss, &cl_ragdoll_maxcount_survivor };
#define RAGDOLL_OVERALL_LIMIT cl_ragdoll_maxcount
#define RAGDOLL_REMOVE_LIMIT cl__ragdoll_max_remove_per_frame
#else
static ConVar sv_ragdoll_maxcount_gib( "sv_ragdoll_maxcount_gib", "30", 0x00000006, "Clients will only show this many gib ragdolls" );
static ConVar sv_ragdoll_maxcount_generic( "sv_ragdoll_maxcount_generic", "45", 0x00000006, "Clients will only show this many generic infected ragdolls" );
static ConVar sv_ragdoll_maxcount_special( "sv_ragdoll_maxcount_special", "9", 0x00000006, "Clients will only show this many special ragdolls" );
static ConVar sv_ragdoll_maxcount_boss( "sv_ragdoll_maxcount_boss", "3", 0x00000006, "Clients will only show this many boss ragdolls" );
static ConVar sv_ragdoll_maxcount_survivor( "sv_ragdoll_maxcount_survivor", "3", 0x00000006, "Clients will only show this many survivor ragdolls" );
static ConVar sv_ragdoll_maxcount( "sv_ragdoll_maxcount", "75", 0x00000006, "Clients will only show this many ragdolls overall" );
static ConVar sv__ragdoll_max_remove_per_frame( "sv__ragdoll_max_remove_per_frame", "1", 0x00000006 );
static ConVar *s_RagdollCategoryLimit[] = { &sv_ragdoll_maxcount_gib, &sv_ragdoll_maxcount_generic, &sv_ragdoll_maxcount_special, &sv_ragdoll_maxcount_boss, &sv_ragdoll_maxcount_survivor };
#define RAGDOLL_OVERALL_LIMIT sv_ragdoll_maxcount
#define RAGDOLL_REMOVE_LIMIT sv__ragdoll_max_remove_per_frame
#endif

CRagdollLRURetirement s_RagdollLRU( "CRagdollLRURetirement" );
CRagdollLRURetirement::CRagdollLRURetirement( const char *name ) : CAutoGameSystemPerFrame( name )
#ifdef CLIENT_DLL
	, m_flFPSMultiplier( 1.0f )
#endif
{}
void CRagdollLRURetirement::LevelInitPreEntity()
{
#ifdef CLIENT_DLL
	m_flLastRealTime = 0.0f;
	m_flLastFPS = 0.0f;
#endif
	m_iMaxRagdolls = -1;
	for ( int i = 0; i < RAGDOLL_TYPE_COUNT; ++i ) m_LRU[i].RemoveAll();
	m_iRagdollCount = 0;
}
void CRagdollLRURetirement::MoveToTopOfLRU( CBaseAnimating *ragdoll, int category )
{
	// Main's out-of-array decision: omit only the invalid list write. A
	// script-corrupted category does not cancel corpse creation or alias it.
	if ( category < 0 || category >= RAGDOLL_TYPE_COUNT ) return;
	CUtlLinkedList< CRagdollHandle > &list = m_LRU[category];
	bool found = false;
	FOR_EACH_LL( list, index )
	{
		if ( list[index].Get() == ragdoll )
		{
			list.Remove( index );
			found = true;
			break;
		}
	}
	if ( !found ) ++m_iRagdollCount;
	list.AddToTail( ragdoll );
}

#ifdef CLIENT_DLL
void CRagdollLRURetirement::AddRagdollFader( CBaseEntity *fader )
{
	EHANDLE handle = fader;
	if ( m_Faders.Find( handle ) < 0 ) m_Faders.AddToTail( handle );
}
void CRagdollLRURetirement::RemoveRagdollFader( CBaseEntity *fader )
{
	EHANDLE handle = fader;
	const int index = m_Faders.Find( handle );
	if ( index >= 0 ) m_Faders.Remove( index );
}
#pragma float_control( precise, on, push )
bool CRagdollLRURetirement::ShouldSkipRagdoll( CBaseAnimating *source )
{
	int maximum = m_iMaxRagdolls < 0 ? RAGDOLL_OVERALL_LIMIT.GetInt() : m_iMaxRagdolls;
	if ( fabs( 1.0 - static_cast< double >( m_flFPSMultiplier ) ) > 0.01 )
	{
		const float scaled = static_cast< float >( maximum ) * m_flFPSMultiplier;
		maximum = RoundFloatToInt( scaled );
	}
	if ( m_iRagdollCount + 1 < maximum ) return false;
	Vector mins, maxs;
	source->GetRenderBounds( mins, maxs );
	const matrix3x4_t &matrix = source->RenderableToWorldTransform();
	Vector translation;
	MatrixGetColumn( matrix, 3, translation );
	Vector localTranslation, localNormal;
	VectorIRotate( translation, matrix, localTranslation );
	VectorIRotate( m_vecViewForward, matrix, localNormal );
	const float yy = localNormal.y * localNormal.y;
	const float xx = localNormal.x * localNormal.x;
	const float zz = localNormal.z * localNormal.z;
	const float squared = ( yy + xx ) + zz;
	const float dx = localTranslation.x * localNormal.x;
	const float dy = localTranslation.y * localNormal.y;
	const float dz = localTranslation.z * localNormal.z;
	cplane_t plane;
	plane.normal = localNormal;
	plane.dist = squared * m_flViewNearDistance - ( ( dx + dy ) + dz );
	plane.type = PLANE_ANYX;
	plane.signbits = SignbitsForPlane( &plane );
	return BoxOnPlaneSide( mins.Base(), maxs.Base(), &plane ) == 2;
}
#pragma float_control( pop )
#endif

static bool ShouldRemoveThisRagdoll( CBaseAnimating *ragdoll )
{
	if ( g_RagdollLVManager.IsLowViolence() ) return true;
	if ( !ragdoll ) { Msg( "ALERT: why is Ragdoll pointer null?" ); return false; }
#ifdef CLIENT_DLL
	if ( !ragdoll->m_pRagdoll ) return false;
	Vector mins, maxs;
	const Vector origin = ragdoll->m_pRagdoll->GetRagdollOrigin();
	ragdoll->m_pRagdoll->GetRagdollBounds( mins, maxs );
	const GameViewFrustum_t &frustum = GetGameViewFrustum();
	if ( frustum.m_nFrame != gpGlobals->framecount ) return false;
	if ( !frustum.CullBox( mins + origin, maxs + origin ) ) return false;
	if ( g_debug_ragdoll_removal.GetBool() )
	{
		debugoverlay->AddBoxOverlay( origin, mins, maxs, vec3_angle, 0, 255, 0, 16, 5.0f );
		debugoverlay->AddLineOverlay( origin, origin + Vector( 0, 0, 64 ), 0, 255, 0, true, 5.0f );
	}
#else
	CBasePlayer *player = UTIL_GetLocalPlayer();
	if ( UTIL_FindClientInPVS( ragdoll->edict() ) )
	{
		// S34FC00 NULL-local-player decision: skip only the invalid cone read.
		if ( !player ) return false;
		if ( player->FInViewCone( ragdoll ) ) return false;
		if ( g_debug_ragdoll_removal.GetBool() ) NDebugOverlay::Line( ragdoll->GetAbsOrigin(), ragdoll->GetAbsOrigin() + Vector( 0, 0, 64 ), 0, 0, 255, true, 5.0f );
	}
	else if ( g_debug_ragdoll_removal.GetBool() ) NDebugOverlay::Line( ragdoll->GetAbsOrigin(), ragdoll->GetAbsOrigin() + Vector( 0, 0, 64 ), 0, 255, 0, true, 5.0f );
#endif
	return true;
}
static void RetireRagdollEntity( CBaseAnimating *ragdoll )
{
#ifdef CLIENT_DLL
	ragdoll->SUB_Remove();
#else
	ragdoll->SUB_StartFadeOut( 0.0f, true );
#endif
}

#pragma float_control( precise, on, push )
void CRagdollLRURetirement::RetireRagdolls( int category, bool keepAwake, bool overallLimit )
{
	CUtlLinkedList< CRagdollHandle > &list = m_LRU[category];
	int maximum = m_iMaxRagdolls;
	if ( g_RagdollLVManager.IsLowViolence() ) { maximum = 0; keepAwake = false; }
	else if ( maximum == -1 ) maximum = overallLimit ? RAGDOLL_OVERALL_LIMIT.GetInt() : s_RagdollCategoryLimit[category]->GetInt();
#ifdef CLIENT_DLL
	if ( maximum > 0 && fabs( 1.0 - static_cast< double >( m_flFPSMultiplier ) ) > 0.01 )
	{
		const int original = maximum;
		const float scaled = static_cast< float >( maximum ) * m_flFPSMultiplier;
		if ( list.Count() && g_debug_ragdoll_removal.GetBool() && C_BasePlayer::GetLocalPlayer() )
			engine->Con_NPrintf( category + 10, "ragdoll famerate multiplier is %f (%d -> %d)\n", m_flFPSMultiplier, original, RoundFloatToInt( scaled ) );
		maximum = RoundFloatToInt( scaled );
		if ( maximum < 1 ) maximum = 1;
	}
	// Native fader pass is independent of all quota/awake decisions.
	FOR_EACH_LL( list, ragdollIndex )
	{
		CBaseAnimating *ragdoll = list[ragdollIndex].Get();
		if ( !ragdoll ) continue;
		for ( int i = 0; i < m_Faders.Count(); ++i )
		{
			CFuncRagdollFader *fader = static_cast< CFuncRagdollFader * >( m_Faders[i].Get() );
			if ( fader ) fader->TestRagdoll( ragdoll );
			else m_Faders.Remove( i ); // native skips the element shifted into i
		}
	}
#endif
	if ( list.Count() <= maximum ) return;
	for ( int i = list.Head(), next; i != list.InvalidIndex(); i = next )
	{
		next = list.Next( i );
		if ( !list[i].Get() ) { list.Remove( i ); --m_iRagdollCount; }
	}
	if ( list.Count() <= maximum ) return;
	// Native checks the per-frame removal cap AFTER the first candidate.
	for ( int i = list.Head(), next; i != list.InvalidIndex(); i = next )
	{
		next = list.Next( i );
		CBaseAnimating *ragdoll = list[i].Get();
		Assert( ragdoll );
		IPhysicsObject *physics = ragdoll->VPhysicsGetObject();
		if ( physics && !physics->IsAsleep() ) ++m_iSimulatedRagdollCount;
		if ( ShouldRemoveThisRagdoll( ragdoll ) )
		{
			--m_iRagdollCount;
			++m_iRemovedThisFrame;
			ragdoll = list[i].Get();
			if ( ragdoll ) RetireRagdollEntity( ragdoll );
			list.Remove( i );
		}
		if ( list.Count() <= maximum || m_iRemovedThisFrame >= RAGDOLL_REMOVE_LIMIT.GetInt() ) return;
	}
	// C285A40/S3505xx gathered local players into a temporary vector, but no
	// subsequent instruction reads it; elided as a write-only local container.
	for ( int i = list.Head(), next; i != list.InvalidIndex() && list.Count() > maximum && m_iRemovedThisFrame < RAGDOLL_REMOVE_LIMIT.GetInt(); i = next )
	{
		next = list.Next( i );
		CBaseAnimating *ragdoll = list[i].Get();
		Assert( ragdoll );
		IPhysicsObject *physics = ragdoll->VPhysicsGetObject();
		if ( keepAwake && ( ragdoll->GetEffectEntity() || ( physics && !physics->IsAsleep() ) ) ) continue;
		--m_iRagdollCount;
		++m_iRemovedThisFrame;
		ragdoll = list[i].Get();
		if ( ragdoll ) RetireRagdollEntity( ragdoll );
		list.Remove( i );
	}
}
void CRagdollLRURetirement::Update( float )
{
#ifdef CLIENT_DLL
	const CViewSetup *setup = view->GetPlayerViewSetup();
	AngleVectors( setup->angles, &m_vecViewForward );
	const float y = m_vecViewForward.y * setup->origin.y;
	const float x = m_vecViewForward.x * setup->origin.x;
	const float z = m_vecViewForward.z * setup->origin.z;
	m_flViewNearDistance = ( ( y + x ) + z ) - 24.0f;
	const float dt = gpGlobals->realtime - m_flLastRealTime;
	float fps = dt > 0.0f ? 1.0f / dt : 60.0f;
	const float unclampedFPS = fps;
	m_flLastRealTime = gpGlobals->realtime;
	float multiplier = 1.0f;
	if ( !( m_flLastFPS > 23.0f && 15.0 > static_cast< double >( fps ) ) )
	{
		const float high = g_ragdoll_max_fps.GetFloat(), low = g_ragdoll_min_fps.GetFloat();
		if ( fps > high ) fps = high;
		else if ( low > fps ) fps = low;
		if ( fps != high )
		{
			const float range = high - low;
			const float missing = high - fps;
			multiplier = 1.0f - missing / range;
			if ( !( multiplier > m_flFPSMultiplier ) ) multiplier = ( m_flFPSMultiplier + multiplier ) * 0.5f;
		}
	}
	// Native stores the UNCLAMPED fps, even in the hysteresis branch.
	m_flLastFPS = unclampedFPS;
	m_flFPSMultiplier = multiplier;
#endif
	m_iSimulatedRagdollCount = 0;
	m_iRemovedThisFrame = 0;
	for ( int i = 0; i < RAGDOLL_TYPE_COUNT; ++i ) RetireRagdolls( i, true, false );
	int maximum = m_iMaxRagdolls < 0 ? RAGDOLL_OVERALL_LIMIT.GetInt() : m_iMaxRagdolls;
#ifdef CLIENT_DLL
	if ( fabs( 1.0 - static_cast< double >( m_flFPSMultiplier ) ) > 0.01 )
	{
		const float scaled = static_cast< float >( maximum ) * m_flFPSMultiplier;
		maximum = RoundFloatToInt( scaled );
	}
#endif
	if ( m_iRagdollCount > maximum )
	{
		m_iSimulatedRagdollCount = 0;
		for ( int i = 0; i < RAGDOLL_TYPE_COUNT; ++i ) RetireRagdolls( i, false, false );
		if ( m_iRagdollCount > maximum )
		{
			m_iSimulatedRagdollCount = 0;
			for ( int i = 0; i < RAGDOLL_TYPE_COUNT; ++i ) RetireRagdolls( i, false, true );
		}
	}
	if ( g_debug_ragdoll_removal.GetBool() )
	{
#ifdef CLIENT_DLL
		const int base = 20;
#else
		const int base = 30;
#endif
		engine->Con_NPrintf( base, "%d simulated\n", m_iSimulatedRagdollCount );
		engine->Con_NPrintf( base + 1, "%d total\n", m_iRagdollCount );
		engine->Con_NPrintf( base + 2, "%d removed\n", m_iRemovedThisFrame );
		for ( int i = 0; i < RAGDOLL_TYPE_COUNT; ++i ) engine->Con_NPrintf( base + 3 + i, "%d\n", m_LRU[i].Count() );
	}
}
#pragma float_control( pop )
void CRagdollLRURetirement::FrameUpdatePostEntityThink() { Update( 0.0f ); }
#undef RAGDOLL_OVERALL_LIMIT
#undef RAGDOLL_REMOVE_LIMIT
