//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/c_witch.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/effects/terror_glow_render.h"
#include "glow_outline_effect.h"
#include "materialsystem/imaterialsystem.h"
#include "toolframework/ienginetool.h"
#include "iefx.h"
#include "dlight.h"
#include "debugoverlay_shared.h"

#include "model_types.h"
#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

extern IEngineTool *g_pL4D2EngineTool; // Optional real CHLClient::Init interface binding.
extern ConVar cl_glow_brightness;
extern ConVar spec_keep_team_glows;

static ConVar z_witch_rage_smoothing( "z_witch_rage_smoothing", "0.9", 0x0000000A );
static ConVar cl_witch_light_offset_x( "cl_witch_light_offset_x", "10", 0x00004008 );
static ConVar cl_witch_light_offset_y( "cl_witch_light_offset_y", "0", 0x00004008 );
static ConVar cl_witch_light_offset_z( "cl_witch_light_offset_z", "35", 0x00004008 );
static ConVar cl_witch_light_offset_z_max( "cl_witch_light_offset_z_max", "30", 0x00004008 );
static ConVar cl_witch_light_brightness( "cl_witch_light_brightness", "5", 0x00004008 );
static ConVar cl_witch_light_radius( "cl_witch_light_radius", "60", 0x00004008 );
static ConVar cl_witch_light_debug( "cl_witch_light_debug", "0", 0x00004008 );
static ConVar cl_witch_light_enable( "cl_witch_light_enable", "1", 0x00004008 );
static ConVar cl_witch_light_cone( "cl_witch_light_cone", "45", 0x00004008 );
static ConVar cl_witch_light_color_r( "cl_witch_light_color_r", "255", 0x00004008 );
static ConVar cl_witch_light_color_g( "cl_witch_light_color_g", "0", 0x00004008 );
static ConVar cl_witch_light_color_b( "cl_witch_light_color_b", "0", 0x00004008 );
// Native legacy console registrations persist although C318250 uses constants.
static ConVar cl_witch_glow_idle_r( "cl_witch_glow_idle_r", "0.2", 0x00080008, "Color of idle witch glow" );
static ConVar cl_witch_glow_idle_g( "cl_witch_glow_idle_g", "0.4", 0x00080008, "Color of idle witch glow" );
static ConVar cl_witch_glow_idle_b( "cl_witch_glow_idle_b", "0.2", 0x00080008, "Color of idle witch glow" );
static ConVar cl_witch_glow_angry_r( "cl_witch_glow_angry_r", "1.0", 0x00080008, "Color of angry witch glow" );
static ConVar cl_witch_glow_angry_g( "cl_witch_glow_angry_g", "0", 0x00080008, "Color of angry witch glow" );
static ConVar cl_witch_glow_angry_b( "cl_witch_glow_angry_b", "0", 0x00080008, "Color of angry witch glow" );
static ConVar cl_witch_glow_range( "cl_witch_glow_range", "1500", 0x00080008, "Distance at which the witch stops glowing" );

IMPLEMENT_CLIENTCLASS_DT( C_Witch, DT_Witch, Witch )
	RecvPropFloat( RECVINFO( m_rage ) ),
	RecvPropFloat( RECVINFO_NAME( m_wanderRage, m_wanderrage ) ),
END_RECV_TABLE()

namespace
{
float WitchAnimationTimescale()
{
	if ( g_pL4D2EngineTool ) return g_pL4D2EngineTool->GetTimescale();
	// Main-approved target capability gap: the non-tools SDK has no exposed
	// CBaseServer time-scale factor. Do not substitute demo playback speed.
	static ConVarRef host_timescale( "host_timescale" );
	return host_timescale.GetFloat();
}
}

C_Witch::C_Witch()
{
	m_eyeLight = NULL;
	SetAllowWounds( false ); // C317A23, inherited sole native4832, after base ctor.
	m_ragePose = -1;
	m_rage = 0.0f;
	m_smoothedRage = 0.0f;
	// Native does not explicitly initialize wander pose/rage or draw frames.
}

