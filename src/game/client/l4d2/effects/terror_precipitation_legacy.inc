// Included once at the end of c_effects.cpp, L4D2 only.
#pragma float_control( precise, on, push )
// C1EBE90. Keep ordered predicates; generic MIN/MAX change NaN polarity.
bool CClient_Precipitation::ComputeEmissionArea( Vector &origin, Vector2D &size, C_BasePlayer *player )
{
	if ( !player ) return false;
	const float emissionSize = r_RainRadius.GetFloat();
	Vector mins = WorldAlignMins(), maxs = WorldAlignMaxs();
	if ( r_RainHack.GetInt() )
	{
		C_World *world = GetClientWorldEntity();
		if ( !world ) return false; // only invalid native world-bounds reads
		mins.x = world->m_WorldMins.x;
		mins.y = world->m_WorldMins.y;
		maxs = world->m_WorldMaxs;
	}
	float height = player->GetAbsOrigin().z + 512.0f;
	if ( height > maxs.z ) height = maxs.z;
	const float fallTime = ( height - player->GetAbsOrigin().z ) / s_rainspeed.GetFloat();
	const float half = emissionSize * 0.5f;
	float lowX = ( player->GetAbsOrigin().x - s_WindVector.x * fallTime ) - half;
	float lowY = ( player->GetAbsOrigin().y - s_WindVector.y * fallTime ) - half;
	float highX = lowX + emissionSize, highY = lowY + emissionSize;
	if ( lowX > maxs.x || lowY > maxs.y || mins.x > highX || mins.y > highY ) return false;
	if ( mins.x > lowX ) lowX = mins.x;
	origin.x = lowX;
	if ( mins.y > lowY ) lowY = mins.y;
	origin.y = lowY;
	origin.z = height;
	if ( highX > maxs.x ) highX = maxs.x;
	if ( highY > maxs.y ) highY = maxs.y;
	size.x = highX - lowX;
	size.y = highY - origin.y;
	return true;
}

// C1F0FE0, single native split-screen slot. Blocker testing is BEFORE the
// wind-corrected skylight trace, and AFTER all four per-attempt RNG draws.
void CClient_Precipitation::EmitParticles( float dt )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return;
	const float playerZ = player->WorldSpaceCenter().z;
	Vector origin;
	Vector2D size;
	if ( !ComputeEmissionArea( origin, size, player ) ) return;
	if ( dt > 0.075f ) dt = 0.075f;
	const double rawDensity = static_cast< double >( m_flDensity ) * 0.001;
	const float densitySpill = static_cast< float >( rawDensity );
	const float density = rawDensity > static_cast< double >( 0.01f ) ? 0.01f : densitySpill;
	const float amount = ( ( size.y * size.x ) * density ) * dt + m_Remainder;
	const int count = _mm_cvttss_si32( _mm_set_ss( amount ) );
	m_Remainder = amount - static_cast< float >( count );
	Vector baseVelocity = s_WindVector;
	baseVelocity.z -= s_rainspeed.GetFloat();
	for ( int i = 0; i < count; ++i )
	{
		Vector velocity = baseVelocity;
		int side = r_RainSideVel.GetInt();
		velocity.x = static_cast< float >( static_cast< double >( random->RandomFloat(
			static_cast< float >( static_cast< int32 >( 0u - static_cast< uint32 >( side ) ) ), static_cast< float >( side ) ) ) + baseVelocity.x );
		side = r_RainSideVel.GetInt();
		velocity.y = static_cast< float >( static_cast< double >( random->RandomFloat(
			static_cast< float >( static_cast< int32 >( 0u - static_cast< uint32 >( side ) ) ), static_cast< float >( side ) ) ) + velocity.y );
		Vector spawn = origin;
		spawn.x = static_cast< float >( static_cast< double >( random->RandomFloat( 0, 1 ) ) * size.x + spawn.x );
		spawn.y = static_cast< float >( static_cast< double >( random->RandomFloat( 0, 1 ) ) * size.y + spawn.y );
		Vector start( spawn.x, spawn.y, playerZ );
		if ( IsInPrecipitationBlocker( start ) )
		{
			if ( r_RainDebugDuration.GetBool() ) debugoverlay->AddLineOverlay( start, spawn, 255, 0, 0, false, r_RainDebugDuration.GetFloat() );
			continue;
		}
		const float deltaZ = spawn.z - playerZ;
		const float inverse = 1.0f / deltaZ;
		start.x += ( velocity.x * inverse ) * deltaZ;
		start.y += ( inverse * velocity.y ) * deltaZ;
		trace_t trace;
		UTIL_TraceLine( start, spawn, 0x400B, NULL, COLLISION_GROUP_NONE, &trace );
		if ( trace.fraction < 1.0f )
		{
			if ( !( trace.surface.flags & SURF_SKY ) )
			{
				if ( r_RainDebugDuration.GetBool() ) debugoverlay->AddLineOverlay( start, trace.endpos, 255, 0, 0, false, r_RainDebugDuration.GetFloat() );
				continue;
			}
			spawn = trace.endpos;
			if ( r_RainDebugDuration.GetBool() ) debugoverlay->AddLineOverlay( start, spawn, 0, 0, 255, false, r_RainDebugDuration.GetFloat() );
		}
		if ( r_RainDebugDuration.GetBool() ) debugoverlay->AddLineOverlay( spawn, start, 0, 255, 0, true, r_RainDebugDuration.GetFloat() );
		CPrecipitationParticle *particle = CreateParticle();
		particle->m_nSplitScreenUser = 0;
		particle->m_Velocity = velocity;
		particle->m_Pos = spawn;
		particle->m_Mass = random->RandomFloat( 0.5f, 1.5f );
		particle->m_flMaxLifetime = static_cast< float >( fabs( ( static_cast< double >( spawn.z ) - playerZ ) / velocity.z ) );
	}
}
#pragma float_control( pop )
