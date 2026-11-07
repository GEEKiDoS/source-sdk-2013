// Compose into cdll_client_int.cpp. C0AAC30 / C47E580..47EA85.
extern void UpdateVisibilityAllEntities();
void SystemLevelChanged( IConVar *, const char *, float );
ConVar cpu_level( "cpu_level", "2", 0xa, "CPU Level - Default: High", SystemLevelChanged );
ConVar gpu_mem_level( "gpu_mem_level", "2", 0xa, "Memory Level - Default: High", SystemLevelChanged );
ConVar mem_level( "mem_level", "2", 0xa, "Memory Level - Default: High", SystemLevelChanged );

struct SystemLevelConVar_t { const char *name; bool chooseMinimum; };
static const SystemLevelConVar_t s_SystemLevelConVars[] =
{
	{ "lower_body", true },
	{ "r_shadow_half_update_rate", false },
	{ "r_rainparticledensity", true },
	{ "cl_particle_fallback_base", false },
	{ "cl_particle_fallback_multiplier", false },
	{ "r_flashlightdepthtexture", true },
	{ "r_shadowrendertotexture", true },
	{ "r_shadowfromworldlights", true },
	{ "cl_detaildist", true },
	{ "cl_detailfade", true },
	{ "r_drawmodeldecals", true },
	{ "r_decalstaticprops", true },
	{ "ragdoll_sleepaftertime", true },
	{ "cl_phys_maxticks", true },
	{ "r_worldlightmin", true },
	{ "props_break_max_pieces", true },
	{ "r_worldlights", true },
	{ "r_decals", true },
	{ "r_decal_overlap_count", true },
	{ "mat_bumpmap", true },
	{ "mat_specular", true },
	{ "mat_phong", true },
	{ "mat_grain_enable", true },
	{ "mat_local_contrast_enable", true },
	{ "mat_motion_blur_enabled", true },
	{ "mat_disablehwmorph", false },
	{ "r_overlayfademin", true },
	{ "r_overlayfademax", true },
	{ "z_mob_simple_shadows", false },
	{ "cl_ragdoll_maxcount", true },
	{ "cl_ragdoll_maxcount_gib", true },
	{ "cl_ragdoll_maxcount_generic", true },
	{ "cl_ragdoll_maxcount_special", true },
	{ "sv_ragdoll_maxcount", true },
	{ "sv_ragdoll_maxcount_gib", true },
	{ "sv_ragdoll_maxcount_generic", true },
	{ "sv_ragdoll_maxcount_special", true },
	{ "z_infected_decals", true },
	{ "cl_impacteffects_limit_general", true },
	{ "cl_impacteffects_limit_exit", true },
	{ "cl_impacteffects_limit_water", true },
	{ "cl_ragdoll_self_collision", true },
	{ "cl_player_max_decal_count", true },
	{ "cl_footstep_fx", true },
	{ "mp_usehwmvcds", true },
	{ "mp_usehwmmodels", true },
	{ "mat_depthfeather_enable", true },
	{ "mat_dxlevel", true },
	{ "r_flashlightinfectedfov", true },
	{ "r_flashlightinfectedfar", true },
	{ "r_flashlightinfectedlinear", true },
	{ "r_rootlod", false },
	{ "mat_picmip", false },
	{ "mat_force_vertexfog", false },
	{ "r_shadowmaxrendered", true },
	{ "nb_shadow_blobby_dist", true },
	{ "mat_treesway", true },
	{ "mat_queue_mode", false },
};

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
static void PerformSystemConfiguration( KeyValues *result, const char *levelName, int level )
{
	char filename[MAX_PATH];
	// The target is the supported primary slot: no original _ss filename branch.
	V_snprintf( filename, sizeof( filename ), "cfg\\%s_%d_pc.ekv", levelName, level );
	FileHandle_t file = filesystem->Open( filename, "rb", "GAME" );
	KeyValues *settings = NULL;
	if ( file )
	{
		const int size = filesystem->Size( file );
		CUtlBuffer buffer( 0, size + 1 );
		buffer.EnsureCapacity( size + 1 );
		filesystem->Read( buffer.Base(), size, file );
		static_cast< char * >( buffer.Base() )[size] = 0;
		filesystem->Close( file );
		UTIL_DecodeICE( static_cast< unsigned char * >( buffer.Base() ), size,
			reinterpret_cast< const unsigned char * >( "zp14Hi(]" ) );
		settings = new KeyValues( "kv" );
		if ( !settings->LoadFromBuffer( filename, static_cast< const char * >( buffer.Base() ), filesystem ) )
		{
			settings->deleteThis();
			settings = NULL;
		}
	}
	if ( !settings )
	{
		DevWarning( "PerformSystemConfiguration: Missing %s\n", filename );
		return;
	}
	for ( KeyValues *value = settings->GetFirstSubKey(); value; value = value->GetNextKey() )
	{
		const char *name = value->GetName();
		int i;
		for ( i = 0; i < ARRAYSIZE( s_SystemLevelConVars ); ++i )
			if ( !Q_stricmp( name, s_SystemLevelConVars[i].name ) ) break;
		if ( i == ARRAYSIZE( s_SystemLevelConVars ) )
		{
			DevWarning( "PerformSystemConfiguration: Bad convar found in %s - %s\n", levelName, name );
			continue;
		}
		KeyValues *old = result->FindKey( name );
		const float next = value->GetFloat();
		if ( !old || ( s_SystemLevelConVars[i].chooseMinimum ? !( next >= old->GetFloat() ) : !( old->GetFloat() >= next ) ) )
		{
			if ( !Q_stricmp( name, "mat_dxlevel" ) )
			{
				ConVarRef dxlevel( "mat_dxlevel" );
				if ( next >= dxlevel.GetFloat() ) continue;
			}
			result->SetString( name, value->GetString() );
		}
	}
	settings->deleteThis();
}
void SystemLevelChanged( IConVar *, const char *, float )
{
	ConVarRef gpu( "gpu_level" );
	const int cpuLevel = clamp( cpu_level.GetInt(), 0, 2 );
	const int gpuLevel = clamp( gpu.GetInt(), 0, 3 );
	const int memLevel = clamp( mem_level.GetInt(), 0, 2 );
	const int gpuMemLevel = clamp( gpu_mem_level.GetInt(), 0, 2 );
	KeyValues *settings = new KeyValues( "kv" );
	PerformSystemConfiguration( settings, "cpu_level", cpuLevel );
	PerformSystemConfiguration( settings, "gpu_level", gpuLevel );
	PerformSystemConfiguration( settings, "mem_level", memLevel );
	PerformSystemConfiguration( settings, "gpu_mem_level", gpuMemLevel );
	for ( KeyValues *value = settings->GetFirstSubKey(); value; value = value->GetNextKey() )
	{
		const char *name = value->GetName();
		ConVar *variable = cvar->FindVar( name );
		if ( !variable ) continue;
		bool overridden = false;
		for ( int i = 1; i < CommandLine()->ParmCount(); ++i )
		{
			const char *parameter = CommandLine()->GetParm( i );
			if ( parameter && parameter[0] == '+' && !Q_stricmp( name, parameter + 1 ) )
			{
				overridden = true;
				break;
			}
		}
		if ( overridden )
			Warning( "UpdateSystemLevel: System configuration ignoring %s due to command line override\n", name );
		else if ( variable->IsFlagSet( 0x1004080 ) )
			Warning( "UpdateSystemLevel: ConVar %s controlled by gpu_level/cpu_level must not be marked as FCVAR_ARCHIVE or FCVAR_CHEAT!\n", name );
		else if ( Q_strcmp( name, "mat_queue_mode" ) || value->GetInt() == 0 )
			variable->SetValue( value->GetString() );
	}
	settings->deleteThis();
	// Main-approved gap: IVEngineClient native +672 SetSystemLevel absent.
	UpdateVisibilityAllEntities();
	if ( view ) static_cast< CViewRender * >( view )->OnSystemLevelChanged();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
