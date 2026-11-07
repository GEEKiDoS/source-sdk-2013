//========= Copyright Valve Corporation, All rights reserved. ============//
// Compose into EXISTING c_props.cpp after the sole C_BasePropDoor definition.
// Replace the empty checkpoint class/table in c_prop_door_rotating_checkpoint.inc;
// parent alone chooses its one include/registration point AFTER whole composition.
// Real includes before memdbgon: KeyValues.h, c_recipientfilter.h, itempents.h,
// l4d2/terror_shareddefs.h, datacache/imdlcache.h, emmintrin.h.
// C_BasePropDoor's five protected wire fields and m_doorUseTimer remain SOLE.
#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// C180C30 is a real existing-model/attachment/eye provider, not ForceClosed.
static bool DoorHandlePositionIsZero( const Vector &position )
{
	return position.x > -0.0099999998f && 0.0099999998f > position.x &&
		position.y > -0.0099999998f && 0.0099999998f > position.y &&
		position.z > -0.0099999998f && 0.0099999998f > position.z;
}

// Sole M7 native3F4C0 value-path provider, already used by actual selection
// and property consumers. Reuse its x87 sum/sqrt + float-spill/SSE products;
// SDK VectorNormalize's approximate rsqrt/1e-10f is a different operation.
extern "C" double L4D2_GlowNormalize( Vector *direction );

static float DoorHandleDistance( const Vector &handle, const Vector &eye )
{
	const float x = handle.x - eye.x;
	const float y = handle.y - eye.y;
	const float z = handle.z - eye.z;
	const float squared = ( y * y + z * z ) + x * x;
	return _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( squared ) ) );
}

static bool IsLookingAtDoorPushHandle( C_BasePropDoor *door, C_BasePlayer *player )
{
	const int rightBone = door->LookupBone( "DoorHandleBone" );
	const int leftBone = door->LookupBone( "DoorHandleBoneLeft" );
	if ( rightBone < 0 || leftBone < 0 ) return false;
	int right = door->LookupAttachment( "RightHandle" );
	int left = door->LookupAttachment( "LeftHandle" );
	bool visible = true;
	const int group = door->FindBodygroupByName( "handles" );
	if ( group >= 0 )
	{
		const int body = door->GetBodygroup( group );
		if ( body == 0 ) visible = false;
		else if ( body == 2 )
		{
			const int swap = right;
			right = left;
			left = swap;
		}
	}
	if ( right < 0 || left < 0 || !visible ) return false;
	matrix3x4_t attachment;
	// Native accepts attachment0 and ignores BOTH GetAttachment results. Do
	// not repair a failed query by zero-initializing or rejecting its matrix.
	door->GetAttachment( right, attachment );
	const Vector rightPosition( attachment[0][3], attachment[1][3], attachment[2][3] );
	door->GetAttachment( left, attachment );
	const Vector leftPosition( attachment[0][3], attachment[1][3], attachment[2][3] );
	if ( DoorHandlePositionIsZero( rightPosition ) || DoorHandlePositionIsZero( leftPosition ) ) return false;
	Vector eye, forward;
	player->EyePositionAndVectors( &eye, &forward, NULL, NULL );
	Vector toward = rightPosition - eye;
	L4D2_GlowNormalize( &toward );
	const float rightDistance = DoorHandleDistance( rightPosition, eye );
	const float leftDistance = DoorHandleDistance( leftPosition, eye );
	if ( rightDistance > leftDistance ) return false;
	const float dot = ( forward.y * toward.y + forward.x * toward.x ) + forward.z * toward.z;
	return dot > 0.0f;
}

// C8D6A0. Do not replace the user's actual render-team virtual with local
// player, owner's team, raw m_iTeamNum, or a null-tolerant invented fallback.
bool C_BasePropDoor::InSameTeam( const C_BaseEntity *entity ) const
{
	const int team = const_cast< C_BaseEntity * >( entity )->GetRenderTeamNumber();
	return team == 2 || team == 4;
}

// C8E910 deliberately calls the root mask directly, not Breakable's bool gate.
unsigned int C_BasePropDoor::PhysicsSolidMaskForEntity() const
{
	return C_BaseEntity::PhysicsSolidMaskForEntity() | 0x0200;
}

