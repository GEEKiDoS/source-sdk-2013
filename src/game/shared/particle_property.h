//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================

#ifndef PARTICLEPROPERTY_H
#define PARTICLEPROPERTY_H
#ifdef _WIN32
#pragma once
#endif

#include "smartptr.h"
#include "globalvars_base.h"
#include "particles_new.h"
#include "particle_parse.h"

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CBaseEntity;
class CNewParticleEffect;
#if defined( L4D2 ) && defined( CLIENT_DLL )
class CParticleSystemDefinition;
#endif

// Argh: Server considers -1 to be an invalid attachment, whereas the client uses 0
#ifdef CLIENT_DLL
#define INVALID_PARTICLE_ATTACHMENT			0
#else
#define INVALID_PARTICLE_ATTACHMENT			-1
#endif

struct ParticleControlPoint_t
{
	ParticleControlPoint_t()
	{
		iControlPoint = 0;
		iAttachType = PATTACH_ABSORIGIN_FOLLOW;
		iAttachmentPoint = 0;
		vecOriginOffset = vec3_origin;
	}

	int								iControlPoint;
	ParticleAttachment_t			iAttachType;
	int								iAttachmentPoint;
	Vector							vecOriginOffset;
	EHANDLE							hEntity;
#if defined( L4D2 ) && defined( CLIENT_DLL )
	matrix3x4_t m_LocalMatrix;
#endif
};

struct ParticleEffectList_t
{
	ParticleEffectList_t()
	{
		pParticleEffect = NULL;
	}

	CUtlVector<ParticleControlPoint_t>	pControlPoints;
	CSmartPtr<CNewParticleEffect>		pParticleEffect;
};

extern int GetAttachTypeFromString( const char *pszString );

//-----------------------------------------------------------------------------
// Encapsulates particle handling for an entity
//-----------------------------------------------------------------------------
class CParticleProperty 
{
	DECLARE_CLASS_NOBASE( CParticleProperty );
	DECLARE_EMBEDDED_NETWORKVAR();
	DECLARE_PREDICTABLE();
	DECLARE_DATADESC();

public:
	CParticleProperty();
	~CParticleProperty();

	void				Init( CBaseEntity *pEntity );
	CBaseEntity			*GetOuter( void ) { return m_pOuter; }

	// Effect Creation
	CNewParticleEffect *Create( const char *pszParticleName, ParticleAttachment_t iAttachType, const char *pszAttachmentName );
#if defined( L4D2 ) && defined( CLIENT_DLL )
	CNewParticleEffect *Create( const char *name, ParticleAttachment_t attachType, int attachment = INVALID_PARTICLE_ATTACHMENT,
		Vector originOffset = vec3_origin, const matrix3x4_t *localMatrix = NULL );
	CNewParticleEffect *Create( CParticleSystemDefinition *definition, ParticleAttachment_t attachType,
		int attachment = INVALID_PARTICLE_ATTACHMENT, Vector originOffset = vec3_origin, const matrix3x4_t *localMatrix = NULL );
#else
	CNewParticleEffect *Create( const char *pszParticleName, ParticleAttachment_t iAttachType, int iAttachmentPoint = INVALID_PARTICLE_ATTACHMENT, Vector vecOriginOffset = vec3_origin );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
	void AddControlPoint( CNewParticleEffect *effect, int controlPoint, C_BaseEntity *entity, ParticleAttachment_t attachType,
		const char *attachmentName = NULL, Vector originOffset = vec3_origin, const matrix3x4_t *localMatrix = NULL );
#else
	void AddControlPoint( CNewParticleEffect *pEffect, int iPoint, C_BaseEntity *pEntity, ParticleAttachment_t iAttachType,
		const char *pszAttachmentName = NULL, Vector vecOriginOffset = vec3_origin );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
	void AddControlPoint( int effectIndex, int controlPoint, C_BaseEntity *entity, ParticleAttachment_t attachType,
		int attachment = INVALID_PARTICLE_ATTACHMENT, Vector originOffset = vec3_origin, const matrix3x4_t *localMatrix = NULL );
#else
	void AddControlPoint( int iEffectIndex, int iPoint, C_BaseEntity *pEntity, ParticleAttachment_t iAttachType, int iAttachmentPoint = INVALID_PARTICLE_ATTACHMENT, Vector vecOriginOffset = vec3_origin );
#endif

	inline void			SetControlPointParent( CNewParticleEffect *pEffect, int whichControlPoint, int parentIdx );
	void				SetControlPointParent( int iEffectIndex, int whichControlPoint, int parentIdx );

	// Commands
	void				StopEmission( CNewParticleEffect *pEffect = NULL, bool bWakeOnStop = false, bool bDestroyAsleepSystems = false );
	void				StopEmissionAndDestroyImmediately( CNewParticleEffect *pEffect = NULL );

	// kill all particle systems involving a given entity for their control points
	void				StopParticlesInvolving( CBaseEntity *pEntity );
#if defined( L4D2 ) && defined( CLIENT_DLL )
	void StopParticlesNamed( const char *name, bool forceRemoveInstantly = false, int onlyDrawForLocalUser = -1 );
#else
	void StopParticlesNamed( const char *pszEffectName, bool bForceRemoveInstantly = false, bool bInverse = false );
#endif
	void				StopParticlesWithNameAndAttachment( const char *pszEffectName, int iAttachmentPoint, bool bForceRemoveInstantly = false ); ///< kills all particles using the given definition name

	// Particle System hooks
	void				OnParticleSystemUpdated( CNewParticleEffect *pEffect, float flTimeDelta );
	void				OnParticleSystemDeleted( CNewParticleEffect *pEffect );
#if defined( CLIENT_DLL ) && defined( L4D2 )
	bool IsValidEffect( CNewParticleEffect *effect ); // Native151A70, nonconst.
#endif

#ifdef CLIENT_DLL
	void				OwnerSetDormantTo( bool bDormant );
#endif

	// Used to replace a particle effect with a different one; attaches the control point updating to the new one
	void				ReplaceParticleEffect( CNewParticleEffect *pOldEffect, CNewParticleEffect *pNewEffect );

	// Debugging
	void				DebugPrintEffects( void );

	int					FindEffect( const char *pEffectName, int nStart = 0 );
	inline CNewParticleEffect *GetParticleEffectFromIdx( int idx );

private:
	int					GetParticleAttachment( C_BaseEntity *pEntity, const char *pszAttachmentName, const char *pszParticleName );
	int					FindEffect( CNewParticleEffect *pEffect );
	void				UpdateParticleEffect( ParticleEffectList_t *pEffect, bool bInitializing = false, int iOnlyThisControlPoint = -1 );
	void				UpdateControlPoint( ParticleEffectList_t *pEffect, int iPoint, bool bInitializing );

private:
	CBaseEntity *m_pOuter;
	CUtlVector<ParticleEffectList_t>	m_ParticleEffects;
	int			m_iDormancyChangedAtFrame;

	friend class CBaseEntity;
};

#include "particle_property_inlines.h"

#endif // PARTICLEPROPERTY_H
