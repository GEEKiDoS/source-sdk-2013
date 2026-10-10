//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_STATICPROPS_H
#define RESTIR_STATICPROPS_H
#pragma once
#include "restir_types.h"
#include "gamebspfile.h"
#include "tier1/utlbuffer.h"
#include "hlight_bsp.h"

struct studiohdr_t;
class CReSTIRVulkanDevice;

// Owned here; filled by the scene builder's .rad parser, as in vrad.cpp:231-237.
extern CUtlVector<char const *> g_NonShadowCastingMaterialStrings;
bool ReSTIR_LoadStudioModel( const char *pModelName, CUtlBuffer &buffer );

// Optional authored-geometry evidence, retained only for diagnostic requests.
struct ReSTIRPropDirectDiagnosticVertex
{
	uint32 sourceIndex;
	Vector position, normal;
};
struct ReSTIRPropDirectDiagnosticMesh
{
	int firstVertex, vertexCount;
};
struct ReSTIRPropDirectDiagnosticProp
{
	uint32 flags;
	int skipReason; // 0 eligible, 1 NO_PER_VERTEX, 2 active pertexel, 3 missing geometry
};

// Owned until the static-prop manager shuts down. Payload offsets are relative
// to payload; entries are dense R8 in light-major order within each hardware
// mesh, followed by its optional linear RGBA16F selected-local direct block.
// Mesh ordinals match VHV serialization, including every LOD and strip group.
struct ReSTIRPropVisibilityData
{
	uint32 selectedLightCount = 0; // canonical manifest domain, including sun
	CUtlVector<hlight::PropVisibilityDisk> props;
	CUtlVector<hlight::PropMeshVisibilityDisk> meshes;
	CUtlVector<hlight::VisibilityEntryDisk> entries;
	CUtlVector<unsigned char> payload;
	bool diagnosticsCaptured = false;
	CUtlVector<ReSTIRPropDirectDiagnosticProp> diagnosticProps;
	CUtlVector<ReSTIRPropDirectDiagnosticMesh> diagnosticMeshes;
	CUtlVector<ReSTIRPropDirectDiagnosticVertex> diagnosticVertices;
	uint32 directEligibleProps = 0, directNoVertexProps = 0, directPerTexelProps = 0, directMissingGeometryProps = 0;
	uint32 directMeshes = 0, directFallbackLights = 0;
	void Clear()
	{
		selectedLightCount = 0;
		props.Purge();
		meshes.Purge();
		entries.Purge();
		payload.Purge();
		diagnosticsCaptured = false;
		diagnosticProps.Purge();
		diagnosticMeshes.Purge();
		diagnosticVertices.Purge();
		directEligibleProps = directNoVertexProps = directPerTexelProps = directMissingGeometryProps = 0;
		directMeshes = directFallbackLights = 0;
	}
};
// Null until the requested mode has completed successfully. Whole-scene paired
// reuse aliases the retained LDR data; unequal modes never share visibility.
const ReSTIRPropVisibilityData *ReSTIR_GetStaticPropVisibility( bool hdr );
void ReSTIR_EnableStaticPropDirectDiagnostics( bool enabled );
bool ReSTIR_WriteStaticPropDirectDiagnostics( const char *path, bool hdr );
void ReSTIR_LogStaticPropDirectSummary( bool hdr );

class CReSTIRStaticPropMgr
{
public:
	bool Init();
	void Shutdown();
	int Count() const;
	bool AppendTriangles( ReSTIRScene &scene, bool textureShadows );
	bool AppendEmitters( ReSTIRScene &scene, const ReSTIROptions &options );
	const ReSTIRPropVisibilityData *GetVisibility( bool hdr ) const;
	// Loaded model peer data. mstudiomodel_t::CacheVertexData receives a pointer to this struct
	// (passed as GetVertexData's pModelData) because studiohdr_t::SetVertexBase is a no-op on
	// PLATFORM_64BITS for models without a studiohdr2_t.
	struct Model
	{
		CUtlBuffer mdl, vtx, vvd;
		studiohdr_t *header;
		void *vertexBase;
		Model() : header( NULL ), vertexBase( NULL ) {}
	};
private:
	struct MeshLighting
	{
		CUtlVector<Vector> colors;
		CUtlVector<unsigned char> texels;
		int lod;
	};
	struct Prop
	{
		StaticPropLump_t lump;
		CUtlVector<MeshLighting *> meshes;
		~Prop() { meshes.PurgeAndDeleteElements(); }
	};
	CUtlVector<Model *> m_Models;
	CUtlVector<Prop *> m_Props;
	ReSTIRPropVisibilityData m_Visibility[2];
	bool m_VisibilityComplete[2] = { false, false };
	bool m_VisibilitySharedHDR = false;
	int m_nVerticesLit = 0;			// vertices lit at their own position
	int m_nVerticesInSolid = 0;		// vertices inside solid, relit from the crawled position (vradstaticprops.cpp:1434-1499)
	bool UnserializeModelDict( CUtlBuffer &buffer );
	bool UnserializeModels( CUtlBuffer &buffer, int version );
	bool UnserializeStaticProps();
	bool LoadModel( Model &model, const char *name );
	bool ComputeLighting( Prop &prop, int propIndex, const ReSTIRScene &scene, CReSTIRVulkanDevice &device, bool rgbLighting, bool enhancedLighting );
	bool SerializeLighting();
	friend bool ReSTIR_ComputeStaticPropLighting( const ReSTIROptions &, const ReSTIRScene &, CReSTIRVulkanDevice & );
	friend bool ReSTIR_ReuseStaticPropLighting( const ReSTIROptions & );
	friend void ReSTIR_LogStaticPropDirectSummary( bool hdr );
};
extern CReSTIRStaticPropMgr g_ReSTIRStaticPropMgr;
#endif
