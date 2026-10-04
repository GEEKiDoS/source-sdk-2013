//========= Copyright Valve Corporation, All rights reserved. ============//
// Ports: vradstaticprops.cpp:464-554, 921-1104, 1311-1505, 1510-1660,
// 1819-2017, and GenerateLightmapSamplesForMesh:2285-2450.
#include "restir_staticprops.h"
#include "restir_scene.h"
#include "restir_vulkan.h"
#include "bsplib.h"
#include "filesystem.h"
#include "studio.h"
#include "optimize.h"
#include "materialsystem/hardwareverts.h"
#include "materialsystem/hardwaretexels.h"
#include "bitmap/imageformat.h"
#include "tier1/utldict.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef ALIGN_TO_POW2
#define ALIGN_TO_POW2( x, y ) ( ( ( x ) + ( y - 1 ) ) & ~( y - 1 ) )
#endif
extern bool g_bHDR;

// Port: utils/vrad/lightmap.cpp:3581-3600 (ConvertLinearToRGBA8888). The engine applies the same
// LinearToVertexLight + ColorClamp sequence, so VHV colors stay consistent with VRAD output.
static void ConvertLinearToRGBA8888( const Vector *pSrcLinear, unsigned char *pDst )
{
	Vector vertexColor;
	vertexColor[0] = LinearToVertexLight( ( *pSrcLinear )[0] );
	vertexColor[1] = LinearToVertexLight( ( *pSrcLinear )[1] );
	vertexColor[2] = LinearToVertexLight( ( *pSrcLinear )[2] );
	ColorClamp( vertexColor );
	pDst[0] = RoundFloatToByte( vertexColor[0] * 255.0f );
	pDst[1] = RoundFloatToByte( vertexColor[1] * 255.0f );
	pDst[2] = RoundFloatToByte( vertexColor[2] * 255.0f );
	pDst[3] = 255;
}

// Port: utils/vrad/lightmap.cpp:3553-3565 (ConvertRGBExp32ToRGBA8888).
static void ConvertRGBExp32ToRGBA8888( const ColorRGBExp32 *pSrc, unsigned char *pDst, Vector *pOptOutLinear = NULL )
{
	Vector linearColor;
	linearColor[0] = TexLightToLinear( pSrc->r, pSrc->exponent );
	linearColor[1] = TexLightToLinear( pSrc->g, pSrc->exponent );
	linearColor[2] = TexLightToLinear( pSrc->b, pSrc->exponent );
	ConvertLinearToRGBA8888( &linearColor, pDst );
	if ( pOptOutLinear )
		*pOptOutLinear = linearColor;
}
static Vector QuantizeTexelToLinear( const Vector &color )
{
	ColorRGBExp32 encoded;
	VectorToColorRGBExp32( color, encoded );
	Vector linear;
	unsigned char rgba[4];
	ConvertRGBExp32ToRGBA8888( &encoded, rgba, &linear );
	return linear;
}
#ifndef VERTEX_COLOR
#define VERTEX_COLOR 0x0004
#endif

CUtlVector<char const *> g_NonShadowCastingMaterialStrings;
CReSTIRStaticPropMgr g_ReSTIRStaticPropMgr;
static CUtlDict<int, unsigned short> s_forcedModels;

static bool ReadFile( const char *name, CUtlBuffer &buf )
{
	return g_pFullFileSystem && g_pFullFileSystem->ReadFile( name, NULL, buf );
}
static void CleanModelName( const char *name, char *out, int outLen )
{
	if ( !Q_strnicmp( name, "models/", 7 ) )
		name += 7;
	Q_strncpy( out, name, outLen );
	char *dot = strrchr( out, '.' );
	if ( dot )
		*dot = 0;
}
void ForceTextureShadowsOnModel( const char *name )
{
	char clean[1024];
	CleanModelName( name, clean, sizeof( clean ) );
	if ( s_forcedModels.Find( clean ) == s_forcedModels.InvalidIndex() )
		s_forcedModels.Insert( clean, 1 );
}
bool IsModelTextureShadowsForced( const char *name )
{
	char clean[1024];
	CleanModelName( name, clean, sizeof( clean ) );
	return s_forcedModels.Find( clean ) != s_forcedModels.InvalidIndex();
}

bool ReSTIR_LoadStudioModel( const char *name, CUtlBuffer &buf )
{
	// Port: vradstaticprops.cpp:464-503.
	if ( !ReadFile( name, buf ) )
	{
		Warning( "Unable to load model \"%s\"\n", name );
		return false;
	}
	if ( buf.TellPut() < 4 || ( Q_strncmp( (const char *)buf.Base(), "IDST", 4 ) && Q_strncmp( (const char *)buf.Base(), "IDAG", 4 ) ) )
		return false;
	studiohdr_t *hdr = (studiohdr_t *)buf.Base();
	Studio_ConvertStudioHdrToNewVersion( hdr );
	if ( hdr->version != STUDIO_VERSION )
		return false;
	return true;
}
static bool LoadVtx( const char *name, const studiohdr_t *hdr, CUtlBuffer &buf )
{
	char file[MAX_PATH];
	Q_StripExtension( name, file, sizeof( file ) );
	Q_strncat( file, ".dx80.vtx", sizeof( file ), COPY_ALL_CHARACTERS );
	if ( !ReadFile( file, buf ) )
		return false;
	OptimizedModel::FileHeader_t *v = (OptimizedModel::FileHeader_t *)buf.Base();
	return v->version == OPTIMIZED_MODEL_FILE_VERSION && v->checkSum == hdr->checksum;
}
static bool LoadVvd( const char *name, const studiohdr_t *hdr, void **out )
{
	char file[MAX_PATH];
	Q_StripExtension( name, file, sizeof( file ) );
	Q_strncat( file, ".vvd", sizeof( file ), COPY_ALL_CHARACTERS );
	CUtlBuffer raw;
	if ( !ReadFile( file, raw ) )
		return false;
	vertexFileHeader_t *src = (vertexFileHeader_t *)raw.Base();
	if ( src->id != MODEL_VERTEX_FILE_ID || src->version != MODEL_VERTEX_FILE_VERSION || src->checksum != hdr->checksum )
		return false;
	vertexFileHeader_t *dst = (vertexFileHeader_t *)malloc( raw.TellPut() );
	if ( !dst )
		return false;
	Studio_LoadVertexes( src, dst, 0, true );
	*out = dst;
	return true;
}
// pModelData is whatever the caller passed to mstudiomodel_t::GetVertexData; this tool always
// passes a CReSTIRStaticPropMgr::Model (see restir_staticprops.h), whose vertexBase is the fixed-up VVD.
const vertexFileHeader_t *mstudiomodel_t::CacheVertexData( void *pModelData )
{
	const CReSTIRStaticPropMgr::Model *m = (const CReSTIRStaticPropMgr::Model *)pModelData;
	return m ? (const vertexFileHeader_t *)m->vertexBase : NULL;
}

