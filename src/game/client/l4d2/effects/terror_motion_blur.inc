//========= Copyright Valve Corporation, All rights reserved. ============//
// C1C81A0 ordinary-view path. The SDK CViewSetup has no native shutter fields.
// Only its real single viewport is used; no fabricated shutter/split-screen state.
#pragma float_control( precise, on, push )

static float ClampTerrorMotionBlur( float value, float low, float high )
{
	if ( value > high ) return high;
	if ( low > value ) return low;
	return value;
}

void DoImageSpaceMotionBlur( const CViewSetup &viewBlur, int x, int y, int w, int h )
{
	static ConVarRef enabled( "mat_motion_blur_enabled" );
	if ( !enabled.GetBool() ) return;

	const float rotationIntensity = mat_motion_blur_rotation_intensity.GetFloat() * 0.15f;
	const float fallingIntensity = mat_motion_blur_falling_intensity.GetFloat();
	const float fallingMin = mat_motion_blur_falling_min.GetFloat();
	const float fallingMax = mat_motion_blur_falling_max.GetFloat();
	const float strength = mat_motion_blur_strength.GetFloat();
	static float lastTime = 0.0f, previousPitch = 0.0f, previousYaw = 0.0f;
	static Vector previousPosition( 0, 0, 0 );
	static float noRotationUntil = 0.0f;
	const float elapsed = gpGlobals->realtime - lastTime;
	float pitch = viewBlur.angles[PITCH], yaw = viewBlur.angles[YAW];
	while ( pitch > 180.0f ) pitch -= 360.0f;
	while ( pitch < -180.0f ) pitch += 360.0f;
	while ( yaw > 180.0f ) yaw -= 360.0f;
	while ( yaw < -180.0f ) yaw += 360.0f;
	matrix3x4_t basis;
	AngleMatrix( viewBlur.angles, basis );
	const Vector forward( basis[0][0], basis[1][0], basis[2][0] );
	const Vector side( basis[0][1], basis[1][1], basis[2][1] );
	const Vector movement = previousPosition - viewBlur.origin;
	const float distance = sqrtf( ( movement.y * movement.y + movement.z * movement.z ) + movement.x * movement.x );
	if ( ( distance > 30.0f && elapsed >= 0.5f ) || elapsed > 0.06666667f )
	{
		for ( int i = 0; i < 4; ++i ) g_vMotionBlurValues[i] = 0.0f;
	}
	else if ( distance > 50.0f )
	{
		noRotationUntil = gpGlobals->realtime + 1.0f;
	}
	else
	{
		const float horizontalFov = viewBlur.fov;
		const float verticalFov = viewBlur.m_flAspectRatio <= 0.0f ? horizontalFov : horizontalFov / viewBlur.m_flAspectRatio;
		const float viewDot = ( forward.y * movement.y + forward.x * movement.x ) + forward.z * movement.z;
		g_vMotionBlurValues[2] = mat_motion_blur_forward_enabled.GetBool() ? viewDot : (float)( fabs( (double)forward.z ) * (double)viewDot );
		const float sideDot = ( side.y * movement.y + side.x * movement.x ) + side.z * movement.z;
		float originalYaw = previousYaw - yaw;
		if ( ( originalYaw > 180.0f || originalYaw < -180.0f ) &&
			previousYaw + yaw > -180.0f && previousYaw + yaw < 180.0f )
			originalYaw = previousYaw + yaw;
		float adjustedYaw = sideDot * 0.33333334f + originalYaw;
		adjustedYaw = originalYaw < 0.0f ? ClampTerrorMotionBlur( adjustedYaw, originalYaw, 0.0f ) :
			ClampTerrorMotionBlur( adjustedYaw, 0.0f, originalYaw );
		const float undampenedYaw = adjustedYaw / horizontalFov;
		const float absolutePitch = (float)fabs( (double)pitch );
		const float pitchFraction = absolutePitch * 0.011111111f;
		g_vMotionBlurValues[0] = ( 1.0f - pitchFraction ) * undampenedYaw;
		const double absoluteForwardZ = fabs( (double)forward.z );
		const float pitchMask = (float)( 1.0 - ( 1.0 - absoluteForwardZ ) * ( 1.0 - absoluteForwardZ ) );
		const float originalPitch = previousPitch - pitch;
		const float compensation = ( viewDot * 0.5f ) * pitchMask;
		float adjustedPitch = pitch > 0.0f ? originalPitch - compensation : originalPitch + compensation;
		adjustedPitch = originalPitch < 0.0f ? ClampTerrorMotionBlur( adjustedPitch, originalPitch, 0.0f ) :
			ClampTerrorMotionBlur( adjustedPitch, 0.0f, originalPitch );
		g_vMotionBlurValues[1] = adjustedPitch / verticalFov;
		g_vMotionBlurValues[3] = ( ( pitchFraction * pitchFraction ) * pitchFraction ) * undampenedYaw;
		const float falling = elapsed > 0.0f ? g_vMotionBlurValues[2] / ( elapsed * 30.0f ) : 0.0f;
		// C1C87CA..FC compares the unspilled x87 quotient against one, but
		// clamps its separate float snapshot against zero.
		const double fallingRatio = ( fabs( (double)falling ) - (double)fallingMin ) /
			( (double)fallingMax - (double)fallingMin );
		float fallingSnapshot = (float)fallingRatio;
		if ( fallingRatio > 1.0 ) fallingSnapshot = 1.0f;
		else if ( 0.0f > fallingSnapshot ) fallingSnapshot = 0.0f;
		g_vMotionBlurValues[2] = ( ( falling >= 0.0f ? 1.0f : -1.0f ) * fallingSnapshot ) * 0.033333335f;
		g_vMotionBlurValues[0] = ( strength * rotationIntensity ) * g_vMotionBlurValues[0];
		g_vMotionBlurValues[1] = ( strength * rotationIntensity ) * g_vMotionBlurValues[1];
		g_vMotionBlurValues[2] = ( strength * fallingIntensity ) * g_vMotionBlurValues[2];
		g_vMotionBlurValues[3] = ( strength * 0.3f ) * g_vMotionBlurValues[3];
		const float fps = elapsed > 0.0f ? 1.0f / elapsed : 0.0f;
		const float dampen = ClampTerrorMotionBlur( ( fps - 30.0f ) * 0.05f, 0.0f, 1.0f );
		for ( int i = 0; i < 4; ++i ) g_vMotionBlurValues[i] = dampen * g_vMotionBlurValues[i];
	}
	if ( noRotationUntil > gpGlobals->realtime )
	{
		g_vMotionBlurValues[0] = g_vMotionBlurValues[1] = g_vMotionBlurValues[3] = 0.0f;
	}
	else noRotationUntil = 0.0f;
	previousPosition = viewBlur.origin;
	previousPitch = pitch;
	previousYaw = yaw;
	lastTime = gpGlobals->realtime;

	ITexture *source = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
	const int sourceWidth = source->GetActualWidth(), sourceHeight = source->GetActualHeight();
	const float lastX = (float)sourceWidth - 1.0f, lastY = (float)sourceHeight - 1.0f;
	g_vTerrorMotionBlurViewport[0] = (float)( x + ( x > 0 ) ) / lastX;
	g_vTerrorMotionBlurViewport[1] = (float)( y + ( y > 0 ) ) / lastY;
	g_vTerrorMotionBlurViewport[2] = (float)( y + h + ( lastY > (float)y ? -1 : 0 ) ) / lastY;
	g_vTerrorMotionBlurViewport[3] = (float)( x + w + ( lastX > (float)x ? -1 : 0 ) ) / lastX;
	for ( int i = 0; i < 4; ++i )
	{
		if ( g_vTerrorMotionBlurViewport[i] <= 0.0f ) g_vTerrorMotionBlurViewport[i] = -1.0f;
		else if ( g_vTerrorMotionBlurViewport[i] >= 1.0f ) g_vTerrorMotionBlurViewport[i] = 2.0f;
	}
	CMatRenderContextPtr context( materials );
	int dummy, width, height;
	context->GetViewport( dummy, dummy, width, height );
	UpdateScreenEffectTexture( 0, x, y, w, h, false );
	IMaterial *material = g_TerrorPostMaterials.GetMotionBlur();
	if ( material )
	{
		context->DrawScreenSpaceRectangle( material, 0, 0, width, height, (float)x, (float)y,
			(float)( x + w - 1 ), (float)( y + h - 1 ), sourceWidth, sourceHeight, GetClientWorldEntity()->GetClientRenderable() );
		if ( g_bDumpRenderTargets ) DumpTGAofRenderTarget( width, height, "MotionBlur" );
	}
}
#pragma float_control( pop )
