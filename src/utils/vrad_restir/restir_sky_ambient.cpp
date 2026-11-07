//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 2D skybox / light_environment ambient brightness match.
//
// A surface that sees the whole upper hemisphere receives `ambient = _ambient intensity / 255` in the engine's linear
// light units (restir_lightmap.glsl returns the same value for a ray that escapes to the sky), and VRAD weights the sky
// by cosine (lightmap.cpp GatherSampleAmbientSkySSE). The skybox is drawn unlit from its face textures, so the sky the
// ambient stands for is the uniform dome of radiance L with the same cosine-weighted integral:
//
//     L = (1 / pi) * integral over z > 0 of Y( sky( w ) ) * w.z dw
//
// with Y the Rec.709 luminance of the colour the sky shader writes (linear, before the tonemap scale that lit surfaces
// share). The stored factor ambient Y / L multiplies the sky in the client, so the sky has the ambient's brightness and
// keeps its hue.
//
// The sky is sampled the way the engine draws it: engine/gl_warp.cpp's face layout (st_to_vec, skytexorder, texcoord
// clamp), the face VMT's $basetexturetransform and the texture's clamp/wrap flags, and the Sky / Sky_DX9 / Sky_HDR_DX9
// shaders' own decoding: LDR $basetexture (sRGB) * $color; HDR $hdrcompressedtexture (RGBS: rgb * a, linear) * 8 * $color,
// else HDR $hdrbasetexture (sRGB) * $color. Skies drawn by UnlitGeneric are not matched: that shader applies $color
// through the engine's 8-bit GammaToLinear table, which cannot carry an exact scale.
//
//=============================================================================//

#include "restir_sky_ambient.h"
#include "bsplib.h"
#include "cmdlib.h"
#include "filesystem.h"
#include "filesystem_tools.h"
#include "bitmap/imageformat.h"
#include "mathlib/vmatrix.h"
#include "tier1/KeyValues.h"
#include "tier1/utlbuffer.h"
#include "vtf/vtf.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
const int FACE_GRID = 1024;		// samples per face edge

// engine/gl_warp.cpp: the world axes (1 = s, 2 = t, 3 = 1; negative = negated) of each face's ( s, t, 1 ), and the
// suffix of the material it draws (skyboxsuffix[skytexorder[face]]).
const int s_StToVec[6][3] = { { 3, -1, 2 }, { -3, 1, 2 }, { 1, 3, 2 }, { -1, -3, 2 }, { -2, -1, 3 }, { 2, -1, -3 } };
const char *const s_FaceSuffix[6] = { "rt", "lf", "bk", "ft", "up", "dn" };
const int DOWN_FACE = 5;		// z = -1 on the whole face: never reaches the upper hemisphere

enum SkyDecode_t { SKY_DECODE_SRGB, SKY_DECODE_RGBS };

struct SkyFace
{
	int					width, height;
	bool				clampS, clampT;
	float				transform[2][3];	// $basetexturetransform rows 0 and 1 applied to ( u, v, 0, 1 ): columns 0, 1, 3
	CUtlVector<float>	luminance;			// per texel, of the colour the sky shader writes

	static int Texel( float coord, int size, bool clamped )
	{
		const int texel = (int)floorf( coord * size );
		return clamped ? clamp( texel, 0, size - 1 ) : ( texel % size + size ) % size;
	}

	float Sample( float u, float v ) const
	{
		const float s = transform[0][0] * u + transform[0][1] * v + transform[0][2];
		const float t = transform[1][0] * u + transform[1][1] * v + transform[1][2];
		return luminance[Texel( t, height, clampT ) * width + Texel( s, width, clampS )];
	}
};

const char *ModeName( bool hdr )
{
	return hdr ? "hdr" : "ldr";
}