bool CReSTIRStaticPropMgr::LoadModel( Model &m, const char *name )
{
	// Some shipped maps store "./models/..." in the dictionary; the engine's model loader normalizes
	// that, the tool filesystem does not.
	char cleanName[MAX_PATH];
	Q_strncpy( cleanName, name, sizeof( cleanName ) );
	Q_RemoveDotSlashes( cleanName );
	name = cleanName;
	m.name = name;
	if ( !ReSTIR_LoadStudioModel( name, m.mdl ) )
		return false;
	m.header = (studiohdr_t *)m.mdl.Base();
	if ( !( m.header->flags & STUDIOHDR_FLAGS_STATIC_PROP ) )
		return false;
	if ( !LoadVtx( name, m.header, m.vtx ) || !LoadVvd( name, m.header, &m.vertexBase ) )
		return false;
	return true;
}
bool CReSTIRStaticPropMgr::UnserializeModelDict( CUtlBuffer &buf )
{
	int count = buf.GetInt();
	if ( count < 0 || count > 65536 )
	{
		Warning( "VRAD ReSTIR: static prop dictionary count %d is invalid\n", count );
		return false;
	}
	for ( int i = 0; i < count; ++i )
	{
		StaticPropDictLump_t l;
		buf.Get( &l, sizeof( l ) );
		Model *m = new Model;
		if ( !LoadModel( *m, l.m_Name ) )
		{
			// vradstaticprops.cpp:464-503: VRAD warns and keeps a null entry; props using it are skipped.
			Warning( "VRAD ReSTIR: static prop model \"%s\" could not be loaded; its instances are skipped\n", l.m_Name );
			m->header = NULL;
		}
		m_Models.AddToTail( m );
	}
	return true;
}
// The engine accepts static prop lump versions 4..6 and 10 through the conversion operators in
// public/gamebspfile.h (StaticPropLump_t::operator=). The baker never rewrites GAMELUMP_STATIC_PROPS,
// so it reads every version the engine reads instead of VRAD's "Re-vbsp the map" error (vradstaticprops.cpp:1067).
bool CReSTIRStaticPropMgr::UnserializeModels( CUtlBuffer &buf, int version )
{
	int count = buf.GetInt();
	if ( count < 0 || count > ( 1 << 20 ) )
	{
		Warning( "VRAD ReSTIR: static prop count %d is invalid\n", count );
		return false;
	}
	for ( int i = 0; i < count; ++i )
	{
		Prop *p = new Prop;
		switch ( version )
		{
		case 4:
		{
			StaticPropLumpV4_t v4;
			buf.Get( &v4, sizeof( v4 ) );
			p->lump = v4;
			break;
		}
		case 5:
		{
			StaticPropLumpV5_t v5;
			buf.Get( &v5, sizeof( v5 ) );
			p->lump = v5;
			break;
		}
		case 6:
		{
			StaticPropLumpV6_t v6;
			buf.Get( &v6, sizeof( v6 ) );
			p->lump = v6;
			break;
		}
		default:
			buf.Get( &p->lump, sizeof( p->lump ) );
			break;
		}
		if ( p->lump.m_PropType >= m_Models.Count() )
		{
			Warning( "VRAD ReSTIR: static prop %d references model %d of %d\n", i, p->lump.m_PropType, m_Models.Count() );
			delete p;
			return false;
		}
		m_Props.AddToTail( p );
	}
	return true;
}
bool CReSTIRStaticPropMgr::UnserializeStaticProps()
{
	GameLumpHandle_t h = g_GameLumps.GetGameLumpHandle( GAMELUMP_STATIC_PROPS );
	if ( h == g_GameLumps.InvalidGameLump() )
		return true;
	int size = g_GameLumps.GameLumpSize( h );
	if ( !size )
		return true;
	const int version = g_GameLumps.GetGameLumpVersion( h );
	if ( version != 4 && version != 5 && version != 6 && version != GAMELUMP_STATIC_PROPS_VERSION )
	{
		Warning( "VRAD ReSTIR: unsupported static prop lump version %d (expected 4, 5, 6 or %d)\n", version, GAMELUMP_STATIC_PROPS_VERSION );
		return false;
	}
	CUtlBuffer b( g_GameLumps.GetGameLump( h ), size, CUtlBuffer::READ_ONLY );
	if ( !UnserializeModelDict( b ) )
		return false;
	int leaf = b.GetInt();
	if ( leaf < 0 || b.TellGet() + leaf * (int)sizeof( StaticPropLeafLump_t ) > size )
	{
		Warning( "VRAD ReSTIR: static prop leaf list is truncated\n" );
		return false;
	}
	b.SeekGet( CUtlBuffer::SEEK_CURRENT, leaf * sizeof( StaticPropLeafLump_t ) );
	return UnserializeModels( b, version );
}
bool CReSTIRStaticPropMgr::Init()
{
	Shutdown();
	m_nVerticesLit = 0;
	m_nVerticesInSolid = 0;
	return UnserializeStaticProps();
}
void CReSTIRStaticPropMgr::Shutdown()
{
	for ( int i = 0; i < m_Models.Count(); ++i )
	{
		Model *m = m_Models[i];
		if ( m->vertexBase )
			free( m->vertexBase );
		delete m;
	}
	m_Models.RemoveAll();
	m_Props.PurgeAndDeleteElements();
}
int CReSTIRStaticPropMgr::Count() const
{
	return m_Props.Count();
}

static void FillTriangle( ReSTIRGpuTriangle &t, const Vector &p0, const Vector &p1, const Vector &p2, const Vector2D &u0, const Vector2D &u1,
						  const Vector2D &u2, int mat, int prop, bool alpha )
{
	t.v0[0] = p0.x;
	t.v0[1] = p0.y;
	t.v0[2] = p0.z;
	t.v0[3] = u0.x;
	t.v1[0] = p1.x;
	t.v1[1] = p1.y;
	t.v1[2] = p1.z;
	t.v1[3] = u0.y;
	t.v2[0] = p2.x;
	t.v2[1] = p2.y;
	t.v2[2] = p2.z;
	t.v2[3] = u1.x;
	t.uv[0] = u1.y;
	t.uv[1] = u2.x;
	t.uv[2] = u2.y;
	t.uv[3] = 0;
	t.hitId = RESTIR_TRACE_ID_STATICPROP | (unsigned int)prop;
	t.material = mat;
	t.flags = RESTIR_TRI_STATICPROP | RESTIR_TRI_SHADOW | ( alpha ? RESTIR_TRI_NONOPAQUE : 0 );
	t.face = -1;
}

