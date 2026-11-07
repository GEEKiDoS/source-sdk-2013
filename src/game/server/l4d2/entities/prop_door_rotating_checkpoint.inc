//========= Copyright Valve Corporation, All rights reserved. ============//
// Included once from props.cpp after CPropDoorRotating's implementation.
// Its private native base remains in props.cpp; no duplicate rotating-door owner.

ConVar z_debug_breakables( "z_debug_breakables", "0", 0x00004004 );

class CPropDoorRotatingCheckpoint : public CPropDoorRotating
{
public:
	DECLARE_CLASS( CPropDoorRotatingCheckpoint, CPropDoorRotating );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	CPropDoorRotatingCheckpoint() : m_hasUnlockSequence( false ) {}
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual bool IsCheckpointDoor() const OVERRIDE { return true; }
	virtual bool HasUnlockSequence() const OVERRIDE { return m_hasUnlockSequence; }
	virtual bool IsUsableByTeam( int team ) const OVERRIDE { return team == TEAM_SURVIVOR; }
	virtual int GetUsePriority( CBaseEntity *user ) OVERRIDE { return IsDoorOpen() ? 2 : 0; } // S1A84D0.
	virtual bool HandleDoorUse( CBaseEntity *activator, CBaseEntity *caller, int actions ) OVERRIDE;
	virtual int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void OnDoorClosed() OVERRIDE;
	virtual void OnDoorOpened() OVERRIDE;
protected:
	virtual void Unlock( CBaseEntity *activator ) OVERRIDE;
private:
	void UnlockThink();
	bool m_hasUnlockSequence;
};

LINK_ENTITY_TO_CLASS( prop_door_rotating_checkpoint, CPropDoorRotatingCheckpoint );
IMPLEMENT_SERVERCLASS_ST( CPropDoorRotatingCheckpoint, DT_PropDoorRotatingCheckpoint )
END_SEND_TABLE()
BEGIN_DATADESC( CPropDoorRotatingCheckpoint )
	DEFINE_FIELD( m_hasUnlockSequence, FIELD_BOOLEAN ),
	DEFINE_THINKFUNC( UnlockThink ),
END_DATADESC()

void CPropDoorRotatingCheckpoint::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Doors.Checkpoint.Unlock" );
	KeyValues *keys = new KeyValues( "" );
	if ( keys->LoadFromBuffer( modelinfo->GetModelName( GetModel() ), modelinfo->GetModelKeyValueText( GetModel() ) ) )
	{
		KeyValues *options = keys->FindKey( "door_options" );
		KeyValues *defaults = options ? options->FindKey( "defaults" ) : NULL;
		const char *crossbar = defaults ? defaults->GetString( "crossbar", NULL ) : NULL;
		if ( crossbar ) PrecacheModel( crossbar );
	}
	keys->deleteThis();
}

void CPropDoorRotatingCheckpoint::Spawn()
{
	BaseClass::Spawn();
	m_hasUnlockSequence = SelectWeightedSequence( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_DOOR_ANIMTOUNLOCK ) ) >= 0;
	if ( m_hasUnlockSequence ) LockWithoutSound();
	m_takedamage = DAMAGE_EVENTS_ONLY;
	ForceClosed();
}

bool CPropDoorRotatingCheckpoint::HandleDoorUse( CBaseEntity *activator, CBaseEntity *caller, int actions )
{
	if ( TerrorGameRules()->IsVersusMode() && HasUnlockSequence() && !IsDoorClosed() )
		return false;
	return BaseClass::HandleDoorUse( activator, caller, actions );
}

void CPropDoorRotatingCheckpoint::Unlock( CBaseEntity *activator )
{
	if ( !m_hasUnlockSequence )
	{
		BaseClass::Unlock( activator );
		return;
	}
	if ( LookupTerrorOriginalActivity( (Activity)GetSequenceActivity( GetSequence() ) ) ==
		TERROR_ORIGINAL_ACT_DOOR_ANIMTOUNLOCK ) return;
	EmitSound( "Doors.Checkpoint.Unlock" );
	PropSetSequence( SelectWeightedSequence( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_DOOR_ANIMTOUNLOCK ) ) );
	SetContextThink( &CPropDoorRotatingCheckpoint::UnlockThink, gpGlobals->curtime, "UNLOCK_CONTEXT" );
	IGameEvent *event = gameeventmanager->CreateEvent( "door_unlocked" );
	if ( event )
	{
		event->SetBool( "checkpoint", true );
		CBasePlayer *player = ToBasePlayer( activator );
		event->SetInt( "userid", player ? player->GetUserID() : 0 );
		gameeventmanager->FireEvent( event );
	}
}

void CPropDoorRotatingCheckpoint::UnlockThink()
{
	if ( GetCycle() < 0.99f || !IsSequenceFinished() )
	{
		SetContextThink( &CPropDoorRotatingCheckpoint::UnlockThink, gpGlobals->curtime, "UNLOCK_CONTEXT" );
		return;
	}
	SetContextThink( NULL, TICK_NEVER_THINK, "UNLOCK_CONTEXT" );
	// Original completion calls the base unlock without the unlock sound.
	UnlockWithoutSound();
	m_takedamage = DAMAGE_YES;
}

int CPropDoorRotatingCheckpoint::OnTakeDamage( const CTakeDamageInfo &input )
{
	if ( m_takedamage != DAMAGE_YES ) return 0;
	CTerrorPlayer *attacker = ToTerrorPlayer( input.GetAttacker() );
	if ( ( !attacker || attacker->GetTeamNumber() != TEAM_INFECTED || attacker->m_zombieClass.Get() != 8 ) &&
		!z_debug_breakables.GetBool() ) return 0;
	CTakeDamageInfo damage = input;
	damage.SetDamage( static_cast< float >( static_cast< int32 >( static_cast< uint32 >( GetHealth() ) * uint32( 10 ) ) ) );
	return BaseClass::OnTakeDamage( damage );
}

void CPropDoorRotatingCheckpoint::OnDoorClosed()
{
	BaseClass::OnDoorClosed();
	CBaseEntity *changelevel = gEntList.FindEntityByClassnameNearest( "info_changelevel", WorldSpaceCenter(), 2000.0f );
	if ( changelevel )
	{
		variant_t empty;
		changelevel->AcceptInput( "CheckpointDoorClosed", GetActivator(), this, empty, 0 );
	}
}

void CPropDoorRotatingCheckpoint::OnDoorOpened()
{
	BaseClass::OnDoorOpened();
	CBaseEntity *changelevel = gEntList.FindEntityByClassnameNearest( "info_changelevel", WorldSpaceCenter(), 2000.0f );
	if ( changelevel )
	{
		variant_t empty;
		changelevel->AcceptInput( "CheckpointDoorOpened", GetActivator(), this, empty, 0 );
	}
}