float Luminance( float r, float g, float b )
{
	return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

bool Skip( bool hdr, const char *path, const char *why )
{
	Warning( "Sky ambient match (%s): %s: %s; no _skyscale_%s written\n", ModeName( hdr ), path, why, ModeName( hdr ) );
	return false;
}

bool IsHighPrecision( ImageFormat format )
{
	return format == IMAGE_FORMAT_RGBA16161616F || format == IMAGE_FORMAT_RGBA16161616 || format == IMAGE_FORMAT_R32F ||
		format == IMAGE_FORMAT_RGB323232F || format == IMAGE_FORMAT_RGBA32323232F;
}

// materialsystem/cmaterial.cpp: "[r g b]" linear floats, "{r g b}" 0..255 bytes.
bool ParseColor( const char *text, Vector &color )
{
	while ( *text == ' ' || *text == '\t' )
		++text;
	if ( *text != '[' && *text != '{' )
		return false;
	const float scale = *text == '{' ? 1.0f / 255.0f : 1.0f;
	++text;
	for ( int i = 0; i < 3; ++i )
	{
		char *end;
		color[i] = (float)strtod( text, &end ) * scale;
		if ( end == text )
			return false;
		text = end;
	}
	return true;
}

// materialsystem/cmaterial.cpp: the two matrix var forms. A string that parses as neither leaves the shader's
// identity transform.
bool ParseTransform( const char *text, VMatrix &mat )
{
	if ( sscanf( text, " [ %f %f %f %f  %f %f %f %f  %f %f %f %f  %f %f %f %f ]",
			&mat.m[0][0], &mat.m[0][1], &mat.m[0][2], &mat.m[0][3],
			&mat.m[1][0], &mat.m[1][1], &mat.m[1][2], &mat.m[1][3],
			&mat.m[2][0], &mat.m[2][1], &mat.m[2][2], &mat.m[2][3],
			&mat.m[3][0], &mat.m[3][1], &mat.m[3][2], &mat.m[3][3] ) == 16 )
		return true;

	Vector2D center, scale, translation;
	float angle;
	if ( sscanf( text, " center %f %f scale %f %f rotate %f translate %f %f",
			&center.x, &center.y, &scale.x, &scale.y, &angle, &translation.x, &translation.y ) != 7 )
		return false;
	VMatrix temp;
	MatrixBuildTranslation( mat, -center.x, -center.y, 0.0f );
	MatrixBuildScale( temp, scale.x, scale.y, 1.0f );
	MatrixMultiply( temp, mat, mat );
	MatrixBuildRotateZ( temp, angle );
	MatrixMultiply( temp, mat, mat );
	MatrixBuildTranslation( temp, center.x + translation.x, center.y + translation.y, 0.0f );
	MatrixMultiply( temp, mat, mat );
	return true;
}

// Selects the texture and modulation the face's shader uses in this mode and builds its luminance image.
bool BuildFace( bool hdr, const char *path, KeyValues &vmt, SkyFace &out )
{
	const char *shader = vmt.GetName();
	if ( Q_stricmp( shader, "Sky" ) && Q_stricmp( shader, "Sky_DX9" ) && Q_stricmp( shader, "Sky_HDR_DX9" ) )
		return Skip( hdr, path, "shader is not Sky (UnlitGeneric skies cannot be scaled exactly through $color)" );

	Vector modulation( 1.0f, 1.0f, 1.0f );
	if ( vmt.FindKey( "$color" ) && !ParseColor( vmt.GetString( "$color" ), modulation ) )
		return Skip( hdr, path, "$color is not a vector" );

	const char *textureKey = "$basetexture";
	SkyDecode_t decode = SKY_DECODE_SRGB;
	if ( hdr )
	{
		// sky_hdr_dx12.cpp: compressed RGBS first (mat_use_compressed_hdr_textures defaults to 1), then $hdrbasetexture.
		if ( vmt.GetString( "$hdrcompressedtexture", "" )[0] )
		{
			textureKey = "$hdrcompressedtexture";
			decode = SKY_DECODE_RGBS;
			modulation *= 8.0f;
		}
		else if ( vmt.GetString( "$hdrcompressedtexture0", "" )[0] )
			return Skip( hdr, path, "$hdrcompressedtexture0 (compression method B) is not drawn by the DX12 sky shader" );
		else
			textureKey = "$hdrbasetexture";
	}

	const char *texture = vmt.GetString( textureKey, "" );
	if ( !texture[0] )
		return Skip( hdr, path, "no sky texture for this mode" );
	char vtfPath[MAX_PATH];
	Q_snprintf( vtfPath, sizeof( vtfPath ), "materials/%s.vtf", texture );
	Q_FixSlashes( vtfPath, CORRECT_PATH_SEPARATOR );

	CUtlBuffer buffer;
	IVTFTexture *vtf = CreateVTFTexture();
	if ( !g_pFullFileSystem->ReadFile( vtfPath, NULL, buffer ) || !vtf->Unserialize( buffer ) )
	{
		DestroyVTFTexture( vtf );
		return Skip( hdr, path, "texture cannot be read" );
	}
	const ImageFormat format = vtf->Format();
	CUtlVector<unsigned char> rgba;
	out.width = vtf->Width();
	out.height = vtf->Height();
	rgba.SetCount( out.width * out.height * 4 );
	const bool decoded = !IsHighPrecision( format ) &&
		ImageLoader::ConvertImageFormat( vtf->ImageData( 0, 0, 0 ), format, rgba.Base(), IMAGE_FORMAT_RGBA8888, out.width, out.height, 0, 0 );
	out.clampS = ( vtf->Flags() & TEXTUREFLAGS_CLAMPS ) != 0;
	out.clampT = ( vtf->Flags() & TEXTUREFLAGS_CLAMPT ) != 0;
	DestroyVTFTexture( vtf );
	if ( !decoded )
		return Skip( hdr, path, "texture is not a decodable 8-bit format" );

	float linear[256];
	for ( int i = 0; i < 256; ++i )
		linear[i] = decode == SKY_DECODE_SRGB ? SrgbGammaToLinear( i / 255.0f ) : i / 255.0f;
	out.luminance.SetCount( out.width * out.height );
	for ( int i = 0; i < out.luminance.Count(); ++i )
	{
		const unsigned char *texel = &rgba[i * 4];
		const float alpha = decode == SKY_DECODE_RGBS ? texel[3] / 255.0f : 1.0f;
		out.luminance[i] = Luminance( linear[texel[0]] * modulation.x, linear[texel[1]] * modulation.y, linear[texel[2]] * modulation.z ) * alpha;
	}

	VMatrix transform;
	MatrixSetIdentity( transform );
	const char *transformText = vmt.GetString( "$basetexturetransform", "" );
	if ( transformText[0] && !ParseTransform( transformText, transform ) )
		MatrixSetIdentity( transform );
	for ( int row = 0; row < 2; ++row )
	{
		out.transform[row][0] = transform.m[row][0];
		out.transform[row][1] = transform.m[row][1];
		out.transform[row][2] = transform.m[row][3];
	}
	return true;
}

bool LoadFace( bool hdr, const char *skyName, int face, SkyFace &out )
{
	char path[MAX_PATH];
	Q_snprintf( path, sizeof( path ), "materials/skybox/%s%s.vmt", skyName, s_FaceSuffix[face] );
	Q_FixSlashes( path, CORRECT_PATH_SEPARATOR );

	KeyValues *vmt = new KeyValues( "vmt" );
	CUtlBuffer buffer( 0, 0, CUtlBuffer::TEXT_BUFFER );
	bool built = false;
	if ( !g_pFullFileSystem->ReadFile( path, NULL, buffer ) || !vmt->LoadFromBuffer( path, buffer ) )
		Skip( hdr, path, "material cannot be read" );
	else if ( !Q_stricmp( vmt->GetName(), "patch" ) )
		Skip( hdr, path, "patch materials are not supported" );
	else
		built = BuildFace( hdr, path, *vmt, out );
	vmt->deleteThis();
	return built;
}

// The cosine-weighted mean sky luminance over the upper hemisphere. Sample ( s, t ) of a cube face subtends
// ds dt / r^3 with r^2 = 1 + s^2 + t^2 and has cosine z / r, so the weight is z / r^4.
double SkyLuminance( const SkyFace faces[6] )
{
	double sum = 0.0, weight = 0.0;
	for ( int face = 0; face < 6; ++face )
	{
		if ( face == DOWN_FACE )
			continue;
		for ( int j = 0; j < FACE_GRID; ++j )
		{
			const float t = ( j + 0.5f ) / FACE_GRID * 2.0f - 1.0f;
			for ( int i = 0; i < FACE_GRID; ++i )
			{
				const float s = ( i + 0.5f ) / FACE_GRID * 2.0f - 1.0f;
				const float axes[3] = { s, t, 1.0f };
				const int zAxis = s_StToVec[face][2];
				const float z = zAxis < 0 ? -axes[-zAxis - 1] : axes[zAxis - 1];
				if ( z <= 0.0f )
					continue;
				const float r2 = 1.0f + s * s + t * t;
				const double w = z / ( r2 * r2 );
				// engine/gl_warp.cpp MakeSkyVec: texcoords stay half a texel of a 512 face off the seam; t is flipped.
				const float u = clamp( ( s + 1.0f ) * 0.5f, 1.0f / 512.0f, 511.0f / 512.0f );
				const float v = 1.0f - clamp( ( t + 1.0f ) * 0.5f, 1.0f / 512.0f, 511.0f / 512.0f );
				sum += w * faces[face].Sample( u, v );
				weight += w;
			}
		}
	}
	return sum / weight;
}

bool ComputeScale( const ReSTIROptions &options, const ReSTIRScene &scene, entity_t *world, float &scale )
{
	const char *mode = ModeName( options.hdr );
	if ( scene.skyAmbientLight < 0 )
	{
		Msg( "VRAD ReSTIR: sky ambient match (%s): no light_environment; no _skyscale_%s written\n", mode, mode );
		return false;
	}
	const char *skyName = ValueForKey( world, "skyname" );
	if ( !skyName[0] )
	{
		Msg( "VRAD ReSTIR: sky ambient match (%s): worldspawn has no skyname; no _skyscale_%s written\n", mode, mode );
		return false;
	}

	SkyFace faces[6];
	for ( int face = 0; face < 6; ++face )
	{
		if ( face != DOWN_FACE && !LoadFace( options.hdr, skyName, face, faces[face] ) )
			return false;
	}

	const float *intensity = scene.lights[scene.skyAmbientLight].intensity;
	const double ambient = Luminance( intensity[0], intensity[1], intensity[2] ) / 255.0;
	const double sky = SkyLuminance( faces );
	if ( ambient <= 0.0 || sky <= 0.0 )
	{
		Msg( "VRAD ReSTIR: sky ambient match (%s): ambient luminance %.5f, sky luminance %.5f; no _skyscale_%s written\n", mode, ambient, sky, mode );
		return false;
	}
	scale = (float)( ambient / sky );
	Msg( "VRAD ReSTIR: sky ambient match (%s): ambient luminance %.5f, sky luminance %.5f, _skyscale_%s %.6g\n", mode, ambient, sky, mode, scale );
	return true;
}

bool RemoveKey( entity_t *entity, const char *key )
{
	for ( epair_t **link = &entity->epairs; *link; link = &( *link )->next )
	{
		if ( Q_stricmp( ( *link )->key, key ) )
			continue;
		epair_t *removed = *link;
		*link = removed->next;
		free( removed->key );
		free( removed->value );
		free( removed );
		return true;
	}
	return false;
}
}

void ReSTIR_WriteSkyAmbientMatch( const ReSTIROptions &options, const ReSTIRScene &scene )
{
	if ( num_entities <= 0 )
		return;
	entity_t *world = &entities[0];
	char key[32];
	V_snprintf( key, sizeof( key ), "_skyscale_%s", ModeName( options.hdr ) );

	bool changed = RemoveKey( world, key );
	float scale;
	if ( ComputeScale( options, scene, world, scale ) )
	{
		char value[32];
		V_snprintf( value, sizeof( value ), "%.6g", scale );
		SetKeyValue( world, key, value );
		changed = true;
	}
	if ( changed )
		UnparseEntities();
}
