//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "deferred/deferred_lights.h"
#include "shadowmaps_dx12.h"
#include "iclientshadowmgr.h"
#include "view.h"
#include "KeyValues.h"
#include "fmtstr.h"
#include "filesystem.h"
#include "tier0/memdbgon.h"

struct DeferredCookie_t
{
	int id;
	ClientShadowHandle_t handle;
};

static CUtlVector<DeferredCookie_t> s_Cookies;

static void FillCookieState( const DeferredLight_t &light, FlashlightState_t &state )
{
	Vector forward, right, up;
	AngleVectors( light.angles, &forward, &right, &up );
	BasisToQuaternion( forward, right, up, state.m_quatOrientation );
	state.m_vecLightOrigin = light.origin;
	state.m_fHorizontalFOVDegrees = light.outerCone * 2.0f;
	state.m_fVerticalFOVDegrees = state.m_fHorizontalFOVDegrees;
	state.m_NearZ = 4.0f;
	state.m_FarZ = light.radius > state.m_NearZ ? light.radius : state.m_NearZ + 1.0f;
	state.m_fConstantAtten = 0;
	state.m_fLinearAtten = 0;
	state.m_fQuadraticAtten = 1;
	Vector rgb = light.color * light.intensity;
	state.m_Color[0] = rgb.x;
	state.m_Color[1] = rgb.y;
	state.m_Color[2] = rgb.z;
	state.m_Color[3] = 0;
	state.m_bEnableShadows = light.shadow;
	state.m_pSpotlightTexture = materials->FindTexture( light.cookie, TEXTURE_GROUP_OTHER, false );
	state.m_nSpotlightTextureFrame = 0;
}

CDeferredLightManager::CDeferredLightManager() : CAutoGameSystemPerFrame( "DeferredLightManager" ), m_NextId( 1 )
{
}

static CDeferredLightManager g_DeferredLights;

CDeferredLightManager &DeferredLights()
{
	return g_DeferredLights;
}

bool CDeferredLightManager::MapFile( char *path, int pathBytes ) const
{
	const char *level = engine->GetLevelName();
	if ( !level || !level[0] )
		return false;
	char base[MAX_PATH];
	Q_FileBase( level, base, sizeof( base ) );
	if ( !base[0] )
		return false;
	Q_snprintf( path, pathBytes, "maps/%s_lights.txt", base );
	return true;
}

DeferredLight_t &CDeferredLightManager::Add( const DeferredLight_t &light )
{
	int index = m_Lights.AddToTail( light );
	DeferredLight_t &stored = m_Lights[index];
	stored.id = m_NextId++;
	return stored;
}

void CDeferredLightManager::Remove( int id )
{
	for ( int i = 0; i < m_Lights.Count(); ++i )
	{
		if ( m_Lights[i].id == id )
		{
			m_Lights.Remove( i );
			return;
		}
	}
}

DeferredLight_t *CDeferredLightManager::Find( int id )
{
	for ( int i = 0; i < m_Lights.Count(); ++i )
	{
		if ( m_Lights[i].id == id )
			return &m_Lights[i];
	}
	return NULL;
}

bool CDeferredLightManager::Load()
{
	DestroyCookies();
	m_Lights.RemoveAll();
	m_NextId = 1;
	char path[MAX_PATH];
	if ( !MapFile( path, sizeof( path ) ) )
		return false;
	KeyValues *root = new KeyValues( "lights" );
	if ( !root->LoadFromFile( filesystem, path, "GAME" ) )
	{
		root->deleteThis();
		return false;
	}
	for ( KeyValues *sub = root->GetFirstSubKey(); sub; sub = sub->GetNextKey() )
	{
		if ( Q_stricmp( sub->GetName(), "light" ) )
			continue;
		DeferredLight_t light;
		memset( &light, 0, sizeof( light ) );
		sscanf( sub->GetString( "origin", "0 0 0" ), "%f %f %f", &light.origin.x, &light.origin.y, &light.origin.z );
		sscanf( sub->GetString( "angles", "0 0 0" ), "%f %f %f", &light.angles.x, &light.angles.y, &light.angles.z );
		sscanf( sub->GetString( "color", "1 1 1" ), "%f %f %f", &light.color.x, &light.color.y, &light.color.z );
		light.intensity = sub->GetFloat( "intensity", 10000 );
		light.radius = sub->GetFloat( "radius", 256 );
		light.innerCone = sub->GetFloat( "innerCone", 20 );
		light.outerCone = sub->GetFloat( "outerCone", 45 );
		light.spot = sub->GetInt( "spot" ) != 0;
		light.shadow = sub->GetInt( "shadow", 1 ) != 0;
		V_strncpy( light.cookie, sub->GetString( "cookie", "" ), sizeof( light.cookie ) );
		light.id = sub->GetInt( "id", 0 );
		if ( light.id <= 0 || Find( light.id ) )
			light.id = m_NextId++;
		else if ( light.id >= m_NextId )
			m_NextId = light.id + 1;
		m_Lights.AddToTail( light );
	}
	root->deleteThis();
	return true;
}

