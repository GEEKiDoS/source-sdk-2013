//========= Copyright Valve Corporation, All rights reserved. ============//
// Dense SH-L2 ambient probe grid for VRAD ReSTIR (public/hprobe_bsp.h). A probe is the same quantity as a leaf
// ambient cube (cosine-convolved radiance in the engine's linear cube units) with a directional basis, so it folds
// in the same sources: the full-source lightmap gather and material emitters (restir_probe_sh.comp) and the
// INAMBIENTCUBE surface worldlights (as ReSTIR_ComputeLeafAmbientLighting).

#include <DirectXPackedVector.h> // before the Source headers: they define min/max macros
#include "ambient_probes.h"
#include "ambient_cube.h"
#include "restir_vulkan.h"
#include "bsplib.h"
#include "tier1/utlhashtable.h"
#include <float.h>
#include <math.h>

namespace
{
const int PROBE_CHUNK = 65536;			// probes per GPU batch
const int RAY_CHUNK = 1 << 20;			// worldlight visibility rays per GPU batch
const int PROBES_PER_BRICK = hprobe::kBrickProbes * hprobe::kBrickProbes * hprobe::kBrickProbes;
const int RING_DIRECTIONS = 128;
const int SELF_CHECK_PROBES = 256;
enum { PROBE_INVALID, PROBE_VALID, PROBE_DILATED };

const float SH_Y0 = 0.282095f;
const float SH_A[3] = { 3.14159265f, 2.09439510f, 0.78539816f };	// clamped-cosine convolution per band l = 0, 1, 2
const Vector s_Axes[6] = { Vector( 1, 0, 0 ), Vector( -1, 0, 0 ), Vector( 0, 1, 0 ), Vector( 0, -1, 0 ), Vector( 0, 0, 1 ), Vector( 0, 0, -1 ) };

struct ProbeSh
{
	float e[9][3];		// irradiance coefficients per basis function k and channel
};

bool Fail( const char *why )
{
	Warning( "Hlight: %s (%s mode)\n", why, g_bHDR ? "HDR" : "LDR" );
	return false;
}

int ShBand( int k )
{
	return k == 0 ? 0 : ( k < 4 ? 1 : 2 );
}

void ShBasis( const Vector &d, float y[9] )
{
	y[0] = SH_Y0;
	y[1] = 0.488603f * d.y;
	y[2] = 0.488603f * d.z;
	y[3] = 0.488603f * d.x;
	y[4] = 1.092548f * d.x * d.y;
	y[5] = 1.092548f * d.y * d.z;
	y[6] = 0.315392f * ( 3.0f * d.z * d.z - 1.0f );
	y[7] = 1.092548f * d.x * d.z;
	y[8] = 0.546274f * ( d.x * d.x - d.y * d.y );
}

float Irradiance( const ProbeSh &sh, int channel, const float basis[9] )
{
	float sum = 0.0f;
	for ( int k = 0; k < 9; ++k )
		sum += sh.e[k][channel] * basis[k];
	return sum;
}

ReSTIRGpuRay MakeRay( const Vector &origin, const Vector &delta )
{
	ReSTIRGpuRay ray;
	ray.origin[0] = origin.x;
	ray.origin[1] = origin.y;
	ray.origin[2] = origin.z;
	ray.origin[3] = 0.0f;
	ray.direction[0] = delta.x;
	ray.direction[1] = delta.y;
	ray.direction[2] = delta.z;
	ray.direction[3] = 1.0f;
	return ray;
}

struct ProbeBuild
{
	const ReSTIROptions &options;
	const ReSTIRScene &scene;
	CReSTIRVulkanDevice &device;
	hprobe::GridDisk grid;
	CUtlVector<uint32> indirection;			// per brick cell: slot or hlight::kMissing
	CUtlVector<int> texelProbe;				// [slot * 125 + (lz * 5 + ly) * 5 + lx] -> unique probe
	CUtlHashtable<uint64, int> probeIds;	// ix | iy << 21 | iz << 42 -> unique probe
	CUtlVector<uint64> keys;
	CUtlVector<Vector> positions;
	CUtlVector<uint8> state;
	CUtlVector<ProbeSh> sh;
	int validCount, dilatedCount, deringedCount;

