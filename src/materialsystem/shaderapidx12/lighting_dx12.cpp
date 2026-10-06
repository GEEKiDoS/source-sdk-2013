//========= Copyright Valve Corporation, All rights reserved. ============//
#include "lighting_dx12.h"
#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "hardwareconfig_dx12.h"
#include "shader_vcs_dx12.h"
#include "shadowmap_bsp.h"
#include "filesystem.h"
#include "materialsystem/stdshaders/common_hlsl_cpp_consts.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/ishadersystem_declarations.h"
#include "tier2/tier2.h"
#include "tier1/callqueue.h"
#include "tier1/utlmap.h"
#include "tier1/strtools.h"
#include <atomic>
#include <cmath>
#include <climits>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <map>
#include <memory>
#include <vector>

namespace shaderapidx12
{
namespace
{
enum LightingOp { View, Pass, EndPass, Restore, EndScope, Unload };
const char *const kPacketError = "Shadowmaps: invalid lighting packet";
D3D12_CPU_DESCRIPTOR_HANDLE Offset( D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT index, UINT stride )
{
	handle.ptr += SIZE_T( index ) * stride;
	return handle;
}
bool RectFits( int x, int y, int width, int height, int fullWidth, int fullHeight )
{
	return x >= 0 && y >= 0 && width > 0 && height > 0 && width <= fullWidth && height <= fullHeight && x <= fullWidth - width && y <= fullHeight - height;
}
struct ShadowTargetDX12 final : IRefCounted
{
	std::atomic<int> references{ 1 };
	DX12ShadowTarget_t identity = 0;
	std::atomic<uint64_t> fence{ 0 };
	bool destroying = false, initialized = false;
	int width = 0, height = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap, srvHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE dsv{}, srv{};
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	int AddRef() override { return ++references; }
	int Release() override
	{
		const int count = --references;
		if ( !count ) delete this;
		return count;
	}
};
struct LightingStatusDX12
{
	DX12LightingStatus state = DX12_LIGHTING_STATUS_PENDING;
	bool completed = false;
	CUtlString error;
};
// A receiver association is a geometric proof, never a colour/content association.
// All fuzzy comparisons below bound float representation error; they do not select
// a nearest face. Every candidate survives until its entire induced field is checked.
bool ReceiverNear( double a, double b )
{
	return std::abs( a - b ) <= 8.0 * FLT_EPSILON * MAX( 1.0, std::abs( a ) + std::abs( b ) );
}
struct ReceiverVertexDX12 { double position[3], uv[2]; };
struct ReceiverBoxDX12
{
	double lo[3]{ DBL_MAX, DBL_MAX, DBL_MAX }, hi[3]{ -DBL_MAX, -DBL_MAX, -DBL_MAX };
	void Add( const float *p ) { for ( int i = 0; i < 3; ++i ) { lo[i] = MIN( lo[i], double( p[i] ) ); hi[i] = MAX( hi[i], double( p[i] ) ); } }
	bool Intersects( const ReceiverBoxDX12 &b ) const
	{
		for ( int i = 0; i < 3; ++i )
			if ( ( hi[i] < b.lo[i] && !ReceiverNear( hi[i], b.lo[i] ) ) || ( b.hi[i] < lo[i] && !ReceiverNear( b.hi[i], lo[i] ) ) ) return false;
		return true;
	}
};
struct ReceiverNodeDX12 { ReceiverBoxDX12 box; int left = -1, right = -1; uint32 face = 0; };
struct ReceiverPlacementDX12
{
	uint32 face = 0;
	int x = 0, y = 0, a = 1, b = 0, c = 0, d = 1;
	int left = 0, top = 0, width = 0, height = 0;
};
struct ReceiverPageDX12
{
	int width = 0, height = 0;
	std::vector<uint32> pixels; // allocation identity (high 24 bits), R8 visibility (low 8)
	std::vector<ReceiverPlacementDX12> placements;
	std::map<uint32, size_t> placementByFace;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
	bool initialized = false;
	DescriptorRangeDX12 table{};
	uint64_t tableFence = 0, fence = 0;
};
struct ReceiverDrawProofDX12
{
	std::shared_ptr<ReceiverPageDX12> page;
	std::vector<std::array<float, 2>> coordinates;
	SunReceiverCoordinatesDX12 Coordinates() const
	{
		return { coordinates.empty() ? nullptr : coordinates.front().data(), uint32( coordinates.size() ) };
	}
};
struct ReceiverReferenceVertexDX12
{
	std::array<float, 2> luxel;
	std::array<float, 3> normal{};
	uint8 normalState = 0; // 0: unseen, 1: proven base normal, 2: ambiguous.
};
// Values, rather than buffer pointers alone, prevent address reuse from resurrecting
// a stale proof. Layout elements are serialized fieldwise, with no padding bytes.
using ReceiverDrawKeyDX12 = std::array<uint64_t, 40>;
struct ReceiverMapDX12
{
	uint32 generation = 0;
	uint64_t fence = 0;
	bool retired = false;
	std::vector<ShadowMapReceiverFaceDisk> faces;
	std::vector<ShadowMapReceiverTriangleDisk> triangles;
	std::vector<DX12LightingReceiverQuad> quads;
	std::vector<unsigned char> visibility, mapped;
	std::vector<ReceiverBoxDX12> boxes;
	std::vector<ReceiverNodeDX12> nodes;
	std::vector<std::map<std::array<float, 3>, ReceiverReferenceVertexDX12>> referenceVertices;
	std::map<ShaderAPITextureHandle_t, std::shared_ptr<ReceiverPageDX12>> pages;
	std::map<ReceiverDrawKeyDX12, ReceiverDrawProofDX12> draws;
	std::map<uint64_t, uint64_t> bufferVersions;
	DX12LightingSunVisibilityStats stats{};
	int BuildNode( std::vector<uint32> &order, size_t begin, size_t end )
	{
		const int index = int( nodes.size() );
		nodes.emplace_back();
		ReceiverBoxDX12 box;
		for ( size_t i = begin; i < end; ++i ) for ( int axis = 0; axis < 3; ++axis )
		{
			box.lo[axis] = MIN( box.lo[axis], boxes[order[i]].lo[axis] );
			box.hi[axis] = MAX( box.hi[axis], boxes[order[i]].hi[axis] );
		}
		nodes[index].box = box;
		if ( end - begin == 1 ) { nodes[index].face = order[begin]; return index; }
		int axis = 0;
		for ( int i = 1; i < 3; ++i ) if ( box.hi[i] - box.lo[i] > box.hi[axis] - box.lo[axis] ) axis = i;
		const size_t middle = begin + ( end - begin ) / 2;
		std::nth_element( order.begin() + begin, order.begin() + middle, order.begin() + end, [&]( uint32 a, uint32 b ) { return boxes[a].lo[axis] + boxes[a].hi[axis] < boxes[b].lo[axis] + boxes[b].hi[axis]; } );
		const int left = BuildNode( order, begin, middle ), right = BuildNode( order, middle, end );
		nodes[index].left = left; nodes[index].right = right;
		return index;
	}
	void Candidates( int node, const ReceiverBoxDX12 &box, std::vector<uint32> &out ) const
	{
		if ( !nodes[node].box.Intersects( box ) ) return;
		if ( nodes[node].left == -1 ) { out.push_back( nodes[node].face ); return; }
		Candidates( nodes[node].left, box, out ); Candidates( nodes[node].right, box, out );
	}
};
bool ValidateReceiverSpans( const DX12LightingMapDesc &map )
{
	if ( map.sunLightIndex < 0 ) return !map.receiverFaceCount && !map.receiverTriangleCount && !map.sunVisibilityCount && !map.receiverQuadCount;
	if ( !map.receiverFaceCount || map.receiverFaceCount > MAX_MAP_FACES || !map.receiverFaces ||
	     !map.receiverTriangleCount || map.receiverTriangleCount > 16u * 1024u * 1024u || !map.receiverTriangles ||
	     !map.sunVisibilityCount || map.sunVisibilityCount > MAX_MAP_LIGHTING || !map.sunVisibility ) return false;
	if ( map.receiverQuadCount > map.receiverFaceCount || (map.receiverQuadCount && !map.receiverQuads) ) return false;
	for ( uint32 i = 0; i < map.receiverQuadCount; ++i )
	{
		const auto &q = map.receiverQuads[i];
		if ( q.faceIndex >= map.receiverFaceCount || (i && q.faceIndex <= map.receiverQuads[i-1].faceIndex) ||
			q.gridSize < 2 || q.gridSize > 17 || ((q.gridSize-1)&(q.gridSize-2)) ||
			map.receiverFaces[q.faceIndex].modelIndex || !(map.receiverFaces[q.faceIndex].flags&SHADOWMAP_RECEIVER_DISPLACEMENT) ) return false;
		for ( const auto &p : q.position ) for ( float value : p ) if ( !ShadowMap_IsFiniteFloat( value ) ) return false;
		int drop = 0;
		for ( int a = 1; a < 3; ++a )
			if ( std::abs( map.receiverFaces[q.faceIndex].plane[a] ) > std::abs( map.receiverFaces[q.faceIndex].plane[drop] ) ) drop = a;
		const int x = (drop+1)%3, y = (drop+2)%3;
		double sign = 0;
		for ( int v = 0; v < 4; ++v )
		{
			const auto &a = q.position[v], &b = q.position[(v+1)%4], &c = q.position[(v+2)%4];
			const double side = (double(b[x])-a[x])*(double(c[y])-b[y])-(double(b[y])-a[y])*(double(c[x])-b[x]);
			if ( side == 0 || (v && (side > 0) != (sign > 0)) ) return false;
			sign = side;
		} // A convex projected quad has a unique bilinear inverse in [0,1]^2.
	}
	uint64_t scalar = 0, triangle = 0, previousLightingEnd = 0;
	for ( uint32 i = 0; i < map.receiverFaceCount; ++i )
	{
		const auto &f = map.receiverFaces[i];
		if ( f.dfaceIndex >= MAX_MAP_FACES || ( i && f.dfaceIndex <= map.receiverFaces[i - 1].dfaceIndex ) ||
		     f.modelIndex >= MAX_MAP_MODELS || f.reserved || ( f.flags & ~( SHADOWMAP_RECEIVER_BUMPED | SHADOWMAP_RECEIVER_DISPLACEMENT ) ) ||
		     !f.luxelW || !f.luxelH || f.luxelW > 16384 || f.luxelH > 16384 ||
		     f.numChannels != ( ( f.flags & SHADOWMAP_RECEIVER_BUMPED ) ? 4u : 1u ) || !f.numStyles || f.numStyles > 4 ||
		     f.firstSunVisibility != scalar || f.firstTriangle != triangle || !f.triangleCount ) return false;
		if ( ( f.lightingOffset & 3u ) || f.lightingOffset < f.numStyles * 4u ||
		     f.lightingOffset - f.numStyles * 4u < previousLightingEnd ) return false;
		previousLightingEnd = uint64_t( f.lightingOffset ) + uint64_t( f.luxelW ) * f.luxelH * f.numChannels * f.numStyles * 4;
		if ( previousLightingEnd > MAX_MAP_LIGHTING ) return false;
		scalar += uint64_t( f.luxelW ) * f.luxelH; triangle += f.triangleCount;
		if ( scalar > map.sunVisibilityCount || triangle > map.receiverTriangleCount ) return false;
		double norm = 0;
		for ( int j = 0; j < 4; ++j )
		{
			if ( !ShadowMap_IsFiniteFloat( f.plane[j] ) ) return false;
			if ( j < 3 ) norm += double( f.plane[j] ) * f.plane[j];
			for ( int a = 0; a < 2; ++a ) if ( !ShadowMap_IsFiniteFloat( f.worldToLuxel[a][j] ) ) return false;
		}
		if ( norm < 0.99 || norm > 1.01 ) return false; // Same nominal-plane contract as the shared BSP validator.
		for ( uint32 t = f.firstTriangle; t < triangle; ++t ) for ( int v = 0; v < 3; ++v )
		{
			const auto &p = map.receiverTriangles[t];
			for ( int a = 0; a < 3; ++a ) if ( !ShadowMap_IsFiniteFloat( p.position[v][a] ) ) return false;
			for ( int a = 0; a < 2; ++a )
			{
				if ( !ShadowMap_IsFiniteFloat( p.luxel[v][a] ) ) return false;
				if ( f.flags & SHADOWMAP_RECEIVER_DISPLACEMENT )
				{
					if ( p.luxel[v][a] < 0 || p.luxel[v][a] > float( ( a ? f.luxelH : f.luxelW ) - 1 ) ) return false;
				}
				else
				{
					double value = f.worldToLuxel[a][3], magnitude = std::abs( value );
					for ( int j = 0; j < 3; ++j )
					{
						const double term = double( p.position[v][j] ) * f.worldToLuxel[a][j];
						value += term; magnitude += std::abs( term );
					}
					if ( std::abs( value - p.luxel[v][a] ) > 16.0 * FLT_EPSILON * ( magnitude + std::abs( double( p.luxel[v][a] ) ) + 1.0 ) ) return false;
				}
			}
		}
	}
	return scalar == map.sunVisibilityCount && triangle == map.receiverTriangleCount;
}
struct ReceiverPoint2DX12 { double x, y; };
using ReceiverPolygonDX12 = std::vector<ReceiverPoint2DX12>;
double ReceiverSide( const ReceiverPoint2DX12 &a, const ReceiverPoint2DX12 &b, const ReceiverPoint2DX12 &p )
{
	return ( b.x - a.x ) * ( p.y - a.y ) - ( b.y - a.y ) * ( p.x - a.x );
}
double ReceiverArea( const ReceiverPolygonDX12 &p )
{
	double area = 0;
	for ( size_t i = 0; i < p.size(); ++i ) { const auto &a = p[i], &b = p[( i + 1 ) % p.size()]; area += a.x * b.y - a.y * b.x; }
	return std::abs( area ) * 0.5;
}
void ReceiverSplit( const ReceiverPolygonDX12 &polygon, const ReceiverPoint2DX12 &a, const ReceiverPoint2DX12 &b, double sign, ReceiverPolygonDX12 &inside, ReceiverPolygonDX12 &outside )
{
	if ( polygon.empty() ) return;
	auto previous = polygon.back();
	double prior = ReceiverSide( a, b, previous ) * sign;
	for ( const auto &point : polygon )
	{
		const double next = ReceiverSide( a, b, point ) * sign;
		if ( ( prior < 0 ) != ( next < 0 ) )
		{
			const double weight = prior / ( prior - next );
			ReceiverPoint2DX12 split{ previous.x + weight * ( point.x - previous.x ), previous.y + weight * ( point.y - previous.y ) };
			inside.push_back( split ); outside.push_back( split );
		}
		( next >= 0 ? inside : outside ).push_back( point );
		previous = point; prior = next;
	}
}
// Distance is used only to REJECT an unplaced displacement that could own a
// fragment. It never chooses a receiver or supplies coordinates for admission.
double ReceiverTriangleDistanceSquared( const double *point, const ShadowMapReceiverTriangleDisk &triangle )
{
	double result = DBL_MAX, plane[3];
	for ( int a = 0; a < 3; ++a )
		plane[a] = (double( triangle.position[1][(a+1)%3] )-triangle.position[0][(a+1)%3]) *
			(double( triangle.position[2][(a+2)%3] )-triangle.position[0][(a+2)%3]) -
			(double( triangle.position[1][(a+2)%3] )-triangle.position[0][(a+2)%3]) *
			(double( triangle.position[2][(a+1)%3] )-triangle.position[0][(a+1)%3]);
	for ( int edge = 0; edge < 3; ++edge )
	{
		double delta[3], length = 0, along = 0;
		for ( int a = 0; a < 3; ++a )
		{
			delta[a] = double( triangle.position[(edge+1)%3][a] )-triangle.position[edge][a];
			length += delta[a]*delta[a];
			along += (point[a]-triangle.position[edge][a])*delta[a];
		}
		const double t = length > 0 ? MAX( 0.0, MIN( 1.0, along/length ) ) : 0;
		double distance = 0;
		for ( int a = 0; a < 3; ++a )
		{
			const double value = point[a]-triangle.position[edge][a]-t*delta[a];
			distance += value*value;
		}
		result = MIN( result, distance );
	}
	double length = 0, distance = 0;
	int drop = 0;
	for ( int a = 0; a < 3; ++a )
	{
		length += plane[a]*plane[a];
		distance += plane[a]*(point[a]-triangle.position[0][a]);
		if ( std::abs( plane[a] ) > std::abs( plane[drop] ) ) drop = a;
	}
	if ( length > 0 )
	{
		const int x = (drop+1)%3, y = (drop+2)%3;
		const ReceiverPoint2DX12 projected{ point[x]-plane[x]*distance/length, point[y]-plane[y]*distance/length };
		bool inside = true;
		for ( int edge = 0; edge < 3; ++edge )
		{
			const ReceiverPoint2DX12 a{ triangle.position[edge][x], triangle.position[edge][y] };
			const ReceiverPoint2DX12 b{ triangle.position[(edge+1)%3][x], triangle.position[(edge+1)%3][y] };
			inside = inside && ReceiverSide( a, b, projected )*(plane[drop] > 0 ? 1 : -1) >= 0;
		}
		if ( inside ) result = MIN( result, distance*distance/length );
	}
	return result;
}
// Source clips displacement decals after lifting each reference vertex by
// 0.1 * its smoothed base normal. The decal's flat normal is not that field.
// Prove one corner or the entire fragment on a lifted triangle; never project
// to a nearest surface or extend allocation ownership into foreign blocks.
bool ReceiverDecalTriangle( const ReceiverVertexDX12 *fragment, int count, const double ( &raised )[3][3],
	const ShadowMapReceiverTriangleDisk &reference, double ( *luxel )[2] )
{
	double plane[3];
	for ( int a = 0; a < 3; ++a )
		plane[a] = ( raised[1][(a+1)%3]-raised[0][(a+1)%3] ) * ( raised[2][(a+2)%3]-raised[0][(a+2)%3] ) -
			( raised[1][(a+2)%3]-raised[0][(a+2)%3] ) * ( raised[2][(a+1)%3]-raised[0][(a+1)%3] );
	int drop = 0;
	for ( int a = 1; a < 3; ++a ) if ( std::abs( plane[a] ) > std::abs( plane[drop] ) ) drop = a;
	if ( plane[drop] == 0 ) return false;
	for ( int v = 0; v < count; ++v )
	{
		double distance = 0, magnitude = 0;
		for ( int a = 0; a < 3; ++a )
		{
			distance += plane[a] * ( fragment[v].position[a]-raised[0][a] );
			magnitude += std::abs( plane[a] ) * ( std::abs( fragment[v].position[a] )+std::abs( raised[0][a] ) );
		}
		if ( std::abs( distance ) > 8.0 * FLT_EPSILON * MAX( 1.0, magnitude ) ) return false;
	}
	const int x = (drop+1)%3, y = (drop+2)%3;
	ReceiverPoint2DX12 clip[3], points[3];
	for ( int v = 0; v < 3; ++v ) clip[v] = { raised[v][x], raised[v][y] };
	for ( int v = 0; v < count; ++v ) points[v] = { fragment[v].position[x], fragment[v].position[y] };
	const double winding = ReceiverSide( clip[0], clip[1], clip[2] );
	if ( winding == 0 || (count == 3 && ReceiverSide( points[0], points[1], points[2] ) == 0) ) return false;
	// These endpoints are float32 clip results, not exact reference endpoints.
	// Propagate coordinate representation error through each edge determinant;
	// an area-relative allowance incorrectly vanishes for thin fragments.
	// A triangle is convex, so these half-planes prove its entire footprint.
	for ( int edge = 0; edge < 3; ++edge )
	{
		const auto &a = clip[edge], &b = clip[(edge+1)%3];
		const double dx = b.x-a.x, dy = b.y-a.y;
		for ( int v = 0; v < count; ++v )
		{
			const auto &point = points[v];
			const double magnitude = std::abs( dx )*MAX( 1.0, std::abs( point.y )+std::abs( a.y ) ) +
				std::abs( dy )*MAX( 1.0, std::abs( point.x )+std::abs( a.x ) );
			if ( ReceiverSide( a, b, point )*(winding > 0 ? 1 : -1) < -8.0*FLT_EPSILON*magnitude ) return false;
		}
	}
	for ( int v = 0; v < count; ++v )
	{
		const ReceiverPoint2DX12 point{ fragment[v].position[x], fragment[v].position[y] };
		const double b = ReceiverSide( clip[2], clip[0], point ) / winding;
		const double c = ReceiverSide( clip[0], clip[1], point ) / winding;
		for ( int a = 0; a < 2; ++a )
			luxel[v][a] = (1-b-c)*reference.luxel[0][a] + b*reference.luxel[1][a] + c*reference.luxel[2][a];
	}
	return true;
}
// Source's displacement-overlay area test accepts an unsigned weight sum
// within .001 of one, but does not normalize it before weighting WORLD
// positions. This is an engine arithmetic rule, not a geometry tolerance.
constexpr float ReceiverSourceAreaSlack = .001f;
float ReceiverFloatProduct( float a, float b )
{
	const volatile float product = a*b;
	return product;
}
void ReceiverFlatQuadPoint( const DX12LightingReceiverQuad &quad, float u, float v, float ( &point )[3] )
{
	for ( int a = 0; a < 3; ++a )
	{
		const float left = ReceiverFloatProduct( quad.position[3][a]-quad.position[0][a], v )+quad.position[0][a];
		const float right = ReceiverFloatProduct( quad.position[2][a]-quad.position[1][a], v )+quad.position[1][a];
		point[a] = ReceiverFloatProduct( right-left, u )+left;
	}
}
float ReceiverSourceArea( const float *a, const float *b, const float *c )
{
	float ab[3], ac[3], n[3];
	for ( int k = 0; k < 3; ++k ) { ab[k] = b[k]-a[k]; ac[k] = c[k]-a[k]; }
	for ( int k = 0; k < 3; ++k )
		n[k] = ReceiverFloatProduct( ab[(k+1)%3], ac[(k+2)%3] )-ReceiverFloatProduct( ab[(k+2)%3], ac[(k+1)%3] );
	const float xy = ReceiverFloatProduct( n[0], n[0] )+ReceiverFloatProduct( n[1], n[1] );
	return std::sqrt( xy+ReceiverFloatProduct( n[2], n[2] ) )*.5f;
}
void ReceiverCross( const double *a, const double *b, double *result )
{
	for ( int i = 0; i < 3; ++i )
		result[i] = a[(i+1)%3]*b[(i+2)%3]-a[(i+2)%3]*b[(i+1)%3];
}
bool ReceiverSourceGridTriangle( const DX12LightingReceiverQuad &quad, float u, float v,
	const ReceiverPoint2DX12 ( &grid )[3] )
{
	const float extent = float(quad.gridSize)-1.000001f;
	const float gx = ReceiverFloatProduct( extent, u ), gy = ReceiverFloatProduct( extent, v );
	const int x = int(gx), y = int(gy);
	const ReceiverPoint2DX12 corners[4] = { {double(x),double(y)}, {double(x+1),double(y)},
		{double(x+1),double(y+1)}, {double(x),double(y+1)} };
	int choices[2][3];
	if ( (x+int(quad.gridSize)*y)&1 )
	{
		const int lower[3] = {0,3,1}, upper[3] = {3,2,1};
		const bool low = (gy-float(y))+(gx-float(x)) < 1.000001f;
		std::copy( low ? lower : upper, (low ? lower : upper)+3, choices[0] );
		std::copy( low ? upper : lower, (low ? upper : lower)+3, choices[1] );
	}
	else
	{
		const int lower[3] = {0,2,1}, upper[3] = {0,3,2};
		const bool low = gx-float(x) >= gy-float(y);
		std::copy( low ? lower : upper, (low ? lower : upper)+3, choices[0] );
		std::copy( low ? upper : lower, (low ? upper : lower)+3, choices[1] );
	}
	for ( int choice = 0; choice < 2; ++choice )
	{
		bool matches = true;
		for ( const auto &p : grid )
		{
			bool vertex = false;
			for ( int index : choices[choice] )
				vertex = vertex || (p.x == corners[index].x && p.y == corners[index].y);
			matches = matches && vertex;
		}
		if ( !matches ) continue;
		if ( choice == 0 ) return true;
		// Source tries the other triangle only when the primary flat-area
		// test fails. Grid containment alone misses this nonparallelogram case.
		float primary[3][3], query[3];
		for ( int i = 0; i < 3; ++i )
			ReceiverFlatQuadPoint( quad, float(corners[choices[0][i]].x)/float(quad.gridSize-1),
				float(corners[choices[0][i]].y)/float(quad.gridSize-1), primary[i] );
		ReceiverFlatQuadPoint( quad, u, v, query );
		const float area = ReceiverSourceArea( primary[0], primary[1], primary[2] );
		if ( !(area > 0) ) return false;
		const float inverse = 1.f/area;
		const float a = ReceiverFloatProduct( ReceiverSourceArea( query, primary[1], primary[2] ), inverse );
		const float b = ReceiverFloatProduct( ReceiverSourceArea( primary[0], query, primary[2] ), inverse );
		const float c = ReceiverFloatProduct( ReceiverSourceArea( primary[0], primary[1], query ), inverse );
		return !(std::abs( 1.f-((a+b)+c) ) < ReceiverSourceAreaSlack);
	}
	return false;
}
// Inversion generates candidates only. Admission requires replaying the
// original flat-quad/grid choice, unsigned-area test and float32 world output.
// No closest point, enlarged surface, or normalized-world-position fallback.
int ReceiverUnsignedDecalPoint( const ReceiverVertexDX12 &point, const ShadowMapReceiverFaceDisk &face,
	const ShadowMapReceiverTriangleDisk &triangle, const DX12LightingReceiverQuad &quad,
	const double ( &raised )[3][3], double *label, double *labelError )
{
	if ( face.luxelW < 2 || face.luxelH < 2 ) return 0;
	double outputScale[3];
	for ( int a = 0; a < 3; ++a )
		outputScale[a] = 8.0*FLT_EPSILON*MAX( 1.0, std::abs(point.position[a])+
			(1.0+ReceiverSourceAreaSlack)*MAX( std::abs(raised[0][a]), MAX(std::abs(raised[1][a]),std::abs(raised[2][a])) ) );
	float flat[3][3];
	ReceiverPoint2DX12 grid[3];
	for ( int i = 0; i < 3; ++i )
	{
		const float u = triangle.luxel[i][0]/float( face.luxelW-1 ), v = triangle.luxel[i][1]/float( face.luxelH-1 );
		ReceiverFlatQuadPoint( quad, u, v, flat[i] );
		grid[i] = { double(u)*(quad.gridSize-1), double(v)*(quad.gridSize-1) };
	}
	const float area = ReceiverSourceArea( flat[0], flat[1], flat[2] );
	const double winding = ReceiverSide( grid[0], grid[1], grid[2] );
	if ( !(area > 0) || !std::isfinite( area ) || winding == 0 ) return 0;
	const float inverseArea = 1.f/area;
	int drop = 0;
	for ( int a = 1; a < 3; ++a ) if ( std::abs( face.plane[a] ) > std::abs( face.plane[drop] ) ) drop = a;
	const int x = (drop+1)%3, y = (drop+2)%3;
	double b[3], c[3], d[3];
	for ( int a = 0; a < 3; ++a )
	{
		b[a] = double(quad.position[1][a])-quad.position[0][a];
		c[a] = double(quad.position[3][a])-quad.position[0][a];
		d[a] = double(quad.position[0][a])-quad.position[1][a]+quad.position[2][a]-quad.position[3][a];
	}
	bool found = false;
	// Signed flat barycentrics sum to one. Solve each unsigned-area branch
	// under that constraint, weighted by world-output float precision.
	// Homogeneous inversion then normalization loses this constraint at
	// clipped grid edges, especially when world XY dwarfs the height.
	for ( int signs = 0; signs < 7; ++signs )
	{
		double axes[2][3], target[3], plane[3];
		for ( int a = 0; a < 3; ++a )
		{
			const double origin = (signs&1) ? -raised[0][a] : raised[0][a];
			target[a] = (point.position[a]-origin)/outputScale[a];
			for ( int i = 0; i < 2; ++i )
				axes[i][a] = (((signs&(2<<i)) ? -raised[i+1][a] : raised[i+1][a])-origin)/outputScale[a];
		}
		ReceiverCross( axes[0], axes[1], plane );
		const double length = plane[0]*plane[0]+plane[1]*plane[1]+plane[2]*plane[2];
		if ( !(length > 0) || !std::isfinite(length) ) continue;
		double inverse[2][3], beta[2] = {}, betaError[2] = {};
		ReceiverCross( axes[1], plane, inverse[0] );
		ReceiverCross( plane, axes[0], inverse[1] );
		for ( int i = 0; i < 2; ++i ) for ( int a = 0; a < 3; ++a )
		{
			inverse[i][a] /= length;
			beta[i] += inverse[i][a]*target[a];
			betaError[i] += std::abs(inverse[i][a]);
		}
		double query[3], queryError[3];
		for ( int a = 0; a < 3; ++a )
		{
			query[a] = flat[0][a];
			queryError[a] = 0;
			for ( int i = 0; i < 2; ++i )
			{
				const double delta = double(flat[i+1][a])-flat[0][a];
				query[a] += beta[i]*delta;
				queryError[a] += betaError[i]*std::abs(delta);
			}
			queryError[a] += 8.0*FLT_EPSILON*MAX( 1.0, std::abs(query[a])+std::abs(double(quad.position[0][a])) );
		}
		double u = .5, v = .5;
		bool invertible = true;
		for ( int iteration = 0; iteration < 12; ++iteration )
		{
			const double rx = quad.position[0][x]+b[x]*u+c[x]*v+d[x]*u*v-query[x];
			const double ry = quad.position[0][y]+b[y]*u+c[y]*v+d[y]*u*v-query[y];
			const double ux = b[x]+d[x]*v, uy = b[y]+d[y]*v, vx = c[x]+d[x]*u, vy = c[y]+d[y]*u;
			const double det = ux*vy-uy*vx;
			if ( det == 0 ) { invertible = false; break; }
			const double du = (rx*vy-ry*vx)/det, dv = (ux*ry-uy*rx)/det;
			u -= du; v -= dv;
			if ( std::abs( du )+std::abs( dv ) <= 8*DBL_EPSILON ) break;
		}
		if ( !invertible || !std::isfinite( u ) || !std::isfinite( v ) ) continue;
		const double ux = b[x]+d[x]*v, uy = b[y]+d[y]*v, vx = c[x]+d[x]*u, vy = c[y]+d[y]*u;
		const double jacobian = std::abs( ux*vy-uy*vx );
		if ( jacobian == 0 ) continue;
		const double uncertainty[2] = {
			(std::abs(vy)*queryError[x]+std::abs(vx)*queryError[y])/jacobian*(face.luxelW-1),
			(std::abs(uy)*queryError[x]+std::abs(ux)*queryError[y])/jacobian*(face.luxelH-1) };
		if ( !std::isfinite(uncertainty[0]) || !std::isfinite(uncertainty[1]) ||
			uncertainty[0] >= .25 || uncertainty[1] >= .25 ) continue;
		const float extent = float(quad.gridSize)-1.000001f;
		const float seedU = float(MAX(0.0,MIN(1.0,u))), seedV = float(MAX(0.0,MIN(1.0,v)));
		const ReceiverPoint2DX12 selected{ ReceiverFloatProduct(extent,seedU), ReceiverFloatProduct(extent,seedV) };
		auto accept = [&]( float fu, float fv ) -> int
		{
			if ( !ReceiverSourceGridTriangle( quad, fu, fv, grid ) ) return 0;
			float flatQuery[3];
			ReceiverFlatQuadPoint( quad, fu, fv, flatQuery );
			const float absolute[3] = {
				ReceiverFloatProduct( ReceiverSourceArea( flatQuery, flat[1], flat[2] ), inverseArea ),
				ReceiverFloatProduct( ReceiverSourceArea( flat[0], flatQuery, flat[2] ), inverseArea ),
				ReceiverFloatProduct( ReceiverSourceArea( flat[0], flat[1], flatQuery ), inverseArea ) };
			if ( !(std::abs( 1.f-((absolute[0]+absolute[1])+absolute[2]) ) < ReceiverSourceAreaSlack) ) return 0;
			for ( int a = 0; a < 3; ++a )
			{
				const float first = ReceiverFloatProduct( absolute[0], float(raised[0][a]) );
				const float second = ReceiverFloatProduct( absolute[1], float(raised[1][a]) );
				const float third = ReceiverFloatProduct( absolute[2], float(raised[2][a]) );
				const float replay = (first+second)+third;
				const double magnitude = std::abs(double(first))+std::abs(double(second))+std::abs(double(third))+std::abs(point.position[a]);
				if ( !std::isfinite(replay) || std::abs(replay-point.position[a]) > 8.0*FLT_EPSILON*MAX(1.0,magnitude) ) return 0;
			}
			const double candidate[2] = { double(fu)*(face.luxelW-1), double(fv)*(face.luxelH-1) };
			if ( found && (std::abs(candidate[0]-label[0]) > uncertainty[0]+labelError[0] ||
				std::abs(candidate[1]-label[1]) > uncertainty[1]+labelError[1]) ) return -1;
			if ( !found ) for ( int a = 0; a < 2; ++a )
			{ label[a] = candidate[a]; labelError[a] = uncertainty[a]; }
			found = true;
			return 1;
		};
		// Query rounding and flat-world rounding are separate operations in
		// Source. A near-edge inverse may land across the selector boundary.
		// Generate its parameter-edge seeds within the propagated uncertainty;
		// none can admit a point without the unchanged forward replay below.
		int accepted = 0;
		for ( int edge = -1; edge < 3 && !accepted; ++edge )
		for ( int projection = 0; projection < (edge >= 0 ? 3 : 1) && !accepted; ++projection )
		{
			float fu = seedU, fv = seedV;
			double dx = 0, dy = 0;
			if ( edge >= 0 )
			{
				const auto &a = grid[edge], &z = grid[(edge+1)%3];
				dx = z.x-a.x; dy = z.y-a.y;
				const double square = dx*dx+dy*dy;
				if ( square == 0 ) continue;
				double t = ((selected.x-a.x)*dx+(selected.y-a.y)*dy)/square;
				// Odd cells dispatch at fracX+fracY == 1.000001, not 1.
				const double shift = dx*dy < 0 ? .5*(1.000001f-1.f) : 0;
				// Preserve each axis in turn: the orthogonal seed can cross a
				// flat-world rounding bin even though a boundary query exists.
				if ( projection == 1 )
				{ if ( dx == 0 ) continue; t = (selected.x-a.x-shift)/dx; }
				if ( projection == 2 )
				{ if ( dy == 0 ) continue; t = (selected.y-a.y-shift)/dy; }
				t = MAX(0.0,MIN(1.0,t));
				fu = float(MAX(0.0,MIN(1.0,(a.x+t*dx+shift)/extent)));
				fv = float(MAX(0.0,MIN(1.0,(a.y+t*dy+shift)/extent)));
			}
			for ( int rounding = -1; rounding < (edge >= 0 ? 3 : 0) && !accepted; ++rounding )
			{
				float ru = fu, rv = fv;
				const float inwardU = float(-dy*(winding > 0 ? 1 : -1));
				const float inwardV = float(dx*(winding > 0 ? 1 : -1));
				if ( (rounding == 0 || rounding == 2) && inwardU != 0 )
					ru = std::nextafter(fu,fu+inwardU);
				if ( (rounding == 1 || rounding == 2) && inwardV != 0 )
					rv = std::nextafter(fv,fv+inwardV);
				if ( ru < 0 || ru > 1 || rv < 0 || rv > 1 ||
					std::abs(double(ru)-seedU)*(face.luxelW-1) > uncertainty[0] ||
					std::abs(double(rv)-seedV)*(face.luxelH-1) > uncertainty[1] ) continue;
				accepted = accept( ru, rv );
				if ( accepted < 0 ) return -1;
			}
		}
	}
	return found ? 1 : 0;
}
// 0: not a planar displacement decal; 1: unique corrected coordinates;
// -1: the displacement identity/normal/coordinate proof is unresolved.
int ReceiverDisplacementDecal( const ReceiverMapDX12 &map, const ReceiverPageDX12 &page,
	const ReceiverVertexDX12 *fragment, std::array<std::array<float, 2>, 3> &coordinates, std::vector<uint32> &candidates )
{
	bool applicable = false, found = false, missing = false;
	ReceiverBoxDX12 box;
	for ( int v = 0; v < 3; ++v ) for ( int a = 0; a < 3; ++a )
	{
		const double radius = .1*(1+ReceiverSourceAreaSlack+8.0*FLT_EPSILON)+(ReceiverSourceAreaSlack+8.0*FLT_EPSILON)*std::abs( fragment[v].position[a] );
		box.lo[a] = MIN( box.lo[a], fragment[v].position[a]-radius );
		box.hi[a] = MAX( box.hi[a], fragment[v].position[a]+radius );
	}
	candidates.clear();
	map.Candidates( 0, box, candidates );
	for ( uint32 faceIndex : candidates )
	{
		const auto &face = map.faces[faceIndex];
		if ( face.modelIndex || !( face.flags & SHADOWMAP_RECEIVER_DISPLACEMENT ) ) continue;
		const auto quadIt = std::lower_bound( map.quads.begin(), map.quads.end(), faceIndex,
			[]( const DX12LightingReceiverQuad &q, uint32 index ) { return q.faceIndex < index; } );
		const DX12LightingReceiverQuad *quad = quadIt != map.quads.end() && quadIt->faceIndex == faceIndex ? &*quadIt : nullptr;
		const auto placed = page.placementByFace.find( faceIndex );
		if ( placed == page.placementByFace.end() )
		{
			// Broken planar UVs may land wholly inside somebody else's allocation.
			// A fragment within the engine's normal-lift radius of an unplaced
			// displacement cannot acquire ownership from those foreign texels.
			bool proximate[3] = {};
			for ( uint32 t = face.firstTriangle; t < face.firstTriangle+face.triangleCount; ++t )
			{
				const auto &reference = map.triangles[t];
				for ( int v = 0; v < 3; ++v )
				if ( !proximate[v] )
				{
					double errorSquared = 0;
					for ( int a = 0; a < 3; ++a )
					{
						const double magnitude = MAX( std::abs( double( reference.position[0][a] ) ),
							MAX( std::abs( double( reference.position[1][a] ) ), std::abs( double( reference.position[2][a] ) ) ) );
						const double error = 8.0*FLT_EPSILON*MAX( 1.0, std::abs( fragment[v].position[a] )+magnitude ) +
							(quad ? ReceiverSourceAreaSlack*magnitude : 0);
						errorSquared += error*error;
					}
					const double radius = .1*(1+ReceiverSourceAreaSlack+8.0*FLT_EPSILON)+std::sqrt( errorSquared );
					proximate[v] = ReceiverTriangleDistanceSquared( fragment[v].position, reference ) <= radius*radius;
				}
				if ( proximate[0] && proximate[1] && proximate[2] ) { missing = true; break; }
			}
			continue;
		}
		const auto &placement = page.placements[placed->second];
		bool planar = true;
		for ( int v = 0; v < 3; ++v )
		{
			double label[2], error[2];
			for ( int a = 0; a < 2; ++a )
			{
				label[a] = face.worldToLuxel[a][3];
				double magnitude = std::abs( label[a] );
				for ( int k = 0; k < 3; ++k )
				{
					const double term = fragment[v].position[k]*face.worldToLuxel[a][k];
					label[a] += term; magnitude += std::abs( term );
				}
				error[a] = 8.0*FLT_EPSILON*MAX( 1.0, magnitude );
			}
			const double qx = page.width*fragment[v].uv[0]-.5, qy = page.height*fragment[v].uv[1]-.5;
			const double ex = 8.0*FLT_EPSILON*MAX( 1.0, std::abs( qx+.5 ) ) + std::abs( placement.a )*error[0] + std::abs( placement.b )*error[1];
			const double ey = 8.0*FLT_EPSILON*MAX( 1.0, std::abs( qy+.5 ) ) + std::abs( placement.c )*error[0] + std::abs( placement.d )*error[1];
			planar = planar && ex < .25 && ey < .25 &&
				std::abs( qx-placement.x-placement.a*label[0]-placement.b*label[1] ) <= ex &&
				std::abs( qy-placement.y-placement.c*label[0]-placement.d*label[1] ) <= ey;
		}
		if ( !planar ) continue;
		applicable = true;
		bool faceFound = false;
		std::array<std::array<float, 2>, 3> candidate;
		// Prefer a common supporting triangle. Source also submits decals whose
		// corners span noncoplanar triangles: reconstruct those corners on this
		// same face, then let rectangular allocation ownership prove the UV
		// interior. Never assemble one decal from different receiver faces.
		for ( int count = quad ? 1 : 3; count >= 1 && !faceFound; count -= 2 )
		{
			int cornerFound[3] = {}; // 1: direct surface; 2: replayed Source interpolation.
			double cornerError[3][2] = {};
			for ( uint32 t = face.firstTriangle; t < face.firstTriangle+face.triangleCount; ++t )
			{
				const auto &reference = map.triangles[t];
				bool possible[3] = { true, true, true };
				for ( int a = 0; a < 3; ++a )
				{
					double lo = MIN( reference.position[0][a], MIN( reference.position[1][a], reference.position[2][a] ) )-.1;
					double hi = MAX( reference.position[0][a], MAX( reference.position[1][a], reference.position[2][a] ) )+.1;
					if ( quad && count == 1 )
					{
						const double expansion = ReceiverSourceAreaSlack*MAX( std::abs( lo ), std::abs( hi ) );
						lo -= expansion; hi += expansion;
					}
					for ( int v = 0; v < 3; ++v )
						possible[v] = possible[v] && ( fragment[v].position[a] >= lo || ReceiverNear( fragment[v].position[a], lo ) ) &&
							( fragment[v].position[a] <= hi || ReceiverNear( fragment[v].position[a], hi ) );
				}
				if ( count == 3 ? !(possible[0] && possible[1] && possible[2]) : !(possible[0] || possible[1] || possible[2]) ) continue;
				double raised[3][3];
				bool normals = true;
				for ( int v = 0; v < 3; ++v )
				{
					const auto vertex = map.referenceVertices[placement.face].find( { reference.position[v][0], reference.position[v][1], reference.position[v][2] } );
					if ( vertex == map.referenceVertices[placement.face].end() || vertex->second.normalState != 1 )
					{ normals = false; break; }
					for ( int a = 0; a < 3; ++a )
					{
						// Retain the engine's two float operations before polygon clipping.
						const volatile float push = .1f*vertex->second.normal[a];
						raised[v][a] = float( reference.position[v][a]+push );
					}
				}
				if ( !normals ) { missing = true; continue; }
				for ( int first = 0; first < 3; first += count )
				{
					if ( !possible[first] ) continue;
					double label[3][2];
					double labelError[2] = {};
					int proof = 0;
					if ( quad && count == 1 )
					{
						proof = ReceiverUnsignedDecalPoint( fragment[first], face, reference, *quad, raised, label[0], labelError );
						if ( proof < 0 ) return -1;
						if ( proof ) proof = 2;
					}
					if ( !proof && ReceiverDecalTriangle( fragment+first, count, raised, reference, label ) ) proof = 1;
					if ( !proof ) continue;
					for ( int i = 0; i < count; ++i )
					{
						const int v = first+i;
						if ( cornerFound[v] > proof ) continue;
						const float u = float( (placement.x+placement.a*label[i][0]+placement.b*label[i][1]+.5)/page.width );
						const float w = float( (placement.y+placement.c*label[i][0]+placement.d*label[i][1]+.5)/page.height );
						const double error[2] = { std::abs(placement.a)*labelError[0]+std::abs(placement.b)*labelError[1],
							std::abs(placement.c)*labelError[0]+std::abs(placement.d)*labelError[1] };
						if ( cornerFound[v] == proof &&
							((!ReceiverNear( page.width*u, page.width*candidate[v][0] ) &&
								std::abs( page.width*(double(u)-candidate[v][0]) ) > error[0]+cornerError[v][0]) ||
							(!ReceiverNear( page.height*w, page.height*candidate[v][1] ) &&
								std::abs( page.height*(double(w)-candidate[v][1]) ) > error[1]+cornerError[v][1])) )
						{
							Warning( "Sun receiver corner ambiguity: dface=%u triangle=%u corner=%d proof=%d p=(%.9g %.9g %.9g) texels=(%.9g %.9g)/(%.9g %.9g)\n",
								face.dfaceIndex, t-face.firstTriangle, v, proof, fragment[v].position[0], fragment[v].position[1], fragment[v].position[2],
								page.width*candidate[v][0], page.height*candidate[v][1], page.width*u, page.height*w );
							return -1;
						}
						if ( cornerFound[v] < proof )
						{ candidate[v] = { u, w }; cornerError[v][0] = error[0]; cornerError[v][1] = error[1]; }
						cornerFound[v] = proof;
					}
				}
			}
			faceFound = cornerFound[0] && cornerFound[1] && cornerFound[2];
		}
		if ( !faceFound ) continue;
		for ( int v = 0; v < 3; ++v )
			if ( found && ( !ReceiverNear( page.width*candidate[v][0], page.width*coordinates[v][0] ) ||
				!ReceiverNear( page.height*candidate[v][1], page.height*coordinates[v][1] ) ) ) return -1;
		if ( !found ) coordinates = candidate;
		found = true;
	}
	if ( (missing || (applicable && !found)) && !map.stats.unresolvedDraws )
	{
		Warning( "Sun receiver displacement proof: missing=%d found=%d candidates=%u\n", int( missing ), int( found ), uint32( candidates.size() ) );
		for ( int v = 0; v < 3; ++v )
			Warning( "  fragment p=(%.9g %.9g %.9g) texel=(%.9g %.9g)\n", fragment[v].position[0], fragment[v].position[1], fragment[v].position[2],
				page.width*fragment[v].uv[0]-.5, page.height*fragment[v].uv[1]-.5 );
		for ( uint32 f : candidates )
		{
			const auto placed = page.placementByFace.find( f );
			if ( placed == page.placementByFace.end() || !( map.faces[f].flags & SHADOWMAP_RECEIVER_DISPLACEMENT ) ) continue;
			const auto &p = page.placements[placed->second];
			uint32 known = 0, ambiguous = 0;
			for ( const auto &vertex : map.referenceVertices[f] )
			{ known += vertex.second.normalState == 1; ambiguous += vertex.second.normalState == 2; }
			Warning( "  dface=%u rect=(%d,%d,%d,%d) origin=(%d,%d) axes=(%d,%d,%d,%d) normals=%u/%u ambiguous=%u\n",
				map.faces[f].dfaceIndex, p.left, p.top, p.width, p.height, p.x, p.y, p.a, p.b, p.c, p.d, known, uint32( map.referenceVertices[f].size() ), ambiguous );
		}
	}
	return missing || (applicable && !found) ? -1 : found ? 1 : 0;
}
// Subtract the reference triangle union. Testing only the three vertices would
// incorrectly accept triangles crossing a hole or a concave/primitive boundary.
bool ReceiverCovered( const ReceiverMapDX12 &map, const ShadowMapReceiverFaceDisk &face, const ReceiverVertexDX12 ( &v )[3] )
{
	double normal[3];
	for ( int a = 0; a < 3; ++a )
		normal[a] = ( v[1].position[( a + 1 ) % 3] - v[0].position[( a + 1 ) % 3] ) * ( v[2].position[( a + 2 ) % 3] - v[0].position[( a + 2 ) % 3] ) -
		            ( v[1].position[( a + 2 ) % 3] - v[0].position[( a + 2 ) % 3] ) * ( v[2].position[( a + 1 ) % 3] - v[0].position[( a + 1 ) % 3] );
	int drop = 0;
	for ( int a = 1; a < 3; ++a ) if ( std::abs( normal[a] ) > std::abs( normal[drop] ) ) drop = a;
	const int x = ( drop + 1 ) % 3, y = ( drop + 2 ) % 3;
	bool referenceEndpoints = true;
	const auto &lookup = map.referenceVertices[size_t( &face - map.faces.data() )];
	ReceiverPolygonDX12 initial;
	for ( const auto &p : v )
	{
		const std::array<float, 3> key{ float( p.position[0] ), float( p.position[1] ), float( p.position[2] ) };
		referenceEndpoints = referenceEndpoints && lookup.find( key ) != lookup.end();
		initial.push_back( { p.position[x], p.position[y] } );
	}
	const double area = ReceiverArea( initial );
	if ( area == 0 ) return false;
	std::vector<ReceiverPolygonDX12> remainder{ initial };
	for ( uint32 t = face.firstTriangle; t < face.firstTriangle + face.triangleCount && !remainder.empty(); ++t )
	{
		const auto &triangle = map.triangles[t];
		// Stock BSP polygons are not necessarily on their nominal BSP plane.
		// Original reference endpoints establish polygon identity even when the
		// renderer chooses a different triangulation of that nonplanar polygon.
		// Clipped/new endpoints instead require this actual triangle's surface.
		if ( !referenceEndpoints )
		{
			double plane[3];
			for ( int a = 0; a < 3; ++a )
				plane[a] = ( double( triangle.position[1][( a + 1 ) % 3] ) - triangle.position[0][( a + 1 ) % 3] ) * ( double( triangle.position[2][( a + 2 ) % 3] ) - triangle.position[0][( a + 2 ) % 3] ) -
				           ( double( triangle.position[1][( a + 2 ) % 3] ) - triangle.position[0][( a + 2 ) % 3] ) * ( double( triangle.position[2][( a + 1 ) % 3] ) - triangle.position[0][( a + 1 ) % 3] );
			bool coplanar = true;
			for ( const auto &point : v )
			{
				double distance = 0, magnitude = 0;
				for ( int a = 0; a < 3; ++a )
				{
					distance += plane[a] * ( point.position[a] - triangle.position[0][a] );
					magnitude += std::abs( plane[a] ) * ( std::abs( point.position[a] ) + std::abs( double( triangle.position[0][a] ) ) );
				}
				coplanar = coplanar && std::abs( distance ) <= 8.0 * FLT_EPSILON * MAX( 1.0, magnitude );
			}
			if ( !coplanar ) continue;
		}
		ReceiverPoint2DX12 clip[3];
		for ( int i = 0; i < 3; ++i ) clip[i] = { triangle.position[i][x], triangle.position[i][y] };
		const double winding = ReceiverSide( clip[0], clip[1], clip[2] );
		if ( winding == 0 ) continue;
		std::vector<ReceiverPolygonDX12> next;
		for ( const auto &polygon : remainder )
		{
			ReceiverPolygonDX12 inside = polygon;
			for ( int edge = 0; edge < 3 && !inside.empty(); ++edge )
			{
				ReceiverPolygonDX12 kept, outside;
				ReceiverSplit( inside, clip[edge], clip[( edge + 1 ) % 3], winding > 0 ? 1 : -1, kept, outside );
				if ( outside.size() >= 3 && ReceiverArea( outside ) > area * 32.0 * FLT_EPSILON ) next.push_back( std::move( outside ) );
				inside = std::move( kept );
			}
		}
		remainder = std::move( next );
	}
	return remainder.empty();
}
bool ReceiverLuxels( const ReceiverMapDX12 &map, const ShadowMapReceiverFaceDisk &face, const ReceiverVertexDX12 ( &v )[3], double ( &luxel )[3][2], bool &unresolved )
{
	if ( !( face.flags & SHADOWMAP_RECEIVER_DISPLACEMENT ) )
	{
		if ( !ReceiverCovered( map, face, v ) ) return false;
		for ( int p = 0; p < 3; ++p ) for ( int a = 0; a < 2; ++a )
		{
			luxel[p][a] = face.worldToLuxel[a][3];
			for ( int j = 0; j < 3; ++j ) luxel[p][a] += v[p].position[j] * face.worldToLuxel[a][j];
		}
		return true;
	}
	// LOD may connect reference vertices, but morph/seam vertices without unique
	// preserved labels are not parameterized by nearest-point/planar projection.
	for ( int p = 0; p < 3; ++p )
	{
		const std::array<float, 3> key{ float( v[p].position[0] ), float( v[p].position[1] ), float( v[p].position[2] ) };
		const auto &lookup = map.referenceVertices[size_t( &face - map.faces.data() )];
		const auto found = lookup.find( key );
		if ( found == lookup.end() ) return false;
		if ( found->second.luxel[0] < 0 || found->second.luxel[1] < 0 ) { unresolved = true; return false; }
		for ( int a = 0; a < 2; ++a ) luxel[p][a] = found->second.luxel[a];
	}
	const double area = ( luxel[1][0] - luxel[0][0] ) * ( luxel[2][1] - luxel[0][1] ) - ( luxel[1][1] - luxel[0][1] ) * ( luxel[2][0] - luxel[0][0] );
	return area != 0;
}
bool ReceiverEquivalent( const ReceiverMapDX12 &map, const ReceiverPlacementDX12 &a, const ReceiverPlacementDX12 &b );
bool ReceiverFit( const ReceiverMapDX12 &map, uint32 faceIndex, const ReceiverVertexDX12 ( &v )[3], int width, int height, ReceiverPlacementDX12 &out, bool &unresolved )
{
	const auto &f = map.faces[faceIndex];
	double luxel[3][2];
	if ( !ReceiverLuxels( map, f, v, luxel, unresolved ) ) return false;
	double luxelError[3][2], uvError[3][2];
	for ( int p = 0; p < 3; ++p ) for ( int a = 0; a < 2; ++a )
	{
		double magnitude = std::abs( luxel[p][a] );
		if ( !( f.flags & SHADOWMAP_RECEIVER_DISPLACEMENT ) )
		{
			magnitude = std::abs( double( f.worldToLuxel[a][3] ) );
			for ( int j = 0; j < 3; ++j ) magnitude += std::abs( v[p].position[j] * f.worldToLuxel[a][j] );
		}
		luxelError[p][a] = 8.0 * FLT_EPSILON * MAX( 1.0, magnitude );
		uvError[p][a] = 8.0 * FLT_EPSILON * MAX( 1.0, std::abs( ( a ? height : width ) * v[p].uv[a] ) );
	}
	const int transforms[8][4] = { {1,0,0,1}, {-1,0,0,1}, {1,0,0,-1}, {-1,0,0,-1}, {0,1,1,0}, {0,-1,1,0}, {0,1,-1,0}, {0,-1,-1,0} };
	bool found = false;
	for ( const auto &m : transforms )
	{
		const double x = width * v[0].uv[0] - 0.5 - m[0] * luxel[0][0] - m[1] * luxel[0][1];
		const double y = height * v[0].uv[1] - 0.5 - m[2] * luxel[0][0] - m[3] * luxel[0][1];
		const double rx = std::round( x ), ry = std::round( y );
		const double errorX = uvError[0][0] + std::abs( m[0] ) * luxelError[0][0] + std::abs( m[1] ) * luxelError[0][1];
		const double errorY = uvError[0][1] + std::abs( m[2] ) * luxelError[0][0] + std::abs( m[3] ) * luxelError[0][1];
		if ( rx < -16384 || rx > 32768 || ry < -16384 || ry > 32768 || errorX >= 0.25 || errorY >= 0.25 ||
		     std::abs( x - rx ) > errorX || std::abs( y - ry ) > errorY ) continue;
		bool valid = true;
		for ( int p = 0; p < 3; ++p )
		{
			const double qx = width * v[p].uv[0] - 0.5, qy = height * v[p].uv[1] - 0.5;
			const double errorX = uvError[p][0] + std::abs( m[0] ) * luxelError[p][0] + std::abs( m[1] ) * luxelError[p][1];
			const double errorY = uvError[p][1] + std::abs( m[2] ) * luxelError[p][0] + std::abs( m[3] ) * luxelError[p][1];
			valid = valid && errorX < 0.25 && errorY < 0.25 &&
				std::abs( qx - ( rx + m[0] * luxel[p][0] + m[1] * luxel[p][1] ) ) <= errorX &&
				std::abs( qy - ( ry + m[2] * luxel[p][0] + m[3] * luxel[p][1] ) ) <= errorY;
		}
		if ( !valid ) continue;
		ReceiverPlacementDX12 placement;
		placement.face = faceIndex; placement.x = int( rx ); placement.y = int( ry );
		placement.a = m[0]; placement.b = m[1]; placement.c = m[2]; placement.d = m[3];
		const int dx = m[0] * int( f.luxelW - 1 ), ex = m[1] * int( f.luxelH - 1 );
		const int dy = m[2] * int( f.luxelW - 1 ), ey = m[3] * int( f.luxelH - 1 );
		placement.left = placement.x + MIN( 0, dx ) + MIN( 0, ex ); placement.top = placement.y + MIN( 0, dy ) + MIN( 0, ey );
		placement.width = std::abs( dx ) + std::abs( ex ) + 1; placement.height = std::abs( dy ) + std::abs( ey ) + 1;
		if ( !RectFits( placement.left, placement.top, placement.width, placement.height, width, height ) ) continue;
		if ( found && !ReceiverEquivalent( map, out, placement ) ) { unresolved = true; return false; }
		if ( !found ) out = placement;
		found = true;
	}
	return found;
}
unsigned char ReceiverField( const ReceiverMapDX12 &map, const ReceiverPlacementDX12 &p, int x, int y )
{
	const auto &face = map.faces[p.face];
	// Proven brush allocations remain runtime-only, including their decals.
	if ( face.modelIndex ) return 255;
	const int dx = x - p.x, dy = y - p.y;
	const int s = p.a * dx + p.c * dy, t = p.b * dx + p.d * dy;
	return map.visibility[face.firstSunVisibility + size_t( t ) * face.luxelW + s];
}
bool ReceiverEquivalent( const ReceiverMapDX12 &map, const ReceiverPlacementDX12 &a, const ReceiverPlacementDX12 &b )
{
	if ( a.left != b.left || a.top != b.top || a.width != b.width || a.height != b.height ) return false;
	if ( a.face == b.face && a.x == b.x && a.y == b.y && a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d ) return true;
	for ( int y = a.top; y < a.top + a.height; ++y ) for ( int x = a.left; x < a.left + a.width; ++x )
		if ( ReceiverField( map, a, x, y ) != ReceiverField( map, b, x, y ) ) return false;
	return true;
}
void Transition( CCommandRecorderDX12 *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES &state, D3D12_RESOURCE_STATES after )
{
	if ( state == after ) return;
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = state;
	barrier.Transition.StateAfter = after;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	list->ResourceBarrier( 1, &barrier );
	state = after;
}
bool CpuHeap( ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> &heap )
{
	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Type = type;
	desc.NumDescriptors = count;
	return SUCCEEDED( device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &heap ) ) );
}
bool CreateReceiverPage( ID3D12Device *device, ReceiverPageDX12 &page, int width, int height, uint32 initial )
{
	page.width = width; page.height = height;
	page.pixels.assign( size_t( width ) * height, initial );
	D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R32_UINT; desc.SampleDesc.Count = 1;
	if ( FAILED( device->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &page.resource ) ) ) ||
	     !CpuHeap( device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, page.srvHeap ) ) return false;
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = DXGI_FORMAT_R32_UINT; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView( page.resource.Get(), &srv, page.srvHeap->GetCPUDescriptorHandleForHeapStart() );
	return true;
}
bool UploadReceiverRect( CShaderDeviceDX12 &device, CPipelineCacheDX12 &pipeline, ReceiverPageDX12 &page, int x, int y, int width, int height )
{
	const uint32_t pitch = ( uint32_t( width ) * sizeof( uint32 ) + 255u ) & ~255u;
	const size_t bytes = ( size_t( pitch ) * height + 511u ) & ~size_t( 511u );
	std::vector<unsigned char> upload( bytes, 0 );
	for ( int row = 0; row < height; ++row )
		memcpy( upload.data() + size_t( row ) * pitch, page.pixels.data() + size_t( y + row ) * page.width + x, size_t( width ) * sizeof( uint32 ) );
	ID3D12Resource *source = nullptr; uint64_t offset = 0;
	const uint64_t fence = device.NextFenceValue();
	if ( !pipeline.UploadStructured( upload.data(), bytes, 512, fence, &source, &offset ) ) return false;
	pipeline.RetainExternalResource( page.resource.Get(), fence );
	Transition( device.CommandList(), page.resource.Get(), page.state, D3D12_RESOURCE_STATE_COPY_DEST );
	D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
	src.pResource = source; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint.Offset = offset;
	src.PlacedFootprint.Footprint = { DXGI_FORMAT_R32_UINT, UINT( width ), UINT( height ), 1, pitch };
	dst.pResource = page.resource.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	device.CommandList()->CopyTextureRegion( &dst, x, y, 0, &src, nullptr );
	page.fence = fence;
	return true;
}
bool ReceiverElement( const SunReceiverDrawDX12 &draw, const VertexInputDX12 &element, uint32 index, double *out, unsigned count )
{
	if ( element.inputSlot >= ARRAYSIZE( draw.streams ) ) return false;
	const auto &stream = draw.streams[element.inputSlot];
	if ( !stream.buffer || stream.repetitions != 1 || index < stream.firstVertex || uint64_t( index ) >= uint64_t( stream.firstVertex ) + stream.vertexCount ||
	     index >= uint32( stream.buffer->WrittenCount() ) ) return false;
	unsigned components = element.format == DXGI_FORMAT_R32G32_FLOAT ? 2 : element.format == DXGI_FORMAT_R32G32B32_FLOAT ? 3 : element.format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 4 : 0;
	if ( components < count ) return false;
	const uint64_t offset = uint64_t( stream.byteOffset ) + uint64_t( index ) * stream.buffer->Stride() + element.byteOffset;
	const auto bytes = stream.buffer->Data();
	if ( element.byteOffset + components * sizeof( float ) > stream.buffer->Stride() || offset > bytes.size() || components * sizeof( float ) > bytes.size() - offset ) return false;
	float values[4];
	memcpy( values, bytes.data() + size_t( offset ), components * sizeof( float ) );
	for ( unsigned i = 0; i < count; ++i )
	{
		if ( !ShadowMap_IsFiniteFloat( values[i] ) ) return false;
		out[i] = values[i];
	}
	return true;
}
bool LightingNullTable( ID3D12Device *device, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> &table )
{
	if ( !CpuHeap( device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, DX12_LIGHTING_VIEW_TABLE_COUNT, table ) ) return false;
	const UINT stride = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	D3D12_SHADER_RESOURCE_VIEW_DESC texture{};
	texture.Format = DXGI_FORMAT_R32_FLOAT;
	texture.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	texture.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	texture.Texture2D.MipLevels = 1;
	for ( UINT i = 0; i <= DX12_LIGHTING_T_STATIC_SUN; ++i )
		device->CreateShaderResourceView( nullptr, &texture, Offset( table->GetCPUDescriptorHandleForHeapStart(), i, stride ) );
	for ( UINT i = DX12_LIGHTING_T_LIGHTS; i <= DX12_LIGHTING_T_TILE_INDICES; ++i )
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC buffer{};
		buffer.Format = DXGI_FORMAT_UNKNOWN;
		buffer.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		buffer.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		buffer.Buffer.NumElements = 1;
		buffer.Buffer.StructureByteStride = i == DX12_LIGHTING_T_LIGHTS ? sizeof( RuntimeShadowLightGpu ) : i == DX12_LIGHTING_T_TILE_RANGES ? 8 : 4;
		device->CreateShaderResourceView( nullptr, &buffer, Offset( table->GetCPUDescriptorHandleForHeapStart(), i, stride ) );
	}
	return true;
}
bool NativeLogicalAvailable( IFileSystem &filesystem, const char *name, bool pixel, bool feature )
{
	ShaderVcsFile file;
	CUtlString error;
	if ( !file.Open( filesystem, name, pixel ? VcsStage::Pixel : VcsStage::Vertex, error ) ) return false;
	if ( pixel && V_strstr( name, "_highres_" ) &&
		( !file.HighresAbi() || !file.SamplerRolesReady() || !file.LightmapSamplerMask() ) ) return false;
	uint32 index = 0;
	for ( size_t ordinal = 0; file.StaticComboIndex( ordinal, index ); ++ordinal )
	{
		if ( !file.LoadStaticCombo( index, error ) ) return false;
		for ( uint32 dynamic = 0; dynamic < file.DynamicComboCount(); ++dynamic )
		{
			const VcsPayload *payload = file.DynamicPayload( index, dynamic );
			if ( !payload ) continue;
			if ( payload->tokens.Count() < 4 || memcmp( payload->tokens.Base(), "DXBC", 4 ) ) return false;
			bool lighting = false, visibility = false;
			if ( !ValidateLightingShaderDX12( { payload->tokens.Base(), SIZE_T( payload->tokens.Count() ) }, pixel, &lighting, error, &visibility ) ) return false;
			if ( pixel && !V_strcmp( name, "lightmappedgeneric_shadowmap_ps51" ) && !visibility ) return false;
			if ( ( !V_strcmp( name, "shadow_depth_restore_vs51" ) || !V_strcmp( name, "shadow_depth_restore_ps51" ) ) &&
				!ValidateShadowDepthRestoreShaderDX12( { payload->tokens.Base(), SIZE_T( payload->tokens.Count() ) }, pixel ) ) return false;
			// Replay/unlit combos can legally precede the first actual RGB receiver.
			if ( !feature || !pixel || lighting ) return true;
		}
	}
	return false;
}
} // namespace

