//========= Copyright Valve Corporation, All rights reserved. ============//
// Included only by player_lagcompensation.cpp's L4D2 branch. This is that file's
// CLagCompensationManager, not a second manager or an engine-interface replica.
// Recovery: S:0x187A70, 0x188470, 0x185C50, 0x187100, 0x187360, 0x180C10.

static ConVar sv_lagcompensationforcerestore( "sv_lagcompensationforcerestore", "1", 0x00004004, "Don't test validity of a lag comp restore, just do it." );
static ConVar sv_lagpushticks( "sv_lagpushticks", "0", 0x00000006, "Push computed lag compensation amount by this many ticks." );

struct TerrorLagRecord
{
	TerrorLagRecord() : m_fFlags( 0 ), m_flSimulationTime( -1.0f ), m_masterSequence( 0 ), m_masterCycle( 0.0f )
	{
		m_vecOrigin.Init();
		m_vecAngles.Init();
		m_vecMins.Init();
		m_vecMaxs.Init();
	}

	int m_fFlags;
	Vector m_vecOrigin;
	QAngle m_vecAngles;
	Vector m_vecMins;
	Vector m_vecMaxs;
	float m_flSimulationTime;
	LayerRecord m_layerRecords[MAX_LAYER_RECORDS];
	int m_masterSequence;
	float m_masterCycle;
	// The original 304-byte record has no pose parameters/playback rate/AI
	// clocks. Do not rewind behavior or add a second animation clock here.
};

struct TerrorLagTrack
{
	TerrorLagTrack() : m_bRestore( false ) {}
	EHANDLE m_hEntity;
	CUtlFixedLinkedList< TerrorLagRecord > m_History;
	TerrorLagRecord m_Restore;
	TerrorLagRecord m_Change;
	bool m_bRestore;
};

class CLagCompensationManager : public CAutoGameSystemPerFrame, public ILagCompensationManager
{
public:
	CLagCompensationManager( const char *pName ) : CAutoGameSystemPerFrame( pName ),
		m_Tracks( DefLessFunc( CBaseHandle ) ), m_AdditionalEntities( DefLessFunc( CBaseHandle ) ), m_pCurrentPlayer( NULL ),
		m_Type( TERROR_LAG_COMPENSATE_BOUNDS ), m_flWeaponRange( 0.0f ),
		m_bNeedToRestore( false ), m_isCurrentlyDoingCompensation( false )
	{
		m_WeaponPosition.Init();
		m_WeaponAngles.Init();
	}
	virtual ~CLagCompensationManager() { ClearHistory(); }

	virtual void Shutdown() OVERRIDE { ClearHistory(); }
	virtual void LevelShutdownPostEntity() OVERRIDE { ClearHistory(); }
	virtual void FrameUpdatePostEntityThink() OVERRIDE;
	virtual void StartLagCompensation( CBasePlayer *pPlayer, CUserCmd *pCommand ) OVERRIDE
	{
		StartLagCompensation( pPlayer, pCommand, TERROR_LAG_COMPENSATE_HITBOXES, vec3_origin, vec3_angle, 0.0f );
	}
	void StartLagCompensation( CBasePlayer *pPlayer, CUserCmd *pCommand, TerrorLagCompensationType type,
		const Vector &weaponPosition, const QAngle &weaponAngles, float weaponRange );
	virtual void FinishLagCompensation( CBasePlayer *pPlayer ) OVERRIDE;
	virtual bool IsCurrentlyDoingLagCompensation() const OVERRIDE { return m_isCurrentlyDoingCompensation; }
	void AddAdditionalEntity( CBaseEntity *pEntity );
	void RemoveAdditionalEntity( CBaseEntity *pEntity );

private:
	void ClearHistory();
	void RecordEntity( CBaseEntity *pEntity );
	bool BacktrackEntity( TerrorLagTrack *pTrack, float targetTime );
	bool WantsLagCompensation( CBaseEntity *pEntity, const CUserCmd *pCommand, const CBitVec<MAX_EDICTS> *pTransmitBits ) const;
	void RestoreEntity( TerrorLagTrack *pTrack );
	bool ShouldRewindAnimation( CBaseEntity *pEntity ) const;
	void SaveAnimation( CBaseAnimating *pAnimating, TerrorLagRecord *pRecord );
	void ApplyAnimation( CBaseAnimating *pAnimating, const TerrorLagRecord &record,
		const TerrorLagRecord *pNewer = NULL, float fraction = 0.0f );

