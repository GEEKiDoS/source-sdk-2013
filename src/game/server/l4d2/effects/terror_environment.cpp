//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/terror_environment.h"
#include "l4d2/player/terrorplayer.h"
#include "player.h"
#include "fogcontroller.h"
#include "GameEventListener.h"
#include "igamesystem.h"
#include "collisionutils.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( trigger_fog, CFogTrigger );

BEGIN_DATADESC( CFogTrigger )
	DEFINE_KEYFIELD( m_fog.colorPrimary, FIELD_COLOR32, "fogcolor" ),
	DEFINE_KEYFIELD( m_fog.colorSecondary, FIELD_COLOR32, "fogcolor2" ),
	DEFINE_KEYFIELD( m_fog.dirPrimary, FIELD_VECTOR, "fogdir" ),
	DEFINE_KEYFIELD( m_fog.enable, FIELD_BOOLEAN, "fogenable" ),
	DEFINE_KEYFIELD( m_fog.blend, FIELD_BOOLEAN, "fogblend" ),
	DEFINE_KEYFIELD( m_fog.start, FIELD_FLOAT, "fogstart" ),
	DEFINE_KEYFIELD( m_fog.end, FIELD_FLOAT, "fogend" ),
	DEFINE_KEYFIELD( m_fog.farz, FIELD_FLOAT, "farz" ),
END_DATADESC()

void CFogTrigger::Spawn()
{
	AddSpawnFlags( SF_TRIGGER_ALLOW_ALL );
	BaseClass::Spawn();
	InitTrigger();
}

void CFogTrigger::StartTouch( CBaseEntity *other )
{
	if ( PassesTriggerFilters( other ) )
	{
		BaseClass::StartTouch( other );
		CBaseCombatCharacter *character = other->MyCombatCharacterPointer();
		if ( character )
			character->AddFogTrigger( this );
	}
}

void CFogTrigger::EndTouch( CBaseEntity *other )
{
	if ( PassesTriggerFilters( other ) )
	{
		BaseClass::EndTouch( other );
		CBaseCombatCharacter *character = other->MyCombatCharacterPointer();
		if ( character )
			character->RemoveFogTrigger( this );
	}
}

void CBaseCombatCharacter::AddFogTrigger( CBaseEntity *trigger )
{
	m_hTriggerFogList.AddToHead( EHANDLE( trigger ) );
}

void CBaseCombatCharacter::RemoveFogTrigger( CBaseEntity *trigger )
{
	m_hTriggerFogList.FindAndRemove( EHANDLE( trigger ) );
}

CBaseEntity *CBaseCombatCharacter::GetFogTrigger() const
{
	float nearestRange = 999999.0f;
	CBaseEntity *nearest = NULL;
	for ( int i = 0; i < m_hTriggerFogList.Count(); ++i )
	{
		CBaseEntity *trigger = m_hTriggerFogList[i].Get();
		if ( !trigger )
			continue;
		const Vector triggerCenter = trigger->WorldSpaceCenter();
		const Vector delta = WorldSpaceCenter() - triggerCenter;
		const float range = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		if ( nearestRange > range )
		{
			nearestRange = range;
			nearest = trigger;
		}
	}
	if ( nearest )
		m_hLastFogTrigger = nearest;
	// Native behavior retains the last selected trigger after all touches end.
	return m_hLastFogTrigger.Get();
}

bool CBaseCombatCharacter::GetFogParams( fogparams_t *fog ) const
{
	if ( !fog )
		return false;

	// The pinned NativeNAV lookup at S2E2A90 always returns false. It contributes
	// no fog data and must not be replaced with invented per-area parameters.
	const fogparams_t *source = NULL;
	if ( GetFogTrigger() )
	{
		CFogTrigger *trigger = dynamic_cast< CFogTrigger * >( GetFogTrigger() );
		if ( trigger )
			source = &trigger->GetFog();
	}
	if ( !source )
	{
		CFogController *controller = FogSystem()->GetMasterFogController();
		if ( controller )
			source = &controller->m_fog;
	}
	if ( source )
	{
		if ( *source != *fog )
		{
			*fog = *source;
			return true;
		}
		return false;
	}
	if ( fog->farz == -1.0f && !fog->enable )
		return false;
	fog->farz = -1.0f;
	fog->enable = false;
	return true;
}

LINK_ENTITY_TO_CLASS( postprocess_controller, CPostProcessController );