bool ValidateShadowDepthRestoreShaderDX12( const D3D12_SHADER_BYTECODE &bytecode, bool pixel )
{
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
	if ( FAILED( D3DReflect( bytecode.pShaderBytecode, bytecode.BytecodeLength, IID_PPV_ARGS( &reflection ) ) ) ) return false;
	D3D12_SHADER_DESC shader{};
	if ( FAILED( reflection->GetDesc( &shader ) ) || ( ( shader.Version >> 16 ) & 0xffffu ) != ( pixel ? D3D12_SHVER_PIXEL_SHADER : D3D12_SHVER_VERTEX_SHADER ) ) return false;
	bool constants = false, source = !pixel;
	for ( UINT i = 0; i < shader.BoundResources; ++i )
	{
		D3D12_SHADER_INPUT_BIND_DESC binding{};
		if ( FAILED( reflection->GetResourceBindingDesc( i, &binding ) ) ) return false;
		if ( binding.Type == D3D_SIT_CBUFFER )
		{
			if ( constants || binding.Space || binding.BindPoint || binding.BindCount != 1 || V_strcmp( binding.Name, "ShadowDepthRestoreConstants" ) ) return false;
			D3D12_SHADER_BUFFER_DESC buffer{};
			ID3D12ShaderReflectionConstantBuffer *block = reflection->GetConstantBufferByName( binding.Name );
			if ( !block || FAILED( block->GetDesc( &buffer ) ) || buffer.Size != 64 || buffer.Variables != 4 ) return false;
			const char *const names[] = { "srcRect", "dstRect", "srcSize", "dstSize" };
			for ( UINT member = 0; member < 4; ++member )
			{
				ID3D12ShaderReflectionVariable *variable = block->GetVariableByIndex( member );
				D3D12_SHADER_VARIABLE_DESC value{};
				D3D12_SHADER_TYPE_DESC type{};
				if ( !variable || FAILED( variable->GetDesc( &value ) ) || !variable->GetType() || FAILED( variable->GetType()->GetDesc( &type ) ) ||
					V_strcmp( value.Name, names[member] ) || value.StartOffset != member * 16 || value.Size != 16 ||
					type.Class != D3D_SVC_VECTOR || type.Type != D3D_SVT_UINT || type.Rows != 1 || type.Columns != 4 || type.Elements ) return false;
			}
			constants = true;
		}
		else if ( binding.Type == D3D_SIT_TEXTURE )
		{
			if ( !pixel || source || binding.Space || binding.BindPoint || binding.BindCount != 1 || binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D || binding.ReturnType != D3D_RETURN_TYPE_FLOAT ) return false;
			source = true;
		}
		else return false;
	}
	return constants && source;
}

