#include "cbase.h"
#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )
#if defined( _MSC_VER ) && _MSC_VER >= 1933
#pragma fp_contract( off )
#endif

class CBeamSpotlight : public CBaseEntity
{
public:
	DECLARE_CLASS( CBeamSpotlight, CBaseEntity );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	bool KeyValue( const char *key, const char *value ) OVERRIDE
	{
		if ( Q_stricmp( key, "SpotlightWidth" ) ) return BaseClass::KeyValue( key, value );
		const double width = atof( value );
		if ( static_cast< double >( m_flSpotlightGoalWidth.Get() ) != width )
			m_flSpotlightGoalWidth.SetDirect( static_cast< float >( width ) );
		if ( m_flSpotlightGoalWidth > 102.3f )
		{
			Warning( "Map Bug:  %s has SpotLightWidth %f > %f, clamping value\n", STRING( GetEntityName() ), m_flSpotlightGoalWidth.Get(), 102.3000030517578 );
			if ( m_flSpotlightGoalWidth > 102.3f ) m_flSpotlightGoalWidth = 102.3f;
		}
		return true;
	}
	void Precache() OVERRIDE
	{
		m_nHaloIndex = PrecacheModel( HasSpawnFlags( 64 ) ? "sprites/light_glow03_nofog.vmt" : "sprites/light_glow03.vmt" );
		PrecacheModel( HasSpawnFlags( 64 ) ? "sprites/glow_test02_nofog.vmt" : "sprites/glow_test02.vmt" );
	}
	int UpdateTransmitState() OVERRIDE { return SetTransmitState( FL_EDICT_PVSCHECK ); }
	void Spawn() OVERRIDE
	{
		Precache();
		UTIL_SetSize( this, vec3_origin, vec3_origin );
		AddSolidFlags( FSOLID_NOT_SOLID );
		SetMoveType( MOVETYPE_NONE );
		AddEFlags( EFL_FORCE_CHECK_TRANSMIT );
		DispatchUpdateTransmitState();
		m_bHasDynamicLight = !HasSpawnFlags( 2 );
		m_bSpotlightOn = HasSpawnFlags( 1 );
		m_isRotating = HasSpawnFlags( 4 );
		m_isReversed = HasSpawnFlags( 8 );
		m_bNoFog = HasSpawnFlags( 64 );
		UpdateRotation();
	}
	void InputTurnOn( inputdata_t & ) { m_bSpotlightOn = true; }
	void InputTurnOff( inputdata_t & ) { m_bSpotlightOn = false; }
	void InputStart( inputdata_t & ) { if ( !m_isRotating ) { m_isRotating = true; UpdateRotation(); } }
	void InputStop( inputdata_t & ) { if ( m_isRotating ) { m_isRotating = false; UpdateRotation(); } }
	void InputReverse( inputdata_t & ) { m_isReversed = !m_isReversed; UpdateRotation(); }
private:
	void UpdateRotation()
	{
		if ( !m_isRotating || m_flmaxSpeed == 0.0f )
		{
			if ( m_flRotationSpeed.Get() != 0.0f ) m_flRotationSpeed.SetDirect( 0.0f );
			return;
		}
		if ( m_nRotationAxis != 1 ) m_nRotationAxis = 1;
		if ( HasSpawnFlags( 32 ) )
		{
			if ( m_nRotationAxis != 0 ) m_nRotationAxis = 0;
		}
		else if ( HasSpawnFlags( 16 ) && m_nRotationAxis != 2 ) m_nRotationAxis = 2;
		if ( m_flRotationSpeed.Get() != m_flmaxSpeed ) m_flRotationSpeed.SetDirect( m_flmaxSpeed );
		if ( m_isReversed )
		{
			const float reversed = m_flRotationSpeed.Get() * -1.0f;
			if ( m_flRotationSpeed.Get() != reversed ) m_flRotationSpeed.SetDirect( reversed );
		}
	}
	CNetworkVar( int, m_nHaloIndex );
	CNetworkVar( bool, m_bSpotlightOn );
	CNetworkVar( bool, m_bHasDynamicLight );
	CNetworkVar( bool, m_bNoFog );
	CNetworkVar( float, m_flSpotlightMaxLength );
	CNetworkVar( float, m_flSpotlightGoalWidth );
	CNetworkVar( float, m_flHDRColorScale );
	CNetworkVar( int, m_nRotationAxis );
	CNetworkVar( float, m_flRotationSpeed );
	float m_flmaxSpeed;
	bool m_isRotating, m_isReversed;
	COutputEvent m_OnOn, m_OnOff; // native outputs are registered, never fired
};
LINK_ENTITY_TO_CLASS( beam_spotlight, CBeamSpotlight );
IMPLEMENT_SERVERCLASS_ST( CBeamSpotlight, DT_BeamSpotlight )
	SendPropInt( SENDINFO( m_nHaloIndex ), 16, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bSpotlightOn ) ),
	SendPropBool( SENDINFO( m_bHasDynamicLight ) ),
	SendPropBool( SENDINFO( m_bNoFog ) ),
	SendPropFloat( SENDINFO( m_flSpotlightMaxLength ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flSpotlightGoalWidth ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flHDRColorScale ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flRotationSpeed ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_nRotationAxis ), 2, SPROP_UNSIGNED ),
END_SEND_TABLE()
BEGIN_DATADESC( CBeamSpotlight )
	DEFINE_FIELD( m_nHaloIndex, FIELD_MODELINDEX ),
	DEFINE_FIELD( m_bSpotlightOn, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bHasDynamicLight, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bNoFog, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flRotationSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( m_isRotating, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_isReversed, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_nRotationAxis, FIELD_INTEGER ),
	DEFINE_KEYFIELD( m_flmaxSpeed, FIELD_FLOAT, "maxspeed" ),
	DEFINE_KEYFIELD( m_flSpotlightMaxLength, FIELD_FLOAT, "SpotlightLength" ),
	DEFINE_KEYFIELD( m_flSpotlightGoalWidth, FIELD_FLOAT, "SpotlightWidth" ),
	DEFINE_KEYFIELD( m_flHDRColorScale, FIELD_FLOAT, "HDRColorScale" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "LightOn", InputTurnOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "LightOff", InputTurnOff ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Start", InputStart ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Stop", InputStop ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Reverse", InputReverse ),
	DEFINE_OUTPUT( m_OnOn, "OnLightOn" ),
	DEFINE_OUTPUT( m_OnOff, "OnLightOff" ),
END_DATADESC()
#pragma float_control( pop )
