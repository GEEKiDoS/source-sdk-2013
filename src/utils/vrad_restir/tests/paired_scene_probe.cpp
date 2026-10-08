#include "../restir_paired_scene.h"
#include <stdio.h>

static int failures = 0;
static void Check( bool value, const char *name )
{
	if ( !value ) { fprintf( stderr, "FAIL %s\n", name ); ++failures; }
}
template <typename T> static void AddZero( CUtlVector<T> &values )
{
	T value;
	memset( &value, 0, sizeof( value ) );
	values.AddToTail( value );
}
int main()
{
	ReSTIRScene a, b;
	Check( ReSTIR_PairedScenesEqual( a, b ), "empty equal domains" );
	AddZero( a.lights ); AddZero( b.lights );
	b.lights[0].intensity[2] = 0.2f;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "per-light HDR scale before bounce" );
	b.lights[0].intensity[2] = 0;
	uint32 nanBits = 0x7fc00001u;
	memcpy( &a.lights[0].intensity[2], &nanBits, sizeof( nanBits ) );
	memcpy( &b.lights[0].intensity[2], &nanBits, sizeof( nanBits ) );
	Check( !ReSTIR_PairedScenesEqual( a, b ), "identical NaN is not proven equivalence" );
	a.lights[0].intensity[2] = b.lights[0].intensity[2] = 0;
	b.lights[0].lightFlags = RESTIR_LIGHT_RUNTIME_DIRECT;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "selected receiver split" );
	b.lights[0].lightFlags = 0;
	AddZero( a.triangles ); AddZero( b.triangles );
	b.triangles[0].flags = RESTIR_TRI_SHADOW;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "RAD occlusion differs despite equal lights" );
	b.triangles[0].flags = 0;
	AddZero( a.faces ); AddZero( b.faces );
	b.faces[0].numChannels = 4;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "bumped sampling domain" );
	b.faces[0].numChannels = 0;
	b.faces[0].styles[RESTIR_MAX_FACE_STYLES-1] = 63;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "full source style boundary" );
	b.faces[0].styles[RESTIR_MAX_FACE_STYLES-1] = 0;
	AddZero( a.samples );
	Check( !ReSTIR_PairedScenesEqual( a, b ), "sample count despite equal receiver grid" );
	AddZero( b.samples );
	b.samples[0].position[0] = 0.125f;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "sample position differs" );
	b.samples[0].position[0] = 0;
	AddZero( a.materials ); AddZero( b.materials );
	b.materials[0].albedoScale[0] = 0.5f;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "per-texel bounce material differs" );
	b.materials[0].albedoScale[0] = 0;
#define SCALAR_CASE( field ) \
	b.field += 1; Check( !ReSTIR_PairedScenesEqual( a, b ), #field ); b.field = a.field
	SCALAR_CASE( worldMins.x ); SCALAR_CASE( worldMaxs.z );
	SCALAR_CASE( skyAmbientLight ); SCALAR_CASE( skyLight );
	SCALAR_CASE( numOutputValues ); SCALAR_CASE( shadowSunAngularRadius );
#undef SCALAR_CASE
	b.receiverStyleMask ^= uint64( 1 ) << 63;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "top receiver style bit" );
	b.receiverStyleMask = a.receiverStyleMask;
	a.textures.AddToTail();
	a.textures[0].width = a.textures[0].height = 1; a.textures[0].channels = 4;
	for ( int i = 0; i < 4; ++i ) a.textures[0].texels.AddToTail( 128 );
	Check( !ReSTIR_PairedScenesEqual( a, b ), "texture count" );
	b.textures.AddToTail();
	b.textures[0].width = b.textures[0].height = 1; b.textures[0].channels = 4;
	for ( int i = 0; i < 4; ++i ) b.textures[0].texels.AddToTail( 128 );
	Check( ReSTIR_PairedScenesEqual( a, b ), "deep texture equality" );
	b.textures[0].texels[3] ^= 1;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "coverage alpha differs" );
	b.textures[0].texels[3] ^= 1;
	++b.textures[0].width;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "texture dimensions differ" );
	--b.textures[0].width;
	b.textures[0].channels = 1;
	Check( !ReSTIR_PairedScenesEqual( a, b ), "texture interpretation differs" );
	printf( "paired scene failures=%d\n", failures );
	return failures ? 1 : 0;
}
