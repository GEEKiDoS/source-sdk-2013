// L4D2-only inclusion after g_ragdoll_fadespeed/g_ragdoll_lvfadespeed in c_baseanimating.cpp.
// Replaces only L4D2 HandleAnimatedFriction/FadeOut/ClientThink bodies.
// Preserve the current native-qualified 20-record map, constructor, OnRestore and SUB_Remove owners.
static ConVar z_burn_decay_rate( "z_burn_decay_rate", "0.0", 0x0000000A, "How fast the burn effect dies on burning ragdolls" );
static ConVar z_it_decay_rate( "z_it_decay_rate", "0.001", 0x0000000A, "How fast the IT effect dies" );
static ConVar z_camo_decay_rate( "z_camo_decay_rate", "0.001", 0x00004008, "How fast the camouflage effect dies on ragdolls" );

void C_ClientRagdoll::HandleAnimatedFriction()
{
	// C2D4A4..C2D4BF, native default-off Physics2/VPhysics1 path. The native
	// callback is retained; do not insert SDK animated-friction transitions.
	if ( m_iFrictionAnimState != RAGDOLL_FRICTION_OFF && m_pRagdoll ) m_pRagdoll->RagdollBoneCount();
}
void C_ClientRagdoll::FadeOut()
{
	if ( !m_bFadingOut ) return;
	const int speed = g_RagdollLVManager.IsLowViolence() ? g_ragdoll_lvfadespeed.GetInt() : g_ragdoll_fadespeed.GetInt();
	float alpha = static_cast< float >( GetRenderColor().a ) - static_cast< float >( speed ) * gpGlobals->frametime;
	if ( !( alpha > 0.0f ) ) alpha = 0.0f; // COMISS/JA: unordered also zero
	const int integerAlpha = _mm_cvttss_si32( _mm_set_ss( alpha ) );
	SetRenderMode( kRenderTransAlpha );
	SetRenderColorA( integerAlpha );
	if ( integerAlpha == 0 ) m_bReleaseRagdoll = true;
}
void C_ClientRagdoll::ClientThink()
{
	if ( m_bReleaseRagdoll ) { ClientEntityList().AddToDeleteList( this ); return; }
	if ( g_debug_ragdoll_visualize.GetBool() && m_pRagdoll && debugoverlay )
	{
		Vector mins, maxs;
		const Vector origin = m_pRagdoll->GetRagdollOrigin();
		m_pRagdoll->GetRagdollBounds( mins, maxs );
		debugoverlay->AddBoxOverlay( origin, mins, maxs, vec3_angle, 0, 255, 0, 16, 0.0f );
	}
	HandleAnimatedFriction();
	if ( m_ragdollType == RAGDOLL_TYPE_GIB && gpGlobals->curtime > m_flGibExpireTime ) SUB_Remove();
	FadeOut();
	m_burnPercent -= z_burn_decay_rate.GetFloat();
	if ( m_burnPercent < 0.0f ) m_burnPercent = 0.0f;
	m_itBlend -= z_it_decay_rate.GetFloat();
	if ( m_itBlend < 0.0f ) m_itBlend = 0.0f;
	m_camoPercent -= z_camo_decay_rate.GetFloat();
	if ( m_camoPercent < 0.0f ) m_camoPercent = 0.0f;
}
