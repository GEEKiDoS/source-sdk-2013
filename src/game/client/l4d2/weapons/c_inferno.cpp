//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_inferno.h"
#include "clientleafsystem.h"
#include "particle_property.h"
#include "view.h"
#include "view_shared.h"
#include "iefx.h"
#include "dlight.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imesh.h"
#include "materialsystem/materialsystemutil.h"
#include "tier0/memdbgon.h"

ConVar inferno_dlight_spacing( "inferno_dlight_spacing", "200", 0x00004008, "Inferno dlights are at least this far apart" );
ConVar inferno_dlights( "inferno_dlights", "30", 0x0000000A, "Min FPS at which molotov dlights will be created" );
ConVar inferno_fire( "inferno_fire", "2", 0x0000000A );
// This is the original entity-flame control, consumed by that client owner rather than ground fire.
ConVar inferno_entity_flame_scale( "inferno_entity_flame_scale", "0", 0x0000000A );

IMPLEMENT_CLIENTCLASS_DT( C_Inferno, DT_Inferno, CInferno )
	RecvPropArray3( RECVINFO_ARRAY( m_fireXDelta ), RecvPropInt( RECVINFO( m_fireXDelta[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_fireYDelta ), RecvPropInt( RECVINFO( m_fireYDelta[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_fireZDelta ), RecvPropInt( RECVINFO( m_fireZDelta[0] ) ) ),
	RecvPropInt( RECVINFO( m_fireCount ) ),
END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_DT( C_FireCrackerBlast, DT_FireCrackerBlast, CFireCrackerBlast )
END_RECV_TABLE()

C_Inferno::C_Inferno()
{
	m_fireCount = 0;
	m_nDrawables = 0;
	m_flFireHalfWidth = 30.0f;
	m_flFireHeight = 80.0f;
	m_mins.Init();
	m_maxs.Init();
	m_hFireEffect = NULL;
	for ( int i = 0; i < MAX_DRAWABLES; ++i )
	{
		m_fireXDelta[i] = 0;
		m_fireYDelta[i] = 0;
		m_fireZDelta[i] = 0;
	}
}

C_Inferno::~C_Inferno()
{
	StopParticles();
}

void C_Inferno::Spawn()
{
	BaseClass::Spawn();
	m_fireCount = 0;
	m_nDrawables = 0;
	m_hFireEffect = NULL;
	m_mins.Init();
	m_maxs.Init();
	SetNextClientThink( CLIENT_THINK_ALWAYS );
}

void C_Inferno::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );
	if ( updateType == DATA_UPDATE_CREATED )
	{
		// Spawn's zeroing precedes receipt in the original. Do not clear received
		// fire deltas/count here, after the initial packet has already been decoded.
		SetNextClientThink( CLIENT_THINK_ALWAYS );
		UpdateVisibility();
	}
}

void C_Inferno::UpdateOnRemove()
{
	StopParticles();
	BaseClass::UpdateOnRemove();
}

const char *C_Inferno::GetParticleEffectName() const { return "molotov_groundfire"; }
const char *C_FireCrackerBlast::GetParticleEffectName() const { return "firework_crate_ground_effect"; }

void C_Inferno::StopParticles()
{
	if ( m_hFireEffect )
	{
		m_hFireEffect->StopEmission( false, false, false );
		m_hFireEffect = NULL;
	}
}

void C_Inferno::NotifyBoundsChanged()
{
	if ( GetRenderHandle() != INVALID_CLIENT_RENDER_HANDLE )
		ClientLeafSystem()->RenderableChanged( GetRenderHandle() );
}

void C_Inferno::GetRenderBounds( Vector &mins, Vector &maxs )
{
	if ( m_nDrawables )
	{
		mins = m_mins - GetRenderOrigin();
		maxs = m_maxs - GetRenderOrigin();
	}
	else
	{
		mins.Init();
		maxs.Init();
	}
}

void C_Inferno::GetRenderBoundsWorldspace( Vector &mins, Vector &maxs )
{
	if ( m_nDrawables )
	{
		mins = m_mins;
		maxs = m_maxs;
	}
	else
	{
		mins.Init();
		maxs.Init();
	}
}

void C_Inferno::RecomputeBounds()
{
	m_mins.Init( 999999.9f, 999999.9f, 999999.9f );
	m_maxs.Init( -999999.9f, -999999.9f, -999999.9f );
	for ( int i = 0; i < m_nDrawables; ++i )
	{
		const Drawable &fire = m_drawable[i];
		if ( fire.m_state == DEAD )
			continue;
		m_mins.x = MIN( m_mins.x, fire.m_pos.x - m_flFireHalfWidth );
		m_maxs.x = MAX( m_maxs.x, fire.m_pos.x + m_flFireHalfWidth );
		m_mins.y = MIN( m_mins.y, fire.m_pos.y - m_flFireHalfWidth );
		m_maxs.y = MAX( m_maxs.y, fire.m_pos.y + m_flFireHalfWidth );
		m_mins.z = MIN( m_mins.z, fire.m_pos.z );
		m_maxs.z = MAX( m_maxs.z, fire.m_pos.z + m_flFireHeight );
	}
}

C_Inferno::Drawable *C_Inferno::FindDrawable( const Vector &position )
{
	for ( int i = 0; i < m_nDrawables; ++i )
	{
		Drawable &fire = m_drawable[i];
		if ( fabsf( fire.m_pos.x - position.x ) <= 12.0f &&
			fabsf( fire.m_pos.y - position.y ) <= 12.0f && fabsf( fire.m_pos.z - position.z ) <= 12.0f )
		{
			fire.m_pos = position;
			return &fire;
		}
	}
	return NULL;
}

void C_Inferno::SynchronizeDrawables()
{
	for ( int i = 0; i < m_nDrawables; ++i )
	{
		if ( m_drawable[i].m_state == BURNING )
			m_drawable[i].m_state = UNCLAIMED;
	}
	for ( int i = 0; i < m_fireCount; ++i )
	{
		// A zero X/Y pair is the original removal sentinel. The Z delta is not
		// tested, even for a genuinely burning cell directly above the origin.
		if ( !m_fireXDelta[i] && !m_fireYDelta[i] )
			continue;
		Vector position = GetAbsOrigin() + Vector( m_fireXDelta[i], m_fireYDelta[i], m_fireZDelta[i] );
		Drawable *pFound = FindDrawable( position );
		if ( pFound )
		{
			if ( pFound->m_state == UNCLAIMED )
				pFound->m_state = BURNING;
			continue;
		}
		if ( m_nDrawables >= MAX_DRAWABLES )
			continue;
		Drawable &fire = m_drawable[m_nDrawables];
		fire.m_state = GROWING;
		fire.m_flStateTime = gpGlobals->realtime;
		fire.m_pos = position;
		// DT_Inferno does not send normals; original client allocation leaves these zero.
		fire.m_normal.Init();
		fire.m_flFrameInterval = RandomFloat( 0.04f, 0.06f );
		fire.m_bFlip = RandomInt( 0, 100 ) < 50;
		fire.m_flHeight = 0.0f;
		fire.m_flMaxHeight = RandomFloat( 70.0f, 90.0f );
		fire.m_nLight = entindex() + m_nDrawables + 0x10000000;
		float flSpacing = inferno_dlight_spacing.GetFloat();
		for ( int j = 0; j < m_nDrawables; ++j )
		{
			if ( m_drawable[j].m_state != DEAD && m_drawable[j].m_nLight > 0 &&
				( m_drawable[j].m_pos - position ).LengthSqr() < flSpacing * flSpacing )
			{
				fire.m_nLight = 0;
				break;
			}
		}
		// Preserve original ordering: the new drawable is not included until the
		// next bounds calculation, even though it has already been initialized.
		RecomputeBounds();
		NotifyBoundsChanged();
		++m_nDrawables;
	}
	for ( int i = 0; i < m_nDrawables; ++i )
	{
		if ( m_drawable[i].m_state == UNCLAIMED )
		{
			m_drawable[i].m_state = FADING;
			m_drawable[i].m_flStateTime = gpGlobals->realtime;
		}
	}
}

void C_Inferno::ClientThink()
{
	SynchronizeDrawables();
	bool bBoundsChanged = false;
	for ( int i = 0; i < m_nDrawables; ++i )
	{
		Drawable &fire = m_drawable[i];
		float flElapsed = gpGlobals->realtime - fire.m_flStateTime;
		if ( fire.m_state == GROWING )
		{
			fire.m_flHeight = flElapsed * ( fire.m_flMaxHeight * 0.5f );
			if ( fire.m_flHeight > fire.m_flMaxHeight )
			{
				fire.m_flHeight = fire.m_flMaxHeight;
				fire.m_state = BURNING;
				fire.m_flStateTime = gpGlobals->realtime;
			}
		}
		else if ( fire.m_state == FADING )
		{
			fire.m_flHeight = fire.m_flMaxHeight - flElapsed * ( fire.m_flMaxHeight * 0.5f );
			if ( fire.m_flHeight <= 0.0f )
			{
				fire.m_state = DEAD;
				fire.m_flStateTime = gpGlobals->realtime;
				RecomputeBounds();
				NotifyBoundsChanged();
				bBoundsChanged = true;
			}
		}
	}
	if ( GetMoveParent() && !bBoundsChanged )
		RecomputeBounds();
	UpdateParticles();
}

void C_Inferno::UpdateParticles()
{
	if ( m_nDrawables > 0 && ( inferno_fire.GetInt() & 2 ) )
	{
		if ( !m_hFireEffect )
		{
			m_hFireEffect = ParticleProp()->Create( GetParticleEffectName(), PATTACH_ABSORIGIN_FOLLOW );
			return;
		}
		for ( int i = 0; i < m_nDrawables && i < MAX_PARTICLE_FIRES; ++i )
		{
			m_hFireEffect->SetControlPointEntity( i, NULL );
			m_hFireEffect->SetControlPoint( i, m_drawable[i].m_pos );
			if ( ( i % 2 ) == 0 )
			{
				dlight_t *pLight = effects->CL_AllocElight( m_drawable[i].m_nLight );
				pLight->origin = m_drawable[i].m_pos + Vector( 0, 0, 64 );
				pLight->color.r = 254;
				pLight->color.g = 100;
				pLight->color.b = 10;
				pLight->radius = RandomFloat( 30.0f, 60.0f );
				pLight->die = gpGlobals->curtime + RandomFloat( 0.01f, 0.025f );
				pLight->color.exponent = 5;
			}
		}
		// This is intentionally an absolute 0.1, not curtime+0.1. The original
		// reschedules in the past and keeps running on every rendered client frame.
		SetNextClientThink( 0.1f );
		m_hFireEffect->SetSortOrigin( GetAbsOrigin() );
		m_hFireEffect->SetNeedsBBoxUpdate( true );
	}
	else if ( m_hFireEffect )
	{
		m_hFireEffect->StopEmission( false, false, false );
	}
}

void C_Inferno::DrawFire( Drawable &fire, IMaterial *pMaterial )
{
	bool bFoundFrame, bFoundTexture;
	IMaterialVar *pFrame = pMaterial->FindVar( "$frame", &bFoundFrame, false );
	IMaterialVar *pTexture = pMaterial->FindVar( "$basetexture", &bFoundTexture, false );
	if ( bFoundFrame && bFoundTexture )
	{
		ITexture *pTextureValue = pTexture->GetTextureValue();
		const int nFrames = pTextureValue ? pTextureValue->GetNumAnimationFrames() : 0;
		if ( nFrames != 0 )
			pFrame->SetIntValue( static_cast< int >( gpGlobals->realtime / fire.m_flFrameInterval ) % nFrames );
	}
	CMatRenderContextPtr context( materials );
	context->Bind( pMaterial, this );
	IMesh *pMesh = context->GetDynamicMesh();
	CMeshBuilder mesh;
	mesh.Begin( pMesh, MATERIAL_QUADS, 1 );
	Vector right = CurrentViewRight() * ( fire.m_flHeight * 0.33333334f );
	if ( fire.m_bFlip )
		right = -right;
	Vector top = fire.m_pos + Vector( 0, 0, fire.m_flHeight );
	Vector vertices[4] = { top + right, fire.m_pos + right, fire.m_pos - right, top - right };
	const float u[4] = { 1, 1, 0, 0 };
	const float v[4] = { 0, 1, 1, 0 };
	for ( int i = 0; i < 4; ++i )
	{
		mesh.Position3fv( vertices[i].Base() );
		mesh.Color4ub( 150, 150, 150, 255 );
		mesh.TexCoord2f( 0, u[i], v[i] );
		mesh.AdvanceVertex();
	}
	mesh.End();
	pMesh->Draw();

	if ( fire.m_nLight > 0 && inferno_dlights.GetFloat() >= 1.0f )
	{
		static float flLastRealTime;
		static float flSmoothedFPS = -1.0f;
		float flFrameTime = gpGlobals->realtime - flLastRealTime;
		if ( flFrameTime <= 2.0f && flFrameTime > 0.0f )
		{
			float flFPS = 1.0f / flFrameTime;
			flSmoothedFPS = flSmoothedFPS >= 0.0f ? flFPS * 0.1f + flSmoothedFPS * 0.9f : flFPS;
			if ( inferno_dlights.GetFloat() > static_cast< int >( flSmoothedFPS ) )
			{
				fire.m_nLight = 0;
				flLastRealTime = gpGlobals->realtime;
				return;
			}
		}
		float flScale = ( fire.m_flHeight / fire.m_flMaxHeight ) * 1.5f;
		flLastRealTime = gpGlobals->realtime;
		dlight_t *pLight = effects->CL_AllocElight( fire.m_nLight );
		pLight->origin = fire.m_pos + Vector( 0, 0, flScale * 16.0f );
		pLight->color.r = 254;
		pLight->color.g = 100;
		pLight->color.b = 10;
		pLight->radius = RandomFloat( 50.0f, 131.0f ) * flScale;
		pLight->die = gpGlobals->curtime + 0.1f;
		pLight->color.exponent = 5;
	}
}

int C_Inferno::DrawModel( int flags )
{
	if ( inferno_fire.GetInt() & 1 )
	{
		static CMaterialReference material( "sprites/fire1", TEXTURE_GROUP_CLIENT_EFFECTS );
		if ( material->IsErrorMaterial() )
			return 0;
		for ( int i = 0; i < m_nDrawables; ++i )
			DrawFire( m_drawable[i], material );
	}
	return 0;
}
