// Transplant into existing src/game/server/l4d2/director_tank_props.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native S25DD80/03EAC0: entity-only traces admitting only CPhysicsProp.
class CTraceFilterPhysics : public ITraceFilter
{
public:
 bool ShouldHitEntity( IHandleEntity *handle, int contentsMask ) OVERRIDE
 {
  if ( !handle ) return false;
  CBaseEntity *entity = EntityFromEntityHandle( handle ); // Existing server helper rejects static props.
  return entity && dynamic_cast< CPhysicsProp * >( entity ) != NULL;
 }
 TraceType_t GetTraceType() const OVERRIDE { return TRACE_ENTITIES_ONLY; }
};

// Native callback 0x00271FD0 (command_function).
static void CC_fade_crosshair( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return;
	Vector forward;
	player->EyeVectors( &forward );
	trace_t trace;
	CTraceFilterPhysics filter;
	UTIL_TraceLine( player->EyePosition(), player->EyePosition() + forward * 57016.32f,
		MASK_ALL, &filter, &trace );
	if ( trace.m_pEnt )
	{
		TheDirector->RegisterTankProp( trace.m_pEnt );
		TheDirector->OnLastTankKilled();
	}
}

// Native object 0x0083DA38; callback 0x00271FD0; command_function.
static ConCommand fade_crosshair_command( "fade_crosshair", CC_fade_crosshair, "Fades the prop being looked at", 0x00004004 );

#endif