BEGIN_DATADESC( CPostProcessController )
	DEFINE_KEYFIELD( m_fLocalContrastStrength, FIELD_FLOAT, "localcontraststrength" ),
	DEFINE_KEYFIELD( m_fLocalContrastEdgeStrength, FIELD_FLOAT, "localcontrastedgestrength" ),
	DEFINE_KEYFIELD( m_fVignetteStart, FIELD_TIME, "vignettestart" ),
	DEFINE_KEYFIELD( m_fVignetteEnd, FIELD_TIME, "vignetteend" ),
	DEFINE_KEYFIELD( m_fVignetteBlurStrength, FIELD_FLOAT, "vignetteblurstrength" ),
	DEFINE_KEYFIELD( m_fFadeToBlackStrength, FIELD_FLOAT, "fadetoblackstrength" ),
	DEFINE_KEYFIELD( m_fGrainStrength, FIELD_FLOAT, "grainstrength" ),
	DEFINE_KEYFIELD( m_fTopVignetteStrength, FIELD_FLOAT, "topvignettestrength" ),
	DEFINE_KEYFIELD( m_fFadeTime, FIELD_FLOAT, "fadetime" ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetLocalContrastStrength", InputSetLocalContrastStrength ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetLocalContrastEdgeStrength", InputSetLocalContrastEdgeStrength ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetVignetteStart", InputSetVignetteStart ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetVignetteEnd", InputSetVignetteEnd ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetVignetteBlurStrength", InputSetVignetteBlurStrength ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetFadeToBlackStrength", InputSetFadeToBlackStrength ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetGrainStrength", InputSetGrainStrength ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetTopVignetteStrength", InputSetTopVignetteStrength ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetFadeTime", InputSetFadeTime ),
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CPostProcessController, DT_PostProcessController )
	SendPropFloat( SENDINFO( m_fLocalContrastStrength ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_fLocalContrastEdgeStrength ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_fVignetteStart ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_fVignetteEnd ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_fVignetteBlurStrength ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_fFadeToBlackStrength ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_fGrainStrength ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_fTopVignetteStrength ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_fFadeTime ), 0, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_bMaster ) ),
END_SEND_TABLE()

CPostProcessController::CPostProcessController()
{
	m_fLocalContrastStrength = 0.0f;
	m_fLocalContrastEdgeStrength = 0.0f;
	m_fVignetteStart = 1.0f;
	m_fVignetteEnd = 2.0f;
	m_fVignetteBlurStrength = 0.0f;
	m_fFadeToBlackStrength = 0.0f;
	m_fGrainStrength = 1.0f;
	m_fTopVignetteStrength = 1.0f;
	m_fFadeTime = 2.0f;
	m_bMaster = false;
}

void CPostProcessController::Spawn()
{
	BaseClass::Spawn();
	m_bMaster = HasSpawnFlags( 1 );
}

int CPostProcessController::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

void CPostProcessController::InputSetLocalContrastStrength( inputdata_t &data ) { m_fLocalContrastStrength = data.value.Float(); }
void CPostProcessController::InputSetLocalContrastEdgeStrength( inputdata_t &data ) { m_fLocalContrastEdgeStrength = data.value.Float(); }
void CPostProcessController::InputSetVignetteStart( inputdata_t &data ) { m_fVignetteStart = data.value.Float(); }
void CPostProcessController::InputSetVignetteEnd( inputdata_t &data ) { m_fVignetteEnd = data.value.Float(); }
void CPostProcessController::InputSetVignetteBlurStrength( inputdata_t &data ) { m_fVignetteBlurStrength = data.value.Float(); }
void CPostProcessController::InputSetFadeToBlackStrength( inputdata_t &data ) { m_fFadeToBlackStrength = data.value.Float(); }
void CPostProcessController::InputSetGrainStrength( inputdata_t &data ) { m_fGrainStrength = data.value.Float(); }
void CPostProcessController::InputSetTopVignetteStrength( inputdata_t &data ) { m_fTopVignetteStrength = data.value.Float(); }
void CPostProcessController::InputSetFadeTime( inputdata_t &data ) { m_fFadeTime = data.value.Float(); }

