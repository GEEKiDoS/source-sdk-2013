// Native L4D2 brush/blocker entities. External integration source; see integration_packet.txt.
#include "cbase.h"
#include "modelentities.h"
#include "ndebugoverlay.h"
#include "tier0/memdbgon.h"

// S0B8700/S0B8990: these are contents-mask filters, not GetTeamNumber filters.
static bool BlockerShouldCollide( int blockType, int group, unsigned int mask )
{
	if ( group != COLLISION_GROUP_PLAYER_MOVEMENT ) return false;
	switch ( blockType )
	{
	case 1: return ( mask & 0x00000800u ) != 0;
	case 2: return ( mask & 0x80000000u ) != 0;
	case 3: return ( mask & 0x80001000u ) != 0;
	default: return ( mask & 0x80001800u ) != 0;
	}
}

class CEnv_Blocker : public CBaseEntity
{
public:
	DECLARE_CLASS( CEnv_Blocker, CBaseEntity );
	DECLARE_DATADESC();
	void Spawn() OVERRIDE
	{
		SetMoveType( MOVETYPE_NONE );
		SetBlocksLOS( false );
		SetCollisionGroup( COLLISION_GROUP_PLAYER_MOVEMENT );
		SetSolid( m_bBlocked ? SOLID_BBOX : SOLID_NONE );
	}
	bool ShouldCollide( int group, int mask ) const OVERRIDE { return BlockerShouldCollide( m_nBlockType, group, mask ); }
	void InputTurnOn( inputdata_t & ) { m_bBlocked = true; SetSolid( SOLID_BBOX ); }
	void InputTurnOff( inputdata_t & ) { m_bBlocked = false; SetSolid( SOLID_NONE ); }
	int DrawDebugTextOverlays() OVERRIDE
	{
		int offset = BaseClass::DrawDebugTextOverlays();
		if ( m_debugOverlays & OVERLAY_TEXT_BIT )
		{
			static const char *types[] = { "Everyone", "Survivors", "Player Infected", "All Special Infected (Player and AI)", "<UNKNOWN>" };
			const unsigned int type = static_cast< unsigned int >( m_nBlockType );
			EntityText( offset++, CFmtStr( "Initial State: %s", m_bBlocked ? "Enabled" : "Disabled" ), 0 );
			EntityText( offset++, CFmtStr( "BlockType: %s", types[type < 4 ? type : 4] ), 0 );
		}
		NDebugOverlay::BoxAngles( GetAbsOrigin(), CollisionProp()->OBBMins(), CollisionProp()->OBBMaxs(), GetAbsAngles(), 0, 255, 0, m_bBlocked ? 20 : 0, 0.01023f );
		return offset;
	}
private:
	bool m_bBlocked;
	int m_nBlockType;
};
LINK_ENTITY_TO_CLASS( env_player_blocker, CEnv_Blocker );
BEGIN_DATADESC( CEnv_Blocker )
	DEFINE_KEYFIELD( m_bBlocked, FIELD_BOOLEAN, "InitialState" ),
	DEFINE_KEYFIELD( m_nBlockType, FIELD_INTEGER, "BlockType" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputTurnOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputTurnOff ),
END_DATADESC()

class CEnvPhysicsBlocker : public CBaseEntity
{
public:
	DECLARE_CLASS( CEnvPhysicsBlocker, CBaseEntity );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();
	int UpdateTransmitState() OVERRIDE { return SetTransmitState( FL_EDICT_PVSCHECK ); }
	bool ShouldCollide( int group, int mask ) const OVERRIDE { return BlockerShouldCollide( m_nBlockType, group, mask ); }
	void Spawn() OVERRIDE
	{
		SetBlocksLOS( false );
		if ( m_nBlockType != 4 && GetAbsAngles() == vec3_angle )
		{
			SetMoveType( MOVETYPE_NONE );
			SetCollisionGroup( COLLISION_GROUP_PLAYER_MOVEMENT );
			SetSolid( SOLID_BBOX );
		}
		else
		{
			SetMoveType( MOVETYPE_VPHYSICS );
			CollisionProp()->SetCollisionBounds( m_vMins, m_vMaxs );
			SetSolid( SOLID_OBB );
			VPhysicsInitStatic();
			SetSolid( SOLID_VPHYSICS );
		}
		SetBlocksLOS( false );
		if ( m_bBlocked ) RemoveSolidFlags( FSOLID_NOT_SOLID );
		else AddSolidFlags( FSOLID_NOT_SOLID );
	}
	void InputTurnOn( inputdata_t & ) { m_bBlocked = true; RemoveSolidFlags( FSOLID_NOT_SOLID ); }
	void InputTurnOff( inputdata_t & ) { m_bBlocked = false; AddSolidFlags( FSOLID_NOT_SOLID ); }
private:
	CNetworkVector( m_vMins );
	CNetworkVector( m_vMaxs );
	CNetworkVar( int, m_nBlockType );
	CNetworkVar( bool, m_bBlocked );
};
LINK_ENTITY_TO_CLASS( env_physics_blocker, CEnvPhysicsBlocker );
IMPLEMENT_SERVERCLASS_ST( CEnvPhysicsBlocker, DT_PhysicsBlocker )
	SendPropVector( SENDINFO( m_vMins ), -1, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_vMaxs ), -1, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_bBlocked ) ),
	SendPropInt( SENDINFO( m_nBlockType ), 3, SPROP_UNSIGNED ),
