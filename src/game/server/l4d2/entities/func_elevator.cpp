#include "cbase.h"
#include "l4d2/entities/func_elevator.h"
#include "l4d2/entities/func_elevator_shared.h"
#include "entitylist.h"
#include "recipientfilter.h"
#include "l4d2/player/terrorplayer.h"
#include "vphysics_interface.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( func_elevator, CFuncElevator );
LINK_ENTITY_TO_CLASS( info_elevator_floor, CInfoElevatorFloor );

IMPLEMENT_SERVERCLASS_ST( CFuncElevator, DT_FuncElevator )
	SendPropExclude( "DT_BaseEntity", "m_vecOrigin" ),
	SendPropVector( SENDINFO( m_vecOrigin ), -1, SPROP_NOSCALE | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_Origin ),
	SendPropFloat( SENDINFO( m_acceleration ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_movementStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_movementStartSpeed ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_movementStartZ ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_destinationFloorPosition ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_maxSpeed ), 0, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_isMoving ) ),
END_SEND_TABLE()

BEGIN_DATADESC( CInfoElevatorFloor )
	DEFINE_OUTPUT( m_OnReachedFloor, "OnReachedFloor" ),
END_DATADESC()

BEGIN_DATADESC( CFuncElevator )
	DEFINE_KEYFIELD( m_topFloorPosition, FIELD_POSITION_VECTOR, "top" ),
	DEFINE_KEYFIELD( m_bottomFloorPosition, FIELD_POSITION_VECTOR, "bottom" ),
	DEFINE_KEYFIELD( m_maxSpeed, FIELD_FLOAT, "speed" ),
	DEFINE_KEYFIELD( m_acceleration, FIELD_FLOAT, "acceleration" ),
	DEFINE_KEYFIELD( m_soundStart, FIELD_SOUNDNAME, "StartSound" ),
	DEFINE_KEYFIELD( m_soundStop, FIELD_SOUNDNAME, "StopSound" ),
	DEFINE_KEYFIELD( m_soundDisable, FIELD_SOUNDNAME, "DisableSound" ),
	DEFINE_FIELD( m_currentSound, FIELD_SOUNDNAME ),
	DEFINE_KEYFIELD( m_flBlockDamage, FIELD_FLOAT, "BlockDamage" ),
	DEFINE_INPUTFUNC( FIELD_STRING, "MoveToFloor", InputMoveToFloor ),
	DEFINE_INPUTFUNC( FIELD_STRING, "Disable", InputDisable ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetMaxSpeed", InputSetMaxSpeed ),
	DEFINE_OUTPUT( m_OnReachedTop, "OnReachedTop" ),
	DEFINE_OUTPUT( m_OnReachedBottom, "OnReachedBottom" ),
	DEFINE_THINKFUNC( StopMoveSoundThink ),
END_DATADESC()

static ConVar elevator_movedelay( "elevator_movedelay", "0.75", 0x00000006, "Time delay for elevator movement start." );
static CUtlVector< CFuncElevator * > s_elevators;

int GetFuncElevatorCount()
{
	return s_elevators.Count();
}

CFuncElevator *GetFuncElevator( int index )
{
	return s_elevators.IsValidIndex( index ) ? s_elevators[index] : NULL;
}

CFuncElevator::CFuncElevator()
{
	s_elevators.AddToTail( this );
}

CFuncElevator::~CFuncElevator()
{
	s_elevators.FindAndRemove( this );
}

void CFuncElevator::Precache()
{
	if ( m_soundStart != NULL_STRING ) PrecacheScriptSound( STRING( m_soundStart ) );
	if ( m_soundStop != NULL_STRING ) PrecacheScriptSound( STRING( m_soundStop ) );
	if ( m_soundDisable != NULL_STRING ) PrecacheScriptSound( STRING( m_soundDisable ) );
	m_currentSound = NULL_STRING;
}

bool CFuncElevator::CreateVPhysics()
{
	VPhysicsInitShadow( false, false );
	return true;
}

int CFuncElevator::FloorCompare( const FloorInfo *a, const FloorInfo *b )
{
	// The original comparator is a boolean, not a corrected three-way comparison.
	return a->height > b->height;
}

static CBaseEntityOutput *ElevatorOutput( CBaseEntity *entity, const typedescription_t &field )
{
	return reinterpret_cast< CBaseEntityOutput * >( reinterpret_cast< byte * >( entity ) + field.fieldOffset[TD_OFFSET_NORMAL] );
}

static CBaseEntity *FindElevatorOutputSource( CBaseEntity *after, string_t target )
{
	for ( CBaseEntity *entity = after ? gEntList.NextEnt( after ) : gEntList.FirstEnt(); entity; entity = gEntList.NextEnt( entity ) )
	{
		for ( datamap_t *map = entity->GetDataDescMap(); map; map = map->baseMap )
		{
			for ( int i = 0; i < map->dataNumFields; ++i )
			{
				const typedescription_t &field = map->dataDesc[i];
				if ( field.fieldType == FIELD_CUSTOM && ( field.flags & FTYPEDESC_OUTPUT ) != 0 &&
					 ElevatorOutput( entity, field )->GetActionForTarget( target ) )
					return entity;
			}
		}
	}
	return NULL;
}

void CFuncElevator::BuildFloorList()
{
	m_floors.RemoveAll();
	FloorInfo floor;
	// Native height slots are Vector.z (top +8, bottom +8).
	floor.height = m_bottomFloorPosition.z;
	floor.name = AllocPooledString( "bottom" );
	floor.button = NULL;
	m_floors.AddToTail( floor );
	floor.height = m_topFloorPosition.z;
	floor.name = AllocPooledString( "top" );
	m_floors.AddToTail( floor );

	CBaseEntity *source = NULL;
	while ( ( source = FindElevatorOutputSource( source, GetEntityName() ) ) != NULL )
	{
		bool found = false;
		for ( datamap_t *map = source->GetDataDescMap(); map && !found; map = map->baseMap )
		{
			for ( int i = 0; i < map->dataNumFields; ++i )
			{
				const typedescription_t &field = map->dataDesc[i];
				if ( field.fieldType != FIELD_CUSTOM || !( field.flags & FTYPEDESC_OUTPUT ) ) continue;
				const CEventAction *action = ElevatorOutput( source, field )->GetActionForTarget( GetEntityName() );
				if ( !action || Q_stricmp( STRING( action->m_iTargetInput ), "MoveToFloor" ) || action->m_iParameter == NULL_STRING ) continue;
				CBaseEntity *button = source->ClassMatches( "func_button*" ) ? source : NULL;
				int index;
				for ( index = 0; index < m_floors.Count(); ++index )
					if ( m_floors[index].name == action->m_iParameter ) break;
				if ( index < m_floors.Count() )
				{
					if ( button ) m_floors[index].button = button;
				}
				else
				{
					CBaseEntity *destination = gEntList.FindEntityByName( NULL, STRING( action->m_iParameter ) );
					if ( destination )
					{
						floor.height = destination->GetAbsOrigin().z;
						floor.name = action->m_iParameter;
						floor.button = button;
						m_floors.AddToTail( floor );
					}
				}
				found = true;
				break;
			}
		}
	}
	m_floors.Sort( FloorCompare );
}

void CFuncElevator::Spawn()
{
	SetMoveType( MOVETYPE_CUSTOM );
	SetModel( STRING( GetModelName() ) );
	SetTouch( NULL );
	Precache();
	AddFlag( FL_CONVEYOR );
	SetSolid( SOLID_BSP );
	if ( m_acceleration == 0.0f ) m_acceleration = m_maxSpeed;
	m_enabled = true;
	BuildFloorList();
	m_movementStartTime = gpGlobals->curtime;
	m_stopTime = -1.0f;
	m_movementStartSpeed = m_currentSpeed;
	m_movementStartZ = GetAbsOrigin().z;
	m_destinationFloorPosition = GetAbsOrigin().z;
	SetThink( NULL );
	m_destinationFloor = NULL;
	m_isMoving = false;
	CreateVPhysics();
}

const FloorInfo *CFuncElevator::GetFloor( int index ) const
{
	return m_floors.IsValidIndex( index ) ? &m_floors[index] : NULL;
}

int CFuncElevator::GetCurrentFloor() const
{
	for ( int i = 0; i < m_floors.Count(); ++i )
		if ( fabs( GetAbsOrigin().z - m_floors[i].height ) < 0.5f ) return i;
	return -1;
}

int CFuncElevator::GetDestinationFloor() const
{
	if ( m_currentSpeed != 0.0f )
		for ( int i = 0; i < m_floors.Count(); ++i )
			if ( fabs( m_destinationFloorPosition - m_floors[i].height ) < 0.5f ) return i;
	return GetCurrentFloor();
}

int CFuncElevator::GetNearestFloor( float z ) const
{
	float nearest = 100.0f;
	int result = -1;
	for ( int i = 0; i < m_floors.Count(); ++i )
	{
		const double distance = fabs( m_floors[i].height - z );
		if ( nearest > distance ) { nearest = distance; result = i; }
	}
	return result;
}

EHANDLE CFuncElevator::GetNearbyControlButton() const
{
	const FloorInfo *current = GetFloor( GetCurrentFloor() );
	EHANDLE result;
	if ( !current ) return result;
	float nearest = 100.0f;
	for ( int i = 0; i < m_floors.Count(); ++i )
	{
		const FloorInfo *floor = GetFloor( i );
		CBaseEntity *button = floor->button.Get();
		if ( floor == current || !button ) continue;
		const double distance = fabs( button->WorldSpaceCenter().z - current->height );
		if ( nearest > distance ) { nearest = distance; result = button; }
	}
	return result;
}

bool CFuncElevator::HasEntity( CBaseEntity *entity ) const
{
	if ( !entity->IsAlive() ) return false;
	CBaseEntity *ground = entity->GetGroundEntity();
	if ( !ground ) return false;
	if ( ground == this ) return true;
	// Original100E3EE0 repeatedly queries the SAME ground entity, not its
	// successive ancestors.  Only these immediate relationships are accepted.
	if ( ground->GetMoveParent() == this ) return true;
	return ground->GetOwnerEntity() == this;
}

void CFuncElevator::CollectOccupants( CUtlVector< CBasePlayer * > *occupants, int team ) const
{
	Vector mins, maxs;
	CollisionProp()->WorldSpaceAABB( &mins, &maxs );
	maxs.z += 71.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() ) continue;
		if ( team != TEAM_ANY && player->GetTeamNumber() != team ) continue;
		const Vector &origin = player->GetAbsOrigin();
		if ( HasEntity( player ) || ( origin.x >= mins.x && maxs.x >= origin.x &&
			origin.y >= mins.y && maxs.y >= origin.y && origin.z >= mins.z && maxs.z >= origin.z ) )
			occupants->AddToTail( player );
	}
}

double CFuncElevator::CalcPosition( const char *context, float time, float *speed ) const
{
	*speed = 0.0f;
	if ( m_isMoving && gpGlobals->curtime >= m_movementStartTime )
		return ComputeElevatorPosition( time - m_movementStartTime, m_movementStartZ,
			m_movementStartSpeed, m_destinationFloorPosition, m_maxSpeed, m_acceleration, speed );
	return GetAbsOrigin().z;
}

double CFuncElevator::UpdateCurrentPosition( const char *context )
{
	return CalcPosition( context, gpGlobals->curtime, &m_currentSpeed );
}

void CFuncElevator::EmitMoveSound( string_t sound )
{
	CPASAttenuationFilter filter( this, 0.8f );
	EmitSound_t params;
	params.m_nChannel = CHAN_STATIC;
	params.m_pSoundName = STRING( sound );
	params.m_flVolume = 1.0f;
	params.m_SoundLevel = SNDLVL_75dB;
	EmitSound( filter, entindex(), params );
}

void CFuncElevator::MoveTo( float z )
{
	if ( !m_enabled ) return;
	m_isMoving = true;
	m_movementStartTime = elevator_movedelay.GetFloat() + gpGlobals->curtime;
	m_stopTime = -1.0f;
	m_movementStartSpeed = m_currentSpeed;
	m_movementStartZ = GetAbsOrigin().z;
	m_destinationFloorPosition = z;
	if ( m_soundStart != NULL_STRING )
	{
		if ( m_currentSound == m_soundStart )
			StopSound( entindex(), CHAN_STATIC, STRING( m_soundStop ) );
		else
		{
			m_currentSound = m_soundStart;
			EmitMoveSound( m_soundStart );
		}
	}
	Vector mins, maxs;
	CollisionProp()->WorldSpaceAABB( &mins, &maxs );
	mins.z -= 71.0f;
	maxs.z += 71.0f;
	CBaseEntity *entities[128];
	const int count = UTIL_EntitiesInBox( entities, ARRAYSIZE( entities ), mins, maxs, 0 );
	for ( int i = 0; i < count; ++i )
		if ( entities[i]->ClassMatches( "prop_physics" ) ) UTIL_Remove( entities[i] );
	SetThink( NULL );
}