class CPostProcessSystem : public CAutoGameSystem, public CGameEventListener
{
public:
	CPostProcessSystem() : CAutoGameSystem( "PostProcessSystem" ) {}
	virtual void LevelInitPreEntity() OVERRIDE
	{
		m_hMaster = NULL;
		ListenForGameEvent( "round_start" );
	}
	virtual void LevelInitPostEntity() OVERRIDE
	{
		FindMaster();
		if ( gpGlobals->maxClients == 1 )
		{
			CBasePlayer *player = UTIL_GetLocalPlayer();
			if ( player && !player->GetTerrorPostProcessController() )
				player->SetTerrorPostProcessController( m_hMaster.Get() );
		}
	}
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE { FindMaster(); }
	CPostProcessController *GetMaster() const { return m_hMaster.Get(); }
private:
	void FindMaster()
	{
		CPostProcessController *controller = NULL;
		while ( ( controller = dynamic_cast< CPostProcessController * >(
			gEntList.FindEntityByClassname( controller, "postprocess_controller" ) ) ) != NULL )
		{
			if ( !m_hMaster.Get() || controller->HasSpawnFlags( 1 ) )
				m_hMaster = controller;
		}
	}
	CHandle< CPostProcessController > m_hMaster;
};
static CPostProcessSystem s_PostProcessSystem;

CPostProcessController *Terror_GetMasterPostProcessController()
{
	return s_PostProcessSystem.GetMaster();
}

class CColorCorrectionSystem : public CAutoGameSystem, public CGameEventListener
{
public:
	CColorCorrectionSystem() : CAutoGameSystem( "ColorCorrectionSystem" ) {}
	virtual void LevelInitPreEntity() OVERRIDE
	{
		m_hMaster = NULL;
		ListenForGameEvent( "round_start" );
	}
	virtual void LevelInitPostEntity() OVERRIDE
	{
		FindMaster();
		if ( gpGlobals->maxClients == 1 )
		{
			CBasePlayer *player = UTIL_GetLocalPlayer();
			if ( player && !player->GetTerrorColorCorrectionController() )
				player->SetTerrorColorCorrectionController( m_hMaster.Get() );
		}
	}
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE { FindMaster(); }
	CBaseEntity *GetMaster() const { return m_hMaster.Get(); }
private:
	void FindMaster()
	{
		CBaseEntity *controller = NULL;
		while ( ( controller = Terror_CastColorCorrection(
			gEntList.FindEntityByClassname( controller, "color_correction" ) ) ) != NULL )
		{
			if ( !m_hMaster.Get() || controller->HasSpawnFlags( 1 ) )
				m_hMaster = controller;
		}
	}
	EHANDLE m_hMaster;
};
static CColorCorrectionSystem s_ColorCorrectionSystem;

CBaseEntity *Terror_GetMasterColorCorrection()
{
	return s_ColorCorrectionSystem.GetMaster();
}

class CFogVolume : public CBaseEntity
{
	DECLARE_CLASS( CFogVolume, CBaseEntity );
public:
	DECLARE_DATADESC();
	CFogVolume();
	virtual ~CFogVolume();
	virtual void Spawn() OVERRIDE;
	virtual void Activate() OVERRIDE;
	void InputEnable( inputdata_t &data );
	void InputDisable( inputdata_t &data );
	static CFogVolume *FindAtPoint( const Vector &position );
	static int Count() { return s_volumes.Count(); }
	CFogController *GetFogController() const { return m_hFogController.Get(); }
	CPostProcessController *GetPostProcessController() const { return m_hPostProcessController.Get(); }
	CBaseEntity *GetColorCorrectionController() const { return m_hColorCorrectionController.Get(); }
private:
	void Register();
	static CUtlVector< CFogVolume * > s_volumes;
	string_t m_fogName;
	string_t m_postProcessName;
	string_t m_colorCorrectionName;
	CHandle< CFogController > m_hFogController;
	CHandle< CPostProcessController > m_hPostProcessController;
	EHANDLE m_hColorCorrectionController;
	bool m_bDisabled;
	bool m_bRegistered;
};

CUtlVector< CFogVolume * > CFogVolume::s_volumes;
LINK_ENTITY_TO_CLASS( fog_volume, CFogVolume );
BEGIN_DATADESC( CFogVolume )
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	DEFINE_KEYFIELD( m_fogName, FIELD_STRING, "FogName" ),
	DEFINE_KEYFIELD( m_postProcessName, FIELD_STRING, "PostProcessName" ),
	DEFINE_KEYFIELD( m_colorCorrectionName, FIELD_STRING, "ColorCorrectionName" ),
	DEFINE_KEYFIELD( m_bDisabled, FIELD_BOOLEAN, "StartDisabled" ),
	DEFINE_FIELD( m_hFogController, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hPostProcessController, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hColorCorrectionController, FIELD_EHANDLE ),