	class RecordBot
	{
	public:
		RecordBot( CLagCompensationManager *pManager ) : m_pManager( pManager ) {}
		bool operator()( INextBot *pBot )
		{
			CBaseEntity *pEntity = pBot->GetEntity();
			if ( pEntity && !pEntity->IsPlayer() )
				m_pManager->RecordEntity( pEntity );
			return true;
		}
	private:
		CLagCompensationManager *m_pManager;
	};

	CUtlMap< CBaseHandle, TerrorLagTrack * > m_Tracks;
	CUtlMap< CBaseHandle, EHANDLE > m_AdditionalEntities;
	CBasePlayer *m_pCurrentPlayer;
	TerrorLagCompensationType m_Type;
	Vector m_WeaponPosition;
	QAngle m_WeaponAngles;
	float m_flWeaponRange;
	bool m_bNeedToRestore;
	bool m_isCurrentlyDoingCompensation;
};

static CLagCompensationManager g_LagCompensationManager( "CLagCompensationManager" );
ILagCompensationManager *lagcompensation = &g_LagCompensationManager;

void StartTerrorLagCompensation( CBasePlayer *pPlayer, TerrorLagCompensationType type,
	const Vector &weaponPosition, const QAngle &weaponAngles, float weaponRange )
{
	g_LagCompensationManager.StartLagCompensation( pPlayer, pPlayer->GetCurrentCommand(),
		type, weaponPosition, weaponAngles, weaponRange );
}

void AddTerrorLagCompensatedEntity( CBaseEntity *pEntity )
{
	g_LagCompensationManager.AddAdditionalEntity( pEntity );
}

void RemoveTerrorLagCompensatedEntity( CBaseEntity *pEntity )
{
	g_LagCompensationManager.RemoveAdditionalEntity( pEntity );
}

void CLagCompensationManager::AddAdditionalEntity( CBaseEntity *pEntity )
{
	const CBaseHandle handle = pEntity ? pEntity->GetRefEHandle() : CBaseHandle();
	if ( m_AdditionalEntities.Find( handle ) == m_AdditionalEntities.InvalidIndex() )
		m_AdditionalEntities.Insert( handle, pEntity );
}

void CLagCompensationManager::RemoveAdditionalEntity( CBaseEntity *pEntity )
{
	const CBaseHandle handle = pEntity ? pEntity->GetRefEHandle() : CBaseHandle();
	m_AdditionalEntities.Remove( handle );
}

void CLagCompensationManager::ClearHistory()
{
	FOR_EACH_MAP( m_Tracks, i )
		delete m_Tracks[i];
	m_Tracks.RemoveAll();
	m_pCurrentPlayer = NULL;
	m_bNeedToRestore = false;
	m_isCurrentlyDoingCompensation = false;
}

void CLagCompensationManager::SaveAnimation( CBaseAnimating *pAnimating, TerrorLagRecord *pRecord )
{
	pRecord->m_masterSequence = pAnimating->GetSequence();
	pRecord->m_masterCycle = pAnimating->GetCycle();
	CBaseAnimatingOverlay *pOverlay = dynamic_cast< CBaseAnimatingOverlay * >( pAnimating );
	if ( !pOverlay )
		return;
	int layerCount = pOverlay->GetNumAnimOverlays();
	for ( int i = 0; i < layerCount; ++i )
	{
		CAnimationLayer *pLayer = pOverlay->GetAnimOverlay( i );
		if ( !pLayer )
			continue;
		pRecord->m_layerRecords[i].m_sequence = pLayer->m_nSequence;
		pRecord->m_layerRecords[i].m_cycle = pLayer->m_flCycle;
		pRecord->m_layerRecords[i].m_weight = pLayer->m_flWeight;
		pRecord->m_layerRecords[i].m_order = pLayer->m_nOrder;
	}
}

static float TerrorLerpCycle( float fraction, float older, float newer )
{
	if ( older <= newer )
		return Lerp( fraction, older, newer );
	float cycle = Lerp( fraction, older, newer + 1.0f );
	return cycle < 1.0f ? cycle : cycle - 1.0f;
}

