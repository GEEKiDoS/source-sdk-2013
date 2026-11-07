// Include once at the end of c_effects.cpp, L4D2 only. Existing class owners stay.
// Particle initializers query these same game-owned trees without changing the
// prebuilt particle library's IParticleSystemQuery ABI.
#pragma float_control( precise, on, push )
static CUtlVector< RayTracingEnvironment * > g_PrecipitationRayEnvironments;

static void ClearPrecipitationRayEnvironments()
{
	g_PrecipitationRayEnvironments.PurgeAndDeleteElements();
}

// Native C15DB30 / C15F510. Unknown names select precipitation, not blockers.
int GetPrecipitationRayEnvironmentIndex( const char *name )
{
	return !Q_stricmp( name, "PRECIPITATIONBLOCKER" ) ? 1 : 0;
}

bool TracePrecipitationRayEnvironment( int index, const FourRays &rays,
	const fltx4 &minimum, const fltx4 &maximum, RayTracingResult *result )
{
	if ( !g_PrecipitationRayEnvironments.IsValidIndex( index ) )
		return false; // Native leaves the result undefined before tree creation.
	g_PrecipitationRayEnvironments[index]->Trace4Rays( rays, minimum, maximum, result, -1, NULL );
	return true;
}

// C1F1E00's default-off Physics2 branch: the ORIGINAL VPhysics1 mesh provider.
// BSP debug-mesh coordinates are supplied directly; native applies no transform.
static void AddPrecipitationMesh( RayTracingEnvironment *environment, C_BaseEntity *entity, int &triangleID )
{
	vcollide_t *collide = modelinfo->GetVCollide( entity->GetModelIndex() );
	if ( !collide || collide->solidCount == 0 )
		return;
	Vector *vertices;
	const int count = physcollision->CreateDebugMesh( collide->solids[0], &vertices );
	for ( int vertex = 0; vertex < count; vertex += 3 )
	{
		if ( vertex + 2 >= count )
			continue; // skip only a native malformed-provider out-of-range triangle
		environment->AddTriangle( triangleID++, vertices[vertex], vertices[vertex + 1], vertices[vertex + 2], Vector( 1, 1, 1 ) );
	}
	physcollision->DestroyDebugMesh( count, vertices );
}

void CClient_Precipitation::InitializeParticlePrecipitation()
{
	switch ( m_nPrecipType )
	{
	case PRECIPITATION_TYPE_PARTICLE_ASH:
		m_flParticleNearDistance = 280.0f;
		m_szParticleNear = m_szParticleScreen = "ash";
		m_szParticleOuter = "ash_outer";
		break;
	case PRECIPITATION_TYPE_PARTICLE_RAINSTORM:
		m_flParticleNearDistance = 0.0f;
		m_szParticleNear = "rain_storm";
		m_szParticleScreen = "rain_storm_screen";
		m_szParticleOuter = "rain_storm_outer";
		break;
	case PRECIPITATION_TYPE_PARTICLE_BUGS:
		m_flParticleNearDistance = 128.0f;
		m_szParticleNear = m_szParticleScreen = "bugs_precipitation";
		m_szParticleOuter = "bugs_precipitation_outer";
		break;
	case PRECIPITATION_TYPE_PARTICLE_SMOKE:
		m_flParticleNearDistance = 32.0f;
		m_szParticleNear = "smoke_precipitation_screen";
		m_szParticleScreen = "smoke_precipitation";
		m_szParticleOuter = "smoke_precipitation_outer";
		break;
	default:
		m_flParticleNearDistance = 180.0f;
		m_szParticleNear = m_szParticleScreen = "rain";
		m_szParticleOuter = "rain_outer";
		break;
	}
	ClearPrecipitationRayEnvironments();
	RayTracingEnvironment *precipitation = new RayTracingEnvironment;
	g_PrecipitationRayEnvironments.AddToTail( precipitation );
	RayTracingEnvironment *blockers = new RayTracingEnvironment;
	g_PrecipitationRayEnvironments.AddToTail( blockers );
	precipitation->Flags |= RTE_FLAGS_DONT_STORE_TRIANGLE_COLORS;
	blockers->Flags |= RTE_FLAGS_DONT_STORE_TRIANGLE_COLORS;
	int triangleID = 1;
	FOR_EACH_VEC( g_Precipitations, index )
		AddPrecipitationMesh( precipitation, g_Precipitations[index], triangleID );
	precipitation->SetupAccelerationStructure();
	triangleID = 1;
	FOR_EACH_VEC( g_PrecipitationBlockers, blocker )
		AddPrecipitationMesh( blockers, g_PrecipitationBlockers[blocker], triangleID );
	blockers->SetupAccelerationStructure();
	m_bParticlePrecipitationInitialized = true;
}

