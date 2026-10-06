//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "shadowmap_scene.h"
#include "mathlib/mathlib.h"
#include "tier0/memdbgon.h"

void ShadowMapScene_CubeFaceBasis( int face, Vector &forward, Vector &up, Vector &right )
{
	static const Vector directions[6] = { Vector(1,0,0), Vector(-1,0,0), Vector(0,1,0), Vector(0,-1,0), Vector(0,0,1), Vector(0,0,-1) };
	static const Vector ups[6] = { Vector(0,0,1), Vector(0,0,1), Vector(0,0,1), Vector(0,0,1), Vector(0,-1,0), Vector(0,1,0) };
	Assert( face >= 0 && face < 6 );
	face = clamp( face, 0, 5 );
	forward = directions[face];
	up = ups[face];
	CrossProduct( forward, up, right );
}

int ShadowMapScene_SelectCubeFace( const Vector &v )
{
	float x = fabsf( v.x ), y = fabsf( v.y ), z = fabsf( v.z );
	if ( x >= y && x >= z ) return v.x >= 0 ? 0 : 1;
	if ( y >= z ) return v.y >= 0 ? 2 : 3;
	return v.z >= 0 ? 4 : 5;
}

void ShadowMapScene_BuildViewMatrix( const Vector &origin, const Vector &forward, const Vector &up, VMatrix &m )
{
	Vector f = forward, r, u;
	VectorNormalize( f );
	CrossProduct( f, up, r );
	VectorNormalize( r );
	CrossProduct( r, f, u );
	m.Identity();
	for ( int i = 0; i < 3; ++i ) { m[0][i] = r[i]; m[1][i] = u[i]; m[2][i] = f[i]; }
	m[0][3] = -DotProduct( r, origin );
	m[1][3] = -DotProduct( u, origin );
	m[2][3] = -DotProduct( f, origin );
}

void ShadowMapScene_BuildPerspective( float t, float n, float f, VMatrix &m )
{
	Assert( t > 0 && n > 0 && f > n );
	m.Init( 1/t,0,0,0, 0,1/t,0,0, 0,0,f/(f-n),-n*f/(f-n), 0,0,1,0 );
}

void ShadowMapScene_BuildOrtho( float x, float y, float n, float f, VMatrix &m )
{
	Assert( x > 0 && y > 0 && f > n );
	m.Init( 1/x,0,0,0, 0,1/y,0,0, 0,0,1/(f-n),-n/(f-n), 0,0,0,1 );
}

bool ShadowMapScene_CascadeSplits( float n, float f, float splits[5], float blend[3] )
{
	memset( splits, 0, 5 * sizeof(float) );
	memset( blend, 0, 3 * sizeof(float) );
	if ( n <= 0 || f <= n ) return false;
	splits[0] = n; splits[4] = f;
	for ( int i = 1; i < 4; ++i )
	{
		float a = i / 4.0f;
		splits[i] = 0.6f*n*powf( f/n, a ) + 0.4f*(n+(f-n)*a);
	}
	for ( int i = 1; i < 4; ++i ) blend[i-1] = 0.05f*MIN( splits[i]-splits[i-1], splits[i+1]-splits[i] );
	return true;
}

void ShadowMapScene_SunBasis( const Vector &travel, Vector &x, Vector &y )
{
	Vector f = travel; VectorNormalize( f );
	Vector up = fabsf(f.z) < 0.99f ? Vector(0,0,1) : Vector(0,1,0);
	CrossProduct( f, up, x ); VectorNormalize( x );
	CrossProduct( x, f, y ); VectorNormalize( y );
}

float ShadowMapScene_LocalFar( const dworldlight_t &light, const Vector &mins, const Vector &maxs )
{
	if ( light.radius > 0 ) return MAX( light.radius, DX12_SHADOW_CUBE_NEAR + 0.001f );
	Vector delta;
	for ( int i = 0; i < 3; ++i ) delta[i] = MAX( fabsf(mins[i]-light.origin[i]), fabsf(maxs[i]-light.origin[i]) );
	return MAX( delta.Length() + DX12_SHADOW_CUBE_NEAR, DX12_SHADOW_CUBE_NEAR + 0.001f );
}

void ShadowMapScene_LocalFaceMatrices( const dworldlight_t &light, int face, float n, float f, int S, int U,
	VMatrix &view, VMatrix &proj, VMatrix &clip, float &tanRendered )
{
	Vector forward, up, right;
	bool cube = light.type == emit_point || ShadowMapScene_IsWideSpot( light.stopdot2 );
	if ( cube ) ShadowMapScene_CubeFaceBasis( face, forward, up, right );
	else
	{
		forward = light.normal; VectorNormalize( forward );
		ShadowMapScene_SunBasis( forward, right, up );
	}
	tanRendered = ShadowMapScene_RenderedTanHalfFov( cube ? 1.0f : MAX(0.00001f,ShadowMapScene_NarrowSpotTanHalfFov(light.stopdot2)), S, U );
	ShadowMapScene_BuildViewMatrix( light.origin, forward, up, view );
	ShadowMapScene_BuildPerspective( tanRendered, n, f, proj );
	MatrixMultiply( proj, view, clip );
}