// Port: vradstaticprops.cpp:746-768 (LoadAllTexturesForModel). Returns "<cdmaterials dir><texture>"
// for the first $cdmaterials directory whose VMT exists; falls back to the first directory.
static void ResolvePropMaterialName( const studiohdr_t *pHdr, int textureIndex, char *pOut, int outLen )
{
	const char *pTextureName = pHdr->pTexture( textureIndex )->pszName();
	pOut[0] = 0;
	for ( int j = 0; j < pHdr->numcdtextures; ++j )
	{
		char relative[MAX_PATH];
		Q_strncpy( relative, pHdr->pCdtexture( j ), sizeof( relative ) );
		Q_strncat( relative, pTextureName, sizeof( relative ), COPY_ALL_CHARACTERS );
		Q_FixSlashes( relative, CORRECT_PATH_SEPARATOR );
		if ( j == 0 )
			Q_strncpy( pOut, relative, outLen );
		char vmtPath[MAX_PATH];
		Q_snprintf( vmtPath, sizeof( vmtPath ), "materials/%s.vmt", relative );
		Q_FixSlashes( vmtPath, CORRECT_PATH_SEPARATOR );
		if ( g_pFullFileSystem && g_pFullFileSystem->FileExists( vmtPath ) )
		{
			Q_strncpy( pOut, relative, outLen );
			return;
		}
	}
	if ( !pOut[0] )
		Q_strncpy( pOut, pTextureName, outLen );
}
bool CReSTIRStaticPropMgr::AppendTriangles( ReSTIRScene &scene, bool textureShadows )
{
	// Port: vradstaticprops.cpp:1819-2017. I9 always selects the polygon path.
	for ( int pi = 0; pi < m_Props.Count(); ++pi )
	{
		Prop &p = *m_Props[pi];
		if ( p.lump.m_Flags & STATIC_PROP_NO_SHADOW )
			continue;
		Model &m = *m_Models[p.lump.m_PropType];
		OptimizedModel::FileHeader_t *vh = (OptimizedModel::FileHeader_t *)m.vtx.Base();
		if ( !m.header || !vh )
			continue;
		matrix3x4_t xform;
		AngleMatrix( p.lump.m_Angles, p.lump.m_Origin, xform );
		for ( int body = 0; body < m.header->numbodyparts; ++body )
		{
			mstudiobodyparts_t *bp = m.header->pBodypart( body );
			OptimizedModel::BodyPartHeader_t *vbp = vh->pBodyPart( body );
			for ( int mid = 0; mid < bp->nummodels; ++mid )
			{
				mstudiomodel_t *sm = bp->pModel( mid );
				OptimizedModel::ModelLODHeader_t *lod = vbp->pModel( mid )->pLOD( 0 );
				for ( int meshId = 0; meshId < sm->nummeshes; ++meshId )
				{
					mstudiomesh_t *mesh = sm->pMesh( meshId );
					mstudiotexture_t *tx = m.header->pTexture( mesh->material );
					bool skip = false;
					for ( int n = 0; n < g_NonShadowCastingMaterialStrings.Count(); ++n )
						if ( Q_stristr( tx->pszName(), g_NonShadowCastingMaterialStrings[n] ) )
						{
							skip = true;
							break;
						}
					if ( skip )
						continue;
					const mstudio_meshvertexdata_t *vd = mesh->GetVertexData( (void *)&m );
					if ( !vd )
						continue;
					bool alpha = false;
					bool enable =
						textureShadows && ( ( m.header->flags & STUDIOHDR_FLAGS_CAST_TEXTURE_SHADOWS ) || IsModelTextureShadowsForced( m.name.String() ) );
					// vradstaticprops.cpp:746-768 (LoadAllTexturesForModel): the VMT lives under one of the
					// model's $cdmaterials directories; use the first that exists as the material name.
					char materialName[MAX_PATH];
					ResolvePropMaterialName( m.header, mesh->material, materialName, sizeof( materialName ) );
					// Reflectivity 0: VRAD's radiosity only bounces between brush/disp patches; static props
					// occlude but never re-emit (they are not patches), so GPU paths must die at prop hits.
					int mat = ReSTIR_GetOrAddMaterial( scene, materialName, Vector( 0, 0, 0 ), enable, &alpha );
					alpha = enable && alpha;
					OptimizedModel::MeshHeader_t *vm = lod->pMesh( meshId );
					for ( int group = 0; group < vm->numStripGroups; ++group )
					{
						OptimizedModel::StripGroupHeader_t *sg = vm->pStripGroup( group );
						for ( int si = 0; si < sg->numStrips; ++si )
						{
							OptimizedModel::StripHeader_t *strip = sg->pStrip( si );
							if ( !( strip->flags & OptimizedModel::STRIP_IS_TRILIST ) )
								return false;
							for ( int k = 0; k < strip->numIndices; k += 3 )
							{
								int at = strip->indexOffset + k;
								int a = sg->pVertex( *sg->pIndex( at ) )->origMeshVertID;
								int b = sg->pVertex( *sg->pIndex( at + 1 ) )->origMeshVertID;
								int c = sg->pVertex( *sg->pIndex( at + 2 ) )->origMeshVertID;
								Vector pos[3];
								VectorTransform( *vd->Position( a ), xform, pos[0] );
								VectorTransform( *vd->Position( b ), xform, pos[1] );
								VectorTransform( *vd->Position( c ), xform, pos[2] );
								int tri = scene.triangles.AddToTail();
								FillTriangle( scene.triangles[tri], pos[0], pos[1], pos[2], *vd->Texcoord( a ), *vd->Texcoord( b ), *vd->Texcoord( c ), mat, pi,
											  alpha );
							}
						}
					}
				}
			}
		}
	}
	return true;
}

struct ReSTIRPropEmitter
{
	ReSTIRMaterialEmission emission;
	CUtlVector<ReSTIRGpuEmitterTriangle> triangles;
};

