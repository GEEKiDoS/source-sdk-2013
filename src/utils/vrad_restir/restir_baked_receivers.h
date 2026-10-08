//========= Copyright Valve Corporation, All rights reserved. ============//
// CPU-only renderer receiver interpolation for enhanced selected local direct.
#ifndef RESTIR_BAKED_RECEIVERS_H
#define RESTIR_BAKED_RECEIVERS_H
#pragma once
#include "restir_baked_direct.h"
#include "restir_scene_internal.h"
#include "bsplib.h"
#include <float.h>

// The native VRAD cores smooth N but retain original tangents. Engine
// E:/SourceEngine/engine/disp_mapload.cpp:1041-1318 also blends tangent S in
// T-junction/corner/edge order. Keep that extra work separate from transport.
struct ReSTIRRendererDispTangents
{
	CUtlVector<Vector> s;
	CUtlVector<unsigned char> touched;
};
struct ReSTIRRendererDispFrames
{
	CUtlVector<ReSTIRRendererDispTangents> list;
};
static int RendererDispCorner( const CCoreDispInfo &disp, const Vector &point )
{
	int closest = -1;
	float distance = FLT_MAX;
	for ( int corner = 0; corner < 4; ++corner )
	{
		const float candidate = disp.GetVert( disp.VertIndexToInt( disp.GetCornerPointIndex( corner ) ) ).DistTo( point );
		if ( candidate < distance ) { closest = corner; distance = candidate; }
	}
	return distance <= 0.1f ? closest : -1;
}
static int RendererDispNeighbors( const CCoreDispInfo &disp, int neighbors[512] )
{
	int count = 0;
	for ( int corner = 0; corner < 4; ++corner )
	{
		const CDispCornerNeighbors *n = disp.GetCornerNeighbors( corner );
		for ( int i = 0; i < n->m_nNeighbors && count < 512; ++i ) neighbors[count++] = n->m_Neighbors[i];
	}
	for ( int edge = 0; edge < 4; ++edge )
	{
		const CDispNeighbor *n = disp.GetEdgeNeighbor( edge );
		for ( int sub = 0; sub < 2; ++sub )
			if ( n->m_SubNeighbors[sub].IsValid() && count < 512 ) neighbors[count++] = n->m_SubNeighbors[sub].GetNeighborIndex();
	}
	return count;
}
static void RendererSetDispS( ReSTIRRendererDispFrames &frames, int disp, int vertex, const Vector &s )
{
	frames.list[disp].s[vertex] = s;
	frames.list[disp].touched[vertex] = 1;
}
static void BuildRendererDispFrames( const ReSTIRSceneBuildContext &context, ReSTIRRendererDispFrames &frames )
{
	const int count = context.displacements.Count();
	frames.list.SetCount( count );
	for ( int i = 0; i < count; ++i )
	{
		ReSTIRRendererDispTangents &t = frames.list[i];
		const CCoreDispInfo &disp = *context.displacements[i];
		t.s.SetCount( disp.GetSize() ); t.touched.SetCount( disp.GetSize() );
		for ( int v = 0; v < disp.GetSize(); ++v ) { t.s[v] = disp.GetTangentS( v ); t.touched[v] = 0; }
	}
	for ( int i = 0; i < count; ++i )
	{
		const CCoreDispInfo &disp = *context.displacements[i];
		for ( int edge = 0; edge < 4; ++edge )
		{
			const CDispNeighbor *n = disp.GetEdgeNeighbor( edge );
			if ( !n->m_SubNeighbors[0].IsValid() || !n->m_SubNeighbors[1].IsValid() ) continue;
			const int v = disp.VertIndexToInt( disp.GetEdgeMidPoint( edge ) );
			const int a = n->m_SubNeighbors[0].GetNeighborIndex(), b = n->m_SubNeighbors[1].GetNeighborIndex();
			const CCoreDispInfo &da = *context.displacements[a], &db = *context.displacements[b];
			const int ca = RendererDispCorner( da, disp.GetVert( v ) ), cb = RendererDispCorner( db, disp.GetVert( v ) );
			if ( ca < 0 || cb < 0 ) continue;
			const int va = da.VertIndexToInt( da.GetCornerPointIndex( ca ) ), vb = db.VertIndexToInt( db.GetCornerPointIndex( cb ) );
			Vector s = frames.list[i].s[v] + frames.list[a].s[va] + frames.list[b].s[vb];
			VectorNormalize( s );
			RendererSetDispS( frames, i, v, s ); RendererSetDispS( frames, a, va, s ); RendererSetDispS( frames, b, vb, s );
		}
	}
	for ( int i = 0; i < count; ++i )
	{
		const CCoreDispInfo &disp = *context.displacements[i];
		int neighbors[512], vertices[512];
		const int n = RendererDispNeighbors( disp, neighbors );
		for ( int corner = 0; corner < 4; ++corner )
		{
			const int v = disp.VertIndexToInt( disp.GetCornerPointIndex( corner ) );
			Vector s = frames.list[i].s[v];
			for ( int j = 0; j < n; ++j )
			{
				const CCoreDispInfo &other = *context.displacements[neighbors[j]];
				const int c = RendererDispCorner( other, disp.GetVert( v ) );
				vertices[j] = c < 0 ? -1 : other.VertIndexToInt( other.GetCornerPointIndex( c ) );
				if ( vertices[j] >= 0 ) s += frames.list[neighbors[j]].s[vertices[j]];
			}
			VectorNormalize( s ); RendererSetDispS( frames, i, v, s );
			for ( int j = 0; j < n; ++j ) if ( vertices[j] >= 0 ) RendererSetDispS( frames, neighbors[j], vertices[j], s );
		}
	}
	for ( int i = 0; i < count; ++i )
	{
		CCoreDispInfo &disp = *context.displacements[i];
		for ( int edge = 0; edge < 4; ++edge )
		{
			const CDispNeighbor *n = disp.GetEdgeNeighbor( edge );
			for ( int sub = 0; sub < 2; ++sub )
			{
				if ( !n->m_SubNeighbors[sub].IsValid() ) continue;
				const int other = n->m_SubNeighbors[sub].GetNeighborIndex();
				CCoreDispInfo &neighbor = *context.displacements[other];
				const int dimension = g_EdgeDims[edge];
				CDispSubEdgeIterator it; it.Start( &disp, edge, sub, true ); it.Next();
				CVertIndex previous = it.GetVertIndex();
				while ( it.Next() )
				{
					const int v = disp.VertIndexToInt( it.GetVertIndex() );
					if ( !it.IsLastVert() )
					{
						const int ov = neighbor.VertIndexToInt( it.GetNBVertIndex() );
						Vector s = frames.list[i].s[v] + frames.list[other].s[ov]; VectorNormalize( s );
						RendererSetDispS( frames, i, v, s ); RendererSetDispS( frames, other, ov, s );
					}
					const int start = previous[!dimension], end = it.GetVertIndex()[!dimension];
					for ( int between = start + 1; between < end; ++between )
					{
						Vector s; VectorLerp( frames.list[i].s[disp.VertIndexToInt( previous )], frames.list[i].s[v],
							RemapVal( between, start, end, 0, 1 ), s ); VectorNormalize( s );
						CVertIndex vi; vi[dimension] = it.GetVertIndex()[dimension]; vi[!dimension] = between;
						RendererSetDispS( frames, i, disp.VertIndexToInt( vi ), s );
					}
					previous = it.GetVertIndex();
				}
			}
		}
	}
}

