// Included ONCE by props.cpp after CPropDoorRotating's complete implementation.
// Dependencies use the existing rotating-door, physics, response and NAV owners.

class CPropDoorRotatingBreakable : public CPropDoorRotating
{
public:
	DECLARE_CLASS( CPropDoorRotatingBreakable, CPropDoorRotating );
	DECLARE_DATADESC();
	void Spawn() OVERRIDE;
	void Precache() OVERRIDE { CBreakableProp::Precache(); }
	int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;
	void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE;
	bool ShouldCloseAreaPortals() const OVERRIDE { return m_bAllowPortalClosing; }
	int DrawDebugTextOverlays() OVERRIDE
	{
		int offset = BaseClass::DrawDebugTextOverlays();
		if ( m_debugOverlays & OVERLAY_TEXT_BIT ) EntityText( offset++, m_bBreakable ? "DOOR IS BREAKABLE" : "DOOR IS NOT BREAKABLE", 0.0f );
		return offset;
	}
	void OnDoorClosed() OVERRIDE { BaseClass::OnDoorClosed(); (void)IsCheckpointDoor(); }
	void OnDoorOpened() OVERRIDE { BaseClass::OnDoorOpened(); (void)IsCheckpointDoor(); }
	void InputSetUnbreakable( inputdata_t & ) { m_bBreakable = false; }
	void InputSetBreakable( inputdata_t & ) { m_bBreakable = true; }
private:
	void ReadDamageModels( bool precacheBreakables, bool keepNames );
	void SetDamageModel( int index, bool rebuildPhysics );
	void EmitDamageModelGibs( IRecipientFilter &filter, const Vector &origin, const Vector &velocity );
	bool m_bBreakable; // native6084; no datamap/save/wire descriptors
	bool m_bAllowPortalClosing; // native6085
	int m_iDamageStage; // native6088
	CUtlVector< string_t > m_DamageModels; // native6092
};
LINK_ENTITY_TO_CLASS( prop_door_rotating, CPropDoorRotatingBreakable );
BEGIN_DATADESC( CPropDoorRotatingBreakable )
	DEFINE_INPUTFUNC( FIELD_VOID, "SetUnbreakable", InputSetUnbreakable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "SetBreakable", InputSetBreakable ),
