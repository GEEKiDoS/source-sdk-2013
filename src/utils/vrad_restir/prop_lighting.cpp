//========= Copyright Valve Corporation, All rights reserved. ============//
// Detail-prop ports: vraddetailprops.cpp:79-99, 130-153, 579-617,
// 783-827, and 880-1037. Static-prop orchestration uses
// vradstaticprops.cpp:1311-1505 and :1510-1660.
#include "prop_lighting.h"
#include "restir_staticprops.h"
#include "restir_vulkan.h"
#include "bsplib.h"
#include "gamebspfile.h"
#include "filesystem.h"
#include "studio.h"
#include "tier1/utlbuffer.h"
#include "tier1/utlvector.h"
#include "bitmap/imageformat.h"
#include <string.h>

extern bool g_bHDR;

static CUtlVector<DetailPropLightstylesLump_t> s_DetailPropLightStyleLumpLDR;
static CUtlVector<DetailPropLightstylesLump_t> s_DetailPropLightStyleLumpHDR;
static CUtlVector<DetailPropLightstylesLump_t> *s_pDetailPropLightStyleLump = &s_DetailPropLightStyleLumpLDR;
static CUtlVector<Vector> s_ModelCenterOffset;
static CUtlVector<Vector> s_SpriteCenterOffset;

void VRadRestirDetailProps_SetHDRMode( bool bHDR )
{
	// Port of vraddetailprops.cpp:89-99; this hook is explicit because this
	// DLL does not define VRAD (I8).
	s_pDetailPropLightStyleLump = bHDR ? &s_DetailPropLightStyleLumpHDR : &s_DetailPropLightStyleLumpLDR;
}

static bool UnserializeDetailProps( DetailObjectLump_t *&props, int &count )
{
	props = NULL;
	count = 0;
	GameLumpHandle_t handle = g_GameLumps.GetGameLumpHandle( GAMELUMP_DETAIL_PROPS );
	if ( handle == g_GameLumps.InvalidGameLump() || g_GameLumps.GetGameLumpVersion( handle ) != GAMELUMP_DETAIL_PROPS_VERSION )
		return false;
	int size = g_GameLumps.GameLumpSize( handle );
	if ( size <= 0 )
		return true;
	CUtlBuffer buf( g_GameLumps.GetGameLump( handle ), size, CUtlBuffer::READ_ONLY );
	int modelCount = buf.GetInt();
	if ( modelCount < 0 || modelCount > 1 << 16 )
		return false;
	s_ModelCenterOffset.SetCount( modelCount );
	for ( int i = 0; i < modelCount; ++i )
	{
		DetailObjectDictLump_t lump;
		buf.Get( &lump, sizeof( lump ) );
		CUtlBuffer mdl;
		if ( ReSTIR_LoadStudioModel( lump.m_Name, mdl ) )
		{
			studiohdr_t *h = (studiohdr_t *)mdl.Base();
			s_ModelCenterOffset[i] = ( h->hull_min + h->hull_max ) * 0.5f;
		}
		else
			s_ModelCenterOffset[i] = vec3_origin;
	}
	int spriteCount = buf.GetInt();
	if ( spriteCount < 0 || spriteCount > 1 << 16 )
		return false;
	s_SpriteCenterOffset.SetCount( spriteCount );
	for ( int i = 0; i < spriteCount; ++i )
	{
		DetailSpriteDictLump_t lump;
		buf.Get( &lump, sizeof( lump ) );
		s_SpriteCenterOffset[i] = Vector( 0, ( lump.m_LR.x + lump.m_UL.x ) * 0.5f, ( lump.m_LR.y + lump.m_UL.y ) * 0.5f );
	}
	count = buf.GetInt();
	if ( count < 0 || count > ( size - buf.TellGet() ) / (int)sizeof( DetailObjectLump_t ) )
		return false;
	if ( count )
		props = (DetailObjectLump_t *)buf.PeekGet();
	return true;
}

static void ComputeWorldCenter( const DetailObjectLump_t &prop, Vector &center, Vector &normal )
{
	Vector forward, right;
	AngleVectors( prop.m_Angles, &forward, &right, &normal );
	center = prop.m_Origin;
	if ( prop.m_Type == DETAIL_PROP_TYPE_MODEL )
	{
		if ( s_ModelCenterOffset.IsValidIndex( prop.m_DetailModel ) )
		{
			VectorMA( center, s_ModelCenterOffset[prop.m_DetailModel].x, forward, center );
			VectorMA( center, -s_ModelCenterOffset[prop.m_DetailModel].y, right, center );
			VectorMA( center, s_ModelCenterOffset[prop.m_DetailModel].z, normal, center );
		}
	}
	else if ( prop.m_Type == DETAIL_PROP_TYPE_SPRITE && s_SpriteCenterOffset.IsValidIndex( prop.m_DetailModel ) )
	{
		Vector off = s_SpriteCenterOffset[prop.m_DetailModel] * prop.m_flScale;
		VectorMA( center, off.x, forward, center );
		VectorMA( center, -off.y, right, center );
		VectorMA( center, off.z, normal, center );
	}
}

