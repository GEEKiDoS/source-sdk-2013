// Compose into particlemgr.cpp. Add public void DumpEffects() to CParticleMgr.
// C156BD0 / C15A540.
void CParticleMgr::DumpEffects()
{
	DevMsg( "Particle Effect Systems:\n" );
	for ( unsigned short i = m_Effects.Head(); i != m_Effects.InvalidIndex(); i = m_Effects.Next( i ) )
	{
		CParticleEffectBinding *effect = m_Effects[i];
		DevMsg( "%3d: NumActive: %3d, AutoBBox: %3s \"%s\" \n", i, effect->GetNumActiveParticles(),
			effect->GetAutoUpdateBBox() ? "on" : "off", effect->m_pSim->GetEffectName() );
	}
}
static void CC_ParticlesDumpEffects( const CCommand & ) { ParticleMgr()->DumpEffects(); }
// C36E3B0, expressed through the target library's real dictionary/instance API.
static void CC_ParticlesDumpList( const CCommand &args )
{
	const char *match = args.ArgC() == 2 ? args[1] : NULL;
	if ( match ) DevMsg( "New Particle Systems Matching '%s':\n", match );
	else DevMsg( "All Particle Systems:\n" );
	for ( int i = 0; i < g_pParticleSystemMgr->GetParticleSystemCount(); ++i )
	{
		const char *name = g_pParticleSystemMgr->GetParticleSystemNameFromIndex( i );
		if ( match && !Q_stristr( name, match ) ) continue;
		CParticleSystemDefinition *definition = g_pParticleSystemMgr->FindParticleSystem( name );
		for ( CParticleCollection *collection = definition->FirstCollection(); collection; collection = collection->GetNextCollectionUsingSameDef() )
		{
			Vector minimum, maximum;
			collection->GetBounds( &minimum, &maximum );
			Vector center = ( minimum + maximum ) * 0.5f;
			DevMsg( "%40s: Age: %6.2f, NumActive: %3d, Bounds Center: (%.2f,%.2f,%.2f) (0x%08X)\n",
				name, collection->m_flCurTime, collection->m_nActiveParticles, center.x, center.y, center.z,
				(unsigned int)(uintp)collection );
		}
	}
}
static void CC_BenchmarkParticleSystem( const CCommand &args )
{
	if ( args.ArgC() != 2 )
	{
		Warning( "command is 'cl_benchmark_particle_system <system name>'\n" );
		return;
	}
	CParticleCollection *collection = g_pParticleSystemMgr->CreateParticleCollection( args[1], 0.0f, 0 );
	const float start = Plat_FloatTime();
	// Standing306: a missing named definition returns NULL; skip only simulation.
	if ( collection )
		for ( int i = 10000; i != 0; --i ) collection->Simulate( 0.001f, false );
	Warning( "sim took %f\n", Plat_FloatTime() - start );
	delete collection;
}