struct ReSTIRRendererVertex { Vector position, normal, s, t; };
struct ReSTIRRendererTriangle { int v[3]; };
static void RendererVertexFrame( const ReSTIRGpuFace &face, ReSTIRRendererVertex &vertex )
{
	ReSTIR_BakedTangentFrame( face, vertex.normal, vertex.s, vertex.t );
	ReSTIR_BakedNormalizeTangent( vertex.normal );
}
static bool BuildRendererBrushTriangles( const ReSTIRSceneBuildContext &context, const ReSTIRGpuFace &face, const dface_t &df,
	CUtlVector<ReSTIRRendererVertex> &vertices, CUtlVector<ReSTIRRendererTriangle> &triangles )
{
	vertices.SetCount( df.numedges );
	for ( int i = 0; i < df.numedges; ++i )
	{
		vertices[i].position = dvertexes[ReSTIR_SceneFaceVertex( &df, i )].point + context.faceOrigins[face.dface];
		vertices[i].normal = (*context.vertexNormals[face.dface])[i];
		RendererVertexFrame( face, vertices[i] );
	}
	// engine/gl_rsurf.h BuildIndicesForWorldSurface uses explicit local BSP
	// tessellation indices, otherwise the (0,i,i+1) fan, not VRAD centre wedges.
	if ( df.GetNumPrims() )
	{
		for ( int p = 0; p < df.GetNumPrims(); ++p )
		{
			if ( int(df.firstPrimID)+p >= g_numprimitives ) return false;
			const dprimitive_t &prim = g_primitives[df.firstPrimID+p];
			if ( int(prim.firstIndex)+prim.indexCount > g_numprimindices ||
				int(prim.firstVert)+prim.vertCount > g_numprimverts ||
				( prim.type != PRIM_TRILIST && prim.type != PRIM_TRISTRIP ) ) return false;
			int firstVertex = 0;
			if ( prim.vertCount )
			{
				firstVertex = vertices.Count();
				for ( int v = 0; v < prim.vertCount; ++v )
				{
					ReSTIRRendererVertex vertex;
					vertex.position = g_primverts[prim.firstVert+v].pos + context.faceOrigins[face.dface];
					vertex.normal = ReSTIR_SceneV4( face.faceNormal );
					// BuildMSurfacePrimVerts passes negate=false, unlike edge vertices.
					Vector textureT = ReSTIR_SceneV4( face.textureT ); ReSTIR_BakedNormalizeTangent( textureT );
					vertex.s = CrossProduct( vertex.normal, textureT ); ReSTIR_BakedNormalizeTangent( vertex.s );
					vertex.t = CrossProduct( vertex.s, vertex.normal ); ReSTIR_BakedNormalizeTangent( vertex.t );
					ReSTIR_BakedNormalizeTangent( vertex.normal ); vertices.AddToTail( vertex );
				}
			}
			const int step = prim.type == PRIM_TRILIST ? 3 : 1;
			for ( int j = 0; j+2 < prim.indexCount; j += step )
			{
				ReSTIRRendererTriangle tri;
				for ( int c = 0; c < 3; ++c )
				{
					const int index = g_primindices[prim.firstIndex+j+c];
					if ( index >= ( prim.vertCount ? prim.vertCount : df.numedges ) ) return false;
					tri.v[c] = firstVertex + index;
				}
				triangles.AddToTail( tri );
			}
			if ( !prim.vertCount ) break;
		}
	}
	else for ( int i = 1; i+1 < df.numedges; ++i )
	{
		ReSTIRRendererTriangle tri = { { 0, i, i+1 } }; triangles.AddToTail( tri );
	}
	return triangles.Count() > 0;
}