void CFuncElevator::InputMoveToFloor( inputdata_t &inputdata )
{
	if ( !m_enabled ) return;
	const char *name = inputdata.value.String();
	m_destinationFloor = NULL;
	if ( !Q_stricmp( name, "top" ) ) MoveTo( m_topFloorPosition.z );
	else if ( !Q_stricmp( name, "bottom" ) ) MoveTo( m_bottomFloorPosition.z );
	else
	{
		CBaseEntity *floor = gEntList.FindEntityByName( NULL, name );
		if ( floor ) { m_destinationFloor = floor; MoveTo( floor->GetAbsOrigin().z ); }
		else Warning( "Elevator tried to move to bad floor '%s'\n", name );
	}
}

void CFuncElevator::InputDisable( inputdata_t &inputdata )
{
	if ( !m_enabled ) return;
	if ( m_currentSpeed != 0.0f ) MoveTo( GetAbsOrigin().z );
	SetThink( &CFuncElevator::StopMoveSoundThink );
	SetNextThink( gpGlobals->curtime + 0.1f );
	m_enabled = false;
}

void CFuncElevator::InputSetMaxSpeed( inputdata_t &inputdata )
{
	if ( !m_isMoving ) m_maxSpeed = inputdata.value.Float();
}

void CFuncElevator::StopMoveSoundThink()
{
	const string_t sound = m_enabled ? m_soundStop : m_soundDisable;
	if ( m_currentSound != NULL_STRING && m_currentSound != sound )
		StopSound( entindex(), CHAN_STATIC, STRING( m_currentSound ) );
	if ( sound != NULL_STRING && m_currentSound != sound )
	{
		m_currentSound = sound;
		EmitMoveSound( sound );
	}
	SetThink( NULL );
}