static void AppendPropEmitterTriangle( CUtlVector<ReSTIRGpuEmitterTriangle> &triangles,
	const Vector positions[3], const Vector2D uv[3] )
{
	ReSTIRGpuEmitterTriangle triangle;
	for ( int axis = 0; axis < 3; ++axis )
	{
		triangle.v0[axis] = positions[0][axis];
		triangle.v1[axis] = positions[1][axis];
		triangle.v2[axis] = positions[2][axis];
	}
	triangle.v0[3] = uv[0].x;
	triangle.v1[3] = uv[0].y;
	triangle.v2[3] = uv[1].x;
	triangle.uv[0] = uv[1].y;
	triangle.uv[1] = uv[2].x;
	triangle.uv[2] = uv[2].y;
	triangle.uv[3] = 0.0f;
	triangles.AddToTail( triangle );
}

bool CReSTIRStaticPropMgr::AppendEmitters( ReSTIRScene &scene, const ReSTIROptions &options )
{
	// Geometry traversal mirrors vradstaticprops.cpp:1819-2017, but NO_SHADOW and the
	// non-shadow-casting material list govern visibility only, never surface emission.
	for ( int pi = 0; pi < m_Props.Count(); ++pi )
	{
		const Prop &p = *m_Props[pi];
		Model &m = *m_Models[p.lump.m_PropType];
		OptimizedModel::FileHeader_t *vh = (OptimizedModel::FileHeader_t *)m.vtx.Base();
		if ( !m.header || !vh )
			continue;
		matrix3x4_t xform;
		AngleMatrix( p.lump.m_Angles, p.lump.m_Origin, xform );
		const int skin = m.header->numskinfamilies > 0 ?
			clamp( (int)p.lump.m_Skin, 0, m.header->numskinfamilies - 1 ) : 0;
		CUtlDict<int, int> materialGroups;
		CUtlVector<ReSTIRPropEmitter *> emitters;
		for ( int body = 0; body < m.header->numbodyparts; ++body )
		{
			mstudiobodyparts_t *bp = m.header->pBodypart( body );
			OptimizedModel::BodyPartHeader_t *vbp = vh->pBodyPart( body );
			for ( int mid = 0; mid < bp->nummodels; ++mid )
			{
				mstudiomodel_t *sm = bp->pModel( mid );
				OptimizedModel::ModelLODHeader_t *lod = vbp->pModel( mid )->pLOD( 0 );
				for ( int meshId = 0; meshId < sm->nummeshes; ++meshId )
				{
					mstudiomesh_t *mesh = sm->pMesh( meshId );
					// studio.h skinref table maps each mesh material through the selected skin family.
					const int textureIndex = m.header->numskinfamilies > 0 ?
						*m.header->pSkinref( skin * m.header->numskinref + mesh->material ) : mesh->material;
					char materialName[MAX_PATH];
					ResolvePropMaterialName( m.header, textureIndex, materialName, sizeof( materialName ) );
					ReSTIRMaterialEmission emission;
					if ( !ReSTIR_GetMaterialEmission( scene, options, materialName, emission ) )
						continue;
					const mstudio_meshvertexdata_t *vd = mesh->GetVertexData( (void *)&m );
					if ( !vd )
						continue;
					int groupIndex = materialGroups.Find( materialName );
					if ( groupIndex == materialGroups.InvalidIndex() )
					{
						ReSTIRPropEmitter *emitter = new ReSTIRPropEmitter;
						emitter->emission = emission;
						groupIndex = materialGroups.Insert( materialName, emitters.AddToTail( emitter ) );
					}
					ReSTIRPropEmitter &emitter = *emitters[materialGroups[groupIndex]];
					OptimizedModel::MeshHeader_t *vm = lod->pMesh( meshId );
					for ( int group = 0; group < vm->numStripGroups; ++group )
					{
						OptimizedModel::StripGroupHeader_t *sg = vm->pStripGroup( group );
						for ( int si = 0; si < sg->numStrips; ++si )
						{
							OptimizedModel::StripHeader_t *strip = sg->pStrip( si );
							if ( !( strip->flags & OptimizedModel::STRIP_IS_TRILIST ) )
							{
								emitters.PurgeAndDeleteElements();
								return false;
							}
							for ( int k = 0; k < strip->numIndices; k += 3 )
							{
								Vector positions[3], normalSum( 0, 0, 0 );
								Vector2D uv[3];
								for ( int corner = 0; corner < 3; ++corner )
								{
									const int at = strip->indexOffset + k + corner;
									const int vertex = sg->pVertex( *sg->pIndex( at ) )->origMeshVertID;
									VectorTransform( *vd->Position( vertex ), xform, positions[corner] );
									Vector normal;
									VectorRotate( *vd->Normal( vertex ), xform, normal );
									normalSum += normal;
									uv[corner] = *vd->Texcoord( vertex );
								}
								Vector geometricNormal;
								CrossProduct( positions[1] - positions[0], positions[2] - positions[0], geometricNormal );
								if ( DotProduct( geometricNormal, normalSum ) < 0.0f )
								{
									V_swap( positions[1], positions[2] );
									V_swap( uv[1], uv[2] );
								}
								AppendPropEmitterTriangle( emitter.triangles, positions, uv );
								if ( emitter.emission.twoSided )
								{
									V_swap( positions[1], positions[2] );
									V_swap( uv[1], uv[2] );
									AppendPropEmitterTriangle( emitter.triangles, positions, uv );
								}
							}
						}
					}
				}
			}
		}
		for ( int i = 0; i < emitters.Count(); ++i )
		{
			const ReSTIRPropEmitter &emitter = *emitters[i];
			ReSTIR_AddSurfaceEmitter( scene, emitter.triangles.Base(), emitter.triangles.Count(),
				emitter.emission.intensity, emitter.emission.texture, RESTIR_LIGHT_MATERIAL );
		}
		emitters.PurgeAndDeleteElements();
	}
	return true;
}

static bool Bary( const Vector2D &p, const Vector2D &a, const Vector2D &b, const Vector2D &c, Vector &v )
{
	float d = ( b.y - c.y ) * ( a.x - c.x ) + ( c.x - b.x ) * ( a.y - c.y );
	if ( fabsf( d ) < 1e-8f )
		return false;
	v.x = ( ( b.y - c.y ) * ( p.x - c.x ) + ( c.x - b.x ) * ( p.y - c.y ) ) / d;
	v.y = ( ( c.y - a.y ) * ( p.x - c.x ) + ( a.x - c.x ) * ( p.y - c.y ) ) / d;
	v.z = 1 - v.x - v.y;
	return v.x >= 0 && v.y >= 0 && v.z >= 0;
}
struct PropColorTexel
{
	Vector color;
	Vector worldPosition;
	Vector worldNormal;
	float distanceToTri;
	bool valid;
	bool possiblyInteresting;
};