void ShadowMapScene_LocalInfluence( const dworldlight_t &light, const Vector &worldMins, const Vector &worldMaxs, ShadowMapInfluenceVolume_t &out )
{
	// Shared illumination/caster-transmission coverage stays conservative even
	// when the client raises a spotlight's shadow-camera near plane.
	out.zNear = DX12_SHADOW_CUBE_NEAR;
	out.zFar = ShadowMapScene_LocalFar( light, worldMins, worldMaxs );
	out.cube = light.type == emit_point || ShadowMapScene_IsWideSpot( light.stopdot2 );
	out.tanRenderedHalfFov = ShadowMapScene_RenderedTanHalfFov( out.cube ? 1.0f : MAX(0.00001f,ShadowMapScene_NarrowSpotTanHalfFov(light.stopdot2)), DX12_SHADOW_LOCAL_SLOT_SIZE, DX12_SHADOW_LOCAL_SLOT_USEFUL );
	out.planeCount = out.cube ? 0 : 6;
	if ( out.cube )
	{
		out.mins = light.origin - Vector(out.zFar,out.zFar,out.zFar);
		out.maxs = light.origin + Vector(out.zFar,out.zFar,out.zFar);
		return;
	}
	Vector forward = light.normal, right, up; VectorNormalize( forward );
	ShadowMapScene_SunBasis( forward, right, up );
	Vector normals[6] = { forward, -forward, forward*out.tanRenderedHalfFov+right, forward*out.tanRenderedHalfFov-right, forward*out.tanRenderedHalfFov+up, forward*out.tanRenderedHalfFov-up };
	for ( int i = 0; i < 6; ++i )
	{
		VectorNormalize( normals[i] );
		out.planes[i].m_Normal = normals[i];
		out.planes[i].m_Dist = DotProduct( normals[i], light.origin );
	}
	out.planes[0].m_Dist += out.zNear;
	out.planes[1].m_Dist -= out.zFar;
	out.mins.Init( FLT_MAX, FLT_MAX, FLT_MAX ); out.maxs.Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );
	for ( int d = 0; d < 2; ++d ) for ( int x = -1; x <= 1; x += 2 ) for ( int y = -1; y <= 1; y += 2 )
	{
		float depth = d ? out.zFar : out.zNear;
		Vector corner = light.origin + (forward + (right*x+up*y)*out.tanRenderedHalfFov)*depth;
		VectorMin( out.mins, corner, out.mins ); VectorMax( out.maxs, corner, out.maxs );
	}
}

void ShadowMapScene_SunDomain( const Vector &mins, const Vector &maxs, ShadowMapInfluenceVolume_t &out )
{
	out.mins = mins; out.maxs = maxs; out.planeCount = 0;
	out.zNear = 0; out.zFar = 0; out.cube = false; out.tanRenderedHalfFov = 0;
}

bool ShadowMapScene_VolumeIntersectsBox( const ShadowMapInfluenceVolume_t &v, const Vector &mins, const Vector &maxs )
{
	for ( int a = 0; a < 3; ++a ) if ( maxs[a] < v.mins[a] || mins[a] > v.maxs[a] ) return false;
	for ( int p = 0; p < v.planeCount; ++p )
	{
		const Vector &n = v.planes[p].m_Normal;
		Vector support( n.x >= 0 ? maxs.x : mins.x, n.y >= 0 ? maxs.y : mins.y, n.z >= 0 ? maxs.z : mins.z );
		if ( DotProduct(n,support) < v.planes[p].m_Dist ) return false;
	}
	return true;
}

bool ShadowMapScene_SelectedLightFromWorldLight( const ShadowMapLightDisk &record, uint32 lightIndex, DX12LightingSelectedLight &out )
{
	const dworldlight_t &light = record.light;
	memset( &out, 0, sizeof(out) );
	if ( light.type != emit_point && light.type != emit_spotlight && light.type != emit_skylight ) return false;
	out.lightId = lightIndex;
	out.type = light.type == emit_skylight ? DX12_SHADOW_LIGHT_SUN : light.type == emit_point ? DX12_SHADOW_LIGHT_POINT : DX12_SHADOW_LIGHT_SPOT;
	out.style = light.style;
	out.origin[0] = light.origin.x; out.origin[1] = light.origin.y; out.origin[2] = light.origin.z;
	out.direction[0] = light.normal.x; out.direction[1] = light.normal.y; out.direction[2] = light.normal.z;
	out.radiance[0] = light.intensity.x; out.radiance[1] = light.intensity.y; out.radiance[2] = light.intensity.z;
	out.constantAttn = light.constant_attn; out.linearAttn = light.linear_attn; out.quadraticAttn = light.quadratic_attn; out.exponent = light.exponent;
	out.innerConeCos = light.stopdot; out.outerConeCos = light.stopdot2; out.attenuationRadius = light.radius;
	out.startFade = record.startFade; out.endFade = record.endFade; out.capDist = record.capDist;
	out.shadowSourceRadius = record.shadowSourceRadius; out.shadowSunAngularRadius = record.shadowSunAngularRadius;
	return true;
}