static bool ReadOppositeDetailLighting()
{
	CUtlVector<DetailPropLightstylesLump_t> &dst = g_bHDR ? s_DetailPropLightStyleLumpLDR : s_DetailPropLightStyleLumpHDR;
	int id = g_bHDR ? GAMELUMP_DETAIL_PROP_LIGHTING : GAMELUMP_DETAIL_PROP_LIGHTING_HDR;
	int version = g_bHDR ? GAMELUMP_DETAIL_PROP_LIGHTING_VERSION : GAMELUMP_DETAIL_PROP_LIGHTING_HDR_VERSION;
	GameLumpHandle_t h = g_GameLumps.GetGameLumpHandle( id );
	if ( h == g_GameLumps.InvalidGameLump() || g_GameLumps.GetGameLumpVersion( h ) != version )
		return true;
	int size = g_GameLumps.GameLumpSize( h );
	if ( size < (int)sizeof( int ) )
		return false;
	CUtlBuffer b( g_GameLumps.GetGameLump( h ), size, CUtlBuffer::READ_ONLY );
	int count = b.GetInt();
	if ( count < 0 || (int)sizeof( int ) + count * (int)sizeof( DetailPropLightstylesLump_t ) > size )
		return false;
	dst.SetCount( count );
	if ( count )
		b.Get( dst.Base(), count * sizeof( DetailPropLightstylesLump_t ) );
	return true;
}

static bool WriteSelectedDetailLighting()
{
	int id = g_bHDR ? GAMELUMP_DETAIL_PROP_LIGHTING_HDR : GAMELUMP_DETAIL_PROP_LIGHTING;
	int version = g_bHDR ? GAMELUMP_DETAIL_PROP_LIGHTING_HDR_VERSION : GAMELUMP_DETAIL_PROP_LIGHTING_VERSION;
	GameLumpHandle_t h = g_GameLumps.GetGameLumpHandle( id );
	if ( h != g_GameLumps.InvalidGameLump() )
		g_GameLumps.DestroyGameLump( h );
	CUtlVector<DetailPropLightstylesLump_t> &src = *s_pDetailPropLightStyleLump;
	int bytes = src.Count() * sizeof( DetailPropLightstylesLump_t );
	h = g_GameLumps.CreateGameLump( id, sizeof( int ) + bytes, 0, version );
	if ( h == g_GameLumps.InvalidGameLump() )
		return false;
	CUtlBuffer b( g_GameLumps.GetGameLump( h ), sizeof( int ) + bytes );
	b.PutInt( src.Count() );
	if ( bytes )
		b.Put( src.Base(), bytes );
	return true;
}

bool ReSTIR_ComputeStaticPropLighting( const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device )
{
	const int mode = g_bHDR ? 1 : 0;
	g_ReSTIRStaticPropMgr.m_Visibility[mode].Clear();
	g_ReSTIRStaticPropMgr.m_Visibility[mode].selectedLightCount = scene.shadowLights.Count();
	g_ReSTIRStaticPropMgr.m_VisibilityComplete[mode] = false;
	g_ReSTIRStaticPropMgr.m_VisibilitySharedHDR = false;
	// Visibility belongs to every static receiver, not to the RGB/VHV option
	// or NO_PER_VERTEX_LIGHTING. Preserve those controls only for native RGB.
	for ( int i = 0; i < g_ReSTIRStaticPropMgr.m_Props.Count(); ++i )
	{
		CReSTIRStaticPropMgr::Prop &prop = *g_ReSTIRStaticPropMgr.m_Props[i];
		prop.meshes.PurgeAndDeleteElements();
		if ( !g_ReSTIRStaticPropMgr.ComputeLighting( prop, i, scene, device, options.staticPropLighting, options.shadowMaps ) )
			return false;
	}
	if ( options.staticPropLighting )
	{
		if ( !g_ReSTIRStaticPropMgr.SerializeLighting() )
			return false;
		g_LevelFlags |= g_bHDR ? LVLFLAGS_BAKED_STATIC_PROP_LIGHTING_HDR : LVLFLAGS_BAKED_STATIC_PROP_LIGHTING_NONHDR;
	}
	else
		g_LevelFlags &= ~( g_bHDR ? LVLFLAGS_BAKED_STATIC_PROP_LIGHTING_HDR : LVLFLAGS_BAKED_STATIC_PROP_LIGHTING_NONHDR );
	g_ReSTIRStaticPropMgr.m_VisibilityComplete[mode] = true;
	if ( options.shadowMaps ) ReSTIR_LogStaticPropDirectSummary( options.hdr );
	return true;
}
bool ReSTIR_ReuseStaticPropLighting( const ReSTIROptions &options )
{
	if ( !options.hdr || !g_ReSTIRStaticPropMgr.m_VisibilityComplete[0] )
		return false;
	if ( !options.staticPropLighting )
		g_LevelFlags &= ~LVLFLAGS_BAKED_STATIC_PROP_LIGHTING_HDR;
	else
	{
		// Serialize the retained full-precision meshes through the normal HDR
		// VHV filename/flags and shared PPL contract; never copy an identity header.
		if ( !g_ReSTIRStaticPropMgr.SerializeLighting() )
			return false;
		g_LevelFlags |= LVLFLAGS_BAKED_STATIC_PROP_LIGHTING_HDR;
	}
	// The caller has proved exact whole-scene paired equality. Retain the
	// complete original-vertex visibility alongside RGB, with no lossy copy.
	g_ReSTIRStaticPropMgr.m_VisibilitySharedHDR = true;
	g_ReSTIRStaticPropMgr.m_VisibilityComplete[1] = true;
	if ( options.shadowMaps ) ReSTIR_LogStaticPropDirectSummary( options.hdr );
	return true;
}