// Reusable reference-counted queue packet. Arrays keep capacity when recycled, including per-light pass packets.
struct LightingPacketDX12 final : IRefCounted
{
	CLightingDX12 *owner = nullptr;
	std::atomic<int> references{ 1 };
	int operation = 0, values[8]{};
	bool flag = false, valid = true;
	DX12LightingMapDesc map{};
	DX12LightingViewPacket view{};
	CUtlVector<DX12ShadowTarget_t> ids;
	CUtlVector<RuntimeShadowLightGpu> lights;
	CUtlVector<uint32> ranges, indices;
	CUtlVector<ShadowTargetDX12 *> leases;
	int AddRef() override { return ++references; }
	int Release() override
	{
		const int count = --references;
		if ( !count ) owner->Recycle( this );
		return count;
	}
	void Replay() { owner->Execute( this ); }
};

struct CLightingDX12::Impl
{
	CShaderDeviceDX12 *device = nullptr;
	CShaderAPIDX12 *api = nullptr;
	CThreadFastMutex mutex;
	std::atomic<uint32> receiverGeneration{ 0 };
	CUtlVector<ShadowTargetDX12 *> targets;
	uint32 nextTargetGeneration = 1;
	CUtlVector<LightingPacketDX12 *> freePackets;
	CUtlMap<uint64_t, LightingStatusDX12, uint32_t> statuses{ DefLessFunc( uint64_t ) };
	CUtlVector<DX12LightingSelectedLight> selected;
	std::atomic<uint32> activeMap{ 0 };
	bool highresMap = false;
	uint64 nativeMapGeneration = 0;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mapNullTable;
	std::map<uint32, std::shared_ptr<ReceiverMapDX12>> receiverMaps;
	std::map<uint32, DX12LightingSunVisibilityStats> receiverStats;
	std::map<uint64_t, bool> carrierShaders;
	std::shared_ptr<ReceiverPageDX12> currentReceiverPage, whiteReceiverPage;
	struct ViewState
	{
		LightingPacketDX12 *packet = nullptr;
		uint64_t fence = 0, heap = 0;
		DescriptorRangeDX12 table{};
		D3D12_GPU_VIRTUAL_ADDRESS constants = 0;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> nullTable;
		ID3D12Resource *structured[3]{};
		uint64_t structuredOffsets[3]{};
		bool highresScope = false;
	};
	CUtlVector<ViewState *> views, freeViews;
	ViewState neutral;
	LightingPacketDX12 neutralPacket;
	struct PassState
	{
		ShadowTargetDX12 *target = nullptr;
		D3D12_RECT rect{};
		RenderTargetBindingDX12 savedTargets{};
		ShaderAPITextureHandle_t colors[RenderTargetBindingDX12::kMaxColorTargets]{};
		ShaderAPITextureHandle_t colorHandle = SHADER_RENDERTARGET_BACKBUFFER, depthHandle = SHADER_RENDERTARGET_DEPTHBUFFER;
		ShaderViewport_t viewports[16]{};
		int viewportCount = 0, scissor[5]{};
		ShaderRasterState_t raster{};
		MaterialCullMode_t cullMode = MATERIAL_CULLMODE_CCW;
		CShaderAPIDX12::Snapshot snapshot{};
		StateSnapshot_t snapshotId = StateSnapshot_t( -1 );
		VertexShaderHandle_t vs = VERTEX_SHADER_HANDLE_INVALID;
		PixelShaderHandle_t ps = PIXEL_SHADER_HANDLE_INVALID;
		GeometryShaderHandle_t gs = GEOMETRY_SHADER_HANDLE_INVALID;
		int vertexIndex = -1, pixelIndex = 0;
		bool namedVertex = false, namedPixel = false;
		bool rasterOverride = false;
	};
	CUtlVector<PassState> passes;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> restoreRoot;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> restorePso;