void CLagCompensationManager::ApplyAnimation( CBaseAnimating *pAnimating, const TerrorLagRecord &record,
	const TerrorLagRecord *pNewer, float fraction )
{
	bool interpolate = pNewer && fraction > 0.0f && record.m_masterSequence == pNewer->m_masterSequence;
	pAnimating->SetSequence( record.m_masterSequence );
	pAnimating->SetCycle( interpolate ? TerrorLerpCycle( fraction, record.m_masterCycle, pNewer->m_masterCycle ) : record.m_masterCycle );
	CBaseAnimatingOverlay *pOverlay = dynamic_cast< CBaseAnimatingOverlay * >( pAnimating );
	if ( !pOverlay )
		return;
	int layerCount = pOverlay->GetNumAnimOverlays();
	for ( int i = 0; i < layerCount; ++i )
	{
		CAnimationLayer *pLayer = pOverlay->GetAnimOverlay( i );
		if ( !pLayer )
			continue;
		const LayerRecord &older = record.m_layerRecords[i];
		const LayerRecord *pNext = pNewer ? &pNewer->m_layerRecords[i] : NULL;
		bool interpolateLayer = interpolate && older.m_sequence == pNext->m_sequence && older.m_order == pNext->m_order;
		pLayer->m_nSequence = older.m_sequence;
		pLayer->m_nOrder = older.m_order;
		pLayer->m_flCycle = interpolateLayer ? TerrorLerpCycle( fraction, older.m_cycle, pNext->m_cycle ) : older.m_cycle;
		pLayer->m_flWeight = interpolateLayer ? Lerp( fraction, older.m_weight, pNext->m_weight ) : older.m_weight;
	}
}

void CLagCompensationManager::RecordEntity( CBaseEntity *pEntity )
{
	unsigned short index = m_Tracks.Find( pEntity->GetRefEHandle() );
	TerrorLagTrack *pTrack;
	if ( index == m_Tracks.InvalidIndex() )
	{
		pTrack = new TerrorLagTrack;
		pTrack->m_hEntity = pEntity;
		m_Tracks.Insert( pEntity->GetRefEHandle(), pTrack );
	}
	else
	{
		pTrack = m_Tracks[index];
	}

	CUtlFixedLinkedList< TerrorLagRecord > &history = pTrack->m_History;
	// S:0x187100 truncates the cutoff to int before comparing float timestamps.
	int deadTime = gpGlobals->curtime - sv_maxunlag.GetFloat();
	while ( history.Count() && history[history.Tail()].m_flSimulationTime < deadTime )
		history.Remove( history.Tail() );
	if ( history.Count() && history[history.Head()].m_flSimulationTime >= pEntity->GetSimulationTime() )
		return;

	TerrorLagRecord &record = history[history.AddToHead()];
	record.m_fFlags = pEntity->IsAlive() ? LC_ALIVE : 0;
	record.m_flSimulationTime = pEntity->GetSimulationTime();
	record.m_vecOrigin = pEntity->GetAbsOrigin();
	record.m_vecAngles = pEntity->GetAbsAngles();
	record.m_vecMins = pEntity->CollisionProp()->OBBMins();
	record.m_vecMaxs = pEntity->CollisionProp()->OBBMaxs();
	CBaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	if ( pAnimating )
		SaveAnimation( pAnimating, &record );
}

void CLagCompensationManager::FrameUpdatePostEntityThink()
{
	// This is maxClients, not the number of connected humans. Coop listen hosts
	// have multiple slots and do record common infected with one human present.
	if ( gpGlobals->maxClients <= 1 || !sv_unlag.GetBool() )
	{
		ClearHistory();
		return;
	}

	for ( unsigned short i = m_Tracks.FirstInorder(); i != m_Tracks.InvalidIndex(); )
	{
		unsigned short next = m_Tracks.NextInorder( i );
		if ( !m_Tracks[i]->m_hEntity.Get() )
		{
			delete m_Tracks[i];
			m_Tracks.RemoveAt( i );
		}
		i = next;
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( pPlayer )
			RecordEntity( pPlayer );
	}
	RecordBot recordBot( this );
	TheNextBots().ForEachBot( recordBot );
	FOR_EACH_MAP( m_AdditionalEntities, i )
	{
		CBaseEntity *pEntity = m_AdditionalEntities[i].Get();
		if ( pEntity )
			RecordEntity( pEntity );
	}
}