C_Witch::~C_Witch()
{
	if ( m_eyeLight ) m_eyeLight->die = gpGlobals->curtime;
	m_eyeLight = NULL;
}

CStudioHdr *C_Witch::OnNewModel()
{
	CStudioHdr *studio = BaseClass::OnNewModel();
	m_ragePose = LookupPoseParameter( "rage" );
	m_wanderRagePose = LookupPoseParameter( "wander_rage" );
	return studio;
}

void C_Witch::UpdatePoseParameters()
{
	BaseClass::UpdatePoseParameters();
	if ( m_ragePose >= 0 )
	{
		const float smoothing = z_witch_rage_smoothing.GetFloat();
		const double scale = WitchAnimationTimescale();
		const float factor = static_cast< float >( ( static_cast< double >( smoothing ) - 1.0 ) * scale + 1.0 );
		m_smoothedRage = ( 1.0f - factor ) * m_rage + m_smoothedRage * factor;
		SetPoseParameter( m_ragePose, m_smoothedRage );
	}
	if ( m_wanderRagePose >= 0 )
	{
		const float smoothing = z_witch_rage_smoothing.GetFloat();
		const double scale = WitchAnimationTimescale();
		const float factor = static_cast< float >( ( static_cast< double >( smoothing ) - 1.0 ) * scale + 1.0 );
		m_smoothedWanderRage = ( 1.0f - factor ) * m_wanderRage + m_smoothedWanderRage * factor;
		SetPoseParameter( m_wanderRagePose, m_smoothedWanderRage );
	}
}

void C_Witch::UpdateEyeLight()
{
	if ( !cl_witch_light_enable.GetBool() )
	{
		if ( m_eyeLight ) m_eyeLight->die = gpGlobals->curtime;
		m_eyeLight = NULL;
		return;
	}
	if ( !m_eyeLight ) m_eyeLight = effects->CL_AllocElight( entindex() + 0x20000000 );
	// C317CC0 native dlight+64 owner write intentionally omitted by Main's
	// target-dlight ledger decision (same exception as C254810). No ABI change.
	Vector eye;
	if ( !GetAttachment( LookupAttachment( "reye" ), eye ) ) return;
	const Vector &origin = GetRenderOrigin();
	const QAngle &angles = GetRenderAngles();
	matrix3x4_t transform;
	AngleMatrix( angles, origin, transform );
	Vector localEye;
	VectorITransform( eye, transform, localEye );
	Vector forward, right, up;
	AngleVectors( angles, &forward, &right, &up );
	if ( localEye.z <= cl_witch_light_offset_z_max.GetFloat() ) localEye.z = cl_witch_light_offset_z_max.GetFloat();
	const float z = cl_witch_light_offset_z.GetFloat() + localEye.z;
	m_eyeLight->origin.x = ( ( forward.x * cl_witch_light_offset_x.GetFloat() + origin.x ) + right.x * cl_witch_light_offset_y.GetFloat() ) + up.x * z;
	m_eyeLight->origin.y = ( ( origin.y + forward.y * cl_witch_light_offset_x.GetFloat() ) + right.y * cl_witch_light_offset_y.GetFloat() ) + up.y * z;
	m_eyeLight->origin.z = ( ( origin.z + forward.z * cl_witch_light_offset_x.GetFloat() ) + right.z * cl_witch_light_offset_y.GetFloat() ) + up.z * z;
	m_eyeLight->radius = cl_witch_light_radius.GetFloat();
	m_eyeLight->decay = 0.0f;
	m_eyeLight->m_Direction = -up;
	m_eyeLight->m_OuterAngle = cl_witch_light_cone.GetFloat();
	m_eyeLight->m_InnerAngle = m_eyeLight->m_OuterAngle;
	if ( cl_witch_light_debug.GetBool() ) NDebugOverlay::Axis( m_eyeLight->origin, angles, 10.0f, true, 0.0f );
	m_eyeLight->die = gpGlobals->curtime + 1000000.0f;
	m_eyeLight->color.r = static_cast< unsigned char >( cl_witch_light_color_r.GetInt() );
	m_eyeLight->color.g = static_cast< unsigned char >( cl_witch_light_color_g.GetInt() );
	m_eyeLight->color.b = static_cast< unsigned char >( cl_witch_light_color_b.GetInt() );
	m_eyeLight->color.exponent = static_cast< signed char >( cl_witch_light_brightness.GetInt() );
}

