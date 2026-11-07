//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================

#include "cbase.h"
#include "particles/particles.h"
#include "c_te_effect_dispatch.h"
#include "particles_new.h"
#include "networkstringtable_clientdll.h"
#include "tier0/vprof.h"
#if defined( L4D2 )
#include "particle_parse.h"
#include "particle_property.h"
#include "physics.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: An entity that spawns and controls a particle system
//-----------------------------------------------------------------------------
class C_ParticleSystem : public C_BaseEntity
{
	DECLARE_CLASS( C_ParticleSystem, C_BaseEntity );
public:
	DECLARE_CLIENTCLASS();

	C_ParticleSystem();

	void PreDataUpdate( DataUpdateType_t updateType );
	void PostDataUpdate( DataUpdateType_t updateType );
	void ClientThink( void );

protected:
	int			m_iEffectIndex;
	bool		m_bActive;
	bool		m_bOldActive;
	float		m_flStartTime;	// Time at which the effect started
#if defined( L4D2 )
	Vector m_vServerControlPoints[4];
	unsigned char m_iServerControlPointAssignments[4];
	HPARTICLEFFECT m_hEffect;
#endif

	enum { kMAXCONTROLPOINTS = 63 }; ///< actually one less than the total number of cpoints since 0 is assumed to be me

	
	EHANDLE		m_hControlPointEnts[kMAXCONTROLPOINTS];
	//	SendPropArray3( SENDINFO_ARRAY3(m_iControlPointParents), SendPropInt( SENDINFO_ARRAY(m_iControlPointParents), 3, SPROP_UNSIGNED ) ),
	unsigned char m_iControlPointParents[kMAXCONTROLPOINTS];

#if !defined( L4D2 )
	bool		m_bWeatherEffect;
#endif
};

IMPLEMENT_CLIENTCLASS(C_ParticleSystem, DT_ParticleSystem, CParticleSystem);

BEGIN_RECV_TABLE_NOBASE( C_ParticleSystem, DT_ParticleSystem )
	RecvPropVector( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ) ),
	RecvPropEHandle( RECVINFO(m_hOwnerEntity) ),
	RecvPropInt( RECVINFO_NAME(m_hNetworkMoveParent, moveparent), 0, RecvProxy_IntToMoveParent ),
	RecvPropInt( RECVINFO( m_iParentAttachment ) ),
	RecvPropQAngles( RECVINFO_NAME( m_angNetworkAngles, m_angRotation ) ),

	RecvPropInt( RECVINFO( m_iEffectIndex ) ),
	RecvPropBool( RECVINFO( m_bActive ) ),
	RecvPropFloat( RECVINFO( m_flStartTime ) ),
#if defined( L4D2 )
	RecvPropArray3( RECVINFO_ARRAY(m_vServerControlPoints), RecvPropVector( RECVINFO(m_vServerControlPoints[0]) ) ),
	RecvPropArray3( RECVINFO_ARRAY(m_iServerControlPointAssignments), RecvPropInt( RECVINFO(m_iServerControlPointAssignments[0]) ) ),