void CFuncElevator::MoveDone()
{
	SetContextThink( NULL, TICK_NEVER_THINK, "AccelerationContext" );
	m_currentSpeed = 0.0f;
	m_isMoving = false;
	SetThink( &CFuncElevator::StopMoveSoundThink );
	SetNextThink( gpGlobals->curtime + 0.1f );
	BaseClass::MoveDone();
	if ( GetAbsOrigin().z >= m_topFloorPosition.z ) m_OnReachedTop.FireOutput( this, this );
	else if ( m_bottomFloorPosition.z >= GetAbsOrigin().z ) m_OnReachedBottom.FireOutput( this, this );
	else
	{
		CInfoElevatorFloor *floor = dynamic_cast< CInfoElevatorFloor * >( m_destinationFloor.Get() );
		if ( floor ) floor->m_OnReachedFloor.FireOutput( this, this );
	}
}

void CFuncElevator::Blocked( CBaseEntity *other )
{
	if ( m_flBlockDamage != 0.0f &&
		( ( other->GetTeamNumber() != TEAM_SURVIVOR && other->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) || !GetTouchTrace().DidHitNonWorldEntity() ) )
	{
		CTakeDamageInfo damage( this, this, m_flBlockDamage, DMG_CRUSH );
		other->TakeDamage( damage );
	}
}

