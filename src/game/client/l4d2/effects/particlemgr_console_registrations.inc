// Append after the native callback bodies in the owner TU; CLIENT_DLL && L4D2 only.
// client:console:1575 object 0x00781680 callback 0x0015bb50
static ConCommand cl_benchmark_particle_system( "cl_benchmark_particle_system", CC_BenchmarkParticleSystem, "Measure simulation time for a named particle system", 0x8 );

// client:console:1593 object 0x00780fc4 callback 0x0015a540
static ConCommand cl_particles_dump_effects( "cl_particles_dump_effects", CC_ParticlesDumpEffects, "", 0x8 );

// client:console:1594 object 0x0078165c callback 0x0015bb20
static ConCommand cl_particles_dumplist( "cl_particles_dumplist", CC_ParticlesDumpList, "Dump all new particles, optional name substring.", 0x8 );