static void StopPrecipitationEffect( HPARTICLEFFECT &effect )
{
	if ( effect )
	{
		effect->StopEmission( false, false, false );
		effect = NULL; // real CUtlReference unlink; not immediate particle destruction
	}
}
void CClient_Precipitation::StopInnerParticlePrecipitation()
{
	StopPrecipitationEffect( m_hParticleScreen ); // C1F05E0 screen BEFORE near.
	StopPrecipitationEffect( m_hParticleNear );
}
void CClient_Precipitation::StopOuterParticlePrecipitation()
{
	StopPrecipitationEffect( m_hParticleOuter );
}
void CClient_Precipitation::CreateOuterParticlePrecipitation( C_BasePlayer *player, const Vector &distance )
{
	StopOuterParticlePrecipitation();
	const Vector density( r_RainParticleDensity.GetFloat() * m_flDensity, m_flDensity * 0.0f, m_flDensity * 0.0f );
	m_hParticleOuter = ParticleProp()->Create( m_szParticleOuter, PATTACH_CUSTOMORIGIN );
	if ( !m_hParticleOuter ) return; // only invalid native NULL effect dereference
	ParticleProp()->AddControlPoint( m_hParticleOuter, 2, player, PATTACH_ABSORIGIN_FOLLOW );
	Vector origin = player->EyePosition();
	origin.z += 180.0f;
	m_hParticleOuter->SetControlPoint( 1, origin );
	m_hParticleOuter->SetControlPoint( 3, density );
	m_hParticleOuter->SetControlPoint( 4, distance );
	// Original draw-only split-screen-user0 property has no target multi-slot consumer.
}
void CClient_Precipitation::CreateFullParticlePrecipitation( C_BasePlayer *player, const Vector &forward, const Vector &distance )
{
	StopInnerParticlePrecipitation();
	StopOuterParticlePrecipitation();
	Vector outerOrigin = player->EyePosition();
	outerOrigin.z += 180.0f;
	Vector nearOrigin = player->EyePosition();
	nearOrigin.z += 180.0f;
	nearOrigin += forward * 32.0f;
	Vector screenOrigin = player->EyePosition();
	screenOrigin.z += 180.0f;
	screenOrigin += forward * m_flParticleNearDistance;
	const Vector density( r_RainParticleDensity.GetFloat() * m_flDensity, m_flDensity * 0.0f, m_flDensity * 0.0f );
	m_hParticleOuter = ParticleProp()->Create( m_szParticleOuter, PATTACH_CUSTOMORIGIN );
	m_hParticleNear = ParticleProp()->Create( m_szParticleNear, PATTACH_CUSTOMORIGIN );
	m_hParticleScreen = ParticleProp()->Create( m_szParticleScreen, PATTACH_CUSTOMORIGIN );
	if ( m_hParticleOuter ) ParticleProp()->AddControlPoint( m_hParticleOuter, 2, player, PATTACH_ABSORIGIN_FOLLOW );
	if ( m_hParticleNear ) ParticleProp()->AddControlPoint( m_hParticleNear, 2, player, PATTACH_ABSORIGIN_FOLLOW );
	if ( m_hParticleScreen ) ParticleProp()->AddControlPoint( m_hParticleScreen, 2, player, PATTACH_ABSORIGIN_FOLLOW );
	if ( m_hParticleOuter ) m_hParticleOuter->SetControlPoint( 1, outerOrigin );
	if ( m_hParticleNear ) m_hParticleNear->SetControlPoint( 1, nearOrigin );
	if ( m_hParticleScreen ) m_hParticleScreen->SetControlPoint( 1, screenOrigin );
	// Native groups all three CP3 writes, then all three CP4 writes.
	if ( m_hParticleNear ) m_hParticleNear->SetControlPoint( 3, density );
	if ( m_hParticleScreen ) m_hParticleScreen->SetControlPoint( 3, density );
	if ( m_hParticleOuter ) m_hParticleOuter->SetControlPoint( 3, density );
	if ( m_hParticleNear ) m_hParticleNear->SetControlPoint( 4, distance );
	if ( m_hParticleScreen ) m_hParticleScreen->SetControlPoint( 4, distance );
	if ( m_hParticleOuter ) m_hParticleOuter->SetControlPoint( 4, distance );
}