bool CLagCompensationManager::WantsLagCompensation( CBaseEntity *pEntity, const CUserCmd *pCommand,
	const CBitVec<MAX_EDICTS> *pTransmitBits ) const
{
	CTerrorPlayer *pShooter = ToTerrorPlayer( m_pCurrentPlayer );
	if ( !( pCommand->buttons & ( IN_ATTACK | IN_ATTACK2 ) ) &&
		pCommand->command_number - pShooter->GetLastTerrorWeaponFireCommand() > 5 )
		return false;
	CTerrorPlayer *pTarget = ToTerrorPlayer( pEntity );
	if ( pTarget && ( pTarget->m_jockeyVictim.Get() || pTarget->m_carryAttacker.Get() ) )
		return false;
	if ( !friendlyfire.GetBool() && pEntity->GetTeamNumber() == pShooter->GetTeamNumber() )
		return false;
	if ( pTransmitBits && !pTransmitBits->Get( pEntity->entindex() ) )
		return false;

	CBasePlayer *pTargetPlayer = ToBasePlayer( pEntity );
	float maxSpeed = pTargetPlayer ? pTargetPlayer->MaxSpeed() : 300.0f;
	Vector difference = pEntity->GetAbsOrigin() - pShooter->GetAbsOrigin();
	if ( difference.Length() < 1.5f * maxSpeed * sv_maxunlag.GetFloat() )
		return true;
	Vector forward;
	AngleVectors( pCommand->viewangles, &forward );
	VectorNormalize( difference );
	return forward.Dot( difference ) >= 0.707107f;
}

void CLagCompensationManager::StartLagCompensation( CBasePlayer *pPlayer, CUserCmd *pCommand,
	TerrorLagCompensationType type, const Vector &weaponPosition, const QAngle &weaponAngles, float weaponRange )
{
	Assert( !m_isCurrentlyDoingCompensation );
	FOR_EACH_MAP( m_Tracks, i )
		m_Tracks[i]->m_bRestore = false;
	m_bNeedToRestore = false;
	m_pCurrentPlayer = pPlayer;
	if ( !pPlayer->m_bLagCompensation || gpGlobals->maxClients <= 1 || !sv_unlag.GetBool() ||
		pPlayer->IsFakeClient() || pPlayer->IsObserver() )
		return;
	if ( !pCommand )
		Error( "CLagCompensationManager::StartLagCompensation with NULL CUserCmd!!!\n" );

	m_isCurrentlyDoingCompensation = true;
	m_Type = type;
	m_WeaponPosition = weaponPosition;
	m_WeaponAngles = weaponAngles;
	m_flWeaponRange = weaponRange;
	float correct = 0.0f;
	INetChannelInfo *pChannel = engine->GetPlayerNetInfo( pPlayer->entindex() );
	if ( pChannel )
		correct = pChannel->GetLatency( FLOW_OUTGOING );
	correct += pPlayer->m_fLerpTime;
	correct = clamp( correct, 0.0f, sv_maxunlag.GetFloat() );
	float targetTime = pCommand->tick_count * gpGlobals->interval_per_tick - pPlayer->m_fLerpTime;
	if ( fabs( correct - ( gpGlobals->curtime - targetTime ) ) > 0.2f )
		targetTime = gpGlobals->curtime - correct;
	targetTime += sv_lagpushticks.GetInt() * gpGlobals->interval_per_tick;
	const CBitVec<MAX_EDICTS> *pTransmitBits = engine->GetEntityTransmitBitsForClient( pPlayer->entindex() - 1 );
	FOR_EACH_MAP( m_Tracks, i )
	{
		TerrorLagTrack *pTrack = m_Tracks[i];
		CBaseEntity *pEntity = pTrack->m_hEntity.Get();
		if ( pEntity && pEntity != pPlayer && WantsLagCompensation( pEntity, pCommand, pTransmitBits ) )
			pTrack->m_bRestore = BacktrackEntity( pTrack, targetTime );
	}
}

bool CLagCompensationManager::ShouldRewindAnimation( CBaseEntity *pEntity ) const
{
	if ( m_Type == TERROR_LAG_COMPENSATE_BOUNDS )
		return false;
	if ( m_Type != TERROR_LAG_COMPENSATE_HITBOXES_ALONG_RAY )
		return true;
	Vector forward;
	AngleVectors( m_WeaponAngles, &forward );
	Vector ray = forward * m_flWeaponRange;
	float length = VectorNormalize( ray );
	Vector toCenter = pEntity->WorldSpaceCenter() - m_WeaponPosition;
	float along = DotProduct( toCenter, ray );
	// The original finite-ray distance is negative outside either endpoint.
	if ( along < 0.0f || along > length )
		return false;
	return ( toCenter - ray * along ).Length() <= 100.0f;
}