	uint64_t Key( uint32 map, uint32 view ) const { return ( uint64_t( map ) << 32 ) | view; }
	void Status( uint32 map, uint32 view, DX12LightingStatus status, const char *error = nullptr )
	{
		AUTO_LOCK( mutex );
		const uint64_t key = Key( map, view );
		int index = statuses.Find( key );
		if ( index == statuses.InvalidIndex() ) index = statuses.Insert( key );
		LightingStatusDX12 &entry = statuses[index];
		if ( !view ) entry.completed = true;
		if ( entry.state == DX12_LIGHTING_STATUS_FAILED ) return;
		entry.state = status;
		if ( error ) entry.error = error;
	}
	void Fail( const char *error, uint32 map = 0, uint32 view = 0 )
	{
		if ( device && device->Highres().EnhancedMap() )
			device->Highres().OnNativeFailure( error );
		if ( !map ) map = activeMap.load() ? activeMap.load() : receiverGeneration.load();
		if ( !view && views.Count() && views.Tail()->packet->view.mapGeneration == map ) view = views.Tail()->packet->view.viewGeneration;
		Status( map, view, DX12_LIGHTING_STATUS_FAILED, error );
		Status( map, 0, DX12_LIGHTING_STATUS_FAILED, error );
		const uint32 active = activeMap.load();
		if ( active && active != map ) Status( active, 0, DX12_LIGHTING_STATUS_FAILED, error );
	}
	bool Failed( uint32 map, uint32 view = 0 )
	{
		AUTO_LOCK( mutex );
		int i = statuses.Find( Key( map, 0 ) );
		if ( i != statuses.InvalidIndex() && statuses[i].state == DX12_LIGHTING_STATUS_FAILED ) return true;
		i = statuses.Find( Key( map, view ) );
		return i != statuses.InvalidIndex() && statuses[i].state == DX12_LIGHTING_STATUS_FAILED;
	}
	void Use( ShadowTargetDX12 *target, bool sample )
	{
		const uint64_t fence = device->NextFenceValue();
		if ( target->fence.load() < fence ) target->fence.store( fence );
		api->m_Pipeline.RetainExternalResource( target->resource.Get(), fence );
		CCommandRecorderDX12 *list = device->CommandList();
		if ( !target->initialized )
		{
			Transition( list, target->resource.Get(), target->state, D3D12_RESOURCE_STATE_DEPTH_WRITE );
			list->ClearDepthStencilView( target->dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr );
			target->initialized = true;
		}
		Transition( list, target->resource.Get(), target->state, sample ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_DEPTH_WRITE );
	}
	void RestoreBackend( const PassState &saved )
	{
		memcpy( api->m_RenderTargets, saved.colors, sizeof( saved.colors ) );
		api->m_hRenderTarget = saved.colorHandle;
		api->m_hDepthTarget = saved.depthHandle;
		api->m_RenderTargetCache.valid = false;
		memcpy( api->m_Viewports, saved.viewports, sizeof( saved.viewports ) );
		api->m_nViewportCount = saved.viewportCount;
		memcpy( api->m_FastIntParams, saved.scissor, sizeof( saved.scissor ) );
		api->m_RasterState = saved.raster;
		api->m_bRasterOverride = saved.rasterOverride;
		api->m_CullMode = saved.cullMode;
		api->m_ActiveSnapshot = saved.snapshot;
		api->m_hActiveSnapshotId = saved.snapshotId;
		api->m_hBoundVS = saved.vs;
		api->m_hBoundPS = saved.ps;
		api->m_hBoundGS = saved.gs;
		api->m_nVertexShaderIndex = saved.vertexIndex;
		api->m_nPixelShaderIndex = saved.pixelIndex;
		api->m_bBoundVertexShaderIsNamed = saved.namedVertex;
		api->m_bBoundPixelShaderIsNamed = saved.namedPixel;
		api->m_bNamedVertexShaderDirty = true;
		api->m_bNamedPixelShaderDirty = true;
		api->m_Pipeline.InvalidateGraphicsBindings();
		++api->m_nPipelineMemoEpoch;
		CCommandRecorderDX12 *list = device->CommandList();
		if ( list )
		{
			const RenderTargetBindingDX12 &target = saved.savedTargets;
			list->OMSetRenderTargets( target.colorCount, target.rtvs, FALSE, target.depth ? &target.dsv : nullptr );
			D3D12_VIEWPORT viewport{ 0, 0, float( target.width ), float( target.height ), 0, 1 };
			if ( saved.viewportCount )
			{
				const ShaderViewport_t &v = saved.viewports[0];
				viewport = { float( v.m_nTopLeftX ), float( v.m_nTopLeftY ), float( v.m_nWidth ), float( v.m_nHeight ), v.m_flMinZ, v.m_flMaxZ };
			}
			D3D12_RECT rect{ 0, 0, target.width, target.height };
			if ( saved.scissor[4] ) rect = { saved.scissor[0], saved.scissor[1], saved.scissor[2], saved.scissor[3] };
			list->RSSetViewports( 1, &viewport );
			list->RSSetScissorRects( 1, &rect );
		}
	}
	bool EnsureRestore()
	{
		if ( restorePso ) return true;
		ShaderRecordDX12 *vs = api->ResolveNamedShader( "shadow_depth_restore_vs51", false, 0, 0 );
		ShaderRecordDX12 *ps = api->ResolveNamedShader( "shadow_depth_restore_ps51", true, 0, 0 );
		if ( !vs || !ps || !vs->legacyBytecode.IsEmpty() || !ps->legacyBytecode.IsEmpty() ) return false;
		if ( !ValidateShadowDepthRestoreShaderDX12( vs->Bytecode(), false ) || !ValidateShadowDepthRestoreShaderDX12( ps->Bytecode(), true ) ) return false;
		D3D12_DESCRIPTOR_RANGE range{};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 1;
		D3D12_ROOT_PARAMETER roots[2]{};
		roots[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		roots[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		roots[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		roots[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		roots[1].DescriptorTable = { 1, &range };
		D3D12_ROOT_SIGNATURE_DESC root{};
		root.NumParameters = ARRAYSIZE( roots );
		root.pParameters = roots;
		Microsoft::WRL::ComPtr<ID3DBlob> serialized, error;
		if ( FAILED( D3D12SerializeRootSignature( &root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error ) ) || FAILED( device->NativeDevice()->CreateRootSignature( 0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS( &restoreRoot ) ) ) ) return false;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
		pso.pRootSignature = restoreRoot.Get();
		pso.VS = vs->Bytecode();
		pso.PS = ps->Bytecode();
		pso.SampleMask = UINT_MAX;
		pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pso.RasterizerState.DepthClipEnable = TRUE;
		pso.DepthStencilState.DepthEnable = TRUE;
		pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		pso.DepthStencilState.StencilReadMask = pso.DepthStencilState.StencilWriteMask = 0xff;
		pso.DepthStencilState.FrontFace.StencilFunc = pso.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		pso.DepthStencilState.FrontFace.StencilFailOp = pso.DepthStencilState.FrontFace.StencilDepthFailOp = pso.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		pso.DepthStencilState.BackFace = pso.DepthStencilState.FrontFace;
		pso.BlendState.RenderTarget[0].SrcBlend = pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		pso.BlendState.RenderTarget[0].DestBlend = pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		pso.BlendState.RenderTarget[0].BlendOp = pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		pso.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
		pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pso.SampleDesc.Count = 1;
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		return SUCCEEDED( device->NativeDevice()->CreateGraphicsPipelineState( &pso, IID_PPV_ARGS( &restorePso ) ) );
	}
};

CLightingDX12::CLightingDX12() : m_Impl( new Impl ) {}
CLightingDX12::~CLightingDX12()
{
	Shutdown();
	for ( LightingPacketDX12 *packet : m_Impl->freePackets ) delete packet;
	for ( Impl::ViewState *view : m_Impl->freeViews ) delete view;
	delete m_Impl;
}
void CLightingDX12::Initialize( CShaderDeviceDX12 *device, CShaderAPIDX12 *api )
{
	AUTO_LOCK( m_Impl->mutex );
	m_Impl->device = device;
	m_Impl->api = api;
	m_Impl->neutralPacket.view.constants.cShadowViewport[2] = 1.f;
	m_Impl->neutralPacket.view.constants.cShadowViewport[3] = 1.f;
	m_Impl->neutral.packet = &m_Impl->neutralPacket;
}
void CLightingDX12::Shutdown()
{
	Impl &s = *m_Impl;
	while ( s.views.Count() )
	{
		Impl::ViewState *view = s.views.Tail();
		view->packet->Release();
		s.views.RemoveMultipleFromTail( 1 );
		delete view;
	}
	for ( const Impl::PassState &pass : s.passes ) if ( pass.target ) pass.target->Release();
	s.passes.RemoveAll();
	s.restorePso.Reset();
	s.restoreRoot.Reset();
	AUTO_LOCK( s.mutex );
	for ( ShadowTargetDX12 *target : s.targets ) if ( target ) target->Release();
	s.targets.RemoveAll();
	s.statuses.RemoveAll();
	s.selected.RemoveAll();
	s.mapNullTable.Reset();
	s.currentReceiverPage.reset(); s.whiteReceiverPage.reset();
	s.receiverMaps.clear(); s.receiverStats.clear(); s.carrierShaders.clear();
	s.neutral = {};
	s.activeMap = 0;
	s.highresMap = false; s.nativeMapGeneration = 0;
	s.receiverGeneration.store( 0 );
	s.api = nullptr;
	s.device = nullptr;
}

bool CLightingDX12::ValidateMap( const DX12LightingMapDesc &map, char *error, int errorBytes )
{
	Impl &s = *m_Impl;
	AUTO_LOCK( s.mutex );
	const char *failure = nullptr;
	if ( map.shaderAbi != DX12_LIGHTING_SHADER_ABI ) failure = SHADOWMAP_ERR_SHADER_UNAVAILABLE;
	else if ( !s.device || !s.device->NativeDevice() || !g_pHardwareConfigDX12 || g_pHardwareConfigDX12->ResourceBindingTier() < 2 ) failure = SHADOWMAP_ERR_REQUIRES_DX12;
	else if ( !s.api || !s.api->m_Pipeline.LightingRootAvailable() ) failure = SHADOWMAP_ERR_SHADER_UNAVAILABLE;
	else if ( !map.mapGeneration || map.mode >= SHADOWMAP_MODE_COUNT || ( !map.highresRoute && !map.selectedLightCount ) || map.selectedLightCount > 8192 || ( map.selectedLightCount && !map.selectedLights ) || map.sunLightIndex < -1 || ( map.sunLightIndex >= 0 && uint32( map.sunLightIndex ) >= map.selectedLightCount ) ) failure = SHADOWMAP_ERR_INVALID_METADATA;
	bool sunFound = map.sunLightIndex == -1;
	for ( int axis = 0; !failure && axis < 3; ++axis )
		if ( !ShadowMap_IsFiniteFloat( map.worldMins[axis] ) || !ShadowMap_IsFiniteFloat( map.worldMaxs[axis] ) || map.worldMins[axis] > map.worldMaxs[axis] ) failure = SHADOWMAP_ERR_INVALID_METADATA;
	for ( uint32 i = 0; !failure && i < map.selectedLightCount; ++i )
	{
		const DX12LightingSelectedLight &light = map.selectedLights[i];
		if ( light.type > DX12_SHADOW_LIGHT_SPOT || light.style < 0 || light.style >= 256 || light.lightId != i ) { failure = SHADOWMAP_ERR_INVALID_METADATA; break; }
		const float *floats = light.origin;
		for ( size_t f = 0; f < ( sizeof( light ) - offsetof( DX12LightingSelectedLight, origin ) ) / sizeof( float ); ++f ) if ( !ShadowMap_IsFiniteFloat( floats[f] ) ) failure = SHADOWMAP_ERR_INVALID_METADATA;
		if ( !ShadowMap_IsFiniteFloat( light.shadowSourceRadius ) || !ShadowMap_IsFiniteFloat( light.shadowSunAngularRadius ) || light.shadowSourceRadius < 0.f || light.shadowSunAngularRadius < 0.f || light.shadowSunAngularRadius >= SHADOWMAP_MAX_SUN_ANGULAR_RADIUS ) failure = SHADOWMAP_ERR_INVALID_EMITTER_SIZE;
		if ( light.type == DX12_SHADOW_LIGHT_SUN )
		{
			if ( sunFound || int32( light.lightId ) != map.sunLightIndex ) failure = SHADOWMAP_ERR_INVALID_METADATA;
			sunFound = true;
		}
	}
	if ( !failure && !sunFound ) failure = SHADOWMAP_ERR_INVALID_METADATA;
	if ( !failure && map.highresRoute )
	{
		DX12HighresMapStatus status{}; char reason[512];
		s.device->Highres().GetStatus( status, reason, sizeof( reason ) );
		if ( map.highresRoute != 1 || !map.nativeMapGeneration || status.nativeMapGeneration != map.nativeMapGeneration ||
			( status.state != DX12_HIGHRES_READY && status.state != DX12_HIGHRES_PENDING ) ||
			status.lightingLump != ( map.mode == SHADOWMAP_MODE_HDR ? LUMP_LIGHTING_HDR : LUMP_LIGHTING ) ||
			( status.faceLump != LUMP_FACES && status.faceLump != LUMP_FACES_HDR ) ||
			map.receiverFaceCount || map.receiverFaces || map.receiverTriangleCount || map.receiverTriangles ||
			map.sunVisibilityCount || map.sunVisibility || map.receiverQuadCount || map.receiverQuads )
			failure = SHADOWMAP_ERR_INVALID_METADATA;
	}
	else if ( !failure && ( map.nativeMapGeneration || !ValidateReceiverSpans( map ) ) ) failure = SHADOWMAP_ERR_INVALID_METADATA;
	if ( !failure )
	{
		IFileSystem *filesystem = g_pShaderDeviceMgrDX12 ? g_pShaderDeviceMgrDX12->HostFileSystem() : nullptr;
		const char *const names[] = { "shadow_depth_restore_vs51", "shadow_depth_restore_ps51", "lightmappedgeneric_shadowmap_vs51", "lightmappedgeneric_shadowmap_ps51", "vertexlit_and_unlit_generic_shadowmap_vs51", "vertexlit_and_unlit_generic_shadowmap_ps51", "dx12_detailshadowlit_vs51", "dx12_detailshadowlit_ps51", "dx12_detailshadowlit_depth_ps51" };
		const char *const highresNames[] = {
			"shadow_depth_restore_vs51", "shadow_depth_restore_ps51",
			"lightmappedgeneric_highres_vs51", "lightmappedgeneric_highres_ps51",
			"worldtwotextureblend_highres_ps51", "lightmappedgeneric_decal_highres_vs51",
			"lightmappedgeneric_decal_highres_ps51", "lightmappedreflective_highres_vs51",
			"lightmappedreflective_highres_ps51", "water_highres_vs51", "water_highres_ps51",
			"shatteredglass_highres_vs51", "shatteredglass_highres_ps51",
			"pyro_vision_highres_vs51", "pyro_vision_highres_ps51",
			"worldvertextransition_editor_highres_vs51", "worldvertextransition_editor_highres_ps51",
			"vertexlit_and_unlit_generic_shadowmap_vs51", "vertexlit_and_unlit_generic_shadowmap_ps51",
			"dx12_detailshadowlit_vs51", "dx12_detailshadowlit_ps51", "dx12_detailshadowlit_depth_ps51"
		};
		const char *const *required = map.highresRoute ? highresNames : names;
		const size_t requiredCount = map.highresRoute ? ARRAYSIZE( highresNames ) : ARRAYSIZE( names );
		if ( !filesystem ) failure = SHADOWMAP_ERR_SHADER_UNAVAILABLE;
		for ( size_t requiredIndex = 0; requiredIndex < requiredCount; ++requiredIndex )
		{
			if ( failure ) break;
			const char *name = required[requiredIndex];
			const bool pixel = V_strstr( name, "ps51" ) != nullptr;
			const bool feature = V_strstr( name, "shadowmap" ) != nullptr || V_strstr( name, "highres" ) != nullptr || ( V_strstr( name, "detailshadowlit_ps51" ) != nullptr );
			if ( !NativeLogicalAvailable( *filesystem, name, pixel, feature ) ) failure = SHADOWMAP_ERR_SHADER_UNAVAILABLE;
		}
	}
	if ( error && errorBytes > 0 ) V_strncpy( error, failure ? failure : "", errorBytes );
	return !failure;
}
DX12LightingStatus CLightingDX12::GetStatus( uint32 map, uint32 view, char *error, int errorBytes )
{
	Impl &s = *m_Impl;
	AUTO_LOCK( s.mutex );
	const int viewIndex = s.statuses.Find( s.Key( map, view ) );
	int index = viewIndex;
	if ( view && index == s.statuses.InvalidIndex() )
	{
		const int mapIndex = s.statuses.Find( s.Key( map, 0 ) );
		const char *failure = mapIndex != s.statuses.InvalidIndex() && s.statuses[mapIndex].state == DX12_LIGHTING_STATUS_FAILED ?
			s.statuses[mapIndex].error.Get() : SHADOWMAP_ERR_INVALID_METADATA;
		if ( error && errorBytes > 0 ) V_strncpy( error, failure, errorBytes );
		return DX12_LIGHTING_STATUS_FAILED;
	}
	if ( view && !s.statuses[index].completed )
	{
		if ( error && errorBytes > 0 ) error[0] = 0;
		return DX12_LIGHTING_STATUS_PENDING;
	}
	const int mapIndex = s.statuses.Find( s.Key( map, 0 ) );
	if ( mapIndex != s.statuses.InvalidIndex() && s.statuses[mapIndex].state == DX12_LIGHTING_STATUS_FAILED ) index = mapIndex;
	if ( error && errorBytes > 0 ) V_strncpy( error, index != s.statuses.InvalidIndex() ? s.statuses[index].error.Get() : "", errorBytes );
	const DX12LightingStatus result = index != s.statuses.InvalidIndex() ? s.statuses[index].state : DX12_LIGHTING_STATUS_PENDING;
	if ( view && viewIndex != s.statuses.InvalidIndex() && result != DX12_LIGHTING_STATUS_PENDING ) s.statuses.RemoveAt( viewIndex );
	return result;
}
void CLightingDX12::SetReceiverFeatureGeneration( uint32 generation ) { m_Impl->receiverGeneration.store( generation, std::memory_order_relaxed ); }
uint32 CLightingDX12::ReceiverFeatureGeneration() { return m_Impl->receiverGeneration.load( std::memory_order_relaxed ); }
void CLightingDX12::RejectUnsupportedLitShader( const char *name )
{
	char error[256];
	V_snprintf( error, sizeof( error ), "%s: %s", SHADOWMAP_ERR_SHADER_UNAVAILABLE, name ? name : "<unknown>" );
	if ( m_Impl->device && m_Impl->device->Highres().EnhancedMap() )
	{
		m_Impl->device->Highres().OnNativeFailure( error );
		return;
	}
	const uint32 generation = ReceiverFeatureGeneration();
	if ( generation ) m_Impl->Status( generation, 0, DX12_LIGHTING_STATUS_FAILED, error );
}

DX12ShadowTarget_t CLightingDX12::CreateShadowDepthTarget( const char *name, int width, int height )
{
	Impl &s = *m_Impl;
	AUTO_LOCK( s.mutex );
	if ( !s.device || !s.device->NativeDevice() || width <= 0 || height <= 0 || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ) return 0;
	ShadowTargetDX12 *target = new ShadowTargetDX12;
	target->width = width;
	target->height = height;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R32_TYPELESS;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE clear{};
	clear.Format = DXGI_FORMAT_D32_FLOAT;
	clear.DepthStencil.Depth = 1.f;
	ID3D12Device *device = s.device->NativeDevice();
	if ( FAILED( device->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS( &target->resource ) ) ) || !CpuHeap( device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, target->dsvHeap ) || !CpuHeap( device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, target->srvHeap ) )
	{
		delete target;
		return 0;
	}
	if ( name && *name )
	{
		wchar_t wide[256];
		V_UTF8ToUnicode( name, wide, sizeof( wide ) );
		target->resource->SetName( wide );
	}
	target->dsv = target->dsvHeap->GetCPUDescriptorHandleForHeapStart();
	target->srv = target->srvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView( target->resource.Get(), &dsv, target->dsv );
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView( target->resource.Get(), &srv, target->srv );
	int index = 0;
	for ( ; index < s.targets.Count(); ++index ) if ( !s.targets[index] ) break;
	if ( index == s.targets.Count() ) s.targets.AddToTail( nullptr );
	if ( !s.nextTargetGeneration ) ++s.nextTargetGeneration;
	target->identity = ( uint64_t( s.nextTargetGeneration++ ) << 32 ) | uint32( index + 1 );
	s.targets[index] = target;
	return target->identity;
}
IRefCounted *CLightingDX12::RetainShadowDepthTarget( DX12ShadowTarget_t identity )
{
	Impl &s = *m_Impl;
	AUTO_LOCK( s.mutex );
	const uint32 index = uint32( identity ) - 1;
	if ( !identity || index >= uint32( s.targets.Count() ) ) return nullptr;
	ShadowTargetDX12 *target = s.targets[index];
	if ( !target || target->identity != identity || target->destroying ) return nullptr;
	target->AddRef();
	return target;
}
void CLightingDX12::DestroyShadowDepthTarget( DX12ShadowTarget_t identity )
{
	Impl &s = *m_Impl;
	AUTO_LOCK( s.mutex );
	const uint32 index = uint32( identity ) - 1;
	if ( !identity || index >= uint32( s.targets.Count() ) ) return;
	ShadowTargetDX12 *target = s.targets[index];
	if ( target && target->identity == identity ) target->destroying = true;
}
void CLightingDX12::Reclaim()
{
	Impl &s = *m_Impl;
	if ( !s.device ) return;
	const uint64_t completed = s.device->CompletedFenceValue();
	AUTO_LOCK( s.mutex );
	for ( int i = 0; i < s.targets.Count(); ++i )
	{
		ShadowTargetDX12 *target = s.targets[i];
		if ( target && target->destroying && target->references.load() == 1 && target->fence.load() <= completed )
		{
			s.targets[i] = nullptr;
			target->Release();
		}
	}
	for ( auto i = s.receiverMaps.begin(); i != s.receiverMaps.end(); )
		if ( i->second->retired && i->second->fence <= completed ) i = s.receiverMaps.erase( i ); else ++i;
}
LightingPacketDX12 *CLightingDX12::Packet( int operation )
{
	AUTO_LOCK( m_Impl->mutex );
	LightingPacketDX12 *packet;
	if ( m_Impl->freePackets.Count() )
	{
		packet = m_Impl->freePackets.Tail();
		m_Impl->freePackets.RemoveMultipleFromTail( 1 );
		packet->references.store( 1 );
	}
	else packet = new LightingPacketDX12;
	packet->owner = this;
	packet->operation = operation;
	packet->valid = true;
	packet->flag = false;
	packet->map = {};
	packet->view = {};
	memset( packet->values, 0, sizeof( packet->values ) );
	return packet;
}
void CLightingDX12::Recycle( LightingPacketDX12 *packet )
{
	for ( ShadowTargetDX12 *target : packet->leases ) if ( target ) target->Release();
	packet->leases.RemoveAll();
	packet->ids.RemoveAll();
	packet->lights.RemoveAll();
	packet->ranges.RemoveAll();
	packet->indices.RemoveAll();
	AUTO_LOCK( m_Impl->mutex );
	m_Impl->freePackets.AddToTail( packet );
}
void CLightingDX12::Enqueue( LightingPacketDX12 *packet )
{
	if ( g_pMaterialSystem )
	{
		CMatRenderContextPtr context( g_pMaterialSystem );
		// Scope changes must follow the material command stream, even when this
		// thread temporarily owns the device. Drain buffered draws before the
		// marker; this is not a GPU wait or lifecycle quiescence operation.
		context->Flush( false );
		ICallQueue *queue = context->GetCallQueue();
		if ( queue )
		{
			// tier1's CreateRefCountingFunctor passes the memory policy in the FUNCTOR_BASE template slot (SDK bug:
			// never instantiated upstream), so build the ref-counting member functor explicitly. The functor
			// AddRefs the packet for the queue's lifetime and releases it after replay.
			typedef CMemberFunctor0<LightingPacketDX12 *, void ( LightingPacketDX12::* )(), CFunctorBase, CFuncMemPolicyRefCount<LightingPacketDX12 *> > ReplayFunctor_t;
			CFunctor *functor = new ReplayFunctor_t( packet, &LightingPacketDX12::Replay );
			queue->QueueFunctor( functor );
			functor->Release();
		}
		else packet->Replay();
	}
	else packet->Replay();
	packet->Release();
}
void CLightingDX12::PrepareMap( const DX12LightingMapDesc &map )
{
	// t1026 is uploaded from each view's RuntimeShadowLightGpu packet. Admission needs only CPU
	// descriptors and immutable selected records: no GPU submission, shared recorder access or fence wait.
	Impl &s = *m_Impl;
	char error[256];
	s.Status( map.mapGeneration, 0, DX12_LIGHTING_STATUS_PENDING );
	if ( !ValidateMap( map, error, sizeof( error ) ) )
	{
		s.Status( map.mapGeneration, 0, DX12_LIGHTING_STATUS_FAILED, error );
		return;
	}
	AUTO_LOCK( s.mutex );
	ID3D12Device *device = s.device->NativeDevice();
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> nullTable;
	const bool success = SUCCEEDED( device->GetDeviceRemovedReason() ) && LightingNullTable( device, nullTable );
	int status = s.statuses.Find( s.Key( map.mapGeneration, 0 ) );
	if ( status == s.statuses.InvalidIndex() ) status = s.statuses.Insert( s.Key( map.mapGeneration, 0 ) );
	if ( s.statuses[status].state == DX12_LIGHTING_STATUS_FAILED ) return;
	if ( !success )
	{
		s.statuses[status].state = DX12_LIGHTING_STATUS_FAILED;
		s.statuses[status].error = SHADOWMAP_ERR_RESIDENCY;
		return;
	}
	if ( map.highresRoute )
	{
		s.selected.CopyArray( map.selectedLights, map.selectedLightCount );
		s.mapNullTable = nullTable;
		s.activeMap.store( map.mapGeneration );
		s.highresMap = true; s.nativeMapGeneration = map.nativeMapGeneration;
		s.statuses[status].state = DX12_LIGHTING_STATUS_READY;
		return; // No inverse receiver records, indices, or companion pages exist on this route.
	}
	auto receiver = std::make_shared<ReceiverMapDX12>();
	receiver->generation = map.mapGeneration;
	if ( map.receiverFaceCount )
	{
		receiver->faces.assign( map.receiverFaces, map.receiverFaces + map.receiverFaceCount );
		receiver->triangles.assign( map.receiverTriangles, map.receiverTriangles + map.receiverTriangleCount );
		if ( map.receiverQuadCount ) receiver->quads.assign( map.receiverQuads, map.receiverQuads+map.receiverQuadCount );
		receiver->visibility.assign( map.sunVisibility, map.sunVisibility + map.sunVisibilityCount );
		receiver->mapped.assign( map.receiverFaceCount, 0 );
		receiver->boxes.resize( map.receiverFaceCount );
		receiver->referenceVertices.resize( map.receiverFaceCount );
		std::vector<uint32> order( map.receiverFaceCount );
		for ( uint32 i = 0; i < map.receiverFaceCount; ++i )
		{
			order[i] = i;
			const auto &face = receiver->faces[i];
			for ( uint32 t = face.firstTriangle; t < face.firstTriangle + face.triangleCount; ++t )
				for ( int v = 0; v < 3; ++v ) receiver->boxes[i].Add( receiver->triangles[t].position[v] );
			// Exact reference-vertex identity is also needed for nonplanar brush polygons.
			{
				auto &vertices = receiver->referenceVertices[i];
				for ( uint32 t = face.firstTriangle; t < face.firstTriangle + face.triangleCount; ++t ) for ( int v = 0; v < 3; ++v )
				{
					const auto &triangle = receiver->triangles[t];
					const std::array<float, 3> key{ triangle.position[v][0], triangle.position[v][1], triangle.position[v][2] };
					const std::array<float, 2> label{ triangle.luxel[v][0], triangle.luxel[v][1] };
					const auto existing = vertices.find( key );
					if ( existing == vertices.end() ) vertices.emplace( key, ReceiverReferenceVertexDX12{ label } );
					else if ( existing->second.luxel != label ) existing->second.luxel = { -1.f, -1.f };
				}
			}
		}
		receiver->nodes.reserve( size_t( map.receiverFaceCount ) * 2 );
		receiver->BuildNode( order, 0, order.size() );
	}
	receiver->stats.receiverFaces = map.receiverFaceCount;
	// Generation IDs are immutable. Replacing a live generation could alias queued
	// geometry proofs to different masks, so reject it rather than silently reset.
	if ( s.receiverMaps.find( map.mapGeneration ) != s.receiverMaps.end() )
	{
		s.statuses[status].state = DX12_LIGHTING_STATUS_FAILED;
		s.statuses[status].error = SHADOWMAP_ERR_INVALID_METADATA;
		return;
	}
	s.receiverMaps.emplace( map.mapGeneration, receiver );
	s.receiverStats[map.mapGeneration] = receiver->stats;
	s.selected.CopyArray( map.selectedLights, map.selectedLightCount );
	s.mapNullTable = nullTable;
	s.highresMap = false; s.nativeMapGeneration = 0;
	s.activeMap.store( map.mapGeneration );
	s.statuses[status].state = DX12_LIGHTING_STATUS_READY;
}
void CLightingDX12::BeginView( const DX12LightingViewPacket &view )
{
	// Acceptance establishes Pending before queue replay; unknown/consumed IDs are not pending work.
	m_Impl->Status( view.mapGeneration, view.viewGeneration, DX12_LIGHTING_STATUS_PENDING );
	LightingPacketDX12 *packet = Packet( View );
	packet->view = view;
	packet->valid = view.localTargetCount <= DX12_SHADOW_MAX_LOCAL_PAGES && view.lightCount <= 8192 && view.tileCount <= INT_MAX / 2 && view.tileIndexCount <= INT_MAX && ( !view.localTargetCount || view.localTargets ) && ( !view.lightCount || view.lights ) && ( !view.tileCount || view.tileRanges ) && ( !view.tileIndexCount || view.tileIndices );
	if ( packet->valid )
	{
		packet->ids.CopyArray( view.localTargets, view.localTargetCount );
		packet->lights.CopyArray( view.lights, view.lightCount );
		packet->ranges.CopyArray( view.tileRanges, view.tileCount * 2 );
		packet->indices.CopyArray( view.tileIndices, view.tileIndexCount );
		packet->leases.AddToTail( static_cast<ShadowTargetDX12 *>( RetainShadowDepthTarget( view.cascadeAtlasTarget ) ) );
		packet->leases.AddToTail( static_cast<ShadowTargetDX12 *>( RetainShadowDepthTarget( view.staticSunTarget ) ) );
		for ( DX12ShadowTarget_t id : packet->ids ) packet->leases.AddToTail( static_cast<ShadowTargetDX12 *>( RetainShadowDepthTarget( id ) ) );
	}
	packet->view.localTargets = packet->ids.Base();
	packet->view.lights = packet->lights.Base();
	packet->view.tileRanges = packet->ranges.Base();
	packet->view.tileIndices = packet->indices.Base();
	m_Impl->Status( view.mapGeneration, view.viewGeneration, DX12_LIGHTING_STATUS_PENDING );
	Enqueue( packet );
}
void CLightingDX12::BeginShadowPass( DX12ShadowTarget_t target, int x, int y, int width, int height, bool clear )
{
	LightingPacketDX12 *packet = Packet( Pass );
	packet->leases.AddToTail( static_cast<ShadowTargetDX12 *>( RetainShadowDepthTarget( target ) ) );
	packet->values[0] = x; packet->values[1] = y; packet->values[2] = width; packet->values[3] = height;
	packet->flag = clear;
	Enqueue( packet );
}
void CLightingDX12::EndShadowPass() { Enqueue( Packet( EndPass ) ); }
void CLightingDX12::CopyShadowDepthRect( DX12ShadowTarget_t dst, DX12ShadowTarget_t src, int dstX, int dstY, int srcX, int srcY, int width, int height )
{
	LightingPacketDX12 *packet = Packet( Restore );
	packet->valid = dst != src;
	packet->leases.AddToTail( static_cast<ShadowTargetDX12 *>( RetainShadowDepthTarget( dst ) ) );
	packet->leases.AddToTail( static_cast<ShadowTargetDX12 *>( RetainShadowDepthTarget( src ) ) );
	packet->values[0] = dstX; packet->values[1] = dstY; packet->values[2] = srcX; packet->values[3] = srcY; packet->values[4] = width; packet->values[5] = height;
	Enqueue( packet );
}
void CLightingDX12::EndView() { Enqueue( Packet( EndScope ) ); }
void CLightingDX12::UnloadMap( uint32 generation )
{
	LightingPacketDX12 *packet = Packet( Unload );
	packet->map.mapGeneration = generation;
	Enqueue( packet );
}

void CLightingDX12::Execute( LightingPacketDX12 *packet )
{
	Impl &s = *m_Impl;
	if ( !s.device || !s.api || !s.device->CommandList() )
	{
		s.Fail( kPacketError, packet->operation == View ? packet->view.mapGeneration : packet->map.mapGeneration, packet->view.viewGeneration );
		return;
	}
	CCommandRecorderDX12 *list = s.device->CommandList();
	ID3D12Device *device = s.device->NativeDevice();
	const uint64_t fence = s.device->NextFenceValue();
	if ( packet->operation == View )
	{
		s.currentReceiverPage.reset();
		Impl::ViewState *scope;
		if ( s.freeViews.Count() ) { scope = s.freeViews.Tail(); s.freeViews.RemoveMultipleFromTail( 1 ); *scope = {}; }
		else scope = new Impl::ViewState;
		scope->packet = packet;
		packet->AddRef();
		{
			AUTO_LOCK( s.mutex );
			scope->nullTable = s.mapNullTable;
		}
		s.views.AddToTail( scope );
		const DX12LightingViewPacket &v = packet->view;
		scope->highresScope = s.highresMap;
		if ( scope->highresScope ) s.device->Highres().BeginView( v.nativeMapGeneration, v.styles );
		const char *failure = nullptr;
		if ( !packet->valid || !v.viewGeneration || v.mapGeneration != s.activeMap || v.viewportWidth <= 0 || v.viewportHeight <= 0 || v.constants.cShadowView0[0] != v.mapGeneration || v.constants.cShadowView0[1] != v.viewGeneration || v.constants.cShadowView1[2] != v.lightCount || v.constants.cShadowView0[2] > DX12_SHADOW_FILTER_PCSS || v.constants.cShadowView0[3] > DX12_SHADOW_DEBUG_PCSS_RADIUS || uint64_t( v.constants.cShadowView1[0] ) * v.constants.cShadowView1[1] != v.tileCount || v.constants.cShadowView1[0] != ( uint32( v.viewportWidth ) + 15 ) / 16 || v.constants.cShadowView1[1] != ( uint32( v.viewportHeight ) + 15 ) / 16 ) failure = kPacketError;
		if ( !failure && ( v.highresRoute != uint32( s.highresMap ) || v.nativeMapGeneration != s.nativeMapGeneration ) )
			failure = kPacketError;
		if ( !failure && ( ( v.cascadeAtlasTarget && !packet->leases[0] ) || ( v.staticSunTarget && !packet->leases[1] ) ) ) failure = kPacketError;
		if ( !failure && ( ( v.constants.cShadowView1[3] & DX12_SHADOW_VIEW_CSM_VALID ) && !v.cascadeAtlasTarget || ( v.constants.cShadowView1[3] & DX12_SHADOW_VIEW_STATIC_SUN_VALID ) && !v.staticSunTarget ) ) failure = SHADOWMAP_ERR_RESIDENCY;
		const float *viewFloats = v.constants.cShadowViewport;
		for ( size_t i = 0; !failure && i < ( offsetof( DX12LightingViewConstantsV1, cCascadeRects ) - offsetof( DX12LightingViewConstantsV1, cShadowViewport ) ) / sizeof( float ); ++i )
			if ( !ShadowMap_IsFiniteFloat( viewFloats[i] ) ) failure = kPacketError;
		if ( !failure && ( v.constants.cShadowView1[3] & DX12_SHADOW_VIEW_CSM_VALID ) )
		{
			for ( uint32 i = 0; i < DX12_SHADOW_CSM_CASCADES; ++i )
			{
				const uint32 *rect = v.constants.cCascadeRects[i];
				const float *depth = v.constants.cShadowDepthRecords[i];
				if ( rect[2] != DX12_SHADOW_CSM_SLOT_SIZE || rect[3] || !RectFits( rect[0], rect[1], rect[2], rect[2], DX12_SHADOW_CSM_ATLAS_SIZE, DX12_SHADOW_CSM_ATLAS_SIZE ) || depth[1] <= depth[0] || depth[2] <= 0 ) failure = kPacketError;
			}
		}
		if ( !failure && ( v.constants.cShadowView1[3] & DX12_SHADOW_VIEW_STATIC_SUN_VALID ) )
		{
			const uint32 *rect = v.constants.cStaticSunRect;
			const float *depth = v.constants.cShadowDepthRecords[4];
			if ( rect[0] || rect[1] || rect[2] != DX12_SHADOW_STATIC_SUN_SIZE || rect[3] || depth[1] <= depth[0] || depth[2] <= 0 ) failure = kPacketError;
		}
		for ( int i = 0; !failure && i < packet->leases.Count(); ++i )
		{
			ShadowTargetDX12 *target = packet->leases[i];
			if ( !target ) { if ( i >= 2 && packet->ids[i - 2] ) failure = kPacketError; continue; }
			const int required = i == 0 ? DX12_SHADOW_CSM_ATLAS_SIZE : i == 1 ? DX12_SHADOW_STATIC_SUN_SIZE : DX12_SHADOW_LOCAL_PAGE_SIZE;
			if ( target->width != required || target->height != required ) failure = kPacketError;
		}
		for ( int i = 0; !failure && i < packet->lights.Count(); ++i )
		{
			const RuntimeShadowLightGpu &light = packet->lights[i];
			if ( light.type != DX12_SHADOW_LIGHT_POINT && light.type != DX12_SHADOW_LIGHT_SPOT || ( light.faceCount != 1 && light.faceCount != 6 ) || light.type == DX12_SHADOW_LIGHT_POINT && light.faceCount != 6 || !ShadowMap_IsFiniteFloat( light.shadowNear ) || !ShadowMap_IsFiniteFloat( light.shadowFar ) || light.shadowNear <= 0 || light.shadowFar <= light.shadowNear ) { failure = kPacketError; break; }
			const float *lightFloats = light.origin;
			for ( size_t f = 0; f < ( offsetof( RuntimeShadowLightGpu, faces ) - offsetof( RuntimeShadowLightGpu, origin ) ) / sizeof( float ); ++f )
				if ( !ShadowMap_IsFiniteFloat( lightFloats[f] ) ) failure = kPacketError;
			if ( failure ) break;
			bool owned = false;
			{
				AUTO_LOCK( s.mutex );
				owned = light.lightId < uint32( s.selected.Count() ) && s.selected[light.lightId].type == light.type;
			}
			if ( !owned ) { failure = kPacketError; break; }
			for ( uint32 face = 0; face < light.faceCount; ++face )
			{
				const uint32 *rect = light.faces[face];
				if ( rect[0] >= v.localTargetCount || !packet->leases[2 + rect[0]] ) { failure = SHADOWMAP_ERR_RESIDENCY; break; }
				if ( rect[3] != DX12_SHADOW_LOCAL_SLOT_SIZE || rect[1] % DX12_SHADOW_LOCAL_SLOT_SIZE || rect[2] % DX12_SHADOW_LOCAL_SLOT_SIZE || !RectFits( rect[1], rect[2], rect[3], rect[3], DX12_SHADOW_LOCAL_PAGE_SIZE, DX12_SHADOW_LOCAL_PAGE_SIZE ) ) { failure = kPacketError; break; }
			}
		}
		for ( uint32 i = 0; !failure && i < v.tileCount; ++i )
			if ( packet->ranges[i * 2] > v.tileIndexCount || packet->ranges[i * 2 + 1] > v.tileIndexCount - packet->ranges[i * 2] ) failure = kPacketError;
		for ( uint32 index : packet->indices ) if ( index >= v.lightCount ) failure = kPacketError;
		if ( failure ) s.Fail( failure, v.mapGeneration, v.viewGeneration );
		else
		{
			CPipelineCacheDX12::BindingInputDX12 input( s.api->m_Pipeline.NullShaderResourceView() );
			input.retireFence = fence;
			PrepareReceiverDraw( true, input );
		}
		Reclaim();
		return;
	}
	if ( packet->operation == EndScope )
	{
		s.currentReceiverPage.reset();
		if ( !s.views.Count() ) { s.Fail( kPacketError ); return; }
		Impl::ViewState *scope = s.views.Tail();
		const uint32 map = scope->packet->view.mapGeneration;
		const uint32 view = scope->packet->view.viewGeneration;
		if ( scope->highresScope ) s.device->Highres().EndView();
		s.views.RemoveMultipleFromTail( 1 );
		scope->packet->Release();
		scope->packet = nullptr;
		scope->nullTable.Reset();
		s.freeViews.AddToTail( scope );
		s.api->m_Pipeline.InvalidateGraphicsBindings();
		s.Status( map, view, DX12_LIGHTING_STATUS_READY );
		{
			AUTO_LOCK( s.mutex );
			const uint32_t status = s.statuses.Find( s.Key( map, view ) );
			s.statuses[status].completed = true;
		}
		return;
	}
	if ( packet->operation == Unload )
	{
		const uint32 map = packet->map.mapGeneration;
		{
			AUTO_LOCK( s.mutex );
			s.currentReceiverPage.reset();
			const auto receiver = s.receiverMaps.find( map );
			if ( receiver != s.receiverMaps.end() )
			{
				receiver->second->retired = true;
				receiver->second->fence = MAX( receiver->second->fence, fence );
				receiver->second->draws.clear();
			}
			if ( s.activeMap == map )
			{
				s.activeMap = 0;
				s.highresMap = false; s.nativeMapGeneration = 0;
				s.selected.RemoveAll();
				s.mapNullTable.Reset();
			}
			// Unconsumed EndView results outlive map GPU storage; delayed callers consume them via GetStatus.
		}
		Reclaim();
		return;
	}
	if ( packet->operation == EndPass )
	{
		if ( !s.passes.Count() ) { s.Fail( kPacketError ); return; }
		const Impl::PassState saved = s.passes.Tail();
		s.passes.RemoveMultipleFromTail( 1 );
		s.RestoreBackend( saved );
		if ( saved.target ) saved.target->Release();
		return;
	}
	if ( packet->operation == Pass )
	{
		Impl::PassState saved;
		memcpy( saved.viewports, s.api->m_Viewports, sizeof( saved.viewports ) );
		saved.viewportCount = s.api->m_nViewportCount;
		memcpy( saved.scissor, s.api->m_FastIntParams, sizeof( saved.scissor ) );
		saved.raster = s.api->m_RasterState; saved.rasterOverride = s.api->m_bRasterOverride;
		saved.cullMode = s.api->m_CullMode;
		saved.snapshot = s.api->m_ActiveSnapshot; saved.snapshotId = s.api->m_hActiveSnapshotId;
		saved.vs = s.api->m_hBoundVS; saved.ps = s.api->m_hBoundPS; saved.gs = s.api->m_hBoundGS;
		saved.vertexIndex = s.api->m_nVertexShaderIndex; saved.pixelIndex = s.api->m_nPixelShaderIndex;
		saved.namedVertex = s.api->m_bBoundVertexShaderIsNamed; saved.namedPixel = s.api->m_bBoundPixelShaderIsNamed;
		memcpy( saved.colors, s.api->m_RenderTargets, sizeof( saved.colors ) );
		saved.colorHandle = s.api->m_hRenderTarget; saved.depthHandle = s.api->m_hDepthTarget;
		D3D12_VIEWPORT viewport{}; D3D12_RECT scissor{};
		if ( s.passes.Count() ) PrepareShadowDraw( saved.savedTargets, viewport, scissor );
		else s.api->PrepareRenderTargets( saved.savedTargets );
		for ( ID3D12Resource *resource : saved.savedTargets.colors ) if ( resource ) s.api->m_Pipeline.RetainExternalResource( resource, fence );
		if ( saved.savedTargets.depth ) s.api->m_Pipeline.RetainExternalResource( saved.savedTargets.depth, fence );
		// The light view is not reflected: reset only the parent's winding, preserving material nocull.
		s.api->m_CullMode = MATERIAL_CULLMODE_CCW;
		saved.target = packet->leases[0];
		if ( saved.target ) saved.target->AddRef();
		const bool rectValid = saved.target && RectFits( packet->values[0], packet->values[1], packet->values[2], packet->values[3], saved.target->width, saved.target->height );
		saved.rect = { packet->values[0], packet->values[1], packet->values[0], packet->values[1] };
		if ( rectValid )
		{
			saved.rect.right += packet->values[2];
			saved.rect.bottom += packet->values[3];
		}
		s.passes.AddToTail( saved );
		++s.api->m_nPipelineMemoEpoch;
		s.api->m_bNamedVertexShaderDirty = true;
		s.api->m_bNamedPixelShaderDirty = true;
		s.api->m_Pipeline.InvalidateGraphicsBindings();
		if ( !rectValid ) { s.Fail( kPacketError ); return; }
		s.Use( saved.target, false );
		list->OMSetRenderTargets( 0, nullptr, FALSE, &saved.target->dsv );
		viewport = { float( saved.rect.left ), float( saved.rect.top ), float( packet->values[2] ), float( packet->values[3] ), 0, 1 };
		list->RSSetViewports( 1, &viewport );
		list->RSSetScissorRects( 1, &saved.rect );
		if ( packet->flag ) list->ClearDepthStencilView( saved.target->dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 1, &saved.rect );
		return;
	}
	if ( packet->operation == Restore )
	{
		ShadowTargetDX12 *dst = packet->leases[0], *src = packet->leases[1];
		if ( !packet->valid || !dst || !src || !RectFits( packet->values[0], packet->values[1], packet->values[4], packet->values[5], dst->width, dst->height ) || !RectFits( packet->values[2], packet->values[3], packet->values[4], packet->values[5], src->width, src->height ) ) { s.Fail( kPacketError ); return; }
		if ( !s.EnsureRestore() ) { s.Fail( SHADOWMAP_ERR_SHADER_UNAVAILABLE ); return; }
		s.Use( src, true ); s.Use( dst, false );
		const uint32 constants[16] = { uint32( packet->values[2] ), uint32( packet->values[3] ), uint32( packet->values[4] ), uint32( packet->values[5] ), uint32( packet->values[0] ), uint32( packet->values[1] ), uint32( packet->values[4] ), uint32( packet->values[5] ), uint32( src->width ), uint32( src->height ), 0, 0, uint32( dst->width ), uint32( dst->height ), 0, 0 };
		D3D12_GPU_VIRTUAL_ADDRESS cbv = 0;
		DescriptorRangeDX12 table = s.api->m_Pipeline.AllocateTransientResources( 1, fence );
		if ( table.count != 1 || !s.api->m_Pipeline.UploadTransient( constants, sizeof( constants ), 256, 256, fence, cbv ) ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return; }
		device->CopyDescriptorsSimple( 1, table.cpu, src->srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		ID3D12DescriptorHeap *heap = s.api->m_Pipeline.ResourceDescriptorHeap();
		list->SetDescriptorHeaps( 1, &heap );
		list->SetGraphicsRootSignature( s.restoreRoot.Get() );
		list->SetGraphicsRootConstantBufferView( 0, cbv );
		list->SetGraphicsRootDescriptorTable( 1, table.gpu );
		list->SetPipelineState( s.restorePso.Get() );
		list->OMSetRenderTargets( 0, nullptr, FALSE, &dst->dsv );
		D3D12_VIEWPORT viewport{ 0, 0, float( dst->width ), float( dst->height ), 0, 1 };
		D3D12_RECT rect{ packet->values[0], packet->values[1], packet->values[0] + packet->values[4], packet->values[1] + packet->values[5] };
		list->RSSetViewports( 1, &viewport ); list->RSSetScissorRects( 1, &rect );
		list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		list->DrawInstanced( 3, 1, 0, 0 );
		s.api->m_Pipeline.InvalidateGraphicsBindings();
		if ( s.passes.Count() )
		{
			RenderTargetBindingDX12 target{};
			if ( PrepareShadowDraw( target, viewport, rect ) ) { list->OMSetRenderTargets( 0, nullptr, FALSE, &target.dsv ); list->RSSetViewports( 1, &viewport ); list->RSSetScissorRects( 1, &rect ); }
		}
		else
		{
			RenderTargetBindingDX12 target{};
			if ( s.api->PrepareRenderTargets( target ) )
			{
				list->OMSetRenderTargets( target.colorCount, target.rtvs, FALSE, target.depth ? &target.dsv : nullptr );
				viewport = { 0, 0, float( target.width ), float( target.height ), 0, 1 };
				if ( s.api->m_nViewportCount )
				{
					const ShaderViewport_t &v = s.api->m_Viewports[0];
					viewport = { float( v.m_nTopLeftX ), float( v.m_nTopLeftY ), float( v.m_nWidth ), float( v.m_nHeight ), v.m_flMinZ, v.m_flMaxZ };
				}
				rect = { 0, 0, target.width, target.height };
				if ( s.api->m_FastIntParams[4] ) rect = { s.api->m_FastIntParams[0], s.api->m_FastIntParams[1], s.api->m_FastIntParams[2], s.api->m_FastIntParams[3] };
				list->RSSetViewports( 1, &viewport );
				list->RSSetScissorRects( 1, &rect );
			}
		}
		return;
	}
}

bool CLightingDX12::ShadowPassActive() const { return m_Impl->passes.Count() != 0; }
bool CLightingDX12::PrepareShadowDraw( RenderTargetBindingDX12 &target, D3D12_VIEWPORT &viewport, D3D12_RECT &scissor )
{
	Impl &s = *m_Impl;
	if ( !s.passes.Count() ) return false;
	if ( s.device->Highres().EnhancedMap() && s.device->Highres().Rejected() ) return false;
	const Impl::PassState &pass = s.passes.Tail();
	if ( !pass.target || PresentationBlocked() ) return false;
	s.Use( pass.target, false );
	target = {};
	target.depth = pass.target->resource.Get(); target.dsv = pass.target->dsv; target.depthFormat = DXGI_FORMAT_D32_FLOAT; target.width = pass.target->width; target.height = pass.target->height;
	viewport = { float( pass.rect.left ), float( pass.rect.top ), float( pass.rect.right - pass.rect.left ), float( pass.rect.bottom - pass.rect.top ), 0, 1 };
	scissor = pass.rect;
	return true;
}
bool CLightingDX12::PresentationBlocked()
{
	// Client rejection handles the enhanced route; never latch its errors into old Present.
	if ( m_Impl->device && m_Impl->device->Highres().EnhancedMap() ) return false;
	const uint32 generation = ReceiverFeatureGeneration();
	const uint32 active = m_Impl->activeMap.load();
	return ( generation && m_Impl->Failed( generation ) ) || ( active && m_Impl->Failed( active ) );
}
void CLightingDX12::FailRecording( const char *error ) { m_Impl->Fail( error ); }
void CLightingDX12::GetSunVisibilityStats( uint32 generation, DX12LightingSunVisibilityStats &stats )
{
	AUTO_LOCK( m_Impl->mutex );
	const auto found = m_Impl->receiverStats.find( generation );
	stats = found == m_Impl->receiverStats.end() ? DX12LightingSunVisibilityStats{} : found->second;
}
void CLightingDX12::ForgetSunReceiverTexture( ShaderAPITextureHandle_t texture )
{
	Impl &s = *m_Impl;
	AUTO_LOCK( s.mutex );
	for ( auto &entry : s.receiverMaps )
	{
		auto &map = *entry.second;
		auto found = map.pages.find( texture );
		if ( found == map.pages.end() ) continue;
		if ( s.currentReceiverPage == found->second ) s.currentReceiverPage.reset();
		// Resources used by recorded commands already have pipeline fence leases.
		map.pages.erase( found );
		for ( auto i = map.draws.begin(); i != map.draws.end(); )
			if ( i->first[0] == uint64_t( texture ) ) i = map.draws.erase( i ); else ++i;
		std::fill( map.mapped.begin(), map.mapped.end(), 0 );
		for ( const auto &page : map.pages ) for ( const auto &p : page.second->placements ) map.mapped[p.face] = 1;
		map.stats.mappedFaces = uint32( std::count( map.mapped.begin(), map.mapped.end(), 1 ) );
		map.stats.pages = uint32( map.pages.size() );
		s.receiverStats[entry.first] = map.stats;
	}
}
bool CLightingDX12::ResolveSunReceiverDraw( const SunReceiverDrawDX12 &draw, SunReceiverCoordinatesDX12 &coordinates )
{
	coordinates = {};
	Impl &s = *m_Impl;
	s.currentReceiverPage.reset(); // Ordinary/moved draws must never inherit a prior page.
	if ( !draw.pixelShader ) return true;
	auto shader = s.carrierShaders.find( draw.pixelShader->identity );
	if ( shader == s.carrierShaders.end() )
	{
		Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
		D3D12_SHADER_DESC desc{};
		const auto bytes = draw.pixelShader->Bytecode();
		if ( FAILED( D3DReflect( bytes.pShaderBytecode, bytes.BytecodeLength, IID_PPV_ARGS( &reflection ) ) ) || FAILED( reflection->GetDesc( &desc ) ) )
		{ s.Fail( SHADOWMAP_ERR_SHADER_UNAVAILABLE ); return false; }
		bool carrier = false;
		for ( UINT i = 0; i < desc.BoundResources; ++i )
		{
			D3D12_SHADER_INPUT_BIND_DESC binding{};
			if ( FAILED( reflection->GetResourceBindingDesc( i, &binding ) ) ) { s.Fail( SHADOWMAP_ERR_SHADER_UNAVAILABLE ); return false; }
			carrier = carrier || ( binding.Type == D3D_SIT_TEXTURE && binding.Space == DX12_LIGHTING_REGISTER_SPACE && binding.BindPoint == DX12_LIGHTING_T_SUN_VISIBILITY );
		}
		shader = s.carrierShaders.emplace( draw.pixelShader->identity, carrier ).first;
	}
	if ( !shader->second )
	{
		const bool expected = draw.pixelLogical && ( !V_strcmp( draw.pixelLogical, "lightmappedgeneric_shadowmap_ps51" ) ||
			!V_strcmp( draw.pixelLogical, "worldtwotextureblend_shadowmap_ps51" ) || !V_strcmp( draw.pixelLogical, "lightmappedreflective_shadowmap_ps51" ) ||
			!V_strcmp( draw.pixelLogical, "lightmappedgeneric_decal_shadowmap_ps51" ) );
		if ( expected ) { s.Fail( SHADOWMAP_ERR_SHADER_UNAVAILABLE ); return false; }
		return true;
	}
	const uint32 generation = s.views.Count() ? s.views.Tail()->packet->view.mapGeneration : s.activeMap.load();
	std::shared_ptr<ReceiverMapDX12> retainedMap;
	{
		AUTO_LOCK( s.mutex );
		const auto found = s.receiverMaps.find( generation );
		if ( found != s.receiverMaps.end() && !found->second->retired ) retainedMap = found->second;
	}
	const char *diagnosticStage = "map";
	const auto reject = [&]()
	{
		if ( !retainedMap || !retainedMap->stats.unresolvedDraws )
			Warning( "Sun receiver diagnostic: stage=%s vs=%s ps=%s indices=%d first=%d\n",
				diagnosticStage, draw.vertexLogical ? draw.vertexLogical : "-", draw.pixelLogical ? draw.pixelLogical : "-", draw.indexCount, draw.firstIndex );
		if ( retainedMap )
		{
			AUTO_LOCK( s.mutex );
			++retainedMap->stats.unresolvedDraws;
			s.receiverStats[generation] = retainedMap->stats;
		}
		s.Fail( SHADOWMAP_ERR_RECEIVER_MAPPING, generation );
		return false;
	};
	if ( !retainedMap ) return generation ? reject() : true;
	auto &map = *retainedMap;
	if ( map.faces.empty() ) return true; // Local-only conversion has no scalar carrier.
	diagnosticStage = "layout";
	// The reviewed logical mapping is explicit: reflective's page is s3, not s1.
	int sampler = -1;
	bool decal = s.api->m_ActiveSnapshot.polygonOffset == SHADER_POLYOFFSET_DECAL;
	if ( !draw.vertexLogical || !draw.pixelLogical ) return reject();
	if ( !V_strcmp( draw.vertexLogical, "lightmappedgeneric_shadowmap_vs51" ) &&
	     ( !V_strcmp( draw.pixelLogical, "lightmappedgeneric_shadowmap_ps51" ) || !V_strcmp( draw.pixelLogical, "worldtwotextureblend_shadowmap_ps51" ) ) ) sampler = 1;
	else if ( !V_strcmp( draw.vertexLogical, "lightmappedreflective_shadowmap_vs51" ) && !V_strcmp( draw.pixelLogical, "lightmappedreflective_shadowmap_ps51" ) ) sampler = 3;
	else if ( !V_strcmp( draw.vertexLogical, "lightmappedgeneric_decal_shadowmap_vs51" ) && !V_strcmp( draw.pixelLogical, "lightmappedgeneric_decal_shadowmap_ps51" ) ) { sampler = 1; decal = true; }
	if ( sampler == -1 ) return reject();
	// The actual VS model rows, not a guessed mesh class or matrix-stack identity.
	// Submodels at identity are considered below by recorded BSP ownership.
	for ( int row = 0; row < 3; ++row ) for ( int column = 0; column < 4; ++column )
		if ( s.api->m_VsFloat[VERTEX_SHADER_MODEL + row][column] != ( row == column ? 1.f : 0.f ) ) return true;
	if ( !draw.layout || !draw.layout->valid || draw.firstIndex < 0 || draw.indexCount < 3 ||
	     ( draw.primitive != MATERIAL_TRIANGLES && draw.primitive != MATERIAL_TRIANGLE_STRIP ) ||
	     ( draw.primitive == MATERIAL_TRIANGLES && draw.indexCount % 3 ) ) return reject();
	const VertexInputDX12 *position = nullptr, *uv = nullptr, *normal = nullptr;
	for ( uint32 i = 0; i < draw.layout->inputCount; ++i )
	{
		const auto &element = draw.layout->inputs[i];
		if ( !V_strcmp( element.semantic, "POSITION" ) && !element.semanticIndex ) position = &element;
		if ( !V_strcmp( element.semantic, "TEXCOORD" ) && element.semanticIndex == 1 ) uv = &element;
		if ( !V_strcmp( element.semantic, "NORMAL" ) && !element.semanticIndex ) normal = &element;
	}
	if ( !position || !uv ) return reject();
	diagnosticStage = "texture";
	const ShaderAPITextureHandle_t handle = s.api->m_BoundTextures[sampler];
	const auto *texture = s.api->FindTexture( handle );
	if ( !texture || texture->depth != 1 || texture->width <= 0 || texture->height <= 0 || texture->width > 16384 || texture->height > 16384 ||
	     ( texture->flags & ( TEXTURE_CREATE_CUBEMAP | TEXTURE_CREATE_DEPTHBUFFER ) ) ) return reject();
	diagnosticStage = "streams";
	ReceiverDrawKeyDX12 key{};
	key[0] = uint64_t( handle ); key[1] = draw.pixelShader->identity; key[2] = draw.primitive;
	key[3] = draw.firstIndex; key[4] = draw.indexCount;
	key[5] = draw.indices ? draw.indices->Identity() : 0; key[6] = draw.indices ? draw.indices->ContentVersion() : 0;
	key[7] = draw.indexOffset; key[8] = draw.indices ? draw.indices->IndexSize() : 0;
	key[39] = decal; // A footprint proof is not a base-surface geometry proof.
	size_t k = 9;
	for ( const auto *element : { position, uv, normal } )
	{
		if ( !element ) { k += 10; continue; }
		if ( element->inputSlot >= ARRAYSIZE( draw.streams ) || !draw.streams[element->inputSlot].buffer ) return reject();
		const auto &stream = draw.streams[element->inputSlot];
		key[k++] = stream.buffer->Identity(); key[k++] = stream.buffer->ContentVersion();
		key[k++] = stream.byteOffset; key[k++] = stream.firstVertex; key[k++] = stream.vertexCount;
		key[k++] = stream.buffer->Stride(); key[k++] = stream.repetitions;
		key[k++] = element->inputSlot; key[k++] = element->byteOffset; key[k++] = element->format;
	}
	const auto version = [&]( uint64_t identity, uint64_t content, bool index )
	{
		if ( !identity ) return;
		auto found = map.bufferVersions.find( identity );
		if ( found == map.bufferVersions.end() ) { map.bufferVersions.emplace( identity, content ); return; }
		if ( found->second == content ) return;
		found->second = content;
		for ( auto entry = map.draws.begin(); entry != map.draws.end(); )
		{
			bool obsolete = index ? entry->first[5] == identity :
				( entry->first[9] == identity || entry->first[19] == identity || entry->first[29] == identity );
			if ( obsolete ) entry = map.draws.erase( entry ); else ++entry;
		}
	};
	version( key[5], key[6], true );
	for ( size_t field : { size_t( 9 ), size_t( 19 ), size_t( 29 ) } ) version( key[field], key[field + 1], false );
	auto cached = map.draws.find( key );
	if ( cached != map.draws.end() )
	{
		s.currentReceiverPage = cached->second.page;
		coordinates = cached->second.Coordinates();
		map.fence = s.device->NextFenceValue();
		return true;
	}
	std::vector<ReceiverPlacementDX12> pending;
	std::vector<ReceiverVertexDX12> vertices( draw.indexCount );
	std::vector<std::array<double, 3>> normals( normal ? draw.indexCount : 0 );
	diagnosticStage = "indices/elements";
	const uint32 indexSize = draw.indices ? draw.indices->IndexSize() : 0;
	if ( draw.indices && indexSize != 2 && indexSize != 4 ) return reject();
	const auto indexBytes = draw.indices ? draw.indices->Data() : ByteSpanDX12<const unsigned char>{ nullptr, 0 };
	if ( draw.indices && ( draw.indexOffset > indexBytes.size() || uint64_t( draw.firstIndex ) + draw.indexCount > ( indexBytes.size() - draw.indexOffset ) / indexSize ) ) return reject();
	const auto vertexIndex = [&]( int element )
	{
		uint32 index = uint32( draw.firstIndex ) + uint32( element );
		if ( draw.indices )
		{
			const auto *address = indexBytes.data() + draw.indexOffset + size_t( index ) * indexSize;
			if ( indexSize == 2 ) { uint16 value; memcpy( &value, address, 2 ); index = value; } else memcpy( &index, address, 4 );
		}
		return index;
	};
	for ( int i = 0; i < draw.indexCount; ++i )
	{
		const uint32 index = vertexIndex( i );
		if ( !ReceiverElement( draw, *position, index, vertices[i].position, 3 ) || !ReceiverElement( draw, *uv, index, vertices[i].uv, 2 ) ||
		     ( normal && !ReceiverElement( draw, *normal, index, normals[i].data(), 3 ) ) ) return reject();
	}
	auto page = map.pages.find( handle );
	diagnosticStage = "decal";
	if ( decal )
	{
		if ( page == map.pages.end() ) return reject();
		struct Remap { int first; std::array<std::array<float, 2>, 3> uv; };
		std::vector<Remap> remaps;
		std::vector<uint32> candidates;
		const int step = draw.primitive == MATERIAL_TRIANGLES ? 3 : 1;
		const auto degenerate = [&]( int first )
		{
			double cross[3];
			for ( int a = 0; a < 3; ++a )
				cross[a] = ( vertices[first+1].position[(a+1)%3]-vertices[first].position[(a+1)%3] ) *
					( vertices[first+2].position[(a+2)%3]-vertices[first].position[(a+2)%3] ) -
					( vertices[first+2].position[(a+1)%3]-vertices[first].position[(a+1)%3] ) *
					( vertices[first+1].position[(a+2)%3]-vertices[first].position[(a+2)%3] );
			return cross[0] == 0 && cross[1] == 0 && cross[2] == 0;
		};
		const auto ownerAt = [&]( const std::array<float, 2> &uv )
		{
			const float tx = float( texture->width )*uv[0], ty = float( texture->height )*uv[1];
			if ( !( tx >= 0 && tx < texture->width && ty >= 0 && ty < texture->height ) ) return uint32( 0 );
			return page->second->pixels[size_t( int( ty ) )*texture->width+int( tx )] >> 8;
		};
		for ( int first = 0; first+2 < draw.indexCount; first += step )
		{
			if ( degenerate( first ) ) continue;
			std::array<std::array<float, 2>, 3> selected;
			const int remapped = ReceiverDisplacementDecal( map, *page->second, &vertices[first], selected, candidates );
			if ( remapped < 0 )
			{
				diagnosticStage = "decal displacement";
				if ( !map.stats.unresolvedDraws ) Warning( "Sun receiver displacement decal: triangle=%d p=(%.9g %.9g %.9g)\n",
					first/step, vertices[first].position[0], vertices[first].position[1], vertices[first].position[2] );
				if ( !map.stats.unresolvedDraws )
				{
					for ( int v = 0; v < 3; ++v ) for ( uint32 e = 0; e < draw.layout->inputCount; ++e )
					{
						const auto &element = draw.layout->inputs[e];
						if ( V_strcmp(element.semantic,"TEXCOORD") ) continue;
						double value[4] = {};
						for ( int count = 4; count > 0; --count )
							if ( ReceiverElement(draw,element,vertexIndex(first+v),value,count) )
							{
								Warning("Receiver semantic: corner=%d texcoord=%u format=%u count=%d value=(%.12g %.12g %.12g %.12g)\n",
									v, element.semanticIndex, unsigned(element.format), count, value[0],value[1],value[2],value[3]);
								break;
							}
					}
					for ( uint32 f : candidates ) for ( const auto &vertex : map.referenceVertices[f] )
						if ( vertex.second.normalState == 1 )
							Warning("Receiver normal dump: dface=%u p=(%.12g %.12g %.12g) n=(%.12g %.12g %.12g)\n",
								map.faces[f].dfaceIndex,vertex.first[0],vertex.first[1],vertex.first[2],
								vertex.second.normal[0],vertex.second.normal[1],vertex.second.normal[2]);
					for ( int v = 0; v < draw.indexCount; ++v )
						Warning("Receiver batch dump: element=%d id=%u p=(%.12g %.12g %.12g) uv=(%.12g %.12g)\n",
							v,vertexIndex(v),vertices[v].position[0],vertices[v].position[1],vertices[v].position[2],vertices[v].uv[0],vertices[v].uv[1]);
				}
				return reject();
			}
			bool changed = false;
			uint32 owner = 0;
			for ( int corner = 0; corner < 3; ++corner )
			{
				const std::array<float, 2> original{ float( vertices[first+corner].uv[0] ), float( vertices[first+corner].uv[1] ) };
				if ( !remapped ) selected[corner] = original;
				changed = changed || selected[corner] != original;
				const uint32 candidate = ownerAt( selected[corner] );
				if ( !candidate || (owner && owner != candidate) )
				{
					if ( !map.stats.unresolvedDraws ) Warning( "Sun receiver decal ownership: triangle=%d corner=%d expected=%u actual=%u\n", first/step, corner, owner, candidate );
					return reject();
				}
				owner = candidate;
			}
			if ( changed ) remaps.push_back( { first, selected } );
		}
		ReceiverDrawProofDX12 proof{ page->second, {} };
		if ( !remaps.empty() )
		{
			uint32 maximum = 0;
			for ( int i = 0; i < draw.indexCount; ++i ) maximum = MAX( maximum, vertexIndex( i ) );
			if ( maximum >= UINT_MAX/sizeof( std::array<float, 2> ) ) return reject();
			proof.coordinates.assign( size_t( maximum )+1, { -FLT_MAX, -FLT_MAX } );
			size_t next = 0;
			for ( int first = 0; first+2 < draw.indexCount; first += step )
			{
				if ( degenerate( first ) ) continue;
				const Remap *remap = next < remaps.size() && remaps[next].first == first ? &remaps[next++] : nullptr;
				uint32 owner = 0;
				for ( int corner = 0; corner < 3; ++corner )
				{
					const std::array<float, 2> expected = remap ? remap->uv[corner] :
						std::array<float, 2>{ float( vertices[first+corner].uv[0] ), float( vertices[first+corner].uv[1] ) };
					auto &stored = proof.coordinates[vertexIndex( first+corner )];
					if ( stored[0] == -FLT_MAX ) stored = expected;
					else if ( !ReceiverNear( texture->width*stored[0], texture->width*expected[0] ) ||
						!ReceiverNear( texture->height*stored[1], texture->height*expected[1] ) ) return reject();
					const uint32 candidate = ownerAt( stored );
					if ( !candidate || (owner && owner != candidate) ) return reject();
					owner = candidate;
				}
			}
			// Degenerate primitives still fetch vertices, but do not constrain visible receivers.
			for ( int i = 0; i < draw.indexCount; ++i )
			{
				auto &stored = proof.coordinates[vertexIndex( i )];
				if ( stored[0] == -FLT_MAX ) stored = { float( vertices[i].uv[0] ), float( vertices[i].uv[1] ) };
			}
		}
		s.currentReceiverPage = page->second;
		const auto saved = map.draws.emplace( key, std::move( proof ) );
		coordinates = saved.first->second.Coordinates();
		map.fence = s.device->NextFenceValue();
		return true;
	}
	bool world = false, submodel = false, normalsChanged = false;
	diagnosticStage = "geometry";
	const int step = draw.primitive == MATERIAL_TRIANGLES ? 3 : 1;
	for ( int first = 0; first + 2 < draw.indexCount; first += step )
	{
		int offsets[3]{ first, first + 1, first + 2 };
		if ( step == 1 && ( first & 1 ) ) std::swap( offsets[0], offsets[1] );
		ReceiverVertexDX12 triangle[3]{ vertices[offsets[0]], vertices[offsets[1]], vertices[offsets[2]] };
		double cross[3];
		for ( int a = 0; a < 3; ++a ) cross[a] = ( triangle[1].position[( a + 1 ) % 3] - triangle[0].position[( a + 1 ) % 3] ) * ( triangle[2].position[( a + 2 ) % 3] - triangle[0].position[( a + 2 ) % 3] ) -
		                                                 ( triangle[1].position[( a + 2 ) % 3] - triangle[0].position[( a + 2 ) % 3] ) * ( triangle[2].position[( a + 1 ) % 3] - triangle[0].position[( a + 1 ) % 3] );
		if ( cross[0] == 0 && cross[1] == 0 && cross[2] == 0 ) continue; // Strip/list degenerates have no receiver pixels.
		ReceiverBoxDX12 box;
		for ( const auto &v : triangle ) for ( int a = 0; a < 3; ++a ) { box.lo[a] = MIN( box.lo[a], v.position[a] ); box.hi[a] = MAX( box.hi[a], v.position[a] ); }
		std::vector<uint32> candidates;
		map.Candidates( 0, box, candidates );
		std::vector<ReceiverPlacementDX12> matches;
		for ( uint32 f : candidates )
		{
			const auto &face = map.faces[f];
			bool orientation = true;
			if ( normal && !( face.flags & SHADOWMAP_RECEIVER_DISPLACEMENT ) ) for ( int p = 0; p < 3; ++p )
			{
				double dot = 0; for ( int a = 0; a < 3; ++a ) dot += normals[offsets[p]][a] * face.plane[a];
				orientation = orientation && dot > 0;
			}
			if ( !orientation ) continue;
			ReceiverPlacementDX12 match;
			bool unresolved = false;
			if ( ReceiverFit( map, f, triangle, texture->width, texture->height, match, unresolved ) ) matches.push_back( match );
			if ( unresolved ) return reject();
		}
		if ( matches.empty() )
		{
			if ( !map.stats.unresolvedDraws )
			{
				Warning( "Sun receiver diagnostic: no match candidates=%u page=%dx%d\n", uint32( candidates.size() ), texture->width, texture->height );
				for ( int p = 0; p < 3; ++p )
					Warning( "  vertex %d p=(%.9g %.9g %.9g) uv=(%.9g %.9g) n=(%.9g %.9g %.9g)\n", p,
						triangle[p].position[0], triangle[p].position[1], triangle[p].position[2], triangle[p].uv[0], triangle[p].uv[1],
						normal ? normals[offsets[p]][0] : 0, normal ? normals[offsets[p]][1] : 0, normal ? normals[offsets[p]][2] : 0 );
			}
			return reject();
		}
		bool thisWorld = false, thisSubmodel = false;
		for ( const auto &match : matches ) ( map.faces[match.face].modelIndex ? thisSubmodel : thisWorld ) = true;
		if ( thisWorld && thisSubmodel ) return reject(); // Identity MODEL alone cannot identify coincident brush ownership.
		world = world || thisWorld; submodel = submodel || thisSubmodel;
		for ( size_t i = 1; i < matches.size(); ++i ) if ( !ReceiverEquivalent( map, matches[0], matches[i] ) ) return reject();
		for ( const auto &match : matches )
		{
			if ( normal && ( map.faces[match.face].flags & SHADOWMAP_RECEIVER_DISPLACEMENT ) )
				for ( int vertex = 0; vertex < 3; ++vertex )
				{
					auto &reference = map.referenceVertices[match.face].find( { float( triangle[vertex].position[0] ), float( triangle[vertex].position[1] ), float( triangle[vertex].position[2] ) } )->second;
					const std::array<float, 3> observed{ float( normals[offsets[vertex]][0] ), float( normals[offsets[vertex]][1] ), float( normals[offsets[vertex]][2] ) };
					if ( reference.normalState == 2 ) continue;
					const double normalLengthSquared = double( observed[0] )*observed[0] + double( observed[1] )*observed[1] + double( observed[2] )*observed[2];
					const bool bounded = normalLengthSquared <= 1+8.0*FLT_EPSILON; // Average of unit source normals; bounds the lift radius.
					if ( !bounded || (reference.normalState == 1 && reference.normal != observed) )
					{ reference.normalState = 2; normalsChanged = true; }
					else if ( reference.normalState == 0 )
					{ reference.normal = observed; reference.normalState = 1; normalsChanged = true; }
				}
			bool present = false;
			for ( const auto &existing : pending )
				if ( existing.face == match.face )
				{
					if ( !ReceiverEquivalent( map, existing, match ) ) return reject();
					present = true; break;
				}
			if ( !present ) pending.push_back( match );
		}
	}
	if ( normalsChanged )
		for ( auto cached = map.draws.begin(); cached != map.draws.end(); )
			if ( cached->first[39] ) cached = map.draws.erase( cached ); else ++cached;
	if ( !world && !submodel ) { map.draws.emplace( key, ReceiverDrawProofDX12{} ); return true; }
	if ( page == map.pages.end() )
	{
		auto newPage = std::make_shared<ReceiverPageDX12>();
		if ( !CreateReceiverPage( s.device->NativeDevice(), *newPage, texture->width, texture->height, 0 ) ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
		page = map.pages.emplace( handle, newPage ).first;
	}
	auto &companion = *page->second;
	diagnosticStage = "placement";
	if ( companion.width != texture->width || companion.height != texture->height ) return reject();
	// Each owner must remain one rectangle for allocation-clamped filtering.
	// Identical full fields at identical bounds may share the existing owner.
	const auto compatible = [&]( const ReceiverPlacementDX12 &a, const ReceiverPlacementDX12 &b )
	{
		const bool overlap = a.left < b.left+b.width && b.left < a.left+a.width &&
		                     a.top < b.top+b.height && b.top < a.top+a.height;
		return ( a.face != b.face && !overlap ) || ReceiverEquivalent( map, a, b );
	};
	for ( const auto &p : pending )
	{
		for ( const auto &existing : companion.placements ) if ( !compatible( p, existing ) ) return reject();
		for ( const auto &other : pending ) if ( !compatible( p, other ) ) return reject();
	}
	if ( !companion.initialized )
	{
		if ( !UploadReceiverRect( *s.device, s.api->m_Pipeline, companion, 0, 0, companion.width, companion.height ) ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
		companion.initialized = true;
	}
	for ( const auto &p : pending )
	{
		bool present = false;
		for ( const auto &existing : companion.placements ) if ( existing.face == p.face ) { present = true; break; }
		if ( present ) continue;
		const bool covered = ( companion.pixels[size_t( p.top ) * companion.width + p.left] >> 8 ) != 0;
		if ( covered )
		{
			companion.placementByFace.emplace( p.face, companion.placements.size() );
			companion.placements.push_back( p ); map.mapped[p.face] = 1; continue;
		}
		for ( int y = p.top; y < p.top + p.height; ++y ) for ( int x = p.left; x < p.left + p.width; ++x )
		{
			const size_t pixel = size_t( y ) * companion.width + x;
			companion.pixels[pixel] = ( ( p.face+1 ) << 8 ) | ReceiverField( map, p, x, y );
		}
		if ( !UploadReceiverRect( *s.device, s.api->m_Pipeline, companion, p.left, p.top, p.width, p.height ) ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
		companion.placementByFace.emplace( p.face, companion.placements.size() );
		companion.placements.push_back( p ); map.mapped[p.face] = 1;
	}
	// Keep proven white brush coverage for decals, but base brush draws stay
	// runtime-only. A mixed world/brush batch samples the per-texel fields.
	s.currentReceiverPage = world ? page->second : std::shared_ptr<ReceiverPageDX12>();
	map.draws.emplace( key, ReceiverDrawProofDX12{ s.currentReceiverPage, {} } );
	map.fence = s.device->NextFenceValue();
	{
		AUTO_LOCK( s.mutex );
		map.stats.mappedFaces = uint32( std::count( map.mapped.begin(), map.mapped.end(), 1 ) );
		map.stats.pages = uint32( map.pages.size() );
		s.receiverStats[generation] = map.stats;
	}
	return true;
}
bool CLightingDX12::PrepareReceiverDraw( bool lightingAbi, CPipelineCacheDX12::BindingInputDX12 &input )
{
	Impl &s = *m_Impl;
	input.lightingAbi = lightingAbi;
	if ( !lightingAbi ) return true;
	Impl::ViewState &scope = s.views.Count() ? *s.views.Tail() : s.neutral;
	if ( !s.views.Count() && !scope.nullTable )
	{
		if ( !LightingNullTable( s.device->NativeDevice(), scope.nullTable ) ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
	}
	const DX12LightingViewPacket &view = scope.packet->view;
	if ( s.views.Count() && s.Failed( view.mapGeneration, view.viewGeneration ) ) return false;
	CPipelineCacheDX12 &pipeline = s.api->m_Pipeline;
	const uint64_t fence = s.device->NextFenceValue();
	if ( !pipeline.ReserveResourceDescriptors( DX12_LIGHTING_VIEW_TABLE_COUNT + 1 + 8 + 32 + 16, fence ) ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
	const uint64_t heapGeneration = pipeline.ResourceHeapGeneration();
	ID3D12Device *device = s.device->NativeDevice();
	const UINT stride = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	const uint32 counts[3] = { view.lightCount, view.tileCount, view.tileIndexCount };
	const uint32 sizes[3] = { sizeof( RuntimeShadowLightGpu ), 8, 4 };
	if ( scope.fence != fence || scope.heap != heapGeneration )
	{
		scope.table = pipeline.AllocateTransientResources( DX12_LIGHTING_VIEW_TABLE_COUNT, fence );
		if ( scope.table.count != DX12_LIGHTING_VIEW_TABLE_COUNT ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
		if ( scope.fence != fence )
		{
			if ( !pipeline.UploadTransient( &view.constants, sizeof( view.constants ), 768, 256, fence, scope.constants ) ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
			const void *data[3] = { scope.packet->lights.Base(), scope.packet->ranges.Base(), scope.packet->indices.Base() };
			for ( UINT i = 0; i < 3; ++i )
			{
				scope.structured[i] = nullptr;
				scope.structuredOffsets[i] = 0;
				if ( counts[i] && !pipeline.UploadStructured( data[i], size_t( counts[i] ) * sizes[i], sizes[i], fence, &scope.structured[i], &scope.structuredOffsets[i] ) ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
			}
		}
		const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> &nullTable = scope.nullTable;
		if ( !nullTable ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
		device->CopyDescriptorsSimple( DX12_LIGHTING_VIEW_TABLE_COUNT, scope.table.cpu, nullTable->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		for ( int i = 0; i < scope.packet->leases.Count(); ++i )
		{
			ShadowTargetDX12 *target = scope.packet->leases[i];
			if ( !target ) continue;
			s.Use( target, true );
			const UINT slot = i == 0 ? DX12_LIGHTING_T_CASCADE_ATLAS : i == 1 ? DX12_LIGHTING_T_STATIC_SUN : i - 2;
			device->CopyDescriptorsSimple( 1, Offset( scope.table.cpu, slot, stride ), target->srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		}
		for ( UINT i = 0; i < 3; ++i )
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_UNKNOWN; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Buffer.FirstElement = scope.structuredOffsets[i] / sizes[i]; srv.Buffer.NumElements = MAX( 1u, counts[i] ); srv.Buffer.StructureByteStride = sizes[i];
			device->CreateShaderResourceView( scope.structured[i], &srv, Offset( scope.table.cpu, DX12_LIGHTING_T_LIGHTS + i, stride ) );
		}
		scope.fence = fence; scope.heap = heapGeneration;
	}
	// Returning to a parent whose depth was used in an intervening pass requires transitions, but not descriptor copies.
	for ( ShadowTargetDX12 *target : scope.packet->leases ) if ( target && target->state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ) s.Use( target, true );
	input.lightingViewTable = scope.table;
	input.lightingViewConstants = scope.constants;
	if ( s.highresMap ) return !s.device->Highres().Rejected();
	if ( !s.currentReceiverPage )
	{
		if ( !s.whiteReceiverPage )
		{
			s.whiteReceiverPage = std::make_shared<ReceiverPageDX12>();
			if ( !CreateReceiverPage( device, *s.whiteReceiverPage, 1, 1, 0x1ff ) ) { s.whiteReceiverPage.reset(); s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
		}
		s.currentReceiverPage = s.whiteReceiverPage;
	}
	auto &carrier = *s.currentReceiverPage;
	if ( !carrier.initialized )
	{
		if ( !UploadReceiverRect( *s.device, pipeline, carrier, 0, 0, carrier.width, carrier.height ) ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
		carrier.initialized = true;
	}
	pipeline.RetainExternalResource( carrier.resource.Get(), fence );
	carrier.fence = fence;
	Transition( s.device->CommandList(), carrier.resource.Get(), carrier.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	if ( carrier.tableFence != fence || carrier.table.generation != heapGeneration )
	{
		carrier.table = pipeline.AllocateTransientResources( 1, fence );
		if ( carrier.table.count != 1 ) { s.Fail( SHADOWMAP_ERR_RESIDENCY ); return false; }
		device->CopyDescriptorsSimple( 1, carrier.table.cpu, carrier.srvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		carrier.tableFence = fence;
	}
	input.lightingVisibilityTable = carrier.table;
	return true;
}
} // namespace shaderapidx12