END_DATADESC()
void CPropDoorRotatingBreakable::ReadDamageModels( bool precacheBreakables, bool keepNames )
{
	KeyValues *keys = new KeyValues( "" );
	const char *name = STRING( GetModelName() );
	if ( keys->LoadFromBuffer( name, modelinfo->GetModelKeyValueText( GetModel() ) ) )
	{
		KeyValues *options = keys->FindKey( "door_options" );
		if ( options )
		{
			KeyValues *skin = options->FindKey( CFmtStr( "skin%d", m_nSkin.Get() ) );
			if ( !skin ) skin = options->FindKey( "defaults" );
			if ( skin )
			{
				for ( int stage = 1; ; ++stage )
				{
					const char *model = skin->GetString( CFmtStr( "damage%d", stage ), NULL );
					if ( !model ) break;
					char path[80];
					Q_snprintf( path, sizeof( path ), "models/%s.mdl", model );
					V_FixSlashes( path, '/' );
					if ( precacheBreakables ) PropBreakablePrecacheAll( AllocPooledString( path ) );
					else PrecacheModel( path );
					if ( keepNames ) m_DamageModels.AddToTail( AllocPooledString( model ) );
				}
			}
		}
		else if ( precacheBreakables ) DevMsg( "Breakable door %s has no door_options\n", name );
	}
	else if ( precacheBreakables ) DevMsg( "Breakable door %s has no KeyValues\n", name );
	keys->deleteThis();
}
void CPropDoorRotatingBreakable::Spawn()
{
	m_bAllowPortalClosing = true;
	BaseClass::Spawn();
	ReadDamageModels( true, false );
	m_DamageModels.RemoveAll();
	m_iDamageStage = -1;
	ReadDamageModels( false, true );
	m_bBreakable = !HasSpawnFlags( 0x80000 );
	if ( IsDoorClosed() ) (void)IsCheckpointDoor(); // native discarded virtual query
}
void CPropDoorRotatingBreakable::EmitDamageModelGibs( IRecipientFilter &filter, const Vector &origin, const Vector &velocity )
{
	const color32 color = GetRenderColor();
	TE_PhysicsProp( filter, -1.0f, GetModelIndex(), GetSkin(), origin, GetAbsAngles(), velocity, 1, GetEffects(), color.r, color.g, color.b );
}
void CPropDoorRotatingBreakable::SetDamageModel( int index, bool rebuildPhysics )
{
	m_iDamageStage = index;
	char path[80];
	Q_snprintf( path, sizeof( path ), "models/%s.mdl", STRING( m_DamageModels[index] ) );
	V_FixSlashes( path, '/' );
	SetModel( path );
	if ( !rebuildPhysics ) return;
	CreateVPhysics();
	Teleport( NULL, NULL, NULL );
	KeyValues *keys = new KeyValues( "" );
	if ( keys->LoadFromBuffer( modelinfo->GetModelName( GetModel() ), modelinfo->GetModelKeyValueText( GetModel() ) ) )
	{
		KeyValues *data = keys->FindKey( "prop_data" );
		if ( data )
		{
			SetDmgModBullet( data->GetFloat( "dmg.bullets", GetDmgModBullet() ) );
			SetDmgModClub( data->GetFloat( "dmg.club", GetDmgModClub() ) );
			SetDmgModExplosive( data->GetFloat( "dmg.explosive", GetDmgModExplosive() ) );
			SetDmgModFire( data->GetFloat( "dmg.fire", GetDmgModFire() ) );
			SetBlocksLOS( data->GetInt( "blocklos", BlocksLOS() ) != 0 );
			if ( data->GetInt( "isdebris", 0 ) ) SetCollisionGroup( COLLISION_GROUP_DEBRIS );
		}
	}
	keys->deleteThis();
}
#pragma float_control( precise, on, push )
int CPropDoorRotatingBreakable::OnTakeDamage( const CTakeDamageInfo &input )
{
	const int oldHealth = GetHealth();
	CTakeDamageInfo info( input );
	if ( ( input.GetDamageType() & DMG_BLAST ) && ( input.GetDamage() >= 40.0f || ( input.GetDamageType() & DMG_BLAST_SURFACE ) ) ) info.SetDamage( static_cast< float >( GetMaxHealth() ) );
	if ( !m_bBreakable ) return 0;
	if ( HasInteraction( PROPINTER_GAME_ONLY_BREAKABLE_BY_TANK ) )
	{
		CTerrorPlayer *attacker = ToTerrorPlayer( input.GetAttacker() );
		if ( !attacker || attacker->GetTeamNumber() != TEAM_INFECTED || attacker->GetZombieClass() != 8 ) return 0;
	}
	const int result = CBreakableProp::OnTakeDamage( info );
	if ( oldHealth == GetHealth() || GetHealth() <= 0 || !m_DamageModels.Count() ) return result;
	const int step = GetMaxHealth() / ( m_DamageModels.Count() + 1 );
	// A zero/negative step cannot terminate the native descending-threshold
	// walk without signed overflow. Skip only that undefined stage walk.
	if ( step <= 0 ) return result;
	int stage = -1;
	for ( int threshold = GetMaxHealth() - step; GetHealth() < threshold; threshold -= step ) ++stage;
	if ( stage > m_DamageModels.Count() - 1 ) stage = m_DamageModels.Count() - 1;
	else if ( stage < -1 ) stage = -1;
	if ( stage <= m_iDamageStage ) return result;
	m_bAllowPortalClosing = false;
	if ( IsDoorClosed() ) UpdateAreaPortals( true );
	const CUtlString sequence( GetSequenceName( GetSequence() ) );
	const float cycle = GetCycle();
	PhysBreakSound( this, VPhysicsGetObject(), GetAbsOrigin() );
	CPVSFilter filter( WorldSpaceCenter() );
	while ( stage > m_iDamageStage )
	{
		const float mass = GetNonShadowMass();
		const Vector velocity = mass > 0.0f ? input.GetDamageForce() * ( 1.0f / mass ) : vec3_origin;
		EmitDamageModelGibs( filter, GetAbsOrigin(), velocity );
		SetDamageModel( m_iDamageStage + 1, true );
	}
	const int restoredSequence = LookupSequence( sequence.String() );
	if ( restoredSequence >= 0 ) { ResetSequence( restoredSequence ); SetCycle( cycle ); }
	m_OnBreak.FireOutput( this, this );
	return result;
}
void CPropDoorRotatingBreakable::Event_Killed( const CTakeDamageInfo &info )
{
	if ( m_DamageModels.Count() && m_DamageModels.Count() - 1 > m_iDamageStage )
	{
		PhysBreakSound( this, VPhysicsGetObject(), GetAbsOrigin() );
		CPASFilter filter( WorldSpaceCenter() );
		while ( m_DamageModels.Count() - 1 > m_iDamageStage )
		{
			Vector direction = info.GetDamageForce();
			const double length = VectorNormalize( direction );
			const Vector origin = GetAbsOrigin() + direction * 10.0f;
			const float speed = length >= 300.0 ? 300.0f : static_cast< float >( length );
			EmitDamageModelGibs( filter, origin, direction * speed );
			SetDamageModel( m_iDamageStage + 1, false );
		}
	}
	if ( IsDoorClosed() ) UpdateAreaPortals( true );
	AI_CriteriaSet criteria;
	CTeam *team = info.GetAttacker() ? info.GetAttacker()->GetTeam() : NULL;
	if ( team ) criteria.AppendCriteria( "brokenby", team->GetName(), 1.0f );
	ResponseQueueManager().Queue( "DoorBroken", criteria, 0.0f, ResponseTarget_t( ResponseTarget_t::ANY ), this );
	m_OnBreak.FireOutput( this, this );
	CBreakableProp::Event_Killed( info );
}
#pragma float_control( pop )

class CPropBreakableWall : public CPropDoorRotatingBreakable
{
public:
	DECLARE_CLASS( CPropBreakableWall, CPropDoorRotatingBreakable );
	void Spawn() OVERRIDE
	{
		BaseClass::Spawn();
		LockWithoutSound();
		SetNextThink( gpGlobals->curtime + 1.0f );
	}
	void Think() OVERRIDE
	{
		if ( !TheNavMesh->IsLoaded() ) { SetNextThink( gpGlobals->curtime + 1.0f ); return; }
		CNavArea *area = TheNavMesh->GetNearestNavArea( WorldSpaceCenter(), false, 10000.0f, false, true );
		if ( area ) { area->SetAttributes( area->GetAttributes() | 0x80000 ); m_iNavAreaID = area->GetID(); }
		else m_iNavAreaID = -1;
		SetNextThink( TICK_NEVER_THINK );
		BaseClass::Think();
	}
	void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE
	{
		if ( m_iNavAreaID >= 0 )
		{
			CNavArea *area = TheNavMesh->GetNavAreaByID( m_iNavAreaID );
			if ( area ) area->RemoveAttributes( 0x80000 );
		}
		BaseClass::Event_Killed( info );
	}
private:
	int m_iNavAreaID; // native6112: zero allocation, not a saved field
};
LINK_ENTITY_TO_CLASS( prop_wall_breakable, CPropBreakableWall );