#endif

	RecvPropArray3( RECVINFO_ARRAY(m_hControlPointEnts), RecvPropEHandle( RECVINFO( m_hControlPointEnts[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY(m_iControlPointParents), RecvPropInt( RECVINFO(m_iControlPointParents[0]))), 
#if !defined( L4D2 )
	RecvPropBool( RECVINFO( m_bWeatherEffect ) ),
#endif
END_RECV_TABLE();

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_ParticleSystem::C_ParticleSystem()
{
#if !defined( L4D2 )
	m_bWeatherEffect = false;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_ParticleSystem::PreDataUpdate( DataUpdateType_t updateType )
{
	m_bOldActive = m_bActive;

	BaseClass::PreDataUpdate( updateType );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_ParticleSystem::PostDataUpdate( DataUpdateType_t updateType )
{
	BaseClass::PostDataUpdate( updateType );

#if defined( L4D2 )
	if ( updateType == DATA_UPDATE_CREATED )
	{
		if ( m_bActive )
			SetNextClientThink( gpGlobals->curtime );
	}
	else
	{
		if ( m_bOldActive != m_bActive )
		{
			if ( m_bActive )
				SetNextClientThink( gpGlobals->curtime );
			else
				ParticleProp()->StopEmission( NULL, false, false );
		}
		if ( m_bActive && ParticleProp()->IsValidEffect( m_hEffect() ) )
		{
			for ( int i = 0; i != 4; ++i )
			{
				const unsigned char point = m_iServerControlPointAssignments[i];
				if ( point == 0xFF )
					break;
				m_hEffect->SetControlPoint( point, m_vServerControlPoints[i] );
			}
		}
	}
#else
	// Always restart if just created and updated
	// FIXME: Does this play fairly with PVS?
	if ( updateType == DATA_UPDATE_CREATED )
	{
		if ( m_bActive )
		{
			// Delayed here so that we don't get invalid abs queries on level init with active particle systems
			SetNextClientThink( gpGlobals->curtime );
		}
	}
	else
	{
		if ( m_bOldActive != m_bActive )
		{
			if ( m_bActive )
			{
				// Delayed here so that we don't get invalid abs queries on level init with active particle systems
				SetNextClientThink( gpGlobals->curtime );
			}
			else
			{
						ParticleProp()->StopEmission();
					}
		}
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_ParticleSystem::ClientThink( void )
{
#if defined( L4D2 )
	if ( !m_bActive )
		return;
	const char *name = GetParticleSystemNameFromIndex( m_iEffectIndex );
	if ( !name || !name[0] )
		return;
	CNewParticleEffect *effect = ParticleProp()->Create( name, PATTACH_ABSORIGIN_FOLLOW );
	m_hEffect = effect;
	if ( effect )
	{
		for ( int i = 0; i != kMAXCONTROLPOINTS; ++i )
		{
			C_BaseEntity *entity = m_hControlPointEnts[i].Get();
			if ( entity )
				ParticleProp()->AddControlPoint( effect, i + 1, entity, PATTACH_ABSORIGIN_FOLLOW );
			if ( m_iControlPointParents[i] != 0 )
				effect->SetControlPointParent( i + 1, m_iControlPointParents[i] );
		}
		for ( int i = 0; i != 4; ++i )
		{
			const unsigned char point = m_iServerControlPointAssignments[i];
			if ( point == 0xFF )
				break;
			effect->SetControlPoint( point, m_vServerControlPoints[i] );
		}
		ParticleProp()->OnParticleSystemUpdated( effect, 0.0f );
		const float delta = gpGlobals->curtime - m_flStartTime;
		if ( delta > 0.01f )
		{
			// Native85F10 enters/exits profiling but does NOT call SkipToTime.
			VPROF_BUDGET( "C_ParticleSystem::ClientThink SkipToTime", "Particle Simulation" );
		}
	}
#else
	if ( m_bActive )
	{
		const char *pszName = GetParticleSystemNameFromIndex( m_iEffectIndex );
		if ( pszName && pszName[0] )
		{
			if ( !GameRules()->AllowMapParticleEffect( pszName ) )
				return;

			if ( m_bWeatherEffect && !GameRules()->AllowWeatherParticles() )
				return;

			CNewParticleEffect *pEffect = ParticleProp()->Create( pszName, PATTACH_ABSORIGIN_FOLLOW );
			AssertMsg1( pEffect, "Particle system couldn't make %s", pszName );
			if (pEffect)
			{
				for ( int i = 0 ; i < kMAXCONTROLPOINTS ; ++i )
				{
					CBaseEntity *pOnEntity = m_hControlPointEnts[i].Get();
					if ( pOnEntity )
					{
						ParticleProp()->AddControlPoint( pEffect, i + 1, pOnEntity, PATTACH_ABSORIGIN_FOLLOW );
					}

					AssertMsg2( m_iControlPointParents[i] >= 0 && m_iControlPointParents[i] <= kMAXCONTROLPOINTS ,
						"Particle system specified bogus control point parent (%d) for point %d.",
						m_iControlPointParents[i], i );

					if (m_iControlPointParents[i] != 0)
					{
						pEffect->SetControlPointParent(i+1, m_iControlPointParents[i]);
					}
				}

				// NOTE: What we really want here is to compare our lifetime and that of our children and see if this delta is
				//		 already past the end of it, denoting that we're finished.  In that case, just destroy us and be done. -- jdw

				// TODO: This can go when the SkipToTime code below goes
				ParticleProp()->OnParticleSystemUpdated( pEffect, 0.0f );

				// Skip the effect ahead if we're restarting it
				float flTimeDelta = gpGlobals->curtime - m_flStartTime;
				if ( flTimeDelta > 0.01f )
				{
					VPROF_BUDGET( "C_ParticleSystem::ClientThink SkipToTime", "Particle Simulation" );
					pEffect->SkipToTime( flTimeDelta );
				}
			}
		}
	}
#endif
}


//======================================================================================================================
// PARTICLE SYSTEM DISPATCH EFFECT
//======================================================================================================================
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#if defined( L4D2 )
void ParticleEffectCallback( const CEffectData &data );
#else
void ParticleEffectCallback( const CEffectData &data )
{
	if ( SuppressingParticleEffects() )
		return; // this needs to be before using data.m_nHitBox, since that may be a serialized value that's past the end of the current particle system string table

	const char *pszName = GetParticleSystemNameFromIndex( data.m_nHitBox );

	CSmartPtr<CNewParticleEffect> pEffect = NULL;
	if ( data.m_fFlags & PARTICLE_DISPATCH_FROM_ENTITY )
	{
		if ( data.m_hEntity.Get() )
		{
			C_BaseEntity *pEnt = C_BaseEntity::Instance( data.m_hEntity );
			if ( pEnt && !pEnt->IsDormant() )
			{
				if ( data.m_fFlags & PARTICLE_DISPATCH_RESET_PARTICLES )
				{
					pEnt->ParticleProp()->StopEmission();
				}

				Vector vOffset = vec3_origin;
				ParticleAttachment_t iAttachType = (ParticleAttachment_t)data.m_nDamageType;
				if ( iAttachType == PATTACH_ABSORIGIN_FOLLOW || iAttachType == PATTACH_POINT_FOLLOW || iAttachType == PATTACH_ROOTBONE_FOLLOW )
				{
					vOffset = data.m_vStart;
				}

				pEffect = pEnt->ParticleProp()->Create( pszName, iAttachType, data.m_nAttachmentIndex, vOffset );
				AssertMsg2( pEffect.IsValid() && pEffect->IsValid(), "%s could not create particle effect %s",
					C_BaseEntity::Instance( data.m_hEntity )->GetDebugName(), pszName );
				if ( pEffect.IsValid() && pEffect->IsValid() )
				{
					if ( iAttachType == PATTACH_CUSTOMORIGIN )
					{
						pEffect->SetSortOrigin( data.m_vOrigin );
						pEffect->SetControlPoint( 0, data.m_vOrigin );
						pEffect->SetControlPoint( 1, data.m_vStart );
						Vector vecForward, vecRight, vecUp;
						AngleVectors( data.m_vAngles, &vecForward, &vecRight, &vecUp );
						pEffect->SetControlPointOrientation( 0, vecForward, vecRight, vecUp );
					}
				}
			}
		}
	}	
	else
	{
		if ( GameRules() )
		{
			pszName = GameRules()->TranslateEffectForVisionFilter( "particles", pszName );
		}

		pEffect = CNewParticleEffect::Create( NULL, pszName );
		if ( pEffect->IsValid() )
		{
			pEffect->SetSortOrigin( data.m_vOrigin );
			pEffect->SetControlPoint( 0, data.m_vOrigin );
			pEffect->SetControlPoint( 1, data.m_vStart );
			Vector vecForward, vecRight, vecUp;
			AngleVectors( data.m_vAngles, &vecForward, &vecRight, &vecUp );
			pEffect->SetControlPointOrientation( 0, vecForward, vecRight, vecUp );
		}
	}

	if ( pEffect.IsValid() && pEffect->IsValid() )
	{
		if ( data.m_bCustomColors )
		{
			pEffect->SetControlPoint( CUSTOM_COLOR_CP1, data.m_CustomColors.m_vecColor1 );
			pEffect->SetControlPoint( CUSTOM_COLOR_CP2, data.m_CustomColors.m_vecColor2 );
		}

		if ( data.m_bControlPoint1 )
		{
			pEffect->SetControlPoint( 1, data.m_ControlPoint1.m_vecOffset );
		}
	}
}
#endif

DECLARE_CLIENT_EFFECT( "ParticleEffect", ParticleEffectCallback );


//======================================================================================================================
// PARTICLE SYSTEM STOP EFFECT
//======================================================================================================================
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ParticleEffectStopCallback( const CEffectData &data )
{
	if ( data.m_hEntity.Get() )
	{
		C_BaseEntity *pEnt = C_BaseEntity::Instance( data.m_hEntity );
		if ( pEnt )
		{
				pEnt->ParticleProp()->StopEmission();
			}
		}
	}

DECLARE_CLIENT_EFFECT( "ParticleEffectStop", ParticleEffectStopCallback );

//========= Copyright Valve Corporation, All rights reserved. ============//
// Compose into EXISTING client/c_particle_system.cpp under L4D2. Include
// particle_parse.h, particle_property.h and physics.h before memdbgon.
// Whole native858A0 plus actual1014F7E0 local index/origin/angle/user dispatch.
// Existing ParticleEffectCallback body is STOCK-ONLY. No CEffectData/engine ABI
// extension: only game-DLL calls carry the explicit extra native user argument.
#if defined( L4D2 )
static void StartTerrorParticleEffect( const CEffectData &data, int onlyDrawForLocalUser )
{
	if ( SuppressingParticleEffects() ) return;
	if ( data.m_nHitBox == 0 ) return;
	HPARTICLEFFECT effect;
	if ( data.m_fFlags & PARTICLE_DISPATCH_FROM_ENTITY )
	{
		if ( !data.m_hEntity.Get() ) return;
		C_BaseEntity *entity = C_BaseEntity::Instance( data.m_hEntity );
		if ( !entity || entity->IsDormant() ) return;
		if ( data.m_fFlags & PARTICLE_DISPATCH_RESET_PARTICLES ) entity->ParticleProp()->StopEmission();
		const char *name = GetParticleSystemNameFromIndex( data.m_nHitBox );
		CParticleSystemDefinition *definition = name ? g_pParticleSystemMgr->FindParticleSystem( name ) : NULL;
		if ( !definition ) return; // Native1521D0 index path silently returns NULL.
		const ParticleAttachment_t attachType = static_cast< ParticleAttachment_t >( data.m_nDamageType );
		if ( attachType == PATTACH_CUSTOMORIGIN_FOLLOW )
		{
			Vector forward, right, up;
			AngleVectors( data.m_vAngles, &forward, &right, &up );
			matrix3x4_t effectToWorld, worldToEntity, effectToEntity;
			// Native85670 columns are EXACT forward/right/up (no invented
			// right-vector negation from AngleMatrix's conventional left axis).
			MatrixSetColumn( forward, 0, effectToWorld );
			MatrixSetColumn( right, 1, effectToWorld );
			MatrixSetColumn( up, 2, effectToWorld );
			MatrixSetColumn( data.m_vOrigin, 3, effectToWorld );
			MatrixInvert( entity->RenderableToWorldTransform(), worldToEntity );
			L4D2ClientConcatTransforms( worldToEntity, effectToWorld, effectToEntity );
			effect = entity->ParticleProp()->Create( definition, attachType, data.m_nAttachmentIndex,
				data.m_vOrigin, &effectToEntity );
			if ( effect && effect->IsValid() )
			{
				effect->SetDrawOnlyForLocalUser( onlyDrawForLocalUser );
				effect->SetSortOrigin( data.m_vOrigin );
				effect->SetControlPoint( 0, data.m_vOrigin );
				effect->SetControlPoint( 1, data.m_vStart );
				effect->SetControlPointOrientation( 0, forward, right, up );
				effect->SetControlPointEntity( 0, entity );
			}
			return;
		}
		if ( attachType == PATTACH_WATERWAKE )
		{
			effect = entity->ParticleProp()->Create( definition, attachType,
				data.m_nAttachmentIndex, data.m_vOrigin, NULL );
			if ( effect && effect->IsValid() ) effect->SetDrawOnlyForLocalUser( onlyDrawForLocalUser );
			return;
		}
		effect = entity->ParticleProp()->Create( definition, attachType,
			data.m_nAttachmentIndex, vec3_origin, NULL );
		if ( effect && effect->IsValid() && attachType == PATTACH_CUSTOMORIGIN )
		{
			effect->SetDrawOnlyForLocalUser( onlyDrawForLocalUser );
			effect->SetSortOrigin( data.m_vOrigin );
			effect->SetControlPoint( 0, data.m_vOrigin );
			effect->SetControlPoint( 1, data.m_vStart );
			Vector forward, right, up;
			AngleVectors( data.m_vAngles, &forward, &right, &up );
			effect->SetControlPointOrientation( 0, forward, right, up );
		}
		return;
	}
	const char *name = GetParticleSystemNameFromIndex( data.m_nHitBox );
	CParticleSystemDefinition *definition = name ? g_pParticleSystemMgr->FindParticleSystem( name ) : NULL;
	if ( !definition )
	{
		Error( "StartParticleEffect:  Failed to find precached particle system for %d!!\n", data.m_nHitBox );
		return;
	}
	effect = CNewParticleEffect::CreateForLocalUser( NULL, definition, data.m_vOrigin, 0, onlyDrawForLocalUser );
	if ( effect && effect->IsValid() )
	{
		effect->SetSortOrigin( data.m_vOrigin );
		effect->SetControlPoint( 0, data.m_vOrigin );
		effect->SetControlPoint( 1, data.m_vStart );
		Vector forward, right, up;
		// Actual native world-only orientation-swap bit4; no invented enum.
		if ( data.m_fFlags & 4 ) AngleVectors( data.m_vAngles, &up, &right, &forward );
		else AngleVectors( data.m_vAngles, &forward, &right, &up );
		effect->SetControlPointOrientation( 0, forward, right, up );
	}
}

void ParticleEffectCallback( const CEffectData &data )
{
	StartTerrorParticleEffect( data, -1 );
}

// Native C14F6E0: the local-user selector is game-owned, not CEffectData wire.
void DispatchParticleEffect( const char *pszParticleName, ParticleAttachment_t iAttachType,
	C_BaseEntity *pEntity, int iAttachmentPoint, bool bResetAllParticlesOnEntity, int onlyDrawForLocalUser )
{
	CEffectData data;
	data.m_nHitBox = GetParticleSystemIndex( pszParticleName );
	if ( pEntity )
	{
		data.m_hEntity = pEntity;
		data.m_fFlags |= PARTICLE_DISPATCH_FROM_ENTITY;
		data.m_vOrigin = pEntity->GetAbsOrigin();
	}
	data.m_nDamageType = iAttachType;
	data.m_nAttachmentIndex = iAttachmentPoint;
	if ( bResetAllParticlesOnEntity )
		data.m_fFlags |= PARTICLE_DISPATCH_RESET_PARTICLES;
	StartTerrorParticleEffect( data, onlyDrawForLocalUser );
}

void DispatchParticleEffect( int index, const Vector &origin, const QAngle &angles,
	ParticleAttachment_t attachType, C_BaseEntity *entity, int onlyDrawForLocalUser )
{
	CEffectData data;
	data.m_nHitBox = index;
	data.m_vOrigin = origin;
	data.m_vAngles = angles;
	data.m_hEntity = entity;
	if ( entity )
	{
		data.m_fFlags |= PARTICLE_DISPATCH_FROM_ENTITY;
		data.m_nDamageType = attachType;
	}
	StartTerrorParticleEffect( data, onlyDrawForLocalUser );
}

// Whole native1014F900. These are the EXISTING canonical position overloads,
// moved out of shared/particle_parse.cpp only for CLIENT L4D2. Native by-value
// vector snapshots precede handle/name callbacks. The boolean sets literal
// bit0x4: alternate WORLD axes, NOT the separate entity-reset bit0x2.
void DispatchParticleEffect( int index, Vector origin, Vector start, QAngle angles,
	C_BaseEntity *entity, bool alternateAxes, int onlyDrawForLocalUser )
{
	CEffectData data;
	data.m_nHitBox = index;
	data.m_vOrigin = origin;
	data.m_vStart = start;
	data.m_vAngles = angles;
	if ( alternateAxes ) data.m_fFlags |= 0x4;
	data.m_hEntity = entity;
	if ( entity )
	{
		data.m_fFlags |= PARTICLE_DISPATCH_FROM_ENTITY;
		data.m_nDamageType = PATTACH_CUSTOMORIGIN;
	}
	StartTerrorParticleEffect( data, onlyDrawForLocalUser );
}

// Whole native1014FA80. No extra CS dispatcher or engine callback/data field.
void DispatchParticleEffect( const char *name, Vector origin, Vector start, QAngle angles,
	C_BaseEntity *entity, bool alternateAxes, int onlyDrawForLocalUser )
{
	const int index = GetParticleSystemIndex( name );
	DispatchParticleEffect( index, origin, start, angles, entity, alternateAxes, onlyDrawForLocalUser );
}
#endif