void CClient_Precipitation::SimulateParticlePrecipitation()
{
	// C1F2430: r_RainAllowInSplitScreen remains registered. The approved single
	// target slot cannot be a split-screen session, so that native gate is inert.
	if ( !m_bParticlePrecipitationInitialized ) InitializeParticlePrecipitation();
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return;
	if ( !m_bParticleSlotInitialized )
	{
		m_flParticleInterval = 1.0f / 12.0f;
		m_flParticleNextEvent = 0.0f;
		StopInnerParticlePrecipitation();
		StopOuterParticlePrecipitation();
		m_bParticleSlotInitialized = true;
	}
	Vector forward, right;
	player->EyeVectors( &forward, &right, NULL );
	forward.z = 0.0f;
	VectorNormalize( forward );
	Vector leftRay = forward + right;
	Vector rightRay = forward - right;
	VectorNormalize( leftRay );
	VectorNormalize( rightRay );
	float remaining = gpGlobals->frametime;
	while ( !( remaining < m_flParticleNextEvent ) ) // COMISS/JB: unordered exits below explicitly
	{
		// Native COMISS/JB exits on unordered too, unlike an ordinary !(a<b).
		if ( !( remaining >= m_flParticleNextEvent ) ) break;
		remaining -= m_flParticleNextEvent;
		m_flParticleNextEvent = m_flParticleInterval;
		const Vector eye = player->EyePosition();
		Vector outerOrigin = eye;
		outerOrigin.z += 180.0f;
		Vector nearOrigin = outerOrigin + forward * 32.0f;
		Vector screenOrigin = outerOrigin + forward * 100.0f; // native UPDATE uses100, initial creation uses nearDistance
		const Vector density( r_RainParticleDensity.GetFloat() * m_flDensity, m_flDensity * 0.0f, m_flDensity * 0.0f );
		FourRays rays;
		rays.origin.DuplicateVector( eye );
		rays.direction.LoadAndSwizzle( forward, rightRay, leftRay, Vector( 0, 0, 1 ) );
		if ( g_PrecipitationRayEnvironments.Count() == 0 || !g_PrecipitationRayEnvironments[0] )
			continue; // skip only invalid native global-tree read after hack teardown
		RayTracingResult hit;
		g_PrecipitationRayEnvironments[0]->Trace4Rays( rays, _mm_setzero_ps(), _mm_set_ps( FLT_MAX, 800.0f, 800.0f, 800.0f ), &hit, -1, NULL );
		const bool inside = hit.HitIds[3] != -1 && SubFloat( hit.surface_normal.z, 3 ) < 0.0f;
		const bool anyHit = hit.HitIds[0] >= 0 || hit.HitIds[1] >= 0 || hit.HitIds[2] >= 0 || hit.HitIds[3] >= 0;
		const bool nearby = anyHit && _mm_movemask_ps( _mm_cmple_ps( hit.HitDistance, _mm_set1_ps( 384.0f ) ) ) != 0;
		if ( inside || nearby )
		{
			// MINPS operand order is native. Only low lane is used; the vertical
			// fourth ray is deliberately NOT part of this three-ray minimum.
			const fltx4 minimum = _mm_min_ps( _mm_min_ps( hit.HitDistance, _mm_shuffle_ps( hit.HitDistance, hit.HitDistance, 0x39 ) ),
				_mm_shuffle_ps( hit.HitDistance, hit.HitDistance, 0x4E ) );
			const float nearest = SubFloat( minimum, 0 );
			const Vector distance( inside ? 0.0f : nearest, 0, 0 );
			if ( inside || nearest < m_flParticleNearDistance )
			{
				if ( m_hParticleNear && m_hParticleScreen && m_hParticleOuter )
				{
					m_hParticleOuter->SetControlPoint( 1, outerOrigin );
					m_hParticleNear->SetControlPoint( 1, nearOrigin );
					m_hParticleScreen->SetControlPoint( 1, screenOrigin );
					m_hParticleNear->SetControlPoint( 3, density );
					m_hParticleScreen->SetControlPoint( 3, density );
					m_hParticleOuter->SetControlPoint( 3, density );
					m_hParticleNear->SetControlPoint( 4, distance );
					m_hParticleScreen->SetControlPoint( 4, distance );
					m_hParticleOuter->SetControlPoint( 4, distance );
				}
				else CreateFullParticlePrecipitation( player, forward, distance );
			}
			else
			{
				if ( m_hParticleNear ) StopInnerParticlePrecipitation();
				if ( m_hParticleOuter )
				{
					m_hParticleOuter->SetControlPoint( 1, outerOrigin );
					m_hParticleOuter->SetControlPoint( 3, density );
					m_hParticleOuter->SetControlPoint( 4, distance );
				}
				else CreateOuterParticlePrecipitation( player, distance );
			}
		}
		else
		{
			StopInnerParticlePrecipitation();
			StopOuterParticlePrecipitation();
		}
	}
	m_flParticleNextEvent -= remaining;
}