bool CDeferredLightManager::Save()
{
	char path[MAX_PATH];
	if ( !MapFile( path, sizeof( path ) ) )
		return false;
	KeyValues *root = new KeyValues( "lights" );
	for ( int i = 0; i < m_Lights.Count(); ++i )
	{
		const DeferredLight_t &light = m_Lights[i];
		KeyValues *sub = new KeyValues( "light" );
		root->AddSubKey( sub );
		sub->SetString( "origin", CFmtStr( "%f %f %f", light.origin.x, light.origin.y, light.origin.z ) );
		sub->SetString( "angles", CFmtStr( "%f %f %f", light.angles.x, light.angles.y, light.angles.z ) );
		sub->SetString( "color", CFmtStr( "%f %f %f", light.color.x, light.color.y, light.color.z ) );
		sub->SetFloat( "intensity", light.intensity );
		sub->SetFloat( "radius", light.radius );
		sub->SetFloat( "innerCone", light.innerCone );
		sub->SetFloat( "outerCone", light.outerCone );
		sub->SetInt( "spot", light.spot ? 1 : 0 );
		sub->SetInt( "shadow", light.shadow ? 1 : 0 );
		sub->SetString( "cookie", light.cookie );
		sub->SetInt( "id", light.id );
	}
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	root->RecursiveSaveToFile( buf, 0 );
	root->deleteThis();
	filesystem->CreateDirHierarchy( "maps", "GAME" );
	return filesystem->WriteFile( path, "GAME", buf );
}

void CDeferredLightManager::DestroyCookies()
{
	for ( int i = 0; i < s_Cookies.Count(); ++i )
		g_pClientShadowMgr->DestroyFlashlight( s_Cookies[i].handle );
	s_Cookies.RemoveAll();
}

void CDeferredLightManager::UpdateCookies()
{
	CUtlVector<DeferredCookie_t> next;
	for ( int i = 0; i < m_Lights.Count(); ++i )
	{
		const DeferredLight_t &light = m_Lights[i];
		if ( !light.spot || !light.cookie[0] )
			continue;
		FlashlightState_t state;
		FillCookieState( light, state );
		int slot = -1;
		for ( int c = 0; c < s_Cookies.Count(); ++c )
		{
			if ( s_Cookies[c].id == light.id )
			{
				slot = c;
				break;
			}
		}
		DeferredCookie_t cookie;
		if ( slot < 0 )
		{
			cookie.id = light.id;
			cookie.handle = g_pClientShadowMgr->CreateFlashlight( state );
		}
		else
		{
			cookie = s_Cookies[slot];
			s_Cookies.Remove( slot );
			g_pClientShadowMgr->UpdateFlashlightState( cookie.handle, state );
			g_pClientShadowMgr->UpdateProjectedTexture( cookie.handle, true );
		}
		next.AddToTail( cookie );
	}
	DestroyCookies();
	s_Cookies.Swap( next );
}

void CDeferredLightManager::LevelInitPostEntity()
{
	Load();
}

void CDeferredLightManager::LevelShutdownPreEntity()
{
	DestroyCookies();
	m_Lights.RemoveAll();
	m_NextId = 1;
	ShadowMapsDX12_SetDynamicLights( NULL, 0 );
}

void CDeferredLightManager::Update( float frametime )
{
	ShadowMapsDX12_SetDynamicLights( m_Lights.Base(), m_Lights.Count() );
	UpdateCookies();
}

static void DeferredLightAdd()
{
	Vector forward;
	AngleVectors( MainViewAngles(), &forward );
	const Vector start = MainViewOrigin();
	trace_t tr;
	UTIL_TraceLine( start, start + forward * 4096.0f, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
	DeferredLight_t light;
	memset( &light, 0, sizeof( light ) );
	light.origin = tr.fraction < 1.0f ? tr.endpos + tr.plane.normal * 8.0f : start + forward * 128.0f;
	light.angles = MainViewAngles();
	light.color.Init( 1, 1, 1 );
	light.intensity = 10000;
	light.radius = 256;
	light.innerCone = 20;
	light.outerCone = 45;
	light.spot = false;
	light.shadow = true;
	DeferredLight_t &added = DeferredLights().Add( light );
	Msg( "deferred_light_add: id %d at %.0f %.0f %.0f\n", added.id, added.origin.x, added.origin.y, added.origin.z );
}

static void DeferredLightsSaveCmd()
{
	char path[MAX_PATH];
	if ( DeferredLights().MapFile( path, sizeof( path ) ) && DeferredLights().Save() )
		Msg( "deferred_lights_save: wrote %s\n", path );
	else
		Warning( "deferred_lights_save: failed\n" );
}

static void DeferredLightsReloadCmd()
{
	if ( DeferredLights().Load() )
		Msg( "deferred_lights_reload: %d lights\n", DeferredLights().Lights().Count() );
	else
		Warning( "deferred_lights_reload: no lights file for this map\n" );
}

ConCommand deferred_light_add( "deferred_light_add", DeferredLightAdd, "Add an editor light at the crosshair" );
ConCommand deferred_lights_save( "deferred_lights_save", DeferredLightsSaveCmd, "Write maps/<map>_lights.txt" );
ConCommand deferred_lights_reload( "deferred_lights_reload", DeferredLightsReloadCmd, "Reload maps/<map>_lights.txt" );
