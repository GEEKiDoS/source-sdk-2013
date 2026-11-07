//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_vomitjar_projectile.h"
#include "c_te_effect_dispatch.h"
#include "particles_ez.h"
#include "fx_sparks.h"
#include "fx_blood.h"
#include "particle_util.h"
#include "l4d2/player/c_terrorplayer.h"
#include "particle_property.h"
#include "clientleafsystem.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "datacache/imdlcache.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_VomitJarProjectile, DT_VomitJarProjectile, CVomitJarProjectile )
END_RECV_TABLE()

// Native client registration is observable even though this module never reads it.
static ConVar z_vomit_fade_start( "z_vomit_fade_start", "5", 0x00006008,
	"When the vomit starts to fade away" );

void C_VomitJarProjectile::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	if ( type == DATA_UPDATE_CREATED )
	{
		// C313160/C313130: the canonical game-side leaf flag, not ShouldDraw.
		SetNextClientThink( gpGlobals->curtime + 0.15f );
		ClientLeafSystem_EnableRendering( GetRenderHandle(), false );
	}
}

void C_VomitJarProjectile::ClientThink()
{
	// C313130 restores rendering; there is no repeated client think.
	ClientLeafSystem_EnableRendering( GetRenderHandle(), true );
}


// The jar reaches the original shared csblood effect (C22FBE0 -> C22F380),
// not the target bloodspray/bloodimpact callbacks with different constants.
static ConVar z_max_blood_scale( "z_max_blood_scale", "3", 0x00004008,
	"The biggest starting scale a blood puff can be." );

static void TerrorCSBlood( const CEffectData &data )
{
	if ( UTIL_IsLowViolence() )
		return;
	static ConVarRef violence_hblood( "violence_hblood" );
	if ( violence_hblood.IsValid() && !violence_hblood.GetBool() )
		return;

	const Vector &origin = data.m_vOrigin;
	const Vector &normal = data.m_vNormal;
	const float maxScale = z_max_blood_scale.GetFloat() - 0.5f;
	const float size = data.m_flMagnitude * 0.02f;
	const float scale = ( size <= maxScale ? ( size >= 0.0f ? size : 0.0f ) : maxScale ) + 0.5f;
	const Vector light = WorldGetLightForPoint( origin, true );
	const Vector color( light.x * 64.0f * 0.0039215689f, light.y * 0.0f * 0.0039215689f,
		light.z * 4.0f * 0.0039215689f );

	CSmartPtr< CBloodSprayEmitter > pSpray = CBloodSprayEmitter::Create( "bloodgore" );
	if ( !pSpray )
		return;
	pSpray->SetSortOrigin( origin );
	pSpray->SetGravity( 0.0f );
	PMaterialHandle hCore = ParticleMgr()->GetPMaterial( "effects/blood_core" );

	// C18BE40 uses RandomInt(0,32767), not RandomFloat. Preserve its three
	// draws and component-wise normal multiplication even for a zero normal.
	Vector direction;
	direction.x = normal.x * ( RandomInt( 0, 32767 ) * 0.000030518509f - 0.5f );
	direction.y = normal.y * ( RandomInt( 0, 32767 ) * 0.000030518509f - 0.5f );
	direction.z = normal.z * ( RandomInt( 0, 32767 ) * 0.000030518509f - 0.5f );
	const Vector offset = origin + normal * 2.0f;

	SimpleParticle *pParticle = static_cast< SimpleParticle * >(
		pSpray->AddParticle( sizeof( SimpleParticle ), hCore, offset ) );
	if ( pParticle )
	{
		pParticle->m_flLifetime = 0.0f;
		pParticle->m_flDieTime = 0.75f;
		pParticle->m_vecVelocity = direction * random->RandomFloat( 16.0f, 32.0f );
		pParticle->m_vecVelocity.z -= random->RandomFloat( 8.0f, 16.0f );
		const float ramp = random->RandomFloat( 0.75f, 2.0f );
		for ( int i = 0; i < 3; ++i )
			pParticle->m_uchColor[i] = MIN( 1.0f, color[i] * ramp ) * 255.0f;
		pParticle->m_uchStartAlpha = 255;
		pParticle->m_uchEndAlpha = 0;
		pParticle->m_uchStartSize = 8;
		pParticle->m_uchEndSize = 32;
		pParticle->m_flRoll = random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta = 0.0f;
		pParticle->m_iFlags = 0;
	}

	PMaterialHandle hGore = ParticleMgr()->GetPMaterial( "effects/blood_gore" );
	for ( int i = 0; i < 4; ++i )
	{
		pParticle = static_cast< SimpleParticle * >(
			pSpray->AddParticle( sizeof( SimpleParticle ), hGore, offset ) );
		if ( !pParticle )
			continue;
		pParticle->m_flLifetime = 0.0f;
		pParticle->m_flDieTime = random->RandomFloat( 0.75f, 1.0f );
		pParticle->m_vecVelocity = direction * random->RandomFloat( 16.0f, 32.0f ) * ( i + 1 );
		pParticle->m_vecVelocity.z -= random->RandomFloat( 16.0f, 32.0f ) * ( i + 1 );
		const float ramp = random->RandomFloat( 0.75f, 2.0f );
		for ( int j = 0; j < 3; ++j )
			pParticle->m_uchColor[j] = MIN( 1.0f, color[j] * ramp ) * 255.0f;
		const int startSize = random->RandomInt( 4, 8 ) * scale;
		pParticle->m_uchStartSize = startSize;
		pParticle->m_uchEndSize = 4 * startSize;
		pParticle->m_uchStartAlpha = 255;
		pParticle->m_uchEndAlpha = 0;
		pParticle->m_flRoll = random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta = 0.0f;
		pParticle->m_iFlags = 0;
	}

	CSmartPtr< CTrailParticles > pTrail = CTrailParticles::Create( "blooddrops" );
	if ( !pTrail )
		return;
	pTrail->SetSortOrigin( origin );
	pTrail->SetGravity( 400.0f );
	pTrail->Setup( origin, &normal, 1, 10, 100, 400, 0.2f, 0 );
	PMaterialHandle hDrop = ParticleMgr()->GetPMaterial( "effects/blood_drop" );
	for ( int i = 0; i < 32; ++i )
	{
		TrailParticle *pDrop = static_cast< TrailParticle * >(
			pTrail->AddParticle( sizeof( TrailParticle ), hDrop, origin ) );
		if ( !pDrop )
			break;
		pDrop->m_flLifetime = 0.0f;
		direction.x = RandomInt( 0, 32767 ) * 0.000030518509f * 2.0f - 1.0f;
		direction.y = RandomInt( 0, 32767 ) * 0.000030518509f * 2.0f - 1.0f;
		direction.z = RandomInt( 0, 32767 ) * 0.000030518509f * 2.0f - 1.0f;
		pDrop->m_vecVelocity = direction * random->RandomFloat( 32.0f, 128.0f );
		pDrop->m_flWidth = random->RandomFloat( 1.0f, 3.0f ) * scale;
		pDrop->m_flLength = random->RandomFloat( 0.1f, 0.15f );
		pDrop->m_flDieTime = random->RandomFloat( 0.5f, 1.0f );
		FloatToColor32( pDrop->m_color, color.x, color.y, color.z, 1.0f );
	}
}