END_DATADESC()

CFogVolume::CFogVolume()
	: BaseClass( true ), m_fogName( NULL_STRING ), m_postProcessName( NULL_STRING ),
	m_colorCorrectionName( NULL_STRING ), m_bDisabled( false ), m_bRegistered( false )
{
}

CFogVolume::~CFogVolume()
{
	if ( m_bRegistered )
		s_volumes.FindAndRemove( this );
}

void CFogVolume::Spawn()
{
	BaseClass::Spawn();
	SetSolid( SOLID_BSP );
	SetSolidFlags( FSOLID_NOT_SOLID );
	SetModel( STRING( GetModelName() ) );
}

void CFogVolume::Activate()
{
	BaseClass::Activate();
	m_hFogController = dynamic_cast< CFogController * >( gEntList.FindEntityByName( NULL, STRING( m_fogName ) ) );
	m_hPostProcessController = dynamic_cast< CPostProcessController * >( gEntList.FindEntityByName( NULL, STRING( m_postProcessName ) ) );
	m_hColorCorrectionController = Terror_FindColorCorrectionByName( STRING( m_colorCorrectionName ) );
	if ( !m_bDisabled )
		Register();
}

void CFogVolume::Register()
{
	if ( !m_bRegistered )
	{
		s_volumes.AddToTail( this );
		m_bRegistered = true;
	}
}

void CFogVolume::InputEnable( inputdata_t &data )
{
	m_bDisabled = false;
	Register();
}

void CFogVolume::InputDisable( inputdata_t &data )
{
	m_bDisabled = true;
	if ( m_bRegistered )
	{
		s_volumes.FindAndRemove( this );
		m_bRegistered = false;
	}
}

CFogVolume *CFogVolume::FindAtPoint( const Vector &position )
{
	for ( int i = 0; i < s_volumes.Count(); ++i )
	{
		CFogVolume *volume = s_volumes[i];
		Vector localPosition;
		volume->CollisionProp()->WorldToCollisionSpace( position, &localPosition );
		if ( IsBoxIntersectingSphere( volume->CollisionProp()->OBBMins(),
			volume->CollisionProp()->OBBMaxs(), localPosition, 1.0f ) )
			return volume;
	}
	return NULL;
}

void Terror_UpdatePlayerEnvironment( CBasePlayer *player )
{
	const Vector position = player->GetViewEntity() ? player->GetViewEntity()->GetAbsOrigin() : player->EyePosition();
	CFogVolume *volume = CFogVolume::FindAtPoint( position );
	CFogController *fog;
	CPostProcessController *postProcess;
	CBaseEntity *colorCorrection;
	if ( volume )
	{
		fog = volume->GetFogController();
		postProcess = volume->GetPostProcessController();
		colorCorrection = volume->GetColorCorrectionController();
		if ( !fog )
			fog = FogSystem()->GetMasterFogController();
		if ( !postProcess )
			postProcess = Terror_GetMasterPostProcessController();
		if ( !colorCorrection )
			colorCorrection = Terror_GetMasterColorCorrection();
	}
	else
	{
		// Preserve native stale-handle behavior when the last enabled volume is
		// disabled/deleted. The mere absence of volumes does not reset channels.
		if ( CFogVolume::Count() <= 0 )
			return;
		fog = FogSystem()->GetMasterFogController();
		postProcess = Terror_GetMasterPostProcessController();
		colorCorrection = Terror_GetMasterColorCorrection();
	}
	if ( fog && player->m_Local.m_PlayerFog.m_hCtrl.Get() != fog )
		player->m_Local.m_PlayerFog.m_hCtrl = fog;
	if ( postProcess )
		player->SetTerrorPostProcessController( postProcess );
	if ( colorCorrection )
		player->SetTerrorColorCorrectionController( colorCorrection );
}

// CTonemapTrigger and its handles are separate from fog_volume. Native exposure
// picks a controller class for the actual actor, not the current fog volume.
class CTonemapTrigger : public CBaseTrigger
{
	DECLARE_CLASS( CTonemapTrigger, CBaseTrigger );
public:
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
	virtual void StartTouch( CBaseEntity *other ) OVERRIDE;
	virtual void EndTouch( CBaseEntity *other ) OVERRIDE;
	CBaseEntity *GetController() const { return m_hController.Get(); }
private:
	string_t m_tonemapControllerName;
	EHANDLE m_hController;
};

