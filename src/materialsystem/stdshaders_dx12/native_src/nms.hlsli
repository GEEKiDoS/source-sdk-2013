// Normal Mapping Shadows: cheap self-shadowing from the bumped normal field, for the sun and every local light the
// pixel evaluates (pbr_lighting.hlsli). After Boris Vorontsov's slope trace: walking from the pixel toward the light,
// the tangent-space normals give the surface slope, so integrating it rebuilds the height profile along the ray.
// Wherever that profile rises above the ray toward the light, the pixel is occluded. Everything is
// measured in world units: UV derivatives convert the world-space trace length into texture
// coordinates, so the result does not depend on texture resolution or $basetexturetransform.
#ifndef PBR_NMS_HLSLI
#define PBR_NMS_HLSLI

struct PBRNMSInput
{
	float2 uv;
	float2 uvDx;
	float2 uvDy;
	float3 positionDx;
	float3 positionDy;
	float3 tangent;
	float3 binormal;
	float3 normal;
	float3 toEye;
	float enabled;
};
#if NMS

// Floor for the normal's z when converting it to a slope, so near-vertical texels can't explode.
static const float kNMSMinimumNormalZ = 0.2;
// Height (world units) above the light ray at which a texel is fully shadowed.
static const float kNMSFullShadowDepth = 0.25;

float PBR_NMSVisibility( Texture2D normalTexture, SamplerState normalSampler, PBRNMSInput input, float3 lightWS )
{
	if ( input.enabled == 0.0 || g_PbrNMS.x == 0.0 )
		return 1.0;

	float3 L = normalize( lightWS );
	if ( dot( input.normal, L ) <= 0.0 )
		return 1.0;

	float2 lightXY = float2( dot( L, input.tangent ), dot( L, input.binormal ) );
	float lightXYLength = length( lightXY );
	// A light close to the tangent-space normal has no useful projected ray and is faded out.
	float angleFade = smoothstep( 0.04, 0.40, lightXYLength );
	if ( angleFade == 0.0 )
		return 1.0;

	// UV derivatives are tiny on screen (a texel spans well under a pixel up close), so their determinant is
	// only guarded against exact degeneracy; an absolute epsilon like 1e-6 rejected every nearby pixel.
	float determinant = input.uvDx.x * input.uvDy.y - input.uvDx.y * input.uvDy.x;
	if ( determinant == 0.0 )
		return 1.0;
	float3 positionDu = ( input.positionDx * input.uvDy.y - input.positionDy * input.uvDx.y ) / determinant;
	float3 positionDv = ( input.positionDy * input.uvDx.x - input.positionDx * input.uvDy.x ) / determinant;
	float2 rayDirection = lightXY / lightXYLength;
	float worldPerUV = length( positionDu * rayDirection.x + positionDv * rayDirection.y );
	if ( worldPerUV < 1e-4 )
		return 1.0;

	int steps = clamp( int( g_PbrNMS.z + 0.5 ), 1, 16 );
	float shadowLength = max( g_PbrNMS.y, 0.0 );
	float stepWorld = shadowLength / float( steps );
	float2 uvStep = rayDirection * ( stepWorld / worldPerUV );
	// The ray toward the light rises this much per world unit travelled across the surface.
	float rayRise = dot( L, input.normal ) / lightXYLength;
	// Each sample stands for one step of the ray, so it reads the normal prefiltered to that step's footprint
	// (or the pixel's, if larger): point-sampling texel noise between steps turns the integral into speckle.
	uint width, height, levels;
	normalTexture.GetDimensions( 0, width, height, levels );
	float2 texels = float2( width, height );
	float footprint = max( length( uvStep * texels ), max( length( input.uvDx * texels ), length( input.uvDy * texels ) ) );
	float lod = log2( max( footprint, 1.0 ) );
	// Interleaved-gradient noise on the texel grid offsets the step positions per texel, so long traces with few
	// steps resolve to a soft edge instead of one stair per step. It is fixed in texture space, so it does not crawl.
	float2 texel = floor( input.uv * texels );
	float jitter = frac( 52.9829189 * frac( dot( texel, float2( 0.06711056, 0.00583715 ) ) ) );
	float surfaceHeight = 0.0;
	float maximumOcclusion = 0.0;
	[loop]
	for ( int step = 1; step <= 16; ++step )
	{
		if ( step > steps )
			break;
		float travelled = float( step ) - jitter;
		float2 sampleUV = input.uv + uvStep * travelled;
		float3 sampleNormal = PBR_DecodeNormal( normalTexture.SampleLevel( normalSampler, sampleUV, lod ) );
		// A tangent-space normal (nx, ny, nz) is the gradient -dh/du, -dh/dv scaled by nz: the
		// height changes by -dot(n.xy, dir) / nz per unit distance along the ray.
		surfaceHeight -= dot( sampleNormal.xy, rayDirection ) / max( sampleNormal.z, kNMSMinimumNormalZ ) * stepWorld;
		maximumOcclusion = max( maximumOcclusion, surfaceHeight - rayRise * stepWorld * travelled );
	}

	// Occlusion depth in world units maps to shadow hardness: a rise of kNMSFullShadowDepth above
	// the ray is fully shadowed, shallower rises give a soft penumbra instead of binary noise.
	float hardness = saturate( maximumOcclusion / kNMSFullShadowDepth );
	float distanceFade = 1.0 - smoothstep( 2048.0, 8192.0, length( input.toEye ) );
	return lerp( 1.0, 1.0 - hardness, angleFade * distanceFade );
}
#else
float PBR_NMSVisibility( PBRNMSInput input, float3 lightWS )
{
	return 1.0;
}
#endif

#endif
