// Production renderer receiver interpolation only; no BSP loading or GPU.
#include "restir_baked_receivers.h"
#include "restir_byte_buffer.h"
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <string>

static bool ReadVector( Vector &v ) { return bool( std::cin >> v.x >> v.y >> v.z ); }
static void PrintVector( const Vector &v ) { std::cout << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
int main( int argc, char **argv )
{
	if ( argc != 2 ) return 2;
	std::cout << std::setprecision(9);
	const std::string operation( argv[1] );
	if ( operation == "disp" )
	{
		int power, indices[3]; Vector2D uv; float weights[3];
		if ( !(std::cin >> power >> uv.x >> uv.y) || power < 2 || power > 4 || uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1 ) return 2;
		RendererDispVertices( power, uv, indices, weights );
		std::cout << "{\"indices\":[" << indices[0] << ',' << indices[1] << ',' << indices[2] << "],\"weights\":["
			<< weights[0] << ',' << weights[1] << ',' << weights[2] << "]}\n";
	}
	else if ( operation == "frame" )
	{
		float weights[3]; ReSTIRRendererVertex vertices[3];
		if ( !(std::cin >> weights[0] >> weights[1] >> weights[2]) ) return 2;
		for ( int i=0; i<3; ++i )
			if ( !ReadVector(vertices[i].position) || !ReadVector(vertices[i].normal) || !ReadVector(vertices[i].s) || !ReadVector(vertices[i].t) ) return 2;
		Vector position, normal, bump[3]; RendererInterpolateFrame( vertices, weights, position, normal, bump );
		std::cout << "{\"position\":"; PrintVector(position); std::cout << ",\"N\":"; PrintVector(normal);
		std::cout << ",\"bases\":[";
		for (int i=0; i<3; ++i) { if(i) std::cout << ','; PrintVector(bump[i]); }
		std::cout << "]}\n";
	}
	else if ( operation == "brush" || operation == "brushframe" )
	{
		int vertexCount, triangleCount; Vector position;
		if ( !(std::cin >> vertexCount >> triangleCount) || vertexCount < 3 || vertexCount > 256 || triangleCount < 1 || triangleCount > 256 || !ReadVector(position) ) return 2;
		CUtlVector<ReSTIRRendererVertex> vertices; vertices.SetCount(vertexCount);
		CUtlVector<ReSTIRRendererTriangle> triangles; triangles.SetCount(triangleCount);
		for(int i=0; i<vertexCount; ++i)
		{
			if(!ReadVector(vertices[i].position)) return 2;
			if(operation == "brushframe" &&
				(!ReadVector(vertices[i].normal) || !ReadVector(vertices[i].s) || !ReadVector(vertices[i].t))) return 2;
		}
		for(int i=0; i<triangleCount; ++i) for(int c=0; c<3; ++c)
			if(!(std::cin >> triangles[i].v[c]) || triangles[i].v[c]<0 || triangles[i].v[c]>=vertexCount) return 2;
		int selected; float weights[3];
		if(!RendererBrushWeights(vertices,triangles,position,selected,weights)) return 3;
		std::cout << "{\"triangle\":" << selected << ",\"weights\":[" << weights[0] << ',' << weights[1] << ',' << weights[2] << "]";
		if(operation == "brushframe")
		{
			ReSTIRRendererVertex receiver[3];
			for(int c=0; c<3; ++c) receiver[c] = vertices[triangles[selected].v[c]];
			Vector directPosition, normal, bump[3]; RendererInterpolateFrame(receiver,weights,directPosition,normal,bump);
			std::cout << ",\"position\":"; PrintVector(directPosition); std::cout << ",\"N\":"; PrintVector(normal);
			std::cout << ",\"bases\":[";
			for(int p=0; p<3; ++p) { if(p) std::cout << ','; PrintVector(bump[p]); }
			std::cout << ']';
		}
		std::cout << "}\n";
	}
	else if ( operation == "bytecapacity" )
	{
		int allocated, capacity = -1;
		uint64 required, total, section;
		if (!(std::cin >> allocated >> required >> total >> section)) return 2;
		const bool fits = ReSTIR_ByteCapacity(allocated,required,capacity);
		const bool sectionFits = ReSTIR_AddByteSectionSize(total,section);
		CUtlVector<byte> small;
		if (!ReSTIR_EnsureByteCapacity(small,17)) return 3;
		small.SetCount(17); small[0] = 91;
		if (!ReSTIR_EnsureByteCapacity(small,33)) return 3;
		small.SetCount(33);
		if (small[0] != 91 || small.NumAllocated() < 33) return 3;
		std::cout << "{\"fits\":" << (fits ? "true" : "false") << ",\"capacity\":" << capacity
			<< ",\"sectionFits\":" << (sectionFits ? "true" : "false") << ",\"total\":" << total << "}\n";
	}
	else if ( operation == "largebytegrowth" )
	{
		const int required = 1181116006; // 1.1 GiB; deliberately exceeds 1-GiB capacity.
		void *allocation = malloc(size_t(required));
		if (!allocation)
		{
			std::cout << "{\"skipped\":true,\"reason\":\"large allocation unavailable\"}\n";
			return 0;
		}
		free(allocation);
		CUtlVector<uint8> bytes;
		bytes.EnsureCapacity(1 << 30);
		if (!bytes.Base())
		{
			std::cout << "{\"skipped\":true,\"reason\":\"initial allocation unavailable\"}\n";
			return 0;
		}
		bytes.AddMultipleToTail(required); // Exercises shared CUtlMemory::Grow, not owned bypass.
		if (!bytes.Base())
		{
			std::cout << "{\"skipped\":true,\"reason\":\"growth allocation unavailable\"}\n";
			return 0;
		}
		bytes[0] = 19; bytes[required-1] = 83;
		if (bytes.Count() != required || bytes.NumAllocated() < required || bytes[0] != 19 || bytes[required-1] != 83)
			return 3;
		std::cout << "{\"skipped\":false,\"required\":" << required << ",\"allocated\":" << bytes.NumAllocated()
			<< ",\"passed\":true}\n";
	}
	else return 2;
	return 0;
}