static Vector ComputeBarycentric( const Vector2D &c, const Vector2D &a, const Vector2D &b, float dAA, float dAB, float dBB, float invDenom )
{
	float dCA = c.Dot( a );
	float dCB = c.Dot( b );
	Vector result;
	result.y = ( dBB * dCA - dAB * dCB ) * invDenom;
	result.z = ( dAA * dCB - dAB * dCA ) * invDenom;
	result.x = 1.0f - result.y - result.z;
	return result;
}

static float ComputeBarycentricDistanceToTri( const Vector &bary, const Vector2D uv[3] )
{
	Vector2D realPos = bary.x * uv[0] + bary.y * uv[1] + bary.z * uv[2];
	int minIndex = 0;
	for ( int i = 1; i < 3; ++i )
		if ( bary[i] < bary[minIndex] )
			minIndex = i;
	return CalcDistanceToLineSegment2D( realPos, uv[( minIndex + 1 ) % 3], uv[( minIndex + 2 ) % 3] );
}

static void RasterizePropTriangle( const Vector2D uv[3], int width, int height, CUtlVector<PropColorTexel> &texels,
									const Vector worldPosition[3], const Vector worldNormal[3] )
{
	float minX = Min( uv[0].x, Min( uv[1].x, uv[2].x ) );
	float minY = Min( uv[0].y, Min( uv[1].y, uv[2].y ) );
	float maxX = Max( uv[0].x, Max( uv[1].x, uv[2].x ) );
	float maxY = Max( uv[0].y, Max( uv[1].y, uv[2].y ) );
	if ( minX == maxX || minY == maxY )
		return;
	minX = Max( 0.0f, minX ); minY = Max( 0.0f, minY );
	maxX = Min( 1.0f, maxX ); maxY = Min( 1.0f, maxY );
	const int filterRadius = 1;
	int iMinX = Max( 0, (int)( minX * width ) - filterRadius );
	int iMinY = Max( 0, (int)( minY * height ) - filterRadius );
	int iMaxX = Min( width - 1, (int)( maxX * width ) + 1 + filterRadius );
	int iMaxY = Min( height - 1, (int)( maxY * height ) + 1 + filterRadius );
	Vector2D edgeA = uv[1] - uv[0], edgeB = uv[2] - uv[0];
	float dAA = edgeA.Dot( edgeA ), dAB = edgeA.Dot( edgeB ), dBB = edgeB.Dot( edgeB );
	float invDenom = 1.0f / ( dAA * dBB - dAB * dAB );
	for ( int y = iMinY; y <= iMaxY; ++y )
		for ( int x = iMinX; x <= iMaxX; ++x )
		{
			Vector2D sample( ( x + 0.5f ) / width, ( y + 0.5f ) / height );
			Vector bary = ComputeBarycentric( sample - uv[0], edgeA, edgeB, dAA, dAB, dBB, invDenom );
			bool inside = bary.x >= 0.0f && bary.x <= 1.0f && bary.y >= 0.0f && bary.y <= 1.0f && bary.z >= 0.0f && bary.z <= 1.0f;
			int linear = x + y * width;
			PropColorTexel &texel = texels[linear];
			if ( texel.valid )
				continue;
			float distance = ComputeBarycentricDistanceToTri( bary, uv );
			bool doWrite = inside || !texel.possiblyInteresting || texel.distanceToTri > distance;
			if ( !doWrite )
				continue;
			texel.worldPosition = worldPosition[0] * bary.x + worldPosition[1] * bary.y + worldPosition[2] * bary.z;
			texel.worldNormal = worldNormal[0] * bary.x + worldNormal[1] * bary.y + worldNormal[2] * bary.z;
			VectorNormalize( texel.worldNormal );
			texel.valid = inside;
			texel.possiblyInteresting = true;
			texel.distanceToTri = distance;
		}
}