// Clamp padding outside the polygon to its closest triangle edge. Interior
// samples use renderer barycentrics, with no normalization after interpolation.
static bool RendererBrushWeights( const CUtlVector<ReSTIRRendererVertex> &vertices,
	const CUtlVector<ReSTIRRendererTriangle> &triangles, const Vector &position, int &selected, float weights[3] )
{
	float closest = FLT_MAX;
	selected = -1;
	for ( int i = 0; i < triangles.Count(); ++i )
	{
		const ReSTIRRendererTriangle &tri = triangles[i];
		const Vector &a = vertices[tri.v[0]].position;
		const Vector u = vertices[tri.v[1]].position-a, v = vertices[tri.v[2]].position-a, p = position-a;
		const float uu = DotProduct(u,u), vv = DotProduct(v,v), uv = DotProduct(u,v), denominator = uu*vv-uv*uv;
		if ( denominator <= 0.0f ) continue;
		const float b = ( vv*DotProduct(p,u)-uv*DotProduct(p,v) )/denominator;
		const float c = ( uu*DotProduct(p,v)-uv*DotProduct(p,u) )/denominator;
		if ( b >= 0.0f && c >= 0.0f && b+c <= 1.0f )
		{
			selected = i; weights[0] = 1.0f-b-c; weights[1] = b; weights[2] = c; return true;
		}
		for ( int edge = 0; edge < 3; ++edge )
		{
			const int next = (edge+1)%3;
			const Vector &start = vertices[tri.v[edge]].position;
			const Vector delta = vertices[tri.v[next]].position-start;
			const float length = delta.LengthSqr();
			if ( length <= 0.0f ) continue;
			const float t = clamp( DotProduct(position-start,delta)/length, 0.0f, 1.0f );
			const float distance = ( start+delta*t-position ).LengthSqr();
			if ( distance < closest )
			{
				closest = distance; selected = i; weights[0] = weights[1] = weights[2] = 0.0f;
				weights[edge] = 1.0f-t; weights[next] = t;
			}
		}
	}
	return selected >= 0;
}

// Full-resolution checkerboard triangles of CCoreDispInfo render indices.
// Exact UV endpoints: unlike collision sampling, do not use width-1.000001 or
// a 0.001 diagonal tolerance for renderer interpolation.
static void RendererDispVertices( int power, const Vector2D &uv, int indices[3], float weights[3] )
{
	const int width = (1<<power)+1;
	const float u = uv.x*(width-1), v = uv.y*(width-1);
	const int x = MIN( int(u), width-2 ), y = MIN( int(v), width-2 );
	const float fu = u-x, fv = v-y;
	const int base = y*width+x;
	if ( base%2 )
	{
		if ( fu+fv >= 1.0f )
		{ indices[0]=base+width+1; indices[1]=base+width; indices[2]=base+1; weights[0]=fu+fv-1; weights[1]=1-fu; weights[2]=1-fv; }
		else
		{ indices[0]=base; indices[1]=base+1; indices[2]=base+width; weights[0]=1-fu-fv; weights[1]=fu; weights[2]=fv; }
	}
	else
	{
		if ( fu < fv )
		{ indices[0]=base+width; indices[1]=base+width+1; indices[2]=base; weights[0]=fv-fu; weights[1]=fu; weights[2]=1-fv; }
		else
		{ indices[0]=base+1; indices[1]=base; indices[2]=base+width+1; weights[0]=fu-fv; weights[1]=1-fu; weights[2]=fv; }
	}
}

static void RendererInterpolateFrame( const ReSTIRRendererVertex vertices[3], const float weights[3],
	Vector &position, Vector &normal, Vector bump[3] )
{
	Vector s(0,0,0), t(0,0,0);
	position.Init(); normal.Init();
	for ( int i = 0; i < 3; ++i )
	{
		position += vertices[i].position*weights[i]; normal += vertices[i].normal*weights[i];
		s += vertices[i].s*weights[i]; t += vertices[i].t*weights[i];
	}
	ReSTIR_BakedBumpFrameNormals( normal, s, t, bump );
}
#endif // RESTIR_BAKED_RECEIVERS_H