// Whole C8E660. This is the native door action bitmask, not ObjectCaps or the
// user's use-priority query. Its real master resolves once before delegation.
int C_BasePropDoor::GetDoorUseActionFlags( C_BasePlayer *player )
{
	if ( !player ) return 0;
	C_BasePropDoor *master = m_hMaster.Get();
	if ( master ) return master->GetDoorUseActionFlags( player );
	int flags = 0;
	{
		MDLCACHE_CRITICAL_SECTION();
		const TerrorOriginalActivity activity = LookupTerrorOriginalActivity( GetSequenceActivity( GetSequence() ) );
		const bool changingLock = activity == TERROR_ORIGINAL_ACT_DOOR_ANIMTOLOCK ||
			activity == TERROR_ORIGINAL_ACT_DOOR_OPENFAIL;
		if ( m_eDoorState == 0 && IsLookingAtDoorPushHandle( this, player ) ) flags = changingLock ? 8 : 4;
		if ( flags & 8 ) return 8;
		if ( m_eDoorState == 0 || m_eDoorState == 3 ) flags |= 1;
	}
	return flags | 2;
}

class C_PropDoorRotatingCheckpoint : public C_BasePropDoor
{
public:
	DECLARE_CLASS( C_PropDoorRotatingCheckpoint, C_BasePropDoor );
	DECLARE_CLIENTCLASS();
	C_PropDoorRotatingCheckpoint();
	virtual void PostDataUpdate( DataUpdateType_t type ) OVERRIDE;
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	virtual void UpdateClientSideAnimation() OVERRIDE;
	virtual int GetUsePriority( C_BaseEntity *user ) OVERRIDE;
	virtual bool InSameTeam( const C_BaseEntity *entity ) const OVERRIDE;
	virtual bool IsGlowUseTarget() OVERRIDE;
	virtual int GetDoorUseActionFlags( C_BasePlayer *player ) OVERRIDE;
	virtual bool HasCrossbar() const;
private:
	Vector m_vecCrossbarInitialPosition;     // Native2464; ctor omitted.
	QAngle m_angCrossbarInitialAngles;       // Native2476; ctor omitted.
	bool m_bHasCrossbar;                     // Native2488; ctor omitted.
	char m_szCrossbarModel[256];             // Native2489; ctor omitted.
	int m_nCrossbarBone;                     // Native2748; ctor -1.
	Vector m_vecLastCrossbarPosition;        // Native2752; ctor omitted.
	Vector m_vecCrossbarVelocity;            // Native2764; ctor omitted.
	float m_flLastCrossbarAnimTime;          // Native2776; ctor omitted.
	bool m_bCrossbarInitialized;             // Native2780; ctor omitted.
	bool m_bCrossbarDetached;                // Native2781; ctor false.
};

IMPLEMENT_CLIENTCLASS_DT( C_PropDoorRotatingCheckpoint, DT_PropDoorRotatingCheckpoint, CPropDoorRotatingCheckpoint )
END_RECV_TABLE()

C_PropDoorRotatingCheckpoint::C_PropDoorRotatingCheckpoint()
{
	m_bCrossbarDetached = false;
	m_nCrossbarBone = -1;
}

// Whole C8D840 (networkable this+8): invokes BA directly, NOT the base door
// model-comparison override, so the actual comparison/created-reset occurs once.
void C_PropDoorRotatingCheckpoint::PostDataUpdate( DataUpdateType_t type )
{
	if ( type != DATA_UPDATE_CREATED )
	{
		const model_t *previous = GetModel();
		C_BaseAnimating::PostDataUpdate( type );
		if ( previous != GetModel() ) m_bRecreateVPhysics = true;
	}
	else
	{
		C_BaseAnimating::PostDataUpdate( type );
		m_bCrossbarInitialized = false;
	}
}

// Whole C8D8A0. Actual studio/activity/bone/keyvalue presentation, no server
// ForceClosed/model-path predicate. Preserve weighted-sequence RNG consumption.
CStudioHdr *C_PropDoorRotatingCheckpoint::OnNewModel()
{
	CStudioHdr *model = C_BaseAnimating::OnNewModel();
	m_szCrossbarModel[0] = '\0';
	m_nCrossbarBone = -1;
	if ( !model )
	{
		m_bHasCrossbar = false;
		return model;
	}
	m_bCrossbarInitialized = false;
	m_bHasCrossbar = SelectWeightedSequence( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_DOOR_ANIMTOUNLOCK ) ) >= 0;
	m_nCrossbarBone = LookupBone( "doorhandlebone" );
	if ( m_nCrossbarBone >= 0 )
	{
		KeyValues *values = new KeyValues( "" );
		const char *buffer = modelinfo->GetModelKeyValueText( GetModel() );
		const char *name = modelinfo->GetModelName( GetModel() );
		if ( values->LoadFromBuffer( name, buffer ) )
		{
			KeyValues *options = values->FindKey( "door_options" );
			if ( options )
			{
				KeyValues *defaults = options->FindKey( "defaults" );
				if ( defaults ) V_strncpy( m_szCrossbarModel, defaults->GetString( "crossbar", "" ), sizeof( m_szCrossbarModel ) );
			}
		}
		values->deleteThis();
	}
	return model;
}

