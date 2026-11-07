#include "cbase.h"
#include "l4d2/entities/c_terror_func_brush.h"
#include "physics.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_FuncBrush, DT_FuncBrush, CFuncBrush ) END_RECV_TABLE()
void C_FuncBrush::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	PhysOnDataChanged( this, type == DATA_UPDATE_CREATED );
}

class C_EnvPhysicsBlocker : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_EnvPhysicsBlocker, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // C72790.
	void OnDataChanged( DataUpdateType_t type ) OVERRIDE
	{
		BaseClass::OnDataChanged( type );
		if ( GetMoveType() == MOVETYPE_VPHYSICS )
		{
			if ( type == DATA_UPDATE_CREATED )
			{
				CollisionProp()->SetCollisionBounds( m_vMins, m_vMaxs );
				SetSolid( SOLID_OBB );
				VPhysicsInitStatic();
				SetSolid( SOLID_VPHYSICS );
			}
			if ( m_bBlocked ) RemoveSolidFlags( FSOLID_NOT_SOLID );
			else AddSolidFlags( FSOLID_NOT_SOLID );
			CollisionRulesChanged();
			CollisionProp()->MarkPartitionHandleDirty();
		}
	}
	CollideType_t GetCollideType() OVERRIDE { return IsSolid() ? ENTITY_SHOULD_COLLIDE : ENTITY_SHOULD_NOT_COLLIDE; }
	bool ShouldCollide( int group, int mask ) const OVERRIDE
	{
		if ( group != COLLISION_GROUP_PLAYER_MOVEMENT ) return false;
		switch ( m_nBlockType )
		{
		case 1: return ( mask & 0x00000800 ) != 0;
		case 2: return ( static_cast< unsigned int >( mask ) & 0x80000000u ) != 0;
		case 3: return ( static_cast< unsigned int >( mask ) & 0x80001000u ) != 0;
		default: return ( static_cast< unsigned int >( mask ) & 0x80001800u ) != 0;
		}
	}
private:
	Vector m_vMins, m_vMaxs;
	bool m_bBlocked;
	int m_nBlockType;
};
IMPLEMENT_CLIENTCLASS_DT( C_EnvPhysicsBlocker, DT_PhysicsBlocker, CEnvPhysicsBlocker )
	RecvPropVector( RECVINFO( m_vMins ) ),
	RecvPropVector( RECVINFO( m_vMaxs ) ),
	RecvPropBool( RECVINFO( m_bBlocked ) ),
	RecvPropInt( RECVINFO( m_nBlockType ) ),
END_RECV_TABLE()

class C_FuncPlayerInfectedClip : public C_FuncBrush
{
public:
	DECLARE_CLASS( C_FuncPlayerInfectedClip, C_FuncBrush );
	DECLARE_CLIENTCLASS();
	bool ShouldCollide( int group, int mask ) const OVERRIDE { return group == COLLISION_GROUP_PLAYER_MOVEMENT && ( static_cast< unsigned int >( mask ) & 0x80000000u ) != 0; }
};
IMPLEMENT_CLIENTCLASS_DT( C_FuncPlayerInfectedClip, DT_FuncPlayerInfectedClip, CFuncPlayerInfectedClip ) END_RECV_TABLE()

class C_FuncPlayerGhostInfectedClip : public C_FuncBrush
{
public:
	DECLARE_CLASS( C_FuncPlayerGhostInfectedClip, C_FuncBrush );
	DECLARE_CLIENTCLASS();
	bool ShouldCollide( int group, int mask ) const OVERRIDE { return group == COLLISION_GROUP_PLAYER_MOVEMENT && ( mask & 0x00400000 ) != 0; }
};
IMPLEMENT_CLIENTCLASS_DT( C_FuncPlayerGhostInfectedClip, DT_FuncPlayerGhostInfectedClip, CFuncPlayerGhostInfectedClip ) END_RECV_TABLE()