END_SEND_TABLE()
BEGIN_DATADESC( CEnvPhysicsBlocker )
	DEFINE_KEYFIELD( m_bBlocked, FIELD_BOOLEAN, "InitialState" ),
	DEFINE_KEYFIELD( m_vMins, FIELD_VECTOR, "boxmins" ),
	DEFINE_KEYFIELD( m_vMaxs, FIELD_VECTOR, "boxmaxs" ),
	DEFINE_KEYFIELD( m_nBlockType, FIELD_INTEGER, "BlockType" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputTurnOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputTurnOff ),
END_DATADESC()

// S0AC860: server-only base construction; only ObjectCaps is overridden.
class CDetailBlocker : public CBaseEntity
{
public:
	DECLARE_CLASS( CDetailBlocker, CBaseEntity );
	CDetailBlocker() : CBaseEntity( true ) {}
	int ObjectCaps() OVERRIDE { return BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }
};
LINK_ENTITY_TO_CLASS( func_detail_blocker, CDetailBlocker );

class CFuncPlayerInfectedClip : public CFuncBrush
{
public:
	DECLARE_CLASS( CFuncPlayerInfectedClip, CFuncBrush );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();
	int UpdateTransmitState() OVERRIDE { return SetTransmitState( FL_EDICT_ALWAYS ); }
	void Spawn() OVERRIDE
	{
		BaseClass::Spawn();
		RemoveSolidFlags( FSOLID_TRIGGER );
		RemoveSolidFlags( FSOLID_NOT_SOLID );
		SetCollisionGroup( COLLISION_GROUP_L4D2_20 );
	}
	bool ShouldCollide( int group, int mask ) const OVERRIDE { return group == COLLISION_GROUP_PLAYER_MOVEMENT && ( static_cast< unsigned int >( mask ) & 0x80000000u ) != 0; }
};
LINK_ENTITY_TO_CLASS( func_playerinfected_clip, CFuncPlayerInfectedClip );
IMPLEMENT_SERVERCLASS_ST( CFuncPlayerInfectedClip, DT_FuncPlayerInfectedClip ) END_SEND_TABLE()
BEGIN_DATADESC( CFuncPlayerInfectedClip ) END_DATADESC()

class CFuncPlayerGhostInfectedClip : public CFuncBrush
{
public:
	DECLARE_CLASS( CFuncPlayerGhostInfectedClip, CFuncBrush );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();
	int UpdateTransmitState() OVERRIDE { return SetTransmitState( FL_EDICT_ALWAYS ); }
	void Spawn() OVERRIDE
	{
		BaseClass::Spawn();
		RemoveSolidFlags( FSOLID_TRIGGER );
		RemoveSolidFlags( FSOLID_NOT_SOLID );
		SetCollisionGroup( COLLISION_GROUP_L4D2_20 );
	}
	bool ShouldCollide( int group, int mask ) const OVERRIDE { return group == COLLISION_GROUP_PLAYER_MOVEMENT && ( mask & 0x00400000 ) != 0; }
};
LINK_ENTITY_TO_CLASS( func_playerghostinfected_clip, CFuncPlayerGhostInfectedClip );
IMPLEMENT_SERVERCLASS_ST( CFuncPlayerGhostInfectedClip, DT_FuncPlayerGhostInfectedClip ) END_SEND_TABLE()
BEGIN_DATADESC( CFuncPlayerGhostInfectedClip ) END_DATADESC()