	ProbeBuild( const ReSTIROptions &o, const ReSTIRScene &s, CReSTIRVulkanDevice &d )
		: options( o ), scene( s ), device( d ), validCount( 0 ), dilatedCount( 0 ), deringedCount( 0 )
	{
		memset( &grid, 0, sizeof( grid ) );
	}
};

// Inclusion-exclusion corners of the inclusive brick box [lo, hi] in a 3D difference array.
void AddBox( CUtlVector<int> &delta, int sx, int sy, const int lo[3], const int hi[3] )
{
	for ( int corner = 0; corner < 8; ++corner )
	{
		const int x = ( corner & 1 ) ? hi[0] + 1 : lo[0];
		const int y = ( corner & 2 ) ? hi[1] + 1 : lo[1];
		const int z = ( corner & 4 ) ? hi[2] + 1 : lo[2];
		const bool odd = ( ( corner & 1 ) + ( ( corner >> 1 ) & 1 ) + ( ( corner >> 2 ) & 1 ) ) & 1;
		delta[x + sx * ( y + sy * z )] += odd ? -1 : 1;
	}
}

bool PlanGrid( ProbeBuild &b )
{
	hprobe::GridDisk &g = b.grid;
	const float brickEdge = float( b.options.ambientGridSpacing ) * hprobe::kBrickCells;
	g.spacing = float( b.options.ambientGridSpacing );
	g.reach = b.options.ambientGridReach;
	g.dcFormat = hprobe::kR11G11B10Float;
	g.bandFormat = hprobe::kRGBA8Snorm;
	g.validityFormat = hprobe::kR8Unorm;
	for ( int axis = 0; axis < 3; ++axis )
	{
		g.origin[axis] = floorf( dmodels[0].mins[axis] / brickEdge - 1.0f ) * brickEdge;
		const float bricks = ceilf( ( dmodels[0].maxs[axis] - g.origin[axis] ) / brickEdge ) + 1.0f;
		if ( bricks > float( hprobe::kMaxBricksPerAxis ) )
			return Fail( "ambient grid exceeds 1024 bricks per axis; raise -restir_ambientgrid" );
		g.brickDims[axis] = (uint32)bricks;
	}
	if ( hprobe::IndirectionCount( g ) * 4 > hprobe::kMaxGridBytes )
		return Fail( "ambient grid exceeds 256 MiB; raise -restir_ambientgrid or lower -restir_ambientgrid_reach" );
	return true;
}

// A brick is allocated iff it intersects the AABB, dilated by reach, of a lit world or static prop triangle.
// The union of those boxes is rasterized once with a 3D difference array instead of per triangle.
bool AllocateBricks( ProbeBuild &b )
{
	hprobe::GridDisk &g = b.grid;
	const int dims[3] = { (int)g.brickDims[0], (int)g.brickDims[1], (int)g.brickDims[2] };
	const int sx = dims[0] + 1, sy = dims[1] + 1, sz = dims[2] + 1;
	const float brickEdge = g.spacing * hprobe::kBrickCells, reach = float( g.reach );
	CUtlVector<int> covered;
	covered.SetCount( sx * sy * sz );
	memset( covered.Base(), 0, covered.Count() * sizeof( int ) );
	for ( int i = 0; i < b.scene.triangles.Count(); ++i )
	{
		const ReSTIRGpuTriangle &triangle = b.scene.triangles[i];
		if ( !( triangle.flags & ( RESTIR_TRI_WORLDFACE | RESTIR_TRI_STATICPROP ) ) || ( triangle.flags & RESTIR_TRI_SKY ) )
			continue;
		int lo[3], hi[3];
		bool inside = true;
		for ( int axis = 0; axis < 3; ++axis )
		{
			const float mins = MIN( MIN( triangle.v0[axis], triangle.v1[axis] ), triangle.v2[axis] );
			const float maxs = MAX( MAX( triangle.v0[axis], triangle.v1[axis] ), triangle.v2[axis] );
			lo[axis] = (int)floorf( ( mins - reach - g.origin[axis] ) / brickEdge );
			hi[axis] = (int)floorf( ( maxs + reach - g.origin[axis] ) / brickEdge );
			inside = inside && hi[axis] >= 0 && lo[axis] < dims[axis];
			lo[axis] = MAX( lo[axis], 0 );
			hi[axis] = MIN( hi[axis], dims[axis] - 1 );
		}
		if ( inside )
			AddBox( covered, sx, sy, lo, hi );
	}
	for ( int z = 0; z < sz; ++z )
		for ( int y = 0; y < sy; ++y )
			for ( int x = 1; x < sx; ++x )
				covered[x + sx * ( y + sy * z )] += covered[x - 1 + sx * ( y + sy * z )];
	for ( int z = 0; z < sz; ++z )
		for ( int y = 1; y < sy; ++y )
			for ( int x = 0; x < sx; ++x )
				covered[x + sx * ( y + sy * z )] += covered[x + sx * ( y - 1 + sy * z )];
	for ( int z = 1; z < sz; ++z )
		for ( int y = 0; y < sy; ++y )
			for ( int x = 0; x < sx; ++x )
				covered[x + sx * ( y + sy * z )] += covered[x + sx * ( y + sy * ( z - 1 ) )];

	b.indirection.SetCount( dims[0] * dims[1] * dims[2] );
	uint32 slots = 0;
	for ( int z = 0; z < dims[2]; ++z )
		for ( int y = 0; y < dims[1]; ++y )
			for ( int x = 0; x < dims[0]; ++x )
				b.indirection[x + dims[0] * ( y + dims[1] * z )] =
					( g.reach == 0 || covered[x + sx * ( y + sy * z )] > 0 ) ? slots++ : hlight::kMissing;
	if ( !slots )
		return Fail( "ambient grid covers no lit geometry" );
	g.brickCount = slots;
	hprobe::AtlasBricks( g.brickCount, g.atlasBricks );
	if ( hprobe::GridBytes( g ) > hprobe::kMaxGridBytes )
		return Fail( "ambient grid exceeds 256 MiB; raise -restir_ambientgrid or lower -restir_ambientgrid_reach" );
	return true;
}

// Edge probes are shared by neighbouring bricks in the world but duplicated in the atlas: trace each once.
void CollectProbes( ProbeBuild &b )
{
	const hprobe::GridDisk &g = b.grid;
	b.texelProbe.SetCount( g.brickCount * PROBES_PER_BRICK );
	b.probeIds.Reserve( g.brickCount * hprobe::kBrickCells * hprobe::kBrickCells * hprobe::kBrickCells );
	for ( uint32 bz = 0; bz < g.brickDims[2]; ++bz )
		for ( uint32 by = 0; by < g.brickDims[1]; ++by )
			for ( uint32 bx = 0; bx < g.brickDims[0]; ++bx )
			{
				const uint32 slot = b.indirection[bx + g.brickDims[0] * ( by + g.brickDims[1] * bz )];
				if ( slot == hlight::kMissing )
					continue;
				for ( uint32 lz = 0; lz < hprobe::kBrickProbes; ++lz )
					for ( uint32 ly = 0; ly < hprobe::kBrickProbes; ++ly )
						for ( uint32 lx = 0; lx < hprobe::kBrickProbes; ++lx )
						{
							const uint64 ix = bx * hprobe::kBrickCells + lx, iy = by * hprobe::kBrickCells + ly, iz = bz * hprobe::kBrickCells + lz;
							const uint64 key = ix | iy << 21 | iz << 42;
							bool inserted;
							const UtlHashHandle_t handle = b.probeIds.Insert( key, b.positions.Count(), &inserted );
							if ( inserted )
							{
								b.keys.AddToTail( key );
								b.positions.AddToTail( Vector( g.origin[0] + ix * g.spacing, g.origin[1] + iy * g.spacing, g.origin[2] + iz * g.spacing ) );
							}
							b.texelProbe[slot * PROBES_PER_BRICK + ( lz * hprobe::kBrickProbes + ly ) * hprobe::kBrickProbes + lx] = b.probeIds.Element( handle );
						}
			}
}

// Invalid: inside solid or an opaque leaf brush, or a +-axis ray of one spacing starts inside geometry or reaches a
// displacement back face; the BuildLeafCandidates tests of the leaf cube.
bool ComputeValidity( ProbeBuild &b )
{
	const int count = b.positions.Count();
	b.state.SetCount( count );
	CUtlVector<ReSTIRGpuRay> rays;
	CUtlVector<ReSTIRGpuHit> hits;
	for ( int first = 0; first < count; first += PROBE_CHUNK )
	{
		const int end = MIN( first + PROBE_CHUNK, count );
		rays.RemoveAll();
		for ( int i = first; i < end; ++i )
		{
			const int leaf = ReSTIR_PointLeaf( b.positions[i] );
			const bool open = !( dleafs[leaf].contents & CONTENTS_SOLID ) && !ReSTIR_PointInOpaqueLeafBrush( leaf, b.positions[i] );
			b.state[i] = open ? PROBE_VALID : PROBE_INVALID;
			if ( open )
				for ( int side = 0; side < 6; ++side )
					rays.AddToTail( MakeRay( b.positions[i], s_Axes[side] * b.grid.spacing ) );
		}
		if ( rays.Count() && !b.device.TraceRays( rays, RESTIR_RAY_MASK_SHADOW, hits ) )
			return Fail( "ambient probe validity tracing failed" );
		int ray = 0;
		for ( int i = first; i < end; ++i )
		{
			if ( b.state[i] == PROBE_INVALID )
				continue;
			for ( int side = 0; side < 6; ++side, ++ray )
				if ( ReSTIR_AmbientRayBlocked( b.scene, hits[ray], s_Axes[side] * b.grid.spacing ) )
					b.state[i] = PROBE_INVALID;
		}
	}
	for ( int i = 0; i < count; ++i )
		b.validCount += b.state[i] != PROBE_INVALID;
	if ( !b.validCount )
		return Fail( "ambient grid has no probe in open space" );
	return true;
}

bool ProjectCoefficients( ProbeBuild &b )
{
	b.sh.SetCount( b.positions.Count() );
	memset( b.sh.Base(), 0, b.sh.Count() * sizeof( ProbeSh ) );
	CUtlVector<int> valid;
	for ( int i = 0; i < b.positions.Count(); ++i )
		if ( b.state[i] != PROBE_INVALID )
			valid.AddToTail( i );
	CUtlVector<ReSTIRGpuAmbientQuery> queries;
	CUtlVector<ReSTIRGpuProbeResult> results;
	for ( int first = 0; first < valid.Count(); first += PROBE_CHUNK )
	{
		queries.SetCount( MIN( PROBE_CHUNK, valid.Count() - first ) );
		for ( int i = 0; i < queries.Count(); ++i )
		{
			const Vector &position = b.positions[valid[first + i]];
			queries[i].position[0] = position.x;
			queries[i].position[1] = position.y;
			queries[i].position[2] = position.z;
			queries[i].position[3] = 0.0f;
		}
		if ( !b.device.ProjectProbes( queries, results ) )
			return Fail( "ambient probe projection failed" );
		for ( int i = 0; i < results.Count(); ++i )
			for ( int k = 0; k < 9; ++k )
				for ( int c = 0; c < 3; ++c )
					b.sh[valid[first + i]].e[k][c] = results[i].coeff[k][c];
	}
	return true;
}

// ambient_cube.cpp surface worldlights, as delta lights: e_k += I * ratio * A_l * Y_k(d).
bool AddSurfaceWorldLights( ProbeBuild &b )
{
	CUtlVector<int> lights;
	for ( int i = 0; i < *pNumworldlights; ++i )
		if ( ( dworldlights[i].flags & DWL_FLAGS_INAMBIENTCUBE ) && dworldlights[i].type == emit_surface )
			lights.AddToTail( i );
	if ( !lights.Count() )
		return true;
	const int probesPerChunk = MAX( 1, RAY_CHUNK / lights.Count() );
	CUtlVector<ReSTIRGpuRay> rays;
	CUtlVector<ReSTIRGpuHit> hits;
	CUtlVector<int> rayProbe, rayLight;
	CUtlVector<float> rayRatio;
	for ( int first = 0; first < b.positions.Count(); first += probesPerChunk )
	{
		const int end = MIN( first + probesPerChunk, b.positions.Count() );
		rays.RemoveAll();
		rayProbe.RemoveAll();
		rayLight.RemoveAll();
		rayRatio.RemoveAll();
		for ( int i = first; i < end; ++i )
		{
			if ( b.state[i] == PROBE_INVALID )
				continue;
			for ( int l = 0; l < lights.Count(); ++l )
			{
				const dworldlight_t &light = dworldlights[lights[l]];
				const Vector delta = light.origin - b.positions[i];
				if ( delta.LengthSqr() <= 1e-20f )
					continue;
				const float ratio = ReSTIR_AmbientWorldLightRatio( light, b.positions[i] );
				if ( ratio == 0.0f )
					continue;
				rays.AddToTail( MakeRay( b.positions[i], delta ) );
				rayProbe.AddToTail( i );
				rayLight.AddToTail( lights[l] );
				rayRatio.AddToTail( ratio );
			}
		}
		if ( !rays.Count() )
			continue;
		if ( !b.device.TraceRays( rays, RESTIR_RAY_MASK_SHADOW, hits ) )
			return Fail( "ambient probe worldlight visibility tracing failed" );
		for ( int r = 0; r < hits.Count(); ++r )
		{
			if ( hits[r].t >= 0.0f && hits[r].t < 1.0f )
				continue;
			const dworldlight_t &light = dworldlights[rayLight[r]];
			Vector direction = light.origin - b.positions[rayProbe[r]];
			VectorNormalize( direction );
			float basis[9];
			ShBasis( direction, basis );
			ProbeSh &sh = b.sh[rayProbe[r]];
			for ( int k = 0; k < 9; ++k )
				for ( int c = 0; c < 3; ++c )
					sh.e[k][c] += light.intensity[c] * rayRatio[r] * SH_A[ShBand( k )] * basis[k];
		}
	}
	return true;
}

// Negative lobes of the l = 2 band make the shader clamp to black beside bright surfaces; shrink the band until the
// irradiance over the sphere no longer undershoots by more than 2% of its peak.
bool Rings( const ProbeSh &sh, const float basis[RING_DIRECTIONS][9] )
{
	for ( int c = 0; c < 3; ++c )
	{
		float lowest = FLT_MAX, highest = -FLT_MAX;
		for ( int i = 0; i < RING_DIRECTIONS; ++i )
		{
			const float value = Irradiance( sh, c, basis[i] );
			lowest = MIN( lowest, value );
			highest = MAX( highest, value );
		}
		if ( lowest < -0.02f * highest )
			return true;
	}
	return false;
}

bool Deringed( ProbeSh &sh, const float basis[RING_DIRECTIONS][9] )
{
	static const float factors[4] = { 0.75f, 0.5f, 0.25f, 0.0f };
	if ( !Rings( sh, basis ) )
		return false;
	const ProbeSh original = sh;
	for ( int f = 0; f < 4; ++f )
	{
		sh = original;
		for ( int k = 4; k < 9; ++k )
			for ( int c = 0; c < 3; ++c )
				sh.e[k][c] *= factors[f];
		if ( !Rings( sh, basis ) )
			break;
	}
	return true;
}

void DeringProbes( ProbeBuild &b )
{
	float basis[RING_DIRECTIONS][9];
	for ( int i = 0; i < RING_DIRECTIONS; ++i )
	{
		const float z = 1.0f - ( 2.0f * i + 1.0f ) / RING_DIRECTIONS, radius = sqrtf( 1.0f - z * z ), angle = i * 2.39996323f;	// golden angle
		ShBasis( Vector( radius * cosf( angle ), radius * sinf( angle ), z ), basis[i] );
	}
	for ( int i = 0; i < b.sh.Count(); ++i )
		if ( b.state[i] != PROBE_INVALID && Deringed( b.sh[i], basis ) )
			++b.deringedCount;
}

int NeighborProbe( const ProbeBuild &b, uint64 key, int axis, bool positive )
{
	const int shift = 21 * axis;
	if ( !positive && !( ( key >> shift ) & 0x1FFFFF ) )
		return -1;
	const UtlHashHandle_t handle = b.probeIds.Find( positive ? key + ( uint64( 1 ) << shift ) : key - ( uint64( 1 ) << shift ) );
	return b.probeIds.IsValidHandle( handle ) ? b.probeIds.Element( handle ) : -1;
}

// Two passes of the mean of the valid 6-neighbours; invalid probes that gain a neighbour become valid.
void DilateProbes( ProbeBuild &b )
{
	CUtlVector<int> filled;
	CUtlVector<ProbeSh> means;
	for ( int pass = 0; pass < 2; ++pass )
	{
		filled.RemoveAll();
		means.RemoveAll();
		for ( int i = 0; i < b.positions.Count(); ++i )
		{
			if ( b.state[i] != PROBE_INVALID )
				continue;
			ProbeSh sum;
			memset( &sum, 0, sizeof( sum ) );
			int neighbors = 0;
			for ( int side = 0; side < 6; ++side )
			{
				const int neighbor = NeighborProbe( b, b.keys[i], side / 2, ( side & 1 ) == 0 );
				if ( neighbor < 0 || b.state[neighbor] == PROBE_INVALID )
					continue;
				for ( int k = 0; k < 9; ++k )
					for ( int c = 0; c < 3; ++c )
						sum.e[k][c] += b.sh[neighbor].e[k][c];
				++neighbors;
			}
			if ( !neighbors )
				continue;
			for ( int k = 0; k < 9; ++k )
				for ( int c = 0; c < 3; ++c )
					sum.e[k][c] /= neighbors;
			filled.AddToTail( i );
			means.AddToTail( sum );
		}
		for ( int i = 0; i < filled.Count(); ++i )
		{
			b.sh[filled[i]] = means[i];
			b.state[filled[i]] = PROBE_DILATED;
		}
		b.dilatedCount += filled.Count();
	}
}

// Premultiplied by validity (stored 255) like the invalid zeros: DC = e_0 * Y_0; band s_k = clamp(e_k / DC * 0.25, -1, 1).
void EncodeProbe( const ProbeSh &sh, uint32 texel, uint32 texels, ReSTIRAmbientProbeGrid &out )
{
	float dc[3];
	for ( int c = 0; c < 3; ++c )
		dc[c] = MIN( MAX( sh.e[0][c] * SH_Y0, 0.0f ), 65024.0f );
	DirectX::PackedVector::XMFLOAT3PK packed;
	DirectX::PackedVector::XMStoreFloat3PK( &packed, DirectX::XMVectorSet( dc[0], dc[1], dc[2], 0.0f ) );
	out.dc[texel] = packed.v;
	for ( int c = 0; c < 3; ++c )
		for ( int k = 1; k < 9; ++k )
		{
			const float ratio = MIN( MAX( sh.e[k][c] / MAX( dc[c], 1e-6f ) * 0.25f, -1.0f ), 1.0f );
			out.bands[int( ( uint64( 2 * c + ( k - 1 ) / 4 ) * texels + texel ) * 4 + ( k - 1 ) % 4 )] = int8( ratio * 127.0f + ( ratio >= 0.0f ? 0.5f : -0.5f ) );
		}
	out.validity[texel] = hprobe::kValid;
}

int FillGrid( ProbeBuild &b, ReSTIRAmbientProbeGrid &out )
{
	out.grid = b.grid;
	out.indirection.Swap( b.indirection );
	const uint32 texels = (uint32)hprobe::AtlasTexels( out.grid );
	out.dc.SetCount( texels );
	out.bands.SetCount( texels * hprobe::kBands * 4 );
	out.validity.SetCount( texels );
	memset( out.dc.Base(), 0, out.dc.Count() * sizeof( uint32 ) );
	memset( out.bands.Base(), 0, out.bands.Count() );
	memset( out.validity.Base(), 0, out.validity.Count() );
	int written = 0;
	for ( int entry = 0; entry < b.texelProbe.Count(); ++entry )
	{
		const int probe = b.texelProbe[entry];
		if ( b.state[probe] == PROBE_INVALID )
			continue;
		const uint32 slot = entry / PROBES_PER_BRICK, local = entry % PROBES_PER_BRICK;
		EncodeProbe( b.sh[probe], hprobe::TexelIndex( out.grid, slot, local % 5, ( local / 5 ) % 5, local / 25 ), texels, out );
		++written;
	}
	return written;
}

// Decode strided probes from the stored arrays (the shader's math at the six axis directions) and compare with
// the float coefficients: catches format, packing and addressing mistakes before they reach a map.
bool SelfCheck( const ProbeBuild &b, const ReSTIRAmbientProbeGrid &out, int written )
{
	const uint32 texels = (uint32)hprobe::AtlasTexels( out.grid );
	const int stride = MAX( 1, written / SELF_CHECK_PROBES );
	int seen = 0;
	for ( int entry = 0; entry < b.texelProbe.Count(); ++entry )
	{
		const int probe = b.texelProbe[entry];
		if ( b.state[probe] == PROBE_INVALID || seen++ % stride )
			continue;
		const uint32 slot = entry / PROBES_PER_BRICK, local = entry % PROBES_PER_BRICK;
		const uint32 texel = hprobe::TexelIndex( out.grid, slot, local % 5, ( local / 5 ) % 5, local / 25 );
		DirectX::PackedVector::XMFLOAT3PK packed;
		packed.v = out.dc[texel];
		const DirectX::XMVECTOR dc = DirectX::PackedVector::XMLoadFloat3PK( &packed );
		const float decodedDc[3] = { DirectX::XMVectorGetX( dc ), DirectX::XMVectorGetY( dc ), DirectX::XMVectorGetZ( dc ) };
		float expected[6][3], decoded[6][3], peak = 0.0f;
		for ( int side = 0; side < 6; ++side )
		{
			float basis[9];
			ShBasis( s_Axes[side], basis );
			for ( int c = 0; c < 3; ++c )
			{
				float sum = 0.0f;
				for ( int k = 1; k < 9; ++k )
					sum += out.bands[int( ( uint64( 2 * c + ( k - 1 ) / 4 ) * texels + texel ) * 4 + ( k - 1 ) % 4 )] / 127.0f * basis[k];
				decoded[side][c] = decodedDc[c] * ( 1.0f + 4.0f * sum );
				expected[side][c] = Irradiance( b.sh[probe], c, basis );
				peak = MAX( peak, expected[side][c] );
			}
		}
		for ( int side = 0; side < 6; ++side )
			for ( int c = 0; c < 3; ++c )
				if ( fabsf( decoded[side][c] - expected[side][c] ) > peak / 16.0f + 1e-3f )
					return false;
	}
	return true;
}
}

bool ReSTIR_BuildAmbientProbeGrid( const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device, ReSTIRAmbientProbeGrid &out )
{
	out.Purge();
	ProbeBuild b( options, scene, device );
	if ( !PlanGrid( b ) || !AllocateBricks( b ) )
		return false;
	CollectProbes( b );
	if ( !ComputeValidity( b ) || !ProjectCoefficients( b ) || !AddSurfaceWorldLights( b ) )
		return false;
	DeringProbes( b );
	DilateProbes( b );
	const int written = FillGrid( b, out );
	if ( !SelfCheck( b, out, written ) )
	{
		out.Purge();
		return Fail( "ambient probe encoding/addressing self-check failed" );
	}
	Msg( "Hlight: ambient probe grid: %d bricks (%ux%ux%u), %d probes (%d valid, %d dilated, %d deringed), %llu bytes\n",
		b.grid.brickCount, b.grid.brickDims[0], b.grid.brickDims[1], b.grid.brickDims[2], b.positions.Count(),
		b.validCount, b.dilatedCount, b.deringedCount, (unsigned long long)hprobe::GridBytes( out.grid ) );
	return true;
}