bool ReSTIR_ReuseDetailPropLighting()
{
	if ( !g_bHDR )
		return false;
	DetailObjectLump_t *props;
	int count;
	if ( !UnserializeDetailProps( props, count ) )
		return false;
	if ( !count )
		return true;
	if ( !ReadOppositeDetailLighting() )
		return false;
	s_DetailPropLightStyleLumpHDR.CopyArray( s_DetailPropLightStyleLumpLDR.Base(), s_DetailPropLightStyleLumpLDR.Count() );
	// The shared dprp lighting/style indices are already the equal HDR result.
	return WriteSelectedDetailLighting();
}

bool ReSTIR_ComputeDetailPropLighting( const ReSTIROptions &, const ReSTIRScene &scene, CReSTIRVulkanDevice &device )
{
	DetailObjectLump_t *props;
	int count;
	if ( !UnserializeDetailProps( props, count ) )
		return false;
	if ( !count )
		return true;
	if ( !ReadOppositeDetailLighting() )
		return false;
	s_pDetailPropLightStyleLump->RemoveAll();
	CUtlVector<ReSTIRGpuPointQuery> queries;
	queries.SetCount( count );
	for ( int i = 0; i < count; ++i )
	{
		Vector p, n;
		ComputeWorldCenter( props[i], p, n );
		ReSTIRGpuPointQuery &q = queries[i];
		memset( &q, 0, sizeof( q ) );
		q.position[0] = p.x;
		q.position[1] = p.y;
		q.position[2] = p.z;
		q.normal[0] = n.x;
		q.normal[1] = n.y;
		q.normal[2] = n.z;
		// vraddetailprops.cpp:622-652: detail props gather the full sphere (no normal), every style.
		q.flags = RESTIR_POINT_DETAIL_GATHER;
		q.skipHitId = 0;
	}
	CUtlVector<ReSTIRGpuPointResult> results;
	if ( !device.LightPoints( queries, results ) )
		return false;
	int styles = scene.sceneStyles.Count();
	if ( results.Count() < count * styles )
		return false;
	for ( int i = 0; i < count; ++i )
	{
		DetailObjectLump_t &prop = props[i];
		Vector p, n;
		ComputeWorldCenter( prop, p, n );
		if ( !p.IsValid() || !n.IsValid() )
		{
			// vraddetailprops.cpp:168-183/757-772: bogus prop -> debug red (VRAD also emits one red entry per
			// style here; that artifact is not reproduced).
			static bool s_Warned = false;
			if ( !s_Warned )
			{
				Warning( "WARNING: Bogus detail props encountered!\n" );
				s_Warned = true;
			}
			VectorToColorRGBExp32( Vector( 2, 0, 0 ), prop.m_Lighting );
			prop.m_LightStyles = 0;
			prop.m_LightStyleCount = 0;
			continue;
		}
		ReSTIRGpuPointResult &r0 = results[i * styles];
		VectorToColorRGBExp32( Vector( r0.direct[0] + r0.indirect[0], r0.direct[1] + r0.indirect[1], r0.direct[2] + r0.indirect[2] ), prop.m_Lighting );
		prop.m_LightStyles = 0;
		prop.m_LightStyleCount = 0;
		for ( int slot = 1; slot < styles; ++slot )
		{
			ReSTIRGpuPointResult &r = results[i * styles + slot];
			Vector c( r.direct[0] + r.indirect[0], r.direct[1] + r.indirect[1], r.direct[2] + r.indirect[2] );
			c *= 0.5f;
			if ( c.LengthSqr() <= 0.0f )
				continue;
			if ( prop.m_LightStyleCount == 0 )
				prop.m_LightStyles = s_pDetailPropLightStyleLump->Count();
			int at = s_pDetailPropLightStyleLump->AddToTail();
			VectorToColorRGBExp32( c, ( *s_pDetailPropLightStyleLump )[at].m_Lighting );
			( *s_pDetailPropLightStyleLump )[at].m_Style = (unsigned char)scene.sceneStyles[slot];
			++prop.m_LightStyleCount;
		}
	}
	return WriteSelectedDetailLighting();
}
