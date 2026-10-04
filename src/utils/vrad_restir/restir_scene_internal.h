//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_SCENE_INTERNAL_H
#define RESTIR_SCENE_INTERNAL_H
#pragma once

#include "restir_scene.h"
#include "bsplib.h"
#include "builddisp.h"
#include "mathlib/vmatrix.h"
#include "polylib.h"
#include "tier1/utldict.h"

#ifndef TEX_SPECIAL
#define TEX_SPECIAL ( SURF_SKY | SURF_NOLIGHT )
#endif

struct ReSTIRSceneBuildContext
{
	const ReSTIROptions *options;
	int faceCount;
	CUtlVector<int> faceTriFirst;
	CUtlVector<int> faceTriCount;
	CUtlVector<Vector> faceOrigins;
	CUtlVector<int> faceModels;
	CUtlVector<Vector> faceNormals;
	CUtlVector<int> faceClusters;
	CUtlVector<CUtlVector<int> *> faceClusterLists;
	CUtlVector<CUtlVector<int> *> neighborLists;
	CUtlVector<CUtlVector<Vector> *> vertexNormals;
	CUtlVector<Vector> faceCentroids;
	CUtlVector<CCoreDispInfo *> displacements;

	ReSTIRSceneBuildContext()
		: options( NULL ), faceCount( 0 )
	{
	}
	~ReSTIRSceneBuildContext()
	{
		faceClusterLists.PurgeAndDeleteElements();
		neighborLists.PurgeAndDeleteElements();
		vertexNormals.PurgeAndDeleteElements();
		displacements.PurgeAndDeleteElements();
	}
};

int ReSTIR_SceneFaceCount();
int ReSTIR_SceneFaceVertex( const dface_t *pFace, int edge );
void ReSTIR_SceneFaceOrigin( int model, Vector &origin );
entity_t *ReSTIR_SceneEntityForModel( int model );
const char *ReSTIR_SceneFaceMaterial( const dface_t *pFace );
void ReSTIR_SceneFaceBounds( const dface_t *pFace, const Vector &origin, Vector &mins, Vector &maxs );
float ReSTIR_SceneFaceArea( const dface_t *pFace, const Vector &origin );
Vector ReSTIR_SceneFaceCentroid( const dface_t *pFace, const Vector &origin );
Vector ReSTIR_SceneFaceNormal( const dface_t *pFace, const Vector &origin );
void ReSTIR_SceneSet4( float out[4], const Vector &value, float w = 0.0f );
Vector ReSTIR_SceneV4( const float value[4] );
float ReSTIR_SceneSafeLength( const Vector &value );
void ReSTIR_ScenePrepareLightFiles( const ReSTIROptions &options );
void ReSTIR_SceneAddTriangle( ReSTIRScene &scene, const Vector &v0, const Vector &v1, const Vector &v2,
	int material, unsigned int hitId, unsigned int flags, int face, const float uv[6],
	bool forceWinding, const Vector &planeNormal );
void ReSTIR_SceneAddWindingTriangles( ReSTIRScene &scene, winding_t *pWinding, int texinfoIndex,
	int material, unsigned int hitId, unsigned int flags, int face, const VMatrix &transform,
	bool forceWinding, const Vector &planeNormal );
void ReSTIR_ScenePointUV( const texinfo_t &texinfoValue, const dtexdata_t &texdata,
	const Vector &point, float &u, float &v );
void ReSTIR_SceneCalcFaceVectors( const dface_t *pFace, const Vector &origin, const Vector &normal,
	Vector &luxelOrigin, Vector worldToLuxel[2], Vector luxelToWorld[2] );
Vector ReSTIR_SceneLuxelToWorld( const Vector &luxelOrigin, const Vector luxelToWorld[2], float s, float t );
int ReSTIR_SceneClusterFromPoint( const Vector &point );
Vector ReSTIR_ScenePhongNormal( const ReSTIRSceneBuildContext &context, int faceIndex,
	const Vector &point, const Vector &faceNormal );
void ReSTIR_SceneBuildFaceNeighbors( ReSTIRSceneBuildContext &context, ReSTIRScene &scene );
bool ReSTIR_SceneSaveVertexNormals( const ReSTIRSceneBuildContext &context );

bool ReSTIR_SceneBuildGeometry( ReSTIRSceneBuildContext &context, ReSTIRScene &scene );
bool ReSTIR_SceneBuildLights( ReSTIRSceneBuildContext &context, ReSTIRScene &scene );
bool ReSTIR_SceneBuildSamples( ReSTIRSceneBuildContext &context, ReSTIRScene &scene );

#endif // RESTIR_SCENE_INTERNAL_H