class CMovableEnum : public CFlaggedEntitiesEnum
{
public:
	CMovableEnum( CBaseEntity **list, int count ) : CFlaggedEntitiesEnum( list, count, 0 ) {}
	virtual IterationRetval_t EnumElement( IHandleEntity *handle ) OVERRIDE
	{
		CBaseEntity *entity = EntityFromEntityHandle( handle );
		if ( entity && ( entity->IsPlayer() || entity->MyNextBotPointer() ) )
			return CFlaggedEntitiesEnum::EnumElement( handle );
		return ITERATION_CONTINUE;
	}
};

class CTraceFilterIgnoreMoveableObjects : public CTraceFilterSimple
{
public:
	CTraceFilterIgnoreMoveableObjects( CBaseEntity *entity ) : CTraceFilterSimple( entity, COLLISION_GROUP_PLAYER_MOVEMENT ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE
	{
		if ( !CTraceFilterSimple::ShouldHitEntity( handle, mask ) ) return false;
		CBaseEntity *entity = EntityFromEntityHandle( handle );
		IPhysicsObject *physics = entity->VPhysicsGetObject();
		return ( physics && !physics->IsMoveable() ) || entity->GetMoveType() == MOVETYPE_PUSH;
	}
};

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CFuncElevator::PhysicsSimulate()
{
	if ( m_nSimulationTick == gpGlobals->tickcount ) return;
	m_nSimulationTick = gpGlobals->tickcount;
	PhysicsRunThink();
	if ( !m_isMoving ) { m_currentSpeed = 0.0f; return; }
	Vector mins, maxs;
	CollisionProp()->WorldSpaceAABB( &mins, &maxs );
	mins.z -= 71.0f;
	maxs.z += 71.0f;
	CBaseEntity *entities[128];
	CMovableEnum collector( entities, ARRAYSIZE( entities ) );
	UTIL_EntitiesInBox( mins, maxs, &collector );
	const Vector oldOrigin = GetAbsOrigin();
	Vector origin = oldOrigin;
	const double currentZ = UpdateCurrentPosition( "CFuncElevator::PhysicsSimulate" );
	origin.z = currentZ;
	// S0E4133 stores origin, then subtracts from the still-wide curve result.
	float deltaZ = currentZ - static_cast< double >( oldOrigin.z );
	if ( m_movementStartZ == m_destinationFloorPosition ||
		( m_movementStartZ > m_destinationFloorPosition && m_destinationFloorPosition >= origin.z ) ||
		( m_destinationFloorPosition > m_movementStartZ && origin.z >= m_destinationFloorPosition ) )
	{
		if ( m_stopTime == -1.0f ) m_stopTime = gpGlobals->curtime;
		deltaZ = m_destinationFloorPosition - oldOrigin.z;
		origin.z = m_destinationFloorPosition;
	}
	bool done = false;
	if ( m_stopTime != -1.0f && gpGlobals->curtime >= elevator_movedelay.GetFloat() + m_stopTime )
	{
		m_stopTime = -1.0f;
		done = true;
	}
	SetAbsOrigin( origin );
	PhysicsTouchTriggers( &oldOrigin );
	UpdatePhysicsShadowToCurrentPosition( gpGlobals->frametime );
	if ( done ) MoveDone();
	for ( int i = 0; i < collector.GetCount(); ++i )
	{
		CBaseEntity *entity = entities[i];
		if ( entity->IsPlayer() ) continue;
		const Vector oldEntityOrigin = entity->GetAbsOrigin();
		Vector candidate = oldEntityOrigin;
		candidate.z += deltaZ;
		CTraceFilterIgnoreMoveableObjects filter( entity );
		trace_t trace;
		UTIL_TraceEntity( entity, candidate, candidate, entity->PhysicsSolidMaskForEntity(), &filter, &trace );
		if ( trace.fraction == 1.0f )
		{
			entity->SetAbsOrigin( candidate );
			entity->PhysicsTouchTriggers( &oldEntityOrigin );
			continue;
		}
		Vector alternate = candidate;
		alternate.x += 8.0f; alternate.y += 8.0f;
		UTIL_TraceEntity( entity, alternate, alternate, entity->PhysicsSolidMaskForEntity(), &filter, &trace );
		if ( trace.fraction == 1.0f ) continue;
		alternate = candidate; alternate.x -= 8.0f; alternate.y -= 8.0f;
		UTIL_TraceEntity( entity, alternate, alternate, entity->PhysicsSolidMaskForEntity(), &filter, &trace );
		if ( trace.fraction == 1.0f ) continue;
		alternate = candidate; alternate.z -= deltaZ * 2.0f;
		UTIL_TraceEntity( entity, alternate, alternate, entity->PhysicsSolidMaskForEntity(), &filter, &trace );
		// Native100E4010 moves on the final FAILED trace, not on a clear trace.
		if ( trace.fraction != 1.0f )
		{
			entity->SetAbsOrigin( alternate );
			entity->PhysicsTouchTriggers( &oldEntityOrigin );
		}
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

int CFuncElevator::DrawDebugTextOverlays()
{
	int line = BaseClass::DrawDebugTextOverlays();
	if ( !( m_debugOverlays & OVERLAY_TEXT_BIT ) ) return line;
	char text[512];
	const int index = m_currentSpeed == 0.0f ? GetCurrentFloor() : GetDestinationFloor();
	const FloorInfo *floor = GetFloor( index );
	const char *name = floor ? STRING( floor->name ) : "unknown";
	if ( m_currentSpeed == 0.0f ) Q_snprintf( text, sizeof( text ), "Currently at floor %d(%s)", index, name );
	else Q_snprintf( text, sizeof( text ), "Moving at speed %f to floor %d(%s)", m_currentSpeed, index, name );
	EntityText( line++, text, 0.0f );
	CBaseEntity *button = GetNearbyControlButton().Get();
	if ( button ) Q_snprintf( text, sizeof( text ), "Nearby button is %s", STRING( button->GetEntityName() ) );
	else Q_strncpy( text, "No nearby buttons", sizeof( text ) );
	EntityText( line++, text, 0.0f );
	for ( int i = 0; i < m_floors.Count(); ++i )
	{
		const FloorInfo *nearest = GetFloor( GetNearestFloor( m_floors[i].height ) );
		CBaseEntity *trigger = nearest ? nearest->button.Get() : NULL;
		Q_snprintf( text, sizeof( text ), "Floor %s is at %f, triggered by %s", STRING( m_floors[i].name ),
			m_floors[i].height, trigger ? STRING( trigger->GetEntityName() ) : "<none>" );
		EntityText( line++, text, 0.0f );
	}
	CUtlVector< CBasePlayer * > occupants;
	CollectOccupants( &occupants );
	for ( int i = 0; i < occupants.Count(); ++i )
	{
		Q_snprintf( text, sizeof( text ), "Occupant %d: %s", i, occupants[i]->GetPlayerName() );
		EntityText( line++, text, 0.0f );
	}
	return line;
}