bool CLagCompensationManager::BacktrackEntity( TerrorLagTrack *pTrack, float targetTime )
{
	CBaseEntity *pEntity = pTrack->m_hEntity.Get();
	CUtlFixedLinkedList< TerrorLagRecord > &history = pTrack->m_History;
	if ( !pEntity || !history.Count() )
		return false;
	TerrorLagRecord *pRecord = NULL;
	TerrorLagRecord *pNewer = NULL;
	Vector previousOrigin = pEntity->GetAbsOrigin();
	for ( intp i = history.Head(); history.IsValidIndex( i ); i = history.Next( i ) )
	{
		pNewer = pRecord;
		pRecord = &history[i];
		if ( !( pRecord->m_fFlags & LC_ALIVE ) || ( pRecord->m_vecOrigin - previousOrigin ).LengthSqr() > 4096.0f )
			return false;
		if ( pRecord->m_flSimulationTime <= targetTime )
			break;
		previousOrigin = pRecord->m_vecOrigin;
	}
	if ( !pRecord )
		return false;

	float fraction = 0.0f;
	Vector origin = pRecord->m_vecOrigin;
	QAngle angles = pRecord->m_vecAngles;
	Vector mins = pRecord->m_vecMins;
	Vector maxs = pRecord->m_vecMaxs;
	if ( pNewer && pRecord->m_flSimulationTime < targetTime && pRecord->m_flSimulationTime < pNewer->m_flSimulationTime )
	{
		fraction = ( targetTime - pRecord->m_flSimulationTime ) / ( pNewer->m_flSimulationTime - pRecord->m_flSimulationTime );
		origin = Lerp( fraction, origin, pNewer->m_vecOrigin );
		mins = Lerp( fraction, mins, pNewer->m_vecMins );
		maxs = Lerp( fraction, maxs, pNewer->m_vecMaxs );
		if ( angles != pNewer->m_vecAngles )
		{
			Quaternion older, newer, interpolated;
			AngleQuaternion( angles, older );
			AngleQuaternion( pNewer->m_vecAngles, newer );
			QuaternionSlerp( older, newer, fraction, interpolated );
			QuaternionAngles( interpolated, angles );
		}
	}

	if ( sv_unlag_fixstuck.GetBool() )
	{
		trace_t trace;
		unsigned int mask = ( pEntity->GetTeamNumber() == 2 || pEntity->GetTeamNumber() == 4 ) ? 0x0201420Bu : 0x0202420Bu;
		UTIL_TraceEntity( pEntity, origin, origin, mask, &trace );
		if ( trace.startsolid || trace.allsolid )
		{
			CBaseEntity *pHit = trace.m_pEnt;
			if ( pHit && pHit != m_pCurrentPlayer )
			{
				unsigned short index = m_Tracks.Find( pHit->GetRefEHandle() );
				if ( index != m_Tracks.InvalidIndex() && !m_Tracks[index]->m_bRestore )
				{
					// S186272-S18627A guards the blocker only during recursion.
					m_Tracks[index]->m_bRestore = true;
					BacktrackEntity( m_Tracks[index], targetTime );
					m_Tracks[index]->m_bRestore = false;
				}
			}
			UTIL_TraceEntity( pEntity, pEntity->GetAbsOrigin(), origin, mask, &trace );
			if ( !trace.startsolid && !trace.allsolid )
				origin = Lerp( trace.fraction * g_flFractionScale, pEntity->GetAbsOrigin(), origin );
		}
	}

	TerrorLagRecord &restore = pTrack->m_Restore;
	TerrorLagRecord &change = pTrack->m_Change;
	int flags = 0;
	restore.m_flSimulationTime = pEntity->GetSimulationTime();
	if ( ( pEntity->GetAbsAngles() - angles ).LengthSqr() > LAG_COMPENSATION_EPS_SQR )
	{
		flags |= LC_ANGLES_CHANGED;
		restore.m_vecAngles = pEntity->GetAbsAngles();
		pEntity->SetAbsAngles( angles );
		change.m_vecAngles = angles;
	}
	if ( pEntity->CollisionProp()->OBBMins() != mins || pEntity->CollisionProp()->OBBMaxs() != maxs )
	{
		flags |= LC_SIZE_CHANGED;
		restore.m_vecMins = pEntity->CollisionProp()->OBBMins();
		restore.m_vecMaxs = pEntity->CollisionProp()->OBBMaxs();
		pEntity->SetSize( mins, maxs );
		change.m_vecMins = mins;
		change.m_vecMaxs = maxs;
	}
	if ( ( pEntity->GetAbsOrigin() - origin ).LengthSqr() > LAG_COMPENSATION_EPS_SQR )
	{
		flags |= LC_ORIGIN_CHANGED;
		restore.m_vecOrigin = pEntity->GetAbsOrigin();
		pEntity->SetAbsOrigin( origin );
		change.m_vecOrigin = origin;
	}
	CBaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	if ( pAnimating && ShouldRewindAnimation( pEntity ) )
	{
		flags |= LC_ANIMATION_CHANGED;
		SaveAnimation( pAnimating, &restore );
		ApplyAnimation( pAnimating, *pRecord, pNewer, fraction );
		if ( sv_lagflushbonecache.GetBool() )
			pAnimating->InvalidateBoneCache();
	}
	if ( !flags )
		return false;
	restore.m_fFlags = change.m_fFlags = flags;
	m_bNeedToRestore = true;
	if ( pAnimating && sv_showlagcompensation.GetInt() == 1 )
		pAnimating->DrawServerHitboxes( 4.0f, true );
	return true;
}

