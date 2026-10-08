// Standalone CPU analytic probe of production direct math and R8 decode.
// It does not invoke a bake, renderer, or production hlight page quantizer.
#include "restir_baked_direct.h"
#include "hlight_bsp.h"
#include <iostream>
#include <iomanip>
#include <map>
#include <vector>

static bool ReadVector( Vector &v )
{
	return bool( std::cin >> v.x >> v.y >> v.z );
}
static void PrintVector( const Vector &v )
{
	std::cout << '[' << v.x << ',' << v.y << ',' << v.z << ']';
}
struct StylePlanes
{
	Vector planes[4];
	float scale;
	StylePlanes() : scale( 1.0f ) { for ( int p = 0; p < 4; ++p ) planes[p].Init(); }
};
int main()
{
	Vector position, normal, flatNormal, textureS, textureT, weights;
	if ( !ReadVector( position ) || !ReadVector( normal ) || !ReadVector( flatNormal ) ||
		!ReadVector( textureS ) || !ReadVector( textureT ) || !ReadVector( weights ) ) return 2;
	ReSTIRGpuFace face = {};
	for ( int c = 0; c < 3; ++c )
	{
		face.faceNormal[c] = flatNormal[c];
		face.textureS[c] = textureS[c];
		face.textureT[c] = textureT[c];
	}
	Vector S, T, bases[3];
	ReSTIR_BakedTangentFrame( face, normal, S, T );
	ReSTIR_BakedBumpNormals( face, normal, bases );
	unsigned int count;
	if ( !( std::cin >> count ) || count > 256 ) return 2;
	std::map<int, StylePlanes> styles;
	std::cout << std::setprecision( 9 ) << "{\"S\":"; PrintVector( S );
	std::cout << ",\"T\":"; PrintVector( T );
	std::cout << ",\"bases\":[";
	for ( int p = 0; p < 3; ++p ) { if ( p ) std::cout << ','; PrintVector( bases[p] ); }
	std::cout << "],\"lights\":[";
	for ( unsigned int i = 0; i < count; ++i )
	{
		ShadowMapLightDisk disk = {};
		int type, style, encoding, byte;
		float scale;
		if ( !( std::cin >> type >> style >> encoding >> byte >> scale ) ||
			!ReadVector( disk.light.origin ) || !ReadVector( disk.light.intensity ) ||
			!ReadVector( disk.light.normal ) ||
			!( std::cin >> disk.light.constant_attn >> disk.light.linear_attn >> disk.light.quadratic_attn >>
				disk.light.radius >> disk.capDist >> disk.light.stopdot >> disk.light.stopdot2 >>
				disk.light.exponent >> disk.startFade >> disk.endFade ) ||
			style < 0 || style > 254 || byte < 0 || byte > 255 ||
			( encoding != int( hlight::kVisibilityDense ) && ( encoding < 0 || encoding > 255 ) ) ) return 2;
		disk.light.type = static_cast<emittype_t>( type );
		disk.light.style = style;
		const uint8 payload = uint8( byte );
		hlight::VisibilityView view = {};
		view.payload = &payload;
		hlight::VisibilityEntryDisk entry = {};
		entry.encoding = uint32( encoding );
		entry.sampleCount = encoding == int( hlight::kVisibilityDense ) ? 1 : 0;
		const float visibility = hlight::VisibilitySample( view, entry, 0 );
		Vector L;
		const Vector radiance = ReSTIR_BakedLocalRadiance( disk, position, L );
		float angular[4];
		angular[0] = ReSTIR_BakedLocalAngular( L, normal );
		for ( int p = 0; p < 3; ++p ) angular[p+1] = ReSTIR_BakedLocalAngular( L, bases[p] );
		StylePlanes &sum = styles[style];
		sum.scale = scale;
		for ( int p = 0; p < 4; ++p ) sum.planes[p] += radiance * ( visibility * angular[p] );
		if ( i ) std::cout << ',';
		std::cout << "{\"radiance\":"; PrintVector( radiance );
		std::cout << ",\"L\":"; PrintVector( L );
		std::cout << ",\"visibility\":" << visibility << ",\"angular\":[";
		for ( int p = 0; p < 4; ++p ) { if ( p ) std::cout << ','; std::cout << angular[p]; }
		std::cout << "]}";
	}
	Vector plain( 0, 0, 0 ), bumped( 0, 0, 0 );
	std::cout << "],\"styles\":[";
	bool first = true;
	for ( std::map<int, StylePlanes>::const_iterator i = styles.begin(); i != styles.end(); ++i )
	{
		if ( !first ) std::cout << ',';
		first = false;
		std::cout << "{\"style\":" << i->first << ",\"planes\":[";
		for ( int p = 0; p < 4; ++p ) { if ( p ) std::cout << ','; PrintVector( i->second.planes[p] ); }
		std::cout << "]}";
		plain += i->second.planes[0] * ( i->second.scale / 255.0f );
		for ( int p = 0; p < 3; ++p ) bumped += i->second.planes[p+1] * ( weights[p] * i->second.scale / 255.0f );
	}
	std::cout << "],\"plain\":"; PrintVector( plain );
	std::cout << ",\"bumped\":"; PrintVector( bumped );
	std::cout << "}\n";
	return 0;
}
