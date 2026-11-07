#include "cbase.h"
#include "ragdoll_shared.h"
#include "l4d2/effects/terror_ragdoll_fader.h"
#ifdef CLIENT_DLL
#include "c_baseanimating.h"
#else
#include "physics_prop_ragdoll.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
LINK_ENTITY_TO_CLASS( func_ragdoll_fader, CFuncRagdollFader );
#endif

IMPLEMENT_NETWORKCLASS_ALIASED( FuncRagdollFader, DT_FuncRagdollFader )
BEGIN_NETWORK_TABLE( CFuncRagdollFader, DT_FuncRagdollFader )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_bDisabled ) ),
#else
	SendPropBool( SENDINFO( m_bDisabled ) ),
#endif
END_NETWORK_TABLE()
CFuncRagdollFader::CFuncRagdollFader()
{
	AddEFlags( EFL_USE_PARTITION_WHEN_NOT_SOLID );
	m_bDisabled = false;
}
void CFuncRagdollFader::Spawn()
{
	BaseClass::Spawn();
	SetSolid( SOLID_BSP );
	AddSolidFlags( FSOLID_NOT_SOLID );
	if ( m_bDisabled ) RemoveSolidFlags( FSOLID_TRIGGER | FSOLID_TRIGGER_TOUCH_DEBRIS );
	else AddSolidFlags( FSOLID_TRIGGER | FSOLID_TRIGGER_TOUCH_DEBRIS );
	SetMoveType( MOVETYPE_NONE );
	AddEffects( EF_NODRAW );
#ifndef CLIENT_DLL
	SetModel( STRING( GetModelName() ) );
	SetTransmitState( FL_EDICT_PVSCHECK );
#endif
}
void CFuncRagdollFader::UpdateOnRemove()
{
#ifdef CLIENT_DLL
	s_RagdollLRU.RemoveRagdollFader( this );
#else
	if ( VPhysicsGetObject() ) VPhysicsGetObject()->RemoveTrigger();
	// The native server registry has NO producer (ctor clears; only remove
	// and destructor touch it). Its empty remove is elided, not a no-op API.
#endif
	BaseClass::UpdateOnRemove();
}
#ifdef CLIENT_DLL
BEGIN_DATADESC( CFuncRagdollFader )
	DEFINE_KEYFIELD( m_bDisabled, FIELD_BOOLEAN, "StartDisabled" ),
END_DATADESC()
void CFuncRagdollFader::PostDataUpdate( DataUpdateType_t type )
{
	if ( m_bDisabled ) s_RagdollLRU.RemoveRagdollFader( this );
	else s_RagdollLRU.AddRagdollFader( this );
	BaseClass::PostDataUpdate( type );
}
void CFuncRagdollFader::TestRagdoll( CBaseAnimating *ragdoll )
{
	if ( m_bDisabled || !ragdoll || !ragdoll->GetCollideable() || !GetCollideable() ) return;
	ICollideable *other = ragdoll->GetCollideable();
	const Vector origin = other->GetCollisionOrigin();
	Vector mins, maxs;
	if ( other->GetSolid() == SOLID_BBOX )
	{
		mins = other->OBBMins();
		maxs = other->OBBMaxs();
	}
	else
	{
		other->WorldSpaceSurroundingBounds( &mins, &maxs );
		mins -= origin;
		maxs -= origin;
	}
	Ray_t ray;
	ray.Init( origin, origin, mins, maxs );
	trace_t trace;
	enginetrace->ClipRayToCollideable( ray, 0x0200400B, GetCollideable(), &trace );
	if ( trace.contents & 0x0200400B )
	{
		C_ClientRagdoll *clientRagdoll = dynamic_cast< C_ClientRagdoll * >( ragdoll );
		if ( clientRagdoll ) clientRagdoll->SUB_Remove();
	}
}
#else
LINK_ENTITY_TO_CLASS( func_ragdoll_fader, CFuncRagdollFader );
BEGIN_DATADESC( CFuncRagdollFader )
	DEFINE_KEYFIELD( m_bDisabled, FIELD_BOOLEAN, "StartDisabled" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
END_DATADESC()
void CFuncRagdollFader::StartTouch( CBaseEntity *other )
{
	if ( !m_bDisabled )
	{
		CRagdollProp *ragdoll = dynamic_cast< CRagdollProp * >( other );
		if ( ragdoll ) ragdoll->FadeOut( 0.0f, -1.0f );
	}
	BaseClass::StartTouch( other );
}
void CFuncRagdollFader::InputEnable( inputdata_t & )
{
	m_bDisabled = false;
	if ( VPhysicsGetObject() ) VPhysicsGetObject()->EnableCollisions( true );
	if ( !IsSolidFlagSet( FSOLID_TRIGGER ) )
	{
		AddSolidFlags( FSOLID_TRIGGER | FSOLID_TRIGGER_TOUCH_DEBRIS );
		PhysicsTouchTriggers();
	}
}
void CFuncRagdollFader::InputDisable( inputdata_t & )
{
	m_bDisabled = true;
	if ( VPhysicsGetObject() ) VPhysicsGetObject()->EnableCollisions( false );
	if ( IsSolidFlagSet( FSOLID_TRIGGER ) )
	{
		RemoveSolidFlags( FSOLID_TRIGGER | FSOLID_TRIGGER_TOUCH_DEBRIS );
		PhysicsTouchTriggers();
	}
}
#endif