bool C_Witch::Simulate()
{
	BaseClass::Simulate();
	UpdateEyeLight();
	return true; // C318480: native Witch always returns true.
}

int C_Witch::DrawModel( int flags )
{
	// Main ledger: target single-local-player mapping sends native active slot to0.
	if ( ( flags & STUDIO_RENDER ) && !( flags & 0x40000000 ) ) MarkGlowDrawFrame( 0 );
	return BaseClass::DrawModel( flags );
}

bool C_Witch::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	// C317AA0 bypasses the common-infected ShouldCollide override.
	return C_BaseEntity::ShouldCollide( collisionGroup, contentsMask );
}
int C_Witch::BloodColor() { return 3; } // C317AB0 native Witch blood kind.

const char *C_Witch::GetTerrorFootstepSoundName( const char *surfaceSound, bool rightFoot, float speed, bool force )
{
	static char sound[64];
	Q_snprintf( sound, sizeof( sound ), "Witch.%s", surfaceSound );
	return sound;
}

void C_Witch::GetGlowColor( float *r, float *g, float *b, float *a )
{
	*r = *g = *b = *a = 0.0f;
	C_BasePlayer *base = C_BasePlayer::GetLocalPlayer();
	if ( !spec_keep_team_glows.GetBool() && base && base->GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_BaseEntity *target = base->GetObserverTarget();
		if ( target && target->IsPlayer() ) base = ToBasePlayer( target );
	}
	if ( !base || m_lifeState != LIFE_ALIVE || IsDormant() || base->GetTeamNumber() != TEAM_INFECTED ||
		HasTransferredRagdoll() ) return;
	const Activity activity = GetCurrentInfectedActivity();
	if ( activity == TerrorActivityFromOriginal( 669 ) || activity == TerrorActivityFromOriginal( 667 ) ) return;
	const Vector sourceEye = EyePosition();
	const Vector targetEye = base->EyePosition();
	const Vector delta = targetEye - sourceEye;
	const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
	float alpha;
	if ( !( distance <= 500.0f ) )
	{
		alpha = 1.0f;
		if ( !( distance >= 1500.0f ) )
		{
			alpha = ( ( 1.0f - ( 1500.0f - distance ) * 0.001f ) + 1.0f ) * 0.5f;
			if ( !( alpha > 0.0f ) ) return;
		}
	}
	else alpha = 0.5f;
	*a = alpha;
	float rage = m_rage > m_wanderRage ? m_rage : m_wanderRage;
	*r = rage * 0.39999998f + 0.60000002f;
	*g = 0.80000001f - rage * 0.80000001f;
	*b = 1.0f - rage * 1.0f;
	*r = cl_glow_brightness.GetFloat() * *r;
	*g = cl_glow_brightness.GetFloat() * *g;
	*b = cl_glow_brightness.GetFloat() * *b;
}

bool C_Witch::GetStencilState( ShaderStencilState_t &state )
{
	float r, g, b, alpha;
	GetGlowColor( &r, &g, &b, &alpha );
	if ( !( alpha > 0.0f ) ) return false;
	state.m_nWriteMask = 2;
	state.m_nReferenceValue = 2;
	state.m_bEnable = true;
	state.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
	state.m_PassOp = STENCILOPERATION_REPLACE;
	state.m_FailOp = STENCILOPERATION_KEEP;
	state.m_ZFailOp = STENCILOPERATION_REPLACE;
	return true;
}

#pragma float_control( pop )