// C1EE5B0: source-player is forwarded from the real single-slot owner once.
void CSnowFallManager::CreateSnowFallParticle( const Vector &spawn, int iSnow, C_BasePlayer *player )
{
	if ( !player || !m_aSnow[iSnow].m_pEntity ) return;
	CClient_Precipitation *entity = m_aSnow[iSnow].m_pEntity;
	Vector start = spawn, end = spawn;
	start.z = player->WorldSpaceCenter().z;
	end.z = entity->WorldAlignMaxs().z;
	if ( IsInPrecipitationBlocker( start ) )
	{
		if ( r_RainDebugDuration.GetBool() ) debugoverlay->AddLineOverlay( start, end, 255, 0, 0, false, r_RainDebugDuration.GetFloat() );
		return;
	}
	trace_t trace;
	UTIL_TraceLine( start, end, 0x400B, NULL, COLLISION_GROUP_NONE, &trace );
	if ( trace.fraction < 1.0f )
	{
		if ( !( trace.surface.flags & SURF_SKY ) )
		{
			if ( r_RainDebugDuration.GetBool() ) debugoverlay->AddLineOverlay( start, trace.endpos, 255, 0, 0, false, r_RainDebugDuration.GetFloat() );
			return;
		}
		end = trace.endpos;
		if ( r_RainDebugDuration.GetBool() ) debugoverlay->AddLineOverlay( start, end, 0, 0, 255, false, r_RainDebugDuration.GetFloat() );
	}
	SnowFallParticle *particle = static_cast< SnowFallParticle * >( m_pSnowFallEmitter->AddParticle( sizeof( SnowFallParticle ), m_aSnow[iSnow].m_hMaterial, spawn ) );
	if ( !particle ) return;
	float minimum = -entity->m_minSpeed, maximum = -entity->m_maxSpeed;
	if ( minimum > maximum ) { const float swap = minimum; minimum = maximum; maximum = swap; }
	particle->m_minSpeed = minimum;
	particle->m_maxSpeed = maximum;
	particle->m_flLifetime = 0.0f;
	particle->m_vecVelocity.x = RandomFloat( -5.0f, 5.0f );
	particle->m_vecVelocity.y = RandomFloat( -5.0f, 5.0f );
	particle->m_vecVelocity.z = RandomFloat( minimum, maximum );
	particle->m_flDieTime = static_cast< float >( fabs( ( static_cast< double >( spawn.z ) - m_aSnow[iSnow].m_vecMin.z ) /
		( static_cast< double >( particle->m_vecVelocity.z ) - 0.1 ) ) );
	// The three native r_SnowColor reads precede these unconditional entity
	// RGB overwrites; no read has effects, so only those dead reads are elided.
	particle->m_uchColor[0] = entity->GetRenderColor().r;
	particle->m_uchColor[1] = entity->GetRenderColor().g;
	particle->m_uchColor[2] = entity->GetRenderColor().b;
	particle->m_uchStartSize = r_SnowStartSize.GetInt();
	particle->m_uchEndSize = r_SnowEndSize.GetInt();
	particle->m_uchStartAlpha = r_SnowStartAlpha.GetInt();
	particle->m_uchEndAlpha = r_SnowEndAlpha.GetInt();
	particle->m_flRoll = random->RandomInt( 0, 360 );
	particle->m_flRollDelta = random->RandomFloat( -0.15f, 0.15f );
	particle->m_iFlags = SIMPLE_PARTICLE_FLAG_WINDBLOWN;
}
#pragma float_control( pop )