// Whole C8E0B0. Base animation first, one-time initial pose, then actual unlock
// activity/bodygroup/cycle and bone-time velocity. The real local physics TE is
// emitted through the supported target te->PhysicsProp backend, not a model
// stand-in, actor-only Physics2 API, or server door state approximation.
void C_PropDoorRotatingCheckpoint::UpdateClientSideAnimation()
{
	C_BaseAnimating::UpdateClientSideAnimation();
	if ( !m_bCrossbarInitialized )
	{
		m_bCrossbarInitialized = true;
		m_vecCrossbarInitialPosition = GetAbsOrigin();
		m_vecLastCrossbarPosition = m_vecCrossbarInitialPosition;
		m_angCrossbarInitialAngles = GetAbsAngles();
		m_vecCrossbarVelocity.Init();
		m_flLastCrossbarAnimTime = gpGlobals->curtime;
		if ( m_nCrossbarBone >= 0 )
		{
			GetBonePosition( m_nCrossbarBone, m_vecCrossbarInitialPosition, m_angCrossbarInitialAngles );
			m_vecLastCrossbarPosition = m_vecCrossbarInitialPosition;
		}
	}
	if ( !m_bHasCrossbar ||
		LookupTerrorOriginalActivity( GetSequenceActivity( GetSequence() ) ) != TERROR_ORIGINAL_ACT_DOOR_ANIMTOUNLOCK ||
		GetBodygroup( 1 ) == 0 || m_bCrossbarDetached ) return;
	if ( m_nCrossbarBone >= 0 )
	{
		const float elapsed = GetAnimTime() - m_flLastCrossbarAnimTime;
		if ( elapsed > 0.0f )
		{
			Vector position;
			QAngle angles;
			GetBonePosition( m_nCrossbarBone, position, angles );
			const float inverse = 1.0f / elapsed;
			m_vecCrossbarVelocity.x = inverse * ( position.x - m_vecLastCrossbarPosition.x );
			m_vecCrossbarVelocity.y = ( position.y - m_vecLastCrossbarPosition.y ) * inverse;
			m_vecCrossbarVelocity.z = ( position.z - m_vecLastCrossbarPosition.z ) * inverse;
			m_vecLastCrossbarPosition = position;
			m_flLastCrossbarAnimTime = GetAnimTime();
		}
	}
	if ( !( GetCycle() > 0.89999998f ) ) return;
	SetBodygroup( 1, 0 );
	m_bCrossbarDetached = true;
	const int index = modelinfo->GetModelIndex( m_szCrossbarModel );
	if ( index > 0 && m_nCrossbarBone >= 0 )
	{
		MDLCACHE_CRITICAL_SECTION();
		Vector bonePosition;
		QAngle boneAngles;
		GetBonePosition( m_nCrossbarBone, bonePosition, boneAngles );
		const Vector offset = bonePosition - m_vecCrossbarInitialPosition;
		const QAngle angleOffset = boneAngles - m_angCrossbarInitialAngles;
		CPASFilter filter( WorldSpaceCenter() );
		const int effects = GetEffects();
		const QAngle angles = GetAbsAngles() + angleOffset;
		const Vector position = GetAbsOrigin() + offset;
		// Native TE explicitly sends RGB255/255/255. The existing target
		// PhysicsProp path creates the same default white modulation; no
		// interface/schema extension or ignored color argument is introduced.
		te->PhysicsProp( filter, -1.0f, index, GetSkin(), position, angles,
			m_vecCrossbarVelocity, 0, effects );
	}
}

// C8E9E0. The user argument is intentionally unused.
int C_PropDoorRotatingCheckpoint::GetUsePriority( C_BaseEntity *user )
{
	return m_eDoorState == 2 ? 2 : 0;
}

bool C_PropDoorRotatingCheckpoint::InSameTeam( const C_BaseEntity *entity ) const
{
	return true;
}

// C8E9C0 is the separate native356 query; NEVER replace the genuine360
// IsPotentiallyUseable/ObjectCaps registration gate with this presentation bit.
bool C_PropDoorRotatingCheckpoint::IsGlowUseTarget()
{
	return !HasCrossbar();
}

bool C_PropDoorRotatingCheckpoint::HasCrossbar() const
{
	return m_bHasCrossbar;
}

// Whole C8E760, distinct from base-door activity/handle-mask policy above.
int C_PropDoorRotatingCheckpoint::GetDoorUseActionFlags( C_BasePlayer *player )
{
	if ( !player ) return 0;
	C_BasePropDoor *master = m_hMaster.Get();
	if ( master ) return master->GetDoorUseActionFlags( player );
	if ( m_bLocked )
	{
		const int team = player->GetRenderTeamNumber();
		if ( team == 2 || team == 4 ) return 8;
	}
	return ( m_eDoorState == 0 || m_eDoorState == 3 ) ? 3 : 2;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif
