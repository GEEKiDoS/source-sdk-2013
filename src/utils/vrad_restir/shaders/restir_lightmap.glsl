//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_LIGHTMAP_GLSL
#define RESTIR_LIGHTMAP_GLSL

vec3 SkyEnvironmentTexel( int x, int y )
{
	return skyEnvironment[y * RESTIR_SKY_ENV_WIDTH + x].rgb;
}

// Sky ambient radiance arriving from `direction` (unit, pointing at the sky) in the emit_skyambient intensity's units:
// the uniform light_environment ambient, or with -restir_skyibl the bilinear sky environment (u wraps, v clamps).
vec3 SkyAmbientRadiance( vec3 direction )
{
	if ( ( pc.flags & RESTIR_PC_SKY_ENVIRONMENT ) == 0u )
		return lights[pc.skyAmbientLight].intensity.rgb;
	float u = ( atan( direction.y, direction.x ) / ( 2.0 * RESTIR_PI ) + 0.5 ) * float( RESTIR_SKY_ENV_WIDTH ) - 0.5;
	float v = acos( clamp( direction.z, -1.0, 1.0 ) ) / RESTIR_PI * float( RESTIR_SKY_ENV_HEIGHT ) - 0.5;
	float u0 = floor( u ), v0 = floor( v );
	vec2 f = vec2( u - u0, v - v0 );
	int x0 = ( int( u0 ) + RESTIR_SKY_ENV_WIDTH ) % RESTIR_SKY_ENV_WIDTH;
	int x1 = ( x0 + 1 ) % RESTIR_SKY_ENV_WIDTH;
	int y0 = clamp( int( v0 ), 0, RESTIR_SKY_ENV_HEIGHT - 1 );
	int y1 = clamp( int( v0 ) + 1, 0, RESTIR_SKY_ENV_HEIGHT - 1 );
	return mix( mix( SkyEnvironmentTexel( x0, y0 ), SkyEnvironmentTexel( x1, y0 ), f.x ),
		mix( SkyEnvironmentTexel( x0, y1 ), SkyEnvironmentTexel( x1, y1 ), f.x ), f.y );
}

int SceneFaceFromHit( HitInfo hit )
{
	return hit.face >= 0 ? faceNeighbors[pc.pass + uint( hit.face )] : -1;
}

// utils/vrad/vraddetailprops.cpp:470-481,493-521 CLightSurface hit lookup;
// the scene stores displacement DispUV in the triangle UV fields.
vec2 LightmapCoord( ReSTIRGpuFace face, HitInfo hit, vec3 position )
{
	if ( ( face.flags & RESTIR_FACE_DISP ) == 0 )
		return WorldToLuxelSpace( face, position );
	ReSTIRGpuTriangle triangle = triangles[hit.triangle];
	vec2 uv0 = vec2( triangle.v0.w, triangle.v1.w );
	vec2 uv1 = vec2( triangle.v2.w, triangle.uv.x );
	vec2 uv2 = triangle.uv.yz;
	vec2 uv = uv0 * ( 1.0 - hit.barycentrics.x - hit.barycentrics.y ) + uv1 * hit.barycentrics.x + uv2 * hit.barycentrics.y;
	return uv * vec2( face.luxelW - 1, face.luxelH - 1 );
}

// Albedo of a lightmapped face at a hit (-restir_texturealbedo): the base texture texel at the
// brush texture UV (texinfo axes, restir_types.h ReSTIRGpuFace::textureS/T) or, for displacements,
// the displacement UV already stored on the triangle, linearized with VBSP's 2.2 curve and
// normalized so the texture average equals the material reflectivity. Without an albedo
// texture this is exactly VRAD's per-material reflectivity.
vec3 HitAlbedo( ReSTIRGpuFace face, HitInfo hit, vec3 position )
{
	ReSTIRGpuMaterial material = materials[face.material];
	if ( material.albedoTexture < 0 )
		return face.reflectivity.rgb;
	vec2 uv;
	if ( ( face.flags & RESTIR_FACE_DISP ) != 0 )
	{
		ReSTIRGpuTriangle triangle = triangles[hit.triangle];
		vec2 uv0 = vec2( triangle.v0.w, triangle.v1.w );
		vec2 uv1 = vec2( triangle.v2.w, triangle.uv.x );
		vec2 uv2 = triangle.uv.yz;
		uv = uv0 * ( 1.0 - hit.barycentrics.x - hit.barycentrics.y ) + uv1 * hit.barycentrics.x + uv2 * hit.barycentrics.y;
	}
	else
	{
		uv = vec2( dot( position, face.textureS.xyz ) + face.textureS.w, dot( position, face.textureT.xyz ) + face.textureT.w ) /
			vec2( material.textureWidth, material.textureHeight );
	}
	ivec2 size = textureSize( sceneTextures[nonuniformEXT( material.albedoTexture )], 0 );
	ivec2 texel = ivec2( floor( fract( uv ) * vec2( size ) ) );
	vec3 gamma = texelFetch( sceneTextures[nonuniformEXT( material.albedoTexture )], texel, 0 ).rgb;
	return min( pow( gamma, vec3( 2.2 ) ) * material.albedoScale.rgb, vec3( 0.99 ) );
}

// Same arithmetic mean of valid final base-plane luxels, cached once per face/style
// by UploadFinalLightmap. In particular, use full transport, not receiver-only RGB.
vec3 FaceAverage( int faceIndex, uint slot )
{
	return finalLightmap[uint( faceIndex ) * RESTIR_MAX_FACE_STYLES + slot].rgb;
}

// utils/vrad/vraddetailprops.cpp:317-351, point sample (truncate, clamp), base
// bump channel only; style stride includes all bump channels.
vec3 LightmapPointSample( ReSTIRGpuFace face, uint slot, HitInfo hit, vec3 position, out bool valid )
{
	ivec2 coord = clamp( ivec2( LightmapCoord( face, hit, position ) ), ivec2( 0 ), ivec2( face.luxelW - 1, face.luxelH - 1 ) );
	int luxel = coord.x + coord.y * face.luxelW;
	valid = luxelValid[face.firstLuxel + luxel] != 0u;
	return finalLightmap[pc.numFaces * RESTIR_MAX_FACE_STYLES + uint( face.firstOutput ) + slot * uint( face.numChannels * face.luxelW * face.luxelH ) + uint( luxel )].rgb;
}

// utils/vrad/vraddetailprops.cpp:239-295,579-617 CalcRayAmbientLighting.
// :437 backface exclusion is handled by TraceGatherRay (not generic rays).
vec3 RayAmbientColor( HitInfo hit, vec3 origin, vec3 direction, int style )
{
	if ( ( hit.flags & RESTIR_TRI_SKY ) != 0u )
		return style == 0 && pc.skyAmbientLight >= 0 ? SkyAmbientRadiance( direction ) / 255.0 : vec3( 0.0 );
	int faceIndex = SceneFaceFromHit( hit );
	if ( faceIndex < 0 )
		return vec3( 0.0 );
	ReSTIRGpuFace face = faces[faceIndex];
	int slot = FindStyle( face, style );
	if ( slot < 0 )
		return vec3( 0.0 );
	vec3 position = origin + direction * hit.t;
	float coneRadius = length( direction ) * hit.t * tan( radians( 7.275 ) );
	float averageWeight = clamp( ( coneRadius - 20.0 ) / 20.0, 0.0, 1.0 );
	bool valid;
	vec3 pointColor = LightmapPointSample( face, uint( slot ), hit, position, valid );
	if ( !valid )
		averageWeight = 1.0;
	vec3 color = averageWeight > 0.0 ? mix( pointColor, FaceAverage( faceIndex, uint( slot ) ), averageWeight ) : pointColor;
	return color * HitAlbedo( face, hit, position ) / 255.0;
}

// utils/vrad/vraddetailprops.cpp:658-750 ComputeIndirectLightingAtPoint.
// The source sums lightmapColor WITHOUT an additional dot multiplication;
// dot only gates rays and normalizes the final sum at :746-748.
vec3 StaticPropIndirect( vec3 origin, vec3 normal, bool ignoreNormals )
{
	vec3 sum = vec3( 0.0 );
	float totalDot = 0.0;
	for ( uint sampleIndex = 0u; sampleIndex < 40u; ++sampleIndex )
	{
		vec3 direction = DirectionalSample( sampleIndex );
		float cosine = ignoreNormals ? 0.7071 / 2.0 : dot( normal, direction );
		if ( cosine <= RESTIR_EQUAL_EPSILON )
			continue;
		totalDot += cosine;
		HitInfo hit;
		if ( !TraceGatherRay( origin, direction, RESTIR_DIST_EPSILON, RESTIR_TRACE_LENGTH, hit ) || ( hit.flags & RESTIR_TRI_SKY ) != 0u )
			continue;
		int faceIndex = SceneFaceFromHit( hit );
		if ( faceIndex < 0 )
			continue;
		ReSTIRGpuFace face = faces[faceIndex];
		int slot = FindStyle( face, 0 );
		if ( slot < 0 )
			continue;
		bool valid;
		vec3 color = LightmapPointSample( face, uint( slot ), hit, origin + direction * hit.t, valid );
		if ( !valid )
			color = FaceAverage( faceIndex, uint( slot ) );
		float falloff = 1.0 / ( 1.0 + ( hit.t / 128.0 ) * ( hit.t / 128.0 ) );
		sum += color * HitAlbedo( face, hit, origin + direction * hit.t ) * falloff;
	}
	return totalDot > 0.0 ? sum / totalDot : vec3( 0.0 );
}
#endif
