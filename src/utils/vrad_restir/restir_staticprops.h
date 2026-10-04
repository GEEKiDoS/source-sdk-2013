//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_STATICPROPS_H
#define RESTIR_STATICPROPS_H
#pragma once
#include "restir_types.h"
#include "gamebspfile.h"
#include "tier1/utlbuffer.h"

struct studiohdr_t;
class CReSTIRVulkanDevice;

// Owned here; filled by the scene builder's .rad parser, as in vrad.cpp:231-237.
extern CUtlVector<char const *> g_NonShadowCastingMaterialStrings;
void ForceTextureShadowsOnModel( const char *pModelName );
bool IsModelTextureShadowsForced( const char *pModelName );
bool ReSTIR_LoadStudioModel( const char *pModelName, CUtlBuffer &buffer );

class CReSTIRStaticPropMgr
{
public:
	bool Init();
	void Shutdown();
	int Count() const;
	bool AppendTriangles( ReSTIRScene &scene, bool textureShadows );
	bool AppendEmitters( ReSTIRScene &scene, const ReSTIROptions &options );
	// Loaded model peer data. mstudiomodel_t::CacheVertexData receives a pointer to this struct
	// (passed as GetVertexData's pModelData) because studiohdr_t::SetVertexBase is a no-op on
	// PLATFORM_64BITS for models without a studiohdr2_t.
	struct Model
	{
		CUtlBuffer mdl, vtx, vvd;
		CUtlString name;
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
	int m_nVerticesLit = 0;			// vertices lit at their own position
	int m_nVerticesInSolid = 0;		// vertices inside solid, relit from the crawled position (vradstaticprops.cpp:1434-1499)
	bool UnserializeModelDict( CUtlBuffer &buffer );
	bool UnserializeModels( CUtlBuffer &buffer, int version );
	bool UnserializeStaticProps();
	bool LoadModel( Model &model, const char *name );
	bool ComputeLighting( Prop &prop, int propIndex, const ReSTIRScene &scene, CReSTIRVulkanDevice &device );
	bool SerializeLighting();
	friend bool ReSTIR_ComputeStaticPropLighting( const ReSTIROptions &, const ReSTIRScene &, CReSTIRVulkanDevice & );
};
extern CReSTIRStaticPropMgr g_ReSTIRStaticPropMgr;
#endif