static void RestoreTerrorEntityTo( CBaseEntity *pEntity, const Vector &wanted )
{
	// S:0x180C10 intentionally continues into the validity traces after the
	// force-restore branch; an early return here would change the original.
	if ( sv_lagcompensationforcerestore.GetBool() )
	{
		pEntity->SetAbsOrigin( wanted );
		pEntity->PhysicsTouchTriggers();
	}
	unsigned int mask = ( pEntity->GetTeamNumber() == 2 || pEntity->GetTeamNumber() == 4 ) ? 0x0201420Bu : 0x0202420Bu;
	trace_t trace;
	UTIL_TraceEntity( pEntity, wanted, wanted, mask, pEntity, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
	if ( trace.startsolid || trace.allsolid )
	{
		UTIL_TraceEntity( pEntity, pEntity->GetAbsOrigin(), wanted, mask, pEntity, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
		if ( trace.startsolid || trace.allsolid )
			return;
		pEntity->SetAbsOrigin( Lerp( trace.fraction * g_flFractionScale, pEntity->GetAbsOrigin(), wanted ) );
	}
	else
	{
		pEntity->SetAbsOrigin( trace.endpos );
	}
	pEntity->PhysicsTouchTriggers();
}

void CLagCompensationManager::RestoreEntity( TerrorLagTrack *pTrack )
{
	CBaseEntity *pEntity = pTrack->m_hEntity.Get();
	if ( !pEntity )
		return;
	const TerrorLagRecord &restore = pTrack->m_Restore;
	const TerrorLagRecord &change = pTrack->m_Change;
	if ( ( restore.m_fFlags & LC_SIZE_CHANGED ) && pEntity->CollisionProp()->OBBMins() == change.m_vecMins && pEntity->CollisionProp()->OBBMaxs() == change.m_vecMaxs )
		pEntity->SetSize( restore.m_vecMins, restore.m_vecMaxs );
	if ( ( restore.m_fFlags & LC_ANGLES_CHANGED ) && pEntity->GetAbsAngles() == change.m_vecAngles )
		pEntity->SetAbsAngles( restore.m_vecAngles );
	if ( restore.m_fFlags & LC_ORIGIN_CHANGED )
	{
		Vector delta = pEntity->GetAbsOrigin() - change.m_vecOrigin;
		if ( delta.LengthSqr() < 4096.0f )
			RestoreTerrorEntityTo( pEntity, restore.m_vecOrigin + delta );
	}
	CBaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	if ( ( restore.m_fFlags & LC_ANIMATION_CHANGED ) && pAnimating )
		ApplyAnimation( pAnimating, restore );
	if ( restore.m_fFlags & ( LC_SIZE_CHANGED | LC_ANGLES_CHANGED | LC_ORIGIN_CHANGED | LC_ANIMATION_CHANGED ) )
		pEntity->SetSimulationTime( restore.m_flSimulationTime );
}

void CLagCompensationManager::FinishLagCompensation( CBasePlayer *pPlayer )
{
	m_isCurrentlyDoingCompensation = false;
	if ( m_bNeedToRestore )
	{
		FOR_EACH_MAP( m_Tracks, i )
		{
			if ( m_Tracks[i]->m_bRestore )
				RestoreEntity( m_Tracks[i] );
		}
	}
	m_pCurrentPlayer = NULL;
}
