//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/c_client_path.h"
#include "l4d2/player/c_terrorplayer.h"
#include "tier2/renderutils.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

static ConVar z_escape_route_segment_fade_in_rate( "z_escape_route_segment_fade_in_rate", "150", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL );
static ConVar z_escape_route_segment_fade_out_rate( "z_escape_route_segment_fade_out_rate", "150", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL );
static ConVar z_escape_route_force_visible( "z_escape_route_force_visible", "0", 0x00004008 );
static ConVar z_escape_route_alpha( "z_escape_route_alpha", "25", 0x00004008 );
static ConVar z_escape_route_far( "z_escape_route_far", "500", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL, "Escape route draws segments that are this close to the local player" );
static ConVar z_escape_route_near( "z_escape_route_near", "200", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL, "Escape route draws segments that are this far from the local player" );
static ConVar cl_show_path( "cl_show_path", "1", 0x00000088 );
static ConVar cl_drawescaperoute( "cl_drawescaperoute", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL );

IMPLEMENT_CLIENTCLASS_DT_NOBASE( C_ClientPath, DT_ClientPath, CClientPath )
	RecvPropArray3( RECVINFO_ARRAY( m_posX ), RecvPropInt( RECVINFO( m_posX[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_posY ), RecvPropInt( RECVINFO( m_posY[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_posZ ), RecvPropInt( RECVINFO( m_posZ[0] ) ) ),
	RecvPropInt( RECVINFO( m_posCount ) ),
END_RECV_TABLE()

static CUtlVector< C_ClientPath * > s_ClientPaths;

datamap_t *C_ClientPath::GetPredDescMap() { return NULL; }
C_ClientPath::C_ClientPath()
{
	s_ClientPaths.AddToTail( this );
}
C_ClientPath::~C_ClientPath() { s_ClientPaths.FindAndRemove( this ); }
bool C_ClientPath::ShouldDraw() { return false; }
int C_ClientPath::ComputeModelTranslucencyType() { return 1; }

static int PathTruncate( float value ) { return _mm_cvtt_ss2si( _mm_set_ss( value ) ); }
static float PathLength( const Vector &v ) { return sqrtf( ( v.y * v.y + v.z * v.z ) + v.x * v.x ); }
static void DrawPathTriangle( const Vector &a, const Vector &b, const Vector &c, unsigned char alpha )
{
	const Color faint( 0, 255, 0, PathTruncate( static_cast< float >( alpha ) * 0.25f ) );
	const Color full( 0, 255, 0, alpha );
	RenderTriangle( a, b, c, faint, false );
	RenderTriangle( a, b, c, full, true );
	RenderTriangle( a, c, b, faint, false );
	RenderTriangle( a, c, b, full, true );
}
static void DrawPathSegment( const Vector &head, const Vector &tail, unsigned char alpha )
{
	Vector direction = head - tail;
	const int length = PathTruncate( VectorNormalize( direction ) );
	Vector right, up;
	VectorVectors( direction, right, up );
	right *= 3.0f;
	const Vector wing = right * 3.0f;
	const int count = MAX( 1, static_cast< int >( floor( static_cast< double >( length ) * static_cast< double >( 0.02f ) ) ) );
	for ( int i = 0; i < count; ++i )
	{
		const float step = i == count - 1 ? static_cast< float >( length ) - static_cast< float >( count - 1 ) * 50.0f : 50.0f;
		const float arrowLength = step * 0.75f;
		const Vector start = tail + ( direction * 50.0f ) * static_cast< float >( i );
		const Vector tip = start + direction * arrowLength;
		const Vector shoulder = start + ( direction * arrowLength ) * 0.75f;
		DrawPathTriangle( start + right, start - right, shoulder + right, alpha );
		DrawPathTriangle( start - right, shoulder - right, shoulder + right, alpha );
		DrawPathTriangle( shoulder + wing, shoulder - wing, tip, alpha );
	}
}
void C_ClientPath::Draw( C_TerrorPlayer *player )
{
	if ( IsDormant() || !player ) return;
	if ( !z_escape_route_force_visible.GetBool() &&
		( player->GetTeamNumber() != TEAM_INFECTED || !player->IsAlive() || player->IsGhost() || !cl_show_path.GetBool() ) )
	{
		m_flAlpha = 0.0f;
		V_memset( m_flSegmentAlpha, 0, sizeof( m_flSegmentAlpha ) );
		return;
	}
	m_flAlpha = static_cast< float >( z_escape_route_alpha.GetInt() );
	if ( !( m_flAlpha > 0.0f ) ) return;
	// The server writes at most256 entries. No receive-side count clamp is native.
	for ( int i = m_posCount - 1; i > 0; --i )
	{
		const Vector current( static_cast< float >( m_posX[i] ), static_cast< float >( m_posY[i] ), static_cast< float >( m_posZ[i] ) + 18.0f );
		const Vector next( static_cast< float >( m_posX[i-1] ), static_cast< float >( m_posY[i-1] ), static_cast< float >( m_posZ[i-1] ) + 18.0f );
		const float distance = PathLength( current - player->EyePosition() );
		float desired = 255.0f;
		if ( distance > z_escape_route_far.GetFloat() ) desired = 0.0f;
		else if ( z_escape_route_near.GetFloat() > distance && z_escape_route_near.GetFloat() > 0.0f )
			desired = ( distance / z_escape_route_near.GetFloat() ) * 255.0f;
		float &alpha = m_flSegmentAlpha[i];
		if ( alpha > desired )
		{
			alpha -= z_escape_route_segment_fade_out_rate.GetFloat() * gpGlobals->frametime;
			if ( desired > alpha ) alpha = desired;
		}
		else if ( desired > alpha )
		{
			alpha += z_escape_route_segment_fade_in_rate.GetFloat() * gpGlobals->frametime;
			if ( alpha > desired ) alpha = desired;
		}
		if ( !( alpha > 0.0f ) ) continue;
		const unsigned char drawAlpha = static_cast< unsigned char >( PathTruncate( ( alpha * m_flAlpha ) * 0.0039215689f ) );
		const float length = PathLength( current - next );
		const int pieces = PathTruncate( length * 0.01f );
		if ( pieces < 1 ) DrawPathSegment( next, current, drawAlpha );
		else
		{
			const float step = length / static_cast< float >( pieces );
			Vector direction = next - current;
			VectorNormalize( direction );
			for ( int piece = 0; piece < pieces; ++piece )
				DrawPathSegment( current + ( direction * static_cast< float >( piece + 1 ) ) * step,
					current + ( direction * static_cast< float >( piece ) ) * step, drawAlpha );
		}
	}
}
// Called in CDebugViewRender::Draw3DDebuggingInfo before engine debug overlays,
// matching native C1C2A90 -> C26F350, in the existing3D render phase.
void DrawTerrorClientPaths()
{
	C_TerrorPlayer *player = dynamic_cast< C_TerrorPlayer * >( C_BasePlayer::GetLocalPlayer() );
	if ( !player || !cl_drawescaperoute.GetBool() ) return;
	FOR_EACH_VEC( s_ClientPaths, i ) s_ClientPaths[i]->Draw( player );
}