LINK_ENTITY_TO_CLASS( trigger_tonemap, CTonemapTrigger );
BEGIN_DATADESC( CTonemapTrigger )
	DEFINE_KEYFIELD( m_tonemapControllerName, FIELD_STRING, "TonemapName" ),
END_DATADESC()

void CTonemapTrigger::Spawn()
{
	AddSpawnFlags( SF_TRIGGER_ALLOW_CLIENTS );
	BaseClass::Spawn();
	InitTrigger();
	m_hController = gEntList.FindEntityByName( NULL, STRING( m_tonemapControllerName ) );
}

void CTonemapTrigger::StartTouch( CBaseEntity *other )
{
	if ( PassesTriggerFilters( other ) )
	{
		BaseClass::StartTouch( other );
		if ( other && other->IsPlayer() )
			ToBasePlayer( other )->AddTonemapTrigger( this );
	}
}

void CTonemapTrigger::EndTouch( CBaseEntity *other )
{
	if ( PassesTriggerFilters( other ) )
	{
		BaseClass::EndTouch( other );
		if ( other && other->IsPlayer() )
			ToBasePlayer( other )->RemoveTonemapTrigger( this );
	}
}

class CTonemapSystem : public CAutoGameSystem
{
public:
	CTonemapSystem() : CAutoGameSystem( "TonemapSystem" ) {}
	virtual void LevelInitPreEntity() OVERRIDE
	{
		for ( int i = 0; i < ARRAYSIZE( m_hMaster ); ++i )
			m_hMaster[i] = NULL;
	}
	virtual void LevelInitPostEntity() OVERRIDE
	{
		for ( int i = 0; i < ARRAYSIZE( m_hMaster ); ++i )
		{
			CBaseEntity *controller = NULL;
			while ( ( controller = gEntList.FindEntityByClassname( controller, GetClassname( i ) ) ) != NULL )
			{
				if ( !m_hMaster[i].Get() || controller->HasSpawnFlags( 1 ) )
					m_hMaster[i] = controller;
			}
		}
	}
	CBaseEntity *GetMaster( int kind ) const { return m_hMaster[kind].Get(); }
	static const char *GetClassname( int kind )
	{
		static const char *const names[] = { "env_tonemap_controller", "env_tonemap_controller_infected", "env_tonemap_controller_ghost" };
		return names[kind];
	}
private:
	EHANDLE m_hMaster[3];
};
static CTonemapSystem s_TonemapSystem;

void CBasePlayer::AddTonemapTrigger( CBaseEntity *trigger )
{
	m_hTriggerTonemapList.FindAndRemove( EHANDLE( trigger ) );
	m_hTriggerTonemapList.AddToTail( EHANDLE( trigger ) );
}

void CBasePlayer::RemoveTonemapTrigger( CBaseEntity *trigger )
{
	m_hTriggerTonemapList.FindAndRemove( EHANDLE( trigger ) );
}

void CBasePlayer::UpdateTonemapController()
{
	m_hTonemapController = s_TonemapSystem.GetMaster( 0 );
}

void CTerrorPlayer::UpdateTonemapController()
{
	for ( int i = m_hTriggerTonemapList.Count() - 1; i >= 0; --i )
	{
		if ( !m_hTriggerTonemapList[i].Get() )
			m_hTriggerTonemapList.Remove( i );
	}
	if ( !IsAlive() )
	{
		CBaseEntity *observerTarget = GetObserverTarget();
		if ( observerTarget && observerTarget->IsPlayer() )
		{
			m_hTonemapController = ToBasePlayer( observerTarget )->GetTerrorTonemapController();
			return;
		}
	}

	int kind = 0;
	bool checkTriggers = IsAlive();
	if ( m_isGhost )
	{
		kind = 2;
		checkTriggers = true;
	}
	else if ( IsAlive() && GetTeamNumber() == TEAM_INFECTED )
	{
		kind = 1;
	}
	if ( checkTriggers )
	{
		for ( int i = 0; i < m_hTriggerTonemapList.Count(); ++i )
		{
			CTonemapTrigger *trigger = static_cast< CTonemapTrigger * >( m_hTriggerTonemapList[i].Get() );
			CBaseEntity *controller = trigger->GetController();
			if ( controller && FClassnameIs( controller, CTonemapSystem::GetClassname( kind ) ) )
			{
				m_hTonemapController = controller;
				return;
			}
		}
	}
	m_hTonemapController = s_TonemapSystem.GetMaster( kind );
}
