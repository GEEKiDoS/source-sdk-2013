//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "physics.h"
#include "physics_shared.h"
#include "vphysics_interface.h"
#include "vphysics/friction.h"
#include "tier0/memdbgon.h"

// C268B60: native standalone SAVE map; neither controller nor entity has an own map.
struct game_shadowcontrol_params_t : public hlshadowcontrol_params_t
{
	DECLARE_SIMPLE_DATADESC();
};

BEGIN_SIMPLE_DATADESC( game_shadowcontrol_params_t )
	DEFINE_FIELD( targetPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( targetRotation, FIELD_VECTOR ),
	DEFINE_FIELD( maxAngular, FIELD_FLOAT ),
	DEFINE_FIELD( maxDampAngular, FIELD_FLOAT ),
	DEFINE_FIELD( maxSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( maxDampSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( dampFactor, FIELD_FLOAT ),
	DEFINE_FIELD( teleportDistance, FIELD_FLOAT ),
END_DATADESC()

// The client has NO attach/update producer. These are precisely its native
// constructor, motion callback, detach, and restore bodies; no server pickup
// policy, physcannon code, or network/prediction/save state is imported.
class CGrabController : public IMotionEvent
{
public:
	CGrabController();
	~CGrabController() { DetachEntity( false ); }
	void DetachEntity( bool clearVelocity );
	void OnRestore() { if ( m_controller ) m_controller->SetEventHandler( this ); }
	virtual simresult_e Simulate( IPhysicsMotionController *, IPhysicsObject *, float, Vector &, AngularImpulse & ) OVERRIDE;
private:
	QAngle m_attachedAnglesPlayerSpace;
	Vector m_attachedPositionObjectSpace;
	game_shadowcontrol_params_t m_shadow;
	float m_timeToArrive;
	float m_errorTime;
	float m_error;
	float m_contactAmount;
	float m_angleAlignment;
	bool m_bCarriedEntityBlocksLOS;
	bool m_bIgnoreRelativePitch;
	float m_flLoadWeight;
	float m_savedRotDamping[1024];
	float m_savedMass[1024];
	EHANDLE m_attachedEntity;
	QAngle m_vecPreferredCarryAngles;
	bool m_bHasPreferredCarryAngles;
	IPhysicsMotionController *m_controller;
};

CGrabController::CGrabController()
{
	// C2689F0: remaining members come from the entity's zero-filled allocation.
	m_shadow.dampFactor = 1.0f;
	m_shadow.teleportDistance = 0.0f;
	m_errorTime = 0.0f;
	m_error = 0.0f;
	m_shadow.maxSpeed = 1000.0f;
	m_shadow.maxAngular = 3600.0f;
	m_shadow.maxDampSpeed = 2000.0f;
	m_shadow.maxDampAngular = 3600.0f;
	m_attachedEntity = NULL;
	m_vecPreferredCarryAngles = vec3_angle;
	m_bHasPreferredCarryAngles = false;
}

#pragma float_control( precise, on, push )
static bool PickupObjectHasHeavyContact( IPhysicsObject *object, float mass )
{
	// C2686E0. The original's unordered mass comparison is false.
	bool heavy = false;
	IPhysicsFrictionSnapshot *snapshot = object->CreateFrictionSnapshot();
	while ( snapshot->IsValid() )
	{
		IPhysicsObject *other = snapshot->GetObject( 1 );
		if ( !other->IsMoveable() || other->GetMass() > mass )
		{
			heavy = true;
			break;
		}
		snapshot->NextFrictionData();
	}
	object->DestroyFrictionSnapshot( snapshot );
	return heavy;
}

IMotionEvent::simresult_e CGrabController::Simulate( IPhysicsMotionController *, IPhysicsObject *object,
	float dt, Vector &linear, AngularImpulse &angular )
{
	// C268770: native supplies TWO global tick intervals, not time-to-arrive.
	game_shadowcontrol_params_t params = m_shadow;
	m_contactAmount = Approach( PickupObjectHasHeavyContact( object, m_flLoadWeight ) ? 0.1f : 1.0f,
		m_contactAmount, dt * 2.0f );
	params.maxAngular = ( ( m_shadow.maxAngular * m_contactAmount ) * m_contactAmount ) * m_contactAmount;
	m_timeToArrive = object->ComputeShadowControl( params, gpGlobals->interval_per_tick * 2.0f, dt );
	Vector velocity;
	AngularImpulse angularVelocity;
	object->GetVelocity( &velocity, &angularVelocity );
	PhysComputeSlideDirection( object, velocity, angularVelocity, &velocity, &angularVelocity, m_flLoadWeight );
	object->SetVelocityInstantaneous( &velocity, NULL );
	linear.Init();
	angular.Init();
	m_errorTime += dt;
	return SIM_LOCAL_ACCELERATION;
}
#pragma float_control( pop )

void CGrabController::DetachEntity( bool clearVelocity )
{
	// C2688B0: unlike the server there is NO else velocity-clamp branch.
	C_BaseEntity *entity = m_attachedEntity.Get();
	if ( entity )
	{
		entity->SetBlocksLOS( m_bCarriedEntityBlocksLOS );
		IPhysicsObject *objects[1024];
		const int count = entity->VPhysicsGetObjectList( objects, ARRAYSIZE( objects ) );
		for ( int i = 0; i < count; ++i )
		{
			IPhysicsObject *object = objects[i];
			if ( !object ) continue;
			object->EnableDrag( true );
			object->Wake();
			object->SetMass( m_savedMass[i] );
			object->SetDamping( NULL, &m_savedRotDamping[i] );
			PhysClearGameFlags( object, FVPHYSICS_PLAYER_HELD );
			if ( clearVelocity ) PhysForceClearVelocity( object );
		}
	}
	m_attachedEntity = NULL;
	if ( physenv ) physenv->DestroyMotionController( m_controller );
	m_controller = NULL;
}

class CPlayerPickupController : public C_BaseEntity
{
public:
	DECLARE_CLASS( CPlayerPickupController, C_BaseEntity );
	virtual void OnRestore() OVERRIDE { m_grabController.OnRestore(); }
	virtual void VPhysicsUpdate( IPhysicsObject * ) OVERRIDE {} // Native C268AF0.
private:
	CGrabController m_grabController;
};
LINK_ENTITY_TO_CLASS( player_pickup, CPlayerPickupController ); // C5118A0.