static int ComputeLinearPos( int x, int y, int width, int height )
{
	return Min( Max( 0, y ), height - 1 ) * width + Min( Max( 0, x ), width - 1 );
}
static int PropPointLeafnum( const Vector &p )
{
	int node = 0;
	while ( node >= 0 )
	{
		const dnode_t &n = dnodes[node];
		const dplane_t &plane = dplanes[n.planenum];
		node = n.children[DotProduct( plane.normal, p ) - plane.dist < 0.0f ? 1 : 0];
	}
	return -node - 1;
}
// Port: vradstaticprops.cpp:1137-1147 (PositionInSolid): CONTENTS_SOLID bit test, not equality.
static bool InSolid( const Vector &p )
{
	int leaf = PropPointLeafnum( p );
	return leaf >= 0 && ( dleafs[leaf].contents & CONTENTS_SOLID ) != 0;
}
bool CReSTIRStaticPropMgr::ComputeLighting( Prop &prop, int propIndex, const ReSTIRScene &scene, CReSTIRVulkanDevice &device )
{
	// Port: vradstaticprops.cpp:1311-1505 and 2285-2450. Point records are
	// submitted as one fixed batch; style 0 is direct + indirect.
	Model &model = *m_Models[prop.lump.m_PropType];
	studiohdr_t *hdr = model.header;
	OptimizedModel::FileHeader_t *vtx = (OptimizedModel::FileHeader_t *)model.vtx.Base();
	if ( !hdr || !vtx )
		return true;	// model failed to load (warned in UnserializeModelDict); VRAD skips such props too
	bool withVertex = !( prop.lump.m_Flags & STATIC_PROP_NO_PER_VERTEX_LIGHTING );
	bool withTexel = !( prop.lump.m_Flags & STATIC_PROP_NO_PER_TEXEL_LIGHTING );
	if ( !withVertex && !withTexel )
		return true;
	matrix3x4_t matPos, matNormal;
	AngleMatrix( prop.lump.m_Angles, prop.lump.m_Origin, matPos );
	AngleMatrix( prop.lump.m_Angles, matNormal );
	const int styleCount = Max( 1, scene.sceneStyles.Count() );
	const int skip = ( prop.lump.m_Flags & STATIC_PROP_NO_SELF_SHADOWING ) ? propIndex : -1;
	const unsigned int pointFlags =
		( skip >= 0 ? RESTIR_POINT_NO_SELF_SHADOW : 0 ) | ( ( prop.lump.m_Flags & STATIC_PROP_IGNORE_NORMALS ) ? RESTIR_POINT_IGNORE_NORMALS : 0 );
	CUtlVector<ReSTIRGpuPointQuery> queries;
	CUtlVector<int> queryIndex;
	CUtlVector<Vector> vertexColors;
	CUtlVector<Vector> vertexPositions;
	CUtlVector<Vector> vertexNormals;
	CUtlVector<unsigned char> vertexBad;
	CUtlVector<int> modelStarts;
	CUtlVector<int> modelCounts;
	for ( int body = 0; body < hdr->numbodyparts; ++body )
	{
		mstudiobodyparts_t *bp = hdr->pBodypart( body );
		for ( int mid = 0; mid < bp->nummodels; ++mid )
		{
			mstudiomodel_t *sm = bp->pModel( mid );
			const mstudio_meshvertexdata_t *vd = sm->nummeshes ? sm->pMesh( 0 )->GetVertexData( (void *)&model ) : NULL;
			modelStarts.AddToTail( vertexPositions.Count() );
			modelCounts.AddToTail( sm->numvertices );
			for ( int vi = 0; vi < sm->numvertices; ++vi )
			{
				Vector p = vec3_origin, n = vec3_origin;
				if ( vd )
				{
					VectorTransform( *vd->Position( vi ), matPos, p );
					VectorTransform( *vd->Normal( vi ), matNormal, n );
					VectorNormalize( n );
				}
				bool bad = InSolid( p );
				vertexPositions.AddToTail( p );
				vertexNormals.AddToTail( n );
				vertexBad.AddToTail( bad ? 1 : 0 );
				ReSTIRGpuPointQuery q;
				memset( &q, 0, sizeof( q ) );
				q.position[0] = p.x;
				q.position[1] = p.y;
				q.position[2] = p.z;
				q.normal[0] = n.x;
				q.normal[1] = n.y;
				q.normal[2] = n.z;
				q.flags = pointFlags;
				q.skipHitId = skip >= 0 ? RESTIR_TRACE_ID_STATICPROP | (unsigned int)skip : 0;
				queryIndex.AddToTail( bad ? -1 : queries.AddToTail( q ) );
				vertexColors.AddToTail( vec3_origin );
			}
		}
	}
	m_nVerticesLit += queries.Count();
	m_nVerticesInSolid += vertexBad.Count() - queries.Count();
	CUtlVector<ReSTIRGpuPointResult> results;
	if ( queries.Count() && !device.LightPoints( queries, results ) )
		return false;
	for ( int i = 0; i < queryIndex.Count(); ++i )
	{
		if ( queryIndex[i] < 0 )
			continue;
		ReSTIRGpuPointResult &r = results[queryIndex[i] * styleCount];
		vertexColors[i] = Vector( r.direct[0] + r.indirect[0], r.direct[1] + r.indirect[1], r.direct[2] + r.indirect[2] );
	}
	CUtlVector<ReSTIRGpuPointQuery> badQueries;
	CUtlVector<int> badVertexIndices;
	for ( int modelIndex = 0; modelIndex < modelStarts.Count(); ++modelIndex )
	{
		int start = modelStarts[modelIndex], count = modelCounts[modelIndex], badCount = 0;
		for ( int i = 0; i < count; ++i )
			badCount += vertexBad[start + i] ? 1 : 0;
		if ( !badCount || ( !( prop.lump.m_Flags & STATIC_PROP_USE_LIGHTING_ORIGIN ) && badCount == count ) )
			continue;
		for ( int i = 0; i < count; ++i )
			if ( vertexBad[start + i] )
			{
				Vector best = prop.lump.m_LightingOrigin;
				if ( !( prop.lump.m_Flags & STATIC_PROP_USE_LIGHTING_ORIGIN ) )
				{
					float closest = FLT_MAX;
					int bestIndex = -1;
					for ( int j = 0; j < count; ++j )
						if ( !vertexBad[start + j] )
						{
							float d = ( vertexPositions[start + j] - vertexPositions[start + i] ).LengthSqr();
							if ( d < closest )
							{
								closest = d;
								bestIndex = j;
							}
						}
					if ( bestIndex < 0 )
						continue;
					best = vertexPositions[start + bestIndex];
				}
				Vector mid;
				int iterations = 20;
				while ( --iterations > 0 )
				{
					mid = ( best + vertexPositions[start + i] ) * 0.5f;
					if ( InSolid( mid ) )
						break;
					best = mid;
				}
				ReSTIRGpuPointQuery q;
				memset( &q, 0, sizeof( q ) );
				q.position[0] = best.x;
				q.position[1] = best.y;
				q.position[2] = best.z;
				q.normal[0] = vertexNormals[start + i].x;
				q.normal[1] = vertexNormals[start + i].y;
				q.normal[2] = vertexNormals[start + i].z;
				badVertexIndices.AddToTail( start + i );
				badQueries.AddToTail( q );
			}
	}
	if ( badQueries.Count() )
	{
		CUtlVector<ReSTIRGpuPointResult> badResults;
		if ( !device.LightPoints( badQueries, badResults ) )
			return false;
		for ( int i = 0; i < badVertexIndices.Count(); ++i )
		{
			ReSTIRGpuPointResult &r = badResults[i * styleCount];
			vertexColors[badVertexIndices[i]] = Vector( r.direct[0] + r.indirect[0], r.direct[1] + r.indirect[1], r.direct[2] + r.indirect[2] );
		}
	}
	int base = 0;
	for ( int body = 0; body < hdr->numbodyparts; ++body )
	{
		mstudiobodyparts_t *bp = hdr->pBodypart( body );
		OptimizedModel::BodyPartHeader_t *vbp = vtx->pBodyPart( body );
		for ( int mid = 0; mid < bp->nummodels; ++mid )
		{
			mstudiomodel_t *sm = bp->pModel( mid );
			OptimizedModel::ModelHeader_t *vm = vbp->pModel( mid );
			for ( int lodNo = 0; lodNo < vtx->numLODs; ++lodNo )
			{
				OptimizedModel::ModelLODHeader_t *lod = vm->pLOD( lodNo );
				for ( int meshId = 0; meshId < sm->nummeshes; ++meshId )
				{
					OptimizedModel::MeshHeader_t *mesh = lod->pMesh( meshId );
					for ( int group = 0; group < mesh->numStripGroups; ++group )
					{
						OptimizedModel::StripGroupHeader_t *sg = mesh->pStripGroup( group );
						MeshLighting *out = new MeshLighting;
						out->lod = lodNo;
						if ( withVertex )
						{
							// vradstaticprops.cpp:1260: origMeshVertID is mesh-relative; add the mesh's vertexoffset.
							const int meshVertexOffset = sm->pMesh( meshId )->vertexoffset;
							out->colors.SetCount( sg->numVerts );
							for ( int vi = 0; vi < sg->numVerts; ++vi )
								out->colors[vi] = vertexColors[base + meshVertexOffset + sg->pVertex( vi )->origMeshVertID];
						}
						if ( withTexel )
						{
							const int width = Max( 1, (int)prop.lump.m_nLightmapResolutionX );
							const int height = Max( 1, (int)prop.lump.m_nLightmapResolutionY );
							const int texelCount = width * height;
							CUtlVector<PropColorTexel> texels;
							texels.SetCount( texelCount );
							for ( int i = 0; i < texelCount; ++i )
							{
								texels[i].color = vec3_origin;
								texels[i].distanceToTri = FLT_MAX;
								texels[i].valid = false;
								texels[i].possiblyInteresting = false;
							}
							const mstudio_meshvertexdata_t *vd = sm->pMesh( meshId )->GetVertexData( (void *)&model );
							for ( int si = 0; si < sg->numStrips; ++si )
							{
								OptimizedModel::StripHeader_t *strip = sg->pStrip( si );
								if ( !( strip->flags & OptimizedModel::STRIP_IS_TRILIST ) )
									continue;
								for ( int k = 0; k < strip->numIndices; k += 3 )
								{
									int at = strip->indexOffset + k;
									int a = sg->pVertex( *sg->pIndex( at ) )->origMeshVertID;
									int b = sg->pVertex( *sg->pIndex( at + 1 ) )->origMeshVertID;
									int c = sg->pVertex( *sg->pIndex( at + 2 ) )->origMeshVertID;
									Vector2D uv[3] = { *vd->Texcoord( a ), *vd->Texcoord( b ), *vd->Texcoord( c ) };
									Vector worldPosition[3], worldNormal[3];
									VectorTransform( *vd->Position( a ), matPos, worldPosition[0] );
									VectorTransform( *vd->Position( b ), matPos, worldPosition[1] );
									VectorTransform( *vd->Position( c ), matPos, worldPosition[2] );
									VectorTransform( *vd->Normal( a ), matNormal, worldNormal[0] );
									VectorTransform( *vd->Normal( b ), matNormal, worldNormal[1] );
									VectorTransform( *vd->Normal( c ), matNormal, worldNormal[2] );
									RasterizePropTriangle( uv, width, height, texels, worldPosition, worldNormal );
								}
							}
							CUtlVector<ReSTIRGpuPointQuery> texQueries;
							CUtlVector<int> texQueryIndex;
							texQueryIndex.SetCount( texelCount );
							for ( int i = 0; i < texelCount; ++i )
								texQueryIndex[i] = -1;
							for ( int y = 0; y < height; ++y )
								for ( int x = 0; x < width; ++x )
								{
									int index = x + y * width;
									bool shouldProcess = texels[index].valid;
									if ( texels[index].possiblyInteresting )
									{
										shouldProcess = shouldProcess ||
											texels[ComputeLinearPos( x - 1, y - 1, width, height )].valid ||
											texels[ComputeLinearPos( x, y - 1, width, height )].valid ||
											texels[ComputeLinearPos( x + 1, y - 1, width, height )].valid ||
											texels[ComputeLinearPos( x - 1, y, width, height )].valid ||
											texels[ComputeLinearPos( x + 1, y, width, height )].valid ||
											texels[ComputeLinearPos( x - 1, y + 1, width, height )].valid ||
											texels[ComputeLinearPos( x, y + 1, width, height )].valid ||
											texels[ComputeLinearPos( x + 1, y + 1, width, height )].valid;
									}
									if ( !shouldProcess )
										continue;
									ReSTIRGpuPointQuery query;
									memset( &query, 0, sizeof( query ) );
									query.position[0] = texels[index].worldPosition.x;
									query.position[1] = texels[index].worldPosition.y;
									query.position[2] = texels[index].worldPosition.z;
									query.normal[0] = texels[index].worldNormal.x;
									query.normal[1] = texels[index].worldNormal.y;
									query.normal[2] = texels[index].worldNormal.z;
									query.flags = pointFlags;
									query.skipHitId = skip >= 0 ? RESTIR_TRACE_ID_STATICPROP | (unsigned int)skip : 0;
									texQueryIndex[index] = texQueries.AddToTail( query );
								}
							CUtlVector<ReSTIRGpuPointResult> texResults;
							if ( texQueries.Count() && !device.LightPoints( texQueries, texResults ) )
							{
								delete out;
								return false;
							}
							for ( int i = 0; i < texelCount; ++i )
								if ( texQueryIndex[i] >= 0 )
								{
									ReSTIRGpuPointResult &result = texResults[texQueryIndex[i] * styleCount];
									texels[i].color = Vector( result.direct[0] + result.indirect[0], result.direct[1] + result.indirect[1], result.direct[2] + result.indirect[2] );
								}
							CUtlVector<Vector> filterSource;
							CUtlVector<Vector> filtered;
							filterSource.SetCount( texelCount );
							filtered.SetCount( texelCount );
							for ( int i = 0; i < texelCount; ++i )
								filterSource[i] = QuantizeTexelToLinear( texels[i].color );
							for ( int y = 0; y < height; ++y )
								for ( int x = 0; x < width; ++x )
								{
									int index = x + y * width;
									if ( !texels[index].valid )
									{
										filtered[index] = filterSource[index];
										continue;
									}
									Vector sum = vec3_origin;
									for ( int oy = -1; oy <= 1; ++oy )
										for ( int ox = -1; ox <= 1; ++ox )
										{
											int neighbour = ComputeLinearPos( x + ox, y + oy, width, height );
											if ( !texels[neighbour].valid )
												neighbour = index;
											sum += filterSource[neighbour];
										}
									filtered[index] = sum * ( 1.0f / 9.0f );
								}
							int totalTexels = texelCount, mipWidth = width, mipHeight = height;
							while ( mipWidth > 1 || mipHeight > 1 )
							{
								mipWidth = Max( 1, mipWidth >> 1 );
								mipHeight = Max( 1, mipHeight >> 1 );
								totalTexels += mipWidth * mipHeight;
							}
							out->texels.SetCount( totalTexels * 3 );
							int outputOffset = 0;
							mipWidth = width; mipHeight = height;
							CUtlVector<Vector> mip;
							mip.AddVectorToTail( filtered );
							while ( true )
							{
								for ( int i = 0; i < mipWidth * mipHeight; ++i )
								{
									unsigned char rgba[4];
									ConvertLinearToRGBA8888( &mip[i], rgba );
									out->texels[( outputOffset + i ) * 3 + 0] = rgba[0];
									out->texels[( outputOffset + i ) * 3 + 1] = rgba[1];
									out->texels[( outputOffset + i ) * 3 + 2] = rgba[2];
								}
								outputOffset += mipWidth * mipHeight;
								if ( mipWidth == 1 && mipHeight == 1 )
									break;
								int nextWidth = Max( 1, mipWidth >> 1 );
								int nextHeight = Max( 1, mipHeight >> 1 );
								CUtlVector<Vector> next;
								next.SetCount( nextWidth * nextHeight );
								for ( int y = 0; y < nextHeight; ++y )
									for ( int x = 0; x < nextWidth; ++x )
									{
										int x0 = Min( mipWidth - 1, x * 2 );
										int x1 = Min( mipWidth - 1, x0 + 1 );
										int y0 = Min( mipHeight - 1, y * 2 );
										int y1 = Min( mipHeight - 1, y0 + 1 );
										next[x + y * nextWidth] = ( mip[x0 + y0 * mipWidth] + mip[x1 + y0 * mipWidth] +
											mip[x0 + y1 * mipWidth] + mip[x1 + y1 * mipWidth] ) * 0.25f;
									}
								mip.Purge();
								mip.AddVectorToTail( next );
								mipWidth = nextWidth;
								mipHeight = nextHeight;
							}
						}
						prop.meshes.AddToTail( out );
					}
				}
			}
			base += sm->numvertices;
		}
	}
	return true;
}
static unsigned char *AlignData( unsigned char *p, unsigned char *base )
{
	size_t n = (size_t)( p - base );
	n = ( n + 511 ) & ~(size_t)511;
	return base + n;
}
bool CReSTIRStaticPropMgr::SerializeLighting()
{
	// Port: vradstaticprops.cpp:1510-1660. Win64 fix: offsets use size_t pointer differences.
	int vertexFiles = 0;
	int texelFiles = 0;
	for ( int i = 0; i < m_Props.Count(); ++i )
	{
		Prop &p = *m_Props[i];
		Model &m = *m_Models[p.lump.m_PropType];
		if ( !m.header )
			continue;
		int meshCount = p.meshes.Count();
		if ( !( p.lump.m_Flags & STATIC_PROP_NO_PER_VERTEX_LIGHTING ) )
		{
			int total = 0;
			for ( int j = 0; j < meshCount; ++j )
				total += p.meshes[j]->colors.Count();
			if ( total )
			{
				char name[64];
				V_snprintf( name, sizeof( name ), g_bHDR ? "sp_hdr_%d.vhv" : "sp_%d.vhv", i );
				CUtlBuffer b;
				b.EnsureCapacity( sizeof( HardwareVerts::FileHeader_t ) + meshCount * sizeof( HardwareVerts::MeshHeader_t ) + total * 4 + 1024 );
				Q_memset( b.Base(), 0, b.Size() );
				HardwareVerts::FileHeader_t *h = (HardwareVerts::FileHeader_t *)b.Base();
				h->m_nVersion = VHV_VERSION;
				h->m_nChecksum = m.header->checksum;
				h->m_nVertexFlags = VERTEX_COLOR;
				h->m_nVertexSize = 4;
				h->m_nVertexes = total;
				h->m_nMeshes = meshCount;
				unsigned char *d =
					AlignData( (unsigned char *)b.Base() + sizeof( *h ) + meshCount * sizeof( HardwareVerts::MeshHeader_t ), (unsigned char *)b.Base() );
				for ( int j = 0; j < meshCount; ++j )
				{
					HardwareVerts::MeshHeader_t *mh = h->pMesh( j );
					mh->m_nLod = p.meshes[j]->lod;
					mh->m_nVertexes = p.meshes[j]->colors.Count();
					mh->m_nOffset = (unsigned int)( d - (unsigned char *)b.Base() );
					for ( int k = 0; k < p.meshes[j]->colors.Count(); ++k )
					{
						ColorRGBExp32 c;
						VectorToColorRGBExp32( p.meshes[j]->colors[k], c );
						unsigned char rgba[4];
						ConvertRGBExp32ToRGBA8888( &c, rgba );
						d[0] = rgba[2];
						d[1] = rgba[1];
						d[2] = rgba[0];
						d[3] = rgba[3];
						d += 4;
					}
				}
				d = AlignData( d, (unsigned char *)b.Base() );
				AddBufferToPak( GetPakFile(), name, b.Base(), (int)( d - (unsigned char *)b.Base() ), false );
				++vertexFiles;
			}
		}
		if ( !( p.lump.m_Flags & STATIC_PROP_NO_PER_TEXEL_LIGHTING ) )
		{
			int total = 0;
			for ( int j = 0; j < meshCount; ++j )
				total += p.meshes[j]->texels.Count();
			if ( total )
			{
				char name[64];
				V_snprintf( name, sizeof( name ), "texelslighting_%d.ppl", i );
				CUtlBuffer b;
				b.EnsureCapacity( sizeof( HardwareTexels::FileHeader_t ) + meshCount * sizeof( HardwareTexels::MeshHeader_t ) + total + 1024 );
				Q_memset( b.Base(), 0, b.Size() );
				HardwareTexels::FileHeader_t *h = (HardwareTexels::FileHeader_t *)b.Base();
				h->m_nVersion = VHT_VERSION;
				h->m_nChecksum = m.header->checksum;
				h->m_nTexelFormat = IMAGE_FORMAT_RGB888;
				h->m_nMeshes = meshCount;
				unsigned char *d =
					AlignData( (unsigned char *)b.Base() + sizeof( *h ) + meshCount * sizeof( HardwareTexels::MeshHeader_t ), (unsigned char *)b.Base() );
				for ( int j = 0; j < meshCount; ++j )
				{
					HardwareTexels::MeshHeader_t *mh = h->pMesh( j );
					mh->m_nLod = p.meshes[j]->lod;
					mh->m_nOffset = (unsigned int)( d - (unsigned char *)b.Base() );
					mh->m_nBytes = p.meshes[j]->texels.Count();
					mh->m_nWidth = p.lump.m_nLightmapResolutionX;
					mh->m_nHeight = p.lump.m_nLightmapResolutionY;
					Q_memcpy( d, p.meshes[j]->texels.Base(), p.meshes[j]->texels.Count() );
					d += p.meshes[j]->texels.Count();
				}
				d = AlignData( d, (unsigned char *)b.Base() );
				AddBufferToPak( GetPakFile(), name, b.Base(), (int)( d - (unsigned char *)b.Base() ), false );
				++texelFiles;
			}
		}
	}
	Msg( "VRAD ReSTIR: static prop lighting: %d props, %d vertices lit (%d inside solid), %d %s files, %d texelslighting_*.ppl files written to the pak\n",
		m_Props.Count(), m_nVerticesLit + m_nVerticesInSolid, m_nVerticesInSolid, vertexFiles, g_bHDR ? "sp_hdr_*.vhv" : "sp_*.vhv", texelFiles );
	return true;
}
