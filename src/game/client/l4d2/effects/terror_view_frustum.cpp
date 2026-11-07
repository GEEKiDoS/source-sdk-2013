// C1C9A20 root, populated by C1BAFF0 / C1CA800, not the engine's CullBox.
#include "cbase.h"
#include "view_shared.h"
#include "l4d2/effects/terror_view_frustum.h"
#include "tier0/memdbgon.h"

static GameViewFrustum_t s_GameViewFrustum; // native static frame0; no ready/reset
const GameViewFrustum_t &GetGameViewFrustum() { return s_GameViewFrustum; }

#pragma float_control( precise, on, push )
static float FrustumDot( const Vector &a, const Vector &b )
{
	const float x = a.x * b.x;
	const float y = a.y * b.y;
	const float z = a.z * b.z;
	return ( x + y ) + z;
}
static void SetGameFrustumSide( int index, const Vector &normal, const Vector &origin )
{
	// C363710 keeps the normalize sum in x87, then spills the reciprocal.
	const double yy = static_cast< double >( normal.y ) * normal.y;
	const double xx = static_cast< double >( normal.x ) * normal.x;
	const double zz = static_cast< double >( normal.z ) * normal.z;
	const float inverse = static_cast< float >( 1.0 / ( sqrt( ( yy + xx ) + zz ) + static_cast< double >( FLT_EPSILON ) ) );
	GameFrustumPlane_t &plane = s_GameViewFrustum.m_Planes[index];
	plane.normal.x = normal.x * inverse;
	plane.normal.y = normal.y * inverse;
	plane.normal.z = normal.z * inverse;
	plane.distance = FrustumDot( plane.normal, origin );
}
void UpdateGameViewFrustum( const CViewSetup &view )
{
	Vector forward, right, up;
	AngleVectors( view.angles, &forward, &right, &up );
	float fovForY = view.fov;
	if ( fovForY < 1.0f || fovForY > 179.0f ) fovForY = 90.0f;
	const double radians = static_cast< double >( 0.017453292f );
	const double degrees = static_cast< double >( 57.29578f );
	const double angleY = atan2( tan( static_cast< double >( fovForY ) * radians * 0.5 ) / view.m_flAspectRatio, 1.0 );
	const float fovY = static_cast< float >( ( angleY * degrees ) * 2.0 );
	const float tanX = static_cast< float >( tan( static_cast< double >( view.fov ) * 0.5 * radians ) );
	const float tanY = static_cast< float >( tan( radians * ( 0.5 * fovY ) ) );
	const float intercept = FrustumDot( view.origin, forward );
	s_GameViewFrustum.m_Planes[4].normal = forward;
	s_GameViewFrustum.m_Planes[4].distance = intercept + view.zNear;
	s_GameViewFrustum.m_Planes[5].normal = -forward;
	s_GameViewFrustum.m_Planes[5].distance = -view.zFar - intercept;
	Vector positive, negative;
	for ( int i = 0; i < 3; ++i )
	{
		const float product = tanX * forward[i];
		positive[i] = product + right[i];
		const float twice = right[i] * 2.0f;
		negative[i] = positive[i] - twice;
	}
	SetGameFrustumSide( 1, positive, view.origin );
	SetGameFrustumSide( 0, negative, view.origin );
	for ( int i = 0; i < 3; ++i )
	{
		const float product = tanY * forward[i];
		positive[i] = product + up[i];
		const float twice = up[i] * 2.0f;
		negative[i] = positive[i] - twice;
	}
	SetGameFrustumSide( 3, positive, view.origin );
	SetGameFrustumSide( 2, negative, view.origin );
	// C1BAFF0 publishes only after all native active-slot planes are written.
	s_GameViewFrustum.m_nFrame = gpGlobals->framecount;
}
bool GameViewFrustum_t::CullBox( const Vector &mins, const Vector &maxs ) const
{
	// C363C10 SIMD lanes select the positive support corner and compare <.
	// Scalar spelling preserves its (z+y)+x f32 addition order and unordered
	// polarity. The two padded native lanes are zero and can never cull.
	for ( int i = 0; i < ARRAYSIZE( m_Planes ); ++i )
	{
		const GameFrustumPlane_t &p = m_Planes[i];
		const float x = ( p.normal.x < 0.0f ? mins.x : maxs.x ) * p.normal.x;
		const float y = ( p.normal.y < 0.0f ? mins.y : maxs.y ) * p.normal.y;
		const float z = ( p.normal.z < 0.0f ? mins.z : maxs.z ) * p.normal.z;
		const float zy = z + y;
		if ( zy + x < p.distance ) return true;
	}
	return false;
}
#pragma float_control( pop )
