// Include in c_te_legacytempents.cpp, replacing ONLY the L4D2 PhysicsProp
// bodies. Non-L4D2 retains both original overloads, including the TF helper.
void CTempEnts::PhysicsProp( int modelindex, int skin, const Vector &pos,
	const QAngle &angles, const Vector &velocity, int flags, int effects, int r, int g, int b )
{
	C_PhysPropClientside *entity = C_PhysPropClientside::CreateNew();
	if ( !entity ) return;
	const model_t *model = modelinfo->GetModel( modelindex );
	if ( !model )
	{
		// C210740 deliberately allocates before lookup and leaks this ordinary
		// entity on missing model. Keep that well-defined native bug.
		DevMsg( "CTempEnts::PhysicsProp: model index %i not found\n", modelindex );
		return;
	}
	entity->SetModelName( modelinfo->GetModelName( model ) );
	entity->m_nSkin = skin;
	entity->SetAbsOrigin( pos );
	entity->SetAbsAngles( angles );
	entity->SetPhysicsMode( PHYSICS_MULTIPLAYER_CLIENTSIDE );
	entity->SetEffects( effects );
	entity->SetRenderColor( r, g, b );
	if ( flags & 1 )
	{
		entity->SetModelIndex( modelindex );
		entity->SetCollisionGroup( COLLISION_GROUP_PUSHAWAY );
		entity->SetAbsVelocity( velocity );
		entity->Spawn();
		entity->SetHealth( 0 );
		entity->Break(); // synchronous chunks and Release; no returned pointer
		return;
	}
	if ( entity->Initialize() )
	{
		IPhysicsObject *physics = entity->VPhysicsGetObject();
		if ( physics )
		{
			physics->AddVelocity( &velocity, NULL );
			if ( flags & 2 ) entity->SetBodygroup( 0, RandomInt( 0, entity->GetBodygroupCount( 0 ) - 1 ) );
			return;
		}
	}
	ClientEntityList().AddToDeleteList( entity ); // Native CBB020; preserve actual defer-depth/mark behavior.
}