DECLARE_CLIENT_EFFECT( "csblood", TerrorCSBlood );

static C_TerrorPlayer *TerrorVomitViewPlayer()
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer && pPlayer->GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_TerrorPlayer *pTarget = ToTerrorPlayer( pPlayer->GetObserverTarget() );
		if ( pTarget )
			return pTarget;
	}
	return ToTerrorPlayer( pPlayer );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void C_TerrorPlayer::UpdateVomitParticleEffects()
{
	// C25D6B0, called by the original player's particle update (C261360).
	// A jar's five-second survivor IT timer differs from the screen PCF's
	// lifetime. m_vomitFadeStart is received state, not this consumer's clock.
	const bool bBodyActive = m_lifeState == LIFE_ALIVE && !( gpGlobals->curtime >= m_itTimer.m_timestamp ) &&
		static_cast< double >( gpGlobals->curtime ) - static_cast< double >( m_itTimer.m_timestamp ) +
			static_cast< double >( m_itTimer.m_duration ) < 17.0;
	if ( !bBodyActive )
	{
		if ( m_hITBodyEffect )
		{
			m_hITBodyEffect->StopEmission();
			m_hITBodyEffect = NULL;
		}
	}
	else if ( m_hITBodyEffect )
	{
		m_hITBodyEffect->SetNeedsBBoxUpdate( true );
		m_hITBodyEffect->SetSortOrigin( GetAbsOrigin() );
	}
	else
	{
		MDLCACHE_CRITICAL_SECTION();
		const int team = GetRenderTeamNumber();
		const bool bSurvivor = team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR;
		m_hITBodyEffect = ParticleProp()->Create(
			bSurvivor ? "boomer_vomit_survivor" : "boomer_vomit_infected", PATTACH_ABSORIGIN_FOLLOW );
		if ( m_hITBodyEffect )
		{
			ParticleProp()->AddControlPoint( m_hITBodyEffect, 1, this, PATTACH_ABSORIGIN_FOLLOW );
			m_hITBodyEffect->SetControlPoint( 0, GetAbsOrigin() );
			m_hITBodyEffect->SetControlPoint( 1, GetAbsOrigin() );
			m_hITBodyEffect->SetControlPointEntity( 0, this );
			m_hITBodyEffect->SetControlPointEntity( 1, this );
		}
	}

	if ( !( m_vomitStart > m_flPreviousVomitStart ) )
	{
		if ( ( !bBodyActive || GetObserverTarget() ) && m_hVomitScreenEffect )
		{
			m_hVomitScreenEffect->StopEmission();
			m_hVomitScreenEffect = NULL;
		}
	}
	else if ( TerrorVomitViewPlayer() == this && !m_hVomitScreenEffect )
	{
		MDLCACHE_CRITICAL_SECTION();
		m_hVomitScreenEffect = ParticleProp()->Create( "boomer_vomit_screeneffect", PATTACH_CUSTOMORIGIN );
		if ( m_hVomitScreenEffect )
		{
			ParticleProp()->AddControlPoint( m_hVomitScreenEffect, 1, this, PATTACH_CUSTOMORIGIN );
			m_hVomitScreenEffect->SetControlPoint( 0, GetAbsOrigin() );
			m_hVomitScreenEffect->SetControlPoint( 1, GetAbsOrigin() );
			m_hVomitScreenEffect->SetControlPointEntity( 0, this );
			m_hVomitScreenEffect->SetControlPointEntity( 1, this );
			// Original disables the definition's "draw through leafsystem".
			// Native target lacks that definition flag. Its unclassified
			// instance group keeps this effect out of world render lists.
			ClientLeafSystem()->SetRenderGroup( m_hVomitScreenEffect->RenderHandle(), RENDER_GROUP_OTHER );
		}
	}
	m_flPreviousVomitStart = m_vomitStart;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

bool C_TerrorPlayer::HasVomitScreenSpaceParticles() const
{
	return m_hVomitScreenEffect && m_hVomitScreenEffect->IsValid();
}

void C_TerrorPlayer::RenderVomitScreenSpaceParticles( IMatRenderContext *pRenderContext )
{
	if ( !HasVomitScreenSpaceParticles() )
		return;
	// Original direct DrawModel bypasses the cache when draw-through-leaf is
	// false. Target DrawModel queues opaque effects, so call native Render.
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();
	m_hVomitScreenEffect->Render( pRenderContext, false, this );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
}

bool Terror_HasVomitScreenSpaceParticles()
{
	C_TerrorPlayer *pPlayer = TerrorVomitViewPlayer();
	return pPlayer && pPlayer->HasVomitScreenSpaceParticles();
}

void Terror_RenderVomitScreenSpaceParticles( IMatRenderContext *pRenderContext )
{
	C_TerrorPlayer *pPlayer = TerrorVomitViewPlayer();
	if ( !pPlayer || !pPlayer->HasVomitScreenSpaceParticles() )
		return;
	// C1C7530: this is rendered into the bloom source before EnginePost,
	// not drawn over the screen by an unrelated ScreenFade or material.
	ITexture *pSmallFB = materials->FindTexture( "_rt_SmallFB0", TEXTURE_GROUP_RENDER_TARGET );
	pRenderContext->PushRenderTargetAndViewport();
	pRenderContext->SetRenderTarget( pSmallFB );
	pRenderContext->Viewport( 0, 0, pSmallFB->GetActualWidth(), pSmallFB->GetActualHeight() );
	pPlayer->RenderVomitScreenSpaceParticles( pRenderContext );
	pRenderContext->PopRenderTargetAndViewport();
}

void Terror_BindVomitScreenSpaceMaterial( IMaterial *pMaterial )
{
	// C1C47B0 EnginePost proxy uses this exact boolean. Missing native shader
	// support remains an explicit integration prerequisite, not a fade hack.
	bool bFound = false;
	IMaterialVar *pEnable = pMaterial->FindVar( "$vomitEnable", &bFound, false );
	if ( bFound )
		pEnable->SetIntValue( Terror_HasVomitScreenSpaceParticles() ? 1 : 0 );
}
